/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBAARUFORMAT_CONTEXT_H
#define LIBAARUFORMAT_CONTEXT_H

#include "blake3.h"
#include "crc64.h"
#include "hash_map.h"
#include "lru.h"
#include "md5.h"
#include "sha1.h"
#include "sha256.h"
#include "spamsum.h"
#include "structs.h"
#include "structs/flux.h"
#include "utarray.h"

typedef struct FluxCaptureMapEntry FluxCaptureMapEntry;

/** \file aaruformat/context.h
 *  \brief Central runtime context structures for libaaruformat (image state, caches, checksum buffers).
 *
 *  The principal structure, \ref aaruformat_context, aggregates: header metadata, open stream handle, deduplication
 *  tables (DDT) currently in memory, optical disc auxiliary data (sector prefix/suffix/subchannel), track listings,
 *  geometry & metadata blocks, checksum accumulators, CRC & ECC helper contexts, hash map for deduplication, and
 *  transient write buffers.
 *
 *  Memory ownership model (unless otherwise stated): if a pointer field is non-NULL it is owned by the context and
 *  will be freed (or otherwise released) during context close / destruction. Callers must not free or reallocate
 *  these pointers directly. External callers should treat all internal buffers as read‑only unless explicitly writing.
 *
 *  Threading: a single context instance is NOT thread-safe; serialize access if used across threads.
 *  Lifetime: allocate, initialize/open, perform read/write/verify operations, then close/free.
 *
 *  Deduplication tables (DDT): only a subset (primary table + an active secondary + optional cache) is retained in RAM;
 *  large images may rely on lazy loading of secondary tables. Flags (inMemoryDdt, userDataDdt*, cachedSecondary*)
 *  indicate what is currently resident.
 *
 *  Optical auxiliary buffers (sectorPrefix / sectorSuffix / subchannel / corrected variants) are populated only for
 *  images where those components exist (e.g., raw CD dumps). They may be NULL for block devices / non‑optical media.
 *
 *  Index handling: indexEntries (UT_array) holds a flattened list of \ref IndexEntry structures (regardless of
 * v1/v2/v3). hash_map_t *sectorHashMap provides fast duplicate detection keyed by content fingerprint / sparse sector
 * key.
 *
 *  Invariants / sanity expectations (not strictly enforced everywhere):
 *   - magic == AARU_MAGIC after successful open/create.
 *   - header.imageMajorVersion <= AARUF_VERSION.
 *   - imageStream != NULL when any I/O method is in progress.
 *   - If deduplicate == false, sectorHashMap may still be populated for bookkeeping but duplicates are stored
 * independently.
 *   - If userDataDdtMini != NULL then userDataDdtBig == NULL (and vice versa) for a given level.
 *   - If no_user_data_ddt == true the image carries no user-data DDT (intentional, e.g. flux-only image): user_data_ddt,
 *     user_data_ddt2, cached_secondary_ddt2 and tape_ddt MUST all be NULL, and user_data_ddt_header.entries MUST be 0.
 *     Sector read/write APIs return AARUF_ERROR_USER_DATA_NOT_PRESENT in this state.
 */

#ifndef MD5_DIGEST_LENGTH
#define MD5_DIGEST_LENGTH 16
#endif

#ifndef SHA1_DIGEST_LENGTH
#define SHA1_DIGEST_LENGTH 20
#endif

#ifndef SHA256_DIGEST_LENGTH
#define SHA256_DIGEST_LENGTH 32
#endif

/** \struct CdEccContext
 *  \brief Lookup tables and state for Compact Disc EDC/ECC (P/Q) regeneration / verification.
 *
 *  Fields may be lazily allocated; inited_edc indicates tables are ready.
 */
typedef struct CdEccContext
{
    bool      inited_edc;   ///< True once EDC/ECC tables have been initialized.
    uint8_t  *ecc_b_table;  ///< Backward (B) ECC table (allocated, size implementation-defined).
    uint8_t  *ecc_f_table;  ///< Forward (F) ECC table.
    uint32_t *edc_table;    ///< EDC (CRC) lookup table.
} CdEccContext;

/** \struct Checksums
 *  \brief Collected whole‑image checksums / hashes present in a checksum block.
 *
 *  Only hash arrays with corresponding has* flags set contain valid data. spamsum is a dynamically allocated
 *  NUL‑terminated buffer (original SpamSum signature bytes followed by appended '\0').
 */
typedef struct Checksums
{
    bool     hasMd5;                        ///< True if md5[] buffer populated.
    bool     hasSha1;                       ///< True if sha1[] buffer populated.
    bool     hasSha256;                     ///< True if sha256[] buffer populated.
    bool     hasBlake3;                     ///< True if blake3[] buffer populated.
    bool     hasSpamSum;                    ///< True if spamsum pointer allocated and signature read.
    uint8_t  md5[MD5_DIGEST_LENGTH];        ///< MD5 digest (16 bytes).
    uint8_t  sha1[SHA1_DIGEST_LENGTH];      ///< SHA-1 digest (20 bytes).
    uint8_t  sha256[SHA256_DIGEST_LENGTH];  ///< SHA-256 digest (32 bytes).
    uint8_t  blake3[BLAKE3_OUT_LEN];        ///< BLAKE3 digest (32 bytes).
    uint8_t *spamsum;                       ///< SpamSum fuzzy hash (ASCII), allocated length+1 with trailing 0.
} Checksums;

/** \struct mediaTagEntry
 *  \brief Hash table entry for an arbitrary media tag (e.g., proprietary drive/medium descriptor).
 *
 *  Stored via uthash (hh handle). Type is a format‑specific integer identifier mapping to external interpretation.
 */
typedef struct mediaTagEntry
{
    uint8_t       *data;    ///< Tag data blob (opaque to library core); length bytes long.
    int32_t        type;    ///< Numeric type identifier.
    uint32_t       length;  ///< Length in bytes of data.
    UT_hash_handle hh;      ///< uthash linkage.
} mediaTagEntry;

typedef struct TapeFileHashEntry
{
    uint64_t       key;        ///< Composite key: partition << 32 | file
    TapeFileEntry  fileEntry;  ///< The actual tape file data
    UT_hash_handle hh;         ///< UTHASH handle
} tapeFileHashEntry;

typedef struct TapePartitionHashEntry
{
    uint8_t            key;             ///< Key: partition
    TapePartitionEntry partitionEntry;  ///< The actual tape partition data
    UT_hash_handle     hh;              ///< UTHASH handle
} TapePartitionHashEntry;

typedef struct TapeDdtHashEntry
{
    uint64_t       key;    ///< Key: sector address
    uint64_t       value;  ///< Value: DDT entry
    UT_hash_handle hh;     ///< UTHASH handle
} TapeDdtHashEntry;

/** \struct aaruformat_context
 *  \brief Master context representing an open or in‑creation Aaru image.
 *
 *  Contains stream handle, parsed headers, deduplication structures, optical extras, metadata blocks, checksum
 *  information, caches, and write-state. Allocate with library factory (or zero‑init + explicit open) and destroy
 *  with corresponding close/free routine.
 *
 *  Field grouping:
 *   - Core & header: magic, library*Version, imageStream, header.
 *   - Optical sector adjuncts: sectorPrefix/sectorSuffix/subchannel plus corrected variants & mode2_subheaders.
 *   - Deduplication: inMemoryDdt, userDataDdt*, userDataDdtHeader, mini/big/cached secondary arrays, version tags.
 *   - Metadata & geometry: geometryBlock, metadataBlockHeader+metadataBlock, cicmBlockHeader+cicmBlock, tracksHeader.
 *   - Tracks & hardware: trackEntries, dataTracks, dumpHardwareHeader, dumpHardwareEntriesWithData.
 *   - Integrity & ECC: checksums, eccCdContext, crc64Context.
 *   - Index & dedup lookup: indexEntries (UT_array of IndexEntry), sectorHashMap (duplicate detection), deduplicate
 * flag.
 *   - Write path: isWriting, currentBlockHeader, writingBuffer(+position/offset), nextBlockPosition.
 *
 *  Notes:
 *   - userDataDdt points to memory-mapped or fully loaded DDT (legacy path); userDataDdtMini / userDataDdtBig
 * supersede.
 *   - shift retained for backward compatibility with earlier single‑level address shift semantics.
 *   - mappedMemoryDdtSize is meaningful only if userDataDdt references an mmapped region.
 */
typedef struct aaruformat_context
{
    /* Core & header */
    uint64_t     magic;                  ///< File magic (AARU_MAGIC) post-open.
    AaruHeaderV2 header;                 ///< Parsed container header (v2).
    FILE        *imageStream;            ///< Underlying FILE* stream (binary mode).
    uint8_t      library_major_version;  ///< Linked library major version.
    uint8_t      library_minor_version;  ///< Linked library minor version;

    /* Deduplication tables (DDT) */
    uint64_t         *user_data_ddt;           ///< Legacy flat DDT pointer (NULL when using v2 mini/big arrays).
    TapeDdtHashEntry *tape_ddt;                ///< Hash table root for tape DDT entries
    uint32_t         *sector_prefix_ddt;       ///< Legacy CD sector prefix DDT (deprecated by *2).
    uint32_t         *sector_suffix_ddt;       ///< Legacy CD sector suffix DDT.
    uint64_t         *sector_prefix_ddt2;      ///< CD sector prefix DDT V2.
    uint64_t         *sector_suffix_ddt2;      ///< CD sector suffix DDT V2.
    uint64_t         *user_data_ddt2;          ///< DDT entries (big variant) primary/secondary current.
    uint64_t         *cached_secondary_ddt2;   ///< Cached secondary table (big entries) or NULL.
    DdtHeader2        user_data_ddt_header;    ///< Active user data DDT v2 header (primary table meta).
    uint64_t          cached_ddt_offset;       ///< File offset of currently cached secondary DDT (0=none).
    uint64_t          cached_ddt_position;     ///< Position index of cached secondary DDT.
    uint64_t          primary_ddt_offset;      ///< File offset of the primary DDT v2 table.
    size_t            mapped_memory_ddt_size;  ///< Length of mmapped DDT if userDataDdt is mmapped.
    int               ddt_version;             ///< DDT version in use (1=legacy, 2=v2 hierarchical).
    uint8_t           shift;                   ///< Legacy overall shift (deprecated by data_shift/table_shift).
    bool              in_memory_ddt;           ///< True if primary (and possibly secondary) DDT loaded.
    bool              no_user_data_ddt;        ///< True if image intentionally has no user-data DDT (e.g. flux-only image). When set, user_data_ddt*/tape_ddt MUST be NULL and user_data_ddt_header.entries MUST be 0.

    /* Optical auxiliary buffers (NULL if not present) */
    uint8_t *sector_prefix;               ///< Raw per-sector prefix (e.g., sync+header) uncorrected.
    uint8_t *sector_prefix_corrected;     ///< Corrected variant (post error correction) if stored.
    uint8_t *sector_suffix;               ///< Raw per-sector suffix (EDC/ECC) uncorrected.
    uint8_t *sector_suffix_corrected;     ///< Corrected suffix if stored separately.
    uint8_t *sector_subchannel;           ///< Raw 96-byte subchannel (if captured).
    uint8_t *mode2_subheaders;            ///< MODE2 Form1/Form2 8-byte subheaders (concatenated).
    uint8_t *sector_id;                   ///< DVD sector ID (4 bytes) if present.
    uint8_t *sector_ied;                  ///< DVD sector IED (2 bytes) if present.
    uint8_t *sector_cpr_mai;              ///< DVD sector CPR_MAI (6 bytes) if present.
    uint8_t *sector_edc;                  ///< DVD or Blu-ray sector EDC (4 bytes) if present (see DvdSectorEdc / BdSectorEdc data types).
    uint8_t *sector_decrypted_title_key;  ///< DVD decrypted title key (5 bytes) if present.

    /* Metadata & geometry */
    struct DumpHardwareEntriesWithData *dump_hardware_entries_with_data;  ///< Array of dump hardware entries + strings.
    uint8_t                            *metadata_block;       ///< Raw metadata UTF-16LE concatenated strings.
    uint8_t                            *cicm_block;           ///< CICM XML payload.
    uint8_t                            *json_block;           ///< JSON metadata block payload (UTF-8).
    uint8_t                            *creator;              ///< Who (person) created the image?
    uint8_t                            *media_title;          ///< Title of the media represented by the image
    uint8_t                            *comments;             ///< Image comments
    uint8_t                            *media_manufacturer;   ///< Manufacturer of the media represented by the image
    uint8_t                            *media_model;          ///< Model of the media represented by the image
    uint8_t                            *media_serial_number;  ///< Serial number of the media represented by the image
    uint8_t                            *media_barcode;        ///< Barcode of the media represented by the image
    uint8_t                            *media_part_number;    ///< Part number of the media represented by the image
    uint8_t *drive_manufacturer;   ///< Manufacturer of the drive used to read the media represented by the image
    uint8_t *drive_model;          ///< Model of the drive used to read the media represented by the image
    uint8_t *drive_serial_number;  ///< Serial number of the drive used to read the media represented by the image
    uint8_t
        *drive_firmware_revision;  ///< Firmware revision of the drive used to read the media represented by the image
    GeometryBlockHeader         geometry_block;         ///< Logical geometry block (if present).
    MetadataBlockHeader         metadata_block_header;  ///< Metadata block header.
    CicmMetadataBlock           cicm_block_header;      ///< CICM metadata header (if present).
    DumpHardwareHeader          dump_hardware_header;   ///< Dump hardware header.
    AaruMetadataJsonBlockHeader json_block_header;      ///< JSON metadata block header (if present).
    uint32_t                    cylinders;              ///< Cylinders of the media represented by the image
    uint32_t                    heads;                  ///< Heads of the media represented by the image
    uint32_t sectors_per_track;    ///< Sectors per track of the media represented by the image (for variable image, the
                                   ///< smallest)
    int32_t  media_sequence;       ///< Number in sequence for the media represented by the image
    int32_t  last_media_sequence;  ///< Last media of the sequence the media represented by the image corresponds to

    /* Optical information */
    TrackEntry  *track_entries;          ///< Full track list (tracksHeader.entries elements).
    TrackEntry  *data_tracks;            ///< Filtered list of data tracks (subset of trackEntries).
    TracksHeader tracks_header;          ///< Tracks header (optical) if present.
    uint8_t      number_of_data_tracks;  ///< Count of tracks considered "data" (sequence 1..99 heuristics).

    /* Integrity & ECC */
    CdEccContext *ecc_cd_context;  ///< CD ECC/EDC helper tables (allocated on demand).
    crc64_ctx    *crc64_context;   ///< Opaque CRC64 context for streaming updates.

    /* Index & deduplication lookup */
    UT_array   *index_entries;    ///< Flattened index entries (UT_array of IndexEntry).
    hash_map_t *sector_hash_map;  ///< Deduplication hash map (fingerprint->entry mapping).

    /* Caches */
    struct CacheHeader block_header_cache;  ///< LRU/Cache header for block headers.
    struct CacheHeader block_cache;         ///< LRU/Cache header for block payloads.

    /* High-level summary */
    ImageInfo image_info;  ///< Exposed high-level image info summary.

    /* Tags */
    bool          *readableSectorTags;  ///< Per-sector boolean array (optical tags read successfully?).
    mediaTagEntry *mediaTags;           ///< Hash table of extra media tags (uthash root).

    /* Checksums */
    spamsum_ctx   *spamsum_context;      ///< Opaque SpamSum context for streaming updates
    blake3_hasher *blake3_context;       ///< Opaque BLAKE3 context for streaming updates
    Checksums      checksums;            ///< Whole-image checksums discovered.
    md5_ctx        md5_context;          ///< Opaque MD5 context for streaming updates
    sha1_ctx       sha1_context;         ///< Opaque SHA-1 context for streaming updates
    sha256_ctx     sha256_context;       ///< Opaque SHA-256 context for streaming updates
    bool           calculating_md5;      ///< True if whole-image MD5 being calculated on-the-fly.
    bool           calculating_sha1;     ///< True if whole-image SHA-1 being calculated on-the-fly.
    bool           calculating_sha256;   ///< True if whole-image SHA-256 being calculated on-the-fly.
    bool           calculating_spamsum;  ///< True if whole-image SpamSum being calculated on-the-fly.
    bool           calculating_blake3;   ///< True if whole-image BLAKE3 being calculated on-the-fly.

    /* Write path */
    uint8_t    *writing_buffer;           ///< Accumulation buffer for current block data.
    BlockHeader current_block_header;     ///< Header for block currently being assembled (write path).
    uint64_t    next_block_position;      ///< Absolute file offset where next block will be written.
    uint64_t    last_written_block;       ///< Last written block number (write path).
    size_t      sector_prefix_length;     ///< Length of sector_prefix
    size_t      sector_suffix_length;     ///< Length of sector_suffix
    size_t      sector_prefix_offset;     ///< Current position in sector_prefix
    size_t      sector_suffix_offset;     ///< Current position in sector_suffix
    int         current_block_offset;     ///< Logical offset inside block (units: bytes or sectors depending on path).
    int         writing_buffer_position;  ///< Current size / position within writingBuffer.
    uint8_t     current_track_type;  ///< Current track type (when writing optical images with tracks, needed for block
                                     ///< compression type).
    bool        is_writing;          ///< True if context opened/created for writing.
    bool        rewinded;            ///< True if stream has been rewound after open (write path).
    bool        writing_long;        ///< True if writing long sectors
    bool        block_zero_written;  ///< True if block zero has been written (writing path).
    int32_t (*finalize_write)(struct aaruformat_context *ctx);  ///< Writer finalization hook (NULL for reader).

    /* Options */
    uint32_t lzma_dict_size;       ///< LZMA dictionary size (writing path).
    bool     deduplicate;          ///< Storage deduplication active (duplicates coalesce).
    bool     compression_enabled;  ///< True if block compression enabled (writing path).
    bool     use_zstd;             ///< Use Zstandard instead of LZMA for data blocks.
    bool     has_zstd_blocks;     ///< True if any block was actually written with Zstandard compression.
    int      zstd_level;           ///< Zstandard compression level (writing path, default 19).
    int      num_threads;          ///< Compression worker threads (1 = single-threaded, default).

    /* Tape-specific structures */
    tapeFileHashEntry      *tape_files;       ///< Hash table root for tape files
    TapePartitionHashEntry *tape_partitions;  ///< Hash table root for tape partitions
    bool                    is_tape;          ///< True if the image is a tape image

    /* Flux data structures */
    FluxHeader           flux_data_header;  ///< Flux data header (if present).
    FluxEntry           *flux_entries;      ///< Array of flux entries (flux_data_header.entries elements).
    UT_array            *flux_captures;     ///< Pending flux capture payloads (write path).
    FluxCaptureMapEntry *flux_map;          ///< Hash map for flux capture lookup by head/track/subtrack/capture index.

    /* Dirty flags (controls write behavior in close.c) */
    bool dirty_secondary_ddt;                  ///< True if secondary DDT tables should be written during close
    bool dirty_primary_ddt;                    ///< True if primary DDT table should be written during close
    bool dirty_single_level_ddt;               ///< True if single-level DDT should be written during close
    bool dirty_checksum_block;                 ///< True if checksum block should be written during close
    bool dirty_tracks_block;                   ///< True if tracks block should be written during close
    bool dirty_mode2_subheaders_block;         ///< True if MODE2 subheader block should be written during close
    bool dirty_sector_prefix_block;            ///< True if sector prefix block should be written during close
    bool dirty_sector_prefix_ddt;              ///< True if sector prefix DDT should be written during close
    bool dirty_sector_suffix_block;            ///< True if sector suffix block should be written during close
    bool dirty_sector_suffix_ddt;              ///< True if sector suffix DDT should be written during close
    bool dirty_sector_subchannel_block;        ///< True if subchannel block should be written during close
    bool dirty_dvd_long_sector_blocks;         ///< True if DVD long sector blocks should be written during close
    bool dirty_bd_sector_edc_block;            ///< True if Blu-ray sector EDC data block should be written during close
    bool dirty_dvd_title_key_decrypted_block;  ///< True if decrypted title key block should be written during close
    bool dirty_media_tags;                     ///< True if media tags should be written during close
    bool dirty_tape_ddt;                       ///< True if tape DDT should be written during close
    bool dirty_tape_file_block;                ///< True if tape file block should be written during close
    bool dirty_tape_partition_block;           ///< True if tape partition block should be written during close
    bool dirty_geometry_block;                 ///< True if geometry block should be written during close
    bool dirty_metadata_block;                 ///< True if metadata block should be written during close
    bool dirty_dumphw_block;                   ///< True if dump hardware block should be written during close
    bool dirty_cicm_block;                     ///< True if CICM metadata block should be written during close
    bool dirty_json_block;                     ///< True if JSON metadata block should be written during close
    bool dirty_flux_block;                     ///< True if flux block should be written during close
    bool dirty_index_block;                    ///< True if index block should be written during close

    // PS3 encryption support (lazy-initialized on first use)
    uint8_t *ps3_disc_key;                ///< Cached disc key (16 bytes), NULL if not loaded
    void    *ps3_plaintext_regions;       ///< Parsed Ps3PlaintextRegion array (max 32), NULL if not loaded
    uint32_t ps3_plaintext_region_count;  ///< Number of plaintext regions
    bool     ps3_encryption_initialized;  ///< Whether lazy init has occurred

    // Wii U encryption support (lazy-initialized on first use)
    uint8_t *wiiu_disc_key;                ///< Cached disc key (16 bytes), NULL if not loaded
    void    *wiiu_partition_regions;       ///< Parsed WiiuPartitionRegion array, NULL if not loaded
    uint32_t wiiu_partition_region_count;  ///< Number of partition regions
    bool     wiiu_encryption_initialized;  ///< Whether lazy init has occurred
    uint8_t *wiiu_encrypted_block_cache;   ///< Cached re-encrypted 0x8000-byte physical sector
    uint64_t wiiu_cached_physical_sector;  ///< Physical sector number of cached block
    bool     wiiu_cache_valid;             ///< Whether the encrypted block cache is valid
    bool     wiiu_building_crypto_block;   ///< True while gathering sectors for re-encryption (suppresses recursion)

    // Nintendo GC/Wii junk map support (lazy-initialized on first use)
    void    *ngcw_junk_entries;      ///< Parsed NgcwJunkEntry array, NULL if not loaded
    uint32_t ngcw_junk_entry_count;  ///< Number of junk entries
    uint16_t ngcw_junk_seed_size;    ///< LFG seed size in uint32 words (expected: 17)
    bool     ngcw_junk_initialized;  ///< Whether junk map has been loaded

    // Nintendo Wii encryption support (lazy-initialized on first use)
    void    *wii_partition_regions;       ///< Parsed WiiPartitionRegion array, NULL if not loaded
    uint32_t wii_partition_region_count;  ///< Number of partition regions
    bool     wii_encryption_initialized;  ///< Whether lazy init has occurred
    uint8_t *wii_encrypted_group_cache;   ///< Cached re-encrypted 0x8000-byte group
    uint64_t wii_cached_physical_group;   ///< Physical group number of cached block
    bool     wii_cache_valid;             ///< Whether the encrypted group cache is valid
    bool     wii_building_crypto_block;   ///< True while gathering sectors for re-encryption (suppresses recursion)

    /* Erasure coding (write path) */
    uint8_t   ec_algorithm;           ///< ErasureCodingAlgorithm (0=XOR, 1=RS-Vandermonde).
    uint16_t  ec_K;                   ///< Data blocks per stripe.
    uint16_t  ec_M;                   ///< Parity blocks per stripe.
    uint32_t  ec_data_shard_size;     ///< Max on-disk block size for data blocks (fixed at creation).
    void     *ec_rs_ctx;              ///< rs_context* (opaque RS codec), NULL if EC disabled.
    uint8_t **ec_data_parity;         ///< Array of K * M parity buffers (interleaved stripe slots).
    uint64_t *ec_data_block_offsets;  ///< Array of K * K file offsets for blocks in active stripes.
    uint32_t *ec_data_block_sizes;    ///< Array of K * K actual on-disk sizes for blocks in active stripes.
    uint64_t *ec_data_shard_crcs;     ///< Array of K * K CRC64 values for blocks in active stripes.
    uint16_t *ec_data_stripe_counts;  ///< Array of K: blocks accumulated per stripe slot.
    uint32_t  ec_total_data_blocks;   ///< Total data blocks written (counter for round-robin assignment).
    UT_array *ec_data_stripes;        ///< Completed data stripe descriptors (serialized to ECMB).
    bool      ec_enabled;             ///< True if erasure coding is active.

    /* Erasure coding (read path) */
    void    *ec_read_stripes;         ///< Parsed EcReadStripe array for data group, NULL if no ECMB.
    uint32_t ec_read_stripe_count;    ///< Number of data stripes parsed from ECMB.
    void    *ec_block_lookup;         ///< uthash: block file offset → stripe index + position (data group).
    void    *ec_meta_stripes;         ///< Parsed EcReadStripe array for metadata group.
    uint32_t ec_meta_stripe_count;    ///< Number of metadata stripes.
    uint16_t ec_meta_K;               ///< K for metadata group.
    uint16_t ec_meta_M;               ///< M for metadata group.
    uint32_t ec_meta_shard_size;      ///< Shard size for metadata group.
    void    *ec_meta_block_lookup;    ///< uthash: block file offset → stripe index + position (metadata group).
    void    *ec_ddt_stripes;          ///< Parsed EcReadStripe array for DDT-secondary group.
    uint32_t ec_ddt_stripe_count;     ///< Number of DDT-secondary stripes.
    uint16_t ec_ddt_K;                ///< K for DDT-secondary group.
    uint16_t ec_ddt_M;                ///< M for DDT-secondary group.
    uint32_t ec_ddt_shard_size;       ///< Shard size for DDT-secondary group.
    void    *ec_ddt_block_lookup;     ///< uthash: block file offset → stripe index + position (DDT group).
    bool     ec_recovery_available;   ///< True if ECMB loaded and recovery is possible.
    bool     ec_recovery_in_progress; ///< Recursion guard for recovery (prevents infinite loops).
} aaruformat_context;

#ifndef AARUFORMAT_CONTEXT_DECLARED
#define AARUFORMAT_CONTEXT_DECLARED
#endif

/** \struct DumpHardwareEntriesWithData
 *  \brief In-memory representation of a dump hardware entry plus decoded variable-length fields & extents.
 *
 *  All string pointers are NUL-terminated UTF-8 copies of on-disk data (or NULL if absent). extents array may be NULL
 *  when no ranges were recorded. Freed during context teardown.
 */
typedef struct DumpHardwareEntriesWithData
{
    DumpHardwareEntry  entry;                    ///< Fixed-size header with lengths & counts.
    struct DumpExtent *extents;                  ///< Array of extents (entry.extents elements) or NULL.
    uint8_t           *manufacturer;             ///< Manufacturer string (UTF-8) or NULL.
    uint8_t           *model;                    ///< Model string or NULL.
    uint8_t           *revision;                 ///< Hardware revision string or NULL.
    uint8_t           *firmware;                 ///< Firmware version string or NULL.
    uint8_t           *serial;                   ///< Serial number string or NULL.
    uint8_t           *softwareName;             ///< Dump software name or NULL.
    uint8_t           *softwareVersion;          ///< Dump software version or NULL.
    uint8_t           *softwareOperatingSystem;  ///< Host operating system string or NULL.
} DumpHardwareEntriesWithData;

#pragma pack(push, 1)

/** \struct DumpExtent
 *  \brief Inclusive [start,end] logical sector range contributed by a single hardware environment.
 */
typedef struct DumpExtent
{
    uint64_t start;  ///< Starting LBA (inclusive).
    uint64_t end;    ///< Ending LBA (inclusive); >= start.
} DumpExtent;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_CONTEXT_H

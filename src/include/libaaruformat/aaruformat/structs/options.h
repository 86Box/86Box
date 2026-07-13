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

#ifndef LIBAARUFORMAT_OPTIONS_H
#define LIBAARUFORMAT_OPTIONS_H

#include <stdbool.h>  ///< For bool type used in aaru_options.
#include <stdint.h>   ///< For fixed-width integer types.

/** \file aaruformat/structs/options.h
 *  \brief Image creation / open tuning options structure and related semantics.
 *
 *  The library accepts a semicolon-delimited key=value options string (see parse_options()). Recognized keys:
 *    compress=true|false          Enable/disable block compression (LZMA for data blocks, FLAC for audio tracks).
 *    deduplicate=true|false       If true, identical (duplicate) sectors are stored once (DDT entries point to same
 *                                 physical block). If false, duplicates are still tracked in DDT but each occurrence
 *                                 is stored independently (no storage savings). DDT itself is always present.
 *    dictionary=<bytes>           LZMA dictionary size in bytes (fallback default 33554432 if 0 or invalid).
 *    table_shift=<n>              DDT v2 table shift (default 9) (items per primary entry = 2^n when multi-level).
 *    data_shift=<n>               Global data shift (default 12). Defines per-block address granularity: the low
 *                                 2^n range encodes the sector (or unit) offset within a block; higher bits combine
 *                                 with block_alignment to derive block file offsets. Used by DDT but not limited to it.
 *    block_alignment=<n>          log2 alignment of underlying data blocks (default 9 => 512 bytes) (block size = 2^n).
 *    md5=true|false               Generate MD5 checksum (stored in checksum block if true).
 *    sha1=true|false              Generate SHA-1 checksum.
 *    sha256=true|false            Generate SHA-256 checksum.
 *    blake3=true|false            Generate BLAKE3 checksum (may require build-time support; ignored if unsupported).
 *    spamsum=true|false           Generate SpamSum fuzzy hash.
 *    zstd=true|false              Use Zstandard instead of LZMA for data blocks.
 *    zstd_level=<n>               Zstandard compression level (1-22).
 *    threads=<n>                  Compression worker threads (>= 1). zstd: nbWorkers; LZMA: clamped to [1, 2].
 *
 *  Defaults (when option string NULL or key omitted):
 *    compress=true, deduplicate=true, dictionary=33554432, table_shift=9, data_shift=12,
 *    block_alignment=9, md5=false, sha1=false, sha256=false, blake3=false, spamsum=false.
 *
 *  Validation / normalization done in parse_options():
 *   - Zero / missing dictionary resets to default 33554432.
 *   - Zero table_shift resets to 9.
 *   - Zero data_shift resets to 12.
 *   - Zero block_alignment resets to 9.
 *
 *  Rationale:
 *   - table_shift, data_shift and block_alignment mirror fields stored in on-disk headers (see AaruHeaderV2 &
 * DdtHeader2); data_shift is a global per-block granularity exponent (not DDT-specific) governing how in-block offsets
 * are encoded.
 *   - compress selects adaptive codec usage: LZMA applied to generic/data blocks, FLAC applied to audio track payloads.
 *   - deduplicate toggles storage optimization only: the DDT directory is always built for addressing; disabling simply
 *     forces each sector's content to be written even if already present (useful for forensic byte-for-byte
 * duplication).
 *   - dictionary tunes compression ratio/memory use; large values increase memory footprint.
 *   - Checksums are optional; enabling multiple increases CPU time at write finalization.
 *
 *  Performance / space trade-offs (deduplicate=false):
 *   - Significantly larger image size: every repeated sector payload is written again.
 *   - Higher write I/O and longer creation time for highly redundant sources (e.g., zero-filled regions) compared to
 *     deduplicate=true, although CPU time spent on duplicate detection/hash lookups is reduced.
 *   - Potentially simpler post-process forensic validation (physical ordering preserved without logical coalescing).
 *   - Use when exact physical repetition is more critical than storage efficiency, or to benchmark raw device
 * throughput.
 *   - For typical archival use-cases with large zero / repeated patterns, deduplicate=true markedly reduces footprint.
 *
 *  Approximate in-RAM hash map usage for deduplication (deduplicate=true):
 *   The on-disk DDT can span many secondary tables, but only the primary table plus a currently loaded secondary (and
 *   possibly a small cache) reside in memory; their footprint is typically <<5% of total indexed media space and is
 * often negligible compared to the hash map used to detect duplicate sectors. Therefore we focus here on the hash /
 * lookup structure ("hash_map") memory, not the entire DDT on-disk size.
 *
 *   Worst-case (all sectors unique) per 1 GiB of user data:
 *     sectors_per_GiB = 2^30 / sector_size
 *     hash_bytes ≈ sectors_per_GiB * H   (H ≈ 16 bytes: 8-byte fingerprint + ~8 bytes map overhead)
 *
 *   Resulting hash_map RAM per GiB (unique sectors):
 *     +--------------+------------------+------------------------------+
 *     | Sector size  | Sectors / GiB    | Hash map (~16 B / sector)    |
 *     +--------------+------------------+------------------------------+
 *     |   512 bytes  | 2,097,152        | ~33.5 MiB  (≈32.0–36.0 MiB)  |
 *     |  2048 bytes  |   524,288        | ~ 8.0 MiB  (≈7.5–8.5  MiB)   |
 *     |  4096 bytes  |   262,144        | ~ 4.0 MiB  (≈3.8–4.3  MiB)   |
 *     +--------------+------------------+------------------------------+
 *
 *   (Range reflects allocator + load factor variation.)
 *
 *   Targeted projections (hash map only, R=1):
 *     2048‑byte sectors (~8 MiB per GiB unique)
 *       Capacity | Hash map (MiB) | Hash map (GiB)
 *       ---------+---------------+----------------
 *         25 GiB |     ~200       |   0.20
 *         50 GiB |     ~400       |   0.39
 *
 *     512‑byte sectors (~34 MiB per GiB unique; using 33.5 MiB for calc)
 *       Capacity | Hash map (MiB) | Hash map (GiB)
 *       ---------+---------------+----------------
 *        128 GiB |   ~4288        |   4.19
 *        500 GiB |  ~16750        |  16.36
 *      1   TiB*  |  ~34304        |  33.50
 *      2   TiB*  |  ~68608        |  67.00
 *
 *     *TiB = 1024 GiB binary. For decimal TB reduce by ~7% (×0.93).
 *
 *   Duplicate ratio scaling:
 *     Effective hash RAM ≈ table_value * R, where R = unique_sectors / total_sectors.
 *     Example: 500 GiB @512 B, R=0.4 ⇒ ~16750 MiB * 0.4 ≈ 6700 MiB (~6.54 GiB).
 *
 *   Quick rule of thumb (hash only):
 *     hash_bytes_per_GiB ≈ 16 * (2^30 / sector_size) ≈ (17.1799e9 / sector_size) bytes
 *       → ≈ 33.6 MiB (512 B), 8.4 MiB (2048 B), 4.2 MiB (4096 B) per GiB unique.
 *
 *   Memory planning tip:
 *     If projected hash_map usage risks exceeding available RAM, consider:
 *       - Increasing table_shift (reduces simultaneous secondary loads / contention)
 *       - Lowering data_shift (if practical) to encourage earlier big DDT adoption with fewer unique blocks
 *       - Segmenting the dump into phases (if workflow permits)
 *       - Accepting higher duplicate ratio by pre-zero detection or sparse treatment externally.
 *       - Resuming the dump in multiple passes: each resume rebuilds the hash_map from scratch, so peak RAM still
 *         matches a single-pass estimate, but average RAM over total wall time can drop if you unload between passes.
 *
 *   NOTE: DDT in-RAM portion (primary + one secondary) usually adds only a few additional MiB even for very large
 * images, hence omitted from sizing tables. Include +5% safety margin if extremely tight on memory.
 *
 *  Guidance for table_shift / data_shift selection:
 *   Let:
 *     S = total logical sectors expected in image (estimate if unknown).
 *     T = table_shift (items per primary DDT entry = 2^T when multi-level; 0 => single-level).
 *     D = data_shift (in-block sector offset span = 2^D).
 *     BA = block_alignment (bytes) = 2^block_alignment.
 *     SS = sector size (bytes).
 *
 *   1. data_shift constraints:
 *      - For SMALL DDT entries (12 payload bits after status): D must satisfy 0 < D < 12 and (12 - D) >= 1 so that at
 *        least one bit remains for block index. Practical range for small DDT: 6..10 (leaves 2+ bits for block index).
 *      - For BIG DDT entries (28 payload bits after status): D may be larger (up to 27) but values >16 rarely useful.
 *      - Effective address granularity inside a block = min(2^D * SS, physical block span implied by BA).
 *      - Choosing D too large wastes bits (larger offset range than block actually contains) and reduces the number of
 *        block index bits within a small entry, potentially forcing upgrade to big DDT earlier.
 *
 *      Recommended starting points:
 *        * 512‑byte sectors, 512‑byte block alignment: D=9 (512 offsets) or D=8 (256 offsets) keeps small DDT viable.
 *        * 2048‑byte optical sectors, 2048‑byte alignment: D=8 (256 offsets) typically sufficient.
 *        * Mixed / large logical block sizes: keep D so that (2^D * SS) ≈ typical dedup block region you want
 * addressable.
 *
 *   2. block capacity within an entry:
 *      - SMALL DDT: usable block index bits = 12 - D.
 *        Max representable block index (small) = 2^(12-D) - 1.
 *      - BIG DDT: usable block index bits = 28 - D.
 *        Max representable block index (big)   = 2^(28-D) - 1.
 *      - If (requiredBlockIndex > max) you must either reduce D or rely on big DDT.
 *
 *      Approximate requiredBlockIndex ≈ (TotalUniqueBlocks) where
 *        TotalUniqueBlocks ≈ (S * SS) / (BA * (2^D * SS / (SS))) = S / (2^D * (BA / SS))
 *        Simplified (assuming BA = SS): TotalUniqueBlocks ≈ S / 2^D.
 *
 *   3. table_shift considerations (multi-level DDT):
 *      - Primary entries count ≈ ceil(S / 2^T). Choose T so this count fits memory and keeps lookup fast.
 *      - Larger T reduces primary table size, increasing secondary table dereferences.
 *      - Typical balanced values: T in [8..12] (256..4096 sectors per primary entry).
 *      - Set T=0 for single-level when S is small enough that all entries fit comfortably in memory.
 *
 *      Memory rough estimate for single-level SMALL DDT:
 *        bytes ≈ S * 2  (each small entry 2 bytes). For BIG DDT: bytes ≈ S * 4.
 *      Multi-level: primary table bytes ≈ (S / 2^T) * entrySize + sum(secondary tables).
 *
 *   4. Example scenarios:
 *      - 50M sectors (≈25 GiB @512B), want small DDT: pick D=8 (256); block index bits=4 (max 16 blocks) insufficient.
 *        Need either D=6 (1024 block indices) or accept BIG DDT (28-8=20 bits => million+ blocks). So prefer BIG DDT
 * here.
 *      - 2M sectors, 2048B alignment, optical: D=8 gives S/2^D ≈ 7812 unique offsets; small DDT block index bits=4 (max
 * 16) inadequate → choose D=6 (offset span 64 sectors) giving 6 block index bits (max 64) or just use big DDT.
 *
 *   5. Practical recommendations:
 *      - If unsure and image > ~1M sectors: keep defaults (data_shift=12, table_shift=9) and allow big DDT.
 *      - For small archival (<100k sectors): T=0 (single-level), D≈8..10 to keep small DDT feasible.
 *      - Benchmark before lowering D purely to stay in small DDT; increased secondary lookups or larger primary tables
 * can offset saved space.
 *
 *   Recommended presets (approximate bands):
 *     +----------------------+----------------------+---------------------------+-------------------------------+
 *     | Total logical sectors | table_shift (T)      | data_shift (D)            | Notes                         |
 *     +----------------------+----------------------+---------------------------+-------------------------------+
 *     |   <   50,000          | 0                    | 8 – 10                    | Single-level small DDT likely |
 *     | 50K –   1,000,000     | 8 – 9                | 9 – 10                    | Still feasible small DDT      |
 *     | 1M  –  10,000,000     | 9 – 10               | 10 – 12                   | Borderline small -> big DDT   |
 *     | 10M – 100,000,000     | 10 – 11              | 11 – 12                   | Prefer big DDT; tune T for mem|
 *     |   > 100,000,000       | 11 – 12              | 12                        | Big DDT; higher T saves memory|
 *     +----------------------+----------------------+---------------------------+-------------------------------+
 *     Ranges show typical stable regions; pick the lower end of table_shift if memory is ample, higher if minimizing
 *     primary table size. Always validate actual unique block count vs payload bits.
 *
 *   NOTE: The library will automatically fall back to BIG DDT where needed; these settings bias structure, they do not
 *         guarantee small DDT retention.
 *
 *  Thread-safety: aaru_options is a plain POD struct; caller may copy freely. parse_options() returns by value.
 *
 *  Future compatibility: unknown keys are ignored by current parser; consumers should preserve original option
 *  strings if round-tripping is required.
 */

/** \struct aaru_options
 *  \brief Parsed user-specified tunables controlling compression, deduplication, hashing and DDT geometry.
 *
 *  All shifts are exponents of two.
 */
typedef struct
{
    bool     compress;     ///< Enable adaptive compression (LZMA for data blocks, FLAC for audio). Default: true.
    bool     deduplicate;  ///< Storage dedup flag (DDT always exists). true=share identical sector content, false=store
                           ///< each instance.
    uint32_t dictionary;   ///< LZMA dictionary size in bytes (>= 4096 recommended). Default: 33554432 (32 MiB).
    int8_t   table_shift;  ///< DDT table shift (multi-level fan-out exponent). Default: heuristically calculated.
    uint8_t  data_shift;   ///< Global data shift: low bits encode sector offset inside a block (2^data_shift span).
    uint8_t  block_alignment;  ///< log2 underlying block alignment (2^n bytes). Default: 9 (512 bytes).
    bool     md5;              ///< Generate MD5 checksum (ChecksumAlgorithm::Md5) when finalizing image.
    bool     sha1;             ///< Generate SHA-1 checksum (ChecksumAlgorithm::Sha1) when finalizing image.
    bool     sha256;           ///< Generate SHA-256 checksum (ChecksumAlgorithm::Sha256) when finalizing image.
    bool     blake3;           ///< Generate BLAKE3 checksum if supported (not stored if algorithm unavailable).
    bool     spamsum;          ///< Generate SpamSum fuzzy hash (ChecksumAlgorithm::SpamSum) if enabled.
    bool     zstd;             ///< Use Zstandard instead of LZMA for data blocks. Default: false.
    int      zstd_level;       ///< Zstandard compression level (1-22). Default: 19.
    int      num_threads;      ///< Number of compression worker threads. Default: 1 (single-threaded).
                               ///< zstd: passed as nbWorkers (0 and 1 both mean single-threaded).
                               ///< LZMA: clamped to [1, 2] (2 = threaded match finder).
} aaru_options;

#endif  // LIBAARUFORMAT_OPTIONS_H

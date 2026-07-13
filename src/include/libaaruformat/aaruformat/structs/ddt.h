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

#ifndef LIBAARUFORMAT_DDT_H
#define LIBAARUFORMAT_DDT_H

#include <stdint.h>  // fixed-width types for on-disk layout

#pragma pack(push, 1)

/** \file aaruformat/structs/ddt.h
 *  \brief On-disk headers for Deduplication Data Tables (DDT) versions 1 and 2.
 *
 * A DDT maps logical sector indices (LBAs within an image's logical address space) to (block, sector)
 * pairs plus a base file offset, enabling content de-duplication inside the container. Two generations
 * exist:
 *  - DdtHeader  ("version 1") flat table.
 *  - DdtHeader2 ("version 2") hierarchical, multi-level subtables for scalability.
 *
 * All integers are little-endian. Structures are packed (1-byte alignment). When porting to a big-endian
 * architecture callers must perform byte swapping. Do not rely on compiler-introduced padding.
 *
 * Compression of the table body (entries array) follows the same conventions as data blocks: first
 * decompress according to the compression enum, then validate CRC64 for uncompressed contents.
 *
 * Related enumerations:
 *  - BlockType::DeDuplicationTable / BlockType::DeDuplicationTable2
 *  - CompressionType
 *  - DataType
 *  - DdtSizeType (for DdtHeader2::sizeType)
 */

/**
 * \struct DdtHeader
 * \brief Header preceding a version 1 (flat) deduplication table body.
 *
 * Immediately after this header there are \ref entries table records (compressed if \ref compression != None).
 * Each table record encodes a pointer using an 8-bit file offset component and a sector offset inside a block:
 *   logicalEntryValue = ((uint64_t)fileByteOffset << shift) + sectorOffsetWithinBlock
 * where fileByteOffset is measured in bytes (granularity depends on shift) and sectorOffsetWithinBlock is
 * relative to the start of the referenced data block. The sector size must be taken from the corresponding
 * data block(s) (see BlockHeader::sectorSize) or higher-level metadata.
 *
 * Invariants:
 *  - cmpLength == length if compression == CompressionType::None
 *  - length % (entrySize) == 0 after decompression (implementation-defined entry size)
 *  - entries * entrySize == length
 *  - entries > 0 implies length > 0
 */
typedef struct DdtHeader
{
    uint32_t identifier;   ///< Block identifier, must be BlockType::DeDuplicationTable.
    uint16_t type;         ///< Data classification (\ref DataType) for sectors referenced by this table.
    uint16_t compression;  ///< Compression algorithm for the table body (\ref CompressionType).
    uint8_t  shift;        ///< Left shift applied to per-entry file offset component forming logicalEntryValue.
    uint64_t entries;      ///< Number of deduplication entries contained in (uncompressed) table.
    uint64_t cmpLength;    ///< Size in bytes of compressed entries payload.
    uint64_t length;       ///< Size in bytes of uncompressed entries payload.
    uint64_t cmpCrc64;     ///< CRC64-ECMA of the compressed payload.
    uint64_t crc64;        ///< CRC64-ECMA of the uncompressed payload.
} DdtHeader;

/**
 * \struct DdtHeader2
 * \brief Header preceding a version 2 hierarchical deduplication table.
 *
 * Version 2 introduces multi-level tables to efficiently address very large images by subdividing
 * the logical address space. Tables at higher levels partition regions; leaves contain direct
 * (block, sector) entry mappings. Navigation uses \ref tableLevel (0 = root) and \ref levels (total depth).
 *
 * Logical sector (LBA) mapping (actual implementation in decode_ddt_{single,multi}_level_v2):
 *  1. Let L be the requested logical sector (can be negative externally). Internal index I = L + negative.
 *     Valid range: 0 <= I < blocks. (Total user-data sectors often = blocks - negative - overflow.)
 *  2. If tableShift == 0 (single-level): entryIndex = I.
 *     Else (multi-level):
 *        itemsPerPrimaryEntry = 1 << tableShift
 *        primaryIndex  = I / itemsPerPrimaryEntry
 *        secondaryIndex = I % itemsPerPrimaryEntry
 *        The primary table entry at primaryIndex yields a secondary DDT file offset (scaled by 2^blockAlignmentShift),
 *        whose table entries are then indexed by secondaryIndex.
 *  3. Read raw DDT entry value E (64-bit).
 *  4. If E == 0: sector_status = SectorStatusNotDumped; offset=block_offset=0.
 *     Otherwise extract:
 *        statusBits = E >> 60
 *        baseBits   = E & 0xFFFFFFFFFFFFFFF
 *        sectorOffsetWithinBlock = baseBits & ((1 << dataShift) - 1)
 *        blockIndex              = baseBits >> dataShift
 *        block_offset (bytes)    = blockIndex << blockAlignmentShift
 *        offset (sector units inside block) = sectorOffsetWithinBlock
 *  5. The consumer combines block_offset, offset, and the (external) logical sector size to locate data.
 *
 * Field roles:
 *  - negative:   Count of leading negative LBAs supported; added to L to form internal index.
 *  - overflow:   Count of trailing LBAs beyond the user area upper bound that are still dumped and have
 *                normal DDT entries (e.g. optical disc lead-out). Symmetrical to 'negative' on the high end.
 *  - start:      For secondary tables, base internal index covered (written when creating new tables). Current decoding
 *                logic does not consult this field (future-proof placeholder).
 *  - blockAlignmentShift: log2 alignment of stored data blocks (byte granularity of block_offset).
 *  - dataShift:  log2 of the number of addressable sectors per increment of blockIndex bitfield unit.
 *  - tableShift: log2 of number of logical sectors covered by a single primary-table pointer (multi-level only).
 *
 * Notes & current limitations:
 *  - User area sector count = blocks - negative - overflow.
 *  - Valid external LBA range exposed by the image = [-negative, (blocks - negative - 1)].
 *    * Negative range: [-negative, -1]
 *    * User area range: [0, (blocks - negative - overflow - 1)]
 *    * Overflow range: [(blocks - negative - overflow), (blocks - negative - 1)]
 *  - Both negative and overflow ranges are stored with normal DDT entries (if present), enabling complete
 *    reproduction of lead-in / lead-out or similar padding regions.
 *  - start is presently ignored during decoding; integrity checks against it may be added in future revisions.
 *  - No masking is applied to I besides array bounds; callers must ensure L is within representable range.
 *
 * Example (Compact Disc):
 *  Disc has 360000 user sectors. Lead-in captured as 15000 negative sectors and lead-out as 15000 overflow sectors.
 *    negative = 15000
 *    overflow = 15000
 *    user sectors = 360000
 *    blocks (internal span) = negative + user + overflow = 390000
 *    External LBA spans: -15000 .. 374999
 *      * Negative: -15000 .. -1 (15000 sectors)
 *      * User:      0 .. 359999 (360000 sectors)
 *      * Overflow:  360000 .. 374999 (15000 sectors)
 *  Internal index I for any external L is I = L + negative.
 *  User area sector count reported to callers (ctx->imageInfo.Sectors) = blocks - negative - overflow = 360000.
 */
typedef struct DdtHeader2
{
    uint32_t identifier;           ///< Block identifier, must be BlockType::DeDuplicationTable2.
    uint16_t type;                 ///< Data classification (\ref DataType) for sectors referenced by this table.
    uint16_t compression;          ///< Compression algorithm for this table body (\ref CompressionType).
    uint8_t  levels;               ///< Total number of hierarchy levels (root depth); > 0.
    uint8_t  tableLevel;           ///< Zero-based level index of this table (0 = root, increases downward).
    uint64_t previousLevelOffset;  ///< Absolute byte offset of the parent (previous) level table; 0 if root.
    uint32_t negative;             ///< Leading negative LBA count; added to external L to build internal index.
    uint64_t blocks;               ///< Total internal span (negative + usable + overflow) in logical sectors.
    uint32_t overflow;  ///< Trailing dumped sectors beyond user area (overflow range), still mapped with entries.
    uint64_t
            start;  ///< Base internal index covered by this table (used for secondary tables; currently informational).
    uint8_t blockAlignmentShift;  ///< 2^blockAlignmentShift = block alignment boundary in bytes.
    uint8_t dataShift;            ///< 2^dataShift = sectors represented per increment in blockIndex field.
    uint8_t tableShift;  ///< 2^tableShift = number of logical sectors per primary entry (multi-level only; 0 for
                         ///< single-level or secondary tables).
    uint64_t entries;    ///< Number of entries contained in (uncompressed) table payload.
    uint64_t cmpLength;  ///< Compressed payload size in bytes.
    uint64_t length;     ///< Uncompressed payload size in bytes.
    uint64_t cmpCrc64;   ///< CRC64-ECMA of compressed table payload.
    uint64_t crc64;      ///< CRC64-ECMA of uncompressed table payload.
} DdtHeader2;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_DDT_H

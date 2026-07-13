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
 * */

/**
 * @file tape.h
 * @brief Data structures for tape media support in Aaru disk images.
 *
 * This header defines structures used to represent tape-specific metadata in the Aaru
 * image format. Tape media differs from disk media in several key ways:
 *
 * **Tape Organization:**
 * - Tapes are organized into sequential blocks rather than random-access sectors
 * - Multiple logical files can exist on a single tape
 * - Tapes may be divided into multiple partitions
 * - Files are defined by contiguous ranges of blocks within a partition
 *
 * **File Structure:**
 * Tape files are logical groupings of blocks that represent discrete data units.
 * Each file has:
 * - A unique file number identifying it within its partition
 * - A partition number indicating which physical partition contains it
 * - A block range (FirstBlock to LastBlock, inclusive) defining its extent
 *
 * **Partition Structure:**
 * Tape partitions are physical divisions of the tape medium. Each partition:
 * - Has a unique partition number
 * - Contains a contiguous range of blocks
 * - Can contain multiple files or none
 * - May have different formatting or characteristics
 *
 * **Block Addressing:**
 * Block numbers are sequential within a partition, starting from 0. The FirstBlock
 * and LastBlock values are inclusive, meaning both boundary blocks are part of the
 * file or partition. For example, a file with FirstBlock=100 and LastBlock=199
 * contains 100 blocks (blocks 100 through 199 inclusive).
 *
 * **Use Cases:**
 * - Archiving legacy tape media (DAT, DLT, LTO, QIC, etc.)
 * - Preserving tape structure for restoration to physical media
 * - Forensic imaging of tape evidence
 * - Migration of tape archives to modern storage
 * - Documentation of tape organization for archival purposes
 *
 * **Storage in Aaru Images:**
 * The tape metadata blocks (TapeFileHeader and TapePartitionHeader) are optional
 * structural blocks written during image finalization. They allow readers to
 * understand the logical organization of the tape without parsing the entire
 * data stream.
 *
 * @note All structures in this file use packed alignment (#pragma pack(push, 1))
 *       to ensure consistent on-disk layout across different compilers and platforms.
 *
 * @note Block addresses are 0-based and partition numbers typically start from 0
 *       (though some tape formats may use 1-based numbering).
 *
 * @see BlockType for the identifier constants used in TapeFileHeader and TapePartitionHeader
 */

#ifndef LIBAARUFORMAT_TAPE_H
#define LIBAARUFORMAT_TAPE_H

#include <stdint.h>

#pragma pack(push, 1)

/**
 * @struct TapeFileEntry
 * @brief Describes a single logical file on a tape medium.
 *
 * A tape file is a contiguous sequence of blocks that represents a discrete logical
 * unit of data. Files are the primary organizational unit on tape media, analogous
 * to files on a filesystem but simpler in structure. Each file is identified by a
 * file number and exists within a specific partition.
 *
 * **File Number:**
 * The file number uniquely identifies the file within its partition. File numbering
 * typically starts from 0 or 1 depending on the tape format. Sequential files are
 * numbered consecutively, though gaps may exist if files were deleted or the tape
 * was formatted with specific file positions reserved.
 *
 * **Partition Association:**
 * Each file belongs to exactly one partition. Multi-partition tapes can have files
 * with the same file number in different partitions - the combination of partition
 * number and file number uniquely identifies a file on the tape.
 *
 * **Block Range:**
 * The FirstBlock and LastBlock fields define the inclusive range of blocks that
 * comprise the file. Both endpoints are included in the file. For example:
 * - FirstBlock=0, LastBlock=0: A single-block file (block 0)
 * - FirstBlock=10, LastBlock=19: A ten-block file (blocks 10-19 inclusive)
 * - FirstBlock=100, LastBlock=99: Invalid (FirstBlock must be ≤ LastBlock)
 *
 * **Physical Layout:**
 * Files occupy contiguous blocks on tape. There are no block pointers or allocation
 * tables as found in disk filesystems. The physical ordering matches the logical
 * ordering defined by the block range.
 *
 * **File Marks:**
 * On physical tape, files are typically separated by filemarks (also called tape marks).
 * This structure represents the logical file boundaries; the actual filemarks may be
 * implicit in the block numbering or explicitly stored in the tape data stream.
 *
 * **Size Calculation:**
 * File size in blocks = (LastBlock - FirstBlock + 1)
 * File size in bytes = file_size_in_blocks × block_size (where block_size is
 * tape-format-specific, commonly 512, 1024, or variable)
 *
 * @note File numbers and block addresses are format-specific. Some formats use
 *       0-based numbering while others use 1-based. The Aaru format preserves
 *       the original numbering scheme.
 *
 * @note Files cannot span partitions. If a logical dataset crosses partition
 *       boundaries, it would be represented as separate files in each partition.
 *
 * @note Empty files (where FirstBlock == LastBlock but containing no data) may
 *       exist on some tape formats to serve as markers or placeholders.
 */
typedef struct TapeFileEntry
{
    uint32_t File;  ///< File number (unique within the partition). Identifies this file among all files in the same
                    ///< partition. Numbering scheme is tape-format-dependent.
    uint8_t  Partition;   ///< Partition number containing this file. References a partition defined in the
                          ///< TapePartitionHeader block. Valid range: 0-255.
    uint64_t FirstBlock;  ///< First block of the file (inclusive). This is the starting block address of the file data.
                          ///< Block addresses are 0-based within the partition.
    uint64_t
        LastBlock;  ///< Last block of the file (inclusive). This is the ending block address of the file data. Must be
                    ///< ≥ FirstBlock. The file contains all blocks from FirstBlock through LastBlock inclusive.
} TapeFileEntry;

/**
 * @struct TapeFileHeader
 * @brief Header for a tape file metadata block containing file layout information.
 *
 * This structure serves as the header for a TapeFileBlock, which documents all
 * logical files present on the tape medium. The block consists of this fixed-size
 * header followed by a variable-length array of TapeFileEntry structures (one per file).
 *
 * **Block Structure:**
 * ```
 * +-------------------------+
 * | TapeFileHeader          | <- Fixed 24-byte header
 * +-------------------------+
 * | TapeFileEntry 0         | <- First file entry
 * +-------------------------+
 * | TapeFileEntry 1         | <- Second file entry
 * +-------------------------+
 * | ...                     |
 * +-------------------------+
 * | TapeFileEntry (n-1)     | <- Last file entry
 * +-------------------------+
 * ```
 *
 * **Purpose:**
 * The tape file block enables:
 * - Quick lookup of file locations without scanning the entire tape
 * - Validation of file boundaries and partition assignments
 * - Random access to specific files in the image
 * - Preservation of the original tape's logical organization
 * - Documentation of tape structure for archival purposes
 *
 * **Identifier Field:**
 * The identifier field must be set to BlockType::TapeFileBlock to indicate this
 * block type. This allows the Aaru format parser to recognize and correctly
 * interpret the block during image loading.
 *
 * **Entry Count:**
 * The entries field specifies the number of TapeFileEntry structures that follow
 * the header. A tape with no files would have entries=0, though this is unusual.
 * Maximum value is 2^32-1 (4,294,967,295 files), though practical limits are
 * much lower.
 *
 * **Block Length:**
 * The length field contains the size in bytes of the data following this header
 * (i.e., the array of TapeFileEntry structures). This is calculated as:
 *   length = entries × sizeof(TapeFileEntry)
 *
 * The header itself is NOT included in the length value. Total block size is:
 *   total_size = sizeof(TapeFileHeader) + length
 *
 * **CRC64 Checksum:**
 * The crc64 field contains a CRC64-ECMA checksum computed over the entry data
 * (the array of TapeFileEntry structures, excluding this header). This provides
 * integrity verification to detect corruption in the file table. The CRC is
 * calculated using the ECMA polynomial.
 *
 * **File Order:**
 * Entries should be ordered by partition number (ascending), then by file number
 * (ascending) within each partition. This ordering facilitates efficient lookup
 * and matches the physical organization of most tape formats.
 *
 * **Alignment:**
 * When written to the Aaru image, this block is aligned to the image's block
 * alignment boundary (typically 2^blockAlignmentShift bytes). Padding may be
 * inserted before the block to achieve proper alignment.
 *
 * **Index Integration:**
 * After writing this block to the image file, an IndexEntry is created with:
 * - blockType = TapeFileBlock
 * - dataType = 0 (tape file blocks have no subtype)
 * - offset = file position where this header was written
 *
 * @note This block is optional. Tape images without file structure metadata can
 *       omit it, representing the tape as a simple linear sequence of blocks.
 *
 * @note If a tape has multiple partitions, files from all partitions are included
 *       in a single TapeFileBlock (distinguished by their Partition field).
 *
 * @note Empty tapes (no files) may still have a TapeFileHeader with entries=0,
 *       length=0, though such blocks are typically omitted.
 *
 * @warning The entries count must accurately reflect the number of TapeFileEntry
 *          structures in the block. Mismatches will cause parsing errors.
 *
 * @warning The CRC64 must be recalculated any time the entry data changes.
 *          Stale CRC values will cause integrity check failures.
 *
 * @see TapeFileEntry for the structure of individual file entries
 * @see TapePartitionHeader for partition structure metadata
 * @see BlockType for block type identifier constants
 */
typedef struct TapeFileHeader
{
    uint32_t identifier;  ///< Block type identifier. Must be set to BlockType::TapeFileBlock. This magic value allows
                          ///< parsers to identify the block type.
    uint32_t entries;     ///< Number of file entries following this header. Specifies how many TapeFileEntry structures
                          ///< are in this block. Valid range: 0 to 2^32-1.
    uint64_t length;      ///< Size of entry data in bytes (excluding this header). Calculated as: entries ×
                          ///< sizeof(TapeFileEntry). This is the number of bytes following the header.
    uint64_t crc64;  ///< CRC64-ECMA checksum of the entry data. Computed over the array of TapeFileEntry structures
                     ///< only. Does NOT include this header. Used for integrity verification.
} TapeFileHeader;

/**
 * @struct TapePartitionEntry
 * @brief Describes a single physical partition on a tape medium.
 *
 * Tape partitions are physical divisions of the tape storage area, each with its
 * own block address space. Partitioning allows a single tape to be logically
 * divided for organizational purposes, access control, or compatibility with
 * different systems.
 *
 * **Partition Number:**
 * The partition number uniquely identifies the partition on the tape. Most tapes
 * have a single partition (partition 0), but advanced formats like LTO, DLT, and
 * AIT support multiple partitions. The numbering scheme is format-specific:
 * - Single-partition tapes: Usually partition 0
 * - Multi-partition tapes: Typically 0-255, though most formats support fewer
 *
 * **Block Range:**
 * Each partition has an independent block address space starting from its FirstBlock.
 * The FirstBlock and LastBlock fields define the inclusive range of blocks in the
 * partition:
 * - FirstBlock: The first block number in this partition (often 0)
 * - LastBlock: The last block number in this partition (inclusive)
 *
 * **Block Address Spaces:**
 * Block addresses are local to each partition. For example:
 * - Partition 0: blocks 0-9999 (10,000 blocks)
 * - Partition 1: blocks 0-4999 (5,000 blocks)
 *
 * Block 0 in partition 0 is distinct from block 0 in partition 1. When referencing
 * a block, both the partition number and block number are required for uniqueness.
 *
 * **Partition Size:**
 * Partition size in blocks = (LastBlock - FirstBlock + 1)
 * Total tape capacity = sum of all partition sizes
 *
 * **Physical Organization:**
 * On physical tape, partitions are typically laid out sequentially:
 * - Partition 0 occupies the beginning of the tape
 * - Partition 1 follows partition 0
 * - And so on...
 *
 * Some formats allow non-sequential access to partitions via tape directory
 * structures or partition tables stored in a leader area.
 *
 * **Use Cases:**
 * - **Data segregation**: Separate system backups from user data
 * - **Multi-system use**: Different partitions for different operating systems
 * - **Tiered storage**: Fast-access partition for indices, bulk partition for data
 * - **Compatibility**: Legacy system may require specific partition layouts
 * - **Write protection**: Some formats allow per-partition write protection
 *
 * **Format Examples:**
 * - **LTO (Linear Tape-Open)**: Supports 2-4 partitions depending on generation
 * - **DLT (Digital Linear Tape)**: Typically 1 partition, some variants support more
 * - **DAT (Digital Audio Tape)**: Usually 1 partition
 * - **AIT (Advanced Intelligent Tape)**: Supports multiple partitions
 * - **QIC (Quarter-Inch Cartridge)**: Typically 1 partition
 *
 * @note Partition numbers are format-specific. Some formats use 0-based numbering,
 *       others use 1-based. The Aaru format preserves the original numbering.
 *
 * @note Not all tape formats support partitioning. Single-partition tapes are
 *       common and have one entry with Number=0.
 *
 * @note Partitions cannot overlap in the physical block space, though the logical
 *       block numbering within each partition may start from 0.
 *
 * @note Empty partitions (reserved but unused space) may exist with FirstBlock >
 *       LastBlock or with no files allocated to them.
 */
typedef struct TapePartitionEntry
{
    uint8_t  Number;  ///< Partition number (unique identifier for this partition). Identifies this partition among all
                      ///< partitions on the tape. Valid range: 0-255, though most tapes use 0-3.
    uint64_t FirstBlock;  ///< First block in the partition (inclusive). Starting block address for this partition's
                          ///< address space. Often 0, but format-dependent.
    uint64_t LastBlock;  ///< Last block in the partition (inclusive). Ending block address for this partition's address
                         ///< space. Must be ≥ FirstBlock. The partition contains all blocks from FirstBlock through
                         ///< LastBlock inclusive.
} TapePartitionEntry;

/**
 * @struct TapePartitionHeader
 * @brief Header for a tape partition metadata block containing partition layout information.
 *
 * This structure serves as the header for a TapePartitionBlock, which documents
 * the physical partitioning of the tape medium. The block consists of this fixed-size
 * header followed by a variable-length array of TapePartitionEntry structures (one
 * per partition).
 *
 * **Block Structure:**
 * ```
 * +---------------------------+
 * | TapePartitionHeader       | <- Fixed 21-byte header
 * +---------------------------+
 * | TapePartitionEntry 0      | <- First partition entry
 * +---------------------------+
 * | TapePartitionEntry 1      | <- Second partition entry
 * +---------------------------+
 * | ...                       |
 * +---------------------------+
 * | TapePartitionEntry (n-1)  | <- Last partition entry
 * +---------------------------+
 * ```
 *
 * **Purpose:**
 * The tape partition block enables:
 * - Quick identification of partition boundaries
 * - Validation of partition organization
 * - Understanding of tape physical layout
 * - Support for multi-partition tape formats
 * - Preservation of original partitioning scheme
 * - Correct interpretation of file locations (files reference partition numbers)
 *
 * **Identifier Field:**
 * The identifier field must be set to BlockType::TapePartitionBlock to indicate
 * this block type. This allows the Aaru format parser to recognize and correctly
 * interpret the block during image loading.
 *
 * **Entry Count:**
 * The entries field specifies the number of TapePartitionEntry structures that
 * follow the header. Valid range is 0-255 (uint8_t), though most tapes have 1-4
 * partitions:
 * - entries=0: No partitions (unusual, possibly empty tape)
 * - entries=1: Single partition (most common)
 * - entries>1: Multi-partition tape
 *
 * **Block Length:**
 * The length field contains the size in bytes of the data following this header
 * (i.e., the array of TapePartitionEntry structures). This is calculated as:
 *   length = entries × sizeof(TapePartitionEntry)
 *
 * The header itself is NOT included in the length value. Total block size is:
 *   total_size = sizeof(TapePartitionHeader) + length
 *
 * **CRC64 Checksum:**
 * The crc64 field contains a CRC64-ECMA checksum computed over the entry data
 * (the array of TapePartitionEntry structures, excluding this header). This
 * provides integrity verification to detect corruption in the partition table.
 * The CRC is calculated using the ECMA polynomial.
 *
 * **Partition Order:**
 * Entries should be ordered by partition number (ascending). This matches the
 * typical physical organization of tape partitions and facilitates efficient
 * lookup operations.
 *
 * **Alignment:**
 * When written to the Aaru image, this block is aligned to the image's block
 * alignment boundary (typically 2^blockAlignmentShift bytes). Padding may be
 * inserted before the block to achieve proper alignment.
 *
 * **Index Integration:**
 * After writing this block to the image file, an IndexEntry is created with:
 * - blockType = TapePartitionBlock
 * - dataType = 0 (tape partition blocks have no subtype)
 * - offset = file position where this header was written
 *
 * **Relationship to Files:**
 * TapePartitionHeader should be written before TapeFileHeader, as files reference
 * partitions by number. Readers should parse the partition block first to validate
 * partition numbers in file entries.
 *
 * **Single-Partition Tapes:**
 * Even for single-partition tapes, this block should be present with entries=1,
 * containing one TapePartitionEntry for partition 0. This provides consistency
 * and avoids special-case handling in readers.
 *
 * @note This block is optional but highly recommended for tape images. Without it,
 *       partition information must be inferred from file entries or tape format
 *       specifications.
 *
 * @note The sum of all partition sizes (in blocks) represents the total tape
 *       capacity, assuming partitions are contiguous and non-overlapping.
 *
 * @note Some tape formats allow dynamic repartitioning (changing partition
 *       boundaries). The Aaru image captures the partition layout at imaging time.
 *
 * @warning The entries count must accurately reflect the number of TapePartitionEntry
 *          structures in the block. Mismatches will cause parsing errors.
 *
 * @warning The CRC64 must be recalculated any time the entry data changes.
 *          Stale CRC values will cause integrity check failures.
 *
 * @warning Partition numbers referenced in TapeFileEntry must exist in this
 *          partition table. Orphaned file entries (referencing non-existent
 *          partitions) indicate a corrupt or incomplete image.
 *
 * @see TapePartitionEntry for the structure of individual partition entries
 * @see TapeFileHeader for file structure metadata
 * @see BlockType for block type identifier constants
 */
typedef struct TapePartitionHeader
{
    uint32_t identifier;  ///< Block type identifier. Must be set to BlockType::TapePartitionBlock. This magic value
                          ///< allows parsers to identify the block type.
    uint8_t  entries;     ///< Number of partition entries following this header. Specifies how many TapePartitionEntry
                          ///< structures are in this block. Valid range: 0-255. Most tapes have 1-4 partitions.
    uint64_t length;      ///< Size of entry data in bytes (excluding this header). Calculated as: entries ×
                          ///< sizeof(TapePartitionEntry). This is the number of bytes following the header.
    uint64_t crc64;       ///< CRC64-ECMA checksum of the entry data. Computed over the array of TapePartitionEntry
                          ///< structures only. Does NOT include this header. Used for integrity verification.
} TapePartitionHeader;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_TAPE_H

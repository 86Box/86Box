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

#ifndef LIBAARUFORMAT_INDEX_H
#define LIBAARUFORMAT_INDEX_H

#pragma pack(push, 1)

/** \file aaruformat/structs/index.h
 *  \brief On‑disk index block header and entry structures (versions 1, 2 and 3).
 *
 *  The index provides a directory of all blocks contained in an Aaru image. Each index block starts with
 *  a versioned header (IndexHeader / IndexHeader2 / IndexHeader3) followed by a contiguous array of
 *  fixed‑size \ref IndexEntry records. Version 3 adds support for hierarchical (chained / nested) subindexes.
 *
 *  Version mapping by block identifier (see \ref BlockType):
 *   - IndexBlock  (v1) -> \ref IndexHeader  followed by 16‑bit entry count entries.
 *   - IndexBlock2 (v2) -> \ref IndexHeader2 followed by 64‑bit entry count entries.
 *   - IndexBlock3 (v3) -> \ref IndexHeader3 with optional hierarchical subindex references.
 *
 *  CRC coverage & endianness:
 *   - The crc64 field stores a CRC64-ECMA over the entries array ONLY (header bytes are excluded).
 *   - For images with imageMajorVersion <= AARUF_VERSION_V1 a legacy writer byte-swapped the CRC; readers
 *     compensate (see verify_index_v1/v2/v3). The value in the header remains whatever was originally written.
 *
 *  Hierarchical (v3) behavior:
 *   - Entries whose blockType == IndexBlock3 refer to subindex blocks; readers recursively load and flatten.
 *   - IndexHeader3::previous can point to a preceding index segment (for append / incremental scenarios) or 0.
 *   - CRC of the main index does NOT cover subindex contents; each subindex has its own header + CRC.
 *
 *  Invariants / validation recommendations:
 *   - identifier must equal the expected BlockType variant for that version.
 *   - entries > 0 implies the entries array byte size == entries * sizeof(IndexEntry).
 *   - crc64 must match recomputed CRC64( entries array ) (after legacy byte swap handling if required).
 *   - For v3, if previous != 0 it should point to another IndexBlock3 header (optional best‑effort check).
 *
 *  Notes:
 *   - Structures are packed (1‑byte alignment). All multi-byte integers are little‑endian on disk.
 *   - The index does not store per-entry CRC; integrity relies on each individual block's own CRC plus the index CRC.
 *   - dataType in \ref IndexEntry is meaningful only for block types that carry typed data (e.g. DataBlock,
 * DumpHardwareBlock, etc.).
 *
 *  See also: verify_index_v1(), verify_index_v2(), verify_index_v3() for integrity procedures.
 */

/** \struct IndexHeader
 *  \brief Index header (version 1) for legacy images (identifier == IndexBlock).
 *
 *  Uses a 16‑bit entry counter limiting the number of indexable blocks in v1.
 */
typedef struct IndexHeader
{
    uint32_t identifier;  ///< Block identifier (must be BlockType::IndexBlock).
    uint16_t entries;     ///< Number of \ref IndexEntry records that follow immediately.
    uint64_t crc64;       ///< CRC64-ECMA of the entries array (legacy byte-swapped for early images).
} IndexHeader;

/** \struct IndexHeader2
 *  \brief Index header (version 2) with 64‑bit entry counter (identifier == IndexBlock2).
 *
 *  Enlarges the entry count field to 64 bits for large images; otherwise structurally identical to v1.
 */
typedef struct IndexHeader2
{
    uint32_t identifier;  ///< Block identifier (must be BlockType::IndexBlock2).
    uint64_t entries;     ///< Number of \ref IndexEntry records that follow immediately.
    uint64_t crc64;  ///< CRC64-ECMA of the entries array (legacy byte-swapped rule still applies for old versions).
} IndexHeader2;

/** \struct IndexHeader3
 *  \brief Index header (version 3) adding hierarchical chaining (identifier == IndexBlock3).
 *
 *  Supports flattened hierarchical indexes: entries referencing additional IndexBlock3 subindexes.
 *  The 'previous' pointer allows chaining earlier index segments (e.g., incremental append) enabling
 *  cumulative discovery without rewriting earlier headers.
 */
typedef struct IndexHeader3
{
    uint32_t identifier;  ///< Block identifier (must be BlockType::IndexBlock3).
    uint64_t entries;     ///< Number of \ref IndexEntry records that follow in this (sub)index block.
    uint64_t crc64;       ///< CRC64-ECMA of the local entries array (does NOT cover subindexes or previous chains).
    uint64_t previous;    ///< File offset of a previous IndexBlock3 header (0 if none / root segment).
} IndexHeader3;

/** \struct IndexEntry
 *  \brief Single index entry describing a block's type, (optional) data classification, and file offset.
 *
 *  Semantics by blockType (see \ref BlockType):
 *   - DataBlock / GeometryBlock / ChecksumBlock / etc.: dataType conveys specific stored data category (\ref DataType).
 *   - Deduplication (DDT) or Index blocks: dataType may be ignored or set to a sentinel.
 *   - IndexBlock3: this entry refers to a subindex; offset points to another IndexHeader3.
 */
typedef struct IndexEntry
{
    uint32_t blockType;  ///< Block identifier of the referenced block (value from \ref BlockType).
    uint16_t dataType;   ///< Data classification (value from \ref DataType) or unused for untyped blocks.
    uint64_t offset;     ///< Absolute byte offset in the image where the referenced block header begins.
} IndexEntry;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_INDEX_H

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

#ifndef LIBAARUFORMAT_DATA_H
#define LIBAARUFORMAT_DATA_H

#include <stdint.h>  // Fixed width integer types used in on-disk packed structs.

#pragma pack(push, 1)

/**
 * \file aaruformat/structs/data.h
 * \brief On-disk layout structures for data-bearing and geometry blocks.
 *
 * These packed structures describe the headers that precede variable-length payloads
 * inside blocks whose identifiers are enumerated in \ref BlockType.
 * All integer fields are stored little-endian on disk. The library currently assumes a
 * little-endian host; if ported to a big-endian architecture explicit byte swapping will be required.
 *
 * Layout of a data block (BlockType::DataBlock):
 *   BlockHeader (sizeof(BlockHeader) bytes)
 *   Compressed payload (cmpLength bytes)
 *
 * Payload decoding:
 *   - Apply the algorithm indicated by \ref BlockHeader::compression (\ref CompressionType) to the
 *     cmpLength bytes following the header to obtain exactly \ref BlockHeader::length bytes.
 *   - The uncompressed data MUST be an integer multiple of \ref BlockHeader::sectorSize.
 *   - A CRC64-ECMA is provided for both compressed (cmpCrc64) and uncompressed (crc64) forms to allow
 *     validation at either stage of the pipeline.
 *
 * Geometry block (BlockType::GeometryBlock) has a \ref GeometryBlockHeader followed by no additional
 * fixed payload in the current format version; it conveys legacy CHS-style logical geometry metadata.
 *
 * \warning These structs are packed; do not take their address and assume natural alignment.
 * \see BlockType
 * \see DataType
 * \see CompressionType
 */

/**
 * \struct BlockHeader
 * \brief Header preceding the compressed data payload of a data block (BlockType::DataBlock).
 *
 * Invariants:
 *  - cmpLength > 0 unless length == 0 (empty block)
 *  - length == 0 implies cmpLength == 0
 *  - If compression == CompressionType::None then cmpLength == length
 *  - length % sectorSize == 0
 *
 * Validation strategy (recommended for readers):
 *  1. Verify identifier == BlockType::DataBlock.
 *  2. Verify sectorSize is non-zero and a power-of-two or a commonly used size (512/1024/2048/4096/2352).
 *  3. Verify invariants above and CRCs after (de)compression.
 */
typedef struct BlockHeader
{
    uint32_t identifier;   ///< Block identifier, must be BlockType::DataBlock.
    uint16_t type;         ///< Logical data classification (value from \ref DataType).
    uint16_t compression;  ///< Compression algorithm used (value from \ref CompressionType).
    uint32_t sectorSize;   ///< Size in bytes of each logical sector represented in this block.
    uint32_t cmpLength;    ///< Size in bytes of the compressed payload immediately following this header.
    uint32_t length;       ///< Size in bytes of the uncompressed payload resulting after decompression.
    uint64_t cmpCrc64;     ///< CRC64-ECMA of the compressed payload (cmpLength bytes).
    uint64_t crc64;        ///< CRC64-ECMA of the uncompressed payload (length bytes).
} BlockHeader;

/**
 * \struct GeometryBlockHeader
 * \brief Legacy CHS style logical geometry metadata (BlockType::GeometryBlock).
 *
 * Total logical sectors implied by this header is cylinders * heads * sectorsPerTrack.
 * Sector size is not included here and must be derived from context (e.g., accompanying metadata
 * or defaulting to 512 for many block devices).
 */
typedef struct GeometryBlockHeader
{
    uint32_t identifier;       ///< Block identifier, must be BlockType::GeometryBlock.
    uint32_t cylinders;        ///< Number of cylinders.
    uint32_t heads;            ///< Number of heads (tracks per cylinder).
    uint32_t sectorsPerTrack;  ///< Number of sectors per track.
} GeometryBlockHeader;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_DATA_H

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

#ifndef LIBAARUFORMAT_CHECKSUM_H
#define LIBAARUFORMAT_CHECKSUM_H

#include <stdint.h>  // Fixed-width integer types for on-disk structures.

#pragma pack(push, 1)

/**
 * \file aaruformat/structs/checksum.h
 * \brief On-disk layout definitions for the checksum block (BlockType::ChecksumBlock).
 *
 * A checksum block stores one or more whole-image (user data) checksums. For optical media the
 * user data definition follows the format's raw sector rules (e.g. 2352-byte raw sector when available).
 *
 * Binary layout (all integers are little-endian, structure is packed):
 *
 *  +------------------------------+-------------------------------+
 *  | Field                        | Size (bytes)                  |
 *  +==============================+===============================+
 *  | ChecksumHeader               | sizeof(ChecksumHeader)=9      |
 *  |   identifier                 | 4 (BlockType::ChecksumBlock)  |
 *  |   length                     | 4 (payload bytes that follow)|
 *  |   entries                    | 1 (number of checksum entries)|
 *  +------------------------------+-------------------------------+
 *  | Repeated for each entry:                                     |
 *  |   ChecksumEntry              | sizeof(ChecksumEntry)=5       |
 *  |     type                     | 1 (ChecksumAlgorithm)         |
 *  |     length                   | 4 (digest length)             |
 *  |   digest bytes               | length                        |
 *  +------------------------------+-------------------------------+
 *
 * Thus, the payload size (ChecksumHeader.length) MUST equal the sum over all entries of:
 *   sizeof(ChecksumEntry) + entry.length.
 *
 * Typical digest lengths:
 *  - Md5: 16 bytes
 *  - Sha1: 20 bytes
 *  - Sha256: 32 bytes
 *  - SpamSum: variable length ASCII, NOT null-terminated on disk (a terminating '\0' may be appended in memory).
 *
 * \warning The structures are packed; never rely on host compiler default padding or directly casting from a buffer
 *          without ensuring correct endianness if porting to big-endian systems (current implementation assumes LE).
 *
 * \see BlockType
 * \see ChecksumAlgorithm
 */

/**
 * \struct ChecksumHeader
 * \brief Header that precedes the sequence of checksum entries for a checksum block.
 *
 * After this header, exactly \ref ChecksumHeader::length bytes follow containing \ref ChecksumHeader::entries
 * consecutive \ref ChecksumEntry records, each immediately followed by its digest payload.
 */
typedef struct ChecksumHeader
{
    uint32_t identifier;  ///< Block identifier, must be BlockType::ChecksumBlock.
    uint32_t length;      ///< Length in bytes of the payload (all entries + their digest data, excluding this header).
    uint8_t  entries;     ///< Number of checksum entries that follow in the payload.
} ChecksumHeader;

/**
 * \struct ChecksumEntry
 * \brief Per-checksum metadata immediately followed by the digest / signature bytes.
 *
 * For fixed-length algorithms the \ref length MUST match the known digest size. For SpamSum it is variable.
 * The bytes immediately following this structure (not null-terminated) constitute the digest and are exactly
 * \ref length bytes long.
 *
 * Order of entries is not mandated; readers should scan all entries and match by \ref type.
 */
typedef struct ChecksumEntry
{
    uint8_t  type;    ///< Algorithm used (value from \ref ChecksumAlgorithm).
    uint32_t length;  ///< Length in bytes of the digest that immediately follows this structure.
} ChecksumEntry;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_CHECKSUM_H

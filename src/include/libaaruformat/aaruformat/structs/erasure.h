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

#ifndef LIBAARUFORMAT_ERASURE_H
#define LIBAARUFORMAT_ERASURE_H

#include <stdint.h>

#pragma pack(push, 1)

/**
 * \file aaruformat/structs/erasure.h
 * \brief On-disk structures for erasure coding recovery data.
 *
 * These packed structures describe the Erasure Coding Map Block (ECMB),
 * stripe group descriptors, per-block entries within stripes, and the
 * recovery footer written at the very end of the file.
 *
 * All integer fields are stored little-endian on disk.
 */

/**
 * \struct ErasureCodingMapHeader
 * \brief Header for the Erasure Coding Map Block (BlockType::ErasureCodingMapBlock).
 *
 * The ECMB stores the master recovery map: stripe parameters and block-offset
 * mapping for all five protection groups (data, DDT-secondary, DDT-primary,
 * metadata, index). It is written after the index block and found via the
 * recovery footer, NOT via the index (since the index itself may need recovery).
 *
 * Payload (cmpLength bytes) contains an array of StripeGroupDescriptor followed
 * by per-stripe StripeDescriptor records, optionally compressed.
 */
typedef struct ErasureCodingMapHeader
{
    uint32_t identifier;       ///< Block identifier, must be BlockType::ErasureCodingMapBlock (0x424D4345).
    uint8_t  algorithm;        ///< Erasure coding algorithm (ErasureCodingAlgorithm).
    uint8_t  stripeGroupCount; ///< Number of stripe groups in payload (typically 5).
    uint16_t compression;      ///< Compression algorithm for the mapping payload (CompressionType).
    uint64_t cmpLength;        ///< Size in bytes of the compressed mapping payload.
    uint64_t length;           ///< Size in bytes of the uncompressed mapping payload.
    uint64_t cmpCrc64;         ///< CRC64-ECMA of the compressed mapping payload.
    uint64_t crc64;            ///< CRC64-ECMA of the uncompressed mapping payload.
} ErasureCodingMapHeader;

/**
 * \struct StripeGroupDescriptor
 * \brief Describes one protection group within the ECMB payload.
 *
 * Each group has its own K, M, shard size, and interleave depth.
 * Followed by stripeCount StripeDescriptor records.
 */
typedef struct StripeGroupDescriptor
{
    uint8_t  groupType;       ///< Protection group type (ErasureCodingGroupType).
    uint16_t K;               ///< Number of data blocks per stripe.
    uint16_t M;               ///< Number of parity blocks per stripe.
    uint32_t shardSize;       ///< Fixed shard size in bytes (max possible on-disk block size for this group).
    uint32_t stripeCount;     ///< Number of stripes in this group.
    uint16_t interleaveDepth; ///< Interleave depth D (K for full interleave, 1 for consecutive).
} StripeGroupDescriptor;

/**
 * \struct StripeDataBlockEntry
 * \brief Per-data-block metadata within a stripe descriptor.
 *
 * Stores the file offset, actual on-disk size, and an independent CRC64
 * covering the on-disk bytes (header + compressed payload) zero-padded to
 * shardSize. This CRC64 is independent of BlockHeader.cmpCrc64 — it can
 * detect corruption even when the BlockHeader itself is garbled.
 */
typedef struct StripeDataBlockEntry
{
    uint64_t offset;     ///< Absolute file offset of the block.
    uint32_t onDiskSize; ///< Actual on-disk bytes (sizeof(header) + cmpLength).
    uint64_t shardCrc64; ///< CRC64-ECMA of on-disk bytes zero-padded to shardSize.
} StripeDataBlockEntry;

/**
 * \struct StripeParityBlockEntry
 * \brief Per-parity-block metadata within a stripe descriptor.
 */
typedef struct StripeParityBlockEntry
{
    uint64_t offset; ///< Absolute file offset of the parity DBLK.
} StripeParityBlockEntry;

/**
 * \struct AaruRecoveryFooter
 * \brief Recovery footer written at the very end of the file (last 160 bytes).
 *
 * Enables locating the ECMB and recovering from a corrupted header or index.
 * Found by reading the last 160 bytes of the file and checking footerMagic.
 *
 * Recovery chain:
 *  - Normal: header -> indexOffset -> index -> ECMB (via ecmbOffset)
 *  - Header corrupt: footer backupHeader -> indexOffset -> index -> ECMB
 *  - Index corrupt: footer ecmbOffset -> ECMB -> index parity -> reconstruct
 *  - ECMB corrupt: duplicate ECMB at ecmbOffset + ecmbLength (aligned)
 */
typedef struct AaruRecoveryFooter
{
    uint64_t     ecmbOffset;   ///< Absolute file offset of the primary ECMB.
    uint64_t     ecmbLength;   ///< Total on-disk size of the ECMB (header + payload).
    uint64_t     headerCrc64;  ///< CRC64-ECMA of the original AaruHeaderV2 (147 bytes at offset 0).
    AaruHeaderV2 backupHeader; ///< Complete copy of AaruHeaderV2 from file offset 0.
    uint64_t     footerMagic;  ///< Must be AARU_RECOVERY_FOOTER_MAGIC (0x52464D4345525641).
} AaruRecoveryFooter;

#pragma pack(pop)

#endif /* LIBAARUFORMAT_ERASURE_H */

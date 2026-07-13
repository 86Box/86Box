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

#ifndef LIBAARUFORMAT_ERASURE_INTERNAL_H
#define LIBAARUFORMAT_ERASURE_INTERNAL_H

#include "aaruformat/context.h"
#include "aaruformat/structs/data.h"

/* ---- Write path ---- */

void ec_accumulate_data_block(aaruformat_context *ctx, const BlockHeader *block_header, const uint8_t *lzma_props,
                              const uint8_t *payload, uint32_t payload_size, uint64_t file_offset);

void ec_flush_data_stripe(aaruformat_context *ctx, uint32_t slot);

void ec_finalize(aaruformat_context *ctx);

/* ---- Read path ---- */

/**
 * @brief Try to load the ECMB from the recovery footer at EOF.
 *
 * Reads the last 160 bytes, checks for footerMagic, parses ECMB,
 * and populates the ec_read_* fields and block lookup hashmap.
 * Called from aaruf_open().
 */
void ec_load_ecmb(aaruformat_context *ctx);

/**
 * @brief Attempt to recover a data block that failed decompression or CRC verification.
 *
 * Looks up the block's stripe via the ECMB, reads surviving stripe members + parity,
 * RS-decodes the erased shard, and decompresses the recovered block.
 *
 * @param ctx Context with ec_recovery_available == true.
 * @param block_offset File offset of the corrupted block.
 * @param offset Sector offset within the block.
 * @param data Output buffer for the recovered sector.
 * @param length Output: bytes written to data.
 * @param sector_status Sector status from DDT.
 * @return AARUF_STATUS_OK on success, negative error code on failure.
 */
int32_t ec_recover_data_block(aaruformat_context *ctx, uint64_t block_offset, uint64_t offset,
                              uint8_t *data, uint32_t *length, uint8_t sector_status);

/**
 * @brief Attempt to recover a raw block at a given file offset using a specified stripe group.
 *
 * Reads surviving stripe members + parity, RS-decodes the erased shard,
 * returns the recovered raw on-disk bytes.
 *
 * @param ctx Context.
 * @param block_offset File offset of the corrupted block.
 * @param recovered_data Output: malloc'd buffer with recovered bytes (caller frees). NULL on failure.
 * @param recovered_size Output: size of recovered data.
 * @param stripes EcReadStripe array for the group.
 * @param stripe_count Number of stripes.
 * @param lookup Block lookup hashmap for the group.
 * @param group_K K for this group.
 * @param group_M M for this group.
 * @param group_shard_size Shard size for this group.
 * @return 0 on success, negative on failure.
 */
int32_t ec_recover_raw_block(aaruformat_context *ctx, uint64_t block_offset,
                             uint8_t **recovered_data, uint32_t *recovered_size,
                             void *stripes, uint32_t stripe_count, void *lookup,
                             uint16_t group_K, uint16_t group_M, uint32_t group_shard_size);

/**
 * @brief Attempt to recover a metadata/media-tag block at a given file offset.
 *
 * Convenience wrapper around ec_recover_raw_block using the metadata group.
 */
int32_t ec_recover_meta_block(aaruformat_context *ctx, uint64_t block_offset,
                              uint8_t **recovered_data, uint32_t *recovered_size);

/**
 * @brief Attempt to recover a DDT secondary block at a given file offset.
 */
int32_t ec_recover_ddt_block(aaruformat_context *ctx, uint64_t block_offset,
                             uint8_t **recovered_data, uint32_t *recovered_size);

/* ---- Cleanup ---- */

void ec_free(aaruformat_context *ctx);

#endif /* LIBAARUFORMAT_ERASURE_INTERNAL_H */

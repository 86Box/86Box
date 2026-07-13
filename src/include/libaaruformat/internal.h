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

#ifndef LIBAARUFORMAT_INTERNAL_H
#define LIBAARUFORMAT_INTERNAL_H

/** @brief Clamp num_threads to LZMA's valid range [1, 2]. */
#define LZMA_THREADS(ctx) ((ctx)->num_threads > 1 ? 2 : 1)

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "aaruformat/context.h"

#if defined(AARU_USE_WIN32_FILEIO64)
#define AARU_FSEEK _fseeki64
#define AARU_FTELL _ftelli64
#elif defined(AARU_USE_POSIX_FILEIO64)
#define AARU_FSEEK fseeko
#define AARU_FTELL ftello
#else
#define AARU_FSEEK fseek
#define AARU_FTELL ftell
#endif

typedef int64_t aaru_off_t;

/* Seek using a 64-bit offset. Returns 0 on success, non-zero on failure, just
 * like fseek()/_fseeki64(). Use aaruf_ftell() to retrieve the current offset. */
static inline int aaruf_fseek(FILE *stream, aaru_off_t offset, int origin)
{
    return AARU_FSEEK(stream, offset, origin);
}

/* Return the current file offset as a signed 64-bit value. */
static inline aaru_off_t aaruf_ftell(FILE *stream)
{
    return (aaru_off_t)AARU_FTELL(stream);
}

#ifndef AARU_NO_FILEIO_REMAP
#define fseek(stream, offset, origin) aaruf_fseek((stream), (aaru_off_t)(offset), (origin))
#define ftell(stream) aaruf_ftell((stream))
#endif

#include "utarray.h"

UT_array *process_index_v1(aaruformat_context *ctx);
int32_t   verify_index_v1(aaruformat_context *ctx);
UT_array *process_index_v2(aaruformat_context *ctx);
int32_t   verify_index_v2(aaruformat_context *ctx);
UT_array *process_index_v3(aaruformat_context *ctx);
int32_t   verify_index_v3(aaruformat_context *ctx);
int32_t   process_data_block(aaruformat_context *ctx, IndexEntry *entry);
int32_t   process_ddt_v1(aaruformat_context *ctx, IndexEntry *entry, bool *found_user_data_ddt);
int32_t   process_ddt_v2(aaruformat_context *ctx, IndexEntry *entry, bool *found_user_data_ddt);
void      process_metadata_block(aaruformat_context *ctx, const IndexEntry *entry);
void      process_geometry_block(aaruformat_context *ctx, const IndexEntry *entry);
void      process_tracks_block(aaruformat_context *ctx, const IndexEntry *entry);
void      process_cicm_block(aaruformat_context *ctx, const IndexEntry *entry);
void      process_aaru_metadata_json_block(aaruformat_context *ctx, const IndexEntry *entry);
void      process_dumphw_block(aaruformat_context *ctx, const IndexEntry *entry);
void      process_checksum_block(aaruformat_context *ctx, const IndexEntry *entry);
void      process_tape_files_block(aaruformat_context *ctx, const IndexEntry *entry);
void      process_tape_partitions_block(aaruformat_context *ctx, const IndexEntry *entry);
void      process_flux_data_block(aaruformat_context *ctx, const IndexEntry *entry);
int32_t   flux_map_rebuild_from_entries(aaruformat_context *ctx);
int32_t decode_ddt_entry_v1(aaruformat_context *ctx, uint64_t sector_address, uint64_t *offset, uint64_t *block_offset,
                            uint8_t *sector_status);
int32_t decode_ddt_entry_v2(aaruformat_context *ctx, uint64_t sector_address, bool negative, uint64_t *offset,
                            uint64_t *block_offset, uint8_t *sector_status);
int32_t decode_ddt_single_level_v2(aaruformat_context *ctx, uint64_t sector_address, bool negative, uint64_t *offset,
                                   uint64_t *block_offset, uint8_t *sector_status);
int32_t decode_ddt_multi_level_v2(aaruformat_context *ctx, uint64_t sector_address, bool negative, uint64_t *offset,
                                  uint64_t *block_offset, uint8_t *sector_status);
bool    set_ddt_entry_v2(aaruformat_context *ctx, uint64_t sector_address, bool negative, uint64_t offset,
                         uint64_t block_offset, uint8_t sector_status, uint64_t *ddt_entry);
bool    set_ddt_single_level_v2(aaruformat_context *ctx, uint64_t sector_address, bool negative, uint64_t offset,
                                uint64_t block_offset, uint8_t sector_status, uint64_t *ddt_entry);
bool    set_ddt_multi_level_v2(aaruformat_context *ctx, uint64_t sector_address, bool negative, uint64_t offset,
                               uint64_t block_offset, uint8_t sector_status, uint64_t *ddt_entry);
bool    set_ddt_tape(aaruformat_context *ctx, uint64_t sector_address, uint64_t offset, uint64_t block_offset,
                     uint8_t sector_status, uint64_t *ddt_entry);
aaru_options parse_options(const char *options, bool *table_shift_found);
uint64_t     get_filetime_uint64();
int32_t      aaruf_close_current_block(aaruformat_context *ctx);
int32_t      aaruf_finalize_write(aaruformat_context *ctx);
int          compare_extents(const void *a, const void *b);
void         generate_random_bytes(uint8_t *buffer, size_t length);

#endif  // LIBAARUFORMAT_INTERNAL_H

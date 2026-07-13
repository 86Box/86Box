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

#ifndef LIBAARUFORMAT_FLAC_H
#define LIBAARUFORMAT_FLAC_H

typedef struct
{
    const uint8_t *src_buffer;
    size_t         src_len;
    size_t         src_pos;
    uint8_t       *dst_buffer;
    size_t         dst_len;
    size_t         dst_pos;
    uint8_t        error;
} aaru_flac_ctx;

#endif  // LIBAARUFORMAT_FLAC_H

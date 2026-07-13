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

/**
 * @file lisa_tag.h
 * @brief Structure definitions and conversion/serialization function declarations for Lisa family disk tags.
 *
 * Provides compact C representations for on-disk tag metadata used by Sony, Profile and Priam storage devices
 * in the Apple Lisa ecosystem. See lisa_tag.c for detailed description of field semantics and conversion rules.
 */

#ifndef LIBAARUFORMAT_LISA_TAG_H
#define LIBAARUFORMAT_LISA_TAG_H

#include <stdint.h>

#pragma pack(push, 1)

typedef struct sony_tag
{
    uint16_t version;       ///< 0x00, Lisa OS version number
    uint8_t  kind     : 2;  ///< 0x02 bits 7 to 6, kind of info in this block
    uint8_t  reserved : 6;  ///< 0x02 bits 5 to 0, reserved
    uint8_t  volume;        ///< 0x03, disk volume number
    int16_t  file_id;       ///< 0x04, file ID
    uint16_t rel_page;      ///< 0x06, relative page number
    uint16_t next_block;    ///< 0x08, next block, 0x7FF if it's last block, 0x8000 set if block is valid
    uint16_t prev_block;    ///< 0x0A, previous block, 0x7FF if it's first block
} sony_tag;

typedef struct profile_tag
{
    uint16_t version;          ///< 0x00, Lisa OS version number
    uint8_t  kind     : 2;     ///< 0x02 bits 7 to 6, kind of info in this block
    uint8_t  reserved : 6;     ///< 0x02 bits 5 to 0, reserved
    uint8_t  volume;           ///< 0x03, disk volume number
    int16_t  file_id;          ///< 0x04, file ID
    uint8_t  valid_chk  : 1;   ///< 0x06 bit 7, checksum valid?
    uint16_t used_bytes : 15;  ///< 0x06 bits 6 to 0, used bytes in block
    uint32_t abs_page;         ///< 0x08, 3 bytes, absolute page number
    uint8_t  checksum;         ///< 0x0B, checksum of data
    uint16_t rel_page;         ///< 0x0C, relative page number
    uint32_t next_block;       ///< 0x0E, 3 bytes, next block, 0xFFFFFF if it's last block
    uint32_t prev_block;       ///< 0x11, 3 bytes, previous block, 0xFFFFFF if it's first block
} profile_tag;

typedef struct priam_tag
{
    uint16_t version;          ///< 0x00, Lisa OS version number
    uint8_t  kind     : 2;     ///< 0x02 bits 7 to 6, kind of info in this block
    uint8_t  reserved : 6;     ///< 0x02 bits 5 to 0, reserved
    uint8_t  volume;           ///< 0x03, disk volume number
    int16_t  file_id;          ///< 0x04, file ID
    uint8_t  valid_chk  : 1;   ///< 0x06 bit 7, checksum valid?
    uint16_t used_bytes : 15;  ///< 0x06 bits 6 to 0, used bytes in block
    uint32_t abs_page;         ///< 0x08, 3 bytes, absolute page number
    uint8_t  checksum;         ///< 0x0B, checksum of data
    uint16_t rel_page;         ///< 0x0C, relative page number
    uint32_t next_block;       ///< 0x0E, 3 bytes, next block, 0xFFFFFF if it's last block
    uint32_t prev_block;       ///< 0x11, 3 bytes, previous block, 0xFFFFFF if it's first block
    uint32_t disk_size;        ///< 0x14, disk size
} priam_tag;

#pragma pack(pop)

sony_tag    bytes_to_sony_tag(const uint8_t *bytes);
profile_tag bytes_to_profile_tag(const uint8_t *bytes);
priam_tag   bytes_to_priam_tag(const uint8_t *bytes);
uint8_t    *sony_tag_to_bytes(sony_tag tag);
uint8_t    *profile_tag_to_bytes(profile_tag tag);
uint8_t    *priam_tag_to_bytes(priam_tag tag);

profile_tag sony_tag_to_profile(sony_tag tag);
profile_tag priam_tag_to_profile(priam_tag tag);
priam_tag   sony_tag_to_priam(sony_tag tag);
priam_tag   profile_tag_to_priam(profile_tag tag);
sony_tag    profile_tag_to_sony(profile_tag tag);
sony_tag    priam_tag_to_sony(priam_tag tag);

#endif  // LIBAARUFORMAT_LISA_TAG_H

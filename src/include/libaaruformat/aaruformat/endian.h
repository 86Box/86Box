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

#ifndef LIBAARUFORMAT_ENDIAN_H
#define LIBAARUFORMAT_ENDIAN_H

#ifdef _MSC_VER

#include <stdlib.h>
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)

#elif defined(__APPLE__)

// Mac OS X / Darwin features
#include <libkern/OSByteOrder.h>

#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)

#elif defined(__sun) || defined(sun)

#include <sys/byteorder.h>
#define bswap_16(x) BSWAP_16(x)
#define bswap_32(x) BSWAP_32(x)
#define bswap_64(x) BSWAP_64(x)

#elif defined(__FreeBSD__)

#include <sys/endian.h>
#define bswap_16(x) bswap16(x)
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)

#elif defined(__OpenBSD__)

#include <sys/types.h>
#define bswap_16(x) swap16(x)
#define bswap_32(x) swap32(x)
#define bswap_64(x) swap64(x)

#elif defined(__NetBSD__)

#include <machine/bswap.h>
#include <sys/types.h>
#if defined(__BSWAP_RENAME) && !defined(__bswap_32)
#define bswap_16(x) bswap16(x)
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)
#endif

#elif defined(__linux__)

#include <byteswap.h>

#else

#include <stdint.h>

#define bswap_16(x) ((uint16_t)((((uint16_t)(x) & 0xff00) >> 8) | (((uint16_t)(x) & 0x00ff) << 8)))
#define bswap_32(x)                                                                          \
    ((uint32_t)((((uint32_t)(x) & 0xff000000) >> 24) | (((uint32_t)(x) & 0x00ff0000) >> 8) | \
                (((uint32_t)(x) & 0x0000ff00) << 8) | (((uint32_t)(x) & 0x000000ff) << 24)))
#define bswap_64(x)                                                                                                 \
    ((uint64_t)((((uint64_t)(x) & 0xff00000000000000ULL) >> 56) | (((uint64_t)(x) & 0x00ff000000000000ULL) >> 40) | \
                (((uint64_t)(x) & 0x0000ff0000000000ULL) >> 24) | (((uint64_t)(x) & 0x000000ff00000000ULL) >> 8) |  \
                (((uint64_t)(x) & 0x00000000ff000000ULL) << 8) | (((uint64_t)(x) & 0x0000000000ff0000ULL) << 24) |  \
                (((uint64_t)(x) & 0x000000000000ff00ULL) << 40) | (((uint64_t)(x) & 0x00000000000000ffULL) << 56)))

#endif

#endif  // LIBAARUFORMAT_ENDIAN_H

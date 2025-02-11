/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Various definitions for portable byte-swapping.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          neozeed,
 *
 *          Copyright 2017-2018 Fred N. van Kempen.
 *          Copyright 2016-2018 neozeed.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */

#ifndef __NetBSD__

#ifndef BSWAP_H
#define BSWAP_H

#include <stdint.h>

#define bswap_16(x)					\
	((uint16_t)((((x) & 0x00ffu) << 8) |		\
		    (((x) & 0xff00u) >> 8)))

#define bswap_32(x)					\
	((uint32_t)((((x) & 0x000000fful) << 24) |	\
		    (((x) & 0x0000ff00ul) <<  8) |	\
		    (((x) & 0x00ff0000ul) >>  8) |	\
		    (((x) & 0xff000000ul) >> 24)))

# define bswap_64(x)							\
        ((uint64_t)((((x) & 0x00000000000000ffull) << 56) |		\
		    (((x) & 0x000000000000ff00ull) << 40) |		\
		    (((x) & 0x0000000000ff0000ull) << 24) |		\
		    (((x) & 0x00000000ff000000ull) <<  8) |		\
		    (((x) & 0x000000ff00000000ull) >>  8) |		\
		    (((x) & 0x0000ff0000000000ull) >> 24) |		\
		    (((x) & 0x00ff000000000000ull) >> 40) |		\
		    (((x) & 0xff00000000000000ull) >> 56)))

static __inline uint16_t
bswap16(uint16_t x)
{
#if defined (__GNUC__) || defined (__clang__)
    return __builtin_bswap16(x);
#else
    return bswap_16(x);
#endif
}

static __inline uint32_t
bswap32(uint32_t x)
{
#if defined (__GNUC__) || defined (__clang__)
    return __builtin_bswap32(x);
#else
    return bswap_32(x);
#endif
}

static __inline uint64_t
bswap64(uint64_t x)
{
#if defined (__GNUC__) || defined (__clang__)
    return __builtin_bswap64(x);
#else
    return bswap_16(x);
#endif
}

static __inline void
bswap16s(uint16_t *s)
{
    *s = bswap16(*s);
}

static __inline void
bswap32s(uint32_t *s)
{
    *s = bswap32(*s);
}

static __inline void
bswap64s(uint64_t *s)
{
    *s = bswap64(*s);
}

#if defined(WORDS_BIGENDIAN)
#    define be_bswap(v, size) (v)
#    define le_bswap(v, size) bswap##size(v)
#    define be_bswaps(v, size)
#    define le_bswaps(p, size) *p = bswap##size(*p);
#else
#    define le_bswap(v, size) (v)
#    define be_bswap(v, size) bswap##size(v)
#    define le_bswaps(v, size)
#    define be_bswaps(p, size) *p = bswap##size(*p);
#endif

#define CPU_CONVERT(endian, size, type)                            \
    static __inline type endian##size##_to_cpu(type v)             \
    {                                                              \
        return endian##_bswap(v, size);                            \
    }                                                              \
                                                                   \
    static __inline type cpu_to_##endian##size(type v)             \
    {                                                              \
        return endian##_bswap(v, size);                            \
    }                                                              \
                                                                   \
    static __inline void endian##size##_to_cpus(UNUSED(type *p))   \
    {                                                              \
        endian##_bswaps(p, size)                                   \
    }                                                              \
                                                                   \
    static __inline void cpu_to_##endian##size##s(UNUSED(type *p)) \
    {                                                              \
        endian##_bswaps(p, size)                                   \
    }                                                              \
                                                                   \
    static __inline type endian##size##_to_cpup(const type *p)     \
    {                                                              \
        return endian##size##_to_cpu(*p);                          \
    }                                                              \
                                                                   \
    static __inline void cpu_to_##endian##size##w(type *p, type v) \
    {                                                              \
        *p = cpu_to_##endian##size(v);                             \
    }

CPU_CONVERT(be, 16, uint16_t)
CPU_CONVERT(be, 32, uint32_t)
CPU_CONVERT(be, 64, uint64_t)

CPU_CONVERT(le, 16, uint16_t)
CPU_CONVERT(le, 32, uint32_t)
CPU_CONVERT(le, 64, uint64_t)

/* unaligned versions (optimized for frequent unaligned accesses)*/

#if defined(__i386__) || defined(__powerpc__)
#    define cpu_to_le16wu(p, v) cpu_to_le16w(p, v)
#    define cpu_to_le32wu(p, v) cpu_to_le32w(p, v)
#    define le16_to_cpupu(p)    le16_to_cpup(p)
#    define le32_to_cpupu(p)    le32_to_cpup(p)
#    define cpu_to_be16wu(p, v) cpu_to_be16w(p, v)
#    define cpu_to_be32wu(p, v) cpu_to_be32w(p, v)

#else

static __inline void
cpu_to_le16wu(uint16_t *p, uint16_t v)
{
    uint8_t *p1 = (uint8_t *) p;

    p1[0] = v & 0xff;
    p1[1] = v >> 8;
}

static __inline void
cpu_to_le32wu(uint32_t *p, uint32_t v)
{
    uint8_t *p1 = (uint8_t *) p;

    p1[0] = v;
    p1[1] = v >> 8;
    p1[2] = v >> 16;
    p1[3] = v >> 24;
}

static __inline uint16_t
le16_to_cpupu(const uint16_t *p)
{
    const uint8_t *p1 = (const uint8_t *) p;
    return p1[0] | (p1[1] << 8);
}

static __inline uint32_t
le32_to_cpupu(const uint32_t *p)
{
    const uint8_t *p1 = (const uint8_t *) p;
    return p1[0] | (p1[1] << 8) | (p1[2] << 16) | (p1[3] << 24);
}

static __inline void
cpu_to_be16wu(uint16_t *p, uint16_t v)
{
    uint8_t *p1 = (uint8_t *) p;

    p1[0] = v >> 8;
    p1[1] = v & 0xff;
}

static __inline void
cpu_to_be32wu(uint32_t *p, uint32_t v)
{
    uint8_t *p1 = (uint8_t *) p;

    p1[0] = v >> 24;
    p1[1] = v >> 16;
    p1[2] = v >> 8;
    p1[3] = v;
}

#endif

#ifdef WORDS_BIGENDIAN
#    define cpu_to_32wu cpu_to_be32wu
#else
#    define cpu_to_32wu cpu_to_le32wu
#endif

#undef le_bswap
#undef be_bswap
#undef le_bswaps
#undef be_bswaps

#endif /*BSWAP_H*/

#endif

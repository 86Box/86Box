/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- 64-bit FNV-1a hash over a byte buffer.
 *
 *          The span JIT's block cache and the draw-state census both hash
 *          a whole rage128_draw_state_t with it to pick out candidate
 *          entries before comparing the full struct with memcmp.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEO_ATI_RAGE128_FNV_H
#define VIDEO_ATI_RAGE128_FNV_H

#include <stddef.h>
#include <stdint.h>

static inline uint64_t
r128_fnv1a(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint64_t       h = 0xcbf29ce484222325ull;

    for (size_t i = 0; i < len; i++)
        h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

#endif /* VIDEO_ATI_RAGE128_FNV_H */

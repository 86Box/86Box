/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PRAMIN, instance memory.
 *
 *          The 1 MB window at 0x700000 into the top of VRAM, where the
 *          hash table, the FIFO contexts and every object live. Offset x
 *          of the window is VRAM (x ^ ramin_flip), see nv4_init.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/nv/vid_nv4.h>

static inline uint32_t
nv4_ramin_addr(const nv4_t *nv4, uint32_t off)
{
    return ((off & 0xfffff) ^ nv4->ramin_flip) & nv4->vram_mask;
}

static inline void
nv4_ramin_dirty(nv4_t *nv4, uint32_t vaddr)
{
    nv4->svga.changedvram[vaddr >> 12] = nv4->svga.monitor->mon_changeframecount;
}

uint8_t
nv4_ramin_read8(nv4_t *nv4, uint32_t off)
{
    return nv4->svga.vram[nv4_ramin_addr(nv4, off)];
}

uint16_t
nv4_ramin_read16(nv4_t *nv4, uint32_t off)
{
    return *(uint16_t *) &nv4->svga.vram[nv4_ramin_addr(nv4, off & ~1)];
}

uint32_t
nv4_ramin_read32(nv4_t *nv4, uint32_t off)
{
    return *(uint32_t *) &nv4->svga.vram[nv4_ramin_addr(nv4, off & ~3)];
}

void
nv4_ramin_write8(nv4_t *nv4, uint32_t off, uint8_t val)
{
    uint32_t a = nv4_ramin_addr(nv4, off);

    nv4->svga.vram[a] = val;
    nv4_ramin_dirty(nv4, a);
}

void
nv4_ramin_write16(nv4_t *nv4, uint32_t off, uint16_t val)
{
    uint32_t a = nv4_ramin_addr(nv4, off & ~1);

    *(uint16_t *) &nv4->svga.vram[a] = val;
    nv4_ramin_dirty(nv4, a);
}

void
nv4_ramin_write32(nv4_t *nv4, uint32_t off, uint32_t val)
{
    uint32_t a = nv4_ramin_addr(nv4, off & ~3);

    *(uint32_t *) &nv4->svga.vram[a] = val;
    nv4_ramin_dirty(nv4, a);
}

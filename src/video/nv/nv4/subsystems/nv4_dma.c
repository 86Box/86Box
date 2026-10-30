/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- DMA objects.
 *
 *          A DMA object is a block in instance memory that describes a
 *          window of memory the chip may reach: the DMA pusher reads its
 *          pushbuffer through one, PGRAPH writes notifiers and moves data
 *          through others. The layout (envytools "NV4 DMA objects"):
 *
 *            word 0  class 11:0 (0x02 read, 0x03 write, 0x3d read/write),
 *                    page table present 12, linear 13, target 17:16
 *                    (0 VRAM, 2 PCI, 3 AGP), adjust 31:20 (the byte offset
 *                    of the window in its first page)
 *            word 1  limit, the last valid offset
 *            word 2+ page table entries: frame address 31:12, writable 1,
 *                    present 0. A linear object needs only the first.
 *
 *          PCI and AGP addresses go to the bus (the chipset maps the AGP
 *          aperture); VRAM addresses to the framebuffer.
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
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/nv/vid_nv4.h>

#define NV4_DMA_TARGET_VRAM 0
#define NV4_DMA_TARGET_PCI  2
#define NV4_DMA_TARGET_AGP  3

uint32_t
nv4_dma_class(nv4_t *nv4, uint32_t inst)
{
    return nv4_ramin_read32(nv4, inst << 4) & 0xfff;
}

uint32_t
nv4_dma_limit(nv4_t *nv4, uint32_t inst)
{
    return nv4_ramin_read32(nv4, (inst << 4) + 4);
}

/* The bytes from <offset> to the end of its page. The window starts
   <adjust> bytes into its first page, so its pages begin at offsets
   that are not multiples of 4 KB: a transfer split at offset multiples
   of 4 KB runs off the end of a page into whatever frame follows it in
   memory, not the window's next page. */
uint32_t
nv4_dma_page_left(nv4_t *nv4, uint32_t inst, uint32_t offset)
{
    return 0x1000 - ((offset + (nv4_ramin_read32(nv4, inst << 4) >> 20)) & 0xfff);
}

/* The address of byte <offset> of the window. */
int
nv4_dma_translate(nv4_t *nv4, uint32_t inst, uint32_t offset, int write, uint32_t *addr, int *target)
{
    uint32_t base  = inst << 4;
    uint32_t w0    = nv4_ramin_read32(nv4, base);
    uint32_t limit = nv4_ramin_read32(nv4, base + 4);
    uint32_t cls   = w0 & 0xfff;
    uint32_t o;
    uint32_t pte;

    if ((cls != 0x02) && (cls != 0x03) && (cls != 0x3d))
        return NV4_DMA_FAULT;
    if (offset > limit)
        return NV4_DMA_FAULT;

    o = offset + (w0 >> 20);
    if (w0 & (1u << 13))
        pte = nv4_ramin_read32(nv4, base + 8); /* linear: one frame address */
    else
        pte = nv4_ramin_read32(nv4, base + 8 + ((o >> 12) << 2));
    /* Write protection (PTE bit 1, class 0x02) is not enforced. */
    (void) write;

    if (w0 & (1u << 13))
        *addr = (pte & 0xfffff000) + o;
    else
        *addr = (pte & 0xfffff000) + (o & 0xfff);
    *target = (w0 >> 16) & 3;
    if (!(pte & 1) && (*target != NV4_DMA_TARGET_VRAM)) {
        nv4->dbg_dma_np++;
        nv4->dbg_np_inst   = inst;
        nv4->dbg_np_offset = offset;
        nv4->dbg_np_pte    = pte;
    }
    return NV4_DMA_OK;
}

int
nv4_dma_read(nv4_t *nv4, uint32_t inst, uint32_t offset, uint8_t *buf, uint32_t len)
{
    while (len) {
        uint32_t addr;
        int      target;
        uint32_t chunk = nv4_dma_page_left(nv4, inst, offset);

        if (chunk > len)
            chunk = len;
        if (nv4_dma_translate(nv4, inst, offset, 0, &addr, &target) != NV4_DMA_OK)
            return NV4_DMA_FAULT;
        if (target == NV4_DMA_TARGET_VRAM) {
            for (uint32_t i = 0; i < chunk; i++)
                buf[i] = nv4->svga.vram[(addr + i) & nv4->vram_mask];
        } else
            dma_bm_read(addr, buf, chunk, 4);
        buf += chunk;
        offset += chunk;
        len -= chunk;
    }
    return NV4_DMA_OK;
}

int
nv4_dma_write(nv4_t *nv4, uint32_t inst, uint32_t offset, const uint8_t *buf, uint32_t len)
{
    while (len) {
        uint32_t addr;
        int      target;
        uint32_t chunk = nv4_dma_page_left(nv4, inst, offset);

        if (chunk > len)
            chunk = len;
        if (nv4_dma_translate(nv4, inst, offset, 1, &addr, &target) != NV4_DMA_OK)
            return NV4_DMA_FAULT;
        if (target == NV4_DMA_TARGET_VRAM) {
            for (uint32_t i = 0; i < chunk; i++)
                nv4->svga.vram[(addr + i) & nv4->vram_mask] = buf[i];
            for (uint32_t a = addr & ~0xfffu; a < addr + chunk; a += 0x1000)
                nv4->svga.changedvram[(a & nv4->vram_mask) >> 12] = nv4->svga.monitor->mon_changeframecount;
        } else {
            uint32_t n = nv4->dbg_sysw_n++ & 7;

            nv4->dbg_sysw[n].inst   = inst;
            nv4->dbg_sysw[n].offset = offset;
            nv4->dbg_sysw[n].addr   = addr;
            nv4->dbg_sysw[n].len    = chunk;
            dma_bm_write(addr, buf, chunk, 4);
        }
        buf += chunk;
        offset += chunk;
        len -= chunk;
    }
    return NV4_DMA_OK;
}

int
nv4_dma_read32(nv4_t *nv4, uint32_t inst, uint32_t offset, uint32_t *val)
{
    uint8_t b[4];

    if (nv4_dma_read(nv4, inst, offset, b, 4) != NV4_DMA_OK)
        return NV4_DMA_FAULT;
    *val = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t) b[3] << 24);
    return NV4_DMA_OK;
}

int
nv4_dma_write32(nv4_t *nv4, uint32_t inst, uint32_t offset, uint32_t val)
{
    uint8_t b[4] = { val, val >> 8, val >> 16, val >> 24 };

    return nv4_dma_write(nv4, inst, offset, b, 4);
}

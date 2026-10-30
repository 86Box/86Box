/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PFB (memory interface) and PEXTDEV
 *          (the power-on straps).
 *
 *          PFB_BOOT_0 holds the memory size, width and type. The video
 *          BIOS sizes memory by programming a configuration, writing a
 *          pattern and reading it back; on real boards a configuration
 *          larger than the fitted chips mangles the pattern. Here the
 *          RAM_AMOUNT and RAM_WIDTH fields never go above what the board
 *          has, which leads the BIOS to the same result. The timing and
 *          tiling registers are stored; no timing is modeled.
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

/* RAM_TYPE (5:3) */
#define NV4_RAM_TYPE_SGRAM_16MBIT 1
#define NV4_RAM_TYPE_SDRAM_16MBIT 3
#define NV4_RAM_TYPE_SDRAM_64MBIT 4

static uint32_t
nv4_ram_amount_mb(uint32_t code)
{
    static const uint32_t mb[4] = { 32, 4, 8, 16 };
    return mb[code & 3];
}

/* PFB_BOOT_0 with RAM_AMOUNT and RAM_WIDTH held to the fitted memory. */
static uint32_t
nv4_pfb_clamp_boot_0(const nv4_t *nv4, uint32_t val)
{
    if (nv4_ram_amount_mb(val) > (nv4->vram_size >> 20))
        val = (val & ~3u) | nv4_vram_size_code(nv4);
    if (nv4->board->mem_64bit)
        val &= ~0x04u;
    return val;
}

/* The straps: SUB_VENDOR (subsystem ids from the ROM), the RAM type and
   width, the bus, the crystal, and TV out disabled. AGP boards also
   strap the 66 MHz bus.

   The TNT2 moved them about (envytools' nv4_straps): bits 5:2 are a
   memory configuration the video BIOS looks up in a table of PFB_BOOT_0
   values, the bus type is bit 14 and bits 13:12 are the low device id
   bits. The ELSA ERAZOR III BIOSes' table starts with 32 MB SDRAM at
   128 bits (0) and at 64 bits (1); PFB_BOOT_0 is held to the fitted
   memory, and their probe then finds it. */
uint32_t
nv4_straps(const nv4_t *nv4)
{
    uint32_t straps = 0x02 | (3u << 7);

    if (nv4->chip == NV4_CHIP_TNT2) {
        if (nv4->board->mem_64bit)
            straps |= 1u << 2;
        if (nv4->board->crystal_14m)
            straps |= 0x40;
        straps |= (nv4->board->device_id & 3) << 12;
        if (nv4->is_agp)
            straps |= 1u << 14;
        return straps;
    }

    if (nv4->is_agp)
        straps |= 0x01 | 0x20;
    if (!nv4->board->mem_64bit)
        straps |= 0x10;
    if (!nv4->board->sgram)
        straps |= 0x0c; /* 1024K x 2 banks */
    if (nv4->board->crystal_14m)
        straps |= 0x40;
    return straps;
}

double
nv4_crystal_hz(const nv4_t *nv4)
{
    uint32_t straps = (nv4->straps_ovr & 0x800) ? nv4->straps_ovr : nv4_straps(nv4);
    return (straps & 0x40) ? 14318180.0 : 13500000.0;
}

void
nv4_pfb_reset(nv4_t *nv4)
{
    uint32_t type;

    if (nv4->board->sgram)
        type = NV4_RAM_TYPE_SGRAM_16MBIT;
    else
        type = (nv4->chip == NV4_CHIP_TNT2) ? NV4_RAM_TYPE_SDRAM_64MBIT : NV4_RAM_TYPE_SDRAM_16MBIT;

    nv4->pfb_boot_0   = nv4_pfb_clamp_boot_0(nv4, nv4_vram_size_code(nv4) | 0x04 | (type << 3));
    nv4->pfb_debug_0  = 0x20001000;
    nv4->pfb_green_0  = 0x00000003;
    nv4->pfb_config_0 = 0x00001114;
    nv4->pfb_config_1 = 0xc1016293;
    nv4->pfb_rtl      = 0;
    nv4->straps_ovr   = 0;
}

uint32_t
nv4_pfb_read(nv4_t *nv4, uint32_t addr)
{
    switch (addr) {
        case NV4_PFB_BOOT_0:
            return nv4->pfb_boot_0;
        case NV4_PFB_DEBUG_0:
            return nv4->pfb_debug_0;
        case NV4_PFB_GREEN_0:
            return nv4->pfb_green_0;
        case NV4_PFB_CONFIG_0:
            return nv4->pfb_config_0;
        case NV4_PFB_CONFIG_1:
            return nv4->pfb_config_1;
        case NV4_PFB_RTL:
            return nv4->pfb_rtl;
        default:
            nv4_log("NV4 PFB: read %06x\n", addr);
            return 0;
    }
}

void
nv4_pfb_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    switch (addr) {
        case NV4_PFB_BOOT_0:
            nv4->pfb_boot_0 = nv4_pfb_clamp_boot_0(nv4, val & 0xf13f);
            nv4_log("NV4 PFB: BOOT_0 = %08x -> %08x\n", val, nv4->pfb_boot_0);
            break;
        case NV4_PFB_DEBUG_0:
            nv4->pfb_debug_0 = val;
            break;
        case NV4_PFB_GREEN_0:
            nv4->pfb_green_0 = val & 3;
            break;
        case NV4_PFB_CONFIG_0:
            nv4->pfb_config_0 = val;
            break;
        case NV4_PFB_CONFIG_1:
            nv4->pfb_config_1 = val;
            break;
        case NV4_PFB_RTL:
            nv4->pfb_rtl = val & 0x1f;
            break;
        default:
            nv4_log("NV4 PFB: write %06x = %08x\n", addr, val);
            break;
    }
}

/* PEXTDEV_BOOT_0: the straps, unless software set OVERWRITE (bit 11),
   in which case bits 10:0 read back as written. The video BIOS sets it
   to pick the AGP mode. */
uint32_t
nv4_pextdev_read(nv4_t *nv4, uint32_t addr)
{
    if (addr == NV4_PEXTDEV_BOOT_0)
        return (nv4->straps_ovr & 0x800) ? nv4->straps_ovr : nv4_straps(nv4);
    return 0;
}

void
nv4_pextdev_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    if (addr == NV4_PEXTDEV_BOOT_0)
        /* bits 15:12 are not overwritten */
        nv4->straps_ovr = (val & 0x800) ? ((val & 0xfff) | (nv4_straps(nv4) & 0xf000)) : 0;
}

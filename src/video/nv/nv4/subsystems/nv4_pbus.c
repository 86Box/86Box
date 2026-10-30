/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PBUS, the bus interface.
 *
 *          The bus tuning registers (stored, no timing is modeled), the
 *          bus error interrupt, the ROM timing register. The PCI
 *          configuration mirror at 0x1800 is served by the device shell.
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

void
nv4_pbus_reset(nv4_t *nv4)
{
    /* the register descriptions' reset values */
    nv4->pbus_debug[0]   = 0x08080808;
    nv4->pbus_debug[1]   = 0x000014e9;
    nv4->pbus_debug[2]   = 0x00000220;
    nv4->pbus_debug[3]   = 0x00000000;
    nv4->pbus_intr       = 0;
    nv4->pbus_intr_en    = 0;
    nv4->pbus_rom_config = 0x3f;
}

uint32_t
nv4_pbus_read(nv4_t *nv4, uint32_t addr)
{
    switch (addr) {
        case NV4_PBUS_DEBUG_0:
        case NV4_PBUS_DEBUG_1:
        case NV4_PBUS_DEBUG_2:
        case NV4_PBUS_DEBUG_3:
            return nv4->pbus_debug[(addr - NV4_PBUS_DEBUG_0) >> 2];
        case NV4_PBUS_INTR_0:
            return nv4->pbus_intr;
        case NV4_PBUS_INTR_EN_0:
            return nv4->pbus_intr_en;
        case NV4_PBUS_ROM_CONFIG:
            return nv4->pbus_rom_config;
        default:
            nv4_log("NV4 PBUS: read %06x\n", addr);
            return 0;
    }
}

void
nv4_pbus_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    switch (addr) {
        case NV4_PBUS_DEBUG_0:
        case NV4_PBUS_DEBUG_1:
        case NV4_PBUS_DEBUG_2:
        case NV4_PBUS_DEBUG_3:
            nv4->pbus_debug[(addr - NV4_PBUS_DEBUG_0) >> 2] = val;
            break;
        case NV4_PBUS_INTR_0:
            nv4->pbus_intr &= ~val;
            nv4_pmc_update_irq(nv4);
            break;
        case NV4_PBUS_INTR_EN_0:
            nv4->pbus_intr_en = val & 1;
            nv4_pmc_update_irq(nv4);
            break;
        case NV4_PBUS_ROM_CONFIG:
            nv4->pbus_rom_config = val & 0x3f;
            break;
        default:
            nv4_log("NV4 PBUS: write %06x = %08x\n", addr, val);
            break;
    }
}

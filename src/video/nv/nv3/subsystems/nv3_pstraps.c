/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PSTRAPS - External Devices
 *                        Including straps
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

void nv3_pstraps_init(void)
{
    nv_log("Initialising straps....%c", 10);

    /* The power-on configuration the card latches from its FBA[9:0] resistors (datasheet section 10):
       0     bus speed: the 66MHZ bit in PCI status (AGP needs it high)
       1     subsystem IDs from the adapter's VBIOS (0x54-0x57) rather than the system BIOS
       3:2   RAM type: 01 = 8Mbit SGRAM/SDRAM, 128K x 2 banks x 32
       4     128-bit framebuffer
       5     AGP host interface
       6     crystal: 0 = 13.5 MHz, 1 = 14.31818 MHz
       8:7   TV mode: 01 = NTSC
       9     PCI 2.1 compliant (delayed transactions) */
    nv3->straps = (NV3_PSTRAPS_BIOS_PRESENT << NV3_PSTRAPS_BIOS)
                | (NV3_PSTRAPS_RAM_TYPE_8MBIT << NV3_PSTRAPS_RAM_TYPE)
                | (NV3_PSTRAPS_BUS_WIDTH_128BIT << NV3_PSTRAPS_BUS_WIDTH)
                | (NV3_PSTRAPS_CRYSTAL_13500K << NV3_PSTRAPS_CRYSTAL)
                | (NV3_PSTRAPS_TVMODE_NTSC << NV3_PSTRAPS_TVMODE)
                | (1 << NV3_PSTRAPS_PCI21);

    if (nv3->nvbase.bus_generation == nv_bus_pci)
        nv3->straps |= (NV3_PSTRAPS_BUS_TYPE_PCI << NV3_PSTRAPS_BUS_TYPE) | (NV3_PSTRAPS_BUS_SPEED_33MHZ << NV3_PSTRAPS_BUS_SPEED);
    else
        nv3->straps |= (NV3_PSTRAPS_BUS_TYPE_AGP << NV3_PSTRAPS_BUS_TYPE) | (NV3_PSTRAPS_BUS_SPEED_66MHZ << NV3_PSTRAPS_BUS_SPEED);

    /* With the sub-vendor strap the subsystem vendor/device IDs are read from the VBIOS at
       0x54-0x57 during reset (datasheet appendix A, 0x2C-0x2F) */
    if (((nv3->straps >> NV3_PSTRAPS_BIOS) & 1) && nv3->nvbase.vbios.rom && (nv3->nvbase.vbios.sz > 0x57)) {
        for (int i = 0; i < 4; i++)
            nv3->nvbase.pci_config.pci_regs[NV3_PCI_CFG_SUBSYSTEM_ID + i] = nv3->nvbase.vbios.rom[0x54 + i];
    }

    nv3->pbus.agp_rq = NV3_AGP_STATUS_MAX_REQUESTS_AMOUNT;

    nv_log("Straps = 0x%04x%c", nv3->straps, 10);
    nv_log("Initialising PSTRAPS: Done%c", 10);
}

/* The synthesizers' reference clock, as strapped */
double nv3_pstraps_crystal_hz(void)
{
    return ((nv3->straps >> NV3_PSTRAPS_CRYSTAL) & 1) ? 14318180.0 : 13500000.0;
}

//
// ****** Read/Write functions start ******
//

uint32_t nv3_pstraps_read(UNUSED(uint32_t address)) 
{ 
    return nv3->straps;
}

void nv3_pstraps_write(UNUSED(uint32_t address), uint32_t val) 
{
    /* Rev C (RIVA 128 ZX) straps are writable: a write with STRAP_OVERWRITE (bit 11) set replaces
       them -- the ZX BIOS rewrites the RAM type/width it sized this way. The bit is the written
       value's, not the old one's (it resets to 0). Earlier revisions' straps are read-only. */
    if (nv3->nvbase.gpu_revision != NV3_PCI_CFG_REVISION_C00) {
        nv_warning("Tried to write to the straps (val=%08x) on a rev A/B card\n", val);
        return;
    }
    if ((val >> NV3_PSTRAPS_OVERWRITE) & 1)
        nv3->straps = val & 0xFFF;
}

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 pme: Nvidia Mediaport - External MPEG Decode Interface
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
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

// NOTES:
// Interrupts are notifiers.
// Bit 0 - Image, Bit 4 - VBlank interval, Bit 8 - Video, Bit 12 - Audio, Bit 16 - VMI

uint32_t nv3_pme_read(uint32_t address) 
{ 
    uint32_t ret = 0x00;
    
    switch (address)
    {
        case NV3_PME_INTR:
            ret = nv3->pme.intr;
            break;
        case NV3_PME_INTR_EN:
            ret = nv3->pme.intr_en;
            break;
    }

    return ret;
}

void nv3_pme_write(uint32_t address, uint32_t value) 
{
    switch (address)
    {
        case NV3_PME_INTR:
            nv3->pme.intr &= ~value;
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PME_INTR_EN:
            nv3->pme.intr_en = value & 0x00001111;
            break;
    }
}
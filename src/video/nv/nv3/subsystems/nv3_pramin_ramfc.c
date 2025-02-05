/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PFIFO RAMFC area: Stores context for unused DMA channels
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *
 *          Copyright 2024-2025 starfrost
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86Box/86box.h>
#include <86Box/device.h>
#include <86Box/mem.h>
#include <86box/pci.h>
#include <86Box/rom.h> // DEPENDENT!!!
#include <86Box/video.h>
#include <86Box/nv/vid_nv.h>
#include <86Box/nv/vid_nv3.h>

uint32_t nv3_ramfc_read(uint32_t address)
{
    nv_log("RAMFC (Unused DMA channel context) Read (0x%04x), UNIMPLEMENTED - RETURNING 0x00\n", address);
}

void nv3_ramfc_write(uint32_t address, uint32_t value)
{
    nv_log("RAMFC (Unused DMA channel context) Write (0x%04x -> 0x%04x), UNIMPLEMENTED\n", value, address);
}
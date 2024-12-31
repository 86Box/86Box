/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PFIFO hashtable (Quickly access submitted DMA objects)
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

/* This implements the hash that all the objects are stored within.
It is used to get the offset within RAMHT of a graphics object.
 */

uint32_t nv3_pramin_ramht_hash(nv3_pramin_name_t name, uint32_t channel)
{
    uint32_t hash = (name.byte_high ^ name.byte_mid2 ^ name.byte_mid1 ^ name.byte_low ^ (uint8_t)channel);
    nv_log("NV3: Generating RAMHT hash (RAMHT slot=0x%04x (from name 0x%08x for DMA channel 0x%04x)\n)", name, channel);
    return hash;
}
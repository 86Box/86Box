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
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

/* This implements the hash that all the objects are stored within.
It is used to get the offset within RAMHT of a graphics object.
 */

uint32_t nv3_ramht_hash(uint32_t name, uint32_t channel)
{
    // the official nvidia hash algorithm, tweaked for readability
    uint32_t hash = ((name ^ (name >> 8) ^ (name >> 16) ^ (name >> 24)) & 0xFF) ^ (channel & NV3_DMA_CHANNELS_TOTAL); 


    // is this the right endianness?
    nv_log_verbose_only("Generated RAMHT hash 0x%04x (RAMHT slot=0x%04x (from name 0x%08x for DMA channel 0x%04x)\n)\n", hash, (hash/8), name, channel);
    return hash;
}


uint32_t nv3_ramht_read(uint32_t address)
{
    nv_log_verbose_only("RAMHT (Graphics object storage hashtable) Read (0x%04x), I DON'T BELIEVE THIS SHOULD EVER HAPPEN - RETURNING 0x00\n", address);
}

void nv3_ramht_write(uint32_t address, uint32_t value)
{
    nv_log_verbose_only("RAMHT (Graphics object storage hashtable) Write (0x%04x -> 0x%04x), I DON'T BELIEVE THIS SHOULD EVER HAPPEN - UNIMPLEMENTED\n", value, address);
}

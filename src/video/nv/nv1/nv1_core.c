/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 bringup and device emulation.
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
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv1.h>


void nv1_init()
{

}

void* nv1_init_edge2k(const device_t *info)
{

}

void* nv1_init_edge3k(const device_t *info)
{

}

void nv1_close(void* priv)
{
    
}

void nv1_speed_changed(void *priv)
{

}

void nv1_draw_cursor(svga_t* svga, int32_t drawline)
{

}

void nv1_recalc_timings(svga_t* svga)
{

}

void nv1_force_redraw(void* priv)
{

}

// See if the bios rom is available.
int32_t nv1_available(void)
{
    return (rom_present(NV1_VBIOS_E3D_2X00)
    || rom_present(NV1_VBIOS_E3D_3X00));
}

// NV3 (RIVA 128)
// PCI
// 2MB or 4MB VRAM
const device_t nv1_device_edge2k = 
{
    .name = "nVIDIA NV1 [Diamond Edge 3D 2x00] [Not Direct3D Compatible]",
    .internal_name = "nv1_edge2k",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = nv1_init_edge2k,
    .close = nv1_close,
    .speed_changed = nv1_speed_changed,
    .force_redraw = nv1_force_redraw,
    .available = nv1_available,
    .config = nv1_config,
};

// NV3 (RIVA 128)
// AGP
// 2MB or 4MB VRAM
const device_t nv1_device_edge3k = 
{
    .name = "nVIDIA NV1 [Diamond Edge 3D 3x00] [Not Direct3D Compatible]",
    .internal_name = "nv1_edge3k",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = nv1_init_edge3k,
    .close = nv1_close,
    .speed_changed = nv1_speed_changed,
    .force_redraw = nv1_force_redraw,
    .available = nv1_available,
    .config = nv1_config,
};

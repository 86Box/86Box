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
#include <86box/nv/vid_nv4.h>

void nv4_init()
{

}

void* nv4_init_stb4400(const device_t *info)
{
    
}

void nv4_close(void* priv)
{
    
}

void nv4_speed_changed(void *priv)
{

}

void nv4_draw_cursor(svga_t* svga, int32_t drawline)
{

}

void nv4_recalc_timings(svga_t* svga)
{

}

void nv4_force_redraw(void* priv)
{

}

// See if the bios rom is available.
int32_t nv4_available(void)
{
    return (rom_present(NV4_VBIOS_STB_REVA));
}

// NV3 (RIVA 128)
// AGP
// 8MB or 16MB VRAM
const device_t nv4_device_agp = 
{
    .name = "nVIDIA RIVA TNT [STB Velocity 4400]",
    .internal_name = "nv4_stb4400",
    .flags = DEVICE_AGP,
    .local = 0,
    .init = nv4_init_stb4400,
    .close = nv4_close,
    .speed_changed = nv4_speed_changed,
    .force_redraw = nv4_force_redraw,
    .available = nv4_available,
    .config = nv4_config,
};

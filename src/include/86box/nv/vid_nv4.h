/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 * 
 *          Riva TNT hardware defines
 * 
 * Authors: Connor Hyde <mario64crashed@gmail.com>
 *
 *          Copyright 2024-2025 Connor Hyde
 */


#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <86Box/nv/vid_nv.h>
#include <86Box/nv/vid_nv4_defines.h>
extern const device_config_t nv4_config[];                              // Config for RIVA 128 (revision A/B)



// Structures
typedef struct nv4_s
{
    nv_base_t nvbase;   // Base Nvidia structure
} nv4_t;

// Device Core
void        nv4_init();
void        nv4_close(void* priv);
void        nv4_speed_changed(void *priv);
void        nv4_draw_cursor(svga_t* svga, int32_t drawline);
void        nv4_recalc_timings(svga_t* svga);
void        nv4_force_redraw(void* priv);
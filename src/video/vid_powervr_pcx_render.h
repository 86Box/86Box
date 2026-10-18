/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PowerVR PCX1/PCX2 frame renderer: hidden-surface removal (ISP,
 *          "SABRE") and shading/texturing (TSP, "TEXAS").
 *
 *          The renderer has no 86Box dependencies so the same file can be
 *          built into stand-alone test tools.
 *
 * Authors: Zvi Rosen and contributors.
 *
 *          Copyright 2026 Zvi Rosen.
 */
#ifndef VID_POWERVR_PCX_RENDER_H
#define VID_POWERVR_PCX_RENDER_H

#include <stdint.h>

#define PCXR_MAX_W 1024
#define PCXR_MAX_H 1024

typedef struct pcxr_frame_t {
    /* ISP parameter store: the TLB-mapped buffer, as host words in
       virtual (TLB) order. */
    const uint32_t *isp;
    uint32_t        isp_words;

    /* Texture memory (BAR1) as 32-bit words; TSP parameters live here. */
    const uint32_t *tex;
    uint32_t        tex_words;

    /* Register values used by the renderer. */
    uint32_t object_offset; /* OBJECT_OFFSET: byte offset of the tile list, bit 0 flag */
    uint32_t fog_shift;     /* FOGAMOUNT */
    uint32_t fog_colour;    /* FOGCOL, 0x00RRGGBB in bits 23:0 */
    uint32_t camera;        /* CAMERA: texture c/f/r scale */
    uint32_t xclip;         /* XCLIP */
    uint32_t bilinear;      /* BILINEAR */
    const uint32_t *fog_table; /* 128 entries (FOG_TABLE registers) */
} pcxr_frame_t;

typedef struct pcxr_result_t {
    int      min_x, min_y, max_x, max_y; /* rows/columns covered by tiles (inclusive) */
    uint32_t tiles;
    uint32_t pixels_shaded;
    int      error; /* non-zero if the tile list was malformed and rendering stopped */
} pcxr_result_t;

/* Render one frame into rgb (PCXR_MAX_W x PCXR_MAX_H, 0x00RRGGBB). Pixels
   outside the tiles are left untouched. */
void pcxr_render(const pcxr_frame_t *f, uint32_t *rgb, pcxr_result_t *res);

/* Pack 0x00RRGGBB into the PACKMODE output format. bytes_per_pixel is
   returned; returns the packed value in *out (little endian). */
int pcxr_pack(uint32_t packmode, uint32_t rgb, int x, int y, uint32_t *out);

#endif

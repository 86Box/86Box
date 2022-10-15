/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		IBM XGA emulation.
 *
 *
 *
 * Authors:	TheCollector1995.
 *
 *		Copyright 2022 TheCollector1995.
 */

#ifndef VIDEO_XGA_H
#define VIDEO_XGA_H

#include <86box/rom.h>

typedef struct {
    int      ena;
    int      x, y, xoff, yoff, cur_xsize, cur_ysize;
    uint32_t addr;
} xga_hwcursor_t;

typedef struct xga_t {
    mem_mapping_t  memio_mapping;
    mem_mapping_t  linear_mapping;
    mem_mapping_t  video_mapping;
    rom_t          bios_rom;
    xga_hwcursor_t hwcursor, hwcursor_latch;
    PALETTE        extpal;

    uint8_t test, atest[2], testpixel;
    ;
    uint8_t  pos_regs[8];
    uint8_t  disp_addr;
    uint8_t  cfg_reg;
    uint8_t  instance;
    uint8_t  op_mode;
    uint8_t  aperture_cntl;
    uint8_t  ap_idx;
    uint8_t  access_mode;
    uint8_t  regs[0x100];
    uint8_t  regs_idx;
    uint8_t  hwc_hotspot_x;
    uint8_t  hwc_hotspot_y;
    uint8_t  disp_cntl_1, disp_cntl_2;
    uint8_t  clk_sel_1, clk_sel_2;
    uint8_t  hwc_control;
    uint8_t  bus_arb;
    uint8_t  select_pos_isa;
    uint8_t  hwcursor_oddeven;
    uint8_t  cfg_reg_instance;
    uint8_t  rowcount;
    uint8_t  pal_idx, pal_idx_prefetch;
    uint8_t  pal_seq;
    uint8_t  pal_mask;
    uint8_t  pal_r, pal_r_prefetch;
    uint8_t  pal_g, pal_g_prefetch;
    uint8_t  pal_b, pal_b_prefetch;
    uint8_t  sprite_data[1024];
    uint8_t  scrollcache;
    uint8_t  direct_color;
    uint8_t *vram, *changedvram;

    int16_t hwc_pos_x;
    int16_t hwc_pos_y;

    uint16_t pos_idx;
    uint16_t htotal;
    uint16_t sprite_idx, sprite_idx_prefetch;
    uint16_t hdisp;
    uint16_t vtotal;
    uint16_t vdispend;
    uint16_t vblankstart;
    uint16_t vsyncstart;
    uint16_t linecmp;
    uint16_t pix_map_width;
    uint16_t sprite_pal_addr_idx, old_pal_addr_idx;
    uint16_t sprite_pal_addr_idx_prefetch;

    int v_total, dispend, v_syncstart, split, v_blankstart,
        h_disp, h_disp_old, h_total, h_disp_time, rowoffset,
        dispon, h_disp_on, vc, sc, linepos, oddeven, firstline, lastline,
        firstline_draw, lastline_draw, displine, fullchange, interlace,
        char_width, hwcursor_on;
    int pal_pos, pal_pos_prefetch;
    int on;
    int op_mode_reset, linear_endian_reverse;
    int sprite_pos, sprite_pos_prefetch, cursor_data_on;
    int pal_test, a5_test;
    int type, bus;

    uint32_t linear_base, linear_size, banked_mask;
    uint32_t base_addr_1mb;
    uint32_t hwc_color0, hwc_color1;
    uint32_t disp_start_addr;
    uint32_t ma_latch;
    uint32_t vram_size;
    uint32_t vram_mask;
    uint32_t rom_addr;
    uint32_t ma, maback;
    uint32_t extpallook[256];
    uint32_t read_bank, write_bank;
    uint32_t px_map_base;

    uint64_t dispontime, dispofftime;

    struct
    {
        uint8_t control;
        uint8_t px_map_idx;
        uint8_t frgd_mix, bkgd_mix;
        uint8_t cc_cond;
        uint8_t octant;
        uint8_t draw_mode;
        uint8_t mask_mode;
        uint8_t short_stroke_vector1;
        uint8_t short_stroke_vector2;
        uint8_t short_stroke_vector3;
        uint8_t short_stroke_vector4;

        int16_t bres_err_term;
        int16_t bres_k1, bres_k2;

        uint16_t blt_width;
        uint16_t blt_height;
        uint16_t mask_map_origin_x_off;
        uint16_t mask_map_origin_y_off;
        uint16_t src_map_x, src_map_y;
        uint16_t dst_map_x, dst_map_y;
        uint16_t pat_map_x, pat_map_y;

        int ssv_state;
        int pat_src;
        int src_map;
        int dst_map;
        int bkgd_src;
        int fore_src;
        int x, y, sx, sy, dx, dy, px, py;
        int pattern;
        int command_len;

        uint32_t short_stroke;
        uint32_t color_cmp;
        uint32_t carry_chain;
        uint32_t plane_mask;
        uint32_t frgd_color, bkgd_color;
        uint32_t command;
        uint32_t dir_cmd;

        uint8_t  px_map_format[4];
        uint16_t px_map_width[4];
        uint16_t px_map_height[4];
        uint32_t px_map_base[4];
    } accel;

    volatile int force_busy;
} xga_t;
#endif /*VIDEO_XGA_H*/

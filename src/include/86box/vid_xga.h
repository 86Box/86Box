/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM XGA emulation.
 *
 *
 *
 * Authors: TheCollector1995.
 *
 *          Copyright 2022 TheCollector1995.
 */
#ifndef VIDEO_XGA_H
#define VIDEO_XGA_H

#include <86box/rom.h>

typedef struct xga_hwcursor_t {
    int      ena;
    int      x;
    int      y;
    int      xoff;
    int      yoff;
    int      cur_xsize;
    int      cur_ysize;
    uint32_t addr;
} xga_hwcursor_t;

typedef struct xga_t {
    mem_mapping_t  memio_mapping;
    mem_mapping_t  linear_mapping;
    mem_mapping_t  video_mapping;
    rom_t          bios_rom;
    rom_t          bios_rom2;
    xga_hwcursor_t hwcursor;
    xga_hwcursor_t hwcursor_latch;
    PALETTE        extpal;

    uint8_t test;
    uint8_t test2;
    uint8_t atest[2];
    uint8_t testpixel;

    uint8_t  pos_regs[8];
    uint8_t  disp_addr;
    uint8_t  dac_mask;
    uint8_t  dac_status;
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
    uint8_t  disp_cntl_1;
    uint8_t  disp_cntl_2;
    uint8_t  clk_sel_1;
    uint8_t  clk_sel_2;
    uint8_t  hwc_control;
    uint8_t  bus_arb;
    uint8_t  isa_pos_enable;
    uint8_t  hwcursor_oddeven;
    uint8_t  cfg_reg_instance;
    uint8_t  rowcount;
    uint8_t  pal_idx;
    uint8_t  pal_idx_prefetch;
    uint8_t  pal_seq;
    uint8_t  pal_mask;
    uint8_t  pal_r;
    uint8_t  pal_r_prefetch;
    uint8_t  pal_g;
    uint8_t  pal_g_prefetch;
    uint8_t  pal_b;
    uint8_t  pal_b_prefetch;
    uint8_t  sprite_data[1024];
    uint8_t  scrollcache;
    uint8_t  border_color;
    uint8_t  direct_color;
    uint8_t  dma_channel;
    uint8_t  instance_isa;
    uint8_t  instance_num;
    uint8_t  ext_mem_addr;
    uint8_t  vga_post;
    uint8_t  addr_test;
    uint8_t *vram;
    uint8_t *changedvram;

    int16_t hwc_pos_x;
    int16_t hwc_pos_y;

    uint16_t pos_idx;
    uint16_t htotal;
    uint16_t sprite_idx;
    uint16_t sprite_idx_prefetch;
    uint16_t hdisp;
    uint16_t vtotal;
    uint16_t vdispend;
    uint16_t vblankstart;
    uint16_t vsyncstart;
    uint16_t linecmp;
    uint16_t pix_map_width;
    uint16_t sprite_pal_addr_idx;
    uint16_t old_pal_addr_idx;
    uint16_t sprite_pal_addr_idx_prefetch;

    int dac_addr;
    int dac_pos;
    int dac_r;
    int dac_g;
    int v_total;
    int dispend;
    int v_syncstart;
    int split;
    int v_blankstart;
    int h_disp;
    int h_disp_old;
    int h_total;
    int h_disp_time;
    int rowoffset;
    int dispon;
    int h_disp_on;
    int vc;
    int sc;
    int linepos;
    int oddeven;
    int firstline;
    int lastline;
    int firstline_draw;
    int lastline_draw;
    int displine;
    int fullchange;
    int interlace;
    int char_width;
    int hwcursor_on;
    int pal_pos;
    int pal_pos_prefetch;
    int on;
    int op_mode_reset;
    int linear_endian_reverse;
    int sprite_pos;
    int sprite_pos_prefetch;
    int cursor_data_on;
    int pal_test;
    int a5_test;
    int type;
    int bus;
    int busy;

    uint32_t linear_base;
    uint32_t linear_size;
    uint32_t banked_mask;
    uint32_t base_addr_1mb;
    uint32_t hwc_color0;
    uint32_t hwc_color1;
    uint32_t disp_start_addr;
    uint32_t ma_latch;
    uint32_t vram_size;
    uint32_t vram_mask;
    uint32_t rom_addr;
    uint32_t ma;
    uint32_t maback;
    uint32_t read_bank;
    uint32_t write_bank;
    uint32_t px_map_base;
    uint32_t pallook[512];
    uint32_t bios_diag;

    PALETTE xgapal;

    uint64_t dispontime;
    uint64_t dispofftime;

    struct {
        uint8_t control;
        uint8_t px_map_idx;
        uint8_t frgd_mix;
        uint8_t bkgd_mix;
        uint8_t cc_cond;
        uint8_t octant;
        uint8_t draw_mode;
        uint8_t mask_mode;
        uint8_t short_stroke_vector1;
        uint8_t short_stroke_vector2;
        uint8_t short_stroke_vector3;
        uint8_t short_stroke_vector4;

        int16_t bres_err_term;
        int16_t bres_k1;
        int16_t bres_k2;

        uint16_t blt_width;
        uint16_t blt_height;
        uint16_t mask_map_origin_x_off;
        uint16_t mask_map_origin_y_off;
        uint16_t src_map_x;
        uint16_t src_map_y;
        uint16_t dst_map_x;
        uint16_t dst_map_y;
        uint16_t pat_map_x;
        uint16_t pat_map_y;

        int ssv_state;
        int pat_src;
        int src_map;
        int dst_map;
        int bkgd_src;
        int fore_src;
        int oldx;
        int oldy;
        int x;
        int y;
        int sx;
        int sy;
        int dx;
        int dy;
        int px;
        int py;
        int pattern;
        int command_len;
        int filling;

        uint32_t short_stroke;
        uint32_t color_cmp;
        uint32_t carry_chain;
        uint32_t plane_mask;
        uint32_t frgd_color;
        uint32_t bkgd_color;
        uint32_t command;
        uint32_t dir_cmd;

        uint8_t  px_map_format[4];
        uint16_t px_map_width[4];
        uint16_t px_map_height[4];
        uint32_t px_map_base[4];
    } accel;
} xga_t;

#endif /*VIDEO_XGA_H*/

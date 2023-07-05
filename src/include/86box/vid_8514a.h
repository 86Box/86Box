/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the 8514/A card from IBM for the MCA bus and
 *          generic ISA bus clones without vendor extensions.
 *
 *
 *
 * Authors: TheCollector1995
 *
 *          Copyright 2022 TheCollector1995.
 */

#ifndef VIDEO_8514A_H
#define VIDEO_8514A_H

typedef struct ibm8514_t {
    uint8_t pos_regs[8];

    int force_old_addr;
    int type;

    uint32_t vram_size;
    uint32_t vram_mask;

    PALETTE   vgapal;
    uint8_t   dac_mask, dac_status;
    uint32_t *map8;
    int       dac_addr, dac_pos, dac_r, dac_g;

    struct {
        uint16_t subsys_cntl;
        uint16_t setup_md;
        uint8_t  advfunc_cntl, ext_advfunc_cntl;
        uint16_t cur_y, cur_y_bitres;
        uint16_t cur_x, cur_x_bitres;
        int16_t  desty_axstp;
        int16_t  destx_distp;
        int16_t  err_term;
        int16_t  maj_axis_pcnt;
        uint16_t cmd, cmd_back;
        uint16_t short_stroke;
        uint16_t bkgd_color;
        uint16_t frgd_color;
        uint16_t wrt_mask;
        uint16_t rd_mask;
        uint16_t color_cmp;
        uint16_t bkgd_mix;
        uint16_t frgd_mix;
        uint16_t multifunc_cntl;
        uint16_t multifunc[16];
        int16_t  clip_left, clip_top;
        uint8_t  pix_trans[2];
        int      poly_draw;
        int      ssv_state;
        int      x1, x2, y1, y2;
        int      sys_cnt, sys_cnt2;
        int      temp_cnt;
        int16_t  cx, cy, oldcy;
        int16_t  sx, sy;
        int16_t  dx, dy;
        int16_t  err;
        uint32_t src, dest;
        uint32_t newsrc_blt, newdest_blt;
        uint32_t newdest_in, newdest_out;
        uint8_t *writemono, *nibbleset;
        int      x_count, xx_count, y_count;
        int      input, output;

        uint16_t cur_x_bit12, cur_y_bit12;
        int      ssv_len;
        uint8_t  ssv_dir;
        uint8_t  ssv_draw;
        int      odd_in, odd_out;

        uint16_t scratch;
        int      fill_state, xdir, ydir;
    } accel;

    uint16_t test;
    int ibm_mode;

    int v_total, dispend, v_syncstart, split,
        h_disp, h_disp_old, h_total, h_disp_time, rowoffset,
        dispon, hdisp_on, linecountff,
        vc, linepos, oddeven, cursoron, blink, scrollcache,
        firstline, lastline, firstline_draw, lastline_draw,
        displine, fullchange, x_add, y_add;
    uint32_t ma, maback;

    uint8_t *vram, *changedvram, linedbl;

    uint8_t data_available, data_available2;
    uint8_t scanmodulos, rowcount;
    int     htotal, hdisp, vtadj, vdadj, vsadj, sc,
        vtb, vdb, vsb, vsyncstart, vsyncwidth;
    int     vtotal, vdisp;
    int     disp_cntl, interlace;
    uint8_t subsys_cntl, subsys_stat;

    volatile int force_busy, force_busy2;

    int      blitter_busy;
    uint64_t blitter_time;
    uint64_t status_time;
    int pitch;
} ibm8514_t;
#endif /*VIDEO_8514A_H*/

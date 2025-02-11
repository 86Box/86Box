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

typedef struct hwcursor8514_t {
    int      ena;
    int      x;
    int      y;
    int      xoff;
    int      yoff;
    int      cur_xsize;
    int      cur_ysize;
    int      v_acc;
    int      h_acc;
    uint32_t addr;
    uint32_t pitch;
} hwcursor8514_t;

typedef union {
    uint64_t q;
    uint32_t d[2];
    uint16_t w[4];
    uint8_t  b[8];
} latch8514_t;

typedef struct ibm8514_t {
    rom_t bios_rom;
    rom_t bios_rom2;
    mem_mapping_t bios_mapping;
    uint8_t *rom1;
    uint8_t *rom2;
    hwcursor8514_t hwcursor;
    hwcursor8514_t hwcursor_latch;
    uint8_t        pos_regs[8];
    char *rom_path;

    int force_old_addr;
    int type;
    int local;
    int bpp;
    int on;
    int accel_bpp;

    uint32_t vram_size;
    uint32_t vram_mask;
    uint32_t pallook[512];
    uint32_t bios_addr;

    PALETTE   vgapal;
    uint8_t   hwcursor_oddeven;
    uint8_t   dac_mask;
    uint8_t   dac_status;
    uint32_t *map8;
    int       dac_addr;
    int       dac_pos;
    int       dac_r;
    int       dac_g;
    int       dac_b;
    int       internal_pitch;
    int       hwcursor_on;
    int       modechange;

    uint64_t  dispontime;
    uint64_t  dispofftime;

    struct {
        uint16_t subsys_cntl;
        uint16_t setup_md;
        uint16_t advfunc_cntl;
        uint16_t cur_y;
        uint16_t cur_x;
        int16_t  destx;
        int16_t  desty;
        int16_t  desty_axstp;
        int16_t  destx_distp;
        int16_t  err_term;
        int16_t  maj_axis_pcnt;
        int16_t  maj_axis_pcnt_no_limit;
        uint16_t cmd;
        uint16_t cmd_back;
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
        uint16_t clip_right;
        uint16_t clip_bottom;
        int16_t  clip_left;
        int16_t  clip_top;
        uint8_t  pix_trans[2];
        int      poly_draw;
        int      ssv_state;
        int      x1;
        int      x2;
        int      x3;
        int      y1;
        int      y2;
        int      temp_cnt;
        int16_t  cx;
        int16_t  cx_back;
        int16_t  cy;
        int16_t  oldcx;
        int16_t  oldcy;
        int16_t  sx;
        int16_t  sy;
        int16_t  dx;
        int16_t  dy;
        int16_t  err;
        uint32_t src;
        uint32_t dest;
        int      x_count;
        int      xx_count;
        int      y_count;
        int      input;
        int      input2;
        int      output;
        int      output2;

        int      ssv_len;
        int      ssv_len_back;
        uint8_t  ssv_dir;
        uint8_t  ssv_draw;
        int      odd_in;
        int      odd_out;

        uint16_t scratch;
        int      fill_state;
        int      xdir;
        int      ydir;
        int      linedraw;
        uint32_t ge_offset;
    } accel;

    uint16_t test;
    int      h_blankstart;
    int      h_blank_end_val;
    int      hblankstart;
    int      hblank_end_val;
    int      hblankend;
    int      hblank_ext;
    int      hblank_sub;

    int      v_total_reg;
    int      v_total;
    int      dispend;
    int      v_sync_start;
    int      v_syncstart;
    int      split;
    int      h_disp;
    int      h_total;
    int      h_sync_width;
    int      h_disp_time;
    int      rowoffset;
    int      dispon;
    int      hdisp_on;
    int      linecountff;
    int      vc;
    int      linepos;
    int      oddeven;
    int      cursoron;
    int      blink;
    int      scrollcache;
    int      firstline;
    int      lastline;
    int      firstline_draw;
    int      lastline_draw;
    int      displine;
    int      fullchange;
    uint32_t ma;
    uint32_t maback;

    uint8_t *vram;
    uint8_t *changedvram;
    uint8_t  linedbl;

    uint8_t data_available;
    uint8_t data_available2;
    uint8_t rowcount;
    int     hsync_start;
    int     hsync_width;
    int     htotal;
    int     hdisp;
    int     hdisp2;
    int     hdisped;
    int     sc;
    int     vsyncstart;
    int     vsyncwidth;
    int     vtotal;
    int     v_disp;
    int     v_disp2;
    int     vdisp;
    int     vdisp2;
    int     disp_cntl;
    int     interlace;
    uint8_t subsys_cntl;
    uint8_t subsys_stat;

    atomic_int fifo_idx;
    atomic_int ext_fifo_idx;
    atomic_int force_busy;
    atomic_int force_busy2;

    int      blitter_busy;
    uint64_t blitter_time;
    uint64_t status_time;
    int      pitch;
    int      ext_pitch;
    int      ext_crt_pitch;
    int      extensions;
    int      linear;
    uint32_t vram_amount;
    int      vram_512k_8514;
    int      vendor_mode;
    PALETTE  _8514pal;

    latch8514_t latch;
} ibm8514_t;

#endif /*VIDEO_8514A_H*/

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the 8514/A-compatible Mach8 and Mach32 graphics
 *          chips from ATI for the ISA/VLB/MCA/PCI buses.
 *
 * Authors: TheCollector1995.
 *
 *          Copyright 2022-2024 TheCollector1995.
 */
#ifndef VIDEO_ATI_MACH8_H
#define VIDEO_ATI_MACH8_H

typedef enum {
    ATI_68875 = 0,
    ATI_68860,
    RAMDAC_MAX
} mach_ramdac_type;

typedef struct mach_t {
    ati_eeprom_t eeprom;
    svga_t       svga;

    rom_t         bios_rom;
    rom_t         bios_rom2;
    mem_mapping_t mmio_linear_mapping;
    mem_mapping_t banked_mapping;

    int mca_bus;
    int pci_bus;
    int vlb_bus;
    int has_bios;

    uint8_t regs[256];
    uint8_t pci_regs[256];
    uint8_t int_line;
    uint8_t pci_slot;
    uint8_t irq_state;

    int index;
    mach_ramdac_type ramdac_type;
    int old_mode;

    uint16_t config1;
    uint16_t config2;

    uint8_t  pos_regs[8];
    uint8_t  pci_cntl_reg;
    uint8_t  cursor_col_0;
    uint8_t  cursor_col_1;
    uint8_t  ext_cur_col_0_r;
    uint8_t  ext_cur_col_1_r;
    uint8_t  ext_cur_col_0_g;
    uint8_t  ext_cur_col_1_g;
    uint16_t cursor_col_0_rg;
    uint16_t cursor_col_1_rg;
    uint16_t cursor_col_b;
    uint16_t cursor_offset_lo;
    uint16_t cursor_offset_lo_reg;
    uint16_t cursor_offset_hi;
    uint16_t cursor_offset_hi_reg;
    uint16_t cursor_vh_offset;
    uint16_t cursor_x;
    uint16_t cursor_y;
    uint16_t misc;
    uint16_t memory_aperture;
    uint16_t local_cntl;
    uint32_t linear_base;
    uint8_t  ap_size;
    uint8_t  bank_w;
    uint8_t  bank_r;
    uint16_t shadow_set;
    uint16_t shadow_cntl;
    uint8_t overscan_col_8;
    uint8_t overscan_b_col_24;
    uint8_t overscan_g_col_24;
    uint8_t overscan_r_col_24;
    uint16_t fifo_test_data[17];
    uint8_t old_on1;
    uint8_t old_on2;
    int     crt_resolution;

    struct {
        uint8_t  line_idx;
        int16_t  line_array[6];
        uint8_t  patt_idx;
        uint8_t  patt_len;
        uint8_t  pix_trans[2];
        uint8_t  alu_bg_fn;
        uint8_t  alu_fg_fn;
        uint16_t eeprom_control;
        uint16_t clip_left;
        uint16_t clip_right;
        uint16_t clip_top;
        uint16_t clip_bottom;
        uint16_t dest_x_end;
        uint16_t dest_x_start;
        uint16_t dest_y_end;
        uint16_t src_x_end;
        uint16_t src_x_start;
        uint16_t src_x;
        uint16_t src_y;
        int16_t  bres_count;
        uint16_t clock_sel;
        uint16_t clock_sel_mode;
        uint16_t crt_pitch;
        uint16_t ge_pitch;
        uint16_t src_pitch;
        uint16_t dst_pitch;
        uint16_t dest_cmp_fn;
        uint16_t dp_config;
        uint16_t ext_ge_config;
        uint16_t crt_offset_lo;
        uint16_t crt_offset_hi;
        uint16_t ge_offset_lo;
        uint16_t ge_offset_hi;
        uint16_t linedraw_opt;
        uint16_t max_waitstates;
        uint16_t scan_to_x;
        uint16_t scratch0;
        uint16_t scratch1;
        uint16_t test;
        uint16_t pattern;
        uint16_t test2;
        int      patt_data_idx_reg;
        int      patt_data_idx;
        int      src_y_dir;
        int      cmd_type;
        int      block_write_mono_pattern_enable;
        int      mono_pattern_enable;
        int16_t  cx_end_line;
        int16_t  cy_end_line;
        int16_t  cx;
        int16_t  cx_end;
        int16_t  cy_end;
        int16_t  dx;
        int16_t  dx_end;
        int16_t  dy;
        int16_t  dy_end;
        int16_t  dx_first_row_start;
        int16_t  dx_start;
        int16_t  dy_start;
        int16_t  cy;
        int16_t  sx_start;
        int16_t  sx_end;
        int16_t  sx;
        int16_t  x_count;
        int16_t  xx_count;
        int16_t  xxx_count;
        int16_t  sy;
        int16_t  y_count;
        int16_t  err;
        int16_t  width;
        int16_t  src_width;
        int16_t  height;
        int16_t  bleft, bright, btop, bbottom;
        int      poly_src;
        int      temp_cnt;
        int      stepx;
        int      stepy;
        int      src_stepx;
        uint8_t  mono_pattern_normal[16];
        uint8_t  color_pattern[32];
        uint16_t color_pattern_hicol[8];
        int      mono_pattern[8][8];
        uint32_t src_ge_offset;
        uint32_t dst_ge_offset;
        uint32_t crt_offset;
        uint32_t patt_len_reg;
        int      poly_fill;
        uint16_t dst_clr_cmp_mask;
        int      clip_overrun;
        int      color_pattern_idx;
        int64_t  src_x_scan;
        int64_t  src_y_scan;
    } accel;

    atomic_int force_busy;
    atomic_int fifo_test_idx;
} mach_t;

#endif /*VIDEO_ATI_MACH8_H*/

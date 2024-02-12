/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the EGC graphics processor used by
 *          the NEC PC-98x1 series of computers.
 *
 *
 *
 * Authors: TAKEDA toshiya,
 *          yui/Neko Project II
 *
 *          Copyright 2009-2023 TAKEDA, toshiya.
 *          Copyright 2008-2023 yui/Neko Project II.
 */
#ifndef VIDEO_PC98X1_DISP_H
#    define VIDEO_PC98X1_DISP_H

#define TVRAM_SIZE      0x4000
#define VRAM16_SIZE     0x40000
#define VRAM256_SIZE    0x80000
#define EMS_SIZE        0x10000

enum {
    MODE1_ATRSEL        = 0x00,
    MODE1_GRAPHIC       = 0x01,
    MODE1_COLUMN        = 0x02,
    MODE1_FONTSEL       = 0x03,
    MODE1_200LINE       = 0x04,
    MODE1_KAC           = 0x05,
    MODE1_MEMSW         = 0x06,
    MODE1_DISP          = 0x07,
};

enum {
    MODE2_16COLOR       = 0x00,
    MODE2_EGC           = 0x02,
    MODE2_WRITE_MASK    = 0x03,
    MODE2_256COLOR      = 0x10,
    MODE2_480LINE       = 0x34,
};

enum {
    MODE3_WRITE_MASK    = 0x01,
    MODE3_LINE_COLOR    = 0x09,
    MODE3_NPC_COLOR     = 0x0b,
    MODE3_LINE_CONNECT  = 0x0f,
};

enum {
    GRCG_PLANE_0        = 0x01,
    GRCG_PLANE_1        = 0x02,
    GRCG_PLANE_2        = 0x04,
    GRCG_PLANE_3        = 0x08,
    GRCG_PLANE_SEL      = 0x30,
    GRCG_RW_MODE        = 0x40,
    GRCG_CG_MODE        = 0x80,
};

typedef struct pc98x1_vid_t {
    /* vga */
    uint8_t tvram_buffer[480 * 640];
    uint8_t vram0_buffer[480 * 640];
    uint8_t vram1_buffer[480 * 640];
    uint8_t null_buffer[480 * 640];
    int width;
    int height;
    int last_width;
    int last_height;
    uint8_t dirty;
    uint8_t blink;
    uint32_t palette_chr[8];
    uint32_t palette_gfx[256];

    uint8_t font[0x84000];
    uint8_t tvram[TVRAM_SIZE];
    uint8_t vram16[VRAM16_SIZE];
    uint8_t vram256[VRAM256_SIZE];
    uint8_t ems[EMS_SIZE];
    uint8_t *vram16_disp_b;
    uint8_t *vram16_disp_r;
    uint8_t *vram16_disp_g;
    uint8_t *vram16_disp_e;
    uint8_t *vram16_draw_b;
    uint8_t *vram16_draw_r;
    uint8_t *vram16_draw_g;
    uint8_t *vram16_draw_e;
    uint8_t *vram256_disp;
    uint8_t *vram256_draw_0;
    uint8_t *vram256_draw_1;

    GDCState gdc_chr;
    GDCState gdc_gfx;
    EGCState egc;

    uint8_t grcg_mode;
    uint8_t grcg_tile_cnt;
    uint8_t grcg_tile_b[4];
    uint16_t grcg_tile_w[4];

    uint8_t crtv;
    uint8_t pl;
    uint8_t bl;
    uint8_t cl;
    uint8_t ssl;
    uint8_t sur;
    uint8_t sdr;

    uint8_t mode1[8];
    uint8_t mode2[128];
    uint8_t mode3[128];
    uint8_t mode_select;

    uint8_t digipal[4];
    uint8_t anapal[3][256];
    uint8_t anapal_select;

    uint8_t bank_draw;
    uint8_t bank_disp;
    uint8_t bank256_draw_0;
    uint8_t bank256_draw_1;
    uint16_t vram256_bank_0;
    uint16_t vram256_bank_1;
    uint8_t ems_selected;

    uint16_t font_code;
    uint8_t font_line;
    uint32_t cgwindow_addr_low;
    uint32_t cgwindow_addr_high;

    int htotal;
    int hblank;

    uint64_t dispontime;
    uint64_t dispofftime;

    pc_timer_t timer;
    double clock;
} pc98x1_vid_t;

#    ifdef EMU_DEVICE_H
extern const device_t pc98x1_vid_device;
#    endif // EMU_DEVICE_H

#endif /*VIDEO_PC98X1_EGC_H*/

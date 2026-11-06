/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header files for the PCjr keyboard and video subsystems.
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com>
 *
 *          Copyright 2025 starfrost
 */
#pragma once

#define PCJR_RGB          0
#define PCJR_COMPOSITE    1
#define PCJR_RGB_NO_BROWN 4
#define PCJR_RGB_IBM_5153 5

#define PCJR_COMPOSITE_OLD 0
#define PCJR_COMPOSITE_NEW 1

#define DOUBLE_NONE               0
#define DOUBLE_SIMPLE             1
#define DOUBLE_INTERPOLATE_SRGB   2
#define DOUBLE_INTERPOLATE_LINEAR 3

typedef struct pcjr_s {
    /* Video Controller stuff. */
    mem_mapping_t mapping;
    uint8_t       crtc[32];
    int           crtcreg;
    int           array_index;
    uint8_t       array[32];
    int           array_ff;
    int           memctrl;
    uint8_t       status;
    int           addr_mode;
    uint8_t      *vram;
    uint8_t      *b8000;
    int           linepos;
    int           displine;
    int           scanline;
    int           vc;
    int           dispon;
    int           cursorvisible; // Is the cursor visible on the current scanline?
    int           cursoron;
    int           blink;
    int           vsynctime;
    int           fullchange;
    int           vadj;
    uint16_t      memaddr;
    uint16_t      memaddr_backup;
    uint64_t      dispontime;
    uint64_t      dispofftime;
    pc_timer_t    timer;
    int           firstline;
    int           lastline;
    int           composite;
    int           composite_new_era;
    int           apply_hd;
    int           double_type;

    /* Composite artifact colours of the 320x200x4 and 640x200x2 modes. */
    uint32_t      comp_lut[256];
    uint32_t      comp_key;      /* Mode and palette the table was made for. */
    int           comp_hue;
    int           comp_brightness;
    int           comp_contrast;
    int           comp_saturation;
    int           comp_len;     /* Raw bytes on the last rendered line. */
    uint8_t       comp_raw[512];

    /* Keyboard Controller stuff. */
    int        latched;
    int        data;
    int        serial_data[44];
    int        serial_pos;
    uint8_t    pa;
    uint8_t    pb;

    uint8_t    option_modem;
    uint8_t    option_fdc;
    uint8_t    option_ir;

    pc_timer_t send_delay_timer;

} pcjr_t; 

void pcjr_recalc_timings(pcjr_t *pcjr);

// Note: This is a temporary solution until the pcjr video is made its own gfx card
void pcjr_vid_init(pcjr_t *pcjr);

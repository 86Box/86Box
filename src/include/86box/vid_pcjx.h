/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM PC JX video state, derived from the PCjr video subsystem.
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com>
 *
 *          Copyright 2025 starfrost
 */
#pragma once

#include <stdint.h>
#include <86box/timer.h>
#include <86box/device.h>

#define PCJX_CG2_IMAGE_SIZE 0x38000
#define PCJX_GAIJI_SIZE     0x800

typedef struct pcjx_video_s {
    uint8_t       crtc[32];
    int           crtcreg;
    int           array_index;
    uint8_t       array[32];
    int           array_ff;
    int           memctrl;
    uint8_t       status;
    int           addr_mode;
    uint8_t      *vram;
    int           linepos;
    int           displine;
    int           scanline;
    int           vc;
    int           dispon;
    int           cursorvisible; // Is the cursor visible on the current scanline?
    int           cursoron;
    int           blink;
    int           vsynctime;
    int           vadj;
    uint16_t      memaddr;
    uint16_t      memaddr_backup;
    uint64_t      dispontime;
    uint64_t      dispofftime;
    pc_timer_t    timer;
    int           firstline;
    int           lastline;
    int           composite;
    int           apply_hd;
    int           double_type;

    /* JX VP2 has private 00/01/03 registers and an independent index phase.
       array[] remains VP1 plus the common registers; CPU decode is board-owned. */
    uint8_t       jx_array[32];
    int           jx_array_index;
    int           jx_array_ff;
    uint8_t       pg2;
    uint8_t      *shared_ram;
    uint32_t      shared_size;
    uint8_t      *dedicated_vram;
    uint8_t       cg1[2048]; /* Immutable PCjr hardware-font compatibility copy. */
    const uint8_t *cg2; /* NULL when native Japanese video hardware is absent. */
    uint8_t       *gaiji;
    uint8_t       dot_component[2];

    uint8_t       pb; /* Board PPI port B; bit 2 selects text rather than graphics. */
} pcjx_video_t;

void pcjx_vid_init(pcjx_video_t *video, uint8_t *shared_ram, uint32_t shared_size,
                  uint8_t *dedicated_vram, const uint8_t *cg1,
                  const uint8_t *cg2, uint8_t *gaiji);
uint8_t pcjx_vid_in(uint16_t port, uint8_t vp_mask, void *priv);
void pcjx_vid_out(uint16_t port, uint8_t value, uint8_t vp_mask, void *priv);
void pcjx_vid_waitstates(void);
uint8_t pcjx_vid_font_read(const pcjx_video_t *video, uint32_t offset);
void pcjx_vid_font_write(pcjx_video_t *video, uint32_t offset, uint8_t value);

extern const device_t pcjx_video_device;

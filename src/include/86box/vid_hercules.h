/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Hercules graphics cards.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *          Connor Hyde / starfrost, <mario64crashed@gmail.com
 * 
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2021 Jasmine Iwanek.
 *          Copyright 2025 starfrost
 */
#ifndef VIDEO_HERCULES_H
#define VIDEO_HERCULES_H

typedef struct {
    mem_mapping_t mapping;

    uint8_t crtc[32];
    uint8_t charbuffer[4096];
    int     crtcreg;

    uint8_t ctrl;
    uint8_t ctrl2;
    uint8_t status;

    uint64_t   dispontime;
    uint64_t   dispofftime;
    pc_timer_t timer;

    int firstline;
    int lastline;

    int      linepos;
    int      displine;
    int      vc;
    int      scanline;
    uint16_t memaddr;
    uint16_t memaddr_backup;
    int      cursorvisible;
    int      cursoron;
    int      dispon;
    int      blink;
    int      vsynctime;
    int      vadj;

    int     lp_ff;
    int     fullchange;

    int     cols[256][2][2];

    lpt_t  *lpt;
    uint8_t *vram;
    int      monitor_index;
    int      prev_monitor_index;
} hercules_t;

#define VIDEO_MONITOR_PROLOGUE()                        \
    {                                                   \
        dev->prev_monitor_index = monitor_index_global; \
        monitor_index_global    = dev->monitor_index;   \
    }
#define VIDEO_MONITOR_EPILOGUE()                        \
    {                                                   \
        monitor_index_global = dev->prev_monitor_index; \
    }

static void *hercules_init(const device_t *info);

#endif /*VIDEO_HERCULES_H*/

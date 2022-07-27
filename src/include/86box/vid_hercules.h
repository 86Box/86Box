/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Hercules graphics cards.
 *
 *
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2021 Jasmine Iwanek.
 */

#ifndef VIDEO_HERCULES_H
#define VIDEO_HERCULES_H

typedef struct {
    mem_mapping_t mapping;

    uint8_t crtc[32], charbuffer[4096];
    int     crtcreg;

    uint8_t ctrl,
        ctrl2,
        stat;

    uint64_t dispontime,
        dispofftime;
    pc_timer_t timer;

    int firstline,
        lastline;

    int linepos,
        displine;
    int vc,
        sc;
    uint16_t ma,
        maback;
    int con, coff,
        cursoron;
    int dispon,
        blink;
    int vsynctime;
    int vadj;

    int lp_ff;
    int fullchange;

    int cols[256][2][2];

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

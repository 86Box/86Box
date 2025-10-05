/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the old and new IBM CGA graphics cards.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Connor Hyde / starfrost, <mario64crashed@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2025      starfrost (refactoring).
 */
#ifndef VIDEO_V6355_H
#define VIDEO_V6355_H

typedef struct v6355_t {
    mem_mapping_t mapping;

    uint8_t       cgastat;
    uint8_t       cgamode;
    uint8_t       cgacol;

    uint8_t       pad[3];
    uint8_t       crtc[32];
    uint8_t       v6355data[106];
    uint8_t       charbuffer[256];

    uint16_t      ma;
    uint16_t      maback;

    /* The V6355 has its own set of registers, as well as the emulated MC6845 */
    int           v6355reg;
    int           crtcreg;
    int           fontbase;
    int           linepos;
    int           displine;
    int           sc;
    int           vc;
    int           cgadispon;
    int           con;
    int           coff;
    int           cursoron;
    int           cgablink;
    int           vsynctime;
    int           vadj;
    int           oddeven;
    int           display_type;
    int           firstline;
    int           lastline;
    int           drawcursor;
    int           revision;
    int           rgb_type;
    int           double_type;

    uint32_t      v6355pal[16];

    uint64_t      dispontime;
    uint64_t      dispofftime;

    pc_timer_t    timer;

    uint8_t *     vram;
} v6355_t;

#endif /*VIDEO_V6355_H*/

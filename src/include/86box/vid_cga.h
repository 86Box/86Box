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
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 */

#ifndef VIDEO_CGA_H
#define VIDEO_CGA_H

typedef struct cga_t {
    mem_mapping_t mapping;

    int     crtcreg;
    uint8_t crtc[32];

    uint8_t cgastat;

    uint8_t cgamode;
    uint8_t cgacol;

    uint8_t lp_strobe;

    int      fontbase;
    int      linepos;
    int      displine;
    int      sc;
    int      vc;
    int      cgadispon;
    int      con;
    int      coff;
    int      cursoron;
    int      cgablink;
    int      vsynctime;
    int      vadj;
    uint16_t ma;
    uint16_t maback;
    int      oddeven;

    uint64_t   dispontime;
    uint64_t   dispofftime;
    pc_timer_t timer;

    int firstline;
    int lastline;

    int drawcursor;

    int fullchange;

    uint8_t *vram;

    uint8_t charbuffer[256];

    int revision;
    int composite;
    int snow_enabled;
    int rgb_type;
    int double_type;
} cga_t;

void    cga_init(cga_t *cga);
void    cga_out(uint16_t addr, uint8_t val, void *priv);
uint8_t cga_in(uint16_t addr, void *priv);
void    cga_write(uint32_t addr, uint8_t val, void *priv);
uint8_t cga_read(uint32_t addr, void *priv);
void    cga_recalctimings(cga_t *cga);
void    cga_poll(void *priv);

#ifdef EMU_DEVICE_H
extern const device_config_t cga_config[];

extern const device_t cga_device;
extern const device_t cga_pravetz_device;
#endif

#endif /*VIDEO_CGA_H*/

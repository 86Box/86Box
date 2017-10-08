/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the old and new IBM CGA graphics cards.
 *
 * Version:	@(#)vid_cga.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

typedef struct cga_t
{
        mem_mapping_t mapping;
        
        int crtcreg;
        uint8_t crtc[32];
        
        uint8_t cgastat;
        
        uint8_t cgamode, cgacol;

        int linepos, displine;
        int sc, vc;
        int cgadispon;
        int con, coff, cursoron, cgablink;
        int vsynctime, vadj;
        uint16_t ma, maback;
        int oddeven;

        int64_t dispontime, dispofftime;
        int64_t vidtime;
        
        int firstline, lastline;
        
        int drawcursor;
        
        uint8_t *vram;
        
        uint8_t charbuffer[256];

	int revision;
	int composite;
	int snow_enabled;
#ifndef __unix
	int rgb_type;
#endif
} cga_t;

void    cga_init(cga_t *cga);
void    cga_out(uint16_t addr, uint8_t val, void *p);
uint8_t cga_in(uint16_t addr, void *p);
void    cga_write(uint32_t addr, uint8_t val, void *p);
uint8_t cga_read(uint32_t addr, void *p);
void    cga_recalctimings(cga_t *cga);
void    cga_poll(void *p);

extern device_t cga_device;

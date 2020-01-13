/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		87C716 'SDAC' true colour RAMDAC emulation header.
 *
 * Version:	@(#)vid_sdac_ramdac.h	1.0.1	2019/09/13
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
typedef struct sdac_ramdac_t
{
    uint16_t regs[256];
    int magic_count,
	windex, rindex,
	reg_ff, rs2;
    uint8_t type, command;
} sdac_ramdac_t;

extern void	sdac_ramdac_out(uint16_t addr, int rs2, uint8_t val, sdac_ramdac_t *ramdac, svga_t *svga);
extern uint8_t	sdac_ramdac_in(uint16_t addr, int rs2, sdac_ramdac_t *ramdac, svga_t *svga);
extern float	sdac_getclock(int clock, void *p);

extern const device_t gendac_ramdac_device;
extern const device_t sdac_ramdac_device;

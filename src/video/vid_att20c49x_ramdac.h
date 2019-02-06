/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Header of the emulation of a Sierra SC1502X RAMDAC.
 *
 *		Used by the TLIVESA1 driver for ET4000.
 *
 * Version:	@(#)vid_sc1502x_ramdac.h	1.0.0	2018/10/04
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
typedef struct
{
    int type;
    int state;
    uint8_t ctrl;
} att49x_ramdac_t;

extern void	att49x_ramdac_out(uint16_t addr, uint8_t val, att49x_ramdac_t *ramdac, svga_t *svga);
extern uint8_t	att49x_ramdac_in(uint16_t addr, att49x_ramdac_t *ramdac, svga_t *svga);

extern const device_t att490_ramdac_device;
extern const device_t att492_ramdac_device;

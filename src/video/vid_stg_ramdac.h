/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		STG1702 true colour RAMDAC emulation header.
 *
 * Version:	@(#)vid_stg_ramdac.h	1.0.0	2018/10/04
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
typedef struct stg_ramdac_t
{
    int magic_count, index;
    uint8_t regs[256];
    uint8_t command;
} stg_ramdac_t;

extern void	stg_ramdac_out(uint16_t addr, uint8_t val, stg_ramdac_t *ramdac, svga_t *svga);
extern uint8_t	stg_ramdac_in(uint16_t addr, stg_ramdac_t *ramdac, svga_t *svga);
extern float	stg_getclock(int clock, void *p);

extern const device_t stg_ramdac_device;

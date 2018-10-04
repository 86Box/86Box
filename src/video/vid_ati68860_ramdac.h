/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ATI 68860 RAMDAC emulation header (for Mach64)
 *
 * Version:	@(#)vid_ati68860.h	1.0.0	2018/10/04
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
typedef struct ati68860_ramdac_t
{
    uint8_t regs[16];
    void (*render)(struct svga_t *svga);

    int dac_addr, dac_pos;
    int dac_r, dac_g;
    PALETTE pal;
    uint32_t pallook[2];

    int ramdac_type;
} ati68860_ramdac_t;

extern void	ati68860_ramdac_out(uint16_t addr, uint8_t val, ati68860_ramdac_t *ramdac, svga_t *svga);
extern uint8_t	ati68860_ramdac_in(uint16_t addr, ati68860_ramdac_t *ramdac, svga_t *svga);
extern void	ati68860_set_ramdac_type(ati68860_ramdac_t *ramdac, int type);

extern const device_t ati68860_ramdac_device;

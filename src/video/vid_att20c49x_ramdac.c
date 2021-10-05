/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of a AT&T 20c490/491 and 492/493 RAMDAC.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>


typedef struct
{
    int type;
    int state;
    uint8_t ctrl;
} att49x_ramdac_t;


enum
{
	ATT_490 = 0,
	ATT_491,
	ATT_492
};


static void
att49x_ramdac_control(uint8_t val, void *p, svga_t *svga)
{
	att49x_ramdac_t *ramdac = (att49x_ramdac_t *) p;
	ramdac->ctrl = val;
	switch ((ramdac->ctrl >> 5) & 7) {
		case 0:
		case 1:
		case 2:
		case 3:
		svga->bpp = 8;
		break;
		case 4:
		case 5:
		svga->bpp = 15;
		break;
		case 6:
		svga->bpp = 16;
		break;
		case 7:
		svga->bpp = 24;
		break;
	}
	if (ramdac->type == ATT_490 || ramdac->type == ATT_491)
		svga_set_ramdac_type(svga, (val & 2) ? RAMDAC_8BIT : RAMDAC_6BIT);
	svga_recalctimings(svga);
}

void
att49x_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *p, svga_t *svga)
{
    att49x_ramdac_t *ramdac = (att49x_ramdac_t *) p;
    uint8_t rs = (addr & 0x03);
    rs |= ((!!rs2) << 2);

    switch (rs) {
	case 0x00:
	case 0x01:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x07:
		svga_out(addr, val, svga);
		ramdac->state = 0;
		break;
	case 0x02:
		switch (ramdac->state) {
			case 4:
				att49x_ramdac_control(val, ramdac, svga);
				break;
			default:
				svga_out(addr, val, svga);
				break;
		}
		break;
	case 0x06:
		att49x_ramdac_control(val, ramdac, svga);
		ramdac->state = 0;
		break;
    }
}


uint8_t
att49x_ramdac_in(uint16_t addr, int rs2, void *p, svga_t *svga)
{
    att49x_ramdac_t *ramdac = (att49x_ramdac_t *) p;
    uint8_t temp = 0xff;
    uint8_t rs = (addr & 0x03);
    rs |= ((!!rs2) << 2);

    switch (rs) {
	case 0x00:
	case 0x01:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x07:
		temp = svga_in(addr, svga);
		ramdac->state = 0;
		break;
	case 0x02:
		switch (ramdac->state) {
			case 1:
			case 2: case 3:
				temp = 0x00;
				ramdac->state++;
				break;
			case 4:
				temp = ramdac->ctrl;
				ramdac->state = 0;
				break;
			default:
				temp = svga_in(addr, svga);
				ramdac->state++;
				break;
		}
		break;
	case 0x06:
		temp = ramdac->ctrl;
		ramdac->state = 0;
		break;
    }

    return temp;
}


static void *
att49x_ramdac_init(const device_t *info)
{
    att49x_ramdac_t *ramdac = (att49x_ramdac_t *) malloc(sizeof(att49x_ramdac_t));
    memset(ramdac, 0, sizeof(att49x_ramdac_t));

    ramdac->type = info->local;
    
    return ramdac;
}


static void
att49x_ramdac_close(void *priv)
{
    att49x_ramdac_t *ramdac = (att49x_ramdac_t *) priv;

    if (ramdac)
	free(ramdac);
}


const device_t att490_ramdac_device =
{
        "AT&T 20c490 RAMDAC",
        0, ATT_490,
        att49x_ramdac_init, att49x_ramdac_close,
	NULL, { NULL }, NULL, NULL
};

const device_t att491_ramdac_device =
{
        "AT&T 20c491 RAMDAC",
        0, ATT_491,
        att49x_ramdac_init, att49x_ramdac_close,
	NULL, { NULL }, NULL, NULL
};

const device_t att492_ramdac_device =
{
        "AT&T 20c492 RAMDAC",
        0, ATT_492,
        att49x_ramdac_init, att49x_ramdac_close,
	NULL, { NULL }, NULL, NULL
};
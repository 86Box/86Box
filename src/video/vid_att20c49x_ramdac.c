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
 * Version:	@(#)vid_att20c49x_ramdac.c	1.0.1	2019/01/12
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
#include "86box.h"
#include "device.h"
#include "mem.h"
#include "timer.h"
#include "video.h"
#include "vid_svga.h"


typedef struct
{
    int type;
    int state;
    uint8_t ctrl;
} att49x_ramdac_t;


enum
{
	ATT_490_1 = 0,
	ATT_492_3
};


void
att49x_ramdac_out(uint16_t addr, uint8_t val, void *p, svga_t *svga)
{
    att49x_ramdac_t *ramdac = (att49x_ramdac_t *) p;

    switch (addr) {
	case 0x3C6:
		if (ramdac->state == 4) {
			ramdac->state = 0;
			ramdac->ctrl = val;
			if (ramdac->type == ATT_490_1)
				svga_set_ramdac_type(svga, (val & 2) ? RAMDAC_8BIT : RAMDAC_6BIT);
			switch (val)
			{
				case 0:
				svga->bpp = 8;
				break;
				
				case 0x20:
				svga->bpp = 15;
				break;
				
				case 0x40:
				svga->bpp = 24;
				break;
				
				case 0x60:
				svga->bpp = 16;
				break;
				
				case 0x80:
				case 0xa0:
				svga->bpp = 15;
				break;
				
				case 0xc0:
				svga->bpp = 16;
				break;
				
				case 0xe0:
				svga->bpp = 24;
				break;
			}
			svga_recalctimings(svga);
			return;
		}
		ramdac->state = 0;
		break;
	case 0x3C7:
	case 0x3C8:
	case 0x3C9:
		ramdac->state = 0;
		break;
    }

    svga_out(addr, val, svga);
}


uint8_t
att49x_ramdac_in(uint16_t addr, void *p, svga_t *svga)
{
    att49x_ramdac_t *ramdac = (att49x_ramdac_t *) p;
    uint8_t temp = svga_in(addr, svga);

    switch (addr) {
	case 0x3C6:
		if (ramdac->state == 4) {
			ramdac->state = 0;
			temp = ramdac->ctrl;
			break;
		}
		ramdac->state++;
		break;
	case 0x3C7:
	case 0x3C8:
	case 0x3C9:
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
        "AT&T 20c490/20c491 RAMDAC",
        0, ATT_490_1,
        att49x_ramdac_init, att49x_ramdac_close,
	NULL, NULL, NULL, NULL
};

const device_t att492_ramdac_device =
{
        "AT&T 20c492/20c493 RAMDAC",
        0, ATT_492_3,
        att49x_ramdac_init, att49x_ramdac_close,
	NULL, NULL, NULL, NULL
};

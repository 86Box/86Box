/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of Sierra SC11483 and SC11487 RAMDACs.
 *
 *		Used by the S3 911 and 924 chips.
 *
 *
 *
 * Authors:	TheCollector1995, <mariogplayer90@gmail.com>
 *
 *		Copyright 2020 TheCollector1995.
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
	int rs2;
    uint8_t ctrl;
} sc1148x_ramdac_t;


void
sc1148x_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *p, svga_t *svga)
{
    sc1148x_ramdac_t *ramdac = (sc1148x_ramdac_t *) p;
    uint8_t rs = (addr & 0x03);
    rs |= ((!!rs2) << 2);    

    switch (addr) {
	case 0x3c6:
		if (ramdac->state == 4) {
			ramdac->state = 0;
			ramdac->ctrl = val;
			if (ramdac->ctrl & 0xa0) {
				if ((ramdac->ctrl & 0x40) && (ramdac->type == 1))
					svga->bpp = 16;
				else
					svga->bpp = 15;
			} else
				svga->bpp = 8;
			svga_recalctimings(svga);
			return;
		}
		ramdac->state = 0;
		break;

	case 0x3c7: case 0x3c8:
	case 0x3c9:
		ramdac->state = 0;
		svga_out(addr, val, svga);
		break;
    }
}


uint8_t
sc1148x_ramdac_in(uint16_t addr, int rs2, void *p, svga_t *svga)
{
    sc1148x_ramdac_t *ramdac = (sc1148x_ramdac_t *) p;
    uint8_t ret = 0xff;
    uint8_t rs = (addr & 0x03);
    rs |= ((!!rs2) << 2);    

    switch (addr) {
	case 0x3c6:
		if (ramdac->state == 4) {
			ramdac->state = 0;
			ret = ramdac->ctrl;
			if (ramdac->type == 1) {
				if (((ramdac->ctrl >> 5) == 1) || ((ramdac->ctrl >> 5) == 3))
					ret |= 1;
				else
					ret &= ~1;
			}
			return ret;
		}
		ramdac->state++;
		break;

	case 0x3c7: case 0x3c8:
	case 0x3c9:
		ret = svga_in(addr, svga);
		ramdac->state = 0;
		break;
    }

    return ret;
}


static void *
sc1148x_ramdac_init(const device_t *info)
{
    sc1148x_ramdac_t *ramdac = (sc1148x_ramdac_t *) malloc(sizeof(sc1148x_ramdac_t));
    memset(ramdac, 0, sizeof(sc1148x_ramdac_t));

    ramdac->type = info->local;

    return ramdac;
}


static void
sc1148x_ramdac_close(void *priv)
{
    sc1148x_ramdac_t *ramdac = (sc1148x_ramdac_t *) priv;

    if (ramdac)
	free(ramdac);
}

const device_t sc11483_ramdac_device =
{
        "Sierra SC11483 RAMDAC",
        0, 0,
        sc1148x_ramdac_init, sc1148x_ramdac_close,
	NULL, { NULL }, NULL, NULL
};

const device_t sc11487_ramdac_device =
{
        "Sierra SC11487 RAMDAC",
        0, 1,
        sc1148x_ramdac_init, sc1148x_ramdac_close,
	NULL, { NULL }, NULL, NULL
};

const device_t sc11484_nors2_ramdac_device =
{
        "Sierra SC11484 RAMDAC (no RS2 signal)",
        0, 2,
        sc1148x_ramdac_init, sc1148x_ramdac_close,
	NULL, { NULL }, NULL, NULL
};
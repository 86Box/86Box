/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of a Sierra SC1502X RAMDAC.
 *
 *		Used by the TLIVESA1 driver for ET4000.
 *
 * Version:	@(#)vid_sc1502x_ramdac.c	1.0.3	2018/10/04
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
#include "../86box.h"
#include "../device.h"
#include "../mem.h"
#include "../timer.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_sc1502x_ramdac.h"


void
sc1502x_ramdac_out(uint16_t addr, uint8_t val, sc1502x_ramdac_t *ramdac, svga_t *svga)
{
    int oldbpp = 0;

    switch (addr) {
	case 0x3C6:
		if (ramdac->state == 4) {
			ramdac->state = 0;
			if (val == 0xFF)
				break;
			ramdac->ctrl = val;
			oldbpp = svga->bpp;
			switch ((val & 1) | ((val & 0xc0) >> 5)) {
				case 0:
					svga->bpp = 8;
					break;
				case 2:
				case 3:
					switch (val & 0x20) {
					case 0x00:
						svga->bpp = 32;
						break;
					case 0x20:
						svga->bpp = 24;
						break;
					}
					break;
				case 4:
				case 5:
					svga->bpp = 15;
					break;
				case 6:
					svga->bpp = 16;
					break;
				case 7:
					if (val & 4) {
				                switch (val & 0x20) {
							case 0x00:
								svga->bpp = 32;
								break;
							case 0x20:
								svga->bpp = 24;
								break;
						}
						break;
					} else {
						svga->bpp = 16;
						break;
					}
					break;
			}
			if (oldbpp != svga->bpp)
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
sc1502x_ramdac_in(uint16_t addr, sc1502x_ramdac_t *ramdac, svga_t *svga)
{
    uint8_t temp;
    temp = svga_in(addr, svga);

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
sc1502x_ramdac_init(const device_t *info)
{
    sc1502x_ramdac_t *ramdac = (sc1502x_ramdac_t *) malloc(sizeof(sc1502x_ramdac_t));
    memset(ramdac, 0, sizeof(sc1502x_ramdac_t));

    return ramdac;
}


static void
sc1502x_ramdac_close(void *priv)
{
    sc1502x_ramdac_t *ramdac = (sc1502x_ramdac_t *) priv;

    if (ramdac)
	free(ramdac);
}


const device_t sc1502x_ramdac_device =
{
        "Sierra SC1502x RAMDAC",
        0, 0,
        sc1502x_ramdac_init, sc1502x_ramdac_close,
	NULL, NULL, NULL, NULL
};

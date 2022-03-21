/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Trident TKD8001 RAMDAC emulation.
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
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/video.h>
#include <86box/vid_svga.h>


typedef struct tkd8001_ramdac_t
{
    int state;
    uint8_t ctrl;
} tkd8001_ramdac_t;


void
tkd8001_ramdac_out(uint16_t addr, uint8_t val, void *p, svga_t *svga)
{
    tkd8001_ramdac_t *ramdac = (tkd8001_ramdac_t *) p;

    switch (addr) {
	case 0x3C6:
		if (ramdac->state == 4) {
			ramdac->state = 0;
			ramdac->ctrl = val;
			switch (val >> 5) {
				case 0:
				case 1:
				case 2:
				case 3:
					svga->bpp = 8;
					break;
				case 5:
					svga->bpp = 15;
					break;
				case 6:
					svga->bpp = 24;
					break;
				case 7:
					svga->bpp = 16;
					break;
			}
			return;
		}
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
tkd8001_ramdac_in(uint16_t addr, void *p, svga_t *svga)
{
    tkd8001_ramdac_t *ramdac = (tkd8001_ramdac_t *) p;

    switch (addr) {
	case 0x3C6:
		if (ramdac->state == 4)
			return ramdac->ctrl;
		ramdac->state++;
		break;
	case 0x3C7:
	case 0x3C8:
	case 0x3C9:
		ramdac->state = 0;
		break;
    }
    return svga_in(addr, svga);
}


static void *
tkd8001_ramdac_init(const device_t *info)
{
    tkd8001_ramdac_t *ramdac = (tkd8001_ramdac_t *) malloc(sizeof(tkd8001_ramdac_t));
    memset(ramdac, 0, sizeof(tkd8001_ramdac_t));

    return ramdac;
}


static void
tkd8001_ramdac_close(void *priv)
{
    tkd8001_ramdac_t *ramdac = (tkd8001_ramdac_t *) priv;

    if (ramdac)
	free(ramdac);
}

const device_t tkd8001_ramdac_device = {
    .name = "Trident TKD8001 RAMDAC",
    .internal_name = "tkd8001_ramdac",
    .flags = 0,
    .local = 0,
    .init = tkd8001_ramdac_init,
    .close = tkd8001_ramdac_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

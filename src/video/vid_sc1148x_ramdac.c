/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of Sierra SC1148x RAMDACs and clones (e.g.: Winbond).
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
    int     type;
    int     state;
    int     rs2;
    uint8_t ctrl;
} sc1148x_ramdac_t;

void
sc1148x_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *p, svga_t *svga)
{
    sc1148x_ramdac_t *ramdac = (sc1148x_ramdac_t *) p;
    uint8_t           rs     = (addr & 0x03) | ((!!rs2) << 2);
    int               oldbpp = 0;

    switch (rs) {
        case 2:
        case 6:
            switch (ramdac->state) {
                case 4:
                    ramdac->state = 0;
                    if (val == 0xff)
                        break;
                    ramdac->ctrl = val;
                    ramdac->ctrl = (ramdac->ctrl & ~1) | ((((val >> 2) ^ val) & (val & 0x20)) >> 5);
                    oldbpp       = svga->bpp;
                    switch (ramdac->type) {
                        case 0: /* Sierra Mark 2 (11483)*/
                        case 2: /* Sierra Mark 2 (11484)*/
                        case 3: /* Sierra Mark 1 (11486)*/
                            if (val & 0xa0) {
                                svga->bpp = 15;
                            } else if (val == 0x00)
                                svga->bpp = 8;
                            break;
                        case 1: /* Sierra Mark 3 (11487)*/
                            if (val & 0xa0) {
                                if (val & 0x40)
                                    svga->bpp = 16;
                                else
                                    svga->bpp = 15;
                            } else if (val == 0x00)
                                svga->bpp = 8;
                            break;
                    }
                    if (oldbpp != svga->bpp)
                        svga_recalctimings(svga);
                    return;
                default:
                    svga_out(addr, val, svga);
                    break;
            }
            break;

        default:
            ramdac->state = 0;
            svga_out(addr, val, svga);
            break;
    }
}

uint8_t
sc1148x_ramdac_in(uint16_t addr, int rs2, void *p, svga_t *svga)
{
    sc1148x_ramdac_t *ramdac = (sc1148x_ramdac_t *) p;
    uint8_t           ret = 0xff, rs = (addr & 0x03) | ((!!rs2) << 2);

    switch (rs) {
        case 2:
        case 6:
            switch (ramdac->state) {
                case 1:
                case 2:
                case 3:
                    ret = 0x00;
                    ramdac->state++;
                    break;
                case 4:
                    ret = ramdac->ctrl;
                    ret = (ret & ~0x18) | (svga->dac_mask & 0x18);
                    break;
                default:
                    ret = svga_in(addr, svga);
                    ramdac->state++;
                    break;
            }
            break;

        default:
            ret           = svga_in(addr, svga);
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

const device_t sc11483_ramdac_device = {
    .name          = "Sierra SC11483 RAMDAC",
    .internal_name = "sc11483_ramdac",
    .flags         = 0,
    .local         = 0,
    .init          = sc1148x_ramdac_init,
    .close         = sc1148x_ramdac_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sc11487_ramdac_device = {
    .name          = "Sierra SC11487 RAMDAC",
    .internal_name = "sc11487_ramdac",
    .flags         = 0,
    .local         = 1,
    .init          = sc1148x_ramdac_init,
    .close         = sc1148x_ramdac_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sc11484_nors2_ramdac_device = {
    .name          = "Sierra SC11484 RAMDAC (no RS2 signal)",
    .internal_name = "sc11484_nors2_ramdac",
    .flags         = 0,
    .local         = 2,
    .init          = sc1148x_ramdac_init,
    .close         = sc1148x_ramdac_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sc11486_ramdac_device = {
    .name          = "Sierra SC11486 RAMDAC",
    .internal_name = "sc11486_ramdac",
    .flags         = 0,
    .local         = 3,
    .init          = sc1148x_ramdac_init,
    .close         = sc1148x_ramdac_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

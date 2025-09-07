/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of a AT&T 2xc498 RAMDAC.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2018 Miran Grca.
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

typedef struct att498_ramdac_t {
    int     type;
    int     state;
    int     loop;
    uint8_t ctrl;
} att498_ramdac_t;

static void
att498_ramdac_control(uint8_t val, void *priv, svga_t *svga)
{
    att498_ramdac_t *ramdac = (att498_ramdac_t *) priv;
    ramdac->ctrl            = val;

    if (val == 0xff)
        return;

    switch ((ramdac->ctrl >> 4) & 0x0f) {
        default:
            svga->bpp = 8;
            break;
        case 1:
            if (ramdac->ctrl & 4)
                svga->bpp = 15;
            else
                svga->bpp = 8;
            break;
        case 3:
        case 6:
            svga->bpp = 16;
            break;
        case 5:
        case 7:
            svga->bpp = 32;
            break;
        case 0x0e:
            svga->bpp = 24;
            break;
    }

    svga_set_ramdac_type(svga, (ramdac->ctrl & 2) ? RAMDAC_8BIT : RAMDAC_6BIT);
    svga_recalctimings(svga);
}

void
att498_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga)
{
    att498_ramdac_t *ramdac = (att498_ramdac_t *) priv;
    uint8_t          rs     = (addr & 0x03);
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
                    att498_ramdac_control(val, ramdac, svga);
                    break;
                default:
                    svga_out(addr, val, svga);
                    break;
            }
            break;
        case 0x06:
            att498_ramdac_control(val, ramdac, svga);
            break;

        default:
            break;
    }
}

uint8_t
att498_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga)
{
    att498_ramdac_t *ramdac = (att498_ramdac_t *) priv;
    uint8_t          temp   = 0xff;
    uint8_t          rs     = (addr & 0x03);
    rs |= ((!!rs2) << 2);

    switch (rs) {
        case 0x00:
        case 0x01:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x07:
            temp          = svga_in(addr, svga);
            ramdac->state = 0;
            break;
        case 0x02:
            switch (ramdac->state) {
                case 4:
                    temp = ramdac->ctrl;
                    ramdac->state++;
                    break;
                case 5:
                    temp = 0x84;
                    ramdac->state++;
                    break;
                case 6:
                    temp          = ramdac->ctrl;
                    ramdac->state = 0;
                    break;
                default:
                    temp = svga_in(addr, svga);
                    ramdac->state++;
                    break;
            }
            break;
        case 0x06:
            temp          = ramdac->ctrl;
            ramdac->state = 0;
            break;

        default:
            break;
    }

    return temp;
}

static void *
att498_ramdac_init(const device_t *info)
{
    att498_ramdac_t *ramdac = (att498_ramdac_t *) malloc(sizeof(att498_ramdac_t));
    memset(ramdac, 0, sizeof(att498_ramdac_t));

    ramdac->type = info->local;

    return ramdac;
}

static void
att498_ramdac_close(void *priv)
{
    att498_ramdac_t *ramdac = (att498_ramdac_t *) priv;

    if (ramdac)
        free(ramdac);
}

const device_t att498_ramdac_device = {
    .name          = "AT&T 22c498 RAMDAC",
    .internal_name = "att498_ramdac",
    .flags         = 0,
    .local         = 0,
    .init          = att498_ramdac_init,
    .close         = att498_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

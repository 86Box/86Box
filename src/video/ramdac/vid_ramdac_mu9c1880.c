/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the MUSIC MU9C1880 RAMDAC.
 *
 *          Used by the S3 924 chips.
 *
 * Authors: TheCollector1995, <mariogplayer90@gmail.com>
 *
 *          Copyright 2026 TheCollector1995.
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

typedef struct mu9c1880_ramdac_t {
    uint8_t  ctrl;
    uint8_t  state;
    uint8_t  model;
    uint8_t  no_rs2;
} mu9c1880_ramdac_t;

static void
mu9c1880_ramdac_control(uint8_t val, void *priv, svga_t *svga)
{
    mu9c1880_ramdac_t *ramdac = (mu9c1880_ramdac_t *) priv;

    ramdac->ctrl = val;

    if (val == 0xff)
        return;

    if (ramdac->ctrl & 0x80) {
        if (ramdac->ctrl & 0x20) {
            if ((ramdac->ctrl & 0x06) == 0x06)
                svga->bpp = 16; /*16-bit Direct Color (XGA)*/
            else
                svga->bpp = 15; /*15-bit Direct Color (TARGA)*/
        } else
            svga->bpp = 24; /*24-bit Direct Color*/
    } else
        svga->bpp = 8; /*Pseudo-color*/

    svga_recalctimings(svga);
}

void
mu9c1880_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga)
{
    mu9c1880_ramdac_t *ramdac = (mu9c1880_ramdac_t *) priv;
    uint8_t rs = (addr & 0x03) | ((!!rs2) << 2);

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
                    mu9c1880_ramdac_control(val, ramdac, svga);
                    break;
                default:
                    svga_out(addr, val, svga);
                    break;
            }
            break;
        case 0x06:
            mu9c1880_ramdac_control(val, ramdac, svga);
            ramdac->state = 0;
            break;

        default:
            break;
    }
}

uint8_t
mu9c1880_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga)
{
    mu9c1880_ramdac_t *ramdac = (mu9c1880_ramdac_t *) priv;
    uint8_t temp = 0xff;
    uint8_t rs = (addr & 0x03) | ((!!rs2) << 2);

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
                case 2:
                case 3:
                    temp = 0x00;
                    ramdac->state++;
                    break;
                case 4:
                    temp = ramdac->ctrl;
                    ramdac->state++;
                    break;
                case 5:
                    temp = 0x8e;
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

        default:
            break;
    }

    return temp;
}

static void *
mu9c1880_ramdac_init(const device_t *info)
{
    mu9c1880_ramdac_t *ramdac = (mu9c1880_ramdac_t *) calloc(1, sizeof(mu9c1880_ramdac_t));

    return ramdac;
}

static void
mu9c1880_ramdac_close(void *priv)
{
    mu9c1880_ramdac_t *ramdac = (mu9c1880_ramdac_t *) priv;

    if (ramdac != NULL) {
        free(ramdac);
        ramdac = NULL;
    }
}

const device_t mu9c1880_ramdac_device = {
    .name          = "MUSIC MU9c1880 RAMDAC",
    .internal_name = "mu9c1880_ramdac",
    .flags         = 0,
    .local         = 0,
    .init          = mu9c1880_ramdac_init,
    .close         = mu9c1880_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

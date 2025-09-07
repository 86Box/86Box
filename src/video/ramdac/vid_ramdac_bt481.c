/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Brooktree BT481 true colour RAMDAC
 *          family.
 *
 *
 *
 * Authors: TheCollector1995.
 *
 *          Copyright 2024 TheCollector1995.
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
#include <86box/plat_unused.h>

typedef struct bt481_ramdac_t {
    int     state;
    uint8_t cmd;
} bt481_ramdac_t;

static void
bt481_ramdac_command(uint8_t val, void *priv, svga_t *svga)
{
    bt481_ramdac_t *ramdac  = (bt481_ramdac_t *) priv;
    ramdac->cmd             = val;
    pclog("RAMDAC CMD=%02x.\n", val);
    switch ((ramdac->cmd >> 4) & 0x0f) {
        default:
        case 0x00:
            svga->bpp = 8;
            break;
        case 0x08:
        case 0x0a:
            svga->bpp = 15;
            break;
        case 0x09:
        case 0x0c:
            svga->bpp = 16;
            break;
        case 0x0e:
        case 0x0f:
            svga->bpp = 24;
            break;
    }
    svga_recalctimings(svga);
}

void
bt481_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga)
{
    bt481_ramdac_t *ramdac = (bt481_ramdac_t *) priv;
    uint8_t             rs = (addr & 0x03) | ((!!rs2) << 2);

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
            pclog("RAMDAC Write State=%x.\n", ramdac->state);
            switch (ramdac->state) {
                case 4:
                    bt481_ramdac_command(val, ramdac, svga);
                    break;
                default:
                    svga_out(addr, val, svga);
                    break;
            }
            break;
        case 0x06:
            bt481_ramdac_command(val, ramdac, svga);
            ramdac->state = 0;
            break;

        default:
            break;
    }
}

uint8_t
bt481_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga)
{
    bt481_ramdac_t * ramdac = (bt481_ramdac_t *) priv;
    uint8_t          temp   = 0xff;
    uint8_t          rs     = (addr & 0x03) | ((!!rs2) << 2);

    switch (rs) {
        case 0x02:
        case 0x06:
            switch (ramdac->state) {
                case 4:
                    temp = ramdac->cmd;
                    break;
                default:
                    temp = svga_in(addr, svga);
                    ramdac->state++;
                    break;
            }
            break;

        default:
            temp          = svga_in(addr, svga);
            ramdac->state = 0;
            break;
    }

    pclog("RAMDAC IN=%02x, ret=%02x.\n", rs, temp);
    return temp;
}

static void *
bt481_ramdac_init(UNUSED(const device_t *info))
{
    bt481_ramdac_t *ramdac = (bt481_ramdac_t *) malloc(sizeof(bt481_ramdac_t));
    memset(ramdac, 0, sizeof(bt481_ramdac_t));

    return ramdac;
}

static void
bt481_ramdac_close(void *priv)
{
    bt481_ramdac_t *ramdac = (bt481_ramdac_t *) priv;

    if (ramdac)
        free(ramdac);
}

const device_t bt481_ramdac_device = {
    .name          = "Brooktree Bt481 RAMDAC",
    .internal_name = "bt481_ramdac",
    .flags         = 0,
    .local         = 0,
    .init          = bt481_ramdac_init,
    .close         = bt481_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

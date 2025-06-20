/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of a Sierra SC1502X RAMDAC.
 *
 *          Used by the TLIVESA1 driver for ET4000.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
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
#include <86box/plat_unused.h>

typedef struct sc1502x_ramdac_t {
    int     state;
    uint8_t ctrl;
    uint8_t idx;
    uint8_t regs[256];
    uint32_t pixel_mask;
    uint8_t enable_ext;
} sc1502x_ramdac_t;

static void
sc1502x_ramdac_bpp(uint8_t val, sc1502x_ramdac_t *ramdac, svga_t *svga)
{
    int oldbpp = 0;
    if (val == 0xff)
        return;
    ramdac->ctrl = val;
    oldbpp       = svga->bpp;
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

                default:
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

                    default:
                        break;
                }
            } else
                svga->bpp = 16;
            break;

        default:
            break;
    }
    if (oldbpp != svga->bpp)
        svga_recalctimings(svga);
}

void
sc1502x_ramdac_out(uint16_t addr, uint8_t val, void *priv, svga_t *svga)
{
    sc1502x_ramdac_t *ramdac = (sc1502x_ramdac_t *) priv;

    switch (addr) {
        case 0x3C6:
            if (ramdac->state == 0)
                ramdac->enable_ext = (val == 0x10);

            if (ramdac->state == 4) {
                ramdac->state = 0;
                sc1502x_ramdac_bpp(val, ramdac, svga);
                return;
            }
            ramdac->state = 0;
            break;
        case 0x3C7:
            if (ramdac->enable_ext) {
                ramdac->idx = val;
                return;
            }
            ramdac->state = 0;
            break;
        case 0x3C8:
            if (ramdac->enable_ext) {
                switch (ramdac->idx) {
                    case 8:
                        ramdac->regs[ramdac->idx] = val;
                        svga_set_ramdac_type(svga, (ramdac->regs[ramdac->idx] & 1) ? RAMDAC_8BIT : RAMDAC_6BIT);
                        break;
                    case 0x0d:
                        ramdac->pixel_mask = val & svga->dac_mask;
                        break;
                    case 0x0e:
                        ramdac->pixel_mask |= ((val & svga->dac_mask) << 8);
                        break;
                    case 0x0f:
                        ramdac->pixel_mask |= ((val & svga->dac_mask) << 16);
                        break;
                    default:
                        ramdac->regs[ramdac->idx] = val;
                        break;
                }
                return;
            }
            ramdac->state = 0;
            break;
        case 0x3C9:
            if (ramdac->enable_ext)
                return;
            ramdac->state = 0;
            break;

        default:
            break;
    }
    svga_out(addr, val, svga);
}

uint8_t
sc1502x_ramdac_in(uint16_t addr, void *priv, svga_t *svga)
{
    sc1502x_ramdac_t *ramdac = (sc1502x_ramdac_t *) priv;
    uint8_t           temp   = svga_in(addr, svga);

    switch (addr) {
        case 0x3C6:
            if (ramdac->state == 4) {
                ramdac->state = 0;
                temp          = ramdac->ctrl;
                break;
            }
            ramdac->state++;
            break;
        case 0x3C7:
            ramdac->state = 0;
            break;
        case 0x3C8:
            if (ramdac->enable_ext) {
                switch (ramdac->idx) {
                    case 9:
                        temp = 0x53;
                        break;
                    case 0x0a:
                        temp = 0x3a;
                        break;
                    case 0x0b:
                        temp = 0xb1;
                        break;
                    case 0x0c:
                        temp = 0x41;
                        break;
                    case 0x0d:
                        temp = ramdac->pixel_mask & 0xff;
                        break;
                    case 0x0e:
                        temp = ramdac->pixel_mask >> 8;
                        break;
                    case 0x0f:
                        temp = ramdac->pixel_mask >> 16;
                        break;
                    default:
                        temp = ramdac->regs[ramdac->idx];
                        break;
                }
            } else
                ramdac->state = 0;
            break;
        case 0x3C9:
            if (ramdac->enable_ext)
                temp = ramdac->idx;
            else
                ramdac->state = 0;
            break;

        default:
            break;
    }

    return temp;
}

static void *
sc1502x_ramdac_init(UNUSED(const device_t *info))
{
    sc1502x_ramdac_t *ramdac = (sc1502x_ramdac_t *) malloc(sizeof(sc1502x_ramdac_t));
    memset(ramdac, 0, sizeof(sc1502x_ramdac_t));

    ramdac->ctrl = 0;
    ramdac->pixel_mask = 0xffffff;

    return ramdac;
}

static void
sc1502x_ramdac_close(void *priv)
{
    sc1502x_ramdac_t *ramdac = (sc1502x_ramdac_t *) priv;

    if (ramdac)
        free(ramdac);
}

const device_t sc1502x_ramdac_device = {
    .name          = "Sierra SC1502x RAMDAC",
    .internal_name = "sc1502x_ramdac",
    .flags         = 0,
    .local         = 0,
    .init          = sc1502x_ramdac_init,
    .close         = sc1502x_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

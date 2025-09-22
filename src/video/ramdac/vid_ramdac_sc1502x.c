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
    int     use_rs2;
    uint8_t ctrl;
    uint8_t idx;
    uint8_t regs[256];
    uint32_t pixel_mask;
} sc1502x_ramdac_t;

static void
sc1502x_ramdac_bpp(sc1502x_ramdac_t *ramdac, svga_t *svga)
{
    int oldbpp = svga->bpp;
    //pclog("BPP Val=%02x, truecolortype=%02x.\n", ramdac->ctrl, ramdac->regs[0x10] & 0x01);
    if (ramdac->ctrl & 0x80) {
        if (ramdac->ctrl & 0x40) {
            svga->bpp = 16;
        } else
            svga->bpp = 15;
    } else {
        if (ramdac->ctrl & 0x40) {
            if (ramdac->regs[0x10] & 0x01)
                svga->bpp = 32;
            else if (ramdac->ctrl & 0x20)
                svga->bpp = 24;
            else
                svga->bpp = 32;
        } else
            svga->bpp = 8;
    }
    //pclog("SVGA BPP=%d.\n", svga->bpp);
    if (oldbpp != svga->bpp)
        svga_recalctimings(svga);
}

void
sc1502x_ramdac_out(uint16_t addr, uint8_t val, void *priv, svga_t *svga)
{
    sc1502x_ramdac_t *ramdac = (sc1502x_ramdac_t *) priv;

    switch (addr) {
        case 0x3C6:
            if ((ramdac->state == 4) || (ramdac->ctrl & 0x10)) {
                ramdac->state = 0;
                ramdac->ctrl = val;
                if (val != 0xff)
                    sc1502x_ramdac_bpp(ramdac, svga);
                return;
            }
            ramdac->state = 0;
            svga_out(addr, val, svga);
            break;
        case 0x3C7:
            if (ramdac->ctrl & 0x10)
                ramdac->idx = val;
            else
                svga_out(addr, val, svga);

            ramdac->state = 0;
            break;
        case 0x3C8:
            if (ramdac->ctrl & 0x10) {
                switch (ramdac->idx) {
                    case 8:
                        ramdac->regs[8] = val;
                        svga->ramdac_type = (val & 0x01) ? RAMDAC_8BIT : RAMDAC_6BIT;
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
                    case 0x10:
                        ramdac->regs[0x10] = val;
                        sc1502x_ramdac_bpp(ramdac, svga);
                        break;
                    default:
                        ramdac->regs[ramdac->idx] = val;
                        break;
                }
            } else
                svga_out(addr, val, svga);

            ramdac->state = 0;
            break;
        case 0x3C9:
            ramdac->state = 0;
            svga_out(addr, val, svga);
            break;

        default:
            break;
    }
}

void
sc1502x_rs2_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga)
{
    sc1502x_ramdac_t *ramdac = (sc1502x_ramdac_t *) priv;
    uint8_t rs = (addr & 0x03);
    rs |= ((!!rs2) << 2);

    //pclog("RS=%02x, Write=%02x.\n", rs, val);
    switch (rs) {
        case 0x00:
            if (ramdac->ctrl & 0x10) {
                //pclog("RAMDAC IDX=%02x, Write=%02x.\n", ramdac->idx, val);
                switch (ramdac->idx) {
                    case 8:
                        ramdac->regs[8] = val;
                        svga->ramdac_type = (val & 0x01) ? RAMDAC_8BIT : RAMDAC_6BIT;
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
                    case 0x10:
                        ramdac->regs[0x10] = val;
                        sc1502x_ramdac_bpp(ramdac, svga);
                        break;
                    default:
                        ramdac->regs[ramdac->idx] = val;
                        break;
                }
            } else
                svga_out(addr, val, svga);
            break;
        case 0x01:
            svga_out(addr, val, svga);
            break;
        case 0x02:
            if (ramdac->ctrl & 0x10) {
                ramdac->ctrl = val;
                if (val != 0xff)
                    sc1502x_ramdac_bpp(ramdac, svga);
            } else
                svga_out(addr, val, svga);
            break;
        case 0x03:
            if (ramdac->ctrl & 0x10)
                ramdac->idx = val;
            else
                svga_out(addr, val, svga);
            break;
        case 0x04:
        case 0x05:
        case 0x07:
            svga_out(addr, val, svga);
            break;
        case 0x06:
            ramdac->ctrl = val;
            if (val != 0xff)
                sc1502x_ramdac_bpp(ramdac, svga);
            break;

        default:
            svga_out(addr, val, svga);
            break;
    }
}

uint8_t
sc1502x_ramdac_in(uint16_t addr, void *priv, svga_t *svga)
{
    sc1502x_ramdac_t *ramdac = (sc1502x_ramdac_t *) priv;
    uint8_t           temp   = svga_in(addr, svga);

    switch (addr) {
        case 0x3C6:
            if (ramdac->state == 4) {
                temp = ramdac->ctrl;
                break;
            }
            ramdac->state++;
            break;
        case 0x3C7:
            ramdac->state = 0;
            break;
        case 0x3C8:
            if (ramdac->ctrl & 0x10) {
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
            }
            ramdac->state = 0;
            break;
        case 0x3C9:
            if (ramdac->ctrl & 0x10)
                temp = ramdac->idx;

            ramdac->state = 0;
            break;

        default:
            break;
    }

    return temp;
}

uint8_t
sc1502x_rs2_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga)
{
    sc1502x_ramdac_t *ramdac = (sc1502x_ramdac_t *) priv;
    uint8_t rs = (addr & 0x03);
    uint8_t temp = svga_in(addr, svga);
    rs |= ((!!rs2) << 2);

    switch (rs) {
        case 0x00:
            if (ramdac->ctrl & 0x10) {
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
            }
            break;
        case 0x01:
            if (ramdac->ctrl & 0x10)
                temp = ramdac->idx;
            break;
        case 0x02:
            if (ramdac->ctrl & 0x10)
                temp = ramdac->ctrl;
            break;
        case 0x06:
            temp = ramdac->ctrl;
            break;

        default:
            break;
    }

    return temp;
}

static void *
sc1502x_ramdac_init(const device_t *info)
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

const device_t sc1502x_rs2_ramdac_device = {
    .name          = "Sierra SC1502x RAMDAC with RS2",
    .internal_name = "sc1502x_rs2_ramdac",
    .flags         = 0,
    .local         = 1,
    .init          = sc1502x_ramdac_init,
    .close         = sc1502x_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

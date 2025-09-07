/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          87C716 'SDAC' true colour RAMDAC emulation.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
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

enum {
    ICS_5300 = 0,
    ICS_5301,
    ICS_5340,
    ICS_5341,
    ICS_5342
};

#define ICS_S3_MASK 7
#define ICS_S3      8

#define S3_86C708   (ICS_5300 | ICS_S3)
#define S3_86C716   (ICS_5342 | ICS_S3)

typedef struct sdac_ramdac_t {
    uint16_t regs[256];
    int      magic_count;
    int      windex;
    int      rindex;
    int      reg_ff;
    int      rs2;
    uint8_t  type;
    uint8_t  command;
} sdac_ramdac_t;

static void
sdac_control_write(sdac_ramdac_t *ramdac, svga_t *svga, uint8_t val)
{
    ramdac->command = val;

    switch (ramdac->type & ICS_S3_MASK) {
        case ICS_5300:
        case ICS_5301:
            switch (val >> 5) {
                default:
                case 0x00:
                    svga->bpp = 8;
                    break;
                case 0x01:
                case 0x04:
                case 0x05:
                    svga->bpp = 15;
                    break;
                case 0x03:
                case 0x06:
                    svga->bpp = 16;
                    break;
                case 0x02:
                case 0x07:
                    svga->bpp = 24;
                    break;
            }
            break;
        case ICS_5340:
        case ICS_5341:
        case ICS_5342:
            switch (val >> 4) {
                default:
                case 0x00:
                case 0x01: /* This is actually 8bpp with two pixels read at a time. */
                    svga->bpp = 8;
                    break;
                case 0x02:
                case 0x03:
                case 0x08:
                case 0x0a:
                    svga->bpp = 15;
                    break;
                case 0x05:
                case 0x06:
                case 0x0c:
                    svga->bpp = 16;
                    break;
                case 0x04:
                case 0x09:
                case 0x0e:
                    svga->bpp = 24;
                    break;
                case 0x07:
                    svga->bpp = 32;
                    break;
            }
            break;

        default:
            break;
    }

    svga_recalctimings(svga);
}

static void
sdac_reg_write(sdac_ramdac_t *ramdac, int reg, uint8_t val)
{
    if ((reg >= 2 && reg <= 7) || (reg == 0xa) || (reg == 0xe)) {
        if (!ramdac->reg_ff)
            ramdac->regs[reg] = (ramdac->regs[reg] & 0xff00) | val;
        else
            ramdac->regs[reg] = (ramdac->regs[reg] & 0x00ff) | (val << 8);
    }
    ramdac->reg_ff = !ramdac->reg_ff;
    if (!ramdac->reg_ff)
        ramdac->windex++;
}

static uint8_t
sdac_reg_read(sdac_ramdac_t *ramdac, int reg)
{
    uint8_t temp;

    if (!ramdac->reg_ff)
        temp = ramdac->regs[reg] & 0xff;
    else
        temp = ramdac->regs[reg] >> 8;
    ramdac->reg_ff = !ramdac->reg_ff;
    if (!ramdac->reg_ff)
        ramdac->rindex++;

    return temp;
}

void
sdac_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga)
{
    sdac_ramdac_t *ramdac = (sdac_ramdac_t *) priv;
    uint8_t        rs     = (addr & 0x03);
    rs |= ((!!rs2) << 2);

    if (rs != 0x02)
        ramdac->magic_count = 0;

    switch (rs) {
        case 0x02:
            switch (ramdac->magic_count) {
                case 4:
                    sdac_control_write(ramdac, svga, val);
                    ramdac->magic_count = 0;
                    break;
                default:
                    svga_out(addr, val, svga);
                    break;
            }
            break;
        case 0x00:
        case 0x01:
        case 0x03:
            svga_out(addr, val, svga);
            break;
        case 0x04:
            ramdac->windex = val;
            ramdac->reg_ff = 0;
            break;
        case 0x05:
            sdac_reg_write(ramdac, ramdac->windex & 0xff, val);
            break;
        case 0x06:
            sdac_control_write(ramdac, svga, val);
            break;
        case 0x07:
            ramdac->rindex = val;
            ramdac->reg_ff = 0;
            break;

        default:
            break;
    }
}

uint8_t
sdac_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga)
{
    sdac_ramdac_t *ramdac = (sdac_ramdac_t *) priv;
    uint8_t        temp   = 0xff;
    uint8_t        rs     = (addr & 0x03);
    rs |= ((!!rs2) << 2);

    if (rs != 0x02)
        ramdac->magic_count = 0;

    switch (rs) {
        case 0x02:
            switch (ramdac->magic_count) {
                case 1:
                case 2:
                    temp = 0x00;
                    ramdac->magic_count++;
                    break;
                case 3:
                    temp = (ramdac->type & ICS_S3) ? 0x70 : 0x00;
                    ramdac->magic_count++;
                    break;
                case 4:
                    temp                = ramdac->command;
                    ramdac->magic_count = 0;
                    break;
                default:
                    temp = svga_in(addr, svga);
                    ramdac->magic_count++;
                    break;
            }
            break;
        case 0x00:
        case 0x01:
        case 0x03:
            temp = svga_in(addr, svga);
            break;
        case 0x04:
            temp = ramdac->windex;
            break;
        case 0x05:
            temp = sdac_reg_read(ramdac, ramdac->rindex & 0xff);
            break;
        case 0x06:
            temp = ramdac->command;
            break;
        case 0x07:
            temp = ramdac->rindex;
            break;

        default:
            break;
    }

    return temp;
}

float
sdac_getclock(int clock, void *priv)
{
    const sdac_ramdac_t *ramdac = (sdac_ramdac_t *) priv;
    float                t;
    int                  m;
    int                  n1;
    int                  n2;

    if (ramdac->regs[0xe] & (1 << 5))
        clock = ramdac->regs[0xe] & 7;

    clock &= 7;

    if (clock == 0)
        return 25175000.0;
    if (clock == 1)
        return 28322000.0;

    m  = (ramdac->regs[clock] & 0x7f) + 2;
    n1 = ((ramdac->regs[clock] >> 8) & 0x1f) + 2;
    n2 = ((ramdac->regs[clock] >> 13) & 0x07);
    n2 = (1 << n2);
    t  = (14318184.0f * (float) m) / (float) (n1 * n2);

    return t;
}

void *
sdac_ramdac_init(const device_t *info)
{
    sdac_ramdac_t *ramdac = (sdac_ramdac_t *) malloc(sizeof(sdac_ramdac_t));
    memset(ramdac, 0, sizeof(sdac_ramdac_t));

    ramdac->type = info->local;

    ramdac->regs[0] = 0x6128;
    ramdac->regs[1] = 0x623d;

    return ramdac;
}

static void
sdac_ramdac_close(void *priv)
{
    sdac_ramdac_t *ramdac = (sdac_ramdac_t *) priv;

    if (ramdac)
        free(ramdac);
}

const device_t gendac_ramdac_device = {
    .name          = "S3 GENDAC 86c708 RAMDAC",
    .internal_name = "gendac_ramdac",
    .flags         = 0,
    .local         = S3_86C708,
    .init          = sdac_ramdac_init,
    .close         = sdac_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t tseng_ics5301_ramdac_device = {
    .name          = "Tseng ICS5301 GENDAC RAMDAC",
    .internal_name = "tseng_ics5301_ramdac",
    .flags         = 0,
    .local         = ICS_5301,
    .init          = sdac_ramdac_init,
    .close         = sdac_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t tseng_ics5341_ramdac_device = {
    .name          = "Tseng ICS5341 GENDAC RAMDAC",
    .internal_name = "tseng_ics5341_ramdac",
    .flags         = 0,
    .local         = ICS_5341,
    .init          = sdac_ramdac_init,
    .close         = sdac_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sdac_ramdac_device = {
    .name          = "S3 SDAC 86c716 RAMDAC",
    .internal_name = "sdac_ramdac",
    .flags         = 0,
    .local         = S3_86C716,
    .init          = sdac_ramdac_init,
    .close         = sdac_ramdac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

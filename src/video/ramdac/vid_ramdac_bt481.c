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
 * Authors: TheCollector1995.
 *
 *          Copyright 2024 TheCollector1995.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

typedef struct bt481_ramdac_t {
    int      state;
    uint8_t  cmd_a;
    uint8_t  cmd_b;
    uint8_t  indirect_idx;
    uint8_t  overlay_mask;
    uint8_t  overlay_status;
    int      overlay_addr;
    int      overlay_pos;
    int      overlay_r;
    int      overlay_g;
    int      overlay_b;
    PALETTE  overlay;
} bt481_ramdac_t;

#ifdef ENABLE_BT481_LOG
int bt481_do_log = ENABLE_BT481_LOG;

static void
bt481_log(const char *fmt, ...)
{
    va_list ap;

    if (bt481_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define bt481_log(fmt, ...)
#endif

static void
bt481_ramdac_command(uint8_t val, void *priv, svga_t *svga)
{
    bt481_ramdac_t *ramdac  = (bt481_ramdac_t *) priv;
    ramdac->cmd_a           = val;
    bt481_log("RAMDAC CMD=%02x.\n", val);
    switch ((ramdac->cmd_a >> 4) & 0x0f) {
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

static uint32_t
bt481_lookup_lut_ram(svga_t* svga, uint32_t val)
{
    bt481_ramdac_t *ramdac  = (bt481_ramdac_t *) svga->ramdac;

    if (ramdac->cmd_a & 0x02)
        return val;
    else
        return ((val >> 16) & 0xff) |
               ((val        & 0xff) << 16) |
               (val & 0xff00ff00);
}

#define lookup_lut(val) bt481_lookup_lut_ram(svga, val)

static void
bt481_render_24bpp_lowres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  changed_addr;
    uint32_t  addr;
    uint32_t  dat0;
    uint32_t  dat1;
    uint32_t  dat2;
    uint32_t  fg;

    if (((svga->displine + svga->y_add) < 0) ||
        (svga->monitor->target_buffer == NULL) ||
        (svga->monitor->target_buffer->line[svga->displine + svga->y_add] == NULL))
        return;

    if (svga->force_old_addr) {
        if ((svga->displine + svga->y_add) < 0)
            return;

        if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                fg = svga->vram[svga->memaddr] | (svga->vram[svga->memaddr + 1] << 8) | (svga->vram[svga->memaddr + 2] << 16);
                svga->memaddr += 3;
                svga->memaddr &= svga->vram_display_mask;
                svga->monitor->target_buffer->line[svga->displine + svga->y_add][(x << 1) + svga->x_add] = svga->monitor->target_buffer->line[svga->displine + svga->y_add][(x << 1) + 1 + svga->x_add] = lookup_lut(fg);
            }
        }
    } else {
        changed_addr = svga->remap_func(svga, svga->memaddr);

        if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            if (!svga->remap_required) {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
                    dat0 = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
                    dat1 = *(uint32_t *) (&svga->vram[(svga->memaddr + 4) & svga->vram_display_mask]);
                    dat2 = *(uint32_t *) (&svga->vram[(svga->memaddr + 8) & svga->vram_display_mask]);

                    p[0] = p[1] = lookup_lut(dat0 & 0xffffff);
                    p[2] = p[3] = lookup_lut((dat0 >> 24) | ((dat1 & 0xffff) << 8));
                    p[4] = p[5] = lookup_lut((dat1 >> 16) | ((dat2 & 0xff) << 16));
                    p[6] = p[7] = lookup_lut(dat2 >> 8);
                    p += 8;

                    svga->memaddr += 12;
                }
            } else {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
                    addr = svga->remap_func(svga, svga->memaddr);
                    dat0 = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                    addr = svga->remap_func(svga, svga->memaddr + 4);
                    dat1 = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                    addr = svga->remap_func(svga, svga->memaddr + 8);
                    dat2 = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);

                    p[0] = p[1] = lookup_lut(dat0 & 0xffffff);
                    p[2] = p[3] = lookup_lut((dat0 >> 24) | ((dat1 & 0xffff) << 8));
                    p[4] = p[5] = lookup_lut((dat1 >> 16) | ((dat2 & 0xff) << 16));
                    p[6] = p[7] = lookup_lut(dat2 >> 8);
                    p += 8;

                    svga->memaddr += 12;
                }
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    }
}

static void
bt481_render_24bpp_highres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  changed_addr;
    uint8_t   addr;
    uint32_t  dat0;
    uint32_t  dat1;
    uint32_t  dat2;
    uint32_t  dat;

    if (((svga->displine + svga->y_add) < 0) ||
        (svga->monitor->target_buffer == NULL) ||
        (svga->monitor->target_buffer->line[svga->displine + svga->y_add] == NULL))
        return;

    if (svga->force_old_addr) {
        if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
                dat  = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
                p[x] = lookup_lut(dat & 0xffffff);

                dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + 3) & svga->vram_display_mask]);
                p[x + 1] = lookup_lut(dat & 0xffffff);

                dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + 6) & svga->vram_display_mask]);
                p[x + 2] = lookup_lut(dat & 0xffffff);

                dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + 9) & svga->vram_display_mask]);
                p[x + 3] = lookup_lut(dat & 0xffffff);

                svga->memaddr += 12;
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    } else {
        changed_addr = svga->remap_func(svga, svga->memaddr);

        if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            if (!svga->remap_required) {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
                    dat0 = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
                    dat1 = *(uint32_t *) (&svga->vram[(svga->memaddr + 4) & svga->vram_display_mask]);
                    dat2 = *(uint32_t *) (&svga->vram[(svga->memaddr + 8) & svga->vram_display_mask]);

                    *p++ = lookup_lut(dat0 & 0xffffff);
                    *p++ = lookup_lut((dat0 >> 24) | ((dat1 & 0xffff) << 8));
                    *p++ = lookup_lut((dat1 >> 16) | ((dat2 & 0xff) << 16));
                    *p++ = lookup_lut(dat2 >> 8);

                    svga->memaddr += 12;
                }
            } else {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
                    addr = svga->remap_func(svga, svga->memaddr);
                    dat0 = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                    addr = svga->remap_func(svga, svga->memaddr + 4);
                    dat1 = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                    addr = svga->remap_func(svga, svga->memaddr + 8);
                    dat2 = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);

                    *p++ = lookup_lut(dat0 & 0xffffff);
                    *p++ = lookup_lut((dat0 >> 24) | ((dat1 & 0xffff) << 8));
                    *p++ = lookup_lut((dat1 >> 16) | ((dat2 & 0xff) << 16));
                    *p++ = lookup_lut(dat2 >> 8);

                    svga->memaddr += 12;
                }
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    }
}

void
bt481_recalctimings(void *priv, svga_t *svga)
{
    if (svga->render == svga_render_24bpp_lowres)
        svga->render = bt481_render_24bpp_lowres;
    else if (svga->render == svga_render_24bpp_highres)
        svga->render = bt481_render_24bpp_highres;
}

void
bt481_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga)
{
    bt481_ramdac_t *ramdac = (bt481_ramdac_t *) priv;
    rs2                   |= (ramdac->cmd_a & 0x04);
    uint8_t         rs     = (addr & 0x03) | ((!!rs2) << 2);
    int             index;

    switch (rs) {
        case 0x00:
            if (ramdac->cmd_a & 0x01) {
                ramdac->indirect_idx = val;
                ramdac->state = 0;
                break;
            }
            fallthrough;
        case 0x01:
        case 0x03:
            svga_out(addr, val, svga);
            ramdac->state = 0;
            break;
        case 0x04:
        case 0x07:
            ramdac->overlay_pos    = 0;
            ramdac->overlay_status = addr & 0x03;
            ramdac->overlay_addr   = (val + (addr & 0x01)) & 0x0f;
            break;
        case 0x05:
            switch (ramdac->overlay_pos) {
                case 0:
                    ramdac->overlay_r = val;
                    ramdac->overlay_pos++;
                    break;
                case 1:
                    ramdac->overlay_g = val;
                    ramdac->overlay_pos++;
                    break;
                case 2:
                    index                    = ramdac->overlay_addr & 0xff;
                    ramdac->overlay_b        = val;
                    ramdac->overlay[index].r = ramdac->overlay_r;
                    ramdac->overlay[index].g = ramdac->overlay_g;
                    ramdac->overlay[index].b = ramdac->overlay_b;
                    ramdac->overlay_pos      = 0;
                    ramdac->overlay_addr     = (ramdac->overlay_addr + 1) & 0xff;
                    break;

                default:
                    break;
            }
            ramdac->state = 0;
            break;
        case 0x02:
            bt481_log("RAMDAC Write State=%x.\n", ramdac->state);
            switch (ramdac->state) {
                case 4:
                    bt481_ramdac_command(val, ramdac, svga);
                    ramdac->state = 0;
                    break;
                default:
                    if (ramdac->cmd_a & 0x01)  switch (ramdac->indirect_idx) {
                        default:
                            break;
                        case 0:
                            svga_out(addr, val, svga);
                            break;
                        case 1:
                            ramdac->overlay_mask = val;
                            break;
                        case 2:
                            ramdac->cmd_b = val;
                            break;
                    } else
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
    bt481_ramdac_t *ramdac = (bt481_ramdac_t *) priv;
    uint8_t         temp   = 0xff;
    rs2                   |= (ramdac->cmd_a & 0x04);
    uint8_t         rs     = (addr & 0x03) | ((!!rs2) << 2);
    int             index;

    switch (rs) {
        case 0x02:
            switch (ramdac->state) {
                case 4:
                    temp = ramdac->cmd_a;
                    break;
                default:
                    if (ramdac->cmd_a & 0x01)  switch (ramdac->indirect_idx) {
                        default:
                            temp = 0xff;
                            break;
                        case 0:
                            temp = svga_in(addr, svga);
                            ramdac->state++;
                            break;
                        case 1:
                            temp = ramdac->overlay_mask;
                            break;
                        case 2:
                            temp = ramdac->cmd_b;
                            break;
                    } else {
                        temp = svga_in(addr, svga);
                        ramdac->state++;
                    }
                    break;
            }
            break;

        case 0x04:
            temp = ramdac->overlay_addr;
            ramdac->state = 0;
            break;

        case 0x05:
            index = (ramdac->overlay_addr - 1) & 0xff;
            switch (ramdac->overlay_pos) {
                case 0:
                    ramdac->overlay_pos++;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        temp = ramdac->overlay[index].r;
                    else
                        temp = ramdac->overlay[index].r & 0x3f;
                    break;
                case 1:
                    ramdac->overlay_pos++;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        temp = ramdac->overlay[index].g;
                    else
                        temp = ramdac->overlay[index].g & 0x3f;
                    break;
                case 2:
                    ramdac->overlay_pos  = 0;
                    ramdac->overlay_addr = (ramdac->overlay_addr + 1) & 0x0f;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        temp = ramdac->overlay[index].b;
                    else
                        temp = ramdac->overlay[index].b & 0x3f;
                    break;

                default:
                    break;
            }
            ramdac->state = 0;
            break;

        case 0x06:
            temp = ramdac->cmd_a;
            ramdac->state = 0;
            break;

        case 0x07:
            temp = ramdac->overlay_status;
            ramdac->state = 0;
            break;

        case 0x00:
            if (ramdac->cmd_a & 0x01) {
                temp = ramdac->indirect_idx;
                ramdac->state = 0;
                break;
            }
            fallthrough;
        default:
            temp          = svga_in(addr, svga);
            ramdac->state = 0;
            break;
    }

    bt481_log("RAMDAC IN=%02x, ret=%02x.\n", rs, temp);
    return temp;
}

static void *
bt481_ramdac_init(UNUSED(const device_t *info))
{
    bt481_ramdac_t *ramdac = (bt481_ramdac_t *) calloc(1, sizeof(bt481_ramdac_t));

    ramdac->cmd_a = 0x00;
    ramdac->cmd_b = 0x1e;

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

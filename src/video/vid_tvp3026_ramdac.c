/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Texas Instruments TVP3026 true colour RAMDAC
 *		family.
 *
 *
 *		TODO: Clock and other parts.
 *
 * Authors:	TheCollector1995,
 *
 *		Copyright 2021 TheCollector1995.
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
    PALETTE  extpal;
    uint32_t extpallook[256];
    uint8_t  cursor64_data[1024];
    int      hwc_y, hwc_x;
    uint8_t  ind_idx;
    uint8_t  dcc, dc_init;
    uint8_t  ccr;
    uint8_t  true_color;
    uint8_t  latch_cntl;
    uint8_t  mcr;
    uint8_t  ppr;
    uint8_t  general_cntl;
    uint8_t  mclk;
    uint8_t  misc;
    uint8_t  type;
    uint8_t  mode;
    uint8_t  pll_addr;
    uint8_t  clock_sel;
    struct
    {
        uint8_t m, n, p;
    } pix, mem, loop;
} tvp3026_ramdac_t;

static void
tvp3026_set_bpp(tvp3026_ramdac_t *ramdac, svga_t *svga)
{
    if ((ramdac->true_color & 0x80) == 0x80) {
        if (ramdac->mcr & 0x08)
            svga->bpp = 8;
        else
            svga->bpp = 4;
    } else {
        switch (ramdac->true_color & 0x0f) {
            case 0x01:
            case 0x03:
            case 0x05:
                svga->bpp = 16;
                break;
            case 0x04:
                svga->bpp = 15;
                break;
            case 0x06:
            case 0x07:
                if (ramdac->true_color & 0x10)
                    svga->bpp = 24;
                else
                    svga->bpp = 32;
                break;
            case 0x0e:
            case 0x0f:
                svga->bpp = 24;
                break;
        }
    }
    svga_recalctimings(svga);
}

void
tvp3026_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, void *p, svga_t *svga)
{
    tvp3026_ramdac_t *ramdac = (tvp3026_ramdac_t *) p;
    uint32_t          o32;
    uint8_t          *cd;
    uint16_t          index;
    uint8_t           rs      = (addr & 0x03);
    uint16_t          da_mask = 0x03ff;
    rs |= (!!rs2 << 2);
    rs |= (!!rs3 << 3);

    switch (rs) {
        case 0x00: /* Palette Write Index Register (RS value = 0000) */
            ramdac->ind_idx = val;
        case 0x04: /* Ext Palette Write Index Register (RS value = 0100) */
        case 0x03:
        case 0x07: /* Ext Palette Read Index Register (RS value = 0111) */
            svga->dac_pos    = 0;
            svga->dac_status = addr & 0x03;
            svga->dac_addr   = val;
            if (svga->dac_status)
                svga->dac_addr = (svga->dac_addr + 1) & da_mask;
            break;
        case 0x01: /* Palette Data Register (RS value = 0001) */
        case 0x02: /* Pixel Read Mask Register (RS value = 0010) */
            svga_out(addr, val, svga);
            break;
        case 0x05: /* Ext Palette Data Register (RS value = 0101) */
            svga->dac_status = 0;
            svga->fullchange = changeframecount;
            switch (svga->dac_pos) {
                case 0:
                    svga->dac_r = val;
                    svga->dac_pos++;
                    break;
                case 1:
                    svga->dac_g = val;
                    svga->dac_pos++;
                    break;
                case 2:
                    index                   = svga->dac_addr & 3;
                    ramdac->extpal[index].r = svga->dac_r;
                    ramdac->extpal[index].g = svga->dac_g;
                    ramdac->extpal[index].b = val;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        ramdac->extpallook[index] = makecol32(ramdac->extpal[index].r, ramdac->extpal[index].g, ramdac->extpal[index].b);
                    else
                        ramdac->extpallook[index] = makecol32(video_6to8[ramdac->extpal[index].r & 0x3f], video_6to8[ramdac->extpal[index].g & 0x3f], video_6to8[ramdac->extpal[index].b & 0x3f]);

                    if (svga->ext_overscan && !index) {
                        o32                  = svga->overscan_color;
                        svga->overscan_color = ramdac->extpallook[0];
                        if (o32 != svga->overscan_color)
                            svga_recalctimings(svga);
                    }
                    svga->dac_addr = (svga->dac_addr + 1) & 0xff;
                    svga->dac_pos  = 0;
                    break;
            }
            break;
        case 0x09: /* Direct Cursor Control (RS value = 1001) */
            ramdac->dcc = val;
            if (ramdac->ccr & 0x80) {
                svga->dac_hwcursor.cur_xsize = svga->dac_hwcursor.cur_ysize = 64;
                svga->dac_hwcursor.x                                        = ramdac->hwc_x - svga->dac_hwcursor.cur_xsize;
                svga->dac_hwcursor.y                                        = ramdac->hwc_y - svga->dac_hwcursor.cur_ysize;
                svga->dac_hwcursor.ena                                      = !!(val & 0x03);
                ramdac->mode                                                = val & 0x03;
            }
            break;
        case 0x0a: /* Indexed Data (RS value = 1010) */
            switch (ramdac->ind_idx) {
                case 0x06: /* Indirect Cursor Control */
                    ramdac->ccr                  = val;
                    svga->dac_hwcursor.cur_xsize = svga->dac_hwcursor.cur_ysize = 64;
                    svga->dac_hwcursor.x                                        = ramdac->hwc_x - svga->dac_hwcursor.cur_xsize;
                    svga->dac_hwcursor.y                                        = ramdac->hwc_y - svga->dac_hwcursor.cur_ysize;
                    svga->dac_hwcursor.ena                                      = !!(val & 0x03);
                    ramdac->mode                                                = val & 0x03;
                    break;
                case 0x0f: /* Latch Control */
                    ramdac->latch_cntl = val;
                    break;
                case 0x18: /* True Color Control */
                    ramdac->true_color = val;
                    tvp3026_set_bpp(ramdac, svga);
                    break;
                case 0x19: /* Multiplex Control */
                    ramdac->mcr = val;
                    tvp3026_set_bpp(ramdac, svga);
                    break;
                case 0x1a: /* Clock Selection */
                    ramdac->clock_sel = val;
                    break;
                case 0x1c: /* Palette-Page Register */
                    ramdac->ppr = val;
                    break;
                case 0x1d: /* General Control Register */
                    ramdac->general_cntl = val;
                    break;
                case 0x1e: /* Miscellaneous Control */
                    ramdac->misc      = val;
                    svga->ramdac_type = (val & 0x08) ? RAMDAC_8BIT : RAMDAC_6BIT;
                    break;
                case 0x2c: /* PLL Address */
                    ramdac->pll_addr = val;
                    break;
                case 0x2d: /* Pixel clock PLL data */
                    switch (ramdac->pll_addr & 3) {
                        case 0:
                            ramdac->pix.n = val;
                            break;
                        case 1:
                            ramdac->pix.m = val;
                            break;
                        case 2:
                            ramdac->pix.p = val;
                            break;
                    }
                    ramdac->pll_addr = ((ramdac->pll_addr + 1) & 3) | (ramdac->pll_addr & 0xfc);
                    break;
                case 0x2e: /* Memory Clock PLL Data */
                    switch ((ramdac->pll_addr >> 2) & 3) {
                        case 0:
                            ramdac->mem.n = val;
                            break;
                        case 1:
                            ramdac->mem.m = val;
                            break;
                        case 2:
                            ramdac->mem.p = val;
                            break;
                    }
                    ramdac->pll_addr = ((ramdac->pll_addr + 4) & 0x0c) | (ramdac->pll_addr & 0xf3);
                    break;
                case 0x2f: /* Loop Clock PLL Data */
                    switch ((ramdac->pll_addr >> 4) & 3) {
                        case 0:
                            ramdac->loop.n = val;
                            break;
                        case 1:
                            ramdac->loop.m = val;
                            break;
                        case 2:
                            ramdac->loop.p = val;
                            break;
                    }
                    ramdac->pll_addr = ((ramdac->pll_addr + 0x10) & 0x30) | (ramdac->pll_addr & 0xcf);
                    break;
                case 0x39: /* MCLK/Loop Clock Control */
                    ramdac->mclk = val;
                    break;
            }
            break;
        case 0x0b: /* Cursor RAM Data Register (RS value = 1011) */
            index          = svga->dac_addr & da_mask;
            cd             = (uint8_t *) ramdac->cursor64_data;
            cd[index]      = val;
            svga->dac_addr = (svga->dac_addr + 1) & da_mask;
            break;
        case 0x0c: /* Cursor X Low Register (RS value = 1100) */
            ramdac->hwc_x        = (ramdac->hwc_x & 0x0f00) | val;
            svga->dac_hwcursor.x = ramdac->hwc_x - svga->dac_hwcursor.cur_xsize;
            break;
        case 0x0d: /* Cursor X High Register (RS value = 1101) */
            ramdac->hwc_x        = (ramdac->hwc_x & 0x00ff) | ((val & 0x0f) << 8);
            svga->dac_hwcursor.x = ramdac->hwc_x - svga->dac_hwcursor.cur_xsize;
            break;
        case 0x0e: /* Cursor Y Low Register (RS value = 1110) */
            ramdac->hwc_y        = (ramdac->hwc_y & 0x0f00) | val;
            svga->dac_hwcursor.y = ramdac->hwc_y - svga->dac_hwcursor.cur_ysize;
            break;
        case 0x0f: /* Cursor Y High Register (RS value = 1111) */
            ramdac->hwc_y        = (ramdac->hwc_y & 0x00ff) | ((val & 0x0f) << 8);
            svga->dac_hwcursor.y = ramdac->hwc_y - svga->dac_hwcursor.cur_ysize;
            break;
    }

    return;
}

uint8_t
tvp3026_ramdac_in(uint16_t addr, int rs2, int rs3, void *p, svga_t *svga)
{
    tvp3026_ramdac_t *ramdac = (tvp3026_ramdac_t *) p;
    uint8_t           temp   = 0xff;
    uint8_t          *cd;
    uint16_t          index;
    uint8_t           rs      = (addr & 0x03);
    uint16_t          da_mask = 0x03ff;
    rs |= (!!rs2 << 2);
    rs |= (!!rs3 << 3);

    switch (rs) {
        case 0x00: /* Palette Write Index Register (RS value = 0000) */
        case 0x01: /* Palette Data Register (RS value = 0001) */
        case 0x02: /* Pixel Read Mask Register (RS value = 0010) */
        case 0x04: /* Ext Palette Write Index Register (RS value = 0100) */
            temp = svga_in(addr, svga);
            break;
        case 0x03: /* Palette Read Index Register (RS value = 0011) */
        case 0x07: /* Ext Palette Read Index Register (RS value = 0111) */
            temp = svga->dac_addr & 0xff;
            break;
        case 0x05: /* Ext Palette Data Register (RS value = 0101) */
            index            = (svga->dac_addr - 1) & 3;
            svga->dac_status = 3;
            switch (svga->dac_pos) {
                case 0:
                    svga->dac_pos++;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        temp = ramdac->extpal[index].r;
                    else
                        temp = ramdac->extpal[index].r & 0x3f;
                    break;
                case 1:
                    svga->dac_pos++;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        temp = ramdac->extpal[index].g;
                    else
                        temp = ramdac->extpal[index].g & 0x3f;
                    break;
                case 2:
                    svga->dac_pos  = 0;
                    svga->dac_addr = svga->dac_addr + 1;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        temp = ramdac->extpal[index].b;
                    else
                        temp = ramdac->extpal[index].b & 0x3f;
                    break;
            }
            break;
        case 0x09: /* Direct Cursor Control (RS value = 1001) */
            temp = ramdac->dcc;
            break;
        case 0x0a: /* Indexed Data (RS value = 1010) */
            switch (ramdac->ind_idx) {
                case 0x01: /* Silicon Revision */
                    temp = 0x00;
                    break;
                case 0x06: /* Indirect Cursor Control */
                    temp = ramdac->ccr;
                    break;
                case 0x0f: /* Latch Control */
                    temp = ramdac->latch_cntl;
                    break;
                case 0x18: /* True Color Control */
                    temp = ramdac->true_color;
                    break;
                case 0x19: /* Multiplex Control */
                    temp = ramdac->mcr;
                    break;
                case 0x1a: /* Clock Selection */
                    temp = ramdac->clock_sel;
                    break;
                case 0x1c: /* Palette-Page Register */
                    temp = ramdac->ppr;
                    break;
                case 0x1d: /* General Control Register */
                    temp = ramdac->general_cntl;
                    break;
                case 0x1e: /* Miscellaneous Control */
                    temp = ramdac->misc;
                    break;
                case 0x2c: /* PLL Address */
                    temp = ramdac->pll_addr;
                    break;
                case 0x2d: /* Pixel clock PLL data */
                    switch (ramdac->pll_addr & 3) {
                        case 0:
                            temp = ramdac->pix.n;
                            break;
                        case 1:
                            temp = ramdac->pix.m;
                            break;
                        case 2:
                            temp = ramdac->pix.p;
                            break;
                        case 3:
                            temp = 0x40; /*PLL locked to frequency*/
                            break;
                    }
                    break;
                case 0x2e: /* Memory Clock PLL Data */
                    switch ((ramdac->pll_addr >> 2) & 3) {
                        case 0:
                            temp = ramdac->mem.n;
                            break;
                        case 1:
                            temp = ramdac->mem.m;
                            break;
                        case 2:
                            temp = ramdac->mem.p;
                            break;
                        case 3:
                            temp = 0x40; /*PLL locked to frequency*/
                            break;
                    }
                    break;
                case 0x2f: /* Loop Clock PLL Data */
                    switch ((ramdac->pll_addr >> 4) & 3) {
                        case 0:
                            temp = ramdac->loop.n;
                            break;
                        case 1:
                            temp = ramdac->loop.m;
                            break;
                        case 2:
                            temp = ramdac->loop.p;
                            break;
                    }
                    break;
                case 0x39: /* MCLK/Loop Clock Control */
                    temp = ramdac->mclk;
                    break;
                case 0x3f: /* ID */
                    temp = 0x26;
                    break;
            }
            break;
        case 0x0b: /* Cursor RAM Data Register (RS value = 1011) */
            index = (svga->dac_addr - 1) & da_mask;
            cd    = (uint8_t *) ramdac->cursor64_data;
            temp  = cd[index];

            svga->dac_addr = (svga->dac_addr + 1) & da_mask;
            break;
        case 0x0c: /* Cursor X Low Register (RS value = 1100) */
            temp = ramdac->hwc_x & 0xff;
            break;
        case 0x0d: /* Cursor X High Register (RS value = 1101) */
            temp = (ramdac->hwc_x >> 8) & 0xff;
            break;
        case 0x0e: /* Cursor Y Low Register (RS value = 1110) */
            temp = ramdac->hwc_y & 0xff;
            break;
        case 0x0f: /* Cursor Y High Register (RS value = 1111) */
            temp = (ramdac->hwc_y >> 8) & 0xff;
            break;
    }

    return temp;
}

void
tvp3026_recalctimings(void *p, svga_t *svga)
{
    tvp3026_ramdac_t *ramdac = (tvp3026_ramdac_t *) p;

    svga->interlace = (ramdac->ccr & 0x40);
}

void
tvp3026_hwcursor_draw(svga_t *svga, int displine)
{
    int               x, xx, comb, b0, b1;
    uint16_t          dat[2];
    int               offset = svga->dac_hwcursor_latch.x + svga->dac_hwcursor_latch.xoff;
    int               pitch, bppl, mode, x_pos, y_pos;
    uint32_t          clr1, clr2, clr3, *p;
    uint8_t          *cd;
    tvp3026_ramdac_t *ramdac = (tvp3026_ramdac_t *) svga->ramdac;

    clr1 = ramdac->extpallook[1];
    clr2 = ramdac->extpallook[2];
    clr3 = ramdac->extpallook[3];

    /* The planes come in two parts, and each plane is 1bpp,
       so a 32x32 cursor has 4 bytes per line, and a 64x64
       cursor has 8 bytes per line. */
    pitch = (svga->dac_hwcursor_latch.cur_xsize >> 3); /* Bytes per line. */
    /* A 32x32 cursor has 128 bytes per line, and a 64x64
       cursor has 512 bytes per line. */
    bppl = (pitch * svga->dac_hwcursor_latch.cur_ysize); /* Bytes per plane. */
    mode = ramdac->mode;

    if (svga->interlace && svga->dac_hwcursor_oddeven)
        svga->dac_hwcursor_latch.addr += pitch;

    cd = (uint8_t *) ramdac->cursor64_data;

    for (x = 0; x < svga->dac_hwcursor_latch.cur_xsize; x += 16) {
        dat[0] = (cd[svga->dac_hwcursor_latch.addr] << 8) | cd[svga->dac_hwcursor_latch.addr + 1];
        dat[1] = (cd[svga->dac_hwcursor_latch.addr + bppl] << 8) | cd[svga->dac_hwcursor_latch.addr + bppl + 1];

        for (xx = 0; xx < 16; xx++) {
            b0   = (dat[0] >> (15 - xx)) & 1;
            b1   = (dat[1] >> (15 - xx)) & 1;
            comb = (b0 | (b1 << 1));

            y_pos = displine;
            x_pos = offset + svga->x_add;
            p     = buffer32->line[y_pos];

            if (offset >= svga->dac_hwcursor_latch.x) {
                switch (mode) {
                    case 1: /* Three Color */
                        switch (comb) {
                            case 1:
                                p[x_pos] = clr1;
                                break;
                            case 2:
                                p[x_pos] = clr2;
                                break;
                            case 3:
                                p[x_pos] = clr3;
                                break;
                        }
                        break;
                    case 2: /* XGA */
                        switch (comb) {
                            case 0:
                                p[x_pos] = clr1;
                                break;
                            case 1:
                                p[x_pos] = clr2;
                                break;
                            case 3:
                                p[x_pos] ^= 0xffffff;
                                break;
                        }
                        break;
                    case 3: /* X-Windows */
                        switch (comb) {
                            case 2:
                                p[x_pos] = clr1;
                                break;
                            case 3:
                                p[x_pos] = clr2;
                                break;
                        }
                        break;
                }
            }
            offset++;
        }
        svga->dac_hwcursor_latch.addr += 2;
    }

    if (svga->interlace && !svga->dac_hwcursor_oddeven)
        svga->dac_hwcursor_latch.addr += pitch;
}

float
tvp3026_getclock(int clock, void *p)
{
    tvp3026_ramdac_t *ramdac = (tvp3026_ramdac_t *) p;
    int               n, m, pl;
    float             f_vco, f_pll;

    if (clock == 0)
        return 25175000.0;
    if (clock == 1)
        return 28322000.0;

    /*Fvco = 8 x Fref x (65 - M) / (65 - N)*/
    /*Fpll = Fvco / 2^P*/
    n     = ramdac->pix.n & 0x3f;
    m     = ramdac->pix.m & 0x3f;
    pl    = ramdac->pix.p & 0x03;
    f_vco = 8.0 * 14318184 * (float) (65 - m) / (float) (65 - n);
    f_pll = f_vco / (float) (1 << pl);

    return f_pll;
}

void *
tvp3026_ramdac_init(const device_t *info)
{
    tvp3026_ramdac_t *ramdac = (tvp3026_ramdac_t *) malloc(sizeof(tvp3026_ramdac_t));
    memset(ramdac, 0, sizeof(tvp3026_ramdac_t));

    ramdac->type = info->local;

    ramdac->latch_cntl = 0x06;
    ramdac->true_color = 0x80;
    ramdac->mcr        = 0x98;
    ramdac->clock_sel  = 0x07;
    ramdac->mclk       = 0x18;

    return ramdac;
}

static void
tvp3026_ramdac_close(void *priv)
{
    tvp3026_ramdac_t *ramdac = (tvp3026_ramdac_t *) priv;

    if (ramdac)
        free(ramdac);
}

const device_t tvp3026_ramdac_device = {
    .name          = "TI TVP3026 RAMDAC",
    .internal_name = "tvp3026_ramdac",
    .flags         = 0,
    .local         = 0,
    .init          = tvp3026_ramdac_init,
    .close         = tvp3026_ramdac_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

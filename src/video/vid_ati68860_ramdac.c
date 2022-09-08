/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ATI 68860 RAMDAC emulation (for Mach64)
 *
 *		ATI 68860/68880 Truecolor DACs:
 *		  REG08 (R/W):
 *		  bit 0-?  Always 2 ??
 *
 *		  REG0A (R/W):
 *		  bit 0-?  Always 1Dh ??
 *
 *		  REG0B (R/W):  (GMR ?)
 *		  bit 0-7  Mode. 82h: 4bpp, 83h: 8bpp,
 *				 A0h: 15bpp, A1h: 16bpp, C0h: 24bpp,
 *				 E3h: 32bpp  (80h for VGA modes ?)
 *
 *		  REG0C (R/W):  Device Setup Register A
 *		  bit   0  Controls 6/8bit DAC. 0: 8bit DAC/LUT, 1: 6bit DAC/LUT
 *		  2-3  Depends on Video memory (= VRAM width ?) .
 *			1: Less than 1Mb, 2: 1Mb, 3: > 1Mb
 *		  5-6  Always set ?
 *		    7  If set can remove "snow" in some cases
 *			(A860_Delay_L ?) ??
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
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

typedef struct ati68860_ramdac_t {
    uint8_t regs[16];
    void (*render)(struct svga_t *svga);

    int      dac_addr, dac_pos;
    int      dac_r, dac_g;
    PALETTE  pal;
    uint32_t pallook[2];

    int ramdac_type;
} ati68860_ramdac_t;

void
ati68860_ramdac_out(uint16_t addr, uint8_t val, void *p, svga_t *svga)
{
    ati68860_ramdac_t *ramdac = (ati68860_ramdac_t *) p;

    switch (addr) {
        case 0:
            svga_out(0x3c8, val, svga);
            break;
        case 1:
            svga_out(0x3c9, val, svga);
            break;
        case 2:
            svga_out(0x3c6, val, svga);
            break;
        case 3:
            svga_out(0x3c7, val, svga);
            break;
        default:
            ramdac->regs[addr & 0xf] = val;
            switch (addr & 0xf) {
                case 0x4:
                    ramdac->dac_addr = val;
                    ramdac->dac_pos  = 0;
                    break;
                case 0x5:
                    switch (ramdac->dac_pos) {
                        case 0:
                            ramdac->dac_r = val;
                            ramdac->dac_pos++;
                            break;
                        case 1:
                            ramdac->dac_g = val;
                            ramdac->dac_pos++;
                            break;
                        case 2:
                            if (ramdac->dac_addr > 1)
                                break;
                            ramdac->pal[ramdac->dac_addr].r = ramdac->dac_r;
                            ramdac->pal[ramdac->dac_addr].g = ramdac->dac_g;
                            ramdac->pal[ramdac->dac_addr].b = val;
                            if (ramdac->ramdac_type == RAMDAC_8BIT)
                                ramdac->pallook[ramdac->dac_addr] = makecol32(ramdac->pal[ramdac->dac_addr].r,
                                                                              ramdac->pal[ramdac->dac_addr].g,
                                                                              ramdac->pal[ramdac->dac_addr].b);
                            else
                                ramdac->pallook[ramdac->dac_addr] = makecol32(video_6to8[ramdac->pal[ramdac->dac_addr].r & 0x3f],
                                                                              video_6to8[ramdac->pal[ramdac->dac_addr].g & 0x3f],
                                                                              video_6to8[ramdac->pal[ramdac->dac_addr].b & 0x3f]);
                            ramdac->dac_pos  = 0;
                            ramdac->dac_addr = (ramdac->dac_addr + 1) & 255;
                            break;
                    }
                    break;
                case 0xb:
                    switch (val) {
                        case 0x82:
                            ramdac->render = svga_render_4bpp_highres;
                            break;
                        case 0x83:
                            ramdac->render = svga_render_8bpp_highres;
                            break;
                        case 0xa0:
                        case 0xb0:
                            ramdac->render = svga_render_15bpp_highres;
                            break;
                        case 0xa1:
                        case 0xb1:
                            ramdac->render = svga_render_16bpp_highres;
                            break;
                        case 0xc0:
                        case 0xd0:
                            ramdac->render = svga_render_24bpp_highres;
                            break;
                        case 0xe2:
                        case 0xf7:
                            ramdac->render = svga_render_32bpp_highres;
                            break;
                        case 0xe3:
                            ramdac->render = svga_render_ABGR8888_highres;
                            break;
                        case 0xf2:
                            ramdac->render = svga_render_RGBA8888_highres;
                            break;
                        default:
                            ramdac->render = svga_render_8bpp_highres;
                            break;
                    }
                    break;
                case 0xc:
                    svga_set_ramdac_type(svga, (val & 1) ? RAMDAC_6BIT : RAMDAC_8BIT);
                    break;
            }
            break;
    }
}

uint8_t
ati68860_ramdac_in(uint16_t addr, void *p, svga_t *svga)
{
    ati68860_ramdac_t *ramdac = (ati68860_ramdac_t *) p;
    uint8_t            temp   = 0;

    switch (addr) {
        case 0:
            temp = svga_in(0x3c8, svga);
            break;
        case 1:
            temp = svga_in(0x3c9, svga);
            break;
        case 2:
            temp = svga_in(0x3c6, svga);
            break;
        case 3:
            temp = svga_in(0x3c7, svga);
            break;
        case 4:
        case 8:
            temp = 2;
            break;
        case 6:
        case 0xa:
            temp = 0x1d;
            break;
        case 0xf:
            temp = 0xd0;
            break;

        default:
            temp = ramdac->regs[addr & 0xf];
            break;
    }

    return temp;
}

void
ati68860_set_ramdac_type(void *p, int type)
{
    ati68860_ramdac_t *ramdac = (ati68860_ramdac_t *) p;
    int                c;

    if (ramdac->ramdac_type != type) {
        ramdac->ramdac_type = type;

        for (c = 0; c < 2; c++) {
            if (ramdac->ramdac_type == RAMDAC_8BIT)
                ramdac->pallook[c] = makecol32(ramdac->pal[c].r, ramdac->pal[c].g,
                                               ramdac->pal[c].b);
            else
                ramdac->pallook[c] = makecol32(video_6to8[ramdac->pal[c].r & 0x3f], video_6to8[ramdac->pal[c].g & 0x3f],
                                               video_6to8[ramdac->pal[c].b & 0x3f]);
        }
    }
}

static void *
ati68860_ramdac_init(const device_t *info)
{
    ati68860_ramdac_t *ramdac = (ati68860_ramdac_t *) malloc(sizeof(ati68860_ramdac_t));
    memset(ramdac, 0, sizeof(ati68860_ramdac_t));

    ramdac->render = svga_render_8bpp_highres;

    return ramdac;
}

void
ati68860_ramdac_set_render(void *p, svga_t *svga)
{
    ati68860_ramdac_t *ramdac = (ati68860_ramdac_t *) p;

    svga->render = ramdac->render;
}

void
ati68860_ramdac_set_pallook(void *p, int i, uint32_t col)
{
    ati68860_ramdac_t *ramdac = (ati68860_ramdac_t *) p;

    ramdac->pallook[i] = col;
}

void
ati68860_hwcursor_draw(svga_t *svga, int displine)
{
    ati68860_ramdac_t *ramdac = (ati68860_ramdac_t *) svga->ramdac;
    int                x, offset;
    uint8_t            dat;
    uint32_t           col0 = ramdac->pallook[0];
    uint32_t           col1 = ramdac->pallook[1];

    offset = svga->dac_hwcursor_latch.xoff;
    for (x = 0; x < 64 - svga->dac_hwcursor_latch.xoff; x += 4) {
        dat = svga->vram[svga->dac_hwcursor_latch.addr + (offset >> 2)];
        if (!(dat & 2))
            buffer32->line[displine][svga->dac_hwcursor_latch.x + x + svga->x_add] = (dat & 1) ? col1 : col0;
        else if ((dat & 3) == 3)
            buffer32->line[displine][svga->dac_hwcursor_latch.x + x + svga->x_add] ^= 0xFFFFFF;
        dat >>= 2;
        if (!(dat & 2))
            buffer32->line[displine][svga->dac_hwcursor_latch.x + x + svga->x_add + 1] = (dat & 1) ? col1 : col0;
        else if ((dat & 3) == 3)
            buffer32->line[displine][svga->dac_hwcursor_latch.x + x + svga->x_add + 1] ^= 0xFFFFFF;
        dat >>= 2;
        if (!(dat & 2))
            buffer32->line[displine][svga->dac_hwcursor_latch.x + x + svga->x_add + 2] = (dat & 1) ? col1 : col0;
        else if ((dat & 3) == 3)
            buffer32->line[displine][svga->dac_hwcursor_latch.x + x + svga->x_add + 2] ^= 0xFFFFFF;
        dat >>= 2;
        if (!(dat & 2))
            buffer32->line[displine][svga->dac_hwcursor_latch.x + x + svga->x_add + 3] = (dat & 1) ? col1 : col0;
        else if ((dat & 3) == 3)
            buffer32->line[displine][svga->dac_hwcursor_latch.x + x + svga->x_add + 3] ^= 0xFFFFFF;
        dat >>= 2;
        offset += 4;
    }

    svga->dac_hwcursor_latch.addr += 16;
}

static void
ati68860_ramdac_close(void *priv)
{
    ati68860_ramdac_t *ramdac = (ati68860_ramdac_t *) priv;

    if (ramdac)
        free(ramdac);
}

const device_t ati68860_ramdac_device = {
    .name          = "ATI-68860 RAMDAC",
    .internal_name = "ati68860_ramdac",
    .flags         = 0,
    .local         = 0,
    .init          = ati68860_ramdac_init,
    .close         = ati68860_ramdac_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

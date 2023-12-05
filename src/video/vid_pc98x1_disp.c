/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the EGC graphics processor used by
 *          the NEC PC-98x1 series of computers.
 *
 *
 *
 * Authors: TAKEDA toshiya,
 *          yui/Neko Project II
 *
 *          Copyright 2009-2023 TAKEDA, toshiya.
 *          Copyright 2008-2023 yui/Neko Project II.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_pc98x1_disp.h>
#include <86box/plat_unused.h>

/***********************************************************/
/* NEC PC-9821 VGA */

enum {
    PALETTE_G           = 0,
    PALETTE_R           = 1,
    PALETTE_B           = 2,
};

enum {
    DIRTY_TVRAM         = 0x01,
    DIRTY_VRAM0         = 0x02,
    DIRTY_VRAM1         = 0x04,
    DIRTY_PALETTE       = 0x10,
    DIRTY_DISPLAY       = 0x80,
};

enum {
    ATTR_ST = 0x01,
    ATTR_BL = 0x02,
    ATTR_RV = 0x04,
    ATTR_UL = 0x08,
    ATTR_VL = 0x10,
    ATTR_COL = 0xe0,
};

static void
pc98x1_update_palette(pc98x1_vid_t *dev)
{
    int i;
    uint8_t r, g, b;

    for (i = 0; i < 8; i++) {
        r = (i & 2) ? 0xff : 0;
        g = (i & 4) ? 0xff : 0;
        b = (i & 1) ? 0xff : 0;
        dev->palette_chr[i] = makecol(r, g, b);
    }
    if (dev->mode2[MODE2_256COLOR]) {
        for (i = 0; i < 256; i++) {
            r = dev->anapal[PALETTE_R][i];
            g = dev->anapal[PALETTE_G][i];
            b = dev->anapal[PALETTE_B][i];
            dev->palette_gfx[i] = makecol(r, g, b);
        }
    } else if (s->mode2[MODE2_16COLOR]) {
        for (i = 0; i < 16; i++) {
            r = dev->anapal[PALETTE_R][i] << 4;
            g = dev->anapal[PALETTE_G][i] << 4;
            b = dev->anapal[PALETTE_B][i] << 4;
            dev->palette_gfx[i] = makecol(r, g, b);
        }
    } else {
        for (i = 0; i < 4; i++) {
            static int lo[4] = {7, 5, 6, 4};
            static int hi[4] = {3, 1, 2, 0};
            r = (dev->digipal[i] & 0x02) ? 0xff : 0;
            g = (dev->digipal[i] & 0x04) ? 0xff : 0;
            b = (dev->digipal[i] & 0x01) ? 0xff : 0;
            dev->palette_gfx[lo[i]] = makecol(r, g, b);
            r = (dev->digipal[i] & 0x20) ? 0xff : 0;
            g = (dev->digipal[i] & 0x40) ? 0xff : 0;
            b = (dev->digipal[i] & 0x10) ? 0xff : 0;
            dev->palette_gfx[hi[i]] = makecol(r, g, b);
        }
    }
}

static void
pc98x1_render_chr_screen(pc98x1_vid_t *dev)
{
    int pl, bl, cl;
    int sur, sdr;
    uint32_t *addr, *addr2;
    uint32_t cursor_addr;
    int cursor_top, cursor_bottom;
    int ytop, ysur, ysdr;
    int l, x, y;
    int xofs, addrofs;

    pl = dev->pl & 31;
    if (pl)
        pl = 32 - pl;

    bl = dev->bl + pl + 1;
    cl = dev->cl;
    sur = dev->sur & 31;
    if (sur)
        sur = 32 - sur;

    sdr = dev->sdr + 1;

    addr = pc98x1_gdc_get_address(&dev->gdc_chr, 2, 0x1fff);
    addr2 = addr + 160 * (sur + sdr);
    pc98x1_gdc_get_cursor_address(&dev->gdc_chr, 0x1fff,
                           &cursor_addr, &cursor_top, &cursor_bottom);
    ytop = 0;
    ysur = bl * sur;
    ysdr = bl * (sur + sdr);

    if (s->mode1[MODE1_COLUMN]) {
        xofs = 16;
        addrofs = 2;
    } else {
        xofs = 8;
        addrofs = 1;
    }
    memset(dev->tvram_buffer, 0, 640 * 480);

    for (y = 0; y < 400; y += bl) {
        uint32_t gaiji1st = 0, last = 0, offset;
        int kanji2nd = 0;
        if (y == ysur) {
            ytop = y;
            y -= dev->ssl;
            ysur = 400;
        }
        if (y >= ysdr) {
            y = ytop = ysdr;
            addr = addr2;
            ysdr = 400;
        }
        for (x = 0; x < 640; x += xofs) {
            uint16_t code = *(uint16_t *)(dev->tvram + *addr);
            uint8_t attr = dev->tvram[*addr | 0x2000];
            uint8_t color = (attr & ATTR_COL) ? (attr >> 5) : 8;
            uint8_t cursor = (*addr == cursor_addr);
            addr += addrofs;
            if (kanji2nd) {
                kanji2nd = 0;
                offset = last + 0x800;
            } else if (code & 0xff00) {
                uint16_t lo = code & 0x7f;
                uint16_t hi = (code >> 8) & 0x7f;
                offset = (lo << 4) | (hi << 12);
                if (lo == 0x56 || lo == 0x57) {
                    offset += gaiji1st;
                    gaiji1st ^= 0x800;
                } else {
                    uint16_t lo = code & 0xff;
                    if (lo < 0x09 || lo >= 0x0c)
                        kanji2nd = 1;

                    gaiji1st = 0;
                }
            } else {
                uint16_t lo = code & 0xff;
                if (dev->mode1[MODE1_FONTSEL])
                    offset = 0x80000 | (lo << 4);
                else
                    offset = 0x82000 | (lo << 4);

                gaiji1st = 0;
            }
            last = offset;
            for (l = 0; l < cl && l < 16; l++) {
                int yy = y + l + pl;
                if (yy >= ytop && yy < 480) {
                    uint8_t *dest = dev->tvram_buffer + yy * 640 + x;
                    uint8_t pattern = dev->font[offset + l];
                    if (!(attr & ATTR_ST))
                        pattern = 0;
                    else if (((attr & ATTR_BL) && (s->blink & 0x20)) ||
                               (attr & ATTR_RV))
                        pattern = ~pattern;

                    if ((attr & ATTR_UL) && l == 15)
                        pattern = 0xff;

                    if (attr & ATTR_VL)
                        pattern |= 0x08;

                    if (cursor && l >= cursor_top && l < cursor_bottom)
                        pattern = ~pattern;

                    if (dev->mode1[MODE1_COLUMN]) {
                        if (pattern & 0x80) dest[ 0] = dest[ 1] = color;
                        if (pattern & 0x40) dest[ 2] = dest[ 3] = color;
                        if (pattern & 0x20) dest[ 4] = dest[ 5] = color;
                        if (pattern & 0x10) dest[ 6] = dest[ 7] = color;
                        if (pattern & 0x08) dest[ 8] = dest[ 9] = color;
                        if (pattern & 0x04) dest[10] = dest[11] = color;
                        if (pattern & 0x02) dest[12] = dest[13] = color;
                        if (pattern & 0x01) dest[14] = dest[15] = color;
                    } else {
                        if (pattern & 0x80) dest[0] = color;
                        if (pattern & 0x40) dest[1] = color;
                        if (pattern & 0x20) dest[2] = color;
                        if (pattern & 0x10) dest[3] = color;
                        if (pattern & 0x08) dest[4] = color;
                        if (pattern & 0x04) dest[5] = color;
                        if (pattern & 0x02) dest[6] = color;
                        if (pattern & 0x01) dest[7] = color;
                    }
                }
            }
        }
    }
}

static void
pc98x1_render_gfx_screen(pc98x1_t *dev)
{
    uint8_t *dest;
    int x, y;
    uint8_t b, r, g, e = 0;

    static int prev_mode = -1;
    int mode;

    if (dev->mode2[MODE2_256COLOR]) {
        int addr = 0;
        if (dev->mode2[MODE2_480LINE]) {
            dest = dev->vram0_buffer;
            for (y = 0; y < 480; y++) {
                for (x = 0; x < 640; x++)
                    *dest++ = dev->vram256[addr++];

                addr += 128 * 3;
            }
            mode = 2;
        } else {
            if (dev->bank_disp == DIRTY_VRAM0)
                dest = dev->vram0_buffer;
            else
                dest = dev->vram1_buffer;

            for (y = 0; y < 400; y++) {
                for (x = 0; x < 640; x++) {
                    *dest++ = dev->vram256_disp[addr++];
                }
            }
            mode = 1;
        }
    } else {
        uint32_t *addr = pc98x1_gdc_get_address(&dev->gdc_gfx, 1, 0x7fff);
        if (dev->bank_disp == DIRTY_VRAM0)
            dest = s->vram0_buffer;
        else
            dest = s->vram1_buffer;

        for (y = 0; y < 400; y++) {
            for (x = 0; x < 640; x += 8) {
                b = dev->vram16_draw_b[*addr];
                r = dev->vram16_draw_r[*addr];
                g = dev->vram16_draw_g[*addr];
                if (dev->mode2[MODE2_16COLOR])
                    e = dev->vram16_draw_e[*addr];

                addr++;
                *dest++ = ((b & 0x80) >> 7) | ((r & 0x80) >> 6) | ((g & 0x80) >> 5) | ((e & 0x80) >> 4);
                *dest++ = ((b & 0x40) >> 6) | ((r & 0x40) >> 5) | ((g & 0x40) >> 4) | ((e & 0x40) >> 3);
                *dest++ = ((b & 0x20) >> 5) | ((r & 0x20) >> 4) | ((g & 0x20) >> 3) | ((e & 0x20) >> 2);
                *dest++ = ((b & 0x10) >> 4) | ((r & 0x10) >> 3) | ((g & 0x10) >> 2) | ((e & 0x10) >> 1);
                *dest++ = ((b & 0x08) >> 3) | ((r & 0x08) >> 2) | ((g & 0x08) >> 1) | ((e & 0x08) >> 0);
                *dest++ = ((b & 0x04) >> 2) | ((r & 0x04) >> 1) | ((g & 0x04) >> 0) | ((e & 0x04) << 1);
                *dest++ = ((b & 0x02) >> 1) | ((r & 0x02) >> 0) | ((g & 0x02) << 1) | ((e & 0x02) << 2);
                *dest++ = ((b & 0x01) >> 0) | ((r & 0x01) << 1) | ((g & 0x01) << 2) | ((e & 0x01) << 3);
            }
            if (dev->mode1[MODE1_200LINE]) {
                memset(dest, 0, 640);
                dest += 640;
                y++;
            }
        }
        mode = 0;
    }
    if (prev_mode != mode) {
        switch (mode) {
            case 0:
                pclog("pc98vga: 640x400, 4bpp\n");
                break;
            case 1:
                pclog("pc98vga: 640x400, 8bpp\n");
                break;
            case 2:
                pclog("pc98vga: 640x480, 8bpp\n");
                break;
        }
        prev_mode = mode;
    }
}

static void
pc98x1_vid_timer(void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint8_t prev_blink = dev->blink;
    uint8_t dirty;
    uint8_t *src_chr;
    uint8_t *src_gfx;

    if (dev->mode2[MODE2_256COLOR] && dev->mode2[MODE2_480LINE])
        dev->height = 480;
    else
        dev->height = 400;

    if (!dev->linepos) {
        timer_advance_u64(&dev->timer, dev->dispofftime);
        dev->linepos = 1;
        if (dev->dispon) {
            if (dev->displine == 0)
                video_wait_for_buffer();

            if (dev->mode1[MODE1_DISP]) {
                if (dev->dirty & DIRTY_PALETTE) {
                    /* update palette */
                    pc98x1_update_palette(dev);
                    dev->dirty &= ~DIRTY_PALETTE;
                    dev->dirty |= DIRTY_DISPLAY;
                }
                if (dev->gdc_chr.dirty & GDC_DIRTY_START) {
                    dev->gdc_chr.dirty &= ~GDC_DIRTY_START;
                    dev->dirty |= DIRTY_DISPLAY;
                }
                if (dev->gdc_chr.start) {
                    if ((dev->gdc_chr.dirty & GDC_DIRTY_CHR) || (dev->dirty & DIRTY_TVRAM)) {
                        /* update text screen */
                        pc98x1_render_chr_screen(dev);
                        dev->gdc_chr.dirty &= ~GDC_DIRTY_CHR;
                        dev->dirty &= ~DIRTY_TVRAM;
                        dev->dirty |= DIRTY_DISPLAY;
                    }
                }
                if (dev->gdc_gfx.dirty & GDC_DIRTY_START) {
                    dev->gdc_gfx.dirty &= ~GDC_DIRTY_START;
                    dev->dirty |= DIRTY_DISPLAY;
                }
                if (dev->gdc_gfx.start) {
                    dirty = dev->bank_disp;
                    if (dev->mode2[MODE2_256COLOR]) {
                        if (dev->mode2[MODE2_480LINE])
                            dirty = DIRTY_VRAM0 | DIRTY_VRAM1;
                    }
                    if ((dev->gdc_gfx.dirty & GDC_DIRTY_GFX) || (dev->dirty & dirty)) {
                        /* update cg screen */
                        pc98x1_render_gfx_screen(dev);
                        dev->gdc_gfx.dirty &= ~GDC_DIRTY_GFX;
                        dev->dirty &= ~dirty;
                        dev->dirty |= DIRTY_DISPLAY;
                    }
                }
            }

            /* update screen */
            if (dev->dirty & DIRTY_DISPLAY) {
                if (dev->mode1[MODE1_DISP]) {
                    /* output screen */
                    if (!dev->gdc_chr.start || dev->mode2[MODE2_256COLOR])
                        src_chr = dev->null_buffer;
                    else
                        src_chr = dev->tvram_buffer;

                    if (!dev->gdc_gfx.start)
                        src_gfx = dev->null_buffer;
                    else if (dev->mode2[MODE2_256COLOR] && dev->mode2[MODE2_480LINE])
                        src_gfx = dev->vram0_buffer;
                    else if (dev->bank_disp == DIRTY_VRAM0)
                        src_gfx = dev->vram0_buffer;
                    else
                        src_gfx = dev->vram1_buffer;

                    for (int y = 0; y < dev->height; y++) {
                        for (int x = 0; x < dev->width; x++) {
                            if (*src_chr)
                                buffer32->line[dev->displine][x] = dev->palette_chr[*src_chr & 0x07];
                            else
                                buffer32->line[dev->displine][x] = dev->palette_gfx[*src_gfx];

                            src_chr++;
                            src_gfx++;
                        }
                    }
                } else {
                    for (int y = 0; y < dev->height; y++) {
                        for (int x = 0; x < dev->width; x++) {
                            buffer32->line[dev->displine][x] = 0;
                        }
                    }
                }
                dev->dirty &= ~DIRTY_DISPLAY;
            }
        }

        if (++dev->displine == dev->height) {
            dev->vsync |= GDC_STAT_VSYNC;
            dev->dispon = 0;
            if (dev->crtv) {
                picint(1 << 2);
                dev->crtv = 0;
            }
        }

        if (dev->displine == dev->height + 32) {
            dev->vsync &= ~GDC_STAT_VSYNC;
            dev->dispon = 1;
            dev->displine  = 0;
        }
    } else {
        timer_advance_u64(&dev->timer, dev->dispontime);
        dev->linepos = 0;

        if (dev->displine == dev->height) {
            /* resize screen */
            if (dev->width != xsize || dev->height != ysize) {
                xsize = dev->width;
                ysize = dev->height;
                set_screen_size(xsize, ysize);

                if (video_force_resize_get())
                    video_force_resize_set(0);

                dev->dirty |= DIRTY_DISPLAY;
            }
            video_blit_memtoscreen(0, 0, xsize, ysize);
            frames++;

            video_res_x = dev->width;
            video_res_y = dev->height;
            video_bpp   = 8;
            /* blink */
            dev->blink++;
            if ((prev_blink & 0x20) != (dev->blink & 0x20)) {
                dev->dirty |= DIRTY_TVRAM;
            }
        }
    }
}

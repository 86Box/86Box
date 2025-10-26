/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          EGA renderers.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/vid_ega.h>
#include <86box/vid_ega_render_remap.h>

int
ega_display_line(ega_t *ega)
{
    int          y_add = enable_overscan ? (overscan_y >> 1) : 0;
    unsigned int dl    = ega->displine;

    if (ega->crtc[9] & 0x1f)
        dl -= (ega->crtc[8] & 0x1f);

    dl += y_add;
    dl &= 0x7ff;
    return dl;
}

void
ega_render_blank(ega_t *ega)
{
    if ((ega->displine + ega->y_add) < 0)
        return;

    for (int x = 0; x < (ega->hdisp + ega->scrollcache); x++) {
        switch (ega->seqregs[1] & 9) {
            case 0:
                for (uint8_t xx = 0; xx < 9; xx++)
                    buffer32->line[ega->displine + ega->y_add][ega->x_add + (x * 9) + xx] = 0;
                break;
            case 1:
                for (uint8_t xx = 0; xx < 8; xx++)
                    buffer32->line[ega->displine + ega->y_add][ega->x_add + (x * 8) + xx] = 0;
                break;
            case 8:
                for (uint8_t xx = 0; xx < 18; xx++)
                    buffer32->line[ega->displine + ega->y_add][ega->x_add + (x * 18) + xx] = 0;
                break;
            case 9:
                for (uint8_t xx = 0; xx < 16; xx++)
                    buffer32->line[ega->displine + ega->y_add][ega->x_add + (x * 16) + xx] = 0;
                break;

            default:
                break;
        }
    }
}

void
ega_render_overscan_left(ega_t *ega)
{
    if ((ega->displine + ega->y_add) < 0)
        return;

    if (ega->scrblank || (ega->hdisp == 0))
        return;

    for (int i = 0; i < ega->x_add; i++)
        buffer32->line[ega->displine + ega->y_add][i] = ega->overscan_color;
}

void
ega_render_overscan_right(ega_t *ega)
{
    int right;

    if ((ega->displine + ega->y_add) < 0)
        return;

    if (ega->scrblank || (ega->hdisp == 0))
        return;

    right = (overscan_x >> 1) + ega->scrollcache;
    for (int i = 0; i < right; i++)
        buffer32->line[ega->displine + ega->y_add][ega->x_add + ega->hdisp + i] = ega->overscan_color;
}

void
ega_render_text(ega_t *ega)
{
    if (ega->render_override) {
        ega->render_override(ega->priv_parent);
        return;
    }

    if ((ega->displine + ega->y_add) < 0)
        return;

    if (ega->firstline_draw == 2000)
        ega->firstline_draw = ega->displine;
    ega->lastline_draw = ega->displine;

    if (ega->fullchange) {
        const bool doublewidth   = ((ega->seqregs[1] & 8) != 0);
        const bool attrblink     = ((ega->attrregs[0x10] & 8) != 0);
        const bool attrlinechars = (ega->attrregs[0x10] & 4);
        const bool monoattrs     = (ega->attrregs[0x10] & 2);
        const bool crtcreset     = ((ega->crtc[0x17] & 0x80) == 0);
        const bool seq9dot       = ((ega->seqregs[1] & 1) == 0);
        const int  dwshift       = doublewidth ? 1 : 0;
        const int  dotwidth      = 1 << dwshift;
        const int  charwidth     = dotwidth * (seq9dot ? 9 : 8);
        const bool blinked       = ega->blink & 0x10;
        uint32_t  *p             = &buffer32->line[ega->displine + ega->y_add][ega->x_add];

        /* Compensate for 8dot scroll */
        if (!seq9dot) {
            for (int x = 0; x < dotwidth; x++) {
                p[x] = ega->overscan_color;
            }
            p += dotwidth;
        }

        for (int x = 0; x < (ega->hdisp + ega->scrollcache); x += charwidth) {
            uint32_t addr = ega->remap_func(ega, ega->memaddr) & ega->vrammask;

            int drawcursor = ((ega->memaddr == ega->cursoraddr) && ega->cursorvisible && ega->cursoron);

            uint32_t chr;
            uint32_t attr;
            if (!crtcreset) {
                chr  = ega->vram[addr];
                attr = ega->vram[addr + 1];
            } else
                chr = attr = 0;

            uint32_t charaddr;
            if (attr & 8)
                charaddr = ega->charsetb + (chr * 0x80);
            else
                charaddr = ega->charseta + (chr * 0x80);

            int fg;
            int bg;
            if (drawcursor) {
                bg = ega->pallook[ega->egapal[attr & 0x0f]];
                fg = ega->pallook[ega->egapal[attr >> 4]];
            } else {
                fg = ega->pallook[ega->egapal[attr & 0x0f]];
                bg = ega->pallook[ega->egapal[attr >> 4]];

                if ((attr & 0x80) && attrblink) {
                    bg = ega->pallook[ega->egapal[(attr >> 4) & 7]];
                    if (blinked)
                        fg = bg;
                }
            }

            uint32_t dat = ega->vram[charaddr + (ega->scanline << 2)];
            dat <<= 1;
            if (((chr & ~0x1f) == 0xc0) && attrlinechars)
                dat |= (dat >> 1) & 1;

            for (int xx = 0; xx < charwidth; xx++) {
                if (monoattrs) {
                    int bit   = (dat & (0x100 >> (xx >> dwshift))) ? 1 : 0;
                    int blink = (!drawcursor && (attr & 0x80) && attrblink && blinked);
                    if ((ega->scanline == ega->crtc[0x14]) && ((attr & 7) == 1))
                        p[xx] = ega->mda_attr_to_color_table[attr][blink][1];
                    else
                        p[xx] = ega->mda_attr_to_color_table[attr][blink][bit];
                    if (drawcursor)
                        p[xx] ^= ega->mda_attr_to_color_table[attr][0][1];
                    p[xx] = ega->pallook[ega->egapal[p[xx] & 0x0f]];
                } else
                    p[xx] = (dat & (0x100 >> (xx >> dwshift))) ? fg : bg;
            }

            ega->memaddr += 4;
            p += charwidth;
        }
        ega->memaddr &= 0x3ffff;
    }
}

void
ega_render_graphics(ega_t *ega)
{
    if ((ega->displine + ega->y_add) < 0)
        return;

    if (ega->firstline_draw == 2000)
        ega->firstline_draw = ega->displine;
    ega->lastline_draw = ega->displine;

    const bool    doublewidth = ((ega->seqregs[1] & 8) != 0);
    const bool    cga2bpp     = ((ega->gdcreg[5] & 0x20) != 0);
    const bool    attrblink   = ((ega->attrregs[0x10] & 8) != 0);
    const bool    blinked     = ega->blink & 0x10;
    const bool    crtcreset   = ((ega->crtc[0x17] & 0x80) == 0);
    const bool    seq9dot       = ((ega->seqregs[1] & 1) == 0);
    const bool    seqoddeven  = ((ega->seqregs[1] & 4) != 0);
    const uint8_t blinkmask   = (attrblink ? 0x8 : 0x0);
    const uint8_t blinkval    = (attrblink && blinked ? 0x8 : 0x0);
    uint32_t     *p           = &buffer32->line[ega->displine + ega->y_add][ega->x_add];
    const int     dwshift     = doublewidth ? 1 : 0;
    const int     dotwidth    = 1 << dwshift;
    const int     charwidth   = dotwidth * 8;
    int           secondcclk  = 0;

    /* Compensate for 8dot scroll */
    if (!seq9dot) {
        for (int x = 0; x < dotwidth; x++) {
            p[x] = ega->overscan_color;
        }
        p += dotwidth;
    }

    for (int x = 0; x <= (ega->hdisp + ega->scrollcache); x += charwidth) {
        uint32_t addr = ega->remap_func(ega, ega->memaddr) & ega->vrammask;

        uint8_t edat[4];
        if (seqoddeven) {
            // FIXME: Verify the behaviour of planes 1,3 on actual hardware
            edat[0]    = ega->vram[(addr | 0) ^ secondcclk];
            edat[1]    = ega->vram[(addr | 1) ^ secondcclk];
            edat[2]    = ega->vram[(addr | 2) ^ secondcclk];
            edat[3]    = ega->vram[(addr | 3) ^ secondcclk];
            secondcclk = (secondcclk + 1) & 1;
            if (secondcclk == 0)
                ega->memaddr += 4;
        } else {
            *(uint32_t *) (&edat[0]) = *(uint32_t *) (&ega->vram[addr]);
            ega->memaddr += 4;
        }
        ega->memaddr &= 0x3ffff;

        if (cga2bpp) {
            // Remap CGA 2bpp-chunky data into fully planar data
            uint8_t dat0 = egaremap2bpp[edat[1]] | (egaremap2bpp[edat[0]] << 4);
            uint8_t dat1 = egaremap2bpp[edat[1] >> 1] | (egaremap2bpp[edat[0] >> 1] << 4);
            uint8_t dat2 = egaremap2bpp[edat[3]] | (egaremap2bpp[edat[2]] << 4);
            uint8_t dat3 = egaremap2bpp[edat[3] >> 1] | (egaremap2bpp[edat[2] >> 1] << 4);
            edat[0]      = dat0;
            edat[1]      = dat1;
            edat[2]      = dat2;
            edat[3]      = dat3;
        }

        if (!crtcreset) {
            for (int i = 0; i < 8; i += 2) {
                const int outoffs = i << dwshift;
                const int inshift = 6 - i;
                uint8_t   dat     = (edatlookup[(edat[0] >> inshift) & 3][(edat[1] >> inshift) & 3])
                    | (edatlookup[(edat[2] >> inshift) & 3][(edat[3] >> inshift) & 3] << 2);
                // FIXME: Confirm blink behaviour is actually XOR on real hardware
                uint32_t c0 = (dat >> 4) & 0xF;
                uint32_t c1 = dat & 0xF;
                c0 = ((c0 & ega->plane_mask & ~blinkmask) |
                     ((c0 | ~ega->plane_mask) & blinkmask & blinkval)) ^ blinkmask;
                c1 = ((c1 & ega->plane_mask & ~blinkmask) |
                     ((c1 | ~ega->plane_mask) & blinkmask & blinkval)) ^ blinkmask;
                uint32_t p0 = ega->pallook[ega->egapal[c0]];
                uint32_t p1 = ega->pallook[ega->egapal[c1]];
                for (int subx = 0; subx < dotwidth; subx++)
                    p[outoffs + subx] = p0;
                for (int subx = 0; subx < dotwidth; subx++)
                    p[outoffs + subx + dotwidth] = p1;
            }
        } else
            memset(p, 0x00, charwidth * sizeof(uint32_t));

        p += charwidth;
    }
}

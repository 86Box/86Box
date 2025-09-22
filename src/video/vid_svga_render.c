/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          SVGA renderers.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_svga_render_remap.h>

uint32_t
svga_lookup_lut_ram(svga_t* svga, uint32_t val)
{
    if (!svga->lut_map)
        return val;

    uint8_t r = getcolr(svga->pallook[getcolr(val)]);
    uint8_t g = getcolg(svga->pallook[getcolg(val)]);
    uint8_t b = getcolb(svga->pallook[getcolb(val)]);
    return makecol32(r, g, b) | (val & 0xFF000000);
}

#define lookup_lut(val) svga_lookup_lut_ram(svga, val)

void
svga_render_null(svga_t *svga)
{
    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;
}

void
svga_render_blank(svga_t *svga)
{
    if ((svga->displine + svga->y_add) < 0 || (svga->displine + svga->y_add) >= 2048)
        return;

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    uint32_t char_width = 0;

    switch (svga->seqregs[1] & 9) {
        case 0:
            char_width = 9;
            break;
        case 1:
            char_width = 8;
            break;
        case 8:
            char_width = 18;
            break;
        case 9:
            char_width = 16;
            break;

        default:
            break;
    }

    uint32_t *line_ptr   = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];
    int32_t   line_width = (uint32_t) (svga->hdisp + svga->scrollcache) * char_width * sizeof(uint32_t);

    if (svga->x_add < 0) {
        line_ptr = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][0];
        line_width -= svga->x_add;
    }

    if (((svga->hdisp + svga->scrollcache) > 0) && (line_width >= 0))
        memset(line_ptr, 0, line_width);
}

void
svga_render_overscan_left(svga_t *svga)
{
    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->scrblank || (svga->hdisp <= 0))
        return;

    uint32_t *line_ptr = svga->monitor->target_buffer->line[svga->displine + svga->y_add];

    if (svga->x_add >= 0)  for (int i = 0; i < svga->x_add; i++)
        *line_ptr++ = svga->overscan_color;
}

void
svga_render_overscan_right(svga_t *svga)
{
    int right;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->scrblank || (svga->hdisp <= 0))
        return;

    uint32_t *line_ptr = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add + svga->hdisp];
    right              = overscan_x  - svga->left_overscan;
    for (int i = 0; i < right; i++)
        *line_ptr++ = svga->overscan_color;
}

void
svga_render_text_40(svga_t *svga)
{
    uint32_t *p;
    int       xx;
    int       drawcursor;
    int       xinc;
    uint8_t   chr;
    uint8_t   attr;
    uint8_t   dat;
    uint32_t  charaddr;
    int       fg;
    int       bg;
    uint32_t  addr = 0;

    if (svga->render_override) {
        svga->render_override(svga->priv_parent);
        return;
    }

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    if (svga->fullchange) {
        p    = &svga->monitor->target_buffer->line[(svga->displine + svga->y_add) & 2047][(svga->x_add) & 2047];
        xinc = (svga->seqregs[1] & 1) ? 16 : 18;

        for (int x = 0; x < (svga->hdisp + svga->scrollcache); x += xinc) {
            if (!svga->force_old_addr)
                addr = svga->remap_func(svga, svga->memaddr) & svga->vram_display_mask;

            drawcursor = ((svga->memaddr == svga->cursoraddr) && svga->cursorvisible && svga->cursoron);

            if (svga->force_old_addr) {
                chr  = svga->vram[(svga->memaddr << 1) & svga->vram_display_mask];
                attr = svga->vram[((svga->memaddr << 1) + 1) & svga->vram_display_mask];
            } else {
                chr  = svga->vram[addr];
                attr = svga->vram[addr + 1];
            }

            if (attr & 8)
                charaddr = svga->charsetb + (chr * 128);
            else
                charaddr = svga->charseta + (chr * 128);

            if (drawcursor) {
                bg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                fg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
            } else {
                fg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                bg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];

                if (attr & 0x80 && svga->attrregs[0x10] & 8) {
                    bg = svga->pallook[svga->egapal[(attr >> 4) & 7] & svga->dac_mask];
                    if (svga->blink & 16)
                        fg = bg;
                }
            }

            dat = svga->vram[charaddr + (svga->scanline << 2)];
            if (svga->seqregs[1] & 1) {
                for (xx = 0; xx < 16; xx += 2)
                    p[xx] = p[xx + 1] = (dat & (0x80 >> (xx >> 1))) ? fg : bg;
            } else {
                for (xx = 0; xx < 16; xx += 2)
                    p[xx] = p[xx + 1] = (dat & (0x80 >> (xx >> 1))) ? fg : bg;
                if ((chr & ~0x1f) != 0xc0 || !(svga->attrregs[0x10] & 4))
                    p[16] = p[17] = bg;
                else
                    p[16] = p[17] = (dat & 1) ? fg : bg;
            }
            svga->memaddr += 4;
            p += xinc;
        }
        svga->memaddr &= svga->vram_display_mask;
    }
}

void
svga_render_text_80(svga_t *svga)
{
    uint32_t *p;
    int       xx;
    int       drawcursor;
    int       xinc;
    uint8_t   chr;
    uint8_t   attr;
    uint8_t   dat;
    uint32_t  charaddr;
    int       fg;
    int       bg;
    uint32_t  addr = 0;

    if (svga->render_override) {
        svga->render_override(svga->priv_parent);
        return;
    }

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    if (svga->fullchange) {
        p    = &svga->monitor->target_buffer->line[(svga->displine + svga->y_add) & 2047][(svga->x_add) & 2047];
        xinc = (svga->seqregs[1] & 1) ? 8 : 9;

        static uint32_t col = 0x00000000;

        for (int x = 0; x < (svga->hdisp + svga->scrollcache); x += xinc) {
            if (!svga->force_old_addr)
                addr = svga->remap_func(svga, svga->memaddr) & svga->vram_display_mask;

            drawcursor = ((svga->memaddr == svga->cursoraddr) && svga->cursorvisible && svga->cursoron);

            if (svga->force_old_addr) {
                chr  = svga->vram[(svga->memaddr << 1) & svga->vram_display_mask];
                attr = svga->vram[((svga->memaddr << 1) + 1) & svga->vram_display_mask];
            } else {
                chr  = svga->vram[addr];
                attr = svga->vram[addr + 1];
            }

            if (attr & 8)
                charaddr = svga->charsetb + (chr * 128);
            else
                charaddr = svga->charseta + (chr * 128);

            if (drawcursor) {
                bg = attr & 15;
                fg = attr >> 4;
            } else {
                fg = attr & 15;
                bg = attr >> 4;
                if (attr & 0x80 && svga->attrregs[0x10] & 8) {
                    bg = (attr >> 4) & 7;
                    if (svga->blink & 16)
                        fg = bg;
                }
            }

            dat = svga->vram[charaddr + (svga->scanline << 2)];

            if (svga->attrregs[0x10] & 0x40) {
                if (svga->seqregs[1] & 1) {
                    for (xx = 0; xx < 8; xx++) {
                        uint32_t col16 = (dat & (0x80 >> xx)) ? fg : bg;
                        if ((x + xx - svga->scrollcache) & 1) {
                            col |= col16;
                            if ((x + xx - 1) >= 0)
                                p[xx - 1] = svga->pallook[col & svga->dac_mask];
                            if ((x + xx) >= 0)
                                p[xx] = svga->pallook[col & svga->dac_mask];
                        } else
                           col = col16 << 4;
                    }
                } else {
                    for (xx = 0; xx < 9; xx++) {
                        uint32_t col16;
                        if (xx < 8)
                            col16 = (dat & (0x80 >> xx)) ? fg : bg;
                        else if ((chr & ~0x1F) != 0xC0 || !(svga->attrregs[0x10] & 4))
                            col16 = bg;
                        else
                            col16 = (dat & 1) ? fg : bg;
                        if ((x + xx - svga->scrollcache) & 1) {
                            col |= col16;
                            if ((x + xx - 1) >= 0)
                                p[xx - 1] = svga->pallook[col & svga->dac_mask];
                            if ((x + xx) >= 0)
                                p[xx] = svga->pallook[col & svga->dac_mask];
                        } else
                           col = col16 << 4;
                    }
                }
            } else {
                if (svga->seqregs[1] & 1) {
                    for (xx = 0; xx < 8; xx++) {
                        col = (col << 4) | ((dat & (0x80 >> xx)) ? fg : bg);
                        p[xx] = svga->pallook[svga->egapal[col & 0x0f] & svga->dac_mask];
                    }
                } else {
                    for (xx = 0; xx < 8; xx++) {
                        col = (col << 4) | ((dat & (0x80 >> xx)) ? fg : bg);
                        p[xx] = svga->pallook[svga->egapal[col & 0x0f] & svga->dac_mask];
                    }
                    if ((chr & ~0x1F) != 0xC0 || !(svga->attrregs[0x10] & 4))
                        col = (col << 4) | bg;
                    else
                        col = (col << 4) | ((dat & 1) ? fg : bg);
                    p[8] = svga->pallook[svga->egapal[col & 0x0f] & svga->dac_mask];
                }
            }

            svga->memaddr += 4;
            p += xinc;
        }
        svga->memaddr &= svga->vram_display_mask;
    }
}

/*Not available on most generic cards.*/
void
svga_render_text_80_ksc5601(svga_t *svga)
{
    uint32_t *p;
    int       xx;
    int       drawcursor;
    int       xinc;
    uint8_t   chr;
    uint8_t   attr;
    uint8_t   dat;
    uint8_t   nextchr;
    uint32_t  charaddr;
    int       fg;
    int       bg;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    if (svga->fullchange) {
        p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

        xinc = (svga->seqregs[1] & 1) ? 8 : 9;

        for (int x = 0; x < (svga->hdisp + svga->scrollcache); x += xinc) {
            uint32_t addr = svga->remap_func(svga, svga->memaddr) & svga->vram_display_mask;
            drawcursor    = ((svga->memaddr == svga->cursoraddr) && svga->cursorvisible && svga->cursoron);
            chr           = svga->vram[addr];
            nextchr       = svga->vram[addr + 8];
            attr          = svga->vram[addr + 1];

            if (drawcursor) {
                bg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                fg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
            } else {
                fg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                bg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
                if (attr & 0x80 && svga->attrregs[0x10] & 8) {
                    bg = svga->pallook[svga->egapal[(attr >> 4) & 7] & svga->dac_mask];
                    if (svga->blink & 16)
                        fg = bg;
                }
            }

            if ((x + xinc) < svga->hdisp && (chr & (nextchr | svga->ksc5601_sbyte_mask) & 0x80)) {
                if ((chr == svga->ksc5601_udc_area_msb[0] || chr == svga->ksc5601_udc_area_msb[1]) && (nextchr > 0xa0 && nextchr < 0xff))
                    dat = fontdatksc5601_user[(chr == svga->ksc5601_udc_area_msb[1] ? 96 : 0) + (nextchr & 0x7F) - 0x20].chr[svga->scanline];
                else if (nextchr & 0x80) {
                    if (svga->ksc5601_swap_mode == 1 && (nextchr > 0xa0 && nextchr < 0xff)) {
                        if (chr >= 0x80 && chr < 0x99)
                            chr += 0x30;
                        else if (chr >= 0xB0 && chr < 0xC9)
                            chr -= 0x30;
                    }
                    dat = fontdatksc5601[((chr & 0x7F) << 7) | (nextchr & 0x7F)].chr[svga->scanline];
                } else
                    dat = 0xff;
            } else {
                if (attr & 8)
                    charaddr = svga->charsetb + (chr * 128);
                else
                    charaddr = svga->charseta + (chr * 128);

                if ((svga->ksc5601_english_font_type >> 8) == 1)
                    dat = fontdatksc5601[((svga->ksc5601_english_font_type & 0x7F) << 7) | (chr >> 1)].chr[((chr & 1) << 4) | svga->scanline];
                else
                    dat = svga->vram[charaddr + (svga->scanline << 2)];
            }

            if (svga->seqregs[1] & 1) {
                for (xx = 0; xx < 8; xx++)
                    p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
            } else {
                for (xx = 0; xx < 8; xx++)
                    p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
                if (((chr & ~0x1f) != 0xc0) || !(svga->attrregs[0x10] & 4))
                    p[8] = bg;
                else
                    p[8] = (dat & 1) ? fg : bg;
            }
            svga->memaddr += 4;
            p += xinc;

            if ((x + xinc) < svga->hdisp && (chr & (nextchr | svga->ksc5601_sbyte_mask) & 0x80)) {
                attr = svga->vram[((svga->memaddr << 1) + 1) & svga->vram_display_mask];

                if (drawcursor) {
                    bg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                    fg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
                } else {
                    fg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                    bg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
                    if (attr & 0x80 && svga->attrregs[0x10] & 8) {
                        bg = svga->pallook[svga->egapal[(attr >> 4) & 7] & svga->dac_mask];
                        if (svga->blink & 16)
                            fg = bg;
                    }
                }

                if ((chr == svga->ksc5601_udc_area_msb[0] || chr == svga->ksc5601_udc_area_msb[1]) && (nextchr > 0xa0 && nextchr < 0xff))
                    dat = fontdatksc5601_user[(chr == svga->ksc5601_udc_area_msb[1] ? 96 : 0) + (nextchr & 0x7F) - 0x20].chr[svga->scanline + 16];
                else if (nextchr & 0x80)
                    dat = fontdatksc5601[((chr & 0x7f) << 7) | (nextchr & 0x7F)].chr[svga->scanline + 16];
                else
                    dat = 0xff;

                if (svga->seqregs[1] & 1) {
                    for (xx = 0; xx < 8; xx++)
                        p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
                } else {
                    for (xx = 0; xx < 8; xx++)
                        p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
                    if (((chr & ~0x1f) != 0xc0) || !(svga->attrregs[0x10] & 4))
                        p[8] = bg;
                    else
                        p[8] = (dat & 1) ? fg : bg;
                }

                svga->memaddr += 4;
                p += xinc;
                x += xinc;
            }
        }
        svga->memaddr &= svga->vram_display_mask;
    }
}

void
svga_render_2bpp_s3_lowres(svga_t *svga)
{
    int       changed_offset;
    int       x;
    uint8_t   dat[2];
    uint32_t  addr;
    uint32_t *p;
    uint32_t  changed_addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        changed_offset = ((svga->memaddr << 1) + (svga->scanline & ~svga->crtc[0x17] & 3) * 0x8000) >> 12;

        if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 16) {
                addr = svga->memaddr;

                if (!(svga->crtc[0x17] & 0x40)) {
                    addr = (addr << 1) & svga->vram_mask;
                    addr &= ~7;

                    if ((svga->crtc[0x17] & 0x20) && (svga->memaddr & 0x20000))
                        addr |= 4;

                    if (!(svga->crtc[0x17] & 0x20) && (svga->memaddr & 0x8000))
                        addr |= 4;
                }

                if (!(svga->crtc[0x17] & 0x01))
                    addr = (addr & ~0x8000) | ((svga->scanline & 1) ? 0x8000 : 0);

                if (!(svga->crtc[0x17] & 0x02))
                    addr = (addr & ~0x10000) | ((svga->scanline & 2) ? 0x10000 : 0);

                dat[0] = svga->vram[addr];
                dat[1] = svga->vram[addr | 0x1];
                if (svga->seqregs[1] & 4)
                    svga->memaddr += 2;
                else
                    svga->memaddr += 4;
                svga->memaddr &= svga->vram_mask;
                p[0] = p[1] = svga->pallook[svga->egapal[(dat[0] >> 6) & 3] & svga->dac_mask];
                p[2] = p[3] = svga->pallook[svga->egapal[(dat[0] >> 4) & 3] & svga->dac_mask];
                p[4] = p[5] = svga->pallook[svga->egapal[(dat[0] >> 2) & 3] & svga->dac_mask];
                p[6] = p[7] = svga->pallook[svga->egapal[dat[0] & 3] & svga->dac_mask];
                p[8] = p[9] = svga->pallook[svga->egapal[(dat[1] >> 6) & 3] & svga->dac_mask];
                p[10] = p[11] = svga->pallook[svga->egapal[(dat[1] >> 4) & 3] & svga->dac_mask];
                p[12] = p[13] = svga->pallook[svga->egapal[(dat[1] >> 2) & 3] & svga->dac_mask];
                p[14] = p[15] = svga->pallook[svga->egapal[dat[1] & 3] & svga->dac_mask];
                p += 16;
            }
        }
    } else {
        changed_addr = svga->remap_func(svga, svga->memaddr);

        if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 16) {
                addr = svga->remap_func(svga, svga->memaddr);

                dat[0] = svga->vram[addr];
                dat[1] = svga->vram[addr | 0x1];
                if (svga->seqregs[1] & 4)
                    svga->memaddr += 2;
                else
                    svga->memaddr += 4;

                svga->memaddr &= svga->vram_mask;

                p[0] = p[1] = svga->pallook[svga->egapal[(dat[0] >> 6) & 3] & svga->dac_mask];
                p[2] = p[3] = svga->pallook[svga->egapal[(dat[0] >> 4) & 3] & svga->dac_mask];
                p[4] = p[5] = svga->pallook[svga->egapal[(dat[0] >> 2) & 3] & svga->dac_mask];
                p[6] = p[7] = svga->pallook[svga->egapal[dat[0] & 3] & svga->dac_mask];
                p[8] = p[9] = svga->pallook[svga->egapal[(dat[1] >> 6) & 3] & svga->dac_mask];
                p[10] = p[11] = svga->pallook[svga->egapal[(dat[1] >> 4) & 3] & svga->dac_mask];
                p[12] = p[13] = svga->pallook[svga->egapal[(dat[1] >> 2) & 3] & svga->dac_mask];
                p[14] = p[15] = svga->pallook[svga->egapal[dat[1] & 3] & svga->dac_mask];

                p += 16;
            }
        }
    }
}

void
svga_render_2bpp_s3_highres(svga_t *svga)
{
    int       changed_offset;
    int       x;
    uint8_t   dat[2];
    uint32_t  addr;
    uint32_t *p;
    uint32_t  changed_addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        changed_offset = ((svga->memaddr << 1) + (svga->scanline & ~svga->crtc[0x17] & 3) * 0x8000) >> 12;

        if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
                addr = svga->memaddr;

                if (!(svga->crtc[0x17] & 0x40)) {
                    addr = (addr << 1) & svga->vram_mask;
                    addr &= ~7;

                    if ((svga->crtc[0x17] & 0x20) && (svga->memaddr & 0x20000))
                        addr |= 4;

                    if (!(svga->crtc[0x17] & 0x20) && (svga->memaddr & 0x8000))
                        addr |= 4;
                }

                if (!(svga->crtc[0x17] & 0x01))
                    addr = (addr & ~0x8000) | ((svga->scanline & 1) ? 0x8000 : 0);

                if (!(svga->crtc[0x17] & 0x02))
                    addr = (addr & ~0x10000) | ((svga->scanline & 2) ? 0x10000 : 0);

                dat[0] = svga->vram[addr];
                dat[1] = svga->vram[addr | 0x1];
                if (svga->seqregs[1] & 4)
                    svga->memaddr += 2;
                else
                    svga->memaddr += 4;
                svga->memaddr &= svga->vram_mask;
                p[0] = svga->pallook[svga->egapal[(dat[0] >> 6) & 3] & svga->dac_mask];
                p[1] = svga->pallook[svga->egapal[(dat[0] >> 4) & 3] & svga->dac_mask];
                p[2] = svga->pallook[svga->egapal[(dat[0] >> 2) & 3] & svga->dac_mask];
                p[3] = svga->pallook[svga->egapal[dat[0] & 3] & svga->dac_mask];
                p[4] = svga->pallook[svga->egapal[(dat[1] >> 6) & 3] & svga->dac_mask];
                p[5] = svga->pallook[svga->egapal[(dat[1] >> 4) & 3] & svga->dac_mask];
                p[6] = svga->pallook[svga->egapal[(dat[1] >> 2) & 3] & svga->dac_mask];
                p[7] = svga->pallook[svga->egapal[dat[1] & 3] & svga->dac_mask];
                p += 8;
            }
        }
    } else {
        changed_addr = svga->remap_func(svga, svga->memaddr);

        if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
                addr = svga->remap_func(svga, svga->memaddr);

                dat[0] = svga->vram[addr];
                dat[1] = svga->vram[addr | 0x1];
                if (svga->seqregs[1] & 4)
                    svga->memaddr += 2;
                else
                    svga->memaddr += 4;

                svga->memaddr &= svga->vram_mask;

                p[0] = svga->pallook[svga->egapal[(dat[0] >> 6) & 3] & svga->dac_mask];
                p[1] = svga->pallook[svga->egapal[(dat[0] >> 4) & 3] & svga->dac_mask];
                p[2] = svga->pallook[svga->egapal[(dat[0] >> 2) & 3] & svga->dac_mask];
                p[3] = svga->pallook[svga->egapal[dat[0] & 3] & svga->dac_mask];
                p[4] = svga->pallook[svga->egapal[(dat[1] >> 6) & 3] & svga->dac_mask];
                p[5] = svga->pallook[svga->egapal[(dat[1] >> 4) & 3] & svga->dac_mask];
                p[6] = svga->pallook[svga->egapal[(dat[1] >> 2) & 3] & svga->dac_mask];
                p[7] = svga->pallook[svga->egapal[dat[1] & 3] & svga->dac_mask];

                p += 8;
            }
        }
    }
}

void
svga_render_2bpp_headland_highres(svga_t *svga)
{
    int       oddeven;
    uint32_t  addr;
    uint32_t *p;
    uint8_t   edat[4];
    uint8_t   dat;
    uint32_t  changed_addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    changed_addr = svga->remap_func(svga, svga->memaddr);

    if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
        p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        for (int x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
            addr    = svga->remap_func(svga, svga->memaddr);
            oddeven = 0;

            if (svga->seqregs[1] & 4) {
                oddeven = (addr & 4) ? 1 : 0;
                edat[0] = svga->vram[addr | oddeven];
                edat[2] = svga->vram[addr | oddeven | 0x2];
                edat[1] = edat[3] = 0;
            } else {
                *(uint32_t *) (&edat[0]) = *(uint32_t *) (&svga->vram[addr]);
            }
            svga->memaddr += 4;
            svga->memaddr &= svga->vram_mask;

            dat  = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
            p[0] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask];
            p[1] = svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask];
            dat  = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
            p[2] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask];
            p[3] = svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask];
            dat  = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
            p[4] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask];
            p[5] = svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask];
            dat  = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
            p[6] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask];
            p[7] = svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask];

            p += 8;
        }
    }
}

static void
svga_render_indexed_gfx(svga_t *svga, bool highres, bool combine8bits)
{
    int       x;
    uint32_t  addr;
    uint32_t *p;
    uint32_t  changed_offset;

    const bool blinked   = !!(svga->blink & 0x10);
    const bool attrblink = (!svga->disable_blink) && ((svga->attrregs[0x10] & 0x08) != 0);

    /*
       The following is likely how it works on an IBM VGA - that is, it works with its BIOS.
       But on some cards, certain modes are broken.
       - S3 Trio: mode 13h (320x200x8), incbypow2 given as 2 treated as 0
       - ET4000/W32i: mode 2Eh (640x480x8), incevery given as 2 treated as 1
     */
    const bool forcepacked = combine8bits && (svga->force_old_addr || svga->packed_chain4);

    /*
       SVGA cards with a high-resolution 8bpp mode may actually bypass the VGA shifter logic.
       - HT-216 (+ other Video7 chipsets?) has 0x3C4.0xC8 bit 4 which, when set to 1, loads
         bytes directly, bypassing the shifters.
     */
    const bool highres8bpp = (combine8bits && highres) || svga->force_shifter_bypass;

    const bool     dwordload  = ((svga->seqregs[0x01] & 0x10) != 0);
    const bool     wordload   = ((svga->seqregs[0x01] & 0x04) != 0) && !dwordload;
    const bool     wordincr   = ((svga->crtc[0x17] & 0x08) != 0);
    const bool     dwordincr  = ((svga->crtc[0x14] & 0x20) != 0) && !wordincr;
    const bool     dwordshift = ((svga->crtc[0x14] & 0x40) != 0);
    const bool     wordshift  = ((svga->crtc[0x17] & 0x40) == 0) && !dwordshift;
    const uint32_t incbypow2  = forcepacked ? 0 : (dwordshift ? 2 : wordshift ? 1 : 0);
    const uint32_t incevery   = forcepacked ? 1 : (dwordincr ? 4 : wordincr ? 2 : 1);
    const uint32_t loadevery  = forcepacked ? 1 : (dwordload ? 4 : wordload ? 2 : 1);

    const bool shift4bit = ((svga->gdcreg[0x05] & 0x40) == 0x40) || highres8bpp;
    const bool shift2bit = (((svga->gdcreg[0x05] & 0x60) == 0x20) && !shift4bit);

    const int      dwshift   = highres ? 0 : 1;
    const int      dotwidth  = 1 << dwshift;
    const int      charwidth = dotwidth * ((combine8bits && !svga->packed_4bpp) ? 4 : 8);
    const uint32_t planemask = 0x11111111 * (uint32_t) (svga->plane_mask);
    const uint32_t blinkmask = (attrblink ? 0x88888888 : 0x0);
    const uint32_t blinkval  = (attrblink && blinked ? 0x88888888 : 0x0);

    /*
       This is actually a 8x 3-bit lookup table,
       preshifted by 2 bits to allow shifting by multiples of 4 bits.

       Anyway, when we perform a planar-to-chunky conversion,
       we keep the pixel values in a scrambled order.
       This lookup table unscrambles them.

       WARNING: Octal values are used here!
     */
    const uint32_t shift_values = (shift4bit
                                       ? ((067452301) << 2)
                                       : shift2bit
                                       ? ((026370415) << 2)
                                       : ((002461357) << 2));

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr)
        changed_offset = (svga->memaddr + (svga->scanline & ~svga->crtc[0x17] & 3) * 0x8000) >> 12;
    else
        changed_offset = svga->remap_func(svga, svga->memaddr) >> 12;

    if (!(svga->changedvram[changed_offset] || svga->changedvram[changed_offset + 1] || svga->fullchange))
        return;
    p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

    if (svga->render_line_offset) {
        if (svga->render_line_offset > 0) {
            memset(p, svga->overscan_color, (size_t) charwidth * svga->render_line_offset * sizeof(uint32_t));
            p += charwidth * svga->render_line_offset;
        }
    }

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    uint32_t incr_counter = 0;
    uint32_t load_counter = 0;
    uint32_t edat         = 0;
    static uint32_t col          = 0;
    static uint32_t col2         = 0;
    for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += charwidth) {
        if (load_counter == 0) {
            /* Find our address */
            if (svga->force_old_addr) {
                addr = ((svga->memaddr & ~0x3) << incbypow2);

                if (incbypow2 == 2) {
                    if (svga->memaddr & (4 << 15))
                        addr |= 0x8;
                    if (svga->memaddr & (4 << 14))
                        addr |= 0x4;
                } else if (incbypow2 == 1) {
                    if ((svga->crtc[0x17] & 0x20)) {
                        if (svga->memaddr & (4 << 15))
                            addr |= 0x4;
                    } else {
                        if (svga->memaddr & (4 << 13))
                            addr |= 0x4;
                    }
                } else {
                    /* Nothing */
                }

                if (!(svga->crtc[0x17] & 0x01))
                    addr = (addr & ~0x8000) | ((svga->scanline & 1) ? 0x8000 : 0);
                if (!(svga->crtc[0x17] & 0x02))
                    addr = (addr & ~0x10000) | ((svga->scanline & 2) ? 0x10000 : 0);
            } else if (svga->remap_required)
                addr = svga->remap_func(svga, svga->memaddr);
            else
                addr = svga->memaddr;

            addr &= svga->vram_display_mask;

            /* Load VRAM */
            edat = *(uint32_t *) &svga->vram[addr];

            /*
               EGA and VGA actually use 4bpp planar as its native format.
               But 4bpp chunky is generally easier to deal with on a modern CPU.
               shift4bit is the native format for this renderer (4bpp chunky).
             */
            if (svga->ati_4color || !shift4bit) {
                if (shift2bit && !svga->ati_4color) {
                    /* Group 2x 2bpp values into 4bpp values */
                    edat = (edat & 0xCCCC3333) | ((edat << 14) & 0x33330000) | ((edat >> 14) & 0x0000CCCC);
                } else {
                    /* Group 4x 1bpp values into 4bpp values */
                    edat = (edat & 0xAA55AA55) | ((edat << 7) & 0x55005500) | ((edat >> 7) & 0x00AA00AA);
                    edat = (edat & 0xCCCC3333) | ((edat << 14) & 0x33330000) | ((edat >> 14) & 0x0000CCCC);
                }
            }
        } else {
            /*
               According to the 82C451 VGA clone chipset datasheet, all 4 planes chain in a ring.
               So, rotate them all around.
               Planar version: edat = (edat >> 8) | (edat << 24);
               Here's the chunky version...
             */
            edat = ((edat >> 1) & 0x77777777) | ((edat << 3) & 0x88888888);
        }
        load_counter += 1;
        if (load_counter >= loadevery)
            load_counter = 0;

        incr_counter += 1;
        if (incr_counter >= incevery) {
            incr_counter = 0;
            svga->memaddr += 4;
            /* DISCREPANCY TODO FIXME 2/4bpp used vram_mask, 8bpp used vram_display_mask --GM */
            svga->memaddr &= svga->vram_display_mask;
        }

        uint32_t current_shift = shift_values;
        uint32_t out_edat      = edat;
        /*
           Apply blink
           FIXME: Confirm blink behaviour on real hardware

           The VGA 4bpp graphics blink logic was a pain to work out.

           If plane 3 is enabled in the attribute controller, then:
           - if bit 3 is 0, then we force the output of it to be 1.
           - if bit 3 is 1, then the output blinks.
           This can be tested with Lotus 1-2-3 release 2.3 with the WYSIWYG addon.

           If plane 3 is disabled in the attribute controller, then the output blinks.
           This can be tested with QBASIC SCREEN 10 - anything using color #2 should
           blink and nothing else.

           If you can simplify the following and have it still work, give yourself a medal.
         */
        out_edat = ((out_edat & planemask & ~blinkmask) | ((out_edat | ~planemask) & blinkmask & blinkval)) ^ blinkmask;

        for (int i = 0; i < (8 + (svga->ati_4color ? 8 : 0)); i += (svga->ati_4color ? 4 : 2)) {
            /*
               c0 denotes the first 4bpp pixel shifted, while c1 denotes the second.
               For 8bpp modes, the first 4bpp pixel is the upper 4 bits.
             */
            uint32_t c0 = (out_edat >> (current_shift & 0x1C)) & 0xF;
            current_shift >>= 3;
            uint32_t c1 = (out_edat >> (current_shift & 0x1C)) & 0xF;
            current_shift >>= 3;

            if (svga->ati_4color) {
                uint32_t  q[4];
                q[0]      = svga->pallook[svga->egapal[(c0 & 0x0c) >> 2]];
                q[1]      = svga->pallook[svga->egapal[c0 & 0x03]];
                q[2]      = svga->pallook[svga->egapal[(c1 & 0x0c) >> 2]];
                q[3]      = svga->pallook[svga->egapal[c1 & 0x03]];

                const int outoffs = i << dwshift;
                for (int ch = 0; ch < 4; ch++) {
                    for (int subx = 0; subx < dotwidth; subx++)
                        p[outoffs + subx + (dotwidth * ch)] = q[ch];
                }
            } else if (combine8bits) {
                if (svga->packed_4bpp) {
                    uint32_t  p0;
                    uint32_t  p1;
                    if (svga->half_pixel) {
                        col                 &= 0xf0;
                        col                 |= (c0 >> 4) & 0xff;
                        col2                 = (c0 << 4) & 0xff;
                        col2                |= (c1 >> 4) & 0xff;
                        p0                  = svga->map8[col & svga->dac_mask];
                        p1                  = svga->map8[col2 & svga->dac_mask];
                        col                 = (c1 << 4) & 0xff;
                    } else {
                        p0                = svga->map8[c0 & svga->dac_mask];
                        p1                = svga->map8[c1 & svga->dac_mask];
                        col                 = p1;
                    }
                    const int outoffs = i << dwshift;
                    for (int subx = 0; subx < dotwidth; subx++)
                        p[outoffs + subx] = p0;
                    for (int subx = 0; subx < dotwidth; subx++)
                        p[outoffs + subx + dotwidth] = p1;
                } else {
                    uint32_t  ccombined = (c0 << 4) | c1;
                    uint32_t  p0;
                    if (svga->half_pixel) {
                        col                 &= 0xf0;
                        col                 |= (ccombined >> 4) & 0xff;
                        p0                  = svga->map8[col & svga->dac_mask];
                        col                 = (ccombined << 4) & 0xff;
                    } else {
                        p0                  = svga->map8[ccombined & svga->dac_mask];
                        col                 = p0;
                    }
                    const int outoffs   = (i >> 1) << dwshift;
                    for (int subx = 0; subx < dotwidth; subx++)
                        p[outoffs + subx] = p0;
                }
            } else {
                uint32_t  p0      = svga->pallook[svga->egapal[c0] & svga->dac_mask];
                uint32_t  p1      = svga->pallook[svga->egapal[c1] & svga->dac_mask];
                const int outoffs = i << dwshift;
                for (int subx = 0; subx < dotwidth; subx++)
                    p[outoffs + subx] = p0;
                for (int subx = 0; subx < dotwidth; subx++)
                    p[outoffs + subx + dotwidth] = p1;
                if ((x + i - svga->scrollcache) & 0x01)
                    /* The lower 4 bits are undefined at this point. */
                    col = c1 << 4;
                else
                    col = (c0 << 4) | c1;
            }
        }

        if (svga->ati_4color)
            p += (charwidth << 1);
            // p += charwidth;
        else
            p += charwidth;
    }

    if (svga->render_line_offset < 0) {
        uint32_t *orig_line = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];
        memmove(orig_line, orig_line + (charwidth * -svga->render_line_offset), (svga->hdisp) * 4);
        memset((orig_line + svga->hdisp) - (charwidth * -svga->render_line_offset), svga->overscan_color, (size_t) charwidth * -svga->render_line_offset * 4);
    }
}

/*
   Remap these to the paletted renderer
   (*, highres, combine8bits)
 */
void svga_render_2bpp_lowres(svga_t *svga) { svga_render_indexed_gfx(svga, false, false); }
void svga_render_2bpp_highres(svga_t *svga) { svga_render_indexed_gfx(svga, true, false); }
void svga_render_4bpp_lowres(svga_t *svga) { svga_render_indexed_gfx(svga, false, false); }
void svga_render_4bpp_highres(svga_t *svga) { svga_render_indexed_gfx(svga, true, false); }
void svga_render_8bpp_lowres(svga_t *svga) { svga_render_indexed_gfx(svga, false, true); }
void svga_render_8bpp_highres(svga_t *svga) { svga_render_indexed_gfx(svga, true, true); }

void
svga_render_4bpp_tseng_highres(svga_t *svga)
{
    int       changed_offset;
    int       x;
    int       oddeven;
    uint32_t  addr;
    uint32_t *p;
    uint8_t   edat[4];
    uint8_t   dat;
    uint32_t  changed_addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        changed_offset = (svga->memaddr + (svga->scanline & ~svga->crtc[0x17] & 3) * 0x8000) >> 12;

        if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
                addr    = svga->memaddr;
                oddeven = 0;

                if (!(svga->crtc[0x17] & 0x40)) {
                    addr = (addr << 1) & svga->vram_mask;

                    if (svga->seqregs[1] & 4)
                        oddeven = (addr & 4) ? 1 : 0;

                    addr &= ~7;

                    if ((svga->crtc[0x17] & 0x20) && (svga->memaddr & 0x20000))
                        addr |= 4;
                    if (!(svga->crtc[0x17] & 0x20) && (svga->memaddr & 0x8000))
                        addr |= 4;
                }

                if (!(svga->crtc[0x17] & 0x01))
                    addr = (addr & ~0x8000) | ((svga->scanline & 1) ? 0x8000 : 0);
                if (!(svga->crtc[0x17] & 0x02))
                    addr = (addr & ~0x10000) | ((svga->scanline & 2) ? 0x10000 : 0);

                if (svga->seqregs[1] & 4) {
                    edat[0] = svga->vram[addr | oddeven];
                    edat[2] = svga->vram[addr | oddeven | 0x2];
                    edat[1] = edat[3] = 0;
                    svga->memaddr += 2;
                } else {
                    *(uint32_t *) (&edat[0]) = *(uint32_t *) (&svga->vram[addr]);
                    svga->memaddr += 4;
                }
                svga->memaddr &= svga->vram_mask;

                dat  = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
                p[0] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
                p[1] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
                dat  = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
                p[2] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
                p[3] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
                dat  = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
                p[4] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
                p[5] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
                dat  = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
                p[6] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
                p[7] = svga->pallook[svga->egapal[dat & svga->plane_mask]];

                p += 8;
            }
        }
    } else {
        changed_addr = svga->remap_func(svga, svga->memaddr);

        if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
                addr    = svga->remap_func(svga, svga->memaddr);
                oddeven = 0;

                if (svga->seqregs[1] & 4) {
                    oddeven = (addr & 4) ? 1 : 0;
                    edat[0] = svga->vram[addr | oddeven];
                    edat[2] = svga->vram[addr | oddeven | 0x2];
                    edat[1] = edat[3] = 0;
                    svga->memaddr += 2;
                } else {
                    *(uint32_t *) (&edat[0]) = *(uint32_t *) (&svga->vram[addr]);
                    svga->memaddr += 4;
                }
                svga->memaddr &= svga->vram_mask;

                dat  = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
                p[0] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
                p[1] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
                dat  = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
                p[2] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
                p[3] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
                dat  = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
                p[4] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
                p[5] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
                dat  = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
                p[6] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
                p[7] = svga->pallook[svga->egapal[dat & svga->plane_mask]];

                p += 8;
            }
        }
    }
}

void
svga_render_8bpp_clone_highres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp /* + svga->scrollcache*/); x += 8) {
                dat  = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
                p[0] = svga->map8[dat & svga->dac_mask & 0xff];
                p[1] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                p[2] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                p[3] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + 4) & svga->vram_display_mask]);
                p[4] = svga->map8[dat & svga->dac_mask & 0xff];
                p[5] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                p[6] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                p[7] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                svga->memaddr += 8;
                p += 8;
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
                for (x = 0; x <= (svga->hdisp /* + svga->scrollcache*/); x += 8) {
                    dat  = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
                    p[0] = svga->map8[dat & svga->dac_mask & 0xff];
                    p[1] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                    p[2] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                    p[3] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + 4) & svga->vram_display_mask]);
                    p[4] = svga->map8[dat & svga->dac_mask & 0xff];
                    p[5] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                    p[6] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                    p[7] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                    svga->memaddr += 8;
                    p += 8;
                }
            } else {
                for (x = 0; x <= (svga->hdisp /* + svga->scrollcache*/); x += 4) {
                    addr = svga->remap_func(svga, svga->memaddr);
                    dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                    p[0] = svga->map8[dat & svga->dac_mask & 0xff];
                    p[1] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                    p[2] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                    p[3] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                    svga->memaddr += 4;
                    p += 4;
                }
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    }
}

// TODO: Integrate more of this into the generic paletted renderer --GM
#if 0
void
svga_render_8bpp_lowres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
                dat = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);

                p[0] = p[1] = svga->map8[dat & 0xff];
                p[2] = p[3] = svga->map8[(dat >> 8) & 0xff];
                p[4] = p[5] = svga->map8[(dat >> 16) & 0xff];
                p[6] = p[7] = svga->map8[(dat >> 24) & 0xff];

                svga->memaddr += 4;
                p += 8;
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
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
                    dat  = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
                    p[0] = p[1] = svga->map8[dat & svga->dac_mask & 0xff];
                    p[2] = p[3] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                    p[4] = p[5] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                    p[6] = p[7] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                    svga->memaddr += 4;
                    p += 8;
                }
            } else {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
                    addr = svga->remap_func(svga, svga->memaddr);
                    dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                    p[0] = p[1] = svga->map8[dat & svga->dac_mask & 0xff];
                    p[2] = p[3] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                    p[4] = p[5] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                    p[6] = p[7] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                    svga->memaddr += 4;
                    p += 8;
                }
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    }
}

void
svga_render_8bpp_highres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp /* + svga->scrollcache*/); x += 8) {
                dat  = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
                p[0] = svga->map8[dat & svga->dac_mask & 0xff];
                p[1] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                p[2] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                p[3] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + 4) & svga->vram_display_mask]);
                p[4] = svga->map8[dat & svga->dac_mask & 0xff];
                p[5] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                p[6] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                p[7] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                svga->memaddr += 8;
                p += 8;
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
                for (x = 0; x <= (svga->hdisp /* + svga->scrollcache*/); x += 8) {
                    dat  = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
                    p[0] = svga->map8[dat & svga->dac_mask & 0xff];
                    p[1] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                    p[2] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                    p[3] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + 4) & svga->vram_display_mask]);
                    p[4] = svga->map8[dat & svga->dac_mask & 0xff];
                    p[5] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                    p[6] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                    p[7] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                    svga->memaddr += 8;
                    p += 8;
                }
            } else {
                for (x = 0; x <= (svga->hdisp /* + svga->scrollcache*/); x += 4) {
                    addr = svga->remap_func(svga, svga->memaddr);
                    dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                    p[0] = svga->map8[dat & svga->dac_mask & 0xff];
                    p[1] = svga->map8[(dat >> 8) & svga->dac_mask & 0xff];
                    p[2] = svga->map8[(dat >> 16) & svga->dac_mask & 0xff];
                    p[3] = svga->map8[(dat >> 24) & svga->dac_mask & 0xff];

                    svga->memaddr += 4;
                    p += 4;
                }
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    }
}
#endif

void
svga_render_8bpp_tseng_lowres(svga_t *svga)
{
    uint32_t *p;
    uint32_t  dat;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange || svga->render_line_offset) {
        p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        if (svga->render_line_offset) {
            if (svga->render_line_offset > 0) {
                memset(p, svga->overscan_color, 8 * svga->render_line_offset * sizeof(uint32_t));
                p += 8 * svga->render_line_offset;
            }
        }

        for (int x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
            dat = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[0] = p[1] = svga->map8[dat & svga->dac_mask & 0xff];
            dat >>= 8;
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[2] = p[3] = svga->map8[dat & svga->dac_mask & 0xff];
            dat >>= 8;
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[4] = p[5] = svga->map8[dat & svga->dac_mask & 0xff];
            dat >>= 8;
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[6] = p[7] = svga->map8[dat & svga->dac_mask & 0xff];

            svga->memaddr += 4;
            p += 8;
        }
        if (svga->render_line_offset < 0) {
            uint32_t *orig_line = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];
            memmove(orig_line, orig_line + (8 * -svga->render_line_offset), (svga->hdisp) * 4);
            memset((orig_line + svga->hdisp) - (8 * -svga->render_line_offset), svga->overscan_color, 8 * -svga->render_line_offset * 4);
        }
        svga->memaddr &= svga->vram_display_mask;
    }
}

void
svga_render_8bpp_tseng_highres(svga_t *svga)
{
    uint32_t *p;
    uint32_t  dat;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange || svga->render_line_offset) {
        p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        if (svga->render_line_offset) {
            if (svga->render_line_offset > 0) {
                memset(p, svga->overscan_color, 8 * svga->render_line_offset * sizeof(uint32_t));
                p += 8 * svga->render_line_offset;
            }
        }

        for (int x = 0; x <= (svga->hdisp /* + svga->scrollcache*/); x += 8) {
            dat = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[0] = svga->map8[dat & svga->dac_mask & 0xff];
            dat >>= 8;
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[1] = svga->map8[dat & svga->dac_mask & 0xff];
            dat >>= 8;
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[2] = svga->map8[dat & svga->dac_mask & 0xff];
            dat >>= 8;
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[3] = svga->map8[dat & svga->dac_mask & 0xff];

            dat = *(uint32_t *) (&svga->vram[(svga->memaddr + 4) & svga->vram_display_mask]);
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[4] = svga->map8[dat & svga->dac_mask & 0xff];
            dat >>= 8;
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[5] = svga->map8[dat & svga->dac_mask & 0xff];
            dat >>= 8;
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[6] = svga->map8[dat & svga->dac_mask & 0xff];
            dat >>= 8;
            if (svga->attrregs[0x10] & 0x80)
                dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
            p[7] = svga->map8[dat & svga->dac_mask & 0xff];

            svga->memaddr += 8;
            p += 8;
        }

        if (svga->render_line_offset < 0) {
            uint32_t *orig_line = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];
            memmove(orig_line, orig_line + (8 * -svga->render_line_offset), (svga->hdisp) * 4);
            memset((orig_line + svga->hdisp) - (8 * -svga->render_line_offset), svga->overscan_color, 8 * -svga->render_line_offset * 4);
        }

        svga->memaddr &= svga->vram_display_mask;
    }
}

void
svga_render_15bpp_lowres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
                dat = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1)) & svga->vram_display_mask]);

                p[x << 1] = p[(x << 1) + 1] = svga->conv_16to32(svga, dat & 0xffff, 15);
                p[(x << 1) + 2] = p[(x << 1) + 3] = svga->conv_16to32(svga, dat >> 16, 15);

                dat = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 4) & svga->vram_display_mask]);

                p[(x << 1) + 4] = p[(x << 1) + 5] = svga->conv_16to32(svga, dat & 0xffff, 15);
                p[(x << 1) + 6] = p[(x << 1) + 7] = svga->conv_16to32(svga, dat >> 16, 15);
            }
            svga->memaddr += x << 1;
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
                    dat = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1)) & svga->vram_display_mask]);

                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 15);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 15);

                    dat = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 4) & svga->vram_display_mask]);

                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 15);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 15);
                }
                svga->memaddr += x << 1;
            } else {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 2) {
                    addr = svga->remap_func(svga, svga->memaddr);
                    dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);

                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 15);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 15);
                    svga->memaddr += 4;
                }
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    }
}

void
svga_render_15bpp_highres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
                dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1)) & svga->vram_display_mask]);
                p[x]     = svga->conv_16to32(svga, dat & 0xffff, 15);
                p[x + 1] = svga->conv_16to32(svga, dat >> 16, 15);

                dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 4) & svga->vram_display_mask]);
                p[x + 2] = svga->conv_16to32(svga, dat & 0xffff, 15);
                p[x + 3] = svga->conv_16to32(svga, dat >> 16, 15);

                dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 8) & svga->vram_display_mask]);
                p[x + 4] = svga->conv_16to32(svga, dat & 0xffff, 15);
                p[x + 5] = svga->conv_16to32(svga, dat >> 16, 15);

                dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 12) & svga->vram_display_mask]);
                p[x + 6] = svga->conv_16to32(svga, dat & 0xffff, 15);
                p[x + 7] = svga->conv_16to32(svga, dat >> 16, 15);
            }
            svga->memaddr += x << 1;
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
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1)) & svga->vram_display_mask]);
                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 15);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 15);

                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 4) & svga->vram_display_mask]);
                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 15);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 15);

                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 8) & svga->vram_display_mask]);
                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 15);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 15);

                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 12) & svga->vram_display_mask]);
                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 15);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 15);
                }
                svga->memaddr += x << 1;
            } else {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 2) {
                    addr = svga->remap_func(svga, svga->memaddr);
                    dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);

                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 15);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 15);
                    svga->memaddr += 4;
                }
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    }
}

void
svga_render_15bpp_mix_lowres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
        p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
            dat       = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1)) & svga->vram_display_mask]);
            p[x << 1] = p[(x << 1) + 1] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);

            dat >>= 16;
            p[(x << 1) + 2] = p[(x << 1) + 3] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);

            dat             = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 4) & svga->vram_display_mask]);
            p[(x << 1) + 4] = p[(x << 1) + 5] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);

            dat >>= 16;
            p[(x << 1) + 6] = p[(x << 1) + 7] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);
        }
        svga->memaddr += x << 1;
        svga->memaddr &= svga->vram_display_mask;
    }
}

void
svga_render_15bpp_mix_highres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
        p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
            dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1)) & svga->vram_display_mask]);
            p[x] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);
            dat >>= 16;
            p[x + 1] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);

            dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 4) & svga->vram_display_mask]);
            p[x + 2] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);
            dat >>= 16;
            p[x + 3] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);

            dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 8) & svga->vram_display_mask]);
            p[x + 4] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);
            dat >>= 16;
            p[x + 5] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);

            dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 12) & svga->vram_display_mask]);
            p[x + 6] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);
            dat >>= 16;
            p[x + 7] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : svga->conv_16to32(svga, dat & 0xffff, 15);
        }
        svga->memaddr += x << 1;
        svga->memaddr &= svga->vram_display_mask;
    }
}

void
svga_render_16bpp_lowres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
                dat       = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1)) & svga->vram_display_mask]);
                p[x << 1] = p[(x << 1) + 1] = svga->conv_16to32(svga, dat & 0xffff, 16);
                p[(x << 1) + 2] = p[(x << 1) + 3] = svga->conv_16to32(svga, dat >> 16, 16);

                dat             = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 4) & svga->vram_display_mask]);
                p[(x << 1) + 4] = p[(x << 1) + 5] = svga->conv_16to32(svga, dat & 0xffff, 16);
                p[(x << 1) + 6] = p[(x << 1) + 7] = svga->conv_16to32(svga, dat >> 16, 16);
            }
            svga->memaddr += x << 1;
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
                    dat = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1)) & svga->vram_display_mask]);

                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 16);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 16);

                    dat = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 4) & svga->vram_display_mask]);

                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 16);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 16);
                }
                svga->memaddr += x << 1;
            } else {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 2) {
                    addr = svga->remap_func(svga, svga->memaddr);
                    dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);

                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 16);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 16);
                }
                svga->memaddr += 4;
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    }
}

void
svga_render_16bpp_highres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
                uint32_t dat = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1)) & svga->vram_display_mask]);
                p[x]         = svga->conv_16to32(svga, dat & 0xffff, 16);
                p[x + 1]     = svga->conv_16to32(svga, dat >> 16, 16);

                dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 4) & svga->vram_display_mask]);
                p[x + 2] = svga->conv_16to32(svga, dat & 0xffff, 16);
                p[x + 3] = svga->conv_16to32(svga, dat >> 16, 16);

                dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 8) & svga->vram_display_mask]);
                p[x + 4] = svga->conv_16to32(svga, dat & 0xffff, 16);
                p[x + 5] = svga->conv_16to32(svga, dat >> 16, 16);

                dat      = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 12) & svga->vram_display_mask]);
                p[x + 6] = svga->conv_16to32(svga, dat & 0xffff, 16);
                p[x + 7] = svga->conv_16to32(svga, dat >> 16, 16);
            }
            svga->memaddr += x << 1;
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
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1)) & svga->vram_display_mask]);
                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 16);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 16);

                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 4) & svga->vram_display_mask]);
                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 16);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 16);

                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 8) & svga->vram_display_mask]);
                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 16);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 16);

                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 1) + 12) & svga->vram_display_mask]);
                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 16);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 16);
                }
                svga->memaddr += x << 1;
            } else {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 2) {
                    addr = svga->remap_func(svga, svga->memaddr);
                    dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);

                    *p++ = svga->conv_16to32(svga, dat & 0xffff, 16);
                    *p++ = svga->conv_16to32(svga, dat >> 16, 16);

                    svga->memaddr += 4;
                }
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    }
}

void
svga_render_24bpp_lowres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  changed_addr;
    uint32_t  addr;
    uint32_t  dat0;
    uint32_t  dat1;
    uint32_t  dat2;
    uint32_t  fg;

    if ((svga->displine + svga->y_add) < 0)
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
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                    dat0 = *(uint32_t *) (&svga->vram[svga->memaddr & svga->vram_display_mask]);
                    dat1 = *(uint32_t *) (&svga->vram[(svga->memaddr + 4) & svga->vram_display_mask]);
                    dat2 = *(uint32_t *) (&svga->vram[(svga->memaddr + 8) & svga->vram_display_mask]);

                    p[0] = p[1] = lookup_lut(dat0 & 0xffffff);
                    p[2] = p[3] = lookup_lut((dat0 >> 24) | ((dat1 & 0xffff) << 8));
                    p[4] = p[5] = lookup_lut((dat1 >> 16) | ((dat2 & 0xff) << 16));
                    p[6] = p[7] = lookup_lut(dat2 >> 8);

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

                    svga->memaddr += 12;
                }
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    }
}

void
svga_render_24bpp_highres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  changed_addr;
    uint8_t   addr;
    uint32_t  dat0;
    uint32_t  dat1;
    uint32_t  dat2;
    uint32_t  dat;

    if ((svga->displine + svga->y_add) < 0)
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
svga_render_32bpp_lowres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->fullchange) {
            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                dat = svga->vram[svga->memaddr] | (svga->vram[svga->memaddr + 1] << 8) | (svga->vram[svga->memaddr + 2] << 16);
                svga->memaddr += 4;
                svga->memaddr &= svga->vram_display_mask;
                svga->monitor->target_buffer->line[svga->displine + svga->y_add][(x << 1) + svga->x_add] = svga->monitor->target_buffer->line[svga->displine + svga->y_add][(x << 1) + 1 + svga->x_add] = lookup_lut(dat);
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
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 2)) & svga->vram_display_mask]);
                    *p++ = lookup_lut(dat & 0xffffff);
                    *p++ = lookup_lut(dat & 0xffffff);
                }
                svga->memaddr += (x * 4);
            } else {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                    addr = svga->remap_func(svga, svga->memaddr);
                    dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                    *p++ = lookup_lut(dat & 0xffffff);
                    *p++ = lookup_lut(dat & 0xffffff);
                    svga->memaddr += 4;
                }
                svga->memaddr &= svga->vram_display_mask;
            }
        }
    }
}

void
svga_render_32bpp_highres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (svga->force_old_addr) {
        if (svga->changedvram[svga->memaddr >> 12] || svga->changedvram[(svga->memaddr >> 12) + 1] || svga->changedvram[(svga->memaddr >> 12) + 2] || svga->fullchange) {
            p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

            if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
            svga->lastline_draw = svga->displine;

            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 2)) & svga->vram_display_mask]);
                p[x] = lookup_lut(dat & 0xffffff);
            }
            svga->memaddr += 4;
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
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                    dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 2)) & svga->vram_display_mask]);
                    *p++ = lookup_lut(dat & 0xffffff);
                }
                svga->memaddr += (x * 4);
            } else {
                for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                    addr = svga->remap_func(svga, svga->memaddr);
                    dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                    *p++ = lookup_lut(dat & 0xffffff);

                    svga->memaddr += 4;
                }
            }
            svga->memaddr &= svga->vram_display_mask;
        }
    }
}

void
svga_render_ABGR8888_highres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    changed_addr = svga->remap_func(svga, svga->memaddr);

    if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
        p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        if (!svga->remap_required) {
            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 2)) & svga->vram_display_mask]);
                *p++ = lookup_lut(((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16));
            }
            svga->memaddr += x * 4;
        } else {
            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                addr = svga->remap_func(svga, svga->memaddr);
                dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                *p++ = lookup_lut(((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16));

                svga->memaddr += 4;
            }
        }
        svga->memaddr &= svga->vram_display_mask;
    }
}

void
svga_render_RGBA8888_highres(svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;
    uint32_t  changed_addr;
    uint32_t  addr;

    if ((svga->displine + svga->y_add) < 0)
        return;

    changed_addr = svga->remap_func(svga, svga->memaddr);

    if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
        p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;

        if (!svga->remap_required) {
            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                dat  = *(uint32_t *) (&svga->vram[(svga->memaddr + (x << 2)) & svga->vram_display_mask]);
                *p++ = lookup_lut(dat >> 8);
            }
            svga->memaddr += (x * 4);
        } else {
            for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
                addr = svga->remap_func(svga, svga->memaddr);
                dat  = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);
                *p++ = lookup_lut(dat >> 8);

                svga->memaddr += 4;
            }
        }
        svga->memaddr &= svga->vram_display_mask;
    }
}

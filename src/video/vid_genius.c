/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          MDSI Genius VHR emulation.
 *
 *
 *
 * Authors: John Elliott, <jce@seasip.info>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 John Elliott.
 *          Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/video.h>

#define BIOS_ROM_PATH "roms/video/genius/8x12.bin"

#define GENIUS_XSIZE  728
#define GENIUS_YSIZE  1008

extern uint8_t fontdat8x12[256][16];

static video_timings_t timing_genius = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

/* I'm at something of a disadvantage writing this emulation: I don't have an
 * MDSI Genius card, nor do I have the BIOS extension (VHRBIOS.SYS) that came
 * with it. What I do have are the GEM and Windows 1.04 drivers, plus a driver
 * for a later MCA version of the card. The latter can be found at
 * <http://files.mpoli.fi/hardware/DISPLAY/GENIUS/> and is necessary if you
 * want the Windows driver to work.
 *
 * This emulation appears to work correctly with:
 * The MCA drivers GMC_ANSI.SYS and INS_ANSI.SYS
 * The GEM driver SDGEN9.VGA
 * The Windows 1.04 driver GENIUS.DRV
 *
 * As far as I can see, the card uses a fixed resolution of 728x1008 pixels.
 * It has the following modes of operation:
 *
 * > MDA-compatible:      80x25 text, each character 9x15 pixels.
 * > CGA-compatible:      640x200 mono graphics
 * > Dual:                MDA text in the top half, CGA graphics in the bottom
 * > Native text:         80x66 text, each character 9x15 pixels.
 * > Native graphics:     728x1008 mono graphics.
 *
 * Under the covers, this seems to translate to:
 *  > Text framebuffer.     At B000:0000, 16k. Displayed if enable bit is set
 *                         in the MDA control register.
 *  > Graphics framebuffer. In native modes goes from A000:0000 to A000:FFFF
 *                         and B800:0000 to B800:FFFF. In CGA-compatible
 *                         mode only the section at B800:0000 to B800:7FFF
 *                         is visible. Displayed if enable bit is set in the
 *                         CGA control register.
 *
 * Two card-specific registers control text and graphics display:
 *
 *  03B0: Control register.
 *         Bit 0: Map all graphics framebuffer into memory.
 *         Bit 2: Unknown. Set by GMC /M; cleared by mode set or GMC /T.
 *         Bit 4: Set for CGA-compatible graphics, clear for native graphics.
 *         Bit 5: Set for black on white, clear for white on black.
 *
 *  03B1: Character height register.
 *        Bits 0-1: Character cell height (0 => 15, 1 => 14, 2 => 13, 3 => 12)
 *        Bit  4:   Set to double character cell height (scanlines are doubled)
 *        Bit  7:   Unknown, seems to be set for all modes except 80x66
 *
 *  Not having the card also means I don't have its font. According to the
 *  card brochure the font is an 8x12 bitmap in a 9x15 character cell. I
 *  therefore generated it by taking the MDA font, increasing graphics to
 *  16 pixels in height and reducing the height of characters so they fit
 *  in an 8x12 cell if necessary.
 */

typedef struct genius_t {
    mem_mapping_t mapping;

    uint8_t mda_crtc[32];   /* The 'CRTC' as the host PC sees it */
    int     mda_crtcreg;    /* Current CRTC register */
    uint8_t cga_crtc[32];   /* The 'CRTC' as the host PC sees it */
    int     cga_crtcreg;    /* Current CRTC register */
    uint8_t genius_control; /* Native control register
                             * I think bit 0 enables the full
                             * framebuffer.
                             */
    uint8_t genius_charh;   /* Native character height register:
                             * 00h => chars are 15 pixels high
                             * 81h => chars are 14 pixels high
                             * 83h => chars are 12 pixels high
                             * 90h => chars are 30 pixels high [15 x 2]
                             * 93h => chars are 24 pixels high [12 x 2]
                             */
    uint8_t genius_mode;    /* Current mode (see list at top of file) */
    uint8_t cga_ctrl;       /* Emulated CGA control register */
    uint8_t mda_ctrl;       /* Emulated MDA control register */
    uint8_t cga_colour;     /* Emulated CGA colour register (ignored) */

    uint8_t mda_stat; /* MDA status (IN 0x3BA) */
    uint8_t cga_stat; /* CGA status (IN 0x3DA) */

    int font;    /* Current font, 0 or 1 */
    int enabled; /* Display enabled, 0 or 1 */
    int detach;  /* Detach cursor, 0 or 1 */

    uint64_t   dispontime, dispofftime;
    pc_timer_t timer;

    int linepos, displine;
    int vc;
    int dispon, blink;
    int vsynctime;

    uint8_t *vram;
} genius_t;

static uint8_t genius_pal[4];

/* Mapping of attributes to colours, in MDA emulation mode */
static uint8_t mdaattr[256][2][2];

void    genius_recalctimings(genius_t *genius);
void    genius_write(uint32_t addr, uint8_t val, void *priv);
uint8_t genius_read(uint32_t addr, void *priv);

void
genius_out(uint16_t addr, uint8_t val, void *priv)
{
    genius_t *genius = (genius_t *) priv;

    switch (addr) {
        case 0x3b0: /* Command / control register */
            genius->genius_control = val;
            if (val & 1)
                mem_mapping_set_addr(&genius->mapping, 0xa0000, 0x28000);
            else
                mem_mapping_set_addr(&genius->mapping, 0xb0000, 0x10000);
            break;

        case 0x3b1:
            genius->genius_charh = val;
            break;

        /* Emulated CRTC, register select */
        case 0x3b2:
        case 0x3b4:
        case 0x3b6:
            genius->mda_crtcreg = val & 31;
            break;

        /* Emulated CRTC, value */
        case 0x3b3:
        case 0x3b5:
        case 0x3b7:
            genius->mda_crtc[genius->mda_crtcreg] = val;
            genius_recalctimings(genius);
            return;

        /* Emulated MDA control register */
        case 0x3b8:
            genius->mda_ctrl = val;
            return;

        /* Emulated CRTC, register select */
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            genius->cga_crtcreg = val & 31;
            break;

        /* Emulated CRTC, value */
        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            genius->cga_crtc[genius->cga_crtcreg] = val;
            genius_recalctimings(genius);
            return;

        /* Emulated CGA control register */
        case 0x3d8:
            genius->cga_ctrl = val;
            return;
        /* Emulated CGA colour register */
        case 0x3d9:
            genius->cga_colour = val;
            return;

        default:
            break;
    }
}

uint8_t
genius_in(uint16_t addr, void *priv)
{
    const genius_t *genius = (genius_t *) priv;
    uint8_t         ret    = 0xff;

    switch (addr) {
        case 0x3b0:
        case 0x3b2:
        case 0x3b4:
        case 0x3b6:
            ret = genius->mda_crtcreg;
            break;
        case 0x3b1:
        case 0x3b3:
        case 0x3b5:
        case 0x3b7:
            ret = genius->mda_crtc[genius->mda_crtcreg];
            break;
        case 0x3b8:
            ret = genius->mda_ctrl;
            break;
        case 0x3ba:
            ret = genius->mda_stat;
            break;
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            ret = genius->cga_crtcreg;
            break;
        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            ret = genius->cga_crtc[genius->cga_crtcreg];
            break;
        case 0x3d8:
            ret = genius->cga_ctrl;
            break;
        case 0x3d9:
            ret = genius->cga_colour;
            break;
        case 0x3da:
            ret = genius->cga_stat;
            break;

        default:
            break;
    }

    return ret;
}

static void
genius_waitstates(void)
{
    int ws_array[16] = { 3, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8 };
    int ws;

    ws = ws_array[cycles & 0xf];
    cycles -= ws;
}

void
genius_write(uint32_t addr, uint8_t val, void *priv)
{
    genius_t *genius = (genius_t *) priv;
    genius_waitstates();

    if (genius->genius_control & 1) {
        if ((addr >= 0xa0000) && (addr < 0xb0000))
            addr = (addr - 0xa0000) & 0xffff;
        else if ((addr >= 0xb0000) && (addr < 0xb8000))
            addr = ((addr - 0xb0000) & 0x7fff) + 0x10000;
        else
            addr = ((addr - 0xb8000) & 0xffff) + 0x18000;
    } else {
        /* If hi-res memory is disabled, only visible in the B000 segment */
        if (addr >= 0xb8000)
            addr = (addr & 0x3FFF) + 0x18000;
        else
            addr = (addr & 0x7FFF) + 0x10000;
    }

    genius->vram[addr] = val;
}

uint8_t
genius_read(uint32_t addr, void *priv)
{
    const genius_t *genius = (genius_t *) priv;
    uint8_t         ret;

    genius_waitstates();

    if (genius->genius_control & 1) {
        if ((addr >= 0xa0000) && (addr < 0xb0000))
            addr = (addr - 0xa0000) & 0xffff;
        else if ((addr >= 0xb0000) && (addr < 0xb8000))
            addr = ((addr - 0xb0000) & 0x7fff) + 0x10000;
        else
            addr = ((addr - 0xb8000) & 0xffff) + 0x18000;
    } else {
        /* If hi-res memory is disabled, only visible in the B000 segment */
        if (addr >= 0xb8000)
            addr = (addr & 0x3FFF) + 0x18000;
        else
            addr = (addr & 0x7FFF) + 0x10000;
    }

    ret = genius->vram[addr];
    return ret;
}

void
genius_recalctimings(genius_t *genius)
{
    double disptime;
    double _dispontime;
    double _dispofftime;

    disptime     = 0x31;
    _dispontime  = 0x28;
    _dispofftime = disptime - _dispontime;
    _dispontime *= MDACONST;
    _dispofftime *= MDACONST;
    genius->dispontime  = (uint64_t) (_dispontime);
    genius->dispofftime = (uint64_t) (_dispofftime);
}

static int
genius_lines(genius_t *genius)
{
    int ret = 350;

    switch (genius->genius_charh & 0x13) {
        case 0x00:
            ret = 990; /* 80x66 */
            break;
        case 0x01:
            ret = 980; /* 80x70 */
            break;
        case 0x02:
            ret = 988; /* Guess: 80x76 */
            break;
        case 0x03:
            ret = 984; /* 80x82 */
            break;
        case 0x10:
            ret = 375; /* Logic says 80x33 but it appears to be 80x25 */
            break;
        case 0x11:
            ret = 490; /* Guess: 80x35, fits the logic as well, half of 80x70 */
            break;
        case 0x12:
            ret = 494; /* Guess: 80x38 */
            break;
        case 0x13:
            ret = 492; /* 80x41 */
            break;

        default:
            break;
    }

    return ret;
}

/* Draw a single line of the screen in either text mode */
static void
genius_textline(genius_t *genius, uint8_t background, int mda, int cols80)
{
    int            w  = 80; /* 80 characters across */
    int            cw = 9;  /* Each character is 9 pixels wide */
    uint8_t        chr;
    uint8_t        attr;
    uint8_t        sc;
    uint8_t        ctrl;
    const uint8_t *crtc;
    uint8_t        bitmap[2];
    int            blink;
    int            c;
    int            row;
    int            charh;
    int            drawcursor;
    int            cursorline;
    uint16_t       addr;
    uint16_t       ma       = (genius->mda_crtc[13] | (genius->mda_crtc[12] << 8)) & 0x3fff;
    uint16_t       ca       = (genius->mda_crtc[15] | (genius->mda_crtc[14] << 8)) & 0x3fff;
    const uint8_t *framebuf = genius->vram + 0x10000;
    uint32_t       col;
    uint32_t       dl = genius->displine;

    /* Character height is 12-15 */
    if (mda) {
        if (genius->displine >= genius_lines(genius))
            return;

        crtc  = genius->mda_crtc;
        ctrl  = genius->mda_ctrl;
        charh = 15 - (genius->genius_charh & 3);

#if 0
    if (genius->genius_charh & 0x10) {
        row = ((dl >> 1) / charh);
        sc  = ((dl >> 1) % charh);
    } else {
        row = (dl / charh);
        sc  = (dl % charh);
    }
#else
        row = (dl / charh);
        sc  = (dl % charh);
#endif
    } else {
        if ((genius->displine < 512) || (genius->displine >= 912))
            return;

        crtc = genius->cga_crtc;
        ctrl = genius->cga_ctrl;
        framebuf += 0x08000;

        dl -= 512;
        w     = crtc[1];
        cw    = 8;
        charh = crtc[9] + 1;

        row = ((dl >> 1) / charh);
        sc  = ((dl >> 1) % charh);
    }

    ma = (crtc[13] | (crtc[12] << 8)) & 0x3fff;
    ca = (crtc[15] | (crtc[14] << 8)) & 0x3fff;

    addr = ((ma & ~1) + row * w) * 2;

    if (!mda)
        dl += 512;

    ma += (row * w);

    if ((crtc[10] & 0x60) == 0x20)
        cursorline = 0;
    else
        cursorline = ((crtc[10] & 0x1F) <= sc) && ((crtc[11] & 0x1F) >= sc);

    for (int x = 0; x < w; x++) {
#if 0
    if ((genius->genius_charh & 0x10) && ((addr + 2 * x) > 0x0FFF))
        chr = 0x00;
    if ((genius->genius_charh & 0x10) && ((addr + 2 * x + 1) > 0x0FFF))
        attr = 0x00;
#endif
        chr  = framebuf[(addr + 2 * x) & 0x3FFF];
        attr = framebuf[(addr + 2 * x + 1) & 0x3FFF];

        drawcursor = ((ma == ca) && cursorline && genius->enabled && (ctrl & 8));

        switch (crtc[10] & 0x60) {
            case 0x00:
                drawcursor = drawcursor && (genius->blink & 16);
                break;
            case 0x60:
                drawcursor = drawcursor && (genius->blink & 32);
                break;

            default:
                break;
        }

        blink = ((genius->blink & 16) && (ctrl & 0x20) && (attr & 0x80) && !drawcursor);

        if (ctrl & 0x20)
            attr &= 0x7F;

        /* MDA underline */
        if (mda && (sc == charh) && ((attr & 7) == 1)) {
            col = mdaattr[attr][blink][1];

            if (genius->genius_control & 0x20)
                col ^= 15;

            for (c = 0; c < cw; c++) {
                if (col != background) {
                    if (cols80)
                        buffer32->line[dl][(x * cw) + c] = col;
                    else {
                        buffer32->line[dl][((x * cw) << 1) + (c << 1)] = buffer32->line[dl][((x * cw) << 1) + (c << 1) + 1] = col;
                    }
                }
            }
        } else { /* Draw 8 pixels of character */
            if (mda)
                bitmap[0] = fontdat8x12[chr][sc];
            else
                bitmap[0] = fontdat[chr][sc];

            for (c = 0; c < 8; c++) {
                col = mdaattr[attr][blink][(bitmap[0] & (1 << (c ^ 7))) ? 1 : 0];
                if (!(genius->enabled) || !(ctrl & 8))
                    col = mdaattr[0][0][0];

                if (genius->genius_control & 0x20)
                    col ^= 15;

                if (col != background) {
                    if (cols80)
                        buffer32->line[dl][(x * cw) + c] = col;
                    else {
                        buffer32->line[dl][((x * cw) << 1) + (c << 1)] = buffer32->line[dl][((x * cw) << 1) + (c << 1) + 1] = col;
                    }
                }
            }

            if (cw == 9) {
                /* The ninth pixel column... */
                if ((chr & ~0x1f) == 0xc0) {
                    /* Echo column 8 for the graphics chars */
                    if (cols80) {
                        col = buffer32->line[dl][(x * cw) + 7];
                        if (col != background)
                            buffer32->line[dl][(x * cw) + 8] = col;
                    } else {
                        col = buffer32->line[dl][((x * cw) << 1) + 14];
                        if (col != background) {
                            buffer32->line[dl][((x * cw) << 1) + 16] = buffer32->line[dl][((x * cw) << 1) + 17] = col;
                        }
                    }
                } else { /* Otherwise fill with background */
                    col = mdaattr[attr][blink][0];
                    if (genius->genius_control & 0x20)
                        col ^= 15;
                    if (col != background) {
                        if (cols80)
                            buffer32->line[dl][(x * cw) + 8] = col;
                        else {
                            buffer32->line[dl][((x * cw) << 1) + 16] = buffer32->line[dl][((x * cw) << 1) + 17] = col;
                        }
                    }
                }
            }

            if (drawcursor) {
                for (c = 0; c < cw; c++) {
                    if (cols80)
                        buffer32->line[dl][(x * cw) + c] ^= mdaattr[attr][0][1];
                    else {
                        buffer32->line[dl][((x * cw) << 1) + (c << 1)] ^= mdaattr[attr][0][1];
                        buffer32->line[dl][((x * cw) << 1) + (c << 1) + 1] ^= mdaattr[attr][0][1];
                    }
                }
            }
            ++ma;
        }
    }
}

/* Draw a line in the CGA 640x200 mode */
void
genius_cgaline(genius_t *genius)
{
    uint32_t dat;
    uint32_t addr;
    uint8_t  ink_f;
    uint8_t  ink_b;

    ink_f = (genius->genius_control & 0x20) ? genius_pal[0] : genius_pal[3];
    ink_b = (genius->genius_control & 0x20) ? genius_pal[3] : genius_pal[0];

    /* We draw the CGA at row 512 */
    if ((genius->displine < 512) || (genius->displine >= 912))
        return;

    addr = 0x18000 + 80 * ((genius->displine - 512) >> 2);
    if ((genius->displine - 512) & 2)
        addr += 0x2000;

    for (uint8_t x = 0; x < 80; x++) {
        dat = genius->vram[addr];
        addr++;

        for (uint8_t c = 0; c < 8; c++) {
            if (dat & 0x80)
                buffer32->line[genius->displine][(x << 3) + c] = ink_f;
            else
                buffer32->line[genius->displine][(x << 3) + c] = ink_b;

            dat = dat << 1;
        }
    }
}

/* Draw a line in the native high-resolution mode */
void
genius_hiresline(genius_t *genius)
{
    uint32_t dat;
    uint32_t addr;
    uint8_t  ink_f;
    uint8_t  ink_b;

    ink_f = (genius->genius_control & 0x20) ? genius_pal[0] : genius_pal[3];
    ink_b = (genius->genius_control & 0x20) ? genius_pal[3] : genius_pal[0];

    /* The first 512 lines live at A0000 */
    if (genius->displine < 512)
        addr = 128 * genius->displine;
    else /* The second 496 live at B8000 */
        addr = 0x18000 + (128 * (genius->displine - 512));

    for (uint8_t x = 0; x < 91; x++) {
        dat = genius->vram[addr + x];

        for (uint8_t c = 0; c < 8; c++) {
            if (dat & 0x80)
                buffer32->line[genius->displine][(x << 3) + c] = ink_f;
            else
                buffer32->line[genius->displine][(x << 3) + c] = ink_b;

            dat = dat << 1;
        }
    }
}

void
genius_poll(void *priv)
{
    genius_t *genius = (genius_t *) priv;
    uint8_t   background;

    if (!genius->linepos) {
        timer_advance_u64(&genius->timer, genius->dispofftime);
        genius->cga_stat |= 1;
        genius->mda_stat |= 1;
        genius->linepos = 1;

        if (genius->dispon) {
            if (genius->genius_control & 0x20)
                background = genius_pal[3];
            else
                background = genius_pal[0];

            if (genius->displine == 0)
                video_wait_for_buffer();

            /* Start off with a blank line */
            for (uint16_t x = 0; x < GENIUS_XSIZE; x++)
                buffer32->line[genius->displine][x] = background;

            /* If graphics display enabled, draw graphics on top
             * of the blanked line */
            if (genius->cga_ctrl & 8) {
                if (((genius->genius_control & 0x11) == 0x00) || (genius->genius_control & 0x08))
                    genius_cgaline(genius);
                else if ((genius->genius_control & 0x11) == 0x01)
                    genius_hiresline(genius);
                else {
                    if (genius->cga_ctrl & 2)
                        genius_cgaline(genius);
                    else {
                        if (genius->cga_ctrl & 1)
                            genius_textline(genius, background, 0, 1);
                        else
                            genius_textline(genius, background, 0, 0);
                    }
                }
            }

            /* If MDA display is enabled, draw MDA text on top
             * of the lot */
            if (genius->mda_ctrl & 8)
                genius_textline(genius, background, 1, 1);

            video_process_8(GENIUS_XSIZE, genius->displine);
        }
        genius->displine++;
        /* Hardcode a fixed refresh rate and VSYNC timing */
        if (genius->displine == 1008) { /* Start of VSYNC */
            genius->cga_stat |= 8;
            genius->mda_stat |= 8;
            genius->dispon = 0;
        }
        if (genius->displine == 1040) { /* End of VSYNC */
            genius->displine = 0;
            genius->cga_stat &= ~8;
            genius->mda_stat &= ~8;
            genius->dispon = 1;
        }
    } else {
        if (genius->dispon) {
            genius->cga_stat &= ~1;
            genius->mda_stat &= ~1;
        }
        timer_advance_u64(&genius->timer, genius->dispontime);
        genius->linepos = 0;

        if (genius->displine == 1008) {
            /* Hardcode GENIUS_XSIZE * GENIUS_YSIZE window size */
            if (GENIUS_XSIZE != xsize || GENIUS_YSIZE != ysize) {
                xsize = GENIUS_XSIZE;
                ysize = GENIUS_YSIZE;
                if (xsize < 64)
                    xsize = 656;
                if (ysize < 32)
                    ysize = 200;
                set_screen_size(xsize, ysize);

                if (video_force_resize_get())
                    video_force_resize_set(0);
            }

            video_blit_memtoscreen(0, 0, xsize, ysize);

            frames++;
            /* Fixed 728x1008 resolution */
            video_res_x = GENIUS_XSIZE;
            video_res_y = GENIUS_YSIZE;
            video_bpp   = 1;
            genius->blink++;
        }
    }
}

void *
genius_init(UNUSED(const device_t *info))
{
    genius_t *genius = malloc(sizeof(genius_t));

    memset(genius, 0, sizeof(genius_t));

    video_inform(VIDEO_FLAG_TYPE_MDA, &timing_genius);

    /* 160k video RAM */
    genius->vram = malloc(0x28000);

    loadfont(BIOS_ROM_PATH, 4);

    timer_add(&genius->timer, genius_poll, genius, 1);

    /* Occupy memory between 0xB0000 and 0xBFFFF (moves to 0xA0000 in
     * high-resolution modes)  */
    mem_mapping_add(&genius->mapping, 0xb0000, 0x10000, genius_read, NULL, NULL, genius_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, genius);

    /* Respond to both MDA and CGA I/O ports */
    io_sethandler(0x03b0, 0x000C, genius_in, NULL, NULL, genius_out, NULL, NULL, genius);
    io_sethandler(0x03d0, 0x0010, genius_in, NULL, NULL, genius_out, NULL, NULL, genius);

    genius_pal[0] = 0 + 16;  /* 0 */
    genius_pal[1] = 8 + 16;  /* 8 */
    genius_pal[2] = 7 + 16;  /* 7 */
    genius_pal[3] = 15 + 16; /* F */

    /* MDA attributes */
    /* I don't know if the Genius's MDA emulation actually does
     * emulate bright / non-bright. For the time being pretend it does. */
    for (uint16_t c = 0; c < 256; c++) {
        mdaattr[c][0][0] = mdaattr[c][1][0] = mdaattr[c][1][1] = genius_pal[0];
        if (c & 8)
            mdaattr[c][0][1] = genius_pal[3];
        else
            mdaattr[c][0][1] = genius_pal[2];
    }
    mdaattr[0x70][0][1] = genius_pal[0];
    mdaattr[0x70][0][0] = mdaattr[0x70][1][0] = mdaattr[0x70][1][1] = genius_pal[3];
    mdaattr[0xF0][0][1]                                             = genius_pal[0];
    mdaattr[0xF0][0][0] = mdaattr[0xF0][1][0] = mdaattr[0xF0][1][1] = genius_pal[3];
    mdaattr[0x78][0][1]                                             = genius_pal[2];
    mdaattr[0x78][0][0] = mdaattr[0x78][1][0] = mdaattr[0x78][1][1] = genius_pal[3];
    mdaattr[0xF8][0][1]                                             = genius_pal[2];
    mdaattr[0xF8][0][0] = mdaattr[0xF8][1][0] = mdaattr[0xF8][1][1] = genius_pal[3];
    mdaattr[0x00][0][1] = mdaattr[0x00][1][1] = genius_pal[0];
    mdaattr[0x08][0][1] = mdaattr[0x08][1][1] = genius_pal[0];
    mdaattr[0x80][0][1] = mdaattr[0x80][1][1] = genius_pal[0];
    mdaattr[0x88][0][1] = mdaattr[0x88][1][1] = genius_pal[0];

    /* Start off in 80x25 text mode */
    genius->cga_stat     = 0xF4;
    genius->genius_mode  = 2;
    genius->enabled      = 1;
    genius->genius_charh = 0x90; /* Native character height register */
    genius->genius_control |= 0x10;
    return genius;
}

void
genius_close(void *priv)
{
    genius_t *genius = (genius_t *) priv;

    free(genius->vram);
    free(genius);
}

static int
genius_available(void)
{
    return rom_present(BIOS_ROM_PATH);
}

void
genius_speed_changed(void *priv)
{
    genius_t *genius = (genius_t *) priv;

    genius_recalctimings(genius);
}

const device_t genius_device = {
    .name          = "Genius VHR",
    .internal_name = "genius",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = genius_init,
    .close         = genius_close,
    .reset         = NULL,
    .available     = genius_available,
    .speed_changed = genius_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Yamaha V6355 graphics card.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          John Elliott, <jce@seasip.info>
 *          Miran Grca, <mgrca8@gmail.com>
 *          W. M. Martinez, <anikom15@outlook.com>
 *
 *          Copyright 2008-2025 Sarah Walker.
 *          Copyright 2025 John Elliott.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2023-2025 W. M. Martinez
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
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_v6355.h>
#include <86box/vid_cga.h>
#include <86box/vid_cga_comp.h>
#include <86box/plat_unused.h>

/* Emulation of the Yamaha V6355 chipset. This is a CGA clone that was 
 * probably designed primarily for laptops, where the primary display was
 * a fixed-resolution LCD panel and there was the option of connecting to
 * an external CGA monitor or PAL/SECAM television.
 *
 * Consequently, unlike a real CGA, it doesn't implement the first ten 6845
 * registers; instead, a small number of fixed resolutions can be selected
 * using the V6355's own registers at 0x3DD / 0x3DF. Width is either 512 or 640
 * pixels; height is 64, 192, 200 or 204 pixels.
 *
 * Other features include:
 * - MDA attribute support
 * - Palette support - mapping from RGBI colour to 9-bit rrrgggbbb colours 
 *  (when output is to composite)
 * - Hardware mouse pointer support
 *
 * Outline of the V6355's extra registers, accessed through ports 0x3DD (index)
 * and 0x3DE (data):
 *
 * 0x00-0x1F: Mouse pointer AND mask
 * 0x20-0x3F: Mouse pointer XOR mask
 * 0x40-0x5F: Palette for composite output. 0r,gb,0r,gb,0r,gb etc.
 * 0x60-0x61: Mouse pointer X (big-endian, in 320x200 coordinates)
 * 0x62:      Not used (would be high byte of mouse pointer Y)
 * 0x63:      Mouse pointer Y
 * 0x64:      Mouse pointer visibility & vertical adjustment
 * 0x65:      Screen height & width, display type, RAM type
 * 0x66:      LCD adjust, MDA attribute emulation
 * 0x67:      Horizontal adjustment, other configuration
 * 0x68:      Mouse pointer colour
 * 0x69:      Control data register (not well documented)
 *
 * Currently unimplemented:
 * > Display type (PAL/SECAM @50Hz vs NTSC @60Hz)
 * > MDA monitor support
 * > LCD panel support 
 * > Horizontal / vertical position adjustments
 * > 160x200x16 and 640x200x16 video modes. Documentation suggests that these 
 *   should be selected by setting bit 6 of the CGA control register, but 
 *   that doesn't work on my real hardware, so I can't test and therefore
 *   can't replicate
 * > Palette support on composite output. Composite_Process() does not 
 *   appear to have any support for an arbitrary palette.
 */

#define V6355_RGB                 0
#define V6355_COMPOSITE           1
#define V6355_TRUECOLOR           2

#define COMPOSITE_OLD             0
#define COMPOSITE_NEW             1

#define DOUBLE_NONE               0
#define DOUBLE_SIMPLE             1
#define DOUBLE_INTERPOLATE_SRGB   2
#define DOUBLE_INTERPOLATE_LINEAR 3

#define DEVICE_VRAM      0x4000
#define DEVICE_VRAM_MASK 0x3fff

typedef union {
    uint32_t color;
    struct {
        uint8_t b;
        uint8_t g;
        uint8_t r;
        uint8_t a;
    };
} color_t;

static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static video_timings_t timing_v6355 = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

static uint8_t mdamap[256][2][2];

/* Default values for palette registers */
static uint8_t defpalette[32] = {
    0x00, 0x00,     /* Black */
    0x00, 0x04,     /* Blue */
    0x00, 0x40,     /* Green */
    0x00, 0x44,     /* Cyan */
    0x04, 0x00,     /* Red */
    0x04, 0x04,     /* Magenta */
    0x04, 0x40,     /* Yellow */
    0x04, 0x44,     /* Light grey */
    0x01, 0x11,     /* Dark grey */
    0x00, 0x06,     /* Bright blue */
    0x00, 0x60,     /* Bright green */
    0x00, 0x66,     /* Bright cyan */
    0x06, 0x00,     /* Bright red */
    0x06, 0x06,     /* Bright magenta */
    0x06, 0x60,     /* Bright yellow */
    0x07, 0x77,     /* Bright white */
};

static void v6355_recalctimings(v6355_t *v6355);

static void
v6355_out(uint16_t addr, uint8_t val, void *priv)
{
    v6355_t *v6355 = (v6355_t *) priv;
    uint8_t  old;

    switch (addr) {
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            v6355->crtcreg = val & 31;
            break;
        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            old = v6355->crtc[v6355->crtcreg];
            v6355->crtc[v6355->crtcreg] = val & crtcmask[v6355->crtcreg];
            if (old != val) {
                if (v6355->crtcreg < 0xe || v6355->crtcreg > 0x10)
                    v6355_recalctimings(v6355);
            }
            break;
        case 0x3d8:
            if (((v6355->cgamode ^ val) & 5) != 0) {
                v6355->cgamode = val;
                update_cga16_color(v6355->cgamode);
            }
            v6355->cgamode = val;
            break;
        case 0x3d9:
            v6355->cgacol = val;
            break;
        case 0x3dd:
            v6355->v6355reg = val;
            break;
        case 0x3de:
            v6355->v6355data[v6355->v6355reg] = val;

            /* Writes in the 0x40-0x5F range update the palette */
            if (v6355->v6355reg >= 0x40 && v6355->v6355reg < 0x60) {
                int r = (v6355->v6355data[v6355->v6355reg & 0xFE]) & 7;
                int g = (v6355->v6355data[v6355->v6355reg | 0x01] >> 4) & 7;
                int b = (v6355->v6355data[v6355->v6355reg | 0x01]) & 7;
                v6355->v6355pal[(v6355->v6355reg - 0x40) / 2] = 
                    makecol(r * 0xFF / 7, g * 0xFF / 7, b * 0xFF / 7);
            }

            /* Register autoincrements after a write. */
            v6355->v6355reg = (v6355->v6355reg + 1) % sizeof(v6355->v6355data);
            break;
        case 0x3df:
            /* Supposedly used for memory paging in 16-colour mode, but
             * I've found no documentation to explain how */
            break;
    }
}

static uint8_t
v6355_in(uint16_t addr, void *priv)
{
    v6355_t *v6355 = (v6355_t *) priv;
    uint8_t  ret   = 0xff;

    switch (addr) {
        case 0x3d4:
            ret = v6355->crtcreg;
            break;
        case 0x3d5:
            ret = v6355->crtc[v6355->crtcreg];
            break;
        case 0x3da:
            ret = v6355->cgastat;
            break;
    }

    return ret;
}

static void
v6355_write(uint32_t addr, uint8_t val, void *priv)
{
    v6355_t *v6355 = (v6355_t *) priv;

    v6355->vram[addr & 0x3fff] = val;

    cycles -= 4;
}

static uint8_t
v6355_read(uint32_t addr, void *priv)
{
    v6355_t *v6355 = (v6355_t *) priv;

    cycles -= 4;

    return v6355->vram[addr & 0x3fff];
}

/* Get width of display area (always 512px or 640px) */
static uint32_t
v6355_width(v6355_t *v6355)
{
    return (v6355->v6355data[0x65] & 4) ? 512 : 640;
}

/* Get height of display area (192px, 200px, 204px or 64px) */
static uint32_t
v6355_height(v6355_t *v6355)
{
    static const unsigned heights[4] = { 192, 200, 204, 64 };

    return heights[v6355->v6355data[0x65] & 3];
}

/* Timings on a V6355 are largely fixed */
static void
v6355_recalctimings(v6355_t *v6355) 
{
    double   disptime;
    double   _dispontime, _dispofftime;
#ifndef USE_CGA_TIMINGS
    double   crtcconst = (cpuclock / 21477270.0 * (double) (1ULL << 32)) * 8.0;
#endif

    uint32_t w = v6355_width(v6355);

    disptime = w + 33;
    _dispontime = w;
    _dispofftime = disptime - _dispontime;
#ifdef USE_CGA_TIMINGS
    _dispontime *= CGACONST;
    _dispofftime *= CGACONST;
#else
    _dispontime *= crtcconst;
    _dispofftime *= crtcconst;
#endif
    v6355->dispontime = (uint64_t)_dispontime;
    v6355->dispofftime = (uint64_t)_dispofftime;
}

/* Overlay the pointer on a line of the display. pixel[] is an array of 640
 * IBGR values containing the pixels to draw onto */
static void
v6355_pointer(v6355_t *v6355, uint8_t *pixel)
{
    int c, pxc;
    int y = v6355->displine - v6355->firstline;

    /* The pointer coordinates are on a 336x216 grid, with (16,16) being
     * the top left-hand corner of the visible area */
    int     pointer_x = (v6355->v6355data[0x60] << 8) | (v6355->v6355data[0x61]);
    int     pointer_y = v6355->v6355data[0x63];
    uint8_t mc;
    uint8_t mand;
    uint8_t mxor;
    uint8_t mflags;

    /* Mouse drawing options */
    mflags = v6355->v6355data[0x64];

    /* If the pointer is blinking and not currently shown, don't draw it */
    if ((v6355->cgablink & 8) && (mflags & 1))
        return;

    /* If this line doesn't intersect the pointer, nothing to do */
    if ((y < (pointer_y - 16)) || (y >= pointer_y))
        return;

    y -= (pointer_y - 16);

    /* Get mouse AND and XOR masks */
    mand = v6355->v6355data[0x68] & 0x0F;
    mxor = (v6355->v6355data[0x68] >> 4) & 0x0F;

    /* Draw up to 16 double-width pixels */
    for (c = 0; c < 32; c++) {
        mc = 0x80 >> ((c & 0x0E) >> 1);
        pxc = c + 2 * (pointer_x - 16);

        if (pxc < 0 || pxc >= 640)
            /* X clipping */
            continue;

        if (mflags & 2) {
            /* Apply AND mask? */
            if (v6355->v6355data[y * 2 + c / 16] & mc)
                pixel[pxc] &= mand;
        }

        if (mflags & 4) {
            /* Apply XOR mask? */
            if (v6355->v6355data[y * 2 + c / 16 + 32] & mc)
                pixel[pxc] ^= mxor;
        }
    }	
}

/* Convert attribute byte to CGA colours */
static void
v6355_map_attrs(v6355_t *v6355, uint8_t chr, uint8_t attr, uint8_t *cols)
{
    if (v6355->v6355data[0x66] & 0x40) {	
        /* MDA-style attributes */
        int blink = (v6355->cgamode & 0x20) && (v6355->cgablink & 8) && (attr & 0x80);

        cols[1] = mdamap[attr][blink][1];
        cols[0] = mdamap[attr][blink][0];
    } else {
        /* CGA attributes (blinking enabled) */
        if (v6355->cgamode & 0x20) {
            cols[1] = attr & 15;
            cols[0] = (attr >> 4) & 7;
            if ((v6355->cgablink & 8) && (attr & 0x80) && !v6355->drawcursor)
                cols[1] = cols[0];
        } else {
            /* CGA attributes (blinking disabled) */
            cols[1] = attr & 15;
            cols[0] = attr >> 4;
        }
    }
}

/* Render a line as 640 pixels in 80-column text mode */
static void
v6355_line_text80(v6355_t *v6355, uint8_t *pixel, uint16_t ca)
{
    int32_t  x, c;
    uint8_t  chr, attr;
    uint32_t w = v6355_width(v6355) / 8;
    uint8_t  cols[2];

    for (x = 0; x < w; x++) {
        if (v6355->cgamode & 8) {
            chr  = v6355->charbuffer[x << 1];
            attr = v6355->charbuffer[(x << 1) + 1];
        } else
            chr  = attr = 0;

        v6355_map_attrs(v6355, chr, attr, cols);

        for (c = 0; c < 8; c++) {
            uint8_t data = fontdat[chr + v6355->fontbase][v6355->sc & 7];

            /* Underline attribute if enabled */
            if ((v6355->v6355data[0x66] & 0x80) && ((attr & 7) == 1) && ((v6355->sc & 7) == 7))
                data = 0xff;

            pixel[(x << 3) + c] = cols[(data & (1 << (c ^ 7))) ? 1 : 0];
        }
    }
}

/* Render a line as 640 pixels in 40-column text mode */
static void
v6355_line_text40(v6355_t *v6355, uint8_t *pixel, uint16_t ca)
{
    int32_t  x, c;
    uint8_t  chr, attr;
    uint32_t w = v6355_width(v6355) / 16;
    uint8_t  cols[2];

    for (x = 0; x < w; x++) {
        if (v6355->cgamode & 8) {
            chr  = v6355->vram[((v6355->ma + x) << 1) & 0x3fff];
            attr = v6355->vram[(((v6355->ma + x) << 1) + 1) & 0x3fff];
        } else
            chr = attr = 0;

        v6355_map_attrs(v6355, chr, attr, cols);

        for (c = 0; c < 8; c++) {
            uint8_t data = fontdat[chr + v6355->fontbase][v6355->sc & 7];

            /* Underline attribute if enabled */
            if ((v6355->v6355data[0x66] & 0x80) && ((attr & 7) == 1) && ((v6355->sc & 7) == 7))
                data = 0xff;

            pixel[(x << 4) + (c << 1)] = pixel[(x << 4) + (c << 1) + 1] = 
                cols[(data & (1 << (c ^ 7))) ? 1 : 0];
        }
    }
}

/* Render a line as 640 pixels in 320-pixel graphics mode */
static void
v6355_line_graphics320(v6355_t *v6355, uint8_t *pixel)
{
    int32_t  x;
    int32_t  c;
    uint8_t  cols[4];
    uint8_t  intensity;
    uint16_t dat;
    uint32_t width = v6355_width(v6355) / 16;

    cols[0] = v6355->cgacol & 15;

    intensity = (v6355->cgacol & 16) ? 8 : 0;

    if (v6355->cgamode & 4) {
        cols[1] = intensity | 3;
        cols[2] = intensity | 4;
        cols[3] = intensity | 7;
    } else if (v6355->cgacol & 32) {
        cols[1] = intensity | 3;
        cols[2] = intensity | 5;
        cols[3] = intensity | 7;
    } else {
        cols[1] = intensity | 2;
        cols[2] = intensity | 4;
        cols[3] = intensity | 6;
    }

    for (x = 0; x < width; x++) {
        if (v6355->cgamode & 8)
            dat = (v6355->vram[((v6355->ma << 1) & 0x1fff) + ((v6355->sc & 1) * 0x2000)] << 8) | 
                   v6355->vram[((v6355->ma << 1) & 0x1fff) + ((v6355->sc & 1) * 0x2000) + 1];
        else 
            dat = 0;

        v6355->ma++;

        for (c = 0; c < 8; c++) {
            pixel[(x << 4) + (c << 1)] = pixel[(x << 4) + (c << 1) + 1] = cols[dat >> 14];
            dat <<= 2;
        }	
    }
}

/* Render a line as 640 pixels in 640-pixel graphics mode */
static void
v6355_line_graphics640(v6355_t *v6355, uint8_t *pixel)
{
    int32_t x;
    int32_t c;
    uint8_t cols[2];
    uint16_t dat;
    uint32_t width = v6355_width(v6355) / 16;

    cols[0] = 0;
    cols[1] = v6355->cgacol & 15;

    for (x = 0; x < width; x++) {
        if (v6355->cgamode & 8) 
            dat = (v6355->vram[((v6355->ma << 1) & 0x1fff) + ((v6355->sc & 1) * 0x2000)] << 8) | 
                   v6355->vram[((v6355->ma << 1) & 0x1fff) + ((v6355->sc & 1) * 0x2000) + 1];
        else
            dat = 0;

        v6355->ma++;

        for (c = 0; c < 16; c++) {
            pixel[(x << 4) + c] = cols[dat >> 15];
            dat <<= 1;
        }
    }
}

static void
v6355_render(v6355_t *v6355, int line)
{
    uint16_t ca = (v6355->crtc[15] | (v6355->crtc[14] << 8)) & 0x3fff;
    int      width = v6355_width(v6355);
    int      c;
    int      x;
    int      drawcursor;
    uint32_t cols[4];
    uint8_t  pixel[640];

    /* Draw border */
    cols[0] = ((v6355->cgamode & 0x12) == 0x12) ? 0 : (v6355->cgacol & 15);

    for (c = 0; c < 8; c++) {
        ((uint32_t *) buffer32->line[line])[c] = cols[0];
        ((uint32_t *) buffer32->line[line])[c + width + 8] = cols[0];
    }

    /* Render screen data. */
    if (v6355->cgamode & 1) {
        /* High-res text */
        v6355_line_text80(v6355, pixel, ca);
        v6355_pointer(v6355, pixel);

        for (x = 0; x < (width / 8); x++) {
            drawcursor = ((v6355->ma == ca) && v6355->con && v6355->cursoron);
            if (drawcursor) {
                for (c = 0; c < 8; c++)
                   ((uint32_t *) buffer32->line[line])[(x << 3) + c + 8] = pixel[(x << 3) + c] ^ 0xffffff;
            } else {
                for (c = 0; c < 8; c++)
                   ((uint32_t *)buffer32->line[line])[(x << 3) + c + 8] = pixel[(x << 3) + c];
            }

            v6355->ma++;
        }
    } else if (!(v6355->cgamode & 2)) {
        /* Low-res text */
        v6355_line_text40(v6355, pixel, ca);
        v6355_pointer(v6355, pixel);

        for (x = 0; x < (width / 16); x++) {
            drawcursor = ((v6355->ma == ca) && v6355->con && v6355->cursoron);
            if (drawcursor) {
                for (c = 0; c < 16; c++)
                    ((uint32_t *) buffer32->line[line])[(x << 4) + c + 8] = pixel[(x << 4) + c] ^ 0xffffff;
            } else {
                for (c = 0; c < 16; c++)
                    ((uint32_t *)buffer32->line[line])[(x << 4) + c + 8] = pixel[(x << 4) + c];
            }

            v6355->ma++;
        }
    } else if (!(v6355->cgamode & 16)) {
        /* Low-res graphics 
         * XXX There should be a branch for 160x200x16 graphics somewhere around here */
        v6355_line_graphics320(v6355, pixel);
        v6355_pointer(v6355, pixel);

        for (x = 0; x < (width / 16); x++) {
            for (c = 0; c < 16; c++)
            ((uint32_t *) buffer32->line[line])[(x << 4) + c + 8] = pixel[(x << 4) + c];
        }
    } else {
        /* High-res graphics 
         * XXX There should be a branch for 640x200x16 graphics somewhere around here */
        v6355_line_graphics640(v6355, pixel);
        v6355_pointer(v6355, pixel);

        for (x = 0; x < (width / 16); x++) {
            for (c = 0; c < 16; c++)
            ((uint32_t *) buffer32->line[line])[(x << 4) + c + 8] = pixel[(x << 4) + c];
        }
    }
}

static void
v6355_render_blank(v6355_t *v6355, int line)
{
    int      width = v6355_width(v6355);
    uint32_t cols[4];

    cols[0] = ((v6355->cgamode & 0x12) == 0x12) ? 0 : (v6355->cgacol & 15);
    hline(buffer32, 0, line, width + 16, cols[0]);
}

static void
v6355_render_process(v6355_t *v6355, int line)
{
    int      c;
    uint8_t  border;
    int      width = v6355_width(v6355);
    int      x     = width + 16;

    /* Now render the 640 pixels to the display buffer */
    switch (v6355->display_type) {
        /* XXX V6355_COMPOSITE can't use the V6355's palette registers */
        case V6355_COMPOSITE:
            for (c = 0; c < x; c++)
                buffer32->line[line][c] = ((uint32_t *) buffer32->line[line])[c] & 0xf;

            border = ((v6355->cgamode & 0x12) == 0x12) ? 0 : (v6355->cgacol & 15);

            Composite_Process(v6355->cgamode, border, (width + 16) >> 2, buffer32->line[line]);
            break;
        case V6355_TRUECOLOR:
            /* V6355_TRUECOLOR is a fictitious display that behaves like RGB except it
             * takes account of the V6355's palette registers */
            for (c = 0; c < x; c++)
                ((uint32_t *) buffer32->line[line])[c] = v6355->v6355pal[((uint32_t *) buffer32->line[line])[c] & 0xf];
            break;
        default:
            video_process_8(width + 16, line);
            break;
    }
}

static void
v6355_poll(void *priv)
{
    v6355_t *v6355 = (v6355_t *) priv;
    int      x;
    int      oldvc;
    int      oldsc;
    int      xs_temp;
    int      ys_temp;
    int      old_ma;

    int      width = v6355_width(v6355);
    int      height = v6355_height(v6355);

    /* Simulated CRTC height registers */
    int crtc4, crtc5, crtc6, crtc7, crtc8, crtc9;

    if (!(v6355->cgamode & 2)) {
        /* Text mode values */
        crtc4 = (height + 54) / 8;
        crtc5 = 6;
        crtc6 = height / 8;
        crtc7 = (height + 24) / 8;
        crtc8 = 2;
        crtc9 = 7;
    } else {
        /* Graphics mode values */
        crtc4 = (height + 54) / 2;
        crtc5 = 6;
        crtc6 = height / 2;
        crtc7 = (height + 24) / 2;
        crtc8 = 2;
        crtc9 = 1;
    }

    if (!v6355->linepos) {
        timer_advance_u64(&v6355->timer, v6355->dispofftime);

        v6355->cgastat |= 1;
        v6355->linepos = 1;

        oldsc = v6355->sc;

        if ((crtc8 & 3) == 3)
            v6355->sc = ((v6355->sc << 1) + v6355->oddeven) & 7;

        if (v6355->cgadispon) {
            if (v6355->displine < v6355->firstline) {
                v6355->firstline = v6355->displine;
                video_wait_for_buffer();
            }

            v6355->lastline = v6355->displine;

            switch (v6355->double_type) {
                default:
                    v6355_render(v6355, v6355->displine << 1);
                    v6355_render_blank(v6355, (v6355->displine << 1) + 1);
                    break;
                case DOUBLE_NONE:
                    v6355_render(v6355, v6355->displine);
                    break;
                case DOUBLE_SIMPLE:
                    old_ma = v6355->ma;
                    v6355_render(v6355, v6355->displine << 1);
                    v6355->ma = old_ma;
                    v6355_render(v6355, (v6355->displine << 1) + 1);
                    break;
            }
       } else  switch (v6355->double_type) {
            default:
                v6355_render_blank(v6355, v6355->displine << 1);
                break;
            case DOUBLE_NONE:
                v6355_render_blank(v6355, v6355->displine);
                break;
            case DOUBLE_SIMPLE:
                v6355_render_blank(v6355, v6355->displine << 1);
                v6355_render_blank(v6355, (v6355->displine << 1) + 1);
                break;
        }

        switch (v6355->double_type) {
            default:
                v6355_render_process(v6355, v6355->displine << 1);
                v6355_render_process(v6355, (v6355->displine << 1) + 1);
                break;
            case DOUBLE_NONE:
                v6355_render_process(v6355, v6355->displine);
                break;
        }

        v6355->sc = oldsc;

        if (v6355->vc == crtc7 && !v6355->sc)
            v6355->cgastat |= 8;

        v6355->displine++;

        if (v6355->displine >= 360)
            v6355->displine = 0;
    } else {
        timer_advance_u64(&v6355->timer, v6355->dispontime);

        v6355->linepos = 0;

        if (v6355->vsynctime) {
            v6355->vsynctime--;

            if (!v6355->vsynctime)
                v6355->cgastat &= ~8;
        }

        if (v6355->sc == (v6355->crtc[11] & 31) || ((crtc8 & 3) == 3 && v6355->sc == ((v6355->crtc[11] & 31) >> 1))) {
            v6355->con = 0;
            v6355->coff = 1;
        }

        if ((crtc8 & 3) == 3 && v6355->sc == (crtc9 >> 1))
            v6355->maback = v6355->ma;

        if (v6355->vadj) {
            v6355->sc++;
            v6355->sc &= 31;
            v6355->ma = v6355->maback;
            v6355->vadj--;

            if (!v6355->vadj) {
                v6355->cgadispon = 1;
                v6355->ma = v6355->maback = (v6355->crtc[13] | (v6355->crtc[12] << 8)) & 0x3fff;
                v6355->sc = 0;
            }
        } else if (v6355->sc == crtc9) {
            v6355->maback = v6355->ma;
            v6355->sc = 0;
            oldvc = v6355->vc;
            v6355->vc++;
            v6355->vc &= 127;

            if (v6355->vc == crtc6)
                v6355->cgadispon = 0;

            if (oldvc == crtc4) {
                v6355->vc = 0;
                v6355->vadj = crtc5;

                if (!v6355->vadj)
                    v6355->cgadispon = 1;

                if (!v6355->vadj)
                    v6355->ma = v6355->maback = (v6355->crtc[13] | (v6355->crtc[12] << 8)) & 0x3fff;

                if ((v6355->crtc[10] & 0x60) == 0x20)
                    v6355->cursoron = 0;
                else
                    v6355->cursoron = v6355->cgablink & 8;
            }

            if (v6355->vc == crtc7) {
                v6355->cgadispon = 0;
                v6355->displine = 0;
                v6355->vsynctime = 16;
                if (crtc7) {
                    x = width + 16;
                    v6355->lastline++;

                    xs_temp = x;
                    ys_temp = (v6355->lastline - v6355->firstline) << 1;

                    if ((xs_temp > 0) && (ys_temp > 0)) {
                        if (xs_temp < 64)
                            xs_temp = 656;
                        if (ys_temp < 32)
                            ys_temp = 200;
                        if (!enable_overscan)
                            xs_temp -= 16;

                        if (((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get())) {
                            xsize = xs_temp;
                            ysize = ys_temp;
                            set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));

                            if (video_force_resize_get())
                                video_force_resize_set(0);
                        }

                        cga_do_blit(xsize, v6355->firstline, v6355->lastline, v6355->double_type);
                    }

                    frames++;

                    video_res_x = xsize - 16;
                    video_res_y = ysize;
                    if (v6355->cgamode & 1) {
                        video_res_x /= 8;
                        video_res_y /= crtc9 + 1;
                        video_bpp = 0;
                    } else if (!(v6355->cgamode & 2)) {
                        video_res_x /= 16;
                        video_res_y /= crtc9 + 1;
                        video_bpp = 0;
                    } else if (!(v6355->cgamode & 16)) {
                        video_res_x /= 2;
                        video_bpp = 2;
                    } else
                        video_bpp = 1;
                }

                v6355->firstline = 1000;
                v6355->lastline = 0;
                v6355->cgablink++;
                v6355->oddeven ^= 1;
            }
        } else {
            v6355->sc++;
            v6355->sc &= 31;
            v6355->ma = v6355->maback;
        }

        if (v6355->cgadispon)
            v6355->cgastat &= ~1;

        if ((v6355->sc == (v6355->crtc[10] & 31) || ((crtc8 & 3) == 3 && v6355->sc == ((v6355->crtc[10] & 31) >> 1))))
            v6355->con = 1;

        if (v6355->cgadispon && (v6355->cgamode & 1)) {
            for (x = 0; x < ((width / 8) * 2); x++)
                 v6355->charbuffer[x] = v6355->vram[(((v6355->ma << 1) + x) & 0x3fff)];
        }
    }
}

static void *
v6355_standalone_init(const device_t *info) {
    int      n;
    int      c;
    v6355_t *v6355 = calloc(1, sizeof(v6355_t));

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_v6355);

    v6355->display_type = device_get_config_int("display_type");

    overscan_x = overscan_y = 16;

    /* Initialise the palette registers to default values */
    memcpy(v6355->v6355data + 0x40, defpalette, 0x20);

    for (n = 0; n < 16; n++) {
        int r = (v6355->v6355data[0x40 + 2 * n]) & 7;
        int g = (v6355->v6355data[0x41 + 2 * n] >> 4) & 7;
        int b = (v6355->v6355data[0x41 + 2 * n]) & 7;

        v6355->v6355pal[n] =  makecol((r * 0xff) / 7, (g * 0xff) / 7, (b * 0xff) / 7);
    }

    /* Default to 200 lines */
    v6355->v6355data[0x65] = 0x01;

    /* Set up CGA -> MDA attribute mapping */
    for (c = 0; c < 256; c++) {
        mdamap[c][0][0] = mdamap[c][1][0] = mdamap[c][1][1] = 0;

        if (c & 8)
            mdamap[c][0][1] = 0xf;
        else
            mdamap[c][0][1] = 0x7;
    }

    mdamap[0x70][0][1] = 0;
    mdamap[0x70][0][0] = mdamap[0x70][1][0] = mdamap[0x70][1][1] = 0xf;
    mdamap[0xF0][0][1] = 0;
    mdamap[0xF0][0][0] = mdamap[0xF0][1][0] = mdamap[0xF0][1][1] = 0xf;
    mdamap[0x78][0][1] = 7;
    mdamap[0x78][0][0] = mdamap[0x78][1][0] = mdamap[0x78][1][1] = 0xf;
    mdamap[0xF8][0][1] = 7;
    mdamap[0xF8][0][0] = mdamap[0xF8][1][0] = mdamap[0xF8][1][1] = 0xf;
    mdamap[0x00][0][1] = mdamap[0x00][1][1] = 0;
    mdamap[0x08][0][1] = mdamap[0x08][1][1] = 0;
    mdamap[0x80][0][1] = mdamap[0x80][1][1] = 0;
    mdamap[0x88][0][1] = mdamap[0x88][1][1] = 0;

    v6355->display_type = device_get_config_int("display_type");
    v6355->revision = device_get_config_int("composite_type");

    v6355->vram = malloc(0x4000);

    cga_comp_init(v6355->revision);

    timer_add(&v6355->timer, v6355_poll, v6355, 1);

    mem_mapping_add(&v6355->mapping, 0xb8000, 0x08000,
                    v6355_read, NULL, NULL, v6355_write, NULL, NULL, NULL,
                    MEM_MAPPING_EXTERNAL, v6355);

    io_sethandler(0x03d0, 0x0010,
                  v6355_in, NULL, NULL, v6355_out, NULL, NULL,
                  v6355);

    v6355->rgb_type = device_get_config_int("rgb_type");
    cga_palette     = (v6355->rgb_type << 1);
    cgapal_rebuild();
    update_cga16_color(v6355->cgamode);

    v6355->double_type = device_get_config_int("double_type");
    cga_interpolate_init();

    switch(device_get_config_int("font")) {
        case 0:
            loadfont(FONT_IBM_MDA_437_PATH, 0);
            break;
        case 1:
            loadfont(FONT_IBM_MDA_437_NORDIC_PATH, 0);
            break;
        case 4:
            loadfont(FONT_TULIP_DGA_PATH, 0);
            break;
    }

    monitors[monitor_index_global].mon_composite = (v6355->display_type == V6355_COMPOSITE);

    return v6355;
}

static void
v6355_close(void *priv) {
    v6355_t *v6355 = (v6355_t *) priv;

    free(v6355->vram);
    free(v6355);
}

static void
v6355_speed_changed(void *priv) {
    v6355_t *v6355 = (v6355_t *) priv;

    v6355_recalctimings(v6355);
}

// clang-format off
const device_config_t v6355_config[] = {
    {
        .name           = "display_type",
        .description    = "Display type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = V6355_RGB,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "RGB",        .value = V6355_RGB       },
            { .description = "Composite",  .value = V6355_COMPOSITE },
            { .description = "True color", .value = V6355_TRUECOLOR },
            { .description = ""                                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "composite_type",
        .description    = "Composite type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = COMPOSITE_OLD,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Old", .value = COMPOSITE_OLD },
            { .description = "New", .value = COMPOSITE_NEW },
            { .description = ""                            }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "rgb_type",
        .description    = "RGB type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 5,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Color (generic)",  .value = 0 },
            { .description = "Green Monochrome", .value = 1 },
            { .description = "Amber Monochrome", .value = 2 },
            { .description = "Gray Monochrome",  .value = 3 },
            { .description = "Color (no brown)", .value = 4 },
            { .description = "Color (IBM 5153)", .value = 5 },
            { .description = ""                             }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "double_type",
        .description    = "Line doubling type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = DOUBLE_NONE,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "None",                 .value = DOUBLE_NONE               },
            { .description = "Simple doubling",      .value = DOUBLE_SIMPLE             },
            { .description = "sRGB interpolation",   .value = DOUBLE_INTERPOLATE_SRGB   },
            { .description = "Linear interpolation", .value = DOUBLE_INTERPOLATE_LINEAR },
            { .description = ""                                                         }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "font",
        .description    = "Font",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "US (CP 437)",                 .value = 0 },
            { .description = "IBM Nordic (CP 437-Nordic)",  .value = 1 },
            { .description = "Tulip DGA",                   .value = 4 },
            { .description = ""                                        }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t v6355d_device = {
    .name          = "Yamaha V6355D",
    .internal_name = "v6355d",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = v6355_standalone_init,
    .close         = v6355_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = v6355_speed_changed,
    .force_redraw  = NULL,
    .config        = v6355_config
};

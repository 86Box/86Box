/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Renderer of the Roland MT-32 LCD: 20 characters of 5x7 dots
 *          on a backlit green panel.
 *
 * Authors: Andreas Zdziarstek, <https://github.com/gmcn42>
 *
 *          Copyright 2026 Andreas Zdziarstek.
 */
#include <stdint.h>
#include <string.h>
#include <86box/midi_lcd.h>
#include "midi_lcd_mt32_font.h"

#define LCD_CHARS        20
#define LCD_DOT_COLS     5
#define LCD_DOT_ROWS     7 /* row 7 of the font is the cursor row, not drawn */
#define LCD_DOT_SIZE     7
#define LCD_DOT_PITCH    8
#define LCD_CELL_PITCH   (LCD_DOT_COLS * LCD_DOT_PITCH + LCD_DOT_PITCH)
#define LCD_TEXT_W       (LCD_CHARS * LCD_CELL_PITCH - LCD_DOT_PITCH)
#define LCD_TEXT_H       (LCD_DOT_ROWS * LCD_DOT_PITCH - 1)
#define LCD_TEXT_X       ((MIDI_LCD_MT32_WIDTH - LCD_TEXT_W) / 2)
#define LCD_TEXT_Y       ((MIDI_LCD_MT32_HEIGHT - LCD_TEXT_H) / 2)

#define LCD_BLOCK_GLYPH  96

#define LCD_BEZEL        2 /* dark outer frame */
#define LCD_SHADOW       2 /* darker glass just inside the frame */

#define LCD_LIT          0xcafb10
#define LCD_UNLIT        0xf2feed
#define LCD_UNLIT_ALPHA  0x32
#define LCD_BEZEL_COLOUR 0x1c241b

/* Glass colour without texture; the glow and the noise add to the green. */
#define LCD_GLASS_R 11
#define LCD_GLASS_G 54
#define LCD_GLASS_B 4

static uint32_t
lcd_hash(uint32_t x, uint32_t y)
{
    uint32_t h = x * 0x8da6b343 + y * 0xd8163841 + 0x9e3779b9;

    h ^= h >> 15;
    h *= 0x2c1b3c6d;
    h ^= h >> 12;
    h *= 0x297a2d39;
    h ^= h >> 15;

    return h;
}

/* Smooth value noise in 0-255: hashed values on a grid of cell x cell
   pixels, interpolated bilinearly. */
static int
lcd_noise(int x, int y, int cell, uint32_t seed)
{
    int gx  = x / cell;
    int gy  = y / cell;
    int fx  = ((x % cell) * 256) / cell;
    int fy  = ((y % cell) * 256) / cell;
    int v00 = lcd_hash(gx + seed, gy) & 0xff;
    int v10 = lcd_hash(gx + 1 + seed, gy) & 0xff;
    int v01 = lcd_hash(gx + seed, gy + 1) & 0xff;
    int v11 = lcd_hash(gx + 1 + seed, gy + 1) & 0xff;
    int top = v00 * 256 + (v10 - v00) * fx;
    int bot = v01 * 256 + (v11 - v01) * fx;

    return (top * 256 + (bot - top) * fy) >> 16;
}

/* Extra green of the backlight glow, 0-14: strongest a bit above the middle,
   fading towards the top and bottom edges. */
static int
lcd_glow(int y)
{
    int d = y - (MIDI_LCD_MT32_HEIGHT * 2 / 5);
    int r = MIDI_LCD_MT32_HEIGHT * 3 / 5;

    if (d < 0)
        d = -d;
    if (d >= r)
        return 0;

    return 14 - (14 * d * d) / (r * r);
}

static uint32_t
lcd_glass(int x, int y, int on)
{
    int edge = LCD_BEZEL + LCD_SHADOW;
    int n;
    int r;
    int g;
    int b;

    /* Two octaves of noise give the glass a faint, uneven mottling. */
    n = (lcd_noise(x, y, 40, 0) - 128) * 8 + (lcd_noise(x, y, 9, 977) - 128) * 4;

    r = LCD_GLASS_R + n / 1024;
    g = LCD_GLASS_G + lcd_glow(y) + n / 128;
    b = LCD_GLASS_B;

    if ((x < edge) || (y < edge) || (x >= MIDI_LCD_MT32_WIDTH - edge) || (y >= MIDI_LCD_MT32_HEIGHT - edge)) {
        r = r * 2 / 5;
        g = g * 3 / 5;
        b = b * 2 / 5;
    }

    if (!on) {
        r /= 2;
        g /= 2;
        b /= 2;
    }

    return 0xff000000 | (r << 16) | (g << 8) | b;
}

static uint32_t
lcd_blend(uint32_t dst, uint32_t src, int alpha)
{
    uint32_t out = 0xff000000;

    for (int shift = 0; shift < 24; shift += 8) {
        int d = (dst >> shift) & 0xff;
        int s = (src >> shift) & 0xff;

        out |= (uint32_t) (d + ((s - d) * alpha + 127) / 255) << shift;
    }

    return out;
}

static const uint8_t *
lcd_glyph(unsigned char c)
{
    if (c == 0x01)
        return midi_lcd_mt32_font[LCD_BLOCK_GLYPH];
    if (c == 0x02)
        c = '|';
    if ((c < 0x20) || (c > 0x7f))
        c = ' ';

    return midi_lcd_mt32_font[c - 0x20];
}

static void
lcd_draw_dot(uint32_t *buf, int stride, int x0, int y0, int lit)
{
    for (int y = y0; y < y0 + LCD_DOT_SIZE; y++) {
        uint32_t *p = &buf[y * stride + x0];

        for (int x = 0; x < LCD_DOT_SIZE; x++) {
            if (lit)
                p[x] = 0xff000000 | LCD_LIT;
            else
                p[x] = lcd_blend(p[x], LCD_UNLIT, LCD_UNLIT_ALPHA);
        }
    }
}

/* The empty panel takes most of the time: render it once and copy it
   before drawing the text with midi_lcd_mt32_render_text(). */
void
midi_lcd_mt32_render_glass(uint32_t *buf, int stride, int on)
{
    for (int y = 0; y < MIDI_LCD_MT32_HEIGHT; y++) {
        uint32_t *p = &buf[y * stride];

        for (int x = 0; x < MIDI_LCD_MT32_WIDTH; x++) {
            if ((x < LCD_BEZEL) || (y < LCD_BEZEL) || (x >= MIDI_LCD_MT32_WIDTH - LCD_BEZEL) || (y >= MIDI_LCD_MT32_HEIGHT - LCD_BEZEL))
                p[x] = 0xff000000 | LCD_BEZEL_COLOUR;
            else
                p[x] = lcd_glass(x, y, on);
        }
    }
}

/* Draws the dots of the text on a lit panel from midi_lcd_mt32_render_glass(). */
void
midi_lcd_mt32_render_text(uint32_t *buf, int stride, const char *text)
{
    int ended = (text == NULL);

    for (int i = 0; i < LCD_CHARS; i++) {
        const uint8_t *glyph;
        unsigned char  c = ' ';

        if (!ended) {
            c = (unsigned char) text[i];
            if (c == 0x00) {
                ended = 1;
                c     = ' ';
            }
        }
        glyph = lcd_glyph(c);

        for (int row = 0; row < LCD_DOT_ROWS; row++) {
            for (int col = 0; col < LCD_DOT_COLS; col++) {
                int lit = (glyph[row] >> (LCD_DOT_COLS - 1 - col)) & 1;

                lcd_draw_dot(buf, stride,
                             LCD_TEXT_X + i * LCD_CELL_PITCH + col * LCD_DOT_PITCH,
                             LCD_TEXT_Y + row * LCD_DOT_PITCH, lit);
            }
        }
    }
}

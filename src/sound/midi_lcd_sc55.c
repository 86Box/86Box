/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Renderer of the Roland SC-55 LCD from the state of its LCD
 *          controller: the text fields and the level bars over the
 *          printed glass.
 *
 *          Layout from Nuked-SC55 by nukeykt (GPLv2 or later), colours
 *          at the default contrast of Nuked-SC55-GUI-Float by Linosh K M.
 *          cf. https://github.com/linoshkmalayil/Nuked-SC55-GUI-Float/blob/617c6c5/src/backend/lcd.cpp
 *
 * Authors: nukeykt,
 *          Linosh K M,
 *          Andreas Zdziarstek, <https://github.com/gmcn42>
 *
 *          Copyright 2021-2024 nukeykt.
 *          Copyright 2026 Andreas Zdziarstek.
 */
#include <stdint.h>
#include <string.h>
#include <86box/midi_lcd.h>
#include "midi_lcd_sc55_font.h"
#include "midi_lcd_sc55_print.h"

#define W MIDI_LCD_SC55_WIDTH
#define H MIDI_LCD_SC55_HEIGHT

/* Scale each colour channel by f/256. */
static uint32_t
lcd_mix(uint32_t color, uint32_t f)
{
    const uint32_t r = (((color >> 16) & 0xff) * f) >> 8;
    const uint32_t g = (((color >> 8) & 0xff) * f) >> 8;
    const uint32_t b = ((color & 0xff) * f) >> 8;

    return 0xff000000 | (r << 16) | (g << 8) | b;
}

/* Glyph rows of a character: codes below 16 are the 8 CG RAM characters. */
static const uint8_t *
lcd_glyph(const midi_lcd_t *lcd, uint8_t ch)
{
    if (ch >= 16)
        return sc55_lcd_font[ch - 16];
    return &lcd->cgram[(ch & 7) * 8];
}

static void
lcd_fill(uint32_t *buf, int stride, int x, int y, int w, int h, uint32_t color)
{
    for (int yy = y; yy < (y + h); yy++)
        for (int xx = x; xx < (x + w); xx++)
            buf[yy * stride + xx] = color;
}

/* Print text in the typeface of the glass, from the top left corner. */
static void
lcd_print(uint32_t *buf, int stride, int x, int y, const char *text)
{
    for (; *text; text++) {
        const sc55_print_glyph_t *g = NULL;

        for (unsigned i = 0; i < (sizeof(sc55_print_glyphs) / sizeof(sc55_print_glyphs[0])); i++)
            if (sc55_print_glyphs[i].ch == *text)
                g = &sc55_print_glyphs[i];
        if (g == NULL) {
            x += SC55_PRINT_SPACE;
            continue;
        }

        const int w = (int) strlen(g->rows[0]);
        for (int row = 0; row < SC55_PRINT_HEIGHT; row++)
            for (int col = 0; col < w; col++)
                if (g->rows[row][col] == '#')
                    buf[(y + row) * stride + x + col] = SC55_INK;
        x += w;
    }
}

/* A round dot of the level scale: a square without its corner pixels. */
static void
lcd_scale_dot(uint32_t *buf, int stride, int y, int size)
{
    const int x = 287 - (size / 2);

    for (int row = 0; row < size; row++)
        for (int col = 0; col < size; col++)
            if (((row != 0) && (row != (size - 1))) || ((col != 0) && (col != (size - 1))))
                buf[(y + row) * stride + x + col] = SC55_INK;
}

/* A character of 5x7 dots, 5x5 pixels each at a pitch of 6. */
static void
lcd_char(uint32_t *buf, int stride, const midi_lcd_t *lcd, int x, int y, uint8_t ch,
         uint32_t lit, uint32_t unlit)
{
    const uint8_t *f = lcd_glyph(lcd, ch);

    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
            lcd_fill(buf, stride, x + col * 6, y + row * 6, 5, 5,
                     (f[row] & (0x10 >> col)) ? lit : unlit);
}

/* A column of the level display: a character shown as 8 bars of up to 5
   segments, 24x9 pixels each. */
static void
lcd_level(uint32_t *buf, int stride, const midi_lcd_t *lcd, int x, int y, uint8_t ch, int width,
          uint32_t lit, uint32_t unlit)
{
    const uint8_t *f = lcd_glyph(lcd, ch);

    for (int row = 0; row < 8; row++)
        for (int col = 0; col < width; col++)
            lcd_fill(buf, stride, x + col * 26, y + row * 11, 24, 9,
                     (f[row] & (0x10 >> col)) ? lit : unlit);
}

/* The "L" and "R" marks of the pan display, both switched by bit 0 of
   the first row of one character. */
static const uint8_t lcd_lr[2][12][11] = {
    // clang-format off
    {
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    },
    {
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 },
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0 },
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1 },
    },
    // clang-format on
};

static const int lcd_lr_xy[2][2] = {
    { 264, 70  },
    { 264, 232 }
};

static void
lcd_lr_marks(uint32_t *buf, int stride, const midi_lcd_t *lcd, uint8_t ch, uint32_t lit, uint32_t unlit)
{
    const uint32_t color = (lcd_glyph(lcd, ch)[0] & 1) ? lit : unlit;

    for (int m = 0; m < 2; m++)
        for (int row = 0; row < 12; row++)
            for (int col = 0; col < 11; col++)
                if (lcd_lr[m][row][col])
                    buf[(lcd_lr_xy[m][1] + row) * stride + lcd_lr_xy[m][0] + col] = color;
}

/* Text fields: DD RAM address, length, position. */
static const struct {
    uint8_t  addr;
    uint8_t  len;
    uint16_t x;
    uint16_t y;
} lcd_fields[] = {
    { 0,  3,  34,  11  }, /* part */
    { 3,  16, 153, 11  }, /* instrument */
    { 40, 3,  34,  75  }, /* level */
    { 43, 3,  153, 75  }, /* pan */
    { 49, 3,  34,  139 }, /* reverb */
    { 46, 3,  153, 139 }, /* chorus */
    { 52, 3,  34,  203 }, /* key shift */
    { 55, 3,  153, 203 }, /* MIDI channel */
};

void
midi_lcd_sc55_render(uint32_t *buf, int stride, const midi_lcd_t *lcd)
{
    /* Glass and printed layer. Switched off, the LCD shows the glass dimmed. */
    lcd_fill(buf, stride, 0, 0, W, H, lcd->on ? SC55_GLASS : lcd_mix(SC55_GLASS, 64));
    for (unsigned i = 0; i < (sizeof(sc55_print_labels) / sizeof(sc55_print_labels[0])); i++)
        lcd_print(buf, stride, sc55_print_labels[i].x, sc55_print_labels[i].y, sc55_print_labels[i].text);
    for (unsigned i = 0; i < (sizeof(sc55_print_scale) / sizeof(sc55_print_scale[0])); i++)
        lcd_scale_dot(buf, stride, sc55_print_scale[i].y, sc55_print_scale[i].size);
    if (!lcd->on)
        return;

    /* Dot colours at Nuked-SC55-GUI-Float's default contrast (8). */
    const uint32_t unlit = lcd_mix(SC55_GLASS, 238);
    const uint32_t lit   = lcd_mix(unlit, 136);

    for (unsigned i = 0; i < (sizeof(lcd_fields) / sizeof(lcd_fields[0])); i++)
        for (int c = 0; c < lcd_fields[i].len; c++)
            lcd_char(buf, stride, lcd, lcd_fields[i].x + c * 35, lcd_fields[i].y,
                     lcd->ddram[lcd_fields[i].addr + c], lit, unlit);

    lcd_lr_marks(buf, stride, lcd, lcd->ddram[58], lit, unlit);

    /* Level display: 16 parts as 2 rows of 4 characters, the last one of
       each row with a single column of segments. */
    for (int row = 0; row < 2; row++)
        for (int c = 0; c < 4; c++)
            lcd_level(buf, stride, lcd, 293 + c * 130, 71 + row * 88, lcd->ddram[20 + c + row * 40],
                      (c == 3) ? 1 : 5, lit, unlit);
}

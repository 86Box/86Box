/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the LCD of the current MIDI output device.
 *
 * Authors: Andreas Zdziarstek, <https://github.com/gmcn42>
 *
 *          Copyright 2026 Andreas Zdziarstek.
 */
#ifndef EMU_MIDI_LCD_H
#define EMU_MIDI_LCD_H

#include <stdint.h>

#define MIDI_LCD_NONE 0
#define MIDI_LCD_MT32 1
#define MIDI_LCD_SC55 2

/* Size of the MT-32 LCD image. */
#define MIDI_LCD_MT32_WIDTH  1016
#define MIDI_LCD_MT32_HEIGHT 152

/* Size of the SC-55 LCD image. */
#define MIDI_LCD_SC55_WIDTH  741
#define MIDI_LCD_SC55_HEIGHT 268

typedef struct midi_lcd_t {
    int      type;      /* MIDI_LCD_*, NONE when the device has no LCD */
    uint32_t serial;    /* changes whenever the contents change */
    char     text[21];  /* MT-32: the display as reported by munt */
    int      led;       /* MT-32: MIDI MESSAGE LED */
    int      on;        /* SC-55: the MCU has the display switched on */
    uint8_t  ddram[80]; /* SC-55: LCD controller display data RAM */
    uint8_t  cgram[64]; /* SC-55: LCD controller character generator RAM */
} midi_lcd_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Called by the MIDI output devices, from any thread. */
extern void midi_lcd_set_type(int type);
extern void midi_lcd_set_mt32(const char *text, int led);
extern void midi_lcd_set_sc55(int on, const uint8_t *ddram, const uint8_t *cgram);

/* Copies the current state, for the UI. */
extern void midi_lcd_get(midi_lcd_t *lcd);

/* Draw the MT-32 LCD into buf (0xffRRGGBB pixels, stride in pixels). The
   empty panel is slow to draw: render it once (on = 0 draws the LCD switched
   off), then copy it and draw the text on the copy. text holds up to 20
   characters as munt reports them. */
extern void midi_lcd_mt32_render_glass(uint32_t *buf, int stride, int on);
extern void midi_lcd_mt32_render_text(uint32_t *buf, int stride, const char *text);

/* Draw the SC-55 LCD from the controller state in lcd into buf (0xffRRGGBB
   pixels, stride in pixels). */
extern void midi_lcd_sc55_render(uint32_t *buf, int stride, const midi_lcd_t *lcd);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MIDI_LCD_H */

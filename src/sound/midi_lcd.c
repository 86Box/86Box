/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          State of the LCD of the current MIDI output device.
 *
 *          The device updates it from its render thread, the UI copies it
 *          out to draw the LCD, so neither waits on the other for long.
 *
 * Authors: Andreas Zdziarstek, <https://github.com/gmcn42>
 *
 *          Copyright 2026 Andreas Zdziarstek.
 */
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <86box/midi_lcd.h>

static atomic_flag lcd_lock = ATOMIC_FLAG_INIT;
static midi_lcd_t  lcd_state;

static void
lcd_acquire(void)
{
    while (atomic_flag_test_and_set_explicit(&lcd_lock, memory_order_acquire))
        ;
}

static void
lcd_release(void)
{
    atomic_flag_clear_explicit(&lcd_lock, memory_order_release);
}

void
midi_lcd_set_type(int type)
{
    lcd_acquire();
    lcd_state.type = type;
    lcd_state.serial++;
    memset(lcd_state.text, 0, sizeof(lcd_state.text));
    lcd_state.led = 0;
    lcd_release();
}

void
midi_lcd_set_mt32(const char *text, int led)
{
    lcd_acquire();
    if (text)
        strncpy(lcd_state.text, text, sizeof(lcd_state.text) - 1);
    lcd_state.led = led;
    lcd_state.serial++;
    lcd_release();
}

void
midi_lcd_get(midi_lcd_t *lcd)
{
    lcd_acquire();
    *lcd = lcd_state;
    lcd_release();
}

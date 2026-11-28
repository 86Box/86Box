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
    lcd_state.on  = 0;
    memset(lcd_state.ddram, ' ', sizeof(lcd_state.ddram));
    memset(lcd_state.cgram, 0, sizeof(lcd_state.cgram));
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

/* Called after every render block: count only real changes, so the UI
   redraws only then. */
void
midi_lcd_set_sc55(int on, const uint8_t *ddram, const uint8_t *cgram)
{
    lcd_acquire();
    if ((lcd_state.on != on) || memcmp(lcd_state.ddram, ddram, sizeof(lcd_state.ddram))
        || memcmp(lcd_state.cgram, cgram, sizeof(lcd_state.cgram))) {
        lcd_state.on = on;
        memcpy(lcd_state.ddram, ddram, sizeof(lcd_state.ddram));
        memcpy(lcd_state.cgram, cgram, sizeof(lcd_state.cgram));
        lcd_state.serial++;
    }
    lcd_release();
}

void
midi_lcd_get(midi_lcd_t *lcd)
{
    lcd_acquire();
    *lcd = lcd_state;
    lcd_release();
}

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          LCD extension of the Nuked-SC55-CLAP plugin: the state of the
 *          emulated LCD controller. Must match the plugin's
 *          include/nuked-sc55-clap/lcd.h.
 *
 * Authors: Andreas Zdziarstek, <https://github.com/gmcn42>
 *
 *          Copyright 2026 Andreas Zdziarstek.
 */
#ifndef EMU_NUKED_SC55_LCD_H
#define EMU_NUKED_SC55_LCD_H

#include <stdbool.h>
#include <stdint.h>
#include "clap_defs.h"

#define NUKED_SC55_EXT_LCD "com.johnnovak.nuked-sc55.lcd/1"

typedef struct nuked_sc55_lcd_state {
    bool    enabled;   /* false while the MCU keeps the display off */
    uint8_t ddram[80]; /* character codes, row 0 at 0-39, row 1 at 40-79 */
    uint8_t cgram[64]; /* 8 user characters of 8 rows, bit 4 = left dot */
} nuked_sc55_lcd_state_t;

typedef struct nuked_sc55_plugin_lcd {
    /* Thread-safe. Returns false without an emulator instance. */
    bool (*get_state)(const clap_plugin_t *plugin, nuked_sc55_lcd_state_t *state);
} nuked_sc55_plugin_lcd_t;

#endif /* EMU_NUKED_SC55_LCD_H */

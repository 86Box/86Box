/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Board LEDs, shown in the status bar.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
 */
#ifndef EMU_BOARD_LED_H
#define EMU_BOARD_LED_H

#define BOARD_LED_MAX 8

#ifdef __cplusplus
extern "C" {
#endif

/* How many LEDs the machine has: 0 unless its init added board_led_device. */
extern int  board_led_count(void);
extern int  board_led_get(int led);
extern void board_led_set(int led, int on);

#ifdef EMU_DEVICE_H
/* The parameter is the number of LEDs, up to BOARD_LED_MAX. */
extern const device_t board_led_device;
#endif

#ifdef __cplusplus
}
#endif

#endif /*EMU_BOARD_LED_H*/

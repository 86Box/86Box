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
 *          A machine with LEDs on the board adds this device with the
 *          number of LEDs as its parameter, and drives them with
 *          board_led_set(). The user interface polls their states.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/board_led.h>
#include <86box/plat_unused.h>

static int              led_num = 0;
static volatile uint8_t led_state[BOARD_LED_MAX];

int
board_led_count(void)
{
    return led_num;
}

int
board_led_get(int led)
{
    return ((led >= 0) && (led < led_num)) ? led_state[led] : 0;
}

void
board_led_set(int led, int on)
{
    if ((led >= 0) && (led < led_num))
        led_state[led] = !!on;
}

static void *
board_led_init(const device_t *info)
{
    led_num = (int) info->local;
    if (led_num > BOARD_LED_MAX)
        led_num = BOARD_LED_MAX;

    memset((void *) led_state, 0x00, sizeof(led_state));

    /* Not used, but a device needs something to hand back. */
    return (void *) &led_num;
}

static void
board_led_close(UNUSED(void *priv))
{
    led_num = 0;
}

const device_t board_led_device = {
    .name          = "Board LEDs",
    .internal_name = "board_led",
    .flags         = 0,
    .local         = 0,
    .init          = board_led_init,
    .close         = board_led_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

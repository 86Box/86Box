/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          General GPIO subsystem using libgpiod 2.0.
 *
 *
 *          Copyright 2026 Ignacio Castano.
 */
#include <gpiod.h>
#include <stdint.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/gpio.h>

#define GPIO_MAX_PINS 8

static struct gpiod_chip *chip = NULL;

typedef struct {
    int                        pin;
    int                        current_value;
    struct gpiod_line_request *request;
} gpio_pin_state_t;

static gpio_pin_state_t pins[GPIO_MAX_PINS];
static int              num_pins = 0;

void
gpio_init(void)
{
    if (!gpio_enabled || !gpio_device[0])
        return;

    chip = gpiod_chip_open(gpio_device);
    if (!chip)
        pclog("GPIO: failed to open %s\n", gpio_device);

    num_pins = 0;
}

void
gpio_close(void)
{
    for (int i = 0; i < num_pins; i++) {
        if (pins[i].request)
            gpiod_line_request_release(pins[i].request);
    }

    if (chip)
        gpiod_chip_close(chip);

    chip     = NULL;
    num_pins = 0;
}

void
gpio_set_pin(int pin, int active)
{
    if (!chip || pin < 0)
        return;

    /* Find existing pin state. */
    gpio_pin_state_t *ps = NULL;
    for (int i = 0; i < num_pins; i++) {
        if (pins[i].pin == pin) {
            ps = &pins[i];
            break;
        }
    }

    /* Lazy request on first use. */
    if (!ps) {
        if (num_pins >= GPIO_MAX_PINS)
            return;

        ps                = &pins[num_pins++];
        ps->pin           = pin;
        ps->current_value = -1;
        ps->request       = NULL;

        struct gpiod_line_settings *settings = gpiod_line_settings_new();
        gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
        gpiod_line_settings_set_active_low(settings, gpio_active_low != 0);

        struct gpiod_line_config *line_cfg = gpiod_line_config_new();
        unsigned int              offset   = (unsigned int) pin;
        gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);

        struct gpiod_request_config *req_cfg = gpiod_request_config_new();
        gpiod_request_config_set_consumer(req_cfg, "86box");

        ps->request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

        gpiod_request_config_free(req_cfg);
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(settings);

        if (!ps->request) {
            pclog("GPIO: failed to request pin %d\n", pin);
            num_pins--;
            return;
        }
    }

    /* Only write if value changed. */
    if (ps->current_value != active) {
        ps->current_value = active;
        gpiod_line_request_set_value(ps->request, (unsigned int) pin,
                                     active ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
    }
}

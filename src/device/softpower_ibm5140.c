/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PC Convertible-style soft power control card.
 *
 *          Implements the IBM PC Convertible (5140) power system control
 *          register as an ISA card, so guest software on pre-APM machines
 *          can request a power-off the same way it would on a PC Convertible
 *          (whose BIOS does this on behalf of INT 15h AH=42h):
 *
 *            bit 1 (REQ_POFF): request system power off. Raises the system
 *                suspend NMI if enabled, then removes power after a fixed
 *                delay (~2 seconds on real hardware, during which the NMI
 *                handler saves the system state).
 *            bit 2 (EN_SUS_NMI): gate for the system suspend NMI.
 *            bit 3 (HDWR_RESET): cause a power-on reset.
 *            bit 6 (EXLPWR): status; always set, as emulated systems run
 *                on external power.
 *
 *          Reference: IBM PC Convertible Technical Reference Vol. 1
 *          (6280655), Figures 2-5/2-6 and "Power System Control (Hex 07F)";
 *          Vol. 2 (55X8817), SYS_POWER_OFF and SUSPEND BIOS listings.
 *
 * Authors: Josh Rodd
 *
 *          Copyright 2026 Josh Rodd.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/softpower.h>

#define SOFTPOWER_EN_PON_ALRM 0x01 /* Enable power on by RTC alarm. */
#define SOFTPOWER_REQ_POFF    0x02 /* Request system power off. */
#define SOFTPOWER_EN_SUS_NMI  0x04 /* Enable system suspend NMI. */
#define SOFTPOWER_HDWR_RESET  0x08 /* Cause power-on-reset. */
#define SOFTPOWER_EXLPWR      0x40 /* Status: external power supplied. */

#define SOFTPOWER_CTRL_MASK   0x0f

struct softpower_control_t {
    uint8_t ctrl;
    unsigned delay_ms;
    void (*suspend)(void *);
    void (*power_off)(void *);
    void (*reset)(void *);
    void *priv;
    pc_timer_t timer;
};

typedef struct softpower_t {
    uint16_t base;
    int nmi_enabled;
    softpower_control_t *control;
} softpower_t;

static void
softpower_control_expire(void *priv)
{
    softpower_control_t *control = priv;

    control->power_off(control->priv);
}

softpower_control_t *
softpower_control_create(unsigned delay_ms, void (*suspend)(void *),
                         void (*power_off)(void *), void (*reset)(void *), void *priv)
{
    softpower_control_t *control = calloc(1, sizeof(*control));

    control->delay_ms = delay_ms;
    control->suspend = suspend;
    control->power_off = power_off;
    control->reset = reset;
    control->priv = priv;
    timer_add(&control->timer, softpower_control_expire, control, 0);
    return control;
}

void
softpower_control_reset(softpower_control_t *control)
{
    control->ctrl = 0;
    timer_stop(&control->timer);
}

void
softpower_control_destroy(softpower_control_t *control)
{
    timer_stop(&control->timer);
    free(control);
}

uint8_t
softpower_control_read(const softpower_control_t *control)
{
    return control->ctrl;
}

void
softpower_control_write(softpower_control_t *control, uint8_t value)
{
    uint8_t old = control->ctrl;

    control->ctrl = value & SOFTPOWER_CTRL_MASK;
    if ((control->ctrl & SOFTPOWER_HDWR_RESET) && !(old & SOFTPOWER_HDWR_RESET)) {
        softpower_control_reset(control);
        control->reset(control->priv);
        return;
    }
    if ((control->ctrl & SOFTPOWER_REQ_POFF) && !(old & SOFTPOWER_REQ_POFF)) {
        if ((control->ctrl & SOFTPOWER_EN_SUS_NMI) && control->suspend)
            control->suspend(control->priv);
        /* The split-timer API also handles the ISA card's 30-second setting. */
        timer_stop(&control->timer);
        if (control->delay_ms)
            timer_on_auto(&control->timer, (double) control->delay_ms * 1000.0);
        else
            timer_set_delay_u64(&control->timer, 0);
    }
}

static void
softpower_power_off(UNUSED(void *priv))
{
    plat_power_off();
}

static void
softpower_suspend(void *priv)
{
    const softpower_t *dev = priv;

    if (dev->nmi_enabled)
        nmi_raise();
}

static void
softpower_cpu_reset(UNUSED(void *priv))
{
    softresetx86();
}

static void
softpower_write(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    softpower_t *dev = priv;

    softpower_control_write(dev->control, val);
}

static uint8_t
softpower_read(UNUSED(uint16_t port), void *priv)
{
    const softpower_t *dev = (const softpower_t *) priv;

    /* Bit 6 is always set: the emulated system is on external power. */
    return SOFTPOWER_EXLPWR | softpower_control_read(dev->control);
}

static void
softpower_reset(void *priv)
{
    softpower_t *dev = (softpower_t *) priv;

    softpower_control_reset(dev->control);
}

static void *
softpower_init(UNUSED(const device_t *info))
{
    softpower_t *dev = (softpower_t *) calloc(1, sizeof(softpower_t));

    dev->base       = device_get_config_hex16("base");
    const unsigned delay_ms = device_get_config_int("delay");
    dev->nmi_enabled = !!device_get_config_int("nmi");

    dev->control = softpower_control_create(delay_ms, softpower_suspend,
                                            softpower_power_off, softpower_cpu_reset, dev);
    io_sethandler(dev->base, 1,
                  softpower_read, NULL, NULL,
                  softpower_write, NULL, NULL, dev);

    return dev;
}

static void
softpower_close(void *priv)
{
    softpower_t *dev = (softpower_t *) priv;

    io_removehandler(dev->base, 1,
                     softpower_read, NULL, NULL,
                     softpower_write, NULL, NULL, dev);
    softpower_control_destroy(dev->control);
    free(dev);
}

static const device_config_t softpower_config[] = {
  // clang-format off
    {
        .name           = "base",
        .description    = "I/O base",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x7f,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "7Fh (PC Convertible default)", .value = 0x007f },
            { .description = "6Fh",                          .value = 0x006f },
            { .description = "1EFh",                         .value = 0x01ef },
            { .description = ""                                             }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "delay",
        .description    = "Power-off delay (ms)",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 2000,
        .file_filter    = NULL,
        .spinner        = {
            .min  =     0,
            .max  = 30000,
            .step =   100
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "nmi",
        .description    = "Raise system suspend NMI when enabled",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t softpower_device = {
    .name          = "PC Convertible Soft Power Card",
    .internal_name = "softpower",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = softpower_init,
    .close         = softpower_close,
    .reset         = softpower_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = softpower_config
};

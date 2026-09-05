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

typedef struct softpower_t {
    uint8_t   ctrl;
    uint16_t  base;
    int       nmi_enabled;
    int       delay_ms;
    pc_timer_t power_off_timer;
} softpower_t;

static void
softpower_power_off(UNUSED(void *priv))
{
    plat_power_off();
}

static void
softpower_write(uint16_t port, uint8_t val, void *priv)
{
    softpower_t *dev = (softpower_t *) priv;
    const uint8_t old = dev->ctrl;

    dev->ctrl = val & SOFTPOWER_CTRL_MASK;

    if ((dev->ctrl & SOFTPOWER_HDWR_RESET) && !(old & SOFTPOWER_HDWR_RESET)) {
        /* Cause a power-on reset. */
        dev->ctrl = 0x00;
        timer_disable(&dev->power_off_timer);
        softresetx86();
        return;
    }

    if ((dev->ctrl & SOFTPOWER_REQ_POFF) && !(old & SOFTPOWER_REQ_POFF)) {
        if (dev->nmi_enabled && (dev->ctrl & SOFTPOWER_EN_SUS_NMI))
            nmi_raise(); /* System suspend NMI. */

        /* The power supply shuts the system down ~2 seconds after the
           request, giving the NMI handler time to save system state. */
        timer_set_delay_u64(&dev->power_off_timer,
                            (uint64_t) dev->delay_ms * 1000ULL * TIMER_USEC);
    }
}

static uint8_t
softpower_read(uint16_t port, void *priv)
{
    const softpower_t *dev = (const softpower_t *) priv;

    /* Bit 6 is always set: the emulated system is on external power. */
    return SOFTPOWER_EXLPWR | dev->ctrl;
}

static void
softpower_reset(void *priv)
{
    softpower_t *dev = (softpower_t *) priv;

    dev->ctrl = 0x00;
    timer_disable(&dev->power_off_timer);
}

static void *
softpower_init(UNUSED(const device_t *info))
{
    softpower_t *dev = (softpower_t *) calloc(1, sizeof(softpower_t));

    dev->base       = device_get_config_hex16("base");
    dev->delay_ms   = device_get_config_int("delay");
    dev->nmi_enabled = !!device_get_config_int("nmi");

    timer_add(&dev->power_off_timer, softpower_power_off, dev, 0);
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
    timer_disable(&dev->power_off_timer);
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
            .max  = 60000,
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

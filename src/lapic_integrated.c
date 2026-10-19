/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdint.h>
#include <stdlib.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/chipset.h>
#include "apic_platform.h"

typedef struct integrated_cpu_lapic_t {
    apic_platform_t *apic;
    uint8_t          enabled;
} integrated_cpu_lapic_t;

static int
integrated_cpu_lapic_config_enabled(void)
{
    return !device_get_config_int("disable_apic") &&
           !machine_get_config_int("disable_apic");
}

static void
integrated_cpu_lapic_icr_write(void *priv, uint32_t val)
{
    integrated_cpu_lapic_t *dev = (integrated_cpu_lapic_t *) priv;

    if ((dev == NULL) || !dev->enabled)
        return;

    apic_platform_lapic_icr_write(dev->apic, val);
}

static void
integrated_cpu_lapic_apply(integrated_cpu_lapic_t *dev)
{
    apic_platform_set_lapic_policy(dev->apic, dev->enabled, 0, 0, 0, 0, 0, 0);
    apic_cpu_lapic_set_integrated_policy(dev->enabled);
}

static void
integrated_cpu_lapic_reset(void *priv)
{
    integrated_cpu_lapic_t *dev = (integrated_cpu_lapic_t *) priv;

    if (dev == NULL)
        return;

    apic_platform_reset_lapics(dev->apic);
    if (dev->enabled)
        apic_platform_update_lapic_mapping(dev->apic);
    else {
        apic_platform_stop_lapic_timers(dev->apic);
        apic_platform_disable_lapic_mappings(dev->apic);
    }
}

static void
integrated_cpu_lapic_close(void *priv)
{
    integrated_cpu_lapic_t *dev = (integrated_cpu_lapic_t *) priv;

    if (dev == NULL)
        return;

    apic_cpu_lapic_set_integrated_policy(0);
    apic_platform_stop_lapic_timers(dev->apic);
    apic_platform_disable_lapic_mappings(dev->apic);
    apic_platform_free(dev->apic);
    free(dev);
}

static void *
integrated_cpu_lapic_init(const device_t *info)
{
    integrated_cpu_lapic_t *dev;

    (void) info;

    dev = (integrated_cpu_lapic_t *) calloc(1, sizeof(*dev));
    if (dev == NULL)
        return NULL;

    dev->apic = apic_platform_create(dev);
    if (dev->apic == NULL) {
        free(dev);
        return NULL;
    }

    dev->enabled = integrated_cpu_lapic_config_enabled();
    integrated_cpu_lapic_apply(dev);
    apic_platform_set_icr_write_handler(dev->apic, integrated_cpu_lapic_icr_write);
    apic_platform_reset_lapics(dev->apic);
    if (dev->enabled)
        apic_platform_enable_lapic_mappings(dev->apic);
    else {
        apic_platform_stop_lapic_timers(dev->apic);
        apic_platform_disable_lapic_mappings(dev->apic);
    }

    return dev;
}

const device_t integrated_cpu_lapic_device = {
    .name          = "Integrated CPU Local APIC",
    .internal_name = "integrated_cpu_lapic",
    .flags         = 0,
    .local         = 0,
    .init          = integrated_cpu_lapic_init,
    .close         = integrated_cpu_lapic_close,
    .reset         = integrated_cpu_lapic_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

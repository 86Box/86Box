/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel ICH2 Trap Handler
 *
 *
 *
 * Authors: Tiseno100,
 *
 *          Copyright 2022 Tiseno100.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>

#include <86box/apm.h>
#include <86box/nvr.h>
#include <86box/acpi.h>

#include <86box/intel_ich2_trap.h>

#ifdef ENABLE_INTEL_ICH2_TRAP_LOG
int intel_ich2_trap_do_log = ENABLE_INTEL_ICH2_TRAP_LOG;
static void
intel_ich2_trap_log(const char *fmt, ...)
{
    va_list ap;

    if (intel_ich2_trap_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define intel_ich2_trap_log(fmt, ...)
#endif

void
intel_ich2_trap_set_acpi(intel_ich2_trap_t *dev, acpi_t *acpi)
{
    dev->acpi = acpi;
}

static void
intel_ich2_trap_kick(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv)
{
    intel_ich2_trap_t *dev = (intel_ich2_trap_t *) priv;
    intel_ich2_trap_log("Intel ICH2 Trap: Entered an I/O Trap. Provoking an SMI.\n");
    acpi_raise_smi(dev->acpi, 1);
}

void
intel_ich2_device_trap_setup(uint8_t acpi_reg, uint8_t acpi_reg_val, uint16_t addr, uint16_t size, intel_ich2_trap_t *dev)
{
    uint8_t acpi_reg_recieve = dev->acpi->regs.devtrap_en >> ((acpi_reg & 1) * 8); /* Trap register is 16-bit on ranged ACPIBASE + 48h-49h */
    int     enable           = !!(acpi_reg_recieve & acpi_reg_val);                /* If enabled. Settle in the I/O trap */

    if (enable)
        intel_ich2_trap_log("Intel ICH2 Trap: A new trap was setted up on address 0x%x with the size of %d\n", addr, size);

    io_trap_remap(dev->trap, enable, addr, size);
}

static void
intel_ich2_trap_close(void *priv)
{
    intel_ich2_trap_t *dev = (intel_ich2_trap_t *) priv;

    io_trap_remove(dev->trap); // Remove the I/O Trap
    free(dev);
}

static void *
intel_ich2_trap_init(const device_t *info)
{
    intel_ich2_trap_t *dev = (intel_ich2_trap_t *) malloc(sizeof(intel_ich2_trap_t));
    memset(dev, 0, sizeof(intel_ich2_trap_t));

    intel_ich2_trap_log("Intel ICH2 Trap: Starting a new Trap handler.");

    io_trap_add(intel_ich2_trap_kick, dev);

    return dev;
}

const device_t intel_ich2_trap_device = {
    .name          = "Intel ICH2 Trap Hander",
    .internal_name = "intel_ich2_trap",
    .flags         = 0,
    .local         = 0,
    .init          = intel_ich2_trap_init,
    .close         = intel_ich2_trap_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * Intel ICH2 Trap Handler
 *
 * Authors:	Tiseno100,
 *
 * Copyright 2022 Tiseno100.
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
#define intel_ich2_trap_log(fmt, ...)
#endif

void
intel_ich2_trap_set_acpi(intel_ich2_trap_t *trap, acpi_t *acpi)
{
    trap->acpi = acpi;
}

static void
intel_ich2_trap_kick(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv)
{
    intel_ich2_trap_t *trap = (intel_ich2_trap_t *) priv;
    intel_ich2_trap_log("Intel ICH2 Trap: Entered an I/O Trap. Provoking an SMI.\n");
    acpi_raise_smi(trap->acpi, 1);
}

void
intel_ich2_device_trap_setup(int enable, uint8_t acpi_reg, uint8_t acpi_reg_val, uint16_t addr, uint16_t size, int is_hdd, intel_ich2_trap_t *trap)
{
uint8_t acpi_trap_recieve = ((acpi_reg == 0x49) ? (trap->acpi->regs.devtrap_en >> 8) : (trap->acpi->regs.devtrap_en)) & 0xff; // Check if the decoded range is enabled on ACPIS
int acpi_enable = !!(acpi_trap_recieve & acpi_reg_val);
int trap_enabled = acpi_enable && enable;

if(trap_enabled)
{
    intel_ich2_trap_log("Intel ICH2 Trap: An I/O has been enabled on range 0x%x\n", addr);
    io_trap_add(intel_ich2_trap_kick, trap->trap);
}

io_trap_remap(trap->trap, trap_enabled, addr, size);
}

static void
intel_ich2_trap_close(void *priv)
{
    intel_ich2_trap_t *trap = (intel_ich2_trap_t *) priv;
    
    io_trap_remove(trap->trap); // Remove the I/O Trap
    free(trap);
}

static void *
intel_ich2_trap_init(const device_t *info)
{
    intel_ich2_trap_t *trap = (intel_ich2_trap_t *) malloc(sizeof(intel_ich2_trap_t));
    memset(trap, 0, sizeof(intel_ich2_trap_t));

    intel_ich2_trap_log("Intel ICH2 Trap: Starting a new Trap handler.");

    return trap;
}

const device_t intel_ich2_trap_device = {
    .name = "Intel ICH2 Trap Hander",
    .internal_name = "intel_ich2_trap",
    .flags = 0,
    .local = 0,
    .init = intel_ich2_trap_init,
    .close = intel_ich2_trap_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

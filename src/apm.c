/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Advanced Power Management emulation.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2019 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/apm.h>

#ifdef ENABLE_APM_LOG
int apm_do_log = ENABLE_APM_LOG;

static void
apm_log(const char *fmt, ...)
{
    va_list ap;

    if (apm_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define apm_log(fmt, ...)
#endif

void
apm_set_do_smi(apm_t *dev, uint8_t do_smi)
{
    dev->do_smi = do_smi;
}

static void
apm_out(uint16_t port, uint8_t val, void *priv)
{
    apm_t *dev = (apm_t *) priv;

    apm_log("[%04X:%08X] APM write: %04X = %02X (BX = %04X, CX = %04X)\n", CS, cpu_state.pc, port, val, BX, CX);

    port &= 0x0001;

    if (port == 0x0000) {
        dev->cmd = val;
        if (dev->do_smi)
            smi_raise();
    } else
        dev->stat = val;
}

static uint8_t
apm_in(uint16_t port, void *priv)
{
    const apm_t  *dev = (apm_t *) priv;
    uint8_t       ret = 0xff;

    port &= 0x0001;

    if (port == 0x0000)
        ret = dev->cmd;
    else
        ret = dev->stat;

    apm_log("[%04X:%08X] APM read: %04X = %02X\n", CS, cpu_state.pc, port, ret);

    return ret;
}

static void
apm_reset(void *priv)
{
    apm_t *dev = (apm_t *) priv;

    dev->cmd = dev->stat = 0x00;
}

static void
apm_close(void *priv)
{
    apm_t *dev = (apm_t *) priv;

    free(dev);
}

static void *
apm_init(const device_t *info)
{
    apm_t *dev = (apm_t *) malloc(sizeof(apm_t));
    memset(dev, 0, sizeof(apm_t));

    if (info->local == 0)
        io_sethandler(0x00b2, 0x0002, apm_in, NULL, NULL, apm_out, NULL, NULL, dev);

    return dev;
}

const device_t apm_device = {
    .name          = "Advanced Power Management",
    .internal_name = "apm",
    .flags         = 0,
    .local         = 0,
    .init          = apm_init,
    .close         = apm_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t apm_pci_device = {
    .name          = "Advanced Power Management (PCI)",
    .internal_name = "apm_pci",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = apm_init,
    .close         = apm_close,
    .reset         = apm_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t apm_pci_acpi_device = {
    .name          = "Advanced Power Management (PCI)",
    .internal_name = "apm_pci_acpi",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = apm_init,
    .close         = apm_close,
    .reset         = apm_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

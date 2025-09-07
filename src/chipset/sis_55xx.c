/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 55xx common structure.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2024 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/smram.h>
#include <86box/spd.h>
#include <86box/apm.h>
#include <86box/ddma.h>
#include <86box/acpi.h>
#include <86box/smbus.h>
#include <86box/sis_55xx.h>
#include <86box/chipset.h>
#include <86box/usb.h>

#ifdef ENABLE_SIS_55XX_COMMON_LOG
int sis_55xx_common_do_log = ENABLE_SIS_55XX_COMMON_LOG;

static void
sis_55xx_common_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_55xx_common_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_55xx_common_log(fmt, ...)
#endif

static void
sis_55xx_common_close(void *priv)
{
    sis_55xx_common_t *dev = (sis_55xx_common_t *) priv;

    free(dev);
}

static void *
sis_55xx_common_init(UNUSED(const device_t *info))
{
    sis_55xx_common_t *dev = (sis_55xx_common_t *) calloc(1, sizeof(sis_55xx_common_t));

    return dev;
}

const device_t sis_55xx_common_device = {
    .name          = "SiS 55xx Common Structure",
    .internal_name = "sis_55xx_common",
    .flags         = DEVICE_PCI,
    .local         = 0x00,
    .init          = sis_55xx_common_init,
    .close         = sis_55xx_common_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

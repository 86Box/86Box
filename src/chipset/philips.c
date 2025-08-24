/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Philips XT-compatible machines.
 *
 * Authors: EngiNerd <webmaster.crrc@yahoo.it>
 *
 *          Copyright 2020-2025 EngiNerd.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/nmi.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/gameport.h>
#include <86box/ibm_5161.h>
#include <86box/keyboard.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/chipset.h>
#include <86box/io.h>
#include <86box/video.h>
#include <86box/plat_unused.h>

typedef struct philips_t {
    uint8_t reg;
} philips_t;

#ifdef ENABLE_PHILIPS_LOG
int philips_do_log = ENABLE_PHILIPS_LOG;
static void
philips_log(const char *fmt, ...)
{
    va_list ap;

    if (philips_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define philips_log(fmt, ...)
#endif

static void
philips_write(uint16_t port, uint8_t val, void *priv)
{
    philips_t *dev = (philips_t *) priv;

    switch (port) {
        /* port 0xc0
         * bit 7: turbo
         * bits 4-5: rtc read/set (I2C Bus SDA/SCL?)
         * bit 2: parity disabled
         */
        case 0xc0:
            dev->reg = val;
            if (val & 0x80)
                cpu_dynamic_switch(cpu);
            else
                cpu_dynamic_switch(0);
            break;

        default:
            break;
    }

    philips_log("Philips XT Mainboard: Write %02x at %02x\n", val, port);
}

static uint8_t
philips_read(uint16_t port, void *priv)
{
    const philips_t *dev = (philips_t *) priv;
    uint8_t          ret = 0xff;

    switch (port) {
        /* port 0xc0
         * bit 7: turbo
         * bits 4-5: rtc read/set
         * bit 2: parity disabled
         */
        case 0xc0:
            ret = dev->reg;
            break;

        default:
            break;
    }

    philips_log("Philips XT Mainboard: Read %02x at %02x\n", ret, port);

    return ret;
}

static void
philips_close(void *priv)
{
    philips_t *dev = (philips_t *) priv;

    free(dev);
}

static void *
philips_init(UNUSED(const device_t *info))
{
    philips_t *dev = (philips_t *) malloc(sizeof(philips_t));
    memset(dev, 0, sizeof(philips_t));

    dev->reg = 0x40;

    io_sethandler(0x0c0, 0x01, philips_read, NULL, NULL, philips_write, NULL, NULL, dev);

    return dev;
}

const device_t philips_device = {
    .name          = "Philips XT Mainboard",
    .internal_name = "philips",
    .flags         = 0,
    .local         = 0,
    .init          = philips_init,
    .close         = philips_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

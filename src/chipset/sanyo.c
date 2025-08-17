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

typedef struct sanyo_t {
    uint8_t reg;
} sanyo_t;

#ifdef ENABLE_SANYO_LOG
int sanyo_do_log = ENABLE_SANYO_LOG;

static void
sanyo_log(const char *fmt, ...)
{
    va_list ap;

    if (sanyo_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sanyo_log(fmt, ...)
#endif

static void
sanyo_write(uint16_t port, uint8_t val, void *priv)
{
    sanyo_t *dev = (sanyo_t *) priv;

    dev->reg = val;

    cpu_waitstates = !(val & 0x01);
    cpu_update_waitstates();

    sanyo_log("Sanyo MBC-17 Mainboard: Write %02x at %02x\n", val, port);
}

static uint8_t
sanyo_read(uint16_t port, void *priv)
{
    const sanyo_t *dev = (sanyo_t *) priv;
    uint8_t        ret = 0xff;

    ret = dev->reg;

    sanyo_log("Sanyo MBC-17 Mainboard: Read %02x at %02x\n", ret, port);

    return ret;
}

static void
sanyo_close(void *priv)
{
    sanyo_t *dev = (sanyo_t *) priv;

    free(dev);
}

static void *
sanyo_init(UNUSED(const device_t *info))
{
    sanyo_t *dev = (sanyo_t *) calloc(1, sizeof(sanyo_t));

    dev->reg = cpu_waitstates ? 0x00 : 0x01;

    io_sethandler(0x0063, 0x01, sanyo_read, NULL, NULL, sanyo_write, NULL, NULL, dev);

    return dev;
}

const device_t sanyo_device = {
    .name          = "Sanyo MBC-17 Mainboard",
    .internal_name = "sanyo",
    .flags         = 0,
    .local         = 0,
    .init          = sanyo_init,
    .close         = sanyo_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Tulip Jumper Readout.
 *
 *          Bits 7-5 = board number, 0-5 valid, 6, 7 invalid.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Miran Grca.
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
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/chipset.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>

typedef struct tulip_jumper_t {
    uint8_t jumper;
} tulip_jumper_t;

#ifdef ENABLE_TULIP_JUMPER_LOG
int tulip_jumper_do_log = ENABLE_TULIP_JUMPER_LOG;

static void
tulip_jumper_log(const char *fmt, ...)
{
    va_list ap;

    if (tulip_jumper_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define tulip_jumper_log(fmt, ...)
#endif

static uint8_t
tulip_jumper_read(uint16_t addr, void *priv)
{
    const tulip_jumper_t *dev = (tulip_jumper_t *) priv;
    uint8_t               ret = 0xff;

    tulip_jumper_log("Tulip Jumper: Read %02x\n", dev->jumper);

    ret = dev->jumper;

    return ret;
}

static void
tulip_jumper_close(void *priv)
{
    tulip_jumper_t *dev = (tulip_jumper_t *) priv;

    free(dev);
}

static void *
tulip_jumper_init(const device_t *info)
{
    tulip_jumper_t *dev = (tulip_jumper_t *) calloc(1, sizeof(tulip_jumper_t));

    /* Return board number 05. */
    dev->jumper = 0xbf;

    io_sethandler(0x0d80, 0x0001, tulip_jumper_read, NULL, NULL, NULL, NULL, NULL, dev);

    return dev;
}

const device_t tulip_jumper_device = {
    .name          = "Tulip Jumper Readout",
    .internal_name = "tulip_jumper",
    .flags         = 0,
    .local         = 0,
    .init          = tulip_jumper_init,
    .close         = tulip_jumper_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

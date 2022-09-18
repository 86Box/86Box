/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Phoenix 486 Jumper Readout
 *
 *		Copyright 2020 Tiseno100
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
#include <86box/chipset.h>

/*
    Bit 7 = Super I/O chip: 1 = enabled, 0 = disabled;
    Bit 6 = Graphics card: 1 = standalone, 0 = on-board;
    Bit 5 = ???? (if 1, siren and hangs);
    Bit 4 = ????;
    Bit 3 = ????;
    Bit 2 = ????;
    Bit 1 = ????;
    Bit 0 = ????.
*/

typedef struct
{
    uint8_t type, jumper;
} phoenix_486_jumper_t;

#ifdef ENABLE_PHOENIX_486_JUMPER_LOG
int phoenix_486_jumper_do_log = ENABLE_PHOENIX_486_JUMPER_LOG;

static void
phoenix_486_jumper_log(const char *fmt, ...)
{
    va_list ap;

    if (phoenix_486_jumper_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define phoenix_486_jumper_log(fmt, ...)
#endif

static void
phoenix_486_jumper_write(uint16_t addr, uint8_t val, void *priv)
{
    phoenix_486_jumper_t *dev = (phoenix_486_jumper_t *) priv;
    phoenix_486_jumper_log("Phoenix 486 Jumper: Write %02x\n", val);
    if (dev->type == 1)
        dev->jumper = val & 0xbf;
    else
        dev->jumper = val;
}

static uint8_t
phoenix_486_jumper_read(uint16_t addr, void *priv)
{
    phoenix_486_jumper_t *dev = (phoenix_486_jumper_t *) priv;
    phoenix_486_jumper_log("Phoenix 486 Jumper: Read %02x\n", dev->jumper);
    return dev->jumper;
}

static void
phoenix_486_jumper_reset(void *priv)
{
    phoenix_486_jumper_t *dev = (phoenix_486_jumper_t *) priv;

    if (dev->type == 1)
        dev->jumper = 0x00;
    else {
        dev->jumper = 0x9f;
        if (gfxcard != 0x01)
            dev->jumper |= 0x40;
    }
}

static void
phoenix_486_jumper_close(void *priv)
{
    phoenix_486_jumper_t *dev = (phoenix_486_jumper_t *) priv;

    free(dev);
}

static void *
phoenix_486_jumper_init(const device_t *info)
{
    phoenix_486_jumper_t *dev = (phoenix_486_jumper_t *) malloc(sizeof(phoenix_486_jumper_t));
    memset(dev, 0, sizeof(phoenix_486_jumper_t));

    dev->type = info->local;

    phoenix_486_jumper_reset(dev);

    io_sethandler(0x0078, 0x0001, phoenix_486_jumper_read, NULL, NULL, phoenix_486_jumper_write, NULL, NULL, dev);

    return dev;
}

const device_t phoenix_486_jumper_device = {
    .name          = "Phoenix 486 Jumper Readout",
    .internal_name = "phoenix_486_jumper",
    .flags         = 0,
    .local         = 0,
    .init          = phoenix_486_jumper_init,
    .close         = phoenix_486_jumper_close,
    .reset         = phoenix_486_jumper_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t phoenix_486_jumper_pci_device = {
    .name          = "Phoenix 486 Jumper Readout (PCI machines)",
    .internal_name = "phoenix_486_jumper_pci",
    .flags         = 0,
    .local         = 1,
    .init          = phoenix_486_jumper_init,
    .close         = phoenix_486_jumper_close,
    .reset         = phoenix_486_jumper_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

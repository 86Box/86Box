/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Phoenix 486 Jumper Readout.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Tiseno100,
 *
 *          Copyright 2020-2023 Miran Grca.
 *          Copyright 2020-2023 Tiseno100.
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
#include <86box/plat_unused.h>

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

/*
    PB600 bit meanings:
    Bit 7 = ???? (if 1 BIOS throws beep codes and won't POST)
    Bit 6 = Super I/O chip: 1 = disabled, 0 = enabled
    Bit 5 = ????
    Bit 4 = ????
    Bit 3 = ????
    Bit 2 = ????
    Bit 1 = Quick Boot: 1 = normal boot, 0 = quick boot/skip POST
    Bit 0 = ????
*/

/*
    Intel Monsoon bit meanings:
    Bit 5 = Password enable
    Bit 4 = Onboard video: 1 = disabled, 0 = enabled
    Bit 3 = CMOS Setup enable
    Bit 2 = CMOS clear: 1 = normal, 0 = clear CMOS
*/

/*
    PB400 bit meanings:
    Bit 7 = ????
    Bit 6 = ????
    Bit 5 = ????
    Bit 4 = ????
    Bit 3 = Graphics card: 1 = on-board, 0 = standalone
    Bit 2 = On-board graphics mode: 0 = normal, 1 = VESA
    Bit 1 = ????
    Bit 0 = ????
*/

typedef struct phoenix_486_jumper_t {
    uint8_t type;
    uint8_t jumper;
} phoenix_486_jumper_t;

enum {
    PHOENIX_JUMPER_DEFAULT = 0,
    PHOENIX_JUMPER_PCI     = 1,
    PHOENIX_JUMPER_PB600   = 2,
    PHOENIX_JUMPER_MONSOON = 3,
    PHOENIX_JUMPER_PB400   = 4,
    PHOENIX_JUMPER_PB430   = 5
};

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
phoenix_486_jumper_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    phoenix_486_jumper_t *dev = (phoenix_486_jumper_t *) priv;
    phoenix_486_jumper_log("Phoenix 486 Jumper: Write %02x\n", val);
    if (dev->type == PHOENIX_JUMPER_PCI)
        dev->jumper = val & 0xbf;
    else if (dev->type == PHOENIX_JUMPER_PB600) /* PB600 */
        dev->jumper = ((val & 0xbf) | 0x02);
    else if (dev->type == PHOENIX_JUMPER_MONSOON) { /* Intel Monsoon */
        dev->jumper = ((val & 0xef) | 0x2c);
        if (gfxcard[0] != 0x01)
            dev->jumper |= 0x10;
    } else if (dev->type == PHOENIX_JUMPER_PB400)
        dev->jumper = val & 0xfb;
    else if (dev->type == PHOENIX_JUMPER_PB430)
        dev->jumper = (val & 0xdf) | 0x9f;
    else
        dev->jumper = val;
}

static uint8_t
phoenix_486_jumper_read(UNUSED(uint16_t addr), void *priv)
{
    const phoenix_486_jumper_t *dev = (phoenix_486_jumper_t *) priv;

    phoenix_486_jumper_log("Phoenix 486 Jumper: Read %02x\n", dev->jumper);
    return dev->jumper;
}

static void
phoenix_486_jumper_reset(void *priv)
{
    phoenix_486_jumper_t *dev = (phoenix_486_jumper_t *) priv;

    if (dev->type == PHOENIX_JUMPER_PCI)
        dev->jumper = 0x00;
    else if (dev->type == PHOENIX_JUMPER_PB600) /* PB600 */
        dev->jumper = 0x02;
    else if (dev->type == PHOENIX_JUMPER_MONSOON) { /* Intel Monsoon */
        dev->jumper = 0x2c;
        if (gfxcard[0] != 0x01)
            dev->jumper |= 0x10;
    } else if (dev->type == PHOENIX_JUMPER_PB400) {
        dev->jumper = 0xfb;
        if (gfxcard[0] != 0x01)
            dev->jumper &= 0xf3;
    } else if (dev->type == PHOENIX_JUMPER_PB430) {
        dev->jumper = 0x9f;
        if (gfxcard[0] != 0x01)
            dev->jumper |= 0x40;
    } else {
        dev->jumper = 0x9f;
        if (gfxcard[0] != 0x01)
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
    phoenix_486_jumper_t *dev = (phoenix_486_jumper_t *) calloc(1, sizeof(phoenix_486_jumper_t));

    dev->type = info->local;

    phoenix_486_jumper_reset(dev);

    io_sethandler(0x0078, 0x0001, phoenix_486_jumper_read, NULL, NULL, phoenix_486_jumper_write, NULL, NULL, dev);

    return dev;
}

const device_t phoenix_486_jumper_device = {
    .name          = "Phoenix 486 Jumper Readout",
    .internal_name = "phoenix_486_jumper",
    .flags         = 0,
    .local         = PHOENIX_JUMPER_DEFAULT,
    .init          = phoenix_486_jumper_init,
    .close         = phoenix_486_jumper_close,
    .reset         = phoenix_486_jumper_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t phoenix_486_jumper_pci_device = {
    .name          = "Phoenix 486 Jumper Readout (PCI machines)",
    .internal_name = "phoenix_486_jumper_pci",
    .flags         = 0,
    .local         = PHOENIX_JUMPER_PCI,
    .init          = phoenix_486_jumper_init,
    .close         = phoenix_486_jumper_close,
    .reset         = phoenix_486_jumper_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t phoenix_486_jumper_pci_pb600_device = {
    .name          = "Phoenix 486 Jumper Readout (PB600)",
    .internal_name = "phoenix_486_jumper_pci_pb600",
    .flags         = 0,
    .local         = PHOENIX_JUMPER_PB600,
    .init          = phoenix_486_jumper_init,
    .close         = phoenix_486_jumper_close,
    .reset         = phoenix_486_jumper_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t phoenix_486_jumper_monsoon_device = {
    .name          = "Phoenix 486 Jumper Readout (Monsoon)",
    .internal_name = "phoenix_486_jumper_monsoon",
    .flags         = 0,
    .local         = PHOENIX_JUMPER_MONSOON,
    .init          = phoenix_486_jumper_init,
    .close         = phoenix_486_jumper_close,
    .reset         = phoenix_486_jumper_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t phoenix_486_jumper_pb400_device = {
    .name          = "Phoenix 486 Jumper Readout (PB400)",
    .internal_name = "phoenix_486_jumper_pb400",
    .flags         = 0,
    .local         = PHOENIX_JUMPER_PB400,
    .init          = phoenix_486_jumper_init,
    .close         = phoenix_486_jumper_close,
    .reset         = phoenix_486_jumper_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t phoenix_486_jumper_pb430_device = {
    .name          = "Phoenix 486 Jumper Readout (PB430)",
    .internal_name = "phoenix_486_jumper_pb430",
    .flags         = 0,
    .local         = PHOENIX_JUMPER_PB430,
    .init          = phoenix_486_jumper_init,
    .close         = phoenix_486_jumper_close,
    .reset         = phoenix_486_jumper_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};


/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C283 chipset.
 *
 *
 *
 *      Authors: Tiseno100
 *
 *		Copyright 2021 Tiseno100
 *
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
#include <86box/mem.h>
#include <86box/chipset.h>

#ifdef ENABLE_OPTI283_LOG
int opti283_do_log = ENABLE_OPTI283_LOG;
static void
opti283_log(const char *fmt, ...)
{
    va_list ap;

    if (opti283_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define opti283_log(fmt, ...)
#endif

typedef struct
{
    uint8_t index,
        regs[256];
} opti283_t;

static void opti283_shadow_recalc(opti283_t *dev)
{
    mem_set_mem_state_both(0xf0000, 0x10000, (dev->regs[0x11] & 0x80) ? (MEM_READ_EXTANY | MEM_WRITE_INTERNAL) : (MEM_READ_INTERNAL | ((dev->regs[0x14] & 0x80) ? MEM_WRITE_INTERNAL : MEM_WRITE_DISABLED)));

    for (uint32_t i = 0; i < 4; i++)
    {
        if (dev->regs[0x11] & 0x40)
            mem_set_mem_state_both(0xe0000 + (i << 14), 0x4000, (dev->regs[0x12] & (1 << (4 + i))) ? (MEM_READ_INTERNAL | ((dev->regs[0x11] & 4) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL)) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
        mem_set_mem_state_both(0xe0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

        if (dev->regs[0x11] & 0x20)
            mem_set_mem_state_both(0xd0000 + (i << 14), 0x4000, (dev->regs[0x12] & (1 << i)) ? (MEM_READ_INTERNAL | ((dev->regs[0x11] & 2) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL)) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
        else
            mem_set_mem_state_both(0xd0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

        if (dev->regs[0x11] & 0x10)
            mem_set_mem_state_both(0xc0000 + (i << 14), 0x4000, (dev->regs[0x13] & (1 << (4 + i))) ? (MEM_READ_INTERNAL | ((dev->regs[0x11] & 1) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL)) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
        else
            mem_set_mem_state_both(0xc0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }
}

static void
opti283_write(uint16_t addr, uint8_t val, void *priv)
{
    opti283_t *dev = (opti283_t *)priv;

    switch (addr)
    {
    case 0x22:
        dev->index = val;
        break;
    case 0x24:
        opti283_log("OPTi 283: dev->regs[%02x] = %02x\n", dev->index, val);

        switch (dev->index)
        {
        case 0x10:
            dev->regs[dev->index] = val;
            break;

        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
            dev->regs[dev->index] = val;
            opti283_shadow_recalc(dev);
            break;
        }
        break;
    }
}

static uint8_t
opti283_read(uint16_t addr, void *priv)
{
    opti283_t *dev = (opti283_t *)priv;
    return (addr == 0x24) ? dev->regs[dev->index] : 0xff;
}

static void
opti283_close(void *priv)
{
    opti283_t *dev = (opti283_t *)priv;

    free(dev);
}

static void *
opti283_init(const device_t *info)
{
    opti283_t *dev = (opti283_t *)malloc(sizeof(opti283_t));
    memset(dev, 0, sizeof(opti283_t));

    io_sethandler(0x0022, 0x0001, opti283_read, NULL, NULL, opti283_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti283_read, NULL, NULL, opti283_write, NULL, NULL, dev);

    dev->regs[0x10] = 0x3f;
    dev->regs[0x11] = 0xf0;
    opti283_shadow_recalc(dev);

    return dev;
}

const device_t opti283_device = {
    "OPTi 82C283",
    0,
    0,
    opti283_init,
    opti283_close,
    NULL,
    {NULL},
    NULL,
    NULL,
    NULL};

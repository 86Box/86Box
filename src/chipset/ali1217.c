/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ALi M1217 chipset.
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
#include <86box/port_92.h>
#include <86box/chipset.h>

#ifdef ENABLE_ALI1217_LOG
int ali1217_do_log = ENABLE_ALI1217_LOG;
static void
ali1217_log(const char *fmt, ...)
{
    va_list ap;

    if (ali1217_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define ali1217_log(fmt, ...)
#endif

typedef struct
{
    uint8_t index, regs[256];
    int cfg_locked;
} ali1217_t;

static void ali1217_shadow_recalc(ali1217_t *dev)
{
    for (uint8_t i = 0; i < 4; i++)
    {
        mem_set_mem_state_both(0xc0000 + (i << 15), 0x8000, ((dev->regs[0x14] & (1 << (i * 2))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x14] & (1 << ((i * 2) + 1))) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_both(0xe0000 + (i << 15), 0x8000, ((dev->regs[0x15] & (1 << (i * 2))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x15] & (1 << ((i * 2) + 1))) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }

    shadowbios = !!(dev->regs[0x15] & 5);
    shadowbios_write = !!(dev->regs[0x15] & 0x0a);

    flushmmucache();
}

static void
ali1217_write(uint16_t addr, uint8_t val, void *priv)
{
    ali1217_t *dev = (ali1217_t *)priv;

    switch (addr)
    {
    case 0x22:
        dev->index = val;
        break;
    case 0x23:
        if (dev->index != 0x13)
            ali1217_log("ALi M1217: dev->regs[%02x] = %02x\n", dev->index, val);

        if(dev->index == 0x13)
        dev->cfg_locked = !(val == 0xc5);

        if (!dev->cfg_locked)
        {
            dev->regs[dev->index] = val;
            switch (dev->index)
            {
            case 0x14:
            case 0x15:
                ali1217_shadow_recalc(dev);
                break;
            }
        }
        break;
    }
}

static uint8_t
ali1217_read(uint16_t addr, void *priv)
{
    ali1217_t *dev = (ali1217_t *)priv;

    return !(addr == 0x22) ? dev->regs[dev->index] : dev->index;
}

static void
ali1217_close(void *priv)
{
    ali1217_t *dev = (ali1217_t *)priv;

    free(dev);
}

static void *
ali1217_init(const device_t *info)
{
    ali1217_t *dev = (ali1217_t *)malloc(sizeof(ali1217_t));
    memset(dev, 0, sizeof(ali1217_t));

    device_add(&port_92_device);

    dev->cfg_locked = 1;

/*

ALi M1217 Ports

22h Index Port
23h Data Port

*/
    io_sethandler(0x0022, 0x0002, ali1217_read, NULL, NULL, ali1217_write, NULL, NULL, dev);
    ali1217_shadow_recalc(dev);

    return dev;
}

const device_t ali1217_device = {
    "ALi M1217",
    0,
    0,
    ali1217_init,
    ali1217_close,
    NULL,
    {NULL},
    NULL,
    NULL,
    NULL};

/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Macronix MXIC 305/306 chipset.
 *
 *      Note: This chipset has no datasheet, everything were done via
 *      reverse engineering the BIOS of various machines using it.
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

/* Shadow RAM Registers*/
#define ENABLED (((val & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((val & 0x40) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY))
#define DISABLED (MEM_READ_EXTANY | MEM_WRITE_EXTANY)

#ifdef ENABLE_MXIC306_LOG
int mxic306_do_log = ENABLE_MXIC306_LOG;
static void
mxic306_log(const char *fmt, ...)
{
    va_list ap;

    if (mxic306_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define mxic306_log(fmt, ...)
#endif

typedef struct
{
    uint8_t index, regs[16];
} mxic306_t;

static void
mxic306_shadow_recalc(uint8_t val, mxic306_t *dev)
{

    /* System is shadowed globally if a segment or itself are enabled */
    mem_set_mem_state_both(0xf0000, 0x10000, ENABLED);

    mem_set_mem_state_both(0xe0000, 0x10000, (val & 0x20) ? ENABLED : DISABLED);

    for (int i = 0; i < 4; i++)
        mem_set_mem_state_both(0xc0000 + (i << 15), 0x8000, (val & (1 << i) ? ENABLED : DISABLED));

    flushmmucache_nopc();
}

static void
mxic306_bus_speed_recalc(uint8_t val, mxic306_t *dev)
{
    /* May be wrong */
    switch (val)
    {
    case 0: /* AT Clock 1/5 */
        cpu_set_isa_speed(cpu_busspeed / 5);
        break;

    case 1: /* AT Clock 1/2 */
    case 2: /* AT Clock 1/3 */
    case 3: /* AT Clock 1/4 */
        cpu_set_isa_speed(cpu_busspeed / (2 + val));
        break;
    }
}

static void
mxic306_write(uint16_t addr, uint8_t val, void *priv)
{
    mxic306_t *dev = (mxic306_t *)priv;

    switch (addr)
    {
    case 0x22:
        dev->index = val;
        break;
    case 0x23:
        if ((dev->index >= 0x30) || (dev->index <= 0x3f))
        {
            mxic306_log("MXIC 306: dev->regs[%02x] = %02x\n", dev->index, val);
            dev->regs[dev->index - 0x30] = val;

            switch (dev->index)
            {
            case 0x3a:
                mxic306_shadow_recalc(val, dev);
                break;

            case 0x3d:
                mxic306_bus_speed_recalc(val & 3, dev);
                break;

            case 0x3e:
                cpu_cache_ext_enabled = !!(val & 0x10);
                cpu_update_waitstates();
                break;
            }
        }
        break;
    }
}

static uint8_t
mxic306_read(uint16_t addr, void *priv)
{
    mxic306_t *dev = (mxic306_t *)priv;

    return (addr == 0x23) ? (((dev->index >= 0x30) || (dev->index <= 0x3f)) ? dev->regs[dev->index - 0x30] : 0) : dev->index;
}

static void
mxic306_close(void *priv)
{
    mxic306_t *dev = (mxic306_t *)priv;

    free(dev);
}

static void *
mxic306_init(const device_t *info)
{
    mxic306_t *dev = (mxic306_t *)malloc(sizeof(mxic306_t));
    memset(dev, 0, sizeof(mxic306_t));

    io_sethandler(0x0022, 2, mxic306_read, NULL, NULL, mxic306_write, NULL, NULL, dev);

    return dev;
}

const device_t mxic306_device = {
    "Macronix MXIC 305/306",
    0,
    0,
    mxic306_init,
    mxic306_close,
    NULL,
    {NULL},
    NULL,
    NULL,
    NULL};

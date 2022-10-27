/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ACC 2046/2168 chipset
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *          Tiseno100
 *
 *		Copyright 2019 Sarah Walker.
 *      Copyright 2021 Tiseno100.
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
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

#define ENABLED_SHADOW  (MEM_READ_INTERNAL | ((dev->regs[0x02] & 0x20) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL))
#define DISABLED_SHADOW (MEM_READ_EXTANY | MEM_WRITE_EXTANY)
#define SHADOW_ADDR     ((i <= 1) ? (0xc0000 + (i << 15)) : (0xd0000 + ((i - 2) << 16)))
#define SHADOW_SIZE     ((i <= 1) ? 0x8000 : 0x10000)
#define SHADOW_RECALC   ((dev->regs[0x02] & (1 << i)) ? ENABLED_SHADOW : DISABLED_SHADOW)

#ifdef ENABLE_ACC2168_LOG
int acc2168_do_log = ENABLE_ACC2168_LOG;

static void
acc2168_log(const char *fmt, ...)
{
    va_list ap;

    if (acc2168_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define acc2168_log(fmt, ...)
#endif

typedef struct acc2168_t {
    uint8_t reg_idx, regs[256];
} acc2168_t;

static void
acc2168_shadow_recalc(acc2168_t *dev)
{
    for (uint32_t i = 0; i < 5; i++)
        mem_set_mem_state_both(SHADOW_ADDR, SHADOW_SIZE, SHADOW_RECALC);
}

static void
acc2168_write(uint16_t addr, uint8_t val, void *p)
{
    acc2168_t *dev = (acc2168_t *) p;

    switch (addr) {
        case 0xf2:
            dev->reg_idx = val;
            break;
        case 0xf3:
            acc2168_log("ACC2168: dev->regs[%02x] = %02x\n", dev->reg_idx, val);
            switch (dev->reg_idx) {
                case 0x00:
                    dev->regs[dev->reg_idx] = val;
                    break;

                case 0x01:
                    dev->regs[dev->reg_idx] = val & 0xd3;
                    cpu_update_waitstates();
                    break;

                case 0x02:
                    dev->regs[dev->reg_idx] = val & 0x7f;
                    acc2168_shadow_recalc(dev);
                    break;

                case 0x03:
                    dev->regs[dev->reg_idx] = val & 0x1f;
                    break;

                case 0x04:
                    dev->regs[dev->reg_idx] = val;
                    cpu_cache_ext_enabled   = !!(val & 0x01);
                    cpu_update_waitstates();
                    break;

                case 0x05:
                    dev->regs[dev->reg_idx] = val & 0xf3;
                    break;

                case 0x06:
                case 0x07:
                    dev->regs[dev->reg_idx] = val & 0x1f;
                    break;

                case 0x08:
                    dev->regs[dev->reg_idx] = val & 0x0f;
                    break;

                case 0x09:
                    dev->regs[dev->reg_idx] = val & 0x03;
                    break;

                case 0x0a:
                case 0x0b:
                case 0x0c:
                case 0x0d:
                case 0x0e:
                case 0x0f:
                case 0x10:
                case 0x11:
                    dev->regs[dev->reg_idx] = val;
                    break;

                case 0x12:
                    dev->regs[dev->reg_idx] = val & 0xbb;
                    break;

                case 0x18:
                    dev->regs[dev->reg_idx] = val & 0x77;
                    break;

                case 0x19:
                    dev->regs[dev->reg_idx] = val & 0xfb;
                    break;

                case 0x1a:
                    dev->regs[dev->reg_idx] = val;
                    cpu_cache_int_enabled   = !(val & 0x40);
                    cpu_update_waitstates();
                    break;

                case 0x1b:
                    dev->regs[dev->reg_idx] = val & 0xef;
                    break;

                default: /* ACC 2168 has way more registers which we haven't documented */
                    dev->regs[dev->reg_idx] = val;
                    break;
            }
            break;
    }
}

static uint8_t
acc2168_read(uint16_t addr, void *p)
{
    acc2168_t *dev = (acc2168_t *) p;

    return (addr == 0xf3) ? dev->regs[dev->reg_idx] : dev->reg_idx;
}

static void
acc2168_close(void *priv)
{
    acc2168_t *dev = (acc2168_t *) priv;

    free(dev);
}

static void *
acc2168_init(const device_t *info)
{
    acc2168_t *dev = (acc2168_t *) malloc(sizeof(acc2168_t));
    memset(dev, 0, sizeof(acc2168_t));

    device_add(&port_92_device);
    io_sethandler(0x00f2, 0x0002, acc2168_read, NULL, NULL, acc2168_write, NULL, NULL, dev);

    return dev;
}

const device_t acc2168_device = {
    .name          = "ACC 2046/2168",
    .internal_name = "acc2168",
    .flags         = 0,
    .local         = 0,
    .init          = acc2168_init,
    .close         = acc2168_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

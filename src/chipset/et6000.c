/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ETEQ Cheetah ET6000 chipset.
 *
 *
 *
 * Authors: Tiseno100
 *
 *          Copyright 2021 Tiseno100
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
#include <86box/pit.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

#define INDEX (dev->index - 0x10)

typedef struct et6000_t {
    uint8_t index;
    uint8_t regs[256];
} et6000_t;

#ifdef ENABLE_ET6000_LOG
int et6000_do_log = ENABLE_ET6000_LOG;

static void
et6000_log(const char *fmt, ...)
{
    va_list ap;

    if (et6000_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define et6000_log(fmt, ...)
#endif

static void
et6000_shadow_control(int base, int size, int can_read, int can_write)
{
    mem_set_mem_state_both(base, size, (can_read ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | (can_write ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    flushmmucache_nopc();
}

static void
et6000_write(uint16_t addr, uint8_t val, void *priv)
{
    et6000_t *dev = (et6000_t *) priv;

    switch (addr) {
        case 0x22:
            dev->index = val;
            break;
        case 0x23:
            switch (INDEX) {
                case 0: /* System Configuration Register */
                    dev->regs[INDEX] = val & 0xdf;
                    et6000_shadow_control(0xa0000, 0x20000, val & 1, val & 1);
                    refresh_at_enable = !(val & 0x10);
                    break;

                case 1: /* CACHE Configuration and Non-Cacheable Block Size */
                    dev->regs[INDEX] = val & 0xf0;
                    break;

                case 2: /* Non-Cacheable Block Address Register */
                    dev->regs[INDEX] = val & 0xfe;
                    break;

                case 3: /* DRAM Bank and Type Configuration Register */
                    dev->regs[INDEX] = val;
                    break;

                case 4: /* DRAM Configuration Register */
                    dev->regs[INDEX] = val;
                    et6000_shadow_control(0xc0000, 0x10000, (dev->regs[0x15] & 2) && (val & 0x20), (dev->regs[0x15] & 2) && (val & 0x20) && (dev->regs[0x15] & 1));
                    et6000_shadow_control(0xd0000, 0x10000, (dev->regs[0x15] & 8) && (val & 0x20), (dev->regs[0x15] & 8) && (val & 0x20) && (dev->regs[0x15] & 4));
                    break;

                case 5: /* Shadow RAM Configuration Register */
                    dev->regs[INDEX] = val;
                    et6000_shadow_control(0xc0000, 0x10000, (val & 2) && (dev->regs[0x14] & 0x20), (val & 2) && (dev->regs[0x14] & 0x20) && (val & 1));
                    et6000_shadow_control(0xd0000, 0x10000, (val & 8) && (dev->regs[0x14] & 0x20), (val & 8) && (dev->regs[0x14] & 0x20) && (val & 4));
                    et6000_shadow_control(0xe0000, 0x10000, val & 0x20, (val & 0x20) && (val & 0x10));
                    et6000_shadow_control(0xf0000, 0x10000, val & 0x40, !(val & 0x40));
                    break;

                default:
                    break;
            }
            et6000_log("ET6000: dev->regs[%02x] = %02x\n", dev->index, dev->regs[dev->index]);
            break;

        default:
            break;
    }
}

static uint8_t
et6000_read(uint16_t addr, void *priv)
{
    const et6000_t *dev = (et6000_t *) priv;

    return ((addr == 0x23) && (INDEX >= 0) && (INDEX <= 5)) ? dev->regs[INDEX] : 0xff;
}

static void
et6000_close(void *priv)
{
    et6000_t *dev = (et6000_t *) priv;

    free(dev);
}

static void *
et6000_init(UNUSED(const device_t *info))
{
    et6000_t *dev = (et6000_t *) calloc(1, sizeof(et6000_t));

    /* Port 92h */
    device_add(&port_92_device);

    /* Defaults */
    dev->regs[3] = 1;

    /* Shadow Programming */
    et6000_shadow_control(0xf0000, 0x10000, 0, 1);

    io_sethandler(0x0022, 2, et6000_read, NULL, NULL, et6000_write, NULL, NULL, dev); /* Ports 22h-23h: ETEQ Cheetah ET6000 */

    return dev;
}

const device_t et6000_device = {
    .name          = "ETEQ Cheetah ET6000",
    .internal_name = "et6000",
    .flags         = 0,
    .local         = 0,
    .init          = et6000_init,
    .close         = et6000_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

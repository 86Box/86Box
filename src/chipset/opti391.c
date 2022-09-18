/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C391/392 chipset.
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2021 Miran Grca.
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

#ifdef ENABLE_OPTI391_LOG
int opti391_do_log = ENABLE_OPTI391_LOG;

static void
opti391_log(const char *fmt, ...)
{
    va_list ap;

    if (opti391_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define opti391_log(fmt, ...)
#endif

typedef struct
{
    uint32_t phys, virt;
} mem_remapping_t;

typedef struct
{
    uint8_t index, regs[256];
} opti391_t;

static void
opti391_shadow_recalc(opti391_t *dev)
{
    uint32_t i, base;
    uint8_t  sh_enable, sh_master;
    uint8_t  sh_wp, sh_write_internal;

    shadowbios = shadowbios_write = 0;

    /* F0000-FFFFF */
    sh_enable = !(dev->regs[0x22] & 0x80);
    if (sh_enable)
        mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
    else
        mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);

    sh_write_internal = (dev->regs[0x26] & 0x40);
    /* D0000-EFFFF */
    for (i = 0; i < 8; i++) {
        base = 0xd0000 + (i << 14);
        if (base >= 0xe0000) {
            sh_master = (dev->regs[0x22] & 0x40);
            sh_wp     = (dev->regs[0x22] & 0x10);
        } else {
            sh_master = (dev->regs[0x22] & 0x20);
            sh_wp     = (dev->regs[0x22] & 0x08);
        }
        sh_enable = dev->regs[0x23] & (1 << i);

        if (sh_master) {
            if (sh_enable) {
                if (sh_wp)
                    mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                else
                    mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
            } else if (sh_write_internal)
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
            else
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        } else if (sh_write_internal)
            mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
        else
            mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }

    /* C0000-CFFFF */
    sh_master = !(dev->regs[0x26] & 0x10);
    sh_wp     = (dev->regs[0x26] & 0x20);
    for (i = 0; i < 4; i++) {
        base      = 0xc0000 + (i << 14);
        sh_enable = dev->regs[0x26] & (1 << i);

        if (sh_master) {
            if (sh_enable) {
                if (sh_wp)
                    mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                else
                    mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
            } else if (sh_write_internal)
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
            else
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        } else if (sh_write_internal)
            mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
        else
            mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }
}

static void
opti391_write(uint16_t addr, uint8_t val, void *priv)
{
    opti391_t *dev = (opti391_t *) priv;

    switch (addr) {
        case 0x22:
            dev->index = val;
            break;

        case 0x24:
            opti391_log("OPTi 391: dev->regs[%02x] = %02x\n", dev->index, val);

            switch (dev->index) {
                case 0x20:
                    dev->regs[dev->index] = (dev->regs[dev->index] & 0xc0) | (val & 0x3f);
                    break;

                case 0x21:
                case 0x24:
                case 0x25:
                case 0x27:
                case 0x28:
                case 0x29:
                case 0x2a:
                case 0x2b:
                    dev->regs[dev->index] = val;
                    break;

                case 0x22:
                case 0x23:
                case 0x26:
                    dev->regs[dev->index] = val;
                    opti391_shadow_recalc(dev);
                    break;
            }
            break;
    }
}

static uint8_t
opti391_read(uint16_t addr, void *priv)
{
    opti391_t *dev = (opti391_t *) priv;
    uint8_t    ret = 0xff;

    if (addr == 0x24)
        ret = dev->regs[dev->index];

    return ret;
}

static void
opti391_close(void *priv)
{
    opti391_t *dev = (opti391_t *) priv;

    free(dev);
}

static void *
opti391_init(const device_t *info)
{
    opti391_t *dev = (opti391_t *) malloc(sizeof(opti391_t));
    memset(dev, 0x00, sizeof(opti391_t));

    io_sethandler(0x0022, 0x0001, opti391_read, NULL, NULL, opti391_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti391_read, NULL, NULL, opti391_write, NULL, NULL, dev);

    dev->regs[0x21] = 0x84;
    dev->regs[0x24] = 0x07;
    dev->regs[0x25] = 0xf0;
    dev->regs[0x26] = 0x30;
    dev->regs[0x27] = 0x91;
    dev->regs[0x28] = 0x80;
    dev->regs[0x29] = 0x10;
    dev->regs[0x2a] = 0x80;
    dev->regs[0x2b] = 0x10;

    opti391_shadow_recalc(dev);

    return dev;
}

const device_t opti391_device = {
    .name          = "OPTi 82C391",
    .internal_name = "opti391",
    .flags         = 0,
    .local         = 0,
    .init          = opti391_init,
    .close         = opti391_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

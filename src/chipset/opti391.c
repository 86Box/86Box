/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the OPTi 82C391/392 chipset.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2021 Miran Grca.
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
#include <86box/plat_unused.h>
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

typedef struct mem_remapping_t {
    uint32_t phys;
    uint32_t virt;
} mem_remapping_t;

typedef struct opti391_t {
    uint8_t type;
    uint8_t reg_base;
    uint8_t min_reg;
    uint8_t max_reg;

    uint16_t shadowed;
    uint16_t old_start;

    uint8_t index;
    uint8_t regs[256];
} opti391_t;

static void
opti391_recalcremap(opti391_t *dev)
{
    if (dev->type < 2) {
        if ((mem_size > 8192) || (dev->shadowed & 0x0ff0) ||
            !(dev->regs[0x01] & 0x0f) || !(dev->regs[0x01] & 0x10)) {
            mem_remap_top_ex(0, dev->old_start);
            dev->old_start = 1024;
        } else {
            mem_remap_top_ex(0, dev->old_start);
            dev->old_start = (dev->regs[0x01] & 0x0f) * 1024;
            mem_remap_top_ex(-256, dev->old_start);
        }
    }
}

static void
opti391_shadow_recalc(opti391_t *dev)
{
    uint32_t base;
    uint8_t  sh_enable;
    uint8_t  sh_master;
    uint8_t  sh_wp;
    uint8_t  sh_write_internal;

    shadowbios = shadowbios_write = 0;

    /* F0000-FFFFF */
    sh_enable = (dev->regs[0x02] & 0x80);
    if (sh_enable)
        mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
    else
        mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
    dev->shadowed |= 0xf000;

    sh_write_internal = (dev->regs[0x06] & 0x40);
    /* D0000-EFFFF */
    for (uint8_t i = 0; i < 8; i++) {
        base = 0xd0000 + (i << 14);
        if (base >= 0xe0000) {
            sh_master = (dev->regs[0x02] & 0x20);
            sh_wp     = (dev->regs[0x02] & 0x08);
        } else {
            sh_master = (dev->regs[0x02] & 0x40);
            sh_wp     = (dev->regs[0x02] & 0x10);
        }
        sh_enable = dev->regs[0x03] & (1 << i);

        if (sh_master) {
            if (sh_enable) {
                if (sh_wp)
                    mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                else
                    mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                dev->shadowed |= (1 << (i + 4));
            } else if (sh_write_internal) {
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
                dev->shadowed |= (1 << (i + 4));
            } else {
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
                dev->shadowed &= ~(1 << (i + 4));
            }
        } else if (sh_write_internal) {
            mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
            dev->shadowed |= (1 << (i + 4));
        } else {
            mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
            dev->shadowed &= ~(1 << (i + 4));
        }
    }

    /* C0000-CFFFF */
    sh_master = (dev->regs[0x06] & 0x10); /* OPTi 391 datasheet erratum! */
    sh_wp     = (dev->regs[0x06] & 0x20);
    for (uint8_t i = 0; i < 4; i++) {
        base      = 0xc0000 + (i << 14);
        sh_enable = dev->regs[0x06] & (1 << i);

        if (sh_master) {
            if (sh_enable) {
                if (sh_wp)
                    mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                else
                    mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                dev->shadowed |= (1 << i);
            } else if (sh_write_internal) {
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
                dev->shadowed |= (1 << i);
            } else {
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
                dev->shadowed &= ~(1 << i);
            }
        } else if (sh_write_internal) {
            mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
            dev->shadowed |= (1 << i);
        } else {
            mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
            dev->shadowed &= ~(1 << i);
        }
    }

    opti391_recalcremap(dev);
}

static void
opti391_write(uint16_t addr, uint8_t val, void *priv)
{
    opti391_t *dev = (opti391_t *) priv;

    opti391_log("[W] %04X = %02X\n", addr, val);

    switch (addr) {
        default:
            break;

        case 0x22:
            dev->index = val;
            break;

        case 0x24:
            opti391_log("OPTi 391: dev->regs[%02x] = %02x\n", dev->index, val);

            if ((dev->index <= 0x01) && (dev->type < 2))  switch (dev->index) {
                case 0x00:
                    if (!(dev->regs[0x10] & 0x20) && (val & 0x20)) {
                        softresetx86(); /* Pulse reset! */
                        cpu_set_edx();
                        flushmmucache();
                    }
                    dev->regs[dev->index + 0x10] = val;
                    break;

                case 0x01:
                    dev->regs[dev->index + 0x10] = val;
                    reset_on_hlt = !!(val & 0x02);
                    break;
            } else  switch (dev->index - dev->reg_base) {
                default:
                    break;

                case 0x00:
                    if (dev->type == 2) {
                        reset_on_hlt = !!(val & 0x02);
                        if (!(dev->regs[dev->index - dev->reg_base] & 0x01) && (val & 0x01)) {
                            softresetx86(); /* Pulse reset! */
                            cpu_set_edx();
                            flushmmucache();
                        }
                        dev->regs[dev->index - dev->reg_base] =
                            (dev->regs[dev->index - dev->reg_base] & 0xc0) | (val & 0x3f);
                    }
                    break;

                case 0x01:
                    dev->regs[dev->index - dev->reg_base] = val;
                    if (dev->type == 2) {
                        cpu_cache_ext_enabled = !!(dev->regs[0x01] & 0x10);
                        cpu_update_waitstates();
                    } else
                        opti391_recalcremap(dev);
                    break;

                case 0x05:
                    if (dev->type == 2)
                        dev->regs[dev->index - dev->reg_base] = val & 0xf8;
                    else
                        dev->regs[dev->index - dev->reg_base] = val;
                    break;

                case 0x04:
                case 0x09:
                case 0x0a:
                case 0x0b:
                    dev->regs[dev->index - dev->reg_base] = val;
                    break;

                case 0x07:
                    dev->regs[dev->index - dev->reg_base] = val;
                    if (dev->type < 2) {
                        mem_a20_alt = val & 0x08;
                        mem_a20_recalc();
                    }
                    break;
                case 0x08:
                    if (dev->type == 2)
                        dev->regs[dev->index - dev->reg_base] = val & 0xe3;
                    else {
                        dev->regs[dev->index - dev->reg_base] = val;
                        cpu_cache_ext_enabled = !!(dev->regs[0x02] & 0x40);
                        cpu_update_waitstates();
                    }
                    break;
                case 0x0c:
                case 0x0d:
                    if (dev->type < 2)
                        dev->regs[dev->index - dev->reg_base] = val;
                    break;

                case 0x02:
                case 0x03:
                case 0x06:
                    opti391_log("Write %02X: %02X\n", dev->index - dev->reg_base, val);
                    dev->regs[dev->index - dev->reg_base] = val;
                    opti391_shadow_recalc(dev);
                    break;
            }

            dev->index = 0xff;
            break;
    }
}

static uint8_t
opti391_read(uint16_t addr, void *priv)
{
    opti391_t *dev = (opti391_t *) priv;
    uint8_t    ret = 0xff;

    if (addr == 0x24) {
        if ((dev->index <= 0x01) && (dev->type < 2))
            ret = dev->regs[dev->index + 0x10];
        else if ((dev->index >= dev->min_reg) && (dev->index <= dev->max_reg))
            ret = dev->regs[dev->index - dev->reg_base];

        dev->index = 0xff;
    }

    opti391_log("[R] %04X = %02X\n", addr, ret);

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
    opti391_t *dev = (opti391_t *) calloc(1, sizeof(opti391_t));

    io_sethandler(0x0022, 0x0001, opti391_read, NULL, NULL, opti391_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti391_read, NULL, NULL, opti391_write, NULL, NULL, dev);

    dev->type = info->local;

    if (info->local == 2) {
        dev->reg_base   = 0x20;
        dev->min_reg    = 0x20;
        dev->max_reg    = 0x2b;

        dev->regs[0x02] = 0x84;
        dev->regs[0x04] = 0x07;
        dev->regs[0x05] = 0xf0;
        dev->regs[0x06] = 0x30;
        dev->regs[0x07] = 0x91;
        dev->regs[0x08] = 0x80;
        dev->regs[0x09] = 0x10;
        dev->regs[0x0a] = 0x80;
        dev->regs[0x0b] = 0x10;
    } else {
        dev->reg_base   = 0x0f;
        dev->min_reg    = 0x10;
        dev->max_reg    = 0x1c;

        dev->regs[0x01] = 0x01;
        dev->regs[0x02] = 0xe0;
        if (info->local == 1)
            /* Guess due to no OPTi 48x datasheet. */
            dev->regs[0x04] = 0x07;
        else
            dev->regs[0x04] = 0x77;
        dev->regs[0x05] = 0x60;
        dev->regs[0x06] = 0x10;
        dev->regs[0x07] = 0x50;
        if (info->local == 1) {
            /* Guess due to no OPTi 48x datasheet. */
            dev->regs[0x09] = 0x80; /* Non-Cacheable Block 1 */
            dev->regs[0x0b] = 0x80; /* Non-Cacheable Block 2 */
            dev->regs[0x0d] = 0x91; /* Cacheable Area */
        } else {
            dev->regs[0x09] = 0xe0; /* Non-Cacheable Block 1 */
            dev->regs[0x0b] = 0x10; /* Non-Cacheable Block 2 */
            dev->regs[0x0d] = 0x80; /* Cacheable Area */
        }
        dev->regs[0x0a] = 0x10;
        dev->regs[0x0c] = 0x10;
    }

    dev->old_start = 1024;

    opti391_shadow_recalc(dev);

    return dev;
}

const device_t opti381_device = {
    .name          = "OPTi 82C381",
    .internal_name = "opti381",
    .flags         = 0,
    .local         = 0,
    .init          = opti391_init,
    .close         = opti391_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t opti481_device = {
    .name          = "OPTi 82C481",
    .internal_name = "opti481",
    .flags         = 0,
    .local         = 1,
    .init          = opti391_init,
    .close         = opti391_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t opti391_device = {
    .name          = "OPTi 82C391",
    .internal_name = "opti391",
    .flags         = 0,
    .local         = 2,
    .init          = opti391_init,
    .close         = opti391_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the VLSI VL82c480 chipset.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

typedef struct vl82c480_t {
    uint8_t idx;
    uint8_t regs[256];
} vl82c480_t;

static int
vl82c480_shflags(uint8_t access)
{
    int ret = MEM_READ_EXTANY | MEM_WRITE_EXTANY;

    switch (access) {
        default:
        case 0x00:
            ret = MEM_READ_EXTANY | MEM_WRITE_EXTANY;
            break;
        case 0x01:
            ret = MEM_READ_EXTANY | MEM_WRITE_INTERNAL;
            break;
        case 0x02:
            ret = MEM_READ_INTERNAL | MEM_WRITE_EXTANY;
            break;
        case 0x03:
            ret = MEM_READ_INTERNAL | MEM_WRITE_INTERNAL;
            break;
    }

    return ret;
}

static void
vl82c480_recalc(vl82c480_t *dev)
{
    uint32_t base;
    uint8_t  access;

    shadowbios       = 0;
    shadowbios_write = 0;

    for (uint8_t i = 0; i < 6; i++) {
        for (uint8_t j = 0; j < 8; j += 2) {
            base   = 0x000a0000 + (i << 16) + (j << 13);
            access = (dev->regs[0x0d + i] >> j) & 3;
            mem_set_mem_state(base, 0x4000, vl82c480_shflags(access));
            shadowbios |= ((base >= 0xe0000) && (access & 0x02));
            shadowbios_write |= ((base >= 0xe0000) && (access & 0x01));
        }
    }

    flushmmucache();
}

static void
vl82c480_write(uint16_t addr, uint8_t val, void *priv)
{
    vl82c480_t *dev = (vl82c480_t *) priv;

    switch (addr) {
        case 0xec:
            dev->idx = val;
            break;

        case 0xed:
            if (dev->idx >= 0x01 && dev->idx <= 0x24) {
                switch (dev->idx) {
                    default:
                        dev->regs[dev->idx] = val;
                        break;
                    case 0x04:
                        if (dev->regs[0x00] == 0x98)
                            dev->regs[dev->idx] = (dev->regs[dev->idx] & 0x08) | (val & 0xf7);
                        else
                            dev->regs[dev->idx] = val;
                        break;
                    case 0x05:
                        dev->regs[dev->idx] = (dev->regs[dev->idx] & 0x10) | (val & 0xef);
                        break;
                    case 0x07:
                        dev->regs[dev->idx] = (dev->regs[dev->idx] & 0x40) | (val & 0xbf);
                        break;
                    case 0x0d:
                    case 0x0e:
                    case 0x0f:
                    case 0x10:
                    case 0x11:
                    case 0x12:
                        dev->regs[dev->idx] = val;
                        vl82c480_recalc(dev);
                        break;
                }
            }
            break;

/* TODO: This is actually Fast A20 disable. */
#if 0
        case 0xee:
            if (mem_a20_alt)
                outb(0x92, inb(0x92) & ~2);
            break;
#endif

        default:
            break;
    }
}

static uint8_t
vl82c480_read(uint16_t addr, void *priv)
{
    const vl82c480_t *dev = (vl82c480_t *) priv;
    uint8_t           ret = 0xff;

    switch (addr) {
        case 0xec:
            ret = dev->idx;
            break;

        case 0xed:
            ret = dev->regs[dev->idx];
            break;

/* TODO: This is actually Fast A20 enable. */
#if 0
        case 0xee:
            if (!mem_a20_alt)
                outb(0x92, inb(0x92) | 2);
            break;
#endif

        case 0xef:
            softresetx86();
            cpu_set_edx();
            break;

        default:
            break;
    }

    return ret;
}

static void
vl82c480_close(void *priv)
{
    vl82c480_t *dev = (vl82c480_t *) priv;

    free(dev);
}

static void *
vl82c480_init(const device_t *info)
{
    vl82c480_t *dev = (vl82c480_t *) calloc(1, sizeof(vl82c480_t));

    dev->regs[0x00] = info->local;
    dev->regs[0x01] = 0xff;
    dev->regs[0x02] = 0x8a;
    dev->regs[0x03] = 0x88;
    dev->regs[0x06] = 0x1b;
    if (info->local == 0x98)
        dev->regs[0x07] = 0x21;
    dev->regs[0x08] = 0x38;

    io_sethandler(0x00ec, 0x0004, vl82c480_read, NULL, NULL, vl82c480_write, NULL, NULL, dev);

    device_add(&port_92_device);

    return dev;
}

const device_t vl82c480_device = {
    .name          = "VLSI VL82c480",
    .internal_name = "vl82c480",
    .flags         = 0,
    .local         = 0x90,
    .init          = vl82c480_init,
    .close         = vl82c480_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t vl82c486_device = {
    .name          = "VLSI VL82c486",
    .internal_name = "vl82c486",
    .flags         = 0,
    .local         = 0x98,
    .init          = vl82c480_init,
    .close         = vl82c480_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

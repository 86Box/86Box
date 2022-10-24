/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the SiS 85c401/85c402, 85c460, 85c461, and
 *		85c407/85c471 chipsets.
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019,2020 Miran Grca.
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
#include "x86.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/port_92.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/pic.h>
#include <86box/machine.h>
#include <86box/chipset.h>

typedef struct
{
    uint8_t cur_reg, tries,
        reg_base, reg_last,
        reg_00, is_471,
        force_flush, shadowed,
        smram_enabled, pad,
        regs[39], scratch[2];
    uint32_t   mem_state[8];
    smram_t   *smram;
    port_92_t *port_92;
} sis_85c4xx_t;

static void
sis_85c4xx_recalcremap(sis_85c4xx_t *dev)
{
    if (dev->is_471) {
        if ((mem_size > 8192) || (dev->shadowed & 0x3c) || (dev->regs[0x0b] & 0x02))
            mem_remap_top(0);
        else
            mem_remap_top(-256);
    }
}

static void
sis_85c4xx_recalcmapping(sis_85c4xx_t *dev)
{
    uint32_t base, n    = 0;
    uint32_t i, shflags = 0;
    uint32_t readext, writeext;
    uint8_t  romcs = 0xc0, cur_romcs;

    dev->shadowed = 0x00;

    shadowbios       = 0;
    shadowbios_write = 0;

    if (dev->regs[0x03] & 0x40)
        romcs |= 0x01;
    if (dev->regs[0x03] & 0x80)
        romcs |= 0x30;
    if (dev->regs[0x08] & 0x04)
        romcs |= 0x02;

    for (i = 0; i < 8; i++) {
        base      = 0xc0000 + (i << 15);
        cur_romcs = romcs & (1 << i);
        readext   = cur_romcs ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
        writeext  = cur_romcs ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;

        if ((i > 5) || (dev->regs[0x02] & (1 << i))) {
            shadowbios |= (base >= 0xe0000) && (dev->regs[0x02] & 0x80);
            shadowbios_write |= (base >= 0xe0000) && !(dev->regs[0x02] & 0x40);
            shflags = (dev->regs[0x02] & 0x80) ? MEM_READ_INTERNAL : readext;
            shflags |= (dev->regs[0x02] & 0x40) ? writeext : MEM_WRITE_INTERNAL;
            if (dev->regs[0x02] & 0x80)
                dev->shadowed |= (1 << i);
            if (!(dev->regs[0x02] & 0x40))
                dev->shadowed |= (1 << i);
            if (dev->force_flush || (dev->mem_state[i] != shflags)) {
                n++;
                mem_set_mem_state_both(base, 0x8000, shflags);
                if ((base >= 0xf0000) && (dev->mem_state[i] & MEM_READ_INTERNAL) && !(shflags & MEM_READ_INTERNAL))
                    mem_invalidate_range(base, base + 0x7fff);
                dev->mem_state[i] = shflags;
            }
        } else {
            shflags = readext | writeext;
            if (dev->force_flush || (dev->mem_state[i] != shflags)) {
                n++;
                mem_set_mem_state_both(base, 0x8000, shflags);
                dev->mem_state[i] = shflags;
            }
        }
    }

    if (dev->force_flush) {
        flushmmucache();
        dev->force_flush = 0;
    } else if (n > 0)
        flushmmucache_nopc();

    sis_85c4xx_recalcremap(dev);
}

static void
sis_85c4xx_sw_smi_out(uint16_t port, uint8_t val, void *priv)
{
    sis_85c4xx_t *dev = (sis_85c4xx_t *) priv;

    if (dev->regs[0x18] & 0x02) {
        if (dev->regs[0x0b] & 0x10)
            smi_raise();
        else
            picint(1 << ((dev->regs[0x0b] & 0x08) ? 15 : 12));
        soft_reset_mask = 1;
        dev->regs[0x19] |= 0x02;
    }
}

static void
sis_85c4xx_sw_smi_handler(sis_85c4xx_t *dev)
{
    uint16_t addr;

    if (!dev->is_471)
        return;

    addr = dev->regs[0x14] | (dev->regs[0x15] << 8);

    io_handler((dev->regs[0x0b] & 0x80) && (dev->regs[0x18] & 0x02), addr, 0x0001,
               NULL, NULL, NULL, sis_85c4xx_sw_smi_out, NULL, NULL, dev);
}

static void
sis_85c4xx_out(uint16_t port, uint8_t val, void *priv)
{
    sis_85c4xx_t *dev       = (sis_85c4xx_t *) priv;
    uint8_t       rel_reg   = dev->cur_reg - dev->reg_base;
    uint8_t       valxor    = 0x00;
    uint32_t      host_base = 0x000e0000, ram_base = 0x000a0000;

    switch (port) {
        case 0x22:
            dev->cur_reg = val;
            break;
        case 0x23:
            if ((dev->cur_reg >= dev->reg_base) && (dev->cur_reg <= dev->reg_last)) {
                valxor = val ^ dev->regs[rel_reg];
                if (rel_reg == 0x19)
                    dev->regs[rel_reg] &= ~val;
                else
                    dev->regs[rel_reg] = val;

                switch (rel_reg) {
                    case 0x01:
                        cpu_cache_ext_enabled = ((val & 0x84) == 0x84);
                        cpu_update_waitstates();
                        break;

                    case 0x02:
                    case 0x03:
                    case 0x08:
                        if (valxor)
                            sis_85c4xx_recalcmapping(dev);
                        break;

                    case 0x0b:
                        sis_85c4xx_sw_smi_handler(dev);
                        if (valxor & 0x02)
                            sis_85c4xx_recalcremap(dev);
                        break;

                    case 0x13:
                        if (dev->is_471 && (valxor & 0xf0)) {
                            smram_disable(dev->smram);
                            host_base = (val & 0x80) ? 0x00060000 : 0x000e0000;
                            switch ((val >> 5) & 0x03) {
                                case 0x00:
                                    ram_base = 0x000a0000;
                                    break;
                                case 0x01:
                                    ram_base = 0x000b0000;
                                    break;
                                case 0x02:
                                    ram_base = (val & 0x80) ? 0x00000000 : 0x000e0000;
                                    break;
                                default:
                                    ram_base = 0x00000000;
                                    break;
                            }
                            dev->smram_enabled = (ram_base != 0x00000000);
                            if (ram_base != 0x00000000)
                                smram_enable(dev->smram, host_base, ram_base, 0x00010000, (val & 0x10), 1);
                            sis_85c4xx_recalcremap(dev);
                        }
                        break;

                    case 0x14:
                    case 0x15:
                    case 0x18:
                        sis_85c4xx_sw_smi_handler(dev);
                        break;

                    case 0x1c:
                        if (dev->is_471)
                            soft_reset_mask = 0;
                        break;

                    case 0x22:
                        if (dev->is_471 && (valxor & 0x01)) {
                            port_92_remove(dev->port_92);
                            if (val & 0x01)
                                port_92_add(dev->port_92);
                        }
                        break;
                }
            } else if ((dev->reg_base == 0x60) && (dev->cur_reg == 0x00))
                dev->reg_00 = val;
            dev->cur_reg = 0x00;
            break;

        case 0xe1:
        case 0xe2:
            dev->scratch[port - 0xe1] = val;
            return;
    }
}

static uint8_t
sis_85c4xx_in(uint16_t port, void *priv)
{
    sis_85c4xx_t *dev     = (sis_85c4xx_t *) priv;
    uint8_t       rel_reg = dev->cur_reg - dev->reg_base;
    uint8_t       ret     = 0xff;

    switch (port) {
        case 0x23:
            if (dev->is_471 && (dev->cur_reg == 0x1c))
                ret = inb(0x70);
            /* On the SiS 40x, the shadow RAM read and write enable bits are write-only! */
            if ((dev->reg_base == 0x60) && (dev->cur_reg == 0x62))
                ret = dev->regs[rel_reg] & 0x3f;
            else if ((dev->cur_reg >= dev->reg_base) && (dev->cur_reg <= dev->reg_last))
                ret = dev->regs[rel_reg];
            else if ((dev->reg_base == 0x60) && (dev->cur_reg == 0x00))
                ret = dev->reg_00;
            if (dev->reg_base != 0x60)
                dev->cur_reg = 0x00;
            break;

        case 0xe1:
        case 0xe2:
            ret = dev->scratch[port - 0xe1];
    }

    return ret;
}

static void
sis_85c4xx_reset(void *priv)
{
    sis_85c4xx_t  *dev         = (sis_85c4xx_t *) priv;
    int            mem_size_mb = mem_size >> 10;
    static uint8_t ram_4xx[64] = { 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x0b, 0x00, 0x00, 0x00,
                                   0x19, 0x00, 0x06, 0x00, 0x14, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x1b, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static uint8_t ram_471[64] = { 0x00, 0x00, 0x01, 0x01, 0x02, 0x20, 0x09, 0x09, 0x04, 0x04, 0x05, 0x05, 0x0b, 0x0b, 0x0b, 0x0b,
                                   0x13, 0x21, 0x06, 0x06, 0x0d, 0x0d, 0x0d, 0x0d, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e,
                                   0x1b, 0x1b, 0x1b, 0x1b, 0x0f, 0x0f, 0x0f, 0x0f, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17,
                                   0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e };

    memset(dev->regs, 0x00, sizeof(dev->regs));

    if (cpu_s->rspeed < 25000000)
        dev->regs[0x08] = 0x80;

    if (dev->is_471) {
        dev->regs[0x09] = 0x40;
        if (mem_size_mb >= 64) {
            if ((mem_size_mb >= 65) && (mem_size_mb < 68))
                dev->regs[0x09] |= 0x22;
            else
                dev->regs[0x09] |= 0x24;
        } else
            dev->regs[0x09] |= ram_471[mem_size_mb];

        dev->regs[0x11] = 0x09;
        dev->regs[0x12] = 0xff;
        dev->regs[0x1f] = 0x20; /* Video access enabled. */
        dev->regs[0x23] = 0xf0;
        dev->regs[0x26] = 0x01;

        smram_enable(dev->smram, 0x000e0000, 0x000a0000, 0x00010000, 0, 1);

        port_92_remove(dev->port_92);

        soft_reset_mask = 0;
    } else {
        /* Bits 6 and 7 must be clear on the SiS 40x. */
        if (dev->reg_base == 0x60)
            dev->reg_00 = 0x24;

        if (mem_size_mb == 64)
            dev->regs[0x00] = 0x1f;
        else if (mem_size_mb < 64)
            dev->regs[0x00] = ram_4xx[mem_size_mb];

        dev->regs[0x11] = 0x01;
    }

    dev->scratch[0] = dev->scratch[1] = 0xff;

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    dev->force_flush = 1;
    sis_85c4xx_recalcmapping(dev);
}

static void
sis_85c4xx_close(void *priv)
{
    sis_85c4xx_t *dev = (sis_85c4xx_t *) priv;

    if (dev->is_471)
        smram_del(dev->smram);

    free(dev);
}

static void *
sis_85c4xx_init(const device_t *info)
{
    sis_85c4xx_t *dev = (sis_85c4xx_t *) malloc(sizeof(sis_85c4xx_t));
    memset(dev, 0, sizeof(sis_85c4xx_t));

    dev->is_471 = (info->local >> 8) & 0xff;

    dev->reg_base = info->local & 0xff;

    if (dev->is_471) {
        dev->reg_last = dev->reg_base + 0x76;

        dev->smram = smram_add();

        dev->port_92 = device_add(&port_92_device);
    } else
        dev->reg_last = dev->reg_base + 0x11;

    io_sethandler(0x0022, 0x0002,
                  sis_85c4xx_in, NULL, NULL, sis_85c4xx_out, NULL, NULL, dev);

    io_sethandler(0x00e1, 0x0002,
                  sis_85c4xx_in, NULL, NULL, sis_85c4xx_out, NULL, NULL, dev);

    sis_85c4xx_reset(dev);

    return dev;
}

const device_t sis_85c401_device = {
    .name          = "SiS 85c401/85c402",
    .internal_name = "sis_85c401",
    .flags         = 0,
    .local         = 0x060,
    .init          = sis_85c4xx_init,
    .close         = sis_85c4xx_close,
    .reset         = sis_85c4xx_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_85c460_device = {
    .name          = "SiS 85c460",
    .internal_name = "sis_85c460",
    .flags         = 0,
    .local         = 0x050,
    .init          = sis_85c4xx_init,
    .close         = sis_85c4xx_close,
    .reset         = sis_85c4xx_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* TODO: Log to make sure the registers are correct. */
const device_t sis_85c461_device = {
    .name          = "SiS 85c461",
    .internal_name = "sis_85c461",
    .flags         = 0,
    .local         = 0x050,
    .init          = sis_85c4xx_init,
    .close         = sis_85c4xx_close,
    .reset         = sis_85c4xx_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_85c471_device = {
    .name          = "SiS 85c407/85c471",
    .internal_name = "sis_85c471",
    .flags         = 0,
    .local         = 0x150,
    .init          = sis_85c4xx_init,
    .close         = sis_85c4xx_close,
    .reset         = sis_85c4xx_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

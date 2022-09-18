/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C611/611A VLB IDE controller.

 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *              Copyright 2020 Miran Grca.
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
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>

typedef struct
{
    uint8_t tries,
        in_cfg, cfg_locked,
        regs[19];
} opti611_t;

static void opti611_ide_handler(opti611_t *dev);

static void
opti611_cfg_write(uint16_t addr, uint8_t val, void *priv)
{
    opti611_t *dev = (opti611_t *) priv;

    addr &= 0x0007;

    switch (addr) {
        case 0x0000:
        case 0x0001:
            dev->regs[((dev->regs[0x06] & 0x01) << 4) + addr] = val;
            break;
        case 0x0002:
            dev->regs[0x12] = (val & 0xc1) | 0x02;
            if (val & 0xc0) {
                if (val & 0x40)
                    dev->cfg_locked = 1;
                dev->in_cfg = 0;
                opti611_ide_handler(dev);
            }
            break;
        case 0x0003:
            dev->regs[0x03] = (val & 0xdf);
            break;
        case 0x0005:
            dev->regs[0x05] = (dev->regs[0x05] & 0x78) | (val & 0x87);
            break;
        case 0x0006:
            dev->regs[0x06] = val;
            break;
    }
}

static void
opti611_cfg_writew(uint16_t addr, uint16_t val, void *priv)
{
    opti611_cfg_write(addr, val & 0xff, priv);
    opti611_cfg_write(addr + 1, val >> 8, priv);
}

static void
opti611_cfg_writel(uint16_t addr, uint32_t val, void *priv)
{
    opti611_cfg_writew(addr, val & 0xffff, priv);
    opti611_cfg_writew(addr + 2, val >> 16, priv);
}

static uint8_t
opti611_cfg_read(uint16_t addr, void *priv)
{
    uint8_t    ret = 0xff;
    opti611_t *dev = (opti611_t *) priv;

    addr &= 0x0007;

    switch (addr) {
        case 0x0000:
        case 0x0001:
            ret = dev->regs[((dev->regs[0x06] & 0x01) << 4) + addr];
            break;
        case 0x0002:
            ret = ((!!in_smm) << 7);
            if (ret & 0x80)
                ret |= (dev->regs[addr] & 0x7f);
            break;
        case 0x0003:
        case 0x0004:
        case 0x0005:
        case 0x0006:
            ret = dev->regs[addr];
            break;
    }

    return ret;
}

static uint16_t
opti611_cfg_readw(uint16_t addr, void *priv)
{
    uint16_t ret = 0xffff;

    ret = opti611_cfg_read(addr, priv);
    ret |= (opti611_cfg_read(addr + 1, priv) << 8);

    return ret;
}

static uint32_t
opti611_cfg_readl(uint16_t addr, void *priv)
{
    uint32_t ret = 0xffffffff;

    ret = opti611_cfg_readw(addr, priv);
    ret |= (opti611_cfg_readw(addr + 2, priv) << 16);

    return ret;
}

static void
opti611_ide_write(uint16_t addr, uint8_t val, void *priv)
{
    opti611_t *dev = (opti611_t *) priv;

    uint8_t smia9 = (!!(addr & 0x0200)) << 5;
    uint8_t smia2 = (!!(addr & 0x0004)) << 4;
    uint8_t smibe = (addr & 0x0003);

    if (dev->regs[0x03] & 0x02) {
        smi_raise();
        dev->regs[0x02] = smia9 | smia2 | smibe;
        dev->regs[0x04] = val;
    }
}

static void
opti611_ide_writew(uint16_t addr, uint16_t val, void *priv)
{
    opti611_t *dev = (opti611_t *) priv;

    uint8_t smia9 = (!!(addr & 0x0200)) << 5;
    uint8_t smia2 = (!!(addr & 0x0004)) << 4;
    uint8_t smibe = (addr & 0x0002) | 0x0001;

    if (dev->regs[0x03] & 0x02) {
        smi_raise();
        dev->regs[0x02] = smia9 | smia2 | smibe;
        dev->regs[0x04] = 0x00;
    }
}

static void
opti611_ide_writel(uint16_t addr, uint32_t val, void *priv)
{
    opti611_t *dev = (opti611_t *) priv;

    uint8_t smia9 = (!!(addr & 0x0200)) << 5;
    uint8_t smia2 = (!!(addr & 0x0004)) << 4;

    if (dev->regs[0x03] & 0x02) {
        smi_raise();
        dev->regs[0x02] = smia9 | smia2 | 0x0003;
        dev->regs[0x04] = 0x00;
    }
}

static uint8_t
opti611_ide_read(uint16_t addr, void *priv)
{
    opti611_t *dev = (opti611_t *) priv;

    uint8_t smia9 = (!!(addr & 0x0200)) << 5;
    uint8_t smia2 = (!!(addr & 0x0004)) << 4;
    uint8_t smibe = (addr & 0x0003);

    if (dev->regs[0x03] & 0x02) {
        smi_raise();
        dev->regs[0x02] = smia9 | smia2 | smibe;
        dev->regs[0x04] = 0x00;
    }

    return 0xff;
}

static uint16_t
opti611_ide_readw(uint16_t addr, void *priv)
{
    opti611_t *dev = (opti611_t *) priv;

    uint8_t smia9 = (!!(addr & 0x0200)) << 5;
    uint8_t smia2 = (!!(addr & 0x0004)) << 4;
    uint8_t smibe = (addr & 0x0002) | 0x0001;

    if ((addr & 0x0007) == 0x0001) {
        dev->tries = (dev->tries + 1) & 0x01;
        if ((dev->tries == 0x00) && !dev->cfg_locked) {
            dev->in_cfg = 1;
            opti611_ide_handler(dev);
        }
    }

    if (dev->regs[0x03] & 0x02) {
        smi_raise();
        dev->regs[0x02] = smia9 | smia2 | smibe;
        dev->regs[0x04] = 0x00;
    }

    return 0xffff;
}

static uint32_t
opti611_ide_readl(uint16_t addr, void *priv)
{
    opti611_t *dev = (opti611_t *) priv;

    uint8_t smia9 = (!!(addr & 0x0200)) << 5;
    uint8_t smia2 = (!!(addr & 0x0004)) << 4;

    if (dev->regs[0x03] & 0x02) {
        smi_raise();
        dev->regs[0x02] = smia9 | smia2 | 0x0003;
        dev->regs[0x04] = 0x00;
    }

    return 0xffffffff;
}

static void
opti611_ide_handler(opti611_t *dev)
{
    ide_pri_disable();
    io_removehandler(0x01f0, 0x0007,
                     opti611_ide_read, opti611_ide_readw, opti611_ide_readl,
                     opti611_ide_write, opti611_ide_writew, opti611_ide_writel,
                     dev);
    io_removehandler(0x01f0, 0x0007,
                     opti611_cfg_read, opti611_cfg_readw, opti611_cfg_readl,
                     opti611_cfg_write, opti611_cfg_writew, opti611_cfg_writel,
                     dev);

    if (dev->in_cfg && !dev->cfg_locked) {
        io_sethandler(0x01f0, 0x0007,
                      opti611_cfg_read, opti611_cfg_readw, opti611_cfg_readl,
                      opti611_cfg_write, opti611_cfg_writew, opti611_cfg_writel,
                      dev);
    } else {
        if (dev->regs[0x03] & 0x01)
            ide_pri_enable();
        io_sethandler(0x01f0, 0x0007,
                      opti611_ide_read, opti611_ide_readw, opti611_ide_readl,
                      opti611_ide_write, opti611_ide_writew, opti611_ide_writel,
                      dev);
    }
}

static void
opti611_close(void *priv)
{
    opti611_t *dev = (opti611_t *) priv;

    free(dev);
}

static void *
opti611_init(const device_t *info)
{
    opti611_t *dev = (opti611_t *) malloc(sizeof(opti611_t));
    memset(dev, 0, sizeof(opti611_t));

    dev->regs[0x12] = 0x80;
    dev->regs[0x03] = 0x01;
    dev->regs[0x05] = 0x20;

    device_add(&ide_vlb_device);

    opti611_ide_handler(dev);

    return dev;
}

const device_t ide_opti611_vlb_device = {
    .name          = "OPTi 82C611/82C611A VLB",
    .internal_name = "ide_opti611_vlb",
    .flags         = DEVICE_VLB,
    .local         = 0,
    .init          = opti611_init,
    .close         = opti611_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the UMC UMF8673F IDE controller.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2024 Miran Grca.
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

#include <86box/hdc_ide.h>
#include <86box/hdc.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/smram.h>

#include <86box/chipset.h>

#ifdef ENABLE_UM8673F_LOG
int um8673f_do_log = ENABLE_UM8673F_LOG;

static void
um8673f_log(const char *fmt, ...)
{
    va_list ap;

    if (um8673f_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define um8673f_log(fmt, ...)
#endif

typedef struct um8673f_t {
    uint8_t index;
    uint8_t tries;
    uint8_t unlocked;

    uint8_t regs[256];
} um8673f_t;

static void
um8673f_ide_handler(um8673f_t *dev)
{
    ide_pri_disable();
    ide_sec_disable();
    if (dev->regs[0xb0] & 0x80)
        ide_pri_enable();
    if (dev->regs[0xb0] & 0x40)
        ide_sec_enable();
}

static void
um8673f_write(uint16_t addr, uint8_t val, void *priv)
{
    um8673f_t *dev = (um8673f_t *) priv;

    um8673f_log("[%04X:%08X] [W] %02X = %02X (%i)\n", CS, cpu_state.pc, addr, val, dev->tries);

    switch (addr) {
        case 0x108:
            if (dev->unlocked) {
                if (dev->index == 0x34) {
                    dev->unlocked = 0;
                    dev->tries = 0;
                } else
                    dev->index = val;
            } else if (((dev->tries == 0) && (val == 0x4a)) ||
                ((dev->tries == 1) && (val == 0x6c))) {
                dev->tries++;
                if (dev->tries == 2)
                    dev->unlocked = 1;
            } else
                dev->tries = 0;
            break;

        case 0x109:
            switch (dev->index) {
                case 0xb0:
                    dev->regs[dev->index] = val;
                    um8673f_ide_handler(dev);
                    break;
                case 0xb1 ... 0xb8:
                    dev->regs[dev->index] = val;
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static uint8_t
um8673f_read(uint16_t addr, void *priv)
{
    um8673f_t *dev = (um8673f_t *) priv;
    uint8_t    ret = 0xff;

    switch (addr) {
        case 0x108:
            if (dev->unlocked)
                ret = dev->index;
            else
                dev->tries = 0;
            break;
        case 0x109:
            if ((dev->index >= 0xb0) && (dev->index <= 0xb8))
                ret = dev->regs[dev->index];
            break;

        default:
            break;
    }

    um8673f_log("[%04X:%08X] [R] %02X = %02X\n", CS, cpu_state.pc, addr, ret);

    return ret;
}

static void
um8673f_reset(void *priv)
{
    um8673f_t *dev = (um8673f_t *) priv;

    memset(dev->regs, 0x00, 256);

    ide_pri_disable();
    ide_sec_disable();

    /* IDE registers */
    dev->regs[0xb0] = 0xc0;

    um8673f_ide_handler(dev);
}

static void
um8673f_close(void *priv)
{
    um8673f_t *dev = (um8673f_t *) priv;

    free(dev);
}

static void *
um8673f_init(UNUSED(const device_t *info))
{
    um8673f_t *dev = (um8673f_t *) calloc(1, sizeof(um8673f_t));

    io_sethandler(0x0108, 0x0002, um8673f_read, NULL, NULL, um8673f_write, NULL, NULL, dev);

    device_add(info->local ? &ide_pci_2ch_device : &ide_vlb_2ch_device);

    um8673f_reset(dev);

    return dev;
}

const device_t ide_um8886af_device = {
    .name          = "UMC UM8886F IDE",
    .internal_name = "um8886af_ide",
    .flags         = 0,
    .local         = 1,
    .init          = um8673f_init,
    .close         = um8673f_close,
    .reset         = um8673f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_um8673f_device = {
    .name          = "UMC UM8673F",
    .internal_name = "um8673f",
    .flags         = 0,
    .local         = 0,
    .init          = um8673f_init,
    .close         = um8673f_close,
    .reset         = um8673f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

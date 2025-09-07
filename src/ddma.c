/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Distributed DMA emulation.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/keyboard.h>
#include <86box/nvr.h>
#include <86box/pit.h>
#include <86box/dma.h>
#include <86box/ddma.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_DDMA_LOG
int ddma_do_log = ENABLE_DDMA_LOG;

static void
ddma_log(const char *fmt, ...)
{
    va_list ap;

    if (ddma_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ddma_log(fmt, ...)
#endif

static uint8_t
ddma_reg_read(uint16_t addr, void *priv)
{
    const ddma_channel_t *dev  = (ddma_channel_t *) priv;
    uint8_t               ret  = 0xff;
    int                   ch   = dev->channel;
    uint8_t               dmab = (ch >= 4) ? 0xc0 : 0x00;

    switch (addr & 0x0f) {
        case 0x00:
            ret = dma[ch].ac & 0xff;
            break;
        case 0x01:
            ret = (dma[ch].ac >> 8) & 0xff;
            break;
        case 0x02:
            ret = dma[ch].page;
            break;
        case 0x04:
            ret = dma[ch].cc & 0xff;
            break;
        case 0x05:
            ret = (dma[ch].cc >> 8) & 0xff;
            break;
        case 0x09:
            ret = inb(dmab + 0x08);
            break;

        default:
            break;
    }

    return ret;
}

static void
ddma_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    const ddma_channel_t *dev          = (ddma_channel_t *) priv;
    int                   ch           = dev->channel;
    uint8_t               page_regs[4] = { 7, 3, 1, 2 };
    uint8_t               dmab = (ch >= 4) ? 0xc0 : 0x00;

    switch (addr & 0x0f) {
        case 0x00:
            dma[ch].ab = (dma[ch].ab & 0xffff00) | val;
            dma[ch].ac = dma[ch].ab;
            break;
        case 0x01:
            dma[ch].ab = (dma[ch].ab & 0xff00ff) | (val << 8);
            dma[ch].ac = dma[ch].ab;
            break;
        case 0x02:
            if (ch >= 4)
                outb(0x88 + page_regs[ch & 3], val);
            else
                outb(0x80 + page_regs[ch & 3], val);
            break;
        case 0x04:
            dma[ch].cb = (dma[ch].cb & 0xffff00) | val;
            dma[ch].cc = dma[ch].cb;
            break;
        case 0x05:
            dma[ch].cb = (dma[ch].cb & 0xff00ff) | (val << 8);
            dma[ch].cc = dma[ch].cb;
            break;
        case 0x08:
            outb(dmab + 0x08, val);
            break;
        case 0x09:
            outb(dmab + 0x09, val);
            break;
        case 0x0a:
            outb(dmab + 0x0a, val);
            break;
        case 0x0b:
            outb(dmab + 0x0b, val);
            break;
        case 0x0d:
            outb(dmab + 0x0d, val);
            break;
        case 0x0e:
            for (uint8_t i = 0; i < 4; i++)
                outb(dmab + 0x0a, i);
            break;
        case 0x0f:
            outb(dmab + 0x0a, (val << 2) | (ch & 3));
            break;

        default:
            break;
    }
}

void
ddma_update_io_mapping(ddma_t *dev, int ch, uint8_t base_l, uint8_t base_h, int enable)
{
    if (dev->channels[ch].enable && (dev->channels[ch].io_base != 0x0000))
        io_removehandler(dev->channels[ch].io_base, 0x10, ddma_reg_read, NULL, NULL, ddma_reg_write, NULL, NULL, &dev->channels[ch]);

    dev->channels[ch].io_base = base_l | (base_h << 8);
    dev->channels[ch].enable  = enable;

    if (dev->channels[ch].enable && (dev->channels[ch].io_base != 0x0000))
        io_sethandler(dev->channels[ch].io_base, 0x10, ddma_reg_read, NULL, NULL, ddma_reg_write, NULL, NULL, &dev->channels[ch]);
}

static void
ddma_close(void *priv)
{
    ddma_t *dev = (ddma_t *) priv;

    free(dev);
}

static void *
ddma_init(UNUSED(const device_t *info))
{
    ddma_t *dev;

    dev = (ddma_t *) malloc(sizeof(ddma_t));
    if (dev == NULL)
        return (NULL);
    memset(dev, 0x00, sizeof(ddma_t));

    for (uint8_t i = 0; i < 8; i++)
        dev->channels[i].channel = i;

    return dev;
}

const device_t ddma_device = {
    .name          = "Distributed DMA",
    .internal_name = "ddma",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = ddma_init,
    .close         = ddma_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

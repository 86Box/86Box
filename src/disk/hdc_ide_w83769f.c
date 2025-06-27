/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Winbond W83769F controller.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020-2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/scsi_device.h>
#include <86box/scsi_cdrom.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/zip.h>
#include <86box/mo.h>

typedef struct w83769f_t {
    uint8_t  vlb_idx;
    uint8_t  id;
    uint8_t  in_cfg;
    uint8_t  channels;
    uint8_t  pci;
    uint8_t  pci_slot;
    uint8_t  pad;
    uint8_t  pad0;
    uint8_t  regs[256];
} w83769f_t;

static int next_id = 0;

#ifdef ENABLE_W83769F_LOG
int w83769f_do_log = ENABLE_W83769F_LOG;

static void
w83769f_log(const char *fmt, ...)
{
    va_list ap;

    if (w83769f_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define w83769f_log(fmt, ...)
#endif

void
w83769f_set_irq_0(uint8_t status, void *priv)
{
    w83769f_t *dev = (w83769f_t *) priv;
    int        irq = !!(status & 0x04);

    if (!(dev->regs[0x50] & 0x04) || (status & 0x04))
        dev->regs[0x50] = (dev->regs[0x50] & ~0x04) | status;

    if (!(dev->channels & 1))
        return;

    if (irq)
        picint(1 << 14);
    else
       picintc(1 << 14);
}

void
w83769f_set_irq_1(uint8_t status, void *priv)
{
    w83769f_t *dev = (w83769f_t *) priv;
    int        irq = !!(status & 0x04);

    if (!(dev->regs[0x50] & 0x04) || (status & 0x04))
        dev->regs[0x50] = (dev->regs[0x50] & ~0x04) | status;

    if (!(dev->channels & 2))
        return;

    if (irq)
        picint(1 << 15);
    else
       picintc(1 << 15);
}

static void
w83769f_ide_handlers(w83769f_t *dev)
{
    if (dev->channels & 0x01) {
        ide_pri_disable();

        if (!dev->pci || (dev->regs[0x04] & 0x01))
            ide_pri_enable();
    }

    if (dev->channels & 0x02) {
        ide_sec_disable();

        if ((!dev->pci || (dev->regs[0x04] & 0x01)) && (dev->regs[0x57] & 0x01))
            ide_sec_enable();
    }
}

static void
w83769f_common_write(int addr, uint8_t val, w83769f_t *dev)
{
    switch (addr) {
        case 0x50:
        case 0x57:
            dev->regs[0x57] = val & 0x01;
            w83769f_ide_handlers(dev);
            break;
        case 0x51:
            dev->regs[addr] = val & 0x7f;
            break;
        case 0x52:
        case 0x54:
        case 0x56:
        case 0x58 ... 0x59:
            dev->regs[addr] = val;
            break;
        case 0x53:
        case 0x55:
            dev->regs[addr] = val & 0xcf;
            break;

        default:
            break;
    }
}

static void
w83769f_vlb_write(uint16_t addr, uint8_t val, void *priv)
{
    w83769f_t *dev = (w83769f_t *) priv;

    switch (addr) {
        case 0x0034:
        case 0x00b4:
            dev->vlb_idx = val;
            break;
        case 0x0038:
        case 0x00b8:
            w83769f_common_write(dev->vlb_idx, val, dev);
            break;

        default:
            break;
    }
}

static void
w83769f_vlb_writew(uint16_t addr, uint16_t val, void *priv)
{
    w83769f_vlb_write(addr, val & 0xff, priv);
    w83769f_vlb_write(addr + 1, val >> 8, priv);
}

static void
w83769f_vlb_writel(uint16_t addr, uint32_t val, void *priv)
{
    w83769f_vlb_writew(addr, val & 0xffff, priv);
    w83769f_vlb_writew(addr + 2, val >> 16, priv);
}

static uint8_t
w83769f_vlb_read(uint16_t addr, void *priv)
{
    uint8_t    ret = 0xff;
    w83769f_t *dev = (w83769f_t *) priv;

    switch (addr) {
        case 0x0034:
        case 0x00b4:
            ret = dev->vlb_idx;
            break;
        case 0x0038:
        case 0x00b8:
            ret = dev->regs[dev->vlb_idx];
            if (dev->vlb_idx == 0x50)
                dev->regs[0x50] &= ~0x04;
            break;

        default:
            break;
    }

    return ret;
}

static uint16_t
w83769f_vlb_readw(uint16_t addr, void *priv)
{
    uint16_t ret = 0xffff;

    ret = w83769f_vlb_read(addr, priv);
    ret |= (w83769f_vlb_read(addr + 1, priv) << 8);

    return ret;
}

static uint32_t
w83769f_vlb_readl(uint16_t addr, void *priv)
{
    uint32_t ret = 0xffffffff;

    ret = w83769f_vlb_readw(addr, priv);
    ret |= (w83769f_vlb_readw(addr + 2, priv) << 16);

    return ret;
}

static void
w83769f_pci_write(int func, int addr, uint8_t val, void *priv)
{
    w83769f_t *dev = (w83769f_t *) priv;

    w83769f_log("w83769f_pci_write(%i, %02X, %02X)\n", func, addr, val);

    if (func == 0x00)
        switch (addr) {
            case 0x04:
                dev->regs[addr] = (dev->regs[addr] & 0xbf) | (val & 0x40);
                w83769f_ide_handlers(dev);
                break;
            case 0x07:
                dev->regs[addr] &= ~(val & 0x80);
                break;
        }
}

static uint8_t
w83769f_pci_read(int func, int addr, void *priv)
{
    w83769f_t *dev = (w83769f_t *) priv;
    uint8_t    ret = 0xff;

    if (func == 0x00)
        ret = dev->regs[addr];

    w83769f_log("w83769f_pci_read(%i, %02X, %02X)\n", func, addr, ret);

    return ret;
}

static void
w83769f_reset(void *priv)
{
    w83769f_t *dev = (w83769f_t *) priv;
    int        i   = 0;
    int        min_channel;
    int        max_channel;

    switch (dev->channels) {
        default:
        case 0x00:
            min_channel = max_channel = 0;
            break;
        case 0x01:
            min_channel = 0;
            max_channel = 1;
            break;
        case 0x02:
            min_channel = 2;
            max_channel = 3;
            break;
        case 0x03:
            min_channel = 0;
            max_channel = 3;
            break;
    }

    for (i = 0; i < CDROM_NUM; i++) {
        if ((cdrom[i].bus_type == CDROM_BUS_ATAPI) && (cdrom[i].ide_channel >= min_channel) &&
            (cdrom[i].ide_channel <= max_channel) && cdrom[i].priv)
            scsi_cdrom_reset((scsi_common_t *) cdrom[i].priv);
    }
    for (i = 0; i < ZIP_NUM; i++) {
        if ((zip_drives[i].bus_type == ZIP_BUS_ATAPI) && (zip_drives[i].ide_channel >= min_channel) &&
            (zip_drives[i].ide_channel <= max_channel) && zip_drives[i].priv)
            zip_reset((scsi_common_t *) zip_drives[i].priv);
    }
    for (i = 0; i < MO_NUM; i++) {
        if ((mo_drives[i].bus_type == MO_BUS_ATAPI) && (mo_drives[i].ide_channel >= min_channel) &&
            (mo_drives[i].ide_channel <= max_channel) && mo_drives[i].priv)
            mo_reset((scsi_common_t *) mo_drives[i].priv);
    }

    if (dev->channels & 0x01)
        w83769f_set_irq_0(0x00, priv);

    if (dev->channels & 0x02)
        w83769f_set_irq_1(0x00, priv);

    memset(dev->regs, 0x00, sizeof(dev->regs));

    dev->regs[0x50] = (dev->id << 3); /* Device ID: 00 = 60h, 01 = 61h, 10 = 62h, 11 = 63h */
    dev->regs[0x51] = 0x40;
    dev->regs[0x57] = 0x01;           /* Required by the MSI MS-5109 */
    dev->regs[0x59] = 0x40;

    if (dev->pci) {
        dev->regs[0x00] = 0xad;       /* Winbond */
        dev->regs[0x01] = 0x10;
        dev->regs[0x02] = 0x01;       /* W83769 */
        dev->regs[0x03] = 0x00;
        dev->regs[0x04] = 0x01;
        dev->regs[0x07] = 0x02;       /* DEVSEL timing: 01 medium */
        dev->regs[0x08] = 0x02;       /* 00h for Rev BB, 02h for Rev A3C */
        dev->regs[0x09] = 0x00;       /* Programming interface */
        dev->regs[0x0a] = 0x01;       /* IDE controller */
        dev->regs[0x0b] = 0x01;       /* Mass storage controller */
        dev->regs[0x3c] = 0x0e;       /* IRQ 14 */
        dev->regs[0x3d] = 0x01;       /* INTA */
    } else
        dev->regs[0x04] = 0x01;       /* To make sure the two channels get enabled. */

    w83769f_ide_handlers(dev);
}

static void
w83769f_close(void *priv)
{
    w83769f_t *dev = (w83769f_t *) priv;

    free(dev);

    next_id = 0;
}

static void *
w83769f_init(const device_t *info)
{
    w83769f_t *dev = (w83769f_t *) calloc(1, sizeof(w83769f_t));

    dev->id = next_id | 0x60;

    dev->pci   = !!(info->flags & DEVICE_PCI);

    dev->channels = ((info->local & 0x60000) >> 17) & 0x03;

    if (info->flags & DEVICE_PCI) {
        device_add(&ide_pci_2ch_device);

        if (info->local & 0x80000)
            pci_add_card(PCI_ADD_NORMAL, w83769f_pci_read, w83769f_pci_write, dev, &dev->pci_slot);
        else
            pci_add_card(PCI_ADD_IDE, w83769f_pci_read, w83769f_pci_write, dev, &dev->pci_slot);
    } else if (info->flags & DEVICE_VLB)
        device_add(&ide_vlb_2ch_device);

    if (dev->channels & 0x01)
        ide_set_bus_master(0, NULL, w83769f_set_irq_0, dev);

    if (dev->channels & 0x02)
        ide_set_bus_master(1, NULL, w83769f_set_irq_1, dev);

    /* The CMD PCI-0640B IDE controller has no DMA capability,
       so set our devices IDE devices to force ATA-3 (no DMA). */
    if (dev->channels & 0x01)
        ide_board_set_force_ata3(0, 1);

    if (dev->channels & 0x02)
        ide_board_set_force_ata3(1, 1);

    io_sethandler(info->local & 0xffff, 0x0001,
                  w83769f_vlb_read, w83769f_vlb_readw, w83769f_vlb_readl,
                  w83769f_vlb_write, w83769f_vlb_writew, w83769f_vlb_writel,
                  dev);
    io_sethandler((info->local & 0xffff) + 0x0004, 0x0001,
                  w83769f_vlb_read, w83769f_vlb_readw, w83769f_vlb_readl,
                  w83769f_vlb_write, w83769f_vlb_writew, w83769f_vlb_writel,
                  dev);

    next_id++;

    w83769f_reset(dev);

    return dev;
}

const device_t ide_w83769f_vlb_device = {
    .name          = "Winbond W83769F VLB",
    .internal_name = "ide_w83769f_vlb",
    .flags         = DEVICE_VLB,
    .local         = 0x600b4,
    .init          = w83769f_init,
    .close         = w83769f_close,
    .reset         = w83769f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_w83769f_vlb_34_device = {
    .name          = "Winbond W83769F VLB (Port 34h)",
    .internal_name = "ide_w83769f_vlb_34",
    .flags         = DEVICE_VLB,
    .local         = 0x60034,
    .init          = w83769f_init,
    .close         = w83769f_close,
    .reset         = w83769f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_w83769f_pci_device = {
    .name          = "Winbond W83769F PCI",
    .internal_name = "ide_w83769f_pci",
    .flags         = DEVICE_PCI,
    .local         = 0x600b4,
    .init          = w83769f_init,
    .close         = w83769f_close,
    .reset         = w83769f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_w83769f_pci_34_device = {
    .name          = "Winbond W83769F PCI (Port 34h)",
    .internal_name = "ide_w83769f_pci_34",
    .flags         = DEVICE_PCI,
    .local         = 0x60034,
    .init          = w83769f_init,
    .close         = w83769f_close,
    .reset         = w83769f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_w83769f_pci_single_channel_device = {
    .name          = "Winbond W83769F PCI (Single Channel)",
    .internal_name = "ide_w83769f_pci_single_channel",
    .flags         = DEVICE_PCI,
    .local         = 0x200b4,
    .init          = w83769f_init,
    .close         = w83769f_close,
    .reset         = w83769f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

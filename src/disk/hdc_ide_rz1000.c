/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the PC Technology RZ-1000 controller.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Miran Grca.
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
#include <86box/rdisk.h>
#include <86box/mo.h>

typedef struct rz1000_t {
    uint8_t  vlb_idx;
    uint8_t  id;
    uint8_t  in_cfg;
    uint8_t  channels;
    uint8_t  pci;
    uint8_t  irq_state;
    uint8_t  pci_slot;
    uint8_t  pad0;
    uint8_t  regs[256];
    uint32_t local;
    int      irq_mode[2];
    int      irq_pin;
    int      irq_line;
    uint8_t  type;
} rz1000_t;

static int next_id = 0;

#ifdef ENABLE_RZ1000_LOG
int rz1000_do_log = ENABLE_RZ1000_LOG;

static void
rz1000_log(const char *fmt, ...)
{
    va_list ap;

    if (rz1000_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define rz1000_log(fmt, ...)
#endif

static void
rz1000_ide_handlers(rz1000_t *dev)
{
    uint16_t main;
    uint16_t side;

    if (dev->channels & 0x01) {
        ide_pri_disable();

        main = 0x1f0;
        side = 0x3f6;

        ide_set_base(0, main);
        ide_set_side(0, side);

        if (dev->regs[0x04] & 0x01)
            ide_pri_enable();
    }

    if (dev->channels & 0x02) {
        ide_sec_disable();

        main = 0x170;
        side = 0x376;

        ide_set_base(1, main);
        ide_set_side(1, side);

        if (dev->regs[0x04] & 0x01)
            ide_sec_enable();
    }
}

static void
rz1000_pci_write(int func, int addr, uint8_t val, void *priv)
{
    rz1000_t *dev = (rz1000_t *) priv;

    rz1000_log("rz1000_pci_write(%i, %02X, %02X)\n", func, addr, val);

    if (func == 0x00)
        switch (addr) {
            case 0x04:
                dev->regs[addr] = (val & 0x41);
                rz1000_ide_handlers(dev);
                break;
            case 0x07:
                dev->regs[addr] &= ~(val & 0x80);
                break;
            case 0x09:
                if ((dev->regs[addr] & 0x0a) == 0x0a) {
                    dev->regs[addr]  = (dev->regs[addr] & 0x0a) | (val & 0x05);
                    dev->irq_mode[0] = !!(val & 0x01);
                    dev->irq_mode[1] = !!(val & 0x04);
                    rz1000_ide_handlers(dev);
                }
                break;
            case 0x40 ... 0x4f:
                dev->regs[addr] = val;
                break;
        }
}

static uint8_t
rz1000_pci_read(int func, int addr, void *priv)
{
    rz1000_t *dev = (rz1000_t *) priv;
    uint8_t   ret = 0xff;

    if (func == 0x00)
        ret = dev->regs[addr];

    rz1000_log("rz1000_pci_read(%i, %02X, %02X)\n", func, addr, ret);

    return ret;
}

static void
rz1000_reset(void *priv)
{
    rz1000_t *dev = (rz1000_t *) priv;
    int       i   = 0;
    int       min_channel;
    int       max_channel;

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
    for (i = 0; i < RDISK_NUM; i++) {
        if ((rdisk_drives[i].bus_type == RDISK_BUS_ATAPI) && (rdisk_drives[i].ide_channel >= min_channel) &&
            (rdisk_drives[i].ide_channel <= max_channel) && rdisk_drives[i].priv)
            rdisk_reset((scsi_common_t *) rdisk_drives[i].priv);
    }
    for (i = 0; i < MO_NUM; i++) {
        if ((mo_drives[i].bus_type == MO_BUS_ATAPI) && (mo_drives[i].ide_channel >= min_channel) &&
            (mo_drives[i].ide_channel <= max_channel) && mo_drives[i].priv)
            mo_reset((scsi_common_t *) mo_drives[i].priv);
    }

    memset(dev->regs, 0x00, sizeof(dev->regs));

    rz1000_log("dev->local = %08X\n", dev->local);

    dev->type = ((dev->local >> 8) & 0x01);
    rz1000_log("dev->type = %04X\n", dev->type);

    dev->regs[0x00] = 0x42;       /* PC Technology */
    dev->regs[0x01] = 0x10;
    dev->regs[0x02] = dev->type;  /* RZ-1000/RZ-1001 */
    dev->regs[0x03] = 0x10;
    dev->regs[0x04] = 0x00;
    dev->regs[0x07] = 0x02;       /* DEVSEL timing: 01 medium */
    dev->regs[0x08] = 0x02;       /* Revision 02 */
    dev->regs[0x09] = dev->local; /* Programming interface */
    dev->regs[0x0a] = 0x01;       /* IDE controller */
    dev->regs[0x0b] = 0x01;       /* Mass storage controller */

    dev->regs[0x10] = 0xf1;
    dev->regs[0x11] = 0x01;
    dev->regs[0x14] = 0xf5;
    dev->regs[0x15] = 0x03;
    dev->regs[0x18] = 0x71;
    dev->regs[0x19] = 0x01;
    dev->regs[0x1c] = 0x75;
    dev->regs[0x1d] = 0x03;

    dev->irq_mode[0] = dev->irq_mode[1] = 0;
    dev->irq_pin                        = PCI_INTA;
    dev->irq_line                       = 14;

    rz1000_ide_handlers(dev);
}

static void
rz1000_close(void *priv)
{
    rz1000_t *dev = (rz1000_t *) priv;

    free(dev);

    next_id = 0;
}

static void *
rz1000_init(const device_t *info)
{
    rz1000_t *dev = (rz1000_t *) calloc(1, sizeof(rz1000_t));

    dev->id = next_id | 0x60;

    dev->pci   = !!(info->flags & DEVICE_PCI);
    dev->local = info->local;

    dev->channels = ((info->local & 0x60000) >> 17) & 0x03;

    if (dev->channels & 0x02)
        device_add(&ide_pci_2ch_device);
    else
        device_add(&ide_pci_device);

    if (info->local & 0x80000)
        pci_add_card(PCI_ADD_NORMAL, rz1000_pci_read, rz1000_pci_write, dev, &dev->pci_slot);
    else
        pci_add_card(PCI_ADD_IDE, rz1000_pci_read, rz1000_pci_write, dev, &dev->pci_slot);

    if (dev->channels & 0x01)
        ide_board_set_force_ata3(0, 1);

    if (dev->channels & 0x02)
        ide_board_set_force_ata3(1, 1);

    next_id++;

    rz1000_reset(dev);

    return dev;
}

const device_t ide_rz1000_pci_device = {
    .name          = "PC Technology RZ-1000 PCI",
    .internal_name = "ide_rz1000_pci",
    .flags         = DEVICE_PCI,
    .local         = 0x60000,
    .init          = rz1000_init,
    .close         = rz1000_close,
    .reset         = rz1000_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_rz1000_pci_single_channel_device = {
    .name          = "PC Technology RZ-1000 PCI (Single Channel)",
    .internal_name = "ide_rz1000_pci_single_channel",
    .flags         = DEVICE_PCI,
    .local         = 0x20000,
    .init          = rz1000_init,
    .close         = rz1000_close,
    .reset         = rz1000_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_rz1001_pci_device = {
    .name          = "PC Technology RZ-1001 PCI",
    .internal_name = "ide_rz1001_pci",
    .flags         = DEVICE_PCI,
    .local         = 0x60100,
    .init          = rz1000_init,
    .close         = rz1000_close,
    .reset         = rz1000_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

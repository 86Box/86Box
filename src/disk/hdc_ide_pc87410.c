/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the National Semiconductor PC87410 IDE controller.
 *
 * Authors: win2kgamer
 *
 *          Copyright 2026 win2kgamer.
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
#include <86box/plat_unused.h>
#include <86box/timer.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/rdisk.h>
#include <86box/mo.h>
#include <86box/log.h>

typedef struct pc87410_t {
    uint8_t  channels;
    uint8_t  irq_state;
    uint8_t  pci_slot;
    uint8_t  pad0;
    uint8_t  regs[256];
    uint32_t local;
    int      irq_mode[2];
    int      irq_pin;
    int      irq_line;
    uint8_t  type;

    void *   log; /* New logging system */
} pc87410_t;

#ifdef ENABLE_PC87410_LOG
int pc87410_do_log = ENABLE_PC87410_LOG;

static void
pc87410_log(void *priv, const char *fmt, ...)
{
    if (pc87410_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define pc87410_log(fmt, ...)
#endif

static void
pc87410_ide_handlers(pc87410_t *dev)
{
    uint16_t main;
    uint16_t side;

    ide_pri_disable();

    main = (dev->regs[0x11] << 8) | (dev->regs[0x10] & 0xf8);
    side = ((dev->regs[0x15] << 8) | (dev->regs[0x14] & 0xfc)) + 2;

    pc87410_log(dev->log, "PC87410 IDE chan0 addresses: main = %04X, side = %04X\n", main, side);

    ide_set_base(0, main);
    ide_set_side(0, side);

    if ((dev->regs[0x04] & 0x01) && (dev->regs[0x43] & 0x08))
        ide_pri_enable();

    ide_sec_disable();

    main = (dev->regs[0x19] << 8) | (dev->regs[0x18] & 0xf8);
    side = ((dev->regs[0x1d] << 8) | (dev->regs[0x1c] & 0xfc)) + 2;

    pc87410_log(dev->log, "PC87410 IDE chan1 addresses: main = %04X, side = %04X\n", main, side);

    ide_set_base(1, main);
    ide_set_side(1, side);

    if ((dev->regs[0x04] & 0x01) && (dev->regs[0x47] & 0x08))
        ide_sec_enable();
}

static void
pc87410_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    pc87410_t *dev = (pc87410_t *) priv;

    pc87410_log(dev->log, "pc87410_pci_write(%i, %02X, %02X)\n", func, addr, val);

    if (func == 0x00)
        switch (addr) {
            case 0x04:
                dev->regs[addr] = val;
                pc87410_ide_handlers(dev);
                break;
            case 0x10 ... 0x1f:
                dev->regs[addr] = val;
                dev->regs[0x10] &= 0xf9;
                dev->regs[0x10] |= 0x01;
                dev->regs[0x14] &= 0xfd;
                dev->regs[0x14] |= 0x01;
                dev->regs[0x18] &= 0xf9;
                dev->regs[0x18] |= 0x01;
                dev->regs[0x1c] &= 0xfd;
                dev->regs[0x1c] |= 0x01;
                pc87410_ide_handlers(dev);
                break;
            case 0x30 ... 0x33:
                dev->regs[addr] = val;
                dev->regs[0x30] &= 0xfe;
                break;
            case 0x3c:
                dev->regs[addr] = val;
                break;
            case 0x40 ... 0x48:
                dev->regs[addr] = val;
                pc87410_ide_handlers(dev);
                break;
        }
}

static uint8_t
pc87410_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    pc87410_t *dev = (pc87410_t *) priv;
    uint8_t   ret = 0xff;

    if (func == 0x00)
        ret = dev->regs[addr];

    pc87410_log(dev->log, "pc87410_pci_read(%i, %02X, %02X)\n", func, addr, ret);

    return ret;
}

static void
pc87410_reset(void *priv)
{
    pc87410_t *dev  = (pc87410_t *) priv;
    int i           = 0;
    int min_channel = 0;
    int max_channel = 1;

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

    dev->regs[0x00] = 0x0b; /* National Semiconductor */
    dev->regs[0x01] = 0x10;
    dev->regs[0x02] = 0x01; /* PC87410 */
    dev->regs[0x03] = 0xd0;
    dev->regs[0x04] = 0x00;
    dev->regs[0x07] = 0x02; /* DEVSEL timing: 01 medium */
    dev->regs[0x08] = 0x00;
    dev->regs[0x09] = 0x00; /* Programming interface */
    dev->regs[0x0a] = 0x01; /* IDE controller */
    dev->regs[0x0b] = 0x01; /* Mass storage controller */

    dev->regs[0x10] = 0xf1;
    dev->regs[0x11] = 0x01;
    dev->regs[0x14] = 0xf5;
    dev->regs[0x15] = 0x03;
    dev->regs[0x18] = 0x71;
    dev->regs[0x19] = 0x01;
    dev->regs[0x1c] = 0x75;
    dev->regs[0x1d] = 0x03;
    dev->regs[0x3c] = 0x0e; /* Interrupt line */
    dev->regs[0x3d] = 0x00;

    dev->regs[0x40] = 0xb5; /* IDE Channel 0 Timing Control Register */
    dev->regs[0x43] = 0x08; /* IDE Channel 0 Function Register */
    dev->regs[0x44] = 0xb5; /* IDE Channel 1 Timing Control Register */
    dev->regs[0x47] = 0x08; /* IDE Channel 1 Function Register */
    dev->regs[0x48] = 0x0e; /* PCI Control Register */

    dev->irq_mode[0] = dev->irq_mode[1] = 0;
    dev->irq_pin                        = PCI_INTA;
    dev->irq_line                       = 14;

    pc87410_ide_handlers(dev);
}

static void
pc87410_close(void *priv)
{
    pc87410_t *dev = (pc87410_t *) priv;

    if (dev->log != NULL) {
        log_close(dev->log);
        dev->log = NULL;
    }

    free(dev);
}

static void *
pc87410_init(const device_t *info)
{
    pc87410_t *dev = (pc87410_t *) calloc(1, sizeof(pc87410_t));

    dev->log = log_open("PC87410");

    device_add(&ide_pci_2ch_device);

    if (info->local & 0x80000)
        pci_add_card(PCI_ADD_NORMAL, pc87410_pci_read, pc87410_pci_write, dev, &dev->pci_slot);
    else
        pci_add_card(PCI_ADD_IDE, pc87410_pci_read, pc87410_pci_write, dev, &dev->pci_slot);

    ide_board_set_force_ata3(0, 1);
    ide_board_set_force_ata3(1, 1);

    pc87410_reset(dev);

    return dev;
}

const device_t ide_pc87410_device = {
    .name          = "National Semiconductor PC87410 PCI",
    .internal_name = "ide_pc87410",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = pc87410_init,
    .close         = pc87410_close,
    .reset         = pc87410_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

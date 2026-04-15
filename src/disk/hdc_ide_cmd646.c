/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the CMD PCI-0646 controller.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
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
#include <86box/rom.h>
#include <86box/hdd.h>
#include <86box/scsi_disk.h>
#include <86box/mo.h>
#include "cpu.h"
#include "x86.h"

#define CMD_TYPE_646              0x0000000
#define CMD_TYPE_648              0x0100000
#define CMD_TYPE_649              0x0200000

#define CMD648_JP7                0x0400000    /* Reload subsystem ID on reset. */
#define CMD648_RAID               0x0800000

#define CMD64X_ONBOARD            0x1000000

#define CMD648_BIOS_FILE          "roms/hdd/ide/648_1910.bin"
#define CMD649_REV_1914_BIOS_FILE "roms/hdd/ide/649_1914.bin"
#define CMD649_REV_2301_BIOS_FILE "roms/hdd/ide/649_2301.bin"

typedef struct cmd646_t {
    uint8_t     vlb_idx;
    uint8_t     single_channel;
    uint8_t     in_cfg;
    uint8_t     pci_slot;

    uint8_t     regs[256];

    uint32_t    local;
    uint32_t    rom_addr;
    uint32_t    rom_addr_size;
    uint32_t    rom_addr_mask;

    int         irq_pin;
    int         has_bios;

    int         irq_mode[2];

    rom_t       bios_rom;

    sff8038i_t *bm[2];
} cmd646_t;

#ifdef ENABLE_CMD646_LOG
int cmd646_do_log = ENABLE_CMD646_LOG;

static void
cmd646_log(const char *fmt, ...)
{
    va_list ap;

    if (cmd646_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cmd646_log(fmt, ...)
#endif

static void
cmd646_set_irq_0(uint8_t status, void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;

    if (!(dev->regs[0x50] & 0x04) || (status & 0x04))
        dev->regs[0x50] = (dev->regs[0x50] & ~0x04) | status;

    if (!(dev->local & CMD_TYPE_648) || !(dev->regs[0x71] & 0x10))
        sff_bus_master_set_irq(status, dev->bm[0]);
}

static void
cmd646_set_irq_1(uint8_t status, void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;

    if (!(dev->regs[0x57] & 0x10) || (status & 0x04))
        dev->regs[0x57] = (dev->regs[0x57] & ~0x10) | (status << 2);

    if (!(dev->local & CMD_TYPE_648) || !(dev->regs[0x71] & 0x20))
        sff_bus_master_set_irq(status, dev->bm[1]);
}

static int
cmd646_bus_master_dma_0(uint8_t *data, int transfer_length, int total_length, int out, void *priv)
{
    const cmd646_t *dev = (cmd646_t *) priv;

    return sff_bus_master_dma(data, transfer_length, total_length, out, dev->bm[0]);
}

static int
cmd646_bus_master_dma_1(uint8_t *data, int transfer_length, int total_length, int out, void *priv)
{
    const cmd646_t *dev = (cmd646_t *) priv;

    return sff_bus_master_dma(data, transfer_length, total_length, out, dev->bm[1]);
}

static void
cmd646_ide_handlers(cmd646_t *dev)
{
    uint16_t main;
    uint16_t side;
    int      irq_mode[2] = { IRQ_MODE_LEGACY, IRQ_MODE_LEGACY };
    int      first       = 0;
    int      reg09       = dev->regs[0x09];
    int      reg50       = dev->regs[0x50];
    int      dev_enabled = (hdc_onboard_enabled || !(dev->local & CMD64X_ONBOARD));

    if ((dev->local & CMD_TYPE_648) && (dev->local & CMD648_RAID)) {
        reg09 = 0xff;
        reg50 |= 0x40;
    }

    if (dev->local & 0x80000)
        first += 2;

    sff_set_slot(dev->bm[0], dev->pci_slot);
    sff_set_slot(dev->bm[1], dev->pci_slot);

    ide_handlers(first, 0);

    if ((reg09 & 0x01) && (reg50 & 0x40)) {
        main = (dev->regs[0x11] << 8) | (dev->regs[0x10] & 0xf8);
        side = ((dev->regs[0x15] << 8) | (dev->regs[0x14] & 0xfc)) + 2;
    } else {
        main = 0x1f0;
        side = 0x3f6;
    }

    ide_set_base(first, main);
    ide_set_side(first, side);

    if (reg09 & 0x01)
        irq_mode[0] = IRQ_MODE_PCI_IRQ_PIN;

    sff_set_irq_mode(dev->bm[0], irq_mode[0]);
    cmd646_log("IDE %i: %04X, %04X, %i\n", first, main, side, irq_mode[0]);

    int pri_enabled = (dev->regs[0x04] & 0x01);
    if (dev->local & CMD_TYPE_648)
        pri_enabled = pri_enabled && (dev->regs[0x51] & 0x04);

    if (dev_enabled && pri_enabled)
        ide_handlers(first, 1);

    if (dev->single_channel)
        return;

    ide_handlers(first + 1, 0);

    if ((reg09 & 0x04) && (reg50 & 0x40)) {
        main = (dev->regs[0x19] << 8) | (dev->regs[0x18] & 0xf8);
        side = ((dev->regs[0x1d] << 8) | (dev->regs[0x1c] & 0xfc)) + 2;
    } else {
        main = 0x170;
        side = 0x376;
    }

    ide_set_base(first + 1, main);
    ide_set_side(first + 1, side);

    if (reg09 & 0x04)
        irq_mode[1] = IRQ_MODE_PCI_IRQ_PIN;

    sff_set_irq_mode(dev->bm[1], irq_mode[1]);
    cmd646_log("IDE %i: %04X, %04X, %i\n", first + 1, main, side, irq_mode[1]);

    if (dev_enabled && (dev->regs[0x04] & 0x01) && (dev->regs[0x51] & 0x08))
        ide_handlers(first + 1, 1);
}

static void
cmd646_ide_bm_handlers(cmd646_t *dev)
{
    uint16_t base = (dev->regs[0x20] & 0xf0) | (dev->regs[0x21] << 8);
    int dev_enabled = (hdc_onboard_enabled || !(dev->local & CMD64X_ONBOARD));

    sff_bus_master_handler(dev->bm[0], dev_enabled && (dev->regs[0x04] & 1), base);
    sff_bus_master_handler(dev->bm[1], dev_enabled && (dev->regs[0x04] & 1), base + 8);
}

uint8_t
cmd646_bm_write(uint16_t port, uint8_t val, void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;
    uint8_t   ret = val;

    switch (port & 0x000f) {
        case 0x0001:
            dev->regs[(port & 0x000f) | 0x70] = val & 0xf0;
            if (val & 0x04)
                dev->regs[0x50] &= ~0x04;
            if (val & 0x08)
                dev->regs[0x57] &= ~0x10;
            ret &= 0x03;
            break; 
        case 0x0003:
            dev->regs[0x73] = val;
            break; 
        case 0x0009:
            dev->regs[(port & 0x000f) | 0x70] = (dev->regs[(port & 0x000f) | 0x70] & 0x0f) | (val & 0xf0);
            ret &= 0x03;
            break; 
        case 0x000b:
            dev->regs[0x7b] = val;
            break; 
   }

   return ret;
}

uint8_t
cmd646_bm_read(uint16_t port, uint8_t val, void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;
    uint8_t   ret = val;

    switch (port & 0x000f) {
        case 0x0001:
            ret = (dev->regs[(port & 0x000f) | 0x70] & 0xf3) | (dev->regs[0x50] & 0x04) | ((dev->regs[0x57] & 0x10) >> 1);
            break;
        case 0x0002: case 0x000a:
            ret |= 0x08;
            break;
        case 0x0003:
            ret = dev->regs[0x73];
            break; 
        case 0x0009:
            ret = dev->regs[(port & 0x000f) | 0x70];
            break;
        case 0x000b:
            ret = dev->regs[0x7b];
            break; 
   }

   return ret;
}

static void
cmd646_bios_handler(cmd646_t *dev)
{
    if ((dev->local & CMD_TYPE_648) && dev->has_bios) {
        uint32_t *addr = (uint32_t *) &(dev->regs[0x30]);

        *addr         &= (~dev->rom_addr_mask) | 0x00000001;
        dev->rom_addr  = *addr & 0xfffffff0;

        cmd646_log("ROM address now: %08X\n", dev->rom_addr);

        if ((dev->regs[0x04] & 0x02) && (*addr & 0x00000001))
            mem_mapping_set_addr(&dev->bios_rom.mapping, dev->rom_addr, dev->rom_addr_size);
        else
            mem_mapping_disable(&dev->bios_rom.mapping);
    }
}

static void
cmd646_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    cmd646_t *dev         = (cmd646_t *) priv;
    int       reg50       = dev->regs[0x50];
    int       dev_enabled = (hdc_onboard_enabled || !(dev->local & CMD64X_ONBOARD));

    if ((dev->local & CMD_TYPE_648) && (dev->regs[0x0a] == 0x04) && (dev->regs[0x0b] == 0x01))
        reg50 |= 0x40;

    cmd646_log("[%04X:%08X] (%08X) cmd646_pci_write(%i, %02X, %02X)\n", CS, cpu_state.pc, ESI, func, addr, val);

    if (dev_enabled && (func == 0x00))
        switch (addr) {
            case 0x04:
                if (dev->has_bios)
                    dev->regs[addr] = (val & 0x47);
                else
                    dev->regs[addr] = (val & 0x45);

                cmd646_ide_handlers(dev);
                cmd646_ide_bm_handlers(dev);

                cmd646_bios_handler(dev);
                break;
            case 0x05:
                if (dev->local & CMD_TYPE_648)
                    dev->regs[addr] = (dev->regs[addr] & 0x7e) | (val & 0x01);
                break;
            case 0x07:
                if (dev->local & CMD_TYPE_648)
                    dev->regs[addr] = ((dev->regs[addr] & ~(val & 0xb9)) & 0xbf) | (val & 0x40);
                else
                    dev->regs[addr] &= ~(val & 0xb1);
                break;
            case 0x09:
                if (!(dev->local & CMD_TYPE_648) ||
                    ((dev->regs[0x0a] == 0x01) && (dev->regs[0x0b] == 0x01))) {
                    if ((dev->regs[addr] & 0x0a) == 0x0a) {
                        dev->regs[addr]  = (dev->regs[addr] & 0x8a) | (val & 0x05);
                        dev->irq_mode[0] = !!(val & 0x01);
                        dev->irq_mode[1] = !!(val & 0x04);
                        cmd646_ide_handlers(dev);
                    }
                }
                break;
            case 0x0a: case 0x0b:
                if ((dev->local & CMD_TYPE_648) && (dev->regs[0x4f] & 0x04)) {
                    dev->regs[addr] = val;
                    cmd646_ide_handlers(dev);
                }
                break;
            case 0x10:
                if (reg50 & 0x40) {
                    dev->regs[0x10] = (val & 0xf8) | 1;
                    cmd646_ide_handlers(dev);
                }
                break;
            case 0x11:
                if (reg50 & 0x40) {
                    dev->regs[0x11] = val;
                    cmd646_ide_handlers(dev);
                }
                break;
            case 0x14:
                if (reg50 & 0x40) {
                    dev->regs[0x14] = (val & 0xfc) | 1;
                    cmd646_ide_handlers(dev);
                }
                break;
            case 0x15:
                if (reg50 & 0x40) {
                    dev->regs[0x15] = val;
                    cmd646_ide_handlers(dev);
                }
                break;
            case 0x18:
                if (reg50 & 0x40) {
                    dev->regs[0x18] = (val & 0xf8) | 1;
                    cmd646_ide_handlers(dev);
                }
                break;
            case 0x19:
                if (reg50 & 0x40) {
                    dev->regs[0x19] = val;
                    cmd646_ide_handlers(dev);
                }
                break;
            case 0x1c:
                if (reg50 & 0x40) {
                    dev->regs[0x1c] = (val & 0xfc) | 1;
                    cmd646_ide_handlers(dev);
                }
                break;
            case 0x1d:
                if (reg50 & 0x40) {
                    dev->regs[0x1d] = val;
                    cmd646_ide_handlers(dev);
                }
                break;
            case 0x20:
                dev->regs[0x20] = (val & 0xf0) | 1;
                cmd646_ide_bm_handlers(dev);
                break;
            case 0x21:
                dev->regs[0x21] = val;
                cmd646_ide_bm_handlers(dev);
                break;
            case 0x2c ... 0x2f:
            case 0x8c ... 0x8f:
                if (dev->local & CMD_TYPE_648)
                    dev->regs[(addr & 0x0f) | 0x20] = val;
                break;
            case 0x30 ... 0x33:
                if ((dev->local & CMD_TYPE_648) && dev->has_bios) {
                    dev->regs[addr] = val;
                    cmd646_bios_handler(dev);
                }
                break;
            case 0x3c:
                dev->regs[0x3c] = val;
                break;
            case 0x4f:
                if (dev->local & CMD_TYPE_648)
                    dev->regs[addr] = (dev->regs[addr] & 0xfa) | (val & 0x05);
                break;
            case 0x51:
                if (dev->local & CMD_TYPE_648)
                    dev->regs[addr] = val & 0xcc;
                else
                    dev->regs[addr] = val & 0xc8;
                cmd646_ide_handlers(dev);
                break;
            case 0x52:
            case 0x54:
            case 0x56:
            case 0x58:
            case 0x5b:
                dev->regs[addr] = val;
                break;
            case 0x59:
                if ((dev->local & CMD_TYPE_649) || !(dev->local & CMD_TYPE_648))
                    dev->regs[addr] = val;
                break;
            case 0x53:
            case 0x55:
                dev->regs[addr] = val & 0xc0;
                break;
            case 0x57:
                dev->regs[addr] = (dev->regs[addr] & 0x10) | (val & 0xcc);
                break;
            case 0x64:
                if (dev->local & CMD_TYPE_648)
                    dev->regs[addr] = (dev->regs[addr] & 0xfc) | (val & 0x03);
                break;
            case 0x65:
                if (dev->local & CMD_TYPE_648)
                    dev->regs[addr] = (dev->regs[addr] & 0x7f) | (val & 0x80);
                break;
            case 0x71:
                if (dev->local & CMD_TYPE_648)
                    sff_bus_master_write(addr & 0x0f, val, dev->bm[0]);
                else
                    sff_bus_master_write(addr & 0x0f, val & 0x03, dev->bm[0]);
                break;
            case 0x70:
            case 0x72 ... 0x77:
                sff_bus_master_write(addr & 0x0f, val, dev->bm[0]);
                break;
            case 0x79:
                if (dev->local & CMD_TYPE_648)
                    sff_bus_master_write(addr & 0x0f, val, dev->bm[1]);
                else
                    sff_bus_master_write(addr & 0x0f, val & 0x03, dev->bm[1]);
                break;
            case 0x78:
            case 0x7a ... 0x7f:
                sff_bus_master_write(addr & 0x0f, val, dev->bm[1]);
                break;

            default:
                break;
        }
}

static uint8_t
cmd646_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    cmd646_t *dev         = (cmd646_t *) priv;
    uint8_t   ret         = 0xff;
    int       dev_enabled = (hdc_onboard_enabled || !(dev->local & CMD64X_ONBOARD));

    if (dev_enabled && (func == 0x00)) {
        ret = dev->regs[addr];

        if ((addr == 0x09) && (dev->local & CMD_TYPE_648) && (dev->regs[0x0a] == 0x04))
            ret = 0x00;
        else if (addr == 0x50)
            dev->regs[0x50] &= ~0x04;
        else if (addr == 0x57)
            dev->regs[0x57] &= ~0x10;
        else if ((addr >= 0x70) && (addr <= 0x77))
            ret = sff_bus_master_read(addr & 0x0f, dev->bm[0]);
        else if ((addr >= 0x78) && (addr <= 0x7f))
            ret = sff_bus_master_read(addr & 0x0f, dev->bm[1]);
        else if ((dev->local & CMD_TYPE_648) && (addr >= 0x8c) && (addr <= 0x8f))
            ret = dev->regs[(addr & 0x0f) | 0x20];
    }

    cmd646_log("[%04X:%08X] (%08X) cmd646_pci_read(%i, %02X, %02X)\n", CS, cpu_state.pc, ESI, func, addr, ret);

    return ret;
}

static int
check_ch(cmd646_t *dev, int channel)
{
    int ret = 0;
    int min = 0;
    int max = dev->single_channel ? 1 : 3;

    if (dev->local & 0x80000) {
        min += 4;
        max += 4;
    }

    if ((channel >= min) && (channel <= max))
        ret = 1;

    return ret;
}

static void
cmd646_reset(void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;
    int       i   = 0;

    for (i = 0; i < HDD_NUM; i++) {
        if ((hdd[i].bus_type == HDD_BUS_ATAPI) && check_ch(dev, hdd[i].ide_channel) && hdd[i].priv)
            scsi_disk_reset((scsi_common_t *) hdd[i].priv);
    }
    for (i = 0; i < CDROM_NUM; i++) {
        if ((cdrom[i].bus_type == CDROM_BUS_ATAPI) && check_ch(dev, cdrom[i].ide_channel) && cdrom[i].priv)
            scsi_cdrom_reset((scsi_common_t *) cdrom[i].priv);
    }
    for (i = 0; i < RDISK_NUM; i++) {
        if ((rdisk_drives[i].bus_type == RDISK_BUS_ATAPI) && check_ch(dev, rdisk_drives[i].ide_channel) && rdisk_drives[i].priv)
            rdisk_reset((scsi_common_t *) rdisk_drives[i].priv);
    }
    for (i = 0; i < MO_NUM; i++) {
        if ((mo_drives[i].bus_type == MO_BUS_ATAPI) && check_ch(dev, mo_drives[i].ide_channel) && mo_drives[i].priv)
            mo_reset((scsi_common_t *) mo_drives[i].priv);
    }

    cmd646_set_irq_0(0x00, priv);
    cmd646_set_irq_1(0x00, priv);

    memset(dev->regs, 0x00, sizeof(dev->regs));

    dev->regs[0x00] = 0x95; /* CMD */
    dev->regs[0x01] = 0x10;
    if (dev->local & CMD_TYPE_649)
        dev->regs[0x02] = 0x49; /* PCI-0649 */
    else if (dev->local & CMD_TYPE_648)
        dev->regs[0x02] = 0x48; /* PCI-0648 */
    else
        dev->regs[0x02] = 0x46; /* PCI-0646 */
    dev->regs[0x03] = 0x06;
    dev->regs[0x04] = 0x00;
    dev->regs[0x07] = 0x02;       /* DEVSEL timing: 01 medium */
    dev->regs[0x09] = dev->local; /* Programming interface */
    if ((dev->local & CMD_TYPE_648) && (dev->local & CMD648_RAID)) {
        dev->regs[0x06] = 0x90;
        dev->regs[0x08] = 0x02;
        dev->regs[0x0a] = 0x04;       /* RAID controller */

        dev->regs[0x50] = 0x40;       /* Enable Base address register R/W;
                                         If 0, they return 0 and are read-only 8 */

        /* Blank base addresses */
        dev->regs[0x10] = 0x01;
        dev->regs[0x14] = 0x01;
        dev->regs[0x18] = 0x01;
        dev->regs[0x1c] = 0x01;
    } else {
        dev->regs[0x06] = 0x80;
        dev->regs[0x0a] = 0x01;       /* IDE controller */
    }
    dev->regs[0x0b] = 0x01;       /* Mass storage controller */

    if ((dev->local & CMD_TYPE_648) && (dev->local & CMD648_JP7))
        for (int i = 0; i < 4; i++)
            dev->regs[0x2c + i] = dev->regs[i];

    if ((dev->regs[0x0a] == 0x01) && ((dev->regs[0x09] & 0x8a) == 0x8a)) {
        dev->regs[0x50] = 0x40; /* Enable Base address register R/W;
                                   If 0, they return 0 and are read-only 8 */

        /* Base addresses (1F0, 3F4, 170, 374) */
        dev->regs[0x10] = 0xf1;
        dev->regs[0x11] = 0x01;
        dev->regs[0x14] = 0xf5;
        dev->regs[0x15] = 0x03;
        dev->regs[0x18] = 0x71;
        dev->regs[0x19] = 0x01;
        dev->regs[0x1c] = 0x75;
        dev->regs[0x1d] = 0x03;
    }

    dev->regs[0x20] = 0x01;

    dev->regs[0x3c] = 0x0e; /* IRQ 14 */
    dev->regs[0x3d] = 0x01; /* INTA */
    dev->regs[0x3e] = 0x02; /* Min_Gnt */
    dev->regs[0x3f] = 0x04; /* Max_Iat */

    if (!dev->single_channel)
        dev->regs[0x51] = 0x08;

    dev->regs[0x57] = 0x0c;

    if (dev->local & CMD_TYPE_648) {
        dev->regs[0x34] = 0x60;

        dev->regs[0x4f] = (dev->local & CMD648_JP7) ? 0x02 : 0x00;
        dev->regs[0x51] |= 0x04;

        if (dev->local & CMD_TYPE_649) {
            dev->regs[0x57] |= 0x80;
            dev->regs[0x59] = 0x40;
        } else
            dev->regs[0x57] |= 0xc0;

        dev->regs[0x60] = 0x01;
        dev->regs[0x62] = 0x21;
        dev->regs[0x63] = 0x06;
        dev->regs[0x65] = 0x60;
        dev->regs[0x67] = 0xf0;

        /* 80-pin stuff. */
        dev->regs[0x72] = 0x08;
        dev->regs[0x7a] = 0x08;
        dev->regs[0x79] = 0x83;
    } else
        dev->regs[0x59] = 0x40;

    dev->irq_pin                        = PCI_INTA;

    if ((dev->local & CMD_TYPE_648) && (dev->local & CMD648_RAID))
        dev->irq_mode[0] = dev->irq_mode[1] = IRQ_MODE_PCI_IRQ_PIN;
    else {
        dev->irq_mode[0] = (dev->regs[0x09] & 0x01) ? IRQ_MODE_PCI_IRQ_PIN : IRQ_MODE_LEGACY;
        dev->irq_mode[1] = (dev->regs[0x09] & 0x04) ? IRQ_MODE_PCI_IRQ_PIN : IRQ_MODE_LEGACY;
    }

    dev->irq_pin     = PCI_INTA;

    cmd646_ide_handlers(dev);
    cmd646_ide_bm_handlers(dev);

    cmd646_bios_handler(dev);
}

static void
cmd646_close(void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;

    free(dev);
}

static void *
cmd646_init(const device_t *info)
{
    cmd646_t *dev   = (cmd646_t *) calloc(1, sizeof(cmd646_t));
    int       first = 0;

    dev->local = info->local;

    device_add(&ide_pci_2ch_device);

    if (info->local & 0x80000) {
        first = 2;
        device_add(&ide_pci_ter_qua_2ch_device);
    } else
        device_add(&ide_pci_2ch_device);

    if (info->local & CMD64X_ONBOARD)
        pci_add_card(PCI_ADD_IDE, cmd646_pci_read, cmd646_pci_write, dev, &dev->pci_slot);
    else
        pci_add_card(PCI_ADD_NORMAL, cmd646_pci_read, cmd646_pci_write, dev, &dev->pci_slot);

    dev->single_channel = !!(info->local & 0x20000);

    dev->bm[0] = device_add_inst(&sff8038i_device, first + 1);
    if (!dev->single_channel)
        dev->bm[1] = device_add_inst(&sff8038i_device, first + 2);

    ide_set_bus_master(first, cmd646_bus_master_dma_0, cmd646_set_irq_0, dev);
    if (!dev->single_channel)
        ide_set_bus_master(first + 1, cmd646_bus_master_dma_1, cmd646_set_irq_1, dev);

    sff_set_irq_mode(dev->bm[0], IRQ_MODE_LEGACY);

    if (!dev->single_channel)
        sff_set_irq_mode(dev->bm[1], IRQ_MODE_LEGACY);

    sff_set_slot(dev->bm[0], dev->pci_slot);
    sff_set_slot(dev->bm[1], dev->pci_slot);

    if (dev->local & CMD_TYPE_648) {
        sff_set_ven_handlers(dev->bm[0], cmd646_bm_write, cmd646_bm_read, dev);
        sff_set_ven_handlers(dev->bm[1], cmd646_bm_write, cmd646_bm_read, dev);

        dev->has_bios = device_get_config_int("bios");

        if (dev->has_bios) {
            char *fn             = NULL;

            if (dev->local & CMD_TYPE_649) {
                const char *bios_rev = (char *) device_get_config_bios("bios_rev");
                fn                   = (char *) device_get_bios_file(info, bios_rev, 0);

                dev->rom_addr_size   = device_get_bios_file_size(info, bios_rev);
            } else {
                fn                   = CMD648_BIOS_FILE;

                dev->rom_addr_size   = 0x00004000;
            }

            dev->rom_addr_mask   = dev->rom_addr_size - 1;

            rom_init(&dev->bios_rom, fn,
                     0x000d0000, dev->rom_addr_size, dev->rom_addr_mask, 0, MEM_MAPPING_EXTERNAL);

            mem_mapping_disable(&dev->bios_rom.mapping);
        }
    }

    cmd646_reset(dev);

    if (dev->local & CMD_TYPE_648)
        for (int i = 0; i < 4; i++)
            dev->regs[0x2c + i] = dev->regs[i];

    return dev;
}

static const device_config_t cmd648_config[] = {
    {
        .name           = "bios",
        .description    = "Enable BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

static const device_config_t cmd649_config[] = {
    {
        .name           = "bios",
        .description    = "Enable BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_rev",
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "rev_2301",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "Revision 1.9.14",
                .internal_name = "rev_1914",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { CMD649_REV_2301_BIOS_FILE, "" }
            },
            {
                .name          = "Revision 2.3.01",
                .internal_name = "rev_2301",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { CMD649_REV_2301_BIOS_FILE, "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t ide_cmd646_device = {
    .name          = "CMD PCI-0646",
    .internal_name = "ide_cmd646",
    .flags         = DEVICE_PCI,
    .local         = 0x000008a | CMD64X_ONBOARD,
    .init          = cmd646_init,
    .close         = cmd646_close,
    .reset         = cmd646_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_cmd646_legacy_only_device = {
    .name          = "CMD PCI-0646 (Legacy Mode Only)",
    .internal_name = "ide_cmd646_legacy_only",
    .flags         = DEVICE_PCI,
    .local         = 0x0000080 | CMD64X_ONBOARD,
    .init          = cmd646_init,
    .close         = cmd646_close,
    .reset         = cmd646_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_cmd646_single_channel_device = {
    .name          = "CMD PCI-0646 (Single Channel)",
    .internal_name = "ide_cmd646_single_channel",
    .flags         = DEVICE_PCI,
    .local         = 0x002008a | CMD64X_ONBOARD,
    .init          = cmd646_init,
    .close         = cmd646_close,
    .reset         = cmd646_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_cmd646_ter_qua_device = {
    .name          = "CMD PCI-0646 (Tertiary and Quaternary)",
    .internal_name = "ide_cmd646_ter_qua",
    .flags         = DEVICE_PCI,
    .local         = 0x008008f,
    .init          = cmd646_init,
    .close         = cmd646_close,
    .reset         = cmd646_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_cmd648_ter_qua_device = {
    .name          = "CMD PCI-0648 (Tertiary and Quaternary)",
    .internal_name = "ide_cmd648_ter_qua",
    .flags         = DEVICE_PCI,
    .local         = 0x0d8008f,
    .init          = cmd646_init,
    .close         = cmd646_close,
    .reset         = cmd646_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = cmd648_config
};

const device_t ide_cmd648_ter_qua_onboard_device = {
    .name          = "CMD PCI-0648 (Tertiary and Quaternary) On-Board",
    .internal_name = "ide_cmd648_ter_qua_onboard",
    .flags         = DEVICE_PCI,
    .local         = 0x0d8008f | CMD64X_ONBOARD,
    .init          = cmd646_init,
    .close         = cmd646_close,
    .reset         = cmd646_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_cmd649_ter_qua_device = {
    .name          = "CMD PCI-0649 (Tertiary and Quaternary)",
    .internal_name = "ide_cmd649_ter_qua",
    .flags         = DEVICE_PCI,
    .local         = 0x0f8008f,
    .init          = cmd646_init,
    .close         = cmd646_close,
    .reset         = cmd646_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = cmd649_config
};

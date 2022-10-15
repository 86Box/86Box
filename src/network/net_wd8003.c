/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the following network controllers:
 *			- SMC/WD 8003E (ISA 8-bit);
 *			- SMC/WD 8013EBT (ISA 16-bit);
 *			- SMC/WD 8013EP/A (MCA).
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Peter Grehan, <grehan@iprg.nokia.com>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2016-2019 Miran Grca.
 *		Portions Copyright (C) 2002  MandrakeSoft S.A.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <time.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/mca.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/random.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/net_dp8390.h>
#include <86box/net_wd8003.h>
#include <86box/bswap.h>

#include "cpu.h"

/* Board type codes in card ID */
#define WE_TYPE_WD8003    0x01
#define WE_TYPE_WD8003S   0x02
#define WE_TYPE_WD8003E   0x03
#define WE_TYPE_WD8013EBT 0x05
#define WE_TYPE_TOSHIBA1  0x11 /* named PCETA1 */
#define WE_TYPE_TOSHIBA2  0x12 /* named PCETA2 */
#define WE_TYPE_TOSHIBA3  0x13 /* named PCETB */
#define WE_TYPE_TOSHIBA4  0x14 /* named PCETC */
#define WE_TYPE_WD8003W   0x24
#define WE_TYPE_WD8003EB  0x25
#define WE_TYPE_WD8013W   0x26
#define WE_TYPE_WD8013EP  0x27
#define WE_TYPE_WD8013WC  0x28
#define WE_TYPE_WD8013EPC 0x29
#define WE_TYPE_SMC8216T  0x2a
#define WE_TYPE_SMC8216C  0x2b
#define WE_TYPE_WD8013EBP 0x2c

#define WE_ICR_16BIT_SLOT 0x01

#define WE_MSR_ENABLE_RAM 0x40
#define WE_MSR_SOFT_RESET 0x80

#define WE_IRR_ENABLE_IRQ 0x80

#define WE_ID_ETHERNET    0x01
#define WE_ID_SOFT_CONFIG 0x20
#define WE_ID_EXTRA_RAM   0x40
#define WE_ID_BUS_MCA     0x80

typedef struct {
    dp8390_t     *dp8390;
    mem_mapping_t ram_mapping;
    uint32_t      ram_addr, ram_size;
    uint8_t       maclocal[6]; /* configured MAC (local) address */
    uint8_t       bit16, pad;
    int           board;
    const char   *name;
    uint32_t      base_address;
    int           irq;

    /* POS registers, MCA boards only */
    uint8_t pos_regs[8];

    /* Memory for WD cards*/
    uint8_t msr, /* Memory Select Register (MSR) */
        icr,     /* Interface Configuration Register (ICR) */
        irr,     /* Interrupt Request Register (IRR) */
        laar,    /* LA Address Register (read by Windows 98!) */
        if_chip, board_chip;
} wd_t;

#ifdef ENABLE_WD_LOG
int wd_do_log = ENABLE_WD_LOG;

static void
wdlog(const char *fmt, ...)
{
    va_list ap;

    if (wd_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define wdlog(fmt, ...)
#endif

static const int we_int_table[4] = { 2, 3, 4, 7 };

static void
wd_interrupt(void *priv, int set)
{
    wd_t *dev = (wd_t *) priv;

    if (!(dev->irr & WE_IRR_ENABLE_IRQ))
        return;

    if (set)
        picint(1 << dev->irq);
    else
        picintc(1 << dev->irq);
}

/* reset - restore state to power-up, cancelling all i/o */
static void
wd_reset(void *priv)
{
    wd_t *dev = (wd_t *) priv;

    wdlog("%s: reset\n", dev->name);

    dp8390_reset(dev->dp8390);
}

static void
wd_soft_reset(void *priv)
{
    wd_t *dev = (wd_t *) priv;

    dp8390_soft_reset(dev->dp8390);
}

static uint8_t
wd_ram_read(uint32_t addr, void *priv)
{
    wd_t *dev = (wd_t *) priv;

    wdlog("WD80x3: RAM Read: addr=%06x, val=%02x\n", addr & (dev->ram_size - 1), dev->dp8390->mem[addr & (dev->ram_size - 1)]);
    return dev->dp8390->mem[addr & (dev->ram_size - 1)];
}

static void
wd_ram_write(uint32_t addr, uint8_t val, void *priv)
{
    wd_t *dev = (wd_t *) priv;

    dev->dp8390->mem[addr & (dev->ram_size - 1)] = val;
    wdlog("WD80x3: RAM Write: addr=%06x, val=%02x\n", addr & (dev->ram_size - 1), val);
}

static int
wd_get_irq_index(wd_t *dev)
{
    uint8_t i, irq = 255;

    for (i = 0; i < 4; i++) {
        if (we_int_table[i] == dev->irq)
            irq = i;
    }
    if (irq != 255)
        return ((irq & 0x03) << 5);
    else

        return 0;
}

static uint32_t
wd_smc_read(wd_t *dev, uint32_t off)
{
    uint32_t retval   = 0;
    uint32_t checksum = 0;

    if (dev->board == WD8003E)
        off |= 0x08;

    switch (off) {
        case 0x00:
            if (dev->board_chip & WE_ID_BUS_MCA)
                retval = (dev->msr & 0xc0) | ((dev->ram_addr >> 13) & 0x3f);
            else
                retval = dev->msr;
            break;

        case 0x01:
            if (dev->board_chip & WE_ID_SOFT_CONFIG)
                retval = dev->icr;
            else
                retval = dev->icr & WE_ICR_16BIT_SLOT;
            break;

        case 0x04:
            if (dev->board_chip & WE_ID_SOFT_CONFIG)
                retval = (dev->irr & 0x9f) | wd_get_irq_index(dev);
            break;

        case 0x05:
            if (dev->board_chip & WE_ID_SOFT_CONFIG)
                retval = dev->laar;
            break;

        case 0x07:
            if (dev->board_chip & WE_ID_SOFT_CONFIG)
                retval = dev->if_chip;
            break;

        case 0x08:
            retval = dev->dp8390->physaddr[0];
            break;

        case 0x09:
            retval = dev->dp8390->physaddr[1];
            break;

        case 0x0a:
            retval = dev->dp8390->physaddr[2];
            break;

        case 0x0b:
            retval = dev->dp8390->physaddr[3];
            break;

        case 0x0c:
            retval = dev->dp8390->physaddr[4];
            break;

        case 0x0d:
            retval = dev->dp8390->physaddr[5];
            break;

        case 0x0e:
            retval = dev->board_chip;
            break;

        case 0x0f:
            /*This has to return the byte that adds up to 0xFF*/
            checksum = (dev->dp8390->physaddr[0] + dev->dp8390->physaddr[1] + dev->dp8390->physaddr[2] + dev->dp8390->physaddr[3] + dev->dp8390->physaddr[4] + dev->dp8390->physaddr[5] + dev->board_chip);

            retval = 0xff - (checksum & 0xff);
            break;
    }

    wdlog("%s: ASIC read addr=0x%02x, value=0x%04x\n",
          dev->name, (unsigned) off, (unsigned) retval);

    return (retval);
}

static void
wd_set_ram(wd_t *dev)
{
    uint32_t a13;

    if ((dev->board_chip & 0xa0) == 0x20) {
        a13 = dev->msr & 0x3f;
        a13 <<= 13;

        dev->ram_addr = a13 | (1 << 19);
        mem_mapping_set_addr(&dev->ram_mapping, dev->ram_addr, dev->ram_size);
        wdlog("%s: RAM address set to %08X\n", dev->name, dev->ram_addr);
    }

    if (dev->msr & WE_MSR_ENABLE_RAM)
        mem_mapping_enable(&dev->ram_mapping);
    else
        mem_mapping_disable(&dev->ram_mapping);
    wdlog("%s: RAM now %sabled\n", dev->name, (dev->msr & WE_MSR_ENABLE_RAM) ? "en" : "dis");
}

static void
wd_smc_write(wd_t *dev, uint32_t off, uint32_t val)
{
    uint8_t old;

    wdlog("%s: ASIC write addr=0x%02x, value=0x%04x\n",
          dev->name, (unsigned) off, (unsigned) val);

    if (off && (dev->board == WD8003E))
        return;

    switch (off) {
        /* Bits 0-5: Bits 13-18 of memory address (writable?):
                     Windows 98 requires this to be preloaded with the initial
                     addresss to work correctly;
           Bit 6: Enable memory if set;
           Bit 7: Software reset if set. */
        case 0x00: /* WD Control register */
            old = dev->msr;

            if (!(old & WE_MSR_SOFT_RESET) && (val & WE_MSR_SOFT_RESET)) {
                wd_soft_reset(dev);
                wdlog("WD80x3: Soft reset\n");
            }

            if ((dev->board_chip & 0xa0) == 0x20)
                dev->msr = val;
            else
                dev->msr = (dev->msr & 0x3f) | (val & 0xc0);

            if ((old &= 0x7f) != (val & 0x7f)) {
                wd_set_ram(dev);
                wdlog("WD80x3: Memory now %sabled (addr = %08X)\n", (val & WE_MSR_ENABLE_RAM) ? "en" : "dis", dev->ram_addr);
            }
            break;

        /* Bit 1: 0 = 8-bit slot, 1 = 16-bit slot;
           Bit 3: 0 = 8k RAM, 1 = 32k RAM (only on revision < 2). */
        case 0x01:
            if (dev->bit16 & 2)
                dev->icr = (dev->icr & WE_ICR_16BIT_SLOT) | (val & WE_ICR_16BIT_SLOT);
            else
                dev->icr = val;
            break;

        /* Bit 5: Bit 0 of encoded IRQ;
           Bit 6: Bit 1 of encoded IRQ;
           Bit 7: Enable interrupts. */
        case 0x04:
            if (dev->board_chip & WE_ID_SOFT_CONFIG)
                dev->irr = (dev->irr & 0xe0) | (val & 0x1f);
            break;

        /* Bits 0-4: Bits 19-23 of memory address (writable?):
                     Windows 98 requires this to be preloaded with the initial
                     addresss to work correctly;
           Bit 5: Enable software interrupt;
           Bit 6: Enable 16-bit RAM for LAN if set;
           Bit 7: Enable 16-bit RAM for host if set. */
        case 0x05:
            if (dev->board_chip & WE_ID_SOFT_CONFIG)
                dev->laar = val;
            break;

        /* Bits 0-4: Chip ID;
           Bit 5: Software configuration is supported if present;
           Bit 6: 0 = 16k RAM, 1 = 32k RAM. */
        case 0x07:
            if (dev->board_chip & WE_ID_SOFT_CONFIG)
                dev->if_chip = val;
            break;

        default:
            /* This is invalid, but happens under win95 device detection:
               maybe some clone cards implement writing for some other
               registers? */
            wdlog("%s: ASIC write invalid address %04x, ignoring\n",
                  dev->name, (unsigned) off);
            break;
    }
}

static uint8_t
wd_read(uint16_t addr, void *priv, int len)
{
    wd_t *dev = (wd_t *) priv;

    uint8_t retval = 0;
    int     off    = addr - dev->base_address;

    wdlog("%s: read addr %x\n", dev->name, addr);

    if (off == 0x10)
        retval = dp8390_read_cr(dev->dp8390);
    else if ((off >= 0x00) && (off <= 0x0f))
        retval = wd_smc_read(dev, off);
    else {
        switch (dev->dp8390->CR.pgsel) {
            case 0x00:
                retval = dp8390_page0_read(dev->dp8390, off - 0x10, len);
                break;
            case 0x01:
                retval = dp8390_page1_read(dev->dp8390, off - 0x10, len);
                break;
            case 0x02:
                retval = dp8390_page2_read(dev->dp8390, off - 0x10, len);
                break;
            default:
                wdlog("%s: unknown value of pgsel in read - %d\n",
                      dev->name, dev->dp8390->CR.pgsel);
                break;
        }
    }

    return (retval);
}

static uint8_t
wd_readb(uint16_t addr, void *priv)
{
    wd_t *dev = (wd_t *) priv;

    return (wd_read(addr, dev, 1));
}

static uint16_t
wd_readw(uint16_t addr, void *priv)
{
    wd_t *dev = (wd_t *) priv;

    return (wd_read(addr, dev, 2));
}

static void
wd_write(uint16_t addr, uint8_t val, void *priv, unsigned int len)
{
    wd_t *dev = (wd_t *) priv;
    int   off = addr - dev->base_address;

    wdlog("%s: write addr %x, value %x\n", dev->name, addr, val);

    if (off == 0x10)
        dp8390_write_cr(dev->dp8390, val);
    else if ((off >= 0x00) && (off <= 0x0f))
        wd_smc_write(dev, off, val);
    else {
        switch (dev->dp8390->CR.pgsel) {
            case 0x00:
                dp8390_page0_write(dev->dp8390, off - 0x10, val, len);
                break;
            case 0x01:
                dp8390_page1_write(dev->dp8390, off - 0x10, val, len);
                break;
            default:
                wdlog("%s: unknown value of pgsel in write - %d\n",
                      dev->name, dev->dp8390->CR.pgsel);
                break;
        }
    }
}

static void
wd_writeb(uint16_t addr, uint8_t val, void *priv)
{
    wd_write(addr, val, priv, 1);
}

static void
wd_writew(uint16_t addr, uint16_t val, void *priv)
{
    wd_write(addr, val & 0xff, priv, 2);
}

static void
wd_io_set(wd_t *dev, uint16_t addr)
{
    if (dev->bit16 & 1) {
        io_sethandler(addr, 0x20,
                      wd_readb, wd_readw, NULL,
                      wd_writeb, wd_writew, NULL, dev);
    } else {
        io_sethandler(addr, 0x20,
                      wd_readb, NULL, NULL,
                      wd_writeb, NULL, NULL, dev);
    }
}

static void
wd_io_remove(wd_t *dev, uint16_t addr)
{
    if (dev->bit16 & 1) {
        io_removehandler(addr, 0x20,
                         wd_readb, wd_readw, NULL,
                         wd_writeb, wd_writew, NULL, dev);
    } else {
        io_removehandler(addr, 0x20,
                         wd_readb, NULL, NULL,
                         wd_writeb, NULL, NULL, dev);
    }
}

static uint8_t
wd_mca_read(int port, void *priv)
{
    wd_t *dev = (wd_t *) priv;

    return (dev->pos_regs[port & 7]);
}

#define MCA_6FC0_IRQS \
    {                 \
        3, 4, 10, 15  \
    }

static void
wd_mca_write(int port, uint8_t val, void *priv)
{
    wd_t  *dev    = (wd_t *) priv;
    int8_t irq[4] = MCA_6FC0_IRQS;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    /* Save the MCA register value. */
    dev->pos_regs[port & 7] = val;

    /*
     * The PS/2 Model 80 BIOS always enables a card if it finds one,
     * even if no resources were assigned yet (because we only added
     * the card, but have not run AutoConfig yet...)
     *
     * So, remove current address, if any.
     */
    if (dev->base_address)
        wd_io_remove(dev, dev->base_address);

    dev->base_address = (dev->pos_regs[2] & 0xfe) << 4;
    dev->ram_addr     = (dev->pos_regs[3] & 0xfc) << 12;
    dev->irq          = irq[dev->pos_regs[5] & 0x02];

    /* Initialize the device if fully configured. */
    /* Register (new) I/O handler. */
    if (dev->pos_regs[2] & 0x01)
        wd_io_set(dev, dev->base_address);

    mem_mapping_set_addr(&dev->ram_mapping, dev->ram_addr, dev->ram_size);

    mem_mapping_disable(&dev->ram_mapping);
    if ((dev->msr & WE_MSR_ENABLE_RAM) && (dev->pos_regs[2] & 0x01))
        mem_mapping_enable(&dev->ram_mapping);

    wdlog("%s: attached IO=0x%X IRQ=%d, RAM addr=0x%06x\n", dev->name,
          dev->base_address, dev->irq, dev->ram_addr);
}

static void
wd_8013epa_mca_write(int port, uint8_t val, void *priv)
{
    wd_t *dev = (wd_t *) priv;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    /* Save the MCA register value. */
    dev->pos_regs[port & 7] = val;

    /*
     * The PS/2 Model 80 BIOS always enables a card if it finds one,
     * even if no resources were assigned yet (because we only added
     * the card, but have not run AutoConfig yet...)
     *
     * So, remove current address, if any.
     */
    if (dev->base_address)
        wd_io_remove(dev, dev->base_address);

    dev->base_address = 0x800 + ((dev->pos_regs[2] & 0xf0) << 8);

    switch (dev->pos_regs[5] & 0x0c) {
        case 0:
            dev->irq = 3;
            break;
        case 4:
            dev->irq = 4;
            break;
        case 8:
            dev->irq = 10;
            break;
        case 0x0c:
            dev->irq = 14;
            break;
    }

    if (dev->pos_regs[3] & 0x10)
        dev->ram_size = 0x4000;
    else
        dev->ram_size = 0x2000;

    dev->ram_addr = ((dev->pos_regs[3] & 0x0f) << 13) + 0xc0000;
    if (dev->pos_regs[3] & 0x80)
        dev->ram_addr += 0xf00000;

    /* Initialize the device if fully configured. */
    /* Register (new) I/O handler. */
    if (dev->pos_regs[2] & 0x01)
        wd_io_set(dev, dev->base_address);

    mem_mapping_set_addr(&dev->ram_mapping, dev->ram_addr, dev->ram_size);

    mem_mapping_disable(&dev->ram_mapping);
    if ((dev->msr & WE_MSR_ENABLE_RAM) && (dev->pos_regs[2] & 0x01))
        mem_mapping_enable(&dev->ram_mapping);

    wdlog("%s: attached IO=0x%X IRQ=%d, RAM addr=0x%06x\n", dev->name,
          dev->base_address, dev->irq, dev->ram_addr);
}

static uint8_t
wd_mca_feedb(void *priv)
{
    return 1;
}

static void *
wd_init(const device_t *info)
{
    uint32_t mac;
    wd_t    *dev;

    dev = malloc(sizeof(wd_t));
    memset(dev, 0x00, sizeof(wd_t));
    dev->name  = info->name;
    dev->board = info->local;

    dev->maclocal[0] = 0x00; /* 00:00:C0 (WD/SMC OID) */
    dev->maclocal[1] = 0x00;
    dev->maclocal[2] = 0xC0;

    /* See if we have a local MAC address configured. */
    mac = device_get_config_mac("mac", -1);

    /* Set up our BIA. */
    if (mac & 0xff000000) {
        /* Generate new local MAC. */
        dev->maclocal[3] = random_generate();
        dev->maclocal[4] = random_generate();
        dev->maclocal[5] = random_generate();
        mac              = (((int) dev->maclocal[3]) << 16);
        mac |= (((int) dev->maclocal[4]) << 8);
        mac |= ((int) dev->maclocal[5]);
        device_set_config_mac("mac", mac);
    } else {
        dev->maclocal[3] = (mac >> 16) & 0xff;
        dev->maclocal[4] = (mac >> 8) & 0xff;
        dev->maclocal[5] = (mac & 0xff);
    }

    if ((dev->board == WD8003ETA) || (dev->board == WD8003EA) || dev->board == WD8013EPA) {
        if (dev->board == WD8013EPA)
            mca_add(wd_mca_read, wd_8013epa_mca_write, wd_mca_feedb, NULL, dev);
        else
            mca_add(wd_mca_read, wd_mca_write, wd_mca_feedb, NULL, dev);
    } else {
        dev->base_address = device_get_config_hex16("base");
        dev->irq          = device_get_config_int("irq");
        dev->ram_addr     = device_get_config_hex20("ram_addr");
    }

    dev->dp8390            = device_add_inst(&dp8390_device, dp3890_inst++);
    dev->dp8390->priv      = dev;
    dev->dp8390->interrupt = wd_interrupt;
    dp8390_set_defaults(dev->dp8390, DP8390_FLAG_CHECK_CR | DP8390_FLAG_CLEAR_IRQ);

    switch (dev->board) {
        /* Ethernet, ISA, no interface chip, RAM 8k */
        case WD8003E:
            dev->board_chip = WE_TYPE_WD8003E;
            dev->ram_size   = 0x2000;
            break;

        /* Ethernet, ISA, 5x3 interface chip, RAM 8k or 32k */
        case WD8003EB:
            dev->board_chip = WE_TYPE_WD8003EB;
            dev->if_chip    = 1;
            dev->ram_size   = device_get_config_int("ram_size");
            if (dev->ram_size == 0x8000)
                dev->board_chip |= WE_ID_EXTRA_RAM;

            /* Bit A19 is implicit 1. */
            dev->msr |= (dev->ram_addr >> 13) & 0x3f;
            break;

        /* Ethernet, ISA, no interface chip, RAM 8k or 32k (8-bit slot) / 16k or 64k (16-bit slot) */
        case WD8013EBT:
            dev->board_chip = WE_TYPE_WD8013EBT;
            dev->ram_size   = device_get_config_int("ram_size");
            if (dev->ram_size == 0x10000)
                dev->board_chip |= WE_ID_EXTRA_RAM;

            dev->bit16 = 2;
            if (is286)
                dev->bit16 |= 1;
            else {
                dev->bit16 |= 0;
                if (dev->irq == 9)
                    dev->irq = 2;
                dev->ram_size >>= 1; /* Half the RAM when in 8-bit slot. */
            }
            break;

        /* Ethernet, MCA, 5x3 interface chip, RAM 16k */
        case WD8003EA:
            dev->board_chip = WE_ID_SOFT_CONFIG;
        /* Ethernet, MCA, no interface chip, RAM 16k */
        case WD8003ETA:
            dev->board_chip |= WE_TYPE_WD8013EBT | WE_ID_BUS_MCA;
            dev->ram_size    = 0x4000;
            dev->pos_regs[0] = 0xC0;
            dev->pos_regs[1] = 0x6F;
            dev->bit16       = 3;
            break;

        case WD8013EPA:
            dev->board_chip  = WE_TYPE_WD8013EP | WE_ID_BUS_MCA;
            dev->ram_size    = device_get_config_int("ram_size");
            dev->pos_regs[0] = 0xC8;
            dev->pos_regs[1] = 0x61;
            dev->bit16       = 3;
            break;
    }

    dev->irr |= WE_IRR_ENABLE_IRQ;
    dev->icr |= (dev->bit16 & 0x01);

    dp8390_mem_alloc(dev->dp8390, 0x0000, dev->ram_size);

    if (dev->base_address)
        wd_io_set(dev, dev->base_address);

    memcpy(dev->dp8390->physaddr, dev->maclocal, sizeof(dev->maclocal));

    wdlog("%s: I/O=%04x, IRQ=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
          dev->name, dev->base_address, dev->irq,
          dev->dp8390->physaddr[0], dev->dp8390->physaddr[1], dev->dp8390->physaddr[2],
          dev->dp8390->physaddr[3], dev->dp8390->physaddr[4], dev->dp8390->physaddr[5]);

    /* Reset the board. */
    wd_reset(dev);

    /* Map this system into the memory map. */
    mem_mapping_add(&dev->ram_mapping, dev->ram_addr, dev->ram_size,
                    wd_ram_read, NULL, NULL,
                    wd_ram_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    mem_mapping_disable(&dev->ram_mapping);

    /* Attach ourselves to the network module. */
    dev->dp8390->card = network_attach(dev->dp8390, dev->dp8390->physaddr, dp8390_rx, NULL);

    if (!(dev->board_chip & WE_ID_BUS_MCA)) {
        wdlog("%s: attached IO=0x%X IRQ=%d, RAM addr=0x%06x\n", dev->name,
              dev->base_address, dev->irq, dev->ram_addr);
    }

    return (dev);
}

static void
wd_close(void *priv)
{
    wd_t *dev = (wd_t *) priv;

    wdlog("%s: closed\n", dev->name);

    free(dev);
}

// clang-format off
static const device_config_t wd8003_config[] = {
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x300,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "0x240", .value = 0x240 },
            { .description = "0x280", .value = 0x280 },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x380", .value = 0x380 },
            { .description = ""                      }
        },
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 3,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "IRQ 2", .value = 2 },
            { .description = "IRQ 3", .value = 3 },
            { .description = "IRQ 5", .value = 5 },
            { .description = "IRQ 7", .value = 7 },
            { .description = ""                  }
        },
    },
    {
        .name = "ram_addr",
        .description = "RAM address",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xD0000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "C800", .value = 0xC8000 },
            { .description = "CC00", .value = 0xCC000 },
            { .description = "D000", .value = 0xD0000 },
            { .description = "D400", .value = 0xD4000 },
            { .description = "D800", .value = 0xD8000 },
            { .description = "DC00", .value = 0xDC000 },
            { .description = ""                       }
        },
    },
    {
        .name = "mac",
        .description = "MAC Address",
        .type = CONFIG_MAC,
        .default_string = "",
        .default_int = -1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t wd8003eb_config[] = {
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x280,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "0x200", .value = 0x200 },
            { .description = "0x220", .value = 0x220 },
            { .description = "0x240", .value = 0x240 },
            { .description = "0x260", .value = 0x260 },
            { .description = "0x280", .value = 0x280 },
            { .description = "0x2A0", .value = 0x2A0 },
            { .description = "0x2C0", .value = 0x2C0 },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x380", .value = 0x380 },
            { .description = ""                      }
        },
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 3,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "IRQ 2/9", .value = 9 },
            { .description = "IRQ 3",   .value = 3 },
            { .description = "IRQ 4",   .value = 4 },
            { .description = "IRQ 7",   .value = 7 },
            { .description = ""                    }
        },
    },
    {
        .name = "ram_addr",
        .description = "RAM address",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xD0000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "C000", .value = 0xC0000 },
            { .description = "C400", .value = 0xC4000 },
            { .description = "C800", .value = 0xC8000 },
            { .description = "CC00", .value = 0xCC000 },
            { .description = "D000", .value = 0xD0000 },
            { .description = "D400", .value = 0xD4000 },
            { .description = "D800", .value = 0xD8000 },
            { .description = "DC00", .value = 0xDC000 },
            { .description = ""                       }
        },
    },
    {
        .name = "ram_size",
        .description = "RAM size",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 8192,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "8 kB",  .value =  8192 },
            { .description = "32 kB", .value = 32768 },
            { .description = ""                      }
        },
    },
    {
        .name = "mac",
        .description = "MAC Address",
        .type = CONFIG_MAC,
        .default_string = "",
        .default_int = -1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

/* WD8013EBT configuration and defaults set according to this site:
   http://www.stack.nl/~marcolz/network/wd80x3.html#WD8013EBT */
static const device_config_t wd8013_config[] = {
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x280,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "0x200", .value = 0x200 },
            { .description = "0x220", .value = 0x220 },
            { .description = "0x240", .value = 0x240 },
            { .description = "0x260", .value = 0x260 },
            { .description = "0x280", .value = 0x280 },
            { .description = "0x2A0", .value = 0x2A0 },
            { .description = "0x2C0", .value = 0x2C0 },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x380", .value = 0x380 },
            { .description = ""                      }
        },
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 3,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "IRQ 2/9", .value =  9 },
            { .description = "IRQ 3",   .value =  3 },
            { .description = "IRQ 4",   .value =  4 },
            { .description = "IRQ 5",   .value =  5 },
            { .description = "IRQ 7",   .value =  7 },
            { .description = "IRQ 10",  .value = 10 },
            { .description = "IRQ 11",  .value = 11 },
            { .description = "IRQ 15",  .value = 15 },
            { .description = ""                     }
        },
    },
    {
        .name = "ram_addr",
        .description = "RAM address",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xD0000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "C000", .value = 0xC0000 },
            { .description = "C400", .value = 0xC4000 },
            { .description = "C800", .value = 0xC8000 },
            { .description = "CC00", .value = 0xCC000 },
            { .description = "D000", .value = 0xD0000 },
            { .description = "D400", .value = 0xD4000 },
            { .description = "D800", .value = 0xD8000 },
            { .description = "DC00", .value = 0xDC000 },
            { .description = ""                       }
        },
    },
    {
        .name = "ram_size",
        .description = "RAM size",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 16384,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "16 kB", .value = 16384 },
            { .description = "64 kB", .value = 65536 },
            { .description = ""                      }
        },
    },
    {
        .name = "mac",
        .description = "MAC Address",
        .type = CONFIG_MAC,
        .default_string = "",
        .default_int = -1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t wd8013epa_config[] = {
    {
        .name = "ram_size",
        .description = "Initial RAM size",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 16384,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "8 kB",  .value = 8192  },
            { .description = "16 kB", .value = 16384 },
            { .description = ""                      }
        },
    },
    {
        .name = "mac",
        .description = "MAC Address",
        .type = CONFIG_MAC,
        .default_string = "",
        .default_int = -1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t mca_mac_config[] = {
    {
        .name = "mac",
        .description = "MAC Address",
        .type = CONFIG_MAC,
        .default_string = "",
        .default_int = -1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t wd8003e_device = {
    .name          = "Western Digital WD8003E",
    .internal_name = "wd8003e",
    .flags         = DEVICE_ISA,
    .local         = WD8003E,
    .init          = wd_init,
    .close         = wd_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = wd8003_config
};

const device_t wd8003eb_device = {
    .name          = "Western Digital WD8003EB",
    .internal_name = "wd8003eb",
    .flags         = DEVICE_ISA,
    .local         = WD8003EB,
    .init          = wd_init,
    .close         = wd_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = wd8003eb_config
};

const device_t wd8013ebt_device = {
    .name          = "Western Digital WD8013EBT",
    .internal_name = "wd8013ebt",
    .flags         = DEVICE_ISA,
    .local         = WD8013EBT,
    .init          = wd_init,
    .close         = wd_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = wd8013_config
};

const device_t wd8003eta_device = {
    .name          = "Western Digital WD8003ET/A",
    .internal_name = "wd8003eta",
    .flags         = DEVICE_MCA,
    .local         = WD8003ETA,
    .init          = wd_init,
    .close         = wd_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mca_mac_config
};

const device_t wd8003ea_device = {
    .name          = "Western Digital WD8003E/A",
    .internal_name = "wd8003ea",
    .flags         = DEVICE_MCA,
    .local         = WD8003EA,
    .init          = wd_init,
    .close         = wd_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mca_mac_config
};

const device_t wd8013epa_device = {
    .name          = "Western Digital WD8013EP/A",
    .internal_name = "wd8013epa",
    .flags         = DEVICE_MCA,
    .local         = WD8013EPA,
    .init          = wd_init,
    .close         = wd_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = wd8013epa_config
};

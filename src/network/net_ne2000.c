/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the following network controllers:
 *              - Novell NE1000 (ISA 8-bit);
 *              - Novell NE2000 (ISA 16-bit);
 *              - Novell NE/2 compatible (NetWorth Inc. Ethernext/MC) (MCA 16-bit);
 *              - Realtek RTL8019AS (ISA 16-bit, PnP);
 *              - Realtek RTL8029AS (PCI).
 *
 *
 *
 * Based on    @(#)ne2k.cc v1.56.2.1 2004/02/02 22:37:22 cbothamy
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Peter Grehan, <grehan@iprg.nokia.com>
 *
 *          Copyright 2017-2018 Fred N. van Kempen.
 *          Copyright 2016-2018 Miran Grca.
 *          Portions Copyright (C) 2002  MandrakeSoft S.A.
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
#include <86box/mca.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/random.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/net_dp8390.h>
#include <86box/net_ne2000.h>
#include <86box/isapnp.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

/* ROM BIOS file paths. */
#define ROM_PATH_NE1000  "roms/network/ne1000/ne1000.rom"
#define ROM_PATH_NE2000  "roms/network/ne2000/ne2000.rom"
#define ROM_PATH_RTL8019 "roms/network/rtl8019as/rtl8019as.rom"
#define ROM_PATH_RTL8029 "roms/network/rtl8029as/rtl8029as.rom"

/* PCI info. */
#define PCI_VENDID  0x10ec /* Realtek, Inc */
#define PCI_DEVID   0x8029 /* RTL8029AS */
#define PCI_REGSIZE 256    /* size of PCI space */

typedef struct nic_t {
    dp8390_t   *dp8390;

    const char *name;

    uint8_t     pnp_csnsav;
    uint8_t     pci_slot;
    uint8_t     irq_state;
    uint8_t     pad;

    /* RTL8019AS/RTL8029AS registers */
    uint8_t     config0;
    uint8_t     config2;
    uint8_t     config3;
    uint8_t     _9346cr;

    uint8_t     pci_regs[PCI_REGSIZE];
    uint8_t     eeprom[128]; /* for RTL8029AS */

    uint8_t     maclocal[6]; /* configured MAC (local) address */

    /* POS registers, MCA boards only */
    uint8_t     pos_regs[8];

    int         board;
    int         is_pci;
    int         is_mca;
    int         is_8bit;
    int         base_irq;
    int         has_bios;

    uint32_t    base_address;
    uint32_t    bios_addr;
    uint32_t    bios_size;
    uint32_t    bios_mask;

    bar_t       pci_bar[2];

    rom_t       bios_rom;

    void       *pnp_card;
} nic_t;

#ifdef ENABLE_NE2K_LOG
int ne2k_do_log = ENABLE_NE2K_LOG;

static void
nelog(int lvl, const char *fmt, ...)
{
    va_list ap;

    if (ne2k_do_log >= lvl) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define nelog(lvl, fmt, ...)
#endif

static void
nic_interrupt(void *priv, int set)
{
    nic_t *dev = (nic_t *) priv;

    if (dev->is_pci) {
        if (set)
            pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
        else
            pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    } else {
        if (set)
            picint(1 << dev->base_irq);
        else
            picintc(1 << dev->base_irq);
    }
}

/* reset - restore state to power-up, cancelling all i/o */
static void
nic_reset(void *priv)
{
    nic_t *dev = (nic_t *) priv;

    nelog(1, "%s: reset\n", dev->name);

    dp8390_reset(dev->dp8390);
}

static void
nic_soft_reset(void *priv)
{
    nic_t *dev = (nic_t *) priv;

    dp8390_soft_reset(dev->dp8390);
}

/*
 * Access the ASIC I/O space.
 *
 * This is the high 16 bytes of i/o space (the lower 16 bytes
 * is for the DS8390). Only two locations are used: offset 0,
 * which is used for data transfer, and offset 0x0f, which is
 * used to reset the device.
 *
 * The data transfer port is used to as 'external' DMA to the
 * DS8390. The chip has to have the DMA registers set up, and
 * after that, insw/outsw instructions can be used to move
 * the appropriate number of bytes to/from the device.
 */
static uint32_t
asic_read(nic_t *dev, uint32_t off, unsigned int len)
{
    uint32_t retval = 0;

    switch (off) {
        case 0x00: /* Data register */
            /* A read remote-DMA command must have been issued,
               and the source-address and length registers must
               have been initialised. */
            if (len > dev->dp8390->remote_bytes) {
                nelog(3, "%s: DMA read underrun iolen=%d remote_bytes=%d\n",
                      dev->name, len, dev->dp8390->remote_bytes);
            }

            nelog(3, "%s: DMA read: addr=%4x remote_bytes=%d\n",
                  dev->name, dev->dp8390->remote_dma, dev->dp8390->remote_bytes);
            retval = dp8390_chipmem_read(dev->dp8390, dev->dp8390->remote_dma, len);

            /* The 8390 bumps the address and decreases the byte count
               by the selected word size after every access, not by
               the amount of data requested by the host (io_len). */
            if (len == 4) {
                dev->dp8390->remote_dma += len;
            } else {
                dev->dp8390->remote_dma += (dev->dp8390->DCR.wdsize + 1);
            }

            if (dev->dp8390->remote_dma == dev->dp8390->page_stop << 8) {
                dev->dp8390->remote_dma = dev->dp8390->page_start << 8;
            }

            /* keep s.remote_bytes from underflowing */
            if (dev->dp8390->remote_bytes > dev->dp8390->DCR.wdsize) {
                if (len == 4) {
                    dev->dp8390->remote_bytes -= len;
                } else {
                    dev->dp8390->remote_bytes -= (dev->dp8390->DCR.wdsize + 1);
                }
            } else {
                dev->dp8390->remote_bytes = 0;
            }

            /* If all bytes have been written, signal remote-DMA complete */
            if (dev->dp8390->remote_bytes == 0) {
                dev->dp8390->ISR.rdma_done = 1;
                if (dev->dp8390->IMR.rdma_inte)
                    nic_interrupt(dev, 1);
            }
            break;

        case 0x0f: /* Reset register */
            nic_soft_reset(dev);
            break;

        default:
            nelog(3, "%s: ASIC read invalid address %04x\n",
                  dev->name, (unsigned) off);
            break;
    }

    return retval;
}

static void
asic_write(nic_t *dev, uint32_t off, uint32_t val, unsigned len)
{
    nelog(3, "%s: ASIC write addr=0x%02x, value=0x%04x\n",
          dev->name, (unsigned) off, (unsigned) val);

    switch (off) {
        case 0x00: /* Data register - see asic_read for a description */
            if ((len > 1) && (dev->dp8390->DCR.wdsize == 0)) {
                nelog(3, "%s: DMA write length %d on byte mode operation\n",
                      dev->name, len);
                break;
            }
            if (dev->dp8390->remote_bytes == 0)
                nelog(3, "%s: DMA write, byte count 0\n", dev->name);

            dp8390_chipmem_write(dev->dp8390, dev->dp8390->remote_dma, val, len);
            if (len == 4)
                dev->dp8390->remote_dma += len;
            else
                dev->dp8390->remote_dma += (dev->dp8390->DCR.wdsize + 1);

            if (dev->dp8390->remote_dma == dev->dp8390->page_stop << 8)
                dev->dp8390->remote_dma = dev->dp8390->page_start << 8;

            if (len == 4)
                dev->dp8390->remote_bytes -= len;
            else
                dev->dp8390->remote_bytes -= (dev->dp8390->DCR.wdsize + 1);

            if (dev->dp8390->remote_bytes > dev->dp8390->mem_size)
                dev->dp8390->remote_bytes = 0;

            /* If all bytes have been written, signal remote-DMA complete */
            if (dev->dp8390->remote_bytes == 0) {
                dev->dp8390->ISR.rdma_done = 1;
                if (dev->dp8390->IMR.rdma_inte)
                    nic_interrupt(dev, 1);
            }
            break;

        case 0x0f: /* Reset register */
            /* end of reset pulse */
            break;

        default: /* this is invalid, but happens under win95 device detection */
            nelog(3, "%s: ASIC write invalid address %04x, ignoring\n",
                  dev->name, (unsigned) off);
            break;
    }
}

/* Writes to this page are illegal. */
static uint32_t
page3_read(nic_t *dev, uint32_t off, UNUSED(unsigned int len))
{
    if (dev->board >= NE2K_RTL8019AS_PNP)
        switch (off) {
            case 0x1: /* 9346CR */
                return (dev->_9346cr);

            case 0x3:          /* CONFIG0 */
                return 0x00; /* Cable not BNC */

            case 0x5: /* CONFIG2 */
                return (dev->config2 & 0xe0);

            case 0x6: /* CONFIG3 */
                return (dev->config3 & 0x46);

            case 0x8: /* CSNSAV */
                return ((dev->board == NE2K_RTL8019AS_PNP) ? dev->pnp_csnsav : 0x00);

            case 0xe: /* 8029ASID0 */
                if (dev->board == NE2K_RTL8029AS)
                    return 0x29;
                break;

            case 0xf: /* 8029ASID1 */
                if (dev->board == NE2K_RTL8029AS)
                    return 0x80;
                break;

            default:
                break;
        }

    nelog(3, "%s: Page3 read register 0x%02x attempted\n", dev->name, off);
    return 0x00;
}

static void
page3_write(nic_t *dev, uint32_t off, uint32_t val, UNUSED(unsigned len))
{
    if (dev->board >= NE2K_RTL8019AS_PNP) {
        nelog(3, "%s: Page2 write to register 0x%02x, len=%u, value=0x%04x\n",
              dev->name, off, len, val);

        switch (off) {
            case 0x01: /* 9346CR */
                dev->_9346cr = (val & 0xfe);
                break;

            case 0x05: /* CONFIG2 */
                dev->config2 = (val & 0xe0);
                break;

            case 0x06: /* CONFIG3 */
                dev->config3 = (val & 0x46);
                break;

            case 0x09: /* HLTCLK  */
                break;

            default:
                nelog(3, "%s: Page3 write to reserved register 0x%02x\n",
                      dev->name, off);
                break;
        }
    } else
        nelog(3, "%s: Page3 write register 0x%02x attempted\n", dev->name, off);
}

static uint32_t
nic_read(nic_t *dev, uint32_t addr, unsigned len)
{
    uint32_t retval = 0;
    int      off    = addr - dev->base_address;

    nelog(3, "%s: read addr %x, len %d\n", dev->name, addr, len);

    if (off >= 0x10)
        retval = asic_read(dev, off - 0x10, len);
    else if (off == 0x00)
        retval = dp8390_read_cr(dev->dp8390);
    else
        switch (dev->dp8390->CR.pgsel) {
            case 0x00:
                retval = dp8390_page0_read(dev->dp8390, off, len);
                break;
            case 0x01:
                retval = dp8390_page1_read(dev->dp8390, off, len);
                break;
            case 0x02:
                retval = dp8390_page2_read(dev->dp8390, off, len);
                break;
            case 0x03:
                retval = page3_read(dev, off, len);
                break;
            default:
                nelog(3, "%s: unknown value of pgsel in read - %d\n",
                      dev->name, dev->dp8390->CR.pgsel);
                break;
        }

    return retval;
}

static uint8_t
nic_readb(uint16_t addr, void *priv)
{
    return (nic_read((nic_t *) priv, addr, 1));
}

static uint16_t
nic_readw(uint16_t addr, void *priv)
{
    return (nic_read((nic_t *) priv, addr, 2));
}

static uint32_t
nic_readl(uint16_t addr, void *priv)
{
    return (nic_read((nic_t *) priv, addr, 4));
}

static void
nic_write(nic_t *dev, uint32_t addr, uint32_t val, unsigned len)
{
    int off = addr - dev->base_address;

    nelog(3, "%s: write addr %x, value %x len %d\n", dev->name, addr, val, len);

    /* The high 16 bytes of i/o space are for the ne2000 asic -
       the low 16 bytes are for the DS8390, with the current
       page being selected by the PS0,PS1 registers in the
       command register */
    if (off >= 0x10)
        asic_write(dev, off - 0x10, val, len);
    else if (off == 0x00)
        dp8390_write_cr(dev->dp8390, val);
    else
        switch (dev->dp8390->CR.pgsel) {
            case 0x00:
                dp8390_page0_write(dev->dp8390, off, val, len);
                break;
            case 0x01:
                dp8390_page1_write(dev->dp8390, off, val, len);
                break;
            case 0x02:
                dp8390_page2_write(dev->dp8390, off, val, len);
                break;
            case 0x03:
                page3_write(dev, off, val, len);
                break;
            default:
                nelog(3, "%s: unknown value of pgsel in write - %d\n",
                      dev->name, dev->dp8390->CR.pgsel);
                break;
        }
}

static void
nic_writeb(uint16_t addr, uint8_t val, void *priv)
{
    nic_write((nic_t *) priv, addr, val, 1);
}

static void
nic_writew(uint16_t addr, uint16_t val, void *priv)
{
    nic_write((nic_t *) priv, addr, val, 2);
}

static void
nic_writel(uint16_t addr, uint32_t val, void *priv)
{
    nic_write((nic_t *) priv, addr, val, 4);
}

static void nic_ioset(nic_t *dev, uint16_t addr);
static void nic_ioremove(nic_t *dev, uint16_t addr);

static void
nic_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    if (ld)
        return;

    nic_t *dev = (nic_t *) priv;

    if (dev->base_address) {
        nic_ioremove(dev, dev->base_address);
        dev->base_address = 0;
    }

    dev->base_address = config->io[0].base;
    dev->base_irq     = config->irq[0].irq;

    if (config->activate && (dev->base_address != ISAPNP_IO_DISABLED))
        nic_ioset(dev, dev->base_address);
}

static void
nic_pnp_csn_changed(uint8_t csn, void *priv)
{
    nic_t *dev = (nic_t *) priv;

    dev->pnp_csnsav = csn;
}

static uint8_t
nic_pnp_read_vendor_reg(uint8_t ld, uint8_t reg, void *priv)
{
    if (ld != 0)
        return 0x00;

    const nic_t *dev = (nic_t *) priv;

    switch (reg) {
        case 0xF0:
            return dev->config0;

        case 0xF2:
            return dev->config2;

        case 0xF3:
            return dev->config3;

        case 0xF5:
            return dev->pnp_csnsav;

        default:
            break;
    }

    return 0x00;
}

static void
nic_pnp_write_vendor_reg(uint8_t ld, uint8_t reg, uint8_t val, void *priv)
{
    nic_t *dev = (nic_t *) priv;

    if ((ld == 0) && (reg == 0xf6) && (val & 0x04)) {
        uint8_t csn = dev->pnp_csnsav;
        isapnp_set_csn(dev->pnp_card, 0);
        dev->pnp_csnsav = csn;
    }
}

static void
nic_ioset(nic_t *dev, uint16_t addr)
{
    if (dev->is_pci) {
        io_sethandler(addr, 32,
                      nic_readb, nic_readw, nic_readl,
                      nic_writeb, nic_writew, nic_writel, dev);
    } else {
        io_sethandler(addr, 16,
                      nic_readb, NULL, NULL,
                      nic_writeb, NULL, NULL, dev);
        if (dev->is_8bit) {
            io_sethandler(addr + 16, 16,
                          nic_readb, NULL, NULL,
                          nic_writeb, NULL, NULL, dev);
        } else {
            io_sethandler(addr + 16, 16,
                          nic_readb, nic_readw, NULL,
                          nic_writeb, nic_writew, NULL, dev);
        }
    }
}

static void
nic_ioremove(nic_t *dev, uint16_t addr)
{
    if (dev->is_pci) {
        io_removehandler(addr, 32,
                         nic_readb, nic_readw, nic_readl,
                         nic_writeb, nic_writew, nic_writel, dev);
    } else {
        io_removehandler(addr, 16,
                         nic_readb, NULL, NULL,
                         nic_writeb, NULL, NULL, dev);
        if (dev->is_8bit) {
            io_removehandler(addr + 16, 16,
                             nic_readb, NULL, NULL,
                             nic_writeb, NULL, NULL, dev);
        } else {
            io_removehandler(addr + 16, 16,
                             nic_readb, nic_readw, NULL,
                             nic_writeb, nic_writew, NULL, dev);
        }
    }
}

static void
nic_update_bios(nic_t *dev)
{
    int reg_bios_enable;

    reg_bios_enable = 1;

    if (!dev->has_bios)
        return;

    if (dev->is_pci)
        reg_bios_enable = dev->pci_bar[1].addr_regs[0] & 0x01;

    /* PCI BIOS stuff, just enable_disable. */
    if (reg_bios_enable) {
        mem_mapping_set_addr(&dev->bios_rom.mapping,
                             dev->bios_addr, dev->bios_size);
        nelog(1, "%s: BIOS now at: %06X\n", dev->name, dev->bios_addr);
    } else {
        nelog(1, "%s: BIOS disabled\n", dev->name);
        mem_mapping_disable(&dev->bios_rom.mapping);
    }
}

static uint8_t
nic_pci_read(UNUSED(int func), int addr, void *priv)
{
    const nic_t  *dev = (nic_t *) priv;
    uint8_t       ret = 0x00;

    switch (addr) {
        case 0x00: /* PCI_VID_LO */
        case 0x01: /* PCI_VID_HI */
            ret = dev->pci_regs[addr];
            break;

        case 0x02: /* PCI_DID_LO */
        case 0x03: /* PCI_DID_HI */
            ret = dev->pci_regs[addr];
            break;

        case 0x04: /* PCI_COMMAND_LO */
        case 0x05: /* PCI_COMMAND_HI */
            ret = dev->pci_regs[addr];
            break;

        case 0x06: /* PCI_STATUS_LO */
        case 0x07: /* PCI_STATUS_HI */
            ret = dev->pci_regs[addr];
            break;

        case 0x08:      /* PCI_REVID */
            ret = 0x00; /* Rev. 00 */
            break;
        case 0x09:      /* PCI_PIFR */
            ret = 0x00; /* Rev. 00 */
            break;

        case 0x0A: /* PCI_SCR */
            ret = dev->pci_regs[addr];
            break;

        case 0x0B: /* PCI_BCR */
            ret = dev->pci_regs[addr];
            break;

        case 0x10: /* PCI_BAR 7:5 */
            ret = (dev->pci_bar[0].addr_regs[0] & 0xe0) | 0x01;
            break;
        case 0x11: /* PCI_BAR 15:8 */
            ret = dev->pci_bar[0].addr_regs[1];
            break;
        case 0x12: /* PCI_BAR 23:16 */
            ret = dev->pci_bar[0].addr_regs[2];
            break;
        case 0x13: /* PCI_BAR 31:24 */
            ret = dev->pci_bar[0].addr_regs[3];
            break;

        case 0x2C: /* PCI_SVID_LO */
        case 0x2D: /* PCI_SVID_HI */
            ret = dev->pci_regs[addr];
            break;

        case 0x2E: /* PCI_SID_LO */
        case 0x2F: /* PCI_SID_HI */
            ret = dev->pci_regs[addr];
            break;

        case 0x30: /* PCI_ROMBAR */
            ret = dev->pci_bar[1].addr_regs[0] & 0x01;
            break;
        case 0x31: /* PCI_ROMBAR 15:11 */
            ret = dev->pci_bar[1].addr_regs[1] & 0x80;
            break;
        case 0x32: /* PCI_ROMBAR 23:16 */
            ret = dev->pci_bar[1].addr_regs[2];
            break;
        case 0x33: /* PCI_ROMBAR 31:24 */
            ret = dev->pci_bar[1].addr_regs[3];
            break;

        case 0x3C: /* PCI_ILR */
            ret = dev->pci_regs[addr];
            break;

        case 0x3D: /* PCI_IPR */
            ret = dev->pci_regs[addr];
            break;

        default:
            break;
    }

    nelog(2, "%s: PCI_Read(%d, %04x) = %02x\n", dev->name, func, addr, ret);

    return ret;
}

static void
nic_pci_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    nic_t  *dev = (nic_t *) priv;
    uint8_t valxor;

    nelog(2, "%s: PCI_Write(%d, %04x, %02x)\n", dev->name, func, addr, val);

    switch (addr) {
        case 0x04: /* PCI_COMMAND_LO */
            valxor = (val & 0x03) ^ dev->pci_regs[addr];
            if (valxor & PCI_COMMAND_IO) {
                nic_ioremove(dev, dev->base_address);
                if ((dev->base_address != 0) && (val & PCI_COMMAND_IO)) {
                    nic_ioset(dev, dev->base_address);
                }
            }
            dev->pci_regs[addr] = val & 0x03;
            break;

        case 0x10:       /* PCI_BAR */
            val &= 0xe0; /* 0xe0 acc to RTL DS */
            val |= 0x01; /* re-enable IOIN bit */
            fallthrough;

        case 0x11: /* PCI_BAR */
        case 0x12: /* PCI_BAR */
        case 0x13: /* PCI_BAR */
            /* Remove old I/O. */
            nic_ioremove(dev, dev->base_address);

            /* Set new I/O as per PCI request. */
            dev->pci_bar[0].addr_regs[addr & 3] = val;

            /* Then let's calculate the new I/O base. */
            dev->base_address = dev->pci_bar[0].addr & 0xffe0;

            /* Log the new base. */
            nelog(1, "%s: PCI: new I/O base is %04X\n",
                  dev->name, dev->base_address);
            /* We're done, so get out of the here. */
            if (dev->pci_regs[4] & PCI_COMMAND_IO) {
                if (dev->base_address != 0) {
                    nic_ioset(dev, dev->base_address);
                }
            }
            break;

        case 0x30: /* PCI_ROMBAR */
        case 0x31: /* PCI_ROMBAR */
        case 0x32: /* PCI_ROMBAR */
        case 0x33: /* PCI_ROMBAR */
            dev->pci_bar[1].addr_regs[addr & 3] = val;
#if 0
            dev->pci_bar[1].addr_regs[1] &= dev->bios_mask;
#endif
            dev->pci_bar[1].addr &= 0xffff8001;
            dev->bios_addr = dev->pci_bar[1].addr & 0xffff8000;
            nic_update_bios(dev);
            return;

        case 0x3C: /* PCI_ILR */
            nelog(1, "%s: IRQ now: %i\n", dev->name, val);
            dev->base_irq       = val;
            dev->pci_regs[addr] = dev->base_irq;
            return;

        default:
            break;
    }
}

static void
nic_rom_init(nic_t *dev, char *s)
{
    uint32_t temp;
    FILE    *fp;

    if (s == NULL)
        return;

    if (dev->bios_addr == 0)
        return;

    if ((fp = rom_fopen(s, "rb")) != NULL) {
        fseek(fp, 0L, SEEK_END);
        temp = ftell(fp);
        fclose(fp);
        dev->bios_size = 0x10000;
        if (temp <= 0x8000)
            dev->bios_size = 0x8000;
        if (temp <= 0x4000)
            dev->bios_size = 0x4000;
        if (temp <= 0x2000)
            dev->bios_size = 0x2000;
        dev->bios_mask = (dev->bios_size >> 8) & 0xff;
        dev->bios_mask = (0x100 - dev->bios_mask) & 0xff;
    } else {
        dev->bios_addr = 0x00000;
        dev->bios_size = 0;
        return;
    }

    /* Create a memory mapping for the space. */
    rom_init(&dev->bios_rom, s, dev->bios_addr,
             dev->bios_size, dev->bios_size - 1, 0, MEM_MAPPING_EXTERNAL);

    nelog(1, "%s: BIOS configured at %06lX (size %ld)\n",
          dev->name, dev->bios_addr, dev->bios_size);
}

static uint8_t
nic_mca_read(int port, void *priv)
{
    const nic_t *dev = (nic_t *) priv;

    return (dev->pos_regs[port & 7]);
}

#define MCA_611F_IO_PORTS                           \
    {                                               \
        0x300, 0x340, 0x320, 0x360, 0x1300, 0x1340, \
            0x1320, 0x1360                          \
    }

#define MCA_611F_IRQS              \
    {                              \
        2, 3, 4, 5, 10, 11, 12, 15 \
    }

static void
nic_mca_write(int port, uint8_t val, void *priv)
{
    nic_t   *dev    = (nic_t *) priv;
    uint16_t base[] = MCA_611F_IO_PORTS;
    int8_t   irq[]  = MCA_611F_IRQS;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    /* Save the MCA register value. */
    dev->pos_regs[port & 7] = val;

    nic_ioremove(dev, dev->base_address);

    /* This is always necessary so that the old handler doesn't remain. */
    /* Get the new assigned I/O base address. */
    dev->base_address = base[(dev->pos_regs[2] & 0xE0) >> 4];

    /* Save the new IRQ values. */
    dev->base_irq = irq[(dev->pos_regs[2] & 0xE) >> 1];

    dev->bios_addr = 0x0000;
    dev->has_bios  = 0;

    /*
     * The PS/2 Model 80 BIOS always enables a card if it finds one,
     * even if no resources were assigned yet (because we only added
     * the card, but have not run AutoConfig yet...)
     *
     * So, remove current address, if any.
     */

    /* Initialize the device if fully configured. */
    if (dev->pos_regs[2] & 0x01) {
        /* Card enabled; register (new) I/O handler. */

        nic_ioset(dev, dev->base_address);

        nic_reset(dev);

        nelog(2, "EtherNext/MC: Port=%04x, IRQ=%d\n", dev->base_address, dev->base_irq);
    }
}

static uint8_t
nic_mca_feedb(void *priv)
{
    const nic_t *dev = (nic_t *) priv;

    return (dev->pos_regs[2] & 0x01);
}

static void *
nic_init(const device_t *info)
{
    uint32_t mac;
    uint32_t mac_oui;
    char    *rom = NULL;
    nic_t   *dev;
    int      set_oui = 0;

    dev = calloc(1, sizeof(nic_t));
    dev->name  = info->name;
    dev->board = info->local;

    if (dev->board >= NE2K_RTL8019AS_PNP) {
        dev->base_address = 0x340;
        dev->base_irq     = 12;
        if (dev->board == NE2K_RTL8029AS) {
            dev->bios_addr = 0xD0000;
            dev->has_bios  = device_get_config_int("bios");
        } else {
            dev->bios_addr = 0x00000;
            dev->has_bios  = 0;
        }
    } else {
        if (dev->board != NE2K_ETHERNEXT_MC) {
            dev->base_address = device_get_config_hex16("base");
            dev->base_irq     = device_get_config_int("irq");
            if ((dev->board == NE2K_NE2000) || (dev->board == NE2K_NE2000_COMPAT) || 
                (dev->board == NE2K_NE2000_COMPAT_8BIT) ) {
                dev->bios_addr = device_get_config_hex20("bios_addr");
                dev->has_bios  = !!dev->bios_addr;
            } else {
                dev->bios_addr = 0x00000;
                dev->has_bios  = 0;
            }
        } else {
            mca_add(nic_mca_read, nic_mca_write, nic_mca_feedb, NULL, dev);
        }
    }

    /* See if we have a local MAC address configured. */
    mac = device_get_config_mac("mac", -1);

    /* Set up our BIA. */
    if (mac & 0xff000000) {
        /* Generate new local MAC. */
        dev->maclocal[3] = random_generate();
        dev->maclocal[4] = random_generate();
        dev->maclocal[5] = random_generate();
        mac              = (((int) dev->maclocal[3]) << 16);
        mac             |= (((int) dev->maclocal[4]) << 8);
        mac             |= ((int) dev->maclocal[5]);
        device_set_config_mac("mac", mac);
    } else {
        dev->maclocal[3] = (mac >> 16) & 0xff;
        dev->maclocal[4] = (mac >> 8) & 0xff;
        dev->maclocal[5] = (mac & 0xff);
    }

    dev->dp8390            = device_add_inst(&dp8390_device, dp3890_inst++);
    dev->dp8390->priv      = dev;
    dev->dp8390->interrupt = nic_interrupt;

    switch (dev->board) {
        case NE2K_NE1000:
            dev->maclocal[0] = 0x00; /* 00:00:D8 (Novell OID) */
            dev->maclocal[1] = 0x00;
            dev->maclocal[2] = 0xD8;
            dev->is_8bit     = 1;
            rom              = NULL;
            dp8390_set_defaults(dev->dp8390, DP8390_FLAG_CHECK_CR | DP8390_FLAG_CLEAR_IRQ);
            dp8390_mem_alloc(dev->dp8390, 0x2000, 0x2000);
            break;

        case NE2K_NE1000_COMPAT:
            dev->maclocal[0] = 0x00; /* 00:86:B0 (86Box OID) */
            dev->maclocal[1] = 0x86;
            dev->maclocal[2] = 0xB0;
            dev->is_8bit     = 1;
            rom              = NULL;
            set_oui          = 1;
            dp8390_set_defaults(dev->dp8390, DP8390_FLAG_CHECK_CR | DP8390_FLAG_CLEAR_IRQ);
            dp8390_mem_alloc(dev->dp8390, 0x2000, 0x2000);
            break;

        case NE2K_NE2000:
            dev->maclocal[0] = 0x00; /* 00:00:D8 (Novell OID) */
            dev->maclocal[1] = 0x00;
            dev->maclocal[2] = 0xD8;
            rom              = ROM_PATH_NE2000;
            dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC | DP8390_FLAG_CHECK_CR | DP8390_FLAG_CLEAR_IRQ);
            dp8390_mem_alloc(dev->dp8390, 0x4000, 0x4000);
            break;

        case NE2K_NE2000_COMPAT:
            dev->maclocal[0] = 0x00; /* 00:86:B0 (86Box OID) */
            dev->maclocal[1] = 0x86;
            dev->maclocal[2] = 0xB0;
            rom              = ROM_PATH_NE2000;
            set_oui          = 1;
            dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC | DP8390_FLAG_CHECK_CR | DP8390_FLAG_CLEAR_IRQ);
            dp8390_mem_alloc(dev->dp8390, 0x4000, 0x4000);
            break;

        case NE2K_NE2000_COMPAT_8BIT:
            dev->maclocal[0] = 0x00; /* 00:86:B0 (86Box OID) */
            dev->maclocal[1] = 0x86;
            dev->maclocal[2] = 0xB0;
            dev->is_8bit     = 1;
            rom              = ROM_PATH_NE2000;
            set_oui          = 1;
            dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC | DP8390_FLAG_CHECK_CR | DP8390_FLAG_CLEAR_IRQ);
            dp8390_mem_alloc(dev->dp8390, 0x4000, 0x4000);
            break;

        case NE2K_ETHERNEXT_MC:
            dev->maclocal[0] = 0x00; /* 00:00:D8 (Networth Inc. OID) */
            dev->maclocal[1] = 0x00;
            dev->maclocal[2] = 0x79;
            dev->pos_regs[0] = 0x1F;
            dev->pos_regs[1] = 0x61;
            rom              = NULL;
            dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC | DP8390_FLAG_CHECK_CR | DP8390_FLAG_CLEAR_IRQ);
            dp8390_mem_alloc(dev->dp8390, 0x4000, 0x4000);
            break;

        case NE2K_DE220P:
            dev->maclocal[0] = 0x00; /* 00:80:C8 (D-Link OID) */
            dev->maclocal[1] = 0x80;
            dev->maclocal[2] = 0xC8;
            rom              = NULL;
            dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC | DP8390_FLAG_CLEAR_IRQ);
            dp8390_set_id(dev->dp8390, 0x50, 0x70);
            dp8390_mem_alloc(dev->dp8390, 0x4000, 0x8000);
            break;

        case NE2K_RTL8019AS_PNP:
        case NE2K_RTL8029AS:
            dev->is_pci      = (dev->board == NE2K_RTL8029AS) ? 1 : 0;
            dev->maclocal[0] = 0x00; /* 00:E0:4C (Realtek OID) */
            dev->maclocal[1] = 0xE0;
            dev->maclocal[2] = 0x4C;
            rom              = (dev->board == NE2K_RTL8019AS_PNP) ? ROM_PATH_RTL8019 : ROM_PATH_RTL8029;
            if (dev->is_pci)
                dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC);
            else
                dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC | DP8390_FLAG_CLEAR_IRQ);
            dp8390_set_id(dev->dp8390, 0x50, (dev->board == NE2K_RTL8019AS_PNP) ? 0x70 : 0x43);
            dp8390_mem_alloc(dev->dp8390, 0x4000, 0x8000);
            break;

        default:
            break;
    }

    
    if (set_oui) {
        /* See if we have a local MAC address configured. */
        mac_oui = device_get_config_mac("mac_oui", -1);

        /* Set up our BIA. */
        if (mac_oui & 0xff000000) {
            mac_oui          = (((int) dev->maclocal[0]) << 16);
            mac_oui         |= (((int) dev->maclocal[1]) << 8);
            mac_oui         |= ((int) dev->maclocal[2]);
            device_set_config_mac("mac", mac);
        } else {
            dev->maclocal[0] = (mac_oui >> 16) & 0xff;
            dev->maclocal[1] = (mac_oui >> 8) & 0xff;
            dev->maclocal[2] = (mac_oui & 0xff);
        }
    }

    memcpy(dev->dp8390->physaddr, dev->maclocal, sizeof(dev->maclocal));

    nelog(2, "%s: I/O=%04x, IRQ=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
          dev->name, dev->base_address, dev->base_irq,
          dev->dp8390->physaddr[0], dev->dp8390->physaddr[1], dev->dp8390->physaddr[2],
          dev->dp8390->physaddr[3], dev->dp8390->physaddr[4], dev->dp8390->physaddr[5]);

    /*
     * Make this device known to the I/O system.
     * PnP and PCI devices start with address spaces inactive.
     */
    if ((dev->board < NE2K_RTL8019AS_PNP) && (dev->board != NE2K_ETHERNEXT_MC))
        nic_ioset(dev, dev->base_address);

    /* Set up our BIOS ROM space, if any. */
    nic_rom_init(dev, rom);

    if (dev->board >= NE2K_RTL8019AS_PNP) {
        if (dev->is_pci) {
            /*
             * Configure the PCI space registers.
             *
             * We do this here, so the I/O routines are generic.
             */
            memset(dev->pci_regs, 0, PCI_REGSIZE);

            dev->pci_regs[0x00] = (PCI_VENDID & 0xff);
            dev->pci_regs[0x01] = (PCI_VENDID >> 8);
            dev->pci_regs[0x02] = (PCI_DEVID & 0xff);
            dev->pci_regs[0x03] = (PCI_DEVID >> 8);

            dev->pci_regs[0x04] = 0x03; /* IOEN */
            dev->pci_regs[0x05] = 0x00;
            dev->pci_regs[0x07] = 0x02; /* DST0, medium devsel */

            dev->pci_regs[0x09] = 0x00; /* PIFR */

            dev->pci_regs[0x0B] = 0x02; /* BCR: Network Controller */
            dev->pci_regs[0x0A] = 0x00; /* SCR: Ethernet */

            dev->pci_regs[0x2C] = (PCI_VENDID & 0xff);
            dev->pci_regs[0x2D] = (PCI_VENDID >> 8);
            dev->pci_regs[0x2E] = (PCI_DEVID & 0xff);
            dev->pci_regs[0x2F] = (PCI_DEVID >> 8);

            dev->pci_regs[0x3D] = PCI_INTA; /* PCI_IPR */

            /* Enable our address space in PCI. */
            dev->pci_bar[0].addr_regs[0] = 0x01;

            /* Enable our BIOS space in PCI, if needed. */
            if (dev->bios_addr > 0) {
                dev->pci_bar[1].addr         = 0xFFFF8000;
                dev->pci_bar[1].addr_regs[1] = dev->bios_mask;
            } else {
                dev->pci_bar[1].addr = 0;
                dev->bios_size       = 0;
            }

            mem_mapping_disable(&dev->bios_rom.mapping);

            /* Add device to the PCI bus, keep its slot number. */
            pci_add_card(PCI_ADD_NORMAL, nic_pci_read, nic_pci_write, dev, &dev->pci_slot);
        }

        /* Initialize the RTL80x9 EEPROM. */
        memset(dev->eeprom, 0x00, sizeof(dev->eeprom));

        if (dev->board == NE2K_RTL8029AS) {
            memcpy(&dev->eeprom[0x02], dev->maclocal, 6);

            dev->eeprom[0x76] = dev->eeprom[0x7A] = dev->eeprom[0x7E] = (PCI_DEVID & 0xff);
            dev->eeprom[0x77] = dev->eeprom[0x7B] = dev->eeprom[0x7F] = (PCI_DEVID >> 8);
            dev->eeprom[0x78] = dev->eeprom[0x7C] = (PCI_VENDID & 0xff);
            dev->eeprom[0x79] = dev->eeprom[0x7D] = (PCI_VENDID >> 8);
        } else {
            const char *pnp_rom_file = NULL;
            int pnp_rom_len = 0x4a;
            switch (dev->board) {
                case NE2K_RTL8019AS_PNP:
                    pnp_rom_file = "roms/network/rtl8019as/RTL8019A.BIN";
                    break;

                case NE2K_DE220P:
                    pnp_rom_file = "roms/network/de220p/dlk2201a.bin";
                    pnp_rom_len = 0x43;
                    break;

                default:
                    break;
            }

            uint8_t *pnp_rom = NULL;
            if (pnp_rom_file) {
                FILE *fp = rom_fopen(pnp_rom_file, "rb");
                if (fp) {
                    if (fread(&dev->eeprom[0x12], 1, pnp_rom_len, fp) == pnp_rom_len)
                        pnp_rom = &dev->eeprom[0x12];
                    fclose(fp);
                }
            }

            switch (info->local) {
                case NE2K_RTL8019AS_PNP:
                case NE2K_DE220P:
                    dev->pnp_card = isapnp_add_card(pnp_rom, pnp_rom_len,
                                                    nic_pnp_config_changed, nic_pnp_csn_changed,
                                                    nic_pnp_read_vendor_reg, nic_pnp_write_vendor_reg,
                                                    dev);
                    break;

                default:
                    break;
            }
        }
    }

    if (dev->board != NE2K_ETHERNEXT_MC)
        /* Reset the board. */
        nic_reset(dev);

    /* Attach ourselves to the network module. */
    dev->dp8390->card = network_attach(dev->dp8390, dev->dp8390->physaddr, dp8390_rx, NULL);

    nelog(1, "%s: %s attached IO=0x%X IRQ=%d\n", dev->name,
          dev->is_pci ? "PCI" : "ISA", dev->base_address, dev->base_irq);

    return dev;
}

static void
nic_close(void *priv)
{
    nic_t *dev = (nic_t *) priv;

    nelog(1, "%s: closed\n", dev->name);

    free(dev);
}

static int
rtl8019as_available(void)
{
    return rom_present("roms/network/rtl8019as/RTL8019A.BIN");
}

static int
de220p_available(void)
{
    return rom_present("roms/network/de220p/dlk2201a.bin");
}

// clang-format off
static const device_config_t ne1000_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x300,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            /* Source: Windows 95 .INF file. */
            { .description = "0x300", .value = 0x300 },
            { .description = "0x320", .value = 0x320 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x360", .value = 0x360 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            /* Source: Windows 95 .INF file. */
            { .description = "IRQ 2",  .value =  2 },
            { .description = "IRQ 3",  .value =  3 },
            { .description = "IRQ 4",  .value =  4 },
            { .description = "IRQ 5",  .value =  5 },
            { .description = "IRQ 9",  .value =  9 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t ne1000_compat_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x300,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            /* Source: Windows 95 .INF file. */
            { .description = "0x200", .value = 0x200 },
            { .description = "0x220", .value = 0x220 },
            { .description = "0x240", .value = 0x240 },
            { .description = "0x260", .value = 0x260 },
            { .description = "0x280", .value = 0x280 },
            { .description = "0x2a0", .value = 0x2a0 },
            { .description = "0x2c0", .value = 0x2c0 },
            { .description = "0x2e0", .value = 0x2e0 },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x320", .value = 0x320 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x360", .value = 0x360 },
            { .description = "0x380", .value = 0x380 },
            { .description = "0x3a0", .value = 0x3a0 },
            { .description = "0x3c0", .value = 0x3c0 },
            { .description = "0x3e0", .value = 0x3e0 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            /* Source: Windows 95 .INF file. */
            { .description = "IRQ 2",  .value =  2 },
            { .description = "IRQ 3",  .value =  3 },
            { .description = "IRQ 4",  .value =  4 },
            { .description = "IRQ 5",  .value =  5 },
            { .description = "IRQ 7",  .value =  7 },
            { .description = "IRQ 9",  .value =  9 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "mac_oui",
        .description    = "MAC Address OUI",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t ne2000_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x300,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            /* Source: Windows 95 .INF file. */
            { .description = "0x300", .value = 0x300 },
            { .description = "0x320", .value = 0x320 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x360", .value = 0x360 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            /* Source: Windows 95 .INF file. */
            { .description = "IRQ 2",  .value =  2 },
            { .description = "IRQ 3",  .value =  3 },
            { .description = "IRQ 4",  .value =  4 },
            { .description = "IRQ 5",  .value =  5 },
            { .description = "IRQ 9",  .value =  9 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name = "bios_addr",
        .description = "BIOS Address",
        .type = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0x00000 },
            { .description = "D000",     .value = 0xD0000 },
            { .description = "D800",     .value = 0xD8000 },
            { .description = "C800",     .value = 0xC8000 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t ne2000_compat_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x300,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            /* Source: Windows 95 .INF file. */
            { .description = "0x200", .value = 0x200 },
            { .description = "0x220", .value = 0x220 },
            { .description = "0x240", .value = 0x240 },
            { .description = "0x260", .value = 0x260 },
            { .description = "0x280", .value = 0x280 },
            { .description = "0x2a0", .value = 0x2a0 },
            { .description = "0x2c0", .value = 0x2c0 },
            { .description = "0x2e0", .value = 0x2e0 },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x320", .value = 0x320 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x360", .value = 0x360 },
            { .description = "0x380", .value = 0x380 },
            { .description = "0x3a0", .value = 0x3a0 },
            { .description = "0x3c0", .value = 0x3c0 },
            { .description = "0x3e0", .value = 0x3e0 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 10,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            /* Source: Windows 95 .INF file - not giving impossible IRQ's
                       such as 6, 8, or 13. */
            { .description = "IRQ 3",  .value =  3 },
            { .description = "IRQ 4",  .value =  4 },
            { .description = "IRQ 5",  .value =  5 },
            { .description = "IRQ 7",  .value =  7 },
            { .description = "IRQ 9",  .value =  9 },
            { .description = "IRQ 10", .value = 10 },
            { .description = "IRQ 11", .value = 11 },
            { .description = "IRQ 12", .value = 12 },
            { .description = "IRQ 14", .value = 14 },
            { .description = "IRQ 15", .value = 15 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "mac_oui",
        .description    = "MAC Address OUI",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0x00000 },
            { .description = "D000",     .value = 0xD0000 },
            { .description = "D800",     .value = 0xD8000 },
            { .description = "C800",     .value = 0xC8000 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t ne2000_compat_8bit_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x320,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            /* Source: board docs, https://github.com/skiselev/isa8_eth */
            { .description = "0x200", .value = 0x200 },
            { .description = "0x220", .value = 0x220 },
            { .description = "0x240", .value = 0x240 },
            { .description = "0x260", .value = 0x260 },
            { .description = "0x280", .value = 0x280 },
            { .description = "0x2a0", .value = 0x2a0 },
            { .description = "0x2c0", .value = 0x2c0 },
            { .description = "0x2e0", .value = 0x2e0 },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x320", .value = 0x320 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x360", .value = 0x360 },
            { .description = "0x380", .value = 0x380 },
            { .description = "0x3a0", .value = 0x3a0 },
            { .description = "0x3c0", .value = 0x3c0 },
            { .description = "0x3e0", .value = 0x3e0 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            /* Source: board docs, https://github.com/skiselev/isa8_eth */
            { .description = "IRQ 2",  .value =  2 },
            { .description = "IRQ 3",  .value =  3 },
            { .description = "IRQ 4",  .value =  4 },
            { .description = "IRQ 5",  .value =  5 },
            { .description = "IRQ 9",  .value =  9 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "mac_oui",
        .description    = "MAC Address OUI",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
			/* Source: board docs, https://github.com/skiselev/isa8_eth */
            { .description = "Disabled", .value = 0x00000 },
            { .description = "C000",     .value = 0xC0000 },
            { .description = "C400",     .value = 0xC4000 },
            { .description = "C800",     .value = 0xC8000 },
            { .description = "CC00",     .value = 0xCC000 },
            { .description = "D000",     .value = 0xD0000 },
            { .description = "D400",     .value = 0xD4000 },
            { .description = "D800",     .value = 0xD8000 },
            { .description = "DC00",     .value = 0xDC000 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};


static const device_config_t rtl8019as_config[] = {
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t rtl8029as_config[] = {
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
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t mca_mac_config[] = {
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t ne1000_device = {
    .name          = "Novell NE1000",
    .internal_name = "novell_ne1k",
    .flags         = DEVICE_ISA,
    .local         = NE2K_NE1000,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ne1000_config
};

const device_t ne1000_compat_device = {
    .name          = "NE1000 Compatible",
    .internal_name = "ne1k",
    .flags         = DEVICE_ISA,
    .local         = NE2K_NE1000_COMPAT,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ne1000_compat_config
};

const device_t ne2000_device = {
    .name          = "Novell NE2000",
    .internal_name = "novell_ne2k",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = NE2K_NE2000,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ne2000_config
};

const device_t ne2000_compat_device = {
    .name          = "NE2000 Compatible",
    .internal_name = "ne2k",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = NE2K_NE2000_COMPAT,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ne2000_compat_config
};

const device_t ne2000_compat_8bit_device = {
    .name          = "NE2000 Compatible 8-bit",
    .internal_name = "ne2k8",
    .flags         = DEVICE_ISA, 
    .local         = NE2K_NE2000_COMPAT_8BIT,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ne2000_compat_8bit_config
};

const device_t ethernext_mc_device = {
    .name          = "NetWorth EtherNext/MC",
    .internal_name = "ethernextmc",
    .flags         = DEVICE_MCA,
    .local         = NE2K_ETHERNEXT_MC,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mca_mac_config
};

const device_t rtl8019as_pnp_device = {
    .name          = "Realtek RTL8019AS",
    .internal_name = "ne2kpnp",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = NE2K_RTL8019AS_PNP,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = NULL,
    .available     = rtl8019as_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = rtl8019as_config
};

const device_t de220p_device = {
    .name          = "D-Link DE-220P",
    .internal_name = "de220p",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = NE2K_DE220P,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = NULL,
    .available     = de220p_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = rtl8019as_config
};

const device_t rtl8029as_device = {
    .name          = "Realtek RTL8029AS",
    .internal_name = "ne2kpci",
    .flags         = DEVICE_PCI,
    .local         = NE2K_RTL8029AS,
    .init          = nic_init,
    .close         = nic_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = rtl8029as_config
};

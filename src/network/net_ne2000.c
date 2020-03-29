/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the following network controllers:
 *			- Novell NE1000 (ISA 8-bit);
 *			- Novell NE2000 (ISA 16-bit);
 *			- Novell NE/2 compatible (NetWorth Inc. Ethernext/MC) (MCA 16-bit);
 *			- Realtek RTL8019AS (ISA 16-bit, PnP);
 *			- Realtek RTL8029AS (PCI).
 *
 *
 *
 * Based on	@(#)ne2k.cc v1.56.2.1 2004/02/02 22:37:22 cbothamy
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Peter Grehan, <grehan@iprg.nokia.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 *		Copyright 2016-2018 Miran Grca.
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
#include <86box/mca.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/random.h>
#include <86box/device.h>
#include <86box/network.h>
#include <86box/net_dp8390.h>
#include <86box/net_ne2000.h>
#include <86box/bswap.h>


enum {
	PNP_PHASE_WAIT_FOR_KEY = 0,
	PNP_PHASE_CONFIG,
	PNP_PHASE_ISOLATION,
	PNP_PHASE_SLEEP
};

/* ROM BIOS file paths. */
#define ROM_PATH_NE1000		L"roms/network/ne1000/ne1000.rom"
#define ROM_PATH_NE2000		L"roms/network/ne2000/ne2000.rom"
#define ROM_PATH_RTL8019	L"roms/network/rtl8019as/rtl8019as.rom"
#define ROM_PATH_RTL8029	L"roms/network/rtl8029as/rtl8029as.rom"

/* PCI info. */
#define PNP_VENDID		0x4a8c		/* Realtek, Inc */
#define PCI_VENDID		0x10ec		/* Realtek, Inc */
#define PNP_DEVID		0x8019		/* RTL8029AS */
#define PCI_DEVID		0x8029		/* RTL8029AS */
#define PCI_REGSIZE		256		/* size of PCI space */


uint8_t pnp_init_key[32] = { 0x6A, 0xB5, 0xDA, 0xED, 0xF6, 0xFB, 0x7D, 0xBE,
			     0xDF, 0x6F, 0x37, 0x1B, 0x0D, 0x86, 0xC3, 0x61,
			     0xB0, 0x58, 0x2C, 0x16, 0x8B, 0x45, 0xA2, 0xD1,
			     0xE8, 0x74, 0x3A, 0x9D, 0xCE, 0xE7, 0x73, 0x39 };


typedef struct {
    dp8390_t	*dp8390;
    const char	*name;
    int		board;
    int		is_pci, is_mca, is_8bit;
    uint32_t	base_address;
    int		base_irq;
    uint32_t	bios_addr,
		bios_size,
		bios_mask;
    int		card;			/* PCI card slot */
    int		has_bios, pad;
    uint8_t	pnp_regs[256];
    uint8_t	pnp_res_data[256];
    bar_t	pci_bar[2];
    uint8_t	pci_regs[PCI_REGSIZE];
    uint8_t	eeprom[128];		/* for RTL8029AS */
    rom_t	bios_rom;
    uint8_t	pnp_phase;
    uint8_t	pnp_magic_count;
    uint8_t	pnp_address;
    uint8_t	pnp_res_pos;
    uint8_t	pnp_csn;
    uint8_t	pnp_activate;
    uint8_t	pnp_io_check;
    uint8_t	pnp_csnsav;
    uint8_t	pnp_id_checksum;
    uint8_t	pnp_serial_read_pos;
    uint8_t	pnp_serial_read_pair;
    uint8_t	pnp_serial_read;
    uint8_t	maclocal[6];		/* configured MAC (local) address */
    uint16_t	pnp_read;
    uint64_t	pnp_id;

    /* RTL8019AS/RTL8029AS registers */
    uint8_t	config0, config2, config3;
    uint8_t	_9346cr;
    uint32_t	pad0;

    /* POS registers, MCA boards only */
    uint8_t pos_regs[8];
} nic_t;


#ifdef ENABLE_NIC_LOG
int nic_do_log = ENABLE_NIC_LOG;

static void
nelog(int lvl, const char *fmt, ...)
{
    va_list ap;

    if (nic_do_log >= lvl) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define nelog(lvl, fmt, ...)
#endif


static void
nic_interrupt(void *priv, int set)
{
    nic_t *dev = (nic_t *) priv;

    if (dev->is_pci) {
	if (set)
		pci_set_irq(dev->card, PCI_INTA);
	  else
		pci_clear_irq(dev->card, PCI_INTA);
    } else {
	if (set)
		picint(1<<dev->base_irq);
	  else
		picintc(1<<dev->base_irq);
	}
}


/* reset - restore state to power-up, cancelling all i/o */
static void
nic_reset(void *priv)
{
    nic_t *dev = (nic_t *)priv;

    nelog(1, "%s: reset\n", dev->name);

    dp8390_reset(dev->dp8390);
}


static void
nic_soft_reset(void *priv)
{
    nic_t *dev = (nic_t *)priv;

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

    switch(off) {
	case 0x00:	/* Data register */
		/* A read remote-DMA command must have been issued,
		   and the source-address and length registers must
		   have been initialised. */
		if (len > dev->dp8390->remote_bytes) {
			nelog(3, "%s: DMA read underrun iolen=%d remote_bytes=%d\n",
					dev->name, len, dev->dp8390->remote_bytes);
		}

		nelog(3, "%s: DMA read: addr=%4x remote_bytes=%d\n",
			dev->name, dev->dp8390->remote_dma,dev->dp8390->remote_bytes);
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

	case 0x0f:	/* Reset register */
		nic_soft_reset(dev);
		break;

	default:
		nelog(3, "%s: ASIC read invalid address %04x\n",
					dev->name, (unsigned)off);
		break;
    }

    return(retval);
}

static void
asic_write(nic_t *dev, uint32_t off, uint32_t val, unsigned len)
{
    nelog(3, "%s: ASIC write addr=0x%02x, value=0x%04x\n",
		dev->name, (unsigned)off, (unsigned) val);

    switch(off) {
	case 0x00:	/* Data register - see asic_read for a description */
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

	case 0x0f:  /* Reset register */
		/* end of reset pulse */
		break;	
		
	default: /* this is invalid, but happens under win95 device detection */
		nelog(3, "%s: ASIC write invalid address %04x, ignoring\n",
						dev->name, (unsigned)off);
		break;
    }
}


/* Writes to this page are illegal. */
static uint32_t
page3_read(nic_t *dev, uint32_t off, unsigned int len)
{ 
    if (dev->board >= NE2K_RTL8019AS) switch(off) {
	case 0x1:	/* 9346CR */
		return(dev->_9346cr);

	case 0x3:	/* CONFIG0 */
		return(0x00);	/* Cable not BNC */

	case 0x5:	/* CONFIG2 */
		return(dev->config2 & 0xe0);

	case 0x6:	/* CONFIG3 */
		return(dev->config3 & 0x46);

	case 0x8:	/* CSNSAV */
		return((dev->board == NE2K_RTL8019AS) ? dev->pnp_csnsav : 0x00);

	case 0xe:	/* 8029ASID0 */
		return(0x29);

	case 0xf:	/* 8029ASID1 */
		return(0x08);

	default:
		break;
    }

    nelog(3, "%s: Page3 read register 0x%02x attempted\n", dev->name, off);
    return(0x00);
}


static void
page3_write(nic_t *dev, uint32_t off, uint32_t val, unsigned len)
{
    if (dev->board >= NE2K_RTL8019AS) {
	nelog(3, "%s: Page2 write to register 0x%02x, len=%u, value=0x%04x\n",
		 dev->name, off, len, val);

	switch(off) {
		case 0x01:	/* 9346CR */
			dev->_9346cr = (val & 0xfe);
			break;

		case 0x05:	/* CONFIG2 */
			dev->config2 = (val & 0xe0);
			break;

		case 0x06:	/* CONFIG3 */
			dev->config3 = (val & 0x46);
			break;

		case 0x09:	/* HLTCLK  */
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
    int off = addr - dev->base_address;

    nelog(3, "%s: read addr %x, len %d\n", dev->name, addr, len);

    if (off >= 0x10)
	retval = asic_read(dev, off - 0x10, len);
    else if (off == 0x00)
	retval = dp8390_read_cr(dev->dp8390);
    else switch(dev->dp8390->CR.pgsel) {
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

    return(retval);
}


static uint8_t
nic_readb(uint16_t addr, void *priv)
{
    return(nic_read((nic_t *)priv, addr, 1));
}


static uint16_t
nic_readw(uint16_t addr, void *priv)
{
    return(nic_read((nic_t *)priv, addr, 2));
}


static uint32_t
nic_readl(uint16_t addr, void *priv)
{
    return(nic_read((nic_t *)priv, addr, 4));
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
    else switch(dev->dp8390->CR.pgsel) {
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
    nic_write((nic_t *)priv, addr, val, 1);
}


static void
nic_writew(uint16_t addr, uint16_t val, void *priv)
{
    nic_write((nic_t *)priv, addr, val, 2);
}


static void
nic_writel(uint16_t addr, uint32_t val, void *priv)
{
    nic_write((nic_t *)priv, addr, val, 4);
}


static void	nic_iocheckset(nic_t *dev, uint16_t addr);
static void	nic_iocheckremove(nic_t *dev, uint16_t addr);
static void	nic_ioset(nic_t *dev, uint16_t addr);
static void	nic_ioremove(nic_t *dev, uint16_t addr);


static uint8_t
nic_pnp_io_check_readb(uint16_t addr, void *priv)
{
    nic_t *dev = (nic_t *) priv;

    return((dev->pnp_io_check & 0x01) ? 0x55 : 0xAA);
}


static uint8_t
nic_pnp_readb(uint16_t addr, void *priv)
{
    nic_t *dev = (nic_t *) priv;
    uint8_t bit, next_shift;
    uint8_t ret = 0xFF;

    /* Plug and Play Registers */
    switch(dev->pnp_address) {
	/* Card Control Registers */
	case 0x01:	/* Serial Isolation */
		if (dev->pnp_phase != PNP_PHASE_ISOLATION) {
			ret = 0x00;
			break;
		}
		if (dev->pnp_serial_read_pair) {
			dev->pnp_serial_read <<= 1;
			/* TODO: Support for multiple PnP devices.
			if (pnp_get_bus_data() != dev->pnp_serial_read)
				dev->pnp_phase = PNP_PHASE_SLEEP;
			} else {
			*/
			if (!dev->pnp_serial_read_pos) {
				dev->pnp_res_pos = 0x1B;
				dev->pnp_phase = PNP_PHASE_CONFIG;
				nelog(1, "\nASSIGN CSN phase\n");
			}
		} else {
			if (dev->pnp_serial_read_pos < 64) {
				bit = (dev->pnp_id >> dev->pnp_serial_read_pos) & 0x01;
				next_shift = (!!(dev->pnp_id_checksum & 0x02) ^ !!(dev->pnp_id_checksum & 0x01) ^ bit) & 0x01;
				dev->pnp_id_checksum >>= 1;
				dev->pnp_id_checksum |= (next_shift << 7);
			} else {
				if (dev->pnp_serial_read_pos == 64)
					dev->eeprom[0x1A] = dev->pnp_id_checksum;
				bit = (dev->pnp_id_checksum >> (dev->pnp_serial_read_pos & 0x07)) & 0x01;
			}
			dev->pnp_serial_read = bit ? 0x55 : 0x00;
			dev->pnp_serial_read_pos = (dev->pnp_serial_read_pos + 1) % 72;
		}
		dev->pnp_serial_read_pair ^= 1;
		ret = dev->pnp_serial_read;
		break;
	case 0x04:	/* Resource Data */
		ret = dev->eeprom[dev->pnp_res_pos];
		dev->pnp_res_pos++;
		break;
	case 0x05:	/* Status */
		ret = 0x01;
		break;
	case 0x06:	/* Card Select Number (CSN) */
		nelog(1, "Card Select Number (CSN)\n");
		ret = dev->pnp_csn;
		break;
	case 0x07:	/* Logical Device Number */
		nelog(1, "Logical Device Number\n");
		ret = 0x00;
		break;
	case 0x30:	/* Activate */
		nelog(1, "Activate\n");
		ret = dev->pnp_activate;
		break;
	case 0x31:	/* I/O Range Check */
		nelog(1, "I/O Range Check\n");
		ret = dev->pnp_io_check;
		break;

	/* Logical Device Configuration Registers */
	/* Memory Configuration Registers
	   We currently force them to stay 0x00 because we currently do not
	   support a RTL8019AS BIOS. */
	case 0x40:	/* BROM base address bits[23:16] */
	case 0x41:	/* BROM base address bits[15:0] */
	case 0x42:	/* Memory Control */
		ret = 0x00;
		break;

	/* I/O Configuration Registers */
	case 0x60:	/* I/O base address bits[15:8] */
		ret = (dev->base_address >> 8);
		break;
	case 0x61:	/* I/O base address bits[7:0] */
		ret = (dev->base_address & 0xFF);
		break;

	/* Interrupt Configuration Registers */
	case 0x70:	/* IRQ level */
		ret = dev->base_irq;
		break;
	case 0x71:	/* IRQ type */
		ret = 0x02;	/* high, edge */
		break;

	/* DMA Configuration Registers */
	case 0x74:	/* DMA channel select 0 */
	case 0x75:	/* DMA channel select 1 */
		ret = 0x04;	/* indicating no DMA channel is needed */
		break;

	/* Vendor Defined Registers */
	case 0xF0:	/* CONFIG0 */
	case 0xF1:	/* CONFIG1 */
		ret = 0x00;
		break;
	case 0xF2:	/* CONFIG2 */
		ret = (dev->config2 & 0xe0);
		break;
	case 0xF3:	/* CONFIG3 */
		ret = (dev->config3 & 0x46);
		break;
	case 0xF5:	/* CSNSAV */
		ret = (dev->pnp_csnsav);
		break;
    }

    nelog(1, "nic_pnp_readb(%04X) = %02X\n", addr, ret);
    return(ret);
}


static void	nic_pnp_io_set(nic_t *dev, uint16_t read_addr);
static void	nic_pnp_io_remove(nic_t *dev);


static void
nic_pnp_writeb(uint16_t addr, uint8_t val, void *priv)
{
    nic_t *dev = (nic_t *) priv;
    uint16_t new_addr = 0;

    nelog(1, "nic_pnp_writeb(%04X, %02X)\n", addr, val);

    /* Plug and Play Registers */
    switch(dev->pnp_address) {
	/* Card Control Registers */
	case 0x00:	/* Set RD_DATA port */
		new_addr = val;
		new_addr <<= 2;
		new_addr |= 3;
		nic_pnp_io_remove(dev);
		nic_pnp_io_set(dev, new_addr);
		nelog(1, "PnP read data address now: %04X\n", new_addr);
		break;
	case 0x02:	/* Config Control */
		if (val & 0x01) {
			/* Reset command */
			nic_pnp_io_remove(dev);
			memset(dev->pnp_regs, 0, 256);
			nelog(1, "All logical devices reset\n");
		}
		if (val & 0x02) {
			/* Wait for Key command */
			dev->pnp_phase = PNP_PHASE_WAIT_FOR_KEY;
			nelog(1, "WAIT FOR KEY phase\n");
		}
		if (val & 0x04) {
			/* PnP Reset CSN command */
			dev->pnp_csn = dev->pnp_csnsav = 0;
			nelog(1, "CSN reset\n");
		}
		break;
	case 0x03:	/* Wake[CSN] */
		nelog(1, "Wake[%02X]\n", val);
		if (val == dev->pnp_csn) {
			dev->pnp_res_pos = 0x12;
			dev->pnp_id_checksum = 0x6A;
			if (dev->pnp_phase == PNP_PHASE_SLEEP) {
				dev->pnp_phase = val ? PNP_PHASE_CONFIG : PNP_PHASE_ISOLATION;
			}
		} else {
			if ((dev->pnp_phase == PNP_PHASE_CONFIG) || (dev->pnp_phase == PNP_PHASE_ISOLATION))
				dev->pnp_phase = PNP_PHASE_SLEEP;
		}
		break;
	case 0x06:	/* Card Select Number (CSN) */
	    	dev->pnp_csn = dev->pnp_csnsav = val;
		dev->pnp_phase = PNP_PHASE_CONFIG;
		nelog(1, "CSN set to %02X\n", dev->pnp_csn);
		break;
	case 0x30:	/* Activate */
		if ((dev->pnp_activate ^ val) & 0x01) {
			nic_ioremove(dev, dev->base_address);
			if (val & 0x01)
				nic_ioset(dev, dev->base_address);

			nelog(1, "I/O range %sabled\n", val & 0x02 ? "en" : "dis");
		}
		dev->pnp_activate = val;
		break;
	case 0x31:	/* I/O Range Check */
		if ((dev->pnp_io_check ^ val) & 0x02) {
			nic_iocheckremove(dev, dev->base_address);
			if (val & 0x02)
				nic_iocheckset(dev, dev->base_address);

			nelog(1, "I/O range check %sabled\n", val & 0x02 ? "en" : "dis");
		}
		dev->pnp_io_check = val;
		break;

	/* Logical Device Configuration Registers */
	/* Memory Configuration Registers
	   We currently force them to stay 0x00 because we currently do not
	   support a RTL8019AS BIOS. */

	/* I/O Configuration Registers */
	case 0x60:	/* I/O base address bits[15:8] */
		if ((dev->pnp_activate & 0x01) || (dev->pnp_io_check & 0x02))
			nic_ioremove(dev, dev->base_address);
		dev->base_address &= 0x00ff;
		dev->base_address |= (((uint16_t) val) << 8);
		if ((dev->pnp_activate & 0x01) || (dev->pnp_io_check & 0x02))
			nic_ioset(dev, dev->base_address);
		nelog(1, "Base address now: %04X\n", dev->base_address);
		break;
	case 0x61:	/* I/O base address bits[7:0] */
		if ((dev->pnp_activate & 0x01) || (dev->pnp_io_check & 0x02))
			nic_ioremove(dev, dev->base_address);
		dev->base_address &= 0xff00;
		dev->base_address |= val;
		if ((dev->pnp_activate & 0x01) || (dev->pnp_io_check & 0x02))
			nic_ioset(dev, dev->base_address);
		nelog(1, "Base address now: %04X\n", dev->base_address);
		break;

	/* Interrupt Configuration Registers */
	case 0x70:	/* IRQ level */
		dev->base_irq = val;
		nelog(1, "IRQ now: %02i\n", dev->base_irq);
		break;

	/* Vendor Defined Registers */
	case 0xF6:	/* Vendor Control */
		dev->pnp_csn = 0;
		break;
    }
    return;
}


static void
nic_pnp_io_set(nic_t *dev, uint16_t read_addr)
{
	if ((read_addr >= 0x0200) && (read_addr <= 0x03FF)) {
		io_sethandler(read_addr, 1,
			      nic_pnp_readb, NULL, NULL,
			      NULL, NULL, NULL, dev);
	}
	dev->pnp_read = read_addr;
}


static void
nic_pnp_io_remove(nic_t *dev)
{
	if ((dev->pnp_read >= 0x0200) && (dev->pnp_read <= 0x03FF)) {
		io_removehandler(dev->pnp_read, 1,
			      nic_pnp_readb, NULL, NULL,
			      NULL, NULL, NULL, dev);
	}
}


static void
nic_pnp_address_writeb(uint16_t addr, uint8_t val, void *priv)
{
    nic_t *dev = (nic_t *) priv;

    /* nelog(1, "nic_pnp_address_writeb(%04X, %02X)\n", addr, val); */

    switch(dev->pnp_phase) {
	case PNP_PHASE_WAIT_FOR_KEY:
		if (val == pnp_init_key[dev->pnp_magic_count]) {
			dev->pnp_magic_count = (dev->pnp_magic_count + 1) & 0x1f;
			if (!dev->pnp_magic_count)
				dev->pnp_phase = PNP_PHASE_SLEEP;
		} else
			dev->pnp_magic_count = 0;
		break;
	default:
		dev->pnp_address = val;
		break;
    }
    return;
}


static void
nic_iocheckset(nic_t *dev, uint16_t addr)
{
    io_sethandler(addr, 32,
		  nic_pnp_io_check_readb, NULL, NULL,
		  NULL, NULL, NULL, dev);
}


static void
nic_iocheckremove(nic_t *dev, uint16_t addr)
{
    io_removehandler(addr, 32,
		     nic_pnp_io_check_readb, NULL, NULL,
		     NULL, NULL, NULL, dev);
}


static void
nic_ioset(nic_t *dev, uint16_t addr)
{	
    if (dev->is_pci) {
	io_sethandler(addr, 16,
			 nic_readb, nic_readw, nic_readl,
			 nic_writeb, nic_writew, nic_writel, dev);
	io_sethandler(addr+16, 16,
			 nic_readb, nic_readw, nic_readl,
			 nic_writeb, nic_writew, nic_writel, dev);
	io_sethandler(addr+0x1f, 1,
			 nic_readb, nic_readw, nic_readl,
			 nic_writeb, nic_writew, nic_writel, dev);
    } else {
	io_sethandler(addr, 16,
			 nic_readb, NULL, NULL,
			 nic_writeb, NULL, NULL, dev);
	if (dev->is_8bit) {
		io_sethandler(addr+16, 16,
				 nic_readb, NULL, NULL,
				 nic_writeb, NULL, NULL, dev);
	} else {
		io_sethandler(addr+16, 16,
				 nic_readb, nic_readw, NULL,
				 nic_writeb, nic_writew, NULL, dev);
	}
	io_sethandler(addr+0x1f, 1,
			 nic_readb, NULL, NULL,
			 nic_writeb, NULL, NULL, dev);	
    }
}


static void
nic_ioremove(nic_t *dev, uint16_t addr)
{
    if (dev->is_pci) {
	io_removehandler(addr, 16,
			 nic_readb, nic_readw, nic_readl,
			 nic_writeb, nic_writew, nic_writel, dev);
	io_removehandler(addr+16, 16,
			 nic_readb, nic_readw, nic_readl,
			 nic_writeb, nic_writew, nic_writel, dev);
	io_removehandler(addr+0x1f, 1,
			 nic_readb, nic_readw, nic_readl,
			 nic_writeb, nic_writew, nic_writel, dev);
    } else {
	io_removehandler(addr, 16,
			 nic_readb, NULL, NULL,
			 nic_writeb, NULL, NULL, dev);
	if (dev->is_8bit) {
		io_removehandler(addr+16, 16,
				 nic_readb, NULL, NULL,
				 nic_writeb, NULL, NULL, dev);
	} else {
		io_removehandler(addr+16, 16,
				 nic_readb, nic_readw, NULL,
				 nic_writeb, nic_writew, NULL, dev);
	}
	io_removehandler(addr+0x1f, 1,
			 nic_readb, NULL, NULL,
			 nic_writeb, NULL, NULL, dev);	
    }
}


static void
nic_update_bios(nic_t *dev)
{
    int reg_bios_enable;
	
    reg_bios_enable = 1;

    if (! dev->has_bios) return;

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
nic_pci_read(int func, int addr, void *priv)
{
    nic_t *dev = (nic_t *)priv;
    uint8_t ret = 0x00;

    switch(addr) {
	case 0x00:			/* PCI_VID_LO */
	case 0x01:			/* PCI_VID_HI */
		ret = dev->pci_regs[addr];
		break;

	case 0x02:			/* PCI_DID_LO */
	case 0x03:			/* PCI_DID_HI */
		ret = dev->pci_regs[addr];
		break;

	case 0x04:			/* PCI_COMMAND_LO */
	case 0x05:			/* PCI_COMMAND_HI */
		ret = dev->pci_regs[addr];
		break;

	case 0x06:			/* PCI_STATUS_LO */
	case 0x07:			/* PCI_STATUS_HI */
		ret = dev->pci_regs[addr];
		break;

	case 0x08:			/* PCI_REVID */
		ret = 0x00;		/* Rev. 00 */
		break;
	case 0x09:			/* PCI_PIFR */
		ret = 0x00;		/* Rev. 00 */
		break;

	case 0x0A:			/* PCI_SCR */
		ret = dev->pci_regs[addr];
		break;

	case 0x0B:			/* PCI_BCR */
		ret = dev->pci_regs[addr];
		break;

	case 0x10:			/* PCI_BAR 7:5 */
		ret = (dev->pci_bar[0].addr_regs[0] & 0xe0) | 0x01;
		break;
	case 0x11:			/* PCI_BAR 15:8 */
		ret = dev->pci_bar[0].addr_regs[1];
		break;
	case 0x12:			/* PCI_BAR 23:16 */
		ret = dev->pci_bar[0].addr_regs[2];
		break;
	case 0x13:			/* PCI_BAR 31:24 */
		ret = dev->pci_bar[0].addr_regs[3];
		break;

	case 0x2C:			/* PCI_SVID_LO */
	case 0x2D:			/* PCI_SVID_HI */
		ret = dev->pci_regs[addr];
		break;

	case 0x2E:			/* PCI_SID_LO */
	case 0x2F:			/* PCI_SID_HI */
		ret = dev->pci_regs[addr];
		break;

	case 0x30:			/* PCI_ROMBAR */
		ret = dev->pci_bar[1].addr_regs[0] & 0x01;
		break;
	case 0x31:			/* PCI_ROMBAR 15:11 */
		ret = dev->pci_bar[1].addr_regs[1] & 0x80;
		break;
	case 0x32:			/* PCI_ROMBAR 23:16 */
		ret = dev->pci_bar[1].addr_regs[2];
		break;
	case 0x33:			/* PCI_ROMBAR 31:24 */
		ret = dev->pci_bar[1].addr_regs[3];
		break;

	case 0x3C:			/* PCI_ILR */
		ret = dev->pci_regs[addr];
		break;

	case 0x3D:			/* PCI_IPR */
		ret = dev->pci_regs[addr];
		break;
    }

    nelog(2, "%s: PCI_Read(%d, %04x) = %02x\n", dev->name, func, addr, ret);

    return(ret);
}


static void
nic_pci_write(int func, int addr, uint8_t val, void *priv)
{
    nic_t *dev = (nic_t *)priv;
    uint8_t valxor;

    nelog(2, "%s: PCI_Write(%d, %04x, %02x)\n", dev->name, func, addr, val);

    switch(addr) {
	case 0x04:			/* PCI_COMMAND_LO */
		valxor = (val & 0x03) ^ dev->pci_regs[addr];
		if (valxor & PCI_COMMAND_IO)
		{
			nic_ioremove(dev, dev->base_address);
			if ((dev->base_address != 0) && (val & PCI_COMMAND_IO))
			{
				nic_ioset(dev, dev->base_address);
			}
		}
		dev->pci_regs[addr] = val & 0x03;
		break;

	case 0x10:			/* PCI_BAR */
		val &= 0xe0;	/* 0xe0 acc to RTL DS */
		val |= 0x01;	/* re-enable IOIN bit */
		/*FALLTHROUGH*/

	case 0x11:			/* PCI_BAR */
	case 0x12:			/* PCI_BAR */
	case 0x13:			/* PCI_BAR */
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
		if (dev->pci_regs[4] & PCI_COMMAND_IO)
		{
			if (dev->base_address != 0)
			{
				nic_ioset(dev, dev->base_address);
			}
		}
		break;

	case 0x30:			/* PCI_ROMBAR */
	case 0x31:			/* PCI_ROMBAR */
	case 0x32:			/* PCI_ROMBAR */
	case 0x33:			/* PCI_ROMBAR */
		dev->pci_bar[1].addr_regs[addr & 3] = val;
		/* dev->pci_bar[1].addr_regs[1] &= dev->bios_mask; */
		dev->pci_bar[1].addr &= 0xffff8001;
		dev->bios_addr = dev->pci_bar[1].addr;
		nic_update_bios(dev);
		return;

	case 0x3C:			/* PCI_ILR */
		nelog(1, "%s: IRQ now: %i\n", dev->name, val);
		dev->base_irq = val;
		dev->pci_regs[addr] = dev->base_irq;
		return;
    }
}


static void
nic_rom_init(nic_t *dev, wchar_t *s)
{
    uint32_t temp;
    FILE *f;

    if (s == NULL) return;

    if (dev->bios_addr == 0) return;

    if ((f = rom_fopen(s, L"rb")) != NULL) {
	fseek(f, 0L, SEEK_END);
	temp = ftell(f);
	fclose(f);
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
	     dev->bios_size, dev->bios_size-1, 0, MEM_MAPPING_EXTERNAL);

    nelog(1, "%s: BIOS configured at %06lX (size %ld)\n",
		dev->name, dev->bios_addr, dev->bios_size);
}

static uint8_t
nic_mca_read(int port, void *priv)
{
    nic_t *dev = (nic_t *)priv;

    return(dev->pos_regs[port & 7]);
}

#define MCA_611F_IO_PORTS { 0x300, 0x340, 0x320, 0x360, 0x1300, 0x1340, \
			    0x1320, 0x1360 }

#define MCA_611F_IRQS { 2, 3, 4, 5, 10, 11, 12, 15 }

static void
nic_mca_write(int port, uint8_t val, void *priv)
{
    nic_t *dev = (nic_t *)priv;
    uint16_t base[] = MCA_611F_IO_PORTS;
    int8_t irq[] = MCA_611F_IRQS;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102) return;

    /* Save the MCA register value. */
    dev->pos_regs[port & 7] = val;

    nic_ioremove(dev, dev->base_address);	

    /* This is always necessary so that the old handler doesn't remain. */
	/* Get the new assigned I/O base address. */
	dev->base_address = base[(dev->pos_regs[2] & 0xE0) >> 4];

	/* Save the new IRQ values. */
	dev->base_irq = irq[(dev->pos_regs[2] & 0xE) >> 1];

	dev->bios_addr = 0x0000;
	dev->has_bios = 0;

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
    nic_t *dev = (nic_t *)priv;

    return (dev->pos_regs[2] & 0x01);
}


static void *
nic_init(const device_t *info)
{
    uint32_t mac;
    wchar_t *rom;
    nic_t *dev;
#ifdef ENABLE_NIC_LOG
    int i;
#endif
    int c;
    char *ansi_id = "REALTEK PLUG & PLAY ETHERNET CARD";
    uint64_t *eeprom_pnp_id;

    /* Get the desired debug level. */
#ifdef ENABLE_NIC_LOG
    i = device_get_config_int("debug");
    if (i > 0) nic_do_log = i;
#endif

    dev = malloc(sizeof(nic_t));
    memset(dev, 0x00, sizeof(nic_t));
    dev->name = info->name;
    dev->board = info->local;
    rom = NULL;

    if (dev->board >= NE2K_RTL8019AS) {
	dev->base_address = 0x340;
	dev->base_irq = 12;
	if (dev->board == NE2K_RTL8029AS) {
		dev->bios_addr = 0xD0000;
		dev->has_bios = device_get_config_int("bios");
	} else {
		dev->bios_addr = 0x00000;
		dev->has_bios = 0;
	}
    } else {
	if (dev->board != NE2K_ETHERNEXT_MC) {
		dev->base_address = device_get_config_hex16("base");
		dev->base_irq = device_get_config_int("irq");
		if (dev->board == NE2K_NE2000) {
			dev->bios_addr = device_get_config_hex20("bios_addr");
			dev->has_bios = !!dev->bios_addr;
		} else {
			dev->bios_addr = 0x00000;
			dev->has_bios = 0;
		}		
	}
	else {
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
	mac = (((int) dev->maclocal[3]) << 16);
	mac |= (((int) dev->maclocal[4]) << 8);
	mac |= ((int) dev->maclocal[5]);
	device_set_config_mac("mac", mac);
    } else {
	dev->maclocal[3] = (mac>>16) & 0xff;
	dev->maclocal[4] = (mac>>8) & 0xff;
	dev->maclocal[5] = (mac & 0xff);
    }

    dev->dp8390 = device_add(&dp8390_device);
    dev->dp8390->priv = dev;
    dev->dp8390->interrupt = nic_interrupt;

    memcpy(dev->dp8390->physaddr, dev->maclocal, sizeof(dev->maclocal));

    nelog(2, "%s: I/O=%04x, IRQ=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
	dev->name, dev->base_address, dev->base_irq,
	dev->dp8390->physaddr[0], dev->dp8390->physaddr[1], dev->dp8390->physaddr[2],
	dev->dp8390->physaddr[3], dev->dp8390->physaddr[4], dev->dp8390->physaddr[5]);

    switch(dev->board) {
	case NE2K_NE1000:
		dev->maclocal[0] = 0x00;  /* 00:00:D8 (Novell OID) */
		dev->maclocal[1] = 0x00;
		dev->maclocal[2] = 0xD8;
		dev->is_8bit = 1;
		rom = NULL;
		dp8390_set_defaults(dev->dp8390, DP8390_FLAG_CHECK_CR | DP8390_FLAG_CLEAR_IRQ);
		dp8390_mem_alloc(dev->dp8390, 0x2000, 0x2000);
		break;

	case NE2K_NE2000:
		dev->maclocal[0] = 0x00;  /* 00:00:D8 (Novell OID) */
		dev->maclocal[1] = 0x00;
		dev->maclocal[2] = 0xD8;
		rom = ROM_PATH_NE2000;
		dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC | DP8390_FLAG_CHECK_CR |
				    DP8390_FLAG_CLEAR_IRQ);
		dp8390_mem_alloc(dev->dp8390, 0x4000, 0x4000);
		break;
		
	case NE2K_ETHERNEXT_MC:
		dev->maclocal[0] = 0x00;  /* 00:00:D8 (Networth Inc. OID) */
		dev->maclocal[1] = 0x00;
		dev->maclocal[2] = 0x79;
		dev->pos_regs[0] = 0x1F;
		dev->pos_regs[1] = 0x61;
		rom = NULL;
		dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC | DP8390_FLAG_CHECK_CR |
				    DP8390_FLAG_CLEAR_IRQ);
		dp8390_mem_alloc(dev->dp8390, 0x4000, 0x4000);
		break;

	case NE2K_RTL8019AS:
	case NE2K_RTL8029AS:
		dev->is_pci = (dev->board == NE2K_RTL8029AS) ? 1 : 0;
		dev->maclocal[0] = 0x00;  /* 00:E0:4C (Realtek OID) */
		dev->maclocal[1] = 0xE0;
		dev->maclocal[2] = 0x4C;
		rom = (dev->board == NE2K_RTL8019AS) ? ROM_PATH_RTL8019 : ROM_PATH_RTL8029;
		if (dev->is_pci)
			dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC);
		else
			dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC | DP8390_FLAG_CLEAR_IRQ);
		dp8390_set_id(dev->dp8390, 0x50, (dev->board == NE2K_RTL8019AS) ? 0x70 : 0x43);
		dp8390_mem_alloc(dev->dp8390, 0x4000, 0x8000);
		break;
    }

    /*
     * Make this device known to the I/O system.
     * PnP and PCI devices start with address spaces inactive.
     */
    if (dev->board < NE2K_RTL8019AS && dev->board != NE2K_ETHERNEXT_MC)
	nic_ioset(dev, dev->base_address);

    /* Set up our BIOS ROM space, if any. */
    nic_rom_init(dev, rom);

    if (dev->board >= NE2K_RTL8019AS) {
	if (dev->is_pci) {
		/*
		 * Configure the PCI space registers.
		 *
		 * We do this here, so the I/O routines are generic.
		 */
		memset(dev->pci_regs, 0, PCI_REGSIZE);

		dev->pci_regs[0x00] = (PCI_VENDID&0xff);
		dev->pci_regs[0x01] = (PCI_VENDID>>8);
		dev->pci_regs[0x02] = (PCI_DEVID&0xff);
		dev->pci_regs[0x03] = (PCI_DEVID>>8);

	        dev->pci_regs[0x04] = 0x03;	/* IOEN */
        	dev->pci_regs[0x05] = 0x00;
	        dev->pci_regs[0x07] = 0x02;	/* DST0, medium devsel */

	        dev->pci_regs[0x09] = 0x00;	/* PIFR */

	        dev->pci_regs[0x0B] = 0x02;	/* BCR: Network Controller */
        	dev->pci_regs[0x0A] = 0x00;	/* SCR: Ethernet */

		dev->pci_regs[0x2C] = (PCI_VENDID&0xff);
		dev->pci_regs[0x2D] = (PCI_VENDID>>8);
		dev->pci_regs[0x2E] = (PCI_DEVID&0xff);
		dev->pci_regs[0x2F] = (PCI_DEVID>>8);

	        dev->pci_regs[0x3D] = PCI_INTA;	/* PCI_IPR */

		/* Enable our address space in PCI. */
		dev->pci_bar[0].addr_regs[0] = 0x01;

		/* Enable our BIOS space in PCI, if needed. */
		if (dev->bios_addr > 0) {
			dev->pci_bar[1].addr = 0xFFFF8000;
			dev->pci_bar[1].addr_regs[1] = dev->bios_mask;
		} else {
			dev->pci_bar[1].addr = 0;
			dev->bios_size = 0;
		}

		mem_mapping_disable(&dev->bios_rom.mapping);

		/* Add device to the PCI bus, keep its slot number. */
		dev->card = pci_add_card(PCI_ADD_NORMAL,
					 nic_pci_read, nic_pci_write, dev);
	} else {
		io_sethandler(0x0279, 1,
			      NULL, NULL, NULL,
			      nic_pnp_address_writeb, NULL, NULL, dev);

		dev->pnp_id = PNP_DEVID;
		dev->pnp_id <<= 32LL;
		dev->pnp_id |= PNP_VENDID;
		dev->pnp_phase = PNP_PHASE_WAIT_FOR_KEY;
	}

	/* Initialize the RTL8029 EEPROM. */
        memset(dev->eeprom, 0x00, sizeof(dev->eeprom));

	if (dev->board == NE2K_RTL8029AS) {
		memcpy(&dev->eeprom[0x02], dev->maclocal, 6);

	        dev->eeprom[0x76] =
		 dev->eeprom[0x7A] =
		 dev->eeprom[0x7E] = (PCI_DEVID&0xff);
	        dev->eeprom[0x77] =
		 dev->eeprom[0x7B] =
		 dev->eeprom[0x7F] = (dev->board == NE2K_RTL8019AS) ? (PNP_DEVID>>8) : (PCI_DEVID>>8);
        	dev->eeprom[0x78] =
		 dev->eeprom[0x7C] = (PCI_VENDID&0xff);
        	dev->eeprom[0x79] =
		 dev->eeprom[0x7D] = (PCI_VENDID>>8);
	} else {
		eeprom_pnp_id = (uint64_t *) &dev->eeprom[0x12];
		*eeprom_pnp_id = dev->pnp_id;

		/* TAG: Plug and Play Version Number. */
		dev->eeprom[0x1B] = 0x0A;	/* Item byte */
		dev->eeprom[0x1C] = 0x10;	/* PnP version */
		dev->eeprom[0x1D] = 0x10;	/* Vendor version */

		/* TAG: ANSI Identifier String. */
		dev->eeprom[0x1E] = 0x82;	/* Item byte */
		dev->eeprom[0x1F] = 0x22;	/* Length bits 7-0 */
		dev->eeprom[0x20] = 0x00;	/* Length bits 15-8 */
		memcpy(&dev->eeprom[0x21], ansi_id, 0x22);

		/* TAG: Logical Device ID. */
		dev->eeprom[0x43] = 0x16;	/* Item byte */
		dev->eeprom[0x44] = 0x4A;	/* Logical device ID0 */
		dev->eeprom[0x45] = 0x8C;	/* Logical device ID1 */
		dev->eeprom[0x46] = 0x80;	/* Logical device ID2 */
		dev->eeprom[0x47] = 0x19;	/* Logical device ID3 */
		dev->eeprom[0x48] = 0x02;	/* Flag0 (02=BROM/disabled) */
		dev->eeprom[0x49] = 0x00;	/* Flag 1 */

		/* TAG: Compatible Device ID (NE2000) */
		dev->eeprom[0x4A] = 0x1C;	/* Item byte			*/
		dev->eeprom[0x4B] = 0x41;	/* Compatible ID0 */
		dev->eeprom[0x4C] = 0xD0;	/* Compatible ID1 */
		dev->eeprom[0x4D] = 0x80;	/* Compatible ID2 */
		dev->eeprom[0x4E] = 0xD6;	/* Compatible ID3 */

		/* TAG: I/O Format */
		dev->eeprom[0x4F] = 0x47;	/* Item byte */
		dev->eeprom[0x50] = 0x00;	/* I/O information */
		dev->eeprom[0x51] = 0x20;	/* Min. I/O base bits 7-0 */
		dev->eeprom[0x52] = 0x02;	/* Min. I/O base bits 15-8 */
		dev->eeprom[0x53] = 0x80;	/* Max. I/O base bits 7-0 */
		dev->eeprom[0x54] = 0x03;	/* Max. I/O base bits 15-8 */
		dev->eeprom[0x55] = 0x20;	/* Base alignment */
		dev->eeprom[0x56] = 0x20;	/* Range length */

		/* TAG: IRQ Format. */
		dev->eeprom[0x57] = 0x23;	/* Item byte */
		dev->eeprom[0x58] = 0x38;	/* IRQ mask bits 7-0 */
		dev->eeprom[0x59] = 0x9E;	/* IRQ mask bits 15-8 */
		dev->eeprom[0x5A] = 0x01;	/* IRQ information */

		/* TAG: END Tag */
		dev->eeprom[0x5B] = 0x79;	/* Item byte */
		for (c = 0x1b; c < 0x5c; c++)	/* Checksum (2's compl) */
			dev->eeprom[0x5C] += dev->eeprom[c];
		dev->eeprom[0x5C] = -dev->eeprom[0x5C];

		io_sethandler(0x0A79, 1,
			      NULL, NULL, NULL,
			      nic_pnp_writeb, NULL, NULL, dev);
	}
    }

    if (dev->board != NE2K_ETHERNEXT_MC)
	/* Reset the board. */
	nic_reset(dev);

    /* Attach ourselves to the network module. */
    network_attach(dev->dp8390, dev->dp8390->physaddr, dp8390_rx, NULL);

    nelog(1, "%s: %s attached IO=0x%X IRQ=%d\n", dev->name,
	dev->is_pci?"PCI":"ISA", dev->base_address, dev->base_irq);

    return(dev);
}


static void
nic_close(void *priv)
{
    nic_t *dev = (nic_t *)priv;

    nelog(1, "%s: closed\n", dev->name);

    free(dev);
}


static const device_config_t ne1000_config[] =
{
	{
		"base", "Address", CONFIG_HEX16, "", 0x300,
		{
			{
				"0x280", 0x280
			},
			{
				"0x300", 0x300
			},
			{
				"0x320", 0x320
			},
			{
				"0x340", 0x340
			},
			{
				"0x360", 0x360
			},
			{
				"0x380", 0x380
			},
			{
				""
			}
		},
	},
	{
		"irq", "IRQ", CONFIG_SELECTION, "", 3,
		{
			{
				"IRQ 2", 2
			},
			{
				"IRQ 3", 3
			},
			{
				"IRQ 5", 5
			},
			{
				"IRQ 7", 7
			},
			{
				"IRQ 10", 10
			},
			{
				"IRQ 11", 11
			},
			{
				""
			}
		},
	},
	{
		"mac", "MAC Address", CONFIG_MAC, "", -1
	},
	{
		"", "", -1
	}
};

static const device_config_t ne2000_config[] =
{
	{
		"base", "Address", CONFIG_HEX16, "", 0x300,
		{
			{
				"0x280", 0x280
			},
			{
				"0x300", 0x300
			},
			{
				"0x320", 0x320
			},
			{
				"0x340", 0x340
			},
			{
				"0x360", 0x360
			},
			{
				"0x380", 0x380
			},
			{
				""
			}
		},
	},
	{
		"irq", "IRQ", CONFIG_SELECTION, "", 10,
		{
			{
				"IRQ 2", 2
			},
			{
				"IRQ 3", 3
			},
			{
				"IRQ 5", 5
			},
			{
				"IRQ 7", 7
			},
			{
				"IRQ 10", 10
			},
			{
				"IRQ 11", 11
			},
			{
				""
			}
		},
	},
	{
		"mac", "MAC Address", CONFIG_MAC, "", -1
	},
	{
		"bios_addr", "BIOS address", CONFIG_HEX20, "", 0,
		{
			{
				"Disabled", 0x00000
			},
			{
				"D000", 0xD0000
			},
			{
				"D800", 0xD8000
			},
			{
				"C800", 0xC8000
			},
			{
				""
			}
		},
	},
	{
		"", "", -1
	}
};

static const device_config_t rtl8019as_config[] =
{
	{
		"mac", "MAC Address", CONFIG_MAC, "", -1
	},
	{
		"", "", -1
	}
};

static const device_config_t rtl8029as_config[] =
{
	{
		"bios", "Enable BIOS", CONFIG_BINARY, "", 0
	},
	{
		"mac", "MAC Address", CONFIG_MAC, "", -1
	},
	{
		"", "", -1
	}
};

static const device_config_t mca_mac_config[] =
{
	{
		"mac", "MAC Address", CONFIG_MAC, "", -1
	},
	{
		"", "", -1
	}
};



const device_t ne1000_device = {
    "Novell NE1000",
    DEVICE_ISA,
    NE2K_NE1000,
    nic_init, nic_close, NULL,
    NULL, NULL, NULL,
    ne1000_config
};

const device_t ne2000_device = {
    "Novell NE2000",
    DEVICE_ISA | DEVICE_AT,
    NE2K_NE2000,
    nic_init, nic_close, NULL,
    NULL, NULL, NULL,
    ne2000_config
};

const device_t ethernext_mc_device = {
    "NetWorth EtherNext/MC",
    DEVICE_MCA,
    NE2K_ETHERNEXT_MC,
    nic_init, nic_close, NULL,
    NULL, NULL, NULL,
    mca_mac_config
};

const device_t rtl8019as_device = {
    "Realtek RTL8019AS",
    DEVICE_ISA | DEVICE_AT,
    NE2K_RTL8019AS,
    nic_init, nic_close, NULL,
    NULL, NULL, NULL,
    rtl8019as_config
};

const device_t rtl8029as_device = {
    "Realtek RTL8029AS",
    DEVICE_PCI,
    NE2K_RTL8029AS,
    nic_init, nic_close, NULL,
    NULL, NULL, NULL,
    rtl8029as_config
};

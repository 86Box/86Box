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
 *			- Realtek RTL8019AS (ISA 16-bit, PnP);
 *			- Realtek RTL8029AS (PCI).
 *
 * Version:	@(#)net_ne2000.c	1.0.4	2018/04/26
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
#include "../86box.h"
#include "../config.h"
#include "../machine/machine.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../pci.h"
#include "../pic.h"
#include "../random.h"
#include "../device.h"
#include "../ui.h"
#include "network.h"
#include "net_dp8390.h"
#include "net_ne2000.h"
#include "bswap.h"


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
    dp8390_t dp8390;
	uint8_t	macaddr[32];		/* ASIC ROM'd MAC address, even bytes */
    int		board;
    int		is_pci, is_8bit;
    const char	*name;
    uint32_t	base_address;
    int		base_irq;
    uint32_t	bios_addr,
		bios_size,
		bios_mask;
    uint8_t	pnp_regs[256];
    uint8_t	pnp_res_data[256];
    bar_t	pci_bar[2];
    uint8_t	pci_regs[PCI_REGSIZE];
    uint8_t	maclocal[6];		/* configured MAC (local) address */
    uint8_t	eeprom[128];		/* for RTL8029AS */
    rom_t	bios_rom;
    int		card;			/* PCI card slot */
    int		has_bios;
    uint8_t	pnp_phase;
    uint8_t	pnp_magic_count;
    uint8_t	pnp_address;
    uint8_t	pnp_res_pos;
    uint8_t	pnp_csn;
    uint8_t	pnp_activate;
    uint8_t	pnp_io_check;
    uint8_t	pnp_csnsav;
    uint16_t	pnp_read;
    uint64_t	pnp_id;
    uint8_t	pnp_id_checksum;
    uint8_t	pnp_serial_read_pos;
    uint8_t	pnp_serial_read_pair;
    uint8_t	pnp_serial_read;

    /* RTL8019AS/RTL8029AS registers */
    uint8_t	config0, config2, config3;
    uint8_t	_9346cr;
} nic_t;


static void	nic_rx(void *, uint8_t *, int);
static void	nic_tx(nic_t *, uint32_t);


static void
nelog(int lvl, const char *fmt, ...)
{
#ifdef ENABLE_NIC_LOG
    va_list ap;

    if (nic_do_log >= lvl) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}


static void
nic_interrupt(nic_t *dev, int set)
{
    if (PCI && dev->is_pci) {
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
    int i;

    nelog(1, "%s: reset\n", dev->name);

    if (dev->board >= NE2K_NE2000) {
	/* Initialize the MAC address area by doubling the physical address */
	dev->macaddr[0]  = dev->dp8390.physaddr[0];
	dev->macaddr[1]  = dev->dp8390.physaddr[0];
	dev->macaddr[2]  = dev->dp8390.physaddr[1];
	dev->macaddr[3]  = dev->dp8390.physaddr[1];
	dev->macaddr[4]  = dev->dp8390.physaddr[2];
	dev->macaddr[5]  = dev->dp8390.physaddr[2];
	dev->macaddr[6]  = dev->dp8390.physaddr[3];
	dev->macaddr[7]  = dev->dp8390.physaddr[3];
	dev->macaddr[8]  = dev->dp8390.physaddr[4];
	dev->macaddr[9]  = dev->dp8390.physaddr[4];
	dev->macaddr[10] = dev->dp8390.physaddr[5];
	dev->macaddr[11] = dev->dp8390.physaddr[5];

	/* ne2k signature */
	for (i=12; i<32; i++)
		dev->macaddr[i] = 0x57;
    } else {
	/* Initialize the MAC address area by doubling the physical address */
	dev->macaddr[0]  = dev->dp8390.physaddr[0];
	dev->macaddr[1]  = dev->dp8390.physaddr[1];
	dev->macaddr[2]  = dev->dp8390.physaddr[2];
	dev->macaddr[3]  = dev->dp8390.physaddr[3];
	dev->macaddr[4]  = dev->dp8390.physaddr[4];
	dev->macaddr[5]  = dev->dp8390.physaddr[5];

	/* ne1k signature */
	for (i=6; i<16; i++)
		dev->macaddr[i] = 0x57;
    }

    /* Zero out registers and memory */
    memset(&dev->dp8390.CR,  0x00, sizeof(dev->dp8390.CR) );
    memset(&dev->dp8390.ISR, 0x00, sizeof(dev->dp8390.ISR));
    memset(&dev->dp8390.IMR, 0x00, sizeof(dev->dp8390.IMR));
    memset(&dev->dp8390.DCR, 0x00, sizeof(dev->dp8390.DCR));
    memset(&dev->dp8390.TCR, 0x00, sizeof(dev->dp8390.TCR));
    memset(&dev->dp8390.TSR, 0x00, sizeof(dev->dp8390.TSR));
    memset(&dev->dp8390.RSR, 0x00, sizeof(dev->dp8390.RSR));
    dev->dp8390.tx_timer_active = 0;
    dev->dp8390.local_dma  = 0;
    dev->dp8390.page_start = 0;
    dev->dp8390.page_stop  = 0;
    dev->dp8390.bound_ptr  = 0;
    dev->dp8390.tx_page_start = 0;
    dev->dp8390.num_coll   = 0;
    dev->dp8390.tx_bytes   = 0;
    dev->dp8390.fifo       = 0;
    dev->dp8390.remote_dma = 0;
    dev->dp8390.remote_start = 0;
    dev->dp8390.remote_bytes = 0;
    dev->dp8390.tallycnt_0 = 0;
    dev->dp8390.tallycnt_1 = 0;
    dev->dp8390.tallycnt_2 = 0;

    dev->dp8390.curr_page = 0;

    dev->dp8390.rempkt_ptr   = 0;
    dev->dp8390.localpkt_ptr = 0;
    dev->dp8390.address_cnt  = 0;

    memset(&dev->dp8390.mem, 0x00, sizeof(dev->dp8390.mem));

    /* Set power-up conditions */
    dev->dp8390.CR.stop      = 1;
    dev->dp8390.CR.rdma_cmd  = 4;
    dev->dp8390.ISR.reset    = 1;
    dev->dp8390.DCR.longaddr = 1;

    nic_interrupt(dev, 0);
}


static void
nic_soft_reset(void *priv)
{
    nic_t *dev = (nic_t *)priv;

    memset(&(dev->dp8390.ISR), 0x00, sizeof(dev->dp8390.ISR));
    dev->dp8390.ISR.reset = 1;
}


/*
 * Access the 32K private RAM.
 *
 * The NE2000 memory is accessed through the data port of the
 * ASIC (offset 0) after setting up a remote-DMA transfer.
 * Both byte and word accesses are allowed.
 * The first 16 bytes contains the MAC address at even locations,
 * and there is 16K of buffer memory starting at 16K.
 */
static uint32_t
chipmem_read(nic_t *dev, uint32_t addr, unsigned int len)
{
    uint32_t retval = 0;

    if ((len == 2) && (addr & 0x1)) {
	nelog(3, "%s: unaligned chipmem word read\n", dev->name);
    }

    /* ROM'd MAC address */
    if (dev->board >= NE2K_NE2000) {
	    if (addr <= 31) {
		retval = dev->macaddr[addr % 32];
		if ((len == 2) || (len == 4)) {
			retval |= (dev->macaddr[(addr + 1) % 32] << 8);
		}
		if (len == 4) {
			retval |= (dev->macaddr[(addr + 2) % 32] << 16);
			retval |= (dev->macaddr[(addr + 3) % 32] << 24);
		}
		return(retval);
	    }

	    if ((addr >= DP8390_DWORD_MEMSTART) && (addr < DP8390_DWORD_MEMEND)) {
		retval = dev->dp8390.mem[addr - DP8390_DWORD_MEMSTART];
		if ((len == 2) || (len == 4)) {
			retval |= (dev->dp8390.mem[addr - DP8390_DWORD_MEMSTART + 1] << 8);
		}
		if (len == 4) {
			retval |= (dev->dp8390.mem[addr - DP8390_DWORD_MEMSTART + 2] << 16);
			retval |= (dev->dp8390.mem[addr - DP8390_DWORD_MEMSTART + 3] << 24);
		}
		return(retval);
	    }
    } else {
	    if (addr <= 15) {
		retval = dev->macaddr[addr % 16];
		if (len == 2) {
			retval |= (dev->macaddr[(addr + 1) % 16] << 8);
		}
		return(retval);
	    }

	    if ((addr >= DP8390_WORD_MEMSTART) && (addr < DP8390_WORD_MEMEND)) {
		retval = dev->dp8390.mem[addr - DP8390_WORD_MEMSTART];
		if (len == 2) {
			retval |= (dev->dp8390.mem[addr - DP8390_WORD_MEMSTART + 1] << 8);
		}
		return(retval);
	    }
    }

    nelog(3, "%s: out-of-bounds chipmem read, %04X\n", dev->name, addr);

    if (dev->is_pci) {
	return(0xff);
    } else {
	switch(len) {
		case 1:
			return(0xff);
		case 2:
			return(0xffff);
	}
    }

    return(0xffff);
}


static void
chipmem_write(nic_t *dev, uint32_t addr, uint32_t val, unsigned len)
{
    if ((len == 2) && (addr & 0x1)) {
	nelog(3, "%s: unaligned chipmem word write\n", dev->name);
    }

    if (dev->board >= NE2K_NE2000) {
	if ((addr >= DP8390_DWORD_MEMSTART) && (addr < DP8390_DWORD_MEMEND)) {
		dev->dp8390.mem[addr-DP8390_DWORD_MEMSTART] = val & 0xff;
		if ((len == 2) || (len == 4)) {
			dev->dp8390.mem[addr-DP8390_DWORD_MEMSTART+1] = val >> 8;
		}
		if (len == 4) {
			dev->dp8390.mem[addr-DP8390_DWORD_MEMSTART+2] = val >> 16;
			dev->dp8390.mem[addr-DP8390_DWORD_MEMSTART+3] = val >> 24;
		}
	} else {
		nelog(3, "%s: out-of-bounds chipmem write, %04X\n", dev->name, addr);
	}
    } else {
	if ((addr >= DP8390_WORD_MEMSTART) && (addr < DP8390_WORD_MEMEND)) {
		dev->dp8390.mem[addr-DP8390_WORD_MEMSTART] = val & 0xff;
		if (len == 2) {
			dev->dp8390.mem[addr-DP8390_WORD_MEMSTART+1] = val >> 8;
		}
	} else {
		nelog(3, "%s: out-of-bounds chipmem write, %04X\n", dev->name, addr);
	}
    }
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
		if (len > dev->dp8390.remote_bytes) {
			nelog(3, "%s: DMA read underrun iolen=%d remote_bytes=%d\n",
					dev->name, len, dev->dp8390.remote_bytes);
		}

		nelog(3, "%s: DMA read: addr=%4x remote_bytes=%d\n",
			dev->name, dev->dp8390.remote_dma,dev->dp8390.remote_bytes);
		retval = chipmem_read(dev, dev->dp8390.remote_dma, len);

		/* The 8390 bumps the address and decreases the byte count
		   by the selected word size after every access, not by
		   the amount of data requested by the host (io_len). */
		if (len == 4) {
			dev->dp8390.remote_dma += len;
		} else {
			dev->dp8390.remote_dma += (dev->dp8390.DCR.wdsize + 1);
		}

		if (dev->dp8390.remote_dma == dev->dp8390.page_stop << 8) {
			dev->dp8390.remote_dma = dev->dp8390.page_start << 8;
		}

		/* keep s.remote_bytes from underflowing */
		if (dev->dp8390.remote_bytes > dev->dp8390.DCR.wdsize) {
			if (len == 4) {
				dev->dp8390.remote_bytes -= len;
			} else {
				dev->dp8390.remote_bytes -= (dev->dp8390.DCR.wdsize + 1);
			}
		} else {
			dev->dp8390.remote_bytes = 0;
		}

		/* If all bytes have been written, signal remote-DMA complete */
		if (dev->dp8390.remote_bytes == 0) {
			dev->dp8390.ISR.rdma_done = 1;
			if (dev->dp8390.IMR.rdma_inte)
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
		if ((len > 1) && (dev->dp8390.DCR.wdsize == 0)) {
			nelog(3, "%s: DMA write length %d on byte mode operation\n",
							dev->name, len);
			break;
		}
		if (dev->dp8390.remote_bytes == 0)
			nelog(3, "%s: DMA write, byte count 0\n", dev->name);

		chipmem_write(dev, dev->dp8390.remote_dma, val, len);
		if (len == 4)
			dev->dp8390.remote_dma += len;
		  else
			dev->dp8390.remote_dma += (dev->dp8390.DCR.wdsize + 1);

		if (dev->dp8390.remote_dma == dev->dp8390.page_stop << 8)
			dev->dp8390.remote_dma = dev->dp8390.page_start << 8;

		if (len == 4)
			dev->dp8390.remote_bytes -= len;
		  else
			dev->dp8390.remote_bytes -= (dev->dp8390.DCR.wdsize + 1);

		if (dev->dp8390.remote_bytes > DP8390_DWORD_MEMSIZ)
			dev->dp8390.remote_bytes = 0;

		/* If all bytes have been written, signal remote-DMA complete */
		if (dev->dp8390.remote_bytes == 0) {
			dev->dp8390.ISR.rdma_done = 1;
			if (dev->dp8390.IMR.rdma_inte)
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


/* Handle reads/writes to the 'zeroth' page of the DS8390 register file. */
static uint32_t
page0_read(nic_t *dev, uint32_t off, unsigned int len)
{
    uint8_t retval = 0;

    if (len > 1) {
	/* encountered with win98 hardware probe */
	nelog(3, "%s: bad length! Page0 read from register 0x%02x, len=%u\n",
							dev->name, off, len);
	return(retval);
    }

    switch(off) {
	case 0x01:	/* CLDA0 */
		retval = (dev->dp8390.local_dma & 0xff);
		break;

	case 0x02:	/* CLDA1 */
		retval = (dev->dp8390.local_dma >> 8);
		break;

	case 0x03:	/* BNRY */
		retval = dev->dp8390.bound_ptr;
		break;

	case 0x04:	/* TSR */
		retval = ((dev->dp8390.TSR.ow_coll    << 7) |
			  (dev->dp8390.TSR.cd_hbeat   << 6) |
			  (dev->dp8390.TSR.fifo_ur    << 5) |
			  (dev->dp8390.TSR.no_carrier << 4) |
			  (dev->dp8390.TSR.aborted    << 3) |
			  (dev->dp8390.TSR.collided   << 2) |
			  (dev->dp8390.TSR.tx_ok));
		break;

	case 0x05:	/* NCR */
		retval = dev->dp8390.num_coll;
		break;

	case 0x06:	/* FIFO */
		/* reading FIFO is only valid in loopback mode */
		nelog(3, "%s: reading FIFO not supported yet\n", dev->name);
		retval = dev->dp8390.fifo;
		break;

	case 0x07:	/* ISR */
		retval = ((dev->dp8390.ISR.reset     << 7) |
			  (dev->dp8390.ISR.rdma_done << 6) |
			  (dev->dp8390.ISR.cnt_oflow << 5) |
			  (dev->dp8390.ISR.overwrite << 4) |
			  (dev->dp8390.ISR.tx_err    << 3) |
			  (dev->dp8390.ISR.rx_err    << 2) |
			  (dev->dp8390.ISR.pkt_tx    << 1) |
			  (dev->dp8390.ISR.pkt_rx));
		break;

	case 0x08:	/* CRDA0 */
		retval = (dev->dp8390.remote_dma & 0xff);
		break;

	case 0x09:	/* CRDA1 */
		retval = (dev->dp8390.remote_dma >> 8);
		break;

	case 0x0a:	/* reserved / RTL8029ID0 */
		if (dev->board == NE2K_RTL8019AS) {
			retval = 0x50;
		} else if (dev->board == NE2K_RTL8029AS) {
			retval = 0x50;
		} else {
			nelog(3, "%s: reserved Page0 read - 0x0a\n", dev->name);
			retval = 0xff;
		}
		break;

	case 0x0b:	/* reserved / RTL8029ID1 */
		if (dev->board == NE2K_RTL8019AS) {
			retval = 0x70;
		} else if (dev->board == NE2K_RTL8029AS) {
			retval = 0x43;
		} else {
			nelog(3, "%s: reserved Page0 read - 0x0b\n", dev->name);
			retval = 0xff;
		}
		break;

	case 0x0c:	/* RSR */
		retval = ((dev->dp8390.RSR.deferred    << 7) |
			  (dev->dp8390.RSR.rx_disabled << 6) |
			  (dev->dp8390.RSR.rx_mbit     << 5) |
			  (dev->dp8390.RSR.rx_missed   << 4) |
			  (dev->dp8390.RSR.fifo_or     << 3) |
			  (dev->dp8390.RSR.bad_falign  << 2) |
			  (dev->dp8390.RSR.bad_crc     << 1) |
			  (dev->dp8390.RSR.rx_ok));
		break;

	case 0x0d:	/* CNTR0 */
		retval = dev->dp8390.tallycnt_0;
		break;

	case 0x0e:	/* CNTR1 */
		retval = dev->dp8390.tallycnt_1;
		break;

	case 0x0f:	/* CNTR2 */
		retval = dev->dp8390.tallycnt_2;
		break;

	default:
		nelog(3, "%s: Page0 register 0x%02x out of range\n",
							dev->name, off);
		break;
    }

    nelog(3, "%s: Page0 read from register 0x%02x, value=0x%02x\n",
						dev->name, off, retval);

    return(retval);
}


static void
page0_write(nic_t *dev, uint32_t off, uint32_t val, unsigned len)
{
    uint8_t val2;

    /* It appears to be a common practice to use outw on page0 regs... */

    /* break up outw into two outb's */
    if (len == 2) {
	page0_write(dev, off, (val & 0xff), 1);
	if (off < 0x0f)
		page0_write(dev, off+1, ((val>>8)&0xff), 1);
	return;
    }

    nelog(3, "%s: Page0 write to register 0x%02x, value=0x%02x\n",
						dev->name, off, val);

    switch(off) {
	case 0x01:	/* PSTART */
		dev->dp8390.page_start = val;
		break;

	case 0x02:	/* PSTOP */
		dev->dp8390.page_stop = val;
		break;

	case 0x03:	/* BNRY */
		dev->dp8390.bound_ptr = val;
		break;

	case 0x04:	/* TPSR */
		dev->dp8390.tx_page_start = val;
		break;

	case 0x05:	/* TBCR0 */
		/* Clear out low byte and re-insert */
		dev->dp8390.tx_bytes &= 0xff00;
		dev->dp8390.tx_bytes |= (val & 0xff);
		break;

	case 0x06:	/* TBCR1 */
		/* Clear out high byte and re-insert */
		dev->dp8390.tx_bytes &= 0x00ff;
		dev->dp8390.tx_bytes |= ((val & 0xff) << 8);
		break;

	case 0x07:	/* ISR */
		val &= 0x7f;  /* clear RST bit - status-only bit */
		/* All other values are cleared iff the ISR bit is 1 */
		dev->dp8390.ISR.pkt_rx    &= !((int)((val & 0x01) == 0x01));
		dev->dp8390.ISR.pkt_tx    &= !((int)((val & 0x02) == 0x02));
		dev->dp8390.ISR.rx_err    &= !((int)((val & 0x04) == 0x04));
		dev->dp8390.ISR.tx_err    &= !((int)((val & 0x08) == 0x08));
		dev->dp8390.ISR.overwrite &= !((int)((val & 0x10) == 0x10));
		dev->dp8390.ISR.cnt_oflow &= !((int)((val & 0x20) == 0x20));
		dev->dp8390.ISR.rdma_done &= !((int)((val & 0x40) == 0x40));
		val = ((dev->dp8390.ISR.rdma_done << 6) |
		       (dev->dp8390.ISR.cnt_oflow << 5) |
		       (dev->dp8390.ISR.overwrite << 4) |
		       (dev->dp8390.ISR.tx_err    << 3) |
		       (dev->dp8390.ISR.rx_err    << 2) |
		       (dev->dp8390.ISR.pkt_tx    << 1) |
		       (dev->dp8390.ISR.pkt_rx));
		val &= ((dev->dp8390.IMR.rdma_inte << 6) |
		        (dev->dp8390.IMR.cofl_inte << 5) |
		        (dev->dp8390.IMR.overw_inte << 4) |
		        (dev->dp8390.IMR.txerr_inte << 3) |
		        (dev->dp8390.IMR.rxerr_inte << 2) |
		        (dev->dp8390.IMR.tx_inte << 1) |
		        (dev->dp8390.IMR.rx_inte));
		if (val == 0x00)
			nic_interrupt(dev, 0);
		break;

	case 0x08:	/* RSAR0 */
		/* Clear out low byte and re-insert */
		dev->dp8390.remote_start &= 0xff00;
		dev->dp8390.remote_start |= (val & 0xff);
		dev->dp8390.remote_dma = dev->dp8390.remote_start;
		break;

	case 0x09:	/* RSAR1 */
		/* Clear out high byte and re-insert */
		dev->dp8390.remote_start &= 0x00ff;
		dev->dp8390.remote_start |= ((val & 0xff) << 8);
		dev->dp8390.remote_dma = dev->dp8390.remote_start;
		break;

	case 0x0a:	/* RBCR0 */
		/* Clear out low byte and re-insert */
		dev->dp8390.remote_bytes &= 0xff00;
		dev->dp8390.remote_bytes |= (val & 0xff);
		break;

	case 0x0b:	/* RBCR1 */
		/* Clear out high byte and re-insert */
		dev->dp8390.remote_bytes &= 0x00ff;
		dev->dp8390.remote_bytes |= ((val & 0xff) << 8);
		break;

	case 0x0c:	/* RCR */
		/* Check if the reserved bits are set */
		if (val & 0xc0) {
			nelog(3, "%s: RCR write, reserved bits set\n",
							dev->name);
		}

		/* Set all other bit-fields */
		dev->dp8390.RCR.errors_ok = ((val & 0x01) == 0x01);
		dev->dp8390.RCR.runts_ok  = ((val & 0x02) == 0x02);
		dev->dp8390.RCR.broadcast = ((val & 0x04) == 0x04);
		dev->dp8390.RCR.multicast = ((val & 0x08) == 0x08);
		dev->dp8390.RCR.promisc   = ((val & 0x10) == 0x10);
		dev->dp8390.RCR.monitor   = ((val & 0x20) == 0x20);

		/* Monitor bit is a little suspicious... */
		if (val & 0x20) nelog(3, "%s: RCR write, monitor bit set!\n",
								dev->name);
		break;

	case 0x0d:	/* TCR */
		/* Check reserved bits */
		if (val & 0xe0) nelog(3, "%s: TCR write, reserved bits set\n",
								dev->name);

		/* Test loop mode (not supported) */
		if (val & 0x06) {
			dev->dp8390.TCR.loop_cntl = (val & 0x6) >> 1;
			nelog(3, "%s: TCR write, loop mode %d not supported\n",
						dev->name, dev->dp8390.TCR.loop_cntl);
		} else {
			dev->dp8390.TCR.loop_cntl = 0;
		}

		/* Inhibit-CRC not supported. */
		if (val & 0x01) nelog(3,
			"%s: TCR write, inhibit-CRC not supported\n",dev->name);

		/* Auto-transmit disable very suspicious */
		if (val & 0x08) nelog(3,
			"%s: TCR write, auto transmit disable not supported\n",
								dev->name);

		/* Allow collision-offset to be set, although not used */
		dev->dp8390.TCR.coll_prio = ((val & 0x08) == 0x08);
		break;

	case 0x0e:	/* DCR */
		/* the loopback mode is not suppported yet */
		if (! (val & 0x08)) nelog(3,
			"%s: DCR write, loopback mode selected\n", dev->name);

		/* It is questionable to set longaddr and auto_rx, since
		 * they are not supported on the NE2000. Print a warning
		 * and continue. */
		if (val & 0x04)
			nelog(3, "%s: DCR write - LAS set ???\n", dev->name);
		if (val & 0x10)
			nelog(3, "%s: DCR write - AR set ???\n", dev->name);

		/* Set other values. */
		dev->dp8390.DCR.wdsize   = ((val & 0x01) == 0x01);
		dev->dp8390.DCR.endian   = ((val & 0x02) == 0x02);
		dev->dp8390.DCR.longaddr = ((val & 0x04) == 0x04); /* illegal ? */
		dev->dp8390.DCR.loop     = ((val & 0x08) == 0x08);
		dev->dp8390.DCR.auto_rx  = ((val & 0x10) == 0x10); /* also illegal ? */
		dev->dp8390.DCR.fifo_size = (val & 0x50) >> 5;
		break;

	case 0x0f:  /* IMR */
		/* Check for reserved bit */
		if (val & 0x80)
			nelog(3, "%s: IMR write, reserved bit set\n",dev->name);

		/* Set other values */
		dev->dp8390.IMR.rx_inte    = ((val & 0x01) == 0x01);
		dev->dp8390.IMR.tx_inte    = ((val & 0x02) == 0x02);
		dev->dp8390.IMR.rxerr_inte = ((val & 0x04) == 0x04);
		dev->dp8390.IMR.txerr_inte = ((val & 0x08) == 0x08);
		dev->dp8390.IMR.overw_inte = ((val & 0x10) == 0x10);
		dev->dp8390.IMR.cofl_inte  = ((val & 0x20) == 0x20);
		dev->dp8390.IMR.rdma_inte  = ((val & 0x40) == 0x40);
		val2 = ((dev->dp8390.ISR.rdma_done << 6) |
		        (dev->dp8390.ISR.cnt_oflow << 5) |
		        (dev->dp8390.ISR.overwrite << 4) |
		        (dev->dp8390.ISR.tx_err    << 3) |
		        (dev->dp8390.ISR.rx_err    << 2) |
		        (dev->dp8390.ISR.pkt_tx    << 1) |
		        (dev->dp8390.ISR.pkt_rx));
		if (((val & val2) & 0x7f) == 0)
			nic_interrupt(dev, 0);
		  else
			nic_interrupt(dev, 1);
		break;

	default:
		nelog(3, "%s: Page0 write, bad register 0x%02x\n",
						dev->name, off);
		break;
    }
}


/* Handle reads/writes to the first page of the DS8390 register file. */
static uint32_t
page1_read(nic_t *dev, uint32_t off, unsigned int len)
{
    nelog(3, "%s: Page1 read from register 0x%02x, len=%u\n",
					dev->name, off, len);

    switch(off) {
	case 0x01:	/* PAR0-5 */
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x06:
		return(dev->dp8390.physaddr[off - 1]);

	case 0x07:	/* CURR */
		nelog(3, "%s: returning current page: 0x%02x\n",
				dev->name, (dev->dp8390.curr_page));
		return(dev->dp8390.curr_page);

	case 0x08:	/* MAR0-7 */
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		return(dev->dp8390.mchash[off - 8]);

	default:
		nelog(3, "%s: Page1 read register 0x%02x out of range\n",
							dev->name, off);
		return(0);
    }
}


static void
page1_write(nic_t *dev, uint32_t off, uint32_t val, unsigned len)
{
    nelog(3, "%s: Page1 write to register 0x%02x, len=%u, value=0x%04x\n",
						dev->name, off, len, val);

    switch(off) {
	case 0x01:	/* PAR0-5 */
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x06:
		dev->dp8390.physaddr[off - 1] = val;
		if (off == 6) nelog(3,
		  "%s: physical address set to %02x:%02x:%02x:%02x:%02x:%02x\n",
			dev->name,
			dev->dp8390.physaddr[0], dev->dp8390.physaddr[1],
			dev->dp8390.physaddr[2], dev->dp8390.physaddr[3],
			dev->dp8390.physaddr[4], dev->dp8390.physaddr[5]);
		break;

	case 0x07:	/* CURR */
		dev->dp8390.curr_page = val;
		break;

	case 0x08:	/* MAR0-7 */
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		dev->dp8390.mchash[off - 8] = val;
		break;

	default:
		nelog(3, "%s: Page1 write register 0x%02x out of range\n",
							dev->name, off);
		break;
    }
}


/* Handle reads/writes to the second page of the DS8390 register file. */
static uint32_t
page2_read(nic_t *dev, uint32_t off, unsigned int len)
{
    nelog(3, "%s: Page2 read from register 0x%02x, len=%u\n",
					dev->name, off, len);
  
    switch(off) {
	case 0x01:	/* PSTART */
		return(dev->dp8390.page_start);

	case 0x02:	/* PSTOP */
		return(dev->dp8390.page_stop);

	case 0x03:	/* Remote Next-packet pointer */
		return(dev->dp8390.rempkt_ptr);

	case 0x04:	/* TPSR */
		return(dev->dp8390.tx_page_start);

	case 0x05:	/* Local Next-packet pointer */
		return(dev->dp8390.localpkt_ptr);

	case 0x06:	/* Address counter (upper) */
		return(dev->dp8390.address_cnt >> 8);

	case 0x07:	/* Address counter (lower) */
		return(dev->dp8390.address_cnt & 0xff);

	case 0x08:	/* Reserved */
	case 0x09:
	case 0x0a:
	case 0x0b:
		nelog(3, "%s: reserved Page2 read - register 0x%02x\n",
							dev->name, off);
		return(0xff);

	case 0x0c:	/* RCR */
		return	((dev->dp8390.RCR.monitor   << 5) |
			 (dev->dp8390.RCR.promisc   << 4) |
			 (dev->dp8390.RCR.multicast << 3) |
			 (dev->dp8390.RCR.broadcast << 2) |
			 (dev->dp8390.RCR.runts_ok  << 1) |
			 (dev->dp8390.RCR.errors_ok));

	case 0x0d:	/* TCR */
		return	((dev->dp8390.TCR.coll_prio   << 4) |
			 (dev->dp8390.TCR.ext_stoptx  << 3) |
			 ((dev->dp8390.TCR.loop_cntl & 0x3) << 1) |
			 (dev->dp8390.TCR.crc_disable));

	case 0x0e:	/* DCR */
		return	(((dev->dp8390.DCR.fifo_size & 0x3) << 5) |
			 (dev->dp8390.DCR.auto_rx  << 4) |
			 (dev->dp8390.DCR.loop     << 3) |
			 (dev->dp8390.DCR.longaddr << 2) |
			 (dev->dp8390.DCR.endian   << 1) |
			 (dev->dp8390.DCR.wdsize));

	case 0x0f:	/* IMR */
		return	((dev->dp8390.IMR.rdma_inte  << 6) |
			 (dev->dp8390.IMR.cofl_inte  << 5) |
			 (dev->dp8390.IMR.overw_inte << 4) |
			 (dev->dp8390.IMR.txerr_inte << 3) |
			 (dev->dp8390.IMR.rxerr_inte << 2) |
			 (dev->dp8390.IMR.tx_inte    << 1) |
			 (dev->dp8390.IMR.rx_inte));

	default:
		nelog(3, "%s: Page2 register 0x%02x out of range\n",
						dev->name, off);
		break;
    }

    return(0);
}


static void
page2_write(nic_t *dev, uint32_t off, uint32_t val, unsigned len)
{
/* Maybe all writes here should be BX_PANIC()'d, since they
   affect internal operation, but let them through for now
   and print a warning. */
    nelog(3, "%s: Page2 write to register 0x%02x, len=%u, value=0x%04x\n",
						dev->name, off, len, val);
    switch(off) {
	case 0x01:	/* CLDA0 */
		/* Clear out low byte and re-insert */
		dev->dp8390.local_dma &= 0xff00;
		dev->dp8390.local_dma |= (val & 0xff);
		break;

	case 0x02:	/* CLDA1 */
		/* Clear out high byte and re-insert */
		dev->dp8390.local_dma &= 0x00ff;
		dev->dp8390.local_dma |= ((val & 0xff) << 8);
		break;

	case 0x03:	/* Remote Next-pkt pointer */
		dev->dp8390.rempkt_ptr = val;
		break;

	case 0x04:
		nelog(3, "page 2 write to reserved register 0x04\n");
		break;

	case 0x05:	/* Local Next-packet pointer */
		dev->dp8390.localpkt_ptr = val;
		break;

	case 0x06:	/* Address counter (upper) */
		/* Clear out high byte and re-insert */
		dev->dp8390.address_cnt &= 0x00ff;
		dev->dp8390.address_cnt |= ((val & 0xff) << 8);
		break;

	case 0x07:	/* Address counter (lower) */
		/* Clear out low byte and re-insert */
		dev->dp8390.address_cnt &= 0xff00;
		dev->dp8390.address_cnt |= (val & 0xff);
		break;

	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		nelog(3, "%s: Page2 write to reserved register 0x%02x\n",
							dev->name, off);
		break;

	default:
		nelog(3, "%s: Page2 write, illegal register 0x%02x\n",
							dev->name, off);
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


/* Routines for handling reads/writes to the Command Register. */
static uint32_t
read_cr(nic_t *dev)
{
    uint32_t retval;

    retval =	(((dev->dp8390.CR.pgsel    & 0x03) << 6) |
		 ((dev->dp8390.CR.rdma_cmd & 0x07) << 3) |
		  (dev->dp8390.CR.tx_packet << 2) |
		  (dev->dp8390.CR.start     << 1) |
		  (dev->dp8390.CR.stop));
    nelog(3, "%s: read CR returns 0x%02x\n", dev->name, retval);

    return(retval);
}


static void
write_cr(nic_t *dev, uint32_t val)
{
    nelog(3, "%s: wrote 0x%02x to CR\n", dev->name, val);

    /* Validate remote-DMA */
    if ((val & 0x38) == 0x00) {
	nelog(3, "%s: CR write - invalid rDMA value 0\n", dev->name);
	val |= 0x20; /* dma_cmd == 4 is a safe default */
    }

    /* Check for s/w reset */
    if (val & 0x01) {
	dev->dp8390.ISR.reset = 1;
	dev->dp8390.CR.stop   = 1;
    } else {
	dev->dp8390.CR.stop = 0;
    }

    dev->dp8390.CR.rdma_cmd = (val & 0x38) >> 3;

    /* If start command issued, the RST bit in the ISR */
    /* must be cleared */
    if ((val & 0x02) && !dev->dp8390.CR.start)
	dev->dp8390.ISR.reset = 0;

    dev->dp8390.CR.start = ((val & 0x02) == 0x02);
    dev->dp8390.CR.pgsel = (val & 0xc0) >> 6;

    /* Check for send-packet command */
    if (dev->dp8390.CR.rdma_cmd == 3) {
	/* Set up DMA read from receive ring */
	dev->dp8390.remote_start = dev->dp8390.remote_dma = dev->dp8390.bound_ptr * 256;
	dev->dp8390.remote_bytes = (uint16_t) chipmem_read(dev, dev->dp8390.bound_ptr * 256 + 2, 2);
	nelog(3, "%s: sending buffer #x%x length %d\n",
		dev->name, dev->dp8390.remote_start, dev->dp8390.remote_bytes);
    }

    /* Check for start-tx */
    if ((val & 0x04) && dev->dp8390.TCR.loop_cntl) {
	if (dev->dp8390.TCR.loop_cntl != 1) {
		nelog(3, "%s: loop mode %d not supported\n",
				dev->name, dev->dp8390.TCR.loop_cntl);
	} else {
		if (dev->board >= NE2K_NE2000) {
			nic_rx(dev,
				  &dev->dp8390.mem[dev->dp8390.tx_page_start*256 - DP8390_DWORD_MEMSTART],
				  dev->dp8390.tx_bytes);
		} else {
			nic_rx(dev,
				  &dev->dp8390.mem[dev->dp8390.tx_page_start*256 - DP8390_WORD_MEMSTART],
				  dev->dp8390.tx_bytes);
		}
	}
    } else if (val & 0x04) {
	if (dev->dp8390.CR.stop || (!dev->dp8390.CR.start && (dev->board < NE2K_RTL8019AS))) {
		if (dev->dp8390.tx_bytes == 0) /* njh@bandsman.co.uk */ {
			return; /* Solaris9 probe */
		}
		nelog(3, "%s: CR write - tx start, dev in reset\n", dev->name);
	}

	if (dev->dp8390.tx_bytes == 0)
		nelog(3, "%s: CR write - tx start, tx bytes == 0\n", dev->name);

	/* Send the packet to the system driver */
	dev->dp8390.CR.tx_packet = 1;
	if (dev->board >= NE2K_NE2000) {
		network_tx(&dev->dp8390.mem[dev->dp8390.tx_page_start*256 - DP8390_DWORD_MEMSTART],
			   dev->dp8390.tx_bytes);
	} else {
		network_tx(&dev->dp8390.mem[dev->dp8390.tx_page_start*256 - DP8390_WORD_MEMSTART],
			   dev->dp8390.tx_bytes);
	}

	/* some more debug */
	if (dev->dp8390.tx_timer_active)
		nelog(3, "%s: CR write, tx timer still active\n", dev->name);

	nic_tx(dev, val);
    }

    /* Linux probes for an interrupt by setting up a remote-DMA read
     * of 0 bytes with remote-DMA completion interrupts enabled.
     * Detect this here */
    if (dev->dp8390.CR.rdma_cmd == 0x01 && dev->dp8390.CR.start && dev->dp8390.remote_bytes == 0) {
	dev->dp8390.ISR.rdma_done = 1;
	if (dev->dp8390.IMR.rdma_inte) {
		nic_interrupt(dev, 1);
		if (! dev->is_pci)
			nic_interrupt(dev, 0);
	}
    }
}


static uint32_t
nic_read(nic_t *dev, uint32_t addr, unsigned len)
{
    uint32_t retval = 0;
    int off = addr - dev->base_address;

    nelog(3, "%s: read addr %x, len %d\n", dev->name, addr, len);

    if (off >= 0x10) {
	retval = asic_read(dev, off - 0x10, len);
    } else if (off == 0x00) {
	retval = read_cr(dev);
    } else switch(dev->dp8390.CR.pgsel) {
	case 0x00:
		retval = page0_read(dev, off, len);
		break;

	case 0x01:
		retval = page1_read(dev, off, len);
		break;

	case 0x02:
		retval = page2_read(dev, off, len);
		break;

	case 0x03:
		retval = page3_read(dev, off, len);
		break;

	default:
		nelog(3, "%s: unknown value of pgsel in read - %d\n",
						dev->name, dev->dp8390.CR.pgsel);
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
    nic_t *dev = (nic_t *)priv;

    if (dev->dp8390.DCR.wdsize & 1)
	return(nic_read(dev, addr, 2));
      else
	return(nic_read(dev, addr, 1));
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
    if (off >= 0x10) {
	asic_write(dev, off - 0x10, val, len);
    } else if (off == 0x00) {
	write_cr(dev, val);
    } else switch(dev->dp8390.CR.pgsel) {
	case 0x00:
		page0_write(dev, off, val, len);
		break;

	case 0x01:
		page1_write(dev, off, val, len);
		break;

	case 0x02:
		page2_write(dev, off, val, len);
		break;

	case 0x03:
		page3_write(dev, off, val, len);
		break;

	default:
		nelog(3, "%s: unknown value of pgsel in write - %d\n",
						dev->name, dev->dp8390.CR.pgsel);
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
    nic_t *dev = (nic_t *)priv;

    if (dev->dp8390.DCR.wdsize & 1)
	nic_write(dev, addr, val, 2);
      else
	nic_write(dev, addr, val, 1);
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

    if (PCI && dev->is_pci)
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

#if 0
	case 0x0C:			/* (reserved) */
		ret = dev->pci_regs[addr];
		break;

	case 0x0D:			/* PCI_LTR */
	case 0x0E:			/* PCI_HTR */
		ret = dev->pci_regs[addr];
		break;

	case 0x0F:			/* (reserved) */
		ret = dev->pci_regs[addr];
		break;
#endif

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
#if 0
		if (val & PCI_COMMAND_MEMORY) {
			...
		}
#endif
		dev->pci_regs[addr] = val & 0x03;
		break;

#if 0
	case 0x0C:			/* (reserved) */
		dev->pci_regs[addr] = val;
		break;

	case 0x0D:			/* PCI_LTR */
		dev->pci_regs[addr] = val;
		break;

	case 0x0E:			/* PCI_HTR */
		dev->pci_regs[addr] = val;
		break;

	case 0x0F:			/* (reserved) */
		dev->pci_regs[addr] = val;
		break;
#endif

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
nic_tx(nic_t *dev, uint32_t val)
{
    dev->dp8390.CR.tx_packet = 0;
    dev->dp8390.TSR.tx_ok = 1;
    dev->dp8390.ISR.pkt_tx = 1;

    /* Generate an interrupt if not masked */
    if (dev->dp8390.IMR.tx_inte)
	nic_interrupt(dev, 1);
    dev->dp8390.tx_timer_active = 0;
}


/*
 * Called by the platform-specific code when an Ethernet frame
 * has been received. The destination address is tested to see
 * if it should be accepted, and if the RX ring has enough room,
 * it is copied into it and the receive process is updated.
 */
static void
nic_rx(void *priv, uint8_t *buf, int io_len)
{
    static uint8_t bcast_addr[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    nic_t *dev = (nic_t *)priv;
    uint8_t pkthdr[4];
    uint8_t *startptr;
    int pages, avail;
    int idx, nextpage;
    int endbytes;

    //FIXME: move to upper layer
    ui_sb_update_icon(SB_NETWORK, 1);

    if (io_len != 60)
	nelog(2, "%s: rx_frame with length %d\n", dev->name, io_len);

    if ((dev->dp8390.CR.stop != 0) || (dev->dp8390.page_start == 0)) return;

    /*
     * Add the pkt header + CRC to the length, and work
     * out how many 256-byte pages the frame would occupy.
     */
    pages = (io_len + sizeof(pkthdr) + sizeof(uint32_t) + 255)/256;
    if (dev->dp8390.curr_page < dev->dp8390.bound_ptr) {
	avail = dev->dp8390.bound_ptr - dev->dp8390.curr_page;
    } else {
	avail = (dev->dp8390.page_stop - dev->dp8390.page_start) -
		(dev->dp8390.curr_page - dev->dp8390.bound_ptr);
    }

    /*
     * Avoid getting into a buffer overflow condition by
     * not attempting to do partial receives. The emulation
     * to handle this condition seems particularly painful.
     */
    if	((avail < pages)
#if NE2K_NEVER_FULL_RING
		 || (avail == pages)
#endif
		) {
	nelog(1, "%s: no space\n", dev->name);

    //FIXME: move to upper layer
	ui_sb_update_icon(SB_NETWORK, 0);
	return;
    }

    if ((io_len < 40/*60*/) && !dev->dp8390.RCR.runts_ok) {
	nelog(1, "%s: rejected small packet, length %d\n", dev->name, io_len);

    //FIXME: move to upper layer
	ui_sb_update_icon(SB_NETWORK, 0);
	return;
    }

    /* Some computers don't care... */
    if (io_len < 60)
	io_len = 60;

    nelog(2, "%s: RX %x:%x:%x:%x:%x:%x > %x:%x:%x:%x:%x:%x len %d\n",
	dev->name,
	buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
	buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
 	io_len);

    /* Do address filtering if not in promiscuous mode. */
    if (! dev->dp8390.RCR.promisc) {
	/* If this is a broadcast frame.. */
	if (! memcmp(buf, bcast_addr, 6)) {
		/* Broadcast not enabled, we're done. */
		if (! dev->dp8390.RCR.broadcast) {
			nelog(2, "%s: RX BC disabled\n", dev->name);

    //FIXME: move to upper layer
			ui_sb_update_icon(SB_NETWORK, 0);
			return;
		}
	}

	/* If this is a multicast frame.. */
	else if (buf[0] & 0x01) {
		/* Multicast not enabled, we're done. */
		if (! dev->dp8390.RCR.multicast) {
#if 1
			nelog(2, "%s: RX MC disabled\n", dev->name);
#endif

    //FIXME: move to upper layer
			ui_sb_update_icon(SB_NETWORK, 0);
			return;
		}

		/* Are we listening to this multicast address? */
		idx = mcast_index(buf);
		if (! (dev->dp8390.mchash[idx>>3] & (1<<(idx&0x7)))) {
			nelog(2, "%s: RX MC not listed\n", dev->name);

    //FIXME: move to upper layer
			ui_sb_update_icon(SB_NETWORK, 0);
			return;
		}
	}

	/* Unicast, must be for us.. */
	else if (memcmp(buf, dev->dp8390.physaddr, 6)) return;
    } else {
	nelog(2, "%s: RX promiscuous receive\n", dev->name);
    }

    nextpage = dev->dp8390.curr_page + pages;
    if (nextpage >= dev->dp8390.page_stop)
	nextpage -= (dev->dp8390.page_stop - dev->dp8390.page_start);

    /* Set up packet header. */
    pkthdr[0] = 0x01;			/* RXOK - packet is OK */
    if (buf[0] & 0x01)
	pkthdr[0] |= 0x20;		/* MULTICAST packet */
    pkthdr[1] = nextpage;		/* ptr to next packet */
    pkthdr[2] = (io_len + sizeof(pkthdr))&0xff;	/* length-low */
    pkthdr[3] = (io_len + sizeof(pkthdr))>>8;	/* length-hi */
    nelog(2, "%s: RX pkthdr [%02x %02x %02x %02x]\n",
	dev->name, pkthdr[0], pkthdr[1], pkthdr[2], pkthdr[3]);

    /* Copy into buffer, update curpage, and signal interrupt if config'd */
    if (dev->board >= NE2K_NE2000)
	startptr = &dev->dp8390.mem[(dev->dp8390.curr_page * 256) - DP8390_DWORD_MEMSTART];
    else
	startptr = &dev->dp8390.mem[(dev->dp8390.curr_page * 256) - DP8390_WORD_MEMSTART];
    memcpy(startptr, pkthdr, sizeof(pkthdr));
    if ((nextpage > dev->dp8390.curr_page) ||
	((dev->dp8390.curr_page + pages) == dev->dp8390.page_stop)) {
	memcpy(startptr+sizeof(pkthdr), buf, io_len);
    } else {
	endbytes = (dev->dp8390.page_stop - dev->dp8390.curr_page) * 256;
	memcpy(startptr+sizeof(pkthdr), buf, endbytes-sizeof(pkthdr));
	if (dev->board >= NE2K_NE2000)
		startptr = &dev->dp8390.mem[(dev->dp8390.page_start * 256) - DP8390_DWORD_MEMSTART];
	else
		startptr = &dev->dp8390.mem[(dev->dp8390.page_start * 256) - DP8390_WORD_MEMSTART];
	memcpy(startptr, buf+endbytes-sizeof(pkthdr), io_len-endbytes+8);
    }
    dev->dp8390.curr_page = nextpage;

    dev->dp8390.RSR.rx_ok = 1;
    dev->dp8390.RSR.rx_mbit = (buf[0] & 0x01) ? 1 : 0;
    dev->dp8390.ISR.pkt_rx = 1;

    if (dev->dp8390.IMR.rx_inte)
	nic_interrupt(dev, 1);

    //FIXME: move to upper layer
    ui_sb_update_icon(SB_NETWORK, 0);
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
    switch(dev->board) {
	case NE2K_NE1000:
		dev->is_8bit = 1;
		/*FALLTHROUGH*/

	case NE2K_NE2000:
		dev->maclocal[0] = 0x00;  /* 00:00:D8 (Novell OID) */
		dev->maclocal[1] = 0x00;
		dev->maclocal[2] = 0xD8;
		rom = (dev->board == NE2K_NE1000) ? NULL : ROM_PATH_NE2000;
		break;

	case NE2K_RTL8019AS:
	case NE2K_RTL8029AS:
		dev->is_pci = (dev->board == NE2K_RTL8029AS) ? 1 : 0;
		dev->maclocal[0] = 0x00;  /* 00:E0:4C (Realtek OID) */
		dev->maclocal[1] = 0xE0;
		dev->maclocal[2] = 0x4C;
		rom = (dev->board == NE2K_RTL8019AS) ? ROM_PATH_RTL8019 : ROM_PATH_RTL8029;
		break;
    }

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

    /* See if we have a local MAC address configured. */
    mac = device_get_config_mac("mac", -1);

    /*
     * Make this device known to the I/O system.
     * PnP and PCI devices start with address spaces inactive.
     */
    if (dev->board < NE2K_RTL8019AS)
	nic_ioset(dev, dev->base_address);

    /* Set up our BIOS ROM space, if any. */
    nic_rom_init(dev, rom);

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
    memcpy(dev->dp8390.physaddr, dev->maclocal, sizeof(dev->maclocal));

    nelog(0, "%s: I/O=%04x, IRQ=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
	dev->name, dev->base_address, dev->base_irq,
	dev->dp8390.physaddr[0], dev->dp8390.physaddr[1], dev->dp8390.physaddr[2],
	dev->dp8390.physaddr[3], dev->dp8390.physaddr[4], dev->dp8390.physaddr[5]);

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

    /* Reset the board. */
    nic_reset(dev);

    /* Attach ourselves to the network module. */
    network_attach(dev, dev->dp8390.physaddr, nic_rx);

    nelog(1, "%s: %s attached IO=0x%X IRQ=%d\n", dev->name,
	dev->is_pci?"PCI":"ISA", dev->base_address, dev->base_irq);

    return(dev);
}


static void
nic_close(void *priv)
{
    nic_t *dev = (nic_t *)priv;

    /* Make sure the platform layer is shut down. */
    network_close();

    nic_ioremove(dev, dev->base_address);

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
				"IRQ 4", 4
			},
			{
				"IRQ 5", 5
			},
			{
				"IRQ 7", 7
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
				"IRQ 4", 4
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

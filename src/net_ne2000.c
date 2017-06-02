/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of an NE2000/RTL8029AS network controller.
 *
 * NOTE:	Its still a mess, but we're getting there. The file will
 *		also implement an NE1000 for 8-bit ISA systems.
 *
 * Version:	@(#)net_ne2000.c	1.0.8	2017/06/01
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Peter Grehan, grehan@iprg.nokia.com>
 *		SA1988, Tenshi
 *
 * Based on	@(#)ne2k.cc v1.56.2.1 2004/02/02 22:37:22 cbothamy
 *		Portions Copyright (C) 2002  MandrakeSoft S.A.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "pci.h"
#include "pic.h"
#include "device.h"
#include "config.h"
#include "disc_random.h"
#include "network.h"
#include "net_ne2000.h"
#include "bswap.h"


#ifdef WALTJE
# define ENABLE_NE2000_LOG 2
#endif


/* ROM BIOS file paths. */
#define ROM_PATH_NE1000		L"roms/network/ne1000/ne1000.rom"
#define ROM_PATH_NE2000		L"roms/network/ne2000/ne2000.rom"
#define ROM_PATH_RTL8029	L"roms/network/rtl8029as/rtl8029as.rom"

/* PCI info. */
#define PCI_VENDID		0x10ec		/* Realtek, Inc */
#define PCI_DEVID		0x8029		/* RTL8029AS */
#define PCI_REGSIZE		256		/* size of PCI space */


/* For PCI. */
typedef union {
    uint32_t addr;
    uint8_t addr_regs[4];
} bar_t;


#if ENABLE_NE2000_LOG
static int	nic_do_log = ENABLE_NE2000_LOG;
#else
static int	nic_do_log = 0;
#endif


/* Never completely fill the ne2k ring so that we never
   hit the unclear completely full buffer condition. */
#define NE2K_NEVER_FULL_RING (1)

#define NE2K_MEMSIZ    (32*1024)
#define NE2K_MEMSTART  (16*1024)
#define NE2K_MEMEND    (NE2K_MEMSTART+NE2K_MEMSIZ)


typedef struct {
    /* Page 0 */

    /* Command Register - 00h read/write */
    struct CR_t {
	int	stop;		/* STP - Software Reset command */
	int	start;		/* START - start the NIC */
	int	tx_packet;	/* TXP - initiate packet transmission */
	uint8_t	rdma_cmd;	/* RD0,RD1,RD2 - Remote DMA command */
	uint8_t	pgsel;		/* PS0,PS1 - Page select */
    }		CR;

    /* Interrupt Status Register - 07h read/write */
    struct ISR_t {
	int	pkt_rx;		/* PRX - packet received with no errors */
	int	pkt_tx;		/* PTX - packet txed with no errors */
	int	rx_err;		/* RXE - packet rxed with 1 or more errors */
	int	tx_err;		/* TXE - packet txed   "  " "    "    " */
	int	overwrite;	/* OVW - rx buffer resources exhausted */
	int	cnt_oflow;	/* CNT - network tally counter MSB's set */
	int	rdma_done;	/* RDC - remote DMA complete */
	int	reset;		/* RST - reset status */
    }		ISR;

    /* Interrupt Mask Register - 0fh write */
    struct IMR_t {
	int	rx_inte;	/* PRXE - packet rx interrupt enable */
	int	tx_inte;	/* PTXE - packet tx interrput enable */
	int	rxerr_inte;	/* RXEE - rx error interrupt enable */
	int	txerr_inte;	/* TXEE - tx error interrupt enable */
	int	overw_inte;	/* OVWE - overwrite warn int enable */
	int	cofl_inte;	/* CNTE - counter o'flow int enable */
	int	rdma_inte;	/* RDCE - remote DMA complete int enable */
	int	reserved;	/* D7 - reserved */
    }		IMR;

    /* Data Configuration Register - 0eh write */
    struct DCR_t {
	int	wdsize;		/* WTS - 8/16-bit select */
	int	endian;		/* BOS - byte-order select */
	int	longaddr;	/* LAS - long-address select */
	int	loop;		/* LS  - loopback select */
	int	auto_rx;	/* AR  - auto-remove rx pkts with remote DMA */
	uint8_t fifo_size;	/* FT0,FT1 - fifo threshold */
    }		DCR;

    /* Transmit Configuration Register - 0dh write */
    struct TCR_t {
	int	crc_disable;	/* CRC - inhibit tx CRC */
	uint8_t	loop_cntl;	/* LB0,LB1 - loopback control */
	int	ext_stoptx;	/* ATD - allow tx disable by external mcast */
	int	coll_prio;	/* OFST - backoff algorithm select */
	uint8_t	reserved;	/* D5,D6,D7 - reserved */
    }		TCR;

    /* Transmit Status Register - 04h read */
    struct TSR_t {
	int	tx_ok;		/* PTX - tx complete without error */
	int	reserved;	/*  D1 - reserved */
	int	collided;	/* COL - tx collided >= 1 times */
	int	aborted;	/* ABT - aborted due to excessive collisions */
	int	no_carrier;	/* CRS - carrier-sense lost */
	int	fifo_ur;	/* FU  - FIFO underrun */
	int	cd_hbeat;	/* CDH - no tx cd-heartbeat from transceiver */
	int	ow_coll;	/* OWC - out-of-window collision */
    }		TSR;

    /* Receive Configuration Register - 0ch write */
    struct RCR_t {
	int	errors_ok;	/* SEP - accept pkts with rx errors */
	int	runts_ok;	/* AR  - accept < 64-byte runts */
	int	broadcast;	/* AB  - accept eth broadcast address */
	int	multicast;	/* AM  - check mcast hash array */
	int	promisc;	/* PRO - accept all packets */
	int	monitor;	/* MON - check pkts, but don't rx */
	uint8_t	reserved;	/* D6,D7 - reserved */
    }		RCR;

    /* Receive Status Register - 0ch read */
    struct RSR_t {
	int	rx_ok;		/* PRX - rx complete without error */
	int	bad_crc;	/* CRC - Bad CRC detected */
	int	bad_falign;	/* FAE - frame alignment error */
	int	fifo_or;	/* FO  - FIFO overrun */
	int	rx_missed;	/* MPA - missed packet error */
	int	rx_mbit;	/* PHY - unicast or mcast/bcast address match */
	int	rx_disabled;	/* DIS - set when in monitor mode */
	int	deferred;	/* DFR - collision active */
    }		RSR;

    uint16_t	local_dma;	/* 01,02h read ; current local DMA addr */
    uint8_t	page_start;	/* 01h write ; page start regr */
    uint8_t	page_stop;	/* 02h write ; page stop regr */
    uint8_t	bound_ptr;	/* 03h read/write ; boundary pointer */
    uint8_t	tx_page_start;	/* 04h write ; transmit page start reg */
    uint8_t	num_coll;	/* 05h read  ; number-of-collisions reg */
    uint16_t	tx_bytes;	/* 05,06h write ; transmit byte-count reg */
    uint8_t	fifo;		/* 06h read  ; FIFO */
    uint16_t	remote_dma;	/* 08,09h read ; current remote DMA addr */
    uint16_t	remote_start;	/* 08,09h write ; remote start address reg */
    uint16_t	remote_bytes;	/* 0a,0bh write ; remote byte-count reg */
    uint8_t	tallycnt_0;	/* 0dh read  ; tally ctr 0 (frame align errs) */
    uint8_t	tallycnt_1;	/* 0eh read  ; tally ctr 1 (CRC errors) */
    uint8_t	tallycnt_2;	/* 0fh read  ; tally ctr 2 (missed pkt errs) */

    /* Page 1 */

    /*   Command Register 00h (repeated) */

    uint8_t	physaddr[6];	/* 01-06h read/write ; MAC address */
    uint8_t	curr_page;	/* 07h read/write ; current page register */
    uint8_t	mchash[8];	/* 08-0fh read/write ; multicast hash array */

    /* Page 2  - diagnostic use only */

    /*   Command Register 00h (repeated) */

    /*   Page Start Register 01h read  (repeated)
     *   Page Stop Register  02h read  (repeated)
     *   Current Local DMA Address 01,02h write (repeated)
     *   Transmit Page start address 04h read (repeated)
     *   Receive Configuration Register 0ch read (repeated)
     *   Transmit Configuration Register 0dh read (repeated)
     *   Data Configuration Register 0eh read (repeated)
     *   Interrupt Mask Register 0fh read (repeated)
     */
    uint8_t	rempkt_ptr;		/* 03h read/write ; rmt next-pkt ptr */
    uint8_t	localpkt_ptr;		/* 05h read/write ; lcl next-pkt ptr */
    uint16_t	address_cnt;		/* 06,07h read/write ; address cter */

    /* Page 3  - should never be modified. */

    /* Novell ASIC state */
    uint8_t	macaddr[32];		/* ASIC ROM'd MAC address, even bytes */
    uint8_t	mem[NE2K_MEMSIZ];	/* on-chip packet memory */

    int		board;
    int		is_pci;
    char	name[32];
    uint32_t	base_address;
    int		base_irq;
    uint32_t	bios_addr,
		bios_size,
		bios_mask;
    bar_t	pci_bar[2];
    uint8_t	pci_regs[PCI_REGSIZE];
    int		tx_timer_index;
    int		tx_timer_active;
    uint8_t	maclocal[6];		/* configured MAC (local) address */
    uint8_t	eeprom[128];		/* for RTL8029AS */
    rom_t	bios_rom;
    int		card;			/* PCI card slot */
} nic_t;


static void	nic_rx(void *, uint8_t *, int);
static void	nic_tx(nic_t *, uint32_t);


static void
nelog(int lvl, const char *fmt, ...)
{
#ifdef ENABLE_NE2000_LOG
    va_list ap;

    if (nic_do_log >= lvl) {
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
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

    /* Initialize the MAC address area by doubling the physical address */
    dev->macaddr[0]  = dev->physaddr[0];
    dev->macaddr[1]  = dev->physaddr[0];
    dev->macaddr[2]  = dev->physaddr[1];
    dev->macaddr[3]  = dev->physaddr[1];
    dev->macaddr[4]  = dev->physaddr[2];
    dev->macaddr[5]  = dev->physaddr[2];
    dev->macaddr[6]  = dev->physaddr[3];
    dev->macaddr[7]  = dev->physaddr[3];
    dev->macaddr[8]  = dev->physaddr[4];
    dev->macaddr[9]  = dev->physaddr[4];
    dev->macaddr[10] = dev->physaddr[5];
    dev->macaddr[11] = dev->physaddr[5];

    /* ne2k signature */
    for (i=12; i<32; i++)
	dev->macaddr[i] = 0x57;

    /* Zero out registers and memory */
    memset(&dev->CR,  0x00, sizeof(dev->CR) );
    memset(&dev->ISR, 0x00, sizeof(dev->ISR));
    memset(&dev->IMR, 0x00, sizeof(dev->IMR));
    memset(&dev->DCR, 0x00, sizeof(dev->DCR));
    memset(&dev->TCR, 0x00, sizeof(dev->TCR));
    memset(&dev->TSR, 0x00, sizeof(dev->TSR));
    memset(&dev->RSR, 0x00, sizeof(dev->RSR));
    dev->tx_timer_active = 0;
    dev->local_dma  = 0;
    dev->page_start = 0;
    dev->page_stop  = 0;
    dev->bound_ptr  = 0;
    dev->tx_page_start = 0;
    dev->num_coll   = 0;
    dev->tx_bytes   = 0;
    dev->fifo       = 0;
    dev->remote_dma = 0;
    dev->remote_start = 0;
    dev->remote_bytes = 0;
    dev->tallycnt_0 = 0;
    dev->tallycnt_1 = 0;
    dev->tallycnt_2 = 0;

    dev->curr_page = 0;

    dev->rempkt_ptr   = 0;
    dev->localpkt_ptr = 0;
    dev->address_cnt  = 0;

    memset(&dev->mem, 0x00, sizeof(dev->mem));

    /* Set power-up conditions */
    dev->CR.stop      = 1;
    dev->CR.rdma_cmd  = 4;
    dev->ISR.reset    = 1;
    dev->DCR.longaddr = 1;

    nic_interrupt(dev, 0);
}


static void
nic_soft_reset(void *priv)
{
    nic_t *dev = (nic_t *)priv;

    memset(&(dev->ISR), 0x00, sizeof(dev->ISR));
    dev->ISR.reset = 1;
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
    if ((addr >=0) && (addr <= 31)) {
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

    if ((addr >= NE2K_MEMSTART) && (addr < NE2K_MEMEND)) {
	retval = dev->mem[addr - NE2K_MEMSTART];
	if ((len == 2) || (len == 4)) {
		retval |= (dev->mem[addr - NE2K_MEMSTART + 1] << 8);
	}
	if (len == 4) {
		retval |= (dev->mem[addr - NE2K_MEMSTART + 2] << 16);
		retval |= (dev->mem[addr - NE2K_MEMSTART + 3] << 24);
	}
	return(retval);
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

    if ((addr >= NE2K_MEMSTART) && (addr < NE2K_MEMEND)) {
	dev->mem[addr-NE2K_MEMSTART] = val & 0xff;
	if ((len == 2) || (len == 4)) {
		dev->mem[addr-NE2K_MEMSTART+1] = val >> 8;
	}
	if (len == 4) {
		dev->mem[addr-NE2K_MEMSTART+2] = val >> 16;
		dev->mem[addr-NE2K_MEMSTART+3] = val >> 24;
	}
    } else {
	nelog(3, "%s: out-of-bounds chipmem write, %04X\n", dev->name, addr);
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
		if (len > dev->remote_bytes) {
			nelog(3, "%s: DMA read underrun iolen=%d remote_bytes=%d\n",
					dev->name, len, dev->remote_bytes);
		}

		nelog(3, "%s: DMA read: addr=%4x remote_bytes=%d\n",
			dev->name, dev->remote_dma,dev->remote_bytes);
		retval = chipmem_read(dev, dev->remote_dma, len);

		/* The 8390 bumps the address and decreases the byte count
		   by the selected word size after every access, not by
		   the amount of data requested by the host (io_len). */
		if (len == 4) {
			dev->remote_dma += len;
		} else {
			dev->remote_dma += (dev->DCR.wdsize + 1);
		}

		if (dev->remote_dma == dev->page_stop << 8) {
			dev->remote_dma = dev->page_start << 8;
		}

		/* keep s.remote_bytes from underflowing */
		if (dev->remote_bytes > dev->DCR.wdsize) {
			if (len == 4) {
				dev->remote_bytes -= len;
			} else {
				dev->remote_bytes -= (dev->DCR.wdsize + 1);
			}
		} else {
			dev->remote_bytes = 0;
		}

		/* If all bytes have been written, signal remote-DMA complete */
		if (dev->remote_bytes == 0) {
			dev->ISR.rdma_done = 1;
			if (dev->IMR.rdma_inte)
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
		if ((len > 1) && (dev->DCR.wdsize == 0)) {
			nelog(3, "%s: DMA write length %d on byte mode operation\n",
							dev->name, len);
			break;
		}
		if (dev->remote_bytes == 0) {
			nelog(3, "%s: DMA write, byte count 0\n", dev->name);
		}

		chipmem_write(dev, dev->remote_dma, val, len);
		if (len == 4) {
			dev->remote_dma += len;
		} else {
			dev->remote_dma += (dev->DCR.wdsize + 1);
		}

		if (dev->remote_dma == dev->page_stop << 8) {
			dev->remote_dma = dev->page_start << 8;
		}

		if (len == 4) {
			dev->remote_bytes -= len;
		} else {
			dev->remote_bytes -= (dev->DCR.wdsize + 1);
		}

		if (dev->remote_bytes > NE2K_MEMSIZ) {
			dev->remote_bytes = 0;
		}

		/* If all bytes have been written, signal remote-DMA complete */
		if (dev->remote_bytes == 0) {
			dev->ISR.rdma_done = 1;
			if (dev->IMR.rdma_inte)
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
		retval = (dev->local_dma & 0xff);
		break;

	case 0x02:	/* CLDA1 */
		retval = (dev->local_dma >> 8);
		break;

	case 0x03:	/* BNRY */
		retval = dev->bound_ptr;
		break;

	case 0x04:	/* TSR */
		retval = ((dev->TSR.ow_coll    << 7) |
			  (dev->TSR.cd_hbeat   << 6) |
			  (dev->TSR.fifo_ur    << 5) |
			  (dev->TSR.no_carrier << 4) |
			  (dev->TSR.aborted    << 3) |
			  (dev->TSR.collided   << 2) |
			  (dev->TSR.tx_ok));
		break;

	case 0x05:	/* NCR */
		retval = dev->num_coll;
		break;

	case 0x06:	/* FIFO */
		/* reading FIFO is only valid in loopback mode */
		nelog(3, "%s: reading FIFO not supported yet\n", dev->name);
		retval = dev->fifo;
		break;

	case 0x07:	/* ISR */
		retval = ((dev->ISR.reset     << 7) |
			  (dev->ISR.rdma_done << 6) |
			  (dev->ISR.cnt_oflow << 5) |
			  (dev->ISR.overwrite << 4) |
			  (dev->ISR.tx_err    << 3) |
			  (dev->ISR.rx_err    << 2) |
			  (dev->ISR.pkt_tx    << 1) |
			  (dev->ISR.pkt_rx));
		break;

	case 0x08:	/* CRDA0 */
		retval = (dev->remote_dma & 0xff);
		break;

	case 0x09:	/* CRDA1 */
		retval = (dev->remote_dma >> 8);
		break;

	case 0x0a:	/* reserved / RTL8029ID0 */
		if (dev->is_pci) {
			retval = 0x50;
		} else {
			nelog(3, "%s: reserved Page0 read - 0x0a\n", dev->name);
			retval = 0xff;
		}
		break;

	case 0x0b:	/* reserved / RTL8029ID1 */
		if (dev->is_pci) {
			retval = 0x43;
		} else {
			nelog(3, "%s: reserved Page0 read - 0x0b\n", dev->name);
			retval = 0xff;
		}
		break;

	case 0x0c:	/* RSR */
		retval = ((dev->RSR.deferred    << 7) |
			  (dev->RSR.rx_disabled << 6) |
			  (dev->RSR.rx_mbit     << 5) |
			  (dev->RSR.rx_missed   << 4) |
			  (dev->RSR.fifo_or     << 3) |
			  (dev->RSR.bad_falign  << 2) |
			  (dev->RSR.bad_crc     << 1) |
			  (dev->RSR.rx_ok));
		break;

	case 0x0d:	/* CNTR0 */
		retval = dev->tallycnt_0;
		break;

	case 0x0e:	/* CNTR1 */
		retval = dev->tallycnt_1;
		break;

	case 0x0f:	/* CNTR2 */
		retval = dev->tallycnt_2;
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
		dev->page_start = val;
		break;

	case 0x02:	/* PSTOP */
		dev->page_stop = val;
		break;

	case 0x03:	/* BNRY */
		dev->bound_ptr = val;
		break;

	case 0x04:	/* TPSR */
		dev->tx_page_start = val;
		break;

	case 0x05:	/* TBCR0 */
		/* Clear out low byte and re-insert */
		dev->tx_bytes &= 0xff00;
		dev->tx_bytes |= (val & 0xff);
		break;

	case 0x06:	/* TBCR1 */
		/* Clear out high byte and re-insert */
		dev->tx_bytes &= 0x00ff;
		dev->tx_bytes |= ((val & 0xff) << 8);
		break;

	case 0x07:	/* ISR */
		val &= 0x7f;  /* clear RST bit - status-only bit */
		/* All other values are cleared iff the ISR bit is 1 */
		dev->ISR.pkt_rx    &= ~((int)((val & 0x01) == 0x01));
		dev->ISR.pkt_tx    &= ~((int)((val & 0x02) == 0x02));
		dev->ISR.rx_err    &= ~((int)((val & 0x04) == 0x04));
		dev->ISR.tx_err    &= ~((int)((val & 0x08) == 0x08));
		dev->ISR.overwrite &= ~((int)((val & 0x10) == 0x10));
		dev->ISR.cnt_oflow &= ~((int)((val & 0x20) == 0x20));
		dev->ISR.rdma_done &= ~((int)((val & 0x40) == 0x40));
		val = ((dev->ISR.rdma_done << 6) |
		       (dev->ISR.cnt_oflow << 5) |
		       (dev->ISR.overwrite << 4) |
		       (dev->ISR.tx_err    << 3) |
		       (dev->ISR.rx_err    << 2) |
		       (dev->ISR.pkt_tx    << 1) |
		       (dev->ISR.pkt_rx));
		val &= ((dev->IMR.rdma_inte << 6) |
		        (dev->IMR.cofl_inte << 5) |
		        (dev->IMR.overw_inte << 4) |
		        (dev->IMR.txerr_inte << 3) |
		        (dev->IMR.rxerr_inte << 2) |
		        (dev->IMR.tx_inte << 1) |
		        (dev->IMR.rx_inte));
		if (val == 0x00)
			nic_interrupt(dev, 0);
		break;

	case 0x08:	/* RSAR0 */
		/* Clear out low byte and re-insert */
		dev->remote_start &= 0xff00;
		dev->remote_start |= (val & 0xff);
		dev->remote_dma = dev->remote_start;
		break;

	case 0x09:	/* RSAR1 */
		/* Clear out high byte and re-insert */
		dev->remote_start &= 0x00ff;
		dev->remote_start |= ((val & 0xff) << 8);
		dev->remote_dma = dev->remote_start;
		break;

	case 0x0a:	/* RBCR0 */
		/* Clear out low byte and re-insert */
		dev->remote_bytes &= 0xff00;
		dev->remote_bytes |= (val & 0xff);
		break;

	case 0x0b:	/* RBCR1 */
		/* Clear out high byte and re-insert */
		dev->remote_bytes &= 0x00ff;
		dev->remote_bytes |= ((val & 0xff) << 8);
		break;

	case 0x0c:	/* RCR */
		/* Check if the reserved bits are set */
		if (val & 0xc0) {
			nelog(3, "%s: RCR write, reserved bits set\n",
							dev->name);
		}

		/* Set all other bit-fields */
		dev->RCR.errors_ok = ((val & 0x01) == 0x01);
		dev->RCR.runts_ok  = ((val & 0x02) == 0x02);
		dev->RCR.broadcast = ((val & 0x04) == 0x04);
		dev->RCR.multicast = ((val & 0x08) == 0x08);
		dev->RCR.promisc   = ((val & 0x10) == 0x10);
		dev->RCR.monitor   = ((val & 0x20) == 0x20);

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
			dev->TCR.loop_cntl = (val & 0x6) >> 1;
			nelog(3, "%s: TCR write, loop mode %d not supported\n",
						dev->name, dev->TCR.loop_cntl);
		} else {
			dev->TCR.loop_cntl = 0;
		}

		/* Inhibit-CRC not supported. */
		if (val & 0x01) nelog(3,
			"%s: TCR write, inhibit-CRC not supported\n",dev->name);

		/* Auto-transmit disable very suspicious */
		if (val & 0x08) nelog(3,
			"%s: TCR write, auto transmit disable not supported\n",
								dev->name);

		/* Allow collision-offset to be set, although not used */
		dev->TCR.coll_prio = ((val & 0x08) == 0x08);
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
		dev->DCR.wdsize   = ((val & 0x01) == 0x01);
		dev->DCR.endian   = ((val & 0x02) == 0x02);
		dev->DCR.longaddr = ((val & 0x04) == 0x04); /* illegal ? */
		dev->DCR.loop     = ((val & 0x08) == 0x08);
		dev->DCR.auto_rx  = ((val & 0x10) == 0x10); /* also illegal ? */
		dev->DCR.fifo_size = (val & 0x50) >> 5;
		break;

	case 0x0f:  /* IMR */
		/* Check for reserved bit */
		if (val & 0x80)
			nelog(3, "%s: IMR write, reserved bit set\n",dev->name);

		/* Set other values */
		dev->IMR.rx_inte    = ((val & 0x01) == 0x01);
		dev->IMR.tx_inte    = ((val & 0x02) == 0x02);
		dev->IMR.rxerr_inte = ((val & 0x04) == 0x04);
		dev->IMR.txerr_inte = ((val & 0x08) == 0x08);
		dev->IMR.overw_inte = ((val & 0x10) == 0x10);
		dev->IMR.cofl_inte  = ((val & 0x20) == 0x20);
		dev->IMR.rdma_inte  = ((val & 0x40) == 0x40);
		val2 = ((dev->ISR.rdma_done << 6) |
		        (dev->ISR.cnt_oflow << 5) |
		        (dev->ISR.overwrite << 4) |
		        (dev->ISR.tx_err    << 3) |
		        (dev->ISR.rx_err    << 2) |
		        (dev->ISR.pkt_tx    << 1) |
		        (dev->ISR.pkt_rx));
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
		return(dev->physaddr[off - 1]);

	case 0x07:	/* CURR */
		nelog(3, "%s: returning current page: 0x%02x\n",
				dev->name, (dev->curr_page));
		return(dev->curr_page);

	case 0x08:	/* MAR0-7 */
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		return(dev->mchash[off - 8]);

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
		dev->physaddr[off - 1] = val;
		if (off == 6) nelog(3,
		  "%s: physical address set to %02x:%02x:%02x:%02x:%02x:%02x\n",
			dev->name,
			dev->physaddr[0], dev->physaddr[1],
			dev->physaddr[2], dev->physaddr[3],
			dev->physaddr[4], dev->physaddr[5]);
		break;

	case 0x07:	/* CURR */
		dev->curr_page = val;
		break;

	case 0x08:	/* MAR0-7 */
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		dev->mchash[off - 8] = val;
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
		return(dev->page_start);

	case 0x02:	/* PSTOP */
		return(dev->page_stop);

	case 0x03:	/* Remote Next-packet pointer */
		return(dev->rempkt_ptr);

	case 0x04:	/* TPSR */
		return(dev->tx_page_start);

	case 0x05:	/* Local Next-packet pointer */
		return(dev->localpkt_ptr);

	case 0x06:	/* Address counter (upper) */
		return(dev->address_cnt >> 8);

	case 0x07:	/* Address counter (lower) */
		return(dev->address_cnt & 0xff);

	case 0x08:	/* Reserved */
	case 0x09:
	case 0x0a:
	case 0x0b:
		nelog(3, "%s: reserved Page2 read - register 0x%02x\n",
							dev->name, off);
		return(0xff);

	case 0x0c:	/* RCR */
		return	((dev->RCR.monitor   << 5) |
			 (dev->RCR.promisc   << 4) |
			 (dev->RCR.multicast << 3) |
			 (dev->RCR.broadcast << 2) |
			 (dev->RCR.runts_ok  << 1) |
			 (dev->RCR.errors_ok));

	case 0x0d:	/* TCR */
		return	((dev->TCR.coll_prio   << 4) |
			 (dev->TCR.ext_stoptx  << 3) |
			 ((dev->TCR.loop_cntl & 0x3) << 1) |
			 (dev->TCR.crc_disable));

	case 0x0e:	/* DCR */
		return	(((dev->DCR.fifo_size & 0x3) << 5) |
			 (dev->DCR.auto_rx  << 4) |
			 (dev->DCR.loop     << 3) |
			 (dev->DCR.longaddr << 2) |
			 (dev->DCR.endian   << 1) |
			 (dev->DCR.wdsize));

	case 0x0f:	/* IMR */
		return	((dev->IMR.rdma_inte  << 6) |
			 (dev->IMR.cofl_inte  << 5) |
			 (dev->IMR.overw_inte << 4) |
			 (dev->IMR.txerr_inte << 3) |
			 (dev->IMR.rxerr_inte << 2) |
			 (dev->IMR.tx_inte    << 1) |
			 (dev->IMR.rx_inte));

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
		dev->local_dma &= 0xff00;
		dev->local_dma |= (val & 0xff);
		break;

	case 0x02:	/* CLDA1 */
		/* Clear out high byte and re-insert */
		dev->local_dma &= 0x00ff;
		dev->local_dma |= ((val & 0xff) << 8);
		break;

	case 0x03:	/* Remote Next-pkt pointer */
		dev->rempkt_ptr = val;
		break;

	case 0x04:
		nelog(3, "page 2 write to reserved register 0x04\n");
		break;

	case 0x05:	/* Local Next-packet pointer */
		dev->localpkt_ptr = val;
		break;

	case 0x06:	/* Address counter (upper) */
		/* Clear out high byte and re-insert */
		dev->address_cnt &= 0x00ff;
		dev->address_cnt |= ((val & 0xff) << 8);
		break;

	case 0x07:	/* Address counter (lower) */
		/* Clear out low byte and re-insert */
		dev->address_cnt &= 0xff00;
		dev->address_cnt |= (val & 0xff);
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
    if (dev->is_pci) switch(off) {
	case 0x3:	/* CONFIG0 */
		return(0x00);

	case 0x5:	/* CONFIG2 */
		return(0x40);

	case 0x6:	/* CONFIG3 */
		return(0x40);

	default:
		break;
    }

    nelog(3, "%s: Page3 read register 0x%02x attempted\n", dev->name, off);
    return(0x00);
}


static void
page3_write(nic_t *dev, uint32_t off, uint32_t val, unsigned len)
{
    nelog(3, "%s: Page3 write register 0x%02x attempted\n", dev->name, off);
}


/* Routines for handling reads/writes to the Command Register. */
static uint32_t
read_cr(nic_t *dev)
{
    uint32_t retval;

    retval =	(((dev->CR.pgsel    & 0x03) << 6) |
		 ((dev->CR.rdma_cmd & 0x07) << 3) |
		  (dev->CR.tx_packet << 2) |
		  (dev->CR.start     << 1) |
		  (dev->CR.stop));
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
	dev->ISR.reset = 1;
	dev->CR.stop   = 1;
    } else {
	dev->CR.stop = 0;
    }

    dev->CR.rdma_cmd = (val & 0x38) >> 3;

    /* If start command issued, the RST bit in the ISR */
    /* must be cleared */
    if ((val & 0x02) && !dev->CR.start)
	dev->ISR.reset = 0;

    dev->CR.start = ((val & 0x02) == 0x02);
    dev->CR.pgsel = (val & 0xc0) >> 6;

    /* Check for send-packet command */
    if (dev->CR.rdma_cmd == 3) {
	/* Set up DMA read from receive ring */
	dev->remote_start = dev->remote_dma = dev->bound_ptr * 256;
	dev->remote_bytes = (uint16_t) chipmem_read(dev, dev->bound_ptr * 256 + 2, 2);
	nelog(3, "%s: sending buffer #x%x length %d\n",
		dev->name, dev->remote_start, dev->remote_bytes);
    }

    /* Check for start-tx */
    if ((val & 0x04) && dev->TCR.loop_cntl) {
	if (dev->TCR.loop_cntl != 1) {
		nelog(3, "%s: loop mode %d not supported\n",
				dev->name, dev->TCR.loop_cntl);
	} else {
		nic_rx(dev,
			  &dev->mem[dev->tx_page_start*256 - NE2K_MEMSTART],
			  dev->tx_bytes);
	}
    } else if (val & 0x04) {
	if (dev->CR.stop || (!dev->CR.start && !dev->is_pci)) {
		if (dev->tx_bytes == 0) /* njh@bandsman.co.uk */ {
			return; /* Solaris9 probe */
		}
		nelog(3, "%s: CR write - tx start, dev in reset\n", dev->name);
	}

	if (dev->tx_bytes == 0)
		nelog(3, "%s: CR write - tx start, tx bytes == 0\n", dev->name);

	/* Send the packet to the system driver */
	dev->CR.tx_packet = 1;
	network_tx(&dev->mem[dev->tx_page_start*256 - NE2K_MEMSTART],
		   dev->tx_bytes);

	/* some more debug */
	if (dev->tx_timer_active)
		nelog(3, "%s: CR write, tx timer still active\n", dev->name);

	nic_tx(dev, val);
    }

    /* Linux probes for an interrupt by setting up a remote-DMA read
     * of 0 bytes with remote-DMA completion interrupts enabled.
     * Detect this here */
    if (dev->CR.rdma_cmd == 0x01 && dev->CR.start && dev->remote_bytes == 0) {
	dev->ISR.rdma_done = 1;
	if (dev->IMR.rdma_inte) {
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
    } else switch(dev->CR.pgsel) {
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
						dev->name, dev->CR.pgsel);
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

    if (dev->DCR.wdsize & 1)
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
    } else switch(dev->CR.pgsel) {
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
						dev->name, dev->CR.pgsel);
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

    if (dev->DCR.wdsize & 1)
	nic_write(dev, addr, val, 2);
      else
	nic_write(dev, addr, val, 1);
}


static void
nic_writel(uint16_t addr, uint32_t val, void *priv)
{
    nic_write((nic_t *)priv, addr, val, 4);
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
	io_sethandler(addr+16, 16,
		      nic_readb, nic_readw, NULL,
		      nic_writeb, nic_writew, NULL, dev);
	io_sethandler(addr+0x1f, 1,
		      nic_readb, NULL, NULL,
		      nic_writeb, NULL, NULL, dev);	
    }
}


static void
nic_ioremove(nic_t *dev, int16_t addr)
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
	io_removehandler(addr+16, 16,
			 nic_readb, nic_readw, NULL,
			 nic_writeb, nic_writew, NULL, dev);
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
	
    /* PCI BIOS stuff, just enable_disable. */
    if ((dev->bios_addr > 0) && reg_bios_enable) {
	mem_mapping_enable(&dev->bios_rom.mapping);
	mem_mapping_set_addr(&dev->bios_rom.mapping,
			     dev->bios_addr, dev->bios_size);
	nelog(1, "%s: BIOS now at: %06X\n", dev->name, dev->bios_addr);
    } else {
	nelog(1, "%s: BIOS disabled\n", dev->name);
	mem_mapping_disable(&dev->bios_rom.mapping);
	dev->bios_addr = 0;
	if (dev->is_pci)
		dev->pci_bar[1].addr = 0;
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

	case 0x10:			/* PCI_BAR 7:5 */
		ret = (dev->pci_bar[0].addr_regs[1] & 0xe0) | 0x01;
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
		ret = (dev->pci_bar[1].addr_regs[1] & dev->bios_mask) | 0x18;
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

    nelog(2, "%s: PCI_Write(%d, %04x, %02x)\n", dev->name, func, addr, val);

    switch(addr) {
	case 0x04:			/* PCI_COMMAND_LO */
		val &= 0x03;
		nic_ioremove(dev, dev->base_address);
		if (val & PCI_COMMAND_IO)
			nic_ioset(dev, dev->base_address);
#if 0
		if (val & PCI_COMMAND_MEMORY) {
			...
		}
#endif
		dev->pci_regs[addr] = val;
		break;

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

	case 0x10:			/* PCI_BAR */
		val &= 0xfc;	/* 0xe0 acc to RTL DS */
		val |= 0x01;	/* re-enable IOIN bit */
		/*FALLTHROUGH*/

	case 0x11:			/* PCI_BAR
	case 0x12:			/* PCI_BAR
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
		if (val & PCI_COMMAND_IO)
			nic_ioset(dev, dev->base_address);
		break;

	case 0x30:			/* PCI_ROMBAR */
	case 0x31:			/* PCI_ROMBAR */
	case 0x32:			/* PCI_ROMBAR */
	case 0x33:			/* PCI_ROMBAR */
		dev->pci_bar[1].addr_regs[addr & 3] = val;
		dev->pci_bar[1].addr_regs[1] &= dev->bios_mask;
		dev->pci_bar[1].addr &= 0xffffe000;
		dev->bios_addr = dev->pci_bar[1].addr;
		dev->pci_bar[1].addr |= 0x1801;
		nic_update_bios(dev);
		return;

#if 0
	case 0x3C:			/* PCI_ILR */
		if (val != 0xFF) {
			nelog(1, "%s: IRQ now: %i\n", dev->name, val);
			dev->base_irq = val;
		}
		dev->pci_regs[addr] = dev->base_irq;
		return;
#endif
    }
}


/*
 * Return the 6-bit index into the multicast
 * table. Stolen unashamedly from FreeBSD's if_ed.c
 */
static int
mcast_index(const void *dst)
{
#define POLYNOMIAL 0x04c11db6
	unsigned long crc = 0xffffffffL;
	int carry, i, j;
	unsigned char b;
	unsigned char *ep = (unsigned char *) dst;

	for (i = 6; --i >= 0;)
	{
		b = *ep++;
		for (j = 8; --j >= 0;)
		{
			carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
			crc <<= 1;
			b >>= 1;
			if (carry)
			{
				crc = ((crc ^ POLYNOMIAL) | carry);
			}
		}
	}
	return (crc >> 26);
#undef POLYNOMIAL
}


static void
nic_tx(nic_t *dev, uint32_t val)
{
    dev->CR.tx_packet = 0;
    dev->TSR.tx_ok = 1;
    dev->ISR.pkt_tx = 1;

    /* Generate an interrupt if not masked */
    if (dev->IMR.tx_inte)
	nic_interrupt(dev, 1);
    dev->tx_timer_active = 0;
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

    if (io_len != 60)
	nelog(2, "%s: rx_frame with length %d\n", dev->name, io_len);

    if ((dev->CR.stop != 0) || (dev->page_start == 0)) return;

    /*
     * Add the pkt header + CRC to the length, and work
     * out how many 256-byte pages the frame would occupy.
     */
    pages = (io_len + sizeof(pkthdr) + sizeof(uint32_t) + 255)/256;
    if (dev->curr_page < dev->bound_ptr) {
	avail = dev->bound_ptr - dev->curr_page;
    } else {
	avail = (dev->page_stop - dev->page_start) -
		(dev->curr_page - dev->bound_ptr);
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
	return;
    }

    if ((io_len < 40/*60*/) && !dev->RCR.runts_ok) {
	nelog(1, "%s: rejected small packet, length %d\n", dev->name, io_len);
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
    if (! dev->RCR.promisc) {
	/* If this is a broadcast frame.. */
	if (! memcmp(buf, bcast_addr, 6)) {
		/* Broadcast not enabled, we're done. */
		if (! dev->RCR.broadcast) {
			nelog(2, "%s: RX BC disabled\n", dev->name);
			return;
		}
	}

	/* If this is a multicast frame.. */
	else if (buf[0] & 0x01) {
		/* Multicast not enabled, we're done. */
		if (! dev->RCR.multicast) {
#if 1
			nelog(2, "%s: RX MC disabled\n", dev->name);
#endif
			return;
		}

		/* Are we listening to this multicast address? */
		idx = mcast_index(buf);
		if (! (dev->mchash[idx>>3] & (1<<(idx&0x7)))) {
			nelog(2, "%s: RX MC not listed\n", dev->name);
			return;
		}
	}

	/* Unicast, must be for us.. */
	else if (memcmp(buf, dev->physaddr, 6)) return;
    } else {
	nelog(2, "%s: RX promiscuous receive\n", dev->name);
    }

    nextpage = dev->curr_page + pages;
    if (nextpage >= dev->page_stop)
	nextpage -= (dev->page_stop - dev->page_start);

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
    startptr = &dev->mem[(dev->curr_page * 256) - NE2K_MEMSTART];
    memcpy(startptr, pkthdr, sizeof(pkthdr));
    if ((nextpage > dev->curr_page) ||
	((dev->curr_page + pages) == dev->page_stop)) {
	memcpy(startptr+sizeof(pkthdr), buf, io_len);
    } else {
	endbytes = (dev->page_stop - dev->curr_page) * 256;
	memcpy(startptr+sizeof(pkthdr), buf, endbytes-sizeof(pkthdr));
	startptr = &dev->mem[(dev->page_start * 256) - NE2K_MEMSTART];
	memcpy(startptr, buf+endbytes-sizeof(pkthdr), io_len-endbytes+8);
    }
    dev->curr_page = nextpage;

    dev->RSR.rx_ok = 1;
    dev->RSR.rx_mbit = (buf[0] & 0x01) ? 1 : 0;
    dev->ISR.pkt_rx = 1;

    if (dev->IMR.rx_inte)
	nic_interrupt(dev, 1);
}


static void
nic_rom_init(nic_t *dev, wchar_t *s)
{
    uint32_t temp;
    FILE *f;

    if (dev->bios_addr > 0) {
	if ((f = romfopen(s, L"rb")) != NULL) {
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
    }

    nelog(1, "%s: BIOS configured at %06lX (size %ld)\n",
		dev->name, dev->bios_addr, dev->bios_size);
}


static void *
nic_init(int board)
{
    uint32_t mac;
    wchar_t *rom;
    nic_t *dev;
    int i;

    /* Get the desired debug level. */
    i = device_get_config_int("debug");
    if (i > 0) nic_do_log = i;

    dev = malloc(sizeof(nic_t));
    memset(dev, 0x00, sizeof(nic_t));
    dev->board = board;
    switch(dev->board) {
	case NE2K_NE1000:
		strcpy(dev->name, "NE1000");
		dev->maclocal[0] = 0x00;  /* 00:00:D8 (NE1000 ISA OID) */
		dev->maclocal[1] = 0x00;
		dev->maclocal[2] = 0xD8;
		rom = ROM_PATH_NE1000;
		break;

	case NE2K_NE2000:
		strcpy(dev->name, "NE2000");
		dev->maclocal[0] = 0x00;  /* 00:A0:0C (NE2000 compatible OID) */
		dev->maclocal[1] = 0xA0;
		dev->maclocal[2] = 0x0C;
		rom = ROM_PATH_NE2000;
		break;

	case NE2K_RTL8029AS:
		dev->is_pci = (PCI) ? 1 : 0;
		strcpy(dev->name, "RTL8029AS");
		dev->maclocal[0] = 0x00;  /* 00:20:18 (RTL 8029AS PCI OID) */
		dev->maclocal[1] = 0x20;
		dev->maclocal[2] = 0x18;
		rom = ROM_PATH_RTL8029;
		break;
    }

    if (dev->is_pci) {
	dev->base_address = 0x340;
    } else {
	dev->base_address = device_get_config_hex16("base");
	dev->bios_addr = device_get_config_hex20("bios_addr");
    }
    dev->base_irq = device_get_config_int("irq");

    /* See if we have a local MAC address configured. */
    mac = device_get_config_mac("mac", -1);

    /* Make this device known to the I/O system. */
    nic_ioset(dev, dev->base_address);

    /* Set up our BIOS ROM space, if any. */
    nic_rom_init(dev, rom);

    if (dev->is_pci) {
	/*
	 * Configure the PCI space registers.
	 *
	 * We do this here, so the I/O routines are generic.
	 */
	dev->pci_regs[0x00] = (PCI_VENDID&0xff);
	dev->pci_regs[0x01] = (PCI_VENDID>>8);
	dev->pci_regs[0x02] = (PCI_DEVID&0xff);
	dev->pci_regs[0x03] = (PCI_DEVID>>8);

        dev->pci_regs[0x04] = 0x01;		/* IOEN */
        dev->pci_regs[0x05] = 0x00;
        dev->pci_regs[0x07] = 0x02;		/* DST0, medium devsel */

        dev->pci_regs[0x0B] = 0x02;		/* BCR: Network Controller */
        dev->pci_regs[0x0A] = 0x00;		/* SCR: Ethernet */

	dev->pci_regs[0x2C] = (PCI_VENDID&0xff);
	dev->pci_regs[0x2D] = (PCI_VENDID>>8);
	dev->pci_regs[0x2E] = (PCI_DEVID&0xff);
	dev->pci_regs[0x2F] = (PCI_DEVID>>8);

	dev->pci_regs[0x3C] = dev->base_irq;	/* PCI_ILR */
        dev->pci_regs[0x3D] = 0x01;		/* PCI_IPR */

	/* Enable our address space in PCI. */
	dev->pci_bar[0].addr_regs[0] = 0x01;

	/* Enable our BIOS space in PCI, if needed. */
	if (dev->bios_addr > 0) {
		dev->pci_bar[1].addr = 0x000F8000;
		dev->pci_bar[1].addr_regs[1] = dev->bios_mask;
		dev->pci_bar[1].addr |= 0x1801;
	} else {
		dev->pci_bar[1].addr = 0;
	}

	/* Initialize the RTL8029 EEPROM. */
        memset(dev->eeprom, 0x00, sizeof(dev->eeprom));
        dev->eeprom[0x76] =
	 dev->eeprom[0x7A] =
	 dev->eeprom[0x7E] = (PCI_DEVID&0xff);
        dev->eeprom[0x77] =
	 dev->eeprom[0x7B] =
	 dev->eeprom[0x7F] = (PCI_DEVID>>8);
        dev->eeprom[0x78] =
	 dev->eeprom[0x7C] = (PCI_VENDID&0xff);
        dev->eeprom[0x79] =
	 dev->eeprom[0x7D] = (PCI_VENDID>>8);

	/* Insert this device onto the PCI bus, keep its slot number. */
	dev->card = pci_add(nic_pci_read, nic_pci_write, dev);
    }

    /* Set up our BIA. */
    if (mac & 0xff000000) {
	/* Generate new local MAC. */
	dev->maclocal[3] = disc_random_generate();
	dev->maclocal[4] = disc_random_generate();
	dev->maclocal[5] = disc_random_generate();
	mac = (((int) dev->maclocal[3]) << 16);
	mac |= (((int) dev->maclocal[4]) << 8);
	mac |= ((int) dev->maclocal[5]);
	device_set_config_mac("mac", mac);
    } else {
	dev->maclocal[3] = (mac>>16) & 0xff;
	dev->maclocal[4] = (mac>>8) & 0xff;
	dev->maclocal[5] = (mac & 0xff);
    }
    memcpy(dev->physaddr, dev->maclocal, sizeof(dev->maclocal));

    nelog(0, "%s: I/O=%04x, IRQ=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
	dev->name, dev->base_address, dev->base_irq,
	dev->physaddr[0], dev->physaddr[1], dev->physaddr[2],
	dev->physaddr[3], dev->physaddr[4], dev->physaddr[5]);

    /* Reset the board. */
    nic_reset(dev);

    if (network_attach(dev, dev->physaddr, nic_rx) < 0) {
#if 0
	msgbox_error_wstr(ghwnd, L"Unable to init platform network");
#endif
	nelog(0, "%s: unable to init platform network type %d\n",
					dev->name, network_type);
#if 0
	/*
	 * To avoid crashes, we ignore the fact that even though
	 * there is no active platform support, we just continue
	 * initializing. If we return an error here, the device
	 * handling code will throw a fatal error... --FvK
	 */
	free(dev);
	return(NULL);
#endif
    }

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

    free(dev);

    nelog(1, "%s: closed\n", dev->name);
}


static void *
ne1000_init(void)
{
    return(nic_init(NE2K_NE1000));
}


static void *
ne2000_init(void)
{
    return(nic_init(NE2K_NE2000));
}


static void *
rtl8029as_init(void)
{
    return(nic_init(NE2K_RTL8029AS));
}


static device_config_t ne1000_config[] =
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
				"IRQ 3", 3
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

static device_config_t ne2000_config[] =
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

static device_config_t rtl8029as_config[] =
{
	{
		"irq", "IRQ", CONFIG_SELECTION, "", 10,
		{
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
#if 1
	/*
	 * WTF.
	 * Even though it is PCI, the user should still have control
	 * over whether or not it's Option ROM BIOS will be enabled
	 * or not.
	 */
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
#endif
	{
		"mac", "MAC Address", CONFIG_MAC, "", -1
	},
	{
		"", "", -1
	}
};



device_t ne1000_device = {
    "Novell NE1000",
    0,
    ne1000_init,
    nic_close,
    NULL,
    NULL,
    NULL,
    NULL,
    ne1000_config
};

device_t ne2000_device = {
    "Novell NE2000",
    0,
    ne2000_init,
    nic_close,
    NULL,
    NULL,
    NULL,
    NULL,
    ne2000_config
};

device_t rtl8029as_device = {
    "Realtek RTL8029AS",
    0,
    rtl8029as_init,
    nic_close,
    NULL,
    NULL,
    NULL,
    NULL,
    rtl8029as_config
};

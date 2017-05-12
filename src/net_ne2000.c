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
 * Version:	@(#)net_ne2000.c	1.0.2	2017/05/11
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
# define ENABLE_NE2000_LOG 1
#endif


/* For PCI. */
typedef union {
    uint32_t addr;
    uint8_t addr_regs[4];
} bar_t;


/* This stuff should go into the struct. --FvK */
static uint8_t	maclocal[6] = {0xac, 0xde, 0x48, 0x88, 0xbb, 0xaa};
static uint8_t	maclocal_pci[6] = {0xac, 0xde, 0x48, 0x88, 0xbb, 0xaa};
static uint8_t	rtl8029as_eeprom[128];
static uint8_t	pci_regs[256];
static bar_t	pci_bar[2];
static uint32_t	old_base_addr = 0;
#if ENABLE_NE2000_LOG
static int	nic_do_log = ENABLE_NE2000_LOG;
#else
static int	nic_do_log = 0;
#endif


#define BX_RESET_HARDWARE 0
#define BX_RESET_SOFTWARE 1

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
    uint8_t	rempkt_ptr;	/* 03h read/write ; remote next-packet ptr */
    uint8_t	localpkt_ptr;	/* 05h read/write ; local next-packet ptr */
    uint16_t	address_cnt;	/* 06,07h read/write ; address counter */

    /* Page 3  - should never be modified. */

    /* Novell ASIC state */
    uint8_t	macaddr[32];	/* ASIC ROM'd MAC address, even bytes */
    uint8_t	mem[NE2K_MEMSIZ];	/* on-chip packet memory */

    /* ne2k internal state */
    char	name[32];
    uint32_t	base_address;
    int		base_irq;
    int		is_rtl8029as;
    int		tx_timer_index;
    int		tx_timer_active;

    uint32_t	bios_addr,
		bios_size,
		bios_mask;
    int		disable_netbios;
    rom_t	bios_rom;
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
    }
#endif
}
#define pclog	nelog


/* reset - restore state to power-up, cancelling all i/o */
static void
nic_reset(void *priv, int reset)
{
    nic_t *dev = (nic_t *)priv;
    int i;

    pclog(1, "%s: reset\n", dev->name);

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
    for (i = 12; i < 32; i++) {
	dev->macaddr[i] = 0x57;
    }

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

    picint(1<<dev->base_irq);
    picintc(1<<dev->base_irq);
}


/* chipmem_read/chipmem_write - access the 64K private RAM.
   The ne2000 memory is accessed through the data port of
   the asic (offset 0) after setting up a remote-DMA transfer.
   Both byte and word accesses are allowed.
   The first 16 bytes contains the MAC address at even locations,
   and there is 16K of buffer memory starting at 16K
*/
static uint32_t
chipmem_read(nic_t *dev, uint32_t addr, unsigned int len)
{
    uint32_t retval = 0;

    if ((len == 2) && (addr & 0x1)) {
	pclog(1, "%s: unaligned chipmem word read\n", dev->name);
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

    pclog(1, "%s: out-of-bounds chipmem read, %04X\n", dev->name, addr);

    if (dev->is_rtl8029as) {
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
	pclog(1, "%s: unaligned chipmem word write\n", dev->name);
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
	pclog(1, "%s: out-of-bounds chipmem write, %04X\n", dev->name, addr);
    }
}


/* asic_read/asic_write - This is the high 16 bytes of i/o space
   (the lower 16 bytes is for the DS8390). Only two locations
   are used: offset 0, which is used for data transfer, and
   offset 0xf, which is used to reset the device.
   The data transfer port is used to as 'external' DMA to the
   DS8390. The chip has to have the DMA registers set up, and
   after that, insw/outsw instructions can be used to move
   the appropriate number of bytes to/from the device.
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
			pclog(1, "%s: DMA read underrun iolen=%d remote_bytes=%d\n",
					dev->name, len, dev->remote_bytes);
		}

		pclog(2, "%s: DMA read: addr=%4x remote_bytes=%d\n",
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
			if (dev->IMR.rdma_inte) {
				picint(1 << dev->base_irq);
			}
		}
		break;

	case 0x0f:	/* Reset register */
		nic_reset(dev, BX_RESET_SOFTWARE);
		break;

	default:
		pclog(1, "%s: ASIC read invalid address %04x\n",
					dev->name, (unsigned)off);
		break;
    }

    return(retval);
}


static void
asic_write(nic_t *dev, uint32_t off, uint32_t val, unsigned len)
{
    pclog(2, "%s: asic write addr=0x%02x, value=0x%04x\n",
		dev->name, (unsigned)off, (unsigned) val);
    switch(off) {
	case 0x00:	/* Data register - see asic_read for a description */
		if ((len > 1) && (dev->DCR.wdsize == 0)) {
			pclog(2, "%s: DMA write length %d on byte mode operation\n",
							dev->name, len);
			break;
		}
		if (dev->remote_bytes == 0) {
			pclog(2, "%s: DMA write, byte count 0\n", dev->name);
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
			if (dev->IMR.rdma_inte) {
				picint(1 << dev->base_irq);
			}
		}
		break;

	case 0x0f:  /* Reset register */
		/* end of reset pulse */
		break;

	default: /* this is invalid, but happens under win95 device detection */
		pclog(1, "%s: ASIC write invalid address %04x, ignoring\n",
						dev->name, (unsigned)off);
		break;
    }
}


/* page0_read/page0_write - These routines handle reads/writes to
   the 'zeroth' page of the DS8390 register file */
static uint32_t
page0_read(nic_t *dev, uint32_t off, unsigned int len)
{
    uint8_t retval = 0;

    if (len > 1) {
	/* encountered with win98 hardware probe */
	pclog(1, "%s: bad length! Page0 read from register 0x%02x, len=%u\n",
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
		pclog(1, "%s: reading FIFO not supported yet\n", dev->name);
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
		if (dev->is_rtl8029as) {
			retval = 0x50;
		} else {
			pclog(1, "%s: reserved Page0 read - 0x0a\n", dev->name);
			retval = 0xff;
		}
		break;

	case 0x0b:	/* reserved / RTL8029ID1 */
		if (dev->is_rtl8029as) {
			retval = 0x43;
		} else {
			pclog(1, "%s: reserved Page0 read - 0x0b\n", dev->name);
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
		pclog(1, "%s: Page0 register 0x%02x out of range\n",
							dev->name, off);
		break;
    }

    pclog(2, "%s: Page0 read from register 0x%02x, value=0x%02x\n",
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

    pclog(2, "%s: Page0 write to register 0x%02x, value=0x%02x\n",
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
		if (val == 0x00) {
			picintc(1 << dev->base_irq);
		}
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
			pclog(1, "%s: RCR write, reserved bits set\n",
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
		if (val & 0x20) pclog(1, "%s: RCR write, monitor bit set!\n",
								dev->name);
		break;

	case 0x0d:	/* TCR */
		/* Check reserved bits */
		if (val & 0xe0) pclog(1, "%s: TCR write, reserved bits set\n",
								dev->name);

		/* Test loop mode (not supported) */
		if (val & 0x06) {
			dev->TCR.loop_cntl = (val & 0x6) >> 1;
			pclog(1, "%s: TCR write, loop mode %d not supported\n",
						dev->name, dev->TCR.loop_cntl);
		} else {
			dev->TCR.loop_cntl = 0;
		}

		/* Inhibit-CRC not supported. */
		if (val & 0x01) pclog(1,
			"%s: TCR write, inhibit-CRC not supported\n",dev->name);

		/* Auto-transmit disable very suspicious */
		if (val & 0x08) pclog(1,
			"%s: TCR write, auto transmit disable not supported\n",
								dev->name);

		/* Allow collision-offset to be set, although not used */
		dev->TCR.coll_prio = ((val & 0x08) == 0x08);
		break;

	case 0x0e:	/* DCR */
		/* the loopback mode is not suppported yet */
		if (! (val & 0x08)) pclog(1,
			"%s: DCR write, loopback mode selected\n", dev->name);

		/* It is questionable to set longaddr and auto_rx, since
		 * they are not supported on the NE2000. Print a warning
		 * and continue. */
		if (val & 0x04)
			pclog(1, "%s: DCR write - LAS set ???\n", dev->name);
		if (val & 0x10)
			pclog(1, "%s: DCR write - AR set ???\n", dev->name);

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
			pclog(1, "%s: IMR write, reserved bit set\n",dev->name);

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
		if (((val & val2) & 0x7f) == 0) {
			picintc(1 << dev->base_irq);
		} else {
			picint(1 << dev->base_irq);
		}
		break;

	default:
		pclog(1, "%s: Page0 write, bad register 0x%02x\n",
						dev->name, off);
		break;
    }
}


/* page1_read/page1_write - These routines handle reads/writes to
   the first page of the DS8390 register file */
static uint32_t
page1_read(nic_t *dev, uint32_t off, unsigned int len)
{
    pclog(2, "%s: Page1 read from register 0x%02x, len=%u\n",
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
		pclog(2, "%s: returning current page: 0x%02x\n",
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
		pclog(1, "%s: Page1 read register 0x%02x out of range\n",
							dev->name, off);
		return(0);
    }
}


static void
page1_write(nic_t *dev, uint32_t off, uint32_t val, unsigned len)
{
    pclog(2, "%s: Page1 write to register 0x%02x, len=%u, value=0x%04x\n",
						dev->name, off, len, val);

    switch(off) {
	case 0x01:	/* PAR0-5 */
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x06:
		dev->physaddr[off - 1] = val;
		if (off == 6) pclog(1,
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
		pclog(1, "%s: Page1 write register 0x%02x out of range\n",
							dev->name, off);
		break;
    }
}


/* page2_read/page2_write - These routines handle reads/writes to
   the second page of the DS8390 register file */
static uint32_t
page2_read(nic_t *dev, uint32_t off, unsigned int len)
{
    pclog(2, "%s: Page2 read from register 0x%02x, len=%u\n",
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
		pclog(1, "%s: reserved Page2 read - register 0x%02x\n",
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
		pclog(1, "%s: Page2 register 0x%02x out of range\n",
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
    pclog(2, "%s: Page2 write to register 0x%02x, len=%u, value=0x%04x\n",
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
		pclog(1, "page 2 write to reserved register 0x04\n");
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
		pclog(1, "%s: Page2 write to reserved register 0x%02x\n",
							dev->name, off);
		break;

	default:
		pclog(1, "%s: Page2 write, illegal register 0x%02x\n",
							dev->name, off);
		break;
    }
}


/* page3_read/page3_write - writes to this page are illegal */
static uint32_t
page3_read(nic_t *dev, uint32_t off, unsigned int len)
{ 
    if (dev->is_rtl8029as) switch(off) {
	case 0x3:	/* CONFIG0 */
		return(0x00);

	case 0x5:	/* CONFIG2 */
		return(0x40);

	case 0x6:	/* CONFIG3 */
		return(0x40);

	default:
		break;
    }

    pclog(1, "%s: Page3 read register 0x%02x attempted\n", dev->name, off);
    return(0x00);
}


static void
page3_write(nic_t *dev, uint32_t off, uint32_t val, unsigned len)
{
    pclog(1, "%s: Page3 write register 0x%02x attempted\n", dev->name, off);
}


/* read_cr/write_cr - utility routines for handling reads/writes to
   the Command Register */
static uint32_t
read_cr(nic_t *dev)
{
    uint32_t retval;

    retval =	(((dev->CR.pgsel    & 0x03) << 6) |
		 ((dev->CR.rdma_cmd & 0x07) << 3) |
		  (dev->CR.tx_packet << 2) |
		  (dev->CR.start     << 1) |
		  (dev->CR.stop));
    pclog(2, "%s: read CR returns 0x%02x\n", dev->name, retval);

    return(retval);
}


static void
write_cr(nic_t *dev, uint32_t val)
{
    pclog(2, "%s: wrote 0x%02x to CR\n", dev->name, val);

    /* Validate remote-DMA */
    if ((val & 0x38) == 0x00) {
	pclog(2, "%s: CR write - invalid rDMA value 0\n", dev->name);
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
    if ((val & 0x02) && !dev->CR.start) {
	dev->ISR.reset = 0;
    }

    dev->CR.start = ((val & 0x02) == 0x02);
    dev->CR.pgsel = (val & 0xc0) >> 6;

    /* Check for send-packet command */
    if (dev->CR.rdma_cmd == 3) {
	/* Set up DMA read from receive ring */
	dev->remote_start = dev->remote_dma = dev->bound_ptr * 256;
	dev->remote_bytes = (uint16_t) chipmem_read(dev, dev->bound_ptr * 256 + 2, 2);
	pclog(2, "%s: sending buffer #x%x length %d\n",
		dev->name, dev->remote_start, dev->remote_bytes);
    }

    /* Check for start-tx */
    if ((val & 0x04) && dev->TCR.loop_cntl) {
	if (dev->TCR.loop_cntl != 1) {
		pclog(1, "%s: loop mode %d not supported\n",
				dev->name, dev->TCR.loop_cntl);
	} else {
		nic_rx(dev,
			  &dev->mem[dev->tx_page_start*256 - NE2K_MEMSTART],
			  dev->tx_bytes);
	}
    } else if (val & 0x04) {
	if (dev->CR.stop || (!dev->CR.start && !dev->is_rtl8029as)) {
		if (dev->tx_bytes == 0) /* njh@bandsman.co.uk */ {
			return; /* Solaris9 probe */
		}
		pclog(1, "%s: CR write - tx start, dev in reset\n", dev->name);
	}

	if (dev->tx_bytes == 0)
		pclog(1, "%s: CR write - tx start, tx bytes == 0\n", dev->name);

	/* Send the packet to the system driver */
	dev->CR.tx_packet = 1;
	network_tx(&dev->mem[dev->tx_page_start*256 - NE2K_MEMSTART],
		   dev->tx_bytes);

	/* some more debug */
	if (dev->tx_timer_active)
		pclog(1, "%s: CR write, tx timer still active\n", dev->name);

	nic_tx(dev, val);
    }

    /* Linux probes for an interrupt by setting up a remote-DMA read
     * of 0 bytes with remote-DMA completion interrupts enabled.
     * Detect this here */
    if (dev->CR.rdma_cmd == 0x01 && dev->CR.start && dev->remote_bytes == 0) {
	dev->ISR.rdma_done = 1;
	if (dev->IMR.rdma_inte) {
		picint(1 << dev->base_irq);
		if (! dev->is_rtl8029as) {
			picintc(1 << dev->base_irq);
		}
	}
    }
}


static uint32_t
nic_read(nic_t *dev, uint32_t addr, unsigned len)
{
    uint32_t retval = 0;
    int off = addr - dev->base_address;

    pclog(2, "%s: read addr %x, len %d\n", dev->name, addr, len);

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
		pclog(1, "%s: unknown value of pgsel in read - %d\n",
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

    pclog(2, "%s: write addr %x, value %x len %d\n", dev->name, addr, val, len);

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
		pclog(1, "%s: unknown value of pgsel in write - %d\n",
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
    old_base_addr = addr;

    if (dev->is_rtl8029as) {
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
    if (dev->is_rtl8029as) {
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
    if (!dev->disable_netbios && reg_bios_enable) {
	mem_mapping_enable(&dev->bios_rom.mapping);
	mem_mapping_set_addr(&dev->bios_rom.mapping, dev->bios_addr, 0x10000);
	pclog(1, "%s: BIOS now at: %06X\n", dev->name, dev->bios_addr);
    } else {
	mem_mapping_disable(&dev->bios_rom.mapping);
	if (dev->is_rtl8029as)
		pci_bar[1].addr = 0;
    }
}


static uint8_t
nic_pci_read(int func, int addr, void *priv)
{
    nic_t *dev = (nic_t *)priv;

    switch(addr) {
	case 0x00:
		return 0xec;
	case 0x01:
		return 0x10;

	case 0x02:
		return 0x29;
	case 0x03:
		return 0x80;

	case 0x2C:
		return 0xF4;
	case 0x2D:
		return 0x1A;
	case 0x2E:
		return 0x00;
	case 0x2F:
		return 0x11;

	case 0x04:
		return pci_regs[0x04];	/*Respond to IO and memory accesses*/
	case 0x05:
		return pci_regs[0x05];

	case 0x07:
		return 2;

	case 0x08:
		return 0;		/*Revision ID*/
	case 0x09:
		return 0;		/*Programming interface*/

	case 0x0B:
		return pci_regs[0x0B];

	case 0x10:
		return 1;		/*I/O space*/
	case 0x11:
		return pci_bar[0].addr_regs[1];
	case 0x12:
		return pci_bar[0].addr_regs[2];
	case 0x13:
		return pci_bar[0].addr_regs[3];

	case 0x30:
		return pci_bar[1].addr_regs[0] & 0x01;	/*BIOS ROM address*/
	case 0x31:
		return (pci_bar[1].addr_regs[1] & dev->bios_mask) | 0x18;
	case 0x32:
		return pci_bar[1].addr_regs[2];
	case 0x33:
		return pci_bar[1].addr_regs[3];

	case 0x3C:
		return pci_regs[0x3C];
	case 0x3D:
		return pci_regs[0x3D];
    }

    return 0;
}


static void
nic_pci_write(int func, int addr, uint8_t val, void *priv)
{
    nic_t *dev = (nic_t *)priv;

    switch(addr) {
	case 0x04:
		nic_ioremove(dev, dev->base_address);
		if (val & PCI_COMMAND_IO) {
			nic_ioset(dev, dev->base_address);
		}
		pci_regs[addr] = val;
		break;

	case 0x10:
		val &= 0xfc;
		val |= 1;
	case 0x11: case 0x12: case 0x13:
		/* I/O Base set. */
		/* First, remove the old I/O, if old base was >= 0x280. */
		nic_ioremove(dev, dev->base_address);

		/* Then let's set the PCI regs. */
		pci_bar[0].addr_regs[addr & 3] = val;

		/* Then let's calculate the new I/O base. */
		dev->base_address = pci_bar[0].addr & 0xff00;

		/* Log the new base. */
		pclog(1, "%s: PCI: new I/O base is %04X\n",
				dev->name, dev->base_address);
		/* We're done, so get out of the here. */
		if (val & PCI_COMMAND_IO)
			nic_ioset(dev, dev->base_address);
		return;

	case 0x30: case 0x31: case 0x32: case 0x33:
		pci_bar[1].addr_regs[addr & 3] = val;
		pci_bar[1].addr_regs[1] &= dev->bios_mask;
		dev->bios_addr = pci_bar[1].addr & 0xffffe000;
		pci_bar[1].addr &= 0xffffe000;
		pci_bar[1].addr |= 0x1801;
		nic_update_bios(dev);
		return;

#if 0
	/* Commented out until an APIC controller is emulated for
	 * the PIIX3, otherwise the RTL-8029/AS will not get an IRQ
	 * on boards using the PIIX3. */
	case 0x3C:
		pci_regs[addr] = val;
		if (val != 0xFF) {
			pclog(1, "%s: IRQ now: %i\n", dev->name, val);
			dev->base_irq = irq;
		}
		return;
#endif
	}
}


static void
nic_tx(nic_t *dev, uint32_t val)
{
    dev->CR.tx_packet = 0;
    dev->TSR.tx_ok = 1;
    dev->ISR.pkt_tx = 1;

    /* Generate an interrupt if not masked */
    if (dev->IMR.tx_inte)
	picint(1 << dev->base_irq);
    dev->tx_timer_active = 0;
}


/*
 * mcast_index() - return the 6-bit index into the multicast
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


/*
 * rx_frame() - called by the platform-specific code when an
 * ethernet frame has been received. The destination address
 * is tested to see if it should be accepted, and if the
 * rx ring has enough room, it is copied into it and
 * the receive process is updated
 */
static void
nic_rx(void *priv, uint8_t *buf, int io_len)
{
    static uint8_t bcast_addr[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    nic_t *dev = (nic_t *)priv;
    int pages;
    int avail;
    int idx;
    int nextpage;
    uint8_t pkthdr[4];
    uint8_t *pktbuf = (uint8_t *) buf;
    uint8_t *startptr;
    uint32_t mac_cmp32[2];
    uint16_t mac_cmp16[2];

    if (io_len != 60)
	pclog(2, "%s: rx_frame with length %d\n", dev->name, io_len);

    if	((dev->CR.stop != 0) || (dev->page_start == 0)) return;

    /* Add the pkt header + CRC to the length, and work
       out how many 256-byte pages the frame would occupy */
    pages = (io_len + 4 + 4 + 255)/256;
    if (dev->curr_page < dev->bound_ptr) {
	avail = dev->bound_ptr - dev->curr_page;
    } else {
	avail = (dev->page_stop - dev->page_start) - (dev->curr_page - dev->bound_ptr);
    }

    /* Avoid getting into a buffer overflow condition by not attempting
       to do partial receives. The emulation to handle this condition
       seems particularly painful. */
    if	((avail < pages)
#if NE2K_NEVER_FULL_RING
		 || (avail == pages)
#endif
		) {
	pclog(1, "%s: no space\n", dev->name);
	return;
    }

    if ((io_len < 40/*60*/) && !dev->RCR.runts_ok) {
	pclog(1, "%s: rejected small packet, length %d\n", dev->name, io_len);
	return;
    }

    /* some computers don't care... */
    if (io_len < 60) {
	io_len=60;
    }

    /* Do address filtering if not in promiscuous mode */
    if (! dev->RCR.promisc) {
	/* Received. */
	mac_cmp32[0] = *(uint32_t *) (buf);
	mac_cmp16[0] = *(uint16_t *) (((uint8_t *) buf) + 4);

	/* Local. */
	mac_cmp32[1] = *(uint32_t *) (bcast_addr);
	mac_cmp16[1] = *(uint16_t *) (bcast_addr+4);
	if ((mac_cmp32[0] == mac_cmp32[1]) && (mac_cmp16[0] == mac_cmp16[1])) {
		if (! dev->RCR.broadcast) {
			return;
		}
	} else if (pktbuf[0] & 0x01) {
		if (! dev->RCR.multicast) {
			return;
		}
		idx = mcast_index(buf);
		if (!(dev->mchash[idx >> 3] & (1 << (idx & 0x7)))) {
			return;
		}
	} else if (0 != memcmp(buf, dev->physaddr, 6)) {
		return;
	}
    } else {
	pclog(2, "%s: rx_frame promiscuous receive\n", dev->name);
    }

    pclog(2, "%s: rx_frame %d to %x:%x:%x:%x:%x:%x from %x:%x:%x:%x:%x:%x\n",
	dev->name, io_len,
	pktbuf[0], pktbuf[1], pktbuf[2], pktbuf[3], pktbuf[4], pktbuf[5],
	pktbuf[6], pktbuf[7], pktbuf[8], pktbuf[9], pktbuf[10], pktbuf[11]);

    nextpage = dev->curr_page + pages;
    if (nextpage >= dev->page_stop) {
	nextpage -= dev->page_stop - dev->page_start;
    }

    /* Setup packet header */
    pkthdr[0] = 0;	/* rx status - old behavior
    pkthdr[0] = 1;	/* Probably better to set it all the time
			   rather than set it to 0, which is clearly wrong. */
    if (pktbuf[0] & 0x01) {
	pkthdr[0] |= 0x20;		/* rx status += multicast packet */
    }
    pkthdr[1] = nextpage;		/* ptr to next packet */
    pkthdr[2] = (io_len + 4) & 0xff;	/* length-low */
    pkthdr[3] = (io_len + 4) >> 8;	/* length-hi */

    /* copy into buffer, update curpage, and signal interrupt if config'd */
    startptr = & dev->mem[dev->curr_page * 256 - NE2K_MEMSTART];
    if ((nextpage > dev->curr_page) ||
	((dev->curr_page + pages) == dev->page_stop)) {
	*(uint32_t *) startptr = *(uint32_t *) pkthdr;
	memcpy(startptr + 4, buf, io_len);
	dev->curr_page = nextpage;
    } else {
	int endbytes = (dev->page_stop - dev->curr_page) * 256;
	*(uint32_t *) startptr = *(uint32_t *) pkthdr;
	memcpy(startptr + 4, buf, endbytes - 4);
	startptr = & dev->mem[dev->page_start * 256 - NE2K_MEMSTART];
	memcpy(startptr, (void *)(pktbuf + endbytes - 4), io_len - endbytes + 8);
	dev->curr_page = nextpage;
    }

    dev->RSR.rx_ok = 1;
    if (pktbuf[0] & 0x80) {
	dev->RSR.rx_mbit = 1;
    }

    dev->ISR.pkt_rx = 1;

    if (dev->IMR.rx_inte)
	picint(1 << dev->base_irq);
}


static void
nic_rom_init(nic_t *dev, wchar_t *s)
{
    FILE *f = romfopen(s, L"rb");
    uint32_t temp;

    if (f != NULL) {
	dev->disable_netbios = 1;
	nic_update_bios(dev);
	return;
    }
    fseek(f, 0, SEEK_END);
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

#if 1
    /* Shouldn't we use the configured address?? --FvK */
    rom_init(&dev->bios_rom, s, 0xd0000,
	     dev->bios_size, dev->bios_size - 1, 0, MEM_MAPPING_EXTERNAL);
#else
    rom_init(&dev->bios_rom, s, dev->bios_addr,
	     dev->bios_size, dev->bios_size - 1, 0, MEM_MAPPING_EXTERNAL);
#endif
}


static void *
nic_init(int board)
{
    uint32_t mac;
    uint8_t *ptr;
    nic_t *dev;

    dev = malloc(sizeof(nic_t));
    memset(dev, 0x00, sizeof(nic_t));
    dev->is_rtl8029as = (PCI && (board == NE2K_RTL8029AS)) ? 1 : 0;
    if (board == NE2K_RTL8029AS)
	strcpy(dev->name, "RTL8029AS");
      else if (board == NE2K_NE1000)
	strcpy(dev->name, "NE1000");
      else
	strcpy(dev->name, "NE2000");

    if (dev->is_rtl8029as) {
	dev->base_address = 0x340;
	mac = config_get_int(NULL, "maclocal_pci", -1);
    } else {
	dev->base_address = device_get_config_int("addr");
	mac = config_get_int(NULL, "maclocal", -1);
    }
    dev->base_irq = device_get_config_int("irq");
    dev->disable_netbios = device_get_config_int("disable_netbios");

    /* Set up our MAC address. */
    if (dev->is_rtl8029as) {
	maclocal_pci[0] = 0x00;	/* 00:20:18 (RTL 8029AS PCI vendor prefix). */
	maclocal_pci[1] = 0x20;
	maclocal_pci[2] = 0x18;
	ptr = maclocal_pci;
    } else {
	maclocal[0] = 0x00;	/* 00:00:D8 (NE2000 ISA vendor prefix). */
	maclocal[1] = 0x00;
	maclocal[2] = 0xD8;
	ptr = maclocal;
    }
pclog(1, "MAClocal: mac=%08lx\n", mac);
    if (mac & 0xff000000) {
	/* Generating new MAC. */
	ptr[3] = disc_random_generate();
	ptr[4] = disc_random_generate();
	ptr[5] = disc_random_generate();
    } else {
	ptr[3] = (mac>>16) & 0xff;
	ptr[4] = (mac>>8) & 0xff;
	ptr[5] = mac & 0xff;
    }
    memcpy(dev->physaddr, ptr, 6);

    pclog(1,"%s: I/O=%04x, IRQ=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x BIOS=%d\n",
	dev->name, dev->base_address, dev->base_irq,
	dev->physaddr[0], dev->physaddr[1], dev->physaddr[2],
	dev->physaddr[3], dev->physaddr[4], dev->physaddr[5],
	!dev->disable_netbios);

    if (network_attach(dev, dev->physaddr, nic_rx) < 0) {
	pclog(1, "%s: unable to init platform network type %d\n",
					dev->name, network_type);
	free(dev);
	return(NULL);
    }

    if (dev->is_rtl8029as)
	pci_add(nic_pci_read, nic_pci_write, dev);
    nic_ioset(dev, dev->base_address);

    if (! dev->disable_netbios) {
	nic_rom_init(dev, dev->is_rtl8029as ? L"roms/rtl8029as.rom"
					       : L"roms/ne2000.rom");
	if (dev->is_rtl8029as)
		mem_mapping_disable(&dev->bios_rom.mapping);
    }
	
    if (dev->is_rtl8029as) {
        pci_regs[0x04] = 1;
        pci_regs[0x05] = 0;
        pci_regs[0x07] = 2;

        /* Network controller. */
        pci_regs[0x0B] = 2;
		
	pci_bar[0].addr_regs[0] = 1;
		
	if (! dev->disable_netbios) {
		pci_bar[1].addr = 0;
		dev->bios_addr = 0;
	} else {
		pci_bar[1].addr = 0x000F8000;
		pci_bar[1].addr_regs[1] = dev->bios_mask;
		pci_bar[1].addr |= 0x1801;
		dev->bios_addr = 0xD0000;
	}

	pci_regs[0x3C] = dev->base_irq;
	pclog(1, "%s: IRQ=%i\n", dev->name, pci_regs[0x3C]);
        pci_regs[0x3D] = 1;

        memset(rtl8029as_eeprom, 0, 128);
        rtl8029as_eeprom[0x76] =
	 rtl8029as_eeprom[0x7A] =
	 rtl8029as_eeprom[0x7E] = 0x29;
        rtl8029as_eeprom[0x77] =
	 rtl8029as_eeprom[0x7B] =
	 rtl8029as_eeprom[0x7F] = 0x80;
        rtl8029as_eeprom[0x78] =
	 rtl8029as_eeprom[0x7C] = 0x10;
        rtl8029as_eeprom[0x79] =
	 rtl8029as_eeprom[0x7D] = 0xEC;
    }

    nic_reset(dev, BX_RESET_HARDWARE);

    pclog(1, "%s: %s init 0x%X %d\n", dev->name,
	dev->is_rtl8029as?"PCI":"ISA", dev->base_address, dev->base_irq);

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

    pclog(1, "%s: closed\n", dev->name);
}


uint32_t
ne2000_get_maclocal(void)
{
    uint32_t temp;

    temp = (((int) maclocal[3]) << 16);
    temp |= (((int) maclocal[4]) << 8);
    temp |= ((int) maclocal[5]);

    return(temp);
}


uint32_t
ne2000_get_maclocal_pci(void)
{
    uint32_t temp;

    temp = (((int) maclocal_pci[3]) << 16);
    temp |= (((int) maclocal_pci[4]) << 8);
    temp |= ((int) maclocal_pci[5]);

    return(temp);
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
		"addr", "Address", CONFIG_SELECTION, "", 0x300,
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
		"net_type", "Network type", CONFIG_SELECTION, "", 0,
		{
			{
				"PCap", 0
			},
			{
				"SLiRP", 1
			},
			{
				""
			}
		},
	},
	{
		"disable_netbios", "Disable network BIOS", CONFIG_BINARY, "", 0
	},
	{
		"", "", -1
	}
};

static device_config_t ne2000_config[] =
{
	{
		"addr", "Address", CONFIG_SELECTION, "", 0x300,
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
		"net_type", "Network type", CONFIG_SELECTION, "", 0,
		{
			{
				"PCap", 0
			},
			{
				"SLiRP", 1
			},
			{
				""
			}
		},
	},
	{
		"disable_netbios", "Disable network BIOS", CONFIG_BINARY, "", 0
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
	{
		"net_type", "Network type", CONFIG_SELECTION, "", 0,
		{
			{
				"PCap", 0
			},
			{
				"SLiRP", 1
			},
			{
				""
			}
		},
	},
	{
		"disable_netbios", "Disable network BIOS", CONFIG_BINARY, "", 0
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

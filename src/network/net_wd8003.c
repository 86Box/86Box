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
 * Version:	@(#)net_wd8003.c	1.0.1	2018/10/02
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
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../mca.h"
#include "../pci.h"
#include "../pic.h"
#include "../random.h"
#include "../device.h"
#include "../ui.h"
#include "network.h"
#include "net_dp8390.h"
#include "net_wd8003.h"
#include "bswap.h"

/* Board type codes in card ID */
#define WE_TYPE_WD8003		0x01
#define WE_TYPE_WD8003S		0x02
#define WE_TYPE_WD8003E		0x03
#define WE_TYPE_WD8013EBT	0x05
#define	WE_TYPE_TOSHIBA1	0x11	/* named PCETA1 */
#define	WE_TYPE_TOSHIBA2	0x12	/* named PCETA2 */
#define	WE_TYPE_TOSHIBA3	0x13	/* named PCETB */
#define	WE_TYPE_TOSHIBA4	0x14	/* named PCETC */
#define	WE_TYPE_WD8003W		0x24
#define	WE_TYPE_WD8003EB	0x25
#define	WE_TYPE_WD8013W		0x26
#define WE_TYPE_WD8013EP	0x27
#define WE_TYPE_WD8013WC	0x28
#define WE_TYPE_WD8013EPC	0x29
#define	WE_TYPE_SMC8216T	0x2a
#define	WE_TYPE_SMC8216C	0x2b
#define WE_TYPE_WD8013EBP	0x2c

typedef struct {
    dp8390_t dp8390;
	mem_mapping_t	ram_mapping;
	uint32_t		ram_addr, ram_size;
	uint8_t	macaddr[32];		/* ASIC ROM'd MAC address, even bytes */
    uint8_t	maclocal[6];		/* configured MAC (local) address */
    int		board;
    const char	*name;
    uint32_t	base_address;
    int		base_irq;
	
	/* POS registers, MCA boards only */
	uint8_t pos_regs[8];
	
	/* Memory for WD cards*/
	uint8_t reg1;
	uint8_t reg5;
	uint8_t if_chip;
	uint8_t board_chip;
} wd_t;

static void	wd_rx(void *, uint8_t *, int);
static void	wd_tx(wd_t *, uint32_t);

#ifdef ENABLE_WD_LOG
int wd_do_log = ENABLE_WD_LOG;
#endif

static void
wdlog(int lvl, const char *fmt, ...)
{
#ifdef ENABLE_WD_LOG
    va_list ap;

    if (wd_do_log >= lvl) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}

static void
wd_interrupt(wd_t *dev, int set)
{
	if (set)
		picint(1<<dev->base_irq);
	else
		picintc(1<<dev->base_irq);
}

/* reset - restore state to power-up, cancelling all i/o */
static void
wd_reset(void *priv)
{
    wd_t *dev = (wd_t *)priv;

    wdlog(1, "%s: reset\n", dev->name);

	/* Initialize the MAC address area by doubling the physical address */
	dev->macaddr[0]  = dev->dp8390.physaddr[0];
	dev->macaddr[1]  = dev->dp8390.physaddr[1];
	dev->macaddr[2]  = dev->dp8390.physaddr[2];
	dev->macaddr[3]  = dev->dp8390.physaddr[3];
	dev->macaddr[4]  = dev->dp8390.physaddr[4];
	dev->macaddr[5]  = dev->dp8390.physaddr[5];
	
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
	
    wd_interrupt(dev, 0);
}

static uint32_t
wd_chipmem_read(wd_t *dev, uint32_t addr, unsigned int len)
{
    uint32_t retval = 0;

    if ((len == 2) && (addr & 0x1)) {
	wdlog(3, "%s: unaligned chipmem word read\n", dev->name);
    }

    /* ROM'd MAC address */
	if (dev->board == WD8003E)
	{
		if (addr <= 15) {
		retval = dev->macaddr[addr % 16];
		if (len == 2) {
			retval |= (dev->macaddr[(addr + 1) % 16] << 8);
		}
		return(retval);
		}
	}

	return(0xff);
}

static uint32_t
wd_ram_read(uint32_t addr, unsigned len, void *priv)
{
	wd_t *dev = (wd_t *)priv;
	uint32_t ret;
	
    if ((addr & 0x3fff) >= 0x2000)
	{
		if (len == 2)
			return 0xffff;
		else if (len == 1)
			return 0xff;
		else
			return 0xffffffff;
	}
	
	ret = dev->dp8390.mem[addr & 0x1fff];
	if (len == 2 || len == 4)
		ret |= dev->dp8390.mem[(addr+1) & 0x1fff] << 8;
	if (len == 4) {
		ret |= dev->dp8390.mem[(addr+2) & 0x1fff] << 16;
		ret |= dev->dp8390.mem[(addr+3) & 0x1fff] << 24;
	}
	
	return ret;	
}

static uint8_t
wd_ram_readb(uint32_t addr, void *priv)
{
	wd_t *dev = (wd_t *)priv;
	
	return wd_ram_read(addr, 1, dev);
}

static uint16_t
wd_ram_readw(uint32_t addr, void *priv)
{
	wd_t *dev = (wd_t *)priv;
	
	return wd_ram_read(addr, 2, dev);
}

static uint32_t
wd_ram_readl(uint32_t addr, void *priv)
{
	wd_t *dev = (wd_t *)priv;
	
	return wd_ram_read(addr, 4, dev);
}

static void
wd_ram_write(uint32_t addr, uint32_t val, unsigned len, void *priv)
{
	wd_t *dev = (wd_t *)priv;

	if ((addr & 0x3fff) >= 0x2000)
		return;

	dev->dp8390.mem[addr & 0x1fff] = val & 0xff;
	if (len == 2 || len == 4)
		dev->dp8390.mem[(addr+1) & 0x1fff] = val >> 8;
	if (len == 4) {
		dev->dp8390.mem[(addr+2) & 0x1fff] = val >> 16;
		dev->dp8390.mem[(addr+3) & 0x1fff] = val >> 24;
	}
}

static void
wd_ram_writeb(uint32_t addr, uint8_t val, void *priv)
{
	wd_t *dev = (wd_t *)priv;

	wd_ram_write(addr, val, 1, dev);
}

static void
wd_ram_writew(uint32_t addr, uint16_t val, void *priv)
{
	wd_t *dev = (wd_t *)priv;

	wd_ram_write(addr, val, 2, dev);
}

static void
wd_ram_writel(uint32_t addr, uint32_t val, void *priv)
{
	wd_t *dev = (wd_t *)priv;

	wd_ram_write(addr, val, 4, dev);
}

static uint32_t
wd_smc_read(wd_t *dev, uint32_t off)
{
    uint32_t retval = 0;
	uint32_t checksum = 0;
	
    switch(off) {
	case 0x00:
		break;

	case 0x01:
		if (dev->board == WD8003E)
			retval = dev->dp8390.physaddr[0];
		if (dev->board == WD8013EPA)
			retval = dev->reg1;
		break;
	
	case 0x02:
		if (dev->board == WD8003E)
			retval = dev->dp8390.physaddr[1];
		break;

	case 0x03:
		if (dev->board == WD8003E)	
			retval = dev->dp8390.physaddr[2];
		break;

	case 0x04:
		if (dev->board == WD8003E)	
			retval = dev->dp8390.physaddr[3];
		break;
	
	case 0x05:
		if (dev->board == WD8003E)	
			retval = dev->dp8390.physaddr[4];
		if (dev->board == WD8013EPA)
			retval = dev->reg5;
		break;
		
	case 0x06:
		if (dev->board == WD8003E)	
			retval = dev->dp8390.physaddr[5];
		break;
	
	case 0x07:
		if (dev->board == WD8013EPA)
		{
			if (dev->if_chip != 0x35 && dev->if_chip != 0x3A)
			{
				retval = 0;
				break;
			}
			
			retval = dev->if_chip;
		}
		break;
		
	case 0x08:
		retval = dev->dp8390.physaddr[0];
		break;	
	
	case 0x09:
		retval = dev->dp8390.physaddr[1];
		break;	
	
	case 0x0a:
		retval = dev->dp8390.physaddr[2];
		break;	
	
	case 0x0b:
		retval = dev->dp8390.physaddr[3];
		break;	
	
	case 0x0c:
		retval = dev->dp8390.physaddr[4];
		break;
		
	case 0x0d:
		retval = dev->dp8390.physaddr[5];
		break;
	
	case 0x0e:
		retval = dev->board_chip;
		break;
		
	case 0x0f:
	{
		/*This has to return the byte that adds up to 0xFF*/
		checksum = (dev->dp8390.physaddr[0] + dev->dp8390.physaddr[1] + dev->dp8390.physaddr[2] +
				dev->dp8390.physaddr[3] + dev->dp8390.physaddr[4] + dev->dp8390.physaddr[5] +
				dev->board_chip);

		retval = 0xff - (checksum & 0xff);
	}
	break;
    }

    wdlog(2, "%s: ASIC read addr=0x%02x, value=0x%04x\n",
		dev->name, (unsigned)off, (unsigned) retval);
	
    return(retval);
}

static void
wd_smc_write(wd_t *dev, uint32_t off, uint32_t val)
{
    wdlog(2, "%s: ASIC write addr=0x%02x, value=0x%04x\n",
		dev->name, (unsigned)off, (unsigned) val);

    switch(off) {
	case 0x00:	/* WD Control register */
		if (val & 0x80)
		{
			dev->dp8390.ISR.reset = 1;
			return;
		}
	
		mem_mapping_disable(&dev->ram_mapping);
	
		if (val & 0x40)
		{
			mem_mapping_enable(&dev->ram_mapping);
		}
		break;
		
	case 0x01:
		dev->reg1 = val;
		break;
		
	case 0x04:
		break;
		
	case 0x05:
		dev->reg5 = val;
		break;
		
	case 0x06:
		break;
		
	case 0x07:
		dev->if_chip = val;
		break;		
		
	default: /* this is invalid, but happens under win95 device detection */
		wdlog(3, "%s: ASIC write invalid address %04x, ignoring\n",
						dev->name, (unsigned)off);
		break;
    }
}


/* Handle reads/writes to the 'zeroth' page of the DS8390 register file. */
static uint8_t
page0_read(wd_t *dev, uint32_t off)
{
    uint8_t retval = 0;

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
		wdlog(3, "%s: reading FIFO not supported yet\n", dev->name);
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
		wdlog(3, "%s: reserved Page0 read - 0x0a\n", dev->name);
		retval = 0xff;
		break;

	case 0x0b:	/* reserved / RTL8029ID1 */
		wdlog(3, "%s: reserved Page0 read - 0x0b\n", dev->name);
		retval = 0xff;
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
		wdlog(3, "%s: Page0 register 0x%02x out of range\n",
							dev->name, off);
		break;
    }

    wdlog(3, "%s: Page0 read from register 0x%02x, value=0x%02x\n",
						dev->name, off, retval);

    return(retval);
}


static void
page0_write(wd_t *dev, uint32_t off, uint8_t val)
{
    uint8_t val2;

    wdlog(3, "%s: Page0 write to register 0x%02x, value=0x%02x\n",
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
			wd_interrupt(dev, 0);
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
			wdlog(3, "%s: RCR write, reserved bits set\n",
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
		if (val & 0x20) wdlog(3, "%s: RCR write, monitor bit set!\n",
								dev->name);
		break;

	case 0x0d:	/* TCR */
		/* Check reserved bits */
		if (val & 0xe0) wdlog(3, "%s: TCR write, reserved bits set\n",
								dev->name);

		/* Test loop mode (not supported) */
		if (val & 0x06) {
			dev->dp8390.TCR.loop_cntl = (val & 0x6) >> 1;
			wdlog(3, "%s: TCR write, loop mode %d not supported\n",
						dev->name, dev->dp8390.TCR.loop_cntl);
		}
		else {
			dev->dp8390.TCR.loop_cntl = 0;
		}

		/* Inhibit-CRC not supported. */
		if (val & 0x01) wdlog(3,
			"%s: TCR write, inhibit-CRC not supported\n",dev->name);

		/* Auto-transmit disable very suspicious */
		if (val & 0x08) 
		{
			wdlog(3,
			"%s: TCR write, auto transmit disable not supported\n",
							dev->name);
		}
		
		/* Allow collision-offset to be set, although not used */
		dev->dp8390.TCR.coll_prio = ((val & 0x08) == 0x08);
		break;

	case 0x0e:	/* DCR */
		/* the loopback mode is not suppported yet */
		if (! (val & 0x08)) wdlog(3,
			"%s: DCR write, loopback mode selected\n", dev->name);

		/* It is questionable to set longaddr and auto_rx, since
		 * they are not supported on the NE2000. Print a warning
		 * and continue. */
		if (val & 0x04)
			wdlog(3, "%s: DCR write - LAS set ???\n", dev->name);
		if (val & 0x10)
			wdlog(3, "%s: DCR write - AR set ???\n", dev->name);

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
			wdlog(3, "%s: IMR write, reserved bit set\n",dev->name);

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
			wd_interrupt(dev, 0);
		  else
			wd_interrupt(dev, 1);
		break;

	default:
		wdlog(3, "%s: Page0 write, bad register 0x%02x\n",
						dev->name, off);
		break;
    }
}

/* Handle reads/writes to the first page of the DS8390 register file. */
static uint8_t
page1_read(wd_t *dev, uint32_t off)
{
    wdlog(3, "%s: Page1 read from register 0x%02x\n",
					dev->name, off);

    switch(off) {
	case 0x01:	/* PAR0-5 */
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x06:
		return(dev->dp8390.physaddr[off - 1]);

	case 0x07:	/* CURR */
		wdlog(3, "%s: returning current page: 0x%02x\n",
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
		wdlog(3, "%s: Page1 read register 0x%02x out of range\n",
							dev->name, off);
		return(0);
    }
}


static void
page1_write(wd_t *dev, uint32_t off, uint8_t val)
{
    wdlog(3, "%s: Page1 write to register 0x%02x, value=0x%04x\n",
						dev->name, off, val);

    switch(off) {
	case 0x01:	/* PAR0-5 */
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x06:
		dev->dp8390.physaddr[off - 1] = val;
		if (off == 6) wdlog(3,
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
		wdlog(3, "%s: Page1 write register 0x%02x out of range\n",
							dev->name, off);
		break;
    }
}

/* Handle reads/writes to the second page of the DS8390 register file. */
static uint8_t
page2_read(wd_t *dev, uint32_t off)
{
    wdlog(3, "%s: Page2 read from register 0x%02x\n",
					dev->name, off);
  
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
		wdlog(3, "%s: reserved Page2 read - register 0x%02x\n",
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
		wdlog(3, "%s: Page2 register 0x%02x out of range\n",
						dev->name, off);
		break;
    }

    return(0);
}

#if 0
static void
page2_write(wd_t *dev, uint32_t off, uint8_t val)
{
/* Maybe all writes here should be BX_PANIC()'d, since they
   affect internal operation, but let them through for now
   and print a warning. */
    wdlog(3, "%s: Page2 write to register 0x%02x, value=0x%04x\n",
						dev->name, off, val);
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
		wdlog(3, "page 2 write to reserved register 0x04\n");
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
		wdlog(3, "%s: Page2 write to reserved register 0x%02x\n",
							dev->name, off);
		break;

	default:
		wdlog(3, "%s: Page2 write, illegal register 0x%02x\n",
							dev->name, off);
		break;
    }
}
#endif

/* Routines for handling reads/writes to the Command Register. */
static uint8_t
read_cr(wd_t *dev)
{
    uint32_t retval;

    retval =	(((dev->dp8390.CR.pgsel    & 0x03) << 6) |
		 ((dev->dp8390.CR.rdma_cmd & 0x07) << 3) |
		  (dev->dp8390.CR.tx_packet << 2) |
		  (dev->dp8390.CR.start     << 1) |
		  (dev->dp8390.CR.stop));
    wdlog(3, "%s: read CR returns 0x%02x\n", dev->name, retval);

    return(retval);
}

static void
write_cr(wd_t *dev, uint8_t val)
{
    wdlog(3, "%s: wrote 0x%02x to CR\n", dev->name, val);

    /* Validate remote-DMA */
    if ((val & 0x38) == 0x00) {
	wdlog(3, "%s: CR write - invalid rDMA value 0\n", dev->name);
	val |= 0x20; /* dma_cmd == 4 is a safe default */
    }

    /* Check for s/w reset */
    if (val & 0x01) {
	dev->dp8390.ISR.reset = 1;
	dev->dp8390.CR.stop   = 1;
    } else {
	dev->dp8390.CR.stop   = 0;
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
	dev->dp8390.remote_bytes = (uint16_t) wd_chipmem_read(dev, dev->dp8390.bound_ptr * 256 + 2, 2);
	wdlog(2, "%s: sending buffer #x%x length %d\n",
		dev->name, dev->dp8390.remote_start, dev->dp8390.remote_bytes);
    }

    /* Check for start-tx */
	if ((val & 0x04) && dev->dp8390.TCR.loop_cntl) {
	if (dev->dp8390.TCR.loop_cntl) {
		wd_rx(dev, &dev->dp8390.mem[dev->dp8390.tx_page_start*256 - DP8390_WORD_MEMSTART],
				  dev->dp8390.tx_bytes);
	}
    } else if (val & 0x04) {
	if (dev->dp8390.CR.stop) {
		if (dev->dp8390.tx_bytes == 0) /* njh@bandsman.co.uk */ {
			return; /* Solaris9 probe */
		}
		wdlog(3, "%s: CR write - tx start, dev in reset\n", dev->name);
	}

	if (dev->dp8390.tx_bytes == 0)
		wdlog(3, "%s: CR write - tx start, tx bytes == 0\n", dev->name);

	/* Send the packet to the system driver */
	dev->dp8390.CR.tx_packet = 1;

	network_tx(dev->dp8390.mem, dev->dp8390.tx_bytes);
	
    wd_tx(dev, val);
    }

    /* Linux probes for an interrupt by setting up a remote-DMA read
     * of 0 bytes with remote-DMA completion interrupts enabled.
     * Detect this here */
    if (dev->dp8390.CR.rdma_cmd == 0x01 && dev->dp8390.CR.start && dev->dp8390.remote_bytes == 0) {
	dev->dp8390.ISR.rdma_done = 1;
	if (dev->dp8390.IMR.rdma_inte) {
		wd_interrupt(dev, 1);
		wd_interrupt(dev, 0);
	}
    }
}

static uint8_t
wd_readb(uint16_t addr, void *priv)
{
	wd_t *dev = (wd_t *)priv;
	
    uint8_t retval = 0;
    int off = addr - dev->base_address;

    wdlog(3, "%s: read addr %x\n", dev->name, addr);

	if (off == 0x10)
	{
		retval = read_cr(dev);
	}
	else if (off >= 0x00 && off <= 0x0f)
	{
		retval = wd_smc_read(dev, off);	
	}
	else
	{
		switch(dev->dp8390.CR.pgsel) {
		case 0x00:
			retval = page0_read(dev, off - 0x10);
			break;

		case 0x01:
			retval = page1_read(dev, off - 0x10);
			break;

		case 0x02:
			retval = page2_read(dev, off - 0x10);
			break;
			
		default:
			wdlog(3, "%s: unknown value of pgsel in read - %d\n",
							dev->name, dev->dp8390.CR.pgsel);
			break;
		}		
	}
	
    return(retval);
}


static void
wd_writeb(uint16_t addr, uint8_t val, void *priv)
{
	wd_t *dev = (wd_t *)priv;
    int off = addr - dev->base_address;

    wdlog(3, "%s: write addr %x, value %x\n", dev->name, addr, val);

	if (off == 0x10)
	{
		write_cr(dev, val);
	}
	else if (off >= 0x00 && off <= 0x0f)
	{
		wd_smc_write(dev, off, val);	
	}
	else
	{
		switch(dev->dp8390.CR.pgsel) {
		case 0x00:
			page0_write(dev, off - 0x10, val);
			break;

		case 0x01:
			page1_write(dev, off - 0x10, val);
			break;
			
		default:
			wdlog(3, "%s: unknown value of pgsel in write - %d\n",
							dev->name, dev->dp8390.CR.pgsel);
			break;
		}			
	}
}

static void	wd_ioset(wd_t *dev, uint16_t addr);
static void	wd_ioremove(wd_t *dev, uint16_t addr);

static void
wd_ioset(wd_t *dev, uint16_t addr)
{	
	io_sethandler(addr, 0x20,
			 wd_readb, NULL, NULL,
			 wd_writeb, NULL, NULL, dev);
}

static void
wd_ioremove(wd_t *dev, uint16_t addr)
{	
	io_removehandler(addr, 0x20,
			 wd_readb, NULL, NULL,
			 wd_writeb, NULL, NULL, dev);
}

/*
 * Called by the platform-specific code when an Ethernet frame
 * has been received. The destination address is tested to see
 * if it should be accepted, and if the RX ring has enough room,
 * it is copied into it and the receive process is updated.
 */
static void
wd_rx(void *priv, uint8_t *buf, int io_len)
{
    static uint8_t bcast_addr[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    wd_t *dev = (wd_t *)priv;
    uint8_t pkthdr[4];
    uint8_t *startptr;
    int pages, avail;
    int idx, nextpage;
    int endbytes;

    //FIXME: move to upper layer
    ui_sb_update_icon(SB_NETWORK, 1);

    if (io_len != 60)
	wdlog(2, "%s: rx_frame with length %d\n", dev->name, io_len);	

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
	wdlog(1, "%s: no space\n", dev->name);

    //FIXME: move to upper layer
	ui_sb_update_icon(SB_NETWORK, 0);
	return;
    }

    if ((io_len < 40/*60*/) && !dev->dp8390.RCR.runts_ok) {
	wdlog(1, "%s: rejected small packet, length %d\n", dev->name, io_len);

    //FIXME: move to upper layer
	ui_sb_update_icon(SB_NETWORK, 0);
	return;
    }

    /* Some computers don't care... */
    if (io_len < 60)
		io_len = 60;

	wdlog(2, "%s: RX %x:%x:%x:%x:%x:%x > %x:%x:%x:%x:%x:%x len %d\n",
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
			wdlog(2, "%s: RX BC disabled\n", dev->name);

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
			wdlog(2, "%s: RX MC disabled\n", dev->name);
#endif

    //FIXME: move to upper layer
			ui_sb_update_icon(SB_NETWORK, 0);
			return;
		}

		/* Are we listening to this multicast address? */
		idx = mcast_index(buf);
		if (! (dev->dp8390.mchash[idx>>3] & (1<<(idx&0x7)))) {
			wdlog(2, "%s: RX MC not listed\n", dev->name);

    //FIXME: move to upper layer
			ui_sb_update_icon(SB_NETWORK, 0);
			return;
		}
	}

	/* Unicast, must be for us.. */
	else if (memcmp(buf, dev->dp8390.physaddr, 6)) return;
    } else {
	wdlog(2, "%s: RX promiscuous receive\n", dev->name);
    }

    nextpage = dev->dp8390.curr_page + pages;
    if (nextpage >= dev->dp8390.page_stop)
	nextpage -= (dev->dp8390.page_stop - dev->dp8390.page_start);

    /* Set up packet header. */
    pkthdr[0] = 0x01;			/* RXOK - packet is OK */
    pkthdr[1] = nextpage;		/* ptr to next packet */
    pkthdr[2] = (io_len + sizeof(pkthdr))&0xff;	/* length-low */
    pkthdr[3] = (io_len + sizeof(pkthdr))>>8;	/* length-hi */
    wdlog(2, "%s: RX pkthdr [%02x %02x %02x %02x]\n",
	dev->name, pkthdr[0], pkthdr[1], pkthdr[2], pkthdr[3]);

    /* Copy into buffer, update curpage, and signal interrupt if config'd */
	startptr = dev->dp8390.mem + (dev->dp8390.curr_page * 256);
	memcpy(startptr, pkthdr, sizeof(pkthdr));
    if ((nextpage > dev->dp8390.curr_page) ||
	((dev->dp8390.curr_page + pages) == dev->dp8390.page_stop)) {
	memcpy(startptr+sizeof(pkthdr), buf, io_len);
    } else {
	endbytes = (dev->dp8390.page_stop - dev->dp8390.curr_page) * 256;
	memcpy(startptr+sizeof(pkthdr), buf, endbytes-sizeof(pkthdr));
	startptr = dev->dp8390.mem + (dev->dp8390.page_start * 256);	
	memcpy(startptr, buf+endbytes-sizeof(pkthdr), io_len-endbytes+8);
    }
    dev->dp8390.curr_page = nextpage;

    dev->dp8390.RSR.rx_ok = 1;
    dev->dp8390.RSR.rx_mbit = (buf[0] & 0x01) ? 1 : 0;
    dev->dp8390.ISR.pkt_rx = 1;

    if (dev->dp8390.IMR.rx_inte)
	wd_interrupt(dev, 1);

    //FIXME: move to upper layer
    ui_sb_update_icon(SB_NETWORK, 0);
}

static void
wd_tx(wd_t *dev, uint32_t val)
{
    dev->dp8390.CR.tx_packet = 0;
    dev->dp8390.TSR.tx_ok = 1;
    dev->dp8390.ISR.pkt_tx = 1;

    /* Generate an interrupt if not masked */
    if (dev->dp8390.IMR.tx_inte)
		wd_interrupt(dev, 1);
    dev->dp8390.tx_timer_active = 0;
}

static uint8_t
wd_mca_read(int port, void *priv)
{
    wd_t *dev = (wd_t *)priv;

    return(dev->pos_regs[port & 7]);
}

#define MCA_61C8_IRQS { 3, 4, 10, 15 }

static void
wd_mca_write(int port, uint8_t val, void *priv)
{
    wd_t *dev = (wd_t *)priv;
	int8_t irq[4] = MCA_61C8_IRQS;
	uint32_t ram_size = 0;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102) return;

    /* Save the MCA register value. */
    dev->pos_regs[port & 7] = val;

	wd_ioremove(dev, dev->base_address);	
	
	dev->base_address = 0x800 + (((dev->pos_regs[2] & 0xf0) >> 4) * 0x1000);
	
	dev->ram_addr = 0xC0000 + ((dev->pos_regs[3] & 0x0f) * 0x2000) + ((dev->pos_regs[3] & 0x80) ? 0xF00000 : 0);
	
	dev->base_irq = irq[(dev->pos_regs[5] & 0x0c) >> 2];

	ram_size = (dev->pos_regs[3] & 0x10) ? 0x4000 : 0x2000;
	
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
	
	wd_ioset(dev, dev->base_address);
	
	wd_reset(dev);
	
	mem_mapping_add(&dev->ram_mapping, dev->ram_addr, ram_size,
			wd_ram_readb, wd_ram_readw, wd_ram_readl,
			wd_ram_writeb, wd_ram_writew, wd_ram_writel,
			NULL, MEM_MAPPING_EXTERNAL, dev);	
	
	mem_mapping_disable(&dev->ram_mapping);	
	
	wdlog(1, "%s: attached IO=0x%X IRQ=%d, RAM addr=0x%06x\n", dev->name,
	dev->base_address, dev->base_irq, dev->ram_addr);
    }
}

static void *
wd_init(const device_t *info)
{
    uint32_t mac;
    wd_t *dev;
#ifdef ENABLE_NIC_LOG
    int i;
#endif

    /* Get the desired debug level. */
#ifdef ENABLE_NIC_LOG
    i = device_get_config_int("debug");
    if (i > 0) wd_do_log = i;
#endif

    dev = malloc(sizeof(wd_t));
    memset(dev, 0x00, sizeof(wd_t));
    dev->name = info->name;
    dev->board = info->local;
	
    switch(dev->board) {
	case WD8003E:
		dev->board_chip = WE_TYPE_WD8003E;
		dev->maclocal[0] = 0x00;  /* 00:00:C0 (WD/SMC OID) */
		dev->maclocal[1] = 0x00;
		dev->maclocal[2] = 0xC0;
		break;
	
	case WD8013EBT:
		dev->board_chip = WE_TYPE_WD8013EBT;
		dev->maclocal[0] = 0x00;  /* 00:00:C0 (WD/SMC OID) */
		dev->maclocal[1] = 0x00;
		dev->maclocal[2] = 0xC0;
		break;

	case WD8013EPA:
		dev->board_chip = WE_TYPE_WD8013EP | 0x80;
		dev->maclocal[0] = 0x00;  /* 00:00:C0 (WD/SMC OID) */
		dev->maclocal[1] = 0x00;
		dev->maclocal[2] = 0xC0;
		dev->pos_regs[0] = 0xC8;
		dev->pos_regs[1] = 0x61;
		break;
    }

	if (dev->board != WD8013EPA) {
		dev->base_address = device_get_config_hex16("base");
		dev->base_irq = device_get_config_int("irq");
		dev->ram_addr = device_get_config_hex20("ram_addr");
	}
	else {
		mca_add(wd_mca_read, wd_mca_write, dev);	
	}

    /* See if we have a local MAC address configured. */
    mac = device_get_config_mac("mac", -1);

    /*
     * Make this device known to the I/O system.
     * PnP and PCI devices start with address spaces inactive.
     */
    if (dev->board != WD8013EPA)
		wd_ioset(dev, dev->base_address);

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

    wdlog(0, "%s: I/O=%04x, IRQ=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
	dev->name, dev->base_address, dev->base_irq,
	dev->dp8390.physaddr[0], dev->dp8390.physaddr[1], dev->dp8390.physaddr[2],
	dev->dp8390.physaddr[3], dev->dp8390.physaddr[4], dev->dp8390.physaddr[5]);

    /* Reset the board. */
	if (dev->board != WD8013EPA)	
		wd_reset(dev);

    /* Attach ourselves to the network module. */
    network_attach(dev, dev->dp8390.physaddr, wd_rx);
	
	/* Map this system into the memory map. */	
	if (dev->board != WD8013EPA)
	{
		mem_mapping_add(&dev->ram_mapping, dev->ram_addr, 0x4000,
				wd_ram_readb, NULL, NULL,
				wd_ram_writeb, NULL, NULL,
				NULL, MEM_MAPPING_EXTERNAL, dev);
		mem_mapping_disable(&dev->ram_mapping);		
		
		wdlog(1, "%s: attached IO=0x%X IRQ=%d, RAM addr=0x%06x\n", dev->name,
		dev->base_address, dev->base_irq, dev->ram_addr);		
	}

    return(dev);
}


static void
wd_close(void *priv)
{
    wd_t *dev = (wd_t *)priv;

    /* Make sure the platform layer is shut down. */
    network_close();

    wd_ioremove(dev, dev->base_address);

    wdlog(1, "%s: closed\n", dev->name);

    free(dev);
}

static const device_config_t wd8003_config[] =
{
	{
		"base", "Address", CONFIG_HEX16, "", 0x300,
		{
			{
				"0x240", 0x240
			},
			{
				"0x280", 0x280
			},
			{
				"0x300", 0x300
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
				""
			}
		},
	},
	{
		"mac", "MAC Address", CONFIG_MAC, "", -1
	},
	{
		"ram_addr", "RAM address", CONFIG_HEX20, "", 0xD0000,
		{
			{
				"C800", 0xC8000
			},
			{
				"CC00", 0xCC000
			},
			{
				"D000", 0xD0000
			},
			{
				"D400", 0xD4000
			},
			{
				"D800", 0xD8000
			},
			{
				"DC00", 0xDC000
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

static const device_config_t wd8013_config[] =
{
	{
		"base", "Address", CONFIG_HEX16, "", 0x300,
		{
			{
				"0x240", 0x240
			},
			{
				"0x280", 0x280
			},
			{
				"0x300", 0x300
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
				"IRQ 15", 15
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
		"ram_addr", "RAM address", CONFIG_HEX20, "", 0xD0000,
		{
			{
				"C800", 0xC8000
			},
			{
				"CC00", 0xCC000
			},
			{
				"D000", 0xD0000
			},
			{
				"D400", 0xD4000
			},
			{
				"D800", 0xD8000
			},
			{
				"DC00", 0xDC000
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

static const device_config_t mca_mac_config[] =
{
	{
		"mac", "MAC Address", CONFIG_MAC, "", -1
	},
	{
		"", "", -1
	}
};

const device_t wd8003e_device = {
    "Western Digital WD8003E",
    DEVICE_ISA,
    WD8003E,
    wd_init, wd_close, NULL,
    NULL, NULL, NULL,
    wd8003_config
};

const device_t wd8013ebt_device = {
    "Western Digital WD8013EBT",
    DEVICE_ISA,
    WD8013EBT,
    wd_init, wd_close, NULL,
    NULL, NULL, NULL,
    wd8013_config
};

const device_t wd8013epa_device = {
    "Western Digital WD8013EP/A",
    DEVICE_MCA,
    WD8013EPA,
    wd_init, wd_close, NULL,
    NULL, NULL, NULL,
    mca_mac_config
};
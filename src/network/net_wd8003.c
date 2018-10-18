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
 * Version:	@(#)net_wd8003.c	1.0.2	2018/10/17
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
    dp8390_t	*dp8390;
    mem_mapping_t	ram_mapping;
    uint32_t	ram_addr, ram_size;
    uint8_t	maclocal[6];		/* configured MAC (local) address */
    uint8_t	bit16, pad;
    int		board;
    const char	*name;
    uint32_t	base_address;
    int		base_irq;

    /* POS registers, MCA boards only */
    uint8_t	pos_regs[8];

    /* Memory for WD cards*/
    uint8_t	reg0, reg1,
		reg4, reg5,
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
#define wdlog(fmt, ...)
#endif


static const int we_int_table[8] = {9, 3, 5, 7, 10, 11, 15, 4};


static void
wd_interrupt(void *priv, int set)
{
    wd_t *dev = (wd_t *) priv;

    if (dev->reg4 & 0x80)
	return;

    if (set)
	picint(1 << dev->base_irq);
    else
	picintc(1 << dev->base_irq);
}


/* reset - restore state to power-up, cancelling all i/o */
static void
wd_reset(void *priv)
{
    wd_t *dev = (wd_t *)priv;

    wdlog("%s: reset\n", dev->name);

    dp8390_reset(dev->dp8390);
}


static void
wd_soft_reset(void *priv)
{
    wd_t *dev = (wd_t *)priv;

    dp8390_soft_reset(dev->dp8390);
}


static uint32_t
wd_ram_read(uint32_t addr, unsigned len, void *priv)
{
    wd_t *dev = (wd_t *)priv;
    uint32_t ret;
    uint16_t ram_mask = dev->ram_size - 1;

    ret = dev->dp8390->mem[addr & ram_mask];

    if (len == 2)
		ret |= dev->dp8390->mem[(addr + 1) & ram_mask] << 8;

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


static void
wd_ram_write(uint32_t addr, uint32_t val, unsigned len, void *priv)
{
    wd_t *dev = (wd_t *)priv;
    uint16_t ram_mask = dev->ram_size - 1;

    dev->dp8390->mem[addr & ram_mask] = val & 0xff;

    if (len == 2)
	dev->dp8390->mem[(addr + 1) & ram_mask] = val >> 8;
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


static uint32_t
wd_smc_read(wd_t *dev, uint32_t off)
{
    uint32_t retval = 0;
    uint32_t checksum = 0;

    if (dev->board == WD8003E) {
	if ((off >= 0x01) && (off <= 0x06))
		off += 0x07;	/* On the WD8003E, ports 00-06 shadow ports 08-0D. */
	else if (off == 0x07)
		off = 0x0f;	/* Port 07 shadows port 0F (checksum). */
    }

    switch(off) {
	case 0x00:
		if (dev->board == WD8013EBT)
			retval = dev->reg0;
		break;

	case 0x01:
		retval = dev->reg1;
		break;

	case 0x04:
		if (dev->board == WD8013EBT)
			retval = dev->reg4;
		break;

	case 0x05:
		retval = dev->reg5;
		break;
		
	case 0x07:
		if (dev->board == WD8013EPA) {
			if ((dev->if_chip != 0x35) && (dev->if_chip != 0x3A))
				retval = 0;
			else
				retval = dev->if_chip;
		}
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
		checksum = (dev->dp8390->physaddr[0] + dev->dp8390->physaddr[1] + dev->dp8390->physaddr[2] +
				dev->dp8390->physaddr[3] + dev->dp8390->physaddr[4] + dev->dp8390->physaddr[5] +
				dev->board_chip);

		retval = 0xff - (checksum & 0xff);
	break;
    }

    wdlog("%s: ASIC read addr=0x%02x, value=0x%04x\n",
		dev->name, (unsigned)off, (unsigned) retval);

    return(retval);
}


static void
wd_set_irq(wd_t *dev)
{
    uint8_t irq;

    irq = (dev->reg4 & 0x60) >> 5;
    irq |= (dev->reg1 & 0x04);

    dev->base_irq = we_int_table[irq];
}


static void
wd_smc_write(wd_t *dev, uint32_t off, uint32_t val)
{
    wdlog("%s: ASIC write addr=0x%02x, value=0x%04x\n",
	  dev->name, (unsigned)off, (unsigned) val);

    switch(off) {
	case 0x00:	/* WD Control register */
		dev->reg0 = val;

		if (val & 0x80)
			wd_soft_reset(dev);

		if (val & 0x40)
			mem_mapping_enable(&dev->ram_mapping);
		else
			mem_mapping_disable(&dev->ram_mapping);
		break;
		
	case 0x01:
		if (dev->board == WD8003E)
			break;

		dev->reg1 &= 0x60;
		dev->reg1 |= (val & ~0x60);
		if (!dev->bit16)
			dev->reg1 &= ~0x01;

		if (dev->board == WD8013EBT) {
			wd_set_irq(dev);

			dev->ram_size = (dev->reg1 & 0x08) ? 0x8000 : 0x4000;

			mem_mapping_set_addr(&dev->ram_mapping, dev->ram_addr, dev->ram_size);
			if (!(dev->reg0 & 0x40))
				mem_mapping_disable(&dev->ram_mapping);
		}
		break;
		
	case 0x04:
		if (dev->board != WD8013EBT)
			break;

		dev->reg4 = val;
		wd_set_irq(dev);
		break;
		
	case 0x05:
		if (dev->board == WD8003E)
			break;

		dev->reg5 = val;
		break;
		
	case 0x06:
		break;
		
	case 0x07:
		dev->if_chip = val;
		break;
		
	default:
		/* This is invalid, but happens under win95 device detection:
		   maybe some clone cards implement writing for some other
		   registers? */
		wdlog("%s: ASIC write invalid address %04x, ignoring\n",
		      dev->name, (unsigned)off);
		break;
    }
}


static uint8_t
wd_read(uint16_t addr, void *priv, int len)
{
    wd_t *dev = (wd_t *)priv;

    uint8_t retval = 0;
    int off = addr - dev->base_address;

    wdlog("%s: read addr %x\n", dev->name, addr);

    if (off == 0x10)
	retval = dp8390_read_cr(dev->dp8390);
    else if ((off >= 0x00) && (off <= 0x0f))
	retval = wd_smc_read(dev, off);	
    else {
	switch(dev->dp8390->CR.pgsel) {
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

    return(retval);
}


static uint8_t
wd_readb(uint16_t addr, void *priv)
{
    wd_t *dev = (wd_t *) priv;

    return(wd_read(addr, dev, 1));
}


static uint16_t
wd_readw(uint16_t addr, void *priv)
{
    wd_t *dev = (wd_t *) priv;

    return(wd_read(addr, dev, 2));
}


static void
wd_write(uint16_t addr, uint8_t val, void *priv, unsigned int len)
{
    wd_t *dev = (wd_t *)priv;
    int off = addr - dev->base_address;

    wdlog("%s: write addr %x, value %x\n", dev->name, addr, val);

    if (off == 0x10)
	dp8390_write_cr(dev->dp8390, val);
    else if ((off >= 0x00) && (off <= 0x0f))
	wd_smc_write(dev, off, val);	
    else {
	switch(dev->dp8390->CR.pgsel) {
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
    if (dev->bit16) {
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
    if (dev->bit16) {
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
    wd_t *dev = (wd_t *)priv;

    return(dev->pos_regs[port & 7]);
}


#define MCA_61C8_IRQS { 3, 4, 10, 14 }


static void
wd_mca_write(int port, uint8_t val, void *priv)
{
    wd_t *dev = (wd_t *)priv;
    int8_t irq[4] = MCA_61C8_IRQS;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102) return;

    /* Save the MCA register value. */
    dev->pos_regs[port & 7] = val;

    /*
     * The PS/2 Model 80 BIOS always enables a card if it finds one,
     * even if no resources were assigned yet (because we only added
     * the card, but have not run AutoConfig yet...)
     *
     * So, remove current address, if any.
     */
    wd_io_remove(dev, dev->base_address);	

    dev->base_address = 0x800 + ((dev->pos_regs[2] & 0xf0) << 8);

    dev->ram_addr = (dev->pos_regs[3] & 0x0f);
    dev->ram_addr <<= 13;
    dev->ram_addr |= 0xC0000;
    if (dev->pos_regs[3] & 0x80) {
	dev->ram_size = 0x4000;
	dev->ram_addr |= 0xF00000;
    } else {
	if (dev->pos_regs[3] & 0x10)
		dev->ram_size = 0x2000;
	else
		dev->ram_size = 0x4000;
    }

    dev->base_irq = irq[(dev->pos_regs[5] & 0x0c) >> 2];

    /* Initialize the device if fully configured. */
    if (dev->pos_regs[2] & 0x01) {
	/* Card enabled; register (new) I/O handler. */
	wd_io_set(dev, dev->base_address);

	mem_mapping_set_addr(&dev->ram_mapping, dev->ram_addr, dev->ram_size);
	mem_mapping_disable(&dev->ram_mapping);

	wdlog("%s: attached IO=0x%X IRQ=%d, RAM addr=0x%06x\n", dev->name,
	      dev->base_address, dev->base_irq, dev->ram_addr);
    }
}


static void *
wd_init(const device_t *info)
{
    uint32_t mac;
    wd_t *dev;
    int i;
    uint8_t irq;

    /* Get the desired debug level. */
#ifdef ENABLE_NIC_LOG
    i = device_get_config_int("debug");
    if (i > 0) wd_do_log = i;
#endif

    dev = malloc(sizeof(wd_t));
    memset(dev, 0x00, sizeof(wd_t));
    dev->name = info->name;
    dev->board = info->local;

    dev->maclocal[0] = 0x00;  /* 00:00:C0 (WD/SMC OID) */
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
	mac = (((int) dev->maclocal[3]) << 16);
	mac |= (((int) dev->maclocal[4]) << 8);
	mac |= ((int) dev->maclocal[5]);
	device_set_config_mac("mac", mac);
    } else {
	dev->maclocal[3] = (mac>>16) & 0xff;
	dev->maclocal[4] = (mac>>8) & 0xff;
	dev->maclocal[5] = (mac & 0xff);
    }

    if (dev->board == WD8013EPA)
	mca_add(wd_mca_read, wd_mca_write, dev);	
    else {
	dev->base_address = device_get_config_hex16("base");
	dev->base_irq = device_get_config_int("irq");
	dev->ram_addr = device_get_config_hex20("ram_addr");
    }

    dev->dp8390 = device_add(&dp8390_device);
    dev->dp8390->priv = dev;
    dev->dp8390->interrupt = wd_interrupt;

    switch(dev->board) {
	case WD8003E:
		dev->board_chip = WE_TYPE_WD8003E;
		dev->ram_size = 0x2000;
		dp8390_set_defaults(dev->dp8390, DP8390_FLAG_CLEAR_IRQ | DP8390_FLAG_NO_CHIPMEM);
		break;
	
	case WD8013EBT:
		dev->board_chip = WE_TYPE_WD8013EBT;
		dev->ram_size = 0x4000;
		dp8390_set_defaults(dev->dp8390, DP8390_FLAG_CLEAR_IRQ | DP8390_FLAG_NO_CHIPMEM);
		irq = 255;
		for (i = 0; i < 8; i++) {
			if (we_int_table[i] == dev->base_irq)
				irq = i;
		}
		if (irq != 255) {
			dev->reg4 = (irq & 0x03) << 5;
			dev->reg1 = irq & 0x04;
		}

		dev->bit16 = 1;
		break;

	case WD8013EPA:
		dev->board_chip = WE_TYPE_WD8013EP | 0x80;
		dev->ram_size = 0x4000;
		dev->pos_regs[0] = 0xC8;
		dev->pos_regs[1] = 0x61;
		dp8390_set_defaults(dev->dp8390, DP8390_FLAG_CLEAR_IRQ | DP8390_FLAG_NO_CHIPMEM);
		dev->bit16 = 1;
		break;
    }

    if (dev->base_address)
	wd_io_set(dev, dev->base_address);

    memcpy(dev->dp8390->physaddr, dev->maclocal, sizeof(dev->maclocal));

    wdlog("%s: I/O=%04x, IRQ=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
	  dev->name, dev->base_address, dev->base_irq,
	  dev->dp8390->physaddr[0], dev->dp8390->physaddr[1], dev->dp8390->physaddr[2],
	  dev->dp8390->physaddr[3], dev->dp8390->physaddr[4], dev->dp8390->physaddr[5]);

    /* Reset the board. */
    wd_reset(dev);

    /* Map this system into the memory map. */
    if (dev->bit16) {
	mem_mapping_add(&dev->ram_mapping, dev->ram_addr, dev->ram_size,
			wd_ram_readb, wd_ram_readw, NULL,
			wd_ram_writeb, wd_ram_writew, NULL,
			NULL, MEM_MAPPING_EXTERNAL, dev);
    } else {
	mem_mapping_add(&dev->ram_mapping, dev->ram_addr, dev->ram_size,
			wd_ram_readb, NULL, NULL,
			wd_ram_writeb, NULL, NULL,
			NULL, MEM_MAPPING_EXTERNAL, dev);
    }
    mem_mapping_disable(&dev->ram_mapping);		

    /* Attach ourselves to the network module. */
    network_attach(dev->dp8390, dev->dp8390->physaddr, dp8390_rx);

    if (dev->board != WD8013EPA) {
	wdlog("%s: attached IO=0x%X IRQ=%d, RAM addr=0x%06x\n", dev->name,
	      dev->base_address, dev->base_irq, dev->ram_addr);		
    }

    return(dev);
}


static void
wd_close(void *priv)
{
    wd_t *dev = (wd_t *)priv;

    wdlog("%s: closed\n", dev->name);

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
				"IRQ 9", 9
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
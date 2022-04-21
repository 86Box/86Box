/*
 * 86Box	An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA, EISA, VLB, MCA, and PCI system buses,
 *		roughly spanning the era between 1981 and 1995.
 *
 *		This file is part of the 86Box Project.
 *
 *		Implementation of the following network controllers:
 *			- 3Com Etherlink II 3c503 (ISA 8-bit).
 *
 *
 *
 * Based on	@(#)3c503.cpp Carl (MAME)
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Carl, <unknown e-mail address>
 *
 *		Copyright 2018 TheCollector1995.
 *		Copyright 2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 *		Portions Copyright (C) 2018  MAME Project
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
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/random.h>
#include <86box/device.h>
#include <86box/network.h>
#include <86box/net_dp8390.h>
#include <86box/net_3c503.h>
#include <86box/bswap.h>

typedef struct {
    dp8390_t		*dp8390;
    mem_mapping_t	ram_mapping;
    uint32_t		base_address;
    int			base_irq;
    uint32_t		bios_addr;
    uint8_t		maclocal[6];		/* configured MAC (local) address */

    struct {
	uint8_t pstr;
	uint8_t pspr;
	uint8_t dqtr;
	uint8_t bcfr;
	uint8_t pcfr;
	uint8_t gacfr;
	uint8_t ctrl;
	uint8_t streg;
	uint8_t idcfr;
	uint16_t da;
	uint32_t vptr;
	uint8_t rfmsb;
	uint8_t rflsb;
    } regs;

    int dma_channel;
} threec503_t;


#ifdef ENABLE_3COM503_LOG
int threec503_do_log = ENABLE_3COM503_LOG;


static void
threec503_log(const char *fmt, ...)
{
    va_list ap;

    if (threec503_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define threec503_log(fmt, ...)
#endif


static void
threec503_interrupt(void *priv, int set)
{
    threec503_t *dev = (threec503_t *) priv;

    switch (dev->base_irq) {
	case 2:
		dev->regs.idcfr = 0x10;
		break;

	case 3:
		dev->regs.idcfr = 0x20;
		break;

	case 4:
		dev->regs.idcfr = 0x40;
		break;

	case 5:
		dev->regs.idcfr = 0x80;
		break;
    }

    if (set)
	picint(1 << dev->base_irq);
    else
	picintc(1 << dev->base_irq);
}


static void
threec503_ram_write(uint32_t addr, uint8_t val, void *priv)
{
    threec503_t *dev = (threec503_t *)priv;

    if ((addr & 0x3fff) >= 0x2000)
	return;

    dev->dp8390->mem[addr & 0x1fff] = val;
}


static uint8_t
threec503_ram_read(uint32_t addr, void *priv)
{
    threec503_t *dev = (threec503_t *)priv;

    if ((addr & 0x3fff) >= 0x2000)
	return 0xff;

    return dev->dp8390->mem[addr & 0x1fff];
}


static void
threec503_set_drq(threec503_t *dev)
{
    switch (dev->dma_channel) {
	case 1:
		dev->regs.idcfr = 1;
		break;

	case 2:
		dev->regs.idcfr = 2;
		break;

	case 3:
		dev->regs.idcfr = 4;
		break;
    }
}


/* reset - restore state to power-up, cancelling all i/o */
static void
threec503_reset(void *priv)
{
    threec503_t *dev = (threec503_t *)priv;

#ifdef ENABLE_3COM503_LOG
    threec503_log("3Com503: reset\n");
#endif

    dp8390_reset(dev->dp8390);

    memset(&dev->regs, 0, sizeof(dev->regs));

    dev->regs.ctrl = 0x0a;
}


static uint8_t
threec503_nic_lo_read(uint16_t addr, void *priv)
{
    threec503_t *dev = (threec503_t *)priv;
    uint8_t retval = 0;
    int off = addr - dev->base_address;

    switch ((dev->regs.ctrl >> 2) & 3) {
	case 0x00:
		threec503_log("Read offset=%04x\n", off);
		if (off == 0x00)
			retval = dp8390_read_cr(dev->dp8390);
		else switch(dev->dp8390->CR.pgsel) {
			case 0x00:
				retval = dp8390_page0_read(dev->dp8390, off, 1);
				break;

			case 0x01:
				retval = dp8390_page1_read(dev->dp8390, off, 1);
				break;

			case 0x02:
				retval = dp8390_page2_read(dev->dp8390, off, 1);
				break;

			case 0x03:
				retval = 0xff;
				break;
		}
		break;

	case 0x01:
		retval = dev->dp8390->macaddr[off];
		break;

	case 0x02:
		retval = dev->dp8390->macaddr[off + 0x10];
		break;

	case 0x03:
		retval = 0xff;
		break;
    }

    return(retval);
}


static void
threec503_nic_lo_write(uint16_t addr, uint8_t val, void *priv)
{
    threec503_t *dev = (threec503_t *)priv;
    int off = addr - dev->base_address;

    switch ((dev->regs.ctrl >> 2) & 3) {
	case 0x00:
		/* The high 16 bytes of i/o space are for the ne2000 asic -
		   the low 16 bytes are for the DS8390, with the current
		   page being selected by the PS0,PS1 registers in the
		   command register */
		if (off == 0x00)
			dp8390_write_cr(dev->dp8390, val);
		else switch(dev->dp8390->CR.pgsel) {
			case 0x00:
				dp8390_page0_write(dev->dp8390, off, val, 1);
				break;

			case 0x01:
				dp8390_page1_write(dev->dp8390, off, val, 1);
				break;

			case 0x02:
				dp8390_page2_write(dev->dp8390, off, val, 1);
				break;

			case 0x03:
				break;
		}
		break;

	case 0x01:
	case 0x02:
	case 0x03:
		break;
    }

    threec503_log("3Com503: write addr %x, value %x\n", addr, val);
}


static uint8_t
threec503_nic_hi_read(uint16_t addr, void *priv)
{
    threec503_t *dev = (threec503_t *)priv;

    threec503_log("3Com503: Read GA address=%04x\n", addr);

    switch (addr & 0x0f) {
	case 0x00:
		return dev->regs.pstr;

	case 0x01:
		return dev->regs.pspr;

	case 0x02:
		return dev->regs.dqtr;

	case 0x03:
		switch (dev->base_address) {
			default:
			case 0x300:
				dev->regs.bcfr = 0x80;
				break;

			case 0x310:
				dev->regs.bcfr = 0x40;
				break;

			case 0x330:
				dev->regs.bcfr = 0x20;
				break;

			case 0x350:
				dev->regs.bcfr = 0x10;
				break;

			case 0x250:
				dev->regs.bcfr = 0x08;
				break;

			case 0x280:
				dev->regs.bcfr = 0x04;
				break;

			case 0x2a0:
				dev->regs.bcfr = 0x02;
				break;

			case 0x2e0:
				dev->regs.bcfr = 0x01;
				break;
		}

		return dev->regs.bcfr;
		break;

	case 0x04:
		switch (dev->bios_addr) {
			case 0xdc000:
				dev->regs.pcfr = 0x80;
				break;

			case 0xd8000:
				dev->regs.pcfr = 0x40;
				break;

			case 0xcc000:
				dev->regs.pcfr = 0x20;
				break;

			case 0xc8000:
				dev->regs.pcfr = 0x10;
				break;
		}

		return dev->regs.pcfr;
		break;

	case 0x05:
		return dev->regs.gacfr;

	case 0x06:
		return dev->regs.ctrl;

	case 0x07:
		return dev->regs.streg;

	case 0x08:
		return dev->regs.idcfr;

	case 0x09:
		return (dev->regs.da >> 8);

	case 0x0a:
		return (dev->regs.da & 0xff);

	case 0x0b:
		return (dev->regs.vptr >> 12) & 0xff;

	case 0x0c:
		return (dev->regs.vptr >> 4) & 0xff;

	case 0x0d:
		return (dev->regs.vptr & 0x0f) << 4;

	case 0x0e:
	case 0x0f:
		if (!(dev->regs.ctrl & 0x80))
			return 0xff;

		threec503_set_drq(dev);

		return dp8390_chipmem_read(dev->dp8390, dev->regs.da++, 1);
    }

    return 0;
}


static void
threec503_nic_hi_write(uint16_t addr, uint8_t val, void *priv)
{
    threec503_t *dev = (threec503_t *)priv;

    threec503_log("3Com503: Write GA address=%04x, val=%04x\n", addr, val);

    switch (addr & 0x0f) {
	case 0x00:
		dev->regs.pstr = val;
		break;

	case 0x01:
		dev->regs.pspr = val;
		break;

	case 0x02:
		dev->regs.dqtr = val;
		break;

	case 0x05:
		if ((dev->regs.gacfr & 0x0f) != (val & 0x0f)) {
			switch (val & 0x0f) {
				case 0: /*ROM mapping*/
					/* FIXME: Implement this when a BIOS is found/generated. */
					mem_mapping_disable(&dev->ram_mapping);
					break;

				case 9: /*RAM mapping*/
					mem_mapping_enable(&dev->ram_mapping);
					break;

				default: /*No ROM mapping*/
					mem_mapping_disable(&dev->ram_mapping);
					break;
			}
		}

		if (!(val & 0x80))
			threec503_interrupt(dev, 1);
		else
			threec503_interrupt(dev, 0);

		dev->regs.gacfr = val;
		break;

	case 0x06:
		if (val & 1) {
			threec503_reset(dev);
			dev->dp8390->ISR.reset = 1;
			dev->regs.ctrl = 0x0b;
			return;
		}

		if ((val & 0x80) != (dev->regs.ctrl & 0x80)) {
			if (val & 0x80)
				dev->regs.streg |= 0x88;
			else
				dev->regs.streg &= ~0x88;
			dev->regs.streg &= ~0x10;
		}
		dev->regs.ctrl = val;
		break;

	case 0x08:
		switch (val & 0xf0) {
			case 0x00:
			case 0x10:
			case 0x20:
			case 0x40:
			case 0x80:
				dev->regs.idcfr = (dev->regs.idcfr & 0x0f) | (val & 0xf0);
				break;

			default:
				threec503_log("Trying to set multiple IRQs: %02x\n", val);
				break;
		}

		switch (val & 0x0f) {
			case 0x00:
			case 0x01:
			case 0x02:
			case 0x04:
				dev->regs.idcfr = (dev->regs.idcfr & 0xf0) | (val & 0x0f);
				break;

			case 0x08:
				break;

			default:
				threec503_log("Trying to set multiple DMA channels: %02x\n", val);
				break;
		}
		break;

	case 0x09:
		dev->regs.da = (val << 8) | (dev->regs.da & 0xff);
		break;

	case 0x0a:
		dev->regs.da = (dev->regs.da & 0xff00) | val;
		break;

	case 0x0b:
		dev->regs.vptr = (val << 12) | (dev->regs.vptr & 0xfff);
		break;

	case 0x0c:
		dev->regs.vptr = (val << 4) | (dev->regs.vptr & 0xff00f);
		break;

	case 0x0d:
		dev->regs.vptr = (val << 4) | (dev->regs.vptr & 0xffff0);
		break;

	case 0x0e:
	case 0x0f:
		if (!(dev->regs.ctrl & 0x80))
			return;

		threec503_set_drq(dev);

		dp8390_chipmem_write(dev->dp8390, dev->regs.da++, val, 1);
		break;
    }
}


static void
threec503_nic_ioset(threec503_t *dev, uint16_t addr)
{
    io_sethandler(addr, 0x10,
		  threec503_nic_lo_read, NULL, NULL,
		  threec503_nic_lo_write, NULL, NULL, dev);

    io_sethandler(addr+0x400, 0x10,
		  threec503_nic_hi_read, NULL, NULL,
		  threec503_nic_hi_write, NULL, NULL, dev);
}


static void *
threec503_nic_init(const device_t *info)
{
    uint32_t mac;
    threec503_t *dev;

    dev = malloc(sizeof(threec503_t));
    memset(dev, 0x00, sizeof(threec503_t));
    dev->maclocal[0] = 0x02;  /* 02:60:8C (3Com OID) */
    dev->maclocal[1] = 0x60;
    dev->maclocal[2] = 0x8C;

    dev->base_address = device_get_config_hex16("base");
    dev->base_irq = device_get_config_int("irq");
    dev->dma_channel = device_get_config_int("dma");
    dev->bios_addr = device_get_config_hex20("bios_addr");

    /* See if we have a local MAC address configured. */
    mac = device_get_config_mac("mac", -1);

    /*
     * Make this device known to the I/O system.
     * PnP and PCI devices start with address spaces inactive.
     */
    threec503_nic_ioset(dev, dev->base_address);

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
    dev->dp8390->interrupt = threec503_interrupt;
    dp8390_set_defaults(dev->dp8390, DP8390_FLAG_CHECK_CR | DP8390_FLAG_CLEAR_IRQ);
    dp8390_mem_alloc(dev->dp8390, 0x2000, 0x2000);

    memcpy(dev->dp8390->physaddr, dev->maclocal, sizeof(dev->maclocal));

    threec503_log("I/O=%04x, IRQ=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
	dev->base_address, dev->base_irq,
	dev->dp8390->physaddr[0], dev->dp8390->physaddr[1], dev->dp8390->physaddr[2],
	dev->dp8390->physaddr[3], dev->dp8390->physaddr[4], dev->dp8390->physaddr[5]);

    /* Reset the board. */
    threec503_reset(dev);

    /* Map this system into the memory map. */
    mem_mapping_add(&dev->ram_mapping, dev->bios_addr, 0x4000,
		    threec503_ram_read, NULL, NULL,
		    threec503_ram_write, NULL, NULL,
		    NULL, MEM_MAPPING_EXTERNAL, dev);
    // mem_mapping_disable(&dev->ram_mapping);
    dev->regs.gacfr = 0x09;	/* Start with RAM mapping enabled. */

    /* Attach ourselves to the network module. */
    network_attach(dev->dp8390, dev->dp8390->physaddr, dp8390_rx, NULL, NULL);

    return(dev);
}


static void
threec503_nic_close(void *priv)
{
    threec503_t *dev = (threec503_t *)priv;

#ifdef ENABLE_3COM503_LOG
    threec503_log("3Com503: closed\n");
#endif

    free(dev);
}

static const device_config_t threec503_config[] = {
// clang-format off
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x300,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "0x250", .value = 0x250 },
            { .description = "0x280", .value = 0x280 },
            { .description = "0x2a0", .value = 0x2a0 },
            { .description = "0x2e0", .value = 0x2e0 },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x310", .value = 0x310 },
            { .description = "0x330", .value = 0x330 },
            { .description = "0x350", .value = 0x350 },
            { .description = "",      .value =     0 }
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
            { .description = "IRQ 4", .value = 4 },
            { .description = "IRQ 5", .value = 5 },
            { .description = "",      .value = 0 }
        },
    },
    {
        .name = "dma",
        .description = "DMA",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 3,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "DMA 1", .value = 1 },
            { .description = "DMA 2", .value = 2 },
            { .description = "DMA 3", .value = 3 },
            { .description = "",      .value = 0 }
        },
    },
    {
        .name = "mac",
        .description = "MAC Address",
        .type = CONFIG_MAC,
        .default_string = "",
        .default_int = -1,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "", .value = 0 }
        },
    },
    {
        .name = "bios_addr",
        .description = "BIOS address",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xCC000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "DC00", .value = 0xDC000 },
            { .description = "D800", .value = 0xD8000 },
            { .description = "C800", .value = 0xC8000 },
            { .description = "CC00", .value = 0xCC000 },
            { .description = "",     .value = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format off
};

const device_t threec503_device = {
    .name = "3Com EtherLink II",
    .internal_name = "3c503",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = threec503_nic_init,
    .close = threec503_nic_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = threec503_config
};

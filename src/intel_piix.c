/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the Intel PIIX and PIIX3 Xcelerators.
 *
 *		PRD format :
 *		    word 0 - base address
 *		    word 1 - bits 1-15 = byte count, bit 31 = end of transfer
 *
 * Version:	@(#)intel_piix.c	1.0.23	2020/01/24
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "cdrom.h"
#include "cpu.h"
#include "scsi_device.h"
#include "scsi_cdrom.h"
#include "dma.h"
#include "86box_io.h"
#include "device.h"
#include "apm.h"
#include "keyboard.h"
#include "machine.h"
#include "mem.h"
#include "timer.h"
#include "nvr.h"
#include "pci.h"
#include "pic.h"
#include "port_92.h"
#include "hdc.h"
#include "hdc_ide.h"
#include "hdc_ide_sff8038i.h"
#include "zip.h"
#include "machine.h"
#include "smbus.h"
#include "piix.h"


#define ACPI_TIMER_FREQ	3579545
#define PM_FREQ		ACPI_TIMER_FREQ

#define RSM_STS		(1 << 15)
#define PWRBTN_STS	(1 << 8)

#define RTC_EN		(1 << 10)
#define PWRBTN_EN	(1 << 8)
#define GBL_EN		(1 << 5)
#define TMROF_EN	(1 << 0)

#define SCI_EN		(1 << 0)
#define SUS_EN		(1 << 13)

#define ACPI_ENABLE	0xf1
#define	ACPI_DISABLE	0xf0


typedef struct
{
    uint16_t		io_base;
    int			base_channel;
} ddma_t;


typedef struct
{
    uint8_t		gpireg[3], gporeg[4];
    uint16_t		pmsts, pmen,
			pmcntrl;
    uint32_t		glbctl;
    uint64_t		tmr_overflow_time;
    int			timer_index;
} power_t;


typedef struct
{
    uint8_t		stat, next_stat, ctl, cmd, addr,
			data0, data1,
			index,
			data[32];
    pc_timer_t		command_timer;
} piix_smbus_t;


typedef struct
{
    uint8_t		cur_readout_reg,
			type, func_shift,
			max_func, pci_slot,
			regs[4][256],
			readout_regs[256], board_config[2];
    uint16_t		func0_id,
			usb_io_base, power_io_base,
			smbus_io_base;
    sff8038i_t		*bm[2];
    ddma_t		ddma[2];
    power_t		power;
    piix_smbus_t	smbus;
    nvr_t *		nvr;
} piix_t;


#ifdef ENABLE_PIIX_LOG
int piix_do_log = ENABLE_PIIX_LOG;


static void
piix_log(const char *fmt, ...)
{
    va_list ap;

    if (piix_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define piix_log(fmt, ...)
#endif


static
void do_irq(piix_t *dev, int func, int level)
{
    if ((dev == NULL) || (func > dev->max_func) /*||
	(dev->regs[func][0x3d] < PCI_INTA) || (dev->regs[func][0x3d] < PCI_INTD)*/)
	return;

    if (level) {
#ifdef WRONG_SPEC
	pci_set_irq(dev->pci_slot, dev->regs[func][0x3d]);
#else
	picintlevel(1 << 9);
#endif
	piix_log("Raising IRQ...\n");
    } else {
#ifdef WRONG_SPEC
	pci_clear_irq(dev->pci_slot, dev->regs[func][0x3d]);
#else
	picintc(1 << 9);
#endif
	piix_log("Lowering IRQ...\n");
    }
}


static void
piix_ide_legacy_handlers(piix_t *dev, int bus)
{
    if (bus & 0x01) {
	ide_pri_disable();
	if ((dev->regs[1][0x04] & 0x01) && (dev->regs[1][0x41] & 0x80))
		ide_pri_enable();
    }

    if (bus & 0x02) {
	ide_sec_disable();
	if ((dev->regs[1][0x04] & 0x01) && (dev->regs[1][0x43] & 0x80))
		ide_sec_enable();
    }
}


static void
piix_ide_bm_handlers(piix_t *dev)
{
    uint16_t base = (dev->regs[1][0x20] & 0xf0) | (dev->regs[1][0x21] << 8);

    sff_bus_master_handler(dev->bm[0], (dev->regs[1][0x04] & 1), base);
    sff_bus_master_handler(dev->bm[1], (dev->regs[1][0x04] & 1), base + 8);
}


static uint8_t
kbc_alias_reg_read(uint16_t addr, void *p)
{
    uint8_t ret = inb(0x61);

    return ret;
}


static void
kbc_alias_reg_write(uint16_t addr, uint8_t val, void *p)
{
    outb(0x61, val);
}


static void
kbc_alias_update_io_mapping(piix_t *dev)
{
    io_removehandler(0x0063, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
    io_removehandler(0x0065, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
    io_removehandler(0x0067, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);

    if (dev->regs[0][0x4e] & 0x08) {
	io_sethandler(0x0063, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
	io_sethandler(0x0065, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
	io_sethandler(0x0067, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
    }
}


static uint8_t
ddma_reg_read(uint16_t addr, void *p)
{
    ddma_t *dev = (ddma_t *) p;
    uint8_t ret = 0xff;
    int rel_ch = (addr & 0x30) >> 4;
    int ch = dev->base_channel + rel_ch;
    int dmab = (ch >= 4) ? 0xc0 : 0x00;

    switch (addr & 0x0f) {
	case 0x00:
		ret = dma[ch].ac & 0xff;
		break;
	case 0x01:
		ret = (dma[ch].ac >> 8) & 0xff;
		break;
	case 0x02:
		ret = dma[ch].page;
		break;
	case 0x04:
		ret = dma[ch].cc & 0xff;
		break;
	case 0x05:
		ret = (dma[ch].cc >> 8) & 0xff;
		break;
	case 0x09:
		ret = inb(dmab + 0x08);
		break;
    }

    return ret;
}


static void
ddma_reg_write(uint16_t addr, uint8_t val, void *p)
{
    ddma_t *dev = (ddma_t *) p;
    int rel_ch = (addr & 0x30) >> 4;
    int ch = dev->base_channel + rel_ch;
    int page_regs[4] = { 7, 3, 1, 2 };
    int i, dmab = (ch >= 4) ? 0xc0 : 0x00;

    switch (addr & 0x0f) {
	case 0x00:
		dma[ch].ab = (dma[ch].ab & 0xffff00) | val;
		dma[ch].ac = dma[ch].ab;
		break;
	case 0x01:
		dma[ch].ab = (dma[ch].ab & 0xff00ff) | (val << 8);
		dma[ch].ac = dma[ch].ab;
		break;
	case 0x02:
		if (ch >= 4)
			outb(0x88 + page_regs[rel_ch], val);
		else
			outb(0x80 + page_regs[rel_ch], val);
		break;
	case 0x04:
		dma[ch].cb = (dma[ch].cb & 0xffff00) | val;
		dma[ch].cc = dma[ch].cb;
		break;
	case 0x05:
		dma[ch].cb = (dma[ch].cb & 0xff00ff) | (val << 8);
		dma[ch].cc = dma[ch].cb;
		break;
	case 0x08:
		outb(dmab + 0x08, val);
		break;
	case 0x09:
		outb(dmab + 0x09, val);
		break;
	case 0x0a:
		outb(dmab + 0x0a, val);
		break;
	case 0x0b:
		outb(dmab + 0x0b, val);
		break;
	case 0x0d:
		outb(dmab + 0x0d, val);
		break;
	case 0x0e:
		for (i = 0; i < 4; i++)
			outb(dmab + 0x0a, i);
		break;
	case 0x0f:
		outb(dmab + 0x0a, (val << 2) | rel_ch);
		break;
    }
}


static void
ddma_update_io_mapping(piix_t *dev, int n)
{
    int base_reg = 0x92 + (n << 1);

    if (dev->ddma[n].io_base != 0x0000)
	io_removehandler(dev->usb_io_base, 0x40, ddma_reg_read, NULL, NULL, ddma_reg_write, NULL, NULL, &dev->ddma[n]);

    dev->ddma[n].io_base = (dev->regs[0][base_reg] & ~0x3f) | (dev->regs[0][base_reg + 1] << 8);

    if (dev->ddma[n].io_base != 0x0000)
	io_sethandler(dev->ddma[n].io_base, 0x40, ddma_reg_read, NULL, NULL, ddma_reg_write, NULL, NULL, &dev->ddma[n]);
}


static uint8_t
usb_reg_read(uint16_t addr, void *p)
{
    uint8_t ret = 0xff;

    switch (addr & 0x1f) {
	case 0x10: case 0x11: case 0x12: case 0x13:
		/* Port status */
                ret = 0x00;
		break;
    }

    return ret;
}


static void
usb_reg_write(uint16_t addr, uint8_t val, void *p)
{
}


static void
usb_update_io_mapping(piix_t *dev)
{
    if (dev->usb_io_base != 0x0000)
	io_removehandler(dev->usb_io_base, 0x20, usb_reg_read, NULL, NULL, usb_reg_write, NULL, NULL, dev);

    dev->usb_io_base = (dev->regs[2][0x20] & ~0x1f) | (dev->regs[2][0x21] << 8);

    if ((dev->regs[2][PCI_REG_COMMAND] & PCI_COMMAND_IO) && (dev->usb_io_base != 0x0000))
	io_sethandler(dev->usb_io_base, 0x20, usb_reg_read, NULL, NULL, usb_reg_write, NULL, NULL, dev);
}


static uint32_t
power_reg_readl(uint16_t addr, void *p)
{
    piix_t *dev = (piix_t *) p;
    uint32_t timer;
    uint32_t ret = 0xffffffff;

    switch (addr & 0x3c) {
	case 0x08:
		/* ACPI timer */
		timer = (tsc * ACPI_TIMER_FREQ) / machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed;
		timer &= 0x00ffffff;
		ret = timer;
		break;
    }

    piix_log("ACPI: Read L %08X from %04X\n", ret, addr);

    return ret;
}


static uint16_t
power_reg_readw(uint16_t addr, void *p)
{
    piix_t *dev = (piix_t *) p;
    uint16_t ret = 0xffff;
    uint32_t ret32;

    switch (addr & 0x3c) {
	case 0x00:
		break;
	default:
		ret32 = power_reg_readl(addr, p);
		if (addr & 0x02)
			ret = (ret32 >> 16) & 0xffff;
		else
			ret = ret32 & 0xffff;
		break;
    }

    piix_log("ACPI: Read W %08X from %04X\n", ret, addr);

    return ret;
}


static uint8_t
power_reg_read(uint16_t addr, void *p)
{
    piix_t *dev = (piix_t *) p;
    uint32_t timer;
    uint8_t ret = 0xff;
    uint16_t ret16;

    switch (addr & 0x3f) {
	case 0x30: case 0x31: case 0x32:
		ret = dev->power.gporeg[addr & 0x03];
		piix_log("ACPI: Read B %02X from GPIREG %01X\n", ret, addr & 0x03);
		break;
	case 0x34: case 0x35: case 0x36: case 0x37:
		ret = dev->power.gporeg[addr & 0x03];
		piix_log("ACPI: Read B %02X from GPOREG %01X\n", ret, addr & 0x03);
		break;
	default:
		ret16 = power_reg_readw(addr, p);
		if (addr & 0x01)
			ret = (ret16 >> 8) & 0xff;
		else
			ret = ret16 & 0xff;
		break;
    }

    return ret;
}


static void
power_reg_write(uint16_t addr, uint8_t val, void *p)
{
    piix_t *dev = (piix_t *) p;

    piix_log("ACPI: Write %02X to %04X\n", val, addr);

    switch (addr & 0x3f) {
	case 0x34: case 0x35: case 0x36: case 0x37:
		dev->power.gporeg[addr & 0x03] = val;
		break;
    }
}


static void
power_update_io_mapping(piix_t *dev)
{
    if (dev->power_io_base != 0x0000)
	io_removehandler(dev->power_io_base, 0x40, power_reg_read, NULL, NULL, power_reg_write, NULL, NULL, dev);

    dev->power_io_base = (dev->regs[3][0x41] << 8) | (dev->regs[3][0x40] & 0xc0);

    if ((dev->regs[3][0x80] & 0x01) && (dev->power_io_base != 0x0000))
	io_sethandler(dev->power_io_base, 0x40, power_reg_read, NULL, NULL, power_reg_write, NULL, NULL, dev);
}


static uint8_t
smbus_reg_read(uint16_t addr, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    uint8_t ret = 0x00;

    switch (addr - dev->smbus_io_base) {
    	case 0x00:
    		ret = dev->smbus.stat;
    		break;
    	case 0x02:
    		dev->smbus.index = 0;
    		ret = dev->smbus.ctl;
    		break;
    	case 0x03:
    		ret = dev->smbus.cmd;
    		break;
    	case 0x04:
    		ret = dev->smbus.addr;
    		break;
    	case 0x05:
    		ret = dev->smbus.data0;
    		break;
    	case 0x06:
    		ret = dev->smbus.data1;
    		break;
    	case 0x07:
    		ret = dev->smbus.data[dev->smbus.index++];
    		if (dev->smbus.index > 31)
    			dev->smbus.index = 0;
    		break;
    }

    piix_log("smbus_reg_read %02x %02x\n", addr - dev->smbus_io_base, ret);

    return ret;
}


static void
smbus_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    uint8_t smbus_addr;
    uint8_t smbus_read;
    uint16_t temp;

    piix_log("smbus_reg_write %02x %02x\n", addr - dev->smbus_io_base, val);

    dev->smbus.next_stat = 0;
    switch (addr - dev->smbus_io_base) {
    	case 0x00:
    		/* some status bits are reset by writing 1 to them */
    		for (smbus_addr = 0x02; smbus_addr <= 0x10; smbus_addr = smbus_addr << 1) {
    			if (val & smbus_addr)
    				dev->smbus.stat = dev->smbus.stat & ~smbus_addr;
    		}
    		break;
    	case 0x02:
    		dev->smbus.ctl = val & ~(0x40); /* START always reads 0 */
    		if (val & 0x40) { /* dispatch command if START is set */
    			smbus_addr = (dev->smbus.addr >> 1);
    			if (!smbus_has_device(smbus_addr)) {
    				/* raise DEV_ERR if no device is at this address */
    				dev->smbus.next_stat = 0x4;
    				break;
    			}
    			smbus_read = (dev->smbus.addr & 0x01);

    			switch ((val >> 2) & 0x7) {
    				case 0x0: /* quick R/W */
    					dev->smbus.next_stat = 0x2;
    					break;
    				case 0x1: /* byte R/W */
    					if (smbus_read)
    						dev->smbus.data0 = smbus_read_byte(smbus_addr);
    					else
    						smbus_write_byte(smbus_addr, dev->smbus.data0);
    					dev->smbus.next_stat = 0x2;
    					break;
    				case 0x2: /* byte data R/W */
    					if (smbus_read)
    						dev->smbus.data0 = smbus_read_byte_cmd(smbus_addr, dev->smbus.cmd);
    					else
    						smbus_write_byte_cmd(smbus_addr, dev->smbus.cmd, dev->smbus.data0);
    					dev->smbus.next_stat = 0x2;
    					break;
    				case 0x3: /* word data R/W */
    					if (smbus_read) {
    						temp = smbus_read_word_cmd(smbus_addr, dev->smbus.cmd);
    						dev->smbus.data0 = (temp & 0xFF);
    						dev->smbus.data1 = (temp >> 8);
    					} else {
    						temp = (dev->smbus.data1 << 8) | dev->smbus.data0;
    						smbus_write_word_cmd(smbus_addr, dev->smbus.cmd, temp);
    					}
    					dev->smbus.next_stat = 0x2;
    					break;
    				case 0x5: /* block R/W */
    					if (smbus_read)
    						dev->smbus.data0 = smbus_read_block_cmd(smbus_addr, dev->smbus.cmd, dev->smbus.data);
    					else
    						smbus_write_block_cmd(smbus_addr, dev->smbus.cmd, dev->smbus.data, dev->smbus.data0);
    					dev->smbus.next_stat = 0x2;
    					break;
    			}
    		}
    		break;
    	case 0x03:
    		dev->smbus.cmd = val;
    		break;
    	case 0x04:
    		dev->smbus.addr = val;
    		break;
    	case 0x05:
    		dev->smbus.data0 = val;
    		break;
    	case 0x06:
    		dev->smbus.data1 = val;
    		break;
    	case 0x07:
    		dev->smbus.data[dev->smbus.index++] = val;
    		if (dev->smbus.index > 31)
    			dev->smbus.index = 0;
    		break;
    }

    if (dev->smbus.next_stat) {
    	dev->smbus.stat = 0x1;
    	timer_disable(&dev->smbus.command_timer);
    	timer_set_delay_u64(&dev->smbus.command_timer, 10 * TIMER_USEC);
    }
}


static void
smbus_inter(void *priv)
{
    piix_t *dev = (piix_t *) priv;
    dev->smbus.stat = dev->smbus.next_stat;
}


static void
smbus_update_io_mapping(piix_t *dev)
{
    if (dev->smbus_io_base != 0x0000)
	io_removehandler(dev->smbus_io_base, 0x10, smbus_reg_read, NULL, NULL, smbus_reg_write, NULL, NULL, dev);

    dev->smbus_io_base = (dev->regs[3][0x91] << 8) | (dev->regs[3][0x90] & 0xf0);

    if ((dev->regs[3][PCI_REG_COMMAND] & PCI_COMMAND_IO) && (dev->regs[3][0xd2] & 0x01) && (dev->smbus_io_base != 0x0000))
	io_sethandler(dev->smbus_io_base, 0x10, smbus_reg_read, NULL, NULL, smbus_reg_write, NULL, NULL, dev);
}


static void
piix_write(int func, int addr, uint8_t val, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    uint8_t *fregs;

    /* Return on unsupported function. */
    if (func > dev->max_func)
	return;

    piix_log("PIIX function %i write: %02X to %02X\n", func, val, addr);
    fregs = (uint8_t *) dev->regs[func];

    if (func == 0)  switch (addr) {
	case 0x04:
		fregs[0x04] = (val & 0x08) | 0x07;
		break;
	case 0x05:
		if (dev->type > 1)
			fregs[0x05] = (val & 0x01);
		break;
	case 0x07:
		if ((val & 0x40) && (dev->type > 1))
			fregs[0x07] &= 0xbf;
		if (val & 0x20)
			fregs[0x07] &= 0xdf;
		if (val & 0x10)
			fregs[0x07] &= 0xef;
		if (val & 0x08)
			fregs[0x07] &= 0xf7;
		if (val & 0x04)
			fregs[0x07] &= 0xfb;
		break;
	case 0x4c:
		fregs[0x4c] = val;
		if (val & 0x80) {
			if (dev->type > 1)
				dma_alias_remove();
			else
				dma_alias_remove_piix();
		} else {
			if (dev->type > 1)
				dma_alias_set();
			else
				dma_alias_set_piix();
		}
		break;
	case 0x4e:
		fregs[0x4e] = val;
		keyboard_at_set_mouse_scan((val & 0x10) ? 1 : 0);
		if (dev->type >= 4)
			kbc_alias_update_io_mapping(dev);
		break;
	case 0x4f:
		if (dev->type > 3)
			fregs[0x4f] = val & 0x07;
		else if (dev->type == 3)
			fregs[0x4f] = val & 0x01;
		break;
	case 0x60: case 0x61: case 0x62: case 0x63:
		piix_log("Set IRQ routing: INT %c -> %02X\n", 0x41 + (addr & 0x03), val);
		fregs[addr] = val & 0x8f;
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTA + (addr & 0x03), PCI_IRQ_DISABLED);
		else
			pci_set_irq_routing(PCI_INTA + (addr & 0x03), val & 0xf);
		break;
	case 0x64:
		if (dev->type > 3)
			fregs[0x64] = val;
		break;
	case 0x69:
		if (dev->type > 1)
			fregs[0x69] = val & 0xfe;
		else
			fregs[0x69] = val & 0xfa;
		break;
	case 0x6a:
		switch (dev->type) {
			case 0: case 1:
			default:
				fregs[0x6a] = (fregs[0x6a] & 0xfb) | (val & 0x04);
				if (dev->type > 0) {
					fregs[0x0e] = (val & 0x04) ? 0x80 : 0x00;
					dev->max_func = 0 + !!(val & 0x04);
				}
				break;
			case 3:
				fregs[0x6a] = val & 0xd1;
				dev->max_func = 1 + !!(val & 0x10);
				break;
			case 4:
				fregs[0x6a] = val & 0x80;
				break;
		}
		break;
	case 0x6b:
		if ((dev->type > 1) && (val & 0x80))
			fregs[0x6b] &= 0x7f;
		return;
	case 0x70: case 0x71:
		if ((dev->type > 1) && (addr == 0x71))
			break;
		if (dev->type < 4) {
			piix_log("Set MIRQ routing: MIRQ%i -> %02X\n", addr & 0x01, val);
			if (dev->type > 1)
				fregs[addr] = val & 0xef;
			else
				fregs[addr] = val & 0xcf;
			if (val & 0x80)
				pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
			else
				pci_set_mirq_routing(PCI_MIRQ0, val & 0xf);
			piix_log("MIRQ%i is %s\n", addr & 0x01, (val & 0x20) ? "disabled" : "enabled");
		}
		break;
	case 0x76: case 0x77:
		if (dev->type > 1)
			fregs[addr] = val & 0x87;
		else
			fregs[addr] = val & 0x8f;
		break;
	case 0x78: case 0x79:
		if (dev->type < 4)
			fregs[addr] = val;
		break;
	case 0x80:
		if (dev->type > 1)
			fregs[addr] = val & 0x7f;
		break;
	case 0x81:
		if (dev->type > 1)
			fregs[addr] = val & 0x0f;
		break;
	case 0x90:
		if (dev->type > 3)
			fregs[addr] = val;
		break;
	case 0x91:
		if (dev->type > 3)
			fregs[addr] = val & 0xfc;
		break;
	case 0x92: case 0x93: case 0x94: case 0x95:
		if (dev->type == 4) {
			if (addr & 0x01)
				fregs[addr] = val & 0xc0;
			else
				fregs[addr] = val & 0xff;
			ddma_update_io_mapping(dev, (addr >> 2) & 1);
		}
		break;
	case 0xa0:
		if (dev->type < 4)
			fregs[addr] = val & 0x1f;
		break;
	case 0xa2: case 0xa5: case 0xa6: case 0xa8:
	case 0xaa: case 0xac: case 0xae:
		if (dev->type < 4)
			fregs[addr] = val & 0xff;
		break;
	case 0xa3: case 0xab:
		if (dev->type == 3)
			fregs[addr] = val & 0x01;
		break;
	case 0xa4:
		if (dev->type < 4)
			fregs[addr] = val & 0xfb;
		break;
	case 0xa7:
		if (dev->type == 3)
			fregs[addr] = val & 0xef;
		else if (dev->type < 3)
			fregs[addr] = val;
		break;
	case 0xb0:
		if (dev->type > 3)
			fregs[addr] = (fregs[addr] & 0x8c) | (val & 0x73);
		break;
	case 0xb1:
		if (dev->type > 3)
			fregs[addr] = val & 0xdf;
		break;
	case 0xb2:
		if (dev->type > 3)
			fregs[addr] = val;
		break;
	case 0xb3:
		if (dev->type > 3)
			fregs[addr] = val & 0xfb;
		break;
	case 0xcb:
		if (dev->type == 4) {
			fregs[addr] = val & 0x3d;

			nvr_at_handler(0, 0x0070, dev->nvr);
			nvr_at_handler(0, 0x0072, dev->nvr);

			if ((val & 0x01) && (dev->regs[2][0xff] & 0x10))
				nvr_at_handler(1, 0x0070, dev->nvr);
			if (val & 0x04)
				nvr_at_handler(1, 0x0072, dev->nvr);

			nvr_wp_set(!!(val & 0x08), 0, dev->nvr);
			nvr_wp_set(!!(val & 0x10), 1, dev->nvr);
		}
		break;
    } else if (func == 1)  switch(addr) {	/* IDE */
	case 0x04:
		fregs[0x04] = (val & 5);
		if (dev->type < 3)
			fregs[0x04] |= 0x02;
		piix_ide_legacy_handlers(dev, 0x03);
		piix_ide_bm_handlers(dev);
		break;
	case 0x07:
		if (val & 0x20)
			fregs[0x07] &= 0xdf;
		if (val & 0x10)
			fregs[0x07] &= 0xef;
		if (val & 0x08)
			fregs[0x07] &= 0xf7;
		break;
	case 0x0d:
		fregs[0x0d] = val & 0xf0;
		break;
	case 0x20:
		fregs[0x20] = (val & 0xf0) | 1;
		piix_ide_bm_handlers(dev);
		break;
	case 0x21:
		fregs[0x21] = val;
		piix_ide_bm_handlers(dev);
		break;
	case 0x3c:
		piix_log("IDE IRQ write: %02X\n", val);
		fregs[0x3c] = val;
		break;
	case 0x40: case 0x42:
		fregs[addr] = val;
		break;
	case 0x41: case 0x43:
		fregs[addr] = val & ((dev->type > 1) ? 0xf3 : 0xb3);
		piix_ide_legacy_handlers(dev, 1 << !!(addr & 0x02));
		break;
	case 0x44:
		if (dev->type > 1)
			fregs[0x44] = val;
		break;
	case 0x48:
		if (dev->type > 3)
			fregs[0x48] = val & 0x0f;
		break;
	case 0x4a: case 0x4b:
		if (dev->type > 4)
			fregs[addr] = val & 0x33;
		break;
    } else if (func == 2)  switch(addr) {	/* USB */
	case 0x04:
		fregs[0x04] = (val & 5);
		usb_update_io_mapping(dev);
		break;
	case 0x07:
		if (val & 0x20)
			fregs[0x07] &= 0xdf;
		if (val & 0x10)
			fregs[0x07] &= 0xef;
		if (val & 0x08)
			fregs[0x07] &= 0xf7;
		break;
	case 0x0d:
		fregs[0x0d] = val & 0xf0;
		break;
	case 0x20:
		fregs[0x20] = (val & 0xe0) | 1;
		usb_update_io_mapping(dev);
		break;
	case 0x21:
		fregs[0x21] = val;
		usb_update_io_mapping(dev);
		break;
	case 0x3c:
		fregs[0x3c] = val;
		break;
	case 0x6a:
		if (dev->type < 4)
			fregs[0x6a] = val & 0x01;
		break;
	case 0xc0:
		fregs[0xc0] = val;
		break;
	case 0xc1:
		fregs[0xc1] = val & 0xbf;
		break;
	case 0xff:
		if (dev->type >= 4) {
			fregs[addr] = val & 0x10;
			nvr_at_handler(0, 0x0070, dev->nvr);
			if ((dev->regs[0][0xcb] & 0x01) && (dev->regs[2][0xff] & 0x10))
				nvr_at_handler(1, 0x0070, dev->nvr);
		}
		break;
    } else if (func == 3)  switch(addr) {	/* Power Management */
	case 0x04:
		fregs[0x04] = (val & 1);
		power_update_io_mapping(dev);
		smbus_update_io_mapping(dev);
		break;
	case 0x07:
		if (val & 0x08)
			fregs[0x07] &= 0xf7;
		break;
#if 0
	case 0x3c:
		fregs[0x3c] = val;
		break;
#endif
	case 0x40:
		fregs[0x40] = (val & 0xc0) | 1;
		power_update_io_mapping(dev);
		break;
	case 0x41:
		fregs[0x41] = val;
		power_update_io_mapping(dev);
		break;
	case 0x44: case 0x45: case 0x46: case 0x47:
	case 0x48: case 0x49:
	case 0x4c: case 0x4d: case 0x4e:
	case 0x54: case 0x55: case 0x56: case 0x57:
	case 0x59: case 0x5a:
	case 0x5c: case 0x5d: case 0x5e: case 0x5f:
	case 0x60: case 0x61: case 0x62:
	case 0x64: case 0x65:
	case 0x67: case 0x68: case 0x69:
	case 0x6c: case 0x6e: case 0x6f:
	case 0x70: case 0x71:
	case 0x74: case 0x77: case 0x78: case 0x79:
	case 0x7c: case 0x7d:
	case 0xd3: case 0xd4:
	case 0xd5:
		fregs[addr] = val;
		break;
	case 0x4a:
		fregs[addr] = val & 0x73;
		break;
	case 0x4b:
		fregs[addr] = val & 0x01;
		break;
	case 0x4f: case 0x80: case 0xd2:
		fregs[addr] = val & 0x0f;
		if (addr == 0x80)
			power_update_io_mapping(dev);
		else if (addr == 0xd2)
			smbus_update_io_mapping(dev);
		break;
	case 0x50:
		fregs[addr] = val & 0x3f;
		break;
	case 0x51:
		fregs[addr] = val & 0x58;
		break;
	case 0x52:
		fregs[addr] = val & 0x7f;
		break;
	case 0x58:
		fregs[addr] = val & 0x77;
		break;
	case 0x5b:
		fregs[addr] = val & 0x03;
		break;
	case 0x63:
		fregs[addr] = val & 0xf7;
		break;
	case 0x66:
		fregs[addr] = val & 0xef;
		break;
	case 0x6a: case 0x72: case 0x7a: case 0x7e:
		fregs[addr] = val & 0x1f;
		break;
	case 0x6d: case 0x75:
		fregs[addr] = val & 0x80;
		break;
	case 0x90:
		fregs[0x90] = (val & 0xf0) | 1;
		smbus_update_io_mapping(dev);
		break;
	case 0x91:
		fregs[0x91] = val;
		smbus_update_io_mapping(dev);
		break;
    }
}


static uint8_t
piix_read(int func, int addr, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    uint8_t ret = 0xff, *fregs;

    /* Return on unsupported function. */
    if (func <= dev->max_func) {
    	fregs = (uint8_t *) dev->regs[func];
	ret = fregs[addr];

	piix_log("PIIX function %i read: %02X from %02X\n", func, ret, addr);
    }

    return ret;
}


static void
board_write(uint16_t port, uint8_t val, void *priv)
{
    piix_t *dev = (piix_t *) priv;

    if (port == 0x0078)
	dev->board_config[0] = val;
    else if (port == 0x0079)
	dev->board_config[1] = val;
    else if (port == 0x00e0)
	dev->cur_readout_reg = val;
    else if (port == 0x00e1)
	dev->readout_regs[dev->cur_readout_reg] = val;
}


static uint8_t
board_read(uint16_t port, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    uint8_t ret = 0x64;

    if (port == 0x0078)
	ret = dev->board_config[0];
    else if (port == 0x0079)
	ret = dev->board_config[1];
    else if (port == 0x00e0)
	ret = dev->cur_readout_reg;
    else if (port == 0x00e1)
	ret = dev->readout_regs[dev->cur_readout_reg];

    return ret;
}


static void
piix_reset_hard(piix_t *dev)
{
    int i;
    uint8_t *fregs;

    uint16_t old_base = (dev->regs[1][0x20] & 0xf0) | (dev->regs[1][0x21] << 8);

    /* Type 0 is the PB640's PIIX without IDE. */
    if (dev->type > 0) {
	sff_bus_master_reset(dev->bm[0], old_base);
	sff_bus_master_reset(dev->bm[1], old_base + 8);

	if (dev->type == 4) {
		sff_set_irq_mode(dev->bm[0], 0);
		sff_set_irq_mode(dev->bm[1], 0);
	}

#ifdef ENABLE_PIIX_LOG
        piix_log("piix_reset_hard()\n");
#endif
	ide_pri_disable();
	ide_sec_disable();
    }

    if (dev->type > 3) {
	nvr_at_handler(0, 0x0072, dev->nvr);
	nvr_wp_set(0, 0, dev->nvr);
	nvr_wp_set(0, 1, dev->nvr);
	nvr_at_handler(1, 0x0074, dev->nvr);
	nvr_at_handler(1, 0x0076, dev->nvr);
    }

    /* Clear all 4 functions' arrays and set their vendor and device ID's. */
    for (i = 0; i < 4; i++) {
	memset(dev->regs[i], 0, 256);
	dev->regs[i][0x00] = 0x86; dev->regs[i][0x01] = 0x80;		/* Intel */
	dev->regs[i][0x02] = (dev->func0_id & 0xff) + (i << dev->func_shift);
	dev->regs[i][0x03] = (dev->func0_id >> 8);
    }

    /* Function 0: PCI to ISA Bridge */
    fregs = (uint8_t *) dev->regs[0];
    piix_log("PIIX Function 0: 8086:%02X%02X\n", fregs[0x03], fregs[0x02]);
    fregs[0x04] = (dev->type > 0) ? 0x07 : 0x06;	/* Check the value for the PB640 PIIX. */
    fregs[0x06] = 0x80; fregs[0x07] = 0x02;
    fregs[0x08] = (dev->type > 0) ? 0x00 : 0x02;	/* Should normal PIIX alos return 0x02? */
    fregs[0x09] = 0x00;
    fregs[0x0a] = 0x01; fregs[0x0b] = 0x06;
    fregs[0x0e] = (dev->type > 0) ? 0x80 : 0x00;
    fregs[0x4c] = 0x4d;
    fregs[0x4e] = 0x03;
    fregs[0x60] = fregs[0x61] = fregs[0x62] = fregs[0x63] = 0x80;
    fregs[0x64] = (dev->type > 3) ? 0x10 : 0x00;
    fregs[0x69] = 0x02;
    fregs[0x70] = (dev->type < 4) ? 0x80 : 0x00;
    fregs[0x71] = (dev->type < 3) ? 0x80 : 0x00;
    fregs[0x76] = fregs[0x77] = (dev->type > 1) ? 0x04 : 0x0c;
    fregs[0x78] = (dev->type < 4) ? 0x02 : 0x00;
    fregs[0xa0] = (dev->type < 4) ? 0x08 : 0x00;
    fregs[0xa8] = (dev->type < 4) ? 0x0f : 0x00;
    if (dev->type == 4)
	fregs[0xb0] = (is_pentium) ? 0x00 : 0x04;
    fregs[0xcb] = (dev->type > 3) ? 0x21 : 0x00;
    dev->max_func = 0;

    /* Function 1: IDE */
    if (dev->type > 0) {
	fregs = (uint8_t *) dev->regs[1];
	piix_log("PIIX Function 1: 8086:%02X%02X\n", fregs[0x03], fregs[0x02]);
	fregs[0x04] = (dev->type > 3) ? 0x05 : 0x07;
	fregs[0x06] = 0x80; fregs[0x07] = 0x02;
	fregs[0x09] = 0x80;
	fregs[0x0a] = 0x01; fregs[0x0b] = 0x01;
	fregs[0x20] = 0x01;
	dev->max_func = 1;
    }

    /* Function 2: USB */
    if (dev->type > 2) {
	fregs = (uint8_t *) dev->regs[2];
	piix_log("PIIX Function 2: 8086:%02X%02X\n", fregs[0x03], fregs[0x02]);
	fregs[0x04] = 0x05;
	fregs[0x06] = 0x80; fregs[0x07] = 0x02;
	fregs[0x0a] = 0x03; fregs[0x0b] = 0x0c;
	fregs[0x20] = 0x01;
	fregs[0x3d] = 0x04;
	fregs[0x60] = (dev->type > 3) ? 0x10: 0x00;
	fregs[0x6a] = (dev->type == 3) ? 0x01 : 0x00;
	fregs[0xc1] = 0x20;
	fregs[0xff] = (dev->type > 3) ? 0x10 : 0x00;
	dev->max_func = 2;
    }

    /* Function 3: Power Management */
    if (dev->type > 3) {
	fregs = (uint8_t *) dev->regs[3];	
	piix_log("PIIX Function 3: 8086:%02X%02X\n", fregs[0x03], fregs[0x02]);
	fregs[0x06] = 0x80; fregs[0x07] = 0x02;
	fregs[0x0a] = 0x80; fregs[0x0b] = 0x06;
	/* NOTE: The Specification Update says this should default to 0x00 and be read-only. */
#ifdef WRONG_SPEC
	fregs[0x3d] = 0x01;
#endif
	fregs[0x40] = 0x01;
	fregs[0x90] = 0x01;
	dev->max_func = 3;
    }

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    if (dev->type < 4)
	pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
    if (dev->type < 3)
	pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);

    if (dev->type == 4) {
	dev->power.gporeg[0] = 0xff;
	dev->power.gporeg[1] = 0xbf;
	dev->power.gporeg[2] = 0xff;
	dev->power.gporeg[3] = 0x7f;
    }
}


static void
piix_close(void *p)
{
    piix_t *piix = (piix_t *)p;

    free(piix);
}


static void
*piix_init(const device_t *info)
{
    int i;
    CPU *cpu_s = &machines[machine].cpu[cpu_manufacturer].cpus[cpu];

    piix_t *dev = (piix_t *) malloc(sizeof(piix_t));
    memset(dev, 0, sizeof(piix_t));

    dev->type = info->local & 0xff;
    dev->func_shift = info->local >> 8;
    dev->func0_id = info->local >> 16;

    dev->pci_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, piix_read, piix_write, dev);
    piix_log("PIIX%i: Added to slot: %02X\n", dev->type, dev->pci_slot);

    if (dev->type > 0) {	/* PB640's PIIX has no IDE part. */
	dev->bm[0] = device_add_inst(&sff8038i_device, 1);
	dev->bm[1] = device_add_inst(&sff8038i_device, 2);
    }

    if (dev->type > 3)
	dev->nvr = device_add(&piix4_nvr_device);

    piix_reset_hard(dev);

    device_add(&apm_device);
    device_add(&port_92_pci_device);

    dma_alias_set();

    if (dev->type < 4)
	pci_enable_mirq(0);
    if (dev->type < 3)
	pci_enable_mirq(1);

    dev->readout_regs[1] = 0x40;

    /* Port E1 register 01 (TODO: Find how multipliers > 3.0 are defined):

	Bit 6: 1 = can boot, 0 = no;
	Bit 7, 1 = multiplier (00 = 2.5, 01 = 2.0, 10 = 3.0, 11 = 1.5);
	Bit 5, 4 = bus speed (00 = 50 MHz, 01 = 66 MHz, 10 = 60 MHz, 11 = ????):
	Bit 7, 5, 4, 1: 0000 = 125 MHz, 0010 = 166 MHz, 0100 = 150 MHz, 0110 = ??? MHz;
		        0001 = 100 MHz, 0011 = 133 MHz, 0101 = 120 MHz, 0111 = ??? MHz;
		        1000 = 150 MHz, 1010 = 200 MHz, 1100 = 180 MHz, 1110 = ??? MHz;
		        1001 =  75 MHz, 1011 = 100 MHz, 1101 =  90 MHz, 1111 = ??? MHz */

    if (cpu_busspeed <= 0x40000000)
		dev->readout_regs[1] |= 0x30;
    else if ((cpu_busspeed > 0x40000000) && (cpu_busspeed <= 0x50000000))
		dev->readout_regs[1] |= 0x00;
    else if ((cpu_busspeed > 0x50000000) && (cpu_busspeed <= 0x60000000))
		dev->readout_regs[1] |= 0x20;
    else if (cpu_busspeed > 0x60000000)
		dev->readout_regs[1] |= 0x10;

    if (cpu_dmulti <= 1.5)
	dev->readout_regs[1] |= 0x82;
    else if ((cpu_dmulti > 1.5) && (cpu_dmulti <= 2.0))
	dev->readout_regs[1] |= 0x02;
    else if ((cpu_dmulti > 2.0) && (cpu_dmulti <= 2.5))
	dev->readout_regs[1] |= 0x00;
    else if (cpu_dmulti > 2.5)
	dev->readout_regs[1] |= 0x80;

    io_sethandler(0x0078, 0x0002, board_read, NULL, NULL, board_write, NULL, NULL, dev);
    io_sethandler(0x00e0, 0x0002, board_read, NULL, NULL, board_write, NULL, NULL, dev);

    dev->board_config[0] = 0xff;
    /* Register 0x0079: */
    /* Bit 7: 0 = Keep password, 0 = Clear password. */
    /* Bit 6: 0 = NVRAM cleared by jumper, 1 = NVRAM normal. */
    /* Bit 5: 0 = CMOS Setup disabled, 1 = CMOS Setup enabled. */
    /* Bit 4: External CPU clock (Switch 8). */
    /* Bit 3: External CPU clock (Switch 7). */
    /*		50 MHz: Switch 7 = Off, Switch 8 = Off. */
    /*		60 MHz: Switch 7 = On, Switch 8 = Off. */
    /*		66 MHz: Switch 7 = Off, Switch 8 = On. */
    /* Bit 2: 0 = On-board audio absent, 1 = On-board audio present. */
    /* Bit 0: 0 = 1.5x multiplier, 0 = 2x multiplier. */
    dev->board_config[1] = 0xe0;
    if ((cpu_s->rspeed == 75000000) && (cpu_busspeed == 50000000))
	dev->board_config[1] |= 0x01;
    else if ((cpu_s->rspeed == 90000000) && (cpu_busspeed == 60000000))
	dev->board_config[1] |= (0x01 | 0x08);
    else if ((cpu_s->rspeed == 100000000) && (cpu_busspeed == 50000000))
	dev->board_config[1] |= 0x00;
    else if ((cpu_s->rspeed == 100000000) && (cpu_busspeed == 66666666))
	dev->board_config[1] |= (0x01 | 0x10);
    else if ((cpu_s->rspeed == 120000000) && (cpu_busspeed == 60000000))
	dev->board_config[1] |= 0x08;
    else if ((cpu_s->rspeed == 133333333) && (cpu_busspeed == 66666666))
	dev->board_config[1] |= 0x10;
    else
	dev->board_config[1] |= 0x10;	/* TODO: how are the overdrive processors configured? */

    smbus_init();
    dev->smbus.stat = 0;
    dev->smbus.ctl = 0;
    dev->smbus.cmd = 0;
    dev->smbus.addr = 0;
    dev->smbus.data0 = 0;
    dev->smbus.data1 = 0;
    dev->smbus.index = 0;
    for (i = 0; i < 32; i++)
    	dev->smbus.data[i] = 0;
    timer_add(&dev->smbus.command_timer, smbus_inter, dev, 0);

    return dev;
}


const device_t piix_pb640_device =
{
    "Intel 82371FB (PIIX) (PB640)",
    DEVICE_PCI,
    0x122e0100,
    piix_init, 
    piix_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t piix_device =
{
    "Intel 82371FB (PIIX)",
    DEVICE_PCI,
    0x122e0101,
    piix_init, 
    piix_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t piix3_device =
{
    "Intel 82371SB (PIIX3)",
    DEVICE_PCI,
    0x70000403,
    piix_init, 
    piix_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t piix4_device =
{
    "Intel 82371AB/EB (PIIX4/PIIX4E)",
    DEVICE_PCI,
    0x71100004,
    piix_init, 
    piix_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

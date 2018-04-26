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
 * Version:	@(#)intel_piix.c	1.0.15	2018/04/26
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "scsi/scsi.h"
#include "cdrom/cdrom.h"
#include "dma.h"
#include "io.h"
#include "device.h"
#include "keyboard.h"
#include "mem.h"
#include "pci.h"
#include "pic.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "disk/zip.h"
#include "piix.h"


typedef struct
{
    uint8_t	command, status,
		ptr0;
    uint32_t	ptr, ptr_cur,
		addr;
    int		count, eot;
} piix_busmaster_t;

typedef struct
{
    uint8_t		type,
			regs[256], regs_ide[256];
    piix_busmaster_t	bm[2];
} piix_t;


static uint8_t	piix_bus_master_read(uint16_t port, void *priv);
static uint16_t	piix_bus_master_readw(uint16_t port, void *priv);
static uint32_t	piix_bus_master_readl(uint16_t port, void *priv);
static void	piix_bus_master_write(uint16_t port, uint8_t val, void *priv);
static void	piix_bus_master_writew(uint16_t port, uint16_t val, void *priv);
static void	piix_bus_master_writel(uint16_t port, uint32_t val, void *priv);


#ifdef ENABLE_PIIX_LOG
int piix_do_log = ENABLE_PIIX_LOG;
#endif


static void
piix_log(const char *format, ...)
{
#ifdef ENABLE_PIIX_LOG
    va_list ap;

    if (piix_do_log) {
	va_start(ap, format);
	pclog_ex(format, ap);
	va_end(ap);
    }
#endif
}


static void
piix_bus_master_handlers(piix_t *dev, uint16_t old_base)
{
    uint16_t base;

    base = (dev->regs_ide[0x20] & 0xf0) | (dev->regs_ide[0x21] << 8);
    io_removehandler(old_base, 0x08,
		     piix_bus_master_read, piix_bus_master_readw, piix_bus_master_readl,
		     piix_bus_master_write, piix_bus_master_writew, piix_bus_master_writel,
		     &dev->bm[0]);
    io_removehandler(old_base + 8, 0x08,
		     piix_bus_master_read, piix_bus_master_readw, piix_bus_master_readl,
		     piix_bus_master_write, piix_bus_master_writew, piix_bus_master_writel,
		     &dev->bm[1]);

    if ((dev->regs_ide[0x04] & 1) && base) {
	io_sethandler(base, 0x08,
		      piix_bus_master_read, piix_bus_master_readw, piix_bus_master_readl,
		      piix_bus_master_write, piix_bus_master_writew, piix_bus_master_writel,
		      &dev->bm[0]);
	io_sethandler(base + 8, 0x08,
		      piix_bus_master_read, piix_bus_master_readw, piix_bus_master_readl,
		      piix_bus_master_write, piix_bus_master_writew, piix_bus_master_writel,
		      &dev->bm[1]);
    }
}


static void
piix_write(int func, int addr, uint8_t val, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    uint8_t valxor;

    uint16_t old_base = (dev->regs_ide[0x20] & 0xf0) | (dev->regs_ide[0x21] << 8);
    if (func > 1)
	return;

    if (func == 1) {	/*IDE*/
	piix_log("PIIX IDE write: %02X %02X\n", addr, val);
	valxor = val ^ dev->regs_ide[addr];

	switch (addr) {
		case 0x04:
			dev->regs_ide[0x04] = (val & 5) | 2;
			if (valxor & 0x01) {
				ide_pri_disable();
				ide_sec_disable();
				if (val & 0x01) {
					if (dev->regs_ide[0x41] & 0x80)
						ide_pri_enable();
					if (dev->regs_ide[0x43] & 0x80)
						ide_sec_enable();
				}

				piix_bus_master_handlers(dev, old_base);
			}
			break;
		case 0x07:
			dev->regs_ide[0x07] = val & 0x3e;
			break;
		case 0x0d:
			dev->regs_ide[0x0d] = val;
			break;

		case 0x20:
			dev->regs_ide[0x20] = (val & ~0x0f) | 1;
			if (valxor)
				piix_bus_master_handlers(dev, old_base);
			break;
		case 0x21:
			dev->regs_ide[0x21] = val;
			if (valxor)
				piix_bus_master_handlers(dev, old_base);
			break;

		case 0x40:
			dev->regs_ide[0x40] = val;
			break;
		case 0x41:
			dev->regs_ide[0x41] = val;
			if (valxor & 0x80) {
				ide_pri_disable();
				if ((val & 0x80) && (dev->regs_ide[0x04] & 0x01))
					ide_pri_enable();
			}
			break;
		case 0x42:
			dev->regs_ide[0x42] = val;
			break;
		case 0x43:
			dev->regs_ide[0x43] = val;
			if (valxor & 0x80) {
				ide_sec_disable();
				if ((val & 0x80) && (dev->regs_ide[0x04] & 0x01))
					ide_sec_enable();
			}
			break;
		case 0x44:
			if (dev->type >= 3)  dev->regs_ide[0x44] = val;
			break;
	}
    } else {
	piix_log("PIIX writing value %02X to register %02X\n", val, addr);
	valxor = val ^ dev->regs[addr];

	if ((addr >= 0x0f) && (addr < 0x4c))
		return;

	switch (addr) {
		case 0x00: case 0x01: case 0x02: case 0x03:
		case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x0e:
			return;

		case 0x4c:
			if (valxor) {
				if (val & 0x80) {
					if (dev->type == 3)
						dma_alias_remove();
					else
						dma_alias_remove_piix();
				} else
					dma_alias_set();
			}
			break;
		case 0x4e:
			keyboard_at_set_mouse_scan((val & 0x10) ? 1 : 0);
			break;
		case 0x60:
			piix_log("Set IRQ routing: INT A -> %02X\n", val);
			if (val & 0x80)
				pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
			else
				pci_set_irq_routing(PCI_INTA, val & 0xf);
			break;
		case 0x61:
			piix_log("Set IRQ routing: INT B -> %02X\n", val);
			if (val & 0x80)
				pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
			else
				pci_set_irq_routing(PCI_INTB, val & 0xf);
			break;
		case 0x62:
			piix_log("Set IRQ routing: INT C -> %02X\n", val);
			if (val & 0x80)
				pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
			else
				pci_set_irq_routing(PCI_INTC, val & 0xf);
			break;
		case 0x63:
			piix_log("Set IRQ routing: INT D -> %02X\n", val);
			if (val & 0x80)
				pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
			else
				pci_set_irq_routing(PCI_INTD, val & 0xf);
			break;
		case 0x6a:
			if (dev->type == 1)
				dev->regs[addr] = (val & 0xFC) | (dev->regs[addr] | 3);
			else if (dev->type == 3)
				dev->regs[addr] = (val & 0xFD) | (dev->regs[addr] | 2);
			return;
		case 0x70:
			piix_log("Set MIRQ routing: MIRQ0 -> %02X\n", val);
			if (val & 0x80)
				pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
			else
				pci_set_mirq_routing(PCI_MIRQ0, val & 0xf);
			break;
			piix_log("MIRQ0 is %s\n", (val & 0x20) ? "disabled" : "enabled");
		case 0x71:
			if (dev->type == 1) {
				piix_log("Set MIRQ routing: MIRQ1 -> %02X\n", val);
				if (val & 0x80)
					pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
				else
					pci_set_mirq_routing(PCI_MIRQ1, val & 0xf);
			}
			break;
	}

	dev->regs[addr] = val;
    }
}


static uint8_t
piix_read(int func, int addr, void *priv)
{
    piix_t *dev = (piix_t *) priv;

    if (func > 1)
	return 0xff;

    if (func == 1) {	/*IDE*/
	if (addr == 4)
		return (dev->regs_ide[addr] & 5) | 2;
	else if (addr == 5)
		return 0;
	else if (addr == 6)
		return 0x80;
	else if (addr == 7)
		return dev->regs_ide[addr] & 0x3E;
	else if (addr == 0xD)
		return dev->regs_ide[addr] & 0xF0;
	else if (addr == 0x20)
		return (dev->regs_ide[addr] & 0xF0) | 1;
	else if (addr == 0x22)
		return 0;
	else if (addr == 0x23)
		return 0;
	else if (addr == 0x41) {
		if (dev->type == 1)
			return dev->regs_ide[addr] & 0xB3;
		else if (dev->type == 3)
			return dev->regs_ide[addr] & 0xF3;
	} else if (addr == 0x43) {
		if (dev->type == 1)
			return dev->regs_ide[addr] & 0xB3;
		else if (dev->type == 3)
			return dev->regs_ide[addr] & 0xF3;
	} else
               	return dev->regs_ide[addr];
    } else {
	if ((addr & 0xFC) == 0x60)
		return dev->regs[addr] & 0x8F;

	if (addr == 4)
		return (dev->regs[addr] & 0x80) | 7;
	else if (addr == 5) {
		if (dev->type == 1)
			return 0;
		else if (dev->type == 3)
			return dev->regs[addr] & 1;
	} else if (addr == 6)
			return dev->regs[addr] & 0x80;
	else if (addr == 7) {
		if (dev->type == 1)
			return dev->regs[addr] & 0x3E;
		else if (dev->type == 3)
			return dev->regs[addr];
	} else if (addr == 0x4E)
		return (dev->regs[addr] & 0xEF) | keyboard_at_get_mouse_scan();
	else if (addr == 0x69)
		return dev->regs[addr] & 0xFE;
	else if (addr == 0x6A) {
		if (dev->type == 1)
			return dev->regs[addr] & 0x07;
		else if (dev->type == 3)
			return dev->regs[addr] & 0xD1;
	} else if (addr == 0x6B) {
		if (dev->type == 1)
			return 0;
		else if (dev->type == 3)
			return dev->regs[addr] & 0x80;
	}
	else if (addr == 0x70) {
		if (dev->type == 1)
			return dev->regs[addr] & 0xCF;
		else if (dev->type == 3)
			return dev->regs[addr] & 0xEF;
	} else if (addr == 0x71) {
		if (dev->type == 1)
			return dev->regs[addr] & 0xCF;
		else if (dev->type == 3)
			return 0;
	} else if (addr == 0x76) {
		if (dev->type == 1)
			return dev->regs[addr] & 0x8F;
		else if (dev->type == 3)
			return dev->regs[addr] & 0x87;
	} else if (addr == 0x77) {
		if (dev->type == 1)
			return dev->regs[addr] & 0x8F;
		else if (dev->type == 3)
			return dev->regs[addr] & 0x87;
	} else if (addr == 0x80) {
		if (dev->type == 1)
			return 0;
		else if (dev->type == 3)
			return dev->regs[addr] & 0x7F;
	} else if (addr == 0x82) {
		if (dev->type == 1)
			return 0;
		else if (dev->type == 3)
			return dev->regs[addr] & 0x0F;
	} else if (addr == 0xA0)
		return dev->regs[addr] & 0x1F;
	else if (addr == 0xA3) {
		if (dev->type == 1)
			return 0;
		else if (dev->type == 3)
			return dev->regs[addr] & 1;
	} else if (addr == 0xA7) {
		if (dev->type == 1)
			return dev->regs[addr] & 0xEF;
		else if (dev->type == 3)
			return dev->regs[addr];
	} else if (addr == 0xAB) {
		if (dev->type == 1)
			return dev->regs[addr] & 0xFE;
		else if (dev->type == 3)
			return dev->regs[addr];
	} else
		return dev->regs[addr];
    }

    return 0;
}


static void
piix_bus_master_next_addr(piix_busmaster_t *dev)
{
    DMAPageRead(dev->ptr_cur, (uint8_t *)&(dev->addr), 4);
    DMAPageRead(dev->ptr_cur + 4, (uint8_t *)&(dev->count), 4);
    piix_log("PIIX Bus master DWORDs: %08X %08X\n", dev->addr, dev->count);
    dev->eot = dev->count >> 31;
    dev->count &= 0xfffe;
    if (!dev->count)
	dev->count = 65536;
    dev->addr &= 0xfffffffe;
    dev->ptr_cur += 8;
}


static void
piix_bus_master_write(uint16_t port, uint8_t val, void *priv)
{
    piix_busmaster_t *dev = (piix_busmaster_t *) priv;
    int channel = (port & 8) ? 1 : 0;

    piix_log("PIIX Bus master BYTE  write: %04X       %02X\n", port, val);

    switch (port & 7) {
	case 0:
		piix_log("PIIX Cmd   : val = %02X, old = %02X\n", val, dev->command);
		if ((val & 1) && !(dev->command & 1)) {	/*Start*/
			piix_log("PIIX Bus Master start on channel %i\n", channel);
			dev->ptr_cur = dev->ptr;
			piix_bus_master_next_addr(dev);
			dev->status |= 1;
		}
		if (!(val & 1) && (dev->command & 1)) {	/*Stop*/
			piix_log("PIIX Bus Master stop on channel %i\n", channel);
			dev->status &= ~1;
		}

		dev->command = val;
		break;
	case 2:
		piix_log("PIIX Status: val = %02X, old = %02X\n", val, dev->status);
		dev->status &= 0x07;
		dev->status |= (val & 0x60);
		if (val & 0x04)
			dev->status &= ~0x04;
		if (val & 0x02)
			dev->status &= ~0x02;
		break;
	case 4:
		dev->ptr = (dev->ptr & 0xffffff00) | (val & 0xfc);
		dev->ptr %= (mem_size * 1024);
		dev->ptr0 = val;
		break;
	case 5:
		dev->ptr = (dev->ptr & 0xffff00fc) | (val << 8);
		dev->ptr %= (mem_size * 1024);
		break;
	case 6:
		dev->ptr = (dev->ptr & 0xff00fffc) | (val << 16);
		dev->ptr %= (mem_size * 1024);
		break;
	case 7:
		dev->ptr = (dev->ptr & 0x00fffffc) | (val << 24);
		dev->ptr %= (mem_size * 1024);
		break;
    }
}


static void
piix_bus_master_writew(uint16_t port, uint16_t val, void *priv)
{
    piix_busmaster_t *dev = (piix_busmaster_t *) priv;

    piix_log("PIIX Bus master WORD  write: %04X     %04X\n", port, val);

    switch (port & 7) {
	case 0:
	case 2:
		piix_bus_master_write(port, val & 0xff, priv);
		break;
	case 4:
                dev->ptr = (dev->ptr & 0xffff0000) | (val & 0xfffc);
		dev->ptr %= (mem_size * 1024);
		dev->ptr0 = val & 0xff;
                break;
	case 6:
		dev->ptr = (dev->ptr & 0x0000fffc) | (val << 16);
		dev->ptr %= (mem_size * 1024);
		break;
    }
}


static void
piix_bus_master_writel(uint16_t port, uint32_t val, void *priv)
{
    piix_busmaster_t *dev = (piix_busmaster_t *) priv;

    piix_log("PIIX Bus master DWORD write: %04X %08X\n", port, val);

    switch (port & 7) {
	case 0:
	case 2:
		piix_bus_master_write(port, val & 0xff, priv);
		break;
	case 4:
                dev->ptr = (val & 0xfffffffc);
		dev->ptr %= (mem_size * 1024);
		dev->ptr0 = val & 0xff;
                break;
    }
}


static uint8_t
piix_bus_master_read(uint16_t port, void *priv)
{
    piix_busmaster_t *dev = (piix_busmaster_t *) priv;

    uint8_t ret = 0xff;

    switch (port & 7) {
	case 0:
		ret = dev->command;
		break;
	case 2:
		ret = dev->status & 0x67;
		break;
	case 4:
		ret = dev->ptr0;
		break;
	case 5:
		ret = dev->ptr >> 8;
		break;
	case 6:
		ret = dev->ptr >> 16;
		break;
	case 7:
		ret = dev->ptr >> 24;
		break;
    }

    piix_log("PIIX Bus master BYTE  read : %04X       %02X\n", port, ret);

    return ret;
}


static uint16_t
piix_bus_master_readw(uint16_t port, void *priv)
{
    piix_busmaster_t *dev = (piix_busmaster_t *) priv;

    uint16_t ret = 0xffff;

    switch (port & 7) {
	case 0:
	case 2:
		ret = (uint16_t) piix_bus_master_read(port, priv);
		break;
	case 4:
		ret = dev->ptr0 | (dev->ptr & 0xff00);
		break;
	case 6:
		ret = dev->ptr >> 16;
		break;
    }

    piix_log("PIIX Bus master WORD  read : %04X     %04X\n", port, ret);

    return ret;
}


static uint32_t
piix_bus_master_readl(uint16_t port, void *priv)
{
    piix_busmaster_t *dev = (piix_busmaster_t *) priv;

    uint32_t ret = 0xffffffff;

    switch (port & 7) {
	case 0:
	case 2:
		ret = (uint32_t) piix_bus_master_read(port, priv);
		break;
	case 4:
		ret = dev->ptr0 | (dev->ptr & 0xffffff00);
		break;
    }

    piix_log("PIIX Bus master DWORD read : %04X %08X\n", port, ret);

    return ret;
}


static int
piix_bus_master_dma_op(int channel, uint8_t *data, int transfer_length, int out, void *priv)
{
    piix_busmaster_t *dev = (piix_busmaster_t *) priv;
    char *sop;

    int force_end = 0, buffer_pos = 0;

    sop = out ? "Writ" : "Read";

    if (!(dev->status & 1))
	return 2;                                    /*DMA disabled*/

    piix_log("PIIX Bus master %s: %i bytes\n", out ? "read" : "write", transfer_length);

    while (1) {
	if (dev->count <= transfer_length) {
		piix_log("%sing %i bytes to %08X\n", sop, dev->count, dev->addr);
		if (out)
			DMAPageWrite(dev->addr, (uint8_t *)(data + buffer_pos), dev->count);
		else
			DMAPageRead(dev->addr, (uint8_t *)(data + buffer_pos), dev->count);
		transfer_length -= dev->count;
		buffer_pos += dev->count;
	} else {
		piix_log("%sing %i bytes to %08X\n", sop, transfer_length, dev->addr);
		if (out)
			DMAPageWrite(dev->addr, (uint8_t *)(data + buffer_pos), transfer_length);
		else
			DMAPageRead(dev->addr, (uint8_t *)(data + buffer_pos), transfer_length);
		/* Increase addr and decrease count so that resumed transfers do not mess up. */
		dev->addr += transfer_length;
		dev->count -= transfer_length;
		transfer_length = 0;
		force_end = 1;
	}

	if (force_end) {
		piix_log("Total transfer length smaller than sum of all blocks, partial block\n");
		dev->status &= ~2;
		return 0;		/* This block has exhausted the data to transfer and it was smaller than the count, break. */
	} else {
		if (!transfer_length && !dev->eot) {
			piix_log("Total transfer length smaller than sum of all blocks, full block\n");
			dev->status &= ~2;
			return 0;	/* We have exhausted the data to transfer but there's more blocks left, break. */
		} else if (transfer_length && dev->eot) {
			piix_log("Total transfer length greater than sum of all blocks\n");
			dev->status |= 2;
			return 1;	/* There is data left to transfer but we have reached EOT - return with error. */
		} else if (dev->eot) {
			piix_log("Regular EOT\n");
			dev->status &= ~3;
			return 0;	/* We have regularly reached EOT - clear status and break. */
		} else {
			/* We have more to transfer and there are blocks left, get next block. */
			piix_bus_master_next_addr(dev);
		}
	}
    }
    return 0;
}


int
piix_bus_master_dma_read(int channel, uint8_t *data, int transfer_length, void *priv)
{
    return piix_bus_master_dma_op(channel, data, transfer_length, 1, priv);
}


int
piix_bus_master_dma_write(int channel, uint8_t *data, int transfer_length, void *priv)
{
    return piix_bus_master_dma_op(channel, data, transfer_length, 0, priv);
}


void
piix_bus_master_set_irq(int channel, void *priv)
{
    piix_busmaster_t *dev = (piix_busmaster_t *) priv;
    dev->status &= ~4;
    dev->status |= (channel >> 4);

    channel &= 0x01;
    if (dev->status & 0x04) {
	if (channel && pci_use_mirq(0))
		pci_set_mirq(0);
	else
		picint(1 << (14 + channel));
    } else {
	if ((channel & 1) && pci_use_mirq(0))
		pci_clear_mirq(0);
	else
		picintc(1 << (14 + channel));
    }
}


static void
piix_bus_master_reset(piix_t *dev)
{
    uint8_t i;

    uint16_t old_base = (dev->regs_ide[0x20] & 0xf0) | (dev->regs_ide[0x21] << 8);
    if (old_base) {
	io_removehandler(old_base, 0x08,
			 piix_bus_master_read, piix_bus_master_readw, piix_bus_master_readl,
			 piix_bus_master_write, piix_bus_master_writew, piix_bus_master_writel,
			 &dev->bm[0]);
	io_removehandler(old_base + 8, 0x08,
			 piix_bus_master_read, piix_bus_master_readw, piix_bus_master_readl,
			 piix_bus_master_write, piix_bus_master_writew, piix_bus_master_writel,
			 &dev->bm[1]);
    }

    for (i = 0; i < 2; i++) {
	dev->bm[i].command = 0x00;
	dev->bm[i].status = 0x00;
	dev->bm[i].ptr = dev->bm[i].ptr_cur = 0x00000000;
	dev->bm[i].addr = 0x00000000;
	dev->bm[i].ptr0 = 0x00;
	dev->bm[i].count = dev->bm[i].eot = 0x00000000;
    }
}


static void
piix_reset_hard(void *priv)
{
    piix_t *piix = (piix_t *) priv;

    piix_bus_master_reset(piix);

    memset(piix->regs, 0, 256);
    memset(piix->regs_ide, 0, 256);

    piix->regs[0x00] = 0x86; piix->regs[0x01] = 0x80; /*Intel*/
    if (piix->type == 3) {
	piix->regs[0x02] = 0x00; piix->regs[0x03] = 0x70; /*82371SB (PIIX3)*/
    } else {
	piix->regs[0x02] = 0x2e; piix->regs[0x03] = 0x12; /*82371FB (PIIX)*/
    }
    piix->regs[0x04] = 0x07; piix->regs[0x05] = 0x00;
    piix->regs[0x06] = 0x80; piix->regs[0x07] = 0x02;
    piix->regs[0x08] = 0x00; /*A0 stepping*/
    piix->regs[0x09] = 0x00; piix->regs[0x0a] = 0x01; piix->regs[0x0b] = 0x06;
    piix->regs[0x0e] = 0x80; /*Multi-function device*/
    piix->regs[0x4c] = 0x4d;
    piix->regs[0x4e] = 0x03;
    if (piix->type == 3)
	piix->regs[0x4f] = 0x00;
    piix->regs[0x60] = piix->regs[0x61] = piix->regs[0x62] = piix->regs[0x63] = 0x80;
    piix->regs[0x69] = 0x02;
    piix->regs[0x70] = 0xc0;
    if (piix->type != 3)
	piix->regs[0x71] = 0xc0;
    piix->regs[0x76] = piix->regs[0x77] = 0x0c;
    piix->regs[0x78] = 0x02; piix->regs[0x79] = 0x00;
    if (piix->type == 3) {
	piix->regs[0x80] = piix->regs[0x82] = 0x00;
    }
    piix->regs[0xa0] = 0x08;
    piix->regs[0xa2] = piix->regs[0xa3] = 0x00;
    piix->regs[0xa4] = piix->regs[0xa5] = piix->regs[0xa6] = piix->regs[0xa7] = 0x00;
    piix->regs[0xa8] = 0x0f;
    piix->regs[0xaa] = piix->regs[0xab] = 0x00;
    piix->regs[0xac] = 0x00;
    piix->regs[0xae] = 0x00;

    piix->regs_ide[0x00] = 0x86; piix->regs_ide[0x01] = 0x80; /*Intel*/
    if (piix->type == 3) {
	piix->regs_ide[0x02] = 0x10; piix->regs_ide[0x03] = 0x70; /*82371SB (PIIX3)*/
    } else {
	piix->regs_ide[0x02] = 0x30; piix->regs_ide[0x03] = 0x12; /*82371FB (PIIX)*/
    }
    piix->regs_ide[0x04] = 0x03; piix->regs_ide[0x05] = 0x00;
    piix->regs_ide[0x06] = 0x80; piix->regs_ide[0x07] = 0x02;
    piix->regs_ide[0x08] = 0x00;
    piix->regs_ide[0x09] = 0x80; piix->regs_ide[0x0a] = 0x01; piix->regs_ide[0x0b] = 0x01;
    piix->regs_ide[0x0d] = 0x00;
    piix->regs_ide[0x0e] = 0x00;
    piix->regs_ide[0x20] = 0x01; piix->regs_ide[0x21] = piix->regs_ide[0x22] = piix->regs_ide[0x23] = 0x00; /*Bus master interface base address*/
    piix->regs_ide[0x40] = piix->regs_ide[0x42] = 0x00;
    piix->regs_ide[0x41] = piix->regs_ide[0x43] = 0x80;
    if (piix->type == 3)
	piix->regs_ide[0x44] = 0x00;

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
    if (piix->type != 3)
	pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);

    ide_pri_disable();
    ide_sec_disable();

    ide_pri_enable();
    ide_sec_enable();
}


static void
piix_reset(void *p)
{
    int i = 0;

    for (i = 0; i < CDROM_NUM; i++) {
	if (cdrom_drives[i].bus_type == CDROM_BUS_ATAPI)
		cdrom_reset(cdrom[i]);
    }
    for (i = 0; i < ZIP_NUM; i++) {
	if (zip_drives[i].bus_type == ZIP_BUS_ATAPI)
		zip_reset(i);
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
    piix_t *piix = (piix_t *) malloc(sizeof(piix_t));
    memset(piix, 0, sizeof(piix_t));

    device_add(&ide_pci_2ch_device);

    pci_add_card(7, piix_read, piix_write, piix);

    piix->type = info->local;
    piix_reset_hard(piix);

    ide_set_bus_master(piix_bus_master_dma_read, piix_bus_master_dma_write,
		       piix_bus_master_set_irq,
		       &piix->bm[0], &piix->bm[1]);

    port_92_reset();

    port_92_add();

    dma_alias_set();

    pci_enable_mirq(0);
    pci_enable_mirq(1);

    return piix;
}


const device_t piix_device =
{
    "Intel 82371FB (PIIX)",
    DEVICE_PCI,
    1,
    piix_init, 
    piix_close, 
    piix_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t piix3_device =
{
    "Intel 82371SB (PIIX3)",
    DEVICE_PCI,
    1,
    piix_init, 
    piix_close, 
    piix_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

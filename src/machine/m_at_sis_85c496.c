/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SiS 85c496/85c497 chip.
 *
 * Version:	@(#)m_at_sis_85c496.c	1.0.3	2018/11/05
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../keyboard.h"
#include "../io.h"
#include "../pci.h"
#include "../mem.h"
#include "../memregs.h"
#include "../sio.h"
#include "../disk/hdc.h"
#include "machine.h"


typedef struct sis_85c496_t
{
    uint8_t cur_reg,
	    regs[39],
	    pci_conf[256];
} sis_85c496_t;


static void
sis_85c497_write(uint16_t port, uint8_t val, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;

    if (index) {
	if ((val >= 0x50) && (val <= 0x76))
		dev->cur_reg = val;
	return;
    } else {
	if ((dev->cur_reg < 0x50) || (dev->cur_reg > 0x76))
		return;
	/* Writes to 0x52 are blocked as otherwise, large hard disks don't read correctly. */
	if (dev->cur_reg != 0x52)
		dev->regs[dev->cur_reg - 0x50] = val;
    }

    dev->cur_reg = 0;
}


static uint8_t
sis_85c497_read(uint16_t port, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t ret = 0xff;

    if (index)
	ret = dev->cur_reg;
    else {
	if ((dev->cur_reg >= 0x50) && (dev->cur_reg <= 0x76)) {
		ret = dev->regs[dev->cur_reg - 0x50];
		dev->cur_reg = 0;
	}
    }

    return ret;
}


static void
sis_85c496_recalcmapping(sis_85c496_t *dev)
{
    int c;
    uint32_t base;

    for (c = 0; c < 8; c++) {
	base = 0xc0000 + (c << 15);
	if (dev->pci_conf[0x44] & (1 << c)) {
		switch (dev->pci_conf[0x45] & 3) {
			case 0:
				mem_set_mem_state(base, 0x8000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
				break;
			case 1:
				mem_set_mem_state(base, 0x8000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
				break;
			case 2:
				mem_set_mem_state(base, 0x8000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
				break;
			case 3:
				mem_set_mem_state(base, 0x8000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
				break;
		}
	} else
		mem_set_mem_state(base, 0x8000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
    }

    flushmmucache();
    shadowbios = (dev->pci_conf[0x44] & 0xf0);
}


static void
sis_85c496_write(int func, int addr, uint8_t val, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;

    switch (addr) {
	case 0x44: /*Shadow configure*/
		if ((dev->pci_conf[0x44] & val) ^ 0xf0) {
			dev->pci_conf[0x44] = val;
			sis_85c496_recalcmapping(dev);
		}
		break;
	case 0x45: /*Shadow configure*/
		if ((dev->pci_conf[0x45] & val) ^ 0x01) {
			dev->pci_conf[0x45] = val;
			sis_85c496_recalcmapping(dev);
		}
		break;

	case 0x82:
		sis_85c497_write(0x22, val, priv);
		break;

	case 0xc0:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTA, val & 0xf);
		else
			pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
		break;
	case 0xc1:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTB, val & 0xf);
		else
			pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
		break;
	case 0xc2:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTC, val & 0xf);
		else
			pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
		break;
	case 0xc3:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTD, val & 0xf);
		else
			pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
		break;
    }
  
    if ((addr >= 4 && addr < 8) || addr >= 0x40)
	dev->pci_conf[addr] = val;
}


static uint8_t
sis_85c496_read(int func, int addr, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;

    return dev->pci_conf[addr];
}
 

static void
sis_85c497_reset(sis_85c496_t *dev)
{
    int mem_size_mb, i = 0;

    memset(dev->regs, 0, sizeof(dev->regs));

    dev->cur_reg = 0;
    for (i = 0; i < 0x27; i++)
	dev->regs[i] = 0x00;

    dev->regs[9] = 0x40;

    mem_size_mb = mem_size >> 10;
    switch (mem_size_mb) {
	case 0: case 1:
		dev->regs[9] |= 0;
		break;
	case 2: case 3:
		dev->regs[9] |= 1;
		break;
	case 4:
		dev->regs[9] |= 2;
		break;
	case 5:
		dev->regs[9] |= 0x20;
		break;
	case 6: case 7:
		dev->regs[9] |= 9;
		break;
	case 8: case 9:
		dev->regs[9] |= 4;
		break;
	case 10: case 11:
		dev->regs[9] |= 5;
		break;
	case 12: case 13: case 14: case 15:
		dev->regs[9] |= 0xB;
		break;
	case 16:
		dev->regs[9] |= 0x13;
		break;
	case 17:
		dev->regs[9] |= 0x21;
		break;
	case 18: case 19:
		dev->regs[9] |= 6;
		break;
	case 20: case 21: case 22: case 23:
		dev->regs[9] |= 0xD;
		break;
	case 24: case 25: case 26: case 27:
	case 28: case 29: case 30: case 31:
		dev->regs[9] |= 0xE;
		break;
	case 32: case 33: case 34: case 35:
		dev->regs[9] |= 0x1B;
		break;
	case 36: case 37: case 38: case 39:
		dev->regs[9] |= 0xF;
		break;
	case 40: case 41: case 42: case 43:
	case 44: case 45: case 46: case 47:
		dev->regs[9] |= 0x17;
		break;
	case 48:
		dev->regs[9] |= 0x1E;
		break;
	default:
		if (mem_size_mb < 64)
			dev->regs[9] |= 0x1E;
		else if ((mem_size_mb >= 65) && (mem_size_mb < 68))
			dev->regs[9] |= 0x22;
		else
			dev->regs[9] |= 0x24;
		break;
    }

    dev->regs[0x11] = 9;
    dev->regs[0x12] = 0xFF;
    dev->regs[0x23] = 0xF0;
    dev->regs[0x26] = 1;

    io_removehandler(0x0022, 0x0002,
		     sis_85c497_read, NULL, NULL, sis_85c497_write, NULL, NULL, dev);
    io_sethandler(0x0022, 0x0002,
		  sis_85c497_read, NULL, NULL, sis_85c497_write, NULL, NULL, dev);
}


static void
sis_85c496_reset(void *priv)
{
    uint8_t val = 0;

    val = sis_85c496_read(0, 0x44, priv);	/* Read current value of 0x44. */
    sis_85c496_write(0, 0x44, val & 0xf, priv);	/* Turn off shadow BIOS but keep the lower 4 bits. */

    sis_85c497_reset((sis_85c496_t *) priv);
}


static void
sis_85c496_close(void *p)
{
    sis_85c496_t *sis_85c496 = (sis_85c496_t *)p;

    free(sis_85c496);
}


static void
*sis_85c496_init(const device_t *info)
{
    sis_85c496_t *dev = malloc(sizeof(sis_85c496_t));
    memset(dev, 0, sizeof(sis_85c496_t));

    dev->pci_conf[0x00] = 0x39; /*SiS*/
    dev->pci_conf[0x01] = 0x10; 
    dev->pci_conf[0x02] = 0x96; /*496/497*/
    dev->pci_conf[0x03] = 0x04; 

    dev->pci_conf[0x04] = 7;
    dev->pci_conf[0x05] = 0;

    dev->pci_conf[0x06] = 0x80;
    dev->pci_conf[0x07] = 0x02;

    dev->pci_conf[0x08] = 2; /*Device revision*/

    dev->pci_conf[0x09] = 0x00; /*Device class (PCI bridge)*/
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;

    dev->pci_conf[0x0e] = 0x00; /*Single function device*/

    pci_add_card(5, sis_85c496_read, sis_85c496_write, dev);

    sis_85c497_reset(dev);

    return dev;
}


const device_t sis_85c496_device =
{
    "SiS 85c496/85c497",
    DEVICE_PCI,
    0,
    sis_85c496_init, 
    sis_85c496_close, 
    sis_85c496_reset,
    NULL,
    NULL,
    NULL,
    NULL
};


static void
machine_at_sis_85c496_common_init(const machine_t *model)
{
    machine_at_common_init(model);
    device_add(&keyboard_ps2_pci_device);

    device_add(&ide_pci_device);

    memregs_init();
    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x05, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_NORMAL, 4, 1, 2, 3);

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    device_add(&sis_85c496_device);
}


void
machine_at_r418_init(const machine_t *model)
{
    machine_at_sis_85c496_common_init(model);

    device_add(&fdc37c665_device);
}

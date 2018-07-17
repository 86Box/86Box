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
 * Version:	@(#)m_at_sis_85c496.c	1.0.1	2018/04/26
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
#include "../cpu/cpu.h"
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
    uint8_t pci_conf[256];
} sis_85c496_t;


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
sis_85c496_write(int func, int addr, uint8_t val, void *p)
{
    sis_85c496_t *dev = (sis_85c496_t *) p;

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
sis_85c496_read(int func, int addr, void *p)
{
    sis_85c496_t *dev = (sis_85c496_t *) p;

    return dev->pci_conf[addr];
}
 

static void
sis_85c496_reset(void *priv)
{
    uint8_t val = 0;

    val = sis_85c496_read(0, 0x44, priv);	/* Read current value of 0x44. */
    sis_85c496_write(0, 0x44, val & 0xf, priv);	/* Turn off shadow BIOS but keep the lower 4 bits. */
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
    sis_85c496_t *sis496 = malloc(sizeof(sis_85c496_t));
    memset(sis496, 0, sizeof(sis_85c496_t));

    sis496->pci_conf[0x00] = 0x39; /*SiS*/
    sis496->pci_conf[0x01] = 0x10; 
    sis496->pci_conf[0x02] = 0x96; /*496/497*/
    sis496->pci_conf[0x03] = 0x04; 

    sis496->pci_conf[0x04] = 7;
    sis496->pci_conf[0x05] = 0;

    sis496->pci_conf[0x06] = 0x80;
    sis496->pci_conf[0x07] = 0x02;

    sis496->pci_conf[0x08] = 2; /*Device revision*/

    sis496->pci_conf[0x09] = 0x00; /*Device class (PCI bridge)*/
    sis496->pci_conf[0x0a] = 0x00;
    sis496->pci_conf[0x0b] = 0x06;

    sis496->pci_conf[0x0e] = 0x00; /*Single function device*/

    pci_add_card(5, sis_85c496_read, sis_85c496_write, sis496);

    return sis496;
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

    fdc37c665_init();
}

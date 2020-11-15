/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the VIA VT82C505 VL/PCI Bridge Controller.
 *
 *
 *
 * Authors:	Tiseno100,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020 Tiseno100.
 *		Copyright 2020 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/chipset.h>


typedef struct vt82c505_t
{
    uint8_t pci_conf[256];
} vt82c505_t;


static void
vt82c505_write(int func, int addr, uint8_t val, void *priv)
{

    vt82c505_t *dev = (vt82c505_t *) priv;

    /* Read-Only Registers */
    switch (addr) {
	case 0x00: case 0x01:
	case 0x02: case 0x03:
		return;
    }

    switch(addr) {
	case 0x04:
		dev->pci_conf[0x04] = (dev->pci_conf[0x04] & ~0x07) | (val & 0x07);
		break;

	case 0x07:
		dev->pci_conf[0x07] &= ~(val & 0x90);
		break;

        case 0x90:
		if ((dev->pci_conf[0x90] & 0x08) && ((val & 0x07) != 0))
			pci_set_irq_routing(PCI_INTC, val & 0x07);
		else
			pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);

		if ((dev->pci_conf[0x90] & 0x80) && (((val & 0x07) << 4) != 0))
			pci_set_irq_routing(PCI_INTD, ((val & 0x07) << 4));
		else
			pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
		break;

	case 0x91:
		if ((dev->pci_conf[0x91] & 0x08) && ((val & 0x07) != 0))
			pci_set_irq_routing(PCI_INTA, val & 0x07);
		else
			pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);

		if ((dev->pci_conf[0x91] & 0x80) && (((val & 0x07) << 4) != 0))
			pci_set_irq_routing(PCI_INTB, ((val & 0x07) << 4));
		else
			pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
		break;
    }
}


static uint8_t
vt82c505_read(int func, int addr, void *priv)
{
    vt82c505_t *dev = (vt82c505_t *) priv;
    uint8_t ret = 0xff;

    ret = dev->pci_conf[addr];

    return ret;
}


static void
vt82c505_reset(void *priv)
{
    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    pic_reset();
}


static void
vt82c505_close(void *priv)
{
    vt82c505_t *dev = (vt82c505_t *) priv;

    free(dev);
}


static void *
vt82c505_init(const device_t *info)
{
    vt82c505_t *dev = (vt82c505_t *) malloc(sizeof(vt82c505_t));
    memset(dev, 0, sizeof(vt82c505_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, vt82c505_read, vt82c505_write, dev);

    dev->pci_conf[0x00] = 0x06;
    dev->pci_conf[0x01] = 0x11;

    dev->pci_conf[0x02] = 0x05;
    dev->pci_conf[0x03] = 0x05;

    dev->pci_conf[0x04] = 0x07;

    dev->pci_conf[0x07] = 0x90;

    dev->pci_conf[0x81] = 0x01;
    dev->pci_conf[0x84] = 0x03;
    
    dev->pci_conf[0x93] = 0x40;

    return dev;
}


const device_t via_vt82c505_device = {
    "VIA VT82C505",
    DEVICE_PCI,
    0,
    vt82c505_init,
    vt82c505_close,
    vt82c505_reset,
    { NULL },
    NULL,
    NULL,
    NULL
};

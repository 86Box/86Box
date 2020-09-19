/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of PCI-PCI and host-AGP bridges.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/machine.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/pci.h>


#define PCI_BRIDGE_DEC_21150	0x10110022
#define AGP_BRIDGE_INTEL_440LX	0x80867181
#define AGP_BRIDGE_INTEL_440BX	0x80867191
#define AGP_BRIDGE_INTEL_440GX	0x808671a1
#define AGP_BRIDGE_VIA_597	0x11068597
#define AGP_BRIDGE_VIA_598	0x11068598
#define AGP_BRIDGE_VIA_691	0x11068691

#define AGP_BRIDGE_VIA(x)	(((x) >> 4) == 0x1106)
#define AGP_BRIDGE(x)		((x) >= AGP_BRIDGE_VIA_597)


typedef struct
{
    uint32_t	local;
    uint8_t	type;

    uint8_t	regs[256];
    uint8_t	bus_index;
    int 	slot;
} pci_bridge_t;


#ifdef ENABLE_PCI_BRIDGE_LOG
int pci_bridge_do_log = ENABLE_PCI_BRIDGE_LOG;


static void
pci_bridge_log(const char *fmt, ...)
{
    va_list ap;

    if (pci_bridge_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define pci_bridge_log(fmt, ...)
#endif


static void
pci_bridge_write(int func, int addr, uint8_t val, void *priv)
{
    pci_bridge_t *dev = (pci_bridge_t *) priv;

    pci_bridge_log("PCI Bridge %d: write(%d, %02X, %02X)\n", dev->bus_index, func, addr, val);

    if (func > 0)
	return;

    switch (addr) {
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x06: case 0x07: case 0x08: case 0x09:
	case 0x0a: case 0x0b: case 0x0e: case 0x1e:
	case 0x1f: case 0x34: case 0x3d: case 0x67:
	case 0xdc: case 0xdd: case 0xde: case 0xdf:
	case 0xe0: case 0xe1: case 0xe2: case 0xe3:
		return;

	case 0x04:
		val &= 0x67;
		break;

	case 0x05:
		val &= 0x03;
		break;

	case 0x18:
		/* Parent bus number is always 0 on AGP bridges. */
		if (AGP_BRIDGE(dev->local))
			return;
		break;

	case 0x19:
		/* Set our bus number. */
		pci_bridge_log("PCI Bridge %d: switching from bus %02X to %02X\n", dev->bus_index, dev->regs[addr], val);
		if (dev->regs[addr])
			pci_bus_number_to_index_mapping[dev->regs[addr]] = 0xff;
		if (val)
			pci_bus_number_to_index_mapping[val] = dev->bus_index;
		break;

	case 0x1c: case 0x1d: case 0x20: case 0x22:
	case 0x24: case 0x26:
		val &= 0xf0;
		break;

	case 0x3e:
		if (AGP_BRIDGE_VIA(dev->local))
			val &= 0x0c;
		else if (AGP_BRIDGE(dev->local))
			val &= 0x0f;
		else if (dev->local == PCI_BRIDGE_DEC_21150)
			val &= 0xef;
		break;

	case 0x3f:
		if (dev->local == AGP_BRIDGE_INTEL_440LX)
			val &= 0x02;
		else if (AGP_BRIDGE(dev->local))
			return;
		else if (dev->local == PCI_BRIDGE_DEC_21150)
			val &= 0x0f;
		break;

	case 0x40:
		if (dev->local == PCI_BRIDGE_DEC_21150)
			val &= 0x32;
		break;

	case 0x41:
		if (AGP_BRIDGE_VIA(dev->local))
			val &= 0x7e;
		else if (dev->local == PCI_BRIDGE_DEC_21150)
			val &= 0x07;
		break;

	case 0x42:
		if (AGP_BRIDGE_VIA(dev->local))
			val &= 0xfe;
		break;

	case 0x43:
		if (dev->local == PCI_BRIDGE_DEC_21150)
			val &= 0x03;
		break;

	case 0x64:
		if (dev->local == PCI_BRIDGE_DEC_21150)
			val &= 0x7e;
		break;

	case 0x69:
		if (dev->local == PCI_BRIDGE_DEC_21150)
			val &= 0x3f;
		break;
    }

    dev->regs[addr] = val;
}


static uint8_t
pci_bridge_read(int func, int addr, void *priv)
{
    pci_bridge_t *dev = (pci_bridge_t *) priv;
    uint8_t ret;

    if (func > 0)
	ret = 0xff;
    else
	ret = dev->regs[addr];

    pci_bridge_log("PCI Bridge %d: read(%d, %02X) = %02X\n", dev->bus_index, func, addr, ret);
    return ret;
}


static void
pci_bridge_reset(void *priv)
{
    pci_bridge_t *dev = (pci_bridge_t *) priv;

    pci_bridge_log("PCI Bridge %d: reset()\n", dev->bus_index);

    memset(dev->regs, 0, sizeof(dev->regs));

    /* IDs */
    dev->regs[0x00] = dev->local >> 16;
    dev->regs[0x01] = dev->local >> 24;
    dev->regs[0x02] = dev->local;
    dev->regs[0x03] = dev->local >> 8;

    switch (dev->local) {
	case PCI_BRIDGE_DEC_21150:
		dev->regs[0x06] = 0x80;
		dev->regs[0x07] = 0x02;
		break;

	case AGP_BRIDGE_INTEL_440LX:
		dev->regs[0x06] = 0xa0;
		dev->regs[0x07] = 0x02;
		dev->regs[0x08] = 0x03;
		break;

	case AGP_BRIDGE_INTEL_440BX:
	case AGP_BRIDGE_INTEL_440GX:
		dev->regs[0x06] = 0x20;
		dev->regs[0x07] = dev->regs[0x08] = 0x02;
		break;

	case AGP_BRIDGE_VIA_597:
	case AGP_BRIDGE_VIA_691:
		dev->regs[0x04] = 0x07;
		dev->regs[0x06] = 0x20;
		dev->regs[0x07] = 0x02;
		break;
    }

    dev->regs[0x0a] = 0x04; /* PCI-PCI bridge */
    dev->regs[0x0b] = 0x06; /* bridge device */

    dev->regs[0x0e] = 0x01;

    /* IO BARs */
    if (AGP_BRIDGE(dev->local)) {
	dev->regs[0x1c] = 0xf0;
    } else {
	dev->regs[0x1c] = dev->regs[0x1d] = 0x01;
    }

    if (!AGP_BRIDGE_VIA(dev->local)) {
	dev->regs[0x1e] = AGP_BRIDGE(dev->local) ? 0xa0 : 0x80;
	dev->regs[0x1f] = 0x02;
    }

    /* prefetchable memory limits */
    if (AGP_BRIDGE(dev->local)) {
	dev->regs[0x20] = dev->regs[0x24] = 0xf0;
	dev->regs[0x21] = dev->regs[0x25] = 0xff;
    } else {
	dev->regs[0x24] = dev->regs[0x26] = 0x01;
    }

    if (dev->local == AGP_BRIDGE_INTEL_440LX)
	dev->regs[0x3e] = 0x80;

    if (dev->local == PCI_BRIDGE_DEC_21150) {
    	dev->regs[0x34] = 0xdc;
	dev->regs[0x43] = 0x02;
	dev->regs[0xdc] = dev->regs[0xde] = 0x01;
    }
}


static void *
pci_bridge_init(const device_t *info)
{
    uint8_t interrupts[4], interrupt_count, interrupt_mask, slot_count, i;

    pci_bridge_t *dev = (pci_bridge_t *) malloc(sizeof(pci_bridge_t));
    memset(dev, 0, sizeof(pci_bridge_t));

    dev->local = info->local;
    dev->bus_index = last_pci_bus++;
    pci_bridge_log("PCI Bridge %d: init()\n", dev->bus_index);

    pci_bridge_reset(dev);

    dev->slot = pci_add_card(AGP_BRIDGE(dev->local) ? 0x01 : PCI_ADD_BRIDGE, pci_bridge_read, pci_bridge_write, dev);
    interrupt_count = sizeof(interrupts);
    interrupt_mask = interrupt_count - 1;
    for (i = 0; i < interrupt_count; i++)
	interrupts[i] = pci_get_int(dev->slot, PCI_INTA + i);
    pci_bridge_log("PCI Bridge %d: upstream bus %02X slot %02X interrupts %02X %02X %02X %02X\n", dev->bus_index, (dev->slot >> 5) & 0xff, dev->slot & 31, interrupts[0], interrupts[1], interrupts[2], interrupts[3]);

    if (info->local == PCI_BRIDGE_DEC_21150)
	slot_count = 9; /* 9 bus masters */
    else
	slot_count = 1; /* AGP bridges always have 1 slot */

    for (i = 0; i < slot_count; i++) {
	/* Interrupts for bridge slots are assigned in round-robin: ABCD, BCDA, CDAB and so on. */
	pci_bridge_log("PCI Bridge %d: downstream slot %02X interrupts %02X %02X %02X %02X\n", dev->bus_index, i, interrupts[i & interrupt_mask], interrupts[(i + 1) & interrupt_mask], interrupts[(i + 2) & interrupt_mask], interrupts[(i + 3) & interrupt_mask]);
	pci_register_bus_slot(dev->bus_index, i, /*AGP_BRIDGE(dev->local) ? PCI_CARD_SPECIAL : */PCI_CARD_NORMAL,
			      interrupts[i & interrupt_mask],
			      interrupts[(i + 1) & interrupt_mask],
			      interrupts[(i + 2) & interrupt_mask],
			      interrupts[(i + 3) & interrupt_mask]);
    }

    return dev;
}


/* PCI bridges */
const device_t dec21150_device =
{
    "DEC 21150 PCI Bridge",
    DEVICE_PCI,
    PCI_BRIDGE_DEC_21150,
    pci_bridge_init,
    NULL,
    pci_bridge_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

/* AGP bridges */
const device_t i440lx_agp_device =
{
    "Intel 82443LX AGP Bridge",
    DEVICE_PCI,
    AGP_BRIDGE_INTEL_440LX,
    pci_bridge_init,
    NULL,
    pci_bridge_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t i440bx_agp_device =
{
    "Intel 82443BX AGP Bridge",
    DEVICE_PCI,
    AGP_BRIDGE_INTEL_440BX,
    pci_bridge_init,
    NULL,
    pci_bridge_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t i440gx_agp_device =
{
    "Intel 82443GX AGP Bridge",
    DEVICE_PCI,
    AGP_BRIDGE_INTEL_440GX,
    pci_bridge_init,
    NULL,
    pci_bridge_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t via_vp3_agp_device =
{
    "VIA Apollo VP3 AGP Bridge",
    DEVICE_PCI,
    AGP_BRIDGE_VIA_597,
    pci_bridge_init,
    NULL,
    pci_bridge_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t via_mvp3_agp_device =
{
    "VIA Apollo MVP3 AGP Bridge",
    DEVICE_PCI,
    AGP_BRIDGE_VIA_598,
    pci_bridge_init,
    NULL,
    pci_bridge_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t via_apro_agp_device =
{
    "VIA Apollo Pro AGP Bridge",
    DEVICE_PCI,
    AGP_BRIDGE_VIA_691,
    pci_bridge_init,
    NULL,
    pci_bridge_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

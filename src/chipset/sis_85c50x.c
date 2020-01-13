/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SiS 85c501/85c503 chip.
 *
 * Version:	@(#)sis_85c50x.c	1.0.1	2019/10/19
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../mem.h"
#include "../io.h"
#include "../rom.h"
#include "../pci.h"
#include "../device.h"
#include "../keyboard.h"
#include "../port_92.h"
#include "chipset.h"


typedef struct sis_85c501_t
{
    /* 85c501 */
    uint8_t turbo_reg;

    /* 85c503 */

    /* Registers */
    uint8_t pci_conf[2][256];

    /* 85c50x ISA */
    uint8_t cur_reg,
	    regs[39];
} sis_85c50x_t;


static void
sis_85c501_recalcmapping(sis_85c50x_t *dev)
{
    int c, d;
    uint32_t base;

    for (c = 0; c < 1; c++) {
	for (d = 0; d < 4; d++) {
		base = 0xe0000 + (d << 14);
		if (dev->pci_conf[0][0x54 + c] & (1 << (d + 4))) {
			switch (dev->pci_conf[0][0x53] & 0x60) {
				case 0x00:
					mem_set_mem_state(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
					break;
				case 0x20:
					mem_set_mem_state(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
					break;
				case 0x40:
					mem_set_mem_state(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
					break;
				case 0x60:
					mem_set_mem_state(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
					break;
			}
		} else
			mem_set_mem_state(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
	}
    }

    flushmmucache();
    shadowbios = 1;
}


static void
sis_85c501_write(int func, int addr, uint8_t val, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    if (func)
	return;

    if ((addr >= 0x10) && (addr < 0x4f))
	return;

    switch (addr) {
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x08: case 0x09: case 0x0a: case 0x0b:
	case 0x0c: case 0x0e:
		return;

	case 0x04: /*Command register*/
		val &= 0x42;
		val |= 0x04;
		break;
	case 0x05:
		val &= 0x01;
		break;

	case 0x06: /*Status*/
		val = 0;
		break;
	case 0x07:
		val = 0x02;
		break;

	case 0x54: /*Shadow configure*/
		if ((dev->pci_conf[0][0x54] & val) ^ 0xf0) {
			dev->pci_conf[0][0x54] = val;
			sis_85c501_recalcmapping(dev);
		}
		break;
    }

    dev->pci_conf[0][addr] = val;
}


static void
sis_85c503_write(int func, int addr, uint8_t val, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    if (func > 0)
	return;

    if (addr >= 0x0f && addr < 0x41)
	return;

    switch(addr) {
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x08: case 0x09: case 0x0a: case 0x0b:
	case 0x0e:
		return;

	case 0x04: /*Command register*/
		val &= 0x08;
		val |= 0x07;
		break;
	case 0x05:
		val = 0;
		break;

	case 0x06: /*Status*/
		val = 0;
		break;
	case 0x07:
		val = 0x02;
		break;

	case 0x41:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
		else
			pci_set_irq_routing(PCI_INTA, val & 0xf);
		break;
	case 0x42:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
		else
			pci_set_irq_routing(PCI_INTC, val & 0xf);
		break;
	case 0x43:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
		else
			pci_set_irq_routing(PCI_INTB, val & 0xf);
		break;
	case 0x44:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
		else
			pci_set_irq_routing(PCI_INTD, val & 0xf);
		break;
    }

    dev->pci_conf[1][addr] = val;
}


static void
sis_85c50x_isa_write(uint16_t port, uint8_t val, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    if (port & 1) {
	if (dev->cur_reg <= 0x1a) 
		dev->regs[dev->cur_reg] = val;
    } else
	dev->cur_reg = val;
}


static uint8_t
sis_85c501_read(int func, int addr, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    if (func)
	return 0xff;

    return dev->pci_conf[0][addr];
}


static uint8_t
sis_85c503_read(int func, int addr, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    if (func > 0)
	return 0xff;

    return dev->pci_conf[1][addr];
}


static uint8_t
sis_85c50x_isa_read(uint16_t port, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    if (port & 1) {
	if (dev->cur_reg <= 0x1a)
		return dev->regs[dev->cur_reg];
	else
		return 0xff;
    } else
	return dev->cur_reg;
}


static void
sis_85c50x_isa_reset(sis_85c50x_t *dev)
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

    io_removehandler(0x22, 0x0002,
		     sis_85c50x_isa_read, NULL, NULL, sis_85c50x_isa_write, NULL, NULL, dev);
    io_sethandler(0x22, 0x0002,
		  sis_85c50x_isa_read, NULL, NULL, sis_85c50x_isa_write, NULL, NULL, dev);
}


static void
sis_85c50x_reset(void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    uint8_t val = 0;

    val = sis_85c501_read(0, 0x54, priv);	/* Read current value of 0x44. */
    sis_85c501_write(0, 0x54, val & 0xf, priv);	/* Turn off shadow BIOS but keep the lower 4 bits. */

    sis_85c50x_isa_reset(dev);
}


static void
sis_85c50x_setup(sis_85c50x_t *dev)
{
    memset(dev, 0, sizeof(sis_85c50x_t));

    /* 85c501 */
    dev->pci_conf[0][0x00] = 0x39; /*SiS*/
    dev->pci_conf[0][0x01] = 0x10; 
    dev->pci_conf[0][0x02] = 0x06; /*501/502*/
    dev->pci_conf[0][0x03] = 0x04; 

    dev->pci_conf[0][0x04] = 7;
    dev->pci_conf[0][0x05] = 0;

    dev->pci_conf[0][0x06] = 0x80;
    dev->pci_conf[0][0x07] = 0x02;

    dev->pci_conf[0][0x08] = 0; /*Device revision*/

    dev->pci_conf[0][0x09] = 0x00; /*Device class (PCI bridge)*/
    dev->pci_conf[0][0x0a] = 0x00;
    dev->pci_conf[0][0x0b] = 0x06;

    dev->pci_conf[0][0x0e] = 0x00; /*Single function device*/

    dev->pci_conf[0][0x50] = 0xbc;
    dev->pci_conf[0][0x51] = 0xfb;
    dev->pci_conf[0][0x52] = 0xad;
    dev->pci_conf[0][0x53] = 0xfe;

    shadowbios = 1;

    /* 85c503 */
    dev->pci_conf[1][0x00] = 0x39; /*SiS*/
    dev->pci_conf[1][0x01] = 0x10; 
    dev->pci_conf[1][0x02] = 0x08; /*503*/
    dev->pci_conf[1][0x03] = 0x00; 

    dev->pci_conf[1][0x04] = 7;
    dev->pci_conf[1][0x05] = 0;

    dev->pci_conf[1][0x06] = 0x80;
    dev->pci_conf[1][0x07] = 0x02;

    dev->pci_conf[1][0x08] = 0; /*Device revision*/

    dev->pci_conf[1][0x09] = 0x00; /*Device class (PCI bridge)*/
    dev->pci_conf[1][0x0a] = 0x01;
    dev->pci_conf[1][0x0b] = 0x06;

    dev->pci_conf[1][0x0e] = 0x00; /*Single function device*/

    dev->pci_conf[1][0x41] = dev->pci_conf[1][0x42] =
    dev->pci_conf[1][0x43] = dev->pci_conf[1][0x44] = 0x80;
}


static void
sis_85c50x_close(void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    free(dev);
}


static void *
sis_85c50x_init(const device_t *info)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) malloc(sizeof(sis_85c50x_t));

    pci_add_card(0, sis_85c501_read, sis_85c501_write, dev);
    pci_add_card(5, sis_85c503_read, sis_85c503_write, dev);

    sis_85c50x_setup(dev);
    sis_85c50x_isa_reset(dev);

    device_add(&port_92_pci_device);

    return dev;
}


const device_t sis_85c50x_device =
{
    "SiS 85c501/85c503",
    DEVICE_PCI,
    0,
    sis_85c50x_init, 
    sis_85c50x_close, 
    sis_85c50x_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

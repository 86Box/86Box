/*
*
*  86Box:	A hypervisor and IBM PC system emulator that specializes in
* 		    running old operating systems and software designed for IBM
* 		    PC systems and compatibles from 1981 through fairly recent
* 		    system designs based on the PCI bus.
* 
*        <This file is part of the 86Box distribution.>
*
* Basic AMD 640 North Bridge emulation
*
*  Looks similar to the VIA VP2
*  while it's southbridge(AMD-645) is just a 586B [TODO: Probs write it if it has differences]
*
* Copyright(C) 2020 Tiseno100
*
*/


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/chipset.h>

typedef struct amd640_t {
    uint8_t regs[256];
} amd640_t;

static void
amd640_map(uint32_t addr, uint32_t size, int state)
{
    switch (state & 3) {
	case 0:
		mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
		break;
	case 1:
		mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
		break;
	case 2:
		mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
		break;
	case 3:
		mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		break;
    }

    flushmmucache_nopc();
}

static void
amd640_write(int func, int addr, uint8_t val, void *priv)
{
    amd640_t *dev = (amd640_t *) priv;

    /* Read-Only Registers */
    switch(addr){
        case 0x00: case 0x01: case 0x08: case 0x09:
        case 0x0a: case 0x0b: case 0x0c: case 0x0e:
        case 0x0f:
        return;
    }

    switch(addr){

        /* Command */
        case 0x04:
        dev->regs[0x04] = (dev->regs[0x04] & ~0x40) | (val & 0x40);

        /* Status */
        case 0x07:
        dev->regs[0x07] &= ~(val & 0xb0);
		break;

        /* Shadow RAM registers */
        case 0x61:
		if ((dev->regs[0x61] ^ val) & 0x03)
			amd640_map(0xc0000, 0x04000, val & 0x03);
		if ((dev->regs[0x61] ^ val) & 0x0c)
			amd640_map(0xc4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->regs[0x61] ^ val) & 0x30)
			amd640_map(0xc8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->regs[0x61] ^ val) & 0xc0)
			amd640_map(0xcc000, 0x04000, (val & 0xc0) >> 6);
		dev->regs[0x61] = val;
		return;

	    case 0x62:
		if ((dev->regs[0x62] ^ val) & 0x03)
			amd640_map(0xd0000, 0x04000, val & 0x03);
		if ((dev->regs[0x62] ^ val) & 0x0c)
			amd640_map(0xd4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->regs[0x62] ^ val) & 0x30)
			amd640_map(0xd8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->regs[0x62] ^ val) & 0xc0)
			amd640_map(0xdc000, 0x04000, (val & 0xc0) >> 6);
		dev->regs[0x62] = val;
		return;

	    case 0x63:
		if ((dev->regs[0x63] ^ val) & 0x30) {
			amd640_map(0xf0000, 0x10000, (val & 0x30) >> 4);
			shadowbios = (((val & 0x30) >> 4) & 0x02);
		}
		if ((dev->regs[0x63] ^ val) & 0xc0)
			amd640_map(0xe0000, 0x10000, (val & 0xc0) >> 6);
		dev->regs[0x63] = val;
		return;

        case 0x65: /* DRAM Control Register #1 */
        dev->regs[0x65] = (dev->regs[0x65] & ~0x20) | (val & 0x20);

        case 0x66: /* DRAM Control Register #2 */
        dev->regs[0x66] = (dev->regs[0x66] & ~0x05) | (val & 0x05);

        case 0x67: /* 32-Bit DRAM Width Control Register */
        dev->regs[0x67] = (dev->regs[0x67] & ~0xc0) | (val & 0xc0);

        case 0x68: /* Reserved (But referenced by the BIOS?) */
        dev->regs[0x68] = (dev->regs[0x68] & ~0x40) | (val & 0x40);

        case 0x6d: /* DRAM Drive Strength Control Register */
        dev->regs[0x6d] = (dev->regs[0x6d] & ~0x6f) | (val & 0x6f);

        case 0x70: /* PCI Buffer Control Register */
        dev->regs[0x70] = (dev->regs[0x70] & ~0x01) | (val & 0x01);

        case 0x71: /* Processor-to-PCI Control Register #1 */
        dev->regs[0x71] = (dev->regs[0x71] & ~0x4e) | (val & 0x4e);

        case 0x73: /* PCI Initiator Control Register #1 */
        dev->regs[0x73] = (dev->regs[0x73] & ~0x0c) | (val & 0x0c);

        case 0x75: /* PCI Arbitration Control Register #1 */
        dev->regs[0x75] = (dev->regs[0x75] & ~0xc7) | (val & 0xc7);

	    default:
		dev->regs[addr] = val;
		break;
    }
}

static uint8_t
amd640_read(int func, int addr, void *priv)
{
    amd640_t *dev = (amd640_t *) priv;
    uint8_t ret = 0xff;

    if(func == 0){
		ret = dev->regs[addr];
    }

    return ret;
}

static void
amd640_reset(void *priv)
{
    amd640_write(0, 0x63, amd640_read(0, 0x63, priv) & 0xcf, priv);
}


static void *
amd640_init(const device_t *info)
{
    amd640_t *dev = (amd640_t *) malloc(sizeof(amd640_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, amd640_read, amd640_write, dev);

    dev->regs[0x00] = 0x06; /* AMD */
    dev->regs[0x01] = 0x11;

    dev->regs[0x02] = 0x95; /* 640 */
    dev->regs[0x03] = 0x15;

    dev->regs[0x04] = 7; /* Command */

    dev->regs[0x06] = 0xa0; /* Status */
    dev->regs[0x07] = 2;

    dev->regs[0x08] = 0x02; /* Revision ID: 0x02 = D, 0x03 = E, 0x04 = F */

    dev->regs[0x0b] = 6; /* Base Class Code */

    dev->regs[0x52] = 2; /* Non-Cacheable control */

    dev->regs[0x58] = 0x40; /* DRAM Configuration register 1 */
    dev->regs[0x59] = 5; /* DRAM Configuration register 2 */

    /* DRAM Bank Endings */
    dev->regs[0x5a] = 1;
    dev->regs[0x5b] = 1;
    dev->regs[0x5c] = 1;
    dev->regs[0x5d] = 1;
    dev->regs[0x5e] = 1;
    dev->regs[0x5f] = 1;

    dev->regs[0x64] = 0xab;

    return dev;
}

static void
amd640_close(void *priv)
{
    amd640_t *dev = (amd640_t *) priv;

    free(dev);
}

const device_t amd640_device = {
    "AMD-640 System Controller",
    DEVICE_PCI,
    0,
    amd640_init,
    amd640_close,
    amd640_reset,
    NULL,
    NULL,
    NULL,
    NULL
};
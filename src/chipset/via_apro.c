/*

  86Box	A hypervisor and IBM PC system emulator that specializes in
 		running old operating systems and software designed for IBM
 		PC systems and compatibles from 1981 through fairly recent
 		system designs based on the PCI bus.
 
        <This file is part of the 86Box distribution.>

VIA Apollo Pro North Bridge emulation

VT82C691 used in the PC Partner APAS3 board
based on the model of VIA MVP3 by mooch & Sarah

Authors: Sarah Walker, <http://pcem-emulator.co.uk/>
Copyright(C) 2020 Tiseno100
Copyright(C) 2020 Melissa Goad
Copyright(C) 2020 Miran Grca


Note: Due to 99.9% similarities with the VP3, MVP3 but also other later Apollo chipsets. We probably should create a common Apollo tree
just like the Intel 4x0 series.

*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "mem.h"
#include "86box_io.h"
#include "rom.h"
#include "pci.h"
#include "device.h"
#include "keyboard.h"
#include "chipset.h"

typedef struct via_apro_t
{
    uint8_t pci_conf[2][256];
} via_apro_t;

static void
apro_map(uint32_t addr, uint32_t size, int state)
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
via_apro_pci_regs(via_apro_t *dev)
{
    memset(dev, 0, sizeof(via_apro_t));

// Host Bridge registers

    dev->pci_conf[0][0x00] = 0x06; // VIA
    dev->pci_conf[0][0x01] = 0x11;
	
    dev->pci_conf[0][0x02] = 0x91; // VT82C691
    dev->pci_conf[0][0x03] = 0x06;

    dev->pci_conf[0][0x04] = 6; // Command
    dev->pci_conf[0][0x05] = 0;

// These(06h-0fh) probably aren't needed but as they're referenced by the MVP3 chipset code i added them too

    dev->pci_conf[0][0x06] = 0; // Status
    dev->pci_conf[0][0x07] = 0;

    dev->pci_conf[0][0x09] = 0; // Program Interface

    dev->pci_conf[0][0x0a] = 0; // Sub Class Code

    dev->pci_conf[0][0x0b] = 0; // Base Class Code

    dev->pci_conf[0][0x0c] = 0; // reserved

    dev->pci_conf[0][0x0d] = 0; // Latency Timer

    dev->pci_conf[0][0x0e] = 0; // Header Type

    dev->pci_conf[0][0x0f] = 0; // Built-in Self test

    dev->pci_conf[0][0x10] = 0x08; // Graphics Aperature Base

    dev->pci_conf[0][0x34] = 0xa0; // Capability Pointer

    dev->pci_conf[0][0x56] = 1; // Bank 6 Ending
    dev->pci_conf[0][0x57] = 1; // Bank 7 Ending
    dev->pci_conf[0][0x5a] = 1; // Bank 0 Ending
    dev->pci_conf[0][0x5b] = 1; // Bank 1 Ending
    dev->pci_conf[0][0x5c] = 1; // Bank 2 Ending
    dev->pci_conf[0][0x5d] = 1; // Bank 3 Ending
    dev->pci_conf[0][0x5e] = 1; // Bank 4 Ending
    dev->pci_conf[0][0x5f] = 1; // Bank 5 Ending

    dev->pci_conf[0][0x64] = 0xec; // DRAM Timing for Banks 0,1
    dev->pci_conf[0][0x65] = 0xec; // DRAM Timing for Banks 2,3
    dev->pci_conf[0][0x66] = 0xec; // DRAM Timing for Banks 4,5
    dev->pci_conf[0][0x67] = 0x01; // DRAM Timing for Banks 6,7

    dev->pci_conf[0][0x6b] = 1; // DRAM Abritration control

    dev->pci_conf[0][0xa4] = 0x03; // AGP Status
    dev->pci_conf[0][0xa5] = 0x02;
    dev->pci_conf[0][0xa6] = 0;
    dev->pci_conf[0][0xa7] = 0x07;

// PCI-to-PCI

    dev->pci_conf[1][0x00] = 0x06; // VIA
    dev->pci_conf[1][0x01] = 0x11;
	
    dev->pci_conf[1][0x02] = 0x91; // VT82C691
    dev->pci_conf[1][0x03] = 0x06;

    dev->pci_conf[1][0x04] = 7; // Command
    dev->pci_conf[1][0x05] = 0;

    dev->pci_conf[1][0x06] = 0x20; // Status
    dev->pci_conf[1][0x07] = 0x02;

    dev->pci_conf[1][0x09] = 0; // Program Interface

    dev->pci_conf[1][0x0A] = 4; // Sub Class Code

    dev->pci_conf[1][0x0B] = 6; // Base Class Code

    dev->pci_conf[1][0x0C] = 0; // reserved

    dev->pci_conf[1][0x0D] = 0; // Latency Timer

    dev->pci_conf[1][0x0E] = 1; // Header Type

    dev->pci_conf[1][0x0F] = 0; // Built-in Self test

    dev->pci_conf[1][0x1c] = 0xf0; // I/O Base

    dev->pci_conf[1][0x20] = 0xf0; // Memory Base
    dev->pci_conf[1][0x21] = 0xff;
    dev->pci_conf[1][0x24] = 0xf0;
    dev->pci_conf[1][0x25] = 0xff;

}

static void
host_bridge_write(int func, int addr, uint8_t val, void *priv)
{

via_apro_t *dev = (via_apro_t *) priv;

    // Read-Only registers. Exact same as MVP3
    if ((addr < 4) || ((addr >= 5) && (addr < 7)) || ((addr >= 8) && (addr < 0xd)) || ((addr >= 0xe) && (addr < 0x12)) || 
    ((addr >= 0x14) && (addr < 0x50)) || ((addr >= 0x79) && (addr < 0x7e)) || ((addr >= 0x85) && (addr < 0x88)) ||
	((addr >= 0x8c) && (addr < 0xa8)) || ((addr >= 0xad) && (addr < 0xfd)))
	return;

    switch(addr){
        case 0x04: // Command
        dev->pci_conf[0][0x04] = (dev->pci_conf[0][0x04] & ~0x40) | (val & 0x40);

        case 0x07: // Status
        dev->pci_conf[0][0x07] &= ~(val & 0xb0);
		break;

	    case 0x12: //Graphics Aperature Base
		dev->pci_conf[0][0x12] = (val & 0xf0);
		break;
	    case 0x13:
		dev->pci_conf[0][0x13] = val;
		break;

        case 0x61: // Shadow RAM control 1
		if ((dev->pci_conf[0][0x61] ^ val) & 0x03)
			apro_map(0xc0000, 0x04000, val & 0x03);
		if ((dev->pci_conf[0][0x61] ^ val) & 0x0c)
			apro_map(0xc4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->pci_conf[0][0x61] ^ val) & 0x30)
			apro_map(0xc8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->pci_conf[0][0x61] ^ val) & 0xc0)
			apro_map(0xcc000, 0x04000, (val & 0xc0) >> 6);
		dev->pci_conf[0][0x61] = val;
		return;

	    case 0x62: // Shadow RAM Control 2
		if ((dev->pci_conf[0][0x62] ^ val) & 0x03)
			apro_map(0xd0000, 0x04000, val & 0x03);
		if ((dev->pci_conf[0][0x62] ^ val) & 0x0c)
			apro_map(0xd4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->pci_conf[0][0x62] ^ val) & 0x30)
			apro_map(0xd8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->pci_conf[0][0x62] ^ val) & 0xc0)
			apro_map(0xdc000, 0x04000, (val & 0xc0) >> 6);
		dev->pci_conf[0][0x62] = val;
		return;

	    case 0x63: // Shadow RAM Control 3
		if ((dev->pci_conf[0][0x63] ^ val) & 0x30) {
			apro_map(0xf0000, 0x10000, (val & 0x30) >> 4);
			shadowbios = (((val & 0x30) >> 4) & 0x02);
		}
		if ((dev->pci_conf[0][0x63] ^ val) & 0xc0)
			apro_map(0xe0000, 0x10000, (val & 0xc0) >> 6);
		dev->pci_conf[0][0x63] = val;
		return;

        //In case we throw somewhere
	    default:
		dev->pci_conf[0][addr] = val;
		break;
    }
}

static void
pci_to_pci_bridge_write(int func, int addr, uint8_t val, void *priv)
{
    via_apro_t *dev = (via_apro_t *) priv;

    if (func != 1)
	return;

    //As with MVP3. Same deal
    if ((addr < 4) || ((addr >= 5) && (addr < 7)) ||
	((addr >= 8) && (addr < 0x18)) || (addr == 0x1b) ||
	((addr >= 0x1e) && (addr < 0x20)) || ((addr >= 0x28) && (addr < 0x3e)) ||
	(addr >= 0x43))
	return;

    switch(addr) {
	case 0x04:
	dev->pci_conf[1][0x04] = (dev->pci_conf[1][0x04] & ~0x47) | (val & 0x47);
	break;
	case 0x07:
	dev->pci_conf[1][0x07] &= ~(val & 0x30);
	break;

	case 0x20: // Memory Base
	dev->pci_conf[1][0x20] = val & 0xf0;
	break;

	case 0x22: // Memory Limit
	dev->pci_conf[1][0x22] = val & 0xf0;
	break;

	case 0x24: // Prefetchable Memory base
	dev->pci_conf[1][0x24] = val & 0xf0;
	break;

	case 0x26: // Prefetchable Memory limit
    dev->pci_conf[1][0x26] = val & 0xf0;
	break;

	default:
	dev->pci_conf[1][addr] = val;
	break;
    }
}

static uint8_t
via_apro_read(int func, int addr, void *priv)
{
    via_apro_t *dev = (via_apro_t *) priv;
    uint8_t ret = 0xff;

    switch(func) {
        case 0:
		ret = dev->pci_conf[0][addr];
		break;
        case 1:
		ret = dev->pci_conf[1][addr];
		break;
    }

    return ret;
}

static void
via_apro_write(int func, int addr, uint8_t val, void *priv)
{
    switch(func) {
	case 0:
	host_bridge_write(func, addr, val, priv);
	break;

	case 1:
	pci_to_pci_bridge_write(func, addr, val, priv);
	break;
    }
}

static void
via_apro_reset(void *priv)
{
    via_apro_write(0, 0x63, via_apro_read(0, 0x63, priv) & 0xcf, priv);
}

static void *
via_apro_init(const device_t *info)
{
    via_apro_t *dev = (via_apro_t *) malloc(sizeof(via_apro_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, via_apro_read, via_apro_write, dev);

    via_apro_pci_regs(dev);

    return dev;
}

static void
via_apro_close(void *priv)
{
    via_apro_t *dev = (via_apro_t *) priv;

    free(dev);
}

const device_t via_apro_device = {
    "VIA Apollo Pro",
    DEVICE_PCI,
    0,
    via_apro_init,
    via_apro_close,
    via_apro_reset,
    NULL,
    NULL,
    NULL,
    NULL
};
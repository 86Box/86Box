/*

  86Box	A hypervisor and IBM PC system emulator that specializes in
 		running old operating systems and software designed for IBM
 		PC systems and compatibles from 1981 through fairly recent
 		system designs based on the PCI bus.
 
        <This file is part of the 86Box distribution.>

VIA Apollo VPX North Bridge emulation

VT82C585VPX used in the Zida Tomato TX100 board
based on the model of VIA MVP3 by mooch & Sarah

Authors: Sarah Walker, <http://pcem-emulator.co.uk/>
Copyright(C) 2020 Tiseno100
Copyright(C) 2020 Melissa Goad
Copyright(C) 2020 Miran Grca

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

typedef struct via_vpx_t
{
    uint8_t pci_conf[2][256];
} via_vpx_t;

static void
vpx_map(uint32_t addr, uint32_t size, int state)
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
via_vpx_pci_regs(via_vpx_t *dev)
{
    memset(dev, 0, sizeof(via_vpx_t));

// Host Bridge registers

    dev->pci_conf[0][0x00] = 0x06; // VIA
    dev->pci_conf[0][0x01] = 0x11;
	
    dev->pci_conf[0][0x02] = 0x85; // VT82C585VPX
    dev->pci_conf[0][0x03] = 0x05;

    dev->pci_conf[0][0x04] = 7; // Command
    dev->pci_conf[0][0x05] = 0;

    dev->pci_conf[0][0x06] = 0xa0; // Status
    dev->pci_conf[0][0x07] = 2;

    dev->pci_conf[0][0x09] = 0; // Program Interface

    dev->pci_conf[0][0x0a] = 0; // Sub Class Code

    dev->pci_conf[0][0x0b] = 6; // Base Class Code

    dev->pci_conf[0][0x0c] = 0; // reserved

    dev->pci_conf[0][0x0d] = 0; // Latency Timer

    dev->pci_conf[0][0x0e] = 0; // Header Type

    dev->pci_conf[0][0x0f] = 0; // Built-in Self test

    dev->pci_conf[0][0x58] = 0x40; // DRAM Configuration 1
    dev->pci_conf[0][0x59] = 5; // DRAM Configuration 2

    dev->pci_conf[0][0x5a] = 1; // Bank 0 Ending
    dev->pci_conf[0][0x5b] = 1; // Bank 1 Ending
    dev->pci_conf[0][0x5c] = 1; // Bank 2 Ending
    dev->pci_conf[0][0x5d] = 1; // Bank 3 Ending
    dev->pci_conf[0][0x5e] = 1; // Bank 4 Ending
    dev->pci_conf[0][0x5f] = 1; // Bank 5 Ending

    dev->pci_conf[0][0x64] = 0xab; // DRAM reference timing

}

static void
host_bridge_write(int func, int addr, uint8_t val, void *priv)
{

via_vpx_t *dev = (via_vpx_t *) priv;

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

        case 0x61: // Shadow RAM control 1
		if ((dev->pci_conf[0][0x61] ^ val) & 0x03)
			vpx_map(0xc0000, 0x04000, val & 0x03);
		if ((dev->pci_conf[0][0x61] ^ val) & 0x0c)
			vpx_map(0xc4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->pci_conf[0][0x61] ^ val) & 0x30)
			vpx_map(0xc8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->pci_conf[0][0x61] ^ val) & 0xc0)
			vpx_map(0xcc000, 0x04000, (val & 0xc0) >> 6);
		dev->pci_conf[0][0x61] = val;
		return;

	    case 0x62: // Shadow RAM Control 2
		if ((dev->pci_conf[0][0x62] ^ val) & 0x03)
			vpx_map(0xd0000, 0x04000, val & 0x03);
		if ((dev->pci_conf[0][0x62] ^ val) & 0x0c)
			vpx_map(0xd4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->pci_conf[0][0x62] ^ val) & 0x30)
			vpx_map(0xd8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->pci_conf[0][0x62] ^ val) & 0xc0)
			vpx_map(0xdc000, 0x04000, (val & 0xc0) >> 6);
		dev->pci_conf[0][0x62] = val;
		return;

	    case 0x63: // Shadow RAM Control 3
		if ((dev->pci_conf[0][0x63] ^ val) & 0x30) {
			vpx_map(0xf0000, 0x10000, (val & 0x30) >> 4);
			shadowbios = (((val & 0x30) >> 4) & 0x02);
		}
		if ((dev->pci_conf[0][0x63] ^ val) & 0xc0)
			vpx_map(0xe0000, 0x10000, (val & 0xc0) >> 6);
		dev->pci_conf[0][0x63] = val;
		return;

        //In case we throw somewhere
	    default:
		dev->pci_conf[0][addr] = val;
		break;
    }
}

static uint8_t
via_vpx_read(int func, int addr, void *priv)
{
    via_vpx_t *dev = (via_vpx_t *) priv;
    uint8_t ret = 0xff;

    switch(func) {
        case 0:
		ret = dev->pci_conf[0][addr];
		break;
    }

    return ret;
}

static void
via_vpx_write(int func, int addr, uint8_t val, void *priv)
{
	host_bridge_write(func, addr, val, priv);
}

static void
via_vpx_reset(void *priv)
{
    via_vpx_write(0, 0x63, via_vpx_read(0, 0x63, priv) & 0xcf, priv);
}

static void *
via_vpx_init(const device_t *info)
{
    via_vpx_t *dev = (via_vpx_t *) malloc(sizeof(via_vpx_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, via_vpx_read, via_vpx_write, dev);

    via_vpx_pci_regs(dev);

    return dev;
}

static void
via_vpx_close(void *priv)
{
    via_vpx_t *dev = (via_vpx_t *) priv;

    free(dev);
}

const device_t via_vpx_device = {
    "VIA Apollo VPX",
    DEVICE_PCI,
    0,
    via_vpx_init,
    via_vpx_close,
    via_vpx_reset,
    NULL,
    NULL,
    NULL,
    NULL
};
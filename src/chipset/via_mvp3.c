/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the VIA MVP3 chip.
 *
 * Version:	@(#)via_mvp3.c	1.0.1	2019/10/19
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *      Melissa Goad, <mszoopers@protonmail.com>
 *
 *		Copyright 2020 Miran Grca, Melissa Goad.
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
#include "chipset.h"


typedef struct via_mvp3_t
{
    uint8_t pci_conf[2][256];
} via_mvp3_t;


static void
mvp3_map(uint32_t addr, uint32_t size, int state)
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
via_mvp3_setup(via_mvp3_t *dev)
{
    memset(dev, 0, sizeof(via_mvp3_t));

    /* Host Bridge */
    dev->pci_conf[0][0x00] = 0x06; /*VIA*/
    dev->pci_conf[0][0x01] = 0x11;
    dev->pci_conf[0][0x02] = 0x98; /*VT82C598MVP*/
    dev->pci_conf[0][0x03] = 0x05;

    dev->pci_conf[0][0x04] = 6;
    dev->pci_conf[0][0x05] = 0;

    dev->pci_conf[0][0x06] = 0x90;
    dev->pci_conf[0][0x07] = 0x02;

    dev->pci_conf[0][0x09] = 0;
    dev->pci_conf[0][0x0a] = 0;
    dev->pci_conf[0][0x0b] = 6;
    dev->pci_conf[0][0x0c] = 0;
    dev->pci_conf[0][0x0d] = 0;
    dev->pci_conf[0][0x0e] = 0;
    dev->pci_conf[0][0x0f] = 0;
    dev->pci_conf[0][0x10] = 0x08;
    dev->pci_conf[0][0x34] = 0xa0;

    dev->pci_conf[0][0x5a] = 0x01;
    dev->pci_conf[0][0x5b] = 0x01;
    dev->pci_conf[0][0x5c] = 0x01;
    dev->pci_conf[0][0x5d] = 0x01;
    dev->pci_conf[0][0x5e] = 0x01;
    dev->pci_conf[0][0x5f] = 0x01;

    dev->pci_conf[0][0x64] = 0xec;
    dev->pci_conf[0][0x65] = 0xec;
    dev->pci_conf[0][0x66] = 0xec;
    dev->pci_conf[0][0x6b] = 0x01;

    dev->pci_conf[0][0xa0] = 0x02;
    dev->pci_conf[0][0xa2] = 0x10;
    dev->pci_conf[0][0xa4] = 0x03;
    dev->pci_conf[0][0xa5] = 0x02;
    dev->pci_conf[0][0xa7] = 0x07;

    /* PCI-to-PCI Bridge */

    dev->pci_conf[1][0x00] = 0x06; /*VIA*/
    dev->pci_conf[1][0x01] = 0x11;
    dev->pci_conf[1][0x02] = 0x98; /*VT82C598MVP*/
    dev->pci_conf[1][0x03] = 0x85;

    dev->pci_conf[1][0x04] = 7;
    dev->pci_conf[1][0x05] = 0;

    dev->pci_conf[1][0x06] = 0x20;
    dev->pci_conf[1][0x07] = 0x02;

    dev->pci_conf[1][0x09] = 0;
    dev->pci_conf[1][0x0a] = 4;
    dev->pci_conf[1][0x0b] = 6;
    dev->pci_conf[1][0x0c] = 0;
    dev->pci_conf[1][0x0d] = 0;
    dev->pci_conf[1][0x0e] = 1;
    dev->pci_conf[1][0x0f] = 0;

    dev->pci_conf[1][0x1c] = 0xf0;

    dev->pci_conf[1][0x20] = 0xf0;
    dev->pci_conf[1][0x21] = 0xff;
    dev->pci_conf[1][0x24] = 0xf0;
    dev->pci_conf[1][0x25] = 0xff;
}


static void
via_mvp3_host_bridge_write(int func, int addr, uint8_t val, void *priv)
{
    via_mvp3_t *dev = (via_mvp3_t *) priv;

    if (func)
	return;

    /*Read-only addresses*/
    if ((addr < 4) || ((addr >= 5) && (addr < 7)) || ((addr >= 8) && (addr < 0xd)) ||
	((addr >= 0xe) && (addr < 0x12)) || ((addr >= 0x14) && (addr < 0x50)) ||
	((addr >= 0x79) && (addr < 0x7e)) || ((addr >= 0x85) && (addr < 0x88)) ||
	((addr >= 0x8c) && (addr < 0xa8)) || ((addr >= 0xad) && (addr < 0xfd)))
	return;

    switch(addr) {
	case 0x04:
		dev->pci_conf[0][0x04] = (dev->pci_conf[0][0x04] & ~0x40) | (val & 0x40);
		break;
	case 0x07:
		dev->pci_conf[0][0x07] &= ~(val & 0xb0);
		break;

	case 0x12:	/* Graphics Aperture Base */
		dev->pci_conf[0][0x12] = (val & 0xf0);
		break;
	case 0x13:	/* Graphics Aperture Base */
		dev->pci_conf[0][0x13] = val;
		break;

	case 0x61:	/* Shadow RAM Control 1 */
		if ((dev->pci_conf[0][0x61] ^ val) & 0x03)
			mvp3_map(0xc0000, 0x04000, val & 0x03);
		if ((dev->pci_conf[0][0x61] ^ val) & 0x0c)
			mvp3_map(0xc4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->pci_conf[0][0x61] ^ val) & 0x30)
			mvp3_map(0xc8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->pci_conf[0][0x61] ^ val) & 0xc0)
			mvp3_map(0xcc000, 0x04000, (val & 0xc0) >> 6);
		dev->pci_conf[0][0x61] = val;
		return;
	case 0x62:	/* Shadow RAM Control 2 */
		if ((dev->pci_conf[0][0x62] ^ val) & 0x03)
			mvp3_map(0xd0000, 0x04000, val & 0x03);
		if ((dev->pci_conf[0][0x62] ^ val) & 0x0c)
			mvp3_map(0xd4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->pci_conf[0][0x62] ^ val) & 0x30)
			mvp3_map(0xd8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->pci_conf[0][0x62] ^ val) & 0xc0)
			mvp3_map(0xdc000, 0x04000, (val & 0xc0) >> 6);
		dev->pci_conf[0][0x62] = val;
		return;
	case 0x63:	/* Shadow RAM Control 3 */
		if ((dev->pci_conf[0][0x63] ^ val) & 0x30) {
			mvp3_map(0xf0000, 0x10000, (val & 0x30) >> 4);
			shadowbios = (((val & 0x30) >> 4) & 0x02);
		}
		if ((dev->pci_conf[0][0x63] ^ val) & 0xc0)
			mvp3_map(0xe0000, 0x10000, (val & 0xc0) >> 6);
		dev->pci_conf[0][0x63] = val;
		return;

	default:
		dev->pci_conf[0][addr] = val;
		break;
    }
}


static void
via_mvp3_pci_bridge_write(int func, int addr, uint8_t val, void *priv)
{
    via_mvp3_t *dev = (via_mvp3_t *) priv;

    if (func != 1)
	return;

    /*Read-only addresses*/

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

	case 0x20:	/* Memory Base */
		dev->pci_conf[1][0x20] = val & 0xf0;
		break;
	case 0x22:	/* Memory Limit */
		dev->pci_conf[1][0x22] = val & 0xf0;
		break;
	case 0x24:	/* Prefetchable Memory Base */
		dev->pci_conf[1][0x24] = val & 0xf0;
		break;
	case 0x26:	/* Prefetchable Memory Limit */
		dev->pci_conf[1][0x26] = val & 0xf0;
		break;

	default:
		dev->pci_conf[1][addr] = val;
		break;
    }
}


static uint8_t
via_mvp3_read(int func, int addr, void *priv)
{
    via_mvp3_t *dev = (via_mvp3_t *) priv;
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
via_mvp3_write(int func, int addr, uint8_t val, void *priv)
{
    switch(func) {
	case 0:
		via_mvp3_host_bridge_write(func, addr, val, priv);
		break;
	case 1:
		via_mvp3_pci_bridge_write(func, addr, val, priv);
		break;
    }
}


static void
via_mvp3_reset(void *priv)
{
    via_mvp3_write(0, 0x63, via_mvp3_read(0, 0x63, priv) & 0xcf, priv);
}


static void *
via_mvp3_init(const device_t *info)
{
    via_mvp3_t *dev = (via_mvp3_t *) malloc(sizeof(via_mvp3_t));

    pci_add_card(0, via_mvp3_read, via_mvp3_write, dev);

    via_mvp3_setup(dev);

    return dev;
}


static void
via_mvp3_close(void *priv)
{
    via_mvp3_t *dev = (via_mvp3_t *) priv;

    free(dev);
}


const device_t via_mvp3_device =
{
    "VIA MVP3",
    DEVICE_PCI,
    0,
    via_mvp3_init, 
    via_mvp3_close, 
    via_mvp3_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

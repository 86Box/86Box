/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 430LX and 430NX PCISet chips.
 *
 * Version:	@(#)m_at_430lx_nx.c	1.0.11	2018/04/04
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
#include "../mem.h"
#include "../memregs.h"
#include "../rom.h"
#include "../pci.h"
#include "../device.h"
#include "../keyboard.h"
#include "../intel.h"
#include "../intel_flash.h"
#include "../intel_sio.h"
#include "../sio.h"
#include "machine.h"


typedef struct
{
    uint8_t	regs[256];
} i430lx_nx_t;


static void
i430lx_nx_map(uint32_t addr, uint32_t size, int state)
{
    switch (state & 3) {
	case 0:
		mem_set_mem_state(addr, size, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
		break;
	case 1:
		mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
		break;
	case 2:
		mem_set_mem_state(addr, size, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
		break;
	case 3:
		mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		break;
    }
    flushmmucache_nopc();
}


static void
i430lx_nx_write(int func, int addr, uint8_t val, void *priv)
{
    i430lx_nx_t *dev = (i430lx_nx_t *) priv;

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

	case 0x59: /*PAM0*/
		if ((dev->regs[0x59] ^ val) & 0xf0) {
			i430lx_nx_map(0xf0000, 0x10000, val >> 4);
			shadowbios = (val & 0x10);
		}
		break;
	case 0x5a: /*PAM1*/
		if ((dev->regs[0x5a] ^ val) & 0x0f)
			i430lx_nx_map(0xc0000, 0x04000, val & 0xf);
		if ((dev->regs[0x5a] ^ val) & 0xf0)
			i430lx_nx_map(0xc4000, 0x04000, val >> 4);
		break;
	case 0x5b: /*PAM2*/
		if ((dev->regs[0x5b] ^ val) & 0x0f)
			i430lx_nx_map(0xc8000, 0x04000, val & 0xf);
		if ((dev->regs[0x5b] ^ val) & 0xf0)
			i430lx_nx_map(0xcc000, 0x04000, val >> 4);
		break;
	case 0x5c: /*PAM3*/
		if ((dev->regs[0x5c] ^ val) & 0x0f)
			i430lx_nx_map(0xd0000, 0x04000, val & 0xf);
		if ((dev->regs[0x5c] ^ val) & 0xf0)
			i430lx_nx_map(0xd4000, 0x04000, val >> 4);
		break;
	case 0x5d: /*PAM4*/
		if ((dev->regs[0x5d] ^ val) & 0x0f)
			i430lx_nx_map(0xd8000, 0x04000, val & 0xf);
		if ((dev->regs[0x5d] ^ val) & 0xf0)
			i430lx_nx_map(0xdc000, 0x04000, val >> 4);
		break;
	case 0x5e: /*PAM5*/
		if ((dev->regs[0x5e] ^ val) & 0x0f)
			i430lx_nx_map(0xe0000, 0x04000, val & 0xf);
		if ((dev->regs[0x5e] ^ val) & 0xf0)
			i430lx_nx_map(0xe4000, 0x04000, val >> 4);
		break;
	case 0x5f: /*PAM6*/
		if ((dev->regs[0x5f] ^ val) & 0x0f)
			i430lx_nx_map(0xe8000, 0x04000, val & 0xf);
		if ((dev->regs[0x5f] ^ val) & 0xf0)
			i430lx_nx_map(0xec000, 0x04000, val >> 4);
		break;
    }

    dev->regs[addr] = val;
}


static uint8_t
i430lx_nx_read(int func, int addr, void *priv)
{
    i430lx_nx_t *dev = (i430lx_nx_t *) priv;

    if (func)
	return 0xff;

    return dev->regs[addr];
}


static void
i430lx_nx_reset(void *priv)
{
    i430lx_nx_write(0, 0x59, 0x00, priv);
}


static void
i430lx_nx_close(void *p)
{
    i430lx_nx_t *i430lx_nx = (i430lx_nx_t *)p;

    free(i430lx_nx);
}


static void
*i430lx_nx_init(const device_t *info)
{
    i430lx_nx_t *i430lx_nx = (i430lx_nx_t *) malloc(sizeof(i430lx_nx_t));
    memset(i430lx_nx, 0, sizeof(i430lx_nx_t));

    i430lx_nx->regs[0x00] = 0x86; i430lx_nx->regs[0x01] = 0x80; /*Intel*/
    i430lx_nx->regs[0x02] = 0xa3; i430lx_nx->regs[0x03] = 0x04; /*82434LX/NX*/
    i430lx_nx->regs[0x04] = 0x06; i430lx_nx->regs[0x05] = 0x00;
    i430lx_nx->regs[0x06] = 0x00; i430lx_nx->regs[0x07] = 0x02;
    i430lx_nx->regs[0x09] = 0x00; i430lx_nx->regs[0x0a] = 0x00; i430lx_nx->regs[0x0b] = 0x06;
    i430lx_nx->regs[0x57] = 0x31;
    i430lx_nx->regs[0x60] = i430lx_nx->regs[0x61] = i430lx_nx->regs[0x62] = i430lx_nx->regs[0x63] = 0x02;
    i430lx_nx->regs[0x64] = 0x02;

    if (info->local == 1) {
	i430lx_nx->regs[0x08] = 0x10; /*A0 stepping*/
	i430lx_nx->regs[0x50] = 0xA0;
	i430lx_nx->regs[0x52] = 0x44; /*256kb PLB cache*/
	i430lx_nx->regs[0x66] = i430lx_nx->regs[0x67] = 0x02;
    } else {
	i430lx_nx->regs[0x08] = 0x03; /*A3 stepping*/
	i430lx_nx->regs[0x50] = 0x80;
	i430lx_nx->regs[0x52] = 0x40; /*256kb PLB cache*/
    }

    pci_add_card(0, i430lx_nx_read, i430lx_nx_write, i430lx_nx);

    return i430lx_nx;
}


const device_t i430lx_device =
{
    "Intel 82434LX",
    DEVICE_PCI,
    0,
    i430lx_nx_init, 
    i430lx_nx_close, 
    i430lx_nx_reset,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};


const device_t i430nx_device =
{
    "Intel 82434NX",
    DEVICE_PCI,
    1,
    i430lx_nx_init, 
    i430lx_nx_close, 
    i430lx_nx_reset,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};


static void
machine_at_premiere_common_init(const machine_t *model)
{
    machine_at_common_init(model);
    device_add(&keyboard_ps2_ami_pci_device);

    memregs_init();
    pci_init(PCI_CONFIG_TYPE_2);
    pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL, 3, 2, 1, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 1, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 3, 2, 4);
    pci_register_slot(0x02, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    device_add(&sio_device);
    fdc37c665_init();
    intel_batman_init();

    device_add(&intel_flash_bxt_ami_device);
}


void
machine_at_batman_init(const machine_t *model)
{
    machine_at_premiere_common_init(model);

    device_add(&i430lx_device);
}


void
machine_at_plato_init(const machine_t *model)
{
    machine_at_premiere_common_init(model);

    device_add(&i430nx_device);
}

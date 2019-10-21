/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel PCISet chips from 420TX to 440FX.
 *
 * Version:	@(#)intel_4x0.c	1.0.2	2019/10/21
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
#include "../cpu/cpu.h"
#include "../mem.h"
#include "../io.h"
#include "../rom.h"
#include "../pci.h"
#include "../device.h"
#include "../keyboard.h"
#include "chipset.h"


enum
{
    INTEL_420TX,
    INTEL_430LX,
    INTEL_430NX,
    INTEL_430FX,
    INTEL_430FX_PB640,
    INTEL_430HX,
    INTEL_430VX
#if defined(DEV_BRANCH) && defined(USE_I686)
    ,INTEL_440FX
#endif
};

typedef struct
{
    uint8_t	regs[256];
    int		type;
} i4x0_t;
static void
i4x0_map(uint32_t addr, uint32_t size, int state)
{
    switch (state & 3) {
	case 0:
		mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
		break;
	case 1:
		mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
		break;
	case 2:
		mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
		break;
	case 3:
		mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		break;
    }
    flushmmucache_nopc();
}


static void
i4x0_write(int func, int addr, uint8_t val, void *priv)
{
    i4x0_t *dev = (i4x0_t *) priv;

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
		if (dev->type >= INTEL_430FX) {
			if (dev->type == INTEL_430FX_PB640)
				val &= 0x06;
			else
				val &= 0x02;
		} else
			val &= 0x42;
		val |= 0x04;
		break;
	case 0x05:
		if (dev->type >= INTEL_430FX)
			val = 0;
		else
			val &= 0x01;
		break;

	case 0x06: /*Status*/
		val = 0;
		break;
	case 0x07:
		if (dev->type >= INTEL_430HX) {
			val &= 0x80;
			val |= 0x02;
		} else {
			val = 0x02;
			if (dev->type == INTEL_430FX_PB640)
				val |= 0x20;
		}
		break;

	case 0x52: /*Cache Control Register*/
#if defined(DEV_BRANCH) && defined(USE_I686)
		if (dev->type < INTEL_440FX) {
#endif
			cpu_cache_ext_enabled = (val & 0x01);
			cpu_update_waitstates();
#if defined(DEV_BRANCH) && defined(USE_I686)
		}
#endif
		break;

	case 0x59: /*PAM0*/
		if ((dev->regs[0x59] ^ val) & 0xf0) {
			i4x0_map(0xf0000, 0x10000, val >> 4);
			shadowbios = (val & 0x10);
		}
		break;
	case 0x5a: /*PAM1*/
		if ((dev->regs[0x5a] ^ val) & 0x0f)
			i4x0_map(0xc0000, 0x04000, val & 0xf);
		if ((dev->regs[0x5a] ^ val) & 0xf0)
			i4x0_map(0xc4000, 0x04000, val >> 4);
		break;
	case 0x5b: /*PAM2*/
		if ((dev->regs[0x5b] ^ val) & 0x0f)
			i4x0_map(0xc8000, 0x04000, val & 0xf);
		if ((dev->regs[0x5b] ^ val) & 0xf0)
			i4x0_map(0xcc000, 0x04000, val >> 4);
		break;
	case 0x5c: /*PAM3*/
		if ((dev->regs[0x5c] ^ val) & 0x0f)
			i4x0_map(0xd0000, 0x04000, val & 0xf);
		if ((dev->regs[0x5c] ^ val) & 0xf0)
			i4x0_map(0xd4000, 0x04000, val >> 4);
		break;
	case 0x5d: /*PAM4*/
		if ((dev->regs[0x5d] ^ val) & 0x0f)
			i4x0_map(0xd8000, 0x04000, val & 0xf);
		if ((dev->regs[0x5d] ^ val) & 0xf0)
			i4x0_map(0xdc000, 0x04000, val >> 4);
		break;
	case 0x5e: /*PAM5*/
		if ((dev->regs[0x5e] ^ val) & 0x0f)
			i4x0_map(0xe0000, 0x04000, val & 0xf);
		if ((dev->regs[0x5e] ^ val) & 0xf0)
			i4x0_map(0xe4000, 0x04000, val >> 4);
		break;
	case 0x5f: /*PAM6*/
		if ((dev->regs[0x5f] ^ val) & 0x0f)
			i4x0_map(0xe8000, 0x04000, val & 0xf);
		if ((dev->regs[0x5f] ^ val) & 0xf0)
			i4x0_map(0xec000, 0x04000, val >> 4);
		break;
	case 0x72: /*SMRAM*/
		if ((dev->type >= INTEL_430FX) && ((dev->regs[0x72] ^ val) & 0x48))
			i4x0_map(0xa0000, 0x20000, ((val & 0x48) == 0x48) ? 3 : 0);
		else if ((dev->type < INTEL_430FX) && ((dev->regs[0x72] ^ val) & 0x20))
			i4x0_map(0xa0000, 0x20000, ((val & 0x20) == 0x20) ? 3 : 0);
		break;
    }

    dev->regs[addr] = val;
}


static uint8_t
i4x0_read(int func, int addr, void *priv)
{
    i4x0_t *dev = (i4x0_t *) priv;

    if (func)
	return 0xff;

    return dev->regs[addr];
}


static void
i4x0_reset(void *priv)
{
    i4x0_t *i4x0 = (i4x0_t *)priv;

    i4x0_write(0, 0x59, 0x00, priv);
    if (i4x0->type >= INTEL_430FX)
	i4x0_write(0, 0x72, 0x02, priv);
}


static void
i4x0_close(void *p)
{
    i4x0_t *i4x0 = (i4x0_t *)p;

    free(i4x0);
}


static void
*i4x0_init(const device_t *info)
{
    i4x0_t *i4x0 = (i4x0_t *) malloc(sizeof(i4x0_t));
    memset(i4x0, 0, sizeof(i4x0_t));

    i4x0->type = info->local;

    i4x0->regs[0x00] = 0x86; i4x0->regs[0x01] = 0x80; /*Intel*/
    switch(i4x0->type) {
	case INTEL_420TX:
		i4x0->regs[0x02] = 0x83; i4x0->regs[0x03] = 0x04; /*82424TX/ZX*/
		i4x0->regs[0x08] = 0x03; /*A3 stepping*/
		i4x0->regs[0x50] = 0x80;
		i4x0->regs[0x52] = 0x40; /*256kb PLB cache*/
		break;
	case INTEL_430LX:
		i4x0->regs[0x02] = 0xa3; i4x0->regs[0x03] = 0x04; /*82434LX/NX*/
		i4x0->regs[0x08] = 0x03; /*A3 stepping*/
		i4x0->regs[0x50] = 0x80;
		i4x0->regs[0x52] = 0x40; /*256kb PLB cache*/
		break;
	case INTEL_430NX:
		i4x0->regs[0x02] = 0xa3; i4x0->regs[0x03] = 0x04; /*82434LX/NX*/
		i4x0->regs[0x08] = 0x10; /*A0 stepping*/
		i4x0->regs[0x50] = 0xA0;
		i4x0->regs[0x52] = 0x44; /*256kb PLB cache*/
		i4x0->regs[0x66] = i4x0->regs[0x67] = 0x02;
		break;
	case INTEL_430FX:
	case INTEL_430FX_PB640:
		i4x0->regs[0x02] = 0x2d; i4x0->regs[0x03] = 0x12; /*SB82437FX-66*/
		if (i4x0->type == INTEL_430FX_PB640)
			i4x0->regs[0x08] = 0x02; /*???? stepping*/
		else
			i4x0->regs[0x08] = 0x00; /*A0 stepping*/
		i4x0->regs[0x52] = 0x40; /*256kb PLB cache*/
		break;
	case INTEL_430HX:
		i4x0->regs[0x02] = 0x50; i4x0->regs[0x03] = 0x12; /*82439HX*/
		i4x0->regs[0x08] = 0x00; /*A0 stepping*/
		i4x0->regs[0x51] = 0x20;
		i4x0->regs[0x52] = 0xB5; /*512kb cache*/
		i4x0->regs[0x56] = 0x52; /*DRAM control*/
		i4x0->regs[0x59] = 0x40;
		i4x0->regs[0x5A] = i4x0->regs[0x5B] = i4x0->regs[0x5C] = i4x0->regs[0x5D] = 0x44;
		i4x0->regs[0x5E] = i4x0->regs[0x5F] = 0x44;
		i4x0->regs[0x65] = i4x0->regs[0x66] = i4x0->regs[0x67] = 0x02;
		i4x0->regs[0x68] = 0x11;
		break;
	case INTEL_430VX:
		i4x0->regs[0x02] = 0x30; i4x0->regs[0x03] = 0x70; /*82437VX*/
		i4x0->regs[0x08] = 0x00; /*A0 stepping*/
		i4x0->regs[0x52] = 0x42; /*256kb PLB cache*/
		i4x0->regs[0x53] = 0x14;
		i4x0->regs[0x56] = 0x52; /*DRAM control*/
		i4x0->regs[0x67] = 0x11;
		i4x0->regs[0x69] = 0x03;
		i4x0->regs[0x70] = 0x20;
		i4x0->regs[0x74] = 0x0e;
		i4x0->regs[0x78] = 0x23;
		break;
#if defined(DEV_BRANCH) && defined(USE_I686)
	case INTEL_440FX:
		i4x0->regs[0x02] = 0x37; i4x0->regs[0x03] = 0x12; /*82441FX*/
		i4x0->regs[0x08] = 0x02; /*A0 stepping*/
		i4x0->regs[0x2c] = 0xf4;
		i4x0->regs[0x2d] = 0x1a;
		i4x0->regs[0x2f] = 0x11;
		i4x0->regs[0x51] = 0x01;
		i4x0->regs[0x53] = 0x80;
		i4x0->regs[0x58] = 0x10;
		i4x0->regs[0x5a] = i4x0->regs[0x5b] = i4x0->regs[0x5c] = i4x0->regs[0x5d] = 0x11;
		i4x0->regs[0x5e] = 0x11;
		i4x0->regs[0x5f] = 0x31;
		break;
#endif
    }
    i4x0->regs[0x04] = 0x06; i4x0->regs[0x05] = 0x00;
#if defined(DEV_BRANCH) && defined(USE_I686)
    if (i4x0->type == INTEL_440FX)
	i4x0->regs[0x06] = 0x80;
#endif
    if (i4x0->type == INTEL_430FX)
	i4x0->regs[0x07] = 0x82;
#if defined(DEV_BRANCH) && defined(USE_I686)
    else if (i4x0->type != INTEL_440FX)
#else
    else
#endif
	i4x0->regs[0x07] = 0x02;
    i4x0->regs[0x0b] = 0x06;
    if (i4x0->type >= INTEL_430FX)
	i4x0->regs[0x57] = 0x01;
    else
	i4x0->regs[0x57] = 0x31;
    i4x0->regs[0x60] = i4x0->regs[0x61] = i4x0->regs[0x62] = i4x0->regs[0x63] = 0x02;
    i4x0->regs[0x64] = 0x02;
    if (i4x0->type >= INTEL_430FX)
	i4x0->regs[0x72] = 0x02;

#if defined(DEV_BRANCH) && defined(USE_I686)
    if (i4x0->type == INTEL_440FX) {
	cpu_cache_ext_enabled = 1;
	cpu_update_waitstates();
    }
#endif

    pci_add_card(0, i4x0_read, i4x0_write, i4x0);

    return i4x0;
}


const device_t i420tx_device =
{
    "Intel 82424TX",
    DEVICE_PCI,
    INTEL_420TX,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};


const device_t i430lx_device =
{
    "Intel 82434LX",
    DEVICE_PCI,
    INTEL_430LX,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};


const device_t i430nx_device =
{
    "Intel 82434NX",
    DEVICE_PCI,
    INTEL_430NX,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};


const device_t i430fx_device =
{
    "Intel SB82437FX-66",
    DEVICE_PCI,
    INTEL_430FX,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};


const device_t i430fx_pb640_device =
{
    "Intel SB82437FX-66 (PB640)",
    DEVICE_PCI,
    INTEL_430FX_PB640,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};


const device_t i430hx_device =
{
    "Intel 82439HX",
    DEVICE_PCI,
    INTEL_430HX,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};


const device_t i430vx_device =
{
    "Intel 82437VX",
    DEVICE_PCI,
    INTEL_430VX,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};


#if defined(DEV_BRANCH) && defined(USE_I686)
const device_t i440fx_device =
{
    "Intel 82441FX",
    DEVICE_PCI,
    INTEL_440FX,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};
#endif

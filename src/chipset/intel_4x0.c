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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019,2020 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/chipset.h>


enum
{
    INTEL_420TX,
    INTEL_420ZX,
    INTEL_430LX,
    INTEL_430NX,
    INTEL_430FX,
    INTEL_430FX_PB640,
    INTEL_430HX,
    INTEL_430VX,
    INTEL_430TX,
    INTEL_440FX,
    INTEL_440BX,
    INTEL_440ZX
};

typedef struct
{
    uint8_t	pm2_cntrl, max_func,
		smram_locked;
    uint8_t	regs[2][256], regs_locked[2][256];
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
i4x0_smram_map(int smm, uint32_t addr, uint32_t size, int ram)
{
    int state = ram ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    mem_set_mem_state_common(smm, addr, size, state);
    flushmmucache();
}


static void
i4x0_smram_handler_phase0(i4x0_t *dev)
{
    uint32_t i, n;

    /* Disable any active mappings. */
    if (dev->type >= INTEL_430FX) {
	if (dev->type >= INTEL_440BX) {
		/* Disable high extended SMRAM. */
		/* TODO: This area should point to A0000-FFFFF. */
		for (i = 0x100a0000; i < 0x100fffff; i += MEM_GRANULARITY_SIZE) {
			/* This is to make sure that if the remaining area is smaller than
			   or equal to MEM_GRANULARITY_SIZE, we do not change the state of
			   too much memory. */
			n = ((mem_size << 10) - i);
			/* Cap to MEM_GRANULARITY_SIZE if i is either at or beyond the end
			   of RAM or the remaining area is bigger than MEM_GRANULARITY_SIZE. */
			if ((i >= (mem_size << 10)) || (n > MEM_GRANULARITY_SIZE))
				n = MEM_GRANULARITY_SIZE;
			i4x0_smram_map(0, i, n, (i < (mem_size << 10)));
			i4x0_smram_map(1, i, n, (i < (mem_size << 10)));
			if (n < MEM_GRANULARITY_SIZE) {
				i4x0_smram_map(0, i + n, MEM_GRANULARITY_SIZE - n, 0);
				i4x0_smram_map(1, i + n, MEM_GRANULARITY_SIZE - n, 0);
			}
		}

		/* Disable TSEG. */
		i4x0_smram_map(1, ((mem_size << 10) - (1 << 20)), (1 << 20), 1);
	}

	/* Disable low extended SMRAM. */
	i4x0_smram_map(0, 0xa0000, 0x20000, 0);
	i4x0_smram_map(1, 0xa0000, 0x20000, 0);
    } else {
	/* Disable low extended SMRAM. */
	i4x0_smram_map(0, 0xa0000, 0x20000, 0);
	i4x0_smram_map(0, (mem_size << 10) - 0x10000, 0x10000, 1);
	i4x0_smram_map(1, 0xa0000, 0x20000, 0);
	i4x0_smram_map(1, (mem_size << 10) - 0x10000, 0x10000, 1);
    }
}


static void
i4x0_smram_handler_phase1(i4x0_t *dev)
{
    uint8_t *regs = (uint8_t *) dev->regs[0];

    uint32_t s, base[2] = { 0x000a0000, 0x00020000 };
    uint32_t size[2] = { 0, 0 };

    if (dev->type >= INTEL_430FX) {
	/* Set temporary bases and sizes. */
	if ((dev->type >= INTEL_440BX) && (regs[0x73] & 0x80)) {
		base[0] = 0x100a0000;
		size[0] = 0x00060000;
	} else {
		base[0] = 0x000a0000;
		size[0] = 0x00020000;
	}

	/* If D_OPEN = 1 and D_LCK = 0, extended SMRAM is visible outside SMM. */
	i4x0_smram_map(0, base[0], size[0], ((regs[0x72] & 0x70) == 0x40));

	/* If the register is set accordingly, disable the mapping also in SMM. */
	i4x0_smram_map(1, base[0], size[0], ((regs[0x72] & 0x08) && !(regs[0x72] & 0x20)));

	/* TSEG mapping. */
	if (dev->type >= INTEL_440BX) {
		if ((regs[0x72] & 0x08) && (regs[0x73] & 0x01)) {
			size[1] = (1 << (17 + ((regs[0x73] >> 1) & 0x03)));
			base[1] = (mem_size << 10) - size[1];
		} else
			base[1] = size[1] = 0x00000000;
		i4x0_smram_map(1, base[1], size[1], 1);
	} else
		base[1] = size[1] = 0x00000000;
    } else {
	size[0] = 0x00010000;
	switch (regs[0x72] & 0x03) {
		case 0:
		default:
			base[0] = (mem_size << 10) - size[0];
			s = 1;
			break;
		case 1:
			base[0] = size[0] = 0x00000000;
			s = 1;
			break;
		case 2:
			base[0] = 0x000a0000;
			s = 0;
			break;
		case 3:
			base[0] = 0x000b0000;
			s = 0;
			break;
	}

	if (base[0] != 0x00000000) {
		/* If OSS = 1 and LSS = 0, extended SMRAM is visible outside SMM. */
		i4x0_smram_map(0, base[0], size[0], ((regs[0x72] & 0x38) == 0x20) || s);
		/* If base is on top of memory, this mapping will point to RAM.
		   TODO: It should actually point to EXTERNAL (with a SMRAM mapping) instead. */
		/* If we are open, not closed, and not locked, point to RAM. */

		/* If the register is set accordingly, disable the mapping also in SMM. */
		i4x0_smram_map(0, base[0], size[0], !(regs[0x72] & 0x10) || s);
		/* If base is on top of memory, this mapping will point to RAM.
		   TODO: It should actually point to EXTERNAL (with a SMRAM mapping) instead. */
		/* If we are not closed, point to RAM. */
	}
    }
}


static void
i4x0_mask_bar(uint8_t *regs)
{
    uint32_t bar;

    bar = (regs[0x13] << 24) | (regs[0x12] << 16);
    bar &= (((uint32_t) regs[0xb4] << 22) | 0xf0000000);
    regs[0x12] = (bar >> 16) & 0xff;
    regs[0x13] = (bar >> 24) & 0xff;
}


static uint8_t
pm2_cntrl_read(uint16_t addr, void *p)
{
    i4x0_t *dev = (i4x0_t *) p;

    return dev->pm2_cntrl & 0x01;
}


static void
pm2_cntrl_write(uint16_t addr, uint8_t val, void *p)
{
    i4x0_t *dev = (i4x0_t *) p;

    dev->pm2_cntrl = val & 0x01;
}


static void
i4x0_write(int func, int addr, uint8_t val, void *priv)
{
    i4x0_t *dev = (i4x0_t *) priv;
    uint8_t *regs = (uint8_t *) dev->regs[func];
    uint8_t *regs_l = (uint8_t *) dev->regs_locked[func];
    int i;

    if (func > dev->max_func)
  	return;

    if ((addr >= 0x10) && (addr < 0x4f))
	return;

    if (func == 0)  switch (addr) {
	case 0x04: /*Command register*/
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX: case INTEL_430LX: case INTEL_430NX:
			case INTEL_440BX: case INTEL_440ZX:
			default:
				regs[0x04] = (regs[0x04] & ~0x42) | (val & 0x42);
				break;
			case INTEL_430FX: case INTEL_430FX_PB640: case INTEL_430HX: case INTEL_430VX: case INTEL_430TX:
			case INTEL_440FX:
				regs[0x04] = (regs[0x04] & ~0x02) | (val & 0x02);
				break;
		}
		break;
	case 0x05:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX: case INTEL_430LX: case INTEL_430NX: case INTEL_430HX:
			case INTEL_440FX: 
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x05] = (regs[0x05] & ~0x01) | (val & 0x01);
				break;
		}
		break;
	case 0x07:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX: case INTEL_430LX: case INTEL_430NX: case INTEL_430HX:
			default:
				regs[0x07] &= ~(val & 0x70);
				break;
			case INTEL_430FX: case INTEL_430FX_PB640: case INTEL_430VX: case INTEL_430TX:
				regs[0x07] &= ~(val & 0x30);
				break;
			case INTEL_440FX:
				regs[0x07] &= ~(val & 0xf9);
				break;
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x07] &= ~(val & 0xf0);
				break;
		}
		break;
	case 0x0d:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX: case INTEL_430LX: case INTEL_430NX:
				regs[0x0d] = (val & 0xf0);
				break;
			default:
				regs[0x0d] = (val & 0xf8);
				break;
		}
		break;
	case 0x0f:
		switch (dev->type) {
			case INTEL_430FX: case INTEL_430FX_PB640: case INTEL_430HX: case INTEL_430VX: case INTEL_430TX:
				regs[0x0f] = (val & 0x40);
				break;
		}
		break;
	case 0x12:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x12] = (val & 0xc0);
				i4x0_mask_bar(regs);
				break;
		}
		break;
	case 0x13:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x13] = val;
				i4x0_mask_bar(regs);
				break;
		}
		break;
	case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				if (!regs_l[addr]) {
					regs[addr] = val;
					regs_l[addr] = 1;
				}
				break;
		}
		break;
	case 0x4f:
		switch (dev->type) {
			case INTEL_430HX:
				regs[0x4f] = (val & 0x84);
				break;
			case INTEL_430VX:
				regs[0x4f] = (val & 0x94);
				break;
			case INTEL_430TX:
				regs[0x4f] = (val & 0x80);
				break;
		}
		break;
	case 0x50:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: default:
				regs[0x50] = (val & 0xe5);
				break;
			case INTEL_430NX:
				regs[0x50] = (val & 0xe7);
				break;
			case INTEL_430FX: case INTEL_430FX_PB640:
				regs[0x50] = (val & 0xef);
				break;
			case INTEL_430HX:
				regs[0x50] = (val & 0xf7);
				break;
			case INTEL_430VX: case INTEL_430TX:
				regs[0x50] = (val & 0x08);
				break;
			case INTEL_440FX:
				regs[0x50] = (val & 0xf4);
				break;
			case INTEL_440BX:
				regs[0x50] = (regs[0x50] & 0x14) | (val & 0xeb);
				break;
			case INTEL_440ZX:
				regs[0x50] = (regs[0x50] & 0x34) | (val & 0xcb);
				break;

		}
		break;
	case 0x51:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX: case INTEL_430LX: case INTEL_430NX:
				regs[0x51] = (val & 0xc0);
				break;
			case INTEL_440FX:
				regs[0x51] = (val & 0xc3);
				break;
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x51] = (regs[0x50] & 0x70) | (val & 0x8f);
				break;
		}
		break;
	case 0x52:	/* Cache Control Register */
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX:
			case INTEL_430FX: case INTEL_430FX_PB640:
			case INTEL_430VX: case INTEL_430TX:
			default:
				regs[0x52] = (val & 0xfb);
				break;
			case INTEL_430NX: case INTEL_430HX:
			case INTEL_440FX:
				regs[0x52] = val;
				break;
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x52] = val & 0x07;
				break;
		}
		break;
	case 0x53:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX:
				regs[0x53] = val & 0x0b;
				break;
			case INTEL_430NX:
				regs[0x53] = val & 0x0a;
				break;
			case INTEL_430VX: case INTEL_430TX:
				regs[0x53] = val & 0x3f;
				break;
			case INTEL_440BX:
				/* Not applicable to 440ZX as that does not support ECC. */
				regs[0x53] = val;
				break;
		}
		break;
	case 0x54:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: case INTEL_430NX:
				regs[0x54] = val & 0x07;
				break;
			case INTEL_430VX:
				regs[0x54] = val & 0xd8;
				break;
			case INTEL_430TX:
				regs[0x54] = val & 0xfa;
				break;
			case INTEL_440FX:
				regs[0x54] = val & 0x82;
				break;
		}
		break;
	case 0x55:
		switch (dev->type) {
			case INTEL_430VX: case INTEL_430TX:
				regs[0x55] = val & 0x01;
				break;
			case INTEL_440FX:
				regs[0x55] = val;
				break;
		}
		break;
	case 0x56:
		switch (dev->type) {
			case INTEL_430HX:
				regs[0x56] = val & 0x1f;
				break;
			case INTEL_430VX:
				regs[0x56] = val & 0x77;
				break;
			case INTEL_430TX:
				regs[0x56] = val & 0x76;
				break;
			case INTEL_440FX:
				regs[0x56] = val;
				break;
		}
		break;
	case 0x57:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: default:
				regs[0x57] = val & 0x3f;
				break;
			case INTEL_430NX:
				regs[0x57] = val;
				break;
			case INTEL_430FX: case INTEL_430FX_PB640:
			case INTEL_430HX: case INTEL_430VX:
				regs[0x57] = val & 0xcf;
				break;
			case INTEL_430TX:
				regs[0x57] = val & 0xdf;
				break;
			case INTEL_440FX:
				regs[0x57] = val & 0x77;
				break;
			case INTEL_440BX:
				regs[0x57] = val & 0x3f;
				break;
			case INTEL_440ZX:
				regs[0x57] = val & 0x2f;
				break;
		}
		break;
	case 0x58:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: default:
				regs[0x58] = val & 0x01;
				break;
			case INTEL_430NX:
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x58] = val & 0x03;
				break;
			case INTEL_430FX: case INTEL_430FX_PB640:
			case INTEL_440FX:
				regs[0x58] = val & 0x7f;
				break;
			case INTEL_430HX: case INTEL_430VX:
				regs[0x57] = val;
				break;
			case INTEL_430TX:
				regs[0x57] = val & 0x7b;
				break;
		}
		break;
	case 0x59:	/* PAM0 */
		if (dev->type <= INTEL_430NX) {
			if ((regs[0x59] ^ val) & 0x0f)
				i4x0_map(0x80000, 0x20000, val & 0x0f);
		}
		if ((regs[0x59] ^ val) & 0xf0) {
			i4x0_map(0xf0000, 0x10000, val >> 4);
			shadowbios = (val & 0x10);
		}
		if (dev->type > INTEL_430NX)
			regs[0x59] = val & 0x70;
		else
			regs[0x59] = val & 0x77;
		break;
	case 0x5a:	/* PAM1 */
		if ((regs[0x5a] ^ val) & 0x0f)
			i4x0_map(0xc0000, 0x04000, val & 0xf);
		if ((regs[0x5a] ^ val) & 0xf0)
			i4x0_map(0xc4000, 0x04000, val >> 4);
		regs[0x5a] = val & 0x77;
		break;
	case 0x5b:	/*PAM2 */
		if ((regs[0x5b] ^ val) & 0x0f)
			i4x0_map(0xc8000, 0x04000, val & 0xf);
		if ((regs[0x5b] ^ val) & 0xf0)
			i4x0_map(0xcc000, 0x04000, val >> 4);
		regs[0x5b] = val & 0x77;
		break;
	case 0x5c:	/*PAM3 */
		if ((regs[0x5c] ^ val) & 0x0f)
			i4x0_map(0xd0000, 0x04000, val & 0xf);
		if ((regs[0x5c] ^ val) & 0xf0)
			i4x0_map(0xd4000, 0x04000, val >> 4);
		regs[0x5c] = val & 0x77;
		break;
	case 0x5d:	/* PAM4 */
		if ((regs[0x5d] ^ val) & 0x0f)
			i4x0_map(0xd8000, 0x04000, val & 0xf);
		if ((regs[0x5d] ^ val) & 0xf0)
			i4x0_map(0xdc000, 0x04000, val >> 4);
		regs[0x5d] = val & 0x77;
		break;
	case 0x5e:	/* PAM5 */
		if ((regs[0x5e] ^ val) & 0x0f)
			i4x0_map(0xe0000, 0x04000, val & 0xf);
		if ((regs[0x5e] ^ val) & 0xf0)
			i4x0_map(0xe4000, 0x04000, val >> 4);
		regs[0x5e] = val & 0x77;
		break;
	case 0x5f:	/* PAM6 */
		if ((regs[0x5f] ^ val) & 0x0f)
			i4x0_map(0xe8000, 0x04000, val & 0xf);
		if ((regs[0x5f] ^ val) & 0xf0)
			i4x0_map(0xec000, 0x04000, val >> 4);
		regs[0x5f] = val & 0x77;
		break;
	case 0x60: case 0x61: case 0x62: case 0x63: case 0x64:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: case INTEL_430NX:
			case INTEL_430HX:
			case INTEL_440FX: 
      case INTEL_440BX: case INTEL_440ZX:
			default:
				regs[addr] = val;
				break;
			case INTEL_430FX: case INTEL_430FX_PB640:
			case INTEL_430VX:
				regs[addr] = val & 0x3f;
				break;
			case INTEL_430TX:
				regs[addr] = val & 0x7f;
				break;
		}
		break;
	case 0x65:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: case INTEL_430NX:
			case INTEL_430HX:
			case INTEL_440FX:
			case INTEL_440BX: case INTEL_440ZX:
				regs[addr] = val;
				break;
			case INTEL_430VX:
				regs[addr] = val & 0x3f;
				break;
			case INTEL_430TX:
				regs[addr] = val & 0x7f;
				break;
		}
		break;
	case 0x66:
		switch (dev->type) {
			case INTEL_430NX: case INTEL_430HX:
			case INTEL_440FX: 
			case INTEL_440BX: case INTEL_440ZX:
				regs[addr] = val;
				break;
		}
		break;
	case 0x67:
		switch (dev->type) {
			case INTEL_430NX: case INTEL_430HX:	
			case INTEL_440FX: 
			case INTEL_440BX: case INTEL_440ZX:
				regs[addr] = val;
				break;
			case INTEL_430VX:
				regs[addr] = val & 0x11;
				break;
			case INTEL_430TX:
				regs[addr] = val & 0xb7;
				break;
		}
		break;
	case 0x68:
		switch (dev->type) {
			case INTEL_430NX: case INTEL_430HX:
			case INTEL_430VX: case INTEL_430TX:
				regs[0x68] = val;
				break;
			case INTEL_430FX: case INTEL_430FX_PB640:
				regs[0x68] = val & 0x1f;
				break;
			case INTEL_440FX:
				regs[0x68] = val & 0xc0;
				break;
			case INTEL_440BX:
				regs[0x68] = (regs[0x68] & 0x38) | (val & 0xc7);
				break;
			case INTEL_440ZX:
				regs[0x68] = (regs[0x68] & 0x3f) | (val & 0xc0);
				break;
		}
		break;
	case 0x69:
		switch (dev->type) {
			case INTEL_430NX:
			case INTEL_440BX:
				regs[0x69] = val;
				break;
			case INTEL_430VX:
				regs[0x69] = val & 0x07;
				break;
  		case INTEL_440ZX:
				regs[0x69] = val & 0x3f;
				break;
		}
		break;
	case 0x6a: case 0x6b:
		switch (dev->type) {
			case INTEL_430NX:
			case INTEL_440BX:
				regs[addr] = val;
				break;
			case INTEL_440ZX:
				if (addr == 0x6a)
					regs[addr] = val & 0xfc;
				else
					regs[addr] = val & 0x33;
				break;
		}
		break;
	case 0x6c: case 0x6d: case 0x6e:
		switch (dev->type) {
			case INTEL_440BX:
				regs[addr] = val;
				break;
			case INTEL_440ZX:
				if (addr == 0x6c)
					regs[addr] = val & 0x03;
				else if (addr == 0x6d)
					regs[addr] = val & 0xcf;
				break;
		}
		break;
	case 0x70:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX:
				regs[addr] = val & 0xc7;
				break;
			case INTEL_430NX:
				regs[addr] = val;
				break;
			case INTEL_430VX: case INTEL_430TX:
				regs[addr] = val & 0xfc;
				break;
			case INTEL_440FX:
				regs[addr] = val & 0xf8;
				break;
		}
		break;
	case 0x71:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX:
				regs[addr] = val & 0x4d;
				break;
			case INTEL_430TX:
				regs[addr] = val;
				break;
			case INTEL_440FX:
				regs[addr] = val & 0x1f;
				break;
		}
		break;
	case 0x72:	/* SMRAM */
		i4x0_smram_handler_phase0(dev);
		if (dev->type >= INTEL_430FX) {
			if (dev->smram_locked)
				regs[0x72] = (regs[0x72] & 0xdf) | (val & 0x20);
			else {
				regs[0x72] = (regs[0x72] & 0x87) | (val & 0x78);
				dev->smram_locked = (val & 0x10);
				if (dev->smram_locked)
					regs[0x72] &= 0xbf;
			}
		} else {
			if (dev->smram_locked)
				regs[0x72] = (regs[0x72] & 0xef) | (val & 0x10);
			else {
				regs[0x72] = (regs[0x72] & 0xc0) | (val & 0x3f);
				dev->smram_locked = (val & 0x08);
				if (dev->smram_locked)
					regs[0x72] &= 0xef;
			}
		}
		i4x0_smram_handler_phase1(dev);
		break;
	case 0x73:
		switch (dev->type) {
			case INTEL_430VX:
				regs[0x73] = val & 0x03;
				break;
			case INTEL_440BX: case INTEL_440ZX:
				if (!dev->smram_locked) {
					i4x0_smram_handler_phase0(dev);
					regs[0x73] = (regs[0x72] & 0x38) | (val & 0xc7);
					i4x0_smram_handler_phase1(dev);
				}
				break;
		}
		break;
	case 0x74:
		switch (dev->type) {
			case INTEL_430VX:
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x74] = val;
				break;
		}
		break;
	case 0x75: case 0x76:
	case 0x7b:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[addr] = val;
		}
		break;
	case 0x77:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x77] = val & 0x03;
		}
		break;
	case 0x78:
		switch (dev->type) {
			case INTEL_430VX:
				regs[0x78] = val & 0xcf;
				break;
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x78] = val & 0x0f;
				break;
		}
		break;
	case 0x79:
		switch (dev->type) {
			case INTEL_430TX:
				regs[0x79] = val & 0x74;
				io_removehandler(0x0022, 0x01, pm2_cntrl_read, NULL, NULL, pm2_cntrl_write, NULL, NULL, dev);
				if (val & 0x40)
					io_sethandler(0x0022, 0x01, pm2_cntrl_read, NULL, NULL, pm2_cntrl_write, NULL, NULL, dev);
				break;
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x79] = val;
				break;
		}
		break;
	case 0x7a:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x7a] = (regs[0x7a] & 0x0a) | (val & 0xf5);
				io_removehandler(0x0022, 0x01, pm2_cntrl_read, NULL, NULL, pm2_cntrl_write, NULL, NULL, dev);
				if (val & 0x40)
					io_sethandler(0x0022, 0x01, pm2_cntrl_read, NULL, NULL, pm2_cntrl_write, NULL, NULL, dev);
				break;
		}
		break;
	case 0x7c:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: case INTEL_430NX:
				regs[0x7c] = val & 0x8f;
				break;
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x7c] = val & 0x1f;
				break;
		}
	case 0x7d:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: case INTEL_430NX:
				regs[0x7c] = val & 0x32;
				break;
		}
	case 0x7e: case 0x7f:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: case INTEL_430NX:
				regs[addr] = val;
				break;
		}
	case 0x80:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x80] &= ~(val & 0x03);
				break;
		}
		break;
	case 0x90:
		switch (dev->type) {
			case INTEL_430HX:
				regs[0x80] = val & 0x87;
				break;
			case INTEL_440FX:
				regs[0x80] = val & 0x1b;
				break;
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x7c] = val;
				break;
		}
		break;
	case 0x91:
		switch (dev->type) {
			case INTEL_430HX: case INTEL_440BX:
			case INTEL_440FX: 
				/* Not applicable on 82443ZX. */
				regs[0x91] &= ~(val & 0x11);
				break;
		}
		break;
	case 0x92:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x92] &= ~(val & 0x1f);
				break;
		}
		break;
	case 0x93:
		switch (dev->type) {
			case INTEL_440FX:
				regs[0x93] = (val & 0x0f);
				trc_write(0x0093, val & 0x06, NULL);
				break;
		}
		break;
	case 0xa8: case 0xa9:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[addr] = (val & 0x03);
				break;
		}
		break;
	case 0xb0:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0xb0] = (val & 0x80);
				break;
		}
		break;
	case 0xb1:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0xb1] = (val & 0xa0);
				break;
		}
		break;
	case 0xb4:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0xb4] = (val & 0x3f);
				i4x0_mask_bar(regs);
				break;
		}
		break;
	case 0xb9:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0xb9] = (val & 0xf0);
				break;
		}
		break;
	case 0xba: case 0xbb:
	case 0xd0: case 0xd1: case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6: case 0xd7:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[addr] = val;
				break;
		}
		break;
	case 0xca:
		switch (dev->type) {
			case INTEL_440BX:
				regs[addr] = val;
				break;
			case INTEL_440ZX:
				regs[addr] = val & 0xe7;
				break;
		}
		break;
	case 0xcb:
		switch (dev->type) {
			case INTEL_440BX:
				regs[addr] = val;
				break;
			case INTEL_440ZX:
				regs[addr] = val & 0xa7;
				break;
		}
		break;
	case 0xcc:
		switch (dev->type) {
			case INTEL_440BX:
				regs[0xcc] = (val & 0x7f);
				break;
			case INTEL_440ZX:
				regs[0xcc] = (val & 0x58);
				break;
		}
		break;
	case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4:
	case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				if (!regs_l[addr])
					regs[addr] = val;
				break;
		}
		break;
	case 0xe5: case 0xed:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				if (!regs_l[addr])
					regs[addr] = (val & 0x3f);
				break;
		}
		break;
	case 0xe7:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0xe7] = 0x80;
				for (i = 0; i < 16; i++)
					regs_l[0xe0 + i] = !!(val & 0x80);
				if (!regs_l[0xe7]) {
					regs[0xe7] |= (val & 0x7f);
				}
				break;
		}
		break;
	case 0xf0:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0xf0] = (val & 0xc0);
				break;
		}
		break;
	case 0xf1:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0xf1] = (val & 0x03);
				break;
		}
		break;
    } else if (func == 1)  switch (addr) {
	case 0x04:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x04] = (val & 0x1f);
				break;
		}
		break;
	case 0x05:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x05] = (val & 0x01);
				break;
		}
		break;
	case 0x0d: case 0x1b:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[addr] = (val & 0xf8);
				break;
		}
		break;
	case 0x19: case 0x1a:
	case 0x21: case 0x23:
	case 0x25: case 0x27:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[addr] = val;
				break;
		}
		break;
	case 0x1c: case 0x1d:
	case 0x20: case 0x22:
	case 0x24: case 0x26:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[addr] = (val & 0xf0);
				break;
		}
		break;
	case 0x1f:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x1f] &= ~(val & 0xf0);
				break;
		}
		break;
	case 0x3e:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x3e] = (val & 0xed);
				break;
		}
		break;
    }
}


static uint8_t
i4x0_read(int func, int addr, void *priv)
{
    i4x0_t *dev = (i4x0_t *) priv;
    uint8_t ret = 0xff;
    uint8_t *regs = (uint8_t *) dev->regs[func];

  if (func > dev->max_func)
	  ret = 0xff;
  else {
	ret = regs[addr];
	/* Special behavior for 440FX register 0x93 which is basically TRC in PCI space
	   with the addition of bits 3 and 0. */
	if ((func == 0) && (addr == 0x93) && (dev->type == INTEL_440FX))
		ret = (ret & 0xf9) | (trc_read(0x0093, NULL) & 0x06);
    }

    return ret;
}


static void
i4x0_reset(void *priv)
{
    i4x0_t *dev = (i4x0_t *)priv;
    int i;

    if (dev->type >= INTEL_430FX)
	i4x0_write(0, 0x59, 0x00, priv);
    else
	i4x0_write(0, 0x59, 0x0f, priv);

    for (i = 0; i < 6; i++)
	i4x0_write(0, 0x5a + i, 0x00, priv);

    if (dev->type >= INTEL_430FX)
	i4x0_write(0, 0x72, 0x02, priv);
    else
	i4x0_write(0, 0x72, 0x00, priv);

    if ((dev->type == INTEL_440BX) || (dev->type == INTEL_440ZX)) {
	for (i = 0; i <= dev->max_func; i++)
		memset(dev->regs_locked[i], 0x00, 256 * sizeof(uint8_t));
    }

    // smbase = 0xa0000;
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
    i4x0_t *dev = (i4x0_t *) malloc(sizeof(i4x0_t));
    uint8_t *regs;

    memset(dev, 0, sizeof(i4x0_t));

    dev->type = info->local & 0xff;

    regs = (uint8_t *) dev->regs[0];

    // This is off by default and has to be moved to the appropriate register handling.
    // io_sethandler(0x0022, 0x01, pm2_cntrl_read, NULL, NULL, pm2_cntrl_write, NULL, NULL, dev);

    regs[0x00] = 0x86; regs[0x01] = 0x80; /*Intel*/

    switch (dev->type) {
	case INTEL_420TX:
	case INTEL_420ZX:
		regs[0x02] = 0x83; regs[0x03] = 0x04;	/* 82424TX/ZX */
		regs[0x06] = 0x40;
		regs[0x08] = (dev->type == INTEL_420ZX) ? 0x01 : 0x00;
		regs[0x0d] = 0x20;
		if (is486sx)
			regs[0x50] = 0x20;
		else if (is486sx2)
			regs[0x50] = 0x60;	/* Guess based on the SX, DX, and DX2 values. */
		else if (is486dx || isdx4)
			regs[0x50] = 0x00;
		else if (is486dx2)
			regs[0x50] = 0x40;
		else
			regs[0x50] = 0x80;	/* Pentium OverDrive. */
		if (cpu_busspeed <= 25000000)
			regs[0x50] |= 0x01;
		else if ((cpu_busspeed > 25000000) && (cpu_busspeed <= 30000000))
			regs[0x50] |= 0x02;
		else if ((cpu_busspeed > 30000000) && (cpu_busspeed <= 33333333))
			regs[0x50] |= 0x03;
		regs[0x51] = 0x80;
		regs[0x52] = 0xea;	/* 512 kB burst cache, set to 0xaa for 256 kB */
		regs[0x57] = 0x31;
		regs[0x59] = 0x0f;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = regs[0x65] = 0x02;
		break;
	case INTEL_430LX:
		regs[0x02] = 0xa3; regs[0x03] = 0x04;	/* 82434LX/NX */
		regs[0x06] = 0x40;
		regs[0x08] = 0x03;
		regs[0x0d] = 0x20;
		regs[0x50] = 0x82;
		if (cpu_busspeed <= 60000000)
			regs[0x50] |= 0x00;
		else if ((cpu_busspeed > 60000000) && (cpu_busspeed <= 66666667))
			regs[0x50] |= 0x01;
		regs[0x51] = 0x80;
		regs[0x52] = 0xea;	/* 512 kB burst cache, set to 0xaa for 256 kB */
		regs[0x57] = 0x31;
		regs[0x59] = 0x0f;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = regs[0x65] = 0x02;
		break;
	case INTEL_430NX:
		regs[0x02] = 0xa3; regs[0x03] = 0x04;	/* 82434LX/NX */
		regs[0x06] = 0x40;
		regs[0x08] = 0x11;
		regs[0x0d] = 0x20;
		regs[0x50] = 0x80;
		if (cpu_busspeed <= 50000000)
			regs[0x50] |= 0x01;
		else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
			regs[0x50] |= 0x02;
		else if ((cpu_busspeed > 60000000) && (cpu_busspeed <= 66666667))
			regs[0x50] |= 0x03;
		regs[0x51] = 0x80;
		regs[0x52] = 0xea;	/* 512 kB burst cache, set to 0xaa for 256 kB */
		regs[0x57] = 0x31;
		regs[0x59] = 0x0f;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = regs[0x65] = regs[0x66] = regs[0x67] = 0x02;
		break;
	case INTEL_430FX_PB640:
		regs[0x08] = 0x02;
		/* FALLTHROUGH */
	case INTEL_430FX:
		regs[0x02] = 0x2d; regs[0x03] = 0x12;	/* SB82437FX-66 */
		regs[0x52] = 0xb2;	/* 512 kB PLB cache, set to 0x42 for 256 kB */
		if (cpu_busspeed <= 50000000)
			regs[0x57] |= 0x01;
		else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
			regs[0x57] |= 0x02;
		else if ((cpu_busspeed > 60000000) && (cpu_busspeed <= 66666667))
			regs[0x57] |= 0x03;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = 0x02;
		regs[0x72] = 0x02;
		break;
	case INTEL_430HX:
		regs[0x02] = 0x50; regs[0x03] = 0x12;	/* 82439HX */
		regs[0x52] = 0xb2;	/* 512 kB PLB cache, set to 0x42 for 256 kB */
		if (cpu_busspeed <= 50000000)
			regs[0x57] |= 0x01;
		else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
			regs[0x57] |= 0x02;
		else if ((cpu_busspeed > 60000000) && (cpu_busspeed <= 66666667))
			regs[0x57] |= 0x03;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = regs[0x65] = regs[0x66] = regs[0x67] = 0x02;
		regs[0x72] = 0x02;
		break;
	case INTEL_430VX:
		regs[0x02] = 0x30; regs[0x03] = 0x70;	/* 82437VX */
		regs[0x52] = 0xb2;	/* 512 kB PLB cache, set to 0x42 for 256 kB */
		regs[0x53] = 0x14;
		regs[0x56] = 0x52;
		if (cpu_busspeed <= 50000000)
			regs[0x57] |= 0x01;
		else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
			regs[0x57] |= 0x02;
		else if ((cpu_busspeed > 60000000) && (cpu_busspeed <= 66666667))
			regs[0x57] |= 0x03;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = 0x02;
		regs[0x67] = 0x11;
		regs[0x69] = 0x03;
		regs[0x70] = 0x20;
		regs[0x72] = 0x02;
		regs[0x74] = 0x0e;
		regs[0x78] = 0x23;
		break;
	case INTEL_430TX:
		regs[0x02] = 0x00; regs[0x03] = 0x71;	/* 82439TX */
		regs[0x08] = 0x01;
		regs[0x52] = 0xb2;	/* 512 kB PLB cache, set to 0x42 for 256 kB */
		regs[0x53] = 0x14;
		regs[0x56] = 0x52;
		regs[0x57] = 0x01;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = regs[0x65] = 0x02;
		if (cpu_busspeed <= 60000000)
			regs[0x67] |= 0x00;
		else if ((cpu_busspeed > 60000000) && (cpu_busspeed <= 66666667))
			regs[0x67] |= 0x80;
		regs[0x70] = 0x20;
		regs[0x72] = 0x02;
		break;
	case INTEL_440FX:
		regs[0x02] = 0x37; regs[0x03] = 0x12;	/* 82441FX */
		regs[0x08] = 0x02;
		if (cpu_busspeed <= 60000000)
			regs[0x51] |= 0x01;
		else if ((cpu_busspeed > 60000000) && (cpu_busspeed <= 66666667))
			regs[0x51] |= 0x02;
		regs[0x53] = 0x80;
		regs[0x57] = 0x01;
		regs[0x58] = 0x10;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = regs[0x65] = regs[0x66] = regs[0x67] = 0x02;
		regs[0x71] = 0x10;
		regs[0x72] = 0x02;
		break;
	case INTEL_440BX: case INTEL_440ZX:
		regs[0x7a] = (info->local >> 8) & 0xff;
		dev->max_func = (regs[0x7a] & 0x02) ? 0 : 1;

		regs[0x02] = (regs[0x7a] & 0x02) ? 0x92 : 0x90; regs[0x03] = 0x71;	/* 82443BX */
		regs[0x06] = (regs[0x7a] & 0x02) ? 0x00 : 0x10;
		regs[0x08] = 0x02;
		regs[0x10] = 0x08;
		regs[0x34] = (regs[0x7a] & 0x02) ? 0x00 : 0xa0;
		if (cpu_busspeed <= 66666667)
			regs[0x51] |= 0x00;
		else if ((cpu_busspeed > 66666667) && (cpu_busspeed <= 100000000))
			regs[0x51] |= 0x20;
		regs[0x57] = 0x28;	/* 4 DIMMs, SDRAM */
		regs[0x58] = 0x03;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = regs[0x65] = regs[0x66] = regs[0x67] = 0x01;
		regs[0x72] = 0x02;
		regs[0x73] = 0x38;
		regs[0x7b] = 0x38;
		regs[0x90] = 0x80;
		regs[0xa0] = (regs[0x7a] & 0x02) ? 0x00 : 0x02;
		regs[0xa2] = (regs[0x7a] & 0x02) ? 0x00 : 0x10;
		regs[0xa4] = 0x03;
		regs[0xa5] = 0x02;
		regs[0xa7] = 0x1f;
		break;
    }

    regs[0x04] = 0x06; regs[0x07] = 0x02;
    regs[0x0b] = 0x06;

    if (dev->type >= INTEL_440FX) {
	cpu_cache_ext_enabled = 1;
	cpu_update_waitstates();
    }

    i4x0_write(regs[0x59], 0x59, 0x00, dev);
    i4x0_write(regs[0x5a], 0x5a, 0x00, dev);
    i4x0_write(regs[0x5b], 0x5b, 0x00, dev);
    i4x0_write(regs[0x5c], 0x5c, 0x00, dev);
    i4x0_write(regs[0x5d], 0x5d, 0x00, dev);
    i4x0_write(regs[0x5e], 0x5e, 0x00, dev);
    i4x0_write(regs[0x5f], 0x5f, 0x00, dev);
    i4x0_write(regs[0x72], 0x72, 0x00, dev);

    // smbase = 0xa0000;

    if (((dev->type == INTEL_440BX) || (dev->type == INTEL_440ZX)) && (dev->max_func == 1)) {
	regs = (uint8_t *) dev->regs[1];

	regs[0x00] = 0x86; regs[0x01] = 0x80;	/* Intel */
	regs[0x02] = 0x91; regs[0x03] = 0x71;	/* 82443BX */
	regs[0x06] = 0x20; regs[0x07] = 0x02;
	regs[0x08] = 0x02;
	regs[0x0a] = 0x04; regs[0x0b] = 0x06;
	regs[0x0e] = 0x01;
	regs[0x1c] = 0xf0;
	regs[0x1e] = 0xa0; regs[0x1f] = 0x02;
	regs[0x20] = 0xf0; regs[0x21] = 0xff;
	regs[0x24] = 0xf0; regs[0x25] = 0xff;
	regs[0x3e] = 0x80;
    }

    pci_add_card(PCI_ADD_NORTHBRIDGE, i4x0_read, i4x0_write, dev);

    return dev;
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


const device_t i420zx_device =
{
    "Intel 82424ZX",
    DEVICE_PCI,
    INTEL_420ZX,
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


const device_t i430tx_device =
{
    "Intel 82439TX",
    DEVICE_PCI,
    INTEL_430TX,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};


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


const device_t i440bx_device =
{
    "Intel 82443BX",
    DEVICE_PCI,
    0x8000 | INTEL_440BX,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};


const device_t i440zx_device =
{
    "Intel 82443ZX",
    DEVICE_PCI,
    0x8000 | INTEL_440ZX,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

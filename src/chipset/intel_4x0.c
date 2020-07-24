/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel PCISet chips from 420TX to 440GX.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
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
#include <86box/spd.h>


enum
{
    INTEL_420TX,
    INTEL_420ZX,
    INTEL_430LX,
    INTEL_430NX,
    INTEL_430FX,
    INTEL_430HX,
    INTEL_430VX,
    INTEL_430TX,
    INTEL_440FX,
    INTEL_440LX,
    INTEL_440EX,
    INTEL_440BX,
    INTEL_440GX,
    INTEL_440ZX
};

typedef struct
{
    uint8_t	pm2_cntrl, max_func,
		smram_locked, max_drb,
		drb_unit, drb_default;
    uint8_t	regs[2][256], regs_locked[2][256];
    int		type;
} i4x0_t;


#ifdef ENABLE_I4X0_LOG
int i4x0_do_log = ENABLE_I4X0_LOG;


static void
i4x0_log(const char *fmt, ...)
{
    va_list ap;

    if (i4x0_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define i4x0_log(fmt, ...)
#endif


static void
i4x0_map(uint32_t addr, uint32_t size, int state)
{
    switch (state & 3) {
	case 0:
		mem_set_mem_state_both(addr, size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
		break;
	case 1:
		mem_set_mem_state_both(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
		break;
	case 2:
		mem_set_mem_state_both(addr, size, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
		break;
	case 3:
		mem_set_mem_state_both(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		break;
    }
    flushmmucache_nopc();
}


static void
i4x0_smram_map(int smm, uint32_t addr, uint32_t size, int is_smram)
{
    mem_set_mem_state_smram(smm, addr, size, is_smram);
}


static void
i4x0_smram_handler_phase0(i4x0_t *dev)
{
    uint32_t tom = (mem_size << 10);

    /* Disable any active mappings. */
    if (smram[0].size != 0x00000000) {
	i4x0_smram_map(0, smram[0].host_base, smram[0].size, 0);
	i4x0_smram_map(1, smram[0].host_base, smram[0].size, 0);

	memset(&smram[0], 0x00, sizeof(smram_t));
	mem_mapping_disable(&ram_smram_mapping[0]);
    }

    if ((dev->type >= INTEL_440BX) && (smram[1].size != 0x00000000)) {
	i4x0_smram_map(1, smram[1].host_base, smram[1].size, 0);

	tom -=  (1 << 20);
	mem_set_mem_state_smm(tom, (1 << 20), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

	memset(&smram[1], 0x00, sizeof(smram_t));
	mem_mapping_disable(&ram_smram_mapping[1]);
    }
}


static void
i4x0_smram_handler_phase1(i4x0_t *dev)
{
    uint8_t *regs = (uint8_t *) dev->regs[0];
    uint32_t tom = (mem_size << 10);

    uint32_t s, base[2] = { 0x000a0000, 0x00020000 };
    uint32_t size[2] = { 0, 0 };

    if (dev->type >= INTEL_430FX) {
	/* Set temporary bases and sizes. */
	if ((dev->type >= INTEL_440BX) && (regs[0x73] & 0x80)) {
		base[0] = 0x100a0000;
		size[0] = 0x00060000;
	} else {
		if (((dev->type == INTEL_440LX) || (dev->type == INTEL_440EX)) && ((regs[0x72] & 0x07) == 0x04)) {
			base[0] = 0x000c0000;
			size[0] = 0x00010000;
		} else {
			base[0] = 0x000a0000;
			size[0] = 0x00020000;
		}
	}

	if (((regs[0x72] & 0x70) == 0x40) || ((regs[0x72] & 0x08) && !(regs[0x72] & 0x20))) {
		smram[0].host_base = base[0];
		smram[0].ram_base = base[0] & 0x000f0000;
		smram[0].size = size[0];

		mem_mapping_set_addr(&ram_smram_mapping[0], smram[0].host_base, size[0]);
		mem_mapping_set_exec(&ram_smram_mapping[0], ram + smram[0].ram_base);

		/* If D_OPEN = 1 and D_LCK = 0, extended SMRAM is visible outside SMM. */
		i4x0_smram_map(0, base[0], size[0], ((regs[0x72] & 0x70) == 0x40));

		/* If the register is set accordingly, disable the mapping also in SMM. */
		i4x0_smram_map(1, base[0], size[0], ((regs[0x72] & 0x08) && !(regs[0x72] & 0x20)));
	}

	/* TSEG mapping. */
	if (dev->type >= INTEL_440BX) {
		if ((regs[0x72] & 0x08) && (regs[0x73] & 0x01)) {
			size[1] = (1 << (17 + ((regs[0x73] >> 1) & 0x03)));
			tom -= size[1];
			base[1] = tom;
		} else
			base[1] = size[1] = 0x00000000;

		if (size[1] != 0x00000000) {
			smram[1].host_base = base[1] + (1 << 28);
			smram[1].ram_base = base[1];
			smram[1].size = size[1];

			mem_mapping_set_addr(&ram_smram_mapping[1], smram[1].host_base, smram[1].size);
			if (smram[1].ram_base < (1 << 30))
				mem_mapping_set_exec(&ram_smram_mapping[1], ram + smram[1].ram_base);
			else
				mem_mapping_set_exec(&ram_smram_mapping[1], ram2 + smram[1].ram_base - (1 << 30));

			mem_set_mem_state_smm(base[1], size[1], MEM_READ_EXTANY | MEM_WRITE_EXTANY);
			i4x0_smram_map(1, smram[1].host_base, size[1], 1);
		}
	}
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

	if (((((regs[0x72] & 0x38) == 0x20) || s) || (!(regs[0x72] & 0x10) || s)) && (size[0] != 0x00000000)) {
		smram[0].host_base = base[0];
		smram[0].ram_base = base[0];
		smram[0].size = size[0];

		mem_mapping_set_addr(&ram_smram_mapping[0], smram[0].host_base, size[0]);
		mem_mapping_set_exec(&ram_smram_mapping[0], ram + smram[0].ram_base);

		/* If OSS = 1 and LSS = 0, extended SMRAM is visible outside SMM. */
		i4x0_smram_map(0, base[0], size[0], (((regs[0x72] & 0x38) == 0x20) || s));

		/* If the register is set accordingly, disable the mapping also in SMM. */
		i4x0_smram_map(0, base[0], size[0], (!(regs[0x72] & 0x10) || s));
	}
    }

    flushmmucache();
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
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
			default:
				regs[0x04] = (regs[0x04] & ~0x42) | (val & 0x42);
				break;
			case INTEL_430FX: case INTEL_430HX: case INTEL_430VX: case INTEL_430TX:
			case INTEL_440FX: case INTEL_440LX: case INTEL_440EX:
				regs[0x04] = (regs[0x04] & ~0x02) | (val & 0x02);
				break;
		}
		break;
	case 0x05:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX: case INTEL_430LX: case INTEL_430NX: case INTEL_430HX:
			case INTEL_440FX: case INTEL_440LX: case INTEL_440EX:
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
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
			case INTEL_430FX: case INTEL_430VX: case INTEL_430TX:
			case INTEL_440LX: case INTEL_440EX:
				regs[0x07] &= ~(val & 0x30);
				break;
			case INTEL_440FX:
				regs[0x07] &= ~(val & 0xf9);
				break;
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
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
			case INTEL_430FX: case INTEL_430HX: case INTEL_430VX: case INTEL_430TX:
				regs[0x0f] = (val & 0x40);
				break;
		}
		break;
	case 0x12:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
				regs[0x12] = (val & 0xc0);
				i4x0_mask_bar(regs);
				break;
		}
		break;
	case 0x13:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
				regs[0x13] = val;
				i4x0_mask_bar(regs);
				break;
		}
		break;
	case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
				if (!regs_l[addr]) {
					regs[addr] = val;
					regs_l[addr] = 1;
				}
				break;
		}
		break;

	case 0x34:
		switch (dev->type) {
			case INTEL_440LX: case INTEL_440EX:
				regs[0x34] = (val & 0xa0);
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
			case INTEL_430FX:
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
			case INTEL_440LX:
				regs[0x50] = (val & 0x03);
				break;
			case INTEL_440EX:
				regs[0x50] = (val & 0x23);
				break;
			case INTEL_440BX:
				regs[0x50] = (regs[0x50] & 0x14) | (val & 0xeb);
				break;
			case INTEL_440GX:
			/* TODO: Understand it more specifically */
				regs[0x50] = (regs[0x50] & 0x2b) | (val & 0x28);
			 /*regs[0x50] = (regs[0x50] & 0x2b) | (val & 0xd7);*/
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
			case INTEL_440LX: case INTEL_440EX:
				regs[0x51] = (val & 0x80);
				break;
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x51] = (regs[0x50] & 0x70) | (val & 0x8f);
				break;
			case INTEL_440GX:
            regs[0x51] = (regs[0x50] & 0x88) | (val & 0x08);
				/*regs[0x51] = (regs[0x50] & 0x88) | (val & 0x77);*/
				break;
		}
		break;
	case 0x52:	/* Cache Control Register */
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: case INTEL_430FX:
			case INTEL_430VX: case INTEL_430TX:
			default:
				regs[0x52] = (val & 0xfb);
				break;
			case INTEL_430NX: case INTEL_430HX:
			case INTEL_440FX:
				regs[0x52] = val;
				break;
			case INTEL_440LX:
				regs[0x52] = (val & 0xd0);
				break;
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
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
			case INTEL_440LX:
				regs[0x53] = val & 0x0a;
				break;
			case INTEL_440EX: case INTEL_440BX: case INTEL_440GX:
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
			case INTEL_440LX:
				regs[0x54] = val;
				break;
		}
		break;
	case 0x55:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
				/* According to the FreeBSD 3.x source code, the 420TX/ZX chipset has
				   this register. The mask is unknown, so write all bits. */
				regs[0x55] = val;
				break;
			case INTEL_430VX: case INTEL_430TX:
				regs[0x55] = val & 0x01;
				break;
			case INTEL_440FX: case INTEL_440LX:
			case INTEL_440EX:
				regs[0x55] = val;
				break;
		}
		break;
	case 0x56:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
				/* According to the FreeBSD 3.x source code, the 420TX/ZX chipset has
				   this register. The mask is unknown, so write all bits. */
				regs[0x56] = val;
				break;
			case INTEL_430HX:
				regs[0x56] = val & 0x1f;
				break;
			case INTEL_430VX:
				regs[0x56] = val & 0x77;
				break;
			case INTEL_430TX:
				regs[0x56] = val & 0x76;
				break;
			case INTEL_440FX: case INTEL_440LX:
			case INTEL_440EX:
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
			case INTEL_430NX: case INTEL_440EX:
				regs[0x57] = val;
				break;
			case INTEL_430FX: case INTEL_430HX:
			case INTEL_430VX:
				regs[0x57] = val & 0xcf;
				break;
			case INTEL_430TX:
				regs[0x57] = val & 0xdf;
				break;
			case INTEL_440FX:
				regs[0x57] = val & 0x77;
				break;
			case INTEL_440LX:
				regs[0x57] = val & 0x11;
				break;
			case INTEL_440BX: case INTEL_440GX:
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
			case INTEL_430NX: case INTEL_440LX:
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x58] = val & 0x03;
				break;
			case INTEL_430FX: case INTEL_440FX:
				regs[0x58] = val & 0x7f;
				break;
			case INTEL_430HX: case INTEL_430VX:
				regs[0x57] = val;
				break;
			case INTEL_430TX:
				regs[0x57] = val & 0x7b;
				break;
			case INTEL_440EX:
				regs[0x58] = val & 0xbf;
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
		if ((addr & 0x7) <= dev->max_drb) {
			spd_write_drbs(regs, 0x60, 0x60 + dev->max_drb, dev->drb_unit);
			break;
		}
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: case INTEL_430NX:
			case INTEL_430HX:
			case INTEL_440FX:
			case INTEL_440LX: case INTEL_440EX:
			case INTEL_440BX: case INTEL_440ZX:
			default:
				regs[addr] = val;
				break;
			case INTEL_430FX: case INTEL_430VX:
				regs[addr] = val & 0x3f;
				break;
			case INTEL_430TX:
				regs[addr] = val & 0x7f;
				break;
		}
		break;
	case 0x65:
		if ((addr & 0x7) <= dev->max_drb) {
			spd_write_drbs(regs, 0x60, 0x60 + dev->max_drb, dev->drb_unit);
			break;
		}
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: case INTEL_430NX:
			case INTEL_430HX:
			case INTEL_440FX:
			case INTEL_440LX: case INTEL_440EX:
			case INTEL_440GX:
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
		if ((addr & 0x7) <= dev->max_drb) {
			spd_write_drbs(regs, 0x60, 0x60 + dev->max_drb, dev->drb_unit);
			break;
		}
		switch (dev->type) {
			case INTEL_430NX: case INTEL_430HX:
			case INTEL_440FX: case INTEL_440LX:
			case INTEL_440EX: case INTEL_440GX:
			case INTEL_440BX: case INTEL_440ZX:
				regs[addr] = val;
				break;
		}
		break;
	case 0x67:
		if ((addr & 0x7) <= dev->max_drb) {
			spd_write_drbs(regs, 0x60, 0x60 + dev->max_drb, dev->drb_unit);
			break;
		}
		switch (dev->type) {
			case INTEL_430NX: case INTEL_430HX:	
			case INTEL_440FX: case INTEL_440LX:
			case INTEL_440EX:
			case INTEL_440BX: case INTEL_440GX:
			case INTEL_440ZX:
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
			case INTEL_430FX:
				regs[0x68] = val & 0x1f;
				break;
			case INTEL_440FX: case INTEL_440LX:
			case INTEL_440EX:
				regs[0x68] = val & 0xc0;
				break;
			case INTEL_440BX:
				regs[0x68] = (regs[0x68] & 0x38) | (val & 0xc7);
				break;
			case INTEL_440GX:
				regs[0x68] = (regs[0x68] & 0xc0) | (val & 0x3f);
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
         case INTEL_440GX:
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
			case INTEL_440LX:
			case INTEL_440EX:
			case INTEL_440BX:
         case INTEL_440GX:
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
			case INTEL_440LX:
			case INTEL_440EX:
			case INTEL_440BX:
         case INTEL_440GX:
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
	case 0x6f:
		switch (dev->type){
			case INTEL_440LX:
			case INTEL_440EX:
			regs[addr] = val;
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
			case INTEL_440FX: case INTEL_440LX:
			case INTEL_440EX:
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
			case INTEL_430TX: case INTEL_440EX:
				regs[addr] = val;
				break;
			case INTEL_440FX: case INTEL_440LX:
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
				if ((dev->type == INTEL_440LX) || (dev->type == INTEL_440EX))
					regs[0x72] = (regs[0x72] & 0x80) | (val & 0x7f);
				else
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
					regs[0x72] &= 0xdf;
			}
		}
		i4x0_smram_handler_phase1(dev);
		break;
	case 0x73:
		switch (dev->type) {
			case INTEL_430VX:
				regs[0x73] = val & 0x03;
				break;
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
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
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
				regs[addr] = val;
		}
		break;
	case 0x77:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
				regs[0x77] = val & 0x03;
		}
		break;
	case 0x78:
		switch (dev->type) {
			case INTEL_430VX:
				regs[0x78] = val & 0xcf;
				break;
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
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
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
				regs[0x79] = val;
				break;
		}
		break;
	case 0x7a:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX: case INTEL_440ZX:
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
			case INTEL_440BX:  case INTEL_440GX:
			case INTEL_440ZX:
				regs[0x7c] = val & 0x1f;
				break;
		}
	case 0x7d:
		switch (dev->type) {
			case INTEL_420TX: case INTEL_420ZX:
			case INTEL_430LX: case INTEL_430NX:
				regs[0x7d] = val & 0x32;
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
			case INTEL_440LX:
				regs[0x80] = val & 0x08;
				break;
			case INTEL_440EX: case INTEL_440GX:
				regs[0x80] = val & 0x18;
				break;
			case INTEL_440BX: case INTEL_440ZX:
				regs[0x7c] = val;
				break;
		}
		break;
	case 0x91:
		switch (dev->type) {
			case INTEL_430HX: case INTEL_440BX:
			case INTEL_440FX: case INTEL_440LX:
			case INTEL_440EX: case INTEL_440GX:
				/* Not applicable on 82443ZX. */
				regs[0x91] &= ~(val & 0x11);
				break;
		}
		break;
	case 0x92:
		switch (dev->type) {
			case INTEL_440LX: case INTEL_440EX:
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[0x92] &= ~(val & 0x1f);
				break;
		}
		break;
	case 0x93:
		switch (dev->type) {
			case INTEL_440FX:
			case INTEL_440LX:
			case INTEL_440EX:
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
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[0xb0] = (val & 0x80);
				break;
		}
		break;
	case 0xb1:
		switch (dev->type) {
			case INTEL_440EX:
				regs[0xb1] = (val & 0x22);
				break;
			case INTEL_440BX: case INTEL_440ZX:
				regs[0xb1] = (val & 0xa0);
				break;
			case INTEL_440GX:
				regs[0xb1] = (val & 0xa2);
				break;
		}
		break;
	case 0xb4:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[0xb4] = (val & 0x3f);
				i4x0_mask_bar(regs);
				break;
		}
		break;
	case 0xb9:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[0xb9] = (val & 0xf0);
				break;
		}
		break;

	case 0xba: case 0xbb:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[addr] = val;
				break;
		}
		break;

	case 0xbc:
		switch (dev->type) {
			case INTEL_440EX: case INTEL_440GX:
				regs[addr] = (val & 0xf8);
				break;
		}
		break;

	case 0xbd:
		switch (dev->type) {
			case INTEL_440EX: case INTEL_440GX:
				regs[addr] = (val & 0xf8);
				break;
		}
		break;

	case 0xd0: case 0xd1: case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6: case 0xd7:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[addr] = val;
				break;
		}
		break;
	case 0xca:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
				regs[addr] = val;
				break;
			case INTEL_440ZX:
				regs[addr] = val & 0xe7;
				break;
		}
		break;
	case 0xcb:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
				regs[addr] = val;
				break;
			case INTEL_440ZX:
				regs[addr] = val & 0xa7;
				break;
		}
		break;
	case 0xcc:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
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
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				if (!regs_l[addr])
					regs[addr] = val;
				break;
		}
		break;
	case 0xe5: case 0xed:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				if (!regs_l[addr])
					regs[addr] = (val & 0x3f);
				break;
		}
		break;
	case 0xe7:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
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
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[0xf0] = (val & 0xc0);
				break;
		}
		break;
	case 0xf1:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
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
         case INTEL_440GX:
            regs[0x04] = val;
		}
		break;
	case 0x05:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[0x05] = (val & 0x01);
				break;
		}
		break;
	case 0x0d: case 0x1b:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[addr] = (val & 0xf8);
				break;
		}
		break;
	case 0x19: case 0x1a:
	case 0x21: case 0x23:
	case 0x25: case 0x27:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[addr] = val;
				break;
		}
		break;
	case 0x1c: case 0x1d:
	case 0x20: case 0x22:
	case 0x24: case 0x26:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[addr] = (val & 0xf0);
				break;
		}
		break;
	case 0x1f:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
				regs[0x1f] &= ~(val & 0xf0);
				break;
		}
		break;
	case 0x3e:
		switch (dev->type) {
			case INTEL_440BX: case INTEL_440GX:
         case INTEL_440ZX:
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

    for (i = 0; i <= dev->max_drb; i++)
	i4x0_write(0, 0x60 + i, dev->drb_default, priv);

    if (dev->type >= INTEL_430FX) {
	dev->regs[0][0x72] &= 0xef;	/* Forcibly unlock the SMRAM register. */
	i4x0_write(0, 0x72, 0x02, priv);
    } else {
	dev->regs[0][0x72] &= 0xf7;	/* Forcibly unlock the SMRAM register. */
	i4x0_write(0, 0x72, 0x00, priv);
    }

    if ((dev->type == INTEL_440LX) || (dev->type == INTEL_440BX) || (dev->type == INTEL_440ZX)) {
	for (i = 0; i <= dev->max_func; i++)
		memset(dev->regs_locked[i], 0x00, 256 * sizeof(uint8_t));
    }
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

    regs[0x00] = 0x86; regs[0x01] = 0x80; /*Intel*/

    switch (dev->type) {
	case INTEL_420TX:
	case INTEL_420ZX:
		regs[0x02] = 0x83; regs[0x03] = 0x04;	/* 82424TX/ZX */
		regs[0x06] = 0x40;
		regs[0x08] = (dev->type == INTEL_420ZX) ? 0x01 : 0x00;
		regs[0x0d] = 0x20;
		/* According to information from FreeBSD 3.x source code:
			0x00 = 486DX, 0x20 = 486SX, 0x40 = 486DX2 or 486DX4, 0x80 = Pentium OverDrive. */
		if (is486sx)
			regs[0x50] = 0x20;
		else if (is486sx2)
			regs[0x50] = 0x60;	/* Guess based on the SX, DX, and DX2 values. */
		else if (is486dx)
			regs[0x50] = 0x00;
		else if (is486dx2 || isdx4)
			regs[0x50] = 0x40;
		else
			regs[0x50] = 0x80;	/* Pentium OverDrive. */
		/* According to information from FreeBSD 3.x source code:
			00 = 25 MHz, 01 = 33 MHz. */
		if (cpu_busspeed > 25000000)
			regs[0x50] |= 0x01;
		regs[0x51] = 0x80;
		/* According to information from FreeBSD 3.x source code:
			0x00 = None, 0x01 = 64 kB, 0x41 = 128 kB, 0x81 = 256 kB, 0xc1 = 512 kB,
			If bit 0 is set, then if bit 2 is also set, the cache is write back,
			otherwise it's write through. */
		regs[0x52] = 0xc3;		/* 512 kB writeback cache */
		regs[0x57] = 0x31;
		regs[0x59] = 0x0f;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = regs[0x65] = 0x02;
		dev->max_drb = 5;
		dev->drb_unit = 4;
		dev->drb_default = 0x02;
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
		dev->max_drb = 5;
		dev->drb_unit = 4;
		dev->drb_default = 0x02;
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
		dev->max_drb = 7;
		dev->drb_unit = 4;
		dev->drb_default = 0x02;
		break;
	case INTEL_430FX:
		regs[0x02] = 0x2d; regs[0x03] = 0x12;	/* SB82437FX-66 */
		regs[0x08] = (info->local >> 8) & 0xff;
		regs[0x52] = 0xb2;	/* 512 kB PLB cache, set to 0x42 for 256 kB */
		if (cpu_busspeed <= 50000000)
			regs[0x57] |= 0x01;
		else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
			regs[0x57] |= 0x02;
		else if ((cpu_busspeed > 60000000) && (cpu_busspeed <= 66666667))
			regs[0x57] |= 0x03;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = 0x02;
		regs[0x72] = 0x02;
		dev->max_drb = 4;
		dev->drb_unit = 4;
		dev->drb_default = 0x02;
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
		dev->max_drb = 7;
		dev->drb_unit = 4;
		dev->drb_default = 0x02;
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
		dev->max_drb = 4;
		dev->drb_unit = 4;
		dev->drb_default = 0x02;
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
		dev->max_drb = 5;
		dev->drb_unit = 4;
		dev->drb_default = 0x02;
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
		dev->max_drb = 7;
		dev->drb_unit = 8;
		dev->drb_default = 0x02;
		break;
	case INTEL_440LX:
		dev->max_func = 1;

		regs[0x02] = 0x80; regs[0x03] = 0x71;	/* 82443LX */
		regs[0x06] = 0x90;
		regs[0x10] = 0x08;
		regs[0x34] = 0xa0;
		if (cpu_busspeed <= 66666667)
			regs[0x51] |= 0x00;
		else if ((cpu_busspeed > 66666667) && (cpu_busspeed <= 100000000))
			regs[0x51] |= 0x20;
		regs[0x53] = 0x83;
		regs[0x57] = 0x28;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = regs[0x65] = regs[0x66] = regs[0x67] = 0x01;
		regs[0x6c] = regs[0x6d] = regs[0x6e] = regs[0x6f] = 0x55;
		regs[0x72] = 0x02;
		regs[0xa0] = 0x02;
		regs[0xa2] = 0x10;
		regs[0xa4] = 0x03;
		regs[0xa5] = 0x02;
		regs[0xa7] = 0x1f;
		dev->max_drb = 7;
		dev->drb_unit = 8;
		dev->drb_default = 0x01;
		break;
	case INTEL_440EX:
		dev->max_func = 1;

		regs[0x02] = 0x80; regs[0x03] = 0x71;	/* 82443EX. Same Vendor ID as 440LX */
		regs[0x06] = 0x90;
		regs[0x10] = 0x08;
		regs[0x34] = 0xa0;
		if (cpu_busspeed <= 66666667)
			regs[0x51] |= 0x00;
		else if ((cpu_busspeed > 66666667) && (cpu_busspeed <= 100000000))
			regs[0x51] |= 0x20;
		regs[0x53] = 0x83;
		regs[0x57] = 0x28;
		regs[0x60] = regs[0x61] = regs[0x62] = regs[0x63] = regs[0x64] = regs[0x65] = regs[0x66] = regs[0x67] = 0x01;
		regs[0x6c] = regs[0x6d] = regs[0x6e] = regs[0x6f] = 0x55;
		regs[0x72] = 0x02;
		regs[0xa0] = 0x02;
		regs[0xa2] = 0x10;
		regs[0xa4] = 0x03;
		regs[0xa5] = 0x02;
		regs[0xa7] = 0x1f;
		dev->max_drb = 7;
		dev->drb_unit = 8;
		dev->drb_default = 0x01;
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
			regs[0x51] |= 0x20;
		else if ((cpu_busspeed > 66666667) && (cpu_busspeed <= 100000000))
			regs[0x51] |= 0x00;
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
		dev->max_drb = 7;
		dev->drb_unit = 8;
		dev->drb_default = 0x01;
		break;
	case INTEL_440GX:
		regs[0x7a] = (info->local >> 8) & 0xff;
		dev->max_func = (regs[0x7a] & 0x02) ? 0 : 1;

		regs[0x02] = (regs[0x7a] & 0x02) ? 0xa2 : 0xa0; regs[0x03] = 0x71;	/* 82443GX */
		regs[0x06] = (regs[0x7a] & 0x02) ? 0x00 : 0x10;
		regs[0x08] = 0x02;
		regs[0x10] = 0x08;
		regs[0x34] = (regs[0x7a] & 0x02) ? 0x00 : 0xa0;
		regs[0x51] |= 0x20;
		regs[0x57] = 0x28;
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
		dev->max_drb = 7;
		dev->drb_unit = 8;
		dev->drb_default = 0x01;
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

    if (((dev->type == INTEL_440LX) || (dev->type == INTEL_440EX)) && (dev->max_func == 1)) {
	regs = (uint8_t *) dev->regs[1];

	regs[0x00] = 0x86; regs[0x01] = 0x80;	/* Intel */
	regs[0x02] = 0x81; regs[0x03] = 0x71;	/* 82443LX */
	regs[0x06] = 0xa0; regs[0x07] = 0x02;
	regs[0x0a] = 0x04; regs[0x0b] = 0x06;
	regs[0x0e] = 0x01;
	regs[0x1c] = 0xf0;
	regs[0x1e] = 0xa0; regs[0x1f] = 0x02;
	regs[0x20] = 0xf0; regs[0x21] = 0xff;
	regs[0x24] = 0xf0; regs[0x25] = 0xff;
    }

    if (((dev->type == INTEL_440BX) || (dev->type == INTEL_440GX) || (dev->type == INTEL_440ZX)) && (dev->max_func == 1)) {
	regs = (uint8_t *) dev->regs[1];

	regs[0x00] = 0x86; regs[0x01] = 0x80;	/* Intel */
	if(dev->type != INTEL_440GX) {
		regs[0x02] = 0x91; regs[0x03] = 0x71;	/* 82443BX */
	} else {
		regs[0x02] = 0xa1; regs[0x03] = 0x71; /* 82443GX (They seem to share the same deal*/
	}
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


const device_t i430fx_rev02_device =
{
    "Intel SB82437FX-66 (Rev. 02)",
    DEVICE_PCI,
    0x0200 | INTEL_430FX,
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

const device_t i440lx_device =
{
    "Intel 82443LX",
    DEVICE_PCI,
    INTEL_440LX,
    i4x0_init, 
    i4x0_close, 
    i4x0_reset,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t i440ex_device =
{
    "Intel 82443EX",
    DEVICE_PCI,
    INTEL_440EX,
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

const device_t i440gx_device =
{
    "Intel 82443GX",
    DEVICE_PCI,
    0x8000 | INTEL_440GX,
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

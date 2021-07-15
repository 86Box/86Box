/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the SiS 85c401/85c402, 85c460, 85c461, and
 *		85c407/85c471 chipsets.
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019,2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/port_92.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/pic.h>
#include <86box/machine.h>
#include <86box/chipset.h>


typedef struct
{
    uint8_t	cur_reg, tries,
		reg_base, reg_last,
		reg_00, is_471,
		regs[39], scratch[2];
    smram_t	*smram;
    port_92_t	*port_92;
} sis_85c4xx_t;


static void
sis_85c4xx_recalcmapping(sis_85c4xx_t *dev)
{
    uint32_t base;
    uint32_t i, shflags = 0;
    uint32_t readext, writeext;
    uint8_t romcs = 0xc0, cur_romcs;

    shadowbios = 0;
    shadowbios_write = 0;

    if (dev->regs[0x03] & 0x40)
	romcs |= 0x01;
    if (dev->regs[0x03] & 0x80)
	romcs |= 0x30;
    if (dev->regs[0x08] & 0x04)
	romcs |= 0x02;

    for (i = 0; i < 8; i++) {
	base = 0xc0000 + (i << 15);
	cur_romcs = romcs & (1 << i);
	readext = cur_romcs ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
	writeext = cur_romcs ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;

	if ((i > 5) || (dev->regs[0x02] & (1 << i))) {
		shadowbios |= (base >= 0xe0000) && (dev->regs[0x02] & 0x80);
		shadowbios_write |= (base >= 0xe0000) && !(dev->regs[0x02] & 0x40);
		shflags = (dev->regs[0x02] & 0x80) ? MEM_READ_INTERNAL : readext;
		shflags |= (dev->regs[0x02] & 0x40) ? writeext : MEM_WRITE_INTERNAL;
		mem_set_mem_state(base, 0x8000, shflags);
	} else
		mem_set_mem_state(base, 0x8000, readext | writeext);
    }

    flushmmucache_nopc();
}


static void
sis_85c4xx_sw_smi_out(uint16_t port, uint8_t val, void *priv)
{
    sis_85c4xx_t *dev = (sis_85c4xx_t *) priv;

    if (dev->regs[0x18] & 0x02) {
	if (dev->regs[0x0b] & 0x10)
		smi_line = 1;
	else
		picint(1 << ((dev->regs[0x0b] & 0x08) ? 15 : 12));
	soft_reset_mask = 1;
	dev->regs[0x19] |= 0x02;
    }
}


static void
sis_85c4xx_sw_smi_handler(sis_85c4xx_t *dev)
{
    uint16_t addr;

    if (!dev->is_471)
	return;

    addr = dev->regs[0x14] | (dev->regs[0x15] << 8);

    io_handler((dev->regs[0x0b] & 0x80) && (dev->regs[0x18] & 0x02), addr, 0x0001,
		  NULL, NULL, NULL, sis_85c4xx_sw_smi_out, NULL, NULL, dev);
}


static void
sis_85c4xx_out(uint16_t port, uint8_t val, void *priv)
{
    sis_85c4xx_t *dev = (sis_85c4xx_t *) priv;
    uint8_t rel_reg = dev->cur_reg - dev->reg_base;
    uint8_t valxor = 0x00;
    uint32_t host_base = 0x000e0000, ram_base = 0x000a0000;

    switch (port) {
	case 0x22:
		dev->cur_reg = val;
		break;
	case 0x23:
		if ((dev->cur_reg >= dev->reg_base) && (dev->cur_reg <= dev->reg_last)) {
			valxor = val ^ dev->regs[rel_reg];
			if (rel_reg == 0x19)
				dev->regs[rel_reg] &= ~val;
			else
				dev->regs[rel_reg] = val;

			switch (rel_reg) {
				case 0x01:
					cpu_cache_ext_enabled = ((val & 0x84) == 0x84);
					cpu_update_waitstates();
					break;

				case 0x02: case 0x03:
				case 0x08:
					sis_85c4xx_recalcmapping(dev);
					break;

				case 0x0b:
					sis_85c4xx_sw_smi_handler(dev);
					if (dev->is_471 && (valxor & 0x02)) {
						if (val & 0x02)
							mem_remap_top(0);
						else
							mem_remap_top(256);
					}
					break;

				case 0x13:
					if (dev->is_471 && (valxor & 0xf0)) {
						smram_disable(dev->smram);
						host_base = (val & 0x80) ? 0x00060000 : 0x000e0000;
						switch ((val >> 5) & 0x03) {
							case 0x00:
								ram_base = 0x000a0000;
								break;
							case 0x01:
								ram_base = 0x000b0000;
								break;
							case 0x02:
								ram_base = (val & 0x80) ? 0x00000000 : 0x000e0000;
								break;
							default:
								ram_base = 0x00000000;
								break;
						}
						if (ram_base != 0x00000000)
							smram_enable(dev->smram, host_base, ram_base, 0x00010000, (val & 0x10), 1);
					}
					break;

				case 0x14: case 0x15:
				case 0x18:
					sis_85c4xx_sw_smi_handler(dev);
					break;

				case 0x1c:
					if (dev->is_471)
						soft_reset_mask = 0;
					break;

				case 0x22:
					if (dev->is_471 && (valxor & 0x01)) {
						port_92_remove(dev->port_92);
						if (val & 0x01)
							port_92_add(dev->port_92);
					}
					break;
			}
		} else if ((dev->reg_base == 0x60) && (dev->cur_reg == 0x00))
			dev->reg_00 = val;
		dev->cur_reg = 0x00;
		break;

	case 0xe1: case 0xe2:
		dev->scratch[port - 0xe1] = val;
		return;
    }
}


static uint8_t
sis_85c4xx_in(uint16_t port, void *priv)
{
    sis_85c4xx_t *dev = (sis_85c4xx_t *) priv;
    uint8_t rel_reg = dev->cur_reg - dev->reg_base;
    uint8_t ret = 0xff;

    switch (port) {
	case 0x23:
		if (dev->is_471 && (dev->cur_reg == 0x1c))
			ret = inb(0x70);
		/* On the SiS 40x, the shadow RAM read and write enable bits are write-only! */
		if ((dev->reg_base == 0x60) && (dev->cur_reg == 0x62))
			ret = dev->regs[rel_reg] & 0x3f;
		else if ((dev->cur_reg >= dev->reg_base) && (dev->cur_reg <= dev->reg_last))
			ret = dev->regs[rel_reg];
		else if ((dev->reg_base == 0x60) && (dev->cur_reg == 0x00))
			ret = dev->reg_00;
		if (dev->reg_base != 0x60)
			dev->cur_reg = 0x00;
		break;

	case 0xe1: case 0xe2:
		ret = dev->scratch[port - 0xe1];
    }

    return ret;
}


static void
sis_85c4xx_close(void *priv)
{
    sis_85c4xx_t *dev = (sis_85c4xx_t *) priv;

    if (dev->is_471)
	smram_del(dev->smram);

    free(dev);
}


static void *
sis_85c4xx_init(const device_t *info)
{
    int mem_size_mb;

    sis_85c4xx_t *dev = (sis_85c4xx_t *) malloc(sizeof(sis_85c4xx_t));
    memset(dev, 0, sizeof(sis_85c4xx_t));

    dev->is_471 = (info->local >> 8) & 0xff;

    dev->reg_base = info->local & 0xff;

    mem_size_mb = mem_size >> 10;

    if (cpu_s->rspeed < 25000000)
	dev->regs[0x08] = 0x80;

    if (dev->is_471) {
	dev->reg_last = dev->reg_base + 0x76;

	dev->regs[0x09] = 0x40;
	switch (mem_size_mb) {
		case 0: case 1:
			dev->regs[0x09] |= 0x00;
			break;
		case 2: case 3:
			dev->regs[0x09] |= 0x01;
			break;
		case 4:
			dev->regs[0x09] |= 0x02;
			break;
		case 5:
			dev->regs[0x09] |= 0x20;
			break;
		case 6: case 7:
			dev->regs[0x09] |= 0x09;
			break;
		case 8: case 9:
			dev->regs[0x09] |= 0x04;
			break;
		case 10: case 11:
			dev->regs[0x09] |= 0x05;
			break;
		case 12: case 13: case 14: case 15:
			dev->regs[0x09] |= 0x0b;
			break;
		case 16:
			dev->regs[0x09] |= 0x13;
			break;
		case 17:
			dev->regs[0x09] |= 0x21;
			break;
		case 18: case 19:
			dev->regs[0x09] |= 0x06;
			break;
		case 20: case 21: case 22: case 23:
			dev->regs[0x09] |= 0x0d;
			break;
		case 24: case 25: case 26: case 27:
		case 28: case 29: case 30: case 31:
			dev->regs[0x09] |= 0x0e;
			break;
		case 32: case 33: case 34: case 35:
			dev->regs[0x09] |= 0x1b;
			break;
		case 36: case 37: case 38: case 39:
			dev->regs[0x09] |= 0x0f;
			break;
		case 40: case 41: case 42: case 43:
		case 44: case 45: case 46: case 47:
			dev->regs[0x09] |= 0x17;
			break;
		case 48:
			dev->regs[0x09] |= 0x1e;
			break;
		default:
			if (mem_size_mb < 64)
				dev->regs[0x09] |= 0x1e;
			else if ((mem_size_mb >= 65) && (mem_size_mb < 68))
				dev->regs[0x09] |= 0x22;
			else
				dev->regs[0x09] |= 0x24;
			break;
	}

	dev->regs[0x11] = 0x09;
	dev->regs[0x12] = 0xff;
	dev->regs[0x1f] = 0x20;		/* Video access enabled. */
	dev->regs[0x23] = 0xf0;
	dev->regs[0x26] = 0x01;

	dev->smram = smram_add();
	smram_enable(dev->smram, 0x000e0000, 0x000a0000, 0x00010000, 0, 1);

	dev->port_92 = device_add(&port_92_device);
	port_92_remove(dev->port_92);
    } else {
	dev->reg_last = dev->reg_base + 0x11;

	/* Bits 6 and 7 must be clear on the SiS 40x. */
	if (dev->reg_base == 0x60)
		dev->reg_00 = 0x24;

	switch (mem_size_mb) {
		case 1:
		default:
			dev->regs[0x00] = 0x00;
			break;
		case 2:
			dev->regs[0x00] = 0x01;
			break;
		case 4:
			dev->regs[0x00] = 0x02;
			break;
		case 6:
			dev->regs[0x00] = 0x03;
			break;
		case 8:
			dev->regs[0x00] = 0x04;
			break;
		case 10:
			dev->regs[0x00] = 0x05;
			break;
		case 12:
			dev->regs[0x00] = 0x0b;
			break;
		case 16:
			dev->regs[0x00] = 0x19;
			break;
		case 18:
			dev->regs[0x00] = 0x06;
			break;
		case 20:
			dev->regs[0x00] = 0x14;
			break;
		case 24:
			dev->regs[0x00] = 0x15;
			break;
		case 32:
			dev->regs[0x00] = 0x1b;
			break;
		case 36:
			dev->regs[0x00] = 0x16;
			break;
		case 40:
			dev->regs[0x00] = 0x17;
			break;
		case 48:
			dev->regs[0x00] = 0x1e;
			break;
		case 64:
			dev->regs[0x00] = 0x1f;
			break;
	}

	dev->regs[0x11] = 0x01;
    }

    io_sethandler(0x0022, 0x0002,
		  sis_85c4xx_in, NULL, NULL, sis_85c4xx_out, NULL, NULL, dev);

    dev->scratch[0] = dev->scratch[1] = 0xff;

    io_sethandler(0x00e1, 0x0002,
		  sis_85c4xx_in, NULL, NULL, sis_85c4xx_out, NULL, NULL, dev);

    sis_85c4xx_recalcmapping(dev);

    return dev;
}


const device_t sis_85c401_device = {
    "SiS 85c401/85c402",
    0,
    0x060,
    sis_85c4xx_init, sis_85c4xx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};

const device_t sis_85c460_device = {
    "SiS 85c460",
    0,
    0x050,
    sis_85c4xx_init, sis_85c4xx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};

/* TODO: Log to make sure the registers are correct. */
const device_t sis_85c461_device = {
    "SiS 85c461",
    0,
    0x050,
    sis_85c4xx_init, sis_85c4xx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};

const device_t sis_85c471_device = {
    "SiS 85c407/85c471",
    0,
    0x150,
    sis_85c4xx_init, sis_85c4xx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};

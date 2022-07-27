/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Chips&Technology's SCAT (82C235) chipset.
 *
 *		Re-worked version based on the 82C235 datasheet and errata.
 *
 *
 *
 * Authors:	Original by GreatPsycho for PCem.
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017-2019 GreatPsycho.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include "cpu.h"
#include "x86.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/port_92.h>
#include <86box/rom.h>
#include <86box/chipset.h>


#define SCAT_DMA_WAIT_STATE_CONTROL 0x01
#define SCAT_VERSION                0x40
#define SCAT_CLOCK_CONTROL          0x41
#define SCAT_PERIPHERAL_CONTROL     0x44
#define SCAT_MISCELLANEOUS_STATUS   0x45
#define SCAT_POWER_MANAGEMENT       0x46
#define SCAT_ROM_ENABLE             0x48
#define SCAT_RAM_WRITE_PROTECT      0x49
#define SCAT_SHADOW_RAM_ENABLE_1    0x4A
#define SCAT_SHADOW_RAM_ENABLE_2    0x4B
#define SCAT_SHADOW_RAM_ENABLE_3    0x4C
#define SCAT_DRAM_CONFIGURATION     0x4D
#define SCAT_EXTENDED_BOUNDARY      0x4E
#define SCAT_EMS_CONTROL            0x4F

#define SCATSX_LAPTOP_FEATURES          0x60
#define SCATSX_FAST_VIDEO_CONTROL       0x61
#define SCATSX_FAST_VIDEORAM_ENABLE     0x62
#define SCATSX_HIGH_PERFORMANCE_REFRESH 0x63
#define SCATSX_CAS_TIMING_FOR_DMA       0x64


typedef struct {
    uint8_t		valid, pad;

    uint8_t		regs_2x8;
    uint8_t		regs_2x9;

    struct scat_t *	scat;
} ems_page_t;

typedef struct scat_t {
    int			type;

    int			indx;
    uint8_t		regs[256];
    uint8_t		reg_2xA;

    uint32_t		xms_bound;

    int			external_is_RAS;

    ems_page_t		null_page, page[32];

    mem_mapping_t	low_mapping[32];
    mem_mapping_t	remap_mapping[6];
    mem_mapping_t	efff_mapping[44];
    mem_mapping_t	ems_mapping[32];
} scat_t;


static const uint8_t max_map[32] = {
    0, 1,  1,  1,  2,  3,  4,  8,
    4, 8, 12, 16, 20, 24, 28, 32,
    0, 5,  9, 13,  6, 10,  0,  0,
    0, 0,  0,  0,  0,  0,  0,  0
};
static const uint8_t max_map_sx[32] = {
    0,  1,  2,  1,  3,  4,  6, 10,
    5,  9, 13,  4,  8, 12, 16, 14,
   18, 22, 26, 20, 24, 28, 32, 18,
   20, 32,  0,  0,  0,  0,  0,  0
};
static const uint8_t scatsx_external_is_RAS[33] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 1, 1,
    0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    0
};


static uint8_t	scat_in(uint16_t port, void *priv);
static void	scat_out(uint16_t port, uint8_t val, void *priv);


static void
shadow_state_update(scat_t *dev)
{
    int i, val;

    uint32_t base, bit, romcs, shflags = 0;

    shadowbios = shadowbios_write = 0;

    for (i = 0; i < 24; i++) {
	if ((dev->regs[SCAT_DRAM_CONFIGURATION] & 0xf) < 4)
		val = 0;
	else
		val = (dev->regs[SCAT_SHADOW_RAM_ENABLE_1 + (i >> 3)] >> (i & 7)) & 1;

	base = 0xa0000 + (i << 14);
	bit = (base - 0xc0000) >> 15;
	romcs = 0;

	if (base >= 0xc0000)
		romcs = dev->regs[SCAT_ROM_ENABLE] & (1 << bit);

	if (base >= 0xe0000) {
		shadowbios |= val;
		shadowbios_write |= val;
	}

	shflags = val ? MEM_READ_INTERNAL : (romcs ? MEM_READ_EXTANY : MEM_READ_EXTERNAL);
	shflags |= (val ? MEM_WRITE_INTERNAL : (romcs ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL));

	mem_set_mem_state(base, 0x4000, shflags);
    }

    flushmmucache();
}


static void
set_xms_bound(scat_t *dev, uint8_t val)
{
    uint32_t xms_max = ((dev->regs[SCAT_VERSION] & 0xf0) != 0 && ((val & 0x10) != 0)) || (dev->regs[SCAT_VERSION] >= 4) ? 0xfe0000 : 0xfc0000;
    int i;

    switch (val & 0x0f) {
	case 1:
		dev->xms_bound = 0x100000;
		break;

	case 2:
		dev->xms_bound = 0x140000;
		break;

	case 3:
		dev->xms_bound = 0x180000;
		break;

	case 4:
		dev->xms_bound = 0x200000;
		break;

	case 5:
		dev->xms_bound = 0x300000;
		break;

	case 6:
		dev->xms_bound = 0x400000;
		break;

	case 7:
		dev->xms_bound = ((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? 0x600000 : 0x500000;
		break;

	case 8:
		dev->xms_bound = ((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? 0x800000 : 0x700000;
		break;

	case 9:
		dev->xms_bound = ((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? 0xa00000 : 0x800000;
		break;

	case 10:
		dev->xms_bound = ((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? 0xc00000 : 0x900000;
		break;

	case 11:
		dev->xms_bound = ((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? 0xe00000 : 0xa00000;
		break;

	case 12:
		dev->xms_bound = ((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? xms_max : 0xb00000;
		break;

	case 13:
		dev->xms_bound = ((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? xms_max : 0xc00000;
		break;

	case 14:
		dev->xms_bound = ((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? xms_max : 0xd00000;
		break;

	case 15:
		dev->xms_bound = ((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? xms_max : 0xf00000;
		break;

	default:
		dev->xms_bound = xms_max;
		break;
    }

    if ((((dev->regs[SCAT_VERSION] & 0xf0) == 0) && (val & 0x40) == 0 && (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x0f) == 3) ||
        (((dev->regs[SCAT_VERSION] & 0xf0) != 0) && (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) == 3)) {
	if ((val & 0x0f) == 0 || dev->xms_bound > 0x160000)
		dev->xms_bound = 0x160000;

	if (dev->xms_bound > 0x100000)
		mem_set_mem_state(0x100000, dev->xms_bound - 0x100000,
				  MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

	if (dev->xms_bound < 0x160000)
		mem_set_mem_state(dev->xms_bound, 0x160000 - dev->xms_bound,
				  MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    } else {
	if (dev->xms_bound > xms_max)
		dev->xms_bound = xms_max;

	if (dev->xms_bound > 0x100000)
		mem_set_mem_state(0x100000, dev->xms_bound - 0x100000,
				  MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

	if (dev->xms_bound < ((uint32_t)mem_size << 10))
		mem_set_mem_state(dev->xms_bound, (mem_size << 10) - dev->xms_bound,
				  MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }

    mem_mapping_set_addr(&dev->low_mapping[31], 0xf80000,
		     ((dev->regs[SCAT_VERSION] & 0xf0) != 0 && ((val & 0x10) != 0)) ||
		     (dev->regs[SCAT_VERSION] >= 4) ? 0x60000 : 0x40000);
    if (dev->regs[SCAT_VERSION] & 0xf0) {
	for (i = 0; i < 8; i++) {
		if (val & 0x10)
			mem_mapping_disable(&bios_high_mapping);
		else
			mem_mapping_enable(&bios_high_mapping);
	}
    }
}


static uint32_t
get_addr(scat_t *dev, uint32_t addr, ems_page_t *p)
{
#if 1
    int nbanks_2048k, nbanks_512k;
    uint32_t addr2;
    int nbank;
#else
    uint32_t nbanks_2048k, nbanks_512k, addr2, nbank;
#endif

    if (p && p->valid && (dev->regs[SCAT_EMS_CONTROL] & 0x80) && (p->regs_2x9 & 0x80))
	addr = (addr & 0x3fff) | (((p->regs_2x9 & 3) << 8) | p->regs_2x8) << 14;

    if ((dev->regs[SCAT_VERSION] & 0xf0) == 0) {
	switch((dev->regs[SCAT_EXTENDED_BOUNDARY] & ((dev->regs[SCAT_VERSION] & 0x0f) > 3 ? 0x40 : 0)) | (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x0f)) {
		case 0x41:
			nbank = addr >> 19;
			if (nbank < 4)
				nbank = 1;
			else if (nbank == 4)
				nbank = 0;
			else
				nbank -= 3;
			break;

		case 0x42:
			nbank = addr >> 19;
			if (nbank < 8)
				nbank = 1 + (nbank >> 2);
			else if (nbank == 8)
				nbank = 0;
			else
				nbank -= 6;
			break;

		case 0x43:
			nbank = addr >> 19;
			if (nbank < 12)
				nbank = 1 + (nbank >> 2);
			else if (nbank == 12)
				nbank = 0;
			else
				nbank -= 9;
			break;

		case 0x44:
			nbank = addr >> 19;
			if (nbank < 4)
				nbank = 2;
			else if (nbank < 6)
				nbank -= 4;
			else
				nbank -= 3;
			break;

		case 0x45:
			nbank = addr >> 19;
			if (nbank < 8)
				nbank = 2 + (nbank >> 2);
			else if (nbank < 10)
				nbank -= 8;
			else
				nbank -= 6;
			break;

		default:
			nbank = addr >> (((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x0f) < 8 && (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x40) == 0) ? 19 : 21);
			break;
	}

	nbank &= (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80) ? 7 : 3;

	if ((dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x40) == 0 &&
	    (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x0f) == 3 &&
	    nbank == 2 && (addr & 0x7ffff) < 0x60000 && mem_size > 640) {
		nbank = 1;
		addr ^= 0x70000;
	}

	if (dev->external_is_RAS && (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80) == 0) {
		if (nbank == 3)
			nbank = 7;
		else
			return 0xffffffff;
	} else if (!dev->external_is_RAS && dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80) {
		switch(nbank) {
			case 7:
				nbank = 3;
				break;

			/* Note - In the following cases, the chipset accesses multiple memory banks
				  at the same time, so it's impossible to predict which memory bank
				  is actually accessed. */
			case 5:
			case 1:
				nbank = 1;
				break;

			case 3:
				nbank = 2;
				break;

			default:
				nbank = 0;
				break;
		}
	}

	if ((dev->regs[SCAT_VERSION] & 0x0f) > 3 && (mem_size > 2048) && (mem_size & 1536)) {
		if ((mem_size & 1536) == 512) {
			if (nbank == 0)
				addr &= 0x7ffff;
			else
				addr = 0x80000 + ((addr & 0x1fffff) | ((nbank - 1) << 21));
		} else {
			if (nbank < 2)
				addr = (addr & 0x7ffff) | (nbank << 19);
			else
				addr = 0x100000 + ((addr & 0x1fffff) | ((nbank - 2) << 21));
		}
	} else {
		if (mem_size <= ((dev->regs[SCAT_VERSION] & 0x0f) > 3 ? 2048 : 4096) && (((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x0f) < 8) || dev->external_is_RAS)) {
			nbanks_2048k = 0;
			nbanks_512k = mem_size >> 9;
		} else {
			nbanks_2048k = mem_size >> 11;
			nbanks_512k = (mem_size & 1536) >> 9;
		}

		if (nbank < nbanks_2048k || (nbanks_2048k > 0 && nbank >= nbanks_2048k + nbanks_512k + ((mem_size & 511) >> 7))) {
			addr &= 0x1fffff;
			addr |= (nbank << 21);
		} else if (nbank < nbanks_2048k + nbanks_512k || nbank >= nbanks_2048k + nbanks_512k + ((mem_size & 511) >> 7)) {
			addr &= 0x7ffff;
			addr |= (nbanks_2048k << 21) | ((nbank - nbanks_2048k) << 19);
		} else {
			addr &= 0x1ffff;
			addr |= (nbanks_2048k << 21) | (nbanks_512k << 19) | ((nbank - nbanks_2048k - nbanks_512k) << 17);
		}
	}
    } else {
	switch(dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) {
		case 0x02:
		case 0x04:
			nbank = addr >> 19;
			if ((nbank & (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80 ? 7 : 3)) < 2) {
				nbank = (addr >> 10) & 1;
				addr2 = addr >> 11;
			} else
				addr2 = addr >> 10;
			break;

		case 0x03:
			nbank = addr >> 19;
			if ((nbank & (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80 ? 7 : 3)) < 2) {
				nbank = (addr >> 10) & 1;
				addr2 = addr >> 11;
			} else if ((nbank & (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80 ? 7 : 3)) == 2 && (addr & 0x7ffff) < 0x60000) {
				addr ^= 0x1f0000;
				nbank = (addr >> 10) & 1;
				addr2 = addr >> 11;
			} else
				addr2 = addr >> 10;
			break;

		case 0x05:
			nbank = addr >> 19;
			if ((nbank & (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80 ? 7 : 3)) < 4) {
				nbank = (addr >> 10) & 3;
				addr2 = addr >> 12;
			} else
				addr2 = addr >> 10;
			break;

		case 0x06:
			nbank = addr >> 19;
			if (nbank < 2) {
				nbank = (addr >> 10) & 1;
				addr2 = addr >> 11;
			} else {
				nbank = 2 + ((addr - 0x100000) >> 21);
				addr2 = (addr - 0x100000) >> 11;
			}
			break;

		case 0x07:
		case 0x0f:
			nbank = addr >> 19;
			if (nbank < 2) {
				nbank = (addr >> 10) & 1;
				addr2 = addr >> 11;
			} else if (nbank < 10) {
				nbank = 2 + (((addr - 0x100000) >> 11) & 1);
				addr2 = (addr - 0x100000) >> 12;
			} else {
				nbank = 4 + ((addr - 0x500000) >> 21);
				addr2 = (addr - 0x500000) >> 11;
			}
			break;

		case 0x08:
			nbank = addr >> 19;
			if (nbank < 4) {
				nbank = 1;
				addr2 = addr >> 11;
			} else if (nbank == 4) {
				nbank = 0;
				addr2 = addr >> 10;
			} else {
				nbank -= 3;
				addr2 = addr >> 10;
			}
			break;

		case 0x09:
			nbank = addr >> 19;
			if (nbank < 8) {
				nbank = 1 + ((addr >> 11) & 1);
				addr2 = addr >> 12;
			} else if (nbank == 8) {
				nbank = 0;
				addr2 = addr >> 10;
			} else {
				nbank -= 6;
				addr2 = addr >> 10;
			}
			break;

		case 0x0a:
			nbank = addr >> 19;
			if (nbank < 8) {
				nbank = 1 + ((addr >> 11) & 1);
				addr2 = addr >> 12;
			} else if (nbank < 12) {
				nbank = 3;
				addr2 = addr >> 11;
			} else if (nbank == 12) {
				nbank = 0;
				addr2 = addr >> 10;
			} else {
				nbank -= 9;
				addr2 = addr >> 10;
			}
			break;

		case 0x0b:
			nbank = addr >> 21;
			addr2 = addr >> 11;
			break;

		case 0x0c:
		case 0x0d:
			nbank = addr >> 21;
			if ((nbank & (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80 ? 7 : 3)) < 2) {
				nbank = (addr >> 11) & 1;
				addr2 = addr >> 12;
			} else
				addr2 = addr >> 11;
			break;

		case 0x0e:
		case 0x13:
			nbank = addr >> 21;
			if ((nbank & (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80 ? 7 : 3)) < 4) {
				nbank = (addr >> 11) & 3;
				addr2 = addr >> 13;
			} else
				addr2 = addr >> 11;
			break;

		case 0x10:
		case 0x11:
			nbank = addr >> 19;
			if (nbank < 2) {
				nbank = (addr >> 10) & 1;
				addr2 = addr >> 11;
			} else if (nbank < 10) {
				nbank = 2 + (((addr - 0x100000) >> 11) & 1);
				addr2 = (addr - 0x100000) >> 12;
			} else if (nbank < 18) {
				nbank = 4 + (((addr - 0x500000) >> 11) & 1);
				addr2 = (addr - 0x500000) >> 12;
			} else {
				nbank = 6 + ((addr - 0x900000) >> 21);
				addr2 = (addr - 0x900000) >> 11;
			}
			break;

		case 0x12:
			nbank = addr >> 19;
			if (nbank < 2) {
				nbank = (addr >> 10) & 1;
				addr2 = addr >> 11;
			} else if (nbank < 10) {
				nbank = 2 + (((addr - 0x100000) >> 11) & 1);
				addr2 = (addr - 0x100000) >> 12;
			} else {
				nbank = 4 + (((addr - 0x500000) >> 11) & 3);
				addr2 = (addr - 0x500000) >> 13;
			}
			break;

		case 0x14:
		case 0x15:
			nbank = addr >> 21;
			if ((nbank & 7) < 4) {
				nbank = (addr >> 11) & 3;
				addr2 = addr >> 13;
			} else if ((nbank & 7) < 6) {
				nbank = 4 + (((addr - 0x800000) >> 11) & 1);
				addr2 = (addr - 0x800000) >> 12;
			} else {
				nbank = 6 + (((addr - 0xc00000) >> 11) & 3);
				addr2 = (addr - 0xc00000) >> 13;
			}
			break;

		case 0x16:
			nbank = ((addr >> 21) & 4) | ((addr >> 11) & 3);
			addr2 = addr >> 13;
			break;

		case 0x17:
			if (dev->external_is_RAS && (addr & 0x800) == 0)
				return 0xffffffff;
			nbank = addr >> 19;
			if (nbank < 2) {
				nbank = (addr >> 10) & 1;
				addr2 = addr >> 11;
			} else {
				nbank = 2 + ((addr - 0x100000) >> 23);
				addr2 = (addr - 0x100000) >> 12;
			}
			break;

		case 0x18:
			if (dev->external_is_RAS && (addr & 0x800) == 0)
				return 0xffffffff;
			nbank = addr >> 21;
			if (nbank < 4) {
				nbank = 1;
				addr2 = addr >> 12;
			} else if (nbank == 4) {
				nbank = 0;
				addr2 = addr >> 11;
			} else {
				nbank -= 3;
				addr2 = addr >> 11;
			}
			break;

		case 0x19:
			if (dev->external_is_RAS && (addr & 0x800) == 0)
				return 0xffffffff;
			nbank = addr >> 23;
			if ((nbank & 3) < 2) {
				nbank = (addr >> 12) & 1;
				addr2 = addr >> 13;
			} else
				addr2 = addr >> 12;
			break;

		default:
			if ((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) < 6) {
				nbank = addr >> 19;
				addr2 = (addr >> 10) & 0x1ff;
			} else if ((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) < 0x17) {
				nbank = addr >> 21;
				addr2 = (addr >> 11) & 0x3ff;
			} else {
				nbank = addr >> 23;
				addr2 = (addr >> 12) & 0x7ff;
			}
			break;
	}

	nbank &= (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80) ? 7 : 3;

	if ((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) > 0x16 && nbank == 3)
		return 0xffffffff;

	if (dev->external_is_RAS && (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80) == 0) {
		if (nbank == 3)
			nbank = 7;
		else
			return 0xffffffff;
	} else if (!dev->external_is_RAS && dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x80) {
		switch(nbank) {
			case 7:
				nbank = 3;
				break;

			/* Note - In the following cases, the chipset accesses multiple memory banks
			  	at the same time, so it's impossible to predict which memory bank
			  	is actually accessed. */
			case 5:
			case 1:
				nbank = 1;
				break;

			case 3:
				nbank = 2;
				break;

			default:
				nbank = 0;
				break;
		}
	}

	switch(mem_size & ~511) {
		case 1024:
		case 1536:
			addr &= 0x3ff;
			if (nbank < 2)
				addr |= (nbank << 10) | ((addr2 & 0x1ff) << 11);
			else
				addr |= ((addr2 & 0x1ff) << 10) | (nbank << 19);
			break;

		case 2048:
			if ((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) == 5) {
				addr &= 0x3ff;
				if (nbank < 4)
					addr |= (nbank << 10) | ((addr2 & 0x1ff) << 12);
				else
					addr |= ((addr2 & 0x1ff) << 10) | (nbank << 19);
			} else {
				addr &= 0x7ff;
				addr |= ((addr2 & 0x3ff) << 11) | (nbank << 21);
			}
			break;

		case 2560:
			if (nbank == 0)
				addr = (addr & 0x3ff) | ((addr2 & 0x1ff) << 10);
			else {
				addr &= 0x7ff;
				addr2 &= 0x3ff;
				addr = addr + 0x80000 + ((addr2 << 11) | ((nbank - 1) << 21));
			}
			break;

		case 3072:
			if (nbank < 2)
				addr = (addr & 0x3ff) | (nbank << 10) | ((addr2 & 0x1ff) << 11);
			else
				addr = 0x100000 + ((addr & 0x7ff) | ((addr2 & 0x3ff) << 11) | ((nbank - 2) << 21));
			break;

		case 4096:
		case 6144:
			addr &= 0x7ff;
			if (nbank < 2)
				addr |= (nbank << 11) | ((addr2 & 0x3ff) << 12);
			else
				addr |= ((addr2 & 0x3ff) << 11) | (nbank << 21);
			break;

		case 4608:
			if (((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) >= 8 && (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) <= 0x0a) || ((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) == 0x18)) {
				if (nbank == 0)
					addr = (addr & 0x3ff) | ((addr2 & 0x1ff) << 10);
				else if (nbank < 3)
					addr = 0x80000 + ((addr & 0x7ff) | ((nbank - 1) << 11) | ((addr2 & 0x3ff) << 12));
				else
					addr = 0x480000 + ((addr & 0x3ff) | ((addr2 & 0x1ff) << 10) | ((nbank - 3) << 19));
			} else if (nbank == 0)
				addr = (addr & 0x3ff) | ((addr2 & 0x1ff) << 10);
			else {
				addr &= 0x7ff;
				addr2 &= 0x3ff;
				addr = addr + 0x80000 + ((addr2 << 11) | ((nbank - 1) << 21));
			}
			break;

		case 5120:
		case 7168:
			if (nbank < 2)
				addr = (addr & 0x3ff) | (nbank << 10) | ((addr2 & 0x1ff) << 11);
			else if (nbank < 4)
				addr = 0x100000 + ((addr & 0x7ff) | ((addr2 & 0x3ff) << 12) | ((nbank & 1) << 11));
			else
				addr = 0x100000 + ((addr & 0x7ff) | ((addr2 & 0x3ff) << 11) | ((nbank - 2) << 21));
			break;

		case 6656:
			if (((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) >= 8 && (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) <= 0x0a) || ((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) == 0x18)) {
				if (nbank == 0)
					addr = (addr & 0x3ff) | ((addr2 & 0x1ff) << 10);
				else if (nbank < 3)
					addr = 0x80000 + ((addr & 0x7ff) | ((nbank - 1) << 11) | ((addr2 & 0x3ff) << 12));
				else if (nbank == 3)
					addr = 0x480000 + ((addr & 0x7ff) | ((addr2 & 0x3ff) << 11));
				else
					addr = 0x680000 + ((addr & 0x3ff) | ((addr2 & 0x1ff) << 10) | ((nbank - 4) << 19));
			} else if (nbank == 0)
				addr = (addr & 0x3ff) | ((addr2 & 0x1ff) << 10);
			else if (nbank == 1) {
				addr &= 0x7ff;
				addr2 &= 0x3ff;
				addr = addr + 0x80000 + (addr2 << 11);
			} else {
				addr &= 0x7ff;
				addr2 &= 0x3ff;
				addr = addr + 0x280000 + ((addr2 << 12) | ((nbank & 1) << 11) | (((nbank - 2) & 6) << 21));
			}
			break;

		case 8192:
			addr &= 0x7ff;
			if (nbank < 4)
				addr |= (nbank << 11) | ((addr2 & 0x3ff) << 13);
			else
				addr |= ((addr2 & 0x3ff) << 11) | (nbank << 21);
			break;

		case 9216:
			if (nbank < 2)
				addr = (addr & 0x3ff) | (nbank << 10) | ((addr2 & 0x1ff) << 11);
			else if (dev->external_is_RAS) {
				if (nbank < 6)
					addr = 0x100000 + ((addr & 0x7ff) | ((addr2 & 0x3ff) << 12) | ((nbank & 1) << 11));
				else
					addr = 0x100000 + ((addr & 0x7ff) | ((addr2 & 0x3ff) << 11) | ((nbank - 2) << 21));
			} else
				addr = 0x100000 + ((addr & 0xfff) | ((addr2 & 0x7ff) << 12) | ((nbank - 2) << 23));
			break;

		case 10240:
			if (dev->external_is_RAS) {
				addr &= 0x7ff;
				if (nbank < 4)
					addr |= (nbank << 11) | ((addr2 & 0x3ff) << 13);
				else
					addr |= ((addr2 & 0x3ff) << 11) | (nbank << 21);
			} else if (nbank == 0)
				addr = (addr & 0x7ff) | ((addr2 & 0x3ff) << 11);
			else {
				addr &= 0xfff;
				addr2 &= 0x7ff;
				addr = addr + 0x200000 + ((addr2 << 12) | ((nbank - 1) << 23));
			}
			break;

		case 11264:
			if (nbank < 2)
				addr = (addr & 0x3ff) | (nbank << 10) | ((addr2 & 0x1ff) << 11);
			else if (nbank < 6)
				addr = 0x100000 + ((addr & 0x7ff) | ((addr2 & 0x3ff) << 12) | ((nbank & 1) << 11));
			else
				addr = 0x100000 + ((addr & 0x7ff) | ((addr2 & 0x3ff) << 11) | ((nbank - 2) << 21));
			break;

		case 12288:
			if (dev->external_is_RAS) {
				addr &= 0x7ff;
				if (nbank < 4)
					addr |= (nbank << 11) | ((addr2 & 0x3ff) << 13);
				else if (nbank < 6)
					addr |= ((nbank & 1) << 11) | ((addr2 & 0x3ff) << 12) | ((nbank & 4) << 21);
				else
					addr |= ((addr2 & 0x3ff) << 11) | (nbank << 21);
			} else {
				if (nbank < 2)
					addr = (addr & 0x7ff) | (nbank << 11) | ((addr2 & 0x3ff) << 12);
				else
					addr = 0x400000 + ((addr & 0xfff) | ((addr2 & 0x7ff) << 12) | ((nbank - 2) << 23));
			}
			break;

		case 13312:
			if (nbank < 2)
				addr = (addr & 0x3FF) | (nbank << 10) | ((addr2 & 0x1FF) << 11);
			else if (nbank < 4)
				addr = 0x100000 + ((addr & 0x7FF) | ((addr2 & 0x3FF) << 12) | ((nbank & 1) << 11));
			else
				addr = 0x500000 + ((addr & 0x7FF) | ((addr2 & 0x3FF) << 13) | ((nbank & 3) << 11));
			break;

		case 14336:
			addr &= 0x7ff;
			if (nbank < 4)
				addr |= (nbank << 11) | ((addr2 & 0x3ff) << 13);
			else if (nbank < 6)
				addr |= ((nbank & 1) << 11) | ((addr2 & 0x3ff) << 12) | ((nbank & 4) << 21);
			else
				addr |= ((addr2 & 0x3ff) << 11) | (nbank << 21);
			break;

		case 16384:
			if (dev->external_is_RAS) {
				addr &= 0x7ff;
				addr2 &= 0x3ff;
				addr |= ((nbank & 3) << 11) | (addr2 << 13) | ((nbank & 4) << 21);
			} else {
				addr &= 0xfff;
				addr2 &= 0x7ff;
				if (nbank < 2)
					addr |= (addr2 << 13) | (nbank << 12);
				else
					addr |= (addr2 << 12) | (nbank << 23);
			}
			break;

		default:
			if (mem_size < 2048 || ((mem_size & 1536) == 512) || (mem_size == 2048 && (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) < 6)) {
				addr &= 0x3ff;
				addr2 &= 0x1ff;
				addr |= (addr2 << 10) | (nbank << 19);
			} else if (mem_size < 8192 || (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) < 0x17) {
				addr &= 0x7ff;
				addr2 &= 0x3ff;
				addr |= (addr2 << 11) | (nbank << 21);
			} else {
				addr &= 0xfff;
				addr2 &= 0x7ff;
				addr |= (addr2 << 12) | (nbank << 23);
			}
			break;
	}
    }

    return addr;
}


static void
set_global_EMS_state(scat_t *dev, int state)
{
    uint32_t base_addr, virt_addr;
    int i, conf;

    for (i = ((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? 0 : 24; i < 32; i++) {
	base_addr = (i + 16) << 14;

	if (i >= 24)
		base_addr += 0x30000;
	if (state && (dev->page[i].regs_2x9 & 0x80)) {
		virt_addr = get_addr(dev, base_addr, &dev->page[i]);
		if (i < 24)
			mem_mapping_disable(&dev->efff_mapping[i]);
		else
			mem_mapping_disable(&dev->efff_mapping[i + 12]);
		mem_mapping_enable(&dev->ems_mapping[i]);

		if (virt_addr < ((uint32_t)mem_size << 10))
			mem_mapping_set_exec(&dev->ems_mapping[i], ram + virt_addr);
		else
			mem_mapping_set_exec(&dev->ems_mapping[i], NULL);
	} else {
		mem_mapping_set_exec(&dev->ems_mapping[i], ram + base_addr);
		mem_mapping_disable(&dev->ems_mapping[i]);

		conf = (dev->regs[SCAT_VERSION] & 0xf0) ? (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f)
							: (dev->regs[SCAT_DRAM_CONFIGURATION] & 0xf) |
							((dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x40) >> 2);
		if (i < 24) {
			if (conf > 1 || (conf == 1 && i < 16))
				mem_mapping_enable(&dev->efff_mapping[i]);
			else
				mem_mapping_disable(&dev->efff_mapping[i]);
		} else if (conf > 3 || ((dev->regs[SCAT_VERSION] & 0xf0) != 0 && conf == 2))
			mem_mapping_enable(&dev->efff_mapping[i + 12]);
		else
			mem_mapping_disable(&dev->efff_mapping[i + 12]);
	}
    }

    flushmmucache();
}


static void
memmap_state_update(scat_t *dev)
{
    uint32_t addr;
    int i;

    for (i = (((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? 0 : 16); i < 44; i++) {
	addr = get_addr(dev, 0x40000 + (i << 14), &dev->null_page);
	mem_mapping_set_exec(&dev->efff_mapping[i],
			 addr < ((uint32_t)mem_size << 10) ? ram + addr : NULL);
    }

    addr = get_addr(dev, 0, &dev->null_page);
    mem_mapping_set_exec(&dev->low_mapping[0],
		     addr < ((uint32_t)mem_size << 10) ? ram + addr : NULL);

    addr = get_addr(dev, 0xf0000, &dev->null_page);
    mem_mapping_set_exec(&dev->low_mapping[1],
		     addr < ((uint32_t)mem_size << 10) ? ram + addr : NULL);

    for (i = 2; i < 32; i++) {
	addr = get_addr(dev, i << 19, &dev->null_page);
	mem_mapping_set_exec(&dev->low_mapping[i],
			 addr < ((uint32_t)mem_size << 10) ? ram + addr : NULL);
    }

    if ((dev->regs[SCAT_VERSION] & 0xf0) == 0) {
	for (i = 0; i < max_map[(dev->regs[SCAT_DRAM_CONFIGURATION] & 0x0f) |
	     ((dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x40) >> 2)]; i++)
		mem_mapping_enable(&dev->low_mapping[i]);

	for (; i < 32; i++)
		mem_mapping_disable(&dev->low_mapping[i]);

	for (i = 24; i < 36; i++) {
		if (((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x0f) | (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x40)) < 4)
			mem_mapping_disable(&dev->efff_mapping[i]);
		else
			mem_mapping_enable(&dev->efff_mapping[i]);
	}
    } else {
	for (i = 0; i < max_map_sx[dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f]; i++)
		mem_mapping_enable(&dev->low_mapping[i]);

	for (; i < 32; i++)
		mem_mapping_disable(&dev->low_mapping[i]);

	for(i = 24; i < 36; i++) {
		if ((dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) < 2 || (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) == 3)
			mem_mapping_disable(&dev->efff_mapping[i]);
		else
			mem_mapping_enable(&dev->efff_mapping[i]);
	}
    }

    if ((((dev->regs[SCAT_VERSION] & 0xf0) == 0) &&
	  (dev->regs[SCAT_EXTENDED_BOUNDARY] & 0x40) == 0) || ((dev->regs[SCAT_VERSION] & 0xf0) != 0)) {
	if ((((dev->regs[SCAT_VERSION] & 0xf0) == 0) &&
	      (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x0f) == 3) ||
	      (((dev->regs[SCAT_VERSION] & 0xf0) != 0) && (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) == 3)) {
		mem_mapping_disable(&dev->low_mapping[2]);

		for (i = 0; i < 6; i++) {
			addr = get_addr(dev, 0x100000 + (i << 16), &dev->null_page);
			mem_mapping_set_exec(&dev->remap_mapping[i],
					 addr < ((uint32_t)mem_size << 10) ? ram + addr : NULL);
			mem_mapping_enable(&dev->remap_mapping[i]);
		}
	} else {
		for (i = 0; i < 6; i++)
			mem_mapping_disable(&dev->remap_mapping[i]);

		if ((((dev->regs[SCAT_VERSION] & 0xf0) == 0) &&
		      (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x0f) > 4) ||
		      (((dev->regs[SCAT_VERSION] & 0xf0) != 0) &&
		      (dev->regs[SCAT_DRAM_CONFIGURATION] & 0x1f) > 3))
			mem_mapping_enable(&dev->low_mapping[2]);
	}
    } else {
	for (i = 0; i < 6; i++)
		mem_mapping_disable(&dev->remap_mapping[i]);

	mem_mapping_enable(&dev->low_mapping[2]);
    }

    set_global_EMS_state(dev, dev->regs[SCAT_EMS_CONTROL] & 0x80);

    flushmmucache_cr3();
}


static void
scat_out(uint16_t port, uint8_t val, void *priv)
{
    scat_t *dev = (scat_t *)priv;
    uint8_t reg_valid = 0,
	    shadow_update = 0,
	    map_update = 0,
	    indx;
    uint32_t base_addr, virt_addr;

    switch (port) {
	case 0x22:
		dev->indx = val;
		break;

	case 0x23:
		switch (dev->indx) {
			case SCAT_DMA_WAIT_STATE_CONTROL:
			case SCAT_CLOCK_CONTROL:
			case SCAT_PERIPHERAL_CONTROL:
				reg_valid = 1;
				break;

			case SCAT_EMS_CONTROL:
				io_removehandler(0x0208, 0x0003, scat_in, NULL, NULL, scat_out, NULL, NULL, dev);
				io_removehandler(0x0218, 0x0003, scat_in, NULL, NULL, scat_out, NULL, NULL, dev);

				if (val & 0x40) {
					if (val & 1)
						io_sethandler(0x0218, 3, scat_in, NULL, NULL, scat_out, NULL, NULL, dev);
					else
						io_sethandler(0x0208, 3, scat_in, NULL, NULL, scat_out, NULL, NULL, dev);
				}
				set_global_EMS_state(dev, val & 0x80);
				reg_valid = 1;
				break;

			case SCAT_POWER_MANAGEMENT:
				/* TODO - Only use AUX parity disable bit for this version.
					  Other bits should be implemented later. */
				val &= (dev->regs[SCAT_VERSION] & 0xf0) == 0 ? 0x40 : 0x60;
				reg_valid = 1;
				break;

			case SCAT_DRAM_CONFIGURATION:
				map_update = 1;

				if ((dev->regs[SCAT_VERSION] & 0xf0) == 0) {
					cpu_waitstates = (val & 0x70) == 0 ? 1 : 2;
					cpu_update_waitstates();
				}

				reg_valid = 1;
				break;

			case SCAT_EXTENDED_BOUNDARY:
				if ((dev->regs[SCAT_VERSION] & 0xf0) == 0) {
					if (dev->regs[SCAT_VERSION] < 4) {
						val &= 0xbf;
						set_xms_bound(dev, val & 0x0f);
					} else {
						val = (val & 0x7f) | 0x80;
						set_xms_bound(dev, val & 0x4f);
					}
				} else
					set_xms_bound(dev, val & 0x1f);

				mem_set_mem_state(0x40000, 0x60000, (val & 0x20) ? MEM_READ_EXTANY | MEM_WRITE_EXTANY :
										   MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
				if ((val ^ dev->regs[SCAT_EXTENDED_BOUNDARY]) & 0xc0)
					map_update = 1;
				reg_valid = 1;
				break;

			case SCAT_ROM_ENABLE:
			case SCAT_RAM_WRITE_PROTECT:
			case SCAT_SHADOW_RAM_ENABLE_1:
			case SCAT_SHADOW_RAM_ENABLE_2:
			case SCAT_SHADOW_RAM_ENABLE_3:
				reg_valid = 1;
				shadow_update = 1;
				break;

			case SCATSX_LAPTOP_FEATURES:
				if ((dev->regs[SCAT_VERSION] & 0xf0) != 0) {
					val = (val & ~8) | (dev->regs[SCATSX_LAPTOP_FEATURES] & 8);
					reg_valid = 1;
				}
				break;

			case SCATSX_FAST_VIDEO_CONTROL:
			case SCATSX_FAST_VIDEORAM_ENABLE:
			case SCATSX_HIGH_PERFORMANCE_REFRESH:
			case SCATSX_CAS_TIMING_FOR_DMA:
				if ((dev->regs[SCAT_VERSION] & 0xf0) != 0)
					reg_valid = 1;
				break;

			default:
				break;
		}

		if (reg_valid)
			dev->regs[dev->indx] = val;

		if (shadow_update)
			shadow_state_update(dev);

		if (map_update)
			memmap_state_update(dev);
		break;

	case 0x208:
	case 0x218:
		if ((dev->regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4))) {
			if ((dev->regs[SCAT_VERSION] & 0xf0) == 0)
				indx = dev->reg_2xA & 0x1f;
			else
				indx = ((dev->reg_2xA & 0x40) >> 4) + (dev->reg_2xA & 0x3) + 24;
			dev->page[indx].regs_2x8 = val;
			base_addr = (indx + 16) << 14;
			if (indx >= 24)
				base_addr += 0x30000;

			if ((dev->regs[SCAT_EMS_CONTROL] & 0x80) && (dev->page[indx].regs_2x9 & 0x80)) {
				virt_addr = get_addr(dev, base_addr, &dev->page[indx]);
				if (virt_addr < ((uint32_t)mem_size << 10))
					mem_mapping_set_exec(&dev->ems_mapping[indx], ram + virt_addr);
				else
					mem_mapping_set_exec(&dev->ems_mapping[indx], NULL);
				flushmmucache();
			}
		}
		break;

	case 0x209:
	case 0x219:
		if ((dev->regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4))) {
			if ((dev->regs[SCAT_VERSION] & 0xf0) == 0)
				indx = dev->reg_2xA & 0x1f;
			else
				indx = ((dev->reg_2xA & 0x40) >> 4) + (dev->reg_2xA & 0x3) + 24;
			dev->page[indx].regs_2x9 = val;
			base_addr = (indx + 16) << 14;
			if (indx >= 24)
				base_addr += 0x30000;

			if (dev->regs[SCAT_EMS_CONTROL] & 0x80) {
				if (val & 0x80) {
					virt_addr = get_addr(dev, base_addr, &dev->page[indx]);
					if (indx < 24)
						mem_mapping_disable(&dev->efff_mapping[indx]);
					else
						mem_mapping_disable(&dev->efff_mapping[indx + 12]);
					if (virt_addr < ((uint32_t)mem_size << 10))
						mem_mapping_set_exec(&dev->ems_mapping[indx], ram + virt_addr);
					else
						mem_mapping_set_exec(&dev->ems_mapping[indx], NULL);
					mem_mapping_enable(&dev->ems_mapping[indx]);
				} else {
					mem_mapping_set_exec(&dev->ems_mapping[indx], ram + base_addr);
					mem_mapping_disable(&dev->ems_mapping[indx]);
					if (indx < 24)
						mem_mapping_enable(&dev->efff_mapping[indx]);
					else
						mem_mapping_enable(&dev->efff_mapping[indx + 12]);
				}

				flushmmucache();
			}

			if (dev->reg_2xA & 0x80)
				dev->reg_2xA = (dev->reg_2xA & 0xe0) | ((dev->reg_2xA + 1) & (((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? 0x1f : 3));
		}
		break;

	case 0x20a:
	case 0x21a:
		if ((dev->regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
			dev->reg_2xA = ((dev->regs[SCAT_VERSION] & 0xf0) == 0) ? val : val & 0xc3;
		break;
    }
}


static uint8_t
scat_in(uint16_t port, void *priv)
{
    scat_t *dev = (scat_t *)priv;
    uint8_t ret = 0xff, indx;

    switch (port) {
	case 0x23:
		switch (dev->indx) {
			case SCAT_MISCELLANEOUS_STATUS:
				ret = (dev->regs[dev->indx] & 0x3f) | (~nmi_mask & 0x80) | ((mem_a20_key & 2) << 5);
				break;

			case SCAT_DRAM_CONFIGURATION:
				if ((dev->regs[SCAT_VERSION] & 0xf0) == 0)
					ret = (dev->regs[dev->indx] & 0x8f) | (cpu_waitstates == 1 ? 0 : 0x10);
				else
					ret = dev->regs[dev->indx];
				break;

			case SCAT_EXTENDED_BOUNDARY:
				ret = dev->regs[dev->indx];
				if ((dev->regs[SCAT_VERSION] & 0xf0) == 0) {
					if ((dev->regs[SCAT_VERSION] & 0x0f) >= 4)
						ret |= 0x80;
					else
						ret &= 0xaf;
				}
				break;

			default:
				ret = dev->regs[dev->indx];
				break;
		}
		break;

	case 0x208:
	case 0x218:
		if ((dev->regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4))) {
			if ((dev->regs[SCAT_VERSION] & 0xf0) == 0)
				indx = dev->reg_2xA & 0x1f;
			else
				indx = ((dev->reg_2xA & 0x40) >> 4) + (dev->reg_2xA & 0x3) + 24;
			ret = dev->page[indx].regs_2x8;
		}
		break;

	case 0x209:
	case 0x219:
		if ((dev->regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4))) {
			if ((dev->regs[SCAT_VERSION] & 0xf0) == 0)
				indx = dev->reg_2xA & 0x1f;
			else
				indx = ((dev->reg_2xA & 0x40) >> 4) + (dev->reg_2xA & 0x3) + 24;
			ret = dev->page[indx].regs_2x9;
		}
		break;

	case 0x20a:
	case 0x21a:
		if ((dev->regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
			ret = dev->reg_2xA;
		break;
    }

    return ret;
}


static uint8_t
mem_read_scatb(uint32_t addr, void *priv)
{
    ems_page_t *page = (ems_page_t *)priv;
    scat_t *dev = (scat_t *)page->scat;
    uint8_t val = 0xff;

    addr = get_addr(dev, addr, page);
    if (addr < ((uint32_t)mem_size << 10))
	val = ram[addr];

    return val;
}


static uint16_t
mem_read_scatw(uint32_t addr, void *priv)
{
    ems_page_t *page = (ems_page_t *)priv;
    scat_t *dev = (scat_t *)page->scat;
    uint16_t val = 0xffff;

    addr = get_addr(dev, addr, page);
    if (addr < ((uint32_t)mem_size << 10))
	val = *(uint16_t *)&ram[addr];

    return val;
}


static uint32_t
mem_read_scatl(uint32_t addr, void *priv)
{
    ems_page_t *page = (ems_page_t *)priv;
    scat_t *dev = (scat_t *)page->scat;
    uint32_t val = 0xffffffff;

    addr = get_addr(dev, addr, page);
    if (addr < ((uint32_t)mem_size << 10))
	val = *(uint32_t *)&ram[addr];

    return val;
}


static void
mem_write_scatb(uint32_t addr, uint8_t val, void *priv)
{
    ems_page_t *page = (ems_page_t *)priv;
    scat_t *dev = (scat_t *)page->scat;
    uint32_t oldaddr = addr, chkaddr;

    addr = get_addr(dev, addr, page);
    chkaddr = page->valid ? addr : oldaddr;
    if ((chkaddr >= 0xc0000) && (chkaddr < 0x100000)) {
	if (dev->regs[SCAT_RAM_WRITE_PROTECT] & (1 << ((chkaddr - 0xc0000) >> 15)))
		return;
    }

    if (addr < ((uint32_t)mem_size << 10))
	ram[addr] = val;
}


static void
mem_write_scatw(uint32_t addr, uint16_t val, void *priv)
{
    ems_page_t *page = (ems_page_t *)priv;
    scat_t *dev = (scat_t *)page->scat;
    uint32_t oldaddr = addr, chkaddr;

    addr = get_addr(dev, addr, page);
    chkaddr = page->valid ? addr : oldaddr;
    if ((chkaddr >= 0xc0000) && (chkaddr < 0x100000)) {
	if (dev->regs[SCAT_RAM_WRITE_PROTECT] & (1 << ((chkaddr - 0xc0000) >> 15)))
		return;
    }

    if (addr < ((uint32_t)mem_size << 10))
	*(uint16_t *)&ram[addr] = val;
}


static void
mem_write_scatl(uint32_t addr, uint32_t val, void *priv)
{
    ems_page_t *page = (ems_page_t *)priv;
    scat_t *dev = (scat_t *)page->scat;
    uint32_t oldaddr = addr, chkaddr;

    addr = get_addr(dev, addr, page);
    chkaddr = page->valid ? addr : oldaddr;
    if ((chkaddr >= 0xc0000) && (chkaddr < 0x100000)) {
	if (dev->regs[SCAT_RAM_WRITE_PROTECT] & (1 << ((chkaddr - 0xc0000) >> 15)))
		return;
    }

    if (addr < ((uint32_t)mem_size << 10))
	*(uint32_t *)&ram[addr] = val;
}


static void
scat_close(void *priv)
{
    scat_t *dev = (scat_t *)priv;

    free(dev);
}


static void *
scat_init(const device_t *info)
{
    scat_t *dev;
    uint32_t i, k;
    int sx;

    dev = (scat_t *)malloc(sizeof(scat_t));
    memset(dev, 0x00, sizeof(scat_t));
    dev->type = info->local;

    sx = (dev->type == 32) ? 1 : 0;

    for (i = 0; i < sizeof(dev->regs); i++)
	dev->regs[i] = 0xff;

    if (sx) {
	dev->regs[SCAT_VERSION] = 0x13;
	dev->regs[SCAT_CLOCK_CONTROL] = 6;
	dev->regs[SCAT_PERIPHERAL_CONTROL] = 0;
	dev->regs[SCAT_DRAM_CONFIGURATION] = 1;
	dev->regs[SCATSX_LAPTOP_FEATURES] = 0;
	dev->regs[SCATSX_FAST_VIDEO_CONTROL] = 0;
	dev->regs[SCATSX_FAST_VIDEORAM_ENABLE] = 0;
	dev->regs[SCATSX_HIGH_PERFORMANCE_REFRESH] = 8;
	dev->regs[SCATSX_CAS_TIMING_FOR_DMA] = 3;
    } else {
	switch(dev->type) {
		case 4:
			dev->regs[SCAT_VERSION] = 4;
			break;

		default:
			dev->regs[SCAT_VERSION] = 1;
			break;
	}
	dev->regs[SCAT_CLOCK_CONTROL] = 2;
	dev->regs[SCAT_PERIPHERAL_CONTROL] = 0x80;
	dev->regs[SCAT_DRAM_CONFIGURATION] = cpu_waitstates == 1 ? 2 : 0x12;
    }
    dev->regs[SCAT_DMA_WAIT_STATE_CONTROL] = 0;
    dev->regs[SCAT_MISCELLANEOUS_STATUS] = 0x37;
    dev->regs[SCAT_ROM_ENABLE] = 0xc0;
    dev->regs[SCAT_RAM_WRITE_PROTECT] = 0;
    dev->regs[SCAT_POWER_MANAGEMENT] = 0;
    dev->regs[SCAT_SHADOW_RAM_ENABLE_1] = 0;
    dev->regs[SCAT_SHADOW_RAM_ENABLE_2] = 0;
    dev->regs[SCAT_SHADOW_RAM_ENABLE_3] = 0;
    dev->regs[SCAT_EXTENDED_BOUNDARY] = 0;
    dev->regs[SCAT_EMS_CONTROL] = 0;

    /* Disable all system mappings, we will override them. */
    mem_mapping_disable(&ram_low_mapping);
    if (! sx)
	mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);

    k = (sx) ? 0x80000 : 0x40000;

    dev->null_page.valid = 0;
    dev->null_page.regs_2x8 = 0xff;
    dev->null_page.regs_2x9 = 0xff;
    dev->null_page.scat = dev;

    mem_mapping_add(&dev->low_mapping[0], 0, k,
		    mem_read_scatb, mem_read_scatw, mem_read_scatl,
		    mem_write_scatb, mem_write_scatw, mem_write_scatl,
		    ram, MEM_MAPPING_INTERNAL, &dev->null_page);

    mem_mapping_add(&dev->low_mapping[1], 0xf0000, 0x10000,
		    mem_read_scatb, mem_read_scatw, mem_read_scatl,
		    mem_write_scatb, mem_write_scatw, mem_write_scatl,
		    ram + 0xf0000, MEM_MAPPING_INTERNAL, &dev->null_page);

    for (i = 2; i < 32; i++) {
	mem_mapping_add(&dev->low_mapping[i], (i << 19), 0x80000,
			mem_read_scatb, mem_read_scatw, mem_read_scatl,
			mem_write_scatb, mem_write_scatw, mem_write_scatl,
			ram + (i<<19), MEM_MAPPING_INTERNAL, &dev->null_page);
    }

    if (sx) {
	i = 16;
	k = 0x40000;
    } else {
	i = 0;
	k = (dev->regs[SCAT_VERSION] < 4) ? 0x40000 : 0x60000;
    }
    mem_mapping_set_addr(&dev->low_mapping[31], 0xf80000, k);

    for (; i < 44; i++) {
	mem_mapping_add(&dev->efff_mapping[i], 0x40000 + (i << 14), 0x4000,
			mem_read_scatb, mem_read_scatw, mem_read_scatl,
			mem_write_scatb, mem_write_scatw, mem_write_scatl,
			mem_size > (256 + (i << 4)) ? ram + 0x40000 + (i << 14) : NULL,
			MEM_MAPPING_INTERNAL, &dev->null_page);

	if (sx)
		mem_mapping_enable(&dev->efff_mapping[i]);
    }

    if (sx) {
	for (i = 24; i < 32; i++) {
		dev->page[i].valid = 1;
		dev->page[i].regs_2x8 = 0xff;
		dev->page[i].regs_2x9 = 0x03;
		dev->page[i].scat = dev;
		mem_mapping_add(&dev->ems_mapping[i], (i + 28) << 14, 0x04000,
				mem_read_scatb, mem_read_scatw, mem_read_scatl,
				mem_write_scatb, mem_write_scatw, mem_write_scatl,
				ram + ((i + 28) << 14), 0, &dev->page[i]);
		mem_mapping_disable(&dev->ems_mapping[i]);
	}
    } else {
	for (i = 0; i < 32; i++) {
		dev->page[i].valid = 1;
		dev->page[i].regs_2x8 = 0xff;
		dev->page[i].regs_2x9 = 0x03;
		dev->page[i].scat = dev;
		mem_mapping_add(&dev->ems_mapping[i], (i + (i >= 24 ? 28 : 16)) << 14, 0x04000,
				mem_read_scatb, mem_read_scatw, mem_read_scatl,
				mem_write_scatb, mem_write_scatw, mem_write_scatl,
				ram + ((i + (i >= 24 ? 28 : 16)) << 14),
				0, &dev->page[i]);
	}
    }

    for (i = 0; i < 6; i++) {
	mem_mapping_add(&dev->remap_mapping[i], 0x100000 + (i << 16), 0x10000,
			mem_read_scatb, mem_read_scatw, mem_read_scatl,
			mem_write_scatb, mem_write_scatw, mem_write_scatl,
			mem_size >= 1024 ? ram + get_addr(dev, 0x100000 + (i << 16), &dev->null_page) : NULL,
			MEM_MAPPING_INTERNAL, &dev->null_page);
    }

    if (sx) {
	dev->external_is_RAS = scatsx_external_is_RAS[mem_size >> 9];
    } else {
	dev->external_is_RAS = (dev->regs[SCAT_VERSION] > 3) || (((mem_size & ~2047) >> 11) + ((mem_size & 1536) >> 9) + ((mem_size & 511) >> 7)) > 4;
    }

    set_xms_bound(dev, 0);
    memmap_state_update(dev);
    shadow_state_update(dev);

    io_sethandler(0x0022, 2,
		  scat_in, NULL, NULL, scat_out, NULL, NULL, dev);

    device_add(&port_92_device);

    return(dev);
}

const device_t scat_device = {
    .name = "C&T SCAT (v1)",
    .internal_name = "scat",
    .flags = 0,
    .local = 0,
    .init = scat_init,
    .close = scat_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t scat_4_device = {
    .name = "C&T SCAT (v4)",
    .internal_name = "scat_4",
    .flags = 0,
    .local = 4,
    .init = scat_init,
    .close = scat_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t scat_sx_device = {
    .name = "C&T SCATsx",
    .internal_name = "scat_sx",
    .flags = 0,
    .local = 32,
    .init = scat_init,
    .close = scat_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

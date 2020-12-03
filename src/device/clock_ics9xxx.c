/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the ICS9xxx series of clock generators.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/i2c.h>
#include "cpu.h"


enum {
    ICS9150_08 = 1,
    ICS9248_39,
    ICS9248_98,
    ICS9250_08,
    ICS9250_10,
    ICS9250_13,
    ICS9250_14,
    ICS9250_16,
    ICS9250_18,
    ICS9250_19,
    ICS9250_23,
    ICS9250_25,
    ICS9250_26,
    ICS9250_27,
    ICS9250_28,
    ICS9250_29,
    ICS9250_30,
    ICS9250_32,
    ICS9250_38,
    ICS9250_50
};

typedef struct {
    uint16_t	bus;
    double	ram_mult;
    uint8_t	pci_div;
} ics9xxx_frequency_t;

typedef struct {
    uint8_t	model;
    uint8_t	max_reg: 3;
    uint8_t	regs[8];
    struct {
    	uint8_t	normal_reg: 3;
    	uint8_t	normal_bit: 3;
    	uint8_t	inv_reg: 3;
    	uint8_t	inv_bit: 3;
    } fs_regs[5];
    uint8_t	normal_bits_fixed: 1;

    uint8_t	frequencies_ref;
    ics9xxx_frequency_t frequencies[32];
    ics9xxx_frequency_t *frequencies_ptr;

    int8_t	addr_register;
    uint8_t	bus_match: 5;
} ics9xxx_t;


static const ics9xxx_t ics9xxx_devices[] = {
    [ICS9150_08] = {
    	.max_reg = 5,
    	.regs = {0x00, 0xff, 0xff, 0xff, 0x6f, 0xbf},
    	.fs_regs = {{0, 4, 4, 7}, {0, 5, 4, 4}, {0, 6, 5, 6}, {0, 7, 4, 1}, {-1, -1, -1, -1}},
    	.frequencies = {
		{.bus =  5000, .pci_div = 2},
		{.bus =  7500, .pci_div = 2},
		{.bus =  8333, .pci_div = 2},
		{.bus =  6680, .pci_div = 2},
		{.bus = 10300, .pci_div = 3},
		{.bus = 11200, .pci_div = 3},
		{.bus = 13333, .pci_div = 4},
	    	{.bus = 10020, .pci_div = 3},
	}
    },
    [ICS9248_39] = {
    	.max_reg = 5,
    	.regs = {0x00, 0x7f, 0xff, 0xbf, 0xf5, 0xff},
    	.fs_regs = {{0, 4, 3, 6}, {0, 5, 4, 3}, {0, 6, 1, 7}, {0, 7, 4, 1}, {-1, -1, -1, -1}},
    	.frequencies = {
    		{.bus = 12400, .pci_div = 3},
	    	{.bus =  7500, .pci_div = 2},
	    	{.bus =  8333, .pci_div = 2},
	    	{.bus =  6680, .pci_div = 2},
	    	{.bus = 10300, .pci_div = 3},
	    	{.bus = 11200, .pci_div = 3},
	    	{.bus = 13300, .pci_div = 3},
	    	{.bus = 10030, .pci_div = 3},
	    	{.bus = 12000, .pci_div = 3},
	    	{.bus = 11500, .pci_div = 3},
	    	{.bus = 11000, .pci_div = 3},
	    	{.bus = 10500, .pci_div = 3},
	    	{.bus = 14000, .pci_div = 4},
	    	{.bus = 15000, .pci_div = 4},
	    	{.bus = 12400, .pci_div = 4},
	    	{.bus = 13300, .pci_div = 4}
    	}
    },
    [ICS9248_98] = {
    	.max_reg = 6,
    	.regs = {0x00, 0x7f, 0xff, 0xbf, 0xf5, 0xff, 0x06},
    	.fs_regs = {{0, 4, 3, 6}, {0, 5, 4, 3}, {0, 6, 1, 7}, {0, 7, 4, 1}, {-1, -1, -1, -1}},
    	.frequencies = {
		{.bus =  8000, .pci_div = 2},
		{.bus =  7500, .pci_div = 2},
		{.bus =  8331, .pci_div = 2},
		{.bus =  6682, .pci_div = 2},
		{.bus = 10300, .pci_div = 3},
		{.bus = 11201, .pci_div = 3},
		{.bus =  6801, .pci_div = 2},
		{.bus = 10023, .pci_div = 3},
		{.bus = 12000, .pci_div = 3},
		{.bus = 11499, .pci_div = 3},
		{.bus = 10999, .pci_div = 3},
		{.bus = 10500, .pci_div = 3},
		{.bus = 14000, .pci_div = 4},
		{.bus = 15000, .pci_div = 4},
		{.bus = 12400, .pci_div = 4},
		{.bus = 13299, .pci_div = 4},
		{.bus = 13500, .pci_div = 4},
		{.bus = 12999, .pci_div = 4},
		{.bus = 12600, .pci_div = 4},
		{.bus = 11800, .pci_div = 3},
		{.bus = 11598, .pci_div = 3},
		{.bus =  9500, .pci_div = 3},
		{.bus =  9000, .pci_div = 3},
		{.bus =  8501, .pci_div = 3},
		{.bus = 16600, .pci_div = 4},
		{.bus = 16001, .pci_div = 4},
		{.bus = 15499, .pci_div = 4},
		{.bus = 14795, .pci_div = 4},
		{.bus = 14598, .pci_div = 4},
		{.bus = 14398, .pci_div = 4},
		{.bus = 14199, .pci_div = 4},
		{.bus = 13801, .pci_div = 4}
	}
    },
    [ICS9250_08] = {
    	.max_reg = 5,
    	.regs = {0x00, 0xff, 0xff, 0xff, 0x6d, 0xbf},
    	.fs_regs = {{0, 4, 4, 7}, {0, 5, 4, 4}, {0, 6, 5, 6}, {0, 7, 4, 1}, {-1, -1, -1, -1}},
    	.frequencies_ref = ICS9248_39
    },
    [ICS9250_10] = {
    	.max_reg = 5,
    	.regs = {0x1f, 0xff, 0xfe, 0x00, 0x00, 0x06},
    	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
    	.frequencies = {
		{.bus =  6667, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  7067, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  7466, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  8266, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  6350, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  6867, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  7267, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  8866, .ram_mult = 1.5, .pci_div = 2},
		{.bus = 10000, .ram_mult =   1, .pci_div = 3},
		{.bus = 10600, .ram_mult =   1, .pci_div = 3},
		{.bus = 11200, .ram_mult =   1, .pci_div = 3},
		{.bus = 12400, .ram_mult =   1, .pci_div = 3},
		{.bus =  9525, .ram_mult =   1, .pci_div = 3},
		{.bus = 10300, .ram_mult =   1, .pci_div = 3},
		{.bus = 10900, .ram_mult =   1, .pci_div = 3},
		{.bus = 13300, .ram_mult =   1, .pci_div = 3}
	}
    },
    [ICS9250_13] = {
    	.max_reg = 5,
    	.regs = {0x82, 0xcf, 0x7f, 0xff, 0xff, 0xf7},
    	.fs_regs = {{0, 4, 1, 4}, {0, 5, 5, 7}, {0, 6, 1, 5}, {0, 2, 2, 7}, {-1, -1, -1, -1}},
    	.frequencies = {
		{.bus = 9000, .ram_mult = 1, .pci_div = 2},
		{.bus = 8901, .ram_mult = 1, .pci_div = 2},
		{.bus = 8800, .ram_mult = 1, .pci_div = 2},
		{.bus = 8699, .ram_mult = 1, .pci_div = 2},
		{.bus = 8591, .ram_mult = 1, .pci_div = 2},
		{.bus = 8501, .ram_mult = 1, .pci_div = 2},
		{.bus = 8400, .ram_mult = 1, .pci_div = 2},
		{.bus = 8200, .ram_mult = 1, .pci_div = 2},
		{.bus = 8101, .ram_mult = 1, .pci_div = 2},
		{.bus = 8000, .ram_mult = 1, .pci_div = 2},
		{.bus = 8331, .ram_mult = 1, .pci_div = 2},
		{.bus = 6849, .ram_mult = 1, .pci_div = 2},
		{.bus = 7800, .ram_mult = 1, .pci_div = 2},
		{.bus = 7500, .ram_mult = 1, .pci_div = 2},
		{.bus = 7199, .ram_mult = 1, .pci_div = 2},
		{.bus = 6682, .ram_mult = 1, .pci_div = 2}
	}
    },
    [ICS9250_14] = {
    	.max_reg = 5,
    	.regs = {0x02, 0x1f, 0xff, 0xff, 0xeb, 0xff},
    	.fs_regs = {{0, 4, 1, 6}, {0, 5, 4, 2}, {0, 6, 1, 5}, {0, 7, 1, 7}, {0, 2, 4, 4}},
    	.frequencies = {
		{.bus =  6781, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  7000, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  7201, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  6667, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  7301, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  7500, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  7700, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  7801, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  8000, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  8300, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  8449, .ram_mult = 1.5, .pci_div = 2},
		{.bus = 10000, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  8608, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  8800, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  9000, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  9500, .ram_mult = 1.5, .pci_div = 2},
		{.bus =  4990, .ram_mult =   1, .pci_div = 3},
		{.bus = 10000, .ram_mult =   1, .pci_div = 3},
		{.bus =  7485, .ram_mult =   1, .pci_div = 3},
		{.bus =  6658, .ram_mult =   1, .pci_div = 3},
		{.bus =  8284, .ram_mult =   1, .pci_div = 3},
		{.bus =  8981, .ram_mult =   1, .pci_div = 3},
		{.bus =  9480, .ram_mult =   1, .pci_div = 3},
		{.bus = 10050, .ram_mult =   1, .pci_div = 3},
		{.bus = 10478, .ram_mult =   1, .pci_div = 3},
		{.bus = 11177, .ram_mult =   1, .pci_div = 3},
		{.bus = 11477, .ram_mult =   1, .pci_div = 3},
		{.bus = 10000, .ram_mult =   1, .pci_div = 3},
		{.bus = 12375, .ram_mult =   1, .pci_div = 3},
		{.bus = 13274, .ram_mult =   1, .pci_div = 3},
		{.bus = 13975, .ram_mult =   1, .pci_div = 3},
		{.bus = 14969, .ram_mult =   1, .pci_div = 3}
	}
    },
    [ICS9250_16] = {
    	.max_reg = 5,
    	.regs = {0x1f, 0xff, 0xff, 0x00, 0x00, 0x06},
    	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
    	.frequencies = {
		{.bus =  6667, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7000, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7267, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7467, .ram_mult =  1.5, .pci_div = 2},
		{.bus = 10000, .ram_mult =    1, .pci_div = 3},
		{.bus = 10500, .ram_mult =    1, .pci_div = 3},
		{.bus = 10900, .ram_mult =    1, .pci_div = 3},
		{.bus = 11201, .ram_mult =    1, .pci_div = 3},
		{.bus = 13334, .ram_mult =    1, .pci_div = 3},
		{.bus = 14000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 12000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 12400, .ram_mult =    1, .pci_div = 3},
		{.bus = 13334, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 15000, .ram_mult =    1, .pci_div = 4},
		{.bus = 14000, .ram_mult =    1, .pci_div = 4},
		{.bus = 13299, .ram_mult =    1, .pci_div = 4}
	}
    },
    [ICS9250_18] = {
    	.max_reg = 5,
    	.regs = {0x02, 0xff, 0xff, 0xff, 0x6d, 0xbf},
    	.fs_regs = {{0, 4, 4, 7}, {0, 5, 4, 4}, {0, 6, 5, 6}, {0, 7, 4, 1}, {-1, -1, -1, -1}},
    	.frequencies = {
		{.bus =  8000, .pci_div = 2},
		{.bus =  7500, .pci_div = 2},
		{.bus =  8331, .pci_div = 2},
		{.bus =  6690, .pci_div = 2},
		{.bus = 10300, .pci_div = 3},
		{.bus = 11201, .pci_div = 3},
		{.bus =  6801, .pci_div = 2},
		{.bus = 10070, .pci_div = 3},
		{.bus = 12000, .pci_div = 3},
		{.bus = 11499, .pci_div = 3},
		{.bus = 10999, .pci_div = 3},
		{.bus = 10500, .pci_div = 3},
		{.bus = 14000, .pci_div = 4},
		{.bus = 15000, .pci_div = 4},
		{.bus = 12400, .pci_div = 4},
		{.bus = 13390, .pci_div = 4},
		{.bus = 13500, .pci_div = 4},
		{.bus = 12999, .pci_div = 4},
		{.bus = 12600, .pci_div = 4},
		{.bus = 11800, .pci_div = 4},
		{.bus = 11598, .pci_div = 4},
		{.bus =  9500, .pci_div = 3},
		{.bus =  9000, .pci_div = 3},
		{.bus =  8501, .pci_div = 3},
		{.bus = 16600, .pci_div = 4},
		{.bus = 16001, .pci_div = 4},
		{.bus = 15499, .pci_div = 4},
		{.bus = 14795, .pci_div = 4},
		{.bus = 14598, .pci_div = 4},
		{.bus = 14398, .pci_div = 4},
		{.bus = 14199, .pci_div = 4},
		{.bus = 13801, .pci_div = 4}
	}
    },
    [ICS9250_19] = {
    	.max_reg = 5,
    	.regs = {0x02, 0xff, 0xff, 0xff, 0x6d, 0xbf},
    	.fs_regs = {{0, 4, 4, 7}, {0, 5, 4, 4}, {0, 6, 5, 6}, {0, 7, 4, 1}, {-1, -1, -1, -1}},
    	.frequencies_ref = ICS9250_08
    },
    [ICS9250_23] = {
    	.max_reg = 5,
    	.regs = {0x02, 0x1f, 0xff, 0xff, 0xeb, 0xff},
    	.fs_regs = {{0, 4, 1, 6}, {0, 5, 4, 2}, {0, 6, 1, 5}, {0, 7, 1, 7}, {0, 2, 4, 4}},
	.frequencies = {
		{.bus =  6900, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7000, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7100, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  6690, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7200, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7500, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7660, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  8500, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  6800, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7400, .ram_mult =  1.5, .pci_div = 2},
		{.bus = 14000, .ram_mult =    1, .pci_div = 4},
		{.bus = 13333, .ram_mult =    1, .pci_div = 4},
		{.bus = 15000, .ram_mult =    1, .pci_div = 4},
		{.bus = 15500, .ram_mult =    1, .pci_div = 4},
		{.bus = 16600, .ram_mult =    1, .pci_div = 4},
		{.bus = 16600, .ram_mult =    1, .pci_div = 3},
		{.bus = 11177, .ram_mult =    1, .pci_div = 3},
		{.bus = 10478, .ram_mult =    1, .pci_div = 3},
		{.bus = 10951, .ram_mult =    1, .pci_div = 3},
		{.bus = 10090, .ram_mult =    1, .pci_div = 3},
		{.bus = 11700, .ram_mult =    1, .pci_div = 3},
		{.bus = 12375, .ram_mult =    1, .pci_div = 3},
		{.bus = 13333, .ram_mult =    1, .pci_div = 3},
		{.bus = 14250, .ram_mult =    1, .pci_div = 3},
		{.bus = 13600, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14300, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13390, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14667, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14933, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 15330, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 16667, .ram_mult = 0.75, .pci_div = 4}
	}
    },
    [ICS9250_25] = {
    	.max_reg = 6,
    	.regs = {0x02, 0x1f, 0xff, 0xff, 0xeb, 0xff, 0x06},
    	.fs_regs = {{0, 4, 1, 6}, {0, 5, 4, 2}, {0, 6, 1, 5}, {0, 7, 1, 7}, {0, 2, 4, 4}},
	.frequencies = {
		{.bus =  5500, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  6000, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  6680, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  6833, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7000, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7200, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7500, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7700, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  8330, .ram_mult =    1, .pci_div = 3},
		{.bus =  9000, .ram_mult =    1, .pci_div = 3},
		{.bus = 10030, .ram_mult =    1, .pci_div = 3},
		{.bus = 10300, .ram_mult =    1, .pci_div = 3},
		{.bus = 11250, .ram_mult =    1, .pci_div = 3},
		{.bus = 11500, .ram_mult =    1, .pci_div = 3},
		{.bus = 12000, .ram_mult =    1, .pci_div = 3},
		{.bus = 12500, .ram_mult =    1, .pci_div = 3},
		{.bus = 12800, .ram_mult =    1, .pci_div = 4},
		{.bus = 13000, .ram_mult =    1, .pci_div = 4},
		{.bus = 13370, .ram_mult =    1, .pci_div = 4},
		{.bus = 13700, .ram_mult =    1, .pci_div = 4},
		{.bus = 14000, .ram_mult =    1, .pci_div = 4},
		{.bus = 14500, .ram_mult =    1, .pci_div = 4},
		{.bus = 15000, .ram_mult =    1, .pci_div = 4},
		{.bus = 15333, .ram_mult =    1, .pci_div = 4},
		{.bus = 12500, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13370, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13700, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14500, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 15000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 15333, .ram_mult = 0.75, .pci_div = 4}
	}
    },
    [ICS9250_26] = {
    	.max_reg = 5,
    	.regs = {0x1e, 0xff, 0xff, 0x00, 0x00, 0x06},
    	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
    	.frequencies_ref = ICS9250_16
    },
    [ICS9250_27] = {
    	.max_reg = 5,
    	.regs = {0x0f, 0xff, 0xfe, 0x00, 0x00, 0x00},
    	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
    	.frequencies = {
		{.bus =  6666, .ram_mult =  1.5, .pci_div = 2},
		{.bus = 13332, .ram_mult =    1, .pci_div = 4},
		{.bus = 10000, .ram_mult =    1, .pci_div = 3},
		{.bus = 13332, .ram_mult = 0.75, .pci_div = 4},
		{.bus =  6666, .ram_mult =  1.5, .pci_div = 2},
		{.bus = 13332, .ram_mult =    1, .pci_div = 4},
		{.bus = 10000, .ram_mult =    1, .pci_div = 3},
		{.bus = 13332, .ram_mult =    1, .pci_div = 4}
	}
    },
    [ICS9250_28] = {
    	.max_reg = 4,
    	.regs = {0x1e, 0xff, 0xfe, 0x00, 0x00},
    	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
    	.frequencies_ref = ICS9250_27
    },
    [ICS9250_29] = {
    	.max_reg = 5,
    	.regs = {0x16, 0xff, 0xfe, 0x00, 0x00, 0x00},
    	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
    	.frequencies_ref = ICS9250_27
    },
    [ICS9250_30] = {
    	.max_reg = 6,
    	.regs = {0x02, 0x0f, 0xff, 0xff, 0xeb, 0xff, 0x06},
    	.fs_regs = {{0, 4, 1, 6}, {0, 5, 4, 2}, {0, 6, 1, 5}, {0, 7, 1, 7}, {0, 2, 4, 4}},
    	.frequencies = {
		{.bus =  6667, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  6000, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  6680, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  6833, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7000, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7500, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  8000, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  8300, .ram_mult =  1.5, .pci_div = 2},
		{.bus = 10000, .ram_mult =    1, .pci_div = 3},
		{.bus =  9000, .ram_mult =    1, .pci_div = 3},
		{.bus = 10030, .ram_mult =    1, .pci_div = 3},
		{.bus = 10300, .ram_mult =    1, .pci_div = 3},
		{.bus = 10500, .ram_mult =    1, .pci_div = 3},
		{.bus = 11000, .ram_mult =    1, .pci_div = 3},
		{.bus = 11500, .ram_mult =    1, .pci_div = 3},
		{.bus = 20000, .ram_mult =    1, .pci_div = 6},
		{.bus = 13333, .ram_mult =    1, .pci_div = 4},
		{.bus = 16667, .ram_mult =    1, .pci_div = 4},
		{.bus = 13370, .ram_mult =    1, .pci_div = 4},
		{.bus = 13700, .ram_mult =    1, .pci_div = 4},
		{.bus = 14000, .ram_mult =    1, .pci_div = 4},
		{.bus = 14500, .ram_mult =    1, .pci_div = 4},
		{.bus = 15000, .ram_mult =    1, .pci_div = 4},
		{.bus = 16000, .ram_mult =    1, .pci_div = 4},
		{.bus = 13333, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 16667, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13370, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13700, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14500, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 15000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 16000, .ram_mult = 0.75, .pci_div = 4}
	}
    },
    [ICS9250_32] = {
    	.max_reg = 4,
    	.regs = {0x07, 0xff, 0xff, 0x00, 0x00},
    	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}}
    },
    [ICS9250_38] = {
    	.max_reg = 6,
    	.regs = {0x18, 0x07, 0xfe, 0xc7, 0xfc, 0x00, 0x80},
    	.fs_regs = {{0, 0, -1, -1}, {0, 1, -1, -1}, {0, 2, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
    	.normal_bits_fixed = 1,
    	.frequencies = {
	    	{.bus =  6666, .ram_mult =       1, .pci_div = 1},
	    	{.bus = 10000, .ram_mult = 2.0/3.0, .pci_div = 3},
	    	{.bus = 20000, .ram_mult = 1.0/3.0, .pci_div = 6},
	    	{.bus = 13333, .ram_mult =     0.5, .pci_div = 2},
	    	{.bus =  6666, .ram_mult =       1, .pci_div = 1},
	    	{.bus = 10000, .ram_mult = 2.0/3.0, .pci_div = 3},
	    	{.bus = 20000, .ram_mult = 1.0/3.0, .pci_div = 6},
	    	{.bus = 13333, .ram_mult =     0.5, .pci_div = 2}
	}
    },
    [ICS9250_50] = {
    	.max_reg = 6,
    	.regs = {0x02, 0x6f, 0xff, 0xff, 0xef, 0xff, 0x06},
    	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {0, 7, 1, 7}, {0, 2, 4, 4}},
    	.frequencies = {
    		{.bus =  6667, .ram_mult =  1.5, .pci_div = 2},
    		{.bus = 10000, .ram_mult =    1, .pci_div = 3},
    		{.bus = 13333, .ram_mult =    1, .pci_div = 4},
    		{.bus = 13333, .ram_mult = 0.75, .pci_div = 4}
    	}
    }
};

#define ENABLE_ICS9xxx_LOG 1
#ifdef ENABLE_ICS9xxx_LOG
int ics9xxx_do_log = ENABLE_ICS9xxx_LOG;


static void
ics9xxx_log(const char *fmt, ...)
{
    va_list ap;

    if (ics9xxx_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ics9xxx_log(fmt, ...)
#endif


static uint8_t
ics9xxx_start(void *bus, uint8_t addr, uint8_t read, void *priv)
{
    ics9xxx_t *dev = (ics9xxx_t *) priv;

    ics9xxx_log("ICS9xxx: start()\n");

    dev->addr_register = -2; /* -2 = command; -1 = SMBus block length; 0+ = registers */

    return 1;
}


static uint8_t
ics9xxx_read(void *bus, uint8_t addr, void *priv)
{
    ics9xxx_t *dev = (ics9xxx_t *) priv;
    uint8_t ret = 0xff;

    if (dev->addr_register < 0) {
    	dev->addr_register = -1;
	ret = dev->max_reg + 1;
    } else if ((dev->model == ICS9250_50) && (dev->addr_register == 0))
    	ret = dev->regs[dev->addr_register] & 0x0b; /* -50 reads back revision ID instead */
    else
	ret = dev->regs[dev->addr_register];

    ics9xxx_log("ICS9xxx: read(%02X) = %02X\n", dev->addr_register, ret);
    if (++dev->addr_register > dev->max_reg)
    	dev->addr_register = 0; /* roll-over */

    return ret;
}


static void
ics9xxx_set(ics9xxx_t *dev, uint8_t val)
{
    /* Add register-defined frequency bits to the static frequency bits we were passed. */
    for (uint8_t i = 0; i < sizeof(dev->fs_regs) / sizeof(dev->fs_regs[0]); i++) {
    	if ((dev->fs_regs[i].normal_reg < 7) && (dev->regs[dev->fs_regs[i].normal_reg] & (1 << dev->fs_regs[i].normal_bit)))
    		val |= 1 << i;
    }

    ics9xxx_log("ICS9xxx: set(%02X) = %d\n", val, ics9xxx_devices[dev->model].frequencies[val].bus);
}


static uint8_t
ics9xxx_write(void *bus, uint8_t addr, uint8_t data, void *priv)
{
    ics9xxx_t *dev = (ics9xxx_t *) priv;

    ics9xxx_log("ICS9xxx: write(%02X, %02X)\n", dev->addr_register, data);

    if (dev->addr_register >= 0) {
    	/* Preserve fixed bits. */
    	for (uint8_t i = 0; i < sizeof(dev->fs_regs) / sizeof(dev->fs_regs[0]); i++) {
    		if (dev->normal_bits_fixed && (dev->fs_regs[i].normal_reg == dev->addr_register))
    			data = (dev->regs[dev->addr_register] & (1 << dev->fs_regs[i].normal_bit)) | (data & ~(1 << dev->fs_regs[i].normal_bit));
    		if (dev->fs_regs[i].inv_reg == dev->addr_register)
    			data = (dev->regs[dev->addr_register] & (1 << dev->fs_regs[i].inv_bit)) | (data & ~(1 << dev->fs_regs[i].inv_bit));
    	}

    	switch (dev->addr_register) {
    		case 0:
    			if (dev->model == ICS9250_38)
    				data = (dev->regs[dev->addr_register] & ~0xef) | (data & 0xef);
    			else if (dev->model == ICS9250_10)
    				ics9xxx_set(dev, (cpu_busspeed >= 100000000) * 0x08);
    			else if ((dev->model == ICS9250_16) || (dev->model == ICS9250_26))
    				ics9xxx_set(dev, ((cpu_busspeed >= 120000000) * 0x08) | ((((cpu_busspeed >= 100000000) && (cpu_busspeed < 120000000)) || (cpu_busspeed == 150000000) || (cpu_busspeed == 132999999)) * 0x04));
    			else if ((dev->model == ICS9250_27) || (dev->model == ICS9250_28) || (dev->model == ICS9250_29))
    				ics9xxx_set(dev, ((cpu_busspeed == 100000000) * 0x02) | ((cpu_busspeed > 100000000) * 0x01));
    			else
    				ics9xxx_set(dev, 0x00);
    			break;

    		case 1:
    			if (dev->model == ICS9250_38)
    				data = (dev->regs[dev->addr_register] & ~0xfe) | (data & 0xfe);
    			break;

    		case 3:
    			if (dev->model == ICS9250_32)
    				data ^= 0x70;
    			break;

    		case 4:
    			if (dev->model == ICS9250_38)
    				data = (dev->regs[dev->addr_register] & ~0xfc) | (data & 0xfc);
    			break;

    		case 6:
    			if (dev->model == ICS9250_38)
    				data = dev->regs[dev->addr_register];
    			break;
    	}
    	dev->regs[dev->addr_register] = data;
    }
    
    if (++dev->addr_register > dev->max_reg)
    	dev->addr_register = 0; /* roll-over */

    return 1;
}


static uint8_t
find_bus_match(ics9xxx_t *dev, uint32_t bus, uint8_t preset_mask, uint8_t preset) {
    uint8_t best_match = 0;
    uint32_t delta, best_delta = -1;

    for (uint8_t i = 0; i < (sizeof(dev->frequencies) / sizeof(dev->frequencies[0])); i++) {
	if (((i & preset_mask) != preset) || !dev->frequencies_ptr[i].bus)
		continue;

	delta = ABS((int32_t) (bus - (dev->frequencies_ptr[i].bus * 10000)));
	if (delta < best_delta) {
		best_match = i;
		best_delta = delta;
	}
    }

    ics9xxx_log("ICS9xxx: find_match(%02X, %d) = %d (%02X)\n", dev->model, bus, dev->frequencies_ptr[best_match].bus, best_match);

    return best_match;
}


static void *
ics9xxx_init(const device_t *info)
{
    ics9xxx_t *dev = (ics9xxx_t *) malloc(sizeof(ics9xxx_t));
    memcpy(dev, &ics9xxx_devices[info->local], sizeof(ics9xxx_t));

    ics9xxx_log("ICS9xxx: init(%02X)\n", info->local);

    dev->model = info->local;
    dev->frequencies_ptr = (ics9xxx_frequency_t *) ics9xxx_devices[dev->frequencies_ref ? dev->frequencies_ref : dev->model].frequencies;
    if (!dev->frequencies_ptr)
    	fatal("ICS9xxx: NULL frequency table\n");

    /* Determine which frequency bits cannot be strapped (register only). */
    uint8_t register_only_bits = 0x00;
    uint8_t i;
    for (i = 0; i < sizeof(dev->fs_regs) / sizeof(dev->fs_regs[0]); i++) {
    	if ((dev->fs_regs[i].normal_reg == 7) && (dev->fs_regs[i].inv_reg == 7))
    		register_only_bits |= 1 << i;
    }

    /* Find bus speed match and set default register bits accordingly. */
    dev->bus_match = find_bus_match(dev, cpu_busspeed, register_only_bits, 0x00);
    for (i = 0; i < sizeof(dev->fs_regs) / sizeof(dev->fs_regs[0]); i++) {
    	if (dev->fs_regs[i].normal_reg < 7) {
    		if (dev->bus_match & (1 << i))
    			dev->regs[dev->fs_regs[i].normal_reg] |= 1 << dev->fs_regs[i].normal_bit;
    		else
    			dev->regs[dev->fs_regs[i].normal_reg] &= ~(1 << dev->fs_regs[i].normal_bit);
    	}
    	if (dev->fs_regs[i].inv_reg < 7) {
    		if (dev->bus_match & (1 << i))
    			dev->regs[dev->fs_regs[i].inv_reg] &= ~(1 << dev->fs_regs[i].inv_bit);
    		else
    			dev->regs[dev->fs_regs[i].inv_reg] |= 1 << dev->fs_regs[i].inv_bit;
    	}
    }

    i2c_sethandler(i2c_smbus, 0x69, 1, ics9xxx_start, ics9xxx_read, ics9xxx_write, NULL, dev);

    return dev;
}


static void
ics9xxx_close(void *priv)
{
    ics9xxx_t *dev = (ics9xxx_t *) priv;

    ics9xxx_log("ICS9xxx: close()\n");

    i2c_removehandler(i2c_smbus, 0x69, 1, ics9xxx_start, ics9xxx_read, ics9xxx_write, NULL, dev);

    free(dev);
}


const device_t ics9150_08_device = {
    "ICS9150-08 Clock Generator",
    DEVICE_ISA,
    ICS9150_08,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9248_39_device = {
    "ICS9248-39 Clock Generator",
    DEVICE_ISA,
    ICS9248_39,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9248_98_device = {
    "ICS9248-98 Clock Generator",
    DEVICE_ISA,
    ICS9248_98,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_08_device = {
    "ICS9250-08 Clock Generator",
    DEVICE_ISA,
    ICS9250_08,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_10_device = {
    "ICS9250-10 Clock Generator",
    DEVICE_ISA,
    ICS9250_10,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_13_device = {
    "ICS9250-13 Clock Generator",
    DEVICE_ISA,
    ICS9250_13,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_14_device = {
    "ICS9250-14 Clock Generator",
    DEVICE_ISA,
    ICS9250_14,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_16_device = {
    "ICS9250-16 Clock Generator",
    DEVICE_ISA,
    ICS9250_16,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_18_device = {
    "ICS9250-18 Clock Generator",
    DEVICE_ISA,
    ICS9250_18,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_19_device = {
    "ICS9250-19 Clock Generator",
    DEVICE_ISA,
    ICS9250_19,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_23_device = {
    "ICS9250-23 Clock Generator",
    DEVICE_ISA,
    ICS9250_23,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_25_device = {
    "ICS9250-25 Clock Generator",
    DEVICE_ISA,
    ICS9250_25,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_26_device = {
    "ICS9250-26 Clock Generator",
    DEVICE_ISA,
    ICS9250_26,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_27_device = {
    "ICS9250-27 Clock Generator",
    DEVICE_ISA,
    ICS9250_27,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_28_device = {
    "ICS9250-28 Clock Generator",
    DEVICE_ISA,
    ICS9250_28,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_29_device = {
    "ICS9250-29 Clock Generator",
    DEVICE_ISA,
    ICS9250_29,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_30_device = {
    "ICS9250-30 Clock Generator",
    DEVICE_ISA,
    ICS9250_30,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_32_device = {
    "ICS9250-32 Clock Generator",
    DEVICE_ISA,
    ICS9250_32,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_38_device = {
    "ICS9250-38 Clock Generator",
    DEVICE_ISA,
    ICS9250_38,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


const device_t ics9250_50_device = {
    "ICS9250-50 Clock Generator",
    DEVICE_ISA,
    ICS9250_50,
    ics9xxx_init, ics9xxx_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};

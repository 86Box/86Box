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
#include <86box/clock.h>


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
#define ICS9xxx_MODEL(model)	[model] = {.name = #model,
#else
#define ics9xxx_log(fmt, ...)
#define ICS9xxx_MODEL(model)	[model] = {
#endif
#define ICS9xxx_MODEL_END()	},
#define agp_div			ram_mult /* temporarily saves space while neither field matters */



typedef struct {
    uint16_t	bus: 15;
    uint8_t	ram_mult: 2; /* change to full float when this becomes useful */
    uint8_t	pci_div: 3;
} ics9xxx_frequency_t;

typedef struct {
#if defined(ENABLE_ICS9xxx_LOG) || defined(ENABLE_ICS9xxx_DETECT)
    const char	*name; /* populated by macro */
#endif
    uint8_t	max_reg: 3; /* largest register index */
    uint8_t	regs[7]; /* default registers */
    struct { /* for each hardware frequency select bit [FS0:FS4]: */
	uint8_t	normal_reg: 3; /* which register (or -1) for non-inverted input (FSn) */
	uint8_t	normal_bit: 3; /* which bit (0-7) for non-inverted input (FSn) */
	uint8_t	inv_reg: 3; /* which register (or -1) for inverted input (FSn#) */
	uint8_t	inv_bit: 3; /* which bit (0-7) for inverted input (FSn#) */
    } fs_regs[5];
    uint8_t	normal_bits_fixed: 1; /* set to 1 if the non-inverted bits are straps (hardware select only) */
    struct { /* hardware select bit, which should be cleared for hardware select (latched inputs), or set for programming */
	uint8_t	normal_reg: 3; /* which register (or -1) */
	uint8_t	normal_bit: 3; /* which bit (0-7) */
    } hw_select;

    uint8_t	frequencies_ref; /* which other model to use the frequency table from (or 0) */
    const ics9xxx_frequency_t *frequencies; /* frequency table, if not using another model's table */
} ics9xxx_model_t;

typedef struct {
    uint8_t	model_idx;
    ics9xxx_model_t *model;
    device_t	*dyn_device;

    ics9xxx_frequency_t *frequencies_ptr;
    uint8_t	regs[7];
    int8_t	addr_register: 4;
    uint8_t	relevant_regs: 7;
    uint8_t	bus_match: 5;
} ics9xxx_t;


static const ics9xxx_model_t ics9xxx_models[] = {
#ifdef ENABLE_ICS9xxx_DETECT
    ICS9xxx_MODEL(ICS9xxx_xx)
	.max_reg = 6
    ICS9xxx_MODEL_END()
#endif
    ICS9xxx_MODEL(ICS9150_08)
	.max_reg = 5,
	.regs = {0x00, 0xff, 0xff, 0xff, 0x6f, 0xbf},
	.fs_regs = {{0, 4, 4, 7}, {0, 5, 4, 4}, {0, 6, 5, 6}, {0, 7, 4, 1}, {-1, -1, -1, -1}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus =  5000, .pci_div = 2},
		{.bus =  7500, .pci_div = 2},
		{.bus =  8333, .pci_div = 2},
		{.bus =  6680, .pci_div = 2},
		{.bus = 10300, .pci_div = 3},
		{.bus = 11200, .pci_div = 3},
		{.bus = 13333, .pci_div = 4},
		{.bus = 10020, .pci_div = 3},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_39)
	.max_reg = 5,
	.regs = {0x00, 0x7f, 0xff, 0xbf, 0xf5, 0xff},
	.fs_regs = {{0, 4, 3, 6}, {0, 5, 4, 3}, {0, 6, 1, 7}, {0, 7, 4, 1}, {-1, -1, -1, -1}},
	.hw_select = {0, 3},
	.frequencies_ref = ICS9250_08
    ICS9xxx_MODEL_END()
#if 0
    ICS9xxx_MODEL(ICS9248_81)
	.max_reg = 5,
	.regs = {0x82, 0xfe, 0x7f, 0xff, 0xff, 0xb7},
	.fs_regs = {{0, 4, 1, 0}, {0, 5, 2, 7}, {0, 6, 5, 6}, {0, 2, 5, 3}, {-1, -1, -1, -1}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus =  9000, .ram_mult =       1, .pci_div = 3},
		{.bus =  6670, .ram_mult =     1.5, .pci_div = 2},
		{.bus =  9500, .ram_mult = 2.0/3.0, .pci_div = 3},
		{.bus = 10000, .ram_mult = 2.0/3.0, .pci_div = 3},
		{.bus = 10000, .ram_mult =    0.75, .pci_div = 3},
		{.bus = 11200, .ram_mult = 2.0/3.0, .pci_div = 3},
		{.bus = 12400, .ram_mult = 2.0/3.0, .pci_div = 4},
		{.bus = 13330, .ram_mult = 2.0/3.0, .pci_div = 4},
		{.bus =  6670, .ram_mult =       1, .pci_div = 2},
		{.bus =  7500, .ram_mult =       1, .pci_div = 3},
		{.bus =  8330, .ram_mult =       1, .pci_div = 3},
		{.bus =  9500, .ram_mult =       1, .pci_div = 3},
		{.bus = 10000, .ram_mult =       1, .pci_div = 3},
		{.bus = 11200, .ram_mult =       1, .pci_div = 3},
		{.bus = 12400, .ram_mult =       1, .pci_div = 4},
		{.bus = 13330, .ram_mult =       1, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_95)
	.max_reg = 5,
	.regs = {0x82, 0xff, 0xff, 0xff, 0xd5, 0xff}, 
	.fs_regs = {{0, 4, -1, -1}, {0, 5, 4, 3}, {0, 6, -1, -1}, {0, 2, 4, 1}, {-1, -1, -1, -1}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus =  6667, .pci_div = 2},
		{.bus = 10000, .pci_div = 3},
		{.bus = 10030, .pci_div = 3},
		{.bus = 13333, .pci_div = 4},
		{.bus = 10500, .pci_div = 3},
		{.bus = 13337, .pci_div = 4},
		{.bus = 13700, .pci_div = 4},
		{.bus =  7500, .pci_div = 2},
		{.bus = 10000, .pci_div = 3},
		{.bus =  9500, .pci_div = 2},
		{.bus =  9700, .pci_div = 3},
		{.bus = 13333, .pci_div = 4},
		{.bus =  9000, .pci_div = 3},
		{.bus =  9622, .pci_div = 3},
		{.bus =  6681, .pci_div = 2},
		{.bus =  9150, .pci_div = 3},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_98)
	.max_reg = 6,
	.regs = {0x00, 0x7f, 0xff, 0xbf, 0xf5, 0xff, 0x06},
	.fs_regs = {{0, 4, 3, 6}, {0, 5, 4, 3}, {0, 6, 1, 7}, {0, 7, 4, 1}, {0, 2, -1, -1}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
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
		{.bus = 13801, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_101)
	.max_reg = 5,
	.regs = {0x82, 0xff, 0xff, 0xff, 0xf5, 0xff},
	.fs_regs = {{0, 4, -1, -1}, {0, 5, 4, 3}, {0, 6, -1, -1}, {0, 2, 4, 1}, {-1, -1, -1, -1}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus = 12400, .pci_div = 3},
		{.bus = 12000, .pci_div = 3},
		{.bus = 11499, .pci_div = 3},
		{.bus = 10999, .pci_div = 3},
		{.bus = 10500, .pci_div = 3},
		{.bus =  8331, .pci_div = 2},
		{.bus = 13700, .pci_div = 4},
		{.bus =  7500, .pci_div = 2},
		{.bus = 10000, .pci_div = 3},
		{.bus =  9500, .pci_div = 3},
		{.bus =  8331, .pci_div = 3},
		{.bus = 13333, .pci_div = 4},
		{.bus =  9000, .pci_div = 3},
		{.bus =  9622, .pci_div = 3},
		{.bus =  6682, .pci_div = 2},
		{.bus =  9150, .pci_div = 3},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_103)
	.max_reg = 5,
	.regs = {0x82, 0xff, 0xff, 0xff, 0xf5, 0xff},
	.fs_regs = {{0, 4, -1, -1}, {0, 5, 4, 3}, {0, 6, -1, -1}, {0, 2, 4, 1}, {-1, -1, -1, -1}},
	.hw_select = {0, 3},
	.frequencies_ref = ICS9248_101
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_107)
	.max_reg = 6,
	.regs = {0x02, 0xff, 0xff, 0xec, 0xde, 0xff, 0x06},
	.fs_regs = {{0, 4, 4, 5}, {0, 5, 3, 4}, {0, 6, 3, 0}, {0, 7, 3, 1}, {0, 2, 4, 0}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus = 10300, .pci_div = 3},
		{.bus = 10000, .pci_div = 3},
		{.bus = 10045, .pci_div = 3},
		{.bus = 10090, .pci_div = 3},
		{.bus = 10710, .pci_div = 2},
		{.bus = 10900, .pci_div = 3},
		{.bus = 11200, .pci_div = 3},
		{.bus = 11400, .pci_div = 4},
		{.bus = 11600, .pci_div = 4},
		{.bus = 11800, .pci_div = 4},
		{.bus = 13330, .pci_div = 3},
		{.bus = 12000, .pci_div = 4},
		{.bus = 12200, .pci_div = 4},
		{.bus = 12500, .pci_div = 4},
		{.bus =  5000, .pci_div = 2},
		{.bus =  6670, .pci_div = 4},
		{.bus = 13330, .pci_div = 3},
		{.bus = 13390, .pci_div = 3},
		{.bus = 13800, .pci_div = 4},
		{.bus = 14200, .pci_div = 4},
		{.bus = 14600, .pci_div = 4},
		{.bus = 15000, .pci_div = 4},
		{.bus = 15300, .pci_div = 4},
		{.bus = 15600, .pci_div = 4},
		{.bus = 15910, .pci_div = 3},
		{.bus = 16200, .pci_div = 4},
		{.bus = 16670, .pci_div = 4},
		{.bus = 16800, .pci_div = 4},
		{.bus = 17100, .pci_div = 4},
		{.bus = 17400, .pci_div = 4},
		{.bus = 17700, .pci_div = 4},
		{.bus = 18000, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_112)
	.max_reg = 6,
	.regs = {0x02, 0x1f, 0xff, 0xff, 0xfb, 0xff, 0x06},
	.fs_regs = {{0, 4, 1, 6}, {0, 5, 4, 2}, {0, 6, 1, 5}, {0, 7, 1, 7}, {0, 2, -1, -1}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus =  6680, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  6800, .ram_mult =  1.5, .pci_div = 2},
		{.bus = 10030, .ram_mult =    1, .pci_div = 3},
		{.bus = 10300, .ram_mult =    1, .pci_div = 3},
		{.bus = 13372, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14500, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13372, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13733, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14000, .ram_mult =    1, .pci_div = 2},
		{.bus = 11800, .ram_mult =    1, .pci_div = 3},
		{.bus = 12400, .ram_mult =    1, .pci_div = 3},
		{.bus = 13369, .ram_mult =    1, .pci_div = 2},
		{.bus = 13700, .ram_mult =    1, .pci_div = 2},
		{.bus = 15000, .ram_mult = 0.75, .pci_div = 4},
		{.bus =  7250, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7500, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  8300, .ram_mult =    1, .pci_div = 6},
		{.bus = 11000, .ram_mult =    1, .pci_div = 2},
		{.bus = 12000, .ram_mult =    1, .pci_div = 3},
		{.bus = 12500, .ram_mult =    1, .pci_div = 2},
		{.bus =  6925, .ram_mult =  1.5, .pci_div = 1},
		{.bus =  7000, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7667, .ram_mult =  1.5, .pci_div = 2},
		{.bus = 14500, .ram_mult =    1, .pci_div = 3},
		{.bus =  6650, .ram_mult =  1.5, .pci_div = 2},
		{.bus = 15000, .ram_mult =    1, .pci_div = 3},
		{.bus =  9975, .ram_mult =    1, .pci_div = 3},
		{.bus = 15500, .ram_mult =    1, .pci_div = 2},
		{.bus = 16650, .ram_mult =    1, .pci_div = 3},
		{.bus = 15333, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13300, .ram_mult = 0.75, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_138)
	.max_reg = 6,
	.regs = {0x02, 0x3f, 0x7f, 0x6f, 0xff, 0xff, 0x06},
	.fs_regs = {{0, 4, 2, 7}, {0, 5, 1, 6}, {0, 6, 1, 7}, {0, 7, 3, 4}, {0, 2, 3, 7}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus =  6667, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  6687, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  6867, .ram_mult =  1.5, .pci_div = 2},
		{.bus =  7134, .ram_mult =  1.5, .pci_div = 2},
		{.bus = 10000, .ram_mult =    1, .pci_div = 3},
		{.bus = 10030, .ram_mult =    1, .pci_div = 3},
		{.bus = 10300, .ram_mult =    1, .pci_div = 3},
		{.bus = 10700, .ram_mult =    1, .pci_div = 2},
		{.bus = 13333, .ram_mult =    1, .pci_div = 4},
		{.bus = 13372, .ram_mult =    1, .pci_div = 4},
		{.bus = 13733, .ram_mult =    1, .pci_div = 4},
		{.bus = 12000, .ram_mult =    1, .pci_div = 4},
		{.bus = 13333, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13372, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13733, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 12000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 13600, .ram_mult =    1, .pci_div = 4},
		{.bus = 14000, .ram_mult =    1, .pci_div = 4},
		{.bus = 14266, .ram_mult =    1, .pci_div = 3},
		{.bus = 14533, .ram_mult =    1, .pci_div = 4},
		{.bus = 13600, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14266, .ram_mult = 0.75, .pci_div = 3},
		{.bus = 14533, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 14666, .ram_mult =    1, .pci_div = 3},
		{.bus = 15333, .ram_mult =    1, .pci_div = 4},
		{.bus = 16000, .ram_mult =    1, .pci_div = 4},
		{.bus = 16667, .ram_mult =    1, .pci_div = 3},
		{.bus = 14666, .ram_mult = 0.75, .pci_div = 3},
		{.bus = 16000, .ram_mult = 0.75, .pci_div = 4},
		{.bus = 16667, .ram_mult = 0.75, .pci_div = 3},
		{.bus = 20000, .ram_mult =    1, .pci_div = 6},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_141)
	.max_reg = 6,
	.regs = {0x02, 0x6b, 0x7f, 0xff, 0xff, 0xe7, 0x06},
	.fs_regs = {{0, 4, 2, 7}, {0, 5, 5, 3}, {0, 6, 1, 7}, {0, 7, 1, 4}, {0, 2, -1, -1}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus =  9000, .pci_div = 3},
		{.bus =  9500, .pci_div = 2},
		{.bus = 10100, .pci_div = 2},
		{.bus = 10200, .pci_div = 3},
		{.bus = 10090, .pci_div = 3},
		{.bus = 10300, .pci_div = 3},
		{.bus = 10500, .pci_div = 3},
		{.bus = 10000, .pci_div = 3},
		{.bus = 10700, .pci_div = 2},
		{.bus = 10900, .pci_div = 3},
		{.bus = 11000, .pci_div = 2},
		{.bus = 11100, .pci_div = 3},
		{.bus = 11300, .pci_div = 2},
		{.bus = 11500, .pci_div = 3},
		{.bus = 11700, .pci_div = 3},
		{.bus = 13330, .pci_div = 3},
		{.bus = 12000, .pci_div = 3},
		{.bus = 12500, .pci_div = 4},
		{.bus = 13000, .pci_div = 4},
		{.bus = 13372, .pci_div = 4},
		{.bus = 13500, .pci_div = 4},
		{.bus = 13700, .pci_div = 4},
		{.bus = 13900, .pci_div = 4},
		{.bus = 10000, .pci_div = 3},
		{.bus = 14000, .pci_div = 4},
		{.bus = 14300, .pci_div = 4},
		{.bus = 14500, .pci_div = 4},
		{.bus = 14800, .pci_div = 4},
		{.bus = 15000, .pci_div = 4},
		{.bus = 15500, .pci_div = 4},
		{.bus = 16666, .pci_div = 3},
		{.bus = 13333, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_143)
	.max_reg = 5,
	.regs = {0x82, 0xff, 0xff, 0xff, 0xd5, 0xff},
	.fs_regs = {{0, 4, -1, -1}, {0, 5, 4, 3}, {0, 6, -1, -1}, {0, 2, 4, 1}, {-1, -1, -1, -1}},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus =  6667, .pci_div = 2},
		{.bus = 10000, .pci_div = 3},
		{.bus = 10030, .pci_div = 3},
		{.bus = 13333, .pci_div = 4},
		{.bus = 10500, .pci_div = 3},
		{.bus = 13337, .pci_div = 4},
		{.bus = 13700, .pci_div = 4},
		{.bus =  7500, .pci_div = 2},
		{.bus = 10000, .pci_div = 3},
		{.bus =  9500, .pci_div = 2},
		{.bus =  9700, .pci_div = 3},
		{.bus = 13333, .pci_div = 4},
		{.bus =  9000, .pci_div = 3},
		{.bus =  9622, .pci_div = 3},
		{.bus =  6681, .pci_div = 2},
		{.bus =  9150, .pci_div = 3},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_151)
	.max_reg = 6,
	.regs = {0x80, 0x4f, 0xff, 0x3f, 0xff, 0xff, 0x06},
	.fs_regs = {{0, 4, -1, -1}, {0, 5, -1, -1}, {0, 6, 3, 7}, {0, 1, 1, 4}, {0, 2, 1, 5}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus = 20000, .pci_div = 5, .agp_div = 2.5},
		{.bus = 19000, .pci_div = 5, .agp_div = 2.5},
		{.bus = 18000, .pci_div = 5, .agp_div = 2.5},
		{.bus = 17000, .pci_div = 5, .agp_div = 2.5},
		{.bus = 16600, .pci_div = 5, .agp_div = 2.5},
		{.bus = 16000, .pci_div = 5, .agp_div = 2.5},
		{.bus = 15000, .pci_div = 4, .agp_div =   2},
		{.bus = 14500, .pci_div = 4, .agp_div =   2},
		{.bus = 14000, .pci_div = 4, .agp_div =   2},
		{.bus = 13600, .pci_div = 4, .agp_div =   2},
		{.bus = 13000, .pci_div = 4, .agp_div =   2},
		{.bus = 12400, .pci_div = 4, .agp_div =   2},
		{.bus =  6667, .pci_div = 1, .agp_div =   1},
		{.bus = 10000, .pci_div = 3, .agp_div = 1.5},
		{.bus = 11800, .pci_div = 3, .agp_div = 1.5},
		{.bus = 13333, .pci_div = 3, .agp_div =   2},
		{.bus =  6680, .pci_div = 2, .agp_div =   1},
		{.bus = 10020, .pci_div = 3, .agp_div = 1.5},
		{.bus = 11500, .pci_div = 3, .agp_div = 1.5},
		{.bus = 13340, .pci_div = 4, .agp_div =   2},
		{.bus =  6680, .pci_div = 2, .agp_div =   1},
		{.bus = 10020, .pci_div = 3, .agp_div = 1.5},
		{.bus = 11000, .pci_div = 2, .agp_div = 1.5},
		{.bus = 13340, .pci_div = 4, .agp_div =   2},
		{.bus = 10500, .pci_div = 3, .agp_div = 1.5},
		{.bus =  9000, .pci_div = 3, .agp_div = 1.5},
		{.bus =  8500, .pci_div = 3, .agp_div = 1.5},
		{.bus =  7800, .pci_div = 2, .agp_div =   1},
		{.bus =  6667, .pci_div = 1, .agp_div =   1},
		{.bus = 10000, .pci_div = 3, .agp_div = 1.5},
		{.bus =  7500, .pci_div = 2, .agp_div =   1},
		{.bus = 13333, .pci_div = 3, .agp_div =   2},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9248_192)
	.max_reg = 6,
	.regs = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
	.fs_regs = {{0, 4, -1, -1}, {0, 5, 4, 3}, {0, 6, -1, -1}, {0, 7, -1, -1}, {0, 2, -1, -1}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus = 6000, .pci_div = 2},
		{.bus = 6000, .pci_div = 2},
		{.bus = 6000, .pci_div = 2},
		{.bus = 6000, .pci_div = 2},
		{.bus = 6659, .pci_div = 2},
		{.bus = 6659, .pci_div = 2},
		{.bus = 6659, .pci_div = 2},
		{.bus = 6659, .pci_div = 2},
		{.bus = 6731, .pci_div = 2},
		{.bus = 6864, .pci_div = 2},
		{.bus = 6995, .pci_div = 2},
		{.bus = 7259, .pci_div = 2},
		{.bus = 6150, .pci_div = 2},
		{.bus = 6300, .pci_div = 2},
		{.bus = 6400, .pci_div = 2},
		{.bus = 6500, .pci_div = 2},
		{.bus = 6000, .pci_div = 2},
		{.bus = 6659, .pci_div = 2},
		{.bus = 5000, .pci_div = 2},
		{.bus = 4800, .pci_div = 2},
		{.bus = 5880, .pci_div = 2},
		{.bus = 5760, .pci_div = 2},
		{.bus = 5640, .pci_div = 2},
		{.bus = 5400, .pci_div = 2},
		{.bus = 6000, .pci_div = 2},
		{.bus = 6000, .pci_div = 2},
		{.bus = 6000, .pci_div = 2},
		{.bus = 6000, .pci_div = 2},
		{.bus = 6659, .pci_div = 2},
		{.bus = 6659, .pci_div = 2},
		{.bus = 6659, .pci_div = 2},
		{.bus = 6659, .pci_div = 2},
		{0}
	}
    ICS9xxx_MODEL_END()
#endif
    ICS9xxx_MODEL(ICS9250_08)
	.max_reg = 5,
	.regs = {0x00, 0xff, 0xff, 0xff, 0x6d, 0xbf},
	.fs_regs = {{0, 4, 4, 7}, {0, 5, 4, 4}, {0, 6, 5, 6}, {0, 7, 4, 1}, {-1, -1, -1, -1}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
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
		{.bus = 13300, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
#if 0
    ICS9xxx_MODEL(ICS9250_10)
	.max_reg = 5,
	.regs = {0x1f, 0xff, 0xfe, 0x00, 0x00, 0x06},
	.fs_regs = {{5, 0, -1, -1}, {5, 3, -1, -1}, {5, 4, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
	.hw_select = {-1, -1},
	.frequencies = (const ics9xxx_frequency_t[]) {
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
		{.bus = 13300, .ram_mult =   1, .pci_div = 3},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_13)
	.max_reg = 5,
	.regs = {0x82, 0xcf, 0x7f, 0xff, 0xff, 0xf7},
	.fs_regs = {{0, 4, 1, 4}, {0, 5, 5, 7}, {0, 6, 1, 5}, {0, 2, 2, 7}, {-1, -1, -1, -1}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
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
		{.bus = 6682, .ram_mult = 1, .pci_div = 2},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_14)
	.max_reg = 5,
	.regs = {0x02, 0x1f, 0xff, 0xff, 0xeb, 0xff},
	.fs_regs = {{0, 4, 1, 6}, {0, 5, 4, 2}, {0, 6, 1, 5}, {0, 7, 1, 7}, {0, 2, 4, 4}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
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
		{.bus = 14969, .ram_mult =   1, .pci_div = 3},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_16)
	.max_reg = 5,
	.regs = {0x1f, 0xff, 0xff, 0x00, 0x00, 0x06},
	.fs_regs = {{5, 0, -1, -1}, {5, 3, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
	.hw_select = {-1, -1},
	.frequencies = (const ics9xxx_frequency_t[]) {
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
		{.bus = 13299, .ram_mult =    1, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
#endif
    ICS9xxx_MODEL(ICS9250_18)
	.max_reg = 5,
	.regs = {0x02, 0xff, 0xff, 0xff, 0x6d, 0xbf},
	.fs_regs = {{0, 4, 4, 7}, {0, 5, 4, 4}, {0, 6, 5, 6}, {0, 7, 4, 1}, {-1, -1, -1, -1}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
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
		{.bus = 13801, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
#if 0
    ICS9xxx_MODEL(ICS9250_19)
	.max_reg = 5,
	.regs = {0x02, 0xff, 0xff, 0xff, 0x6d, 0xbf},
	.fs_regs = {{0, 4, 4, 7}, {0, 5, 4, 4}, {0, 6, 5, 6}, {0, 7, 4, 1}, {-1, -1, -1, -1}},
	.hw_select = {0, 3},
	.frequencies_ref = ICS9250_08
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_23)
	.max_reg = 5,
	.regs = {0x02, 0x1f, 0xff, 0xff, 0xeb, 0xff},
	.fs_regs = {{0, 4, 1, 6}, {0, 5, 4, 2}, {0, 6, 1, 5}, {0, 7, 1, 7}, {0, 2, 4, 4}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
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
		{.bus = 16667, .ram_mult = 0.75, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_25)
	.max_reg = 6,
	.regs = {0x02, 0x1f, 0xff, 0xff, 0xeb, 0xff, 0x06},
	.fs_regs = {{0, 4, 1, 6}, {0, 5, 4, 2}, {0, 6, 1, 5}, {0, 7, 1, 7}, {0, 2, 4, 4}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
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
		{.bus = 15333, .ram_mult = 0.75, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_26)
	.max_reg = 5,
	.regs = {0x1e, 0xff, 0xff, 0x00, 0x00, 0x06},
	.fs_regs = {{5, 0, -1, -1}, {5, 3, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
	.hw_select = {-1, -1},
	.frequencies_ref = ICS9250_16
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_27)
	.max_reg = 5,
	.regs = {0x0f, 0xff, 0xfe, 0x00, 0x00, 0x00},
	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {3, 0, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
	.hw_select = {-1, -1},
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus =  6666, .ram_mult =  1.5, .pci_div = 2},
		{.bus = 13332, .ram_mult =    1, .pci_div = 4},
		{.bus = 10000, .ram_mult =    1, .pci_div = 3},
		{.bus = 13332, .ram_mult = 0.75, .pci_div = 4},
		{.bus =  6666, .ram_mult =  1.5, .pci_div = 2},
		{.bus = 13332, .ram_mult =    1, .pci_div = 4},
		{.bus = 10000, .ram_mult =    1, .pci_div = 3},
		{.bus = 13332, .ram_mult =    1, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_28)
	.max_reg = 4,
	.regs = {0x1e, 0xff, 0xfe, 0x00, 0x00},
	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {3, 0, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
	.hw_select = {-1, -1},
	.frequencies_ref = ICS9250_27
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_29)
	.max_reg = 5,
	.regs = {0x16, 0xff, 0xfe, 0x00, 0x00, 0x00},
	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {3, 0, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
	.hw_select = {-1, -1},
	.frequencies_ref = ICS9250_27
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_30)
	.max_reg = 6,
	.regs = {0x02, 0x0f, 0xff, 0xff, 0xeb, 0xff, 0x06},
	.fs_regs = {{0, 4, 1, 6}, {0, 5, 4, 2}, {0, 6, 1, 5}, {0, 7, 1, 7}, {0, 2, 4, 4}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
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
		{.bus = 16000, .ram_mult = 0.75, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_32)
	.max_reg = 4,
	.regs = {0x07, 0xff, 0xff, 0x00, 0x00},
	.fs_regs = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_38)
	.max_reg = 6,
	.regs = {0x18, 0x07, 0xfe, 0xc7, 0xfc, 0x00, 0x80},
	.fs_regs = {{0, 0, -1, -1}, {0, 1, -1, -1}, {0, 2, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
	.normal_bits_fixed = 1,
	.frequencies = (const ics9xxx_frequency_t[]) {
		{.bus =  6666, .ram_mult =       1, .pci_div = 1},
		{.bus = 10000, .ram_mult = 2.0/3.0, .pci_div = 3},
		{.bus = 20000, .ram_mult = 1.0/3.0, .pci_div = 6},
		{.bus = 13333, .ram_mult =     0.5, .pci_div = 2},
		{.bus =  6666, .ram_mult =       1, .pci_div = 1},
		{.bus = 10000, .ram_mult = 2.0/3.0, .pci_div = 3},
		{.bus = 20000, .ram_mult = 1.0/3.0, .pci_div = 6},
		{.bus = 13333, .ram_mult =     0.5, .pci_div = 2},
		{0}
	}
    ICS9xxx_MODEL_END()
    ICS9xxx_MODEL(ICS9250_50)
	.max_reg = 6,
	.regs = {0x02, 0x6f, 0xff, 0xff, 0xef, 0xff, 0x06},
	.fs_regs = {{-1, -1, 1, 6}, {-1, -1, 4, 2}, {-1, -1, 1, 5}, {0, 7, 1, 7}, {0, 2, 4, 4}},
	.hw_select = {0, 3},
	.frequencies = (const ics9xxx_frequency_t[]) {
		[0 ... 7]   = {.bus =  6667, .ram_mult =  1.5, .pci_div = 2},
		[8 ... 15]  = {.bus = 10000, .ram_mult =    1, .pci_div = 3},
		[16 ... 23] = {.bus = 13333, .ram_mult =    1, .pci_div = 4},
		[24 ... 31] = {.bus = 13333, .ram_mult = 0.75, .pci_div = 4},
		{0}
	}
    ICS9xxx_MODEL_END()
#endif
};


/* Don't enable the detection device here. Enable it further up near logging. */
#ifdef ENABLE_ICS9xxx_DETECT
static uint16_t	detect_bus = 0;
static uint8_t	detect_reg = 0;
static uint8_t	discarded[ICS9xxx_MAX] = {0};


static void
ics9xxx_detect_reset(void *priv)
{
    pclog("Please enter the frequency set in the BIOS (7500 for 75.00 MHz)\nAnswer 0 if unsure or set to auto, I'll ask again next reset.\n");
    scanf("%hu", &detect_bus);
    if ((detect_bus > 0) && (detect_bus < 1000))
	detect_bus *= 100;
    pclog("Frequency interpreted as %d\n", detect_bus);
}


static void
ics9xxx_detect(ics9xxx_t *dev)
{
    if (!detect_bus) {
	pclog("Frequency not entered on this reset, ignoring change.\n");
	return;
    }

    if ((detect_reg == 0) && (dev->regs[detect_reg] >= 0xfe)) {
	pclog("Register %d set to %02X, probably not it, trying %d instead\n", detect_reg, dev->regs[detect_reg], 3);
	detect_reg = 3;
	dev->relevant_regs = 1 << detect_reg;
	return;
    }

    if (!(dev->regs[detect_reg] & 0x40))
	pclog("Bit 3 of register %d is clear, probably in hardware select mode!\n", detect_reg);

    uint8_t i = 0, matches = 0, val, bitmask;
    ics9xxx_frequency_t *frequencies_ptr;
    uint32_t delta;
    for (uint8_t j = 0; j < ICS9xxx_MAX; j++) {
	if (discarded[j])
		continue;
	discarded[j] = 1;

	frequencies_ptr = (ics9xxx_frequency_t *) ics9xxx_models[ics9xxx_models[j].frequencies_ref ? ics9xxx_models[j].frequencies_ref : j].frequencies;
	if (!frequencies_ptr)
		continue;

	while (frequencies_ptr[i].bus) {
		delta = ABS((int32_t) (detect_bus - frequencies_ptr[i].bus));
		if (delta <= 100) {
			val = bitmask = 0;
			for (uint8_t k = 0; k < sizeof(ics9xxx_models[j].fs_regs) / sizeof(ics9xxx_models[j].fs_regs[0]); k++) {
				if (ics9xxx_models[j].fs_regs[k].normal_reg == detect_reg) {
					bitmask |= 1 << k;
					val |= (1 << k) * !!(dev->regs[detect_reg] & (1 << ics9xxx_models[j].fs_regs[k].normal_bit));
				}
			}
			if (bitmask && (val == (i & bitmask))) {
				matches++;
				discarded[j] = 0;
				pclog("> Potential match for %s (frequency %d index %d)\n", ics9xxx_models[j].name, frequencies_ptr[i].bus, val);
			}
		}

		i++;
	}
    }

    pclog("Found a total of %d matches for register %d value %02X and bus frequency %d\n", matches, detect_reg, dev->regs[detect_reg], detect_bus);
    if (matches == 0) {
	pclog("Resetting list of discarded models since there were no matches.\n");
	memset(discarded, 0, sizeof(discarded));
    }
}
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
	ret = dev->model->max_reg + 1;
    }
#if 0
    else if ((dev->model_idx == ICS9250_50) && (dev->addr_register == 0))
	ret = dev->regs[dev->addr_register] & 0x0b; /* -50 reads back revision ID instead */
#endif
    else
	ret = dev->regs[dev->addr_register];

#ifdef ENABLE_ICS9xxx_LOG
    if (dev->addr_register < 0)
	ics9xxx_log("ICS9xxx: read(%s) = %02X\n", (dev->addr_register == -1) ? "blocklen" : "command", ret);
    else
	ics9xxx_log("ICS9xxx: read(%x) = %02X\n", dev->addr_register, ret);
#endif
    if (dev->addr_register >= dev->model->max_reg)
	dev->addr_register = 0; /* roll-over */
    else
	dev->addr_register++;

    return ret;
}


static void
ics9xxx_set(ics9xxx_t *dev, uint8_t val)
{
    /* Get the active mode, which determines what to add to the static frequency bits we were passed. */
    uint8_t hw_select = (dev->model->hw_select.normal_reg < 7) && !(dev->regs[dev->model->hw_select.normal_reg] & (1 << dev->model->hw_select.normal_bit));
    if (hw_select) {
	/* Hardware select mode: add strapped frequency bits. */
	val |= dev->bus_match;
    } else {
	/* Programmable mode: add register-defined frequency bits. */
	for (uint8_t i = 0; i < sizeof(dev->model->fs_regs) / sizeof(dev->model->fs_regs[0]); i++) {
		if ((dev->model->fs_regs[i].normal_reg < 7) && (dev->regs[dev->model->fs_regs[i].normal_reg] & (1 << dev->model->fs_regs[i].normal_bit)))
			val |= 1 << i;
	}
    }

    uint16_t bus = dev->frequencies_ptr[val].bus;
    uint32_t pci = bus / dev->frequencies_ptr[val].pci_div;
    cpu_set_pci_speed(pci * 10000);

    ics9xxx_log("ICS9xxx: set(%d) = hw=%d bus=%d ram=%d pci=%d\n", val, hw_select, bus, bus * dev->frequencies_ptr[val].ram_mult, pci);
}


static uint8_t
ics9xxx_write(void *bus, uint8_t addr, uint8_t data, void *priv)
{
    ics9xxx_t *dev = (ics9xxx_t *) priv;

#ifdef ENABLE_ICS9xxx_LOG
    if (dev->addr_register < 0)
	ics9xxx_log("ICS9xxx: write(%s, %02X)\n", (dev->addr_register == -1) ? "blocklen" : "command", data);
    else
	ics9xxx_log("ICS9xxx: write(%x, %02X)\n", dev->addr_register, data);
#endif

    if (dev->addr_register >= 0) {
	/* Preserve fixed bits. */
#ifdef ENABLE_ICS9xxx_DETECT
	if (dev->model != ICS9xxx_xx)
#endif
	{
		for (uint8_t i = 0; i < sizeof(dev->model->fs_regs) / sizeof(dev->model->fs_regs[0]); i++) {
			if (dev->model->normal_bits_fixed && (dev->model->fs_regs[i].normal_reg == dev->addr_register))
				data = (dev->regs[dev->addr_register] & (1 << dev->model->fs_regs[i].normal_bit)) | (data & ~(1 << dev->model->fs_regs[i].normal_bit));
			if (dev->model->fs_regs[i].inv_reg == dev->addr_register)
				data = (dev->regs[dev->addr_register] & (1 << dev->model->fs_regs[i].inv_bit)) | (data & ~(1 << dev->model->fs_regs[i].inv_bit));
		}
	}

#if 0
	switch (dev->addr_register) {
		case 0:
			if (dev->model_idx == ICS9250_38)
				data = (dev->regs[dev->addr_register] & ~0xe8) | (data & 0xe8);
			break;

		case 1:
			if (dev->model_idx == ICS9250_38)
				data = (dev->regs[dev->addr_register] & ~0xfe) | (data & 0xfe);
			break;

		case 3:
			if (dev->model_idx == ICS9250_32)
				data ^= 0x70;
			break;

		case 4:
			if (dev->model_idx == ICS9250_38)
				data = (dev->regs[dev->addr_register] & ~0xfc) | (data & 0xfc);
			break;

		case 6:
			if (dev->model_idx == ICS9250_38) /* read-only */
				data = dev->regs[dev->addr_register];
			break;
	}
#endif
	dev->regs[dev->addr_register] = data;

	/* Update frequency if a relevant register was written to. */
	if (dev->relevant_regs & (1 << dev->addr_register)) {
		switch (dev->model_idx) {
#ifdef ENABLE_ICS9xxx_DETECT
			case ICS9xxx_xx:
				ics9xxx_detect(dev);
				break;
#endif
#if 0
			case ICS9250_10:
				ics9xxx_set(dev, (cpu_busspeed >= 100000000) * 0x08);
				break;

			case ICS9250_16:
			case ICS9250_26:
				ics9xxx_set(dev, ((cpu_busspeed >= 120000000) * 0x08) | ((((cpu_busspeed >= 100000000) && (cpu_busspeed < 120000000)) || (cpu_busspeed == 150000000) || (cpu_busspeed == 132999999)) * 0x04));
				break;

			case ICS9250_27:
			case ICS9250_28:
			case ICS9250_29:
				ics9xxx_set(dev, ((cpu_busspeed == 100000000) * 0x02) | ((cpu_busspeed > 100000000) * 0x01));
				break;
#endif
			default:
				ics9xxx_set(dev, 0x00);
				break;
		}
	}
    }

    if (dev->addr_register >= dev->model->max_reg)
	dev->addr_register = 0; /* roll-over */
    else
	dev->addr_register++;

    return 1;
}


static uint8_t
ics9xxx_find_bus_match(ics9xxx_t *dev, uint32_t bus, uint8_t preset_mask, uint8_t preset) {
    uint8_t best_match = 0;
    uint32_t delta, best_delta = -1;

#ifdef ENABLE_ICS9xxx_DETECT
    if (dev->model_idx == ICS9xxx_xx)
	return 0;
#endif

    bus /= 10000;
    uint8_t i = 0;
    while (dev->frequencies_ptr[i].bus) {
	if ((i & preset_mask) == preset) {
		delta = ABS((int32_t) (bus - dev->frequencies_ptr[i].bus));
		if (delta < best_delta) {
			best_match = i;
			best_delta = delta;
		}
	}

	i++;
    }

    ics9xxx_log("ICS9xxx: find_match(%s, %d) = match=%d bus=%d\n", dev->model->name, bus, best_match, dev->frequencies_ptr[best_match].bus);

    return best_match;
}


static void *
ics9xxx_init(const device_t *info)
{
    ics9xxx_t *dev = (ics9xxx_t *) malloc(sizeof(ics9xxx_t));
    memset(dev, 0, sizeof(ics9xxx_t));

    dev->model_idx = info->local;
    dev->model = (ics9xxx_model_t *) &ics9xxx_models[dev->model_idx];
    dev->dyn_device = (device_t *) info;
    memcpy(&dev->regs, &dev->model->regs, dev->model->max_reg + 1);

    ics9xxx_log("ICS9xxx: init(%s)\n", dev->model->name);

    uint8_t i;
#ifdef ENABLE_ICS9xxx_DETECT
    for (i = 0; i < ICS9xxx_MAX; i++) {
	if (ics9xxx_models[i].frequencies_ref || !ics9xxx_models[i].name)
		continue;
	for (uint8_t j = 0; j < i; j++) {
		if (ics9xxx_models[j].frequencies_ref || !ics9xxx_models[j].name)
			continue;
		if (!memcmp(&ics9xxx_models[i].frequencies, &ics9xxx_models[j].frequencies, sizeof(ics9xxx_models[i].frequencies)))
			pclog("Optimization warning: %s and %s have duplicate tables\n", ics9xxx_models[j].name, ics9xxx_models[i].name);
	}
    }

    if (dev->model_idx == ICS9xxx_xx) { /* detection device */
	dev->relevant_regs = 1 << 0; /* register 0 matters the most on the detection device */

	ics9xxx_detect_reset(dev);
    } else
#endif
    { /* regular device */
	dev->frequencies_ptr = (ics9xxx_frequency_t *) (dev->model->frequencies_ref ? ics9xxx_models[dev->model->frequencies_ref].frequencies : dev->model->frequencies);
	if (!dev->frequencies_ptr)
		fatal("ICS9xxx: NULL frequency table\n");
	
	/* Determine which frequency bits cannot be strapped (register only). */
	uint8_t register_only_bits = 0x00;
	for (i = 0; i < sizeof(dev->model->fs_regs) / sizeof(dev->model->fs_regs[0]); i++) {
		if (!dev->model->normal_bits_fixed && (dev->model->fs_regs[i].normal_reg < 7)) /* mark a normal, programmable bit as relevant */
			dev->relevant_regs |= 1 << dev->model->fs_regs[i].normal_reg;
		if ((dev->model->fs_regs[i].normal_reg == 7) && (dev->model->fs_regs[i].inv_reg == 7)) /* mark as register only */
			register_only_bits |= 1 << i;
	}
	
	/* Mark the hardware select bit's register as relevant, if there's one. */
	if (dev->model->hw_select.normal_reg < 7)
		dev->relevant_regs |= 1 << dev->model->hw_select.normal_reg;
	
	/* Find bus speed match and set default register bits accordingly. */
	dev->bus_match = ics9xxx_find_bus_match(dev, cpu_busspeed, register_only_bits, 0x00);
	for (i = 0; i < sizeof(dev->model->fs_regs) / sizeof(dev->model->fs_regs[0]); i++) {
		if (dev->model->fs_regs[i].normal_reg < 7) {
			if (dev->bus_match & (1 << i))
				dev->regs[dev->model->fs_regs[i].normal_reg] |= 1 << dev->model->fs_regs[i].normal_bit;
			else
				dev->regs[dev->model->fs_regs[i].normal_reg] &= ~(1 << dev->model->fs_regs[i].normal_bit);
		}
		if (dev->model->fs_regs[i].inv_reg < 7) {
			if (dev->bus_match & (1 << i))
				dev->regs[dev->model->fs_regs[i].inv_reg] &= ~(1 << dev->model->fs_regs[i].inv_bit);
			else
				dev->regs[dev->model->fs_regs[i].inv_reg] |= 1 << dev->model->fs_regs[i].inv_bit;
		}
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

    free(dev->dyn_device);
    free(dev);
}


device_t *
ics9xxx_get(uint8_t model)
{
    device_t *dev = (device_t *) malloc(sizeof(device_t));
    memset(dev, 0, sizeof(device_t));

    dev->name = "ICS9xxx-xx Clock Generator";
    dev->local = model;
    dev->flags = DEVICE_ISA;
#ifdef ENABLE_ICS9xxx_DETECT
    if (model == ICS9xxx_xx)
	dev->reset = ics9xxx_detect_reset;
#endif
    dev->init = ics9xxx_init;
    dev->close = ics9xxx_close;

    return dev;
}

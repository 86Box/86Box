/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the VIA VT82C686A/B integrated hardware monitor.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
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
#include <86box/io.h>
#include <86box/hwm.h>


#define CLAMP(a, min, max)		(((a) < (min)) ? (min) : (((a) > (max)) ? (max) : (a)))
#define VT82C686_RPM_TO_REG(r, d)	((r) ? CLAMP(1350000 / (r * d), 1, 255) : 0)
/* Temperature/voltage formulas and factors derived from Linux's via686a.c driver. */
#define VT82C686_TEMP_TO_REG(t)		(-1.160370e-10*(t*t*t*t*t*t) + 3.193693e-08*(t*t*t*t*t) - 1.464447e-06*(t*t*t*t) - 2.525453e-04*(t*t*t) + 1.424593e-02*(t*t) + 2.148941e+00*t + 7.275808e+01)
#define VT82C686_VOLTAGE_TO_REG(v, f)	CLAMP((((v) * (2.628 / (f))) - 120.5) / 25, 0, 255)


typedef struct {
    hwm_values_t *values;

    uint8_t	enable;
    uint16_t	io_base;
    uint8_t	regs[128];
} vt82c686_t;


static double voltage_factors[5] = {1.25, 1.25, 1.67, 2.6, 6.3};


static void	vt82c686_reset(vt82c686_t *dev, uint8_t initialization);


static uint8_t
vt82c686_read(uint16_t addr, void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;
    uint8_t ret;

    addr -= dev->io_base;

    switch (addr) {
	case 0x00 ... 0x0f: case 0x50 ... 0x7f: /* undefined registers */
		/* Real 686B returns the contents of 0x40. */
		ret = dev->regs[0x40];
		break;

	case 0x1f: case 0x20: case 0x21: /* temperatures */
		ret = VT82C686_TEMP_TO_REG(dev->values->temperatures[(addr == 0x1f) ? 2 : (addr & 1)]);
		break;

	case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: /* voltages */
		ret = VT82C686_VOLTAGE_TO_REG(dev->values->voltages[addr - 0x22], voltage_factors[addr - 0x22]);
		break;

	case 0x29: case 0x2a: /* fan speeds */
		ret = VT82C686_RPM_TO_REG(dev->values->fans[addr - 0x29], 1 << ((dev->regs[0x47] >> ((addr == 0x29) ? 4 : 6)) & 0x3));
		break;

	default: /* other registers */
		ret = dev->regs[addr];
		break;
    }

    return ret;
}


static void
vt82c686_write(uint16_t port, uint8_t val, void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;
    uint8_t reg = port & 0x7f;

    switch (reg) {
	case 0x00 ... 0x0f:
	case 0x3f: case 0x41: case 0x42: case 0x4a:
	case 0x4c ... 0x7f:
		/* Read-only registers. */
		return;

	case 0x40:
		/* Reset if requested. */
		if (val & 0x80) {
			vt82c686_reset(dev, 1);
			return;
		}
		break;

	case 0x48:
		val &= 0x7f;
		break;
    }

    dev->regs[reg] = val;
}


/* Writes to hardware monitor-related configuration space registers
   of the VT82C686 power management function are sent here by via_pipc.c */
void
vt82c686_hwm_write(uint8_t addr, uint8_t val, void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;

    if (dev->io_base)
	io_removehandler(dev->io_base, 128,
			 vt82c686_read, NULL, NULL, vt82c686_write, NULL, NULL, dev);

    switch (addr) {
	case 0x70:
		dev->io_base &= 0xff00;
		dev->io_base |= val & 0x80;
		break;

	case 0x71:
		dev->io_base &= 0x00ff;
		dev->io_base |= val << 8;
		break;

	case 0x74:
		dev->enable = val & 0x01;
		break;
    }

    if (dev->enable && dev->io_base)
	io_sethandler(dev->io_base, 128,
		      vt82c686_read, NULL, NULL, vt82c686_write, NULL, NULL, dev);
}


static void
vt82c686_reset(vt82c686_t *dev, uint8_t initialization)
{
    memset(dev->regs, 0, sizeof(dev->regs));

    dev->regs[0x17] = 0x80;
    dev->regs[0x3f] = 0xa2;
    dev->regs[0x40] = 0x08;
    dev->regs[0x47] = 0x50;
    dev->regs[0x4b] = 0x15;

    if (!initialization)
	vt82c686_hwm_write(0x74, 0x00, dev);
}


static void
vt82c686_close(void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;

    free(dev);
}


static void *
vt82c686_init(const device_t *info)
{
    vt82c686_t *dev = (vt82c686_t *) malloc(sizeof(vt82c686_t));
    memset(dev, 0, sizeof(vt82c686_t));

    /* Set default values. Since this hardware monitor has a complex voltage factor system,
       the values struct contains voltage values *before* applying their respective factors. */
    hwm_values_t defaults = {
	{    /* fan speeds */
		3000,	/* usually CPU */
		3000	/* usually Chassis */
	}, { /* temperatures */
		30,	/* usually CPU */
		30,	/* usually System */
		30
	}, { /* voltages */
		hwm_get_vcore(), /* Vcore */
		2500,		 /* +2.5V */
		3300,		 /* +3.3V */
		5000,		 /* +5V */
		12000		 /* +12V */
	}
    };
    hwm_values = defaults;
    dev->values = &hwm_values;

    vt82c686_reset(dev, 0);

    return dev;
}


const device_t via_vt82c686_hwm_device = {
    "VIA VT82C686 Integrated Hardware Monitor",
    DEVICE_ISA,
    0,
    vt82c686_init, vt82c686_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};

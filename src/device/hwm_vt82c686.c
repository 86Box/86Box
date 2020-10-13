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
#include "cpu.h"
#include <86box/smbus.h>
#include <86box/hwm.h>


#define CLAMP(a, min, max)	(((a) < (min)) ? (min) : (((a) > (max)) ? (max) : (a)))
#define VT82C686_RPM_TO_REG(r, d)	((r) ? CLAMP(1350000 / (r * d), 1, 255) : 0)
/* Temperature formula from source comments in Linux's via686a.c driver */
#define VT82C686_TEMP_TO_REG(t)		(-1.160370e-10*(t*t*t*t*t*t) + 3.193693e-08*(t*t*t*t*t) - 1.464447e-06*(t*t*t*t) - 2.525453e-04*(t*t*t) + 1.424593e-02*(t*t) + 2.148941e+00*t + 7.275808e+01)
#define VT82C686_VOLTAGE_TO_REG(v)	((v) >> 4)


typedef struct {
    hwm_values_t *values;
    device_t     *lm75[2];

    uint8_t	enable;
    uint16_t	io_base;
    uint8_t	regs[80];
} vt82c686_t;


static void	vt82c686_reset(vt82c686_t *dev, uint8_t initialization);


static uint8_t
vt82c686_read(uint16_t addr, void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;
    return dev->regs[addr - dev->io_base];
}


static void
vt82c686_write(uint16_t port, uint8_t val, void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;
    uint8_t reg = port - dev->io_base;

    if ((reg == 0x41) || (reg == 0x42) || (reg == 0x45) || (reg == 0x46) || (reg == 0x48) || (reg == 0x4a) || (reg >= 0x4c))
	return;

    switch (reg) {
    	case 0x40:
    		if (val & 0x80)
    			vt82c686_reset(dev, 1);
    		break;

	case 0x47:
		val &= 0xf0;
		/* update FAN1/FAN2 values to match the new divisor */
		dev->regs[0x29] = VT82C686_RPM_TO_REG(dev->values->fans[0], 1 << ((dev->regs[0x47] >> 4) & 0x3));
		dev->regs[0x2a] = VT82C686_RPM_TO_REG(dev->values->fans[1], 1 << ((dev->regs[0x47] >> 6) & 0x3));
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
    	io_removehandler(dev->io_base, 0x0050,
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
    	io_sethandler(dev->io_base, 0x0050,
		      vt82c686_read, NULL, NULL, vt82c686_write, NULL, NULL, dev);
}


static void
vt82c686_reset(vt82c686_t *dev, uint8_t initialization)
{
    memset(dev->regs, 0, 80);

    dev->regs[0x1f] = VT82C686_TEMP_TO_REG(dev->values->temperatures[2]);
    dev->regs[0x20] = VT82C686_TEMP_TO_REG(dev->values->temperatures[0]);
    dev->regs[0x21] = VT82C686_TEMP_TO_REG(dev->values->temperatures[1]);

    dev->regs[0x22] = VT82C686_VOLTAGE_TO_REG(dev->values->voltages[0]);
    dev->regs[0x23] = VT82C686_VOLTAGE_TO_REG(dev->values->voltages[1]);
    dev->regs[0x24] = VT82C686_VOLTAGE_TO_REG(dev->values->voltages[2]);
    dev->regs[0x25] = VT82C686_VOLTAGE_TO_REG(dev->values->voltages[3]);
    dev->regs[0x26] = VT82C686_VOLTAGE_TO_REG(dev->values->voltages[4]);

    dev->regs[0x29] = VT82C686_RPM_TO_REG(dev->values->fans[0], 2);
    dev->regs[0x2a] = VT82C686_RPM_TO_REG(dev->values->fans[1], 2);

    dev->regs[0x40] = 0x08;
    dev->regs[0x47] = 0x50;
    dev->regs[0x49] = (dev->values->temperatures[2] & 0x3) << 6;
    dev->regs[0x49] |= (dev->values->temperatures[1] & 0x3) << 4;
    dev->regs[0x4b] = (dev->values->temperatures[0] & 0x3) << 6;
    dev->regs[0x4b] |= 0x15;

    if (!initialization)
	vt82c686_hwm_write(0x85, 0x00, dev);
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

    dev->values = hwm_get_values();

    vt82c686_reset(dev, 0);

    return dev;
}


const device_t via_vt82c686_hwm_device = {
    "VIA VT82C686 Integrated Hardware Monitor",
    DEVICE_ISA,
    0,
    vt82c686_init, vt82c686_close, NULL,
    NULL, NULL, NULL,
    NULL
};

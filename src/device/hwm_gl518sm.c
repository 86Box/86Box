/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Genesys Logic GL518SM hardware monitoring chip.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <math.h>
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
#include <86box/i2c.h>
#include <86box/hwm.h>


#define CLAMP(a, min, max)		(((a) < (min)) ? (min) : (((a) > (max)) ? (max) : (a)))
/* Formulas and factors derived from Linux's gl518sm.c driver. */
#define GL518SM_RPM_TO_REG(r, d)	((r) ? CLAMP((480000 + (r) * (d) / 2) / (r) * (d), 1, 255) : 0)
#define GL518SM_VOLTAGE_TO_REG(v)	((uint8_t) round((v) / 19.0))
#define GL518SM_VDD_TO_REG(v)		((uint8_t) (((v) * 4) / 95.0))


typedef struct {
    uint32_t	  local;
    hwm_values_t  *values;

    uint16_t regs[32];
    uint8_t addr_register: 5;

    uint8_t i2c_addr: 7, i2c_state: 2, i2c_enabled: 1;
} gl518sm_t;


static uint8_t	gl518sm_i2c_start(void *bus, uint8_t addr, uint8_t read, void *priv);
static uint8_t	gl518sm_i2c_read(void *bus, uint8_t addr, void *priv);
static uint16_t	gl518sm_read(gl518sm_t *dev, uint8_t reg);
static uint8_t	gl518sm_i2c_write(void *bus, uint8_t addr, uint8_t data, void *priv);
static uint8_t	gl518sm_write(gl518sm_t *dev, uint8_t reg, uint16_t val);
static void	gl518sm_reset(gl518sm_t *dev);


#ifdef ENABLE_GL518SM_LOG
int gl518sm_do_log = ENABLE_GL518SM_LOG;


static void
gl518sm_log(const char *fmt, ...)
{
    va_list ap;

    if (gl518sm_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define gl518sm_log(fmt, ...)
#endif


static void
gl518sm_remap(gl518sm_t *dev, uint8_t addr)
{
    gl518sm_log("GL518SM: remapping to SMBus %02Xh\n", addr);

    if (dev->i2c_enabled)
	i2c_removehandler(i2c_smbus, dev->i2c_addr, 1, gl518sm_i2c_start, gl518sm_i2c_read, gl518sm_i2c_write, NULL, dev);

    if (addr < 0x80)
	i2c_sethandler(i2c_smbus, addr, 1, gl518sm_i2c_start, gl518sm_i2c_read, gl518sm_i2c_write, NULL, dev);

    dev->i2c_addr = addr & 0x7f;
    dev->i2c_enabled = !(addr & 0x80);
}


static uint8_t
gl518sm_i2c_start(void *bus, uint8_t addr, uint8_t read, void *priv)
{
    gl518sm_t *dev = (gl518sm_t *) priv;

    dev->i2c_state = 0;

    return 1;
}


static uint8_t
gl518sm_i2c_read(void *bus, uint8_t addr, void *priv)
{
    gl518sm_t *dev = (gl518sm_t *) priv;
    uint16_t read = gl518sm_read(dev, dev->addr_register);
    uint8_t ret = 0;

    if (dev->i2c_state == 0)
	dev->i2c_state = 1;

    if ((dev->i2c_state == 1) && (dev->addr_register >= 0x07) && (dev->addr_register <= 0x0c)) { /* two-byte registers: read MSB first */
	dev->i2c_state = 2;
	ret = read >> 8;
    } else {
	ret = read;
	dev->addr_register++;
    }

    return ret;
}


static uint16_t
gl518sm_read(gl518sm_t *dev, uint8_t reg)
{
    uint16_t ret;

    reg &= 0x1f;

    switch (reg) {
	case 0x04: /* temperature */
		ret = (dev->values->temperatures[0] + 119) & 0xff;
		break;

	case 0x07: /* fan speeds */
		ret = GL518SM_RPM_TO_REG(dev->values->fans[0], 1 << ((dev->regs[0x0f] >> 6) & 0x3)) << 8;
		ret |= GL518SM_RPM_TO_REG(dev->values->fans[1], 1 << ((dev->regs[0x0f] >> 4) & 0x3));
		break;

	case 0x0d: /* VIN3 */
		ret = GL518SM_VOLTAGE_TO_REG(dev->values->voltages[2]);
		break;

	case 0x13: /* VIN2 */
		ret = GL518SM_VOLTAGE_TO_REG(dev->values->voltages[1]);
		break;

	case 0x14: /* VIN1 */
		ret = GL518SM_VOLTAGE_TO_REG(dev->values->voltages[0]);
		break;

	case 0x15: /* VDD */
		ret = GL518SM_VDD_TO_REG(dev->values->voltages[3]);
		break;

	default: /* other registers */
		ret = dev->regs[reg];
		break;
    }

    gl518sm_log("GL518SM: read(%02X) = %04X\n", reg, ret);

    return ret;
}


static uint8_t
gl518sm_i2c_write(void *bus, uint8_t addr, uint8_t data, void *priv)
{
    gl518sm_t *dev = (gl518sm_t *) priv;

    switch (dev->i2c_state++) {
	case 0:
		dev->addr_register = data;
		break;

	case 1:
		gl518sm_write(dev, dev->addr_register, (gl518sm_read(dev, dev->addr_register) & 0xff00) | data);
		break;

	case 2:
		gl518sm_write(dev, dev->addr_register, (gl518sm_read(dev, dev->addr_register) << 8) | data);
		break;

	default:
		dev->i2c_state = 3;
		return 0;
    }

    return 1;
}


static uint8_t
gl518sm_write(gl518sm_t *dev, uint8_t reg, uint16_t val)
{
    gl518sm_log("GL518SM: write(%02X, %04X)\n", reg, val);

    switch (reg) {
	case 0x00: case 0x01: case 0x04: case 0x07: case 0x0d: case 0x12: case 0x13: case 0x14: case 0x15:
		/* read-only registers */
		return 0;

	case 0x0a:
		dev->regs[0x13] = val & 0xff;
		break;

	case 0x03:
		dev->regs[reg] = val & 0xfc;

		if (val & 0x80) /* Init */
			gl518sm_reset(dev);
		break;

	case 0x0f:
		dev->regs[reg] = val & 0xf8;
		break;

	case 0x11:
		dev->regs[reg] = val & 0x7f;
		break;

	default:
		dev->regs[reg] = val;
		break;
    }

    return 1;
}


static void
gl518sm_reset(gl518sm_t *dev)
{
    memset(dev->regs, 0, sizeof(dev->regs));

    dev->regs[0x00] = 0x80;
    dev->regs[0x01] = 0x80; /* revision 0x80 can read all voltages */
    dev->regs[0x05] = 0xc7;
    dev->regs[0x06] = 0xc2;
    dev->regs[0x08] = 0x6464;
    dev->regs[0x09] = 0xdac5;
    dev->regs[0x0a] = 0xdac5;
    dev->regs[0x0b] = 0xdac5;
    dev->regs[0x0c] = 0xdac5;
    dev->regs[0x0f] = 0x00;

    gl518sm_remap(dev, dev->i2c_addr | (dev->i2c_enabled ? 0x00 : 0x80));
}


static void
gl518sm_close(void *priv)
{
    gl518sm_t *dev = (gl518sm_t *) priv;

    gl518sm_remap(dev, 0);

    free(dev);
}


static void *
gl518sm_init(const device_t *info)
{
    gl518sm_t *dev = (gl518sm_t *) malloc(sizeof(gl518sm_t));
    memset(dev, 0, sizeof(gl518sm_t));

    dev->local = info->local;

    /* Set default values. */
    hwm_values_t defaults = {
	{    /* fan speeds */
		3000,	/* usually Chassis */
		3000	/* usually CPU */
	}, { /* temperatures */
		30	/* usually CPU */
	}, { /* voltages */
		hwm_get_vcore(),		  /* Vcore */
		RESISTOR_DIVIDER(12000, 150, 47), /* +12V (15K/4.7K divider suggested in the datasheet) */
		3300,				  /* +3.3V */
		5000				  /* +5V */
	}
    };
    hwm_values = defaults;
    dev->values = &hwm_values;

    gl518sm_reset(dev);
    gl518sm_remap(dev, dev->local & 0x7f);

    return dev;
}

/* GL518SM on SMBus address 2Ch */
const device_t gl518sm_2c_device = {
    .name = "Genesys Logic GL518SM Hardware Monitor",
    .internal_name = "gl518sm_2c",
    .flags = DEVICE_ISA,
    .local = 0x2c,
    .init = gl518sm_init,
    .close = gl518sm_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

/* GL518SM on SMBus address 2Dh */
const device_t gl518sm_2d_device = {
    .name = "Genesys Logic GL518SM Hardware Monitor",
    .internal_name = "gl518sm_2d",
    .flags = DEVICE_ISA,
    .local = 0x2d,
    .init = gl518sm_init,
    .close = gl518sm_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

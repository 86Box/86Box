/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the National Semiconductor LM75 temperature sensor chip.
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
#include <86box/i2c.h>
#include <86box/hwm.h>


#define LM75_TEMP_TO_REG(t)	((t) << 8)


#ifdef ENABLE_LM75_LOG
int lm75_do_log = ENABLE_LM75_LOG;


static void
lm75_log(const char *fmt, ...)
{
    va_list ap;

    if (lm75_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define lm75_log(fmt, ...)
#endif


static uint8_t
lm75_i2c_start(void *bus, uint8_t addr, uint8_t read, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;

    dev->i2c_state = 0;

    return 1;
}


uint8_t
lm75_read(lm75_t *dev, uint8_t reg)
{
    uint8_t ret;

    if ((reg & 0x7) == 0x0) /* temperature high byte */
	ret = LM75_TEMP_TO_REG(dev->values->temperatures[dev->local >> 8]) >> 8;
    else if ((reg & 0x7) == 0x1) /* temperature low byte */
	ret = LM75_TEMP_TO_REG(dev->values->temperatures[dev->local >> 8]);
    else
	ret = dev->regs[reg & 0x7];

    lm75_log("LM75: read(%02X) = %02X\n", reg, ret);

    return ret;
}


static uint8_t
lm75_i2c_read(void *bus, uint8_t addr, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;
    uint8_t ret = 0;

    if (dev->i2c_state == 0)
	dev->i2c_state = 1;

    /* The AS99127F hardware monitor uses its primary LM75 device's
       address to access some of its proprietary registers. Pass this
       operation on to the main monitor code, if necessary. */
    if ((dev->addr_register & 0x80) && dev->as99127f) {
	ret = lm78_as99127f_read(dev->as99127f, dev->addr_register);
    } else {
	switch (dev->addr_register & 0x3) {
		case 0x0: /* temperature */
			ret = lm75_read(dev, (dev->i2c_state == 1) ? 0x0 : 0x1);
			break;

		case 0x1: /* configuration */
			ret = lm75_read(dev, 0x2);
			break;

		case 0x2: /* Thyst */
			ret = lm75_read(dev, (dev->i2c_state == 1) ? 0x3 : 0x4);
			break;
		case 0x3: /* Tos */
			ret = lm75_read(dev, (dev->i2c_state == 1) ? 0x5 : 0x6);
			break;
	}
    }

    if (dev->i2c_state < 2)
	dev->i2c_state++;

    return ret;
}


uint8_t
lm75_write(lm75_t *dev, uint8_t reg, uint8_t val)
{
    lm75_log("LM75: write(%02X, %02X)\n", reg, val);

    uint8_t reg_idx = (reg & 0x7);

    if ((reg_idx <= 0x1) || (reg_idx == 0x7))
	return 0; /* read-only registers */

    dev->regs[reg_idx] = val;

    return 1;
}


static uint8_t
lm75_i2c_write(void *bus, uint8_t addr, uint8_t data, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;

    if ((dev->i2c_state > 2) || ((dev->i2c_state == 2) && ((dev->addr_register & 0x3) == 0x1))) {
	return 0;
    } else if (dev->i2c_state == 0) {
	dev->i2c_state = 1;
	/* Linux lm75.c driver relies on the address register not changing if bit 2 is set. */
	if (((dev->addr_register & 0x80) && dev->as99127f) || !(data & 0x04))
		dev->addr_register = data;
	return 1;
    }

    /* The AS99127F hardware monitor uses its primary LM75 device's
       address to access some of its proprietary registers. Pass this
       operation on to the main monitor code, if necessary. */
    if ((dev->addr_register & 0x80) && dev->as99127f) {
	return lm78_as99127f_write(dev->as99127f, dev->addr_register, data);
    } else {
	switch (dev->addr_register & 0x3) {
		case 0x0: /* temperature */
			lm75_write(dev, (dev->i2c_state == 1) ? 0x0 : 0x1, data);
			break;

		case 0x1: /* configuration */
			lm75_write(dev, 0x2, data);
			break;

		case 0x2: /* Thyst */
			lm75_write(dev, (dev->i2c_state == 1) ? 0x3 : 0x4, data);
			break;

		case 0x3: /* Tos */
			lm75_write(dev, (dev->i2c_state == 1) ? 0x5 : 0x6, data);
			break;
	}
    }

    if (dev->i2c_state == 1)
	dev->i2c_state = 2;

    return 1;
}


void
lm75_remap(lm75_t *dev, uint8_t addr)
{
    lm75_log("LM75: remapping to SMBus %02Xh\n", addr);

    if (dev->i2c_enabled)
	i2c_removehandler(i2c_smbus, dev->i2c_addr, 1, lm75_i2c_start, lm75_i2c_read, lm75_i2c_write, NULL, dev);

    if (addr < 0x80)
	i2c_sethandler(i2c_smbus, addr, 1, lm75_i2c_start, lm75_i2c_read, lm75_i2c_write, NULL, dev);

    dev->i2c_addr = addr & 0x7f;
    dev->i2c_enabled = !(addr & 0x80);
}


static void
lm75_reset(lm75_t *dev)
{
    dev->regs[0x3] = 0x4b;
    dev->regs[0x5] = 0x50;

    lm75_remap(dev, dev->i2c_addr | (dev->i2c_enabled ? 0x00 : 0x80));
}


static void
lm75_close(void *priv)
{
    lm75_t *dev = (lm75_t *) priv;

    lm75_remap(dev, 0);

    free(dev);
}


static void *
lm75_init(const device_t *info)
{
    lm75_t *dev = (lm75_t *) malloc(sizeof(lm75_t));
    memset(dev, 0, sizeof(lm75_t));

    dev->local = info->local;

    /* Set default value. */
    if (dev->local)
	hwm_values.temperatures[dev->local >> 8] = 30;
    dev->values = &hwm_values;

    dev->i2c_addr = dev->local & 0x7f;
    dev->i2c_enabled = 1;

    lm75_reset(dev);

    return dev;
}


/* LM75 on SMBus address 4Ah, reporting temperatures[1]. */
const device_t lm75_1_4a_device = {
    .name = "National Semiconductor LM75 Temperature Sensor",
    .internal_name = "lm75_1_4a",
    .flags = DEVICE_ISA,
    .local = 0x14a,
    .init = lm75_init,
    .close = lm75_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};


/* LM75 secondary/tertiary temperature sensors built into
   the Winbond W83781D family. Not to be used stand-alone. */
const device_t lm75_w83781d_device = {
    .name = "Winbond W83781D Secondary Temperature Sensor",
    .internal_name = "lm75_w83781d",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = lm75_init,
    .close = lm75_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

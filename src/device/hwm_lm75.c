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


static void	lm75_i2c_start(void *bus, uint8_t addr, void *priv);
static uint8_t	lm75_i2c_read(void *bus, uint8_t addr, void *priv);
static uint8_t	lm75_i2c_write(void *bus, uint8_t addr, uint8_t data, void *priv);
static void	lm75_reset(lm75_t *dev);


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


void
lm75_remap(lm75_t *dev, uint8_t addr)
{
    lm75_log("LM75: remapping to I2C %02Xh\n", addr);

    if (dev->i2c_addr < 0x80)
	i2c_removehandler(i2c_smbus, dev->i2c_addr, 1, lm75_i2c_start, lm75_i2c_read, lm75_i2c_write, NULL, dev);

    if (addr < 0x80)
	i2c_sethandler(i2c_smbus, addr, 1, lm75_i2c_start, lm75_i2c_read, lm75_i2c_write, NULL, dev);

    dev->i2c_addr = addr;
}


static void
lm75_i2c_start(void *bus, uint8_t addr, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;

    dev->i2c_state = 0;
}


static uint8_t
lm75_i2c_read(void *bus, uint8_t addr, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;
    uint8_t ret = 0;

    switch (dev->addr_register & 0x3) {
	case 0x0: /* temperature */
		ret = lm75_read(dev, (dev->i2c_state == 1) ? 0x1 : 0x0);
		break;

	case 0x1: /* configuration */
		ret = lm75_read(dev, 0x2);
		break;

	case 0x2: /* Thyst */
		ret = lm75_read(dev, (dev->i2c_state == 1) ? 0x4 : 0x3);
		break;
	case 0x3: /* Tos */
		ret = lm75_read(dev, (dev->i2c_state == 1) ? 0x6 : 0x5);
		break;
    }

    return ret;
}


uint8_t
lm75_read(lm75_t *dev, uint8_t reg)
{
    uint8_t ret;

    /* The AS99127F hardware monitor uses the addresses of its LM75 devices
       to access some of its proprietary registers. Pass this operation on to
       the main monitor address through an internal I2C call, if necessary. */
    if ((reg > 0x7) && ((reg & 0xf8) != 0x50) && (dev->as99127f_i2c_addr < 0x80)) {
	i2c_start(i2c_smbus, dev->as99127f_i2c_addr);
	i2c_write(i2c_smbus, dev->as99127f_i2c_addr, reg);
	ret = i2c_read(i2c_smbus, dev->as99127f_i2c_addr);
	i2c_stop(i2c_smbus, dev->as99127f_i2c_addr);
    } else if ((reg & 0x7) == 0x0) /* temperature high byte */
	ret = LM75_TEMP_TO_REG(dev->values->temperatures[dev->local >> 8]) >> 8;
    else if ((reg & 0x7) == 0x1) /* temperature low byte */
	ret = LM75_TEMP_TO_REG(dev->values->temperatures[dev->local >> 8]);
    else
	ret = dev->regs[reg & 0x7];

    lm75_log("LM75: read(%02X) = %02X\n", reg, ret);

    return ret;
}


static uint8_t
lm75_i2c_write(void *bus, uint8_t addr, uint8_t data, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;

    if ((dev->i2c_state > 2) || ((dev->i2c_state == 2) && ((dev->addr_register & 0x3) == 0x1))) {
	return 0;
    } else if (dev->i2c_state == 0) {
	dev->addr_register = data;
	return 1;
    }

    switch (dev->addr_register & 0x3) {
	case 0x0: /* temperature */
		lm75_write(dev, (dev->i2c_state == 1) ? 0x1 : 0x0, data);
		break;

	case 0x1: /* configuration */
		lm75_write(dev, 0x2, data);
		break;

	case 0x2: /* Thyst */
		lm75_write(dev, (dev->i2c_state == 1) ? 0x4 : 0x3, data);
		break;
	case 0x3: /* Tos */
		lm75_write(dev, (dev->i2c_state == 1) ? 0x6 : 0x5, data);
		break;
    }

    return 1;
}


uint8_t
lm75_write(lm75_t *dev, uint8_t reg, uint8_t val)
{
    lm75_log("LM75: write(%02X, %02X)\n", reg, val);

    /* The AS99127F hardware monitor uses the addresses of its LM75 devices
       to access some of its proprietary registers. Pass this operation on to
       the main monitor address through an internal I2C call, if necessary. */
    if ((reg > 0x7) && ((reg & 0xf8) != 0x50) && (dev->as99127f_i2c_addr < 0x80)) {
	i2c_start(i2c_smbus, dev->as99127f_i2c_addr);
	i2c_write(i2c_smbus, dev->as99127f_i2c_addr, reg);
	i2c_write(i2c_smbus, dev->as99127f_i2c_addr, val);
	i2c_stop(i2c_smbus, dev->as99127f_i2c_addr);
	return 1;
    }

    uint8_t reg_idx = (reg & 0x7);

    if ((reg_idx <= 0x1) || (reg_idx == 0x7))
	return 0; /* read-only registers */

    dev->regs[reg_idx] = val;

    return 1;
}


static void
lm75_reset(lm75_t *dev)
{
    dev->regs[0x3] = 0x4b;
    dev->regs[0x5] = 0x50;

    lm75_remap(dev, dev->local & 0x7f);
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

    dev->as99127f_i2c_addr = 0x80;

    lm75_reset(dev);

    return dev;
}


/* LM75 on SMBus address 4Ah, reporting temperatures[1]. */
const device_t lm75_1_4a_device = {
    "National Semiconductor LM75 Temperature Sensor",
    DEVICE_ISA,
    0x14a,
    lm75_init, lm75_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


/* LM75 secondary/tertiary temperature sensors built into
   the Winbond W83781D family. Not to be used stand-alone. */
const device_t lm75_w83781d_device = {
    "Winbond W83781D Secondary Temperature Sensor",
    DEVICE_ISA,
    0,
    lm75_init, lm75_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};

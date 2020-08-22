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
#include <86box/smbus.h>
#include <86box/hwm.h>


#define LM75_TEMP_TO_REG(t)	((t) << 8)


static uint8_t	lm75_smbus_read_byte(uint8_t addr, void *priv);
static uint8_t	lm75_smbus_read_byte_cmd(uint8_t addr, uint8_t cmd, void *priv);
static uint16_t	lm75_smbus_read_word_cmd(uint8_t addr, uint8_t cmd, void *priv);
static void	lm75_smbus_write_byte(uint8_t addr, uint8_t val, void *priv);
static void	lm75_smbus_write_byte_cmd(uint8_t addr, uint8_t cmd, uint8_t val, void *priv);
static void	lm75_smbus_write_word_cmd(uint8_t addr, uint8_t cmd, uint16_t val, void *priv);
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
    lm75_log("LM75: remapping to SMBus %02Xh\n", addr);

    if (dev->smbus_addr < 0x80) smbus_removehandler(dev->smbus_addr, 1,
    			lm75_smbus_read_byte, lm75_smbus_read_byte_cmd, lm75_smbus_read_word_cmd, NULL,
    			lm75_smbus_write_byte, lm75_smbus_write_byte_cmd, lm75_smbus_write_word_cmd, NULL,
    			dev);

    if (addr < 0x80) smbus_sethandler(addr, 1,
    			lm75_smbus_read_byte, lm75_smbus_read_byte_cmd, lm75_smbus_read_word_cmd, NULL,
    			lm75_smbus_write_byte, lm75_smbus_write_byte_cmd, lm75_smbus_write_word_cmd, NULL,
    			dev);

    dev->smbus_addr = addr;
}


static uint8_t
lm75_smbus_read_byte(uint8_t addr, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;
    return lm75_read(dev, dev->addr_register);
}


static uint8_t
lm75_smbus_read_byte_cmd(uint8_t addr, uint8_t cmd, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;
    return lm75_read(dev, cmd);
}

static uint16_t
lm75_smbus_read_word_cmd(uint8_t addr, uint8_t cmd, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;
    uint8_t rethi = 0;
    uint8_t retlo = 0;

    switch (cmd & 0x3) {
    	case 0x0: /* temperature */
    		rethi = lm75_read(dev, 0x0);
    		retlo = lm75_read(dev, 0x1);
    		break;
    	case 0x1: /* configuration */
    		rethi = retlo = lm75_read(dev, 0x2);
    		break;
    	case 0x2: /* Thyst */
    		rethi = lm75_read(dev, 0x3);
    		retlo = lm75_read(dev, 0x4);
    		break;
    	case 0x3: /* Tos */
    		rethi = lm75_read(dev, 0x5);
    		retlo = lm75_read(dev, 0x6);
    		break;
    }

    return (retlo << 8) | rethi; /* byte-swapped for some reason */
}


uint8_t
lm75_read(lm75_t *dev, uint8_t reg)
{
    uint8_t ret;

    /* The AS99127F hardware monitor uses the addresses of its LM75 devices
       to access some of its proprietary registers. Pass this operation on to
       the main monitor address through an internal SMBus call, if necessary. */
    if ((reg > 0x7) && ((reg & 0xf8) != 0x50) && (dev->as99127f_smbus_addr < 0x80))
    	ret = smbus_read_byte_cmd(dev->as99127f_smbus_addr, reg);
    else
    	ret = dev->regs[reg & 0x7];

    lm75_log("LM75: read(%02X) = %02X\n", reg, ret);

    return ret;
}


static void
lm75_smbus_write_byte(uint8_t addr, uint8_t val, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;
    dev->addr_register = val;
}


static void
lm75_smbus_write_byte_cmd(uint8_t addr, uint8_t cmd, uint8_t val, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;
    lm75_write(dev, cmd, val);
}


static void
lm75_smbus_write_word_cmd(uint8_t addr, uint8_t cmd, uint16_t val, void *priv)
{
    lm75_t *dev = (lm75_t *) priv;
    uint8_t valhi = (val >> 8);
    uint8_t vallo = (val & 0xff);

    switch (cmd & 0x3) {
    	case 0x0: /* temperature */
    		lm75_write(dev, 0x0, valhi);
    		lm75_write(dev, 0x1, vallo);
    		break;
    	case 0x1: /* configuration */
    		lm75_write(dev, 0x2, vallo);
    		break;
    	case 0x2: /* Thyst */
    		lm75_write(dev, 0x3, valhi);
    		lm75_write(dev, 0x4, vallo);
    		break;
    	case 0x3: /* Tos */
    		lm75_write(dev, 0x5, valhi);
    		lm75_write(dev, 0x6, vallo);
    		break;
    	break;
    }
}


uint8_t
lm75_write(lm75_t *dev, uint8_t reg, uint8_t val)
{
    lm75_log("LM75: write(%02X, %02X)\n", reg, val);

    /* The AS99127F hardware monitor uses the addresses of its LM75 devices
       to access some of its proprietary registers. Pass this operation on to
       the main monitor address through an internal SMBus call, if necessary. */
    if ((reg > 0x7) && ((reg & 0xf8) != 0x50) && (dev->as99127f_smbus_addr < 0x80)) {
    	smbus_write_byte_cmd(dev->as99127f_smbus_addr, reg, val);
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
    uint16_t temp = LM75_TEMP_TO_REG(dev->values->temperatures[dev->local >> 8]);
    dev->regs[0x0] = (temp >> 8);
    dev->regs[0x1] = temp;
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
    dev->values = hwm_get_values();

    dev->as99127f_smbus_addr = 0x80;

    lm75_reset(dev);

    return dev;
}


/* LM75 on SMBus address 4Ah, reporting temperatures[1]. */
const device_t lm75_1_4a_device = {
    "National Semiconductor LM75 Temperature Sensor",
    DEVICE_AT,
    0x14a,
    lm75_init, lm75_close, NULL,
    NULL, NULL, NULL,
    NULL
};


/* LM75 secondary/tertiary temperature sensors built into
   the Winbond W83781D family. Not to be used stand-alone. */
const device_t lm75_w83781d_device = {
    "Winbond W83781D Secondary Temperature Sensor",
    DEVICE_AT,
    0,
    lm75_init, lm75_close, NULL,
    NULL, NULL, NULL,
    NULL
};

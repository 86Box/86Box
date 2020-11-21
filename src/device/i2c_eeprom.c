/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the 24Cxx series of I2C EEPROMs.
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
#include <86box/i2c.h>


typedef struct {
    void	*i2c;
    uint8_t	addr, *data, writable;

    uint16_t	addr_mask, addr_register;
    uint8_t	i2c_state;
} i2c_eeprom_t;

#define ENABLE_I2C_EEPROM_LOG 1
#ifdef ENABLE_I2C_EEPROM_LOG
int i2c_eeprom_do_log = ENABLE_I2C_EEPROM_LOG;


static void
i2c_eeprom_log(const char *fmt, ...)
{
    va_list ap;

    if (i2c_eeprom_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define i2c_eeprom_log(fmt, ...)
#endif


void
i2c_eeprom_start(void *bus, uint8_t addr, void *priv)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) priv;

    i2c_eeprom_log("I2C EEPROM: start()\n");

    dev->i2c_state = 0;
    dev->addr_register = (addr << 8) & dev->addr_mask;
}


uint8_t
i2c_eeprom_read(void *bus, uint8_t addr, void *priv)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) priv;
    uint8_t ret = dev->data[dev->addr_register];

    i2c_eeprom_log("I2C EEPROM: read(%04X) = %02X\n", dev->addr_register, ret);
    if (++dev->addr_register > dev->addr_mask) /* roll-over */
	dev->addr_register = 0;

    return ret;
}


uint8_t
i2c_eeprom_write(void *bus, uint8_t addr, uint8_t data, void *priv)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) priv;

    if (dev->i2c_state == 0) {
	dev->i2c_state = 1;
	dev->addr_register = ((addr << 8) | data) & dev->addr_mask;
	i2c_eeprom_log("I2C EEPROM: write(address, %04X)\n", dev->addr_register);
    } else {
    	i2c_eeprom_log("I2C EEPROM: write(%04X, %02X) = %s\n", dev->addr_register, data, dev->writable ? "accepted" : "blocked");
    	if (dev->writable)
		dev->data[dev->addr_register] = data;
	if (++dev->addr_register > dev->addr_mask) /* roll-over */
		dev->addr_register = 0;
	return dev->writable;
    }

    return 1;
}


void *
i2c_eeprom_init(void *i2c, uint8_t addr, uint8_t *data, uint16_t size, uint8_t writable)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) malloc(sizeof(i2c_eeprom_t));
    memset(dev, 0, sizeof(i2c_eeprom_t));

    i2c_eeprom_log("I2C EEPROM: init(%02X, %d, %d)\n", addr, size, writable);

    dev->i2c = i2c;
    dev->addr = addr;
    dev->data = data;
    dev->writable = writable;

    dev->addr_mask = size - 1;

    i2c_sethandler(i2c, dev->addr & ~(dev->addr_mask >> 8), (dev->addr_mask >> 8) + 1, i2c_eeprom_start, i2c_eeprom_read, i2c_eeprom_write, NULL, dev);

    return dev;
}


void
i2c_eeprom_close(void *dev_handle)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) dev_handle;

    i2c_eeprom_log("I2C EEPROM: close()\n");

    i2c_removehandler(dev->i2c, dev->addr & ~(dev->addr_mask >> 8), (dev->addr_mask >> 8) + 1, i2c_eeprom_start, i2c_eeprom_read, i2c_eeprom_write, NULL, dev);

    free(dev);
}

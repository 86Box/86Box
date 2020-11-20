/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the AT24Cxx series of I2C EEPROMs.
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
    uint8_t	addr;
    uint8_t	*data;
    uint8_t	writable;

    uint16_t	addr_mask;
    uint8_t	addr_register;
    uint8_t	i2c_state;
} i2c_eeprom_t;


void
i2c_eeprom_start(void *bus, uint8_t addr, void *priv)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) priv;

    dev->i2c_state = 0;
}


uint8_t
i2c_eeprom_read(void *bus, uint8_t addr, void *priv)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) priv;

    return dev->data[((addr << 8) | dev->addr_register++) & dev->addr_mask];
}


uint8_t
i2c_eeprom_write(void *bus, uint8_t addr, uint8_t data, void *priv)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) priv;

    if (dev->i2c_state == 0) {
	dev->i2c_state = 1;
	dev->addr_register = data;
    } else if (dev->writable)
	dev->data[((addr << 8) | dev->addr_register++) & dev->addr_mask] = data;
    else
	return 0;

    return 1;
}


void *
i2c_eeprom_init(void *i2c, uint8_t addr, uint8_t *data, uint16_t size, uint8_t writable)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) malloc(sizeof(i2c_eeprom_t));
    memset(dev, 0, sizeof(i2c_eeprom_t));

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

    i2c_removehandler(dev->i2c, dev->addr & ~(dev->addr_mask >> 8), (dev->addr_mask >> 8) + 1, i2c_eeprom_start, i2c_eeprom_read, i2c_eeprom_write, NULL, dev);

    free(dev);
}

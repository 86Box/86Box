/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the 24Cxx series of I2C EEPROMs.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2020 RichardG.
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
#include <86box/plat_unused.h>

typedef struct i2c_eeprom_t {
    void   *i2c;
    uint8_t addr;
    uint8_t *data;
    uint8_t writable;

    uint32_t addr_mask;
    uint32_t addr_register;
    uint8_t  addr_len;
    uint8_t  addr_pos;
} i2c_eeprom_t;

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
#    define i2c_eeprom_log(fmt, ...)
#endif

static uint8_t
i2c_eeprom_start(UNUSED(void *bus), uint8_t addr, uint8_t read, void *priv)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) priv;

    i2c_eeprom_log("I2C EEPROM %s %02X: start(%c)\n", i2c_getbusname(dev->i2c), dev->addr, read ? 'R' : 'W');

    if (!read) {
        dev->addr_pos      = 0;
        dev->addr_register = (addr << dev->addr_len) & dev->addr_mask;
    }

    return 1;
}

static uint8_t
i2c_eeprom_read(UNUSED(void *bus), UNUSED(uint8_t addr), void *priv)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) priv;
    uint8_t       ret = dev->data[dev->addr_register];

    i2c_eeprom_log("I2C EEPROM %s %02X: read(%06X) = %02X\n", i2c_getbusname(dev->i2c), dev->addr, dev->addr_register, ret);
    dev->addr_register = (dev->addr_register + 1) & dev->addr_mask; /* roll-over */

    return ret;
}

static uint8_t
i2c_eeprom_write(UNUSED(void *bus), uint8_t addr, uint8_t data, void *priv)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) priv;

    if (dev->addr_pos < dev->addr_len) {
        dev->addr_register <<= 8;
        dev->addr_register |= data;
        dev->addr_register &= (1 << dev->addr_len) - 1;
        dev->addr_register |= addr << dev->addr_len;
        dev->addr_register &= dev->addr_mask;
        i2c_eeprom_log("I2C EEPROM %s %02X: write(address, %06X)\n", i2c_getbusname(dev->i2c), dev->addr, dev->addr_register);
        dev->addr_pos += 8;
    } else {
        i2c_eeprom_log("I2C EEPROM %s %02X: write(%06X, %02X) = %d\n", i2c_getbusname(dev->i2c), dev->addr, dev->addr_register, data, !!dev->writable);
        if (dev->writable)
            dev->data[dev->addr_register] = data;
        dev->addr_register = (dev->addr_register + 1) & dev->addr_mask; /* roll-over */
        return dev->writable;
    }

    return 1;
}

static void
i2c_eeprom_stop(UNUSED(void *bus), UNUSED(uint8_t addr), void *priv)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) priv;

    i2c_eeprom_log("I2C EEPROM %s %02X: stop()\n", i2c_getbusname(dev->i2c), dev->addr);

    dev->addr_pos = 0;
}

uint8_t
log2i(uint32_t i)
{
    uint8_t ret = 0;
    while (i >>= 1)
        ret++;
    return ret;
}

void *
i2c_eeprom_init(void *i2c, uint8_t addr, uint8_t *data, uint32_t size, uint8_t writable)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) calloc(1, sizeof(i2c_eeprom_t));

    /* Round size up to the next power of 2. */
    uint32_t pow_size = 1 << log2i(size);
    if (pow_size < size)
        size = pow_size << 1;
    if (size >= 8388608)
        size = 8388608; /* address space limit of 8 MB = 7 bits from I2C address + 16 bits from command address */

    i2c_eeprom_log("I2C EEPROM %s %02X: init(%d, %d)\n", i2c_getbusname(i2c), addr, size, writable);

    dev->i2c      = i2c;
    dev->addr     = addr;
    dev->data     = data;
    dev->writable = writable;

    dev->addr_len  = (size >= 4096) ? 16 : 8; /* use 16-bit addresses on 24C32 and above */
    dev->addr_mask = size - 1;

    uint8_t i2c_mask = dev->addr_mask >> dev->addr_len;
    i2c_sethandler(dev->i2c, dev->addr & ~i2c_mask, i2c_mask + 1, i2c_eeprom_start, i2c_eeprom_read, i2c_eeprom_write, i2c_eeprom_stop, dev);

    return dev;
}

void
i2c_eeprom_close(void *dev_handle)
{
    i2c_eeprom_t *dev = (i2c_eeprom_t *) dev_handle;

    i2c_eeprom_log("I2C EEPROM %s %02X: close()\n", i2c_getbusname(dev->i2c), dev->addr);

    uint8_t i2c_mask = dev->addr_mask >> dev->addr_len;
    i2c_removehandler(dev->i2c, dev->addr & ~i2c_mask, i2c_mask + 1, i2c_eeprom_start, i2c_eeprom_read, i2c_eeprom_write, i2c_eeprom_stop, dev);

    free(dev);
}

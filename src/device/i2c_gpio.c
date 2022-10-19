/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of a GPIO-based I2C host controller.
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
    char   *bus_name;
    void   *i2c;
    uint8_t prev_scl, prev_sda, slave_sda, started,
        slave_addr_received, slave_addr, slave_read, pos, byte;
} i2c_gpio_t;

#ifdef ENABLE_I2C_GPIO_LOG
int i2c_gpio_do_log = ENABLE_I2C_GPIO_LOG;

static void
i2c_gpio_log(int level, const char *fmt, ...)
{
    va_list ap;

    if (i2c_gpio_do_log >= level) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define i2c_gpio_log(fmt, ...)
#endif

void *
i2c_gpio_init(char *bus_name)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) malloc(sizeof(i2c_gpio_t));
    memset(dev, 0, sizeof(i2c_gpio_t));

    i2c_gpio_log(1, "I2C GPIO %s: init()\n", bus_name);

    dev->bus_name = bus_name;
    dev->i2c      = i2c_addbus(dev->bus_name);
    dev->prev_scl = dev->prev_sda = dev->slave_sda = 1;
    dev->slave_addr                                = 0xff;

    return dev;
}

void
i2c_gpio_close(void *dev_handle)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;

    i2c_gpio_log(1, "I2C GPIO %s: close()\n", dev->bus_name);

    i2c_removebus(dev->i2c);

    free(dev);
}

void
i2c_gpio_set(void *dev_handle, uint8_t scl, uint8_t sda)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;

    i2c_gpio_log(3, "I2C GPIO %s: write scl=%d->%d sda=%d->%d read=%d\n", dev->bus_name, dev->prev_scl, scl, dev->prev_sda, sda, dev->slave_read);

    if (dev->prev_scl && scl) {
        if (dev->prev_sda && !sda) {
            i2c_gpio_log(2, "I2C GPIO %s: Start condition\n", dev->bus_name);
            dev->started    = 1;
            dev->pos        = 0;
            dev->slave_addr = 0xff;
            dev->slave_read = 2; /* start with address transfer */
            dev->slave_sda  = 1;
        } else if (!dev->prev_sda && sda) {
            i2c_gpio_log(2, "I2C GPIO %s: Stop condition\n", dev->bus_name);
            dev->started = 0;
            if (dev->slave_addr != 0xff)
                i2c_stop(dev->i2c, dev->slave_addr);
            dev->slave_addr = 0xff;
            dev->slave_sda  = 1;
        }
    } else if (!dev->prev_scl && scl && dev->started) {
        if (dev->pos++ < 8) {
            if (dev->slave_read == 1) {
                dev->slave_sda = !!(dev->byte & 0x80);
                dev->byte <<= 1;
            } else {
                dev->byte <<= 1;
                dev->byte |= sda;
            }

            i2c_gpio_log(2, "I2C GPIO %s: Bit %d = %d\n", dev->bus_name, 8 - dev->pos, (dev->slave_read == 1) ? dev->slave_sda : sda);
        }

        if (dev->pos == 8) {
            i2c_gpio_log(2, "I2C GPIO %s: Byte = %02X\n", dev->bus_name, dev->byte);

            /* (N)ACKing here instead of at the 9th bit may sound odd, but is required by the Matrox Mystique Windows drivers. */
            switch (dev->slave_read) {
                case 2: /* address transfer */
                    dev->slave_addr = dev->byte >> 1;
                    dev->slave_read = dev->byte & 1;

                    /* slave ACKs? */
                    dev->slave_sda = !i2c_start(dev->i2c, dev->slave_addr, dev->slave_read);
                    i2c_gpio_log(2, "I2C GPIO %s: Slave %02X %s %sACK\n", dev->bus_name, dev->slave_addr, dev->slave_read ? "read" : "write", dev->slave_sda ? "N" : "");

                    if (!dev->slave_sda && dev->slave_read) /* read first byte on an ACKed read transfer */
                        dev->byte = i2c_read(dev->i2c, dev->slave_addr);

                    dev->slave_read |= 0x80; /* slave_read was overwritten; stop the master ACK read logic from running at the 9th bit if we're reading */
                    break;

                case 0: /* write transfer */
                    dev->slave_sda = !i2c_write(dev->i2c, dev->slave_addr, dev->byte);
                    i2c_gpio_log(2, "I2C GPIO %s: Write %02X %sACK\n", dev->bus_name, dev->byte, dev->slave_sda ? "N" : "");
                    break;
            }
        } else if (dev->pos == 9) {
            switch (dev->slave_read) {
                case 1:       /* read transfer (unless we're in an address transfer) */
                    if (!sda) /* master ACKs? */
                        dev->byte = i2c_read(dev->i2c, dev->slave_addr);
                    i2c_gpio_log(2, "I2C GPIO %s: Read %02X %sACK\n", dev->bus_name, dev->byte, sda ? "N" : "");
                    break;

                default:
                    dev->slave_read &= 1; /* if we're in an address transfer, clear it */
            }
            dev->pos = 0; /* start over */
        }
    } else if (dev->prev_scl && !scl && (dev->pos != 8)) { /* keep (N)ACK computed at the 8th bit when transitioning to the 9th bit */
        dev->slave_sda = 1;
    }

    dev->prev_scl = scl;
    dev->prev_sda = sda;
}

uint8_t
i2c_gpio_get_scl(void *dev_handle)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;
    return dev->prev_scl;
}

uint8_t
i2c_gpio_get_sda(void *dev_handle)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;
    i2c_gpio_log(3, "I2C GPIO %s: read myscl=%d mysda=%d slavesda=%d\n", dev->bus_name, dev->prev_scl, dev->prev_sda, dev->slave_sda);
    return dev->prev_sda && dev->slave_sda;
}

void *
i2c_gpio_get_bus(void *dev_handle)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;
    return dev->i2c;
}

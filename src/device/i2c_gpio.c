/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of a GPIO-based I2C device.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
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


enum {
    TRANSMITTER_SLAVE = 1,
    TRANSMITTER_MASTER = 2
};

enum {
    I2C_IDLE = 0,
    I2C_RECEIVE,
    I2C_RECEIVE_WAIT,
    I2C_TRANSMIT_START,
    I2C_TRANSMIT,
    I2C_ACKNOWLEDGE,
    I2C_NOTACKNOWLEDGE,
    I2C_TRANSACKNOWLEDGE,
    I2C_TRANSMIT_WAIT
};

enum {
    SLAVE_IDLE = 0,
    SLAVE_RECEIVEADDR,
    SLAVE_RECEIVEDATA,
    SLAVE_SENDDATA,
    SLAVE_INVALID
};

typedef struct {
    char	*bus_name;
    void	*i2c;
    uint8_t	scl, sda, receive_wait_sda, state, slave_state, slave_addr,
		slave_read, last_sda, pos, transmit, byte;
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
#define i2c_gpio_log(fmt, ...)
#endif


void *
i2c_gpio_init(char *bus_name)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) malloc(sizeof(i2c_gpio_t));
    memset(dev, 0, sizeof(i2c_gpio_t));

    i2c_gpio_log(1, "I2C GPIO %s: init()\n", bus_name);

    dev->bus_name = bus_name;
    dev->i2c = i2c_addbus(dev->bus_name);
    dev->scl = dev->sda = 1;
    dev->slave_addr = 0xff;

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
i2c_gpio_next_byte(i2c_gpio_t *dev)
{
    dev->byte = i2c_read(dev->i2c, dev->slave_addr);
    i2c_gpio_log(1, "I2C GPIO %s: Transmitting data %02X\n", dev->bus_name, dev->byte);
}


uint8_t
i2c_gpio_write(i2c_gpio_t *dev)
{
    uint8_t i;

    switch (dev->slave_state) {
	case SLAVE_IDLE:
		i = dev->slave_addr;
		dev->slave_addr = dev->byte >> 1;
		dev->slave_read = dev->byte & 1;

		i2c_gpio_log(1, "I2C GPIO %s: Initiating %s address %02X\n", dev->bus_name, dev->slave_read ? "read from" : "write to", dev->slave_addr);

		if (!i2c_has_device(dev->i2c, dev->slave_addr) ||
		    ((i == 0xff) && !i2c_start(dev->i2c, dev->slave_addr, dev->slave_read))) { /* start only once per transfer */
			dev->slave_state = SLAVE_INVALID;
			dev->slave_addr = 0xff;
			return I2C_NOTACKNOWLEDGE;
		}

		if (dev->slave_read) {
			dev->slave_state = SLAVE_SENDDATA;
			dev->transmit = TRANSMITTER_SLAVE;
			dev->byte = i2c_read(dev->i2c, dev->slave_addr);
		} else {
			dev->slave_state = SLAVE_RECEIVEADDR;
			dev->transmit = TRANSMITTER_MASTER;
		}
		break;

	case SLAVE_RECEIVEADDR:
		i2c_gpio_log(1, "I2C GPIO %s: Receiving address %02X\n", dev->bus_name, dev->byte);
		dev->slave_state = dev->slave_read ? SLAVE_SENDDATA : SLAVE_RECEIVEDATA;
		if (!i2c_write(dev->i2c, dev->slave_addr, dev->byte))
			return I2C_NOTACKNOWLEDGE;
		break;

	case SLAVE_RECEIVEDATA:
		i2c_gpio_log(1, "I2C GPIO %s: Receiving data %02X\n", dev->bus_name, dev->byte);
		if (!i2c_write(dev->i2c, dev->slave_addr, dev->byte))
			return I2C_NOTACKNOWLEDGE;
		break;

	case SLAVE_INVALID:
		return I2C_NOTACKNOWLEDGE;
    }

    return I2C_ACKNOWLEDGE;
}


void
i2c_gpio_stop(i2c_gpio_t *dev)
{
    i2c_gpio_log(1, "I2C GPIO %s: Stopping transfer\n", dev->bus_name);

    if (dev->slave_addr != 0xff) /* don't stop if no transfer was in progress */
	i2c_stop(dev->i2c, dev->slave_addr);

    dev->slave_addr = 0xff;
    dev->slave_state = SLAVE_IDLE;
    dev->transmit = TRANSMITTER_MASTER;
}


void
i2c_gpio_set(void *dev_handle, uint8_t scl, uint8_t sda)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;

    i2c_gpio_log(3, "I2C GPIO %s: scl=%d->%d sda=%d->%d last_valid_sda=%d state=%d\n", dev->bus_name, dev->scl, scl, dev->last_sda, sda, dev->sda, dev->state);

    switch (dev->state) {
	case I2C_IDLE:
		if (scl && dev->last_sda && !sda) { /* start condition; dev->scl check breaks NCR SDMS */
			i2c_gpio_log(2, "I2C GPIO %s: Start condition received (from IDLE)\n", dev->bus_name);
			dev->state = I2C_RECEIVE;
			dev->pos = 0;
		}
		break;

	case I2C_RECEIVE_WAIT:
		if (!dev->scl && scl)
			dev->state = I2C_RECEIVE;
		else if (!dev->scl && !scl && dev->last_sda && sda) /* workaround for repeated start condition on Windows XP DDC */
			dev->receive_wait_sda = 1;
		/* fall-through */

	case I2C_RECEIVE:
		if (!dev->scl && scl) {
			dev->byte <<= 1;
			if (sda)
				dev->byte |= 1;
			else
				dev->byte &= 0xfe;
			if (++dev->pos == 8)
				dev->state = i2c_gpio_write(dev);
		} else if (dev->scl && scl) {
			if (sda && !dev->last_sda) { /* stop condition */
				i2c_gpio_log(2, "I2C GPIO %s: Stop condition received (from RECEIVE)\n", dev->bus_name);
				dev->state = I2C_IDLE;
				i2c_gpio_stop(dev);
			} else if (!sda && dev->last_sda) { /* start condition */
				i2c_gpio_log(2, "I2C GPIO %s: Start condition received (from RECEIVE)\n", dev->bus_name);
				dev->pos = 0;
				dev->slave_state = SLAVE_IDLE;
			}
		}
		break;

	case I2C_ACKNOWLEDGE:
		if (!dev->scl && scl) {
			i2c_gpio_log(2, "I2C GPIO %s: Acknowledging transfer to %02X\n", dev->bus_name, dev->slave_addr);
			sda = 0;
			dev->receive_wait_sda = 0; /* ack */
			dev->pos = 0;
			dev->state = (dev->transmit == TRANSMITTER_MASTER) ? I2C_RECEIVE_WAIT : I2C_TRANSMIT;
		}
		break;

	case I2C_NOTACKNOWLEDGE:
		if (!dev->scl && scl) {
			i2c_gpio_log(2, "I2C GPIO %s: Not acknowledging transfer\n", dev->bus_name);
			sda = 1;
			dev->pos = 0;
			dev->state = I2C_IDLE;
			dev->slave_state = SLAVE_IDLE;
		}
		break;

	case I2C_TRANSACKNOWLEDGE:
		if (!dev->scl && scl) {
			if (sda) { /* not acknowledged; must be end of transfer */
				i2c_gpio_log(2, "I2C GPIO %s: End of transfer\n", dev->bus_name);
				dev->state = I2C_IDLE;
				i2c_gpio_stop(dev);
			} else { /* next byte to transfer */
				dev->state = I2C_TRANSMIT_START;
				i2c_gpio_next_byte(dev);
				dev->pos = 0;
				i2c_gpio_log(2, "I2C GPIO %s: Next byte = %02X\n", dev->bus_name, dev->byte);
			}
		}
		break;

	case I2C_TRANSMIT_WAIT:
		if (dev->scl && scl) {
			if (dev->last_sda && !sda) { /* start condition */
				i2c_gpio_next_byte(dev);
				dev->pos = 0;
				i2c_gpio_log(2, "I2C GPIO %s: Next byte = %02X\n", dev->bus_name, dev->byte);
			}
			if (!dev->last_sda && sda) { /* stop condition */
				i2c_gpio_log(2, "I2C GPIO %s: Stop condition received (from TRANSMIT_WAIT)\n", dev->bus_name);
				dev->state = I2C_IDLE;
				i2c_gpio_stop(dev);
			}
		}
		break;

	case I2C_TRANSMIT_START:
		if (!dev->scl && scl)
			dev->state = I2C_TRANSMIT;
		if (dev->scl && scl && !dev->last_sda && sda) { /* stop condition */
			i2c_gpio_log(2, "I2C GPIO %s: Stop condition received (from TRANSMIT_START)\n", dev->bus_name);
			dev->state = I2C_IDLE;
			i2c_gpio_stop(dev);
		}
		/* fall-through */

	case I2C_TRANSMIT:
		if (!dev->scl && scl) {
			dev->scl = scl;
			if (!dev->pos)
				i2c_gpio_log(2, "I2C GPIO %s: Transmit byte %02X\n", dev->bus_name, dev->byte);
			dev->sda = sda = dev->byte & 0x80;
			i2c_gpio_log(2, "I2C GPIO %s: Transmit bit %02X %d\n", dev->bus_name, dev->byte, dev->pos);
			dev->byte <<= 1;
			dev->pos++;
			return;
		}
		if (dev->scl && !scl && (dev->pos == 8)) {
			dev->state = I2C_TRANSACKNOWLEDGE;
			i2c_gpio_log(2, "I2C GPIO %s: Acknowledge mode\n", dev->bus_name);
		}
		break;
    }

    if (!dev->scl && scl)
	dev->sda = sda;
    dev->last_sda = sda;
    dev->scl = scl;
}


uint8_t
i2c_gpio_get_scl(void *dev_handle)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;
    return dev->scl;
}


uint8_t
i2c_gpio_get_sda(void *dev_handle)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;
    switch (dev->state) {
	case I2C_TRANSMIT:
	case I2C_ACKNOWLEDGE:
		return dev->sda;

	case I2C_RECEIVE_WAIT:
		return dev->receive_wait_sda;

	default:
		return 1;
    }
}


void *
i2c_gpio_get_bus(void *dev_handle)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;
    return dev->i2c;
}

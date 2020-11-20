/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of a GPIO-based I2C device.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2020 RichardG.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
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
    void	*i2c;
    uint8_t	scl, sda, state, slave_state, slave_addr, slave_reg,
		slave_writing, slave_rw, last_sda, pos, transmit, byte;
} i2c_gpio_t;


void *
i2c_gpio_init(char *bus_name)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) malloc(sizeof(i2c_gpio_t));
    memset(dev, 0, sizeof(i2c_gpio_t));

    dev->i2c = i2c_addbus(bus_name);
    dev->scl = dev->sda = 1;

    return dev;
}


void
i2c_gpio_close(void *dev_handle)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;

    i2c_removebus(dev->i2c);

    free(dev);
}


void
i2c_gpio_next_byte(i2c_gpio_t *dev)
{
    dev->byte = i2c_read_byte(dev->i2c, dev->slave_addr);
}


void
i2c_gpio_write(i2c_gpio_t *dev)
{
    switch (dev->slave_state) {
	case SLAVE_IDLE:
		dev->slave_addr = dev->byte >> 1;
		if (!i2c_has_device(dev->i2c, dev->slave_addr)) {
			dev->slave_state = SLAVE_INVALID;
			break;
		}
		dev->slave_rw = dev->byte & 1;
		dev->slave_writing = 0;
		if (dev->slave_rw) {
			dev->slave_state = SLAVE_SENDDATA;
			dev->transmit = TRANSMITTER_SLAVE;
			dev->byte = i2c_read_byte(dev->i2c, dev->slave_addr);
		} else {
			dev->slave_state = SLAVE_RECEIVEADDR;
			dev->transmit = TRANSMITTER_MASTER;
		}
		pclog("slave_idle %02X %d\n", dev->slave_addr, dev->slave_rw);
		break;

	case SLAVE_RECEIVEADDR:
		pclog("slave_receiveaddr %02X %d\n", dev->slave_addr, dev->slave_rw);
		i2c_write_byte(dev->i2c, dev->slave_addr, dev->byte);
		dev->slave_writing = 1;
		dev->slave_reg = dev->byte;
		dev->slave_state = dev->slave_rw ? SLAVE_SENDDATA : SLAVE_RECEIVEDATA;
		break;

	case SLAVE_RECEIVEDATA:
		pclog("slave_receivedata %02X %d = %02X\n", dev->slave_addr, dev->slave_rw, dev->byte);
		dev->slave_writing = 0;
		i2c_write_byte_cmd(dev->i2c, dev->slave_addr, dev->slave_reg, dev->byte);
		break;

	case SLAVE_SENDDATA:
		pclog("slave_senddata %02X %d = %02X\n", dev->slave_addr, dev->slave_rw, dev->byte);
		break;
    }
}


void
i2c_gpio_stop(i2c_gpio_t *dev)
{
    pclog("stopping... state = %d\n", dev->slave_state);
    if (dev->slave_writing)
    	i2c_write_byte(dev->i2c, dev->slave_addr, dev->slave_reg);

    dev->slave_state = SLAVE_IDLE;
    dev->transmit = TRANSMITTER_MASTER;
}


void
i2c_gpio_set(void *dev_handle, uint8_t scl, uint8_t sda)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;

    switch (dev->state) {
	case I2C_IDLE:
		if (dev->scl && scl && dev->last_sda && !sda) { /* start bit */
			dev->state = I2C_RECEIVE;
			dev->pos = 0;
		}
		break;

	case I2C_RECEIVE_WAIT:
		if (!dev->scl && scl)
			dev->state = I2C_RECEIVE;
		/* fall-through */

	case I2C_RECEIVE:
		if (!dev->scl && scl) {
			dev->byte <<= 1;
			if (sda)
				dev->byte |= 1;
			else
				dev->byte &= 0xfe;
			if (++dev->pos == 8) {
				i2c_gpio_write(dev);
				dev->state = I2C_ACKNOWLEDGE;
			}
		} else if (dev->scl && scl) {
			if (sda && !dev->last_sda) { /* stop bit */
				dev->state = I2C_IDLE;
				i2c_gpio_stop(dev);
			} else if (!sda && dev->last_sda) { /* start bit */
				dev->pos = 0;
				dev->slave_state = SLAVE_IDLE;
			}
		}
		break;

	case I2C_ACKNOWLEDGE:
		if (!dev->scl && scl) {
			sda = 0;
			dev->pos = 0;
			dev->state = (dev->transmit == TRANSMITTER_MASTER) ? I2C_RECEIVE_WAIT : I2C_TRANSMIT;
		}
		break;

	case I2C_TRANSACKNOWLEDGE:
		if (!dev->scl && scl) {
			if (sda) { /* not acknowledged; must be end of transfer */
				dev->state = I2C_IDLE;
				i2c_gpio_stop(dev);
			} else { /* next byte to transfer */
				dev->state = I2C_TRANSMIT_START;
				i2c_gpio_next_byte(dev);
				dev->pos = 0;
			}
		}
		break;

	case I2C_TRANSMIT_WAIT:
		if (dev->scl && scl) {
			if (dev->last_sda && !sda) { /* start bit */
				i2c_gpio_next_byte(dev);
				dev->pos = 0;
			}
			if (!dev->last_sda && sda) { /* stop bit */
				dev->state = I2C_IDLE;
				i2c_gpio_stop(dev);
			}
		}
		break;

	case I2C_TRANSMIT_START:
		if (!dev->scl && scl)
			dev->state = I2C_TRANSMIT;
		if (dev->scl && scl && !dev->last_sda && sda) { /* stop bit */
			dev->state = I2C_IDLE;
			i2c_gpio_stop(dev);
		}
		/* fall-through */

	case I2C_TRANSMIT:
		if (!dev->scl && scl) {
			dev->scl = scl;
			dev->sda = sda = dev->byte & 0x80;
			dev->byte <<= 1;
			dev->pos++;
			return;
		}
		if (dev->scl && !scl && (dev->pos == 8))
			dev->state = I2C_TRANSACKNOWLEDGE;
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
    if ((dev->state == I2C_TRANSMIT) || (dev->state == I2C_ACKNOWLEDGE))
	return dev->sda;
    else if (dev->state == I2C_RECEIVE_WAIT)
	return 0; /* ack */
    else
	return 1;
}


void *
i2c_gpio_get_bus(void *dev_handle)
{
    i2c_gpio_t *dev = (i2c_gpio_t *) dev_handle;
    return dev->i2c;
}

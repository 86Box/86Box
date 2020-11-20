/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implement the I2C bus and its operations.
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
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/i2c.h>


#define NADDRS		128		/* I2C supports 128 addresses */
#define MAX(a, b) ((a) > (b) ? (a) : (b))


typedef struct _i2c_ {
    void	(*read_quick)(void *bus, uint8_t addr, void *priv);
    uint8_t	(*read_byte)(void *bus, uint8_t addr, void *priv);
    uint8_t	(*read_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv);
    uint16_t	(*read_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv);
    uint8_t	(*read_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv);

    void	(*write_quick)(void *bus, uint8_t addr, void *priv);
    void	(*write_byte)(void *bus, uint8_t addr, uint8_t val, void *priv);
    void	(*write_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t val, void *priv);
    void	(*write_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint16_t val, void *priv);
    void	(*write_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv);

    void	*priv;

    struct _i2c_ *prev, *next;
} i2c_t;

typedef struct {
    char *name;
    i2c_t *devices[NADDRS], *last[NADDRS];
} i2c_bus_t;


void *i2c_smbus;

#define ENABLE_I2C_LOG 1
#ifdef ENABLE_I2C_LOG
int i2c_do_log = ENABLE_I2C_LOG;


static void
i2c_log(const char *fmt, ...)
{
    va_list ap;

    if (i2c_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define i2c_log(fmt, ...)
#endif


void *
i2c_addbus(char *name)
{
    i2c_bus_t *bus = (i2c_bus_t *) malloc(sizeof(i2c_bus_t));
    memset(bus, 0, sizeof(i2c_bus_t));

    bus->name = name;

    return bus;
}


void
i2c_removebus(void *bus_handle)
{
    if (!bus_handle)
	return;

    free(bus_handle);
}


void
i2c_sethandler(void *bus_handle, uint8_t base, int size,
	       void (*read_quick)(void *bus, uint8_t addr, void *priv),
	       uint8_t (*read_byte)(void *bus, uint8_t addr, void *priv),
	       uint8_t (*read_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
	       uint16_t (*read_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
	       uint8_t (*read_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
	       void (*write_quick)(void *bus, uint8_t addr, void *priv),
	       void (*write_byte)(void *bus, uint8_t addr, uint8_t val, void *priv),
	       void (*write_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
	       void (*write_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
	       void (*write_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
	       void *priv)
{
    int c;
    i2c_t *p, *q = NULL;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;

    if (!bus_handle || ((base + size) >= NADDRS))
	return;

    for (c = 0; c < size; c++) {
	p = bus->last[base + c];
	q = (i2c_t *) malloc(sizeof(i2c_t));
	memset(q, 0, sizeof(i2c_t));
	if (p) {
		p->next = q;
		q->prev = p;
	} else {
		bus->devices[base + c] = q;
		q->prev = NULL;
	}

	q->read_byte = read_byte;
	q->read_byte_cmd = read_byte_cmd;
	q->read_word_cmd = read_word_cmd;
	q->read_block_cmd = read_block_cmd;

	q->write_byte = write_byte;
	q->write_byte_cmd = write_byte_cmd;
	q->write_word_cmd = write_word_cmd;
	q->write_block_cmd = write_block_cmd;

	q->priv = priv;
	q->next = NULL;

	bus->last[base + c] = q;
    }
}


void
i2c_removehandler(void *bus_handle, uint8_t base, int size,
		  void (*read_quick)(void *bus, uint8_t addr, void *priv),
		  uint8_t (*read_byte)(void *bus, uint8_t addr, void *priv),
		  uint8_t (*read_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
		  uint16_t (*read_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
		  uint8_t (*read_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
		  void (*write_quick)(void *bus, uint8_t addr, void *priv),
		  void (*write_byte)(void *bus, uint8_t addr, uint8_t val, void *priv),
		  void (*write_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
		  void (*write_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
		  void (*write_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
		  void *priv)
{
    int c;
    i2c_t *p, *q;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;

    if (!bus_handle || ((base + size) >= NADDRS))
	return;

    for (c = 0; c < size; c++) {
	p = bus->devices[base + c];
	if (!p)
		continue;
	while(p) {
		q = p->next;
		if ((p->read_byte == read_byte) && (p->read_byte_cmd == read_byte_cmd) &&
		    (p->read_word_cmd == read_word_cmd) && (p->read_block_cmd == read_block_cmd) &&
		    (p->write_byte == write_byte) && (p->write_byte_cmd == write_byte_cmd) &&
		    (p->write_word_cmd == write_word_cmd) && (p->write_block_cmd == write_block_cmd) &&
		    (p->priv == priv)) {
			if (p->prev)
				p->prev->next = p->next;
			else
				bus->devices[base + c] = p->next;
			if (p->next)
				p->next->prev = p->prev;
			else
				bus->last[base + c] = p->prev;
			free(p);
			p = NULL;
			break;
		}
		p = q;
	}
    }
}


void
i2c_handler(int set, void *bus_handle, uint8_t base, int size,
	    void (*read_quick)(void *bus, uint8_t addr, void *priv),
	    uint8_t (*read_byte)(void *bus, uint8_t addr, void *priv),
	    uint8_t (*read_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
	    uint16_t (*read_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
	    uint8_t (*read_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
	    void (*write_quick)(void *bus, uint8_t addr, void *priv),
	    void (*write_byte)(void *bus, uint8_t addr, uint8_t val, void *priv),
	    void (*write_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
	    void (*write_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
	    void (*write_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
	    void *priv)
{
    if (set)
	i2c_sethandler(bus_handle, base, size, read_quick, read_byte, read_byte_cmd, read_word_cmd, read_block_cmd, write_quick, write_byte, write_byte_cmd, write_word_cmd, write_block_cmd, priv);
    else
	i2c_removehandler(bus_handle, base, size, read_quick, read_byte, read_byte_cmd, read_word_cmd, read_block_cmd, write_quick, write_byte, write_byte_cmd, write_word_cmd, write_block_cmd, priv);
}


uint8_t
i2c_has_device(void *bus_handle, uint8_t addr)
{
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;

    if (!bus)
	return 0;

    return(!!bus->devices[addr]);
}


void
i2c_read_quick(void *bus_handle, uint8_t addr)
{
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    i2c_t *p;
    int found = 0;

    if (!bus)
	return;

    p = bus->devices[addr];
    if (p) {
	while(p) {
		if (p->read_byte) {
			p->read_quick(bus_handle, addr, p->priv);
			found++;
		}
		p = p->next;
	}
    }

    i2c_log("I2C: read_quick(%s, %02X)\n", bus->name, addr);
}


uint8_t
i2c_read_byte(void *bus_handle, uint8_t addr)
{
    uint8_t ret = 0xff;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    i2c_t *p;
    int found = 0;

    if (!bus)
	return(ret);

    p = bus->devices[addr];
    if (p) {
	while(p) {
		if (p->read_byte) {
			ret &= p->read_byte(bus_handle, addr, p->priv);
			found++;
		}
		p = p->next;
	}
    }

    i2c_log("I2C: read_byte(%s, %02X) = %02X\n", bus->name, addr, ret);

    return(ret);
}

uint8_t
i2c_read_byte_cmd(void *bus_handle, uint8_t addr, uint8_t cmd)
{
    uint8_t ret = 0xff;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    i2c_t *p;
    int found = 0;

    if (!bus)
	return(ret);

    p = bus->devices[addr];
    if (p) {
	while(p) {
		if (p->read_byte_cmd) {
			ret &= p->read_byte_cmd(bus_handle, addr, cmd, p->priv);
			found++;
		}
		p = p->next;
	}
    }

    i2c_log("I2C: read_byte_cmd(%s, %02X, %02X) = %02X\n", bus->name, addr, cmd, ret);

    return(ret);
}

uint16_t
i2c_read_word_cmd(void *bus_handle, uint8_t addr, uint8_t cmd)
{
    uint16_t ret = 0xffff;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    i2c_t *p;
    int found = 0;

    if (!bus)
	return(ret);

    p = bus->devices[addr];
    if (p) {
	while(p) {
		if (p->read_word_cmd) {
			ret &= p->read_word_cmd(bus_handle, addr, cmd, p->priv);
			found++;
		}
		p = p->next;
	}
    }

    i2c_log("I2C: read_word_cmd(%s, %02X, %02X) = %04X\n", bus->name, addr, cmd, ret);

    return(ret);
}

uint8_t
i2c_read_block_cmd(void *bus_handle, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len)
{
    uint8_t ret = 0;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    i2c_t *p;
    int found = 0;

    if (!bus)
	return(ret);

    p = bus->devices[addr];
    if (p) {
	while(p) {
		if (p->read_block_cmd) {
			ret = MAX(ret, p->read_block_cmd(bus_handle, addr, cmd, data, len, p->priv));
			found++;
		}
		p = p->next;
	}
    }

    i2c_log("I2C: read_block_cmd(%s, %02X, %02X) = %02X\n", bus->name, addr, cmd, len);

    return(ret);
}


void
i2c_write_quick(void *bus_handle, uint8_t addr)
{
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    i2c_t *p;
    int found = 0;

    if (!bus)
	return;

    p = bus->devices[addr];
    if (p) {
	while(p) {
		if (p->read_byte) {
			p->write_quick(bus_handle, addr, p->priv);
			found++;
		}
		p = p->next;
	}
    }

    i2c_log("I2C: write_quick(%s, %02X)\n", bus->name, addr);
}


void
i2c_write_byte(void *bus_handle, uint8_t addr, uint8_t val)
{
    i2c_t *p;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    int found = 0;

    if (!bus)
	return;

    if (bus->devices[addr]) {
	p = bus->devices[addr];
	while(p) {
		if (p->write_byte) {
			p->write_byte(bus_handle, addr, val, p->priv);
			found++;
		}
		p = p->next;
	}
    }

    i2c_log("I2C: write_byte(%s, %02X, %02X)\n", bus->name, addr, val);

    return;
}

void
i2c_write_byte_cmd(void *bus_handle, uint8_t addr, uint8_t cmd, uint8_t val)
{
    i2c_t *p;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    int found = 0;

    if (!bus)
	return;

    if (bus->devices[addr]) {
	p = bus->devices[addr];
	while(p) {
		if (p->write_byte_cmd) {
			p->write_byte_cmd(bus_handle, addr, cmd, val, p->priv);
			found++;
		}
		p = p->next;
	}
    }

    i2c_log("I2C: write_byte_cmd(%s, %02X, %02X, %02X)\n", bus->name, addr, cmd, val);

    return;
}

void
i2c_write_word_cmd(void *bus_handle, uint8_t addr, uint8_t cmd, uint16_t val)
{
    i2c_t *p;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    int found = 0;

    if (!bus)
	return;

    if (bus->devices[addr]) {
	p = bus->devices[addr];
	while(p) {
		if (p->write_word_cmd) {
			p->write_word_cmd(bus_handle, addr, cmd, val, p->priv);
			found++;
		}
		p = p->next;
	}
    }

    i2c_log("I2C: write_word_cmd(%s, %02X, %02X, %04X)\n", bus->name, addr, cmd, val);

    return;
}

void
i2c_write_block_cmd(void *bus_handle, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len)
{
    i2c_t *p;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    int found = 0;

    if (!bus)
	return;

    p = bus->devices[addr];
    if (p) {
	while(p) {
		if (p->write_block_cmd) {
			p->write_block_cmd(bus_handle, addr, cmd, data, len, p->priv);
			found++;
		}
		p = p->next;
	}
    }

    i2c_log("I2C: write_block_cmd(%s, %02X, %02X, %02X)\n", bus->name, addr, cmd, len);

    return;
}

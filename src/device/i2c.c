/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the I2C bus and its operations.
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
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/i2c.h>

#define NADDRS    128 /* I2C supports 128 addresses */
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct _i2c_ {
    uint8_t (*start)(void *bus, uint8_t addr, uint8_t read, void *priv);
    uint8_t (*read)(void *bus, uint8_t addr, void *priv);
    uint8_t (*write)(void *bus, uint8_t addr, uint8_t data, void *priv);
    void (*stop)(void *bus, uint8_t addr, void *priv);

    void *priv;

    struct _i2c_ *prev, *next;
} i2c_t;

typedef struct i2c_bus_t {
    char  *name;
    i2c_t *devices[NADDRS];
    i2c_t *last[NADDRS];
} i2c_bus_t;

void *i2c_smbus;

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
#    define i2c_log(fmt, ...)
#endif

void *
i2c_addbus(char *name)
{
    i2c_bus_t *bus = (i2c_bus_t *) calloc(1, sizeof(i2c_bus_t));

    bus->name = name;

    return bus;
}

void
i2c_removebus(void *bus_handle)
{
    i2c_t     *p;
    i2c_t     *q;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;

    if (!bus_handle)
        return;

    for (uint8_t c = 0; c < NADDRS; c++) {
        p = bus->devices[c];
        if (!p)
            continue;
        while (p) {
            q = p->next;
            free(p);
            p = q;
        }
    }

    free(bus);
}

char *
i2c_getbusname(void *bus_handle)
{
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;

    if (!bus_handle)
        return (NULL);

    return (bus->name);
}

void
i2c_sethandler(void *bus_handle, uint8_t base, int size,
               uint8_t (*start)(void *bus, uint8_t addr, uint8_t read, void *priv),
               uint8_t (*read)(void *bus, uint8_t addr, void *priv),
               uint8_t (*write)(void *bus, uint8_t addr, uint8_t data, void *priv),
               void (*stop)(void *bus, uint8_t addr, void *priv),
               void *priv)
{
    i2c_t     *p;
    i2c_t     *q = NULL;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;

    if (!bus_handle || ((base + size) > NADDRS))
        return;

    for (int c = 0; c < size; c++) {
        p = bus->last[base + c];
        q = (i2c_t *) calloc(1, sizeof(i2c_t));
        if (p) {
            p->next = q;
            q->prev = p;
        } else {
            bus->devices[base + c] = q;
            q->prev                = NULL;
        }

        q->start = start;
        q->read  = read;
        q->write = write;
        q->stop  = stop;

        q->priv = priv;
        q->next = NULL;

        bus->last[base + c] = q;
    }
}

void
i2c_removehandler(void *bus_handle, uint8_t base, int size,
                  uint8_t (*start)(void *bus, uint8_t addr, uint8_t read, void *priv),
                  uint8_t (*read)(void *bus, uint8_t addr, void *priv),
                  uint8_t (*write)(void *bus, uint8_t addr, uint8_t data, void *priv),
                  void (*stop)(void *bus, uint8_t addr, void *priv),
                  void *priv)
{
    i2c_t     *p;
    i2c_t     *q;
    i2c_bus_t *bus = (i2c_bus_t *) bus_handle;

    if (!bus_handle || ((base + size) > NADDRS))
        return;

    for (int c = 0; c < size; c++) {
        p = bus->devices[base + c];
        if (!p)
            continue;
        while (p) {
            q = p->next;
            if ((p->start == start) && (p->read == read) && (p->write == write) && (p->stop == stop) && (p->priv == priv)) {
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
            uint8_t (*start)(void *bus, uint8_t addr, uint8_t read, void *priv),
            uint8_t (*read)(void *bus, uint8_t addr, void *priv),
            uint8_t (*write)(void *bus, uint8_t addr, uint8_t data, void *priv),
            void (*stop)(void *bus, uint8_t addr, void *priv),
            void *priv)
{
    if (set)
        i2c_sethandler(bus_handle, base, size, start, read, write, stop, priv);
    else
        i2c_removehandler(bus_handle, base, size, start, read, write, stop, priv);
}

uint8_t
i2c_start(void *bus_handle, uint8_t addr, uint8_t read)
{
    uint8_t          ret = 0;
    const i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    i2c_t           *p;

    if (!bus)
        return ret;

    p = bus->devices[addr];
    if (p) {
        while (p) {
            if (p->start) {
                ret |= p->start(bus_handle, addr, read, p->priv);
            }
            p = p->next;
        }
    }

    i2c_log("I2C %s: start(%02X) = %d\n", bus->name, addr, ret);

    return ret;
}

uint8_t
i2c_read(void *bus_handle, uint8_t addr)
{
    uint8_t          ret = 0;
    const i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    i2c_t           *p;

    if (!bus)
        return ret;

    p = bus->devices[addr];
    if (p) {
        while (p) {
            if (p->read) {
                ret = p->read(bus_handle, addr, p->priv);
                break;
            }
            p = p->next;
        }
    }

    i2c_log("I2C %s: read(%02X) = %02X\n", bus->name, addr, ret);

    return ret;
}

uint8_t
i2c_write(void *bus_handle, uint8_t addr, uint8_t data)
{
    uint8_t          ret = 0;
    i2c_t           *p;
    const i2c_bus_t *bus = (i2c_bus_t *) bus_handle;

    if (!bus)
        return ret;

    p = bus->devices[addr];
    if (p) {
        while (p) {
            if (p->write) {
                ret |= p->write(bus_handle, addr, data, p->priv);
            }
            p = p->next;
        }
    }

    i2c_log("I2C %s: write(%02X, %02X) = %d\n", bus->name, addr, data, ret);

    return ret;
}

void
i2c_stop(void *bus_handle, uint8_t addr)
{
    const i2c_bus_t *bus = (i2c_bus_t *) bus_handle;
    i2c_t           *p;

    if (!bus)
        return;

    p = bus->devices[addr];
    if (p) {
        while (p) {
            if (p->stop) {
                p->stop(bus_handle, addr, p->priv);
            }
            p = p->next;
        }
    }

    i2c_log("I2C %s: stop(%02X)\n", bus->name, addr);
}

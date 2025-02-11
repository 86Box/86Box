/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Port 92 used by PS/2 machines and 386+
 *          clones.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2019 Miran Grca.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/pit.h>
#include <86box/port_92.h>
#include <86box/plat_unused.h>

#define PORT_92_INV   1
#define PORT_92_WORD  2
#define PORT_92_PCI   4
#define PORT_92_RESET 8
#define PORT_92_A20   16
#define PORT_92_KEY   32

static uint8_t
port_92_readb(uint16_t port, void *priv)
{
    uint8_t          ret = 0x00;
    const port_92_t *dev = (port_92_t *) priv;

    if (port == 0x92) {
        /* Return bit 1 directly from mem_a20_alt, so the
           pin can be reset independently of the device. */
        if (dev->flags & PORT_92_KEY)
            ret = (dev->reg & ~0x03) | (mem_a20_key & 2) | (cpu_alt_reset & 1);
        else
            ret = (dev->reg & ~0x03) | (mem_a20_alt & 2) | (cpu_alt_reset & 1);

        if (dev->flags & PORT_92_INV)
            ret |= 0xfc;
        else if (dev->flags & PORT_92_PCI)
            ret |= 0x24; /* Intel SIO datasheet says bits 2 and 5 are always 1. */
    } else if (dev->flags & PORT_92_INV)
        ret = 0xff;

    return ret;
}

static uint16_t
port_92_readw(uint16_t port, void *priv)
{
    uint16_t         ret = 0xffff;
    const port_92_t *dev = (port_92_t *) priv;

    if (!(dev->flags & PORT_92_PCI))
        ret = port_92_readb(port, priv);

    return ret;
}

/*
   This does the exact same thing as keyboard controller reset.
   TODO: ALi M1543(c) behavior.
 */
static void
port_92_pulse(UNUSED(void *priv))
{
    softresetx86(); /* Pulse reset! */
    cpu_set_edx();
    flushmmucache();

    cpu_alt_reset = 1;
}

static void
port_92_writeb(uint16_t port, uint8_t val, void *priv)
{
    port_92_t *dev = (port_92_t *) priv;

    if (port != 0x92)
        return;

    dev->reg = val & 0x03;

    if (dev->flags & PORT_92_KEY) {
        mem_a20_key = val & 2;
        mem_a20_recalc();
    } else if ((mem_a20_alt ^ val) & 2) {
        mem_a20_alt = (mem_a20_alt & 0xfd) | (val & 2);
        mem_a20_recalc();
    }

    if ((~cpu_alt_reset & val) & 1)
        timer_set_delay_u64(&dev->pulse_timer, dev->pulse_period);
    else if (!(val & 1))
        timer_disable(&dev->pulse_timer);

    cpu_alt_reset = (val & 1);

    if (dev->flags & PORT_92_INV)
        dev->reg |= 0xfc;
}

static void
port_92_writew(uint16_t port, uint16_t val, void *priv)
{
    const port_92_t *dev = (port_92_t *) priv;

    if (!(dev->flags & PORT_92_PCI))
        port_92_writeb(port, val & 0xff, priv);
}

void
port_92_set_period(void *priv, uint64_t pulse_period)
{
    port_92_t *dev = (port_92_t *) priv;

    dev->pulse_period = pulse_period;
}

void
port_92_set_features(void *priv, int reset, int a20)
{
    port_92_t *dev = (port_92_t *) priv;

    dev->flags &= ~(PORT_92_RESET | PORT_92_A20);

    if (reset)
        dev->flags |= PORT_92_RESET;

    timer_disable(&dev->pulse_timer);

    if (a20) {
        dev->flags |= PORT_92_A20;
        mem_a20_alt = (dev->reg & 2);
    } else
        mem_a20_alt = 0;

    mem_a20_recalc();
}

void
port_92_add(void *priv)
{
    port_92_t *dev = (port_92_t *) priv;

    if (dev->flags & (PORT_92_WORD | PORT_92_PCI))
        io_sethandler(0x0092, 2,
                      port_92_readb, port_92_readw, NULL, port_92_writeb, port_92_writew, NULL, dev);
    else
        io_sethandler(0x0092, 1,
                      port_92_readb, NULL, NULL, port_92_writeb, NULL, NULL, dev);
}

void
port_92_remove(void *priv)
{
    port_92_t *dev = (port_92_t *) priv;

    if (dev->flags & (PORT_92_WORD | PORT_92_PCI))
        io_removehandler(0x0092, 2,
                         port_92_readb, port_92_readw, NULL, port_92_writeb, port_92_writew, NULL, dev);
    else
        io_removehandler(0x0092, 1,
                         port_92_readb, NULL, NULL, port_92_writeb, NULL, NULL, dev);
}

static void
port_92_reset(UNUSED(void *priv))
{
    cpu_alt_reset = 0;

    mem_a20_alt = 0x00;
    mem_a20_recalc();
}

static void
port_92_close(void *priv)
{
    port_92_t *dev = (port_92_t *) priv;

    timer_disable(&dev->pulse_timer);

    free(dev);
}

void *
port_92_init(const device_t *info)
{
    port_92_t *dev = (port_92_t *) malloc(sizeof(port_92_t));
    memset(dev, 0, sizeof(port_92_t));

    dev->flags = info->local & 0xff;

    timer_add(&dev->pulse_timer, port_92_pulse, dev, 0);

    dev->reg    = 0;
    mem_a20_alt = 0;
    mem_a20_recalc();

    cpu_alt_reset = 0;

    flushmmucache();

    port_92_add(dev);

    dev->pulse_period = (uint64_t) (4.0 * SYSCLK * (double) (1ULL << 32ULL));

    dev->flags |= (PORT_92_RESET | PORT_92_A20);

    return dev;
}

const device_t port_92_device = {
    .name          = "Port 92 Register",
    .internal_name = "port_92",
    .flags         = 0,
    .local         = 0,
    .init          = port_92_init,
    .close         = port_92_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t port_92_key_device = {
    .name          = "Port 92 Register (using A20 key)",
    .internal_name = "port_92_key",
    .flags         = 0,
    .local         = PORT_92_KEY,
    .init          = port_92_init,
    .close         = port_92_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t port_92_inv_device = {
    .name          = "Port 92 Register (inverted bits 2-7)",
    .internal_name = "port_92_inv",
    .flags         = 0,
    .local         = PORT_92_INV,
    .init          = port_92_init,
    .close         = port_92_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t port_92_word_device = {
    .name          = "Port 92 Register (16-bit)",
    .internal_name = "port_92_word",
    .flags         = 0,
    .local         = PORT_92_WORD,
    .init          = port_92_init,
    .close         = port_92_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t port_92_pci_device = {
    .name          = "Port 92 Register (PCI)",
    .internal_name = "port_92_pci",
    .flags         = 0,
    .local         = PORT_92_PCI,
    .init          = port_92_init,
    .close         = port_92_close,
    .reset         = port_92_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

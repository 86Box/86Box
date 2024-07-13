/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of CPU I/O ports used by NEC PC-98x1 machines.
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
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/pit.h>
#include <86box/port_pc98_cpu.h>
#include <86box/plat_unused.h>

static uint8_t
port_pc98_cpu_f2_readb(UNUSED(uint16_t port), UNUSED(void *priv))
{
    uint8_t ret = 0xff;

    ret -= ((rammask >> 20) & 0x01);
    return ret;
}

static uint8_t
port_pc98_cpu_f6_readb(UNUSED(uint16_t port), UNUSED(void *priv))
{
    uint8_t ret = 0x00;

    if (!(rammask & (1 << 20)))
        ret |= 0x01;

    if (nmi_enable)
        ret |= 0x02;

    return ret;
}

static uint8_t
port_pc98_cpu_534_readb(UNUSED(uint16_t port), UNUSED(void *priv))
{
    return 0xec; /*CPU mode*/
}

static uint8_t
port_pc98_cpu_9894_readb(UNUSED(uint16_t port), UNUSED(void *priv))
{
    return 0x90; /*CPU wait*/
}

static void
port_pc98_cpu_pulse(UNUSED(void *priv))
{
    softresetx86(); /* Pulse reset! */
    cpu_set_edx();
    flushmmucache();

    cpu_alt_reset = 1;
}

static void
port_pc98_cpu_f2_writeb(UNUSED(uint16_t port), UNUSED(uint8_t val), UNUSED(void *priv))
{
    mem_a20_alt = 1;
    mem_a20_recalc();
}

static void
port_pc98_cpu_f6_writeb(UNUSED(uint16_t port), uint8_t val, UNUSED(void *priv))
{
    switch (val) {
        case 0x02:
            mem_a20_alt = 1;
            mem_a20_recalc();
            break;
        case 0x03:
            mem_a20_alt = 0;
            mem_a20_recalc();
            break;
        default:
            break;
    }
}

static void
port_pc98_cpu_534_writeb(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    port_pc98_cpu_t *dev = (port_pc98_cpu_t *) priv;

    if ((~cpu_alt_reset & val) & 1)
        timer_set_delay_u64(&dev->pulse_timer, dev->pulse_period);
    else if (!(val & 1))
        timer_disable(&dev->pulse_timer);

    cpu_alt_reset = (val & 1);
}

void
port_pc98_cpu_add(void *priv)
{
    port_92_t *dev = (port_92_t *) priv;

    io_sethandler(0x00f2, 1,
                  port_pc98_cpu_f2_readb, NULL, NULL, port_pc98_cpu_f2_writeb, NULL, NULL, dev);
    io_sethandler(0x00f6, 1,
                  port_pc98_cpu_f6_readb, NULL, NULL, port_pc98_cpu_f6_writeb, NULL, NULL, dev);
    io_sethandler(0x0534, 1,
                  port_pc98_cpu_534_readb, NULL, NULL, port_pc98_cpu_534_writeb, NULL, NULL, dev);
    io_sethandler(0x9894, 1,
                  port_pc98_cpu_9894_readb, NULL, NULL, NULL, NULL, NULL, dev);
}

void
port_pc98_cpu_remove(void *priv)
{
    port_92_t *dev = (port_92_t *) priv;

    io_removehandler(0x00f2, 1,
                  port_pc98_cpu_f2_readb, NULL, NULL, port_pc98_cpu_f2_writeb, NULL, NULL, dev);
    io_removehandler(0x00f6, 1,
                  port_pc98_cpu_f6_readb, NULL, NULL, port_pc98_cpu_f6_writeb, NULL, NULL, dev);
    io_removehandler(0x0534, 1,
                  port_pc98_cpu_534_readb, NULL, NULL, port_pc98_cpu_534_writeb, NULL, NULL, dev);
    io_removehandler(0x9894, 1,
                  port_pc98_cpu_9894_readb, NULL, NULL, NULL, NULL, NULL, dev);
}

static void
port_pc98_cpu_reset(UNUSED(void *priv))
{
    cpu_alt_reset = 0;

    mem_a20_alt = 0x00;
    mem_a20_recalc();
}

static void
port_pc98_cpu_close(void *priv)
{
    port_pc98_cpu_t *dev = (port_pc98_cpu_t *) priv;

    timer_disable(&dev->pulse_timer);

    free(dev);
}

void *
port_pc98_cpu_init(const device_t *info)
{
    port_pc98_cpu_t *dev = (port_pc98_cpu_t *) malloc(sizeof(port_pc98_cpu_t));
    memset(dev, 0, sizeof(port_pc98_cpu_t));

    dev->flags = info->local & 0xff;

    timer_add(&dev->pulse_timer, port_pc98_cpu_pulse, dev, 0);

    mem_a20_alt = 0;
    mem_a20_recalc();

    cpu_alt_reset = 0;

    flushmmucache();

    port_pc98_cpu_add(dev);

    dev->pulse_period = (uint64_t) (4.0 * SYSCLK * (double) (1ULL << 32ULL));

    return dev;
}

const device_t port_pc98_cpu_device = {
    .name          = "NEC PC-98x1 CPU Ports",
    .internal_name = "port_pc98_cpu",
    .flags         = 0,
    .local         = 0,
    .init          = port_pc98_cpu_init,
    .close         = port_pc98_cpu_close,
    .reset         = port_pc98_cpu_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};


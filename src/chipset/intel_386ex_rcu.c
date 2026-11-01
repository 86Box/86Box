/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel 386EX Refresh Control Unit.
 *
 *          While enabled, a 10-bit interval counter, clocked by the
 *          processor clock, counts down from RFSCIR to one, reloads, and
 *          starts a refresh cycle each time. Each refresh cycle advances
 *          the 13-bit refresh address counter that RFSADD reads, which
 *          RFSBAD extends to a full refresh address.
 *
 *          Memory never needs refreshing here, so no refresh bus cycles
 *          are run, and the unit is only its registers: the interval and
 *          address counters follow the time since they were last written.
 *          The real address counter's low 7 bits come from a linear-
 *          feedback shift register, whose sequence is not documented, so
 *          the counter here counts in binary. A write to RFSCIR takes
 *          effect at once instead of at the next reload.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/i386ex.h>
#include <86box/plat_unused.h>

/* Register addresses. */
#define RFSBAD      0xf4a0
#define RFSCIR      0xf4a2
#define RFSCON      0xf4a4
#define RFSADD      0xf4a6

#define RFSBAD_MASK 0x0fff
#define RFSCIR_MASK 0x03ff
#define RFSCON_REN  0x8000
#define RFSADD_MASK 0x3ffe
#define RFSADD_A0   0x0001

struct i386ex_rcu_t {
    uint16_t rfsbad;
    uint16_t rfscir;
    uint16_t rfscon; /* REN, and the interval counter while it is stopped. */
    uint16_t rfsadd; /* The address counter when the unit was last started. */

    uint64_t start; /* When the interval counter was last loaded. */
};

#ifdef ENABLE_I386EX_RCU_LOG
int i386ex_rcu_do_log = ENABLE_I386EX_RCU_LOG;

static void
i386ex_rcu_log(const char *fmt, ...)
{
    va_list ap;

    if (i386ex_rcu_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define i386ex_rcu_log(fmt, ...)
#endif

/* The clocks from one reload to the next: the counter counts RFSCIR down to one. */
static uint32_t
i386ex_rcu_period(const i386ex_rcu_t *dev)
{
    return dev->rfscir ? dev->rfscir : (RFSCIR_MASK + 1);
}

/* The time base is the processor clock, which is what the interval counter counts. */
static uint64_t
i386ex_rcu_elapsed(const i386ex_rcu_t *dev)
{
    return tsc - dev->start;
}

static uint16_t
i386ex_rcu_get_count(const i386ex_rcu_t *dev)
{
    if (!(dev->rfscon & RFSCON_REN))
        return dev->rfscon & RFSCIR_MASK;

    return (i386ex_rcu_period(dev) - (uint32_t) (i386ex_rcu_elapsed(dev) % i386ex_rcu_period(dev))) & RFSCIR_MASK;
}

static uint16_t
i386ex_rcu_get_address(const i386ex_rcu_t *dev)
{
    uint64_t refreshes = 0;

    if (dev->rfscon & RFSCON_REN)
        refreshes = i386ex_rcu_elapsed(dev) / i386ex_rcu_period(dev);

    return (uint16_t) ((dev->rfsadd + (refreshes << 1)) & RFSADD_MASK) | RFSADD_A0;
}

/* Bring both counters up to now, so that they can restart from here. */
static void
i386ex_rcu_settle(i386ex_rcu_t *dev)
{
    dev->rfsadd = i386ex_rcu_get_address(dev);
    dev->rfscon = (dev->rfscon & RFSCON_REN) | i386ex_rcu_get_count(dev);
}

static uint16_t
i386ex_rcu_readw(uint16_t port, void *priv)
{
    const i386ex_rcu_t *dev = (i386ex_rcu_t *) priv;

    switch (port & ~1) {
        case RFSBAD:
            return dev->rfsbad;
        case RFSCIR:
            return dev->rfscir;
        case RFSCON:
            return (dev->rfscon & RFSCON_REN) | i386ex_rcu_get_count(dev);
        case RFSADD:
            return i386ex_rcu_get_address(dev);
        default:
            return 0xffff;
    }
}

static uint8_t
i386ex_rcu_read(uint16_t port, void *priv)
{
    return i386ex_rcu_readw(port & ~1, priv) >> ((port & 1) << 3);
}

static void
i386ex_rcu_writew(uint16_t port, uint16_t val, void *priv)
{
    i386ex_rcu_t *dev = (i386ex_rcu_t *) priv;

    i386ex_rcu_log("386EX RCU: [%04X] = %04X\n", port, val);

    i386ex_rcu_settle(dev);

    switch (port & ~1) {
        case RFSBAD:
            dev->rfsbad = val & RFSBAD_MASK;
            break;
        case RFSCIR:
            dev->rfscir = val & RFSCIR_MASK;
            break;
        case RFSCON:
            /* Enabling the unit loads the interval counter. */
            if ((val & RFSCON_REN) && !(dev->rfscon & RFSCON_REN))
                dev->rfscon = RFSCON_REN | dev->rfscir;
            else
                dev->rfscon = (dev->rfscon & RFSCIR_MASK) | (val & RFSCON_REN);
            break;
        case RFSADD:
            dev->rfsadd = (val & RFSADD_MASK) | RFSADD_A0;
            break;
        default:
            break;
    }

    /* The counters restart from their settled values. */
    if (dev->rfscon & RFSCON_REN)
        dev->start = tsc - (i386ex_rcu_period(dev) - (dev->rfscon & RFSCIR_MASK)) % i386ex_rcu_period(dev);
}

static void
i386ex_rcu_write(uint16_t port, uint8_t val, void *priv)
{
    uint16_t old = i386ex_rcu_readw(port & ~1, priv);

    if (port & 1)
        i386ex_rcu_writew(port & ~1, (old & 0x00ff) | (val << 8), priv);
    else
        i386ex_rcu_writew(port, (old & 0xff00) | val, priv);
}

void
i386ex_rcu_handler(i386ex_rcu_t *dev, int set)
{
    io_handler(set, RFSBAD, 8,
               i386ex_rcu_read, i386ex_rcu_readw, NULL,
               i386ex_rcu_write, i386ex_rcu_writew, NULL, dev);
}

static void *
i386ex_rcu_init(UNUSED(const device_t *info))
{
    i386ex_rcu_t *dev = (i386ex_rcu_t *) calloc(1, sizeof(i386ex_rcu_t));

    dev->rfsadd = 0x00ff;

    return dev;
}

static void
i386ex_rcu_close(void *priv)
{
    free(priv);
}

const device_t i386ex_rcu_device = {
    .name          = "Intel 386EX Refresh Control Unit",
    .internal_name = "i386ex_rcu",
    .flags         = 0,
    .local         = 0,
    .init          = i386ex_rcu_init,
    .close         = i386ex_rcu_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

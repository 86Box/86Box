/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel 386EX Watchdog Timer Unit.
 *
 *          A 32-bit down-counter, clocked by the processor clock, that
 *          reloads itself and pulses the WDTOUT pin each time it reaches
 *          zero. It runs from reset, as a general-purpose timer timing out
 *          every 4M clocks, until software stops its clock; the lockout
 *          sequence puts it in watchdog mode instead, where software must
 *          repeat the sequence to reload it, and nothing short of a device
 *          reset can stop it or change it again.
 *
 *          The chip itself does nothing on a timeout but pulse WDTOUT, and
 *          the inverted pulse into the slave's IR7. What WDTOUT is wired to
 *          (RESET, NMI or nothing) is up to the board.
 *
 *          Bus monitor mode reloads the counter on every ADS# and stops it
 *          on READY#. Bus cycles always end here, so it never times out,
 *          and the counter just holds its reload value. Watchdog mode
 *          should stop the counter while the core is idle, which is not
 *          emulated, nor is the 8-clock length of the WDTOUT pulse.
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
#include <86box/device.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/i386ex.h>
#include <86box/plat_unused.h>

/* Register addresses. */
#define WDTRLDH          0xf4c0
#define WDTRLDL          0xf4c2
#define WDTCNTH          0xf4c4
#define WDTCNTL          0xf4c6
#define WDTCLR           0xf4c8
#define WDTSTATUS        0xf4ca

#define WDTSTATUS_WDTEN  0x80
#define WDTSTATUS_BUSMON 0x02
#define WDTSTATUS_CLKDIS 0x01

#define WDT_LOCKOUT_1    0xf01e
#define WDT_LOCKOUT_2    0x0fe1

#define WDT_RESET_VALUE  0x003fffff

struct i386ex_wdt_t {
    uint32_t reload;
    uint32_t count; /* While the counter is stopped. */
    uint8_t  status;
    uint8_t  lockout; /* The first word of the lockout sequence was written. */

    pc_timer_t timer;

    void (*out_func)(void *priv);
    void *out_priv;
};

#ifdef ENABLE_I386EX_WDT_LOG
int i386ex_wdt_do_log = ENABLE_I386EX_WDT_LOG;

static void
i386ex_wdt_log(const char *fmt, ...)
{
    va_list ap;

    if (i386ex_wdt_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define i386ex_wdt_log(fmt, ...)
#endif

static int
i386ex_wdt_running(const i386ex_wdt_t *dev)
{
    return !(dev->status & (WDTSTATUS_CLKDIS | WDTSTATUS_BUSMON));
}

/* Timer ticks are processor clocks, which is what the counter counts. */
static uint32_t
i386ex_wdt_get_count(i386ex_wdt_t *dev)
{
    uint64_t remaining;

    if (!timer_is_enabled(&dev->timer))
        return dev->count;

    remaining = (uint64_t) (timer_get_remaining_u64(&dev->timer) >> 32);

    return (remaining > dev->reload) ? dev->reload : (uint32_t) remaining;
}

/* Start or stop the counter to suit the mode, keeping its count. */
static void
i386ex_wdt_update(i386ex_wdt_t *dev)
{
    if (i386ex_wdt_running(dev)) {
        if (!timer_is_enabled(&dev->timer))
            timer_set_delay_u64(&dev->timer, ((uint64_t) dev->count + 1) << 32);
    } else {
        dev->count = (dev->status & WDTSTATUS_BUSMON) ? dev->reload : i386ex_wdt_get_count(dev);
        timer_disable(&dev->timer);
    }
}

static void
i386ex_wdt_reload(i386ex_wdt_t *dev)
{
    timer_disable(&dev->timer);
    dev->count = dev->reload;
    i386ex_wdt_update(dev);
}

static void
i386ex_wdt_timeout(void *priv)
{
    i386ex_wdt_t *dev = (i386ex_wdt_t *) priv;

    i386ex_wdt_log("386EX WDT: timeout\n");

    /* The next period starts on the clock after the count reaches zero. */
    timer_advance_u64(&dev->timer, ((uint64_t) dev->reload + 1) << 32);

    /* WDTOUT# idles high into an edge-triggered IR7, so the pulse is an edge. */
    picint(1 << 15);

    if (dev->out_func != NULL)
        dev->out_func(dev->out_priv);
}

static uint16_t
i386ex_wdt_readw(uint16_t port, void *priv)
{
    i386ex_wdt_t *dev = (i386ex_wdt_t *) priv;

    switch (port & ~1) {
        case WDTRLDH:
            return dev->reload >> 16;
        case WDTRLDL:
            return dev->reload & 0xffff;
        case WDTCNTH:
            return i386ex_wdt_get_count(dev) >> 16;
        case WDTCNTL:
            return i386ex_wdt_get_count(dev) & 0xffff;
        case WDTSTATUS:
            return dev->status;
        default:
            return 0xffff;
    }
}

static uint8_t
i386ex_wdt_read(uint16_t port, void *priv)
{
    return i386ex_wdt_readw(port & ~1, priv) >> ((port & 1) << 3);
}

static void
i386ex_wdt_writew(uint16_t port, uint16_t val, void *priv)
{
    i386ex_wdt_t *dev    = (i386ex_wdt_t *) priv;
    int           locked = !!(dev->status & WDTSTATUS_WDTEN);

    i386ex_wdt_log("386EX WDT: [%04X] = %04X\n", port, val);

    if ((port & ~1) != WDTCLR)
        dev->lockout = 0;

    switch (port & ~1) {
        case WDTRLDH:
            if (!locked)
                dev->reload = (dev->reload & 0x0000ffff) | (val << 16);
            break;
        case WDTRLDL:
            if (!locked)
                dev->reload = (dev->reload & 0xffff0000) | val;
            break;
        case WDTCLR:
            /* The lockout sequence enables watchdog mode, and reloads the counter in it. */
            if (dev->lockout && (val == WDT_LOCKOUT_2)) {
                dev->status = WDTSTATUS_WDTEN;
                i386ex_wdt_reload(dev);
            }
            dev->lockout = (val == WDT_LOCKOUT_1);
            break;
        case WDTSTATUS:
            if (!locked) {
                dev->status = val & (WDTSTATUS_BUSMON | WDTSTATUS_CLKDIS);
                i386ex_wdt_update(dev);
            }
            break;

        default:
            break;
    }
}

static void
i386ex_wdt_write(uint16_t port, uint8_t val, void *priv)
{
    i386ex_wdt_t *dev = (i386ex_wdt_t *) priv;
    uint16_t      old;

    /* The lockout sequence has to be word writes. */
    if ((port & ~1) == WDTCLR) {
        dev->lockout = 0;
        return;
    }

    old = i386ex_wdt_readw(port & ~1, priv);
    if (port & 1)
        i386ex_wdt_writew(port & ~1, (old & 0x00ff) | (val << 8), priv);
    else
        i386ex_wdt_writew(port, (old & 0xff00) | val, priv);
}

void
i386ex_wdt_handler(i386ex_wdt_t *dev, int set)
{
    io_handler(set, WDTRLDH, 12,
               i386ex_wdt_read, i386ex_wdt_readw, NULL,
               i386ex_wdt_write, i386ex_wdt_writew, NULL, dev);
}

void
i386ex_wdt_set_out(i386ex_wdt_t *dev, void (*func)(void *priv), void *priv)
{
    dev->out_func = func;
    dev->out_priv = priv;
}

static void *
i386ex_wdt_init(UNUSED(const device_t *info))
{
    i386ex_wdt_t *dev = (i386ex_wdt_t *) calloc(1, sizeof(i386ex_wdt_t));

    dev->reload = dev->count = WDT_RESET_VALUE;

    timer_add(&dev->timer, i386ex_wdt_timeout, dev, 0);
    i386ex_wdt_update(dev);

    return dev;
}

static void
i386ex_wdt_close(void *priv)
{
    free(priv);
}

const device_t i386ex_wdt_device = {
    .name          = "Intel 386EX Watchdog Timer Unit",
    .internal_name = "i386ex_wdt",
    .flags         = 0,
    .local         = 0,
    .init          = i386ex_wdt_init,
    .close         = i386ex_wdt_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

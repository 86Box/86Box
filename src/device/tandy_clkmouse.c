/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tandy 2000 DIGI-Mouse/Clock Controller Board (26-5144).
 *
 * Authors: Curt Coder
 *          WNT50
 *
 *          Copyright 2010-2026 Curt Coder.
 *          Copyright 2026 WNT50.
 */
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/nvr.h>
#include <86box/mouse.h>
#include <86box/plat_unused.h>

/* The board decodes only the top four bytes of the PCS5* range. */
#define TANDY_CLKMOUSE_IRQ  (1 << 10) /* second 8259, IR2 */
#define TANDY_CLKMOUSE_PORT 0x02fc

/* Status register (2FEh): bit 0 = byte ready, bit 1 = busy, bits 4-6 = buttons
   (active low), bit 7 = battery failed. Bit 1 is never driven: commands finish
   immediately. Bit 7 reports a lost battery once, like VRT bit of AT RTC. */
#define STATUS_RXRDY 0x01 /* a byte is waiting in the data port */
#define STATUS_BTN1  0x10 /* active low */
#define STATUS_BTN2  0x20 /* active low */
#define STATUS_BTN3  0x40 /* active low, required by TANMOUSE.DRV */
#define STATUS_BATT  0x80 /* battery failed, clock contents are lost */

/* Battery-backed state: magic, the board clock and the matching host time. */
#define TANDY_CLKMOUSE_NVR_MAGIC "TCM1"
#define TANDY_CLKMOUSE_NVR_SIZE  32

typedef struct tandy_clkmouse_t {
    uint8_t queue[8];
    int     queue_pos;
    int     queue_len;

    uint8_t cmd;
    uint8_t param[4];
    int     cmd_active;
    int     nparam;

    uint8_t mouse_enabled;
    int     last_buttons;

    int64_t clk_offset; /* guest clock minus the board's own clock, seconds */
    time_t  clk_base;   /* board's wall clock when this session started */
    time_t  host_base;  /* host clock at the same moment */

    nvr_t   nvr;
    uint8_t battery_failed; /* still owes the guest one battery warning */

    pc_timer_t irq_timer;
} tandy_clkmouse_t;

#ifdef ENABLE_TANDY_CLKMOUSE_LOG
int tandy_clkmouse_do_log = ENABLE_TANDY_CLKMOUSE_LOG;

static void
tandy_clkmouse_log(const char *fmt, ...)
{
    va_list ap;

    if (tandy_clkmouse_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define tandy_clkmouse_log(fmt, ...)
#endif

static int
bcd_to_bin(uint8_t val)
{
    return ((val >> 4) & 0x0f) * 10 + (val & 0x0f);
}

static uint8_t
bin_to_bcd(int val)
{
    return (uint8_t) (((val / 10) << 4) | (val % 10));
}

static void
tandy_clkmouse_push(tandy_clkmouse_t *dev, uint8_t val)
{
    if (dev->queue_len >= (int) sizeof(dev->queue))
        return;

    dev->queue[(dev->queue_pos + dev->queue_len) % (int) sizeof(dev->queue)] = val;
    dev->queue_len++;
}

static void
tandy_clkmouse_irq(UNUSED(void *priv))
{
    picint(TANDY_CLKMOUSE_IRQ);
}

static void
tandy_clkmouse_raise(void)
{
    /* The 8041 raises one interrupt per byte; the edge has to be re-created
       for every byte, so the line is pulsed rather than held. */
    picintc(TANDY_CLKMOUSE_IRQ);
    picint(TANDY_CLKMOUSE_IRQ);
}

static void
tandy_clkmouse_get_tm(const time_t *t, struct tm *tm)
{
#ifdef _WIN32
    if (time_sync & TIME_SYNC_UTC)
        gmtime_s(tm, t);
    else
        localtime_s(tm, t);
#else
    if (time_sync & TIME_SYNC_UTC)
        gmtime_r(t, tm);
    else
        localtime_r(t, tm);
#endif
}

static time_t
tandy_clkmouse_make_time(struct tm *tm)
{
#ifdef _WIN32
    return (time_sync & TIME_SYNC_UTC) ? _mkgmtime(tm) : mktime(tm);
#else
    return (time_sync & TIME_SYNC_UTC) ? timegm(tm) : mktime(tm);
#endif
}

/* The board keeps its own clock; it only follows the host when sync is on. */
static time_t
tandy_clkmouse_now(tandy_clkmouse_t *dev)
{
    return dev->clk_base + (time(NULL) - dev->host_base) + (time_t) dev->clk_offset;
}

static void
tandy_clkmouse_put_i64(uint8_t *p, int64_t val)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t) (val >> (i << 3));
}

static int64_t
tandy_clkmouse_get_i64(const uint8_t *p)
{
    int64_t val = 0;

    for (int i = 7; i >= 0; i--)
        val = (val << 8) | p[i];

    return val;
}

/* The saved pair only needs to stay self-consistent: the difference between
   the two stamps is how long the board has been running. */
static void
tandy_clkmouse_snapshot(tandy_clkmouse_t *dev)
{
    memcpy(dev->nvr.regs, TANDY_CLKMOUSE_NVR_MAGIC, 4);
    tandy_clkmouse_put_i64(&dev->nvr.regs[12], (int64_t) time(NULL));
    tandy_clkmouse_put_i64(&dev->nvr.regs[4], (int64_t) tandy_clkmouse_now(dev));

    nvr_dosave = 1;
}

static void
tandy_clkmouse_nvr_reset(nvr_t *nvr)
{
    memset(nvr->regs, 0x00, nvr->size);
}

static void
tandy_clkmouse_nvr_start(nvr_t *nvr)
{
    tandy_clkmouse_t *dev   = (tandy_clkmouse_t *) nvr->data;
    const time_t      now   = time(NULL);
    const int         valid = (memcmp(nvr->regs, TANDY_CLKMOUSE_NVR_MAGIC, 4) == 0);

    dev->host_base      = now;
    dev->clk_offset     = 0;
    dev->battery_failed = !valid;

    /* The battery keeps the clock counting while the machine is off. */
    const time_t battery = valid ? (time_t) (tandy_clkmouse_get_i64(&nvr->regs[4])
                                             + (int64_t) (now - (time_t) tandy_clkmouse_get_i64(&nvr->regs[12])))
                                 : 0;

    if (time_sync & TIME_SYNC_ENABLED) {
        /* intclk already carries the fixed year or year offset applied by
           the core, so seed from it instead of the raw host clock. */
        struct tm tm = { 0 };

        if (time_keep_date && valid) {
            /* Keep the board's own date. */
            tandy_clkmouse_get_tm(&battery, &tm);
            nvr_time_set_date(&tm);
        }
        nvr_time_get(&tm);
        tm.tm_isdst   = -1;
        dev->clk_base = tandy_clkmouse_make_time(&tm);
    } else if (valid) {
        dev->clk_base = battery;
    } else {
        struct tm tm = { 0 };

        tm.tm_year = 80;
        tm.tm_mon  = 0;
        tm.tm_mday = 1;
        dev->clk_base = tandy_clkmouse_make_time(&tm);
    }

    tandy_clkmouse_snapshot(dev);
}

static void
tandy_clkmouse_read_time(tandy_clkmouse_t *dev)
{
    const time_t t = tandy_clkmouse_now(dev);
    struct tm    tm;

    tandy_clkmouse_get_tm(&t, &tm);

    tandy_clkmouse_log("CLKMOUSE: read time %02i:%02i %02i/%02i\n",
                       tm.tm_hour, tm.tm_min, tm.tm_mday, tm.tm_mon + 1);

    tandy_clkmouse_push(dev, 'R');
    tandy_clkmouse_push(dev, bin_to_bcd(tm.tm_min));
    tandy_clkmouse_push(dev, bin_to_bcd(tm.tm_hour));
    tandy_clkmouse_push(dev, bin_to_bcd(tm.tm_mday));
    tandy_clkmouse_push(dev, bin_to_bcd(tm.tm_mon + 1));

    /* CLOCKGET and CLOCKSET fetch the bytes with interrupts disabled,
       so the clock never uses the interrupt line the mouse driver owns. */
}

static void
tandy_clkmouse_set_time(tandy_clkmouse_t *dev)
{
    const time_t now = time(NULL);
    struct tm    tm;

    tandy_clkmouse_get_tm(&now, &tm);
    tm.tm_sec  = 0;
    tm.tm_min  = bcd_to_bin(dev->param[0]);
    tm.tm_hour = bcd_to_bin(dev->param[1]);
    tm.tm_mday = bcd_to_bin(dev->param[2]);
    tm.tm_mon  = bcd_to_bin(dev->param[3]) - 1;

    const time_t guest = tandy_clkmouse_make_time(&tm);
    if (guest != (time_t) -1)
        dev->clk_offset = (int64_t) guest - (int64_t) tandy_clkmouse_now(dev);

    tandy_clkmouse_snapshot(dev);

    tandy_clkmouse_log("CLKMOUSE: set time %02i:%02i %02i/%02i\n",
                       tm.tm_hour, tm.tm_min, tm.tm_mday, tm.tm_mon + 1);
}

static void
tandy_clkmouse_exec(tandy_clkmouse_t *dev)
{
    switch (dev->cmd) {
        case 0x01: /* set time: minute, hour, day, month, packed BCD */
            if (dev->nparam >= 4)
                tandy_clkmouse_set_time(dev);
            break;

        case 0x02: /* read time */
            tandy_clkmouse_read_time(dev);
            break;

        case 0x08: /* interrupt enables */
            break;

        case 0x20: /* start/stop sampling */
            dev->mouse_enabled = (dev->nparam > 0) && (dev->param[0] != 0);
            dev->last_buttons  = mouse_get_buttons_ex();
            tandy_clkmouse_log("CLKMOUSE: sampling %s\n",
                                 dev->mouse_enabled ? "on" : "off");
            break;

        default:
            tandy_clkmouse_log("CLKMOUSE: unknown command %02X\n", dev->cmd);
            break;
    }

    dev->cmd_active = 0;
    dev->nparam     = 0;
}

static uint8_t
tandy_clkmouse_read(uint16_t addr, void *priv)
{
    tandy_clkmouse_t *dev = (tandy_clkmouse_t *) priv;
    uint8_t             ret = 0xff;

    switch (addr & 0x0003) {
        case 0x00:
            if (dev->queue_len > 0) {
                ret        = dev->queue[dev->queue_pos];
                dev->queue_pos = (dev->queue_pos + 1) % (int) sizeof(dev->queue);
                dev->queue_len--;
            }
            tandy_clkmouse_log("CLKMOUSE: read %02X (%i left)\n", ret, dev->queue_len);

            picintc(TANDY_CLKMOUSE_IRQ);
            if (dev->queue_len > 0)
                timer_set_delay_u64(&dev->irq_timer, 1000 * TIMER_USEC);
            else
                timer_disable(&dev->irq_timer);
            break;

        case 0x01:
            ret = 0x00;
            break;

        case 0x02: {
            const int buttons = mouse_get_buttons_ex();

            ret = 0x00;
            if (dev->queue_len > 0)
                ret |= STATUS_RXRDY;
            if (!(buttons & 0x01))
                ret |= STATUS_BTN1;
            if (!(buttons & 0x02))
                ret |= STATUS_BTN2;
            /* TANMOUSE.DRV probes the board with (status & 70h) == 70h. */
            ret |= STATUS_BTN3;
            if (dev->battery_failed) {
                /* Report the loss once, then behave as if the battery were
                   good again, just like the VRT bit of the AT RTC. */
                ret |= STATUS_BATT;
                dev->battery_failed = 0;
            }
            if (dev->queue_len > 0)
                tandy_clkmouse_log("CLKMOUSE: status %02X (%i queued)\n", ret, dev->queue_len);
            break;
        }

        case 0x03:
            ret = 0x00;
            break;
    }

    return ret;
}

static void
tandy_clkmouse_write(uint16_t addr, uint8_t val, void *priv)
{
    tandy_clkmouse_t *dev = (tandy_clkmouse_t *) priv;

    switch (addr & 0x0003) {
        case 0x00:
            if (!dev->cmd_active) {
                dev->cmd        = val;
                dev->nparam     = 0;
                dev->cmd_active = 1;
            } else if (dev->nparam < 4)
                dev->param[dev->nparam++] = val;
            break;

        case 0x01:
            break;

        case 0x02:
            /* The last parameter byte executes the command. */
            if (!dev->cmd_active) {
                dev->cmd        = val;
                dev->nparam     = 0;
                dev->cmd_active = 1;
            } else if (dev->nparam < 4)
                dev->param[dev->nparam++] = val;

            tandy_clkmouse_log("CLKMOUSE: command %02X\n", dev->cmd);
            tandy_clkmouse_exec(dev);
            break;

        case 0x03:
            tandy_clkmouse_log("CLKMOUSE: reset\n");
            dev->cmd_active = 0;
            dev->queue_pos  = 0;
            dev->queue_len  = 0;
            dev->nparam     = 0;
            timer_disable(&dev->irq_timer);
            picintc(TANDY_CLKMOUSE_IRQ);
            mouse_clear_coords();
            break;
    }
}

static int
tandy_clkmouse_poll(void *priv)
{
    tandy_clkmouse_t *dev = (tandy_clkmouse_t *) priv;
    const int           buttons = mouse_get_buttons_ex();
    int                 dx, dy;

    if (!dev->mouse_enabled || (dev->queue_len > 0))
        return 0;

    if (!mouse_moved()) {
        if (buttons == dev->last_buttons)
            return 0;

        dev->last_buttons = buttons;
        tandy_clkmouse_push(dev, 'B');
        tandy_clkmouse_raise();
        return 0;
    }

    mouse_subtract_x(&dx, NULL, -32768, 32767, 0);
    mouse_subtract_y(&dy, NULL, -32768, 32767, 0, 0);
    mouse_clear_x();
    mouse_clear_y();

    dev->last_buttons = buttons;
    tandy_clkmouse_log("CLKMOUSE: move %i %i\n", dx, dy);

    tandy_clkmouse_push(dev, 'A');
    tandy_clkmouse_push(dev, (uint8_t) (dx & 0xff));
    tandy_clkmouse_push(dev, (uint8_t) ((dx >> 8) & 0xff));
    tandy_clkmouse_push(dev, (uint8_t) (dy & 0xff));
    tandy_clkmouse_push(dev, (uint8_t) ((dy >> 8) & 0xff));

    tandy_clkmouse_raise();

    return 0;
}

static void *
tandy_clkmouse_init(const device_t *info)
{
    tandy_clkmouse_t *dev = (tandy_clkmouse_t *) calloc(1, sizeof(tandy_clkmouse_t));

    tandy_clkmouse_log("CLKMOUSE: init\n");

    io_sethandler(TANDY_CLKMOUSE_PORT, 4,
                  tandy_clkmouse_read, NULL, NULL,
                  tandy_clkmouse_write, NULL, NULL, dev);

    timer_add(&dev->irq_timer, tandy_clkmouse_irq, dev, 1);

    dev->nvr.data  = dev;
    dev->nvr.size  = TANDY_CLKMOUSE_NVR_SIZE;
    dev->nvr.reset = tandy_clkmouse_nvr_reset;
    dev->nvr.start = tandy_clkmouse_nvr_start;
    nvr_init(&dev->nvr);

    /* The machine only adds this when internal mouse is selected. */
    mouse_reset();
    mouse_set_buttons(2);

    /* The 8041 samples the quadrature inputs 40 times per second. */
    mouse_set_sample_rate(40.0);
    mouse_set_poll(tandy_clkmouse_poll, dev);

    return dev;
}

static void
tandy_clkmouse_close(void *priv)
{
    tandy_clkmouse_t *dev = (tandy_clkmouse_t *) priv;

    timer_disable(&dev->irq_timer);
    picintc(TANDY_CLKMOUSE_IRQ);

    io_removehandler(TANDY_CLKMOUSE_PORT, 4,
                     tandy_clkmouse_read, NULL, NULL,
                     tandy_clkmouse_write, NULL, NULL, dev);

    free(dev);
}

static void
tandy_clkmouse_reset(void *priv)
{
    tandy_clkmouse_t *dev = (tandy_clkmouse_t *) priv;

    /* clk_offset survives a reset: the battery keeps the clock running. */
    dev->mouse_enabled = 0;
    dev->last_buttons  = 0;
    dev->cmd_active    = 0;
    dev->queue_pos     = 0;
    dev->queue_len     = 0;
    dev->nparam        = 0;

    timer_disable(&dev->irq_timer);
    picintc(TANDY_CLKMOUSE_IRQ);
}

const device_t tandy_clkmouse_device = {
    .name          = "Tandy 2000 DIGI-Mouse/Clock Controller Board",
    .internal_name = "tandy_clkmouse",
    .flags         = 0,
    .local         = 0,
    .init          = tandy_clkmouse_init,
    .close         = tandy_clkmouse_close,
    .reset         = tandy_clkmouse_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

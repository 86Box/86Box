/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Olivetti XT-compatible machines.
 *
 *          - Supports MM58174 real-time clock emulation (M24)
 *          - Supports MM58274 real-time clock emulation (M240)
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          EngiNerd <webmaster.crrc@yahoo.it>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2020 EngiNerd.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/hdc.h>
#include <86box/port_6x.h>
#include <86box/serial.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/machine.h>
#include <86box/vid_cga.h>
#include <86box/vid_ogc.h>
#include <86box/vid_colorplus.h>
#include <86box/vid_cga_comp.h>
#include <86box/plat_unused.h>

#define STAT_PARITY       0x80
#define STAT_RTIMEOUT     0x40
#define STAT_TTIMEOUT     0x20
#define STAT_LOCK         0x10
#define STAT_CD           0x08
#define STAT_SYSFLAG      0x04
#define STAT_IFULL        0x02
#define STAT_OFULL        0x01

#define PLANTRONICS_MODE  1
#define OLIVETTI_OGC_MODE 0

#define CGA_RGB           0
#define CGA_COMPOSITE     1

enum MM58174_ADDR {
    /* Registers */
    MM58174_TEST,     /* TEST register, write only */
    MM58174_TENTHS,   /* Tenths of second, read only */
    MM58174_SECOND1,  /* Units of seconds, read only */
    MM58174_SECOND10, /* Tens of seconds, read only */
    MM58174_MINUTE1,
    MM58174_MINUTE10,
    MM58174_HOUR1,
    MM58174_HOUR10,
    MM58174_DAY1,
    MM58174_DAY10,
    MM58174_WEEKDAY,
    MM58174_MONTH1,
    MM58174_MONTH10,
    MM58174_LEAPYEAR, /* Leap year status, write only */
    MM58174_RESET,    /* RESET register, write only */
    MM58174_IRQ       /* Interrupt register, read / write */
};

enum MM58274_ADDR {
    /* Registers */
    MM58274_CONTROL, /* Control register */
    MM58274_TENTHS,  /* Tenths of second, read only */
    MM58274_SECOND1,
    MM58274_SECOND10,
    MM58274_MINUTE1,
    MM58274_MINUTE10,
    MM58274_HOUR1,
    MM58274_HOUR10,
    MM58274_DAY1,
    MM58274_DAY10,
    MM58274_MONTH1,
    MM58274_MONTH10,
    MM58274_YEAR1,
    MM58274_YEAR10,
    MM58274_WEEKDAY,
    MM58274_SETTINGS /* Settings register */
};

static struct tm intclk;

typedef struct m24_kbd_t {
    /* Keyboard stuff. */
    int     wantirq;
    uint8_t command;
    uint8_t status;
    uint8_t out;
    uint8_t output_port;
    uint8_t id;
    int     param;
    int     param_total;
    uint8_t params[16];
    uint8_t scan[7];

    /* Mouse stuff. */
    int        mouse_input_mode;
    int        b;

    pc_timer_t send_delay_timer;
} m24_kbd_t;

typedef struct m19_vid_t {
    ogc_t       ogc;
    colorplus_t colorplus;
    int         mode;
} m19_vid_t;

static uint8_t key_queue[16];
static int     key_queue_start = 0;
static int     key_queue_end   = 0;

video_timings_t timing_m19_vid = { VIDEO_ISA, 8, 16, 32, 8, 16, 32 };

const device_t m19_vid_device;

#ifdef ENABLE_XT_OLIVETTI_LOG
int xt_olivetti_do_log = ENABLE_XT_OLIVETTI_LOG;

static void
xt_olivetti_log(const char *fmt, ...)
{
    va_list ap;

    if (xt_olivetti_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define xt_olivetti_log(fmt, ...)
#endif

/* Set the chip time. */
static void
mm58174_time_set(uint8_t *regs, struct tm *tm)
{
    regs[MM58174_SECOND1]  = (tm->tm_sec % 10);
    regs[MM58174_SECOND10] = (tm->tm_sec / 10);
    regs[MM58174_MINUTE1]  = (tm->tm_min % 10);
    regs[MM58174_MINUTE10] = (tm->tm_min / 10);
    regs[MM58174_HOUR1]    = (tm->tm_hour % 10);
    regs[MM58174_HOUR10]   = (tm->tm_hour / 10);
    regs[MM58174_WEEKDAY]  = (tm->tm_wday + 1);
    regs[MM58174_DAY1]     = (tm->tm_mday % 10);
    regs[MM58174_DAY10]    = (tm->tm_mday / 10);
    regs[MM58174_MONTH1]   = ((tm->tm_mon + 1) % 10);
    regs[MM58174_MONTH10]  = ((tm->tm_mon + 1) / 10);
    /* MM58174 does not store the year, M24 uses the IRQ register to count 8 years from leap year */
    regs[MM58174_IRQ]      = ((tm->tm_year + 1900) % 8);
    regs[MM58174_LEAPYEAR] = 8 >> ((regs[MM58174_IRQ] & 0x07) & 0x03);
}

/* Get the chip time. */
#define nibbles(a) (regs[(a##1)] + 10 * regs[(a##10)])
static void
mm58174_time_get(uint8_t *regs, struct tm *tm)
{
    tm->tm_sec  = nibbles(MM58174_SECOND);
    tm->tm_min  = nibbles(MM58174_MINUTE);
    tm->tm_hour = nibbles(MM58174_HOUR);
    tm->tm_wday = (regs[MM58174_WEEKDAY] - 1);
    tm->tm_mday = nibbles(MM58174_DAY);
    tm->tm_mon  = (nibbles(MM58174_MONTH) - 1);
    /* MM58174 does not store the year */
    tm->tm_year = (1984 + (regs[MM58174_IRQ] & 0x07) - 1900);
}

/* One more second has passed, update the internal clock. */
static void
mm58x74_recalc(void)
{
    /* Ping the internal clock. */
    if (++intclk.tm_sec == 60) {
        intclk.tm_sec = 0;
        if (++intclk.tm_min == 60) {
            intclk.tm_min = 0;
            if (++intclk.tm_hour == 24) {
                intclk.tm_hour = 0;
                if (++intclk.tm_mday == (nvr_get_days(intclk.tm_mon, intclk.tm_year) + 1)) {
                    intclk.tm_mday = 1;
                    if (++intclk.tm_mon == 13) {
                        intclk.tm_mon = 1;
                        intclk.tm_year++;
                    }
                }
            }
        }
    }
}

/* This is called every second through the NVR/RTC hook. */
static void
mm58174_tick(nvr_t *nvr)
{
    mm58x74_recalc();
    mm58174_time_set(nvr->regs, &intclk);
}

static void
mm58174_start(nvr_t *nvr)
{
    struct tm tm;

    /* Initialize the internal and chip times. */
    if (time_sync & TIME_SYNC_ENABLED) {
        /* Use the internal clock's time. */
        nvr_time_get(&tm);
        mm58174_time_set(nvr->regs, &tm);
    } else {
        /* Set the internal clock from the chip time. */
        mm58174_time_get(nvr->regs, &tm);
        nvr_time_set(&tm);
    }
    mm58174_time_get(nvr->regs, &intclk);
}

/* Write to one of the chip registers. */
static void
mm58174_write(uint16_t addr, uint8_t val, void *priv)
{
    nvr_t *nvr = (nvr_t *) priv;

    addr &= 0x0f;
    val &= 0x0f;

    /* Update non-read-only changed values if not synchronizing time to host */
    if ((addr != MM58174_TENTHS) && (addr != MM58174_SECOND1) && (addr != MM58174_SECOND10))
        if ((nvr->regs[addr] != val) && !(time_sync & TIME_SYNC_ENABLED))
            nvr_dosave = 1;

    if ((addr == MM58174_RESET) && (val & 0x01)) {
        /* When timer starts, MM58174 sets seconds and tenths of second to 0 */
        nvr->regs[MM58174_TENTHS] = 0;
        if (!(time_sync & TIME_SYNC_ENABLED)) {
            /* Only set seconds to 0 if not synchronizing time to host clock */
            nvr->regs[MM58174_SECOND1]  = 0;
            nvr->regs[MM58174_SECOND10] = 0;
        }
    }

    /* Store the new value */
    nvr->regs[addr] = val;

    /* Update internal clock with MM58174 time */
    mm58174_time_get(nvr->regs, &intclk);
}

/* Read from one of the chip registers. */
static uint8_t
mm58174_read(uint16_t addr, void *priv)
{
    nvr_t *nvr = (nvr_t *) priv;

    addr &= 0x0f;

    /* Set IRQ control bit to 0 upon read */
    if (addr == 0x0f)
        nvr->regs[addr] &= 0x07;

    /* Grab and return the desired value */
    return (nvr->regs[addr]);
}

/* Reset the MM58174 to a default state. */
static void
mm58174_reset(nvr_t *nvr)
{
    /* Clear the NVRAM. */
    memset(nvr->regs, 0xff, nvr->size);

    /* Reset the RTC registers. */
    memset(nvr->regs, 0x00, 16);
    nvr->regs[MM58174_WEEKDAY] = 0x01;
    nvr->regs[MM58174_DAY1]    = 0x01;
    nvr->regs[MM58174_MONTH1]  = 0x01;
}

static void
mm58174_init(nvr_t *nvr, int size)
{
    /* This is machine specific. */
    nvr->size = size;
    nvr->irq  = -1;

    /* Set up any local handlers here. */
    nvr->reset = mm58174_reset;
    nvr->start = mm58174_start;
    nvr->tick  = mm58174_tick;

    /* Initialize the actual NVR. */
    nvr_init(nvr);

    io_sethandler(0x0070, 16,
                  mm58174_read, NULL, NULL, mm58174_write, NULL, NULL, nvr);
}

/* Set the chip time. */
static void
mm58274_time_set(uint8_t *regs, struct tm *tm)
{
    regs[MM58274_SECOND1]  = (tm->tm_sec % 10);
    regs[MM58274_SECOND10] = (tm->tm_sec / 10);
    regs[MM58274_MINUTE1]  = (tm->tm_min % 10);
    regs[MM58274_MINUTE10] = (tm->tm_min / 10);
    regs[MM58274_HOUR1]    = (tm->tm_hour % 10);
    regs[MM58274_HOUR10]   = (tm->tm_hour / 10);
    /* Store hour in 24-hour or 12-hour mode */
    if (regs[MM58274_SETTINGS] & 0x01) {
        regs[MM58274_HOUR1]  = (tm->tm_hour % 10);
        regs[MM58274_HOUR10] = (tm->tm_hour / 10);
    } else {
        regs[MM58274_HOUR1]  = ((tm->tm_hour % 12) % 10);
        regs[MM58274_HOUR10] = ((tm->tm_hour % 12) / 10);
        if (tm->tm_hour >= 12)
            regs[MM58274_SETTINGS] |= 0x04;
        else
            regs[MM58274_SETTINGS] &= 0x0B;
    }
    regs[MM58274_WEEKDAY] = (tm->tm_wday + 1);
    regs[MM58274_DAY1]    = (tm->tm_mday % 10);
    regs[MM58274_DAY10]   = (tm->tm_mday / 10);
    regs[MM58274_MONTH1]  = ((tm->tm_mon + 1) % 10);
    regs[MM58274_MONTH10] = ((tm->tm_mon + 1) / 10);
    /* MM58274 can store 00 to 99 years but M240 uses the YEAR1 register to count 8 years from leap year */
    regs[MM58274_YEAR1] = ((tm->tm_year + 1900) % 8);
    /* Keep bit 0 and 1 12-hour / 24-hour and AM / PM */
    regs[MM58274_SETTINGS] &= 0x03;
    /* Set leap counter bits 2 and 3 */
    regs[MM58274_SETTINGS] += (4 * (regs[MM58274_YEAR1] & 0x03));
}

/* Get the chip time. */
static void
mm58274_time_get(uint8_t *regs, struct tm *tm)
{
    tm->tm_sec = nibbles(MM58274_SECOND);
    tm->tm_min = nibbles(MM58274_MINUTE);
    /* Read hour in 24-hour or 12-hour mode */
    if (regs[MM58274_SETTINGS] & 0x01)
        tm->tm_hour = nibbles(MM58274_HOUR);
    else
        tm->tm_hour = ((nibbles(MM58274_HOUR) % 12) + (regs[MM58274_SETTINGS] & 0x04) ? 12 : 0);
    tm->tm_wday = (regs[MM58274_WEEKDAY] - 1);
    tm->tm_mday = nibbles(MM58274_DAY);
    tm->tm_mon  = (nibbles(MM58274_MONTH) - 1);
    /* MM58274 can store 00 to 99 years but M240 uses the YEAR1 register to count 8 years from leap year */
    tm->tm_year = (1984 + regs[MM58274_YEAR1] - 1900);
}

/* This is called every second through the NVR/RTC hook. */
static void
mm58274_tick(nvr_t *nvr)
{
    mm58x74_recalc();
    mm58274_time_set(nvr->regs, &intclk);
}

static void
mm58274_start(nvr_t *nvr)
{
    struct tm tm;

    /* Initialize the internal and chip times. */
    if (time_sync & TIME_SYNC_ENABLED) {
        /* Use the internal clock's time. */
        nvr_time_get(&tm);
        mm58274_time_set(nvr->regs, &tm);
    } else {
        /* Set the internal clock from the chip time. */
        mm58274_time_get(nvr->regs, &tm);
        nvr_time_set(&tm);
    }
    mm58274_time_get(nvr->regs, &intclk);
}

/* Write to one of the chip registers. */
static void
mm58274_write(uint16_t addr, uint8_t val, void *priv)
{
    nvr_t *nvr = (nvr_t *) priv;

    addr &= 0x0f;
    val &= 0x0f;

    /* Update non-read-only changed values if not synchronizing time to host */
    if (addr != MM58274_TENTHS)
        if ((nvr->regs[addr] != val) && !(time_sync & TIME_SYNC_ENABLED))
            nvr_dosave = 1;

    if ((addr == MM58274_CONTROL) && (val & 0x04)) {
        /* When timer starts, MM58274 sets tenths of second to 0 */
        nvr->regs[MM58274_TENTHS] = 0;
    }

    /* Store the new value */
    nvr->regs[addr] = val;

    /* Update internal clock with MM58274 time */
    mm58274_time_get(nvr->regs, &intclk);
}

/* Read from one of the chip registers. */
static uint8_t
mm58274_read(uint16_t addr, void *priv)
{
    const nvr_t *nvr = (nvr_t *) priv;

    addr &= 0x0f;

    /* Grab and return the desired value */
    return (nvr->regs[addr]);
}

/* Reset the MM58274 to a default state. */
static void
mm58274_reset(nvr_t *nvr)
{
    /* Clear the NVRAM. */
    memset(nvr->regs, 0xff, nvr->size);

    /* Reset the RTC registers. */
    memset(nvr->regs, 0x00, 16);
    nvr->regs[MM58274_WEEKDAY]  = 0x01;
    nvr->regs[MM58274_DAY1]     = 0x01;
    nvr->regs[MM58274_MONTH1]   = 0x01;
    nvr->regs[MM58274_SETTINGS] = 0x01;
}

static void
mm58274_init(nvr_t *nvr, int size)
{
    /* This is machine specific. */
    nvr->size = size;
    nvr->irq  = -1;

    /* Set up any local handlers here. */
    nvr->reset = mm58274_reset;
    nvr->start = mm58274_start;
    nvr->tick  = mm58274_tick;

    /* Initialize the actual NVR. */
    nvr_init(nvr);

    io_sethandler(0x0070, 16,
                  mm58274_read, NULL, NULL, mm58274_write, NULL, NULL, nvr);
}

static void
m24_kbd_poll(void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *) priv;

    timer_advance_u64(&m24_kbd->send_delay_timer, 1000 * TIMER_USEC);
    if (m24_kbd->wantirq) {
        m24_kbd->wantirq = 0;
        picint(2);
        xt_olivetti_log("M24: take IRQ\n");
    }

    if (!(m24_kbd->status & STAT_OFULL) && key_queue_start != key_queue_end) {
        xt_olivetti_log("Reading %02X from the key queue at %i\n",
                        m24_kbd->out, key_queue_start);
        m24_kbd->out    = key_queue[key_queue_start];
        key_queue_start = (key_queue_start + 1) & 0xf;
        m24_kbd->status |= STAT_OFULL;
        m24_kbd->status &= ~STAT_IFULL;
        m24_kbd->wantirq = 1;
    }
}

static void
m24_kbd_adddata(uint16_t val)
{
    key_queue[key_queue_end] = val;
    key_queue_end            = (key_queue_end + 1) & 0xf;
}

static void
m24_kbd_adddata_ex(uint16_t val)
{
    kbd_adddata_process(val, m24_kbd_adddata);
}

/*
   From the Olivetti M21/M24 Theory of Operation:

   Port   Function
   ----   --------
   60h    Keyboard 8041 Data Transfer Read/Write
   61h    Control Port A Read/Write
   62h    Control Port B Read
   63h    Not Used
   64h    Keyboard 8041 Command/Status
   65h    Communications Port Read
   66h    System Configuration Read
   67h    System Configuration Read
 */
static void
m24_kbd_write(uint16_t port, uint8_t val, void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *) priv;
    uint8_t    ret;

    xt_olivetti_log("M24: write %04X %02X\n", port, val);

    switch (port) {
        case 0x60:
            m24_kbd->status &= ~STAT_CD;

            if (m24_kbd->param != m24_kbd->param_total) {
                m24_kbd->params[m24_kbd->param++] = val;
                if (m24_kbd->param == m24_kbd->param_total) {
                    switch (m24_kbd->command) {
                        case 0x11:
                            m24_kbd->mouse_input_mode = 0;
                            m24_kbd->scan[0]    = m24_kbd->params[0];
                            m24_kbd->scan[1]    = m24_kbd->params[1];
                            m24_kbd->scan[2]    = m24_kbd->params[2];
                            m24_kbd->scan[3]    = m24_kbd->params[3];
                            m24_kbd->scan[4]    = m24_kbd->params[4];
                            m24_kbd->scan[5]    = m24_kbd->params[5];
                            m24_kbd->scan[6]    = m24_kbd->params[6];
                            break;

                        case 0x12:
                            m24_kbd->mouse_input_mode = 1;
                            m24_kbd->scan[0]    = m24_kbd->params[0];
                            m24_kbd->scan[1]    = m24_kbd->params[1];
                            m24_kbd->scan[2]    = m24_kbd->params[2];
                            break;

                        default:
                            xt_olivetti_log("M24: bad keyboard command complete %02X\n", m24_kbd->command);
                    }
                }
            } else {
                m24_kbd->command = val;
                switch (val) {
                    /* 01: FD, 05: ANY ---> Customer test reports no keyboard.
                       01: AA, 05: 01 ---> Customer test reports 102 Deluxe keyboard.
                       01: AA, 05: 02 ---> Customer test reports 83-key keyboard.
                       01: AA, 05: 10 ---> Customer test reports M240 keyboard.
                       01: AA, 05: 20 ---> Customer test reports 101/102/key keyboard.
                       01: AA, 05: 40 or anything else ---> Customer test reports 101/102/key keyboard.

                       AA is the correct return for command 01, as confirmed by the M24 Customer Test. */
                    case 0x01: /*Self-test*/
                        m24_kbd_adddata(0xaa);
                        break;

                    case 0x02: /*Olivetti M240: Read SWB*/
                        /* SWB on mainboard (off=1)
                         * bit 7 - use BIOS HD on mainboard (on) / on controller (off)
                         * bit 6 - use OCG/CGA display adapter (on) / other display adapter (off)
                         */
                        ret = (hdc_current[0] == HDC_INTERNAL) ? 0x00 : 0x80;
                        ret |= video_is_cga() ? 0x40 : 0x00;

                        m24_kbd_adddata(ret);
                        break;

                    case 0x05: /*Read ID*/
                        ret = m24_kbd->id;
                        m24_kbd_adddata(ret);
                        break;

                    case 0x11:
                        m24_kbd->param       = 0;
                        m24_kbd->param_total = 9;
                        break;

                    case 0x12:
                        m24_kbd->param       = 0;
                        m24_kbd->param_total = 4;
                        break;

                    case 0x13: /*Sent by Olivetti M240 Customer Diagnostics*/
                        break;

                    default:
                        xt_olivetti_log("M24: bad keyboard command %02X\n", val);
                }
            }
            break;

        case 0x61:
            ppi.pb = val;

            speaker_update();
            speaker_gated  = val & 1;
            speaker_enable = val & 2;
            if (speaker_enable)
                was_speaker_enable = 1;
            pit_devs[0].set_gate(pit_devs[0].data, 2, val & 1);
            break;

        case 0x64:
            m24_kbd->status |= STAT_CD;

            if (val == 0x02)
                m24_kbd_adddata(0x00);
            break;

        default:
            break;
    }
}

extern uint8_t random_generate(void);

static uint8_t
m24_kbd_read(uint16_t port, void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *) priv;
    uint8_t    ret     = 0xff;

    switch (port) {
        case 0x60:
            ret = m24_kbd->out;
            if (key_queue_start == key_queue_end) {
                m24_kbd->status &= ~STAT_OFULL;
                m24_kbd->wantirq = 0;
            } else {
                m24_kbd->out    = key_queue[key_queue_start];
                key_queue_start = (key_queue_start + 1) & 0xf;
                m24_kbd->status |= STAT_OFULL;
                m24_kbd->status &= ~STAT_IFULL;
                m24_kbd->wantirq = 1;
            }
            break;

        case 0x61:
            /* MS-DOS 5.00 and higher's KEYB.COM freezes due to port 61h not having the
               AT refresh toggle, because for some reson it thinks the M24 is an AT.

               A German-language site confirms this also happens on real hardware.

               The M240 is not affected. */
            ret = ppi.pb;
            break;

        case 0x64:
            ret = m24_kbd->status & 0x0f;
            m24_kbd->status &= ~(STAT_RTIMEOUT | STAT_TTIMEOUT);
            break;

        default:
            xt_olivetti_log("\nBad M24 keyboard read %04X\n", port);
    }

    return ret;
}

static void
m24_kbd_close(void *priv)
{
    m24_kbd_t *kbd = (m24_kbd_t *) priv;

    /* Stop the timer. */
    timer_disable(&kbd->send_delay_timer);

    /* Disable scanning. */
    keyboard_scan = 0;

    keyboard_send = NULL;

    io_removehandler(0x0060, 2,
                     m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, kbd);
    io_removehandler(0x0064, 1,
                     m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, kbd);

    free(kbd);
}

static void
m24_kbd_reset(void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *) priv;

    /* Initialize the keyboard. */
    m24_kbd->status  = STAT_LOCK | STAT_CD;
    m24_kbd->wantirq = 0;
    keyboard_scan    = 1;
    m24_kbd->param = m24_kbd->param_total = 0;
    m24_kbd->mouse_input_mode                   = 0;
    m24_kbd->scan[0]                      = 0x1c;
    m24_kbd->scan[1]                      = 0x53;
    m24_kbd->scan[2]                      = 0x01;
    m24_kbd->scan[3]                      = 0x4b;
    m24_kbd->scan[4]                      = 0x4d;
    m24_kbd->scan[5]                      = 0x48;
    m24_kbd->scan[6]                      = 0x50;
}

static int
ms_poll(void *priv)
{
    m24_kbd_t *m24_kbd = (m24_kbd_t *) priv;
    int delta_x;
    int delta_y;
    int o_x;
    int o_y;
    int b = mouse_get_buttons_ex();

    if (((key_queue_end - key_queue_start) & 0xf) > 14)
        return 0xff;

    if ((b & 1) && !(m24_kbd->b & 1))
        m24_kbd_adddata(m24_kbd->scan[0]);
    if (!(b & 1) && (m24_kbd->b & 1))
        m24_kbd_adddata(m24_kbd->scan[0] | 0x80);
    m24_kbd->b = (m24_kbd->b & ~1) | (b & 1);

    if (((key_queue_end - key_queue_start) & 0xf) > 14)
        return 0xff;

    if ((b & 2) && !(m24_kbd->b & 2))
        m24_kbd_adddata(m24_kbd->scan[2]);
    if (!(b & 2) && (m24_kbd->b & 2))
        m24_kbd_adddata(m24_kbd->scan[2] | 0x80);
    m24_kbd->b = (m24_kbd->b & ~2) | (b & 2);

    if (((key_queue_end - key_queue_start) & 0xf) > 14)
        return 0xff;

    if ((b & 4) && !(m24_kbd->b & 4))
        m24_kbd_adddata(m24_kbd->scan[1]);
    if (!(b & 4) && (m24_kbd->b & 4))
        m24_kbd_adddata(m24_kbd->scan[1] | 0x80);
    m24_kbd->b = (m24_kbd->b & ~4) | (b & 4);

    if (m24_kbd->mouse_input_mode) {
        if (((key_queue_end - key_queue_start) & 0xf) > 12)
            return 0xff;

        if (!mouse_moved())
            return 0xff;

        mouse_subtract_coords(&delta_x, &delta_y, &o_x, &o_y, -127, 127, 1, 0);

        if ((delta_x == -127) && o_x)
            delta_x = 0x80 | ((-delta_x) & 0x7f);

        if ((delta_y == -127) && o_y)
            delta_y = 0x80 | ((-delta_y) & 0x7f);

        m24_kbd_adddata(0xfe);
        m24_kbd_adddata(delta_x);
        m24_kbd_adddata(delta_y);
    } else {
        mouse_subtract_coords(&delta_x, &delta_y, &o_x, &o_y, -127, 127, 1, 0);

        while (delta_x < -4) {
            if (((key_queue_end - key_queue_start) & 0xf) > 14)
                return 0xff;
            delta_x += 4;
            m24_kbd_adddata(m24_kbd->scan[3]);
        }
        while (delta_x > 4) {
            if (((key_queue_end - key_queue_start) & 0xf) > 14)
                return 0xff;
            delta_x -= 4;
            m24_kbd_adddata(m24_kbd->scan[4]);
        }
        while (delta_y < -4) {
            if (((key_queue_end - key_queue_start) & 0xf) > 14)
                return 0xff;
            delta_y += 4;
            m24_kbd_adddata(m24_kbd->scan[5]);
        }
        while (delta_y > 4) {
            if (((key_queue_end - key_queue_start) & 0xf) > 14)
                return 0xff;
            delta_y -= 4;
            m24_kbd_adddata(m24_kbd->scan[6]);
        }
    }

    return 0;
}

/* Remapping as follows:

   - Left Windows  (E0 5B) -> NUMPAD 00 (54);
   - Print Screen  (E0 37) -> SCR PRT (55);
   - Menu          (E0 5D) -> HELP (56);
   - NumPad Enter  (E0 1C) -> NUMPAD ENTER (57).
   - Left          (E0 4B) -> LEFT (58);
   - Down          (E0 50) -> DOWN (59);
   - Right         (E0 4D) -> RIGHT (5A);
   - Up            (E0 48) -> UP (5B);
   - Page Up       (E0 49) -> CLEAR (5C);
   - Page Down     (E0 51) -> BREAK (5D);
   - CE Key        (56)    -> CE KEY (5E);
     WARNING: The Olivetti CE Key is undocumented, but can be inferred from the fact
              its position is missing in the shown layout, it being used by the Italian
              keyboard layout, the keyboard is called 103-key, but only 102 keys are
              shown.
   - NumPad /      (E0 35) -> NUMPAD / (5F);
   - F11           (57)    -> F11 (60);
   - F12           (58)    -> F12 (61);
   - Insert        (E0 52) -> F13 (62);
   - Home          (E0 47) -> F14 (63);
   - Delete        (E0 53) -> F15 (64);
   - End           (E0 4F) -> F16 (65);
   - Right Alt (Gr)(E0 38) -> F16 (66);
   - Right Windows (E0 5C) -> F18 (67).
 */
const scancode scancode_olivetti_m24_deluxe[512] = {
  // clang-format off
    { .mk = {            0 }, .brk = {                   0 } }, /* 000 */
    { .mk = {      0x01, 0 }, .brk = {             0x81, 0 } }, /* 001 */
    { .mk = {      0x02, 0 }, .brk = {             0x82, 0 } }, /* 002 */
    { .mk = {      0x03, 0 }, .brk = {             0x83, 0 } }, /* 003 */
    { .mk = {      0x04, 0 }, .brk = {             0x84, 0 } }, /* 004 */
    { .mk = {      0x05, 0 }, .brk = {             0x85, 0 } }, /* 005 */
    { .mk = {      0x06, 0 }, .brk = {             0x86, 0 } }, /* 006 */
    { .mk = {      0x07, 0 }, .brk = {             0x87, 0 } }, /* 007 */
    { .mk = {      0x08, 0 }, .brk = {             0x88, 0 } }, /* 008 */
    { .mk = {      0x09, 0 }, .brk = {             0x89, 0 } }, /* 009 */
    { .mk = {      0x0a, 0 }, .brk = {             0x8a, 0 } }, /* 00a */
    { .mk = {      0x0b, 0 }, .brk = {             0x8b, 0 } }, /* 00b */
    { .mk = {      0x0c, 0 }, .brk = {             0x8c, 0 } }, /* 00c */
    { .mk = {      0x0d, 0 }, .brk = {             0x8d, 0 } }, /* 00d */
    { .mk = {      0x0e, 0 }, .brk = {             0x8e, 0 } }, /* 00e */
    { .mk = {      0x0f, 0 }, .brk = {             0x8f, 0 } }, /* 00f */
    { .mk = {      0x10, 0 }, .brk = {             0x90, 0 } }, /* 010 */
    { .mk = {      0x11, 0 }, .brk = {             0x91, 0 } }, /* 011 */
    { .mk = {      0x12, 0 }, .brk = {             0x92, 0 } }, /* 013 */
    { .mk = {      0x13, 0 }, .brk = {             0x93, 0 } }, /* 013 */
    { .mk = {      0x14, 0 }, .brk = {             0x94, 0 } }, /* 014 */
    { .mk = {      0x15, 0 }, .brk = {             0x95, 0 } }, /* 015 */
    { .mk = {      0x16, 0 }, .brk = {             0x96, 0 } }, /* 016 */
    { .mk = {      0x17, 0 }, .brk = {             0x97, 0 } }, /* 017 */
    { .mk = {      0x18, 0 }, .brk = {             0x98, 0 } }, /* 018 */
    { .mk = {      0x19, 0 }, .brk = {             0x99, 0 } }, /* 019 */
    { .mk = {      0x1a, 0 }, .brk = {             0x9a, 0 } }, /* 01a */
    { .mk = {      0x1b, 0 }, .brk = {             0x9b, 0 } }, /* 01b */
    { .mk = {      0x1c, 0 }, .brk = {             0x9c, 0 } }, /* 01c */
    { .mk = {      0x1d, 0 }, .brk = {             0x9d, 0 } }, /* 01d */
    { .mk = {      0x1e, 0 }, .brk = {             0x9e, 0 } }, /* 01e */
    { .mk = {      0x1f, 0 }, .brk = {             0x9f, 0 } }, /* 01f */
    { .mk = {      0x20, 0 }, .brk = {             0xa0, 0 } }, /* 020 */
    { .mk = {      0x21, 0 }, .brk = {             0xa1, 0 } }, /* 021 */
    { .mk = {      0x22, 0 }, .brk = {             0xa2, 0 } }, /* 022 */
    { .mk = {      0x23, 0 }, .brk = {             0xa3, 0 } }, /* 023 */
    { .mk = {      0x24, 0 }, .brk = {             0xa4, 0 } }, /* 024 */
    { .mk = {      0x25, 0 }, .brk = {             0xa5, 0 } }, /* 025 */
    { .mk = {      0x26, 0 }, .brk = {             0xa6, 0 } }, /* 026 */
    { .mk = {      0x27, 0 }, .brk = {             0xa7, 0 } }, /* 027 */
    { .mk = {      0x28, 0 }, .brk = {             0xa8, 0 } }, /* 028 */
    { .mk = {      0x29, 0 }, .brk = {             0xa9, 0 } }, /* 029 */
    { .mk = {      0x2a, 0 }, .brk = {             0xaa, 0 } }, /* 02a */
    { .mk = {      0x2b, 0 }, .brk = {             0xab, 0 } }, /* 02b */
    { .mk = {      0x2c, 0 }, .brk = {             0xac, 0 } }, /* 02c */
    { .mk = {      0x2d, 0 }, .brk = {             0xad, 0 } }, /* 02d */
    { .mk = {      0x2e, 0 }, .brk = {             0xae, 0 } }, /* 02e */
    { .mk = {      0x2f, 0 }, .brk = {             0xaf, 0 } }, /* 02f */
    { .mk = {      0x30, 0 }, .brk = {             0xb0, 0 } }, /* 030 */
    { .mk = {      0x31, 0 }, .brk = {             0xb1, 0 } }, /* 031 */
    { .mk = {      0x32, 0 }, .brk = {             0xb2, 0 } }, /* 032 */
    { .mk = {      0x33, 0 }, .brk = {             0xb3, 0 } }, /* 033 */
    { .mk = {      0x34, 0 }, .brk = {             0xb4, 0 } }, /* 034 */
    { .mk = {      0x35, 0 }, .brk = {             0xb5, 0 } }, /* 035 */
    { .mk = {      0x36, 0 }, .brk = {             0xb6, 0 } }, /* 036 */
    { .mk = {      0x37, 0 }, .brk = {             0xb7, 0 } }, /* 037 */
    { .mk = {      0x38, 0 }, .brk = {             0xb8, 0 } }, /* 038 */
    { .mk = {      0x39, 0 }, .brk = {             0xb9, 0 } }, /* 039 */
    { .mk = {      0x3a, 0 }, .brk = {             0xba, 0 } }, /* 03a */
    { .mk = {      0x3b, 0 }, .brk = {             0xbb, 0 } }, /* 03b */
    { .mk = {      0x3c, 0 }, .brk = {             0xbc, 0 } }, /* 03c */
    { .mk = {      0x3d, 0 }, .brk = {             0xbd, 0 } }, /* 03d */
    { .mk = {      0x3e, 0 }, .brk = {             0xbe, 0 } }, /* 03e */
    { .mk = {      0x3f, 0 }, .brk = {             0xbf, 0 } }, /* 03f */
    { .mk = {      0x40, 0 }, .brk = {             0xc0, 0 } }, /* 040 */
    { .mk = {      0x41, 0 }, .brk = {             0xc1, 0 } }, /* 041 */
    { .mk = {      0x42, 0 }, .brk = {             0xc2, 0 } }, /* 042 */
    { .mk = {      0x43, 0 }, .brk = {             0xc3, 0 } }, /* 043 */
    { .mk = {      0x44, 0 }, .brk = {             0xc4, 0 } }, /* 044 */
    { .mk = {      0x45, 0 }, .brk = {             0xc5, 0 } }, /* 045 */
    { .mk = {      0x46, 0 }, .brk = {             0xc6, 0 } }, /* 046 */
    { .mk = {      0x47, 0 }, .brk = {             0xc7, 0 } }, /* 047 */
    { .mk = {      0x48, 0 }, .brk = {             0xc8, 0 } }, /* 048 */
    { .mk = {      0x49, 0 }, .brk = {             0xc9, 0 } }, /* 049 */
    { .mk = {      0x4a, 0 }, .brk = {             0xca, 0 } }, /* 04a */
    { .mk = {      0x4b, 0 }, .brk = {             0xcb, 0 } }, /* 04b */
    { .mk = {      0x4c, 0 }, .brk = {             0xcc, 0 } }, /* 04c */
    { .mk = {      0x4d, 0 }, .brk = {             0xcd, 0 } }, /* 04d */
    { .mk = {      0x4e, 0 }, .brk = {             0xce, 0 } }, /* 04e */
    { .mk = {      0x4f, 0 }, .brk = {             0xcf, 0 } }, /* 04f */
    { .mk = {      0x50, 0 }, .brk = {             0xd0, 0 } }, /* 050 */
    { .mk = {      0x51, 0 }, .brk = {             0xd1, 0 } }, /* 051 */
    { .mk = {      0x52, 0 }, .brk = {             0xd2, 0 } }, /* 052 */
    { .mk = {      0x53, 0 }, .brk = {             0xd3, 0 } }, /* 053 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 054 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 055 */
    { .mk = {      0x5e, 0 }, .brk = {             0xde, 0 } }, /* 056 */
    { .mk = {      0x60, 0 }, .brk = {             0xe0, 0 } }, /* 057 */
    { .mk = {      0x61, 0 }, .brk = {             0xe1, 0 } }, /* 058 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 059 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 060 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 061 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 062 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 063 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 064 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 065 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 066 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 067 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 068 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 069 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 070 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 071 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 072 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 073 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 074 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 075 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 076 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 077 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 078 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 079 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 080 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 081 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 082 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 083 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 084 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 085 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 086 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 087 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 088 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 089 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 090 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 091 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 092 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 093 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 094 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 095 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 096 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 097 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 098 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 099 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0aa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0aa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ab */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ac */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ad */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ae */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0af */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ba */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0be */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ca */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ce */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0da */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0db */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0dc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0dd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0de */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0df */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ea */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0eb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ec */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ed */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ee */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ef */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fe */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ff */
    { .mk = {            0 }, .brk = {                   0 } }, /* 100 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 101 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 102 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 103 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 104 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 105 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 106 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 107 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 108 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 109 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 110 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 111 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 112 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 113 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 114 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 115 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 116 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 117 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 118 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 119 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11b */
    { .mk = {      0x57, 0 }, .brk = {             0xd7, 0 } }, /* 11c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 120 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 121 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 122 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 123 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 124 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 125 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 126 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 127 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 128 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 129 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 130 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 131 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 132 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 133 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 134 */
    { .mk = {      0x5f, 0 }, .brk = {             0xdf, 0 } }, /* 135 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 136 */
    { .mk = {      0x37, 0 }, .brk = {             0xb7, 0 } }, /* 137 */
    { .mk = {      0x66, 0 }, .brk = {             0xe6, 0 } }, /* 138 */
    { .mk = {      0x55, 0 }, .brk = {             0xd5, 0 } }, /* 139 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 140 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 141 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 142 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 143 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 144 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 145 */
    { .mk = {      0x46, 0 }, .brk = {             0xc6, 0 } }, /* 146 */
    { .mk = {      0x63, 0 }, .brk = {             0xe3, 0 } }, /* 147 */
    { .mk = {      0x5b, 0 }, .brk = {             0xdb, 0 } }, /* 148 */
    { .mk = {      0x5c, 0 }, .brk = {             0xdc, 0 } }, /* 149 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14a */
    { .mk = {      0x58, 0 }, .brk = {             0xd8, 0 } }, /* 14b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14c */
    { .mk = {      0x5a, 0 }, .brk = {             0xda, 0 } }, /* 14d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14e */
    { .mk = {      0x65, 0 }, .brk = {             0xe5, 0 } }, /* 14f */
    { .mk = {      0x59, 0 }, .brk = {             0xd9, 0 } }, /* 150 */
    { .mk = {      0x5d, 0 }, .brk = {             0xdd, 0 } }, /* 151 */
    { .mk = {      0x62, 0 }, .brk = {             0xe2, 0 } }, /* 152 */
    { .mk = {      0x64, 0 }, .brk = {             0xe4, 0 } }, /* 153 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 154 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 155 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 156 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 157 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 158 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 159 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15a */
    { .mk = {      0x54, 0 }, .brk = {             0xd4, 0 } }, /* 15b */
    { .mk = {      0x67, 0 }, .brk = {             0xe7, 0 } }, /* 15c */
    { .mk = {      0x56, 0 }, .brk = {             0xd6, 0 } }, /* 15d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 160 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 161 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 162 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 163 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 164 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 165 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 166 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 167 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 168 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 169 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 170 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 171 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 172 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 173 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 174 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 175 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 176 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 177 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 178 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 179 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 180 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 181 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 182 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 183 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 184 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 185 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 186 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 187 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 188 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 189 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 190 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 191 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 192 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 193 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 194 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 195 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 196 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 197 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 198 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 199 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1aa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ab */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ac */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ad */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ae */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1af */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ba */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1be */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ca */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ce */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1da */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1db */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1dc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1dd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1de */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1df */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ea */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1eb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ec */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ed */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ee */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ef */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fe */
    { .mk = {            0 }, .brk = {                   0 } }  /* 1ff */
  // clang-format on
};

/* Remapping as follows:

   - Left Windows  (E0 5B) -> 54;
   - Right Windows (E0 5C) -> 56;
   - Menu          (E0 5D) -> 5C.
 */
const scancode scancode_olivetti_m240[512] = {
  // clang-format off
    { .mk = {            0 }, .brk = {                   0 } }, /* 000 */
    { .mk = {      0x01, 0 }, .brk = {             0x81, 0 } }, /* 001 */
    { .mk = {      0x02, 0 }, .brk = {             0x82, 0 } }, /* 002 */
    { .mk = {      0x03, 0 }, .brk = {             0x83, 0 } }, /* 003 */
    { .mk = {      0x04, 0 }, .brk = {             0x84, 0 } }, /* 004 */
    { .mk = {      0x05, 0 }, .brk = {             0x85, 0 } }, /* 005 */
    { .mk = {      0x06, 0 }, .brk = {             0x86, 0 } }, /* 006 */
    { .mk = {      0x07, 0 }, .brk = {             0x87, 0 } }, /* 007 */
    { .mk = {      0x08, 0 }, .brk = {             0x88, 0 } }, /* 008 */
    { .mk = {      0x09, 0 }, .brk = {             0x89, 0 } }, /* 009 */
    { .mk = {      0x0a, 0 }, .brk = {             0x8a, 0 } }, /* 00a */
    { .mk = {      0x0b, 0 }, .brk = {             0x8b, 0 } }, /* 00b */
    { .mk = {      0x0c, 0 }, .brk = {             0x8c, 0 } }, /* 00c */
    { .mk = {      0x0d, 0 }, .brk = {             0x8d, 0 } }, /* 00d */
    { .mk = {      0x0e, 0 }, .brk = {             0x8e, 0 } }, /* 00e */
    { .mk = {      0x0f, 0 }, .brk = {             0x8f, 0 } }, /* 00f */
    { .mk = {      0x10, 0 }, .brk = {             0x90, 0 } }, /* 010 */
    { .mk = {      0x11, 0 }, .brk = {             0x91, 0 } }, /* 011 */
    { .mk = {      0x12, 0 }, .brk = {             0x92, 0 } }, /* 012 */
    { .mk = {      0x13, 0 }, .brk = {             0x93, 0 } }, /* 013 */
    { .mk = {      0x14, 0 }, .brk = {             0x94, 0 } }, /* 014 */
    { .mk = {      0x15, 0 }, .brk = {             0x95, 0 } }, /* 015 */
    { .mk = {      0x16, 0 }, .brk = {             0x96, 0 } }, /* 016 */
    { .mk = {      0x17, 0 }, .brk = {             0x97, 0 } }, /* 017 */
    { .mk = {      0x18, 0 }, .brk = {             0x98, 0 } }, /* 018 */
    { .mk = {      0x19, 0 }, .brk = {             0x99, 0 } }, /* 019 */
    { .mk = {      0x1a, 0 }, .brk = {             0x9a, 0 } }, /* 01a */
    { .mk = {      0x1b, 0 }, .brk = {             0x9b, 0 } }, /* 01b */
    { .mk = {      0x1c, 0 }, .brk = {             0x9c, 0 } }, /* 01c */
    { .mk = {      0x1d, 0 }, .brk = {             0x9d, 0 } }, /* 01d */
    { .mk = {      0x1e, 0 }, .brk = {             0x9e, 0 } }, /* 01e */
    { .mk = {      0x1f, 0 }, .brk = {             0x9f, 0 } }, /* 01f */
    { .mk = {      0x20, 0 }, .brk = {             0xa0, 0 } }, /* 020 */
    { .mk = {      0x21, 0 }, .brk = {             0xa1, 0 } }, /* 021 */
    { .mk = {      0x22, 0 }, .brk = {             0xa2, 0 } }, /* 023 */
    { .mk = {      0x23, 0 }, .brk = {             0xa3, 0 } }, /* 023 */
    { .mk = {      0x24, 0 }, .brk = {             0xa4, 0 } }, /* 024 */
    { .mk = {      0x25, 0 }, .brk = {             0xa5, 0 } }, /* 025 */
    { .mk = {      0x26, 0 }, .brk = {             0xa6, 0 } }, /* 026 */
    { .mk = {      0x27, 0 }, .brk = {             0xa7, 0 } }, /* 027 */
    { .mk = {      0x28, 0 }, .brk = {             0xa8, 0 } }, /* 028 */
    { .mk = {      0x29, 0 }, .brk = {             0xa9, 0 } }, /* 029 */
    { .mk = {      0x2a, 0 }, .brk = {             0xaa, 0 } }, /* 02a */
    { .mk = {      0x2b, 0 }, .brk = {             0xab, 0 } }, /* 02b */
    { .mk = {      0x2c, 0 }, .brk = {             0xac, 0 } }, /* 02c */
    { .mk = {      0x2d, 0 }, .brk = {             0xad, 0 } }, /* 02d */
    { .mk = {      0x2e, 0 }, .brk = {             0xae, 0 } }, /* 02e */
    { .mk = {      0x2f, 0 }, .brk = {             0xaf, 0 } }, /* 02f */
    { .mk = {      0x30, 0 }, .brk = {             0xb0, 0 } }, /* 030 */
    { .mk = {      0x31, 0 }, .brk = {             0xb1, 0 } }, /* 031 */
    { .mk = {      0x32, 0 }, .brk = {             0xb2, 0 } }, /* 032 */
    { .mk = {      0x33, 0 }, .brk = {             0xb3, 0 } }, /* 033 */
    { .mk = {      0x34, 0 }, .brk = {             0xb4, 0 } }, /* 034 */
    { .mk = {      0x35, 0 }, .brk = {             0xb5, 0 } }, /* 035 */
    { .mk = {      0x36, 0 }, .brk = {             0xb6, 0 } }, /* 036 */
    { .mk = {      0x37, 0 }, .brk = {             0xb7, 0 } }, /* 037 */
    { .mk = {      0x38, 0 }, .brk = {             0xb8, 0 } }, /* 038 */
    { .mk = {      0x39, 0 }, .brk = {             0xb9, 0 } }, /* 039 */
    { .mk = {      0x3a, 0 }, .brk = {             0xba, 0 } }, /* 03a */
    { .mk = {      0x3b, 0 }, .brk = {             0xbb, 0 } }, /* 03b */
    { .mk = {      0x3c, 0 }, .brk = {             0xbc, 0 } }, /* 03c */
    { .mk = {      0x3d, 0 }, .brk = {             0xbd, 0 } }, /* 03d */
    { .mk = {      0x3e, 0 }, .brk = {             0xbe, 0 } }, /* 03e */
    { .mk = {      0x3f, 0 }, .brk = {             0xbf, 0 } }, /* 03f */
    { .mk = {      0x40, 0 }, .brk = {             0xc0, 0 } }, /* 040 */
    { .mk = {      0x41, 0 }, .brk = {             0xc1, 0 } }, /* 041 */
    { .mk = {      0x42, 0 }, .brk = {             0xc2, 0 } }, /* 042 */
    { .mk = {      0x43, 0 }, .brk = {             0xc3, 0 } }, /* 043 */
    { .mk = {      0x44, 0 }, .brk = {             0xc4, 0 } }, /* 044 */
    { .mk = {      0x45, 0 }, .brk = {             0xc5, 0 } }, /* 045 */
    { .mk = {      0x46, 0 }, .brk = {             0xc6, 0 } }, /* 046 */
    { .mk = {      0x47, 0 }, .brk = {             0xc7, 0 } }, /* 047 */
    { .mk = {      0x48, 0 }, .brk = {             0xc8, 0 } }, /* 048 */
    { .mk = {      0x49, 0 }, .brk = {             0xc9, 0 } }, /* 049 */
    { .mk = {      0x4a, 0 }, .brk = {             0xca, 0 } }, /* 04a */
    { .mk = {      0x4b, 0 }, .brk = {             0xcb, 0 } }, /* 04b */
    { .mk = {      0x4c, 0 }, .brk = {             0xcc, 0 } }, /* 04c */
    { .mk = {      0x4d, 0 }, .brk = {             0xcd, 0 } }, /* 04d */
    { .mk = {      0x4e, 0 }, .brk = {             0xce, 0 } }, /* 04e */
    { .mk = {      0x4f, 0 }, .brk = {             0xcf, 0 } }, /* 04f */
    { .mk = {      0x50, 0 }, .brk = {             0xd0, 0 } }, /* 050 */
    { .mk = {      0x51, 0 }, .brk = {             0xd1, 0 } }, /* 051 */
    { .mk = {      0x52, 0 }, .brk = {             0xd2, 0 } }, /* 052 */
    { .mk = {      0x53, 0 }, .brk = {             0xd3, 0 } }, /* 053 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 054 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 055 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 056 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 057 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 058 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 059 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 060 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 061 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 062 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 063 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 064 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 065 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 066 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 067 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 068 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 069 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 070 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 071 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 072 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 073 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 074 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 075 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 076 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 077 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 078 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 079 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 080 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 081 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 082 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 083 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 084 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 085 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 086 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 087 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 088 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 089 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 090 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 091 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 092 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 093 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 094 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 095 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 096 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 097 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 098 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 099 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0aa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ab */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ac */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ad */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ae */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0af */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ba */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0be */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ca */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ce */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0da */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0db */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0dc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0dd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0de */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0df */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ea */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0eb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ec */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ed */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ee */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ef */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fe */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ff */
    { .mk = {            0 }, .brk = {                   0 } }, /* 100 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 101 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 102 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 103 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 104 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 105 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 106 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 107 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 108 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 109 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 110 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 111 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 112 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 113 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 114 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 115 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 116 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 117 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 118 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 119 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11b */
    { .mk = {      0x1c, 0 }, .brk = {             0x9c, 0 } }, /* 11c */
    { .mk = {      0x1d, 0 }, .brk = {             0x9d, 0 } }, /* 11d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 120 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 121 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 122 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 123 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 124 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 125 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 126 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 127 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 128 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 129 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 130 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 131 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 132 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 133 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 134 */
    { .mk = {      0x35, 0 }, .brk = {             0xb5, 0 } }, /* 135 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 136 */
    { .mk = {      0x37, 0 }, .brk = {             0xb7, 0 } }, /* 137 */
    { .mk = {      0x38, 0 }, .brk = {             0xb8, 0 } }, /* 138 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 139 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 140 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 141 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 142 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 143 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 144 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 145 */
    { .mk = {      0x46, 0 }, .brk = {             0xc6, 0 } }, /* 146 */
    { .mk = {      0x47, 0 }, .brk = {             0xc7, 0 } }, /* 147 */
    { .mk = {      0x48, 0 }, .brk = {             0xc8, 0 } }, /* 148 */
    { .mk = {      0x49, 0 }, .brk = {             0xc9, 0 } }, /* 149 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14a */
    { .mk = {      0x4b, 0 }, .brk = {             0xcb, 0 } }, /* 14b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14c */
    { .mk = {      0x4d, 0 }, .brk = {             0xcd, 0 } }, /* 14d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14e */
    { .mk = {      0x4f, 0 }, .brk = {             0xcf, 0 } }, /* 14f */
    { .mk = {      0x50, 0 }, .brk = {             0xd0, 0 } }, /* 150 */
    { .mk = {      0x51, 0 }, .brk = {             0xd1, 0 } }, /* 151 */
    { .mk = {      0x52, 0 }, .brk = {             0xd2, 0 } }, /* 152 */
    { .mk = {      0x53, 0 }, .brk = {             0xd3, 0 } }, /* 153 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 154 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 155 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 156 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 157 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 158 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 159 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15c */
    { .mk = {      0x54, 0 }, .brk = {             0xd4, 0 } }, /* 15d */
    { .mk = {      0x56, 0 }, .brk = {             0xd6, 0 } }, /* 15e */
    { .mk = {      0x5c, 0 }, .brk = {             0xdc, 0 } }, /* 15f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 160 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 161 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 162 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 163 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 164 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 165 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 166 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 167 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 168 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 169 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 170 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 171 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 172 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 173 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 174 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 175 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 176 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 177 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 178 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 179 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 180 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 181 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 182 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 183 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 184 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 185 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 186 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 187 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 188 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 189 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 190 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 191 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 192 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 193 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 194 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 195 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 196 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 197 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 198 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 199 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1aa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ab */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ac */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ad */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ae */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1af */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ba */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1be */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ca */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ce */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1da */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1db */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1dc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1dd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1de */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1df */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ea */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1eb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ec */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ed */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ee */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ef */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fe */
    { .mk = {            0 }, .brk = {                   0 } }  /* 1ff */
  // clang-format on
};

static void
m24_kbd_init(m24_kbd_t *kbd)
{

    /* Initialize the keyboard. */
    io_sethandler(0x0060, 2,
                  m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, kbd);
    io_sethandler(0x0064, 1,
                  m24_kbd_read, NULL, NULL, m24_kbd_write, NULL, NULL, kbd);
    keyboard_send = m24_kbd_adddata_ex;
    m24_kbd_reset(kbd);
    timer_add(&kbd->send_delay_timer, m24_kbd_poll, kbd, 1);

    if (mouse_type == MOUSE_TYPE_INTERNAL) {
        /* Tell mouse driver about our internal mouse. */
        mouse_reset();
        mouse_set_buttons(2);
        mouse_set_poll(ms_poll, kbd);
    }

    keyboard_set_table((kbd->id == 0x01) ? scancode_olivetti_m24_deluxe : scancode_olivetti_m240);
    keyboard_set_is_amstrad(0);
}

static void
m19_vid_out(uint16_t addr, uint8_t val, void *priv)
{
    m19_vid_t *vid     = (m19_vid_t *) priv;
    int        oldmode = vid->mode;

    /* activating plantronics mode */
    if (addr == 0x3dd) {
        /* already in graphics mode */
        if ((val & 0x30) && (vid->ogc.cga.cgamode & 0x2))
            vid->mode = PLANTRONICS_MODE;
        else
            vid->mode = OLIVETTI_OGC_MODE;
        /* setting graphics mode */
    } else if (addr == 0x3d8) {
        if ((val & 0x2) && (vid->colorplus.control & 0x30))
            vid->mode = PLANTRONICS_MODE;
        else
            vid->mode = OLIVETTI_OGC_MODE;
    }
    /* video mode changed */
    if (oldmode != vid->mode) {
        /* activate Plantronics emulation */
        if (vid->mode == PLANTRONICS_MODE) {
            timer_disable(&vid->ogc.cga.timer);
            timer_set_delay_u64(&vid->colorplus.cga.timer, 0);
            /* return to OGC mode */
        } else {
            timer_disable(&vid->colorplus.cga.timer);
            timer_set_delay_u64(&vid->ogc.cga.timer, 0);
        }

        colorplus_recalctimings(&vid->colorplus);
        ogc_recalctimings(&vid->ogc);
    }

    colorplus_out(addr, val, &vid->colorplus);
    ogc_out(addr, val, &vid->ogc);
}

static uint8_t
m19_vid_in(uint16_t addr, void *priv)
{
    m19_vid_t *vid = (m19_vid_t *) priv;

    if (vid->mode == PLANTRONICS_MODE)
        return colorplus_in(addr, &vid->colorplus);
    else
        return ogc_in(addr, &vid->ogc);
}

static uint8_t
m19_vid_read(uint32_t addr, void *priv)
{
    m19_vid_t *vid = (m19_vid_t *) priv;

    vid->colorplus.cga.mapping = vid->ogc.cga.mapping;
    if (vid->mode == PLANTRONICS_MODE)
        return colorplus_read(addr, &vid->colorplus);
    else
        return ogc_read(addr, &vid->ogc);
}

static void
m19_vid_write(uint32_t addr, uint8_t val, void *priv)
{
    m19_vid_t *vid = (m19_vid_t *) priv;

    colorplus_write(addr, val, &vid->colorplus);
    ogc_write(addr, val, &vid->ogc);
}

static void
m19_vid_close(void *priv)
{
    m19_vid_t *vid = (m19_vid_t *) priv;

    free(vid->ogc.cga.vram);
    free(vid->colorplus.cga.vram);
    free(vid);
}

static void
m19_vid_speed_changed(void *priv)
{
    m19_vid_t *vid = (m19_vid_t *) priv;

    colorplus_recalctimings(&vid->colorplus);
    ogc_recalctimings(&vid->ogc);
}

static void
m19_vid_init(m19_vid_t *vid)
{
    device_context(&m19_vid_device);

#if 0
    int display_type;
#endif
    vid->mode = OLIVETTI_OGC_MODE;

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_m19_vid);

#if 0
    display_type = device_get_config_int("display_type");
#endif

    /* OGC emulation part begin */
    loadfont("roms/machines/m19/MBM2764-30 8514 107 AB PCF3.BIN", 7);
    /* composite is not working yet */
    vid->ogc.cga.composite    = 0; // (display_type != CGA_RGB);
    vid->ogc.cga.revision     = device_get_config_int("composite_type");
    vid->ogc.cga.snow_enabled = device_get_config_int("snow_enabled");

    vid->ogc.cga.vram = malloc(0x8000);

#if 0
    cga_comp_init(vid->ogc.cga.revision);
#endif

    vid->ogc.cga.rgb_type = device_get_config_int("rgb_type");
    cga_palette           = (vid->ogc.cga.rgb_type << 1);
    cgapal_rebuild();
    ogc_mdaattr_rebuild();

    /* color display */
    if (device_get_config_int("rgb_type") == 0 || device_get_config_int("rgb_type") == 4)
        vid->ogc.mono_display = 0;
    else
        vid->ogc.mono_display = 1;

    vid->ogc.ctrl_addr = 0x3db;
    /* OGC emulation part end */

    /* Plantronics emulation part begin*/
    /* composite is not working yet */
    vid->colorplus.cga.composite = 0; //(display_type != CGA_RGB);
#if 0
    vid->colorplus.cga.snow_enabled = device_get_config_int("snow_enabled");
#endif

    vid->colorplus.cga.vram = malloc(0x8000);

#if 0
    vid->colorplus.cga.cgamode = 0x1;
#endif
    /* Plantronics emulation part end*/

    timer_add(&vid->ogc.cga.timer, ogc_poll, &vid->ogc, 1);
    timer_add(&vid->colorplus.cga.timer, colorplus_poll, &vid->colorplus, 1);
    timer_disable(&vid->colorplus.cga.timer);
    mem_mapping_add(&vid->ogc.cga.mapping, 0xb8000, 0x08000, m19_vid_read, NULL, NULL, m19_vid_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, vid);
    io_sethandler(0x03d0, 0x0010, m19_vid_in, NULL, NULL, m19_vid_out, NULL, NULL, vid);

    vid->mode = OLIVETTI_OGC_MODE;

    device_context_restore();
}

const device_t m24_kbd_device = {
    .name          = "Olivetti M24 keyboard and mouse",
    .internal_name = "m24_kbd",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = m24_kbd_close,
    .reset         = m24_kbd_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_config_t m19_vid_config[] = {
  // clang-format off
    {
        /* Olivetti / ATT compatible displays */
        .name = "rgb_type",
        .description = "RGB type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = CGA_RGB,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Color",            .value = 0 },
            { .description = "Green Monochrome", .value = 1 },
            { .description = "Amber Monochrome", .value = 2 },
            { .description = "Gray Monochrome",  .value = 3 },
            { .description = ""                             }
        }
    },
    {
        .name = "snow_enabled",
        .description = "Snow emulation",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1,
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t m19_vid_device = {
    .name          = "Olivetti M19 graphics card",
    .internal_name = "m19_vid",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = m19_vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = m19_vid_speed_changed,
    .force_redraw  = NULL,
    .config        = m19_vid_config
};

static uint8_t
m24_read(uint16_t port, UNUSED(void *priv))
{
    uint8_t ret       = 0x00;
    int     fdd_count = 0;

    switch (port) {
        case 0x62:
            /* Control Port B Read */
            ret = 0xff;
            break;

        case 0x65:
            /* Communications Port Read */
            ret = 0xff;
            break;

        /*
         * port 66:
         * DIPSW-0 on mainboard (off=present=1)
         * bit 7 - 2764 (off) / 2732 (on) ROM (BIOS < 1.36)
         * bit 7 - Use (off) / do not use (on) memory bank 1 (BIOS >= 1.36)
         * bit 6 - n/a
         * bit 5 - 8530 (off) / 8250 (on) SCC
         * bit 4 - 8087 present
         * bits 3-0 - installed memory
         */
        case 0x66:
            /* Switch 5 - 8087 present */
            if (hasfpu)
                ret |= 0x10;
            /*
             * Switches 1, 2, 3, 4 - installed memory
             * Switch 8 - Use memory bank 1
             */
            switch (mem_size) {
                case 128:
                    ret |= 0x1;
                    break;
                case 256:
                    ret |= 0x2;
                    break;
                case 384:
                    ret |= 0x1 | 0x2 | 0x80;
                    break;
                case 512:
                    ret |= 0x8;
                    break;
                case 640:
                    ret |= 0x1 | 0x8 | 0x80;
                    break;
                default:
                    break;
            }
            break;

        /*
         * port 67:
         * DIPSW-1 on mainboard (off=present=1)
         * bits 7-6 - number of drives
         * bits 5-4 - display adapter
         * bit 3 - video scroll CPU (on) / slow scroll (off)
         * bit 2 - BIOS HD on mainboard (on) / on controller (off)
         * bit 1 - FDD fast (off) / slow (on) start drive
         * bit 0 - 96 TPI (720 KB 3.5") (off) / 48 TPI (360 KB 5.25") FDD drive
         *
         * Display adapter:
         * off off 80x25 mono
         * off on  40x25 color
         * on off  80x25 color
         * on on   EGA/VGA (works only for BIOS ROM 1.43)
         */
        case 0x67:
            for (uint8_t i = 0; i < FDD_NUM; i++) {
                if (fdd_get_flags(i))
                    fdd_count++;
            }

            /* Switches 7, 8 - floppy drives. */
            if (!fdd_count)
                ret |= 0x00;
            else
                ret |= ((fdd_count - 1) << 6);

            /* Switches 5, 6 - monitor type */
            if (video_is_mda())
                ret |= 0x30;
            else if (video_is_cga())
                ret |= 0x20; /* 0x10 would be 40x25 */
            else
                ret |= 0x0;

            /* Switch 4 - The M21/M24 Theory of Operation says
                          "Reserved for HDU", same as for Switch 3 */

            /* Switch 3 - Disable internal BIOS HD */
            if (hdc_current[0] != HDC_INTERNAL)
                ret |= 0x4;

            /* Switch 2 - Set fast startup */
            ret |= 0x2;

            /* 1 = 720 kB (3.5"), 0 = 360 kB (5.25") */
            ret |= (fdd_doublestep_40(0) || fdd_doublestep_40(1)) ? 0x1 : 0x0;
            break;

        default:
            break;
    }

    return ret;
}

static uint8_t
m240_read(uint16_t port, UNUSED(void *priv))
{
    uint8_t ret = 0x00;
    int     fdd_count = 0;

    switch (port) {
        case 0x62:
            /* SWA on Olivetti M240 mainboard (off=1) */
            ret = 0x00;
            if (ppi.pb & 0x8) {
                /* Switches 4, 5 - floppy drives (number) */
                for (uint8_t i = 0; i < FDD_NUM; i++) {
                    if (fdd_get_flags(i))
                        fdd_count++;
                }
                if (!fdd_count)
                    ret |= 0x00;
                else
                    ret |= ((fdd_count - 1) << 2);
                /* Switches 6, 7 - monitor type */
                if (video_is_mda())
                    ret |= 0x3;
                else if (video_is_cga())
                    ret |= 0x2; /* 0x10 would be 40x25 */
                else
                    ret |= 0x0;
            } else {
                /* bit 2 always on */
                ret |= 0x4;
                /* Switch 8 - 8087 FPU. */
                if (hasfpu)
                    ret |= 0x02;
            }
            break;

        case 0x63:
            /* Olivetti M240 SWB:
               - Bit 7: 1 = MFDD (= high-density) unit present (Customer Test will then always think Drive 2 is absent),
                        0 = MFD unit absent;
               - Bit 6: 1 = Second drive is 3.5" (for low density drive, this means 80-track),
                        0 = Second drive is 5.25" (for low density drive, this means 40-track).
               - Bit 5: 1 = First drive is 3.5" (for low density drive, this means 80-track),
                        0 = First drive is 5.25" (for low density drive, this means 40-track).
             */

            ret = (fdd_is_hd(0) || fdd_is_hd(1)) ? 0x80 : 0x00;
            ret |= fdd_doublestep_40(1) ? 0x40 : 0x00;
            ret |= fdd_doublestep_40(0) ? 0x20 : 0x00;
            break;

        default:
            break;
    }

    return ret;
}

/*
 * Uses M21/M24/M240 keyboard controller and M24 102/103-key Deluxe keyboard.
 */
int
machine_xt_m24_init(const machine_t *model)
{
    int        ret;
    m24_kbd_t *m24_kbd;
    nvr_t     *nvr;

    ret = bios_load_interleaved("roms/machines/m24/olivetti_m24_bios_version_1.44_low_even.bin",
                                "roms/machines/m24/olivetti_m24_bios_version_1.44_high_odd.bin",
                                0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    m24_kbd = (m24_kbd_t *) calloc(1, sizeof(m24_kbd_t));

    machine_common_init(model);

    /* On-board FDC can be disabled only on M24SP */
    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_xt_device);

    /* Address 66-67 = mainboard dip-switch settings */
    io_sethandler(0x0065, 3, m24_read, NULL, NULL, NULL, NULL, NULL, NULL);

    standalone_gameport_type = &gameport_device;

    nmi_init();

    /* Allocate an NVR for this machine. */
    nvr = (nvr_t *) calloc(1, sizeof(nvr_t));
    if (nvr == NULL)
        return 0;

    mm58174_init(nvr, model->nvrmask + 1);

    video_reset(gfxcard[0]);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&ogc_m24_device);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    io_sethandler(0x0062, 1, m24_read, NULL, NULL, NULL, NULL, NULL, NULL);

    m24_kbd->id = 0x01;

    m24_kbd_init(m24_kbd);
    device_add_ex(&m24_kbd_device, m24_kbd);

    if (hdc_current[0] == HDC_INTERNAL)
        device_add(&st506_xt_wd1002a_wx1_nobios_device);

    return ret;
}

/*
 * Uses M21/M24/M240 keyboard controller and M240 keyboard.
 */
int
machine_xt_m240_init(const machine_t *model)
{
    int        ret;
    m24_kbd_t *m24_kbd;
    nvr_t     *nvr;

    ret = bios_load_interleaved("roms/machines/m240/olivetti_m240_pchj_2.11_low.bin",
                                "roms/machines/m240/olivetti_m240_pchk_2.11_high.bin",
                                0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    m24_kbd = (m24_kbd_t *) calloc(1, sizeof(m24_kbd_t));

    machine_common_init(model);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    /* Address 66-67 = mainboard dip-switch settings */
    io_sethandler(0x0062, 2, m240_read, NULL, NULL, NULL, NULL, NULL, NULL);

    /*
     * port 60: should return jumper settings only under unknown conditions
     * SWB on mainboard (off=1)
     * bit 7 - use BIOS HD on mainboard (on) / on controller (off)
     * bit 6 - use OCG/CGA display adapter (on) / other display adapter (off)
     */
    m24_kbd->id = 0x10;

    m24_kbd_init(m24_kbd);
    device_add_ex(&m24_kbd_device, m24_kbd);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device); /* io.c logs clearly show it using port 3F7 */

    if (joystick_type)
        device_add(&gameport_device);

    nmi_init();

    /* Allocate an NVR for this machine. */
    nvr = (nvr_t *) calloc(1, sizeof(nvr_t));
    if (nvr == NULL)
        return 0;

    mm58274_init(nvr, model->nvrmask + 1);

    if (hdc_current[0] == HDC_INTERNAL)
        device_add(&st506_xt_wd1002a_wx1_nobios_device);

    return ret;
}

/*
 * Current bugs:
 * - 640x400x2 graphics mode not supported (bit 0 of register 0x3de cannot be set)
 * - optional mouse emulation missing
 * - setting CPU speed at 4.77MHz sometimes throws a timer error. If the machine is hard-resetted, the error disappears.
 */
int
machine_xt_m19_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m19/BIOS.BIN",
                           0x000fc000, 16384, 0);
    ret &= rom_present("roms/machines/m19/MBM2764-30 8514 107 AB PCF3.BIN");

    if (bios_only || !ret)
        return ret;

    m19_vid_t *vid;

    /* Do not move memory allocation elsewhere. */
    vid = (m19_vid_t *) calloc(1, sizeof(m19_vid_t));

    machine_common_init(model);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    /* On-board FDC cannot be disabled */
    device_add(&fdc_xt_device);

    nmi_init();

    video_reset(gfxcard[0]);

    m19_vid_init(vid);
    device_add_ex(&m19_vid_device, vid);

    device_add(&keyboard_xt_olivetti_device);

    pit_set_clock((uint32_t) 14318184.0);

    return ret;
}

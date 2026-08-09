/*
 * VARCem   Virtual ARchaeological Computer EMulator.
 *          An emulator of (mostly) x86-based PC systems and devices,
 *          using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *          spanning the era between 1981 and 1995.
 *
 *          Implementation of a Clock/RTC Card for the ISA PC/XT.
 *
 *          Systems starting with the PC/XT had, by default, a realtime
 *          clock and NVR chip on the mainboard. The BIOS stored config
 *          data in the NVR, and the system could maintain time and date
 *          using the RTC.
 *
 *          Originally, PC systems did not have this, and they first did
 *          show up in non-IBM clone systems. Shortly after, expansion
 *          cards with this function became available for the PC's (ISA)
 *          bus, and they came in many forms and designs.
 *
 *          This implementation offers some of those boards:
 *
 *            Everex EV-170 (using NatSemi MM58167 chip)
 *            DTK PII-147 Hexa I/O Plus (using UMC 82C8167 chip)
 *            PS/2 Model 30 (using NatSemi MM58167)
 *
 *          and more will follow as time permits.
 *
 * NOTE:    The IRQ functionalities have been implemented, but not yet
 *          tested, as I need to write test software for them first :)
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2018 Fred N. van Kempen.
 *
 *          Redistribution and  use  in source  and binary forms, with
 *          or  without modification, are permitted  provided that the
 *          following conditions are met:
 *
 *          1. Redistributions of  source  code must retain the entire
 *             above notice, this list of conditions and the following
 *             disclaimer.
 *
 *          2. Redistributions in binary form must reproduce the above
 *             copyright  notice,  this list  of  conditions  and  the
 *             following disclaimer in  the documentation and/or other
 *             materials provided with the distribution.
 *
 *          3. Neither the  name of the copyright holder nor the names
 *             of  its  contributors may be used to endorse or promote
 *             products  derived from  this  software without specific
 *             prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/rom.h>
#include <86box/pic.h>
#include <86box/isartc.h>

#define ISARTC_EV170    0
#define ISARTC_DTK      1
#define ISARTC_P5PAK    2
#define ISARTC_A6PAK    3
#define ISARTC_VENDEX   4
#define ISARTC_MPLUS2   5
#define ISARTC_RTC58167 6
#define ISARTC_MM58167  10
#define ISARTC_PS2M30   11

#define ISARTC_ROM_MM58167_1 "roms/rtc/glatick/GLaTICK_0.8.8_NS_86B.ROM"  /* Generic 58167, AST or EV-170 */
#define ISARTC_ROM_MM58167_2 "roms/rtc/glatick/GLaTICK_0.8.8_NS_86B2.ROM" /* PII-147 */

#define ISARTC_DEBUG  0

typedef struct rtcdev_t {
    const char *name;  /* board name */
    uint8_t     board; /* board type */

    uint8_t flags;        /* various flags */
#define FLAG_YEAR80  0x01 /* YEAR byte is base-80 */
#define FLAG_YEARBCD 0x02 /* YEAR byte is in BCD */
#define FLAG_PS2     0x04 /* IBM PS/2 Model 30-8086 I/O mapping */

    int8_t   irq; /* configured IRQ channel */
    int8_t   base_addrsz;
    uint32_t base_addr; /* configured I/O address */
    rom_t    rom; /* BIOS ROM, If configured */

    /* Fields for the specific driver. */
    void    (*f_wr)(uint16_t, uint8_t, void *);
    uint8_t (*f_rd)(uint16_t, void *);
    int8_t    year; /* register for YEAR value */
    int8_t    century; /* register for CENTURY value */
    uint8_t  *irq_mask; /* PS/2 Model 30's gate-array IRQ1 mask at port A1h */
    pc_timer_t msec_timer; /* The Millisecond-resolution counter */
    pc_timer_t rollover_timer; /* The 150 us rollover timer */
    int        msec_count;

    nvr_t nvr; /* RTC/NVR */
} rtcdev_t;

/************************************************************************
 *                                                                      *
 *            Driver for the NatSemi MM58167 chip.                      *
 *                                                                      *
 ************************************************************************/
#define MM67_REGS 32

/* Define the RTC chip registers - see datasheet, pg4. */
#define MM67_MSEC        0    /* milliseconds */
#define MM67_HUNTEN      1    /* hundredths/tenths of seconds */
#define MM67_SEC         2    /* seconds */
#define MM67_MIN         3    /* minutes */
#define MM67_HOUR        4    /* hours */
#define MM67_DOW         5    /* day of the week */
#define MM67_DOM         6    /* day of the month */
#define MM67_MON         7    /* month */
#define MM67_AL_MSEC     8    /* milliseconds */
#define MM67_AL_HUNTEN   9    /* hundredths/tenths of seconds */
#define MM67_AL_SEC      10   /* seconds */
#define MM67_AL_MIN      11   /* minutes */
#define MM67_AL_HOUR     12   /* hours */
#define MM67_AL_DOW      13   /* day of the week */
#define MM67_AL_DOM      14   /* day of the month */
#define MM67_AL_MON      15   /* month */
#define MM67_AL_DONTCARE 0xc0 /* always match in compare */
#define MM67_ISTAT       16   /* IRQ status */
#define MM67_ICTRL       17   /* IRQ control */
#define MM67INT_COMPARE  0x01 /*  Compare */
#define MM67INT_TENTH    0x02 /*  Tenth */
#define MM67INT_SEC      0x04 /*  Second */
#define MM67INT_MIN      0x08 /*  Minute */
#define MM67INT_HOUR     0x10 /*  Hour */
#define MM67INT_DAY      0x20 /*  Day */
#define MM67INT_WEEK     0x40 /*  Week */
#define MM67INT_MON      0x80 /*  Month */
#define MM67_RSTCTR      18   /* reset counters */
#define MM67_RSTRAM      19   /* reset RAM */
#define MM67_STATUS      20   /* status bit */
#define MM67_GOCMD       21   /* GO Command */
#define MM67_STBYIRQ     22   /* standby IRQ */
#define MM67_TEST        31   /* test mode */

#ifdef ENABLE_ISARTC_LOG
int isartc_do_log = ENABLE_ISARTC_LOG;

static void
isartc_log(const char *fmt, ...)
{
    va_list ap;

    if (isartc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define isartc_log(fmt, ...)
#endif

/* Check if the current time matches a set alarm time. */
static int8_t
mm67_chkalrm(nvr_t *nvr, int8_t addr)
{
    int ret = 1;

    if ((addr != MM67_AL_MSEC) && ((nvr->regs[addr] & 0x0c) != 0x0c))
        ret = !((nvr->regs[addr & 0x07] ^ nvr->regs[addr]) & 0x0f);

    if (addr != MM67_AL_DOW) {
        if ((nvr->regs[addr] & 0xc0) != 0xc0)
            ret = ret && !((nvr->regs[addr & 0x07] ^ nvr->regs[addr]) & 0xf0);
    }

    return (int8_t) ret;
}

/*
 * This is called every second through the NVR/RTC hook.
 *
 * We fake a 'running' RTC by updating its registers on
 * each passing second. Not exactly accurate, but good
 * enough.
 *
 * Note that this code looks nasty because of all the
 * BCD to decimal vv going on.
 */
static void
mm67_tick(nvr_t *nvr, const int f_tenth, const int minute)
{
    rtcdev_t *dev  = (rtcdev_t *) nvr->data;
    uint8_t  *regs = nvr->regs;
    int       mon;
    int       year;
    int       f    = f_tenth;

    if (!minute) {
        /* Update and set interrupt if needed. */
        regs[MM67_SEC] = RTC_BCDINC(nvr->regs[MM67_SEC], 1);
        if (regs[MM67_ICTRL] & MM67INT_SEC)
            f = MM67INT_SEC;
    }

    /* Roll over? */
    if (minute || (regs[MM67_SEC] >= RTC_BCD(60))) {
        /* Update and set interrupt if needed. */
        regs[MM67_SEC] = RTC_BCD(0);
        regs[MM67_MIN] = RTC_BCDINC(regs[MM67_MIN], 1);
        if (!minute && (regs[MM67_ICTRL] & MM67INT_MIN))
            f = MM67INT_MIN;

        /* Roll over? */
        if (regs[MM67_MIN] >= RTC_BCD(60)) {
            /* Update and set interrupt if needed. */
            regs[MM67_MIN]  = RTC_BCD(0);
            regs[MM67_HOUR] = RTC_BCDINC(regs[MM67_HOUR], 1);
            if (!minute && (regs[MM67_ICTRL] & MM67INT_HOUR))
                f = MM67INT_HOUR;

            /* Roll over? */
            if (regs[MM67_HOUR] >= RTC_BCD(24)) {
                /* Update and set interrupt if needed. */
                regs[MM67_HOUR] = RTC_BCD(0);
                regs[MM67_DOW]  = RTC_BCDINC(regs[MM67_DOW], 1);
                if (!minute && (regs[MM67_ICTRL] & MM67INT_DAY))
                    f = MM67INT_DAY;

                /* Roll over? */
                if (regs[MM67_DOW] > RTC_BCD(7)) {
                    /* Update and set interrupt if needed. */
                    regs[MM67_DOW] = RTC_BCD(1);
                    if (!minute && (regs[MM67_ICTRL] & MM67INT_WEEK))
                        f = MM67INT_WEEK;
                }

                /* Roll over? */
                regs[MM67_DOM] = RTC_BCDINC(regs[MM67_DOM], 1);
                mon            = RTC_DCB(regs[MM67_MON]);
                if (dev->year != -1) {
                    if (dev->flags & FLAG_YEARBCD)
                        year = RTC_DCB(regs[dev->year]);
                    else
                        year = regs[dev->year];
                    if (dev->flags & FLAG_YEAR80)
                        year += 80;
                } else
                    year = 80;
                year += 1900;
                if (RTC_DCB(regs[MM67_DOM]) > nvr_get_days(mon, year)) {
                    /* Update and set interrupt if needed. */
                    regs[MM67_DOM] = RTC_BCD(1);
                    regs[MM67_MON] = RTC_BCDINC(regs[MM67_MON], 1);
                    if (!minute && (regs[MM67_ICTRL] & MM67INT_MON))
                        f = MM67INT_MON;

                    /* Roll over? */
                    if (regs[MM67_MON] > RTC_BCD(12)) {
                        /* Update. */
                        regs[MM67_MON] = RTC_BCD(1);
                        if (dev->year != -1) {
                            year++;
                            if (dev->flags & FLAG_YEAR80)
                                year -= 80;

                            if (dev->flags & FLAG_YEARBCD)
                                regs[dev->year] = RTC_BCD(year % 100);
                            else
                                regs[dev->year] = year % 100;
                        }
                    }
                }
            }
        }
    }

    if (!minute) {
        /* Check for programmed alarm interrupt. */
        if (regs[MM67_ICTRL] & MM67INT_COMPARE) {
            year = 1;
            for (mon = MM67_AL_MSEC; mon <= MM67_AL_MON; mon++)
                if (mon != dev->year)
                    year &= mm67_chkalrm(nvr, (int8_t) mon);
            f = year ? MM67INT_COMPARE : 0x00;
        }

        /* Raise the IRQ if needed (and if we have one..) */
        if (f != 0) {
            regs[MM67_ISTAT] = f;
            /* PS/2 Model 30's gate array masks RTC IRQ1 with bit 0 of port A1h (1 = masked). */
            if ((nvr->irq != -1) && ((dev->irq_mask == NULL) || !(*dev->irq_mask & 0x01)))
                picint(1 << nvr->irq);
        }
    }
}

/* Get the current NVR time. */
static void
mm67_time_get(nvr_t *nvr, struct tm *tm)
{
    const rtcdev_t *dev  = (rtcdev_t *) nvr->data;
    const uint8_t  *regs = nvr->regs;

    /* NVR is in BCD data mode. */
    tm->tm_sec  = RTC_DCB(regs[MM67_SEC]);
    tm->tm_min  = RTC_DCB(regs[MM67_MIN]);
    tm->tm_hour = RTC_DCB(regs[MM67_HOUR]);
    tm->tm_wday = (RTC_DCB(regs[MM67_DOW]) - 1);
    tm->tm_mday = RTC_DCB(regs[MM67_DOM]);
    tm->tm_mon  = (RTC_DCB(regs[MM67_MON]) - 1);
    if (dev->year != -1) {
        if (dev->flags & FLAG_YEARBCD)
            tm->tm_year = RTC_DCB(regs[dev->year]);
        else
            tm->tm_year = regs[dev->year];
        if (dev->flags & FLAG_YEAR80)
            tm->tm_year += 80;

        if ((dev->century != -1) && !(dev->flags & FLAG_YEAR80)) {
            if (dev->flags & FLAG_YEARBCD)
                tm->tm_year += (RTC_DCB(regs[dev->century]) * 100) - 1900;
            else
                tm->tm_year += (regs[dev->century] * 100) - 1900;
        }

#if ISARTC_DEBUG > 1
        isartc_log("ISARTC: get_time: year=%i [%02x]\n", tm->tm_year, regs[dev->year]);
#endif
    }
}

/* Set the current NVR time. */
static void
mm67_time_set(nvr_t *nvr, struct tm *tm)
{
    const rtcdev_t *dev  = (rtcdev_t *) nvr->data;
    uint8_t        *regs = nvr->regs;
    int             year;

    /* NVR is in BCD data mode. */
    regs[MM67_SEC]  = RTC_BCD(tm->tm_sec);
    regs[MM67_MIN]  = RTC_BCD(tm->tm_min);
    regs[MM67_HOUR] = RTC_BCD(tm->tm_hour);
    regs[MM67_DOW]  = RTC_BCD(tm->tm_wday + 1);
    regs[MM67_DOM]  = RTC_BCD(tm->tm_mday);
    regs[MM67_MON]  = RTC_BCD(tm->tm_mon + 1);
    if (dev->year != -1) {
        year = tm->tm_year;
        if (dev->flags & FLAG_YEAR80)
            year -= 80;
        if (dev->flags & FLAG_YEARBCD)
            regs[dev->year] = RTC_BCD(year % 100);
        else
            regs[dev->year] = year % 100;

        if (!(dev->flags & FLAG_YEAR80)) {
            if (dev->flags & FLAG_YEARBCD)
                regs[dev->century] = RTC_BCD((year + 1900) / 100);
            else
                regs[dev->century] = (year + 1900) / 100;
        }

#if ISARTC_DEBUG > 1
        isartc_log("ISARTC: set_time: [%02x] year=%i (%i)\n", regs[dev->year], year, tm->tm_year);
#endif
    }
}

static void
mm67_start(nvr_t *nvr)
{
    struct tm tm;

    /* Initialize the internal and chip times. */
    if (time_sync) {
        /* Use the internal clock's time. */
        nvr_time_get(&tm);
        mm67_time_set(nvr, &tm);
    } else {
        /* Set the internal clock from the chip time. */
        mm67_time_get(nvr, &tm);
        nvr_time_set(&tm);
    }
}

/* PS/2 Model 30 (8086) has a quirky subsecond timer that the BIOS relies
 * on. (Code is around F000:F970 in the 68X16[27]7 9/2/86 Rev. 0 BIOS.)
 *
 * The Int 15h AH=86h delay function implements a busywait by resetting the
 * the RTC's sub-second counters; it writes to MM67_RSTCTR at 0B2h and then
 * polls the high nibble of MM67MSEC (register 0) in a tight loop. Each time
 * the nibble changes, approx. 1ms has passed. This is not what advances
 * the seconds counter which is a separate hook. This delay is used for
 * things like waiting on the floppy to settle.
 *
 * The status register's bit 0 ("clock operating") is kept set here;
 * reads of the status register consume the flag (see mm67_read). This
 * timer re-arms it much faster than the BIOS's busywaiting window, so
 * every status poll should terminate promptly. However, this delay is not
 * always exactly 1ms. The hardware's internal tick is 150 microseconds
 * and it counts it down 7 times, which is 1.05 milliseconds. */
static void
mm67_msec_timer(void *priv)
{
    rtcdev_t *dev = (rtcdev_t *) priv;

    dev->msec_count = (dev->msec_count + 1) % 1000;

    int       f   = 0;
    int       t   = 0;
    const int msc = dev->msec_count;

    dev->nvr.regs[MM67_MSEC]   = ((msc % 10) << 4);
    dev->nvr.regs[MM67_HUNTEN] = RTC_BCD(msc / 10);

    if ((msc % 10) == 0) {
        if (((msc % 100) == 0) && (dev->nvr.regs[MM67_ICTRL] & MM67INT_TENTH))
            f = MM67INT_TENTH;

        if (msc == 0)
            t = 1;
    }

    if (t)
        mm67_tick(&dev->nvr, f, 0);
    else {
        /* Check for programmed alarm interrupt. */
        if (dev->nvr.regs[MM67_ICTRL] & MM67INT_COMPARE) {
            int a = 1;

            for (int i = MM67_AL_MSEC; i <= MM67_AL_MON; i++)
                if (i != dev->year)
                    a &= mm67_chkalrm(&dev->nvr, (int8_t) i);

            f = a ? MM67INT_COMPARE : 0x00;
        }

        /* Raise the IRQ if needed (and if we have one..) */
        if (f != 0) {
            dev->nvr.regs[MM67_ISTAT] = f;
            /* PS/2 Model 30's gate array masks RTC IRQ1 with bit 0 of port A1h (1 = masked). */
            if ((dev->nvr.irq != -1) && ((dev->irq_mask == NULL) || !(*dev->irq_mask & 0x01)))
                picint(1 << dev->nvr.irq);
        }
    }

    dev->nvr.regs[MM67_STATUS] = 0x01;

    timer_advance_u64(&dev->msec_timer, 1000 * TIMER_USEC);

    timer_set_delay_u64(&dev->rollover_timer, 150 * TIMER_USEC);
}

/* PS/2 Model 30 (8086) time handling: it stores the year as plain BCD in the
 * alarm centiseconds register, 9, and a single century bit in bit 0 of the
 * alarm day-of-month register,  14: set for 1900s, clear for 2000s. The
 * remaining bits of register 14 and the other alarm registers hold the
 * configuration and checksum data maintained by the BIOS, so only bit 0
 * may be touched. The built-in BIOS has problems with Y2K rollover which
 * IBM advised fixing by installing CMOSCLK.SYS for DOS 4.00+; we do not
 * attempt to fix this problems here. */
static void
m30_time_get(nvr_t *nvr, struct tm *tm)
{
    const uint8_t  *regs = nvr->regs;

    tm->tm_sec  = RTC_DCB(regs[MM67_SEC]);
    tm->tm_min  = RTC_DCB(regs[MM67_MIN]);
    tm->tm_hour = RTC_DCB(regs[MM67_HOUR]);
    tm->tm_wday = (RTC_DCB(regs[MM67_DOW]) - 1);
    tm->tm_mday = RTC_DCB(regs[MM67_DOM]);
    tm->tm_mon  = (RTC_DCB(regs[MM67_MON]) - 1);

    /* Year is BCD; century bit set means 19xx. */
    tm->tm_year = RTC_DCB(regs[MM67_AL_HUNTEN]);
    if (!(regs[MM67_AL_DOM] & 0x01))
        tm->tm_year += 100;
}

static void
m30_time_set(nvr_t *nvr, struct tm *tm)
{
    uint8_t  *regs = nvr->regs;

    regs[MM67_SEC]  = RTC_BCD(tm->tm_sec);
    regs[MM67_MIN]  = RTC_BCD(tm->tm_min);
    regs[MM67_HOUR] = RTC_BCD(tm->tm_hour);
    regs[MM67_DOW]  = RTC_BCD(tm->tm_wday + 1);
    regs[MM67_DOM]  = RTC_BCD(tm->tm_mday);
    regs[MM67_MON]  = RTC_BCD(tm->tm_mon + 1);

    /* Year in BCD; century bit set for 1900s, clear for 2000s. */
    regs[MM67_AL_HUNTEN] = RTC_BCD(tm->tm_year % 100);
    regs[MM67_AL_DOM]    = (regs[MM67_AL_DOM] & ~0x01) |
                           ((tm->tm_year < 100) ? 0x01 : 0x00);

    /* Record the month of the last RTC maintenance (register 8 high
     * nibble and register 13 low nibble, don't-care-encoded as the BIOS
     * writes them at F000:5D60). The POST year rollover check bumps the
     * year when the day-of-month is earlier than the recorded month, so
     * a freshly synchronized clock must carry the current month or the
     * very first Int 1Ah call after boot advances the year. */
    uint8_t maint_mon = (uint8_t)(tm->tm_mon + 1);
    regs[MM67_AL_MSEC] = (regs[MM67_AL_MSEC] & 0x0f) | 0xc0 |
                         ((maint_mon & 0x0c) << 2);
    regs[MM67_AL_DOW]  = (regs[MM67_AL_DOW] & 0x0f) | 0xc0 |
                         (maint_mon & 0x03);

    /* Update the IBM configuration checksum: The BIOS's NVR validity
     * check (F000:5C30) sums the nibbles of register 8's high nibble,
     * registers 9 through 0Ch (both nibbles) and the low nibbles of
     * registers 0Dh and 0Eh, truncates to six bits, and compares
     * against the stored value XOR 15h. The stored three two-bit
     * pieces are carried in the high nibble of 0Eh and both nibbles
     * of 0Fh (encoded as don't-care nibbles 0Ch through 0Fh, which
     * decode to the raw two-bit values) ... or at least I think
     * that's what it does. */
    uint8_t sum = (regs[MM67_AL_MSEC] >> 4) & 0x0f;
    for (unsigned i = MM67_AL_HUNTEN; i <= MM67_AL_HOUR; i++)
        sum += (regs[i] & 0x0f) + ((regs[i] >> 4) & 0x0f);
    sum += regs[MM67_AL_DOW] & 0x0f;
    sum += regs[MM67_AL_DOM] & 0x0f;
    sum  = (sum ^ 0x15) & 0x3f;

    regs[MM67_AL_DOM] = (regs[MM67_AL_DOM] & 0x0f) |
                        ((0x0c | ((sum >> 4) & 0x03)) << 4);
    regs[MM67_AL_MON] = ((0x0c | ((sum >> 2) & 0x03)) << 4) |
                        (0x0c | (sum & 0x03));

    /* The battery-backed registers changed so ask for a save. */
    nvr_dosave = 1;
}

static void
m30_start(nvr_t *nvr)
{
    struct tm tm;

    if (time_sync) {
        nvr_time_get(&tm);
        m30_time_set(nvr, &tm);
    } else {
        m30_time_get(nvr, &tm);
        nvr_time_set(&tm);
    }
}

/* Reset the RTC counters to a sane state. */
static void
mm67_reset(nvr_t *nvr)
{
    /* Initialize the RTC to a known state. */
    for (uint8_t i = MM67_MSEC; i <= MM67_MON; i++)
        nvr->regs[i] = RTC_BCD(0);
    nvr->regs[MM67_DOW] = RTC_BCD(1);
    nvr->regs[MM67_DOM] = RTC_BCD(1);
    nvr->regs[MM67_MON] = RTC_BCD(1);
    nvr->regs[MM67_STATUS] = 0x00;
}

/* Handle a READ operation from one of our registers. */
static uint8_t
mm67_read(uint16_t port, void *priv)
{
    rtcdev_t *     dev = (rtcdev_t *) priv;
    const uint16_t reg = (port - dev->base_addr) & 0x001f;
    uint8_t        ret;

    /* This chip is directly mapped on I/O. */
    cycles -= ISA_CYCLES(4);

    switch (reg) {
        default:
            ret = dev->nvr.regs[reg];
            break;

        case MM67_STATUS: /* STATUS (RO) */
            /* Bit 0 is the "clock operating" flag on the Model 30. The
             * BIOS requires it set when sampled (F000:5C17), and its
             * wait loops (Int 15h AH=86h, the Int 1Ah read paths) spin
             * until it clears. The sub-second timer keeps it set; a
             * read consumes the flag (returns it set on the first read,
             * clear on the next) so every one of those loops terminates
             * on its first or second iteration. If you don't do this,
             * the system hangs the first time it waits on the floppy to
             * settle. */
            ret = dev->nvr.regs[reg] & 0x01;
            if (dev->board == ISARTC_PS2M30)
                dev->nvr.regs[reg] = 0x00; /* consume the flag */
            break;

        case MM67_ISTAT: /* IRQ status (RO) */
            ret                = dev->nvr.regs[reg];
            dev->nvr.regs[reg] = 0x00;
            if (dev->irq != -1)
                picintc(1 << dev->irq);
            break;

        case MM67_AL_MSEC:
        case MM67_MSEC:
            ret                = dev->nvr.regs[reg] & 0xf0;
            break;

        case MM67_AL_DOW:
            ret                = dev->nvr.regs[reg] & 0x0f;
            break;

        case MM67_DOW:
            ret                = dev->nvr.regs[reg] & 0x07;
            break;
    }

    if ((reg <= MM67_MON) && timer_is_enabled(&dev->rollover_timer))
        dev->nvr.regs[MM67_STATUS] = 0x01;

#if ISARTC_DEBUG
    isartc_log("[%04X:%08X] ISARTC: read(%04x) = %02x\n", CS, cpu_state.pc, (port - dev->base_addr) & 0x001f, ret);
#endif

    return ret;
}

/* Handle a WRITE operation to one of our registers. */
static void
mm67_write(uint16_t port, uint8_t val, void *priv)
{
    rtcdev_t *     dev      = (rtcdev_t *) priv;
    const uint16_t reg      = (port - dev->base_addr) & 0x001f;
    uint8_t        masks[8] = { 0xf0, 0xff, 0x7f, 0x7f, 0x3f, 0x07, 0x3f, 0x1f };

#if ISARTC_DEBUG
    isartc_log("[%04X:%08X] ISARTC: write(%04x, %02x)\n", CS, cpu_state.pc, (port - dev->base_addr) & 0x001f, val);
#endif

    /* This chip is directly mapped on I/O. */
    cycles -= ISA_CYCLES(4);

    switch (reg) {
        case MM67_ISTAT: /* intr status (RO) */
            break;

        case MM67_ICTRL: /* intr control */
            dev->nvr.regs[MM67_ISTAT] = 0x00;
            dev->nvr.regs[reg]        = val;
            break;

        case MM67_RSTCTR:
            if (val == 0xff) {
                mm67_reset(&dev->nvr);
                dev->msec_count = 0;
                nvr_dosave = 1;
            }
            break;

        case MM67_RSTRAM:
            if (val == 0xff) {
                for (uint8_t i = MM67_AL_MSEC; i <= MM67_AL_MON; i++)
                    dev->nvr.regs[i] = RTC_BCD(0);
                nvr_dosave = 1;
            }
            break;

        case MM67_STATUS: /* STATUS (RO) */
            break;

        case MM67_GOCMD:
            isartc_log("RTC: write gocmd=%02x\n", val);

            if (RTC_DCB(dev->nvr.regs[MM67_SEC]) > 39)
                mm67_tick(&dev->nvr, 0, 1);
            dev->nvr.regs[MM67_SEC] = RTC_BCD(0);
            dev->nvr.regs[MM67_HUNTEN] = RTC_BCD(0);
            dev->nvr.regs[MM67_MSEC] = RTC_BCD(0);
            dev->msec_count = 0;
            nvr_dosave = 1;
            break;

        case MM67_STBYIRQ:
        case MM67_TEST:
            isartc_log("RTC: write %s=%02x\n", (reg == MM67_STBYIRQ) ? "stby" : "test", val);
            break;

        case MM67_AL_MSEC:
            dev->nvr.regs[reg] = val & 0xf0;
            nvr_dosave = 1;
            break;

        case MM67_AL_DOW:
            dev->nvr.regs[reg] = val & 0x0f;
            nvr_dosave = 1;
            break;

        case MM67_MSEC:
        case MM67_HUNTEN:
            dev->nvr.regs[reg] = val & masks[reg];
            dev->msec_count = (dev->nvr.regs[MM67_MSEC] >> 4) +
                              (RTC_DCB(dev->nvr.regs[MM67_HUNTEN]) * 10);
            nvr_dosave = 1;
            break;

        case MM67_SEC ... MM67_DOM:
            dev->nvr.regs[reg] = val & masks[reg];
            nvr_dosave = 1;
            break;

        default:
            dev->nvr.regs[reg] = val;
            nvr_dosave = 1;
            break;
    }
}

/*
   Multitech PC-500/PC-500+ onboard RTC 58167 device designed to use I/O port
   base+0 as register index and base+1 as register data read/write window,
   according to the official RTC utilities SDATE.EXE, STIME.EXE, and TODAY.EXE.

   The RTC utilities check the RTC millisecond counter first to determinate the
   presence of the RTC 58167 IC, so here implement the bogus_msec to fool them.

   Note by OBattler: This has been rectified by actually implementing the 1 kHz
   milisecond-resolution counter.
 */
static uint8_t rtc58167_index = 0x00;

static uint8_t
rtc58167_read(uint16_t port, void *priv)
{
    uint8_t ret = 0xff;

    switch (port)
    {
        case 0x2c0:
        case 0x300:
            ret = rtc58167_index;
            break;

        case 0x2c1:
        case 0x301:
            ret = mm67_read(((port - 1) + rtc58167_index), priv);
            break;

        default:
            break;
    }

    return ret;
}

static void
rtc58167_write(uint16_t port, uint8_t val, void *priv)
{
    switch (port)
    {
        case 0x2c0:
        case 0x300:
            rtc58167_index = val;
            break;

        case 0x2c1:
        case 0x301:
            mm67_write(((port - 1) + rtc58167_index), val, priv);
            break;

        default:
            break;
    }
}

/************************************************************************
 *                                                                      *
 *            Generic code for all supported chips.                     *
 *                                                                      *
 ************************************************************************/

/* Initialize the device for use. */
static void *
isartc_init(const device_t *info)
{
    rtcdev_t *dev;
    int       is_at = IS_AT(machine);
    is_at           = is_at || (machines[machine].init == machine_xt_xi8088_init);

    /* Create a device instance. */
    dev = (rtcdev_t *) calloc(1, sizeof(rtcdev_t));
    dev->name     = info->name;
    dev->board    = info->local;
    dev->irq      = -1;
    dev->year     = -1;
    dev->century  = -1;
    dev->nvr.data = dev;
    dev->nvr.size = 16;

    /* Do per-board initialization. */
    switch (dev->board) {
        case ISARTC_MM58167: /* Generic MM58167 RTC */
            {
                uint32_t rom_addr = device_get_config_hex20("bios_addr");
                if (rom_addr != 0)
                    rom_init(&dev->rom, ISARTC_ROM_MM58167_1,
                             rom_addr, 0x0800, 0x7ff, 0, MEM_MAPPING_EXTERNAL);

            }
        case ISARTC_EV170: /* Everex EV-170 Magic I/O */
            dev->flags |= FLAG_YEAR80;
            dev->base_addr   = device_get_config_hex16("base");
            dev->base_addrsz = 32;
            dev->irq         = (int8_t) device_get_config_int("irq");
            dev->f_rd        = mm67_read;
            dev->f_wr        = mm67_write;
            dev->nvr.reset   = mm67_reset;
            dev->nvr.start   = mm67_start;
            dev->nvr.tick    = NULL;
            dev->year        = MM67_AL_DOM; /* year, NON STANDARD */
            break;

        case ISARTC_PS2M30: /* Support for the 8086 based IBM PS/2 Model 30 */
            /* The Model 30 maps the 32 MM58167 registers into two I/O
             * windows: E0-EF hold registers 0-15 (counters and alarm RAM),
             * while B0-BF hold registers 16-31 (control and status). */
            dev->flags |= (FLAG_YEARBCD | FLAG_PS2);
            dev->base_addr   = 0x00a0;
            dev->base_addrsz = 16;
            dev->irq         = 1;
            dev->f_rd        = mm67_read;
            dev->f_wr        = mm67_write;
            dev->nvr.reset   = mm67_reset;
            dev->nvr.start   = m30_start;
            /* The once-per-second NVR hook advances the seconds counter;
             * the dedicated sub-second timer only ticks register 0 (the
             * hundredths register) on a millisecond cadence for the BIOS's
             * Int 15h AH=86h wait loop. */
            dev->nvr.tick    = NULL;
            /* The Mod. 30 stores the year in the alarm centiseconds
             * register 9 and the century in bit 0 of the alarm
             * day-of-month register 14. */
            dev->year        = MM67_AL_HUNTEN;
            dev->century     = MM67_AL_DOM;
            break;

        case ISARTC_DTK: /* DTK PII-147 Hexa I/O Plus */
            dev->flags |= FLAG_YEARBCD;
            dev->base_addr   = device_get_config_hex16("base");
            dev->base_addrsz = 32;
            dev->f_rd        = mm67_read;
            dev->f_wr        = mm67_write;
            dev->nvr.reset   = mm67_reset;
            dev->nvr.start   = mm67_start;
            dev->nvr.tick    = NULL;
            dev->year        = MM67_AL_HUNTEN; /* year, NON STANDARD */
            break;

        case ISARTC_P5PAK:  /* Paradise Systems 5PAK */
        case ISARTC_A6PAK:  /* AST SixPakPlus */
        case ISARTC_MPLUS2: /* AST MegaPlus II */
            dev->flags |= FLAG_YEAR80;
            dev->base_addr   = 0x02c0;
            dev->base_addrsz = 32;
            dev->irq         = (int8_t) device_get_config_int("irq");
            dev->f_rd        = mm67_read;
            dev->f_wr        = mm67_write;
            dev->nvr.reset   = mm67_reset;
            dev->nvr.start   = mm67_start;
            dev->nvr.tick    = NULL;
            dev->year        = MM67_AL_DOM; /* year, NON STANDARD */
            break;

        case ISARTC_VENDEX: /* Vendex HeadStart Turbo 888-XT RTC */
            dev->flags |= FLAG_YEAR80 | FLAG_YEARBCD;
            dev->base_addr   = 0x0300;
            dev->base_addrsz = 32;
            dev->f_rd        = mm67_read;
            dev->f_wr        = mm67_write;
            dev->nvr.reset   = mm67_reset;
            dev->nvr.start   = mm67_start;
            dev->nvr.tick    = NULL;
            dev->year        = MM67_AL_DOM; /* year, NON STANDARD */
            break;

        case ISARTC_RTC58167: /* Multitech PC-500/PC-500+ onboard RTC */
            dev->flags |= FLAG_YEARBCD;
            dev->base_addr   = machine_get_config_int("rtc_port");
            dev->base_addrsz = 8;
            dev->irq         = (int8_t) machine_get_config_int("rtc_irq");
            dev->f_rd        = rtc58167_read;
            dev->f_wr        = rtc58167_write;
            dev->nvr.reset   = mm67_reset;
            dev->nvr.start   = mm67_start;
            dev->nvr.tick    = NULL;
            dev->year        = MM67_AL_HUNTEN;  /* year,    NON STANDARD */
            dev->century     = MM67_AL_SEC;     /* century, NON STANDARD */
            break;

        default:
            break;
    }

    /* Say hello! */
    isartc_log("ISARTC: %s (I/O=%04XH", info->name, dev->base_addr);
    if (dev->irq != -1) {
        isartc_log(", IRQ%i", (int) dev->irq);
    }
    isartc_log(")\n");

    /* Set up an I/O port handler. */
    if ((dev->flags) & FLAG_PS2) {
        /* The Model 30 splits the 32 MM58167 registers across two I/O
         * windows: B0-BF (registers 16-31) and E0-EF (registers 0-15).
         * Both are decoded with a base of A0h and a 5-bit mask. */
        io_sethandler(0x00b0, 16,
                      dev->f_rd, NULL, NULL, dev->f_wr, NULL, NULL, dev);
        io_sethandler(0x00e0, 16,
                      dev->f_rd, NULL, NULL, dev->f_wr, NULL, NULL, dev);
    } else
        io_sethandler(dev->base_addr, dev->base_addrsz,
                      dev->f_rd, NULL, NULL, dev->f_wr, NULL, NULL, dev);

    /* Hook into the NVR backend. */
    dev->nvr.fn  = (char *) info->internal_name;
    dev->nvr.irq = dev->irq;
    if (!is_at)
        nvr_init(&dev->nvr);

    dev->msec_count = (dev->nvr.regs[MM67_MSEC] >> 4) +
                      (RTC_DCB(dev->nvr.regs[MM67_HUNTEN]) * 10);
    timer_add(&dev->msec_timer, mm67_msec_timer, dev, 0);
    timer_set_delay_u64(&dev->msec_timer, 1000 * TIMER_USEC);

    timer_add(&dev->rollover_timer, NULL, dev, 0);

    /* Let them know our device instance. */
    return ((void *) dev);
}

/* Remove the device from the system. */
static void
isartc_close(void *priv)
{
    rtcdev_t *dev = (rtcdev_t *) priv;

    /* Mirror the same logic the initialisation code uses for the Mod. 30. */
    if (timer_is_enabled(&dev->msec_timer))
        timer_disable(&dev->msec_timer);

    if (timer_is_enabled(&dev->rollover_timer))
        timer_disable(&dev->rollover_timer);

    if ((dev->flags) & FLAG_PS2) {
        io_removehandler(0x00b0, 16,
                         dev->f_rd, NULL, NULL, dev->f_wr, NULL, NULL, dev);
        io_removehandler(0x00e0, 16,
                         dev->f_rd, NULL, NULL, dev->f_wr, NULL, NULL, dev);
    } else
        io_removehandler(dev->base_addr, dev->base_addrsz,
                         dev->f_rd, NULL, NULL, dev->f_wr, NULL, NULL, dev);

    free(dev);
}

static const device_config_t ev170_config[] = {
  // clang-format off
    {
        .name           = "base",
		.description    = "Address",
		.type           = CONFIG_HEX16,
		.default_string = NULL,
		.default_int    = 0x02C0,
		.file_filter    = NULL,
		.spinner        = { 0 },
        .selection      = {
            { .description = "240H", .value = 0x0240 },
            { .description = "2C0H", .value = 0x02c0 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
		.description    = "IRQ",
		.type           = CONFIG_SELECTION,
		.default_string = NULL,
		.default_int    = -1,
		.file_filter    = NULL,
		.spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = -1 },
            { .description = "IRQ2",     .value =  2 },
            { .description = "IRQ5",     .value =  5 },
            { .description = "IRQ7",     .value =  7 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t ev170_device = {
    .name          = "Everex EV-170 Magic I/O",
    .internal_name = "ev170",
    .flags         = DEVICE_ISA,
    .local         = ISARTC_EV170,
    .init          = isartc_init,
    .close         = isartc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ev170_config
};

static const device_config_t pii147_config[] = {
  // clang-format off
    {
        .name           = "base",
		.description    = "Address",
		.type           = CONFIG_HEX16,
		.default_string = NULL,
		.default_int    = 0x0240,
		.file_filter    = NULL,
		.spinner        = { 0 },
        .selection      = {
            { .description = "Clock 1", .value = 0x0240 },
            { .description = "Clock 2", .value = 0x0340 },
            { .description = ""                         }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t pii147_device = {
    .name          = "DTK PII-147 Hexa I/O Plus",
    .internal_name = "pii147",
    .flags         = DEVICE_ISA,
    .local         = ISARTC_DTK,
    .init          = isartc_init,
    .close         = isartc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pii147_config
};

static const device_config_t p5pak_config[] = {
  // clang-format off
    {
        .name           = "irq",
		.description    = "IRQ",
		.type           = CONFIG_SELECTION,
		.default_string = NULL,
		.default_int    = -1,
		.file_filter    = NULL,
		.spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", -1 },
            { .description = "IRQ2",      2 },
            { .description = "IRQ3",      3 },
            { .description = "IRQ5",      5 },
            { .description = ""             }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t p5pak_device = {
    .name          = "Paradise Systems 5-PAK",
    .internal_name = "p5pak",
    .flags         = DEVICE_ISA,
    .local         = ISARTC_P5PAK,
    .init          = isartc_init,
    .close         = isartc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = p5pak_config
};

static const device_config_t a6pak_config[] = {
  // clang-format off
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = -1 },
            { .description = "IRQ2",     .value =  2 },
            { .description = "IRQ3",     .value =  3 },
            { .description = "IRQ5",     .value =  5 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t a6pak_device = {
    .name          = "AST SixPakPlus",
    .internal_name = "a6pak",
    .flags         = DEVICE_ISA,
    .local         = ISARTC_A6PAK,
    .init          = isartc_init,
    .close         = isartc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = a6pak_config
};

static const device_config_t mplus2_config[] = {
  // clang-format off
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = -1,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { "Disabled", -1 },
            { "IRQ2",      2 },
            { "IRQ3",      3 },
            { "IRQ5",      5 },
            { ""             }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t mplus2_device = {
    .name          = "AST MegaPlus II",
    .internal_name = "mplus2",
    .flags         = DEVICE_ISA,
    .local         = ISARTC_MPLUS2,
    .init          = isartc_init,
    .close         = isartc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mplus2_config
};

static const device_config_t mm58167_config[] = {
  // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x02C0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "240H", .value = 0x0240 },
            { .description = "2C0H", .value = 0x02c0 },
            { .description = "340H", .value = 0x0340 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = -1 },
            { .description = "IRQ2",     .value =  2 },
            { .description = "IRQ5",     .value =  5 },
            { .description = "IRQ7",     .value =  7 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xcc000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0x00000 },
            { .description = "C800H",    .value = 0xc8000 },
            { .description = "CA00H",    .value = 0xca000 },
            { .description = "CC00H",    .value = 0xcc000 },
            { .description = "CE00H",    .value = 0xce000 },
            { .description = "D000H",    .value = 0xd0000 },
            { .description = "D200H",    .value = 0xd2000 },
            { .description = "D400H",    .value = 0xd4000 },
            { .description = "D600H",    .value = 0xd6000 },
            { .description = "D800H",    .value = 0xd8000 },
            { .description = "DA00H",    .value = 0xda000 },
            { .description = "DC00H",    .value = 0xdc000 },
            { .description = "DE00H",    .value = 0xde000 },
            { .description = "E000H",    .value = 0xe0000 },
            { .description = "E200H",    .value = 0xe2000 },
            { .description = "E400H",    .value = 0xe4000 },
            { .description = "E600H",    .value = 0xe6000 },
            { .description = "E800H",    .value = 0xe8000 },
            { .description = "EA00H",    .value = 0xea000 },
            { .description = "EC00H",    .value = 0xec000 },
            { .description = "EE00H",    .value = 0xee000 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t mm58167_device = {
    .name          = "Generic MM58167 RTC",
    .internal_name = "rtc_mm58167",
    .flags         = DEVICE_ISA | DEVICE_SIDECAR,
    .local         = ISARTC_MM58167,
    .init          = isartc_init,
    .close         = isartc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mm58167_config
};

/* Onboard RTC devices */
const device_t vendex_xt_rtc_onboard_device = {
    .name          = "National Semiconductor MM58167 (Vendex)",
    .internal_name = "vendex_xt_rtc",
    .flags         = DEVICE_ISA,
    .local         = ISARTC_VENDEX,
    .init          = isartc_init,
    .close         = isartc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t rtc58167_device = {
    .name          = "RTC 58167 IC (Multitech)",
    .internal_name = "rtc58167_xt_rtc",
    .flags         = DEVICE_ISA,
    .local         = ISARTC_RTC58167,
    .init          = isartc_init,
    .close         = isartc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ibmps2m30_rtc_device = {
    .name          = "MM58167 RTC (IBM PS/2 model 30)",
    .internal_name = "rtc58167_ps2_rtc",
    .flags         = DEVICE_ISA,
    .local         = ISARTC_PS2M30,
    .init          = isartc_init,
    .close         = isartc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

void
ibmps2m30_rtc_inform(void *priv, uint8_t *a1_mask)
{
    rtcdev_t *dev = (rtcdev_t *) priv;

    dev->irq_mask = a1_mask;
}

typedef struct rp5c01a_t
{
    nvr_t nvr;
} rp5c01a_t;

/* Local variant of the 'struct tm' type. */
typedef struct tm intclk_t;

/*
 * Ricoh RP5C01A registers.
 *
 * In modes 0 and 1, registers are accessible as noted.
 * In modes 2 and 3, addresses 00 through 0c are treated
 * as non-volatile RAM, 4 bits wide.
 */
enum RTC_REGS {
    RTC_SECOND1 = 0,			/* BCD */
    RTC_SECOND10,
    RTC_ADJUST = RTC_SECOND10,		/* Bank 1 */
    RTC_MINUTE1,			/* BCD */
    RTC_MINUTE10,
    RTC_HOUR1,				/* BCD */
    RTC_HOUR10,
    RTC_WEEKDAY,			/* BCD, 1-base */
    RTC_DAY1,				/* BCD, 1-base */
    RTC_DAY10,
    RTC_MONTH1,				/* BCD, 1-base */
    RTC_MONTH10,
    RTC_SELECT24 = RTC_MONTH10,		/* Bank 1 */
    RTC_YEAR1,				/* BCD */
    RTC_LEAP = RTC_YEAR1,		/* Bank 1 */
    RTC_YEAR10,
    RTC_MODE,				/* MODE */
    RTC_TEST,				/* TEST */
    RTC_RESET				/* RESET */
};

/* Set the current NVR time. */
#define set_nibbles(a, v) regs[(a##10)] = ((v)/10) ; regs[(a##1)] = ((v)%10)
static void
rtc_time_set(uint8_t *regs, const intclk_t *clk)
{
    set_nibbles(RTC_SECOND, clk->tm_sec);
    set_nibbles(RTC_MINUTE, clk->tm_min);
    set_nibbles(RTC_HOUR, clk->tm_hour);
    regs[RTC_WEEKDAY] = (clk->tm_wday - 1);
    set_nibbles(RTC_DAY, clk->tm_mday);
    set_nibbles(RTC_MONTH, clk->tm_mon + 1);
    set_nibbles(RTC_YEAR, (clk->tm_year % 100));
    regs[RTC_LEAP | 0x10] = (clk->tm_year % 4);
    regs[RTC_WEEKDAY | 0x30] = (clk->tm_year / 100) ^ 1;
#ifdef ENABLE_ISARTC_LOG
    const int days = nvr_get_days(clk->tm_mon + 1, clk->tm_year + 1900);
    isartc_log("days = %i\n", days);
#endif
}

/* Get the current NVR time. */
#define get_nibbles(a) ((regs[(a##10)]*10)+regs[(a##1)])
static void
rtc_time_get(const uint8_t *regs, intclk_t *clk)
{
    clk->tm_sec = get_nibbles(RTC_SECOND);
    clk->tm_min = get_nibbles(RTC_MINUTE);
    clk->tm_hour = get_nibbles(RTC_HOUR);
    clk->tm_wday = regs[RTC_WEEKDAY] + 1;
    clk->tm_mday = get_nibbles(RTC_DAY);
    clk->tm_mon = get_nibbles(RTC_MONTH) - 1;
    const int cent = regs[RTC_WEEKDAY | 0x30] & 0x01;
    clk->tm_year = (get_nibbles(RTC_YEAR) + ((cent ^ 1) * 100));
}

/* Bump a dual-4bit-register. */
static int
rtc_bump(uint8_t *regs, int base, int min, int max)
{
    int k;

    if (base == RTC_YEAR1)
        k = (((regs[RTC_WEEKDAY | 0x30] & 0x01) ^ 0x01) * 100) +
            (regs[base + 1] * 10) + regs[base];
    else if ((base == RTC_HOUR1) && !(regs[RTC_SELECT24 | 0x10] & 0x01)) {
        /* 12 hour system. */
        k = (regs[base + 1] * 10) + regs[base];
        /* Treat 12 as 0, so we have 0 PM = noon and 0 AM = midnight. */
        k %= 12;
        if (regs[RTC_SELECT24 | 0x10] & 0x02)
            k += 12;
    } else
        k = (regs[base + 1] * 10) + regs[base];

    if (++k >= (max + min)) {
	/* Rollover, reset to 'min' and return. */
        if (base == RTC_YEAR1) {
            regs[base]                 = (min % 10);
            regs[base + 1]             = ((min / 10) % 10);
            regs[RTC_LEAP | 0x10]      = ((min + 1900) % 4);
            regs[RTC_WEEKDAY | 0x30]   = ((min / 100) ^ 1);
        } else if ((base == RTC_HOUR1) && !(regs[RTC_SELECT24 | 0x10] & 0x01)) {
            regs[base]                 = 0x02;
            regs[base + 1]             = 0x01;
            regs[RTC_SELECT24 | 0x10] &= ~0x02;
        } else {
            regs[base]                 = min;
            regs[base + 1]             = 0x00;
        }

        return 1;
    }

    /* No rollover, save the bumped value. */
    if (base == RTC_YEAR1) {
        regs[base]                = (k % 10);
        regs[base + 1]            = ((k / 10) % 10);
        regs[RTC_LEAP | 0x10]     = ((k + 1900) % 4);
        regs[RTC_WEEKDAY | 0x30]  = ((k / 100) ^ 1);
    } else if ((base == RTC_HOUR1) && !(regs[RTC_SELECT24 | 0x10] & 0x01)) {
        regs[RTC_SELECT24 | 0x10] = (k / 12) ? 0x02 : 0x00;
        k %= 12;
        if (k == 0) {
            regs[base]     = 0x02;
            regs[base + 1] = 0x01;
        } else {
            regs[base]     = (k % 10);
            regs[base + 1] = (k / 10);
        }
    } else {
        regs[base] = (k % 10);
        regs[base + 1] = (k / 10);
    }

    return 0;
}

/*
 * This is called every second through the NVR/RTC hook.
 *
 * We fake a 'running' RTC by updating its registers on
 * each passing second. Not exactly accurate, but good
 * enough.
 */
static void
rtc_ticker(nvr_t *nvr)
{
    uint8_t *regs = nvr->regs;

    /* Only if RTC is running.. */
    if (! (regs[RTC_MODE] & 0x08))
        return;

    if (rtc_bump(regs, RTC_SECOND1, 0, 60)) {
        if (rtc_bump(regs, RTC_MINUTE1, 0, 60)) {
            if (rtc_bump(regs, RTC_HOUR1, 0, 24)) {
                const int mon = get_nibbles(RTC_MONTH);
                const int cent = nvr->regs[RTC_WEEKDAY | 0x30] & 0x01;
                const int yr = (2000 - (cent * 100)) + get_nibbles(RTC_YEAR);
                const int days = nvr_get_days(mon, yr);

                if (rtc_bump(regs, RTC_DAY1, 1, days)) {
                    if (rtc_bump(regs, RTC_MONTH1, 1, 12))
                        rtc_bump(regs, RTC_YEAR1, 0, 199);
                }
            }
        }
    }
}

static void
rtc_start(nvr_t *nvr)
{
    intclk_t clk;

    /* Initialize the internal and chip times. */
    if (time_sync != TIME_SYNC_DISABLED) {
	/* Use the internal clock's time. */
	nvr_time_get(&clk);
	rtc_time_set(nvr->regs, &clk);
    } else {
        /* Set the internal clock from the chip time. */
	rtc_time_get(nvr->regs, &clk);
	nvr_time_set(&clk);
    }
}


/* Reset the machine's NVR to a sane state. */
static void
rtc_reset(nvr_t *nvr)
{
    /* Initialize the RTC to a known state. */
    memset(nvr->regs, 0x00, sizeof(nvr->regs));

    /* Not needed, chip is 0-based. */
    nvr->regs[RTC_WEEKDAY]         = 0x00;
    nvr->regs[RTC_DAY1]            = 0x01;
    nvr->regs[RTC_MONTH1]          = 0x01;
    nvr->regs[RTC_YEAR10]          = 0x08;
    nvr->regs[RTC_SELECT24 | 0x10] = 0x01;
    /* Register 0x36 has century in bit 0 = 0 = 21st, 1 = 20th. */
    nvr->regs[RTC_WEEKDAY | 0x30]  = 0x01;
}


/* Write to one of the RTC registers. */
static void
rtc_write(uint16_t addr, uint8_t val, void *priv)
{
    nvr_t *  nvr = (nvr_t *)priv;

    /* Point to the correct bank. */
    uint8_t *ptr = &nvr->regs[16 * (nvr->regs[RTC_MODE] & 0x03)];

    isartc_log("Zenith: rtc_wr(%04x, %02x)\n", addr, val);

    switch (addr & 0x000f) {
        case RTC_MODE:
        case RTC_TEST:
        case RTC_RESET:
            nvr->regs[addr & 0x000f] = (val & 0x0f);
            nvr_dosave = 1;
            break;

        case RTC_ADJUST:
            ptr[addr & 0x000f] = (val & 0x0f);
            nvr_dosave = 1;

            if (((nvr->regs[RTC_MODE] & 0x03) == 0x01) && (val & 0x01)) {
                const int s = (nvr->regs[RTC_SECOND10] * 10) +
                               nvr->regs[RTC_SECOND1];
                if ((s >= 0) && (s <= 29))
                    nvr->regs[RTC_SECOND1] = nvr->regs[RTC_SECOND10] = 0x00;
                else if ((s >= 30) && (s <= 59)) {
                    for (int i = 0; i < (30 - s); i++)
                        rtc_ticker(nvr);
                }
            }
            break;

        default:
            ptr[addr & 0x000f] = (val & 0x0f);
            nvr_dosave = 1;
            break;
    }
}

/* Read from one of the RTC registers. */
static uint8_t
rtc_read(uint16_t addr, void *priv)
{
    const nvr_t *  nvr = (nvr_t *) priv;
    uint8_t        ret;

    /* Point to the correct bank. */
    const uint8_t *ptr = &nvr->regs[16 * (nvr->regs[RTC_MODE] & 0x03)];

    switch (addr & 0x000f) {
        default:
            ret = ptr[addr & 0x000f];
            break;

        case RTC_MODE:
        case RTC_TEST:
        case RTC_RESET:
            /* Write-only registers */
            ret = 0x00;
            break;
    }

    ret &= 0x0f;

    isartc_log("Zenith: rtc_rd(%04x) = %02x\n", addr, ret);

    return ret;
}

static void
rp5c01a_close(void *priv)
{
    rp5c01a_t *dev = (rp5c01a_t *) priv;

    /* Make sure NVR gets saved. */
    nvr_dosave = 1;
    nvr_save();

    free(dev);
}

static void *
rp5c01a_init(const device_t *info)
{
    rp5c01a_t *dev = (rp5c01a_t *) calloc(1, sizeof(rp5c01a_t));

    /* Set up and initialize the Ricoh RP5C15 RTC. */
    dev->nvr.size = 64;
    dev->nvr.irq = -1;
    dev->nvr.reset = rtc_reset;
    dev->nvr.start = rtc_start;
    dev->nvr.tick = rtc_ticker;
    nvr_init(&dev->nvr);
    io_sethandler(0x0050, 16,
                  rtc_read,NULL,NULL,
                  rtc_write,NULL,NULL, &dev->nvr);

    return dev;
}

const device_t rp5c01a_zenith_device = {
    .name          = "Ricoh RP5C01A RTC (Zenith)",
    .internal_name = "rp5c01a_zenith_rtc",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = rp5c01a_init,
    .close         = rp5c01a_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static const struct {
    const device_t *dev;
} boards[] = {
    // clang-format off
    { &device_none     },
    { &ev170_device    },
    { &pii147_device   },
    { &p5pak_device    },
    { &a6pak_device    },
    { &mplus2_device   },
    { &mm58167_device  },
    { NULL             }
    // clang-format on
};

void
isartc_reset(void)
{
    if (isartc_type == 0)
        return;

    /* Add the device to the system. */
    device_add(boards[isartc_type].dev);
}

const char *
isartc_get_internal_name(int board)
{
    return device_get_internal_name(boards[board].dev);
}

int
isartc_get_from_internal_name(const char *str)
{
    int c = 0;

    while (boards[c].dev != NULL) {
        if (!strcmp(boards[c].dev->internal_name, str))
            return c;
        c++;
    }

    /* Not found. */
    return 0;
}

const device_t *
isartc_get_device(int board)
{
    return (boards[board].dev);
}

int
isartc_has_config(int board)
{
    if (boards[board].dev == NULL)
        return 0;

    return (boards[board].dev->config ? 1 : 0);
}

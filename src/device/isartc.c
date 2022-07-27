/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of a Clock/RTC Card for the ISA PC/XT.
 *
 *		Systems starting with the PC/XT had, by default, a realtime
 *		clock and NVR chip on the mainboard. The BIOS stored config
 *		data in the NVR, and the system could maintain time and date
 *		using the RTC.
 *
 *		Originally, PC systems did not have this, and they first did
 *		show up in non-IBM clone systems. Shortly after, expansion
 *		cards with this function became available for the PC's (ISA)
 *		bus, and they came in many forms and designs.
 *
 *		This implementation offers some of those boards:
 *
 *		  Everex EV-170 (using NatSemi MM58167 chip)
 *		  DTK PII-147 Hexa I/O Plus (using UMC 82C8167 chip)
 *
 *		and more will follow as time permits.
 *
 * NOTE:	The IRQ functionalities have been implemented, but not yet
 *		tested, as I need to write test software for them first :)
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2018 Fred N. van Kempen.
 *
 *		Redistribution and  use  in source  and binary forms, with
 *		or  without modification, are permitted  provided that the
 *		following conditions are met:
 *
 *		1. Redistributions of  source  code must retain the entire
 *		   above notice, this list of conditions and the following
 *		   disclaimer.
 *
 *		2. Redistributions in binary form must reproduce the above
 *		   copyright  notice,  this list  of  conditions  and  the
 *		   following disclaimer in  the documentation and/or other
 *		   materials provided with the distribution.
 *
 *		3. Neither the  name of the copyright holder nor the names
 *		   of  its  contributors may be used to endorse or promote
 *		   products  derived from  this  software without specific
 *		   prior written permission.
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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/ui.h>
#include <86box/plat.h>
#include <86box/pic.h>
#include <86box/isartc.h>


#define ISARTC_EV170	0
#define ISARTC_DTK		1
#define ISARTC_P5PAK	2
#define ISARTC_A6PAK	3

#define ISARTC_DEBUG	0


typedef struct {
    const char	*name;				/* board name */
    uint8_t	board;				/* board type */

    uint8_t	flags;				/* various flags */
#define FLAG_YEAR80	0x01			/* YEAR byte is base-80 */
#define FLAG_YEARBCD	0x02			/* YEAR byte is in BCD */

    int8_t	irq;				/* configured IRQ channel */
    int8_t	base_addrsz;
    uint32_t	base_addr;			/* configured I/O address */

    /* Fields for the specific driver. */
    void	(*f_wr)(uint16_t, uint8_t, void *);
    uint8_t	(*f_rd)(uint16_t, void *);
    int8_t	year;				/* register for YEAR value */
    char	pad[3];

    nvr_t	nvr;				/* RTC/NVR */
} rtcdev_t;


/************************************************************************
 *									*
 *		    Driver for the NatSemi MM58167 chip.		*
 *									*
 ************************************************************************/
#define MM67_REGS		32

/* Define the RTC chip registers - see datasheet, pg4. */
#define MM67_MSEC		0	/* milliseconds */
#define MM67_HUNTEN		1	/* hundredths/tenths of seconds */
#define MM67_SEC		2	/* seconds */
#define MM67_MIN		3	/* minutes */
#define MM67_HOUR		4	/* hours */
#define MM67_DOW		5	/* day of the week */
#define MM67_DOM		6	/* day of the month */
#define MM67_MON		7	/* month */
#define MM67_AL_MSEC		8	/* milliseconds */
#define MM67_AL_HUNTEN		9	/* hundredths/tenths of seconds */
#define MM67_AL_SEC		10	/* seconds */
#define MM67_AL_MIN		11	/* minutes */
#define MM67_AL_HOUR		12	/* hours */
#define MM67_AL_DOW		13	/* day of the week */
#define MM67_AL_DOM		14	/* day of the month */
#define MM67_AL_MON		15	/* month */
# define MM67_AL_DONTCARE	0xc0	/* always match in compare */
#define MM67_ISTAT		16	/* IRQ status */
#define MM67_ICTRL		17	/* IRQ control */
# define MM67INT_COMPARE	0x01	/*  Compare */
# define MM67INT_TENTH		0x02	/*  Tenth */
# define MM67INT_SEC		0x04	/*  Second */
# define MM67INT_MIN		0x08	/*  Minute */
# define MM67INT_HOUR		0x10	/*  Hour */
# define MM67INT_DAY		0x20	/*  Day */
# define MM67INT_WEEK		0x40	/*  Week */
# define MM67INT_MON		0x80	/*  Month */
#define MM67_RSTCTR		18	/* reset counters */
#define MM67_RSTRAM		19	/* reset RAM */
#define MM67_STATUS		20	/* status bit */
#define MM67_GOCMD		21	/* GO Command */
#define MM67_STBYIRQ		22	/* standby IRQ */
#define MM67_TEST		31	/* test mode */

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
#define isartc_log(fmt, ...)
#endif


/* Check if the current time matches a set alarm time. */
static int8_t
mm67_chkalrm(nvr_t *nvr, int8_t addr)
{
    return((nvr->regs[addr-MM67_AL_SEC+MM67_SEC] == nvr->regs[addr]) ||
	   ((nvr->regs[addr] & MM67_AL_DONTCARE) == MM67_AL_DONTCARE));
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
mm67_tick(nvr_t *nvr)
{
    rtcdev_t *dev = (rtcdev_t *)nvr->data;
    uint8_t *regs = nvr->regs;
    int mon, year, f = 0;

    /* Update and set interrupt if needed. */
    regs[MM67_SEC] = RTC_BCDINC(nvr->regs[MM67_SEC], 1);
    if (regs[MM67_ICTRL] & MM67INT_SEC) f = MM67INT_SEC;

    /* Roll over? */
    if (regs[MM67_SEC] >= RTC_BCD(60)) {
	/* Update and set interrupt if needed. */
	regs[MM67_SEC] = RTC_BCD(0);
	regs[MM67_MIN] = RTC_BCDINC(regs[MM67_MIN], 1);
	if (regs[MM67_ICTRL] & MM67INT_MIN) f = MM67INT_MIN;

	/* Roll over? */
	if (regs[MM67_MIN] >= RTC_BCD(60)) {
		/* Update and set interrupt if needed. */
		regs[MM67_MIN] = RTC_BCD(0);
		regs[MM67_HOUR] = RTC_BCDINC(regs[MM67_HOUR], 1);
		if (regs[MM67_ICTRL] & MM67INT_HOUR) f = MM67INT_HOUR;

		/* Roll over? */
		if (regs[MM67_HOUR] >= RTC_BCD(24)) {
			/* Update and set interrupt if needed. */
			regs[MM67_HOUR] = RTC_BCD(0);
			regs[MM67_DOW] = RTC_BCDINC(regs[MM67_DOW], 1);
			if (regs[MM67_ICTRL] & MM67INT_DAY) f = MM67INT_DAY;

			/* Roll over? */
			if (regs[MM67_DOW] > RTC_BCD(7)) {
				/* Update and set interrupt if needed. */
				regs[MM67_DOW] = RTC_BCD(1);
				if (regs[MM67_ICTRL] & MM67INT_WEEK) f = MM67INT_WEEK;
			}

			/* Roll over? */
			regs[MM67_DOM] = RTC_BCDINC(regs[MM67_DOM], 1);
			mon = RTC_DCB(regs[MM67_MON]);
			if (dev->year != -1) {
				year = RTC_DCB(regs[dev->year]);
				if (dev->flags & FLAG_YEAR80)
					year += 80;
			} else
				year = 80;
			year += 1900;
			if (RTC_DCB(regs[MM67_DOM]) > nvr_get_days(mon, year)) {
				/* Update and set interrupt if needed. */
				regs[MM67_DOM] = RTC_BCD(1);
				regs[MM67_MON] = RTC_BCDINC(regs[MM67_MON], 1);
				if (regs[MM67_ICTRL] & MM67INT_MON) f = MM67INT_MON;

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

    /* Check for programmed alarm interrupt. */
    if (regs[MM67_ICTRL] & MM67INT_COMPARE) {
	year = 1;
	for (mon = MM67_AL_SEC; mon <= MM67_AL_MON; mon++)
		if (mon != dev->year)
			year &= mm67_chkalrm(nvr, mon);
	f = year ? MM67INT_COMPARE : 0x00;
    }

    /* Raise the IRQ if needed (and if we have one..) */
    if (f != 0) {
	regs[MM67_ISTAT] = f;
	if (nvr->irq != -1)
		picint(1 << nvr->irq);
    }
}


/* Get the current NVR time. */
static void
mm67_time_get(nvr_t *nvr, struct tm *tm)
{
    rtcdev_t *dev = (rtcdev_t *)nvr->data;
    uint8_t *regs = nvr->regs;

    /* NVR is in BCD data mode. */
    tm->tm_sec = RTC_DCB(regs[MM67_SEC]);
    tm->tm_min = RTC_DCB(regs[MM67_MIN]);
    tm->tm_hour = RTC_DCB(regs[MM67_HOUR]);
    tm->tm_wday = (RTC_DCB(regs[MM67_DOW]) - 1);
    tm->tm_mday = RTC_DCB(regs[MM67_DOM]);
    tm->tm_mon = (RTC_DCB(regs[MM67_MON]) - 1);
    if (dev->year != -1) {
	if (dev->flags & FLAG_YEARBCD)
    		tm->tm_year = RTC_DCB(regs[dev->year]);
	  else
    		tm->tm_year = regs[dev->year];
	if (dev->flags & FLAG_YEAR80)
    		tm->tm_year += 80;
#ifdef MM67_CENTURY
	tm->tm_year += (regs[MM67_CENTURY] * 100) - 1900;
#endif
#if ISARTC_DEBUG > 1
	isartc_log("ISARTC: get_time: year=%i [%02x]\n", tm->tm_year, regs[dev->year]);
#endif
    }
}


/* Set the current NVR time. */
static void
mm67_time_set(nvr_t *nvr, struct tm *tm)
{
    rtcdev_t *dev = (rtcdev_t *)nvr->data;
    uint8_t *regs = nvr->regs;
    int year;

    /* NVR is in BCD data mode. */
    regs[MM67_SEC] = RTC_BCD(tm->tm_sec);
    regs[MM67_MIN] = RTC_BCD(tm->tm_min);
    regs[MM67_HOUR] = RTC_BCD(tm->tm_hour);
    regs[MM67_DOW] = RTC_BCD(tm->tm_wday + 1);
    regs[MM67_DOM] = RTC_BCD(tm->tm_mday);
    regs[MM67_MON] = RTC_BCD(tm->tm_mon + 1);
    if (dev->year != -1) {
	year = tm->tm_year;
	if (dev->flags & FLAG_YEAR80)
		year -= 80;
	if (dev->flags & FLAG_YEARBCD)
		regs[dev->year] = RTC_BCD(year % 100);
	  else
		regs[dev->year] = year % 100;
#ifdef MM67_CENTURY
	regs[MM67_CENTURY] = (year + 1900) / 100;
#endif
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


/* Reset the RTC counters to a sane state. */
static void
mm67_reset(nvr_t *nvr)
{
    int i;

    /* Initialize the RTC to a known state. */
    for (i = MM67_MSEC; i <= MM67_MON; i++)
	nvr->regs[i] = RTC_BCD(0);
    nvr->regs[MM67_DOW] = RTC_BCD(1);
    nvr->regs[MM67_DOM] = RTC_BCD(1);
    nvr->regs[MM67_MON] = RTC_BCD(1);
}


/* Handle a READ operation from one of our registers. */
static uint8_t
mm67_read(uint16_t port, void *priv)
{
    rtcdev_t *dev = (rtcdev_t *)priv;
    int reg = port - dev->base_addr;
    uint8_t ret = 0xff;

    /* This chip is directly mapped on I/O. */
    cycles -= ISA_CYCLES(4);

    switch(reg) {
	case MM67_ISTAT:		/* IRQ status (RO) */
		ret = dev->nvr.regs[reg];
		dev->nvr.regs[reg] = 0x00;
		if (dev->irq != -1)
			picintc(1 << dev->irq);
		break;

	default:
		ret = dev->nvr.regs[reg];
		break;
    }

#if  ISARTC_DEBUG
    isartc_log("ISARTC: read(%04x) = %02x\n", port-dev->base_addr, ret);
#endif

    return(ret);
}


/* Handle a WRITE operation to one of our registers. */
static void
mm67_write(uint16_t port, uint8_t val, void *priv)
{
    rtcdev_t *dev = (rtcdev_t *)priv;
    int reg = port - dev->base_addr;
    int i;

#if  ISARTC_DEBUG
    isartc_log("ISARTC: write(%04x, %02x)\n", port-dev->base_addr, val);
#endif

    /* This chip is directly mapped on I/O. */
    cycles -= ISA_CYCLES(4);

    switch(reg) {
	case MM67_ISTAT:		/* intr status (RO) */
		break;

	case MM67_ICTRL:		/* intr control */
		dev->nvr.regs[MM67_ISTAT] = 0x00;
		dev->nvr.regs[reg] = val;
		break;

	case MM67_RSTCTR:
		if (val == 0xff)
			mm67_reset(&dev->nvr);
		break;

	case MM67_RSTRAM:
		if (val == 0xff) {
			for (i = MM67_AL_MSEC; i <= MM67_AL_MON; i++)
				dev->nvr.regs[i] = RTC_BCD(0);
			dev->nvr.regs[MM67_DOW] = RTC_BCD(1);
			dev->nvr.regs[MM67_DOM] = RTC_BCD(1);
			dev->nvr.regs[MM67_MON] = RTC_BCD(1);
			if (dev->year != -1) {
				val = (dev->flags & FLAG_YEAR80) ? 0 : 80;
				if (dev->flags & FLAG_YEARBCD)
					dev->nvr.regs[dev->year] = RTC_BCD(val);
				  else
					dev->nvr.regs[dev->year] = val;
#ifdef MM67_CENTURY
				dev->nvr.regs[MM67_CENTURY] = 19;
#endif
			}
		}
		break;

	case MM67_STATUS:	/* STATUS (RO) */
		break;

	case MM67_GOCMD:
isartc_log("RTC: write gocmd=%02x\n", val);
		break;

	case MM67_STBYIRQ:
isartc_log("RTC: write stby=%02x\n", val);
		break;

	case MM67_TEST:
isartc_log("RTC: write test=%02x\n", val);
		break;

	default:
		dev->nvr.regs[reg] = val;
		break;
    }
}


/************************************************************************
 *									*
 *		    Generic code for all supported chips.		*
 *									*
 ************************************************************************/

/* Initialize the device for use. */
static void *
isartc_init(const device_t *info)
{
    rtcdev_t *dev;
    int is_at = IS_AT(machine);
    is_at = is_at || !strcmp(machine_get_internal_name(), "xi8088");

    /* Create a device instance. */
    dev = (rtcdev_t *)malloc(sizeof(rtcdev_t));
    memset(dev, 0x00, sizeof(rtcdev_t));
    dev->name = info->name;
    dev->board = info->local;
    dev->irq = -1;
    dev->year = -1;
    dev->nvr.data = dev;
    dev->nvr.size = 16;

    /* Do per-board initialization. */
    switch(dev->board) {
	case ISARTC_EV170:		/* Everex EV-170 Magic I/O */
		dev->flags |= FLAG_YEAR80;
		dev->base_addr = device_get_config_hex16("base");
		dev->base_addrsz = 32;
		dev->irq = device_get_config_int("irq");
		dev->f_rd = mm67_read;
		dev->f_wr = mm67_write;
		dev->nvr.reset = mm67_reset;
		dev->nvr.start = mm67_start;
		dev->nvr.tick = mm67_tick;
		dev->year = MM67_AL_DOM;	/* year, NON STANDARD */
		break;

	case ISARTC_DTK:		/* DTK PII-147 Hexa I/O Plus */
		dev->flags |= FLAG_YEARBCD;
		dev->base_addr = device_get_config_hex16("base");
		dev->base_addrsz = 32;
		dev->f_rd = mm67_read;
		dev->f_wr = mm67_write;
		dev->nvr.reset = mm67_reset;
		dev->nvr.start = mm67_start;
		dev->nvr.tick = mm67_tick;
		dev->year = MM67_AL_HUNTEN;	/* year, NON STANDARD */
		break;

	case ISARTC_P5PAK:		/* Paradise Systems 5PAK */
	case ISARTC_A6PAK:		/* AST SixPakPlus */
		dev->flags |= FLAG_YEAR80;
		dev->base_addr = 0x02c0;
		dev->base_addrsz = 32;
		dev->irq = device_get_config_int("irq");
		dev->f_rd = mm67_read;
		dev->f_wr = mm67_write;
		dev->nvr.reset = mm67_reset;
		dev->nvr.start = mm67_start;
		dev->nvr.tick = mm67_tick;
		dev->year = MM67_AL_DOM;	/* year, NON STANDARD */
		break;

	default:
		break;
    }

    /* Say hello! */
    isartc_log("ISARTC: %s (I/O=%04XH", info->name, dev->base_addr);
    if (dev->irq != -1)
	isartc_log(", IRQ%i", dev->irq);
    isartc_log(")\n");

    /* Set up an I/O port handler. */
    io_sethandler(dev->base_addr, dev->base_addrsz,
		  dev->f_rd,NULL,NULL, dev->f_wr,NULL,NULL, dev);

    /* Hook into the NVR backend. */
    dev->nvr.fn = isartc_get_internal_name(isartc_type);
    dev->nvr.irq = dev->irq;
    if (!is_at)
	nvr_init(&dev->nvr);

    /* Let them know our device instance. */
    return((void *)dev);
}


/* Remove the device from the system. */
static void
isartc_close(void *priv)
{
    rtcdev_t *dev = (rtcdev_t *)priv;

    io_removehandler(dev->base_addr, dev->base_addrsz,
		     dev->f_rd,NULL,NULL, dev->f_wr,NULL,NULL, dev);

    if (dev->nvr.fn != NULL)
	free(dev->nvr.fn);

    free(dev);
}

static const device_config_t ev170_config[] = {
// clang-format off
    {
        "base", "Address", CONFIG_HEX16, "", 0x02C0, "", { 0 },
        {
            { "240H", 0x0240 },
            { "2C0H", 0x02c0 },
            { ""             }
        },
    },
    {
        "irq", "IRQ", CONFIG_SELECTION, "", -1, "", { 0 },
        {
            { "Disabled", -1 },
            { "IRQ2",      2 },
            { "IRQ5",      5 },
            { "IRQ7",      7 },
            { ""             }
        },
    },
    { "", "", -1 }
// clang-format on
};

static const device_t ev170_device = {
    .name = "Everex EV-170 Magic I/O",
    .internal_name = "ev170",
    .flags = DEVICE_ISA,
    .local = ISARTC_EV170,
    .init = isartc_init,
    .close = isartc_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ev170_config
};

static const device_config_t pii147_config[] = {
// clang-format off
    {
        "base", "Address", CONFIG_HEX16, "", 0x0240, "", { 0 },
        {
            { "Clock 1", 0x0240 },
            { "Clock 2", 0x0340 },
            { "" }
        },
    },
    { "", "", -1 }
// clang-format on
};

static const device_t pii147_device = {
    .name = "DTK PII-147 Hexa I/O Plus",
    .internal_name = "pii147",
    .flags = DEVICE_ISA,
    .local = ISARTC_DTK,
    .init = isartc_init,
    .close = isartc_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = pii147_config
};

static const device_config_t p5pak_config[] = {
// clang-format off
    {
        "irq", "IRQ", CONFIG_SELECTION, "", -1, "", { 0 },
        {
            { "Disabled", -1 },
            { "IRQ2",      2 },
            { "IRQ3",      3 },
            { "IRQ5",      5 },
            { ""             }
        },
    },
    { "", "", -1 }
// clang-format on
};

static const device_t p5pak_device = {
    .name = "Paradise Systems 5-PAK",
    .internal_name = "p5pak",
    .flags = DEVICE_ISA,
    .local = ISARTC_P5PAK,
    .init = isartc_init,
    .close = isartc_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = p5pak_config
};

static const device_config_t a6pak_config[] = {
// clang-format off
    {
        "irq", "IRQ", CONFIG_SELECTION, "", -1, "", { 0 },
        {
            { "Disabled", -1 },
            { "IRQ2",      2 },
            { "IRQ3",      3 },
            { "IRQ5",      5 },
            { ""             }
        },
    },
    { "", "", -1 }
// clang-format on
};

static const device_t a6pak_device = {
    .name = "AST SixPakPlus",
    .internal_name = "a6pak",
    .flags = DEVICE_ISA,
    .local = ISARTC_A6PAK,
    .init = isartc_init,
    .close = isartc_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = a6pak_config
};

static const device_t isartc_none_device = {
    .name = "None",
    .internal_name = "none",
    .flags = 0,
    .local = 0,
    .init = NULL,
    .close = NULL,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

static const struct {
    const device_t	*dev;
} boards[] = {
// clang-format off
    { &isartc_none_device },
    { &ev170_device       },
    { &pii147_device      },
    { &p5pak_device       },
    { &a6pak_device       },
    { NULL                },
// clang-format on
};

void
isartc_reset(void)
{
    if (isartc_type == 0) return;

    /* Add the device to the system. */
    device_add(boards[isartc_type].dev);
}

char *
isartc_get_internal_name(int board)
{
    return device_get_internal_name(boards[board].dev);
}


int
isartc_get_from_internal_name(char *s)
{
    int c = 0;

    while (boards[c].dev != NULL) {
	if (! strcmp(boards[c].dev->internal_name, s))
		return(c);
	c++;
    }

    /* Not found. */
    return(0);
}


const device_t *
isartc_get_device(int board)
{
    return(boards[board].dev);
}

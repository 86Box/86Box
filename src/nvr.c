/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implement a more-or-less defacto-standard RTC/NVRAM.
 *
 *		When IBM released the PC/AT machine, it came standard with a
 *		battery-backed RTC chip to keep the time of day, something
 *		that was optional on standard PC's with a myriad variants
 *		being put on the market, often on cheap multi-I/O cards.
 *
 *		The PC/AT had an on-board DS12885-series chip ("the black
 *		block") which was an RTC/clock chip with onboard oscillator
 *		and a backup battery (hence the big size.) The chip also had
 *		a smal amount of RAM bytes available to the user, which was
 *		used by IBM's ROM BIOS to store machine configuration data.
 *
 *		Since then, pretty much any PC has an implementation of that
 *		device, which became known as the "nvr" or "cmos".
 *
 * NOTES	Info extracted from the data sheets:
 *
 *		* The century register at location 32h is a BCD register
 *		  designed to automatically load the BCD value 20 as the
 *		  year register changes from 99 to 00.  The MSB of this
 *		  register is not affected when the load of 20 occurs,
 *		  and remains at the value written by the user.
 *
 *		* Rate Selector (RS3:RS0)
 *		  These four rate-selection bits select one of the 13
 *		  taps on the 15-stage divider or disable the divider
 *		  output.  The tap selected can be used to generate an
 *		  output square wave (SQW pin) and/or a periodic interrupt.
 *
 *		  The user can do one of the following:
 *		   - enable the interrupt with the PIE bit;
 *		   - enable the SQW output pin with the SQWE bit;
 *		   - enable both at the same time and the same rate; or
 *		   - enable neither.
 *
 *		  Table 3 lists the periodic interrupt rates and the square
 *		  wave frequencies that can be chosen with the RS bits.
 *		  These four read/write bits are not affected by !RESET.
 *
 *		* Oscillator (DV2:DV0)
 *		  These three bits are used to turn the oscillator on or
 *		  off and to reset the countdown chain.  A pattern of 010
 *		  is the only combination of bits that turn the oscillator
 *		  on and allow the RTC to keep time.  A pattern of 11x
 *		  enables the oscillator but holds the countdown chain in
 *		  reset.  The next update occurs at 500ms after a pattern
 *		  of 010 is written to DV0, DV1, and DV2.
 *
 *		* Update-In-Progress (UIP)
 *		  This bit is a status flag that can be monitored. When the
 *		  UIP bit is a 1, the update transfer occurs soon.  When
 *		  UIP is a 0, the update transfer does not occur for at
 *		  least 244us.  The time, calendar, and alarm information
 *		  in RAM is fully available for access when the UIP bit
 *		  is 0.  The UIP bit is read-only and is not affected by
 *		  !RESET.  Writing the SET bit in Register B to a 1
 *		  inhibits any update transfer and clears the UIP status bit.
 *
 *		* Daylight Saving Enable (DSE)
 *		  This bit is a read/write bit that enables two daylight
 *		  saving adjustments when DSE is set to 1.  On the first
 *		  Sunday in April (or the last Sunday in April in the
 *		  MC146818A), the time increments from 1:59:59 AM to
 *		  3:00:00 AM.  On the last Sunday in October when the time
 *		  first reaches 1:59:59 AM, it changes to 1:00:00 AM.
 *
 *		  When DSE is enabled, the internal logic test for the
 *		  first/last Sunday condition at midnight.  If the DSE bit
 *		  is not set when the test occurs, the daylight saving
 *		  function does not operate correctly.  These adjustments
 *		  do not occur when the DSE bit is 0. This bit is not
 *		  affected by internal functions or !RESET.
 *
 *		* 24/12
 *		  The 24/12 control bit establishes the format of the hours
 *		  byte. A 1 indicates the 24-hour mode and a 0 indicates
 *		  the 12-hour mode.  This bit is read/write and is not
 *		  affected by internal functions or !RESET.
 *
 *		* Data Mode (DM)
 *		  This bit indicates whether time and calendar information
 *		  is in binary or BCD format.  The DM bit is set by the
 *		  program to the appropriate format and can be read as
 *		  required.  This bit is not modified by internal functions
 *		  or !RESET. A 1 in DM signifies binary data, while a 0 in
 *		  DM specifies BCD data.
 *
 *		* Square-Wave Enable (SQWE)
 *		  When this bit is set to 1, a square-wave signal at the
 *		  frequency set by the rate-selection bits RS3-RS0 is driven
 *		  out on the SQW pin.  When the SQWE bit is set to 0, the
 *		  SQW pin is held low. SQWE is a read/write bit and is
 *		  cleared by !RESET.  SQWE is low if disabled, and is high
 *		  impedance when VCC is below VPF. SQWE is cleared to 0 on
 *		  !RESET.
 *
 *		* Update-Ended Interrupt Enable (UIE)
 *		  This bit is a read/write bit that enables the update-end
 *		  flag (UF) bit in Register C to assert !IRQ.  The !RESET
 *		  pin going low or the SET bit going high clears the UIE bit.
 *		  The internal functions of the device do not affect the UIE
 *		  bit, but is cleared to 0 on !RESET.
 *
 *		* Alarm Interrupt Enable (AIE)
 *		  This bit is a read/write bit that, when set to 1, permits
 *		  the alarm flag (AF) bit in Register C to assert !IRQ.  An
 *		  alarm interrupt occurs for each second that the three time
 *		  bytes equal the three alarm bytes, including a don't-care
 *		  alarm code of binary 11XXXXXX.  The AF bit does not
 *		  initiate the !IRQ signal when the AIE bit is set to 0.
 *		  The internal functions of the device do not affect the AIE
 *		  bit, but is cleared to 0 on !RESET.
 *
 *		* Periodic Interrupt Enable (PIE)
 *		  The PIE bit is a read/write bit that allows the periodic
 *		  interrupt flag (PF) bit in Register C to drive the !IRQ pin
 *		  low.  When the PIE bit is set to 1, periodic interrupts are
 *		  generated by driving the !IRQ pin low at a rate specified
 *		  by the RS3-RS0 bits of Register A.  A 0 in the PIE bit
 *		  blocks the !IRQ output from being driven by a periodic
 *		  interrupt, but the PF bit is still set at the periodic
 *		  rate.  PIE is not modified b any internal device functions,
 *		  but is cleared to 0 on !RESET.
 *
 *		* SET
 *		  When the SET bit is 0, the update transfer functions
 *		  normally by advancing the counts once per second.  When
 *		  the SET bit is written to 1, any update transfer is
 *		  inhibited, and the program can initialize the time and
 *		  calendar bytes without an update occurring in the midst of
 *		  initializing. Read cycles can be executed in a similar
 *		  manner. SET is a read/write bit and is not affected by
 *		  !RESET or internal functions of the device.
 *
 *		* Update-Ended Interrupt Flag (UF)
 *		  This bit is set after each update cycle. When the UIE
 *		  bit is set to 1, the 1 in UF causes the IRQF bit to be
 *		  a 1, which asserts the !IRQ pin.  This bit can be
 *		  cleared by reading Register C or with a !RESET. 
 *
 *		* Alarm Interrupt Flag (AF)
 *		  A 1 in the AF bit indicates that the current time has
 *		  matched the alarm time.  If the AIE bit is also 1, the
 *		  !IRQ pin goes low and a 1 appears in the IRQF bit. This
 *		  bit can be cleared by reading Register C or with a
 *		  !RESET.
 *
 *		* Periodic Interrupt Flag (PF)
 *		  This bit is read-only and is set to 1 when an edge is
 *		  detected on the selected tap of the divider chain.  The
 *		  RS3 through RS0 bits establish the periodic rate. PF is
 *		  set to 1 independent of the state of the PIE bit.  When
 *		  both PF and PIE are 1s, the !IRQ signal is active and
 *		  sets the IRQF bit. This bit can be cleared by reading
 *		  Register C or with a !RESET.
 *
 *		* Interrupt Request Flag (IRQF)
 *		  The interrupt request flag (IRQF) is set to a 1 when one
 *		  or more of the following are true:
 *		   - PF == PIE == 1
 *		   - AF == AIE == 1
 *		   - UF == UIE == 1
 *		  Any time the IRQF bit is a 1, the !IRQ pin is driven low.
 *		  All flag bits are cleared after Register C is read by the
 *		  program or when the !RESET pin is low.
 *
 *		* Valid RAM and Time (VRT)
 *		  This bit indicates the condition of the battery connected
 *		  to the VBAT pin. This bit is not writeable and should
 *		  always be 1 when read.  If a 0 is ever present, an
 *		  exhausted internal lithium energy source is indicated and
 *		  both the contents of the RTC data and RAM data are
 *		  questionable.  This bit is unaffected by !RESET.
 *
 *		This file implements an internal RTC clock, plus a generic
 *		version of the RTC/NVRAM chip, including the later update
 *		(DS12887A) which implemented a "century" register to be 
 *		compatible with Y2K.
 *
 * Version:	@(#)nvr.c	1.0.11	2017/10/28
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Mahod,
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include "86Box.h"
#include "ibm.h"
#include "cpu/cpu.h"
#include "pic.h"
#include "timer.h"
#include "device.h"
#include "machine/machine.h"
#include "plat.h"
#include "nvr.h"


int64_t	enable_sync;		/* configuration variable: enable time sync */
int64_t	nvr_dosave;		/* NVR is dirty, needs saved */


static nvr_t	*saved_nvr = NULL;
static int8_t	days_in_month[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
static struct {
   int64_t sec;
   int64_t min;
   int64_t hour;
   int64_t mday;
   int64_t mon;
   int64_t year;
}		intclk;		/* the internal clock */


/* Determine whether or not the year is leap. */
static int
is_leap(int64_t year)
{
    if (year % 400 == 0) return(1);
    if (year % 100 == 0) return(0);
    if (year % 4 == 0) return(1);

    return(0);
}


/* Determine the days in the current month. */
static int
get_days(int64_t month, int64_t year)
{
    if (month != 2)
	return(days_in_month[month - 1]);

    return(is_leap(year) ? 29 : 28);
}


/* One more second has passed, update the internal clock. */
static void
rtc_tick(void)
{
    /* Ping the internal clock. */
    if (++intclk.sec == 60) {
	intclk.sec = 0;
	intclk.min++;
    }
    if (intclk.min == 60) {
	intclk.min = 0;
	intclk.hour++;
    }
    if (intclk.hour == 24) {
	intclk.hour = 0;
	intclk.mday++;
    }
    if (intclk.mday == (get_days(intclk.mon, intclk.year) + 1)) {
	intclk.mday = 1;
	intclk.mon++;
    }
    if (intclk.mon == 13) {
	intclk.mon = 1;
	intclk.year++;
    }
}


/* Store the broken-down local time into the NVR. */
static void
rtc_getnvr(uint8_t *nvr, struct tm *tm)
{
    if (nvr[RTC_REGB] & REGB_DM) {
	/* NVR is in Binary data mode. */
	nvr[RTC_SECONDS] = tm->tm_sec;
	nvr[RTC_MINUTES] = tm->tm_min;
	nvr[RTC_DOW]     = tm->tm_wday+1;
	nvr[RTC_DOM]     = tm->tm_mday;
	nvr[RTC_MONTH]   = tm->tm_mon+1;
	nvr[RTC_YEAR]    = tm->tm_year%100;

	if (nvr[RTC_REGB] & REGB_2412) {
		/* NVR is in 24h mode. */
		nvr[RTC_HOURS] = tm->tm_hour;
	} else {
		/* NVR is in 12h mode. */
		nvr[RTC_HOURS] = (tm->tm_hour % 12) ? (tm->tm_hour % 12) : 12;
		if (tm->tm_hour > 11)
			nvr[RTC_HOURS] |= RTC_AMPM;
	}
    } else {
	/* NVR is in BCD data mode. */
	nvr[RTC_SECONDS] = RTC_BCD(tm->tm_sec);
	nvr[RTC_MINUTES] = RTC_BCD(tm->tm_min);
	nvr[RTC_DOW]     = RTC_BCD(tm->tm_wday+1);
	nvr[RTC_DOM]     = RTC_BCD(tm->tm_mday);
	nvr[RTC_MONTH]   = RTC_BCD(tm->tm_mon+1);
	nvr[RTC_YEAR]    = RTC_BCD(tm->tm_year%100);

	if (nvr[RTC_REGB] & REGB_2412) {
		/* NVR is in 24h mode. */
		nvr[RTC_HOURS] = RTC_BCD(tm->tm_hour);
	} else {
		/* NVR is in 12h mode. */
		nvr[RTC_HOURS] = (tm->tm_hour % 12)
					? RTC_BCD(tm->tm_hour % 12)
					: RTC_BCD(12);
		if (tm->tm_hour > 11)
			nvr[RTC_HOURS] |= RTC_AMPM;
	}
    }
}


/* Load local time from the NVR. */
static void
rtc_setnvr(uint8_t *nvr)
{
    int64_t temp;

    if (nvr[RTC_REGB] & REGB_DM) {
	intclk.sec = nvr[RTC_SECONDS];
        intclk.min = nvr[RTC_MINUTES];
        temp = nvr[RTC_HOURS];
        intclk.mday = nvr[RTC_DOM];
        intclk.mon = nvr[RTC_MONTH];
        intclk.year = nvr[RTC_YEAR];
        intclk.year += 1900;
    } else {
	intclk.sec = RTC_DCB(nvr[RTC_SECONDS]);
        intclk.min = RTC_DCB(nvr[RTC_MINUTES]);
        temp = RTC_DCB(nvr[RTC_HOURS]);
        intclk.mday = RTC_DCB(nvr[RTC_DOM]);
        intclk.mon = RTC_DCB(nvr[RTC_MONTH]);
        intclk.year = RTC_DCB(nvr[RTC_YEAR]);
        intclk.year += (RTC_DCB(nvr[RTC_CENTURY]) * 100);
    }

    /* Adjust for 12/24 hour mode. */
    if (nvr[RTC_REGB] & REGB_2412)
	intclk.hour = temp;
      else
	intclk.hour = ((temp & ~RTC_AMPM) % 12) + ((temp & RTC_AMPM) ? 12 : 0);
}


static void
rtc_sync(uint8_t *nvr)
{
    struct tm *tm;
    time_t now;

    /* Get the current time of day, and convert to local time. */
    (void)time(&now);
    tm = localtime(&now);

    /* Set the internal clock. */
    intclk.sec = tm->tm_sec;
    intclk.min = tm->tm_min;
    intclk.hour = tm->tm_hour;
    intclk.mday = tm->tm_mday;
    intclk.mon = tm->tm_mon+1;
    intclk.year = tm->tm_year+1900;

    /* Set the NVR registers. */
    rtc_getnvr(nvr, tm);
}


/* This is the RTC one-second timer. */
static void
onesec_timer(void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;

    if (++nvr->onesec_cnt >= 100) {
	if (! (nvr->regs[RTC_REGB] & REGB_SET)) {
		nvr->upd_stat = REGA_UIP;

		/* Update the system RTC. */
		rtc_tick();

		if (nvr->hook != NULL)
			(*nvr->hook)(nvr);

		/* Re-calculate the timer. */
		nvr_recalc();

		nvr->upd_ecount = (int64_t)((244.0 + 1984.0) * TIMER_USEC);
	}
	nvr->onesec_cnt = 0;
    }

    nvr->onesec_time += (int64_t)(10000 * TIMER_USEC);
}


/* Check if the current time matches a set alarm time. */
static int
check_alarm(nvr_t *nvr, int64_t addr)
{
#define ALARM_DONTCARE 0xc0
    return((nvr->regs[addr+1] == nvr->regs[addr]) ||
	   ((nvr->regs[addr+1] & ALARM_DONTCARE) == ALARM_DONTCARE));
}


/* This is the general update timer. */
static void
update_timer(void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;
    struct tm tm;
    int64_t dom, mon, yr, cent, sum, wd;

    if (! (nvr->regs[RTC_REGB] & REGB_SET)) {
	/* Get the current time from the internal clock. */
	tm.tm_sec = intclk.sec;
	tm.tm_min = intclk.min;
	tm.tm_hour = intclk.hour;
         dom = intclk.mday;
         mon = intclk.mon;
         yr = intclk.year % 100;
         cent = ((intclk.year - yr) / 100) % 4;
         sum = dom+mon+yr+cent;
         wd = ((sum + 6) % 7);
	tm.tm_wday = wd;
	tm.tm_mday = intclk.mday;
	tm.tm_mon = intclk.mon-1;
	tm.tm_year = intclk.year-1900;
	rtc_getnvr(nvr->regs, &tm);

	/* Clear update status. */
	nvr->upd_stat = 0;

	if (check_alarm(nvr, RTC_SECONDS) &&
	    check_alarm(nvr, RTC_MINUTES) &&
	    check_alarm(nvr, RTC_HOURS)) {
		nvr->regs[RTC_REGC] |= REGC_AF;
		if (nvr->regs[RTC_REGB] & REGB_AIE) {
			nvr->regs[RTC_REGC] |= REGC_IRQF;

			/* Generate an interrupt. */
			if (nvr->irq != -1)
				picint(1<<nvr->irq);
		}
	}

	/*
	 * The flag and interrupt should be issued
	 * on update ended, not started.
	 */
	nvr->regs[RTC_REGC] |= REGC_UF;
	if (nvr->regs[RTC_REGB] & REGB_UIE) {
		nvr->regs[RTC_REGC] |= REGC_IRQF;

		/* Generate an interrupt. */
		if (nvr->irq != -1)
			picint(1<<nvr->irq);
	}
    }

    nvr->upd_ecount = 0;
}


static void
ticker_timer(void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;
    int64_t c;

    if (! (nvr->regs[RTC_REGA] & REGA_RS)) {
	nvr->rtctime = 0x7fffffff;
	return;
    }

    /* Update our ticker interval. */
    c = 1 << ((nvr->regs[RTC_REGA] & REGA_RS) - 1);
    nvr->rtctime += (int64_t)(RTCCONST*c*(1<<TIMER_SHIFT));

    nvr->regs[RTC_REGC] |= REGC_PF;
    if (nvr->regs[RTC_REGB] & REGB_PIE) {
	nvr->regs[RTC_REGC] |= REGC_IRQF;

	/* Generate an interrupt. */
	if (nvr->irq != -1)
		picint(1<<nvr->irq);
    }
}


/* Set one of the chip's registers. */
static void
nvr_write(nvr_t *nvr, uint16_t reg, uint8_t val)
{
    int64_t c, old;

    old = nvr->regs[reg];
    switch(reg) {
	case RTC_REGA:
		nvr->regs[reg] = val;
		if (val & REGA_RS) {
			c = 1 << ((val & REGA_RS) - 1);
			nvr->rtctime += (int64_t)(RTCCONST*c*(1<<TIMER_SHIFT));
		} else {
			nvr->rtctime = 0x7fffffff;
		}
		break;

	case RTC_REGB:
		nvr->regs[reg] = val;
		if (((old^val) & REGB_SET) && (val&REGB_SET)) {
			/* This has to be done according to the datasheet. */
			nvr->regs[RTC_REGA] &= ~REGA_UIP;

			/* This also has to happen per the specification. */
			nvr->regs[RTC_REGB] &= ~REGB_UIE;
		}
		break;

	case RTC_REGC:		/* R/O */
	case RTC_REGD:		/* R/O */
		break;

	default:		/* non-RTC registers are just NVRAM */
		if (nvr->regs[reg] != val) {
			nvr->regs[reg] = val;

			nvr_dosave = 1;
		}
		break;
    }

    if ((reg < RTC_REGA) || (reg == RTC_CENTURY)) {
	if ((reg != 1) && (reg != 3) && (reg != 5)) {
		if ((old != val) && !enable_sync) {
			/* Update internal clock. */
			rtc_setnvr(nvr->regs);

			nvr_dosave = 1;
		}
	}
    }
}


/* Get one of the chip's registers. */
static uint8_t
nvr_read(nvr_t *nvr, uint16_t reg)
{
    uint8_t ret = 0xff;

    switch(reg) {
	case RTC_REGA:
		ret = (nvr->regs[RTC_REGA] & 0x7f) | nvr->upd_stat;
		break;

	case RTC_REGC:
		picintc(1<<nvr->irq);
		ret = nvr->regs[RTC_REGC];
		nvr->regs[RTC_REGC] = 0x00;
		break;

	case RTC_REGD:
		nvr->regs[RTC_REGD] |= REGD_VRT;
		ret = nvr->regs[RTC_REGD];
		break;

	default:
		ret = nvr->regs[reg];
		break;
    }

    return(ret);
}


/* Initialize the virtual RTC/NVRAM chip. */
void
nvr_init(nvr_t *nvr)
{
    char temp[32];
    int64_t c;

    /* Clear some of it. */
    nvr->upd_stat = 0;
    nvr->upd_ecount = 0;
    nvr->onesec_time = 0;
    nvr->onesec_cnt = 0;
    memset(&intclk, 0x00, sizeof(intclk));

    /* Pre-initialize the NVR file's name here. */
    sprintf(temp, "%s.nvr", machine_get_internal_name_ex(machine));
    c = strlen(temp)+1;
    nvr->fname = (wchar_t *)malloc(c*sizeof(wchar_t));
    mbstowcs(nvr->fname, temp, c);

    /* Set up our local handlers. */
    nvr->get = nvr_read;
    nvr->set = nvr_write;

    /* Set up our timers. */
    timer_add(ticker_timer, &nvr->rtctime, TIMER_ALWAYS_ENABLED, nvr);

    timer_add(onesec_timer, &nvr->onesec_time, TIMER_ALWAYS_ENABLED, nvr);

    timer_add(update_timer, &nvr->upd_ecount, &nvr->upd_ecount, nvr);

    /* It does not need saving yet. */
    nvr_dosave = 0;

    /* Save the NVR data pointer. */
    saved_nvr = nvr;
}


/* Re-calculate the timer values. */
void
nvr_recalc(void)
{
    int64_t c, nt;

    /* Make sure we have been initialized. */
    if (saved_nvr == NULL) return;

    c = 1 << ((saved_nvr->regs[RTC_REGA] & REGA_RS) - 1);
    nt = (int64_t)(RTCCONST * c * (1<<TIMER_SHIFT));
    if (saved_nvr->rtctime > nt)
	saved_nvr->rtctime = nt;
}


/*
 * Load an NVR from file.
 *
 * This function does two things, really.  It clear and initializes
 * the RTC and NVRAM areas, sets up defaults for the RTC part, and
 * then attempts to load data from a saved file.
 *
 * Either way, after that loading, it will continue to configure
 * the local RTC to operate, so it can update either the local RTC,
 * and/or the supplied by a client.
 */
int64_t
nvr_load(void)
{
    FILE *f;
    int64_t c;

    /* Make sure we have been initialized. */
    if (saved_nvr == NULL) return(0);

    /* Clear out any old data. */
    memset(saved_nvr->regs, 0xff, sizeof(saved_nvr->regs));

    /* Set the defaults. */
    memset(saved_nvr->regs, 0x00, RTC_REGS);
    saved_nvr->regs[RTC_DOM] = 1;
    saved_nvr->regs[RTC_MONTH] = 1;
    saved_nvr->regs[RTC_YEAR] = RTC_BCD(80);
    saved_nvr->regs[RTC_CENTURY] = RTC_BCD(19);

    if (saved_nvr->load == NULL) {
	/* We are responsible for loading. */
	f = NULL;
	if (saved_nvr->mask != 0) {
		pclog("Opening NVR file: %ls...\n", saved_nvr->fname);
		f = plat_fopen(nvr_path(saved_nvr->fname), L"rb");
	}

	if (f != NULL) {
		/* Read NVR contents from file. */
		fread(saved_nvr->regs, sizeof(saved_nvr->regs), 1, f);
		(void)fclose(f);
	}
    } else {
	/* OK, use alternate function. */
	(*saved_nvr->load)(saved_nvr->fname);
    }

    /* Update the internal clock state based on the NVR registers. */
    if (enable_sync)
	rtc_sync(saved_nvr->regs);
      else
	rtc_setnvr(saved_nvr->regs);

    /* Get the local RTC running! */
    saved_nvr->regs[RTC_REGA] = (REGA_RS2|REGA_RS1);
    saved_nvr->regs[RTC_REGB] = REGB_2412;
    c = 1 << ((saved_nvr->regs[RTC_REGA] & REGA_RS) - 1);
    saved_nvr->rtctime += (int64_t)(RTCCONST * c * (1<<TIMER_SHIFT));

    return(1);
}


/* Save the current NVR to a file. */
int64_t
nvr_save(void)
{
    FILE *f;

    /* Make sure we have been initialized. */
    if (saved_nvr == NULL) return(0);

    if (saved_nvr->save == NULL) {
	/* We are responsible for saving. */
	f = NULL;
	if (saved_nvr->mask != 0) {
		pclog("Saving NVR file: %ls...\n", saved_nvr->fname);
		f = plat_fopen(nvr_path(saved_nvr->fname), L"wb");
	}

	if (f != NULL) {
		/* Save NVR contents to file. */
		(void)fwrite(saved_nvr->regs, sizeof(saved_nvr->regs), 1, f);
		(void)fclose(f);
	}
    } else {
	/* OK, use alternate function. */
	(*saved_nvr->save)(saved_nvr->fname);
    }

    /* Device is clean again. */
    nvr_dosave = 0;

    return(1);
}


/* Get an absolute path to the NVR folder. */
wchar_t *
nvr_path(wchar_t *str)
{
    static wchar_t temp[1024];

    /* Get the full prefix in place. */
    memset(temp, 0x00, sizeof(temp));
    wcscpy(temp, cfg_path);
    wcscat(temp, NVR_PATH);

    /* Create the directory if needed. */
    if (! plat_dir_check(temp))
	plat_dir_create(temp);

    /* Now append the actual filename. */
#ifdef _WIN32
    wcscat(temp, L"\\");
#else
    wcscat(temp, L"/");
#endif
    wcscat(temp, str);

    return(temp);
}


/* Open or create a file in the NVR area. */
FILE *
nvr_fopen(wchar_t *str, wchar_t *mode)
{
    return(plat_fopen(nvr_path(str), mode));
}

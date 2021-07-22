/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
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
 *		a small amount of RAM bytes available to the user, which was
 *		used by IBM's ROM BIOS to store machine configuration data.
 *		Later versions and clones used the 12886 and/or 1288(C)7
 *		series, or the MC146818 series, all with an external battery.
 *		Many of those batteries would create corrosion issues later
 *		on in mainboard life...
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
 *		This file implements a generic version of the RTC/NVRAM chip,
 *		including the later update (DS12887A) which implemented a
 *		"century" register to be compatible with Y2K.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Mahod,
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2008-2020 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <time.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>


/* RTC registers and bit definitions. */
#define RTC_SECONDS	0
#define RTC_ALSECONDS	1
# define AL_DONTCARE	0xc0		/* Alarm time is not set */
#define RTC_MINUTES	2
#define RTC_ALMINUTES	3
#define RTC_HOURS	4
# define RTC_AMPM	0x80		/* PM flag if 12h format in use */
#define RTC_ALHOURS	5
#define RTC_DOW		6
#define RTC_DOM		7
#define RTC_MONTH	8
#define RTC_YEAR	9
#define RTC_REGA	10
# define REGA_UIP	0x80
# define REGA_DV2	0x40
# define REGA_DV1	0x20
# define REGA_DV0	0x10
# define REGA_DV	0x70
# define REGA_RS3	0x08
# define REGA_RS2	0x04
# define REGA_RS1	0x02
# define REGA_RS0	0x01
# define REGA_RS	0x0f
#define RTC_REGB	11
# define REGB_SET	0x80
# define REGB_PIE	0x40
# define REGB_AIE	0x20
# define REGB_UIE	0x10
# define REGB_SQWE	0x08
# define REGB_DM	0x04
# define REGB_2412	0x02
# define REGB_DSE	0x01
#define RTC_REGC	12
# define REGC_IRQF	0x80
# define REGC_PF	0x40
# define REGC_AF	0x20
# define REGC_UF	0x10
#define RTC_REGD	13
# define REGD_VRT	0x80
#define RTC_CENTURY_AT	0x32		/* century register for AT etc */
#define RTC_CENTURY_PS	0x37		/* century register for PS/1 PS/2 */
#define RTC_ALDAY	0x7D		/* VIA VT82C586B - alarm day */
#define RTC_ALMONTH	0x7E		/* VIA VT82C586B - alarm month */
#define RTC_CENTURY_VIA	0x7F		/* century register for VIA VT82C586B */

#define RTC_ALDAY_SIS 0x7E		/* Day of Month Alarm for SiS */
#define RTC_ALMONT_SIS 0x7F		/* Month Alarm for SiS */

#define RTC_REGS	14		/* number of registers */

#define FLAG_LS_HACK		0x01
#define FLAG_APOLLO_HACK	0x02
#define FLAG_PIIX4		0x04


typedef struct {
    int8_t      stat;

    uint8_t	cent, def,
		flags, read_addr,
		wp_0d, wp_32,
		pad, pad0;

    uint8_t	addr[8], wp[2],
		bank[8], *lock;

    int16_t	count, state;

    uint64_t	ecount,
		rtc_time;
    pc_timer_t  update_timer,
                rtc_timer;
} local_t;


static uint8_t	nvr_at_inited = 0;


/* Get the current NVR time. */
static void
time_get(nvr_t *nvr, struct tm *tm)
{
    local_t *local = (local_t *)nvr->data;
    int8_t temp;

    if (nvr->regs[RTC_REGB] & REGB_DM) {
	/* NVR is in Binary data mode. */
	tm->tm_sec = nvr->regs[RTC_SECONDS];
	tm->tm_min = nvr->regs[RTC_MINUTES];
	temp = nvr->regs[RTC_HOURS];
	tm->tm_wday = (nvr->regs[RTC_DOW] - 1);
	tm->tm_mday = nvr->regs[RTC_DOM];
	tm->tm_mon = (nvr->regs[RTC_MONTH] - 1);
	tm->tm_year = nvr->regs[RTC_YEAR];
	if (local->cent != 0xFF)
		tm->tm_year += (nvr->regs[local->cent] * 100) - 1900;
    } else {
	/* NVR is in BCD data mode. */
	tm->tm_sec = RTC_DCB(nvr->regs[RTC_SECONDS]);
	tm->tm_min = RTC_DCB(nvr->regs[RTC_MINUTES]);
	temp = RTC_DCB(nvr->regs[RTC_HOURS]);
	tm->tm_wday = (RTC_DCB(nvr->regs[RTC_DOW]) - 1);
	tm->tm_mday = RTC_DCB(nvr->regs[RTC_DOM]);
	tm->tm_mon = (RTC_DCB(nvr->regs[RTC_MONTH]) - 1);
	tm->tm_year = RTC_DCB(nvr->regs[RTC_YEAR]);
	if (local->cent != 0xFF)
		tm->tm_year += (RTC_DCB(nvr->regs[local->cent]) * 100) - 1900;
    }

    /* Adjust for 12/24 hour mode. */
    if (nvr->regs[RTC_REGB] & REGB_2412)
	tm->tm_hour = temp;
      else
	tm->tm_hour = ((temp & ~RTC_AMPM)%12) + ((temp&RTC_AMPM) ? 12 : 0);
}


/* Set the current NVR time. */
static void
time_set(nvr_t *nvr, struct tm *tm)
{
    local_t *local = (local_t *)nvr->data;
    int year = (tm->tm_year + 1900);

    if (nvr->regs[RTC_REGB] & REGB_DM) {
	/* NVR is in Binary data mode. */
	nvr->regs[RTC_SECONDS] = tm->tm_sec;
	nvr->regs[RTC_MINUTES] = tm->tm_min;
	nvr->regs[RTC_DOW] = (tm->tm_wday + 1);
	nvr->regs[RTC_DOM] = tm->tm_mday;
	nvr->regs[RTC_MONTH] = (tm->tm_mon + 1);
	nvr->regs[RTC_YEAR] = (year % 100);
	if (local->cent != 0xFF)
		nvr->regs[local->cent] = (year / 100);

	if (nvr->regs[RTC_REGB] & REGB_2412) {
		/* NVR is in 24h mode. */
		nvr->regs[RTC_HOURS] = tm->tm_hour;
	} else {
		/* NVR is in 12h mode. */
		nvr->regs[RTC_HOURS] = (tm->tm_hour % 12) ? (tm->tm_hour % 12) : 12;
		if (tm->tm_hour > 11)
			nvr->regs[RTC_HOURS] |= RTC_AMPM;
	}
    } else {
	/* NVR is in BCD data mode. */
	nvr->regs[RTC_SECONDS] = RTC_BCD(tm->tm_sec);
	nvr->regs[RTC_MINUTES] = RTC_BCD(tm->tm_min);
	nvr->regs[RTC_DOW] = RTC_BCD(tm->tm_wday + 1);
	nvr->regs[RTC_DOM] = RTC_BCD(tm->tm_mday);
	nvr->regs[RTC_MONTH] = RTC_BCD(tm->tm_mon + 1);
	nvr->regs[RTC_YEAR] = RTC_BCD(year % 100);
	if (local->cent != 0xFF)
		nvr->regs[local->cent] = RTC_BCD(year / 100);

	if (nvr->regs[RTC_REGB] & REGB_2412) {
		/* NVR is in 24h mode. */
		nvr->regs[RTC_HOURS] = RTC_BCD(tm->tm_hour);
	} else {
		/* NVR is in 12h mode. */
		nvr->regs[RTC_HOURS] = (tm->tm_hour % 12)
					? RTC_BCD(tm->tm_hour % 12)
					: RTC_BCD(12);
		if (tm->tm_hour > 11)
			nvr->regs[RTC_HOURS] |= RTC_AMPM;
	}
    }
}


/* Check if the current time matches a set alarm time. */
static int8_t
check_alarm(nvr_t *nvr, int8_t addr)
{
    return((nvr->regs[addr+1] == nvr->regs[addr]) ||
	   ((nvr->regs[addr+1] & AL_DONTCARE) == AL_DONTCARE));
}


/* Check for VIA stuff. */
static int8_t
check_alarm_via(nvr_t *nvr, int8_t addr, int8_t addr_2)
{
    local_t *local = (local_t *)nvr->data;

    if (local->cent == RTC_CENTURY_VIA) {
	return((nvr->regs[addr_2] == nvr->regs[addr]) ||
	       ((nvr->regs[addr_2] & AL_DONTCARE) == AL_DONTCARE));
    } else
	return 1;
}


/* Update the NVR registers from the internal clock. */
static void
timer_update(void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;
    local_t *local = (local_t *)nvr->data;
    struct tm tm;

    local->ecount = 0LL;

    if (! (nvr->regs[RTC_REGB] & REGB_SET)) {
	/* Get the current time from the internal clock. */
	nvr_time_get(&tm);

	/* Update registers with current time. */
	time_set(nvr, &tm);

	/* Clear update status. */
	local->stat = 0x00;

	/* Check for any alarms we need to handle. */
	if (check_alarm(nvr, RTC_SECONDS) &&
	    check_alarm(nvr, RTC_MINUTES) &&
	    check_alarm(nvr, RTC_HOURS) &&
	    check_alarm_via(nvr, RTC_DOM, RTC_ALDAY) &&
	    check_alarm_via(nvr, RTC_MONTH, RTC_ALMONTH)/* &&
		check_alarm_via(nvr, RTC_DOM, RTC_ALDAY_SIS) &&
		check_alarm_via(nvr, RTC_MONTH, RTC_ALMONT_SIS)*/) {
		nvr->regs[RTC_REGC] |= REGC_AF;
		if (nvr->regs[RTC_REGB] & REGB_AIE) {
			nvr->regs[RTC_REGC] |= REGC_IRQF;

			/* Generate an interrupt. */
			if (nvr->irq != -1)
				picint(1 << nvr->irq);
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
			picint(1 << nvr->irq);
	}
    }
}


static void
timer_load_count(nvr_t *nvr)
{
    int c = nvr->regs[RTC_REGA] & REGA_RS;
    local_t *local = (local_t *) nvr->data;

    if ((nvr->regs[RTC_REGA] & 0x70) != 0x20) {
	local->state = 0;
	return;
    }

    local->state = 1;

    switch (c) {
	case 0:
		local->state = 0;
		break;
	case 1: case 2:
		local->count = 1 << (c + 6);
		break;
	default:
		local->count = 1 << (c - 1);
		break;
    }
}


static void
timer_intr(void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;
    local_t *local = (local_t *)nvr->data;

    timer_advance_u64(&local->rtc_timer, RTCCONST);

    if (local->state == 1) {
	if (--local->count == 0) {
		timer_load_count(nvr);

		nvr->regs[RTC_REGC] |= REGC_PF;
		if (nvr->regs[RTC_REGB] & REGB_PIE) {
			nvr->regs[RTC_REGC] |= REGC_IRQF;

			/* Generate an interrupt. */
			if (nvr->irq != -1)
				picint(1 << nvr->irq);
		}
	}
    }
}


/* Callback from internal clock, another second passed. */
static void
timer_tick(nvr_t *nvr)
{
    local_t *local = (local_t *)nvr->data;

    /* Only update it there is no SET in progress. */
    if (! (nvr->regs[RTC_REGB] & REGB_SET)) {
	/* Set the UIP bit, announcing the update. */
	local->stat = REGA_UIP;

	rtc_tick();

	/* Schedule the actual update. */
	local->ecount = (244ULL + 1984ULL) * TIMER_USEC;
	timer_set_delay_u64(&local->update_timer, local->ecount);
    }
}


/* This must be exposed because ACPI uses it. */
void
nvr_reg_write(uint16_t reg, uint8_t val, void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;
    local_t *local = (local_t *)nvr->data;
    struct tm tm;
    uint8_t old, i;
    uint16_t checksum = 0x0000;

    old = nvr->regs[reg];
    switch(reg) {
	case RTC_REGA:
		nvr->regs[RTC_REGA] = val;
		timer_load_count(nvr);
		break;

	case RTC_REGB:
		nvr->regs[RTC_REGB] = val;
		if (((old^val) & REGB_SET) && (val&REGB_SET)) {
			/* According to the datasheet... */
			nvr->regs[RTC_REGA] &= ~REGA_UIP;
			nvr->regs[RTC_REGB] &= ~REGB_UIE;
		}
		break;

	case RTC_REGC:		/* R/O */
		break;

	case RTC_REGD:		/* R/O */
		break;

	case 0x2e:
	case 0x2f:
		if (local->flags & FLAG_LS_HACK) {
			/* 2E and 2F are a simple sum of the values of 0E to 2D. */
			for (i = 0x0e; i < 0x2e; i++)
				checksum += (uint16_t) nvr->regs[i];
			nvr->regs[0x2e] = checksum >> 8;
			nvr->regs[0x2f] = checksum & 0xff;
			break;
		}
		/*FALLTHROUGH*/

	case 0x32:
		if ((local->cent == RTC_CENTURY_VIA) && local->wp_32)
			break;
		/* FALLTHROUGH */

	default:		/* non-RTC registers are just NVRAM */
		if ((reg == 0x2c) && (local->flags & FLAG_LS_HACK))
			nvr->new = 0;
		if ((reg == 0x52) && (local->flags & FLAG_APOLLO_HACK))
			nvr->new = 0;
		if ((reg >= 0x38) && (reg <= 0x3f) && local->wp[0])
			break;
		if ((reg >= 0xb8) && (reg <= 0xbf) && local->wp[1])
			break;
		if (local->lock[reg])
			break;
		if (nvr->regs[reg] != val) {
			nvr->regs[reg] = val;
			nvr_dosave = 1;
		}
		break;
    }

    if ((reg < RTC_REGA) || ((local->cent != 0xff) && (reg == local->cent))) {
	if ((reg != 1) && (reg != 3) && (reg != 5)) {
		if ((old != val) && !(time_sync & TIME_SYNC_ENABLED)) {
			/* Update internal clock. */
			time_get(nvr, &tm);
			nvr_time_set(&tm);
			nvr_dosave = 1;
		}
	}
    }
}


/* Write to one of the NVR registers. */
static void
nvr_write(uint16_t addr, uint8_t val, void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;
    local_t *local = (local_t *)nvr->data;
    uint8_t addr_id = (addr & 0x0e) >> 1;

    cycles -= ISA_CYCLES(8);

    if (local->bank[addr_id] == 0xff)
	return;

    if (addr & 1) {
	// if (local->bank[addr_id] == 0xff)
		// return;
	nvr_reg_write(local->addr[addr_id], val, priv);
    } else {
	local->addr[addr_id] = (val & (nvr->size - 1));
	/* Some chipsets use a 256 byte NVRAM but ports 70h and 71h always access only 128 bytes. */
	if (addr_id == 0x0)
		local->addr[addr_id] &= 0x7f;
	else if ((addr_id == 0x1) && (local->flags & FLAG_PIIX4))
		local->addr[addr_id] = (local->addr[addr_id] & 0x7f) | 0x80;
	if (local->bank[addr_id] > 0)
		local->addr[addr_id] = (local->addr[addr_id] & 0x7f) | (0x80 * local->bank[addr_id]);
	if (!(machines[machine].flags & MACHINE_MCA) &&
	    !(machines[machine].flags & MACHINE_NONMI))
		nmi_mask = (~val & 0x80);
    }
}


/* Read from one of the NVR registers. */
static uint8_t
nvr_read(uint16_t addr, void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;
    local_t *local = (local_t *)nvr->data;
    uint8_t ret;
    uint8_t addr_id = (addr & 0x0e) >> 1;
    uint16_t i, checksum = 0x0000;

    cycles -= ISA_CYCLES(8);

    if (local->bank[addr_id] == 0xff)
	ret = 0xff;
    else if (addr & 1)  switch(local->addr[addr_id]) {
	case RTC_REGA:
		ret = (nvr->regs[RTC_REGA] & 0x7f) | local->stat;
		break;

	case RTC_REGC:
		picintc(1 << nvr->irq);
		ret = nvr->regs[RTC_REGC];
		nvr->regs[RTC_REGC] = 0x00;
		break;

	case RTC_REGD:
		/* Bits 6-0 of this register always read 0. Bit 7 is battery state,
		   we should always return it set, as that means the battery is OK. */
		ret = REGD_VRT;
		break;

	case 0x2c:
		if (!nvr->new && (local->flags & FLAG_LS_HACK))
			ret = nvr->regs[local->addr[addr_id]] & 0x7f;
		else
			ret = nvr->regs[local->addr[addr_id]];
		break;

	case 0x2e:
	case 0x2f:
		if (!nvr->new && (local->flags & FLAG_LS_HACK)) {
			for (i = 0x10; i <= 0x2d; i++) {
				if (i == 0x2c)
					checksum += (nvr->regs[i] & 0x7f);
				else
					checksum += nvr->regs[i];
			}
			if (local->addr[addr_id] == 0x2e)
				ret = checksum >> 8;
			else
				ret = checksum & 0xff;
		} else
			ret = nvr->regs[local->addr[addr_id]];
		break;

	case 0x3e:
	case 0x3f:
		if (!nvr->new && (local->flags & FLAG_APOLLO_HACK)) {
			/* The checksum at 3E-3F is for 37-3D and 40-7F. */
			for (i = 0x37; i <= 0x3d; i++)
				checksum += nvr->regs[i];
			for (i = 0x40; i <= 0x7f; i++) {
				if (i == 0x52)
					checksum += (nvr->regs[i] & 0xf3);
				else
					checksum += nvr->regs[i];
			}
			if (local->addr[addr_id] == 0x3e)
				ret = checksum >> 8;
			else
				ret = checksum & 0xff;
		} else
			ret = nvr->regs[local->addr[addr_id]];
		break;

	case 0x52:
		if (!nvr->new && (local->flags & FLAG_APOLLO_HACK))
			ret = nvr->regs[local->addr[addr_id]] & 0xf3;
		else
			ret = nvr->regs[local->addr[addr_id]];
		break;

	default:
		ret = nvr->regs[local->addr[addr_id]];
		break;
    } else {
	ret = local->addr[addr_id];
	if (!local->read_addr)
		ret &= 0x80;
	if (alt_access)
		ret = (ret & 0x7f) | (nmi_mask ? 0x00 : 0x80);
    }

    return(ret);
}

/* Secondary NVR write - used by SMC. */
static void
nvr_sec_write(uint16_t addr, uint8_t val, void *priv)
{
    nvr_write(0x72 + (addr & 1), val, priv);
}


/* Secondary NVR read - used by SMC. */
static uint8_t
nvr_sec_read(uint16_t addr, void *priv)
{
    return nvr_read(0x72 + (addr & 1), priv);
}

/* Reset the RTC state to 1980/01/01 00:00. */
static void
nvr_reset(nvr_t *nvr)
{
    local_t *local = (local_t *)nvr->data;

    /* memset(nvr->regs, local->def, RTC_REGS); */
    memset(nvr->regs, local->def, nvr->size);
    nvr->regs[RTC_DOM] = 1;
    nvr->regs[RTC_MONTH] = 1;
    nvr->regs[RTC_YEAR] = RTC_BCD(80);
    if (local->cent != 0xFF)
	nvr->regs[local->cent] = RTC_BCD(19);
}

/* Process after loading from file. */
static void
nvr_start(nvr_t *nvr)
{
    int i;
    local_t *local = (local_t *) nvr->data;

    struct tm tm;
    int default_found = 0;

    for (i = 0; i < nvr->size; i++) {
	if (nvr->regs[i] == local->def)
		default_found++;
    }

    if (default_found == nvr->size)
	nvr->regs[0x0e] = 0xff;		/* If load failed or it loaded an uninitialized NVR,
					   mark everything as bad. */

    /* Initialize the internal and chip times. */
    if (time_sync & TIME_SYNC_ENABLED) {
	/* Use the internal clock's time. */
	nvr_time_get(&tm);
	time_set(nvr, &tm);
    } else {
	/* Set the internal clock from the chip time. */
	time_get(nvr, &tm);
	nvr_time_set(&tm);
    }

    /* Start the RTC. */
    nvr->regs[RTC_REGA] = (REGA_RS2|REGA_RS1);
    nvr->regs[RTC_REGB] = REGB_2412;
}


static void
nvr_at_speed_changed(void *priv)
{
    nvr_t *nvr = (nvr_t *) priv;
    local_t *local = (local_t *) nvr->data;

    timer_disable(&local->rtc_timer);
    timer_set_delay_u64(&local->rtc_timer, RTCCONST);

    timer_disable(&local->update_timer);
    if (local->ecount > 0ULL)
	timer_set_delay_u64(&local->update_timer, local->ecount);

    timer_disable(&nvr->onesec_time);
    timer_set_delay_u64(&nvr->onesec_time, (10000ULL * TIMER_USEC));
}


void
nvr_at_handler(int set, uint16_t base, nvr_t *nvr)
{
    io_handler(set, base, 2,
	       nvr_read,NULL,NULL, nvr_write,NULL,NULL, nvr);
}


void
nvr_at_sec_handler(int set, uint16_t base, nvr_t *nvr)
{
    io_handler(set, base, 2,
	       nvr_sec_read,NULL,NULL, nvr_sec_write,NULL,NULL, nvr);
}

void
nvr_read_addr_set(int set, nvr_t *nvr)
{
    local_t *local = (local_t *) nvr->data;

    local->read_addr = set;
}


void
nvr_wp_set(int set, int h, nvr_t *nvr)
{
    local_t *local = (local_t *) nvr->data;

    local->wp[h] = set;
}


void
nvr_via_wp_set(int set, int reg, nvr_t *nvr)
{
    local_t *local = (local_t *) nvr->data;

    if (reg == 0x0d)
	local->wp_0d = set;
    else
	local->wp_32 = set;
}


void
nvr_bank_set(int base, uint8_t bank, nvr_t *nvr)
{
    local_t *local = (local_t *) nvr->data;

    local->bank[base] = bank;
}


void
nvr_lock_set(int base, int size, int lock, nvr_t *nvr)
{
    local_t *local = (local_t *) nvr->data;
    int i;

    for (i = 0; i < size; i++)
	local->lock[base + i] = lock;
}


static void
nvr_at_reset(void *priv)
{
    nvr_t *nvr = (nvr_t *) priv;

    /* These bits are reset on reset. */
    nvr->regs[RTC_REGB] &= ~(REGB_PIE | REGB_AIE | REGB_UIE | REGB_SQWE);
    nvr->regs[RTC_REGC] &= ~(REGC_PF | REGC_AF | REGC_UF | REGC_IRQF);
}


static void *
nvr_at_init(const device_t *info)
{
    local_t *local;
    nvr_t *nvr;

    /* Allocate an NVR for this machine. */
    nvr = (nvr_t *)malloc(sizeof(nvr_t));
    if (nvr == NULL) return(NULL);
    memset(nvr, 0x00, sizeof(nvr_t));

    local = (local_t *)malloc(sizeof(local_t));
    memset(local, 0x00, sizeof(local_t));
    nvr->data = local;

    /* This is machine specific. */
    nvr->size = machines[machine].nvrmask + 1;
    local->lock = (uint8_t *) malloc(nvr->size);
    memset(local->lock, 0x00, nvr->size);
    local->def = 0xff /*0x00*/;
    local->flags = 0x00;
    switch(info->local & 7) {
	case 0:		/* standard AT, no century register */
		nvr->irq = 8;
		local->cent = 0xff;
		break;

	case 1:		/* standard AT */
	case 5:		/* Lucky Star LS-486E */
	case 6:		/* AMI Apollo */
		if (info->local == 9)
			local->flags |= FLAG_PIIX4;
		else {
			local->def = 0x00;
			if ((info->local & 7) == 5)
				local->flags |= FLAG_LS_HACK;
			else if ((info->local & 7) == 6)
				local->flags |= FLAG_APOLLO_HACK;
			else
				local->def = 0xff;
		}
		nvr->irq = 8;
		local->cent = RTC_CENTURY_AT;
		break;

	case 2:		/* PS/1 or PS/2 */
		nvr->irq = 8;
		local->cent = RTC_CENTURY_PS;
		local->def = 0x00;
		break;

	case 3:		/* Amstrad PC's */
		nvr->irq = 1;
		local->cent = RTC_CENTURY_AT;
		local->def = 0xff;
		break;

	case 4:		/* IBM AT */
		nvr->irq = 8;
		local->cent = RTC_CENTURY_AT;
		local->def = 0xff;
		break;

	case 7:		/* VIA VT82C586B */
		nvr->irq = 8;
		local->cent = RTC_CENTURY_VIA;
		break;
    }

    local->read_addr = 1;

    /* Set up any local handlers here. */
    nvr->reset = nvr_reset;
    nvr->start = nvr_start;
    nvr->tick = timer_tick;

    /* Initialize the generic NVR. */
    nvr_init(nvr);

    if (nvr_at_inited == 0) {
	/* Start the timers. */
	timer_add(&local->update_timer, timer_update, nvr, 0);

	timer_add(&local->rtc_timer, timer_intr, nvr, 0);
	/* On power on, if the oscillator is disabled, it's reenabled. */
	if ((nvr->regs[RTC_REGA] & 0x70) == 0x00)
		nvr->regs[RTC_REGA] = (nvr->regs[RTC_REGA] & 0x8f) | 0x20;
	nvr_at_reset(nvr);
	timer_load_count(nvr);
	timer_set_delay_u64(&local->rtc_timer, RTCCONST);

	/* Set up the I/O handler for this device. */
	io_sethandler(0x0070, 2,
		      nvr_read,NULL,NULL, nvr_write,NULL,NULL, nvr);
	if (info->local & 8) {
		io_sethandler(0x0072, 2,
			      nvr_read,NULL,NULL, nvr_write,NULL,NULL, nvr);
	}

	nvr_at_inited = 1;
    }

    return(nvr);
}


static void
nvr_at_close(void *priv)
{
    nvr_t *nvr = (nvr_t *) priv;
    local_t *local = (local_t *) nvr->data;

    nvr_close();

    timer_disable(&local->rtc_timer);
    timer_disable(&local->update_timer);
    timer_disable(&nvr->onesec_time);

    if (nvr != NULL) {
	if (nvr->fn != NULL)
		free(nvr->fn);

	if (nvr->data != NULL)
		free(nvr->data);

	free(nvr);
    }

    if (nvr_at_inited == 1)
	nvr_at_inited = 0;
}


const device_t at_nvr_old_device = {
    "PC/AT NVRAM (No century)",
    DEVICE_ISA | DEVICE_AT,
    0,
    nvr_at_init, nvr_at_close, nvr_at_reset,
    { NULL }, nvr_at_speed_changed,
    NULL
};

const device_t at_nvr_device = {
    "PC/AT NVRAM",
    DEVICE_ISA | DEVICE_AT,
    1,
    nvr_at_init, nvr_at_close, nvr_at_reset,
    { NULL }, nvr_at_speed_changed,
    NULL
};

const device_t ps_nvr_device = {
    "PS/1 or PS/2 NVRAM",
    DEVICE_PS2,
    2,
    nvr_at_init, nvr_at_close, nvr_at_reset,
    { NULL }, nvr_at_speed_changed,
    NULL
};

const device_t amstrad_nvr_device = {
    "Amstrad NVRAM",
    DEVICE_ISA | DEVICE_AT,
    3,
    nvr_at_init, nvr_at_close, nvr_at_reset,
    { NULL }, nvr_at_speed_changed,
    NULL
};

const device_t ibmat_nvr_device = {
    "IBM AT NVRAM",
    DEVICE_ISA | DEVICE_AT,
    4,
    nvr_at_init, nvr_at_close, nvr_at_reset,
    { NULL }, nvr_at_speed_changed,
    NULL
};

const device_t piix4_nvr_device = {
    "Intel PIIX4 PC/AT NVRAM",
    DEVICE_ISA | DEVICE_AT,
    9,
    nvr_at_init, nvr_at_close, nvr_at_reset,
    { NULL }, nvr_at_speed_changed,
    NULL
};

const device_t ls486e_nvr_device = {
    "Lucky Star LS-486E PC/AT NVRAM",
    DEVICE_ISA | DEVICE_AT,
    13,
    nvr_at_init, nvr_at_close, nvr_at_reset,
    { NULL }, nvr_at_speed_changed,
    NULL
};

const device_t ami_apollo_nvr_device = {
    "AMI Apollo PC/AT NVRAM",
    DEVICE_ISA | DEVICE_AT,
    14,
    nvr_at_init, nvr_at_close, nvr_at_reset,
    { NULL }, nvr_at_speed_changed,
    NULL
};

const device_t via_nvr_device = {
    "VIA PC/AT NVRAM",
    DEVICE_ISA | DEVICE_AT,
    15,
    nvr_at_init, nvr_at_close, nvr_at_reset,
    { NULL }, nvr_at_speed_changed,
    NULL
};

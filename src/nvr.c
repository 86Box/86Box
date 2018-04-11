/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implement a generic NVRAM/CMOS/RTC device.
 *
 * NOTE:	I should re-do 'intclk' using a TM struct.
 *
 * Version:	@(#)nvr.c	1.0.3	2018/03/19
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
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
#include "86box.h"
#include "device.h"
#include "machine/machine.h"
#include "machine/m_xt_t1000.h"
#include "mem.h"
#include "pic.h"
#include "pit.h"
#include "rom.h"
#include "timer.h"
#include "plat.h"
#include "nvr.h"


/* Define the internal clock. */
typedef struct {
   int16_t	year;
   int8_t	sec;
   int8_t	min;
   int8_t	hour;
   int8_t	mday;
   int8_t	mon;
} intclk_t;


int	enable_sync;		/* configuration variable: enable time sync */
int	nvr_dosave;		/* NVR is dirty, needs saved */


static int8_t	days_in_month[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
static intclk_t	intclk;
static nvr_t	*saved_nvr = NULL;


/* Determine whether or not the year is leap. */
int
nvr_is_leap(int year)
{
    if (year % 400 == 0) return(1);
    if (year % 100 == 0) return(0);
    if (year % 4 == 0) return(1);

    return(0);
}


/* Determine the days in the current month. */
int
nvr_get_days(int month, int year)
{
    if (month != 2)
	return(days_in_month[month - 1]);

    return(nvr_is_leap(year) ? 29 : 28);
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
    if (intclk.mday == (nvr_get_days(intclk.mon, intclk.year) + 1)) {
	intclk.mday = 1;
	intclk.mon++;
    }
    if (intclk.mon == 13) {
	intclk.mon = 1;
	intclk.year++;
    }
}


/* This is the RTC one-second timer. */
static void
onesec_timer(void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;

    if (++nvr->onesec_cnt >= 100) {
	/* Update the internal clock. */
	rtc_tick();

	/* Update the RTC device if needed. */
	if (nvr->tick != NULL)
		(*nvr->tick)(nvr);

	nvr->onesec_cnt = 0;
    }

    nvr->onesec_time += (int64_t)(10000 * TIMER_USEC);
}


/* Initialize the generic NVRAM/RTC device. */
void
nvr_init(nvr_t *nvr)
{
    char temp[64];
    struct tm *tm;
    time_t now;
    int c;

    /* Set up the NVR file's name. */
    sprintf(temp, "%s.nvr", machine_get_internal_name());
    c = strlen(temp)+1;
    nvr->fn = (wchar_t *)malloc(c*sizeof(wchar_t));
    mbstowcs(nvr->fn, temp, c);

    /* Initialize the internal clock as needed. */
    memset(&intclk, 0x00, sizeof(intclk));
    if (enable_sync) {
	/* Get the current time of day, and convert to local time. */
	(void)time(&now);
	tm = localtime(&now);

	/* Set the internal clock. */
	nvr_time_set(tm);
    } else {
	/* Reset the internal clock to 1980/01/01 00:00. */
	intclk.mon = 1;
	intclk.year = 1980;
    }

    /* Set up our timer. */
    timer_add(onesec_timer, &nvr->onesec_time, TIMER_ALWAYS_ENABLED, nvr);

    /* It does not need saving yet. */
    nvr_dosave = 0;

    /* Save the NVR data pointer. */
    saved_nvr = nvr;

    /* Try to load the saved data. */
    (void)nvr_load();
}


/*
 * Load an NVR from file.
 *
 * This function does two things, really. It clears and initializes
 * the RTC and NVRAM areas, sets up defaults for the RTC part, and
 * then attempts to load data from a saved file.
 *
 * Either way, after that, it will continue to configure the local
 * RTC to operate, so it can update either the local RTC, and/or
 * the one supplied by a client.
 */
int
nvr_load(void)
{
    FILE *f;

    /* Make sure we have been initialized. */
    if (saved_nvr == NULL) return(0);

    /* Clear out any old data. */
    memset(saved_nvr->regs, 0xff, sizeof(saved_nvr->regs));

    /* Set the defaults. */
    if (saved_nvr->reset != NULL)
	saved_nvr->reset(saved_nvr);

    /* Load the (relevant) part of the NVR contents. */
    if (saved_nvr->size != 0) {
	pclog("NVR: loading from '%ls'\n", nvr_path(saved_nvr->fn));
	f = plat_fopen(nvr_path(saved_nvr->fn), L"rb");
	if (f != NULL) {
		/* Read NVR contents from file. */
		(void)fread(saved_nvr->regs, saved_nvr->size, 1, f);
		(void)fclose(f);
	}
    }

    if (romset == ROM_T1000)
	t1000_nvr_load();
    else if (romset == ROM_T1200)
	t1200_nvr_load();

    /* Get the local RTC running! */
    if (saved_nvr->start != NULL)
	saved_nvr->start(saved_nvr);

    return(1);
}


/* Save the current NVR to a file. */
int
nvr_save(void)
{
    FILE *f;

    /* Make sure we have been initialized. */
    if (saved_nvr == NULL) return(0);

    if (saved_nvr->size != 0) {
	pclog("NVR: saving to '%ls'\n", nvr_path(saved_nvr->fn));
	f = plat_fopen(nvr_path(saved_nvr->fn), L"wb");
	if (f != NULL) {
		/* Save NVR contents to file. */
		(void)fwrite(saved_nvr->regs, saved_nvr->size, 1, f);
		fclose(f);
	}
    }

    if (romset == ROM_T1000)
	t1000_nvr_save();
    else if (romset == ROM_T1200)
	t1200_nvr_save();

    /* Device is clean again. */
    nvr_dosave = 0;

    return(1);
}


/* Get current time from internal clock. */
void
nvr_time_get(struct tm *tm)
{
    int8_t dom, mon, sum, wd;
    int16_t cent, yr;

    tm->tm_sec = intclk.sec;
    tm->tm_min = intclk.min;
    tm->tm_hour = intclk.hour;
     dom = intclk.mday;
     mon = intclk.mon;
     yr = (intclk.year % 100);
     cent = ((intclk.year - yr) / 100) % 4;
     sum = dom+mon+yr+cent;
     wd = ((sum + 6) % 7);
    tm->tm_wday = wd;
    tm->tm_mday = intclk.mday;
    tm->tm_mon = (intclk.mon - 1);
    tm->tm_year = (intclk.year - 1900);
}


/* Set internal clock time. */
void
nvr_time_set(struct tm *tm)
{
    intclk.sec = tm->tm_sec;
    intclk.min = tm->tm_min;
    intclk.hour = tm->tm_hour;
    intclk.mday = tm->tm_mday;
    intclk.mon = (tm->tm_mon + 1);
    intclk.year = (tm->tm_year + 1900);
}


/* Get an absolute path to the NVR folder. */
wchar_t *
nvr_path(wchar_t *str)
{
    static wchar_t temp[1024];

    /* Get the full prefix in place. */
    memset(temp, 0x00, sizeof(temp));
    wcscpy(temp, usr_path);
    wcscat(temp, NVR_PATH);

    /* Create the directory if needed. */
    if (! plat_dir_check(temp))
	plat_dir_create(temp);

    /* Now append the actual filename. */
    plat_path_slash(temp);
    wcscat(temp, str);

    return(temp);
}


/* Open or create a file in the NVR area. */
FILE *
nvr_fopen(wchar_t *str, wchar_t *mode)
{
    return(plat_fopen(nvr_path(str), mode));
}

/* Emulation of:
   Toshiba TC8521 Real Time Clock */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nvr.h"
#include "rtc_tc8521.h"

#define peek2(a) (nvrram[(a##1)] + 10 * nvrram[(a##10)])

struct
{
        int sec;
        int min;
        int hour;
        int mday;
        int mon;
        int year;
} internal_clock;

/* Table for days in each month */
static int rtc_days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/* Called to determine whether the year is leap or not */
static int rtc_is_leap(int org_year)
{
        if (org_year % 400 == 0)  return 1;
        if (org_year % 100 == 0)  return 0;
        if (org_year % 4 == 0)  return 1;
        return 0;
}

/* Called to determine the days in the current month */
static int rtc_get_days(int org_month, int org_year)
{
        if (org_month != 2)
                return rtc_days_in_month[org_month];
        else
                return rtc_is_leap(org_year) ? 29 : 28;
}

/* Called when the internal clock gets updated */
static void tc8521_recalc()
{
        if (internal_clock.sec == 60)
        {
                internal_clock.sec = 0;
                internal_clock.min++;
        }
        if (internal_clock.min == 60)
        {
                internal_clock.min = 0;
                internal_clock.hour++;
        }
        if (internal_clock.hour == 24)
        {
                internal_clock.hour = 0;
                internal_clock.mday++;
        }
        if (internal_clock.mday == (rtc_get_days(internal_clock.mon, internal_clock.year) + 1))
        {
                internal_clock.mday = 1;
                internal_clock.mon++;
        }
        if (internal_clock.mon == 13)
        {
                internal_clock.mon = 1;
                internal_clock.year++;
        }
}

/* Called when ticking the second */
void tc8521_tick()
{
        internal_clock.sec++;
        tc8521_recalc();
}

/* Called when modifying the NVR registers */
void tc8521_update(uint8_t *nvrram, int reg)
{
        int temp;

        switch(reg)
        {
                case TC8521_SECOND1:
		case TC8521_SECOND10:
                internal_clock.sec = peek2(TC8521_SECOND);
                break;
                case TC8521_MINUTE1:
		case TC8521_MINUTE10:
                internal_clock.min = peek2(TC8521_MINUTE);
                break;
                case TC8521_HOUR1:
		case TC8521_HOUR10:
                temp = peek2(TC8521_HOUR); 
		if (nvrram[TC8521_24HR] & 1)
                        internal_clock.hour = temp;
                else
                        internal_clock.hour = ((temp & ~0x20) % 12) + ((temp & 0x20) ? 12 : 0);
                break;
                case TC8521_DAY1:
                case TC8521_DAY10:
                internal_clock.mday = peek2(TC8521_DAY);
                break;
                case TC8521_MONTH1:
                case TC8521_MONTH10:
                internal_clock.mon = peek2(TC8521_MONTH);
                break;
                case TC8521_YEAR1:
                case TC8521_YEAR10:
                internal_clock.year = 1980 + peek2(TC8521_YEAR);
                break;
        }
}

/* Called to obtain the current day of the week based on the internal clock */
static int time_week_day()
{
        int day_of_month = internal_clock.mday;
        int month2 = internal_clock.mon;
        int year2 = internal_clock.year % 100;
        int century = ((internal_clock.year - year2) / 100) % 4;
        int sum = day_of_month + month2 + year2 + century;
        /* (Sum mod 7) gives 0 for Saturday, we need it for Sunday, so +6 for Saturday to get 6 and Sunday 0 */
        int raw_wd = ((sum + 6) % 7);
        return raw_wd;
}

/* Called to get time into the internal clock */
static void tc8521_internal_get(struct tm *time_var)
{
        time_var->tm_sec = internal_clock.sec;
        time_var->tm_min = internal_clock.min;
        time_var->tm_hour = internal_clock.hour;
        time_var->tm_wday = time_week_day();
        time_var->tm_mday = internal_clock.mday;
        time_var->tm_mon = internal_clock.mon - 1;
        time_var->tm_year = internal_clock.year - 1900;
}

static void tc8521_internal_set(struct tm *time_var)
{
        internal_clock.sec = time_var->tm_sec;
        internal_clock.min = time_var->tm_min;
        internal_clock.hour = time_var->tm_hour;
        internal_clock.mday = time_var->tm_mday;
        internal_clock.mon = time_var->tm_mon + 1;
        internal_clock.year = time_var->tm_year + 1900;
}

static void tc8521_set_nvrram(uint8_t *nvrram, struct tm *cur_time_tm)
{
	nvrram[TC8521_SECOND1]  = cur_time_tm->tm_sec % 10;	
	nvrram[TC8521_SECOND10] = cur_time_tm->tm_sec / 10;	
	nvrram[TC8521_MINUTE1]  = cur_time_tm->tm_min % 10;	
	nvrram[TC8521_MINUTE10] = cur_time_tm->tm_min / 10;	
	if (nvrram[TC8521_24HR] & 1)
	{
		nvrram[TC8521_HOUR1]    = cur_time_tm->tm_hour % 10;	
		nvrram[TC8521_HOUR10]   = cur_time_tm->tm_hour / 10;	
	}
	else
	{
		nvrram[TC8521_HOUR1]    = (cur_time_tm->tm_hour % 12) % 10;	
		nvrram[TC8521_HOUR10]   = ((cur_time_tm->tm_hour % 12) / 10) 
					 | (cur_time_tm->tm_hour >= 12) ? 2 : 0;
	}
	nvrram[TC8521_WEEKDAY] = cur_time_tm->tm_wday;
	nvrram[TC8521_DAY1]    = cur_time_tm->tm_mday % 10;
	nvrram[TC8521_DAY10]   = cur_time_tm->tm_mday / 10;
	nvrram[TC8521_MONTH1]  = (cur_time_tm->tm_mon + 1) / 10;
	nvrram[TC8521_MONTH10] = (cur_time_tm->tm_mon + 1) % 10;
	nvrram[TC8521_YEAR1]   = (cur_time_tm->tm_year - 80) % 10;
	nvrram[TC8521_YEAR10]  = ((cur_time_tm->tm_year - 80) % 100) / 10;
}


void tc8521_internal_set_nvrram(uint8_t *nvrram)
{
        /* Load the entire internal clock state from the NVR. */
        internal_clock.sec  = peek2(TC8521_SECOND);
	internal_clock.min  = peek2(TC8521_MINUTE);
	if (nvrram[TC8521_24HR] & 1)
	{
		internal_clock.hour = peek2(TC8521_HOUR);
	}
	else
	{
		internal_clock.hour = (peek2(TC8521_HOUR) % 12) 
					+ (nvrram[TC8521_HOUR10] & 2) ? 12 : 0;
	}
        internal_clock.mday = peek2(TC8521_DAY);
        internal_clock.mon  = peek2(TC8521_MONTH);
        internal_clock.year = 1980 + peek2(TC8521_YEAR);
}

void tc8521_internal_sync(uint8_t *nvrram)
{
        struct tm *cur_time_tm;
        time_t cur_time;

	time(&cur_time);
        cur_time_tm = localtime(&cur_time);
  
        tc8521_internal_set(cur_time_tm);

        tc8521_set_nvrram(nvrram, cur_time_tm);
}

void tc8521_get(uint8_t *nvrram)
{
        struct tm cur_time_tm;

        tc8521_internal_get(&cur_time_tm);

        tc8521_set_nvrram(nvrram, &cur_time_tm);
}

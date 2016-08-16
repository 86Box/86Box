/* Copyright holders: Mahod, Tenshi, Sarah Walker
   see COPYING for more details
*/
/* Emulation of:
   Dallas Semiconductor DS12C887 Real Time Clock

   http://datasheets.maximintegrated.com/en/ds/DS12885-DS12C887A.pdf

   http://dev-docs.atariforge.org/files/MC146818A_RTC_1984.pdf  
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nvr.h"
#include "rtc.h"

int enable_sync;

struct
{
        int sec;
        int min;
        int hour;
        int mday;
        int mon;
        int year;
} internal_clock;

/* When the RTC was last updated */
static time_t rtc_set_time = 0;

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
static void rtc_recalc()
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
void rtc_tick()
{
        internal_clock.sec++;
        rtc_recalc();
}

/* Called when modifying the NVR registers */
void time_update(char *nvrram, int reg)
{
        int temp;

        switch(reg)
        {
                case RTC_SECONDS:
                internal_clock.sec = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_SECONDS] : DCB(nvrram[RTC_SECONDS]);
                break;
                case RTC_MINUTES:
                internal_clock.min = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_MINUTES] : DCB(nvrram[RTC_MINUTES]);
                break;
                case RTC_HOURS:
                temp = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_HOURS] : DCB(nvrram[RTC_HOURS]);

                if (nvrram[RTC_REGB] & RTC_2412)
                        internal_clock.hour = temp;
                else
                        internal_clock.hour = ((temp & ~RTC_AMPM) % 12) + ((temp & RTC_AMPM) ? 12 : 0);
                break;
                case RTC_DOM:
                internal_clock.mday = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_DOM] : DCB(nvrram[RTC_DOM]);
                break;
                case RTC_MONTH:
                internal_clock.mon = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_MONTH] : DCB(nvrram[RTC_MONTH]);
                break;
                case RTC_YEAR:
                internal_clock.year = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_YEAR] : DCB(nvrram[RTC_YEAR]);
                internal_clock.year += (nvrram[RTC_REGB] & RTC_DM) ? 1900 : (DCB(nvrram[RTC_CENTURY]) * 100);
                break;
                case RTC_CENTURY:
                if (nvrram[RTC_REGB] & RTC_DM)
                        return;
                internal_clock.year %= 100;
                internal_clock.year += (DCB(nvrram[RTC_CENTURY]) * 100);
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
static void time_internal_get(struct tm *time_var)
{
        time_var->tm_sec = internal_clock.sec;
        time_var->tm_min = internal_clock.min;
        time_var->tm_hour = internal_clock.hour;
        time_var->tm_wday = time_week_day();
        time_var->tm_mday = internal_clock.mday;
        time_var->tm_mon = internal_clock.mon - 1;
        time_var->tm_year = internal_clock.year - 1900;
}

static void time_internal_set(struct tm *time_var)
{
        internal_clock.sec = time_var->tm_sec;
        internal_clock.min = time_var->tm_min;
        internal_clock.hour = time_var->tm_hour;
        internal_clock.mday = time_var->tm_mday;
        internal_clock.mon = time_var->tm_mon + 1;
        internal_clock.year = time_var->tm_year + 1900;
}

static void time_set_nvrram(char *nvrram, struct tm *cur_time_tm)
{
        int dow, mon, year;

        if (nvrram[RTC_REGB] & RTC_DM)
        {
                nvrram[RTC_SECONDS] = cur_time_tm->tm_sec;
                nvrram[RTC_MINUTES] = cur_time_tm->tm_min;
                nvrram[RTC_DOW]     = cur_time_tm->tm_wday + 1;
                nvrram[RTC_DOM]     = cur_time_tm->tm_mday;
                nvrram[RTC_MONTH]   = cur_time_tm->tm_mon + 1;
                nvrram[RTC_YEAR]    = cur_time_tm->tm_year % 100;

                if (nvrram[RTC_REGB] & RTC_2412)
                {
                        nvrram[RTC_HOURS] = cur_time_tm->tm_hour;
                }
                else
                {
                        nvrram[RTC_HOURS] = (cur_time_tm->tm_hour % 12) ? (cur_time_tm->tm_hour % 12) : 12;
                        if (cur_time_tm->tm_hour > 11)
                                nvrram[RTC_HOURS] |= RTC_AMPM;
                }
        }
        else
        {
                nvrram[RTC_SECONDS] = BCD(cur_time_tm->tm_sec);
                nvrram[RTC_MINUTES] = BCD(cur_time_tm->tm_min);
                nvrram[RTC_DOW]     = BCD(cur_time_tm->tm_wday + 1);
                nvrram[RTC_DOM]     = BCD(cur_time_tm->tm_mday);
                nvrram[RTC_MONTH]   = BCD(cur_time_tm->tm_mon + 1);
                nvrram[RTC_YEAR]    = BCD(cur_time_tm->tm_year % 100);

                if (nvrram[RTC_REGB] & RTC_2412)
                {
                        nvrram[RTC_HOURS] = BCD(cur_time_tm->tm_hour);
                }
                else
                {
                        nvrram[RTC_HOURS] = (cur_time_tm->tm_hour % 12) ? BCD(cur_time_tm->tm_hour % 12) : BCD(12);
                        if (cur_time_tm->tm_hour > 11)
                                nvrram[RTC_HOURS] |= RTC_AMPM;
                }
        }
}

void time_internal_set_nvrram(char *nvrram)
{
        int temp;

        /* Load the entire internal clock state from the NVR. */
        internal_clock.sec = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_SECONDS] : DCB(nvrram[RTC_SECONDS]);
        internal_clock.min = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_MINUTES] : DCB(nvrram[RTC_MINUTES]);

        temp = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_HOURS] : DCB(nvrram[RTC_HOURS]);

        if (nvrram[RTC_REGB] & RTC_2412)
                internal_clock.hour = temp;
        else
                internal_clock.hour = ((temp & ~RTC_AMPM) % 12) + ((temp & RTC_AMPM) ? 12 : 0);

        internal_clock.mday = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_DOM] : DCB(nvrram[RTC_DOM]);
        internal_clock.mon = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_MONTH] : DCB(nvrram[RTC_MONTH]);
        internal_clock.year = (nvrram[RTC_REGB] & RTC_DM) ? nvrram[RTC_YEAR] : DCB(nvrram[RTC_YEAR]);
        internal_clock.year += (nvrram[RTC_REGB] & RTC_DM) ? 1900 : (DCB(nvrram[RTC_CENTURY]) * 100);
}

void time_internal_sync(char *nvrram)
{
        struct tm *cur_time_tm;
        time_t cur_time;

	time(&cur_time);
        cur_time_tm = localtime(&cur_time);
  
        time_internal_set(cur_time_tm);

        time_set_nvrram(nvrram, cur_time_tm);
}

void time_get(char *nvrram)
{
        struct tm cur_time_tm;

        time_internal_get(&cur_time_tm);

        time_set_nvrram(nvrram, &cur_time_tm);
}

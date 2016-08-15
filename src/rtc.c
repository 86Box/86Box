/* Copyright holders: Mahod, Tenshi
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

typedef struct
{
        int sec;
        int min;
        int hour;
        int mday;
        int mon;
        int year;
}
internal_clock_t;

internal_clock_t internal_clock;

/* When the RTC was last updated */
time_t rtc_set_time = 0;

/* Table for days in each month */
int rtc_days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

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
        {
                return rtc_days_in_month[org_month];
        }
        else
        {
                return rtc_is_leap(org_year) ? 29 : 28;
        }
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
                case RTCSECONDS:
                        internal_clock.sec = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCSECONDS] : DCB(nvrram[RTCSECONDS]);
                        break;
                case RTCMINUTES:
                        internal_clock.min = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCMINUTES] : DCB(nvrram[RTCMINUTES]);
                        break;
                case RTCHOURS:
                        temp = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCHOURS] : DCB(nvrram[RTCHOURS]);

                        if (nvrram[RTCREGB] & RTC2412)
                        {
                                internal_clock.hour = temp;
                        }
                        else
                        {
                                internal_clock.hour = ((temp & ~RTCAMPM) % 12) + ((temp & RTCAMPM) ? 12 : 0);
                        }
                        break;
                case RTCDOM:
                        internal_clock.mday = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCDOM] : DCB(nvrram[RTCDOM]);
                        break;
                case RTCMONTH:
                        internal_clock.mon = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCMONTH] : DCB(nvrram[RTCMONTH]);
                        break;
                case RTCYEAR:
                        internal_clock.year = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCYEAR] : DCB(nvrram[RTCYEAR]);
                        internal_clock.year += (nvrram[RTCREGB] & RTCDM) ? 1900 : (DCB(nvrram[RTCCENTURY]) * 100);
                        break;
                case RTCCENTURY:
                        if (nvrram[RTCREGB] & RTCDM)  return;
                        internal_clock.year %= 100;
                        internal_clock.year += (DCB(nvrram[RTCCENTURY]) * 100);
                        break;
                case 0xFF:        /* Load the entire internal clock state from the NVR. */
                        internal_clock.sec = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCSECONDS] : DCB(nvrram[RTCSECONDS]);
                        internal_clock.min = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCMINUTES] : DCB(nvrram[RTCMINUTES]);

                        temp = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCHOURS] : DCB(nvrram[RTCHOURS]);

                        if (nvrram[RTCREGB] & RTC2412)
                        {
                                internal_clock.hour = temp;
                        }
                        else
                        {
                                internal_clock.hour = ((temp & ~RTCAMPM) % 12) + ((temp & RTCAMPM) ? 12 : 0);
                        }

                        internal_clock.mday = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCDOM] : DCB(nvrram[RTCDOM]);
                        internal_clock.mon = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCMONTH] : DCB(nvrram[RTCMONTH]);
                        internal_clock.year = (nvrram[RTCREGB] & RTCDM) ? nvrram[RTCYEAR] : DCB(nvrram[RTCYEAR]);
                        internal_clock.year += (nvrram[RTCREGB] & RTCDM) ? 1900 : (DCB(nvrram[RTCCENTURY]) * 100);
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
static void time_internal(struct tm **time_var)
{
        if (*time_var == NULL)  *time_var = (struct tm *) malloc(sizeof(struct tm));

        (*time_var)->tm_sec = internal_clock.sec;
        (*time_var)->tm_min = internal_clock.min;
        (*time_var)->tm_hour = internal_clock.hour;
        (*time_var)->tm_wday = time_week_day();
        (*time_var)->tm_mday = internal_clock.mday;
        (*time_var)->tm_mon = internal_clock.mon - 1;
        (*time_var)->tm_year = internal_clock.year - 1900;
}

time_t cur_time;
struct tm* cur_time_tm;

/* Periodic RTC update function
   See also: nvr_onesec() in nvr.c
 */
void time_get(char *nvrram)
{
        int dow, mon, year;

        if (enable_sync)
        {
		time(&cur_time);

                /* Mingw doesn't support localtime_r */
                #if __MINGW32__
                cur_time_tm = localtime(&cur_time);
                #else
                #if __MINGW64__
                cur_time_tm = localtime(&cur_time);
                #else
                localtime_r(&cur_time, &cur_time_tm);
                #endif
                #endif
        }
        else
        {
                time_internal(&cur_time_tm);
        }

        if (nvrram[RTCREGB] & RTCDM)
        {
                nvrram[RTCSECONDS] = cur_time_tm->tm_sec;
                nvrram[RTCMINUTES] = cur_time_tm->tm_min;
                nvrram[RTCDOW]     = cur_time_tm->tm_wday + 1;
                nvrram[RTCDOM]     = cur_time_tm->tm_mday;
                nvrram[RTCMONTH]   = cur_time_tm->tm_mon + 1;
                nvrram[RTCYEAR]    = cur_time_tm->tm_year % 100;

                if (nvrram[RTCREGB] & RTC2412)
                {
                        nvrram[RTCHOURS] = cur_time_tm->tm_hour;
                }
                else
                {
                        nvrram[RTCHOURS] = (cur_time_tm->tm_hour % 12) ? (cur_time_tm->tm_hour % 12) : 12;
                        if (cur_time_tm->tm_hour > 11)
                        {
                                nvrram[RTCHOURS] |= RTCAMPM;
                        }
                }
        }
        else
        {
                nvrram[RTCSECONDS] = BCD(cur_time_tm->tm_sec);
                nvrram[RTCMINUTES] = BCD(cur_time_tm->tm_min);
                nvrram[RTCDOW]     = BCD(cur_time_tm->tm_wday + 1);
                nvrram[RTCDOM]     = BCD(cur_time_tm->tm_mday);
                nvrram[RTCMONTH]   = BCD(cur_time_tm->tm_mon + 1);
                nvrram[RTCYEAR]    = BCD(cur_time_tm->tm_year % 100);

                if (nvrram[RTCREGB] & RTC2412)
                {
                        nvrram[RTCHOURS] = BCD(cur_time_tm->tm_hour);
                }
                else
                {
                        nvrram[RTCHOURS] = (cur_time_tm->tm_hour % 12) ? BCD(cur_time_tm->tm_hour % 12) : BCD(12);
                        if (cur_time_tm->tm_hour > 11)
                        {
                                nvrram[RTCHOURS] |= RTCAMPM;
                        }
                }
        }
}
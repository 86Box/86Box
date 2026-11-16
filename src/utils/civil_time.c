/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Calendar dates for host timestamps, without the C library's
 *          limits on the range of times.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2026 RichardG.
 */
#include <stdint.h>
#include <time.h>
#include <86box/civil_time.h>

int
civil_time_local_offset(time_t when, int fallback)
{
    struct tm utc;

#ifdef _WIN32
    if (gmtime_s(&utc, &when))
        return fallback;
#else
    if (!gmtime_r(&when, &utc))
        return fallback;
#endif
    /* The UTC time read as a local one, with daylight saving as it applies then. */
    utc.tm_isdst = -1;
    time_t local = mktime(&utc);
    return (local == (time_t) -1) ? fallback : (int) (when - local);
}

void
civil_time_from_seconds(int64_t seconds, civil_time_t *out)
{
    int64_t secs = seconds % 86400;
    int64_t days = seconds / 86400;
    if (secs < 0) {
        secs += 86400;
        days -= 1;
    }
    out->hour   = secs / 3600;
    out->minute = (secs % 3600) / 60;
    out->second = secs % 60;

    /* Based on: https://howardhinnant.github.io/date_algorithms.html#civil_from_days */
    days += 719468;
    int64_t era = ((days >= 0) ? days : (days - 146096)) / 146097;
    int64_t doe = days - (era * 146097);
    int64_t yoe = (doe - (doe / 1460) + (doe / 36524) - (doe / 146096)) / 365;
    int64_t doy = doe - ((365 * yoe) + (yoe / 4) - (yoe / 100));
    int64_t mp  = ((5 * doy) + 2) / 153;
    out->day    = doy - (((153 * mp) + 2) / 5) + 1;
    out->month  = mp + ((mp < 10) ? 3 : -9);
    out->year   = yoe + (era * 400) + (out->month <= 2);
}

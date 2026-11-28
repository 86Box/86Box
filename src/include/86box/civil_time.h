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
#ifndef EMU_CIVIL_TIME_H
#define EMU_CIVIL_TIME_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct civil_time_t {
    int64_t year;
    int     month;  /* 1 to 12 */
    int     day;    /* 1 to 31 */
    int     hour;
    int     minute;
    int     second;
} civil_time_t;

/* The host's offset from UTC at a time, daylight saving included, in seconds east; fallback if the
   C library can't tell for that time. */
extern int civil_time_local_offset(time_t when, int fallback);

/* Splits seconds since 1970-01-01 00:00 into a date and time; any value works. */
extern void civil_time_from_seconds(int64_t seconds, civil_time_t *out);

#ifdef __cplusplus
}
#endif

#endif /* EMU_CIVIL_TIME_H */

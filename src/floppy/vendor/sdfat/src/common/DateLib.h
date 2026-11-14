/**
 * Copyright (c) 2011-2022 Bill Greiman
 * This file is part of the SdFat library for SD memory cards.
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#pragma once

/**
 * @file
 */
/** Various calendar algorithms. */
#include <stdint.h>
/** Set USE_1901_2099 nonzero for year in [1901,2099]. */
#define USE_1901_2099 1

/** EPOCH starts on Jan 1 of EPOCH_YEAR. */
#define EPOCH_YEAR 1970

/** Determine leap year.
 * @param[in] Y year
 * @return true if Y is a leap year
 */
inline bool leap(uint16_t Y) {
#if USE_1901_2099
  return (Y & 3) == 0;
#else   // USE_1901_2099
  return Y % 4 != 0 ? false : Y % 100 != 0 ? true : Y % 400 == 0;
#endif  // USE_1901_2099
}

/** Number of days in the year before the current month
 * @param[in] Y year
 * @param[in] M month 0 < M < 13
 * @return days in year before current month [0, 335]
 * Probably from "Astronomical Algorithms" ISBN 0-943396-61-1
 */
inline uint16_t daysBeforeMonth(uint16_t Y, uint8_t M) {
  bool L = leap(Y);
  return (275 * M / 9) - (M > 2 ? L ? 1 : 2 : 0) - 30;
}

/** Number of days in a month.
 * @param[in] Y year in range supported by leap().
 * @param[in] M month 0 < M < 13
 * @return Count of days in month [1, 31].
 */
inline uint8_t daysInMonth(uint16_t Y, uint8_t M) {
  bool L = leap(Y);
  return M == 2 ? (L ? 29 : 28) : M < 8 ? 30 + (M & 1) : 31 - (M & 1);
}

/** Day of week with Sunday == 0.
 * @param[in] Y year
 * @param[in] M month 1 <= M <= 12
 * @param[in] D day 1 <= D <= (last day of month)
 * @return Day of week [0,6].
 */
inline uint8_t dayOfWeek(uint16_t Y, uint8_t M, uint8_t D) {
  if (M < 3) {
    M += 12;
    Y--;
  }
#if USE_1901_2099
  // For the year in [1901,2099], - Y /100 + Y / 400 is -15.
  return (2 + D + (13 * M - 2) / 5 + Y + Y / 4 - 15) % 7;
#else   // USE_1901_2099
  return (2 + D + (13 * M - 2) / 5 + Y + Y / 4 - Y / 100 + Y / 400) % 7;
#endif  // USE_1901_2099
}

/** Day of year with Jan1 == 0.
 *
 * @param[in] Y year in range supported by leap().
 * @param[in] M month 0 < M < 13
 * @param[in] D day 0 < D <= length of month
 * @return Day of year [0, 365]
 **/
inline uint16_t dayOfYear(uint16_t Y, uint8_t M, uint8_t D) {
  return daysBeforeMonth(Y, M) + D - 1;
}

/** Day of year to day
 * @param[in] doy day of year 0 <= yday <= 365
 * @param[in] Y year
 * @param[in] M month 1 <= M <= 12
 * @return day of month
 * Based on http://ss64.net/merlyn/daycount.htm#DYZ
 */
inline uint8_t dayOfYearToDay(uint16_t doy, uint16_t Y, uint8_t M) {
  bool L = leap(Y);
  return doy + 1 - (M < 3 ? 31 * (M - 1) : (153 * M - 2) / 5 - (L ? 31 : 32));
}

/** Day of year to month
 * @param[in] doy day of year 0 <= yday <= 365
 * @param[in] Y year
 * @return month [1,12]
 * Based on http://ss64.net/merlyn/daycount.htm#DYZ
 */
inline uint8_t dayOfYearToMonth(uint16_t doy, uint16_t Y) {
  bool L = leap(Y);
  return doy < 31 ? 1 : 1 + (303 + 5 * (doy - (L ? 59 : 58))) / 153;
}

/** Count of days since Epoch.
 * 1900 < EPOCH_YEAR, MAX_YEAR < 2100, (MAX_YEAR - EPOCH_YEAR) < 178.
 * @param[in] Y year (EPOCH_YEAR <= Y <= MAX_YEAR)
 * @param[in] M month 1 <= M <= 12
 * @param[in] D day 1 <= D <= 31
 * @return Count of days since epoch
 *
 * Derived from Zeller's congruence
 */
inline uint16_t epochDay(uint16_t Y, uint8_t M, uint8_t D) {
  if (M < 3) {
    M += 12;
    Y--;
  }
  return 365 * (Y + 1 - EPOCH_YEAR) + Y / 4 - (EPOCH_YEAR - 1) / 4 +
         (153 * M - 2) / 5 + D - 398;
}

/** epoch day to day of week (Sunday == 0)
 * 1900 < EPOCH_YEAR, MAX_YEAR < 2100, (MAX_YEAR - EPOCH_YEAR) < 178.
 * @param[in] eday count of days since epoch.
 * @return day of week (Sunday == 0)
 **/
inline uint8_t epochDayToDayOfWeek(uint16_t eday) {
  return (eday + EPOCH_YEAR - 1 + (EPOCH_YEAR - 1) / 4) % 7;
}

/** Day of epoch to year
 * 1900 < EPOCH_YEAR, MAX_YEAR < 2100, (MAX_YEAR - EPOCH_YEAR) < 178.
 * @param[in] eday count of days since epoch
 * @return year for count of days since epoch
 */
inline uint16_t epochDayToYear(uint16_t eday) {
  return EPOCH_YEAR +
         (eday - (eday + 365 * (1 + (EPOCH_YEAR - 1) % 4)) / 1461) / 365;
}

/** epoch day to year, month, day
 * 1900 < EPOCH_YEAR, MAX_YEAR < 2100, (MAX_YEAR - EPOCH_YEAR) < 178.
 * Based on "Software, Practice and Experience", Vol. 23 (1993) page 384.
 *
 * @param[in] eday count of days since epoch
 * @param[out] Y year
 * @param[out] M month
 * @param[out] D day
 */
inline void epochDayToYMD(uint16_t eday, uint16_t* Y, uint8_t* M, uint8_t* D) {
  // Align day number with leap year.
  eday += 365 * (3 - EPOCH_YEAR % 4);
  uint8_t n4 = eday / 1461;
  eday = eday % 1461;
  uint8_t n1 = eday / 365;
  eday = eday % 365;
  uint16_t yr = 4 * n4 + n1 + EPOCH_YEAR - (3 - EPOCH_YEAR % 4);
  if (n1 == 4) {
    // last day of a leap year.
    *Y = yr - 1;
    *M = 12;
    *D = 31;
  } else {
    *Y = yr;
    *M = dayOfYearToMonth(eday, yr);
    *D = eday - daysBeforeMonth(yr, *M) + 1;
  }
}

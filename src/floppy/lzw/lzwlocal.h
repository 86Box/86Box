/***************************************************************************
*          Header for Lempel-Ziv-Welch Encoding and Decoding Library
*
*   File    : lzwlocal.h
*   Purpose : Provides constant definitions for functions values used within
*             the functions for Lempel-Ziv-Welch encoding/decoding.
*   Author  : Michael Dipperstein
*   Date    : February 22, 2015
*
****************************************************************************
*
* LZW: An ANSI C Lempel-Ziv-Welch Encoding/Decoding Routines
* Copyright (C) 2015 by
* Michael Dipperstein (mdipperstein@gmail.com)
*
* This file is part of the lzw library.
*
* The lzw library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public License as
* published by the Free Software Foundation; either version 3 of the
* License, or (at your option) any later version.
*
* The lzw library is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
* General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
***************************************************************************/

#ifndef _LZWLOCAL_H_
#define _LZWLOCAL_H_
/***************************************************************************
*                             INCLUDED FILES
***************************************************************************/
#include <stdio.h>
#include <limits.h>

/***************************************************************************
*                                CONSTANTS
***************************************************************************/
#define MIN_CODE_LEN    12                  /* min # bits in a code word */
#define MAX_CODE_LEN    12                  /* max # bits in a code word */

#define FIRST_CODE      (1 << CHAR_BIT)     /* value of 1st string code */
#define MAX_CODES       (1 << MAX_CODE_LEN)

#if (MIN_CODE_LEN <= CHAR_BIT)
#error Code words must be larger than 1 character
#endif

#if ((MAX_CODES - 1) > INT_MAX)
#error There cannot be more codes than can fit in an integer
#endif

/***************************************************************************
*                                  MACROS
***************************************************************************/
#define CURRENT_MAX_CODES(bits)     ((unsigned int)(1 << (bits)))

#endif  /* ndef _LZWLOCAL_H_ */

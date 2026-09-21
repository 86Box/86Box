/*
 * MiniVHD	Minimalist VHD implementation in C.
 *
 *		This file is part of the MiniVHD Project.
 *
 *		Define library version and build info.
 *
 * Version:	@(#)version.h	1.034	2021/04/16
 *
 * Author:	Fred N. van Kempen, <waltje@varcem.com>
 *
 *		Copyright 2021 Fred N. van Kempen.
 *
 *		MIT License
 *
 *		Permission is hereby granted, free of  charge, to any person
 *		obtaining a copy of this software  and associated documenta-
 *		tion files (the "Software"), to deal in the Software without
 *		restriction, including without limitation the rights to use,
 *		copy, modify, merge, publish, distribute, sublicense, and/or
 *		sell copies of  the Software, and  to permit persons to whom
 *		the Software is furnished to do so, subject to the following
 *		conditions:
 *
 *		The above  copyright notice and this permission notice shall
 *		be included in  all copies or  substantial  portions of  the
 *		Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING  BUT NOT LIMITED TO THE  WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN  NO EVENT  SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER  IN AN ACTION OF  CONTRACT, TORT OR  OTHERWISE, ARISING
 * FROM, OUT OF  O R IN  CONNECTION WITH THE  SOFTWARE OR  THE USE  OR  OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef MINIVHD_VERSION_H
# define MINIVHD_VERSION_H


/* Library name. */
#define LIB_NAME       "MiniVHD"

/* Version info. */
#define LIB_VER_MAJOR  1
#define LIB_VER_MINOR  0
#define LIB_VER_REV    3
#define LIB_VER_PATCH  0


/* Standard C preprocessor macros. */
#define STR_STRING(x)  #x
#define STR(x)         STR_STRING(x)
#define STR_RC(a,e)    a ## , ## e


/* These are used in the application. */
#define LIB_VER_NUM    LIB_VER_MAJOR.LIB_VER_MINOR.LIB_VER_REV
#if defined(LIB_VER_PATCH) && LIB_VER_PATCH > 0
# define LIB_VER_NUM_4 LIB_VER_MAJOR.LIB_VER_MINOR.LIB_VER_REV.LIB_VER_PATCH
#else
# define LIB_VER_NUM_4 LIB_VER_MAJOR.LIB_VER_MINOR.LIB_VER_REV.0
#endif
#define LIB_VERSION    STR(LIB_VER_NUM)
#define LIB_VERSION_4  STR(LIB_VER_NUM_4)


#endif /*MINIVHD_VERSION_H*/

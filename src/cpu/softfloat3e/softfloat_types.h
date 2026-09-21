/*============================================================================

This C header file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014, 2015, 2017 The Regents of the University of
California.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions, and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions, and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

 3. Neither the name of the University nor the names of its contributors may
    be used to endorse or promote products derived from this software without
    specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS", AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ARE
DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=============================================================================*/

#ifndef _SOFTFLOAT_TYPES_H_
#define _SOFTFLOAT_TYPES_H_

#include <stdint.h>

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point types.
*----------------------------------------------------------------------------*/
typedef uint16_t float16, bfloat16;
typedef uint32_t float32;
typedef uint64_t float64;

struct uint128 { uint64_t v0, v64; };
struct uint64_extra { uint64_t extra, v; };
struct uint128_extra { uint64_t extra; struct uint128 v; };

/*----------------------------------------------------------------------------
| Types used to pass 16-bit, 32-bit, 64-bit, and 128-bit floating-point
| arguments and results to/from functions.  These types must be exactly
| 16 bits, 32 bits, 64 bits, and 128 bits in size, respectively.
*----------------------------------------------------------------------------*/
typedef struct f16_t { uint16_t v; } float16_t;
typedef struct f32_t { uint32_t v; } float32_t;
typedef struct f64_t { uint64_t v; } float64_t;
typedef struct uint128 float128_t;

/*----------------------------------------------------------------------------
| The format of an 80-bit extended floating-point number in memory.  This
| structure must contain a 16-bit field named 'signExp' and a 64-bit field
| named 'signif'.
*----------------------------------------------------------------------------*/

struct extFloat80M {
  uint64_t signif;
  uint16_t signExp;
};

/*----------------------------------------------------------------------------
| The type used to pass 80-bit extended floating-point arguments and
| results to/from functions.  This type must have size identical to
| 'struct extFloat80M'.  Type 'extFloat80_t' can be defined as an alias for
| 'struct extFloat80M'.  Alternatively, if a platform has "native" support
| for IEEE-Standard 80-bit extended floating-point, it may be possible,
| if desired, to define 'extFloat80_t' as an alias for the native type
| (presumably either 'long double' or a nonstandard compiler-intrinsic type).
| In that case, the 'signif' and 'signExp' fields of 'struct extFloat80M'
| must align exactly with the locations in memory of the sign, exponent, and
| significand of the native type.
*----------------------------------------------------------------------------*/
typedef struct extFloat80M extFloat80_t, floatx80;

#endif

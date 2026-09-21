/*============================================================================

This C header file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014, 2015, 2016, 2017 The Regents of the
University of California.  All rights reserved.

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

#ifndef _PRIMITIVES_H_
#define _PRIMITIVES_H_

#include <stdbool.h>
#include <stdint.h>
#include "softfloat_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOFTFLOAT_FAST_DIV64TO32

#ifndef softfloat_shortShiftRightJam64
/*----------------------------------------------------------------------------
| Shifts 'a' right by the number of bits given in 'dist', which must be in
| the range 1 to 63.  If any nonzero bits are shifted off, they are "jammed"
| into the least-significant bit of the shifted value by setting the least-
| significant bit to 1.  This shifted-and-jammed value is returned.
*----------------------------------------------------------------------------*/
static __inline
uint64_t softfloat_shortShiftRightJam64(uint64_t a, uint8_t dist)
{
    return a>>dist | ((a & (((uint64_t) 1<<dist) - 1)) != 0);
}
#endif

#ifndef softfloat_shiftRightJam32
/*----------------------------------------------------------------------------
| Shifts 'a' right by the number of bits given in 'dist', which must not
| be zero.  If any nonzero bits are shifted off, they are "jammed" into the
| least-significant bit of the shifted value by setting the least-significant
| bit to 1.  This shifted-and-jammed value is returned.
|   The value of 'dist' can be arbitrarily large.  In particular, if 'dist' is
| greater than 32, the result will be either 0 or 1, depending on whether 'a'
| is zero or nonzero.
*----------------------------------------------------------------------------*/
static __inline uint32_t softfloat_shiftRightJam32(uint32_t a, uint16_t dist)
{
    return (dist < 31) ? a>>dist | ((uint32_t) (a<<(-dist & 31)) != 0) : (a != 0);
}
#endif

#ifndef softfloat_shiftRightJam64
/*----------------------------------------------------------------------------
| Shifts 'a' right by the number of bits given in 'dist', which must not
| be zero.  If any nonzero bits are shifted off, they are "jammed" into the
| least-significant bit of the shifted value by setting the least-significant
| bit to 1.  This shifted-and-jammed value is returned.
|   The value of 'dist' can be arbitrarily large.  In particular, if 'dist' is
| greater than 64, the result will be either 0 or 1, depending on whether 'a'
| is zero or nonzero.
*----------------------------------------------------------------------------*/
static __inline uint64_t softfloat_shiftRightJam64(uint64_t a, uint32_t dist)
{
    return (dist < 63) ? a>>dist | ((uint64_t) (a<<(-dist & 63)) != 0) : (a != 0);
}
#endif

/*----------------------------------------------------------------------------
| A constant table that translates an 8-bit unsigned integer (the array index)
| into the number of leading 0 bits before the most-significant 1 of that
| integer.  For integer zero (index 0), the corresponding table element is 8.
*----------------------------------------------------------------------------*/
extern const uint_least8_t softfloat_countLeadingZeros8[256];

#ifndef softfloat_countLeadingZeros16
/*----------------------------------------------------------------------------
| Returns the number of leading 0 bits before the most-significant 1 bit of
| 'a'.  If 'a' is zero, 16 is returned.
*----------------------------------------------------------------------------*/
static __inline uint8_t softfloat_countLeadingZeros16(uint16_t a)
{
    uint8_t count = 8;
    if (0x100 <= a) {
        count = 0;
        a >>= 8;
    }
    count += softfloat_countLeadingZeros8[a];
    return count;
}
#endif

#ifndef softfloat_countLeadingZeros32
/*----------------------------------------------------------------------------
| Returns the number of leading 0 bits before the most-significant 1 bit of
| 'a'.  If 'a' is zero, 32 is returned.
*----------------------------------------------------------------------------*/
static __inline uint8_t softfloat_countLeadingZeros32(uint32_t a)
{
    uint8_t count = 0;
    if (a < 0x10000) {
        count = 16;
        a <<= 16;
    }
    if (a < 0x1000000) {
        count += 8;
        a <<= 8;
    }
    count += softfloat_countLeadingZeros8[a>>24];
    return count;
}
#endif

#ifndef softfloat_countLeadingZeros64
/*----------------------------------------------------------------------------
| Returns the number of leading 0 bits before the most-significant 1 bit of
| 'a'.  If 'a' is zero, 64 is returned.
*----------------------------------------------------------------------------*/
uint8_t softfloat_countLeadingZeros64(uint64_t a);
#endif

extern const uint16_t softfloat_approxRecip_1k0s[16];
extern const uint16_t softfloat_approxRecip_1k1s[16];

#ifndef softfloat_approxRecip32_1
/*----------------------------------------------------------------------------
| Returns an approximation to the reciprocal of the number represented by 'a',
| where 'a' is interpreted as an unsigned fixed-point number with one integer
| bit and 31 fraction bits.  The 'a' input must be "normalized", meaning that
| its most-significant bit (bit 31) must be 1.  Thus, if A is the value of
| the fixed-point interpretation of 'a', then 1 <= A < 2.  The returned value
| is interpreted as a pure unsigned fraction, having no integer bits and 32
| fraction bits.  The approximation returned is never greater than the true
| reciprocal 1/A, and it differs from the true reciprocal by at most 2.006 ulp
| (units in the last place).
*----------------------------------------------------------------------------*/
#ifdef SOFTFLOAT_FAST_DIV64TO32
#define softfloat_approxRecip32_1(a) ((uint32_t) (UINT64_C(0x7FFFFFFFFFFFFFFF) / (uint32_t) (a)))
#endif
#endif

extern const uint16_t softfloat_approxRecipSqrt_1k0s[16];
extern const uint16_t softfloat_approxRecipSqrt_1k1s[16];

/*----------------------------------------------------------------------------
| Returns an approximation to the reciprocal of the square root of the number
| represented by 'a', where 'a' is interpreted as an unsigned fixed-point
| number either with one integer bit and 31 fraction bits or with two integer
| bits and 30 fraction bits.  The format of 'a' is determined by 'oddExpA',
| which must be either 0 or 1.  If 'oddExpA' is 1, 'a' is interpreted as
| having one integer bit, and if 'oddExpA' is 0, 'a' is interpreted as having
| two integer bits.  The 'a' input must be "normalized", meaning that its
| most-significant bit (bit 31) must be 1.  Thus, if A is the value of the
| fixed-point interpretation of 'a', it follows that 1 <= A < 2 when 'oddExpA'
| is 1, and 2 <= A < 4 when 'oddExpA' is 0.
|   The returned value is interpreted as a pure unsigned fraction, having
| no integer bits and 32 fraction bits.  The approximation returned is never
| greater than the true reciprocal 1/sqrt(A), and it differs from the true
| reciprocal by at most 2.06 ulp (units in the last place).  The approximation
| returned is also always within the range 0.5 to 1; thus, the most-
| significant bit of the result is always set.
*----------------------------------------------------------------------------*/
uint32_t softfloat_approxRecipSqrt32_1(unsigned int oddExpA, uint32_t a);

/*----------------------------------------------------------------------------
| Returns true if the 128-bit unsigned integer formed by concatenating 'a64'
| and 'a0' is equal to the 128-bit unsigned integer formed by concatenating
| 'b64' and 'b0'.
*----------------------------------------------------------------------------*/
static __inline
bool softfloat_eq128(uint64_t a64, uint64_t a0, uint64_t b64, uint64_t b0)
{
    return (a64 == b64) && (a0 == b0);
}

/*----------------------------------------------------------------------------
| Returns true if the 128-bit unsigned integer formed by concatenating 'a64'
| and 'a0' is less than or equal to the 128-bit unsigned integer formed by
| concatenating 'b64' and 'b0'.
*----------------------------------------------------------------------------*/
static __inline
bool softfloat_le128(uint64_t a64, uint64_t a0, uint64_t b64, uint64_t b0)
{
    return (a64 < b64) || ((a64 == b64) && (a0 <= b0));
}

/*----------------------------------------------------------------------------
| Returns true if the 128-bit unsigned integer formed by concatenating 'a64'
| and 'a0' is less than the 128-bit unsigned integer formed by concatenating
| 'b64' and 'b0'.
*----------------------------------------------------------------------------*/
static __inline
bool softfloat_lt128(uint64_t a64, uint64_t a0, uint64_t b64, uint64_t b0)
{
    return (a64 < b64) || ((a64 == b64) && (a0 < b0));
}

/*----------------------------------------------------------------------------
| Shifts the 128 bits formed by concatenating 'a64' and 'a0' left by the
| number of bits given in 'dist', which must be in the range 1 to 63.
*----------------------------------------------------------------------------*/
static __inline
struct uint128 softfloat_shortShiftLeft128(uint64_t a64, uint64_t a0, uint8_t dist)
{
    struct uint128 z;
    z.v64 = a64<<dist | a0>>(-dist & 63);
    z.v0 = a0<<dist;
    return z;
}

/*----------------------------------------------------------------------------
| Shifts the 128 bits formed by concatenating 'a64' and 'a0' right by the
| number of bits given in 'dist', which must be in the range 1 to 63.
*----------------------------------------------------------------------------*/
static __inline
struct uint128 softfloat_shortShiftRight128(uint64_t a64, uint64_t a0, uint8_t dist)
{
    struct uint128 z;
    z.v64 = a64>>dist;
    z.v0 = a64<<(-dist & 63) | a0>>dist;
    return z;
}

/*----------------------------------------------------------------------------
| This function is the same as 'softfloat_shiftRightJam64Extra' (below),
| except that 'dist' must be in the range 1 to 63.
*----------------------------------------------------------------------------*/
static __inline
struct uint64_extra softfloat_shortShiftRightJam64Extra(uint64_t a, uint64_t extra, uint8_t dist)
{
    struct uint64_extra z;
    z.v = a>>dist;
    z.extra = a<<(-dist & 63) | (extra != 0);
    return z;
}

/*----------------------------------------------------------------------------
| Shifts the 128 bits formed by concatenating 'a64' and 'a0' right by the
| number of bits given in 'dist', which must be in the range 1 to 63.  If any
| nonzero bits are shifted off, they are "jammed" into the least-significant
| bit of the shifted value by setting the least-significant bit to 1.  This
| shifted-and-jammed value is returned.
*----------------------------------------------------------------------------*/
static __inline
struct uint128 softfloat_shortShiftRightJam128(uint64_t a64, uint64_t a0, uint8_t dist)
{
    uint8_t negDist = -dist;
    struct uint128 z;
    z.v64 = a64>>dist;
    z.v0 =
        a64<<(negDist & 63) | a0>>dist
            | ((uint64_t) (a0<<(negDist & 63)) != 0);
    return z;
}

/*----------------------------------------------------------------------------
| This function is the same as 'softfloat_shiftRightJam128Extra' (below),
| except that 'dist' must be in the range 1 to 63.
*----------------------------------------------------------------------------*/
extern struct uint128_extra softfloat_shortShiftRightJam128Extra(uint64_t a64, uint64_t a0, uint64_t extra, uint8_t dist);

/*----------------------------------------------------------------------------
| Shifts the 128 bits formed by concatenating 'a' and 'extra' right by 64
| _plus_ the number of bits given in 'dist', which must not be zero.  This
| shifted value is at most 64 nonzero bits and is returned in the 'v' field
| of the 'struct uint64_extra' result.  The 64-bit 'extra' field of the result
| contains a value formed as follows from the bits that were shifted off:  The
| _last_ bit shifted off is the most-significant bit of the 'extra' field, and
| the other 63 bits of the 'extra' field are all zero if and only if _all_but_
| _the_last_ bits shifted off were all zero.
|   (This function makes more sense if 'a' and 'extra' are considered to form
| an unsigned fixed-point number with binary point between 'a' and 'extra'.
| This fixed-point value is shifted right by the number of bits given in
| 'dist', and the integer part of this shifted value is returned in the 'v'
| field of the result.  The fractional part of the shifted value is modified
| as described above and returned in the 'extra' field of the result.)
*----------------------------------------------------------------------------*/
static __inline
struct uint64_extra
 softfloat_shiftRightJam64Extra(uint64_t a, uint64_t extra, uint32_t dist)
{
    struct uint64_extra z;
    if (dist < 64) {
        z.v = a>>dist;
        z.extra = a<<(-dist & 63);
    } else {
        z.v = 0;
        z.extra = (dist == 64) ? a : (a != 0);
    }
    z.extra |= (extra != 0);
    return z;
}

/*----------------------------------------------------------------------------
| Shifts the 128 bits formed by concatenating 'a64' and 'a0' right by the
| number of bits given in 'dist', which must not be zero.  If any nonzero bits
| are shifted off, they are "jammed" into the least-significant bit of the
| shifted value by setting the least-significant bit to 1.  This shifted-and-
| jammed value is returned.
|   The value of 'dist' can be arbitrarily large.  In particular, if 'dist' is
| greater than 128, the result will be either 0 or 1, depending on whether the
| original 128 bits are all zeros.
*----------------------------------------------------------------------------*/
static __inline
struct uint128
 softfloat_shiftRightJam128(uint64_t a64, uint64_t a0, uint32_t dist)
{
    uint8_t u8NegDist;
    struct uint128 z;

    if (dist < 64) {
        u8NegDist = -dist;
        z.v64 = a64>>dist;
        z.v0 =
            a64<<(u8NegDist & 63) | a0>>dist
                | ((uint64_t) (a0<<(u8NegDist & 63)) != 0);
    } else {
        z.v64 = 0;
        z.v0 =
            (dist < 127)
                ? a64>>(dist & 63)
                      | (((a64 & (((uint64_t) 1<<(dist & 63)) - 1)) | a0)
                             != 0)
                : ((a64 | a0) != 0);
    }
    return z;
}

/*----------------------------------------------------------------------------
| Shifts the 192 bits formed by concatenating 'a64', 'a0', and 'extra' right
| by 64 _plus_ the number of bits given in 'dist', which must not be zero.
| This shifted value is at most 128 nonzero bits and is returned in the 'v'
| field of the 'struct uint128_extra' result.  The 64-bit 'extra' field of the
| result contains a value formed as follows from the bits that were shifted
| off:  The _last_ bit shifted off is the most-significant bit of the 'extra'
| field, and the other 63 bits of the 'extra' field are all zero if and only
| if _all_but_the_last_ bits shifted off were all zero.
|   (This function makes more sense if 'a64', 'a0', and 'extra' are considered
| to form an unsigned fixed-point number with binary point between 'a0' and
| 'extra'.  This fixed-point value is shifted right by the number of bits
| given in 'dist', and the integer part of this shifted value is returned
| in the 'v' field of the result.  The fractional part of the shifted value
| is modified as described above and returned in the 'extra' field of the
| result.)
*----------------------------------------------------------------------------*/
static __inline
struct uint128_extra
 softfloat_shiftRightJam128Extra(uint64_t a64, uint64_t a0, uint64_t extra, uint32_t dist)
{
    uint8_t u8NegDist;
    struct uint128_extra z;

    u8NegDist = -dist;
    if (dist < 64) {
        z.v.v64 = a64>>dist;
        z.v.v0 = a64<<(u8NegDist & 63) | a0>>dist;
        z.extra = a0<<(u8NegDist & 63);
    } else {
        z.v.v64 = 0;
        if (dist == 64) {
            z.v.v0 = a64;
            z.extra = a0;
        } else {
            extra |= a0;
            if (dist < 128) {
                z.v.v0 = a64>>(dist & 63);
                z.extra = a64<<(u8NegDist & 63);
            } else {
                z.v.v0 = 0;
                z.extra = (dist == 128) ? a64 : (a64 != 0);
            }
        }
    }
    z.extra |= (extra != 0);
    return z;

}

/*----------------------------------------------------------------------------
| Shifts the 256-bit unsigned integer pointed to by 'aPtr' right by the number
| of bits given in 'dist', which must not be zero.  If any nonzero bits are
| shifted off, they are "jammed" into the least-significant bit of the shifted
| value by setting the least-significant bit to 1.  This shifted-and-jammed
| value is stored at the location pointed to by 'zPtr'.  Each of 'aPtr' and
| 'zPtr' points to an array of four 64-bit elements that concatenate in the
| platform's normal endian order to form a 256-bit integer.
|   The value of 'dist' can be arbitrarily large.  In particular, if 'dist'
| is greater than 256, the stored result will be either 0 or 1, depending on
| whether the original 256 bits are all zeros.
*----------------------------------------------------------------------------*/
void
 softfloat_shiftRightJam256M(const uint64_t *aPtr, uint32_t dist, uint64_t *zPtr);

#ifndef softfloat_add128
/*----------------------------------------------------------------------------
| Returns the sum of the 128-bit integer formed by concatenating 'a64' and
| 'a0' and the 128-bit integer formed by concatenating 'b64' and 'b0'.  The
| addition is modulo 2^128, so any carry out is lost.
*----------------------------------------------------------------------------*/
static __inline
struct uint128 softfloat_add128(uint64_t a64, uint64_t a0, uint64_t b64, uint64_t b0)
{
    struct uint128 z;
    z.v0 = a0 + b0;
    z.v64 = a64 + b64 + (z.v0 < a0);
    return z;
}
#endif

#ifndef softfloat_add256M
/*----------------------------------------------------------------------------
| Adds the two 256-bit integers pointed to by 'aPtr' and 'bPtr'.  The addition
| is modulo 2^256, so any carry out is lost.  The sum is stored at the
| location pointed to by 'zPtr'.  Each of 'aPtr', 'bPtr', and 'zPtr' points to
| an array of four 64-bit elements that concatenate in the platform's normal
| endian order to form a 256-bit integer.
*----------------------------------------------------------------------------*/
void
 softfloat_add256M(const uint64_t *aPtr, const uint64_t *bPtr, uint64_t *zPtr);
#endif

#ifndef softfloat_sub128
/*----------------------------------------------------------------------------
| Returns the difference of the 128-bit integer formed by concatenating 'a64'
| and 'a0' and the 128-bit integer formed by concatenating 'b64' and 'b0'.
| The subtraction is modulo 2^128, so any borrow out (carry out) is lost.
*----------------------------------------------------------------------------*/
static __inline
struct uint128 softfloat_sub128(uint64_t a64, uint64_t a0, uint64_t b64, uint64_t b0)
{
    struct uint128 z;
    z.v0 = a0 - b0;
    z.v64 = a64 - b64;
    z.v64 -= (a0 < b0);
    return z;
}
#endif

#ifndef softfloat_sub256M
/*----------------------------------------------------------------------------
| Subtracts the 256-bit integer pointed to by 'bPtr' from the 256-bit integer
| pointed to by 'aPtr'.  The addition is modulo 2^256, so any borrow out
| (carry out) is lost.  The difference is stored at the location pointed to
| by 'zPtr'.  Each of 'aPtr', 'bPtr', and 'zPtr' points to an array of four
| 64-bit elements that concatenate in the platform's normal endian order to
| form a 256-bit integer.
*----------------------------------------------------------------------------*/
void
 softfloat_sub256M(const uint64_t *aPtr, const uint64_t *bPtr, uint64_t *zPtr);
#endif

/*----------------------------------------------------------------------------
| Returns the 128-bit product of 'a', 'b', and 2^32.
*----------------------------------------------------------------------------*/
static __inline struct uint128 softfloat_mul64ByShifted32To128(uint64_t a, uint32_t b)
{
    uint64_t mid;
    struct uint128 z;
    mid = (uint64_t) (uint32_t) a * b;
    z.v0 = mid<<32;
    z.v64 = (uint64_t) (uint32_t) (a>>32) * b + (mid>>32);
    return z;
}

/*----------------------------------------------------------------------------
| Returns the 128-bit product of 'a' and 'b'.
*----------------------------------------------------------------------------*/
struct uint128 softfloat_mul64To128(uint64_t a, uint64_t b);

/*----------------------------------------------------------------------------
| Returns the product of the 128-bit integer formed by concatenating 'a64' and
| 'a0', multiplied by 'b'.  The multiplication is modulo 2^128; any overflow
| bits are discarded.
*----------------------------------------------------------------------------*/
static __inline
struct uint128 softfloat_mul128By32(uint64_t a64, uint64_t a0, uint32_t b)
{
    struct uint128 z;
    uint64_t mid;
    uint32_t carry;
    z.v0 = a0 * b;
    mid = (uint64_t) (uint32_t) (a0>>32) * b;
    carry = (uint32_t) ((uint32_t) (z.v0>>32) - (uint32_t) mid);
    z.v64 = a64 * b + (uint32_t) ((mid + carry)>>32);
    return z;
}

/*----------------------------------------------------------------------------
| Multiplies the 128-bit unsigned integer formed by concatenating 'a64' and
| 'a0' by the 128-bit unsigned integer formed by concatenating 'b64' and
| 'b0'.  The 256-bit product is stored at the location pointed to by 'zPtr'.
| Argument 'zPtr' points to an array of four 64-bit elements that concatenate
| in the platform's normal endian order to form a 256-bit integer.
*----------------------------------------------------------------------------*/
void
 softfloat_mul128To256M(uint64_t a64, uint64_t a0, uint64_t b64, uint64_t b0, uint64_t *zPtr);
#ifdef __cplusplus
}
#endif

#endif // _PRIMITIVES_H_

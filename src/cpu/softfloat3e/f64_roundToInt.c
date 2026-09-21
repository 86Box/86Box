/*============================================================================

This C source file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014, 2017 The Regents of the University of
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

#include <stdbool.h>
#include <stdint.h>
#include "internals.h"
#include "specialize.h"
#include "softfloat.h"

float64 f64_roundToInt(float64 a, uint8_t scale, uint8_t roundingMode, bool exact, struct softfloat_status_t *status)
{
    int16_t exp;
    int64_t frac;
    bool sign;
    uint64_t uiZ, lastBitMask, roundBitsMask;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    scale &= 0xF;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    exp = expF64UI(a);
    frac = fracF64UI(a);
    sign = signF64UI(a);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (0x433 <= (exp + scale)) {
        if ((exp == 0x7FF) && frac) {
            return softfloat_propagateNaNF64UI(a, 0, status);
        }
        return a;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!exp) {
            frac = 0;
            a = packToF64UI(sign, 0, 0);
        }
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if ((exp + scale) <= 0x3FE) {
        if (!(exp | frac)) return a;
        if (exact) softfloat_raiseFlags(status, softfloat_flag_inexact);
        uiZ = packToF64UI(sign, 0, 0);
        switch (roundingMode) {
         case softfloat_round_near_even:
            if (!frac) break;
         case softfloat_round_near_maxMag:
            if ((exp + scale) == 0x3FE) uiZ |= packToF64UI(0, 0x3FF - scale, 0);
            break;
         case softfloat_round_min:
            if (uiZ) uiZ = packToF64UI(1, 0x3FF - scale, 0);
            break;
         case softfloat_round_max:
            if (!uiZ) uiZ = packToF64UI(0, 0x3FF - scale, 0);
            break;
        }
        return uiZ;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    uiZ = a;
    lastBitMask = (uint64_t) 1<<(0x433 - exp - scale);
    roundBitsMask = lastBitMask - 1;
    if (roundingMode == softfloat_round_near_maxMag) {
        uiZ += lastBitMask>>1;
    } else if (roundingMode == softfloat_round_near_even) {
        uiZ += lastBitMask>>1;
        if (!(uiZ & roundBitsMask)) uiZ &= ~lastBitMask;
    } else if (roundingMode == (signF64UI(uiZ) ? softfloat_round_min : softfloat_round_max)) {
        uiZ += roundBitsMask;
    }
    uiZ &= ~roundBitsMask;
    if (uiZ != a) {
        if (exact) softfloat_raiseFlags(status, softfloat_flag_inexact);
    }
    return uiZ;
}

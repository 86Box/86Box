/*============================================================================

This C source file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014, 2015, 2016 The Regents of the University of
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

/*----------------------------------------------------------------------------
| Return the result of a floating point scale of the double-precision floating
| point value `a' by multiplying it by 2 power of the double-precision
| floating point value 'b' converted to integral value. If the result cannot
| be represented in double precision, then the proper overflow response (for
| positive scaling operand), or the proper underflow response (for negative
| scaling operand) is issued. The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 f64_scalef(float64 a, float64 b, struct softfloat_status_t *status)
{
    bool signA;
    int16_t expA;
    uint64_t sigA;
    bool signB;
    int16_t expB;
    uint64_t sigB;
    int shiftCount;
    int scale = 0;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    signA = signF64UI(a);
    expA  = expF64UI(a);
    sigA  = fracF64UI(a);
    signB = signF64UI(b);
    expB  = expF64UI(b);
    sigB  = fracF64UI(b);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expB == 0x7FF) {
        if (sigB) return softfloat_propagateNaNF64UI(a, b, status);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!expA) sigA = 0;
        if (!expB) sigB = 0;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expA == 0x7FF) {
        if (sigA) {
            int aIsSignalingNaN = (sigA & UINT64_C(0x0008000000000000)) == 0;
            if (aIsSignalingNaN || expB != 0x7FF || sigB)
                return softfloat_propagateNaNF64UI(a, b, status);

            return signB ? 0 : packToF64UI(0, 0x7FF, 0);
        }

        if (expB == 0x7FF && signB) {
            softfloat_raiseFlags(status, softfloat_flag_invalid);
            return defaultNaNF64UI;
        }

        return a;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (! expA) {
        if (! sigA) {
            if (expB == 0x7FF && ! signB) {
                softfloat_raiseFlags(status, softfloat_flag_invalid);
                return defaultNaNF64UI;
            }
            return packToF64UI(signA, 0, 0);
        }
        softfloat_raiseFlags(status, softfloat_flag_denormal);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if ((expB | sigB) == 0) return a;

    if (expB == 0x7FF) {
        if (signB) return packToF64UI(signA, 0, 0);
        return packToF64UI(signA, 0x7FF, 0);
    }

    if (0x40F <= expB) {
        // handle obvious overflow/underflow result
        return softfloat_roundPackToF64(signA, signB ? -0x3FF : 0x7FF, sigA, status);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expB < 0x3FF) {
        if (expB == 0)
            softfloat_raiseFlags(status, softfloat_flag_denormal);
        scale = -signB;
    }
    else {
        sigB |= UINT64_C(0x0010000000000000);
        shiftCount = 0x433 - expB;
        uint64_t prev_sigB = sigB;
        sigB >>= shiftCount;
        scale = (int32_t) sigB;
        if (signB) {
            if ((sigB<<shiftCount) != prev_sigB) scale++;
            scale = -scale;
        }

        if (scale >  0x1000) scale =  0x1000;
        if (scale < -0x1000) scale = -0x1000;
    }

    if (expA != 0) {
        sigA |= UINT64_C(0x0010000000000000);
    } else {
        expA++;
    }

    expA += scale - 1;
    sigA <<= 10;
    return softfloat_normRoundPackToF64(signA, expA, sigA, status);
}

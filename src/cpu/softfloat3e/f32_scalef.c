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
| Return the result of a floating point scale of the single-precision floating
| point value `a' by multiplying it by 2 power of the single-precision
| floating point value 'b' converted to integral value. If the result cannot
| be represented in single precision, then the proper overflow response (for
| positive scaling operand), or the proper underflow response (for negative
| scaling operand) is issued. The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 f32_scalef(float32 a, float32 b, struct softfloat_status_t *status)
{
    bool signA;
    int16_t expA;
    uint32_t sigA;
    bool signB;
    int16_t expB;
    uint32_t sigB;
    int shiftCount;
    int scale = 0;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    signA = signF32UI(a);
    expA  = expF32UI(a);
    sigA  = fracF32UI(a);
    signB = signF32UI(b);
    expB  = expF32UI(b);
    sigB  = fracF32UI(b);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expB == 0xFF) {
        if (sigB) return softfloat_propagateNaNF32UI(a, b, status);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!expA) sigA = 0;
        if (!expB) sigB = 0;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expA == 0xFF) {
        if (sigA) {
            int aIsSignalingNaN = (sigA & 0x00400000) == 0;
            if (aIsSignalingNaN || expB != 0xFF || sigB)
                return softfloat_propagateNaNF32UI(a, b, status);

            return signB ? 0 : packToF32UI(0, 0xFF, 0);
        }

        if (expB == 0xFF && signB) {
            softfloat_raiseFlags(status, softfloat_flag_invalid);
            return defaultNaNF32UI;
        }

        return a;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (! expA) {
        if (! sigA) {
            if (expB == 0xFF && ! signB) {
                softfloat_raiseFlags(status, softfloat_flag_invalid);
                return defaultNaNF32UI;
            }
            return packToF32UI(signA, 0, 0);
        }
        softfloat_raiseFlags(status, softfloat_flag_denormal);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if ((expB | sigB) == 0) return a;

    if (expB == 0xFF) {
        if (signB) return packToF32UI(signA, 0, 0);
        return packToF32UI(signA, 0xFF, 0);
    }

    if (expB >= 0x8E) {
        // handle obvious overflow/underflow result
        return softfloat_roundPackToF32(signA, signB ? -0x7F : 0xFF, sigA, status);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expB <= 0x7E) {
        if (! expB)
            softfloat_raiseFlags(status, softfloat_flag_denormal);
        scale = -signB;
    }
    else {
        shiftCount = expB - 0x9E;
        sigB = (sigB | 0x800000)<<8;
        scale = sigB>>(-shiftCount);

        if (signB) {
            if ((uint32_t) (sigB<<(shiftCount & 31))) scale++;
            scale = -scale;
        }

        if (scale >  0x200) scale =  0x200;
        if (scale < -0x200) scale = -0x200;
    }

    if (expA != 0) {
        sigA |= 0x00800000;
    } else {
        expA++;
    }

    expA += scale - 1;
    sigA <<= 7;
    return softfloat_normRoundPackToF32(signA, expA, sigA, status);
}

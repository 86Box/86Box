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
#include "primitives.h"
#include "specialize.h"
#include "softfloat.h"

float32 f32_sqrt(float32 a, struct softfloat_status_t *status)
{
    bool signA;
    int16_t expA;
    uint32_t sigA;
    struct exp16_sig32 normExpSig;
    int16_t expZ;
    uint32_t sigZ, shiftedSigZ;
    uint32_t negRem;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    signA = signF32UI(a);
    expA  = expF32UI(a);
    sigA  = fracF32UI(a);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expA == 0xFF) {
        if (sigA) {
            return softfloat_propagateNaNF32UI(a, 0, status);
        }
        if (! signA) return a;
        goto invalid;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!expA) {
            sigA = 0;
            a = packToF32UI(signA, 0, 0);
        }
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (signA) {
        if (! (expA | sigA)) return a;
        goto invalid;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (! expA) {
        if (! sigA) return a;
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF32Sig(sigA);
        expA = normExpSig.exp;
        sigA = normExpSig.sig;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expZ = ((expA - 0x7F)>>1) + 0x7E;
    expA &= 1;
    sigA = (sigA | 0x00800000)<<8;
    sigZ =
        ((uint64_t) sigA * softfloat_approxRecipSqrt32_1(expA, sigA))>>32;
    if (expA) sigZ >>= 1;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    sigZ += 2;
    if ((sigZ & 0x3F) < 2) {
        shiftedSigZ = sigZ>>2;
        negRem = shiftedSigZ * shiftedSigZ;
        sigZ &= ~3;
        if (negRem & 0x80000000) {
            sigZ |= 1;
        } else {
            if (negRem) --sigZ;
        }
    }
    return softfloat_roundPackToF32(0, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 invalid:
    softfloat_raiseFlags(status,  softfloat_flag_invalid);
    return defaultNaNF32UI;
}

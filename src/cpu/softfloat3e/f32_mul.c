/*============================================================================

This C source file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014, 2015 The Regents of the University of
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

float32 f32_mul(float32 a, float32 b, struct softfloat_status_t *status)
{
    bool signA;
    int16_t expA;
    uint32_t sigA;
    bool signB;
    int16_t expB;
    uint32_t sigB;
    bool signZ;
    uint32_t magBits;
    struct exp16_sig32 normExpSig;
    int16_t expZ;
    uint32_t sigZ, uiZ;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    signA = signF32UI(a);
    expA  = expF32UI(a);
    sigA  = fracF32UI(a);
    signB = signF32UI(b);
    expB  = expF32UI(b);
    sigB  = fracF32UI(b);
    signZ = signA ^ signB;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!expA) sigA = 0;
        if (!expB) sigB = 0;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expA == 0xFF) {
        if (sigA || ((expB == 0xFF) && sigB)) goto propagateNaN;
        magBits = expB | sigB;
        if (sigB && !expB) softfloat_raiseFlags(status, softfloat_flag_denormal);
        goto infArg;
    }
    if (expB == 0xFF) {
        if (sigB) goto propagateNaN;
        magBits = expA | sigA;
        if (sigA && !expA) softfloat_raiseFlags(status, softfloat_flag_denormal);
        goto infArg;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (! expA) {
        if (! sigA) {
            if (sigB && !expB) softfloat_raiseFlags(status, softfloat_flag_denormal);
            goto zero;
        }
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF32Sig(sigA);
        expA = normExpSig.exp;
        sigA = normExpSig.sig;
    }
    if (! expB) {
        if (! sigB) {
            if (sigA && !expA) softfloat_raiseFlags(status, softfloat_flag_denormal);
            goto zero;
        }
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF32Sig(sigB);
        expB = normExpSig.exp;
        sigB = normExpSig.sig;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expZ = expA + expB - 0x7F;
    sigA = (sigA | 0x00800000)<<7;
    sigB = (sigB | 0x00800000)<<8;
    sigZ = softfloat_shortShiftRightJam64((uint64_t) sigA * sigB, 32);
    if (sigZ < 0x40000000) {
        --expZ;
        sigZ <<= 1;
    }
    return softfloat_roundPackToF32(signZ, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 propagateNaN:
    return softfloat_propagateNaNF32UI(a, b, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 infArg:
    if (! magBits) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        uiZ = defaultNaNF32UI;
    } else {
        uiZ = packToF32UI(signZ, 0xFF, 0);
    }
    return uiZ;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 zero:
    return packToF32UI(signZ, 0, 0);
}

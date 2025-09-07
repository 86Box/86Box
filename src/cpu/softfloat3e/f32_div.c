/*============================================================================

This C source file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014 The Regents of the University of California.
All rights reserved.

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

#define SOFTFLOAT_FAST_DIV64TO32

float32 f32_div(float32 a, float32 b, struct softfloat_status_t *status)
{
    bool signA;
    int16_t expA;
    uint32_t sigA;
    bool signB;
    int16_t expB;
    uint32_t sigB;
    bool signZ;
    struct exp16_sig32 normExpSig;
    int16_t expZ;
#ifdef SOFTFLOAT_FAST_DIV64TO32
    uint64_t sig64A;
    uint32_t sigZ;
#endif

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
        if (sigA) goto propagateNaN;
        if (expB == 0xFF) {
            if (sigB) goto propagateNaN;
            goto invalid;
        }
        if (sigB && !expB)
            softfloat_raiseFlags(status, softfloat_flag_denormal);
        goto infinity;
    }
    if (expB == 0xFF) {
        if (sigB) goto propagateNaN;
        if (sigA && !expA)
            softfloat_raiseFlags(status, softfloat_flag_denormal);
        goto zero;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (! expB) {
        if (! sigB) {
            if (! (expA | sigA)) goto invalid;
            softfloat_raiseFlags(status, softfloat_flag_infinite);
            goto infinity;
        }
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF32Sig(sigB);
        expB = normExpSig.exp;
        sigB = normExpSig.sig;
    }
    if (! expA) {
        if (! sigA) goto zero;
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF32Sig(sigA);
        expA = normExpSig.exp;
        sigA = normExpSig.sig;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expZ = expA - expB + 0x7E;
    sigA |= 0x00800000;
    sigB |= 0x00800000;
#ifdef SOFTFLOAT_FAST_DIV64TO32
    if (sigA < sigB) {
        --expZ;
        sig64A = (uint64_t) sigA<<31;
    } else {
        sig64A = (uint64_t) sigA<<30;
    }
    sigZ = sig64A / sigB;
    if (! (sigZ & 0x3F)) sigZ |= ((uint64_t) sigB * sigZ != sig64A);
#endif
    return softfloat_roundPackToF32(signZ, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 propagateNaN:
    return softfloat_propagateNaNF32UI(a, b, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 invalid:
    softfloat_raiseFlags(status, softfloat_flag_invalid);
    return defaultNaNF32UI;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 infinity:
    return packToF32UI(signZ, 0xFF, 0);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 zero:
    return packToF32UI(signZ, 0, 0);
}

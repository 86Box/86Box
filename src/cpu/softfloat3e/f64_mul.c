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
#include "primitives.h"
#include "specialize.h"
#include "softfloat.h"

float64 f64_mul(float64 a, float64 b, struct softfloat_status_t *status)
{
    bool signA;
    int16_t expA;
    uint64_t sigA;
    bool signB;
    int16_t expB;
    uint64_t sigB;
    bool signZ;
    uint64_t magBits;
    struct exp16_sig64 normExpSig;
    int16_t expZ;
    struct uint128 sig128Z;
    uint64_t sigZ, uiZ;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    signA = signF64UI(a);
    expA  = expF64UI(a);
    sigA  = fracF64UI(a);
    signB = signF64UI(b);
    expB  = expF64UI(b);
    sigB  = fracF64UI(b);
    signZ = signA ^ signB;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!expA) sigA = 0;
        if (!expB) sigB = 0;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expA == 0x7FF) {
        if (sigA || ((expB == 0x7FF) && sigB)) goto propagateNaN;
        magBits = expB | sigB;
        if (sigB && !expB) softfloat_raiseFlags(status, softfloat_flag_denormal);
        goto infArg;
    }
    if (expB == 0x7FF) {
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
        normExpSig = softfloat_normSubnormalF64Sig(sigA);
        expA = normExpSig.exp;
        sigA = normExpSig.sig;
    }
    if (! expB) {
        if (! sigB) {
            if (sigA && !expA) softfloat_raiseFlags(status, softfloat_flag_denormal);
            goto zero;
        }
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF64Sig(sigB);
        expB = normExpSig.exp;
        sigB = normExpSig.sig;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expZ = expA + expB - 0x3FF;
    sigA = (sigA | UINT64_C(0x0010000000000000))<<10;
    sigB = (sigB | UINT64_C(0x0010000000000000))<<11;
    sig128Z = softfloat_mul64To128(sigA, sigB);
    sigZ = sig128Z.v64 | (sig128Z.v0 != 0);
    if (sigZ < UINT64_C(0x4000000000000000)) {
        --expZ;
        sigZ <<= 1;
    }
    return softfloat_roundPackToF64(signZ, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 propagateNaN:
    return softfloat_propagateNaNF64UI(a, b, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 infArg:
    if (! magBits) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        uiZ = defaultNaNF64UI;
    } else {
        uiZ = packToF64UI(signZ, 0x7FF, 0);
    }
    return uiZ;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 zero:
    return packToF64UI(signZ, 0, 0);
}

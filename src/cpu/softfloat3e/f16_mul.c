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
#include "specialize.h"
#include "softfloat.h"

float16 f16_mul(float16 a, float16 b, struct softfloat_status_t *status)
{
    bool signA;
    int8_t expA;
    uint16_t sigA;
    bool signB;
    int8_t expB;
    uint16_t sigB;
    bool signZ;
    uint16_t magBits;
    struct exp8_sig16 normExpSig;
    int8_t expZ;
    uint32_t sig32Z;
    uint16_t sigZ, uiZ;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    signA = signF16UI(a);
    expA  = expF16UI(a);
    sigA  = fracF16UI(a);
    signB = signF16UI(b);
    expB  = expF16UI(b);
    sigB  = fracF16UI(b);
    signZ = signA ^ signB;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!expA) sigA = 0;
        if (!expB) sigB = 0;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expA == 0x1F) {
        if (sigA || ((expB == 0x1F) && sigB)) goto propagateNaN;
        magBits = expB | sigB;
        if (sigB && !expB) softfloat_raiseFlags(status, softfloat_flag_denormal);
        goto infArg;
    }
    if (expB == 0x1F) {
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
        normExpSig = softfloat_normSubnormalF16Sig(sigA);
        expA = normExpSig.exp;
        sigA = normExpSig.sig;
    }
    if (! expB) {
        if (! sigB) {
            if (sigB && !expB) softfloat_raiseFlags(status, softfloat_flag_denormal);
            goto zero;
        }
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF16Sig(sigB);
        expB = normExpSig.exp;
        sigB = normExpSig.sig;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expZ = expA + expB - 0xF;
    sigA = (sigA | 0x0400)<<4;
    sigB = (sigB | 0x0400)<<5;
    sig32Z = (uint32_t) sigA * sigB;
    sigZ = sig32Z>>16;
    if (sig32Z & 0xFFFF) sigZ |= 1;
    if (sigZ < 0x4000) {
        --expZ;
        sigZ <<= 1;
    }
    return softfloat_roundPackToF16(signZ, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 propagateNaN:
    return softfloat_propagateNaNF16UI(a, b, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 infArg:
    if (! magBits) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        uiZ = defaultNaNF16UI;
    } else {
        uiZ = packToF16UI(signZ, 0x1F, 0);
    }
    return uiZ;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 zero:
    return packToF16UI(signZ, 0, 0);
}

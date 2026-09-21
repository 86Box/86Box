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

#define SOFTFLOAT_FAST_DIV32TO16 1

extern const uint16_t softfloat_approxRecip_1k0s[];
extern const uint16_t softfloat_approxRecip_1k1s[];

float16 f16_div(float16 a, float16 b, struct softfloat_status_t *status)
{
    bool signA;
    int8_t expA;
    uint16_t sigA;
    bool signB;
    int8_t expB;
    uint16_t sigB;
    bool signZ;
    struct exp8_sig16 normExpSig;
    int8_t expZ;
#ifdef SOFTFLOAT_FAST_DIV32TO16
    uint32_t sig32A;
    uint16_t sigZ;
#endif

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
        if (sigA) goto propagateNaN;
        if (expB == 0x1F) {
            if (sigB) goto propagateNaN;
            goto invalid;
        }
        if (sigB && !expB)
            softfloat_raiseFlags(status, softfloat_flag_denormal);
        goto infinity;
    }
    if (expB == 0x1F) {
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
        normExpSig = softfloat_normSubnormalF16Sig(sigB);
        expB = normExpSig.exp;
        sigB = normExpSig.sig;
    }
    if (! expA) {
        if (! sigA) goto zero;
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF16Sig(sigA);
        expA = normExpSig.exp;
        sigA = normExpSig.sig;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expZ = expA - expB + 0xE;
    sigA |= 0x0400;
    sigB |= 0x0400;
#ifdef SOFTFLOAT_FAST_DIV32TO16
    if (sigA < sigB) {
        --expZ;
        sig32A = (uint32_t) sigA<<15;
    } else {
        sig32A = (uint32_t) sigA<<14;
    }
    sigZ = sig32A / sigB;
    if (! (sigZ & 7)) sigZ |= ((uint32_t) sigB * sigZ != sig32A);
#endif
    return softfloat_roundPackToF16(signZ, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 propagateNaN:
    return softfloat_propagateNaNF16UI(a, b, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 invalid:
    softfloat_raiseFlags(status, softfloat_flag_invalid);
    return defaultNaNF16UI;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 infinity:
    return packToF16UI(signZ, 0x1F, 0);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 zero:
    return packToF16UI(signZ, 0, 0);
}

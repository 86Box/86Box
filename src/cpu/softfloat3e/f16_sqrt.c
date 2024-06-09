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

extern const uint16_t softfloat_approxRecipSqrt_1k0s[];
extern const uint16_t softfloat_approxRecipSqrt_1k1s[];

float16 f16_sqrt(float16 a, struct softfloat_status_t *status)
{
    bool signA;
    int8_t expA;
    uint16_t sigA;
    struct exp8_sig16 normExpSig;
    int8_t expZ;
    int index;
    uint16_t r0;
    uint32_t ESqrR0;
    uint16_t sigma0;
    uint16_t recipSqrt16, sigZ, shiftedSigZ;
    uint16_t negRem;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    signA = signF16UI(a);
    expA  = expF16UI(a);
    sigA  = fracF16UI(a);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expA == 0x1F) {
        if (sigA) {
            return softfloat_propagateNaNF16UI(a, 0, status);
        }
        if (! signA) return a;
        goto invalid;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!expA) {
            sigA = 0;
            a = packToF16UI(signA, 0, 0);
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
        normExpSig = softfloat_normSubnormalF16Sig(sigA);
        expA = normExpSig.exp;
        sigA = normExpSig.sig;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expZ = ((expA - 0xF)>>1) + 0xE;
    expA &= 1;
    sigA |= 0x0400;
    index = (sigA>>6 & 0xE) + expA;
    r0 = softfloat_approxRecipSqrt_1k0s[index]
             - (((uint32_t) softfloat_approxRecipSqrt_1k1s[index] * (sigA & 0x7F)) >>11);
    ESqrR0 = ((uint32_t) r0 * r0)>>1;
    if (expA) ESqrR0 >>= 1;
    sigma0 = ~(uint16_t) ((ESqrR0 * sigA)>>16);
    recipSqrt16 = r0 + (((uint32_t) r0 * sigma0)>>25);
    if (! (recipSqrt16 & 0x8000)) recipSqrt16 = 0x8000;
    sigZ = ((uint32_t) (sigA<<5) * recipSqrt16)>>16;
    if (expA) sigZ >>= 1;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    ++sigZ;
    if (! (sigZ & 7)) {
        shiftedSigZ = sigZ>>1;
        negRem = shiftedSigZ * shiftedSigZ;
        sigZ &= ~1;
        if (negRem & 0x8000) {
            sigZ |= 1;
        } else {
            if (negRem) --sigZ;
        }
    }
    return softfloat_roundPackToF16(0, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 invalid:
    softfloat_raiseFlags(status, softfloat_flag_invalid);
    return defaultNaNF16UI;
}

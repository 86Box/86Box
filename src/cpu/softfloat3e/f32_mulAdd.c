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

#include <stdint.h>
#include "internals.h"
#include "primitives.h"
#include "softfloat.h"
#include "specialize.h"

float32 f32_mulAdd(float32 a, float32 b, float32 c, uint8_t op, struct softfloat_status_t *status)
{
    bool signA;
    int16_t expA;
    uint32_t sigA;
    bool signB;
    int16_t expB;
    uint32_t sigB;
    bool signC;
    int16_t expC;
    uint32_t sigC;
    bool signProd;
    uint32_t magBits, uiA, uiB, uiC, uiZ;
    struct exp16_sig32 normExpSig;
    int16_t expProd;
    uint64_t sigProd;
    bool signZ;
    int16_t expZ;
    uint32_t sigZ;
    int16_t expDiff;
    uint64_t sig64Z, sig64C;
    int8_t shiftDist;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    uiA = a;
    uiB = b;
    uiC = c;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    signA = signF32UI(uiA);
    expA  = expF32UI(uiA);
    sigA  = fracF32UI(uiA);
    signB = signF32UI(uiB);
    expB  = expF32UI(uiB);
    sigB  = fracF32UI(uiB);
    signC = signF32UI(uiC) ^ ((op & softfloat_mulAdd_subC) != 0);
    expC  = expF32UI(uiC);
    sigC  = fracF32UI(uiC);
    signProd = signA ^ signB ^ ((op & softfloat_mulAdd_subProd) != 0);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    bool aisNaN = (expA == 0xFF) && sigA;
    bool bisNaN = (expB == 0xFF) && sigB;
    bool cisNaN = (expC == 0xFF) && sigC;
    if (aisNaN | bisNaN | cisNaN) {
        uiZ = (aisNaN | bisNaN) ? softfloat_propagateNaNF32UI(uiA, uiB, status) : 0;
        return softfloat_propagateNaNF32UI(uiZ, uiC, status);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!expA) sigA = 0;
        if (!expB) sigB = 0;
        if (!expC) sigC = 0;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expA == 0xFF) {
        magBits = expB | sigB;
        goto infProdArg;
    }
    if (expB == 0xFF) {
        magBits = expA | sigA;
        goto infProdArg;
    }
    if (expC == 0xFF) {
        if ((sigA && !expA) || (sigB && !expB)) {
            softfloat_raiseFlags(status, softfloat_flag_denormal);
        }
        return packToF32UI(signC, 0xFF, 0);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (! expA) {
        if (! sigA) goto zeroProd;
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF32Sig(sigA);
        expA = normExpSig.exp;
        sigA = normExpSig.sig;
    }
    if (! expB) {
        if (! sigB) goto zeroProd;
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF32Sig(sigB);
        expB = normExpSig.exp;
        sigB = normExpSig.sig;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expProd = expA + expB - 0x7E;
    sigA = (sigA | 0x00800000)<<7;
    sigB = (sigB | 0x00800000)<<7;
    sigProd = (uint64_t) sigA * sigB;
    if (sigProd < UINT64_C(0x2000000000000000)) {
        --expProd;
        sigProd <<= 1;
    }
    signZ = signProd;
    if (! expC) {
        if (! sigC) {
            expZ = expProd - 1;
            sigZ = softfloat_shortShiftRightJam64(sigProd, 31);
            goto roundPack;
        }
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF32Sig(sigC);
        expC = normExpSig.exp;
        sigC = normExpSig.sig;
    }
    sigC = (sigC | 0x00800000)<<6;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expDiff = expProd - expC;
    if (signProd == signC) {
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        if (expDiff <= 0) {
            expZ = expC;
            sigZ = sigC + softfloat_shiftRightJam64(sigProd, 32 - expDiff);
        } else {
            expZ = expProd;
            sig64Z = sigProd + softfloat_shiftRightJam64((uint64_t) sigC<<32, expDiff);
            sigZ = softfloat_shortShiftRightJam64(sig64Z, 32);
        }
        if (sigZ < 0x40000000) {
            --expZ;
            sigZ <<= 1;
        }
    } else {
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        sig64C = (uint64_t) sigC<<32;
        if (expDiff < 0) {
            signZ = signC;
            expZ = expC;
            sig64Z = sig64C - softfloat_shiftRightJam64(sigProd, -expDiff);
        } else if (! expDiff) {
            expZ = expProd;
            sig64Z = sigProd - sig64C;
            if (! sig64Z) goto completeCancellation;
            if (sig64Z & UINT64_C(0x8000000000000000)) {
                signZ = ! signZ;
                sig64Z = -sig64Z;
            }
        } else {
            expZ = expProd;
            sig64Z = sigProd - softfloat_shiftRightJam64(sig64C, expDiff);
        }
        shiftDist = softfloat_countLeadingZeros64(sig64Z) - 1;
        expZ -= shiftDist;
        shiftDist -= 32;
        if (shiftDist < 0) {
            sigZ = softfloat_shortShiftRightJam64(sig64Z, -shiftDist);
        } else {
            sigZ = (uint32_t) sig64Z<<shiftDist;
        }
    }
 roundPack:
    return softfloat_roundPackToF32(signZ, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 infProdArg:
    if (magBits) {
        uiZ = packToF32UI(signProd, 0xFF, 0);
        if (signProd == signC || expC != 0xFF) {
            if ((sigA && !expA) || (sigB && !expB) || (sigC && !expC))
                softfloat_raiseFlags(status, softfloat_flag_denormal);
            return uiZ;
        }
    }
    softfloat_raiseFlags(status, softfloat_flag_invalid);
    uiZ = defaultNaNF32UI;
    return softfloat_propagateNaNF32UI(uiZ, uiC, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 zeroProd:
    uiZ = packToF32UI(signC, expC, sigC);
    if (!expC && sigC) {
        /* Exact zero plus a denormal */
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        if (softfloat_flushUnderflowToZero(status)) {
            softfloat_raiseFlags(status, softfloat_flag_underflow | softfloat_flag_inexact);
            return packToF32UI(signC, 0, 0);
        }
    }
    if (! (expC | sigC) && (signProd != signC)) {
 completeCancellation:
        uiZ = packToF32UI((softfloat_getRoundingMode(status) == softfloat_round_min), 0, 0);
    }
    return uiZ;
}

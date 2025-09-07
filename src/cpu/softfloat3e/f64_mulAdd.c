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

float64 f64_mulAdd(float64 a, float64 b, float64 c, uint8_t op, struct softfloat_status_t *status)
{
    bool signA;
    int16_t expA;
    uint64_t sigA;
    bool signB;
    int16_t expB;
    uint64_t sigB;
    bool signC;
    int16_t expC;
    uint64_t sigC;
    bool signZ;
    uint64_t magBits, uiA, uiB, uiC, uiZ;
    struct exp16_sig64 normExpSig;
    int16_t expZ;
    struct uint128 sig128Z;
    uint64_t sigZ;
    int16_t expDiff;
    struct uint128 sig128C;
    int8_t shiftDist;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    uiA = a;
    uiB = b;
    uiC = c;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    signA = signF64UI(uiA);
    expA  = expF64UI(uiA);
    sigA  = fracF64UI(uiA);
    signB = signF64UI(uiB);
    expB  = expF64UI(uiB);
    sigB  = fracF64UI(uiB);
    signC = signF64UI(uiC) ^ ((op & softfloat_mulAdd_subC) != 0);
    expC  = expF64UI(uiC);
    sigC  = fracF64UI(uiC);
    signZ = signA ^ signB ^ ((op & softfloat_mulAdd_subProd) != 0);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    bool aisNaN = (expA == 0x7FF) && sigA;
    bool bisNaN = (expB == 0x7FF) && sigB;
    bool cisNaN = (expC == 0x7FF) && sigC;
    if (aisNaN | bisNaN | cisNaN) {
        uiZ = (aisNaN | bisNaN) ? softfloat_propagateNaNF64UI(uiA, uiB, status) : 0;
        return softfloat_propagateNaNF64UI(uiZ, uiC, status);
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
    if (expA == 0x7FF) {
        magBits = expB | sigB;
        goto infProdArg;
    }
    if (expB == 0x7FF) {
        magBits = expA | sigA;
        goto infProdArg;
    }
    if (expC == 0x7FF) {
        if ((sigA && !expA) || (sigB && !expB)) {
            softfloat_raiseFlags(status, softfloat_flag_denormal);
        }
        return packToF64UI(signC, 0x7FF, 0);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (! expA) {
        if (! sigA) goto zeroProd;
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF64Sig(sigA);
        expA = normExpSig.exp;
        sigA = normExpSig.sig;
    }
    if (! expB) {
        if (! sigB) goto zeroProd;
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF64Sig(sigB);
        expB = normExpSig.exp;
        sigB = normExpSig.sig;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expZ = expA + expB - 0x3FE;
    sigA = (sigA | UINT64_C(0x0010000000000000))<<10;
    sigB = (sigB | UINT64_C(0x0010000000000000))<<10;
    sig128Z = softfloat_mul64To128(sigA, sigB);
    if (sig128Z.v64 < UINT64_C(0x2000000000000000)) {
        --expZ;
        sig128Z = softfloat_add128(sig128Z.v64, sig128Z.v0, sig128Z.v64, sig128Z.v0);
    }
    if (! expC) {
        if (! sigC) {
            --expZ;
            sigZ = sig128Z.v64<<1 | (sig128Z.v0 != 0);
            goto roundPack;
        }
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalF64Sig(sigC);
        expC = normExpSig.exp;
        sigC = normExpSig.sig;
    }
    sigC = (sigC | UINT64_C(0x0010000000000000))<<9;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expDiff = expZ - expC;
    if (expDiff < 0) {
        expZ = expC;
        if ((signZ == signC) || (expDiff < -1)) {
            sig128Z.v64 = softfloat_shiftRightJam64(sig128Z.v64, -expDiff);
        } else {
            sig128Z = softfloat_shortShiftRightJam128(sig128Z.v64, sig128Z.v0, 1);
        }
    } else if (expDiff) {
        sig128C = softfloat_shiftRightJam128(sigC, 0, expDiff);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (signZ == signC) {
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        if (expDiff <= 0) {
            sigZ = (sigC + sig128Z.v64) | (sig128Z.v0 != 0);
        } else {
            sig128Z = softfloat_add128(sig128Z.v64, sig128Z.v0, sig128C.v64, sig128C.v0);
            sigZ = sig128Z.v64 | (sig128Z.v0 != 0);
        }
        if (sigZ < UINT64_C(0x4000000000000000)) {
            --expZ;
            sigZ <<= 1;
        }
    } else {
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        if (expDiff < 0) {
            signZ = signC;
            sig128Z = softfloat_sub128(sigC, 0, sig128Z.v64, sig128Z.v0);
        } else if (! expDiff) {
            sig128Z.v64 = sig128Z.v64 - sigC;
            if (! (sig128Z.v64 | sig128Z.v0)) goto completeCancellation;
            if (sig128Z.v64 & UINT64_C(0x8000000000000000)) {
                signZ = ! signZ;
                sig128Z = softfloat_sub128(0, 0, sig128Z.v64, sig128Z.v0);
            }
        } else {
            sig128Z = softfloat_sub128(sig128Z.v64, sig128Z.v0, sig128C.v64, sig128C.v0);
        }
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        if (! sig128Z.v64) {
            expZ -= 64;
            sig128Z.v64 = sig128Z.v0;
            sig128Z.v0 = 0;
        }
        shiftDist = softfloat_countLeadingZeros64(sig128Z.v64) - 1;
        expZ -= shiftDist;
        if (shiftDist < 0) {
            sigZ = softfloat_shortShiftRightJam64(sig128Z.v64, -shiftDist);
        } else {
            sig128Z = softfloat_shortShiftLeft128(sig128Z.v64, sig128Z.v0, shiftDist);
            sigZ = sig128Z.v64;
        }
        sigZ |= (sig128Z.v0 != 0);
    }
 roundPack:
    return softfloat_roundPackToF64(signZ, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 infProdArg:
    if (magBits) {
        uiZ = packToF64UI(signZ, 0x7FF, 0);
        if (signZ == signC || expC != 0x7FF) {
            if ((sigA && !expA) || (sigB && !expB) || (sigC && !expC))
                softfloat_raiseFlags(status, softfloat_flag_denormal);
            return uiZ;
        }
    }
    softfloat_raiseFlags(status, softfloat_flag_invalid);
    uiZ = defaultNaNF64UI;
    return softfloat_propagateNaNF64UI(uiZ, uiC, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 zeroProd:
    uiZ = packToF64UI(signC, expC, sigC);
    if (!expC && sigC) {
        /* Exact zero plus a denormal */
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        if (softfloat_flushUnderflowToZero(status)) {
            softfloat_raiseFlags(status, softfloat_flag_underflow | softfloat_flag_inexact);
            return packToF64UI(signC, 0, 0);
        }
    }
    if (! (expC | sigC) && (signZ != signC)) {
 completeCancellation:
        uiZ = packToF64UI((softfloat_getRoundingMode(status) == softfloat_round_min), 0, 0);
    }
    return uiZ;
}

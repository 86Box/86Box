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

#include <stdint.h>
#include "internals.h"
#include "primitives.h"
#include "specialize.h"
#include "softfloat.h"

extFloat80_t
 softfloat_subMagsExtF80(uint16_t uiA64, uint64_t uiA0, uint16_t uiB64, uint64_t uiB0, bool signZ, struct softfloat_status_t *status)
{
    int32_t expA;
    uint64_t sigA;
    int32_t expB;
    uint64_t sigB;
    int32_t expDiff;
    int32_t expZ;
    uint64_t sigExtra;
    struct uint128 sig128;
    struct exp32_sig64 normExpSig;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expA = expExtF80UI64(uiA64);
    sigA = uiA0;
    expB = expExtF80UI64(uiB64);
    sigB = uiB0;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expA == 0x7FFF) {
        if ((sigA<<1)) goto propagateNaN;
        if (expB == 0x7FFF) {
            if ((sigB<<1)) goto propagateNaN;
            softfloat_raiseFlags(status, softfloat_flag_invalid);
            return packToExtF80_twoargs(defaultNaNExtF80UI64, defaultNaNExtF80UI0);
        }
        if (sigB && ! expB)
            softfloat_raiseFlags(status, softfloat_flag_denormal);
        return packToExtF80_twoargs(uiA64, uiA0);
    }
    if (expB == 0x7FFF) {
        if ((sigB<<1)) goto propagateNaN;
        if (sigA && ! expA)
            softfloat_raiseFlags(status, softfloat_flag_denormal);
        return packToExtF80(signZ ^ 1, 0x7FFF, UINT64_C(0x8000000000000000));
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (! expA) {
        if (! sigA) {
            if (! expB) {
                if (sigB) {
                    softfloat_raiseFlags(status, softfloat_flag_denormal);
                    normExpSig = softfloat_normSubnormalExtF80Sig(sigB);
                    expB = normExpSig.exp + 1;
                    sigB = normExpSig.sig;
                    return softfloat_roundPackToExtF80(signZ ^ 1, expB, sigB, 0, softfloat_extF80_roundingPrecision(status), status);
                }
                return packToExtF80((softfloat_getRoundingMode(status) == softfloat_round_min), 0, 0);
            }
            return softfloat_roundPackToExtF80(signZ ^ 1, expB, sigB, 0, softfloat_extF80_roundingPrecision(status), status);
        }
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalExtF80Sig(sigA);
        expA = normExpSig.exp + 1;
        sigA = normExpSig.sig;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (! expB) {
        if (! sigB)
            return softfloat_roundPackToExtF80(signZ, expA, sigA, 0, softfloat_extF80_roundingPrecision(status), status);

        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalExtF80Sig(sigB);
        expB = normExpSig.exp + 1;
        sigB = normExpSig.sig;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expDiff = expA - expB;
    if (0 < expDiff) goto expABigger;
    if (expDiff < 0) goto expBBigger;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expZ = expA;
    sigExtra = 0;
    if (sigB < sigA) goto aBigger;
    if (sigA < sigB) goto bBigger;
    return packToExtF80((softfloat_getRoundingMode(status) == softfloat_round_min), 0, 0);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 expBBigger:
    sig128 = softfloat_shiftRightJam128(sigA, 0, -expDiff);
    sigA = sig128.v64;
    sigExtra = sig128.v0;
    expZ = expB;
 bBigger:
    signZ = ! signZ;
    sig128 = softfloat_sub128(sigB, 0, sigA, sigExtra);
    goto normRoundPack;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 expABigger:
    sig128 = softfloat_shiftRightJam128(sigB, 0, expDiff);
    sigB = sig128.v64;
    sigExtra = sig128.v0;
    expZ = expA;
 aBigger:
    sig128 = softfloat_sub128(sigA, 0, sigB, sigExtra);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 normRoundPack:
    return
        softfloat_normRoundPackToExtF80(
            signZ, expZ, sig128.v64, sig128.v0, softfloat_extF80_roundingPrecision(status), status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 propagateNaN:
    return softfloat_propagateNaNExtF80UI(uiA64, uiA0, uiB64, uiB0, status);
}

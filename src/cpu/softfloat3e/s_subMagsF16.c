/*============================================================================

This C source file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014, 2015, 2016, 2017 The Regents of the
University of California.  All rights reserved.

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

float16 softfloat_subMagsF16(uint16_t uiA, uint16_t uiB, struct softfloat_status_t *status)
{
    int8_t expA;
    uint16_t sigA;
    int8_t expB;
    uint16_t sigB;
    int8_t expDiff;
    uint16_t uiZ;
    int16_t sigDiff;
    bool signZ;
    int8_t shiftDist, expZ;
    uint16_t sigZ, sigX, sigY;
    uint32_t sig32Z;
    int8_t roundingMode;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expA = expF16UI(uiA);
    sigA = fracF16UI(uiA);
    expB = expF16UI(uiB);
    sigB = fracF16UI(uiB);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!expA) sigA = 0;
        if (!expB) sigB = 0;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expDiff = expA - expB;
    if (! expDiff) {
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        if (expA == 0x1F) {
            if (sigA | sigB) goto propagateNaN;
            softfloat_raiseFlags(status, softfloat_flag_invalid);
            return defaultNaNF16UI;
        }
        if (!expA && (sigA | sigB)) softfloat_raiseFlags(status, softfloat_flag_denormal);
        sigDiff = sigA - sigB;
        if (! sigDiff) {
            return packToF16UI((softfloat_getRoundingMode(status) == softfloat_round_min), 0, 0);
        }
        if (expA) --expA;
        signZ = signF16UI(uiA);
        if (sigDiff < 0) {
            signZ = ! signZ;
            sigDiff = -sigDiff;
        }
        shiftDist = softfloat_countLeadingZeros16(sigDiff) - 5;
        expZ = expA - shiftDist;
        if (expZ < 0) {
            shiftDist = expA;
            expZ = 0;
        }
        sigZ = sigDiff<<shiftDist;
        if (!expZ && sigDiff) {
            if (softfloat_flushUnderflowToZero(status)) {
                softfloat_raiseFlags(status, softfloat_flag_underflow | softfloat_flag_inexact);
                return packToF16UI(signZ, 0, 0);
            }
            if (! softfloat_isMaskedException(status, softfloat_flag_underflow)) {
                softfloat_raiseFlags(status, softfloat_flag_underflow);
            }
        }
        goto pack;
    } else {
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        signZ = signF16UI(uiA);
        if (expDiff < 0) {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            signZ = ! signZ;
            if (expB == 0x1F) {
                if (sigB) goto propagateNaN;
                if (sigA && !expA) softfloat_raiseFlags(status, softfloat_flag_denormal);
                return packToF16UI(signZ, 0x1F, 0);
            }

            if ((sigA && !expA) || (sigB && !expB))
                softfloat_raiseFlags(status, softfloat_flag_denormal);

            if (expDiff <= -13) {
                uiZ = packToF16UI(signZ, expB, sigB);
                if (expA | sigA) goto subEpsilon;
                return uiZ;
            }
            expZ = expA + 19;
            sigX = sigB | 0x0400;
            sigY = sigA + (expA ? 0x0400 : sigA);
            expDiff = -expDiff;
        } else {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            uiZ = uiA;
            if (expA == 0x1F) {
                if (sigA) goto propagateNaN;
                if (sigB && !expB) softfloat_raiseFlags(status, softfloat_flag_denormal);
                return uiZ;
            }

            if ((sigA && !expA) || (sigB && !expB))
                softfloat_raiseFlags(status, softfloat_flag_denormal);

            if (13 <= expDiff) {
                if (expB | sigB) goto subEpsilon;
                return uiZ;
            }
            expZ = expB + 19;
            sigX = sigA | 0x0400;
            sigY = sigB + (expB ? 0x0400 : sigB);
        }
        sig32Z = ((uint32_t) sigX<<expDiff) - sigY;
        shiftDist = softfloat_countLeadingZeros32(sig32Z) - 1;
        sig32Z <<= shiftDist;
        expZ -= shiftDist;
        sigZ = sig32Z>>16;
        if (sig32Z & 0xFFFF) {
            sigZ |= 1;
        } else {
            if (! (sigZ & 0xF) && ((unsigned int) expZ < 0x1E)) {
                sigZ >>= 4;
                goto pack;
            }
        }
        return softfloat_roundPackToF16(signZ, expZ, sigZ, status);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 propagateNaN:
    return softfloat_propagateNaNF16UI(uiA, uiB, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 subEpsilon:
    roundingMode = softfloat_getRoundingMode(status);
    if (roundingMode != softfloat_round_near_even) {
        if ((roundingMode == softfloat_round_minMag)
                || (roundingMode == (signF16UI(uiZ) ? softfloat_round_max : softfloat_round_min))) {
            --uiZ;
        }
    }
    softfloat_raiseFlags(status, softfloat_flag_inexact);
    return uiZ;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 pack:
    return packToF16UI(signZ, expZ, sigZ);
}

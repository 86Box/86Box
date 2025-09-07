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

#include <stdint.h>
#include "internals.h"
#include "primitives.h"
#include "specialize.h"
#include "softfloat.h"

float64 softfloat_subMagsF64(uint64_t uiA, uint64_t uiB, bool signZ, struct softfloat_status_t *status)
{
    int16_t expA;
    uint64_t sigA;
    int16_t expB;
    uint64_t sigB;
    int16_t expDiff;
    int64_t sigDiff;
    int8_t shiftDist;
    int16_t expZ;
    uint64_t sigZ;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expA = expF64UI(uiA);
    sigA = fracF64UI(uiA);
    expB = expF64UI(uiB);
    sigB = fracF64UI(uiB);
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
        if (expA == 0x7FF) {
            if (sigA | sigB) goto propagateNaN;
            softfloat_raiseFlags(status, softfloat_flag_invalid);
            return defaultNaNF64UI;
        }
        if (!expA && (sigA | sigB)) softfloat_raiseFlags(status, softfloat_flag_denormal);
        sigDiff = sigA - sigB;
        if (! sigDiff) {
            return packToF64UI((softfloat_getRoundingMode(status) == softfloat_round_min), 0, 0);
        }
        if (expA) --expA;
        if (sigDiff < 0) {
            signZ = ! signZ;
            sigDiff = -sigDiff;
        }
        shiftDist = softfloat_countLeadingZeros64(sigDiff) - 11;
        expZ = expA - shiftDist;
        if (expZ < 0) {
            shiftDist = expA;
            expZ = 0;
        }
        if (!expZ && sigDiff) {
            if (softfloat_flushUnderflowToZero(status)) {
                softfloat_raiseFlags(status, softfloat_flag_underflow | softfloat_flag_inexact);
                return packToF64UI(signZ, 0, 0);
            }
            if (! softfloat_isMaskedException(status, softfloat_flag_underflow)) {
                softfloat_raiseFlags(status, softfloat_flag_underflow);
            }
        }
        return packToF64UI(signZ, expZ, sigDiff<<shiftDist);
    } else {
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        sigA <<= 10;
        sigB <<= 10;
        if (expDiff < 0) {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            signZ = ! signZ;
            if (expB == 0x7FF) {
                if (sigB) goto propagateNaN;
                if (sigA && !expA) softfloat_raiseFlags(status, softfloat_flag_denormal);
                return packToF64UI(signZ, 0x7FF, 0);
            }

            if ((sigA && !expA) || (sigB && !expB))
                softfloat_raiseFlags(status, softfloat_flag_denormal);

            sigA += expA ? UINT64_C(0x4000000000000000) : sigA;
            sigA = softfloat_shiftRightJam64(sigA, -expDiff);
            sigB |= UINT64_C(0x4000000000000000);
            expZ = expB;
            sigZ = sigB - sigA;
        } else {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            if (expA == 0x7FF) {
                if (sigA) goto propagateNaN;
                if (sigB && !expB) softfloat_raiseFlags(status, softfloat_flag_denormal);
                return uiA;
            }

            if ((sigA && !expA) || (sigB && !expB))
                softfloat_raiseFlags(status, softfloat_flag_denormal);

            sigB += expB ? UINT64_C(0x4000000000000000) : sigB;
            sigB = softfloat_shiftRightJam64(sigB, expDiff);
            sigA |= UINT64_C(0x4000000000000000);
            expZ = expA;
            sigZ = sigA - sigB;
        }
        return softfloat_normRoundPackToF64(signZ, expZ - 1, sigZ, status);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 propagateNaN:
    return softfloat_propagateNaNF64UI(uiA, uiB, status);
}

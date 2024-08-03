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

#include <stdbool.h>
#include <stdint.h>
#include "internals.h"
#include "specialize.h"
#include "softfloat.h"

float16 softfloat_addMagsF16(uint16_t uiA, uint16_t uiB, struct softfloat_status_t *status)
{
    int8_t expA;
    uint16_t sigA;
    int8_t expB;
    uint16_t sigB;
    int8_t expDiff;
    uint16_t uiZ;
    bool signZ;
    int8_t expZ;
    uint16_t sigZ;
    uint16_t sigX, sigY;
    int8_t shiftDist;
    uint32_t sig32Z;
    int8_t roundingMode;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expA = expF16UI(uiA);
    sigA = fracF16UI(uiA);
    expB = expF16UI(uiB);
    sigB = fracF16UI(uiB);
    signZ = signF16UI(uiA);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!expA) {
            sigA = 0;
            uiA = packToF16UI(signZ, 0, 0);
        }
        if (!expB) sigB = 0;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expDiff = expA - expB;
    if (! expDiff) {
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        if (! expA) {
            uiZ = uiA + sigB;
            if (sigA | sigB) {
                softfloat_raiseFlags(status, softfloat_flag_denormal);
                bool isTiny = (expF16UI(uiZ) == 0);
                if (isTiny) {
                    if (softfloat_flushUnderflowToZero(status)) {
                        softfloat_raiseFlags(status, softfloat_flag_underflow | softfloat_flag_inexact);
                        return packToF16UI(signZ, 0, 0);
                    }
                    if (! softfloat_isMaskedException(status, softfloat_flag_underflow)) {
                        softfloat_raiseFlags(status, softfloat_flag_underflow);
                    }
                }
            }
            return uiZ;
        }
        if (expA == 0x1F) {
            if (sigA | sigB) goto propagateNaN;
            return uiA;
        }
        expZ = expA;
        sigZ = 0x0800 + sigA + sigB;
        if (! (sigZ & 1) && (expZ < 0x1E)) {
            sigZ >>= 1;
            goto pack;
        }
        sigZ <<= 3;
    } else {
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        if (expDiff < 0) {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            if (expB == 0x1F) {
                if (sigB) goto propagateNaN;
                if (sigA && !expA) softfloat_raiseFlags(status, softfloat_flag_denormal);
                return packToF16UI(signZ, 0x1F, 0);
            }

            if ((!expA && sigA) || (!expB && sigB))
                softfloat_raiseFlags(status, softfloat_flag_denormal);

            if (expDiff <= -13) {
                uiZ = packToF16UI(signZ, expB, sigB);
                if (expA | sigA) goto addEpsilon;
                return uiZ;
            }
            expZ = expB;
            sigX = sigB | 0x0400;
            sigY = sigA + (expA ? 0x0400 : sigA);
            shiftDist = 19 + expDiff;
        } else {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            uiZ = uiA;
            if (expA == 0x1F) {
                if (sigA) goto propagateNaN;
                if (sigB && !expB) softfloat_raiseFlags(status, softfloat_flag_denormal);
                return uiZ;
            }

            if ((!expA && sigA) || (!expB && sigB))
                softfloat_raiseFlags(status, softfloat_flag_denormal);

            if (13 <= expDiff) {
                if (expB | sigB) goto addEpsilon;
                return uiZ;
            }
            expZ = expA;
            sigX = sigA | 0x0400;
            sigY = sigB + (expB ? 0x0400 : sigB);
            shiftDist = 19 - expDiff;
        }
        sig32Z = ((uint32_t) sigX<<19) + ((uint32_t) sigY<<shiftDist);
        if (sig32Z < 0x40000000) {
            --expZ;
            sig32Z <<= 1;
        }
        sigZ = sig32Z>>16;
        if (sig32Z & 0xFFFF) {
            sigZ |= 1;
        } else {
            if (! (sigZ & 0xF) && (expZ < 0x1E)) {
                sigZ >>= 4;
                goto pack;
            }
        }
    }
    return softfloat_roundPackToF16(signZ, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 propagateNaN:
    return softfloat_propagateNaNF16UI(uiA, uiB, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 addEpsilon:
    roundingMode = softfloat_getRoundingMode(status);
    if (roundingMode != softfloat_round_near_even) {
        if (roundingMode == (signF16UI(uiZ) ? softfloat_round_min : softfloat_round_max)) {
            ++uiZ;
            if ((uint16_t) (uiZ<<1) == 0xF800) {
                softfloat_raiseFlags(status, softfloat_flag_overflow | softfloat_flag_inexact);
            }
        }
    }
    softfloat_raiseFlags(status, softfloat_flag_inexact);
    return uiZ;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 pack:
    return packToF16UI(signZ, expZ, sigZ);
}

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
#include "primitives.h"
#include "specialize.h"

float32 softfloat_addMagsF32(uint32_t uiA, uint32_t uiB, struct softfloat_status_t *status)
{
    int16_t expA;
    uint32_t sigA;
    int16_t expB;
    uint32_t sigB;
    int16_t expDiff;
    uint32_t uiZ;
    bool signZ;
    int16_t expZ;
    uint32_t sigZ;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    expA = expF32UI(uiA);
    sigA = fracF32UI(uiA);
    expB = expF32UI(uiB);
    sigB = fracF32UI(uiB);
    signZ = signF32UI(uiA);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (softfloat_denormalsAreZeros(status)) {
        if (!expA) {
            sigA = 0;
            uiA = packToF32UI(signZ, 0, 0);
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
                bool isTiny = (expF32UI(uiZ) == 0);
                if (isTiny) {
                    if (softfloat_flushUnderflowToZero(status)) {
                        softfloat_raiseFlags(status, softfloat_flag_underflow | softfloat_flag_inexact);
                        return packToF32UI(signZ, 0, 0);
                    }
                    if (! softfloat_isMaskedException(status, softfloat_flag_underflow)) {
                        softfloat_raiseFlags(status, softfloat_flag_underflow);
                    }
                }
            }
            return uiZ;
        }
        if (expA == 0xFF) {
            if (sigA | sigB) goto propagateNaN;
            return uiA;
        }
        expZ = expA;
        sigZ = 0x01000000 + sigA + sigB;
        if (! (sigZ & 1) && (expZ < 0xFE)) {
            return packToF32UI(signZ, expZ, sigZ>>1);
        }
        sigZ <<= 6;
    } else {
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        signZ = signF32UI(uiA);
        sigA <<= 6;
        sigB <<= 6;
        if (expDiff < 0) {
            if (expB == 0xFF) {
                if (sigB) goto propagateNaN;
                if (sigA && !expA) softfloat_raiseFlags(status, softfloat_flag_denormal);
                return packToF32UI(signZ, 0xFF, 0);
            }

            if ((!expA && sigA) || (!expB && sigB))
                softfloat_raiseFlags(status, softfloat_flag_denormal);

            expZ = expB;
            sigA += expA ? 0x20000000 : sigA;
            sigA = softfloat_shiftRightJam32(sigA, -expDiff);
        } else {
            if (expA == 0xFF) {
                if (sigA) goto propagateNaN;
                if (sigB && !expB) softfloat_raiseFlags(status, softfloat_flag_denormal);
                return uiA;
            }

            if ((!expA && sigA) || (!expB && sigB))
                softfloat_raiseFlags(status, softfloat_flag_denormal);

            expZ = expA;
            sigB += expB ? 0x20000000 : sigB;
            sigB = softfloat_shiftRightJam32(sigB, expDiff);
        }
        sigZ = 0x20000000 + sigA + sigB;
        if (sigZ < 0x40000000) {
            --expZ;
            sigZ <<= 1;
        }
    }
    return softfloat_roundPackToF32(signZ, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 propagateNaN:
    return softfloat_propagateNaNF32UI(uiA, uiB, status);
}

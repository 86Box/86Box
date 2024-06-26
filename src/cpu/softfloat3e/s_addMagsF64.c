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

float64 softfloat_addMagsF64(uint64_t uiA, uint64_t uiB, bool signZ, struct softfloat_status_t *status)
{
    int16_t expA;
    uint64_t sigA;
    int16_t expB;
    uint64_t sigB;
    int16_t expDiff;
    uint64_t uiZ;
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
        if (!expA) {
            sigA = 0;
            uiA = packToF64UI(signZ, 0, 0);
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
                bool isTiny = (expF64UI(uiZ) == 0);
                if (isTiny) {
                    if (softfloat_flushUnderflowToZero(status)) {
                        softfloat_raiseFlags(status, softfloat_flag_underflow | softfloat_flag_inexact);
                        return packToF64UI(signZ, 0, 0);
                    }
                    if (! softfloat_isMaskedException(status, softfloat_flag_underflow)) {
                        softfloat_raiseFlags(status, softfloat_flag_underflow);
                    }
                }
            }
            return uiZ;
        }
        if (expA == 0x7FF) {
            if (sigA | sigB) goto propagateNaN;
            return uiA;
        }
        expZ = expA;
        sigZ = UINT64_C(0x0020000000000000) + sigA + sigB;
        sigZ <<= 9;
    } else {
        /*--------------------------------------------------------------------
        *--------------------------------------------------------------------*/
        sigA <<= 9;
        sigB <<= 9;
        if (expDiff < 0) {
            if (expB == 0x7FF) {
                if (sigB) goto propagateNaN;
                if (sigA && !expA) softfloat_raiseFlags(status, softfloat_flag_denormal);
                return packToF64UI(signZ, 0x7FF, 0);
            }

            if ((!expA && sigA) || (!expB && sigB))
                softfloat_raiseFlags(status, softfloat_flag_denormal);

            expZ = expB;
            if (expA) {
                sigA += UINT64_C(0x2000000000000000);
            } else {
                sigA <<= 1;
            }
            sigA = softfloat_shiftRightJam64(sigA, -expDiff);
        } else {
            if (expA == 0x7FF) {
                if (sigA) goto propagateNaN;
                if (sigB && !expB) softfloat_raiseFlags(status, softfloat_flag_denormal);
                return uiA;
            }

            if ((!expA && sigA) || (!expB && sigB))
                softfloat_raiseFlags(status, softfloat_flag_denormal);

            expZ = expA;
            if (expB) {
                sigB += UINT64_C(0x2000000000000000);
            } else {
                sigB <<= 1;
            }
            sigB = softfloat_shiftRightJam64(sigB, expDiff);
        }
        sigZ = UINT64_C(0x2000000000000000) + sigA + sigB;
        if (sigZ < UINT64_C(0x4000000000000000)) {
            --expZ;
            sigZ <<= 1;
        }
    }
    return softfloat_roundPackToF64(signZ, expZ, sigZ, status);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 propagateNaN:
    return softfloat_propagateNaNF64UI(uiA, uiB, status);
}

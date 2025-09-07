/*============================================================================

This C source file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014, 2015, 2017 The Regents of the University of
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
#include "softfloat.h"

float32 softfloat_roundPackToF32(bool sign, int16_t exp, uint32_t sig, struct softfloat_status_t *status)
{
    uint8_t roundingMode;
    bool roundNearEven;
    uint8_t roundIncrement, roundBits;
    bool isTiny;
    uint32_t uiZ;
    uint32_t sigRef;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    roundingMode = softfloat_getRoundingMode(status);
    roundNearEven = (roundingMode == softfloat_round_near_even);
    roundIncrement = 0x40;
    if (! roundNearEven && (roundingMode != softfloat_round_near_maxMag)) {
        roundIncrement =
            (roundingMode == (sign ? softfloat_round_min : softfloat_round_max)) ? 0x7F : 0;
    }
    roundBits = sig & 0x7F;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (0xFD <= (unsigned int) exp) {
        if (exp < 0) {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            isTiny = (exp < -1) || (sig + roundIncrement < 0x80000000);
            if (isTiny && ! softfloat_isMaskedException(status, softfloat_flag_underflow)) {
                softfloat_raiseFlags(status, softfloat_flag_underflow);
                exp += 192;
            }
            else {
                sig = softfloat_shiftRightJam32(sig, -exp);
                exp = 0;
                roundBits = sig & 0x7F;
                if (isTiny) {
                    if (softfloat_flushUnderflowToZero(status)) {
                        softfloat_raiseFlags(status, softfloat_flag_underflow | softfloat_flag_inexact);
                        return packToF32UI(sign, 0, 0);
                    }
                    if (roundBits) softfloat_raiseFlags(status, softfloat_flag_underflow);
                }
            }
        } else if ((0xFD < exp) || (0x80000000 <= sig + roundIncrement)) {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            softfloat_raiseFlags(status, softfloat_flag_overflow);
            if (roundBits || softfloat_isMaskedException(status, softfloat_flag_overflow)) {
                softfloat_raiseFlags(status, softfloat_flag_inexact);
                if (roundIncrement != 0) softfloat_setRoundingUp(status);
            }
            uiZ = packToF32UI(sign, 0xFF, 0) - ! roundIncrement;
            return uiZ;
        }
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    sigRef = sig;
    sig = (sig + roundIncrement)>>7;
    sig &= ~(uint32_t) (! (roundBits ^ 0x40) & roundNearEven);
    if (! sig) exp = 0;
    if (roundBits) {
        softfloat_raiseFlags(status, softfloat_flag_inexact);
        if ((sig << 7) > sigRef) softfloat_setRoundingUp(status);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    return packToF32UI(sign, exp, sig);
}

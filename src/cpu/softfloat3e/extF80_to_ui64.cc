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

uint64_t extF80_to_ui64(extFloat80_t a, uint8_t roundingMode, bool exact, struct softfloat_status_t *status)
{
    uint16_t uiA64;
    bool sign;
    int32_t exp;
    uint64_t sig;
    int32_t shiftDist;
    uint64_t sigExtra;
    struct uint64_extra sig64Extra;

    // handle unsupported extended double-precision floating encodings
    if (extF80_isUnsupported(a)) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        return ui64_fromNaN;
    }

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    uiA64 = a.signExp;
    sign = signExtF80UI64(uiA64);
    exp  = expExtF80UI64(uiA64);
    sig = a.signif;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    shiftDist = 0x403E - exp;
    if (shiftDist < 0) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        return
            (exp == 0x7FFF) && (sig & UINT64_C(0x7FFFFFFFFFFFFFFF))
                ? ui64_fromNaN
                : sign ? ui64_fromNegOverflow : ui64_fromPosOverflow;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    sigExtra = 0;
    if (shiftDist) {
        sig64Extra = softfloat_shiftRightJam64Extra(sig, 0, shiftDist);
        sig = sig64Extra.v;
        sigExtra = sig64Extra.extra;
    }
    return softfloat_roundToUI64(sign, sig, sigExtra, roundingMode, exact, status);
}

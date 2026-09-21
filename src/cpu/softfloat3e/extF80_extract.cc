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
#include "specialize.h"
#include "softfloat.h"

/*----------------------------------------------------------------------------
| Separate the source extended double-precision floating point value `a'
| into its exponent and significand, store the significant back to the
| 'a' and return the exponent. The operation performed is a superset of
| the IEC/IEEE recommended logb(x) function.
*----------------------------------------------------------------------------*/

extFloat80_t extF80_extract(extFloat80_t *a, struct softfloat_status_t *status)
{
    uint16_t uiA64;
    uint64_t uiA0;
    bool signA;
    int32_t expA;
    uint64_t sigA;
    struct exp32_sig64 normExpSig;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    // handle unsupported extended double-precision floating encodings
    if (extF80_isUnsupported(*a)) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        *a = packToExtF80_twoargs(defaultNaNExtF80UI64, defaultNaNExtF80UI0);
        return *a;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    uiA64 = a->signExp;
    uiA0  = a->signif;
    signA = signExtF80UI64(uiA64);
    expA  = expExtF80UI64(uiA64);
    sigA  = uiA0;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (expA == 0x7FFF) {
        if (sigA<<1) {
            *a = softfloat_propagateNaNExtF80UI(uiA64, uiA0, 0, 0, status);
            return *a;
        }
        return packToExtF80(0, 0x7FFF, BX_CONST64(0x8000000000000000));
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (! expA) {
        if (! sigA) {
            softfloat_raiseFlags(status, softfloat_flag_divbyzero);
            *a = packToExtF80(signA, 0, 0);
            return packToExtF80(1, 0x7FFF, BX_CONST64(0x8000000000000000));
        }
        softfloat_raiseFlags(status, softfloat_flag_denormal);
        normExpSig = softfloat_normSubnormalExtF80Sig(sigA);
        expA = normExpSig.exp + 1;
        sigA = normExpSig.sig;
    }

    *a = packToExtF80(signA, 0x3FFF, sigA);
    return i32_to_extF80(expA - 0x3FFF);
}

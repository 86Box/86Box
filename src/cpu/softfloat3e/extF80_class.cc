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
#include "softfloat.h"

softfloat_class_t extF80_class(extFloat80_t a)
{
    uint16_t uiA64;
    uint64_t uiA0;
    bool signA;
    int32_t expA;
    uint64_t sigA;

    uiA64 = a.signExp;
    uiA0  = a.signif;
    signA = signExtF80UI64(uiA64);
    expA  = expExtF80UI64(uiA64);
    sigA  = uiA0;

    if (! expA) {
        if (! sigA) return softfloat_zero;
        return softfloat_denormal; /* denormal or pseudo-denormal */
    }

    /* valid numbers have the MS bit set */
    if (!(sigA & UINT64_C(0x8000000000000000)))
        return softfloat_SNaN; /* report unsupported as SNaNs */

    if (expA == 0x7FFF) {
        if ((sigA<<1) == 0)
            return (signA) ? softfloat_negative_inf : softfloat_positive_inf;

        return (sigA & UINT64_C(0x4000000000000000)) ? softfloat_QNaN : softfloat_SNaN;
    }

    return softfloat_normalized;
}

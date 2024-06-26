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

#include <stdio.h>
#include <stdint.h>
#include <86box/86box.h>
#include "../cpu.h"

#include "internals.h"
#include "softfloat.h"

/*----------------------------------------------------------------------------
| Compare  between  two extended precision  floating  point  numbers. Returns
| 'float_relation_equal'  if the operands are equal, 'float_relation_less' if
| the    value    'a'   is   less   than   the   corresponding   value   `b',
| 'float_relation_greater' if the value 'a' is greater than the corresponding
| value `b', or 'float_relation_unordered' otherwise.
*----------------------------------------------------------------------------*/

int extF80_compare(extFloat80_t a, extFloat80_t b, int quiet, struct softfloat_status_t *status)
{
    uint16_t uiA64;
    uint64_t uiA0;
    bool signA;
    int32_t expA;
    uint64_t sigA;

    uint16_t uiB64;
    uint64_t uiB0;
    bool signB;
    int32_t expB;
    uint64_t sigB;

    struct exp32_sig64 normExpSig;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    softfloat_class_t aClass = extF80_class(a);
    softfloat_class_t bClass = extF80_class(b);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (fpu_type < FPU_287XL) {
        if ((aClass == softfloat_positive_inf) && (bClass == softfloat_negative_inf))
        {
            return softfloat_relation_equal;
        }

        if ((aClass == softfloat_negative_inf) && (bClass == softfloat_positive_inf))
        {
            return softfloat_relation_equal;
        }
    }

    if (aClass == softfloat_SNaN || bClass == softfloat_SNaN)
    {
        /* unsupported reported as SNaN */
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        return softfloat_relation_unordered;
    }

    if (aClass == softfloat_QNaN || bClass == softfloat_QNaN) {
        if (! quiet) softfloat_raiseFlags(status, softfloat_flag_invalid);
        return softfloat_relation_unordered;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (aClass == softfloat_denormal || bClass == softfloat_denormal) {
        softfloat_raiseFlags(status, softfloat_flag_denormal);
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    uiA64 = a.signExp;
    uiA0  = a.signif;
    signA = signExtF80UI64(uiA64);
    expA  = expExtF80UI64(uiA64);
    sigA  = uiA0;

    uiB64 = b.signExp;
    uiB0  = b.signif;
    signB = signExtF80UI64(uiB64);
    expB  = expExtF80UI64(uiB64);
    sigB  = uiB0;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (aClass == softfloat_zero) {
        if (bClass == softfloat_zero) return softfloat_relation_equal;
        return signB ? softfloat_relation_greater : softfloat_relation_less;
    }

    if (bClass == softfloat_zero || signA != signB) {
        return signA ? softfloat_relation_less : softfloat_relation_greater;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (aClass == softfloat_denormal) {
        normExpSig = softfloat_normSubnormalExtF80Sig(sigA);
        expA += normExpSig.exp + 1;
        sigA = normExpSig.sig;
    }
    if (bClass == softfloat_denormal) {
        normExpSig = softfloat_normSubnormalExtF80Sig(sigB);
        expB += normExpSig.exp + 1;
        sigB = normExpSig.sig;
    }

    if (expA == expB && sigA == sigB)
        return softfloat_relation_equal;

    int less_than =
        signA ? ((expB < expA) || ((expB == expA) && (sigB < sigA)))
              : ((expA < expB) || ((expA == expB) && (sigA < sigB)));

    if (less_than) return softfloat_relation_less;
    return softfloat_relation_greater;
}

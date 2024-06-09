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
#include "softfloat.h"

/*----------------------------------------------------------------------------
| Compare  between  two  single  precision  floating  point  numbers. Returns
| 'float_relation_equal'  if the operands are equal, 'float_relation_less' if
| the    value    'a'   is   less   than   the   corresponding   value   `b',
| 'float_relation_greater' if the value 'a' is greater than the corresponding
| value `b', or 'float_relation_unordered' otherwise.
*----------------------------------------------------------------------------*/

int f32_compare(float32 a, float32 b, bool quiet, struct softfloat_status_t *status)
{
    softfloat_class_t aClass;
    softfloat_class_t bClass;
    bool signA;
    bool signB;

    aClass = f32_class(a);
    bClass = f32_class(b);

    if (aClass == softfloat_SNaN || bClass == softfloat_SNaN) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        return softfloat_relation_unordered;
    }

    if (aClass == softfloat_QNaN || bClass == softfloat_QNaN) {
        if (! quiet) softfloat_raiseFlags(status, softfloat_flag_invalid);
        return softfloat_relation_unordered;
    }

    if (aClass == softfloat_denormal) {
        if (softfloat_denormalsAreZeros(status))
            a = a & 0x80000000;
        else
            softfloat_raiseFlags(status, softfloat_flag_denormal);
    }

    if (bClass == softfloat_denormal) {
        if (softfloat_denormalsAreZeros(status))
            b = b & 0x80000000;
        else
            softfloat_raiseFlags(status, softfloat_flag_denormal);
    }

    if ((a == b) || ((uint32_t) ((a | b)<<1) == 0)) return softfloat_relation_equal;

    signA = signF32UI(a);
    signB = signF32UI(b);
    if (signA != signB)
        return (signA) ? softfloat_relation_less : softfloat_relation_greater;

    if (signA ^ (a < b)) return softfloat_relation_less;
    return softfloat_relation_greater;
}

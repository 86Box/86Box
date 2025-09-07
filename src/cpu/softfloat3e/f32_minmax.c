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
| Compare between two single precision floating point numbers and return the
| smaller of them.
*----------------------------------------------------------------------------*/

float32 f32_min(float32 a, float32 b, struct softfloat_status_t *status)
{
    if (softfloat_denormalsAreZeros(status)) {
        a = f32_denormal_to_zero(a);
        b = f32_denormal_to_zero(b);
    }

    return (f32_compare_normal(a, b, status) == softfloat_relation_less) ? a : b;
}

/*----------------------------------------------------------------------------
| Compare between two single precision floating point numbers and return the
| larger of them.
*----------------------------------------------------------------------------*/

float32 f32_max(float32 a, float32 b, struct softfloat_status_t *status)
{
    if (softfloat_denormalsAreZeros(status)) {
        a = f32_denormal_to_zero(a);
        b = f32_denormal_to_zero(b);
    }

    return (f32_compare_normal(a, b, status) == softfloat_relation_greater) ? a : b;
}

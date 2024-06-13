/*============================================================================

This C source file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014 The Regents of the University of California.
All rights reserved.

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

extern float128_t
 softfloat_addMagsF128(uint64_t, uint64_t, uint64_t, uint64_t, bool, struct softfloat_status_t *);
extern float128_t
 softfloat_subMagsF128(uint64_t, uint64_t, uint64_t, uint64_t, bool, struct softfloat_status_t *);

float128_t f128_add(float128_t a, float128_t b, struct softfloat_status_t *status)
{
    uint64_t uiA64, uiA0;
    bool signA;
    uint64_t uiB64, uiB0;
    bool signB;

    uiA64 = a.v64;
    uiA0  = a.v0;
    signA = signF128UI64(uiA64);

    uiB64 = b.v64;
    uiB0  = b.v0;
    signB = signF128UI64(uiB64);

    if (signA == signB) {
        return softfloat_addMagsF128(uiA64, uiA0, uiB64, uiB0, signA, status);
    } else {
        return softfloat_subMagsF128(uiA64, uiA0, uiB64, uiB0, signA, status);
    }
}

float128_t f128_sub(float128_t a, float128_t b, struct softfloat_status_t *status)
{
    uint64_t uiA64, uiA0;
    bool signA;
    uint64_t uiB64, uiB0;
    bool signB;

    uiA64 = a.v64;
    uiA0  = a.v0;
    signA = signF128UI64(uiA64);

    uiB64 = b.v64;
    uiB0  = b.v0;
    signB = signF128UI64(uiB64);

    if (signA == signB) {
        return softfloat_subMagsF128(uiA64, uiA0, uiB64, uiB0, signA, status);
    } else {
        return softfloat_addMagsF128(uiA64, uiA0, uiB64, uiB0, signA, status);
    }
}

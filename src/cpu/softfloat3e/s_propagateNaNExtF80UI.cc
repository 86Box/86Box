/*============================================================================

This C source file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014, 2018 The Regents of the University of
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
| Interpreting the unsigned integer formed from concatenating 'uiA64' and
| 'uiA0' as an 80-bit extended floating-point value, and likewise interpreting
| the unsigned integer formed from concatenating 'uiB64' and 'uiB0' as another
| 80-bit extended floating-point value, and assuming at least on of these
| floating-point values is a NaN, returns the bit pattern of the combined NaN
| result.  If either original floating-point value is a signaling NaN, the
| invalid exception is raised.
*----------------------------------------------------------------------------*/
extFloat80_t
softfloat_propagateNaNExtF80UI(uint16_t uiA64, uint64_t uiA0, uint16_t uiB64, uint64_t uiB0, struct softfloat_status_t *status)
{
    bool isSigNaNA, isSigNaNB;
    uint64_t uiNonsigA0, uiNonsigB0;
    uint16_t uiMagA64, uiMagB64;
    extFloat80_t z;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    isSigNaNA = softfloat_isSigNaNExtF80UI(uiA64, uiA0);
    isSigNaNB = softfloat_isSigNaNExtF80UI(uiB64, uiB0);
    /*------------------------------------------------------------------------
    | Make NaNs non-signaling.
    *------------------------------------------------------------------------*/
    uiNonsigA0 = uiA0 | UINT64_C(0xC000000000000000);
    uiNonsigB0 = uiB0 | UINT64_C(0xC000000000000000);
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if (isSigNaNA | isSigNaNB) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        if (isSigNaNA) {
            if (isSigNaNB) goto returnLargerMag;
            if (isNaNExtF80UI(uiB64, uiB0)) goto returnB;
            goto returnA;
        } else {
            if (isNaNExtF80UI(uiA64, uiA0)) goto returnA;
            goto returnB;
        }
    }
 returnLargerMag:
    uiMagA64 = uiA64 & 0x7FFF;
    uiMagB64 = uiB64 & 0x7FFF;
    if (uiMagA64 < uiMagB64) goto returnB;
    if (uiMagB64 < uiMagA64) goto returnA;
    if (uiA0 < uiB0) goto returnB;
    if (uiB0 < uiA0) goto returnA;
    if (uiA64 < uiB64) goto returnA;
 returnB:
    z.signExp = uiB64;
    z.signif  = uiNonsigB0;
    return z;
 returnA:
    z.signExp = uiA64;
    z.signif  = uiNonsigA0;
    return z;
}

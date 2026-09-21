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

#include <stdint.h>
#include "primitives.h"
#include "specialize.h"

/*----------------------------------------------------------------------------
| Converts the common NaN pointed to by `aPtr' into an 80-bit extended
| floating-point NaN, and returns the bit pattern of this value as an unsigned
| integer.
*----------------------------------------------------------------------------*/
struct uint128 softfloat_commonNaNToExtF80UI(const struct commonNaN *aPtr)
{
    struct uint128 uiZ;

    uiZ.v64 = (uint16_t) aPtr->sign<<15 | 0x7FFF;
    uiZ.v0 = UINT64_C(0xC000000000000000) | aPtr->v64>>1;
    return uiZ;
}

/*----------------------------------------------------------------------------
| Assuming the unsigned integer formed from concatenating `uiA64' and `uiA0'
| has the bit pattern of an 80-bit extended floating-point NaN, converts
| this NaN to the common NaN form, and stores the resulting common NaN at the
| location pointed to by `zPtr'.  If the NaN is a signaling NaN, the invalid
| exception is raised.
*----------------------------------------------------------------------------*/
void softfloat_extF80UIToCommonNaN(uint16_t uiA64, uint64_t uiA0, struct commonNaN *zPtr, struct softfloat_status_t *status)
{
    if (softfloat_isSigNaNExtF80UI(uiA64, uiA0)) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
    }
    zPtr->sign = uiA64>>15;
    zPtr->v64  = uiA0<<1;
    zPtr->v0   = 0;
}

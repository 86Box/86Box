/*============================================================================

This C header file is part of the SoftFloat IEEE Floating-Point Arithmetic
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

#include <stdio.h>
#include <stdint.h>
#include <86box/86box.h>
#include "../cpu.h"

#include "softfloat-specialize.h"

const floatx80 Const_QNaN = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
const floatx80 Const_Z    = packFloatx80(0, 0x0000, 0);
const floatx80 Const_1    = packFloatx80(0, 0x3fff, BX_CONST64(0x8000000000000000));
const floatx80 Const_L2T  = packFloatx80(0, 0x4000, BX_CONST64(0xd49a784bcd1b8afe));
const floatx80 Const_L2E  = packFloatx80(0, 0x3fff, BX_CONST64(0xb8aa3b295c17f0bc));
const floatx80 Const_PI   = packFloatx80(0, 0x4000, BX_CONST64(0xc90fdaa22168c235));
const floatx80 Const_LG2  = packFloatx80(0, 0x3ffd, BX_CONST64(0x9a209a84fbcff799));
const floatx80 Const_LN2  = packFloatx80(0, 0x3ffe, BX_CONST64(0xb17217f7d1cf79ac));
const floatx80 Const_INF  = packFloatx80(0, 0x7fff, BX_CONST64(0x8000000000000000));

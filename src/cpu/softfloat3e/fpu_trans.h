/////////////////////////////////////////////////////////////////////////
// $Id$
/////////////////////////////////////////////////////////////////////////
//
//   Copyright (c) 2003-2018 Stanislav Shwartsman
//          Written by Stanislav Shwartsman [sshwarts at sourceforge net]
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
//
/////////////////////////////////////////////////////////////////////////

#ifndef _FPU_TRANS_H_
#define _FPU_TRANS_H_

#include "softfloat.h"
#include "softfloat-specialize.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision operations.
*----------------------------------------------------------------------------*/

int floatx80_remainder(floatx80 a, floatx80 b, floatx80 *r, uint64_t *q, struct softfloat_status_t *status);
int floatx80_ieee754_remainder(floatx80 a, floatx80 b, floatx80 *r, uint64_t *q, struct softfloat_status_t *status);

floatx80 f2xm1(floatx80 a, struct softfloat_status_t *status);
#ifdef __cplusplus
floatx80 fyl2x(floatx80 a, floatx80 b, softfloat_status_t &status);
floatx80 fyl2xp1(floatx80 a, floatx80 b, softfloat_status_t &status);
floatx80 fpatan(floatx80 a, floatx80 b, softfloat_status_t &status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision trigonometric functions.
*----------------------------------------------------------------------------*/

int fsincos(floatx80 a, floatx80 *sin_a, floatx80 *cos_a, softfloat_status_t &status);
int fsin(floatx80 &a, softfloat_status_t &status);
int fcos(floatx80 &a, softfloat_status_t &status);
int ftan(floatx80 &a, softfloat_status_t &status);
#else
floatx80 fyl2x(floatx80 a, floatx80 b, struct softfloat_status_t *status);
floatx80 fyl2xp1(floatx80 a, floatx80 b, struct softfloat_status_t *status);
floatx80 fpatan(floatx80 a, floatx80 b, struct softfloat_status_t *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision trigonometric functions.
*----------------------------------------------------------------------------*/

int fsincos(floatx80 a, floatx80 *sin_a, floatx80 *cos_a, struct softfloat_status_t *status);
int fsin(floatx80 *a, struct softfloat_status_t *status);
int fcos(floatx80 *a, struct softfloat_status_t *status);
int ftan(floatx80 *a, struct softfloat_status_t *status);
#endif

#ifdef __cplusplus
}
#endif

/*-----------------------------------------------------------------------------
| Calculates the absolute value of the extended double-precision floating-point
| value `a'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

#ifdef __cplusplus
static __inline floatx80 &floatx80_abs(floatx80 &reg)
#else
static __inline floatx80 floatx80_abs(floatx80 reg)
#endif
{
    reg.signExp &= 0x7FFF;
    return reg;
}

/*-----------------------------------------------------------------------------
| Changes the sign of the extended double-precision floating-point value 'a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

#ifdef __cplusplus
static __inline floatx80 &floatx80_chs(floatx80 &reg)
#else
static __inline floatx80 floatx80_chs(floatx80 reg)
#endif
{
    reg.signExp ^= 0x8000;
    return reg;
}

#ifdef __cplusplus
static __inline floatx80 FPU_round_const(const floatx80 &a, int adj)
#else
static __inline floatx80 FPU_round_const(const floatx80 a, int adj)
#endif
{
  floatx80 result = a;
  result.signif += adj;
  return result;
}

#endif

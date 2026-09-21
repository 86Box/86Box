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

#ifndef _POLY_H_
#define _POLY_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
float128_t EvenPoly(float128_t x, const float128_t *arr, int n, softfloat_status_t &status);
float128_t OddPoly(float128_t x, const float128_t *arr, int n, softfloat_status_t &status);
#else
float128_t EvenPoly(float128_t x, const float128_t *arr, int n, struct softfloat_status_t *status);
float128_t OddPoly(float128_t x, const float128_t *arr, int n, struct float_status_t *status);
#endif // __cplusplus

#ifdef __cplusplus
}
#endif

#endif // _POLY_H_

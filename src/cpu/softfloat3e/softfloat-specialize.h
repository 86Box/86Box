/*============================================================================
This C source fragment is part of the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://www.cs.berkeley.edu/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

#ifndef _SOFTFLOAT_SPECIALIZE_H_
#define _SOFTFLOAT_SPECIALIZE_H_

#include "config.h"
#include "softfloat.h"
#include "softfloat_types.h"

/*============================================================================
 * Adapted for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

extern const int16_t int16_indefinite;
extern const int32_t int32_indefinite;
extern const int64_t int64_indefinite;

extern const uint16_t uint16_indefinite;
extern const uint32_t uint32_indefinite;
extern const uint64_t uint64_indefinite;

/*----------------------------------------------------------------------------
| Commonly used half-precision floating point constants
*----------------------------------------------------------------------------*/
extern const float16 float16_negative_inf;
extern const float16 float16_positive_inf;
extern const float16 float16_negative_zero;
extern const float16 float16_positive_zero;

/*----------------------------------------------------------------------------
| The pattern for a default generated half-precision NaN.
*----------------------------------------------------------------------------*/
extern const float16 float16_default_nan;

#define FLOAT16_EXP_BIAS 0xF

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
| single-precision floating-point value, returning the result.  After being
| shifted into the proper positions, the three fields are simply added
| together to form the result.  This means that any integer portion of `zSig'
| will be added into the exponent.  Since a properly normalized significand
| will have an integer portion equal to 1, the `zExp' input should be 1 less
| than the desired result exponent whenever `zSig' is a complete, normalized
| significand.
*----------------------------------------------------------------------------*/

static __inline float16 packFloat16(int zSign, int zExp, uint16_t zSig)
{
    return (((uint16_t) zSign)<<15) + (((uint16_t) zExp)<<10) + zSig;
}

/*----------------------------------------------------------------------------
| Commonly used single-precision floating point constants
*----------------------------------------------------------------------------*/
extern const float32 float32_negative_inf;
extern const float32 float32_positive_inf;
extern const float32 float32_negative_zero;
extern const float32 float32_positive_zero;
extern const float32 float32_negative_one;
extern const float32 float32_positive_one;
extern const float32 float32_max_float;
extern const float32 float32_min_float;

/*----------------------------------------------------------------------------
| The pattern for a default generated single-precision NaN.
*----------------------------------------------------------------------------*/
extern const float32 float32_default_nan;

#define FLOAT32_EXP_BIAS 0x7F

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
| single-precision floating-point value, returning the result.  After being
| shifted into the proper positions, the three fields are simply added
| together to form the result.  This means that any integer portion of `zSig'
| will be added into the exponent.  Since a properly normalized significand
| will have an integer portion equal to 1, the `zExp' input should be 1 less
| than the desired result exponent whenever `zSig' is a complete, normalized
| significand.
*----------------------------------------------------------------------------*/

static __inline float32 packFloat32(int zSign, int16_t zExp, uint32_t zSig)
{
    return (((uint32_t) zSign)<<31) + (((uint32_t) zExp)<<23) + zSig;
}

/*----------------------------------------------------------------------------
| Commonly used single-precision floating point constants
*----------------------------------------------------------------------------*/
extern const float64 float64_negative_inf;
extern const float64 float64_positive_inf;
extern const float64 float64_negative_zero;
extern const float64 float64_positive_zero;
extern const float64 float64_negative_one;
extern const float64 float64_positive_one;
extern const float64 float64_max_float;
extern const float64 float64_min_float;

/*----------------------------------------------------------------------------
| The pattern for a default generated double-precision NaN.
*----------------------------------------------------------------------------*/
extern const float64 float64_default_nan;

#define FLOAT64_EXP_BIAS 0x3FF

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
| double-precision floating-point value, returning the result.  After being
| shifted into the proper positions, the three fields are simply added
| together to form the result.  This means that any integer portion of `zSig'
| will be added into the exponent.  Since a properly normalized significand
| will have an integer portion equal to 1, the `zExp' input should be 1 less
| than the desired result exponent whenever `zSig' is a complete, normalized
| significand.
*----------------------------------------------------------------------------*/

static __inline float64 packFloat64(int zSign, int16_t zExp, uint64_t zSig)
{
    return (((uint64_t) zSign)<<63) + (((uint64_t) zExp)<<52) + zSig;
}

/*-----------------------------------------------------------------------------
| Commonly used extended double-precision floating-point constants.
*----------------------------------------------------------------------------*/
extern const floatx80 Const_Z;
extern const floatx80 Const_1;
extern const floatx80 Const_L2T;
extern const floatx80 Const_L2E;
extern const floatx80 Const_PI;
extern const floatx80 Const_LG2;
extern const floatx80 Const_LN2;
extern const floatx80 Const_INF;
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.  The
| `high' and `low' values hold the most- and least-significant bits,
| respectively.
*----------------------------------------------------------------------------*/
#define floatx80_default_nan_exp 0xFFFF
#define floatx80_default_nan_fraction BX_CONST64(0xC000000000000000)

#define FLOATX80_EXP_BIAS 0x3FFF

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into an
| extended double-precision floating-point value, returning the result.
*----------------------------------------------------------------------------*/

static __inline floatx80 packFloatx80(int zSign, int32_t zExp, uint64_t zSig)
{
    floatx80 z;
    z.signif = zSig;
    z.signExp = (zSign << 15) + zExp;
    return z;
}

#ifdef FLOAT128

/*----------------------------------------------------------------------------
| Packs the sign `zSign', the exponent `zExp', and the significand formed
| by the concatenation of `zSig0' and `zSig1' into a quadruple-precision
| floating-point value, returning the result.  After being shifted into the
| proper positions, the three fields `zSign', `zExp', and `zSig0' are simply
| added together to form the most significant 32 bits of the result.  This
| means that any integer portion of `zSig0' will be added into the exponent.
| Since a properly normalized significand will have an integer portion equal
| to 1, the `zExp' input should be 1 less than the desired result exponent
| whenever `zSig0' and `zSig1' concatenated form a complete, normalized
| significand.
*----------------------------------------------------------------------------*/

static __inline float128_t packFloat128(int zSign, int32_t zExp, uint64_t zSig0, uint64_t zSig1)
{
    float128_t z;
    z.v0  = zSig1;
    z.v64 = (((uint64_t) zSign)<<63) + (((uint64_t) zExp)<<48) + zSig0;
    return z;
}

/*----------------------------------------------------------------------------
| Packs two 64-bit precision integers into into the quadruple-precision
| floating-point value, returning the result.
*----------------------------------------------------------------------------*/

static __inline float128_t packFloat128(uint64_t zHi, uint64_t zLo)
{
    float128_t z;
    z.v0  = zLo;
    z.v64 = zHi;
    return z;
}

#define PACK_FLOAT_128(hi,lo) packFloat128(BX_CONST64(hi),BX_CONST64(lo))

#endif
#endif

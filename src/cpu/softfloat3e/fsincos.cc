/*============================================================================
This source file is an extension to the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b, written for Bochs (x86 achitecture simulator)
floating point emulation.

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

/*============================================================================
 * Written for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#define FLOAT128

#define USE_estimateDiv128To64
#include "config.h"
#include "specialize.h"

#include "fpu_trans.h"
#include "softfloat-helpers.h"
#include "fpu_constant.h"
#include "poly.h"


static const floatx80 floatx80_one = packFloatx80(0, 0x3fff, BX_CONST64(0x8000000000000000));

/* reduce trigonometric function argument using 128-bit precision
   M_PI approximation */
static uint64_t argument_reduction_kernel(uint64_t aSig0, int Exp, uint64_t *zSig0, uint64_t *zSig1)
{
    uint64_t term0, term1, term2;
    uint64_t aSig1 = 0;

    shortShift128Left(aSig1, aSig0, Exp, &aSig1, &aSig0);
    uint64_t q = estimateDiv128To64(aSig1, aSig0, FLOAT_PI_HI);
    mul128By64To192(FLOAT_PI_HI, FLOAT_PI_LO, q, &term0, &term1, &term2);
    sub128(aSig1, aSig0, term0, term1, zSig1, zSig0);
    while ((int64_t)(*zSig1) < 0) {
        --q;
        add192(*zSig1, *zSig0, term2, 0, FLOAT_PI_HI, FLOAT_PI_LO, zSig1, zSig0, &term2);
    }
    *zSig1 = term2;
    return q;
}

static int reduce_trig_arg(int expDiff, int &zSign, uint64_t &aSig0, uint64_t &aSig1)
{
    uint64_t term0, term1, q = 0;

    if (expDiff < 0) {
        shortShift128Right(aSig0, 0, 1, &aSig0, &aSig1);
        expDiff = 0;
    }
    if (expDiff > 0) {
        q = argument_reduction_kernel(aSig0, expDiff, &aSig0, &aSig1);
    }
    else {
        if (FLOAT_PI_HI <= aSig0) {
            aSig0 -= FLOAT_PI_HI;
            q = 1;
        }
    }

    shortShift128Right(FLOAT_PI_HI, FLOAT_PI_LO, 1, &term0, &term1);
    if (! softfloat_lt128(aSig0, aSig1, term0, term1))
    {
        int lt = softfloat_lt128(term0, term1, aSig0, aSig1);
        int eq = softfloat_eq128(aSig0, aSig1, term0, term1);

        if ((eq && (q & 1)) || lt) {
            zSign = !zSign;
            ++q;
        }
        if (lt) sub128(FLOAT_PI_HI, FLOAT_PI_LO, aSig0, aSig1, &aSig0, &aSig1);
    }

    return (int)(q & 3);
}

#define SIN_ARR_SIZE 11
#define COS_ARR_SIZE 11

static float128_t sin_arr[SIN_ARR_SIZE] =
{
    PACK_FLOAT_128(0x3fff000000000000, 0x0000000000000000), /*  1 */
    PACK_FLOAT_128(0xbffc555555555555, 0x5555555555555555), /*  3 */
    PACK_FLOAT_128(0x3ff8111111111111, 0x1111111111111111), /*  5 */
    PACK_FLOAT_128(0xbff2a01a01a01a01, 0xa01a01a01a01a01a), /*  7 */
    PACK_FLOAT_128(0x3fec71de3a556c73, 0x38faac1c88e50017), /*  9 */
    PACK_FLOAT_128(0xbfe5ae64567f544e, 0x38fe747e4b837dc7), /* 11 */
    PACK_FLOAT_128(0x3fde6124613a86d0, 0x97ca38331d23af68), /* 13 */
    PACK_FLOAT_128(0xbfd6ae7f3e733b81, 0xf11d8656b0ee8cb0), /* 15 */
    PACK_FLOAT_128(0x3fce952c77030ad4, 0xa6b2605197771b00), /* 17 */
    PACK_FLOAT_128(0xbfc62f49b4681415, 0x724ca1ec3b7b9675), /* 19 */
    PACK_FLOAT_128(0x3fbd71b8ef6dcf57, 0x18bef146fcee6e45)  /* 21 */
};

static float128_t cos_arr[COS_ARR_SIZE] =
{
    PACK_FLOAT_128(0x3fff000000000000, 0x0000000000000000), /*  0 */
    PACK_FLOAT_128(0xbffe000000000000, 0x0000000000000000), /*  2 */
    PACK_FLOAT_128(0x3ffa555555555555, 0x5555555555555555), /*  4 */
    PACK_FLOAT_128(0xbff56c16c16c16c1, 0x6c16c16c16c16c17), /*  6 */
    PACK_FLOAT_128(0x3fefa01a01a01a01, 0xa01a01a01a01a01a), /*  8 */
    PACK_FLOAT_128(0xbfe927e4fb7789f5, 0xc72ef016d3ea6679), /* 10 */
    PACK_FLOAT_128(0x3fe21eed8eff8d89, 0x7b544da987acfe85), /* 12 */
    PACK_FLOAT_128(0xbfda93974a8c07c9, 0xd20badf145dfa3e5), /* 14 */
    PACK_FLOAT_128(0x3fd2ae7f3e733b81, 0xf11d8656b0ee8cb0), /* 16 */
    PACK_FLOAT_128(0xbfca6827863b97d9, 0x77bb004886a2c2ab), /* 18 */
    PACK_FLOAT_128(0x3fc1e542ba402022, 0x507a9cad2bf8f0bb)  /* 20 */
};

/* 0 <= x <= pi/4 */
static __inline float128_t poly_sin(float128_t x, softfloat_status_t &status)
{
    //                 3     5     7     9     11     13     15
    //                x     x     x     x     x      x      x
    // sin (x) ~ x - --- + --- - --- + --- - ---- + ---- - ---- =
    //                3!    5!    7!    9!    11!    13!    15!
    //
    //                 2     4     6     8     10     12     14
    //                x     x     x     x     x      x      x
    //   = x * [ 1 - --- + --- - --- + --- - ---- + ---- - ---- ] =
    //                3!    5!    7!    9!    11!    13!    15!
    //
    //           3                          3
    //          --       4k                --        4k+2
    //   p(x) = >  C  * x   > 0     q(x) = >  C   * x     < 0
    //          --  2k                     --  2k+1
    //          k=0                        k=0
    //
    //                          2
    //   sin(x) ~ x * [ p(x) + x * q(x) ]
    //

    return OddPoly(x, (const float128_t*) sin_arr, SIN_ARR_SIZE, status);
}

/* 0 <= x <= pi/4 */
static __inline float128_t poly_cos(float128_t x, softfloat_status_t &status)
{
    //                 2     4     6     8     10     12     14
    //                x     x     x     x     x      x      x
    // cos (x) ~ 1 - --- + --- - --- + --- - ---- + ---- - ----
    //                2!    4!    6!    8!    10!    12!    14!
    //
    //           3                          3
    //          --       4k                --        4k+2
    //   p(x) = >  C  * x   > 0     q(x) = >  C   * x     < 0
    //          --  2k                     --  2k+1
    //          k=0                        k=0
    //
    //                      2
    //   cos(x) ~ [ p(x) + x * q(x) ]
    //

    return EvenPoly(x, (const float128_t*) cos_arr, COS_ARR_SIZE, status);
}

static __inline void sincos_invalid(floatx80 *sin_a, floatx80 *cos_a, floatx80 a)
{
    if (sin_a) *sin_a = a;
    if (cos_a) *cos_a = a;
}

static __inline void sincos_tiny_argument(floatx80 *sin_a, floatx80 *cos_a, floatx80 a)
{
    if (sin_a) *sin_a = a;
    if (cos_a) *cos_a = floatx80_one;
}

static floatx80 sincos_approximation(int neg, float128_t r, uint64_t quotient, softfloat_status_t &status)
{
    if (quotient & 0x1) {
        r = poly_cos(r, status);
        neg = 0;
    } else  {
        r = poly_sin(r, status);
    }

    floatx80 result = f128_to_extF80(r, &status);
    if (quotient & 0x2)
        neg = ! neg;

    if (neg)
        floatx80_chs(result);

    return result;
}

// =================================================
// FSINCOS               Compute sin(x) and cos(x)
// =================================================

//
// Uses the following identities:
// ----------------------------------------------------------
//
//  sin(-x) = -sin(x)
//  cos(-x) =  cos(x)
//
//  sin(x+y) = sin(x)*cos(y)+cos(x)*sin(y)
//  cos(x+y) = sin(x)*sin(y)+cos(x)*cos(y)
//
//  sin(x+ pi/2)  =  cos(x)
//  sin(x+ pi)    = -sin(x)
//  sin(x+3pi/2)  = -cos(x)
//  sin(x+2pi)    =  sin(x)
//

int fsincos(floatx80 a, floatx80 *sin_a, floatx80 *cos_a, softfloat_status_t &status)
{
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    const floatx80 floatx80_default_nan =
        packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    uint64_t aSig0, aSig1 = 0;
    int32_t aExp, zExp, expDiff;
    int aSign, zSign;
    int q = 0;

    // handle unsupported extended double-precision floating encodings
    if (extF80_isUnsupported(a)) {
        goto invalid;
    }

    aSig0 = extF80_fraction(a);
    aExp = extF80_exp(a);
    aSign = extF80_sign(a);

    /* invalid argument */
    if (aExp == 0x7FFF) {
        if (aSig0 << 1) {
            sincos_invalid(sin_a, cos_a, softfloat_propagateNaNExtF80UI(a.signExp, aSig0, 0, 0, &status));
            return 0;
        }

    invalid:
        softfloat_raiseFlags(&status, softfloat_flag_invalid);
        sincos_invalid(sin_a, cos_a, floatx80_default_nan);
        return 0;
    }

    if (! aExp) {
        if (! aSig0) {
            sincos_tiny_argument(sin_a, cos_a, a);
            return 0;
        }

        softfloat_raiseFlags(&status, softfloat_flag_denormal);

        /* handle pseudo denormals */
        if (! (aSig0 & BX_CONST64(0x8000000000000000)))
        {
            softfloat_raiseFlags(&status, softfloat_flag_inexact);
            if (sin_a)
                softfloat_raiseFlags(&status, softfloat_flag_underflow);
            sincos_tiny_argument(sin_a, cos_a, a);
            return 0;
        }

        struct exp32_sig64 normExpSig = softfloat_normSubnormalExtF80Sig(aSig0);
        aExp = normExpSig.exp + 1;
        aSig0 = normExpSig.sig;
    }

    zSign = aSign;
    zExp = FLOATX80_EXP_BIAS;
    expDiff = aExp - zExp;

    /* argument is out-of-range */
    if (expDiff >= 63)
        return -1;

    softfloat_raiseFlags(&status, softfloat_flag_inexact);

    if (expDiff < -1) {    // doesn't require reduction
        if (expDiff <= -68) {
            a = packFloatx80(aSign, aExp, aSig0);
            sincos_tiny_argument(sin_a, cos_a, a);
            return 0;
        }
        zExp = aExp;
    }
    else {
        q = reduce_trig_arg(expDiff, zSign, aSig0, aSig1);
    }

    /* **************************** */
    /* argument reduction completed */
    /* **************************** */

    /* using float128 for approximation */
    float128_t r = softfloat_normRoundPackToF128(0, zExp-0x10, aSig0, aSig1, &status);

    if (aSign) q = -q;
    if (sin_a) *sin_a = sincos_approximation(zSign, r,   q, status);
    if (cos_a) *cos_a = sincos_approximation(zSign, r, q+1, status);

    return 0;
}

int fsin(floatx80 &a, softfloat_status_t &status)
{
    return fsincos(a, &a, 0, status);
}

int fcos(floatx80 &a, softfloat_status_t &status)
{
    return fsincos(a, 0, &a, status);
}

// =================================================
// FPTAN                 Compute tan(x)
// =================================================

//
// Uses the following identities:
//
// 1. ----------------------------------------------------------
//
//  sin(-x) = -sin(x)
//  cos(-x) =  cos(x)
//
//  sin(x+y) = sin(x)*cos(y)+cos(x)*sin(y)
//  cos(x+y) = sin(x)*sin(y)+cos(x)*cos(y)
//
//  sin(x+ pi/2)  =  cos(x)
//  sin(x+ pi)    = -sin(x)
//  sin(x+3pi/2)  = -cos(x)
//  sin(x+2pi)    =  sin(x)
//
// 2. ----------------------------------------------------------
//
//           sin(x)
//  tan(x) = ------
//           cos(x)
//

int ftan(floatx80 &a, softfloat_status_t &status)
{
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    const floatx80 floatx80_default_nan =
        packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    uint64_t aSig0, aSig1 = 0;
    int32_t aExp, zExp, expDiff;
    int aSign, zSign;
    int q = 0;

    // handle unsupported extended double-precision floating encodings
    if (extF80_isUnsupported(a)) {
        goto invalid;
    }

    aSig0 = extF80_fraction(a);
    aExp = extF80_exp(a);
    aSign = extF80_sign(a);

    /* invalid argument */
    if (aExp == 0x7FFF) {
        if (aSig0 << 1)
        {
            a = softfloat_propagateNaNExtF80UI(a.signExp, aSig0, 0, 0, &status);
            return 0;
        }

    invalid:
        softfloat_raiseFlags(&status, softfloat_flag_invalid);
        a = floatx80_default_nan;
        return 0;
    }

    if (! aExp) {
        if (! aSig0) return 0;
        softfloat_raiseFlags(&status, softfloat_flag_denormal);
        /* handle pseudo denormals */
        if (! (aSig0 & BX_CONST64(0x8000000000000000)))
        {
            softfloat_raiseFlags(&status, softfloat_flag_inexact | softfloat_flag_underflow);
            return 0;
        }

        struct exp32_sig64 normExpSig = softfloat_normSubnormalExtF80Sig(aSig0);
        aExp = normExpSig.exp + 1;
        aSig0 = normExpSig.sig;
    }

    zSign = aSign;
    zExp = FLOATX80_EXP_BIAS;
    expDiff = aExp - zExp;

    /* argument is out-of-range */
    if (expDiff >= 63)
        return -1;

    softfloat_raiseFlags(&status, softfloat_flag_inexact);

    if (expDiff < -1) {    // doesn't require reduction
        if (expDiff <= -68) {
            a = packFloatx80(aSign, aExp, aSig0);
            return 0;
        }
        zExp = aExp;
    }
    else {
        q = reduce_trig_arg(expDiff, zSign, aSig0, aSig1);
    }

    /* **************************** */
    /* argument reduction completed */
    /* **************************** */

    /* using float128 for approximation */
    float128_t r = softfloat_normRoundPackToF128(0, zExp-0x10, aSig0, aSig1, &status);

    float128_t sin_r = poly_sin(r, status);
    float128_t cos_r = poly_cos(r, status);

    if (q & 0x1) {
        r = f128_div(cos_r, sin_r, &status);
        zSign = ! zSign;
    } else {
        r = f128_div(sin_r, cos_r, &status);
    }

    a = f128_to_extF80(r, &status);
    if (zSign)
        floatx80_chs(a);

    return 0;
}

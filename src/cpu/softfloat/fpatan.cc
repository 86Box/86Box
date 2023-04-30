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

#include "softfloatx80.h"
#include "softfloat-round-pack.h"
#include "fpu_constant.h"

#define FPATAN_ARR_SIZE 11

static const float128 float128_one =
        packFloat128(BX_CONST64(0x3fff000000000000), BX_CONST64(0x0000000000000000));
static const float128 float128_sqrt3 =
        packFloat128(BX_CONST64(0x3fffbb67ae8584ca), BX_CONST64(0xa73b25742d7078b8));
static const floatx80 floatx80_pi  =
        packFloatx80(0, 0x4000, BX_CONST64(0xc90fdaa22168c235));

static const float128 float128_pi2 =
        packFloat128(BX_CONST64(0x3fff921fb54442d1), BX_CONST64(0x8469898CC5170416));
static const float128 float128_pi4 =
        packFloat128(BX_CONST64(0x3ffe921fb54442d1), BX_CONST64(0x8469898CC5170416));
static const float128 float128_pi6 =
        packFloat128(BX_CONST64(0x3ffe0c152382d736), BX_CONST64(0x58465BB32E0F580F));

static float128 atan_arr[FPATAN_ARR_SIZE] =
{
    PACK_FLOAT_128(0x3fff000000000000, 0x0000000000000000), /*  1 */
    PACK_FLOAT_128(0xbffd555555555555, 0x5555555555555555), /*  3 */
    PACK_FLOAT_128(0x3ffc999999999999, 0x999999999999999a), /*  5 */
    PACK_FLOAT_128(0xbffc249249249249, 0x2492492492492492), /*  7 */
    PACK_FLOAT_128(0x3ffbc71c71c71c71, 0xc71c71c71c71c71c), /*  9 */
    PACK_FLOAT_128(0xbffb745d1745d174, 0x5d1745d1745d1746), /* 11 */
    PACK_FLOAT_128(0x3ffb3b13b13b13b1, 0x3b13b13b13b13b14), /* 13 */
    PACK_FLOAT_128(0xbffb111111111111, 0x1111111111111111), /* 15 */
    PACK_FLOAT_128(0x3ffae1e1e1e1e1e1, 0xe1e1e1e1e1e1e1e2), /* 17 */
    PACK_FLOAT_128(0xbffaaf286bca1af2, 0x86bca1af286bca1b), /* 19 */
    PACK_FLOAT_128(0x3ffa861861861861, 0x8618618618618618)  /* 21 */
};

extern float128 OddPoly(float128 x, float128 *arr, int n, struct float_status_t *status);

/* |x| < 1/4 */
static float128 poly_atan(float128 x1, struct float_status_t *status)
{
/*
    //                 3     5     7     9     11     13     15     17
    //                x     x     x     x     x      x      x      x
    // atan(x) ~ x - --- + --- - --- + --- - ---- + ---- - ---- + ----
    //                3     5     7     9     11     13     15     17
    //
    //                 2     4     6     8     10     12     14     16
    //                x     x     x     x     x      x      x      x
    //   = x * [ 1 - --- + --- - --- + --- - ---- + ---- - ---- + ---- ]
    //                3     5     7     9     11     13     15     17
    //
    //           5                          5
    //          --       4k                --        4k+2
    //   p(x) = >  C  * x           q(x) = >  C   * x
    //          --  2k                     --  2k+1
    //          k=0                        k=0
    //
    //                            2
    //    atan(x) ~ x * [ p(x) + x * q(x) ]
    //
*/
    return OddPoly(x1, atan_arr, FPATAN_ARR_SIZE, status);
}

// =================================================
// FPATAN                  Compute y * log (x)
//                                        2
// =================================================

//
// Uses the following identities:
//
// 1. ----------------------------------------------------------
//
//   atan(-x) = -atan(x)
//
// 2. ----------------------------------------------------------
//
//                             x + y
//   atan(x) + atan(y) = atan -------, xy < 1
//                             1-xy
//
//                             x + y
//   atan(x) + atan(y) = atan ------- + PI, x > 0, xy > 1
//                             1-xy
//
//                             x + y
//   atan(x) + atan(y) = atan ------- - PI, x < 0, xy > 1
//                             1-xy
//
// 3. ----------------------------------------------------------
//
//   atan(x) = atan(INF) + atan(- 1/x)
//
//                           x-1
//   atan(x) = PI/4 + atan( ----- )
//                           x+1
//
//                           x * sqrt(3) - 1
//   atan(x) = PI/6 + atan( ----------------- )
//                             x + sqrt(3)
//
// 4. ----------------------------------------------------------
//                   3     5     7     9                 2n+1
//                  x     x     x     x              n  x
//   atan(x) = x - --- + --- - --- + --- - ... + (-1)  ------ + ...
//                  3     5     7     9                 2n+1
//

floatx80 fpatan(floatx80 a, floatx80 b, struct float_status_t *status)
{
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a) || floatx80_is_unsupported(b)) {
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    Bit64u aSig = extractFloatx80Frac(a);
    Bit32s aExp = extractFloatx80Exp(a);
    int aSign = extractFloatx80Sign(a);
    Bit64u bSig = extractFloatx80Frac(b);
    Bit32s bExp = extractFloatx80Exp(b);
    int bSign = extractFloatx80Sign(b);

    int zSign = aSign ^ bSign;

    if (bExp == 0x7FFF)
    {
        if ((Bit64u) (bSig<<1))
            return propagateFloatx80NaN(a, b, status);

        if (aExp == 0x7FFF) {
            if ((Bit64u) (aSig<<1))
                return propagateFloatx80NaN(a, b, status);

            if (aSign) {   /* return 3PI/4 */
                return roundAndPackFloatx80(80, bSign,
                        FLOATX80_3PI4_EXP, FLOAT_3PI4_HI, FLOAT_3PI4_LO, status);
            }
            else {         /* return  PI/4 */
                return roundAndPackFloatx80(80, bSign,
                        FLOATX80_PI4_EXP, FLOAT_PI_HI, FLOAT_PI_LO, status);
            }
        }

        if (aSig && (aExp == 0))
            float_raise(status, float_flag_denormal);

        /* return PI/2 */
        return roundAndPackFloatx80(80, bSign, FLOATX80_PI2_EXP, FLOAT_PI_HI, FLOAT_PI_LO, status);
    }
    if (aExp == 0x7FFF)
    {
        if ((Bit64u) (aSig<<1))
            return propagateFloatx80NaN(a, b, status);

        if (bSig && (bExp == 0))
            float_raise(status, float_flag_denormal);

return_PI_or_ZERO:

        if (aSign) {   /* return PI */
            return roundAndPackFloatx80(80, bSign, FLOATX80_PI_EXP, FLOAT_PI_HI, FLOAT_PI_LO, status);
        } else {       /* return  0 */
            return packFloatx80(bSign, 0, 0);
        }
    }
    if (bExp == 0)
    {
        if (bSig == 0) {
             if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
             goto return_PI_or_ZERO;
        }

        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }
    if (aExp == 0)
    {
        if (aSig == 0)   /* return PI/2 */
            return roundAndPackFloatx80(80, bSign, FLOATX80_PI2_EXP, FLOAT_PI_HI, FLOAT_PI_LO, status);

        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    float_raise(status, float_flag_inexact);

    /* |a| = |b| ==> return PI/4 */
    if (aSig == bSig && aExp == bExp)
        return roundAndPackFloatx80(80, bSign, FLOATX80_PI4_EXP, FLOAT_PI_HI, FLOAT_PI_LO, status);

    /* ******************************** */
    /* using float128 for approximation */
    /* ******************************** */

    float128 a128 = normalizeRoundAndPackFloat128(0, aExp-0x10, aSig, 0, status);
    float128 b128 = normalizeRoundAndPackFloat128(0, bExp-0x10, bSig, 0, status);
    float128 x;
    int swap = 0, add_pi6 = 0, add_pi4 = 0;

    if (aExp > bExp || (aExp == bExp && aSig > bSig))
    {
        x = float128_div(b128, a128, status);
    }
    else {
        x = float128_div(a128, b128, status);
        swap = 1;
    }

    Bit32s xExp = extractFloat128Exp(x);

    if (xExp <= FLOATX80_EXP_BIAS-40)
        goto approximation_completed;

    if (x.hi >= BX_CONST64(0x3ffe800000000000))        // 3/4 < x < 1
    {
        /*
        arctan(x) = arctan((x-1)/(x+1)) + pi/4
        */
        float128 t1 = float128_sub(x, float128_one, status);
        float128 t2 = float128_add(x, float128_one, status);
        x = float128_div(t1, t2, status);
        add_pi4 = 1;
    }
    else
    {
        /* argument correction */
        if (xExp >= 0x3FFD)                     // 1/4 < x < 3/4
        {
            /*
            arctan(x) = arctan((x*sqrt(3)-1)/(x+sqrt(3))) + pi/6
            */
            float128 t1 = float128_mul(x, float128_sqrt3, status);
            float128 t2 = float128_add(x, float128_sqrt3, status);
            x = float128_sub(t1, float128_one, status);
            x = float128_div(x, t2, status);
            add_pi6 = 1;
        }
    }

    x = poly_atan(x, status);
    if (add_pi6) x = float128_add(x, float128_pi6, status);
    if (add_pi4) x = float128_add(x, float128_pi4, status);

approximation_completed:
    if (swap) x = float128_sub(float128_pi2, x, status);
    floatx80 result = float128_to_floatx80(x, status);
    if (zSign) floatx80_chs(result);
    int rSign = extractFloatx80Sign(result);
    if (!bSign && rSign)
        return floatx80_add(result, floatx80_pi, status);
    if (bSign && !rSign)
        return floatx80_sub(result, floatx80_pi, status);
    return result;
}

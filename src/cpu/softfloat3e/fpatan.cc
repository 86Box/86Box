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

#include "config.h"
#include "softfloat.h"
#include "specialize.h"

#include "fpu_trans.h"
#include "fpu_constant.h"
#include "poly.h"

#define FPATAN_ARR_SIZE 11

static const float128_t float128_one =
        packFloat128(BX_CONST64(0x3fff000000000000), BX_CONST64(0x0000000000000000));
static const float128_t float128_sqrt3 =
        packFloat128(BX_CONST64(0x3fffbb67ae8584ca), BX_CONST64(0xa73b25742d7078b8));
static const floatx80 floatx80_pi  =
        packFloatx80(0, 0x4000, BX_CONST64(0xc90fdaa22168c235));

static const float128_t float128_pi2 =
        packFloat128(BX_CONST64(0x3fff921fb54442d1), BX_CONST64(0x8469898CC5170416));
static const float128_t float128_pi4 =
        packFloat128(BX_CONST64(0x3ffe921fb54442d1), BX_CONST64(0x8469898CC5170416));
static const float128_t float128_pi6 =
        packFloat128(BX_CONST64(0x3ffe0c152382d736), BX_CONST64(0x58465BB32E0F580F));

static float128_t atan_arr[FPATAN_ARR_SIZE] =
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

extern float128_t OddPoly(float128_t x, const float128_t *arr, int n, softfloat_status_t &status);

/* |x| < 1/4 */
static float128_t poly_atan(float128_t x1, softfloat_status_t &status)
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
    return OddPoly(x1, (const float128_t*) atan_arr, FPATAN_ARR_SIZE, status);
}

// =================================================
// FPATAN                  Compute arctan(y/x)
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

floatx80 fpatan(floatx80 a, floatx80 b, softfloat_status_t &status)
{
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    const floatx80 floatx80_default_nan =
        packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    // handle unsupported extended double-precision floating encodings
    if (extF80_isUnsupported(a) || extF80_isUnsupported(b)) {
        softfloat_raiseFlags(&status, softfloat_flag_invalid);
        return floatx80_default_nan;
    }

    uint64_t aSig = extF80_fraction(a);
    int32_t aExp = extF80_exp(a);
    int aSign = extF80_sign(a);
    uint64_t bSig = extF80_fraction(b);
    int32_t bExp = extF80_exp(b);
    int bSign = extF80_sign(b);

    int zSign = aSign ^ bSign;

    if (bExp == 0x7FFF)
    {
        if (bSig<<1)
            return softfloat_propagateNaNExtF80UI(a.signExp, aSig, b.signExp, bSig, &status);

        if (aExp == 0x7FFF) {
            if (aSig<<1)
                return softfloat_propagateNaNExtF80UI(a.signExp, aSig, b.signExp, bSig, &status);

            if (aSign)     /* return 3PI/4 */
                return softfloat_roundPackToExtF80(bSign, FLOATX80_3PI4_EXP, FLOAT_3PI4_HI, FLOAT_3PI4_LO, 80, &status);
            else           /* return  PI/4 */
                return softfloat_roundPackToExtF80(bSign, FLOATX80_PI4_EXP, FLOAT_PI_HI, FLOAT_PI_LO, 80, &status);
        }

        if (aSig && ! aExp)
            softfloat_raiseFlags(&status, softfloat_flag_denormal);

        /* return PI/2 */
        return softfloat_roundPackToExtF80(bSign, FLOATX80_PI2_EXP, FLOAT_PI_HI, FLOAT_PI_LO, 80, &status);
    }
    if (aExp == 0x7FFF)
    {
        if (aSig<<1)
            return softfloat_propagateNaNExtF80UI(a.signExp, aSig, b.signExp, bSig, &status);

        if (bSig && ! bExp)
            softfloat_raiseFlags(&status, softfloat_flag_denormal);

return_PI_or_ZERO:

        if (aSign)   /* return PI */
            return softfloat_roundPackToExtF80(bSign, FLOATX80_PI_EXP, FLOAT_PI_HI, FLOAT_PI_LO, 80, &status);
        else         /* return  0 */
            return packToExtF80(bSign, 0, 0);
    }
    if (! bExp)
    {
        if (! bSig) {
             if (aSig && ! aExp) softfloat_raiseFlags(&status, softfloat_flag_denormal);
             goto return_PI_or_ZERO;
        }

        softfloat_raiseFlags(&status, softfloat_flag_denormal);
        struct exp32_sig64 normExpSig = softfloat_normSubnormalExtF80Sig(bSig);
        bExp = normExpSig.exp + 1;
        bSig = normExpSig.sig;
    }
    if (! aExp)
    {
        if (! aSig)   /* return PI/2 */
            return softfloat_roundPackToExtF80(bSign, FLOATX80_PI2_EXP, FLOAT_PI_HI, FLOAT_PI_LO, 80, &status);

        softfloat_raiseFlags(&status, softfloat_flag_denormal);
        struct exp32_sig64 normExpSig = softfloat_normSubnormalExtF80Sig(aSig);
        aExp = normExpSig.exp + 1;
        aSig = normExpSig.sig;
    }

    softfloat_raiseFlags(&status, softfloat_flag_inexact);

    /* |a| = |b| ==> return PI/4 */
    if (aSig == bSig && aExp == bExp) {
        if (aSign)
            return softfloat_roundPackToExtF80(bSign, FLOATX80_3PI4_EXP, FLOAT_3PI4_HI, FLOAT_3PI4_LO, 80, &status);
        else
            return softfloat_roundPackToExtF80(bSign, FLOATX80_PI4_EXP, FLOAT_PI_HI, FLOAT_PI_LO, 80, &status);
    }

    /* ******************************** */
    /* using float128 for approximation */
    /* ******************************** */

    float128_t a128 = softfloat_normRoundPackToF128(0, aExp-0x10, aSig, 0, &status);
    float128_t b128 = softfloat_normRoundPackToF128(0, bExp-0x10, bSig, 0, &status);
    float128_t x;
    int swap = 0, add_pi6 = 0, add_pi4 = 0;

    if (aExp > bExp || (aExp == bExp && aSig > bSig))
    {
        x = f128_div(b128, a128, &status);
    }
    else {
        x = f128_div(a128, b128, &status);
        swap = 1;
    }

    int32_t xExp = expF128UI64(x.v64);

    if (xExp <= FLOATX80_EXP_BIAS-40)
        goto approximation_completed;

    if (x.v64 >= BX_CONST64(0x3ffe800000000000))        // 3/4 < x < 1
    {
        /*
        arctan(x) = arctan((x-1)/(x+1)) + pi/4
        */
        float128_t t1 = f128_sub(x, float128_one, &status);
        float128_t t2 = f128_add(x, float128_one, &status);
        x = f128_div(t1, t2, &status);
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
            float128_t t1 = f128_mul(x, float128_sqrt3, &status);
            float128_t t2 = f128_add(x, float128_sqrt3, &status);
            x = f128_sub(t1, float128_one, &status);
            x = f128_div(x, t2, &status);
            add_pi6 = 1;
        }
    }

    x = poly_atan(x, status);
    if (add_pi6) x = f128_add(x, float128_pi6, &status);
    if (add_pi4) x = f128_add(x, float128_pi4, &status);

approximation_completed:
    if (swap) x = f128_sub(float128_pi2, x, &status);
    floatx80 result = f128_to_extF80(x, &status);
    if (zSign) floatx80_chs(result);
    int rSign = extF80_sign(result);
    if (!bSign && rSign)
        return extF80_add(result, floatx80_pi, &status);
    if (bSign && !rSign)
        return extF80_sub(result, floatx80_pi, &status);
    return result;
}

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
#include "fpu_trans.h"
#include "specialize.h"
#include "softfloat-helpers.h"
#include "fpu_constant.h"
#include "poly.h"

static const floatx80 floatx80_one = packFloatx80(0, 0x3fff, BX_CONST64(0x8000000000000000));

static const float128_t float128_one =
    packFloat128(BX_CONST64(0x3fff000000000000), BX_CONST64(0x0000000000000000));
static const float128_t float128_two =
    packFloat128(BX_CONST64(0x4000000000000000), BX_CONST64(0x0000000000000000));

static const float128_t float128_ln2inv2 =
    packFloat128(BX_CONST64(0x400071547652b82f), BX_CONST64(0xe1777d0ffda0d23a));

#define SQRT2_HALF_SIG 	BX_CONST64(0xb504f333f9de6484)

#define L2_ARR_SIZE 9

static float128_t ln_arr[L2_ARR_SIZE] =
{
    PACK_FLOAT_128(0x3fff000000000000, 0x0000000000000000), /*  1 */
    PACK_FLOAT_128(0x3ffd555555555555, 0x5555555555555555), /*  3 */
    PACK_FLOAT_128(0x3ffc999999999999, 0x999999999999999a), /*  5 */
    PACK_FLOAT_128(0x3ffc249249249249, 0x2492492492492492), /*  7 */
    PACK_FLOAT_128(0x3ffbc71c71c71c71, 0xc71c71c71c71c71c), /*  9 */
    PACK_FLOAT_128(0x3ffb745d1745d174, 0x5d1745d1745d1746), /* 11 */
    PACK_FLOAT_128(0x3ffb3b13b13b13b1, 0x3b13b13b13b13b14), /* 13 */
    PACK_FLOAT_128(0x3ffb111111111111, 0x1111111111111111), /* 15 */
    PACK_FLOAT_128(0x3ffae1e1e1e1e1e1, 0xe1e1e1e1e1e1e1e2)  /* 17 */
};

static float128_t poly_ln(float128_t x1, softfloat_status_t &status)
{
/*
    //
    //                     3     5     7     9     11     13     15
    //        1+u         u     u     u     u     u      u      u
    // 1/2 ln ---  ~ u + --- + --- + --- + --- + ---- + ---- + ---- =
    //        1-u         3     5     7     9     11     13     15
    //
    //                     2     4     6     8     10     12     14
    //                    u     u     u     u     u      u      u
    //       = u * [ 1 + --- + --- + --- + --- + ---- + ---- + ---- ] =
    //                    3     5     7     9     11     13     15
    //
    //           3                          3
    //          --       4k                --        4k+2
    //   p(u) = >  C  * u           q(u) = >  C   * u
    //          --  2k                     --  2k+1
    //          k=0                        k=0
    //
    //          1+u                 2
    //   1/2 ln --- ~ u * [ p(u) + u * q(u) ]
    //          1-u
    //
*/
    return OddPoly(x1, (const float128_t*) ln_arr, L2_ARR_SIZE, status);
}

/* required sqrt(2)/2 < x < sqrt(2) */
static float128_t poly_l2(float128_t x, softfloat_status_t &status)
{
    /* using float128 for approximation */
    float128_t x_p1 = f128_add(x, float128_one, &status);
    float128_t x_m1 = f128_sub(x, float128_one, &status);
    x = f128_div(x_m1, x_p1, &status);
    x = poly_ln(x, status);
    x = f128_mul(x, float128_ln2inv2, &status);
    return x;
}

static float128_t poly_l2p1(float128_t x, softfloat_status_t &status)
{
    /* using float128 for approximation */
    float128_t x_plus2 = f128_add(x, float128_two, &status);
    x = f128_div(x, x_plus2, &status);
    x = poly_ln(x, status);
    x = f128_mul(x, float128_ln2inv2, &status);
    return x;
}

// =================================================
// FYL2X                   Compute y * log (x)
//                                        2
// =================================================

//
// Uses the following identities:
//
// 1. ----------------------------------------------------------
//              ln(x)
//   log (x) = -------,  ln (x*y) = ln(x) + ln(y)
//      2       ln(2)
//
// 2. ----------------------------------------------------------
//                1+u             x-1
//   ln (x) = ln -----, when u = -----
//                1-u             x+1
//
// 3. ----------------------------------------------------------
//                        3     5     7           2n+1
//       1+u             u     u     u           u
//   ln ----- = 2 [ u + --- + --- + --- + ... + ------ + ... ]
//       1-u             3     5     7           2n+1
//

floatx80 fyl2x(floatx80 a, floatx80 b, softfloat_status_t &status)
{
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    // handle unsupported extended double-precision floating encodings
    if (extF80_isUnsupported(a) || extF80_isUnsupported(b)) {
invalid:
        softfloat_raiseFlags(&status, softfloat_flag_invalid);
        return floatx80_default_nan;
    }

    uint64_t aSig = extF80_fraction(a);
    int32_t aExp = extF80_exp(a);
    int aSign = extF80_sign(a);
    uint64_t bSig = extF80_fraction(b);
    int32_t bExp = extF80_exp(b);
    int bSign = extF80_sign(b);

    int zSign = bSign ^ 1;

    if (aExp == 0x7FFF) {
        if ((aSig<<1) || ((bExp == 0x7FFF) && (bSig<<1))) {
            return softfloat_propagateNaNExtF80UI(a.signExp, aSig, b.signExp, bSig, &status);
        }
        if (aSign) goto invalid;
        else {
            if (! bExp) {
                if (! bSig) goto invalid;
                softfloat_raiseFlags(&status, softfloat_flag_denormal);
            }
            return packFloatx80(bSign, 0x7FFF, BX_CONST64(0x8000000000000000));
        }
    }
    if (bExp == 0x7FFF) {
        if (bSig << 1)
            return softfloat_propagateNaNExtF80UI(a.signExp, aSig, b.signExp, bSig, &status);
        if (aSign && (uint64_t)(aExp | aSig)) goto invalid;
        if (aSig && ! aExp)
            softfloat_raiseFlags(&status, softfloat_flag_denormal);
        if (aExp < 0x3FFF) {
            return packFloatx80(zSign, 0x7FFF, BX_CONST64(0x8000000000000000));
        }
        if (aExp == 0x3FFF && ! (aSig<<1)) goto invalid;
        return packFloatx80(bSign, 0x7FFF, BX_CONST64(0x8000000000000000));
    }
    if (! aExp) {
        if (! aSig) {
            if ((bExp | bSig) == 0) goto invalid;
            softfloat_raiseFlags(&status, softfloat_flag_divbyzero);
            return packFloatx80(zSign, 0x7FFF, BX_CONST64(0x8000000000000000));
        }
        if (aSign) goto invalid;
        softfloat_raiseFlags(&status, softfloat_flag_denormal);
        struct exp32_sig64 normExpSig = softfloat_normSubnormalExtF80Sig(aSig);
        aExp = normExpSig.exp + 1;
        aSig = normExpSig.sig;
    }
    if (aSign) goto invalid;
    if (! bExp) {
        if (! bSig) {
            if (aExp < 0x3FFF) return packFloatx80(zSign, 0, 0);
            return packFloatx80(bSign, 0, 0);
        }
        softfloat_raiseFlags(&status, softfloat_flag_denormal);
        struct exp32_sig64 normExpSig = softfloat_normSubnormalExtF80Sig(bSig);
        bExp = normExpSig.exp + 1;
        bSig = normExpSig.sig;
    }
    if (aExp == 0x3FFF && ! (aSig<<1))
        return packFloatx80(bSign, 0, 0);

    softfloat_raiseFlags(&status, softfloat_flag_inexact);

    int ExpDiff = aExp - 0x3FFF;
    aExp = 0;
    if (aSig >= SQRT2_HALF_SIG) {
        ExpDiff++;
        aExp--;
    }

    /* ******************************** */
    /* using float128 for approximation */
    /* ******************************** */

    float128_t b128 = softfloat_normRoundPackToF128(bSign, bExp-0x10, bSig, 0, &status);

    uint64_t zSig0, zSig1;
    shortShift128Right(aSig<<1, 0, 16, &zSig0, &zSig1);
    float128_t x = packFloat128(0, aExp+0x3FFF, zSig0, zSig1);
    x = poly_l2(x, status);
    x = f128_add(x, i32_to_f128(ExpDiff), &status);
    x = f128_mul(x, b128, &status);
    return f128_to_extF80(x, &status);
}

// =================================================
// FYL2XP1                 Compute y * log (x + 1)
//                                        2
// =================================================

//
// Uses the following identities:
//
// 1. ----------------------------------------------------------
//              ln(x)
//   log (x) = -------
//      2       ln(2)
//
// 2. ----------------------------------------------------------
//                  1+u              x
//   ln (x+1) = ln -----, when u = -----
//                  1-u             x+2
//
// 3. ----------------------------------------------------------
//                        3     5     7           2n+1
//       1+u             u     u     u           u
//   ln ----- = 2 [ u + --- + --- + --- + ... + ------ + ... ]
//       1-u             3     5     7           2n+1
//

floatx80 fyl2xp1(floatx80 a, floatx80 b, softfloat_status_t &status)
{
    int32_t aExp, bExp;
    uint64_t aSig, bSig, zSig0, zSig1, zSig2;
    int aSign, bSign;

/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    // handle unsupported extended double-precision floating encodings
    if (extF80_isUnsupported(a) || extF80_isUnsupported(b)) {
invalid:
        softfloat_raiseFlags(&status, softfloat_flag_invalid);
        return floatx80_default_nan;
    }


    aSig = extF80_fraction(a);
    aExp = extF80_exp(a);
    aSign = extF80_sign(a);
    bSig = extF80_fraction(b);
    bExp = extF80_exp(b);
    bSign = extF80_sign(b);
    int zSign = aSign ^ bSign;

    if (aExp == 0x7FFF) {
        if ((aSig<<1) != 0 || ((bExp == 0x7FFF) && (bSig<<1) != 0)) {
            return softfloat_propagateNaNExtF80UI(a.signExp, aSig, b.signExp, bSig, &status);
        }
        if (aSign) goto invalid;
        else {
            if (! bExp) {
                if (! bSig) goto invalid;
                softfloat_raiseFlags(&status, softfloat_flag_denormal);
            }
            return packFloatx80(bSign, 0x7FFF, BX_CONST64(0x8000000000000000));
        }
    }
    if (bExp == 0x7FFF)
    {
        if (bSig << 1)
            return softfloat_propagateNaNExtF80UI(a.signExp, aSig, b.signExp, bSig, &status);

        if (! aExp) {
            if (! aSig) goto invalid;
            softfloat_raiseFlags(&status, softfloat_flag_denormal);
        }

        return packFloatx80(zSign, 0x7FFF, BX_CONST64(0x8000000000000000));
    }
    if (! aExp) {
        if (! aSig) {
            if (bSig && ! bExp) softfloat_raiseFlags(&status, softfloat_flag_denormal);
            return packFloatx80(zSign, 0, 0);
        }
        softfloat_raiseFlags(&status, softfloat_flag_denormal);
        struct exp32_sig64 normExpSig = softfloat_normSubnormalExtF80Sig(aSig);
        aExp = normExpSig.exp + 1;
        aSig = normExpSig.sig;
    }
    if (! bExp) {
        if (! bSig) return packFloatx80(zSign, 0, 0);
        softfloat_raiseFlags(&status, softfloat_flag_denormal);
        struct exp32_sig64 normExpSig = softfloat_normSubnormalExtF80Sig(bSig);
        bExp = normExpSig.exp + 1;
        bSig = normExpSig.sig;
    }

    softfloat_raiseFlags(&status, softfloat_flag_inexact);

    if (aSign && aExp >= 0x3FFF)
        return a;

    if (aExp >= 0x3FFC) // big argument
    {
        return fyl2x(extF80_add(a, floatx80_one, &status), b, status);
    }

    // handle tiny argument
    if (aExp < FLOATX80_EXP_BIAS-70)
    {
        // first order approximation, return (a*b)/ln(2)
        int32_t zExp = aExp + FLOAT_LN2INV_EXP - 0x3FFE;

        mul128By64To192(FLOAT_LN2INV_HI, FLOAT_LN2INV_LO, aSig, &zSig0, &zSig1, &zSig2);
        if (0 < (int64_t) zSig0) {
            shortShift128Left(zSig0, zSig1, 1, &zSig0, &zSig1);
            --zExp;
        }

        zExp = zExp + bExp - 0x3FFE;
        mul128By64To192(zSig0, zSig1, bSig, &zSig0, &zSig1, &zSig2);
        if (0 < (int64_t) zSig0) {
            shortShift128Left(zSig0, zSig1, 1, &zSig0, &zSig1);
            --zExp;
        }

        return softfloat_roundPackToExtF80(aSign ^ bSign, zExp, zSig0, zSig1, 80, &status);
    }

    /* ******************************** */
    /* using float128 for approximation */
    /* ******************************** */

    float128_t b128 = softfloat_normRoundPackToF128(bSign, bExp-0x10, bSig, 0, &status);

    shortShift128Right(aSig<<1, 0, 16, &zSig0, &zSig1);
    float128_t x = packFloat128(aSign, aExp, zSig0, zSig1);
    x = poly_l2p1(x, status);
    x = f128_mul(x, b128, &status);
    return f128_to_extF80(x, &status);
}

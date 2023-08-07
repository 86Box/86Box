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

static const floatx80 floatx80_negone  = packFloatx80(1, 0x3fff, BX_CONST64(0x8000000000000000));
static const floatx80 floatx80_neghalf = packFloatx80(1, 0x3ffe, BX_CONST64(0x8000000000000000));
static const float128 float128_ln2     =
    packFloat128(BX_CONST64(0x3ffe62e42fefa39e), BX_CONST64(0xf35793c7673007e6));

#ifdef BETTER_THAN_PENTIUM

#define LN2_SIG_HI BX_CONST64(0xb17217f7d1cf79ab)
#define LN2_SIG_LO BX_CONST64(0xc9e3b39800000000)  /* 96 bit precision */

#else

#define LN2_SIG_HI BX_CONST64(0xb17217f7d1cf79ab)
#define LN2_SIG_LO BX_CONST64(0xc000000000000000)  /* 67-bit precision */

#endif

#define EXP_ARR_SIZE 15

static float128 exp_arr[EXP_ARR_SIZE] =
{
    PACK_FLOAT_128(0x3fff000000000000, 0x0000000000000000), /*  1 */
    PACK_FLOAT_128(0x3ffe000000000000, 0x0000000000000000), /*  2 */
    PACK_FLOAT_128(0x3ffc555555555555, 0x5555555555555555), /*  3 */
    PACK_FLOAT_128(0x3ffa555555555555, 0x5555555555555555), /*  4 */
    PACK_FLOAT_128(0x3ff8111111111111, 0x1111111111111111), /*  5 */
    PACK_FLOAT_128(0x3ff56c16c16c16c1, 0x6c16c16c16c16c17), /*  6 */
    PACK_FLOAT_128(0x3ff2a01a01a01a01, 0xa01a01a01a01a01a), /*  7 */
    PACK_FLOAT_128(0x3fefa01a01a01a01, 0xa01a01a01a01a01a), /*  8 */
    PACK_FLOAT_128(0x3fec71de3a556c73, 0x38faac1c88e50017), /*  9 */
    PACK_FLOAT_128(0x3fe927e4fb7789f5, 0xc72ef016d3ea6679), /* 10 */
    PACK_FLOAT_128(0x3fe5ae64567f544e, 0x38fe747e4b837dc7), /* 11 */
    PACK_FLOAT_128(0x3fe21eed8eff8d89, 0x7b544da987acfe85), /* 12 */
    PACK_FLOAT_128(0x3fde6124613a86d0, 0x97ca38331d23af68), /* 13 */
    PACK_FLOAT_128(0x3fda93974a8c07c9, 0xd20badf145dfa3e5), /* 14 */
    PACK_FLOAT_128(0x3fd6ae7f3e733b81, 0xf11d8656b0ee8cb0)  /* 15 */
};

extern float128 EvalPoly(float128 x, float128 *arr, int n, struct float_status_t *status);

/* required -1 < x < 1 */
static float128 poly_exp(float128 x, struct float_status_t *status)
{
/*
    //               2     3     4     5     6     7     8     9
    //  x           x     x     x     x     x     x     x     x
    // e - 1 ~ x + --- + --- + --- + --- + --- + --- + --- + --- + ...
    //              2!    3!    4!    5!    6!    7!    8!    9!
    //
    //                     2     3     4     5     6     7     8
    //              x     x     x     x     x     x     x     x
    //   = x [ 1 + --- + --- + --- + --- + --- + --- + --- + --- + ... ]
    //              2!    3!    4!    5!    6!    7!    8!    9!
    //
    //           8                          8
    //          --       2k                --        2k+1
    //   p(x) = >  C  * x           q(x) = >  C   * x
    //          --  2k                     --  2k+1
    //          k=0                        k=0
    //
    //    x
    //   e  - 1 ~ x * [ p(x) + x * q(x) ]
    //
*/
    float128 t = EvalPoly(x, exp_arr, EXP_ARR_SIZE, status);
    return float128_mul(t, x, status);
}

// =================================================
//                                  x
// FX2M1                   Compute 2  - 1
// =================================================

//
// Uses the following identities:
//
// 1. ----------------------------------------------------------
//      x    x*ln(2)
//     2  = e
//
// 2. ----------------------------------------------------------
//                      2     3     4     5           n
//      x        x     x     x     x     x           x
//     e  = 1 + --- + --- + --- + --- + --- + ... + --- + ...
//               1!    2!    3!    4!    5!          n!
//

floatx80 f2xm1(floatx80 a, struct float_status_t *status)
{
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    Bit64u zSig0, zSig1, zSig2;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a))
    {
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    Bit64u aSig = extractFloatx80Frac(a);
    Bit32s aExp = extractFloatx80Exp(a);
    int aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((Bit64u) (aSig<<1))
            return propagateFloatx80NaNOne(a, status);

        return (aSign) ? floatx80_negone : a;
    }

    if (aExp == 0) {
        if (aSig == 0) return a;
        float_raise(status, float_flag_denormal | float_flag_inexact);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);

    tiny_argument:
        mul128By64To192(LN2_SIG_HI, LN2_SIG_LO, aSig, &zSig0, &zSig1, &zSig2);
        if (0 < (Bit64s) zSig0) {
            shortShift128Left(zSig0, zSig1, 1, &zSig0, &zSig1);
            --aExp;
        }
        return
            roundAndPackFloatx80(80, aSign, aExp, zSig0, zSig1, status);
    }

    float_raise(status, float_flag_inexact);

    if (aExp < 0x3FFF)
    {
        if (aExp < FLOATX80_EXP_BIAS-68)
            goto tiny_argument;

        /* ******************************** */
        /* using float128 for approximation */
        /* ******************************** */

        float128 x = floatx80_to_float128(a, status);
        x = float128_mul(x, float128_ln2, status);
        x = poly_exp(x, status);
        return float128_to_floatx80(x, status);
    }
    else
    {
        if (a.exp == 0xBFFF && ! (aSig<<1))
           return floatx80_neghalf;

        return a;
    }
}

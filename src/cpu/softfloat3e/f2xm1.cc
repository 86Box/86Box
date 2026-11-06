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
#include "specialize.h"

#include "fpu_trans.h"
#include "softfloat-helpers.h"

static const floatx80 floatx80_negone  = packFloatx80(1, 0x3fff, BX_CONST64(0x8000000000000000));
static const floatx80 floatx80_neghalf = packFloatx80(1, 0x3ffe, BX_CONST64(0x8000000000000000));
static const float128_t float128_ln2   =
    packFloat128(BX_CONST64(0x3ffe62e42fefa39e), BX_CONST64(0xf35793c7673007e6));

#ifdef BETTER_THAN_PENTIUM

#define LN2_SIG_HI BX_CONST64(0xb17217f7d1cf79ab)
#define LN2_SIG_LO BX_CONST64(0xc9e3b39800000000)  /* 96 bit precision */

#else

#define LN2_SIG_HI BX_CONST64(0xb17217f7d1cf79ab)
#define LN2_SIG_LO BX_CONST64(0xc000000000000000)  /* 67-bit precision */

#endif

/* Taylor coefficients 1/n!. With |x| <= ln(2), the omitted tail after
   fifteen terms is large enough to lose thousands of binary80 ULPs near
   the ends of F2XM1's input range. Twenty-four terms bound that tail by
   2^-95; keep the evaluation in binary128 until the final rounding. */
#define EXP_ARR_SIZE 24

static float128_t exp_arr[EXP_ARR_SIZE] =
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
    PACK_FLOAT_128(0x3fd6ae7f3e733b81, 0xf11d8656b0ee8cb0), /* 15 */
    PACK_FLOAT_128(0x3fd2ae7f3e733b81, 0xf11d8656b0ee8cb0), /* 16 */
    PACK_FLOAT_128(0x3fce952c77030ad4, 0xa6b2605197771b00), /* 17 */
    PACK_FLOAT_128(0x3fca6827863b97d9, 0x77bb004886a2c2ab), /* 18 */
    PACK_FLOAT_128(0x3fc62f49b4681415, 0x724ca1ec3b7b9675), /* 19 */
    PACK_FLOAT_128(0x3fc1e542ba402022, 0x507a9cad2bf8f0bb), /* 20 */
    PACK_FLOAT_128(0x3fbd71b8ef6dcf57, 0x18bef146fcee6e45), /* 21 */
    PACK_FLOAT_128(0x3fb90ce396db7f85, 0x29450c90b7f338ec), /* 22 */
    PACK_FLOAT_128(0x3fb4761b41316381, 0x9d97b8704dd7f628), /* 23 */
    PACK_FLOAT_128(0x3faff2cf01972f57, 0x7cca4b4067ca9d8a)  /* 24 */
};

extern float128_t EvalPoly(float128_t x, const float128_t *arr, int n, struct softfloat_status_t *status);

/* required -1 < x < 1 */
static float128_t poly_exp(float128_t x, struct softfloat_status_t *status)
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
    float128_t t = EvalPoly(x, (const float128_t*) exp_arr, EXP_ARR_SIZE, status);
    return f128_mul(t, x, status);
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
floatx80 f2xm1(floatx80 a, struct softfloat_status_t *status)
{
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    static const floatx80 floatx80_default_nan =
        packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    uint64_t zSig0, zSig1, zSig2;
    struct exp32_sig64 normExpSig;

    // handle unsupported extended double-precision floating encodings
    if (extF80_isUnsupported(a)) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        return floatx80_default_nan;
    }

    uint64_t aSig = extF80_fraction(a);
    int32_t aExp = extF80_exp(a);
    int aSign = extF80_sign(a);

    if (aExp == 0x7FFF) {
        if (aSig << 1)
            return softfloat_propagateNaNExtF80UI(a.signExp, aSig, 0, 0, status);

        return (aSign) ? floatx80_negone : a;
    }

    if (! aExp) {
        if (! aSig) return a;
        softfloat_raiseFlags(status, softfloat_flag_denormal | softfloat_flag_inexact);
        normExpSig = softfloat_normSubnormalExtF80Sig(aSig);
        aExp = normExpSig.exp + 1;
        aSig = normExpSig.sig;

    tiny_argument:
        mul128By64To192(LN2_SIG_HI, LN2_SIG_LO, aSig, &zSig0, &zSig1, &zSig2);
        if (0 < (int64_t) zSig0) {
            shortShift128Left(zSig0, zSig1, 1, &zSig0, &zSig1);
            --aExp;
        }
        return softfloat_roundPackToExtF80(aSign, aExp, zSig0, zSig1, 80, status);
    }

    softfloat_raiseFlags(status, softfloat_flag_inexact);

    if (aExp < 0x3FFF)
    {
        if (aExp < FLOATX80_EXP_BIAS-68)
            goto tiny_argument;

        /* ******************************** */
        /* using float128 for approximation */
        /* ******************************** */

        float128_t x = extF80_to_f128(a, status);
        x = f128_mul(x, float128_ln2, status);
        x = poly_exp(x, status);
        return f128_to_extF80(x, status);
    }
    else
    {
        if (a.signExp == 0xBFFF && ! (aSig<<1))
           return floatx80_neghalf;

        return a;
    }
}

/* Early Intel387 CORDIC model. The recurrence is the pseudo-division /
   reverse pseudo-multiplication described by Kahan, rather than a change
   to the final rounded value. Work values have an implicit exponent and
   a signed 68-bit Q2.65 format (sign, two integer bits, 65 fraction bits).
   This working precision is inferred from Intel's MCP diagnostics; it is
   not a claim to reproduce every revision of the original microcode.

   References: W. Kahan, "Pseudo-Division Algorithms for Floating-Point
   Logarithms and Exponentials" (2001), pp. 5-6; J. Valerio, "Computer
   System Support for Scientific and Engineering Computation", lecture 13
   (1988), on the 387/960 CORDIC datapath and fifteen log constants. */
static const uint64_t log2_387[15] = {
    BX_CONST64(0x570068e7ef5a1e7f), /*  1 */
    BX_CONST64(0x934f0979a3715fc9), /*  2 */
    BX_CONST64(0xb803473f7ad0f3f4), /*  3 */
    BX_CONST64(0xcc7edf592262cf9a), /*  4 */
    BX_CONST64(0xd75a6eb1dfb0e627), /*  5 */
    BX_CONST64(0xdcf2d0b85a45314e), /*  6 */
    BX_CONST64(0xdfca16dde10a2ff2), /*  7 */
    BX_CONST64(0xe1388daf558ee95b), /*  8 */
    BX_CONST64(0xe1f07fe14eac98c7), /*  9 */
    BX_CONST64(0xe24ca6e87e8a83f5), /* 10 */
    BX_CONST64(0xe27ac5ef2af8615f), /* 11 */
    BX_CONST64(0xe291d85437f911c2), /* 12 */
    BX_CONST64(0xe29d623f4a6c6ea9), /* 13 */
    BX_CONST64(0xe2a32762fa6ba651), /* 14 */
    BX_CONST64(0xe2a60a005c95c8ce)  /* 15 */
};

/* Convert an unsigned fixed-point significand without rounding it to
   binary80 first. All callers are finite and well within binary128 range. */
static float128_t fixed_387_to_f128(uint64_t hi, uint64_t lo, int power,
                                  struct softfloat_status_t *status)
{
    float128_t value = ui64_to_f128(lo);
    if (hi) {
        float128_t upper = ui64_to_f128(hi);
        upper.v64 += BX_CONST64(0x0040000000000000); /* multiply by 2^64 */
        value = f128_add(upper, value, status);
    }
    return f128_mul(value, packFloat128(uint64_t(0x3fff + power) << 48, 0), status);
}

floatx80 f2xm1_387(floatx80 a, struct softfloat_status_t *status)
{
    const int aExp = extF80_exp(a);

    /* Keep the shared handling of zeros, denormals, NaNs, unsupported
       encodings, +/-1 and undefined out-of-range operands. */
    if (extF80_isUnsupported(a) || aExp < FLOATX80_EXP_BIAS - 68 ||
        aExp >= FLOATX80_EXP_BIAS)
        return f2xm1(a, status);

    const int start = FLOATX80_EXP_BIAS - aExp;
    const int end = (start > 15) ? start : 15;
    uint64_t hi = a.signif >> 62;
    uint64_t lo = a.signif << 2;
    unsigned quotient = 0;

    /* ROM entries are round(2^k * log2(1 + 2^-k) * 2^65).
       Their upper word is always 2; only the low word is stored above. */
    for (int k = start; k <= 15; k++) {
        if (hi > 2 || (hi == 2 && lo >= log2_387[k - 1])) {
            sub128(hi, lo, 2, log2_387[k - 1], &hi, &lo);
            quotient |= 1u << k;
        }
        if (k < 15)
            shortShift128Left(hi, lo, 1, &hi, &lo);
    }

    /* Approximate the small residue before rebuilding the original input.
       Retain the residue in binary128, then truncate to the fixed-point
       register at its implicit exponent, independent of guest RC. */
    float128_t residue = fixed_387_to_f128(hi, lo, -65 - end, status);
    float128_t value = poly_exp(f128_mul(residue, float128_ln2, status), status);
    if (!(value.v64 | value.v0)) {
        hi = lo = 0;
    } else {
        const int shift = 47 - (int((value.v64 >> 48) & 0x7fff) - 0x3fff) - end;
        const uint64_t sigHi = (value.v64 & BX_CONST64(0x0000ffffffffffff)) |
                              BX_CONST64(0x0001000000000000);
        if (shift < 64)
            shortShift128Right(sigHi, value.v0, shift, &hi, &lo);
        else {
            hi = 0;
            lo = (shift < 128) ? sigHi >> (shift - 64) : 0;
        }
    }

    for (int k = 15; k >= start; k--) {
        if (quotient & (1u << k)) {
            uint64_t termHi, termLo;
            shortShift128Right(hi, lo, k, &termHi, &termLo);
            struct uint128 sum = softfloat_add128(hi, lo, termHi + 2, termLo);
            hi = sum.v64;
            lo = sum.v0;
        }
        if (k > start)
            shortShift128Right(hi, lo, 1, &hi, &lo);
    }

    value = fixed_387_to_f128(hi, lo, -65 - start, status);
    if (extF80_sign(a)) {
        float128_t denominator = f128_add(value, packFloat128(BX_CONST64(0x3fff000000000000), 0), status);
        value.v64 |= BX_CONST64(0x8000000000000000);
        value = f128_div(value, denominator, status);
    }
    softfloat_raiseFlags(status, softfloat_flag_inexact);
    return f128_to_extF80(value, status);
}

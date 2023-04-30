/*============================================================================
This C source file is part of the SoftFloat IEC/IEEE Floating-point Arithmetic
Package, Release 2b.

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

/*============================================================================
 * This code is based on QEMU patch by Peter Maydell
 * Adapted for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#include "softfloat.h"
#include "softfloat-round-pack.h"

/*----------------------------------------------------------------------------
| Primitive arithmetic functions, including multi-word arithmetic, and
| division and square root approximations. (Can be specialized to target
| if desired).
*----------------------------------------------------------------------------*/
#include "softfloat-macros.h"

/*----------------------------------------------------------------------------
| Functions and definitions to determine:  (1) whether tininess for underflow
| is detected before or after rounding by default, (2) what (if anything)
| happens when exceptions are raised, (3) how signaling NaNs are distinguished
| from quiet NaNs, (4) the default generated quiet NaNs, and (5) how NaNs
| are propagated from function inputs to output.  These details are target-
| specific.
*----------------------------------------------------------------------------*/
#include "softfloat-specialize.h"

/*----------------------------------------------------------------------------
| Takes three single-precision floating-point values `a', `b' and `c', one of
| which is a NaN, and returns the appropriate NaN result.  If any of  `a',
| `b' or `c' is a signaling NaN, the invalid exception is raised.
| The input infzero indicates whether a*b was 0*inf or inf*0 (in which case
| obviously c is a NaN, and whether to propagate c or some other NaN is
| implementation defined).
*----------------------------------------------------------------------------*/

static float32 propagateFloat32MulAddNaN(float32 a, float32 b, float32 c, struct float_status_t *status)
{
    int aIsNaN = float32_is_nan(a);
    int bIsNaN = float32_is_nan(b);

    int aIsSignalingNaN = float32_is_signaling_nan(a);
    int bIsSignalingNaN = float32_is_signaling_nan(b);
    int cIsSignalingNaN = float32_is_signaling_nan(c);

    a |= 0x00400000;
    b |= 0x00400000;
    c |= 0x00400000;

    if (aIsSignalingNaN | bIsSignalingNaN | cIsSignalingNaN)
        float_raise(status, float_flag_invalid);

    //  operate according to float_first_operand_nan mode
    if (aIsSignalingNaN | aIsNaN) {
        return a;
    }
    else {
        return (bIsSignalingNaN | bIsNaN) ? b : c;
    }
}

/*----------------------------------------------------------------------------
| Takes three double-precision floating-point values `a', `b' and `c', one of
| which is a NaN, and returns the appropriate NaN result.  If any of  `a',
| `b' or `c' is a signaling NaN, the invalid exception is raised.
| The input infzero indicates whether a*b was 0*inf or inf*0 (in which case
| obviously c is a NaN, and whether to propagate c or some other NaN is
| implementation defined).
*----------------------------------------------------------------------------*/

static float64 propagateFloat64MulAddNaN(float64 a, float64 b, float64 c, struct float_status_t *status)
{
    int aIsNaN = float64_is_nan(a);
    int bIsNaN = float64_is_nan(b);

    int aIsSignalingNaN = float64_is_signaling_nan(a);
    int bIsSignalingNaN = float64_is_signaling_nan(b);
    int cIsSignalingNaN = float64_is_signaling_nan(c);

    a |= BX_CONST64(0x0008000000000000);
    b |= BX_CONST64(0x0008000000000000);
    c |= BX_CONST64(0x0008000000000000);

    if (aIsSignalingNaN | bIsSignalingNaN | cIsSignalingNaN)
        float_raise(status, float_flag_invalid);

    //  operate according to float_first_operand_nan mode
    if (aIsSignalingNaN | aIsNaN) {
        return a;
    }
    else {
        return (bIsSignalingNaN | bIsNaN) ? b : c;
    }
}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the single-precision floating-point values
| `a' and `b' then adding 'c', with no intermediate rounding step after the
| multiplication.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic 754-2008.
| The flags argument allows the caller to select negation of the
| addend, the intermediate product, or the final result. (The difference
| between this and having the caller do a separate negation is that negating
| externally will flip the sign bit on NaNs.)
*----------------------------------------------------------------------------*/

float32 float32_muladd(float32 a, float32 b, float32 c, int flags, struct float_status_t *status)
{
    int aSign, bSign, cSign, zSign;
    Bit16s aExp, bExp, cExp, pExp, zExp;
    Bit32u aSig, bSig, cSig;
    int pInf, pZero, pSign;
    Bit64u pSig64, cSig64, zSig64;
    Bit32u pSig;
    int shiftcount;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);
    bSig = extractFloat32Frac(b);
    bExp = extractFloat32Exp(b);
    bSign = extractFloat32Sign(b);
    cSig = extractFloat32Frac(c);
    cExp = extractFloat32Exp(c);
    cSign = extractFloat32Sign(c);

    /* It is implementation-defined whether the cases of (0,inf,qnan)
     * and (inf,0,qnan) raise InvalidOperation or not (and what QNaN
     * they return if they do), so we have to hand this information
     * off to the target-specific pick-a-NaN routine.
     */
    if (((aExp == 0xff) && aSig) ||
        ((bExp == 0xff) && bSig) ||
        ((cExp == 0xff) && cSig)) {
        return propagateFloat32MulAddNaN(a, b, c, status);
    }

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
        if (cExp == 0) cSig = 0;
    }

    int infzero = ((aExp == 0 && aSig == 0 && bExp == 0xff && bSig == 0) ||
                   (aExp == 0xff && aSig == 0 && bExp == 0 && bSig == 0));

    if (infzero) {
        float_raise(status, float_flag_invalid);
        return float32_default_nan;
    }

    if (flags & float_muladd_negate_c) {
        cSign ^= 1;
    }

    /* Work out the sign and type of the product */
    pSign = aSign ^ bSign;
    if (flags & float_muladd_negate_product) {
        pSign ^= 1;
    }
    pInf = (aExp == 0xff) || (bExp == 0xff);
    pZero = ((aExp | aSig) == 0) || ((bExp | bSig) == 0);

    if (cExp == 0xff) {
        if (pInf && (pSign ^ cSign)) {
            /* addition of opposite-signed infinities => InvalidOperation */
            float_raise(status, float_flag_invalid);
            return float32_default_nan;
        }
        /* Otherwise generate an infinity of the same sign */
        if ((aSig && aExp == 0) || (bSig && bExp == 0)) {
            float_raise(status, float_flag_denormal);
        }
        return packFloat32(cSign, 0xff, 0);
    }

    if (pInf) {
        if ((aSig && aExp == 0) || (bSig && bExp == 0) || (cSig && cExp == 0)) {
            float_raise(status, float_flag_denormal);
        }
        return packFloat32(pSign, 0xff, 0);
    }

    if (pZero) {
        if (cExp == 0) {
            if (cSig == 0) {
                /* Adding two exact zeroes */
                if (pSign == cSign) {
                    zSign = pSign;
                } else if (get_float_rounding_mode(status) == float_round_down) {
                    zSign = 1;
                } else {
                    zSign = 0;
                }
                return packFloat32(zSign, 0, 0);
            }
            /* Exact zero plus a denormal */
            float_raise(status, float_flag_denormal);
            if (get_flush_underflow_to_zero(status)) {
                float_raise(status, float_flag_underflow | float_flag_inexact);
                return packFloat32(cSign, 0, 0);
            }
        }
        /* Zero plus something non-zero */
        return packFloat32(cSign, cExp, cSig);
    }

    if (aExp == 0) {
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(bSig, &bExp, &bSig);
    }

    /* Calculate the actual result a * b + c */

    /* Multiply first; this is easy. */
    /* NB: we subtract 0x7e where float32_mul() subtracts 0x7f
     * because we want the true exponent, not the "one-less-than"
     * flavour that roundAndPackFloat32() takes.
     */
    pExp = aExp + bExp - 0x7e;
    aSig = (aSig | 0x00800000) << 7;
    bSig = (bSig | 0x00800000) << 8;
    pSig64 = (Bit64u)aSig * bSig;
    if ((Bit64s)(pSig64 << 1) >= 0) {
        pSig64 <<= 1;
        pExp--;
    }

    zSign = pSign;

    /* Now pSig64 is the significand of the multiply, with the explicit bit in
     * position 62.
     */
    if (cExp == 0) {
        if (!cSig) {
            /* Throw out the special case of c being an exact zero now */
            pSig = (Bit32u) shift64RightJamming(pSig64, 32);
            return roundAndPackFloat32(zSign, pExp - 1, pSig, status);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(cSig, &cExp, &cSig);
    }

    cSig64 = (Bit64u)cSig << 39;
    cSig64 |= BX_CONST64(0x4000000000000000);
    int expDiff = pExp - cExp;

    if (pSign == cSign) {
        /* Addition */
        if (expDiff > 0) {
            /* scale c to match p */
            cSig64 = shift64RightJamming(cSig64, expDiff);
            zExp = pExp;
        } else if (expDiff < 0) {
            /* scale p to match c */
            pSig64 = shift64RightJamming(pSig64, -expDiff);
            zExp = cExp;
        } else {
            /* no scaling needed */
            zExp = cExp;
        }
        /* Add significands and make sure explicit bit ends up in posn 62 */
        zSig64 = pSig64 + cSig64;
        if ((Bit64s)zSig64 < 0) {
            zSig64 = shift64RightJamming(zSig64, 1);
        } else {
            zExp--;
        }
        zSig64 = shift64RightJamming(zSig64, 32);
        return roundAndPackFloat32(zSign, zExp, zSig64, status);
    } else {
        /* Subtraction */
        if (expDiff > 0) {
            cSig64 = shift64RightJamming(cSig64, expDiff);
            zSig64 = pSig64 - cSig64;
            zExp = pExp;
        } else if (expDiff < 0) {
            pSig64 = shift64RightJamming(pSig64, -expDiff);
            zSig64 = cSig64 - pSig64;
            zExp = cExp;
            zSign ^= 1;
        } else {
            zExp = pExp;
            if (cSig64 < pSig64) {
                zSig64 = pSig64 - cSig64;
            } else if (pSig64 < cSig64) {
                zSig64 = cSig64 - pSig64;
                zSign ^= 1;
            } else {
                /* Exact zero */
                return packFloat32(get_float_rounding_mode(status) == float_round_down, 0, 0);
            }
        }
        --zExp;
        /* Do the equivalent of normalizeRoundAndPackFloat32() but
         * starting with the significand in a Bit64u.
         */
        shiftcount = countLeadingZeros64(zSig64) - 1;
        zSig64 <<= shiftcount;
        zExp -= shiftcount;
        zSig64 = shift64RightJamming(zSig64, 32);
        return roundAndPackFloat32(zSign, zExp, zSig64, status);
    }
}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the double-precision floating-point values
| `a' and `b' then adding 'c', with no intermediate rounding step after the
| multiplication.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic 754-2008.
| The flags argument allows the caller to select negation of the
| addend, the intermediate product, or the final result. (The difference
| between this and having the caller do a separate negation is that negating
| externally will flip the sign bit on NaNs.)
*----------------------------------------------------------------------------*/

float64 float64_muladd(float64 a, float64 b, float64 c, int flags, struct float_status_t *status)
{
    int aSign, bSign, cSign, zSign;
    Bit16s aExp, bExp, cExp, pExp, zExp;
    Bit64u aSig, bSig, cSig;
    int pInf, pZero, pSign;
    Bit64u pSig0, pSig1, cSig0, cSig1, zSig0, zSig1;
    int shiftcount;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);
    bSig = extractFloat64Frac(b);
    bExp = extractFloat64Exp(b);
    bSign = extractFloat64Sign(b);
    cSig = extractFloat64Frac(c);
    cExp = extractFloat64Exp(c);
    cSign = extractFloat64Sign(c);

    /* It is implementation-defined whether the cases of (0,inf,qnan)
     * and (inf,0,qnan) raise InvalidOperation or not (and what QNaN
     * they return if they do), so we have to hand this information
     * off to the target-specific pick-a-NaN routine.
     */
    if (((aExp == 0x7ff) && aSig) ||
        ((bExp == 0x7ff) && bSig) ||
        ((cExp == 0x7ff) && cSig)) {
        return propagateFloat64MulAddNaN(a, b, c, status);
    }

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
        if (cExp == 0) cSig = 0;
    }

    int infzero = ((aExp == 0 && aSig == 0 && bExp == 0x7ff && bSig == 0) ||
                   (aExp == 0x7ff && aSig == 0 && bExp == 0 && bSig == 0));

    if (infzero) {
        float_raise(status, float_flag_invalid);
        return float64_default_nan;
    }

    if (flags & float_muladd_negate_c) {
        cSign ^= 1;
    }

    /* Work out the sign and type of the product */
    pSign = aSign ^ bSign;
    if (flags & float_muladd_negate_product) {
        pSign ^= 1;
    }
    pInf = (aExp == 0x7ff) || (bExp == 0x7ff);
    pZero = ((aExp | aSig) == 0) || ((bExp | bSig) == 0);

    if (cExp == 0x7ff) {
        if (pInf && (pSign ^ cSign)) {
            /* addition of opposite-signed infinities => InvalidOperation */
            float_raise(status, float_flag_invalid);
            return float64_default_nan;
        }
        /* Otherwise generate an infinity of the same sign */
        if ((aSig && aExp == 0) || (bSig && bExp == 0)) {
            float_raise(status, float_flag_denormal);
        }
        return packFloat64(cSign, 0x7ff, 0);
    }

    if (pInf) {
        if ((aSig && aExp == 0) || (bSig && bExp == 0) || (cSig && cExp == 0)) {
            float_raise(status, float_flag_denormal);
        }
        return packFloat64(pSign, 0x7ff, 0);
    }

    if (pZero) {
        if (cExp == 0) {
            if (cSig == 0) {
                /* Adding two exact zeroes */
                if (pSign == cSign) {
                    zSign = pSign;
                } else if (get_float_rounding_mode(status) == float_round_down) {
                    zSign = 1;
                } else {
                    zSign = 0;
                }
                return packFloat64(zSign, 0, 0);
            }
            /* Exact zero plus a denormal */
            float_raise(status, float_flag_denormal);
            if (get_flush_underflow_to_zero(status)) {
                float_raise(status, float_flag_underflow | float_flag_inexact);
                return packFloat64(cSign, 0, 0);
            }
        }
        /* Zero plus something non-zero */
        return packFloat64(cSign, cExp, cSig);
    }

    if (aExp == 0) {
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(bSig, &bExp, &bSig);
    }

    /* Calculate the actual result a * b + c */

    /* Multiply first; this is easy. */
    /* NB: we subtract 0x3fe where float64_mul() subtracts 0x3ff
     * because we want the true exponent, not the "one-less-than"
     * flavour that roundAndPackFloat64() takes.
     */
    pExp = aExp + bExp - 0x3fe;
    aSig = (aSig | BX_CONST64(0x0010000000000000))<<10;
    bSig = (bSig | BX_CONST64(0x0010000000000000))<<11;
    mul64To128(aSig, bSig, &pSig0, &pSig1);
    if ((Bit64s)(pSig0 << 1) >= 0) {
        shortShift128Left(pSig0, pSig1, 1, &pSig0, &pSig1);
        pExp--;
    }

    zSign = pSign;

    /* Now [pSig0:pSig1] is the significand of the multiply, with the explicit
     * bit in position 126.
     */
    if (cExp == 0) {
        if (!cSig) {
            /* Throw out the special case of c being an exact zero now */
            shift128RightJamming(pSig0, pSig1, 64, &pSig0, &pSig1);
            return roundAndPackFloat64(zSign, pExp - 1, pSig1, status);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(cSig, &cExp, &cSig);
    }

    cSig0 = cSig << 10;
    cSig1 = 0;
    cSig0 |= BX_CONST64(0x4000000000000000);
    int expDiff = pExp - cExp;

    if (pSign == cSign) {
        /* Addition */
        if (expDiff > 0) {
            /* scale c to match p */
            shift128RightJamming(cSig0, cSig1, expDiff, &cSig0, &cSig1);
            zExp = pExp;
        } else if (expDiff < 0) {
            /* scale p to match c */
            shift128RightJamming(pSig0, pSig1, -expDiff, &pSig0, &pSig1);
            zExp = cExp;
        } else {
            /* no scaling needed */
            zExp = cExp;
        }
        /* Add significands and make sure explicit bit ends up in posn 126 */
        add128(pSig0, pSig1, cSig0, cSig1, &zSig0, &zSig1);
        if ((Bit64s)zSig0 < 0) {
            shift128RightJamming(zSig0, zSig1, 1, &zSig0, &zSig1);
        } else {
            zExp--;
        }
        shift128RightJamming(zSig0, zSig1, 64, &zSig0, &zSig1);
        return roundAndPackFloat64(zSign, zExp, zSig1, status);
    } else {
        /* Subtraction */
        if (expDiff > 0) {
            shift128RightJamming(cSig0, cSig1, expDiff, &cSig0, &cSig1);
            sub128(pSig0, pSig1, cSig0, cSig1, &zSig0, &zSig1);
            zExp = pExp;
        } else if (expDiff < 0) {
            shift128RightJamming(pSig0, pSig1, -expDiff, &pSig0, &pSig1);
            sub128(cSig0, cSig1, pSig0, pSig1, &zSig0, &zSig1);
            zExp = cExp;
            zSign ^= 1;
        } else {
            zExp = pExp;
            if (lt128(cSig0, cSig1, pSig0, pSig1)) {
                sub128(pSig0, pSig1, cSig0, cSig1, &zSig0, &zSig1);
            } else if (lt128(pSig0, pSig1, cSig0, cSig1)) {
                sub128(cSig0, cSig1, pSig0, pSig1, &zSig0, &zSig1);
                zSign ^= 1;
            } else {
                /* Exact zero */
                return packFloat64(get_float_rounding_mode(status) == float_round_down, 0, 0);
            }
        }
        --zExp;
        /* Do the equivalent of normalizeRoundAndPackFloat64() but
         * starting with the significand in a pair of Bit64u.
         */
        if (zSig0) {
            shiftcount = countLeadingZeros64(zSig0) - 1;
            shortShift128Left(zSig0, zSig1, shiftcount, &zSig0, &zSig1);
            if (zSig1) {
                zSig0 |= 1;
            }
            zExp -= shiftcount;
        } else {
            shiftcount = countLeadingZeros64(zSig1) - 1;
            zSig0 = zSig1 << shiftcount;
            zExp -= (shiftcount + 64);
        }
        return roundAndPackFloat64(zSign, zExp, zSig0, status);
    }
}

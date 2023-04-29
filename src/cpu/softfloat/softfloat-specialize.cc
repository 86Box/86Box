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

#define FLOAT128

/*============================================================================
 * Adapted for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#include "softfloat.h"
#include "softfloat-specialize.h"

/*----------------------------------------------------------------------------
| Takes two single-precision floating-point values `a' and `b', one of which
| is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
| signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

float32 propagateFloat32NaN(float32 a, float32 b, struct float_status_t *status)
{
    int aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    aIsNaN = float32_is_nan(a);
    aIsSignalingNaN = float32_is_signaling_nan(a);
    bIsNaN = float32_is_nan(b);
    bIsSignalingNaN = float32_is_signaling_nan(b);
    a |= 0x00400000;
    b |= 0x00400000;
    if (aIsSignalingNaN | bIsSignalingNaN) float_raise(status, float_flag_invalid);
    if (get_float_nan_handling_mode(status) == float_larger_significand_nan) {
        if (aIsSignalingNaN) {
            if (bIsSignalingNaN) goto returnLargerSignificand;
            return bIsNaN ? b : a;
        }
        else if (aIsNaN) {
            if (bIsSignalingNaN | ! bIsNaN) return a;
      returnLargerSignificand:
            if ((Bit32u) (a<<1) < (Bit32u) (b<<1)) return b;
            if ((Bit32u) (b<<1) < (Bit32u) (a<<1)) return a;
            return (a < b) ? a : b;
        }
        else {
            return b;
        }
    } else {
        return (aIsSignalingNaN | aIsNaN) ? a : b;
    }
}

/*----------------------------------------------------------------------------
| Takes two double-precision floating-point values `a' and `b', one of which
| is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
| signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

float64 propagateFloat64NaN(float64 a, float64 b, struct float_status_t *status)
{
    int aIsNaN = float64_is_nan(a);
    int aIsSignalingNaN = float64_is_signaling_nan(a);
    int bIsNaN = float64_is_nan(b);
    int bIsSignalingNaN = float64_is_signaling_nan(b);
    a |= BX_CONST64(0x0008000000000000);
    b |= BX_CONST64(0x0008000000000000);
    if (aIsSignalingNaN | bIsSignalingNaN) float_raise(status, float_flag_invalid);
    if (get_float_nan_handling_mode(status) == float_larger_significand_nan) {
        if (aIsSignalingNaN) {
            if (bIsSignalingNaN) goto returnLargerSignificand;
            return bIsNaN ? b : a;
        }
        else if (aIsNaN) {
            if (bIsSignalingNaN | ! bIsNaN) return a;
      returnLargerSignificand:
            if ((Bit64u) (a<<1) < (Bit64u) (b<<1)) return b;
            if ((Bit64u) (b<<1) < (Bit64u) (a<<1)) return a;
            return (a < b) ? a : b;
        }
        else {
            return b;
        }
    } else {
        return (aIsSignalingNaN | aIsNaN) ? a : b;
    }
}

#ifdef FLOATX80

/*----------------------------------------------------------------------------
| Takes two extended double-precision floating-point values `a' and `b', one
| of which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

floatx80 propagateFloatx80NaN(floatx80 a, floatx80 b, struct float_status_t *status)
{
    int aIsNaN = floatx80_is_nan(a);
    int aIsSignalingNaN = floatx80_is_signaling_nan(a);
    int bIsNaN = floatx80_is_nan(b);
    int bIsSignalingNaN = floatx80_is_signaling_nan(b);
    a.fraction |= BX_CONST64(0xC000000000000000);
    b.fraction |= BX_CONST64(0xC000000000000000);
    if (aIsSignalingNaN | bIsSignalingNaN) float_raise(status, float_flag_invalid);
    if (aIsSignalingNaN) {
        if (bIsSignalingNaN) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if (aIsNaN) {
        if (bIsSignalingNaN | ! bIsNaN) return a;
 returnLargerSignificand:
        if (a.fraction < b.fraction) return b;
        if (b.fraction < a.fraction) return a;
        return (a.exp < b.exp) ? a : b;
    }
    else {
        return b;
    }
}

#endif /* FLOATX80 */

#ifdef FLOAT128

/*----------------------------------------------------------------------------
| Takes two quadruple-precision floating-point values `a' and `b', one of
| which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

float128 propagateFloat128NaN(float128 a, float128 b, struct float_status_t *status)
{
    int aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;
    aIsNaN = float128_is_nan(a);
    aIsSignalingNaN = float128_is_signaling_nan(a);
    bIsNaN = float128_is_nan(b);
    bIsSignalingNaN = float128_is_signaling_nan(b);
    a.hi |= BX_CONST64(0x0000800000000000);
    b.hi |= BX_CONST64(0x0000800000000000);
    if (aIsSignalingNaN | bIsSignalingNaN) float_raise(status, float_flag_invalid);
    if (aIsSignalingNaN) {
        if (bIsSignalingNaN) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if (aIsNaN) {
        if (bIsSignalingNaN | !bIsNaN) return a;
 returnLargerSignificand:
        if (lt128(a.hi<<1, a.lo, b.hi<<1, b.lo)) return b;
        if (lt128(b.hi<<1, b.lo, a.hi<<1, a.lo)) return a;
        return (a.hi < b.hi) ? a : b;
    }
    else {
        return b;
    }
}

/*----------------------------------------------------------------------------
| The pattern for a default generated quadruple-precision NaN.
*----------------------------------------------------------------------------*/
const float128 float128_default_nan =
    packFloat128(float128_default_nan_hi, float128_default_nan_lo);

#endif /* FLOAT128 */

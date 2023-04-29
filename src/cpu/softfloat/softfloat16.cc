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
 * Adapted for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#include "softfloat.h"

#ifdef FLOAT16

#include "softfloat-round-pack.h"
#include "softfloat-specialize.h"
#include "softfloat-macros.h"

/*----------------------------------------------------------------------------
| Determine half-precision floating-point number class
*----------------------------------------------------------------------------*/

float_class_t float16_class(float16 a)
{
   Bit16s aExp = extractFloat16Exp(a);
   Bit16u aSig = extractFloat16Frac(a);
   int  aSign = extractFloat16Sign(a);

   if(aExp == 0x1F) {
       if (aSig == 0)
           return (aSign) ? float_negative_inf : float_positive_inf;

       return (aSig & 0x200) ? float_QNaN : float_SNaN;
   }

   if(aExp == 0) {
       if (aSig == 0) return float_zero;
       return float_denormal;
   }

   return float_normalized;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the half-precision floating-point value
| `a' to the single-precision floating-point format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float32 float16_to_float32(float16 a, struct float_status_t *status)
{
    Bit16u aSig = extractFloat16Frac(a);
    Bit16s aExp = extractFloat16Exp(a);
    int aSign = extractFloat16Sign(a);

    if (aExp == 0x1F) {
        if (aSig) return commonNaNToFloat32(float16ToCommonNaN(a, status));
        return packFloat32(aSign, 0xFF, 0);
    }
    if (aExp == 0) {
        // ignore denormals_are_zeros flag
        if (aSig == 0) return packFloat32(aSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat16Subnormal(aSig, &aExp, &aSig);
        --aExp;
    }

    return packFloat32(aSign, aExp + 0x70, ((Bit32u) aSig)<<13);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the half-precision floating-point format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float16 float32_to_float16(float32 a, struct float_status_t *status)
{
    Bit32u aSig = extractFloat32Frac(a);
    Bit16s aExp = extractFloat32Exp(a);
    int aSign = extractFloat32Sign(a);

    if (aExp == 0xFF) {
        if (aSig) return commonNaNToFloat16(float32ToCommonNaN(a, status));
        return packFloat16(aSign, 0x1F, 0);
    }
    if (aExp == 0) {
        if (get_denormals_are_zeros(status)) aSig = 0;
        if (aSig == 0) return packFloat16(aSign, 0, 0);
        float_raise(status, float_flag_denormal);
    }

    aSig = shift32RightJamming(aSig, 9);
    Bit16u zSig = (Bit16u) aSig;
    if (aExp || zSig) {
        zSig |= 0x4000;
        aExp -= 0x71;
    }

    return roundAndPackFloat16(aSign, aExp, zSig, status);
}

#endif

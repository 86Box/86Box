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

#include "softfloat-specialize.h"

/*============================================================================
 * Adapted for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

const int16_t int16_indefinite = (int16_t) 0x8000;
const int32_t int32_indefinite = (int32_t) 0x80000000;
const int64_t int64_indefinite = (int64_t) BX_CONST64(0x8000000000000000);

const uint16_t uint16_indefinite = 0xffff;
const uint32_t uint32_indefinite = 0xffffffff;
const uint64_t uint64_indefinite = BX_CONST64(0xffffffffffffffff);

/*----------------------------------------------------------------------------
| Commonly used half-precision floating point constants
*----------------------------------------------------------------------------*/
const float16 float16_negative_inf  = 0xfc00;
const float16 float16_positive_inf  = 0x7c00;
const float16 float16_negative_zero = 0x8000;
const float16 float16_positive_zero = 0x0000;

/*----------------------------------------------------------------------------
| The pattern for a default generated half-precision NaN.
*----------------------------------------------------------------------------*/
const float16 float16_default_nan = 0xFE00;

/*----------------------------------------------------------------------------
| Commonly used single-precision floating point constants
*----------------------------------------------------------------------------*/
const float32 float32_negative_inf  = 0xff800000;
const float32 float32_positive_inf  = 0x7f800000;
const float32 float32_negative_zero = 0x80000000;
const float32 float32_positive_zero = 0x00000000;
const float32 float32_negative_one  = 0xbf800000;
const float32 float32_positive_one  = 0x3f800000;
const float32 float32_max_float     = 0x7f7fffff;
const float32 float32_min_float     = 0xff7fffff;

/*----------------------------------------------------------------------------
| The pattern for a default generated single-precision NaN.
*----------------------------------------------------------------------------*/
const float32 float32_default_nan   = 0xffc00000;

/*----------------------------------------------------------------------------
| Commonly used single-precision floating point constants
*----------------------------------------------------------------------------*/
const float64 float64_negative_inf  = BX_CONST64(0xfff0000000000000);
const float64 float64_positive_inf  = BX_CONST64(0x7ff0000000000000);
const float64 float64_negative_zero = BX_CONST64(0x8000000000000000);
const float64 float64_positive_zero = BX_CONST64(0x0000000000000000);
const float64 float64_negative_one  = BX_CONST64(0xbff0000000000000);
const float64 float64_positive_one  = BX_CONST64(0x3ff0000000000000);
const float64 float64_max_float     = BX_CONST64(0x7fefffffffffffff);
const float64 float64_min_float     = BX_CONST64(0xffefffffffffffff);

/*----------------------------------------------------------------------------
| The pattern for a default generated double-precision NaN.
*----------------------------------------------------------------------------*/
const float64 float64_default_nan = BX_CONST64(0xFFF8000000000000);

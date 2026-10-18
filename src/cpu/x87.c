#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define fplog 0
#include <math.h>
#include <float.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/pic.h>
#include "x86.h"
#include "x86_flags.h"
#include "x86_ops.h"
#include "x86seg_common.h"
#include "x87_sf.h"
#include "x87.h"
#include "386_common.h"
#include "softfloat3e/config.h"
#include "softfloat3e/fpu_trans.h"
#include "softfloat3e/specialize.h"
#include <86box/plat_unused.h>

uint32_t x87_pc_off;
uint32_t x87_op_off;
uint16_t x87_pc_seg;
uint16_t x87_op_seg;

#ifdef ENABLE_FPU_X87_LOG
int fpu_x87_do_log = ENABLE_FPU_X87_LOG;

void
fpu_x87_log(const char *fmt, ...)
{
    va_list ap;

    if (fpu_x87_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define fpu_x87_log(fmt, ...)
#endif

uint16_t
x87_gettag(void)
{
    uint16_t ret = 0;

    for (uint8_t c = 0; c < 8; c++) {
        if (cpu_state.tag[c] == TAG_EMPTY)
            ret |= X87_TAG_EMPTY << (c * 2);
        else if (cpu_state.tag[c] & TAG_UINT64)
            ret |= 2 << (c * 2);
        else if (cpu_state.ST[c] == 0.0 && !cpu_state.ismmx)
            ret |= X87_TAG_ZERO << (c * 2);
        else
            ret |= X87_TAG_VALID << (c * 2);
    }

    return ret;
}

void
x87_settag(uint16_t new_tag)
{
    for (uint8_t c = 0; c < 8; c++) {
        int tag = (new_tag >> (c * 2)) & 3;

        if (tag == X87_TAG_EMPTY)
            cpu_state.tag[c] = TAG_EMPTY;
        else if (tag == 2)
            cpu_state.tag[c] = TAG_VALID | TAG_UINT64;
        else
            cpu_state.tag[c] = TAG_VALID;
    }
}

static extFloat80_t
FPU_handle_NaN32_Func(extFloat80_t a, int aIsNaN, float32 b32, int bIsNaN, struct softfloat_status_t *status)
{
    int aIsSignalingNaN = extF80_isSignalingNaN(a);
    int bIsSignalingNaN = f32_isSignalingNaN(b32);

    if (aIsSignalingNaN | bIsSignalingNaN)
        softfloat_raiseFlags(status, softfloat_flag_invalid);

    // propagate QNaN to SNaN
    a = softfloat_propagateNaNExtF80UI(a.signExp, a.signif, 0, 0, status);

    if (aIsNaN & !bIsNaN)
        return a;

    // float32 is NaN so conversion will propagate SNaN to QNaN and raise
    // appropriate exception flags
    extFloat80_t b = f32_to_extF80(b32, status);

    if (aIsSignalingNaN) {
        if (bIsSignalingNaN)
            goto returnLargerSignificand;
        return bIsNaN ? b : a;
    } else if (aIsNaN) {
        if (bIsSignalingNaN)
            return a;
returnLargerSignificand:
        if (a.signif < b.signif)
            return b;
        if (b.signif < a.signif)
            return a;
        return (a.signExp < b.signExp) ? a : b;
    } else {
        return b;
    }
}

int
FPU_handle_NaN32(extFloat80_t a, float32 b, extFloat80_t *r, struct softfloat_status_t *status)
{
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    const floatx80 floatx80_default_nan =
        packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    if (extF80_isUnsupported(a)) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        *r = floatx80_default_nan;
        return 1;
    }

    int aIsNaN = extF80_isNaN(a);
    int bIsNaN = f32_isNaN(b);
    if (aIsNaN | bIsNaN) {
        *r = FPU_handle_NaN32_Func(a, aIsNaN, b, bIsNaN, status);
        return 1;
    }
    return 0;
}

static extFloat80_t
FPU_handle_NaN64_Func(extFloat80_t a, int aIsNaN, float64 b64, int bIsNaN, struct softfloat_status_t *status)
{
    int aIsSignalingNaN = extF80_isSignalingNaN(a);
    int bIsSignalingNaN = f64_isSignalingNaN(b64);

    if (aIsSignalingNaN | bIsSignalingNaN)
        softfloat_raiseFlags(status, softfloat_flag_invalid);

    // propagate QNaN to SNaN
    a = softfloat_propagateNaNExtF80UI(a.signExp, a.signif, 0, 0, status);

    if (aIsNaN & !bIsNaN)
        return a;

    // float64 is NaN so conversion will propagate SNaN to QNaN and raise
    // appropriate exception flags
    extFloat80_t b = f64_to_extF80(b64, status);

    if (aIsSignalingNaN) {
        if (bIsSignalingNaN)
            goto returnLargerSignificand;
        return bIsNaN ? b : a;
    } else if (aIsNaN) {
        if (bIsSignalingNaN)
            return a;
returnLargerSignificand:
        if (a.signif < b.signif)
            return b;
        if (b.signif < a.signif)
            return a;
        return (a.signExp < b.signExp) ? a : b;
    } else {
        return b;
    }
}

int
FPU_handle_NaN64(extFloat80_t a, float64 b, extFloat80_t *r, struct softfloat_status_t *status)
{
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    const extFloat80_t floatx80_default_nan =
        packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    if (extF80_isUnsupported(a)) {
        softfloat_raiseFlags(status, softfloat_flag_invalid);
        *r = floatx80_default_nan;
        return 1;
    }

    int aIsNaN = extF80_isNaN(a);
    int bIsNaN = f64_isNaN(b);
    if (aIsNaN | bIsNaN) {
        *r = FPU_handle_NaN64_Func(a, aIsNaN, b, bIsNaN, status);
        return 1;
    }
    return 0;
}

struct softfloat_status_t
i387cw_to_softfloat_status_word(uint16_t control_word)
{
    struct softfloat_status_t status;
    int                   precision = control_word & FPU_CW_PC;

    switch (precision) {
        case FPU_PR_32_BITS:
            status.extF80_roundingPrecision = 32;
            break;
        case FPU_PR_64_BITS:
            status.extF80_roundingPrecision = 64;
            break;
        case FPU_PR_80_BITS:
            status.extF80_roundingPrecision = 80;
            break;
        default:
            /* With the precision control bits set to 01 "(reserved)", a
               real CPU behaves as if the precision control bits were
               set to 11 "80 bits" */
            status.extF80_roundingPrecision = 80;
            break;
    }

    status.softfloat_exceptionFlags             = 0; // clear exceptions before execution
    status.softfloat_roundingMode               = (control_word & FPU_CW_RC) >> 10;
    status.softfloat_flush_underflow_to_zero    = 0;
    status.softfloat_suppressException          = 0;
    status.softfloat_exceptionMasks             = control_word & FPU_CW_Exceptions_Mask;
    status.softfloat_denormals_are_zeros        = 0;
    return status;
}

int
FPU_status_word_flags_fpu_compare(int float_relation)
{
    switch (float_relation) {
        case softfloat_relation_unordered:
            return (FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3);

        case softfloat_relation_greater:
            return 0;

        case softfloat_relation_less:
            return FPU_SW_C0;

        case softfloat_relation_equal:
            return FPU_SW_C3;

        default:
            break;
    }

    return (-1); // should never get here
}

void
FPU_write_eflags_fpu_compare(int float_relation)
{
    /* ZF, PF and CF are written, not ORed in */
    cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
    switch (float_relation) {
        case softfloat_relation_unordered:
            cpu_state.flags |= (Z_FLAG | P_FLAG | C_FLAG);
            break;

        case softfloat_relation_greater:
            break;

        case softfloat_relation_less:
            cpu_state.flags |= C_FLAG;
            break;

        case softfloat_relation_equal:
            cpu_state.flags |= Z_FLAG;
            break;

        default:
            break;
    }
}

static uint16_t
FPU_exception_record(uint16_t exceptions, int store)
{
    uint16_t status;
    uint16_t unmasked;

    /* Extract only the bits which we use to set the status word */
    exceptions &= FPU_SW_Exceptions_Mask;
    status = fpu_state.swd;

    unmasked = (exceptions & ~fpu_state.cwd) & FPU_CW_Exceptions_Mask;

    // if IE or DZ exception happen nothing else will be reported
    if (exceptions & (FPU_EX_Invalid | FPU_EX_Zero_Div)) {
        unmasked &= (FPU_EX_Invalid | FPU_EX_Zero_Div);
    }

    /* Set summary bits if exception isn't masked */
    if (unmasked) {
        fpu_state.swd |= (FPU_SW_Summary | FPU_SW_Backward);
    }

    if (exceptions & FPU_EX_Invalid) {
        // FPU_EX_Invalid cannot come with any other exception but x87 stack fault
        fpu_state.swd |= exceptions;
        if (exceptions & FPU_SW_Stack_Fault) {
            if (!(exceptions & FPU_SW_C1)) {
                /* This bit distinguishes over- from underflow for a stack fault,
                   and roundup from round-down for precision loss. */
                fpu_state.swd &= ~FPU_SW_C1;
            }
        }
        return unmasked;
    }

    if (exceptions & FPU_EX_Zero_Div) {
        fpu_state.swd |= FPU_EX_Zero_Div;
        return unmasked;
    }

    if (exceptions & FPU_EX_Denormal) {
        fpu_state.swd |= FPU_EX_Denormal;
        if (unmasked & FPU_EX_Denormal) {
            return (unmasked & FPU_EX_Denormal);
        }
    }

    /* Set the corresponding exception bits */
    fpu_state.swd |= exceptions;

    if (exceptions & FPU_EX_Precision) {
        if (!(exceptions & FPU_SW_C1)) {
            /* This bit distinguishes over- from underflow for a stack fault,
                 and roundup from round-down for precision loss. */
            fpu_state.swd &= ~FPU_SW_C1;
        }
    }

    // If #P unmasked exception occurred the result still has to be
    // written to the destination.
    unmasked &= ~FPU_EX_Precision;

    if (unmasked & (FPU_EX_Underflow | FPU_EX_Overflow)) {
        // If unmasked over- or underflow occurs and dest is a memory location:
        //   - the TOS and destination operands remain unchanged
        //   - the inexact-result condition is not reported and C1 flag is cleared
        //   - no result is stored in the memory
        // If the destination is in the register stack, adjusted resulting value
        // is stored in the destination operand.
        if (!store)
            unmasked &= ~(FPU_EX_Underflow | FPU_EX_Overflow);
        else {
            fpu_state.swd &= ~FPU_SW_C1;
            if (!(status & FPU_EX_Precision))
                fpu_state.swd &= ~FPU_EX_Precision;
        }
    }
    return unmasked;
}

/* An exception the instruction raised: its flags, ES and B with an
   unmasked one, and FERR# for it as the processor asserts it
   (x87_ferr_raised). A store whose result is still written (its
   exceptions masked, or only precision unmasked) raises it once the write
   is done (FPU_store_done): a faulting write leaves the status as it was. */
uint16_t
FPU_exception(UNUSED(uint32_t fetchdat), uint16_t exceptions, int store)
{
    const int was_es = fpu_state.swd & FPU_SW_Summary;
    uint16_t  raised = (exceptions & FPU_SW_Exceptions_Mask) & ~fpu_state.cwd & FPU_CW_Exceptions_Mask;
    uint16_t  ret;

    if (exceptions & (FPU_EX_Invalid | FPU_EX_Zero_Div))
        raised &= (FPU_EX_Invalid | FPU_EX_Zero_Div);
    x87_ferr_store_hold(0, 0);
    ret = FPU_exception_record(exceptions, store);
    if (raised) {
        if (store && !ret)
            x87_ferr_store_hold(was_es, raised);
        else
            x87_ferr_raised(was_es, raised, x87_op);
    }
    return ret;
}

void
FPU_stack_overflow(uint32_t fetchdat)
{
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    const floatx80 floatx80_default_nan =
        packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    /* The masked response */
    if (is_IA_masked()) {
        FPU_push();
        FPU_save_regi(floatx80_default_nan, 0);
    }
    FPU_exception(fetchdat, FPU_EX_Stack_Overflow, 0);
}

void
FPU_stack_underflow(uint32_t fetchdat, int stnr, int pop_stack)
{
/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
    const floatx80 floatx80_default_nan =
        packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    /* The masked response */
    if (is_IA_masked()) {
        FPU_save_regi(floatx80_default_nan, stnr);
        if (pop_stack)
            FPU_pop();
    }
    FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
}

/* -----------------------------------------------------------
 * Slimmed down version used to compile against a CPU simulator
 * rather than a kernel (ported by Kevin Lawton)
 * ------------------------------------------------------------ */
int
FPU_tagof(const extFloat80_t reg)
{
    int32_t exp = extF80_exp(reg);
    if (exp == 0) {
        if (!extF80_fraction(reg))
            return X87_TAG_ZERO;

        /* The number is a de-normal or pseudodenormal. */
        return X87_TAG_INVALID;
    }

    if (exp == 0x7fff) {
        /* Is an Infinity, a NaN, or an unsupported data type. */
        return X87_TAG_INVALID;
    }

    if (!(reg.signif & BX_CONST64(0x8000000000000000))) {
        /* Unsupported data type. */
        /* Valid numbers have the ms bit set to 1. */
        return X87_TAG_INVALID;
    }

    return X87_TAG_VALID;
}

uint8_t
pack_FPU_TW(uint16_t twd)
{
    uint8_t tag_byte = 0;

    if ((twd & 0x0003) != 0x0003)
        tag_byte |= 0x01;
    if ((twd & 0x000c) != 0x000c)
        tag_byte |= 0x02;
    if ((twd & 0x0030) != 0x0030)
        tag_byte |= 0x04;
    if ((twd & 0x00c0) != 0x00c0)
        tag_byte |= 0x08;
    if ((twd & 0x0300) != 0x0300)
        tag_byte |= 0x10;
    if ((twd & 0x0c00) != 0x0c00)
        tag_byte |= 0x20;
    if ((twd & 0x3000) != 0x3000)
        tag_byte |= 0x40;
    if ((twd & 0xc000) != 0xc000)
        tag_byte |= 0x80;

    return tag_byte;
}

uint16_t
unpack_FPU_TW(uint16_t tag_byte)
{
    uint32_t twd = 0;

    /*                                 FTW
     *
     * Note that the original format for FTW can be recreated from the stored
     * FTW valid bits and the stored 80-bit FP data (assuming the stored data
     * was not the contents of MMX registers) using the following table:

       | Exponent | Exponent | Fraction | J,M bits | FTW valid | x87 FTW |
       |  all 1s  |  all 0s  |  all 0s  |          |           |         |
       -------------------------------------------------------------------
       |    0     |    0     |    0     |    0x    |     1     | S    10 |
       |    0     |    0     |    0     |    1x    |     1     | V    00 |
       -------------------------------------------------------------------
       |    0     |    0     |    1     |    00    |     1     | S    10 |
       |    0     |    0     |    1     |    10    |     1     | V    00 |
       -------------------------------------------------------------------
       |    0     |    1     |    0     |    0x    |     1     | S    10 |
       |    0     |    1     |    0     |    1x    |     1     | S    10 |
       -------------------------------------------------------------------
       |    0     |    1     |    1     |    00    |     1     | Z    01 |
       |    0     |    1     |    1     |    10    |     1     | S    10 |
       -------------------------------------------------------------------
       |    1     |    0     |    0     |    1x    |     1     | S    10 |
       |    1     |    0     |    0     |    1x    |     1     | S    10 |
       -------------------------------------------------------------------
       |    1     |    0     |    1     |    00    |     1     | S    10 |
       |    1     |    0     |    1     |    10    |     1     | S    10 |
       -------------------------------------------------------------------
       |        all combinations above             |     0     | E    11 |

     *
     * The J-bit is defined to be the 1-bit binary integer to the left of
     * the decimal place in the significand.
     *
     * The M-bit is defined to be the most significant bit of the fractional
     * portion of the significand (i.e., the bit immediately to the right of
     * the decimal place). When the M-bit is the most significant bit of the
     * fractional portion  of the significand, it must be  0 if the fraction
     * is all 0's.
     */

    for (int index = 7; index >= 0; index--, twd <<= 2, tag_byte <<= 1) {
        if (tag_byte & 0x80)
            twd |= FPU_tagof(fpu_state.st_space[index & 7]);
        else
            twd |= X87_TAG_EMPTY;
    }

    return (twd >> 2);
}

#ifdef ENABLE_FPU_X87_LOG
void
x87_dumpregs(void)
{
    if (cpu_state.ismmx) {
        fpu_x87_log("MM0=%016llX\tMM1=%016llX\tMM2=%016llX\tMM3=%016llX\n", cpu_state.MM[0].q, cpu_state.MM[1].q, cpu_state.MM[2].q, cpu_state.MM[3].q);
        fpu_x87_log("MM4=%016llX\tMM5=%016llX\tMM6=%016llX\tMM7=%016llX\n", cpu_state.MM[4].q, cpu_state.MM[5].q, cpu_state.MM[6].q, cpu_state.MM[7].q);
    } else {
        fpu_x87_log("ST(0)=%f\tST(1)=%f\tST(2)=%f\tST(3)=%f\t\n", cpu_state.ST[cpu_state.TOP], cpu_state.ST[(cpu_state.TOP + 1) & 7], cpu_state.ST[(cpu_state.TOP + 2) & 7], cpu_state.ST[(cpu_state.TOP + 3) & 7]);
        fpu_x87_log("ST(4)=%f\tST(5)=%f\tST(6)=%f\tST(7)=%f\t\n", cpu_state.ST[(cpu_state.TOP + 4) & 7], cpu_state.ST[(cpu_state.TOP + 5) & 7], cpu_state.ST[(cpu_state.TOP + 6) & 7], cpu_state.ST[(cpu_state.TOP + 7) & 7]);
    }
    fpu_x87_log("Status = %04X  Control = %04X  Tag = %04X\n", cpu_state.npxs, cpu_state.npxc, x87_gettag());
}
#endif

/* The common divide of x87_legacy_divide without the software arithmetic:
   two operands well inside the double range, round to nearest. The host
   quotient q is rounded once to 53 bits, and r = num - q * den is exact, so
   x - q = r / den places the true quotient x against the grid of each
   precision. An exact quotient is exact at every precision (its odd part is
   no wider than the numerator's), so PE is r != 0 throughout. At 64 bits
   the result is the software divide's double rounding, which differs from
   q only when the 64-bit value lands on a 53-bit midpoint; at 24 bits, only
   when q is a 24-bit midpoint. Those, near-ties and a power-of-two q (whose
   lower neighbours are closer) go to the software divide: 0 is returned.
   C1 is the round-up of the rounding at the control word's precision.*/
static int
x87_divide_fast(double num, double den, double *res, unsigned *flags)
{
#if defined(FLT_EVAL_METHOD) && (FLT_EVAL_METHOD != 0)
    return 0;
#else
    union { double d; uint64_t u; } a = { .d = num }, b = { .d = den }, q, ulp;
    const unsigned ea = (unsigned) (a.u >> 52) & 0x7ff;
    const unsigned eb = (unsigned) (b.u >> 52) & 0x7ff;
    double r;
    int    up;

    if ((cpu_state.npxc & 0xc00) || (ea < 553) || (ea > 1493) || (eb < 553) || (eb > 1493))
        return 0;
    q.d = num / den;
    if (!(q.u & UINT64_C(0x000fffffffffffff)))
        return 0;
#    if defined(FP_FAST_FMA) || defined(__FP_FAST_FMA)
    r = fma(-q.d, den, num);
#    else
    {
        /* Dekker's exact product q * den = p + e, by halves of 26 bits */
        const double split = 134217729.0;
        double       t, qh, ql, bh, bl, p, e;

        t  = split * q.d;
        qh = t - (t - q.d);
        ql = q.d - qh;
        t  = split * den;
        bh = t - (t - den);
        bl = den - bh;
        p  = q.d * den;
        e  = ((qh * bh - p) + qh * bl + ql * bh) + ql * bl;
        r  = (num - p) - e;
    }
#    endif
    /* |x| > |q|: x - q has the quotient's sign */
    up = (r != 0.0) && (signbit(r) != signbit(num));

    switch (cpu_state.npxc & 0x300) {
        case 0x000: /* 24 bits */
            {
                const float f = (float) q.d;

                if ((r != 0.0) && ((q.u & 0x1fffffff) == 0x10000000))
                    return 0;
                if (!isnormal(f))
                    return 0;
                *res = (double) f;
                if (*res != q.d) {
                    *flags = FPU_SW_Precision | ((fabs(*res) > fabs(q.d)) ? FPU_SW_C1 : 0);
                    return 1;
                }
                break;
            }
        case 0x200: /* 53 bits */
            *res = q.d;
            break;
        case 0x300: /* 64 bits */
            *res = q.d;
            if (r != 0.0) {
                /* x - q in units of the 64-bit grid, which q is on */
                double t, n, d;

                ulp.u = (q.u & UINT64_C(0x7ff0000000000000)) - (UINT64_C(63) << 52);
                t     = (r / den) / ulp.d;
                n     = nearbyint(t);
                d     = fabs(t - n);
                if ((fabs(n) >= 1024.0) || (d < 0x1p-20) || (d > (0.5 - 0x1p-20)))
                    return 0;
                /* the 64-bit rounding went up in magnitude: x and q + n * ulp
                   on the same side of zero, x nearer */
                up = (signbit(t - n) != signbit(q.d));
            }
            break;
        default:
            return 0;
    }
    *flags = (r != 0.0) ? (FPU_SW_Precision | (up ? FPU_SW_C1 : 0)) : 0;
    return 1;
#endif
}

/* The legacy register file is double based, but guest exception classes
   still follow x87 rules. Use the existing software arithmetic rather than
   treating every zero denominator as divide-by-zero. Unmasked exceptions
   suppress the result; delivery waits for the next waiting instruction. */
int
x87_legacy_divide(double *dest, double numerator, double denominator, int empty)
{
    union { double d; uint64_t u; } a = { .d = numerator }, b = { .d = denominator }, result;
    struct softfloat_status_t status = i387cw_to_softfloat_status_word(cpu_state.npxc);
    unsigned fast_flags = 0;
    cpu_state.npxs &= ~FPU_SW_C1;
    if (!empty && x87_divide_fast(numerator, denominator, &result.d, &fast_flags))
        status.softfloat_exceptionFlags = fast_flags;
    else if (empty & 1) {
        cpu_state.npxs |= FPU_SW_Stack_Fault | FPU_SW_Invalid;
        result.u = UINT64_C(0xfff8000000000000);
        status.softfloat_exceptionFlags = FPU_SW_Invalid;
    } else {
        extFloat80_t x = f64_to_extF80(a.u, &status);
        extFloat80_t y = f64_to_extF80(b.u, &status);
        /* A subnormal double is normal in the x87 extended register
           format. DE applies to a denormal memory operand, not to the
           legacy host representation of a register. Preserve SNaN IE. */
        status.softfloat_exceptionFlags &= FPU_SW_Invalid;
        if (empty & 2)
            status.softfloat_exceptionFlags |= FPU_SW_Denormal_Op;
        extFloat80_t q = extF80_div(x, y, &status);
        /* Converting the representation back must not invent guest x87
           exceptions caused solely by the legacy double register file. */
        struct softfloat_status_t conversion = i387cw_to_softfloat_status_word(cpu_state.npxc);
        result.u = extF80_to_f64(q, &conversion);
    }
    unsigned exceptions = status.softfloat_exceptionFlags & (0x3f | FPU_SW_C1);
    if (exceptions & FPU_SW_Invalid)
        exceptions &= FPU_SW_Invalid;
    else if (exceptions & FPU_SW_Zero_Div)
        exceptions = FPU_SW_Zero_Div;
    else if ((exceptions & FPU_SW_Denormal_Op) && !(cpu_state.npxc & FPU_SW_Denormal_Op))
        exceptions = FPU_SW_Denormal_Op;
    cpu_state.npxs |= exceptions;
    unsigned unmasked = exceptions & ~cpu_state.npxc & 0x3f;
    if (unmasked) {
        const int was_es = cpu_state.npxs & FPU_SW_Summary;

        cpu_state.npxs |= 0x8080;
        x87_ferr_raised(was_es, unmasked, x87_op);
        /* Unlike pre-computation faults, unmasked precision still commits
           the rounded arithmetic result (Intel Vol. 1, inexact result). */
        if (unmasked & ~FPU_SW_Precision)
            return 1;
    }
    *dest = result.d;
    return 0;
}

int
x87_legacy_pending(void)
{
    if (!(cpu_state.npxs & FPU_SW_Summary)) {
        fpu_ignne = 0;
        return 0;
    }
    if (cr0 & 0x20) {
        cpu_state.pc = cpu_state.oldpc;
        new_ne = 1;
        return 1;
    }
    if (!fpu_ignne) {
        cpu_state.pc = cpu_state.oldpc;
        x87_ferr_assert();
        return 1;
    }
    return 0;
}

int
x87_legacy_pending_entry(void)
{
    /* The ordinary entry check gives #NM priority over pending #MF. */
    return (cr0 & 0xc) ? 0 : x87_legacy_pending();
}

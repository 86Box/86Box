/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The legacy FPU's integer conversions and the status its stores
 *          leave (x87_ops.h; the FIST tests include it alone).
 */
#ifndef EMU_X87_OPS_FIST_H
#define EMU_X87_OPS_FIST_H

/* Round per RC in the double domain, so a value out of the integer's
   range (or a NaN) is seen before any conversion: FIST then stores the
   integer indefinite, the most negative value */
static __inline double
x87_fround_rc(double b)
{
    double da;
    double diff;

    switch ((cpu_state.npxc >> 10) & 3) {
        case 0: /*Nearest, ties to even*/
            da   = floor(b);
            diff = b - da;
            if (diff > 0.5)
                return da + 1.0;
            if (diff < 0.5)
                return da;
            return (fmod(da, 2.0) == 0.0) ? da : (da + 1.0);
        case 1: /*Down*/
            return floor(b);
        case 2: /*Up*/
            return ceil(b);
        default: /*Chop*/
            return trunc(b);
    }
}

static __inline int16_t
x87_fround16(double b)
{
    double r = x87_fround_rc(b);

    if (isnan(r) || (r < -32768.0) || (r > 32767.0))
        return (int16_t) 0x8000;
    return (int16_t) r;
}

static __inline int64_t
x87_fround16_64(double b)
{
    return (int64_t) x87_fround16(b);
}

static __inline int32_t
x87_fround32(double b)
{
    double r = x87_fround_rc(b);

    if (isnan(r) || (r < -2147483648.0) || (r > 2147483647.0))
        return (int32_t) 0x80000000;
    return (int32_t) r;
}

static __inline int64_t
x87_fround32_64(double b)
{
    return (int64_t) x87_fround32(b);
}

static __inline int64_t
x87_fround_nearest(double b)
{
    double da, dc;
    int64_t a, c;
    
    da = floor(b);
    dc = floor(b + 1.0);
    a = (int64_t) da;
    c = (int64_t) dc;
    if ((b - a) < (c - b))
        return a;
    else if ((b - a) > (c - b))
        return c;
    else
        return (a & 1) ? c : a;
}

/* An unmasked exception of the instruction: ES and B, and FERR# for it as
   the processor asserts it (x87_ferr_raised) */
static __inline void
x87_raise_es(uint16_t unmasked)
{
    const int was_es = cpu_state.npxs & FPU_SW_Summary;

    cpu_state.npxs |= 0x8080;
    x87_ferr_raised(was_es, unmasked, x87_op);
}

/* FIST and FISTP of a NaN or a value that rounds outside [lo, hi_ex): an
   invalid operation ("#IA ... source operand is too large for the
   destination format, ... or a NaN", SDM 1999, FIST). Unmasked, nothing is
   stored or popped, IE and ES are set and C1 cleared (answers -1); masked,
   the conversion stores the integer indefinite and IE is set once it is
   (answers 1, for x87_store_done); 0 for a value in range. */
static __inline int
x87_fist_invalid(double b, double lo, double hi_ex)
{
    double r = x87_fround_rc(b);

    if (!isnan(r) && (r >= lo) && (r < hi_ex))
        return 0;
    if (cpu_state.npxc & FPU_SW_Invalid)
        return 1;
    {
        const int was_es = cpu_state.npxs & FPU_SW_Summary;

        cpu_state.npxs = (cpu_state.npxs & ~FPU_SW_C1) | FPU_SW_Invalid | 0x8080;
        x87_ferr_raised(was_es, FPU_SW_Invalid, x87_op);
    }
    return -1;
}

/* The status a store leaves once its write is done (a faulting write leaves
   it as it was): C1 clear, "0 = not roundup ... Cleared to 0 otherwise"
   (SDM 1999, FST and FIST), and IE for the masked invalid operation whose
   indefinite it stored. */
static __inline void
x87_store_done(int invalid)
{
    cpu_state.npxs = (cpu_state.npxs & ~FPU_SW_C1) | (invalid ? FPU_SW_Invalid : 0);
}

static __inline int64_t
x87_fround(double b)
{
    double r = x87_fround_rc(b);

    if (isnan(r) || (r < -9223372036854775808.0) || (r >= 9223372036854775808.0))
        return (int64_t) 0x8000000000000000ULL;
    return (int64_t) r;
}

#endif

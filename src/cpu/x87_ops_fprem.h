/* FPREM (quotient chopped) and FPREM1 (rounded to nearest): with an
   exponent difference below 64 the remainder is complete and the
   quotient's low three bits go to C0, C3 and C1; from 64 on it is partial,
   reduced by ST(1) * 2^(D - N) with N = (D & 1Fh) | 20h as the softfloat
   core does, and C2 is set for the next pass */
static int
x87_fprem_common(int nearest)
{
    double a, b;
    int    q = 0;

    if (X87_STACK_FAULTS && X87_ST_EMPTY2(0, 1)) {
        cpu_state.npxs &= ~FPU_SW_C2;
        x87_stack_underflow(0, 0);
        return 1;
    }
    a = ST(0);
    b = ST(1);

    cpu_state.npxs &= ~(FPU_SW_C0 | FPU_SW_C1 | FPU_SW_C2 | FPU_SW_C3);
    if (isnan(a) || isnan(b) || isinf(a) || (b == 0.0)) {
        cpu_state.npxs |= FPU_SW_Invalid;
        /* Unmasked, the operands stay and ES is set (SDM 1999, FPREM: #IA) */
        if (!(cpu_state.npxc & FPU_SW_Invalid)) {
            x87_raise_es(FPU_SW_Invalid);
            return 0;
        }
        ST(0) = -NAN;
        return 0;
    }
    if ((a != 0.0) && !isinf(b)) {
        x87_td va = { .d = a }, vb = { .d = b };
        int ea = (int) ((va.i >> 52) & 0x7ff);
        int eb = (int) ((vb.i >> 52) & 0x7ff);
        /* Normal operands have the same exponent bias; subnormals need normalization. */
        int d = (ea && eb) ? ea - eb : ilogb(a) - ilogb(b);
        if (d >= 64) {
            int n = (d & 0x1f) | 0x20;
            ST(0) = fmod(a, ldexp(b, d - n));
            cpu_state.npxs |= FPU_SW_C2;
            return 0;
        }
    }
    if (nearest)
        ST(0) = remquo(a, b, &q);
    else {
        double rt = fmod(a, b);
        double rn = remquo(a, b, &q);
        q         = (q < 0) ? -q : q;
        if (rt != rn)
            q = (q - 1) & 7; /* remquo need only return three quotient bits */
        ST(0) = rt;
    }
    q = (q < 0) ? -q : q;
    if (q & 4)
        cpu_state.npxs |= FPU_SW_C0;
    if (q & 2)
        cpu_state.npxs |= FPU_SW_C3;
    if (q & 1)
        cpu_state.npxs |= FPU_SW_C1;
    return 0;
}

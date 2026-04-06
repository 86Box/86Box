/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opCVTPI2PS_xmm_mm_a16(uint32_t fetchdat)
{
    MMX_REG src;

    feclearexcept(FE_ALL_EXCEPT);
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    MMX_GETSRC();
    fesetround(rounding_modes[(mxcsr >> 13) & 3]);
    XMM[cpu_reg].f[0] = src.l[0];
    XMM[cpu_reg].f[1] = src.l[1];
    fesetround(FE_TONEAREST);
    check_sse_exceptions(XMM[cpu_reg].f[0]);
    check_sse_exceptions(XMM[cpu_reg].f[1]);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTPI2PS_xmm_mm_a32(uint32_t fetchdat)
{
    MMX_REG src;

    feclearexcept(FE_ALL_EXCEPT);
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    MMX_GETSRC();
    fesetround(rounding_modes[(mxcsr >> 13) & 3]);
    XMM[cpu_reg].f[0] = src.l[0];
    XMM[cpu_reg].f[1] = src.l[1];
    fesetround(FE_TONEAREST);
    check_sse_exceptions(XMM[cpu_reg].f[0]);
    check_sse_exceptions(XMM[cpu_reg].f[1]);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTSI2SS_xmm_l_a16(uint32_t fetchdat)
{
    feclearexcept(FE_ALL_EXCEPT);
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        fesetround(rounding_modes[(mxcsr >> 13) & 3]);
        XMM[cpu_reg].f[0] = getr32(cpu_rm);
        fesetround(FE_TONEAREST);
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        fesetround(rounding_modes[(mxcsr >> 13) & 3]);
        XMM[cpu_reg].f[0] = dst;
        fesetround(FE_TONEAREST);
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opCVTSI2SS_xmm_l_a32(uint32_t fetchdat)
{
    feclearexcept(FE_ALL_EXCEPT);
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        fesetround(rounding_modes[(mxcsr >> 13) & 3]);
        XMM[cpu_reg].f[0] = getr32(cpu_rm);
        fesetround(FE_TONEAREST);
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        fesetround(rounding_modes[(mxcsr >> 13) & 3]);
        XMM[cpu_reg].f[0] = dst;
        fesetround(FE_TONEAREST);
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opCVTTPS2PI_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG *dst;

    feclearexcept(FE_ALL_EXCEPT);
    MMX_ENTER();
    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();
    dst->l[0] = trunc(src.f[0]);
    dst->l[1] = trunc(src.f[1]);
    check_sse_exceptions(src.f[0]);
    check_sse_exceptions(src.f[1]);
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTTPS2PI_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG *dst;

    feclearexcept(FE_ALL_EXCEPT);
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();
    dst->l[0] = trunc(src.f[0]);
    dst->l[1] = trunc(src.f[1]);
    check_sse_exceptions(src.f[0]);
    check_sse_exceptions(src.f[1]);
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTTSS2SI_l_xmm_a16(uint32_t fetchdat)
{
    feclearexcept(FE_ALL_EXCEPT);
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        setr32(cpu_reg, trunc(XMM[cpu_rm].f[0]));
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float dst_real;
        dst_real = *(float *) &dst;
        setr32(cpu_reg, trunc(dst_real));
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opCVTTSS2SI_l_xmm_a32(uint32_t fetchdat)
{
    feclearexcept(FE_ALL_EXCEPT);
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        setr32(cpu_reg, trunc(XMM[cpu_rm].f[0]));
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float dst_real;
        dst_real = *(float *) &dst;
        setr32(cpu_reg, trunc(dst_real));
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opCVTPS2PI_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG *dst;

    feclearexcept(FE_ALL_EXCEPT);
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();
    fesetround(rounding_modes[(mxcsr >> 13) & 3]);
    if (src.f[0] > 2147483647.0) {
        dst->l[0] = 0x80000000;
        mxcsr |= 1;
    } else
        dst->l[0] = src.f[0];
    if (src.f[1] > 2147483647.0) {
        dst->l[1] = 0x80000000;
        mxcsr |= 1;
    } else
        dst->l[1] = src.f[1];
    fesetround(FE_TONEAREST);
    check_sse_exceptions(src.f[0]);
    check_sse_exceptions(src.f[1]);
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTPS2PI_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG *dst;

    feclearexcept(FE_ALL_EXCEPT);
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();
    fesetround(rounding_modes[(mxcsr >> 13) & 3]);
    if (src.f[0] > 2147483647.0) {
        dst->l[0] = 0x80000000;
        mxcsr |= 1;
    } else
        dst->l[0] = src.f[0];
    if (src.f[1] > 2147483647.0) {
        dst->l[1] = 0x80000000;
        mxcsr |= 1;
    } else
        dst->l[1] = src.f[1];
    fesetround(FE_TONEAREST);
    check_sse_exceptions(src.f[0]);
    check_sse_exceptions(src.f[1]);
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTSS2SI_l_xmm_a16(uint32_t fetchdat)
{
    feclearexcept(FE_ALL_EXCEPT);
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        fesetround(rounding_modes[(mxcsr >> 13) & 3]);
        setr32(cpu_reg, XMM[cpu_rm].f[0]);
        fesetround(FE_TONEAREST);
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float dst_real;
        dst_real = *(float *) &dst;
        fesetround(rounding_modes[(mxcsr >> 13) & 3]);
        setr32(cpu_reg, dst_real);
        fesetround(FE_TONEAREST);
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opCVTSS2SI_l_xmm_a32(uint32_t fetchdat)
{
    feclearexcept(FE_ALL_EXCEPT);
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        fesetround(rounding_modes[(mxcsr >> 13) & 3]);
        setr32(cpu_reg, XMM[cpu_rm].f[0]);
        fesetround(FE_TONEAREST);
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float dst_real;
        dst_real = *(float *) &dst;
        fesetround(rounding_modes[(mxcsr >> 13) & 3]);
        setr32(cpu_reg, dst_real);
        fesetround(FE_TONEAREST);
        check_sse_exceptions(XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(2);
    }

    return 0;
}

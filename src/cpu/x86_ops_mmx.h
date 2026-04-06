#define SSATB(val)     (((val) < -128) ? -128 : (((val) > 127) ? 127 : (val)))
#define SSATW(val)     (((val) < -32768) ? -32768 : (((val) > 32767) ? 32767 : (val)))
#define USATB(val)     (((val) < 0) ? 0 : (((val) > 255) ? 255 : (val)))
#define USATW(val)     (((val) < 0) ? 0 : (((val) > 65535) ? 65535 : (val)))

#define MMX_GETREGP(r) MMP[r]
#define MMX_GETREG(r)  *(MMP[r])

#define MMX_SETEXP(r) \
    *(MMEP[r]) = 0xffff

#define MMX_GETSRC()                               \
    if (cpu_mod == 3) {                            \
        src = MMX_GETREG(cpu_rm);                  \
        CLOCK_CYCLES(1);                           \
    } else {                                       \
        SEG_CHECK_READ(cpu_state.ea_seg);          \
        src.q = readmemq(easeg, cpu_state.eaaddr); \
        if (cpu_state.abrt)                        \
            return 1;                              \
        CLOCK_CYCLES(2);                           \
    }

#define SSE_GETSRC()                                      \
    if (cpu_mod == 3) {                                   \
        src = XMM[cpu_rm];                                \
        CLOCK_CYCLES(1);                                  \
    } else {                                              \
        SEG_CHECK_READ(cpu_state.ea_seg);                 \
        src.q[0] = readmemq(easeg, cpu_state.eaaddr);     \
        if (cpu_state.abrt)                               \
            return 1;                                     \
        src.q[1] = readmemq(easeg, cpu_state.eaaddr + 8); \
        if (cpu_state.abrt)                               \
            return 1;                                     \
        CLOCK_CYCLES(2);                                  \
    }

#define MMX_ENTER()                          \
    if (!cpu_has_feature(CPU_FEATURE_MMX) || (cr0 & 0x4)) { \
        cpu_state.pc = cpu_state.oldpc;      \
        x86illegal();                        \
        return 1;                            \
    }                                        \
    if (cr0 & 0x8) {                         \
        x86_int(7);                          \
        return 1;                            \
    }                                        \
    x87_set_mmx()

static int
opEMMS(UNUSED(uint32_t fetchdat))
{
    if (!cpu_has_feature(CPU_FEATURE_MMX)) {
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 1;
    }
    if (cr0 & 0xc) {
        x86_int(7);
        return 1;
    }
    x87_emms();
    CLOCK_CYCLES(100); /*Guess*/
    return 0;
}

static inline int
check_sse_exceptions(double result)
{
    int fperaised = fetestexcept(FE_ALL_EXCEPT);
    if (fperaised & FE_INVALID)
        mxcsr |= 1;
    if (fpclassify(result) == FP_SUBNORMAL)
        mxcsr |= 2;
    if (fperaised & FE_DIVBYZERO)
        mxcsr |= 4;
    if (fperaised & FE_OVERFLOW)
        mxcsr |= 8;
    if (fperaised & FE_UNDERFLOW)
        mxcsr |= 0x10;
    if (fperaised & FE_INEXACT)
        mxcsr |= 0x20;

    int unmasked = ~((mxcsr >> 7) & 0x3f);
    if ((mxcsr & 0x3f) && (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_doabrt(0x13);
        ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }
    return 0;
}

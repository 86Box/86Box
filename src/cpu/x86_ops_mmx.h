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
        CHECK_READ_EA(8);                          \
        src.q = readmemq(easeg, cpu_state.eaaddr); \
        if (cpu_state.abrt)                        \
            return 1;                              \
        CLOCK_CYCLES(2);                           \
    }

/* PUNPCKL*: only the low doubleword of a memory source is read */
#define MMX_GETSRC_LOW32()                                 \
    if (cpu_mod == 3) {                                    \
        src = MMX_GETREG(cpu_rm);                          \
        CLOCK_CYCLES(1);                                   \
    } else {                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                  \
        CHECK_READ_EA(4);                                  \
        src.l[0] = readmeml(easeg, cpu_state.eaaddr);      \
        src.l[1] = 0;                                      \
        if (cpu_state.abrt)                                \
            return 1;                                      \
        CLOCK_CYCLES(2);                                   \
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
    if (fpu_softfloat) {                     \
        FPU_check_pending_exceptions();      \
    } else if (x87_legacy_pending())         \
        return 1;                            \
    /* Resetting a non-zero TOP, or entering MMX mode, changes the stack \
       mapping a recompiled block was compiled against: end the block, \
       see FSTOR in x87_ops_misc.h. Not on every MMX instruction, or   \
       each would become a block of its own. */                        \
    if ((cpu_state.TOP & 7) || !cpu_state.ismmx)                       \
        CPU_BLOCK_END();                                               \
    x87_set_mmx()

static int
opEMMS(UNUSED(uint32_t fetchdat))
{
    if (!cpu_has_feature(CPU_FEATURE_MMX)) {
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 1;
    }
    /* As MMX_ENTER: CR0.EM makes it invalid, CR0.TS only unavailable */
    if (cr0 & 0x4) {
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 1;
    }
    if (cr0 & 0x8) {
        x86_int(7);
        return 1;
    }
    /* A pending x87 exception is taken first (SDM 1999, 11.5.1) */
    if (fpu_softfloat) {
        FPU_check_pending_exceptions();
    } else if (x87_legacy_pending())
        return 1;
    x87_emms();
    CLOCK_CYCLES(100); /*Guess*/
    return 0;
}

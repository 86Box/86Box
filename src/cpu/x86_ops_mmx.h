#define SSATB(val) (((val) < -128) ? -128 : (((val) > 127) ? 127 : (val)))
#define SSATW(val) (((val) < -32768) ? -32768 : (((val) > 32767) ? 32767 : (val)))
#define USATB(val) (((val) < 0) ? 0 : (((val) > 255) ? 255 : (val)))
#define USATW(val) (((val) < 0) ? 0 : (((val) > 65535) ? 65535 : (val)))

#define MMX_GETSRC()                                                            \
        if (cpu_mod == 3)                                                           \
        {                                                                       \
                src = cpu_state.MM[cpu_rm];                                                   \
                CLOCK_CYCLES(1);                                                \
        }                                                                       \
        else                                                                    \
        {                                                                       \
                SEG_CHECK_READ(cpu_state.ea_seg);                               \
                src.q = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;            \
                CLOCK_CYCLES(2);                                                \
        }

#define MMX_ENTER()                                                     \
        if (!cpu_has_feature(CPU_FEATURE_MMX))                          \
        {                                                               \
                cpu_state.pc = cpu_state.oldpc;                                   \
                x86illegal();                                           \
                return 1;                                               \
        }                                                               \
        if (cr0 & 0xc)                                                  \
        {                                                               \
                x86_int(7);                                             \
                return 1;                                               \
        }                                                               \
        x87_set_mmx()

static int opEMMS(uint32_t fetchdat)
{
        if (!cpu_has_feature(CPU_FEATURE_MMX))
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
        if (cr0 & 0xc)
        {
                x86_int(7);
                return 1;
        }
        x87_emms();
        CLOCK_CYCLES(100); /*Guess*/
        return 0;
}

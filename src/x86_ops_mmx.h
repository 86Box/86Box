#define SSATB(val) (((val) < -128) ? -128 : (((val) > 127) ? 127 : (val)))
#define SSATW(val) (((val) < -32768) ? -32768 : (((val) > 32767) ? 32767 : (val)))
#define USATB(val) (((val) < 0) ? 0 : (((val) > 255) ? 255 : (val)))
#define USATW(val) (((val) < 0) ? 0 : (((val) > 65535) ? 65535 : (val)))

#define MMX_GETSRC()                                                            \
        if (mod == 3)                                                           \
        {                                                                       \
                src = MM[rm];                                                   \
                CLOCK_CYCLES(1);                                                \
        }                                                                       \
        else                                                                    \
        {                                                                       \
                src.q = readmemq(easeg, eaaddr); if (abrt) return 1;            \
                CLOCK_CYCLES(2);                                                \
        }

#define MMX_ENTER()                                                     \
        if (!cpu_hasMMX)                                                \
        {                                                               \
                cpu_state.pc = oldpc;                                   \
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
        if (!cpu_hasMMX)
        {
                cpu_state.pc = oldpc;
                x86illegal();
                return 1;
        }
        if (cr0 & 4)
        {
                x86_int(7);
                return 1;
        }
        x87_emms();
        CLOCK_CYCLES(100); /*Guess*/
        return 0;
}

#ifdef IS_DYNAREC
#define BS_common(start, end, dir, dest, time)                                  \
        flags_rebuild();                                                        \
        if (temp)                                                               \
        {                                                                       \
                int c;                                                          \
                cpu_state.flags &= ~Z_FLAG;                                               \
                for (c = start; c != end; c += dir)                             \
                {                                                               \
                        CLOCK_CYCLES(time);                                     \
                        if (temp & (1 << c))                                    \
                        {                                                       \
                                dest = c;                                       \
                                break;                                          \
                        }                                                       \
                }                                                               \
        }                                                                       \
        else                                                                    \
                cpu_state.flags |= Z_FLAG;
#else
#define BS_common(start, end, dir, dest, time)                                  \
        flags_rebuild();                                                        \
        instr_cycles = 0;                                                       \
        if (temp)                                                               \
        {                                                                       \
                int c;                                                          \
                cpu_state.flags &= ~Z_FLAG;                                               \
                for (c = start; c != end; c += dir)                             \
                {                                                               \
                        CLOCK_CYCLES(time);                                     \
                        instr_cycles += time;                                   \
                        if (temp & (1 << c))                                    \
                        {                                                       \
                                dest = c;                                       \
                                break;                                          \
                        }                                                       \
                }                                                               \
        }                                                                       \
        else                                                                    \
                cpu_state.flags |= Z_FLAG;
#endif

static int opBSF_w_a16(uint32_t fetchdat)
{
        uint16_t temp;
#ifndef IS_DYNAREC
        int instr_cycles = 0;
#endif

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;

        BS_common(0, 16, 1, cpu_state.regs[cpu_reg].w, (is486) ? 1 : 3);

        CLOCK_CYCLES((is486) ? 6 : 10);
#ifndef IS_DYNAREC
        instr_cycles += ((is486) ? 6 : 10);
        PREFETCH_RUN(instr_cycles, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
#endif
        return 0;
}
static int opBSF_w_a32(uint32_t fetchdat)
{
        uint16_t temp;
#ifndef IS_DYNAREC
        int instr_cycles = 0;
#endif

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;

        BS_common(0, 16, 1, cpu_state.regs[cpu_reg].w, (is486) ? 1 : 3);

        CLOCK_CYCLES((is486) ? 6 : 10);
#ifndef IS_DYNAREC
        instr_cycles += ((is486) ? 6 : 10);
        PREFETCH_RUN(instr_cycles, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
#endif
        return 0;
}
static int opBSF_l_a16(uint32_t fetchdat)
{
        uint32_t temp;
#ifndef IS_DYNAREC
        int instr_cycles = 0;
#endif

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteal();                        if (cpu_state.abrt) return 1;

        BS_common(0, 32, 1, cpu_state.regs[cpu_reg].l, (is486) ? 1 : 3);

        CLOCK_CYCLES((is486) ? 6 : 10);
#ifndef IS_DYNAREC
        instr_cycles += ((is486) ? 6 : 10);
        PREFETCH_RUN(instr_cycles, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 0);
#endif
        return 0;
}
static int opBSF_l_a32(uint32_t fetchdat)
{
        uint32_t temp;
#ifndef IS_DYNAREC
        int instr_cycles = 0;
#endif

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteal();                        if (cpu_state.abrt) return 1;

        BS_common(0, 32, 1, cpu_state.regs[cpu_reg].l, (is486) ? 1 : 3);

        CLOCK_CYCLES((is486) ? 6 : 10);
#ifndef IS_DYNAREC
        instr_cycles += ((is486) ? 6 : 10);
        PREFETCH_RUN(instr_cycles, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 1);
#endif
        return 0;
}

static int opBSR_w_a16(uint32_t fetchdat)
{
        uint16_t temp;
#ifndef IS_DYNAREC
        int instr_cycles = 0;
#endif

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;

        BS_common(15, -1, -1, cpu_state.regs[cpu_reg].w, 3);

        CLOCK_CYCLES((is486) ? 6 : 10);
#ifndef IS_DYNAREC
        instr_cycles += ((is486) ? 6 : 10);
        PREFETCH_RUN(instr_cycles, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
#endif
        return 0;
}
static int opBSR_w_a32(uint32_t fetchdat)
{
        uint16_t temp;
#ifndef IS_DYNAREC
        int instr_cycles = 0;
#endif

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;

        BS_common(15, -1, -1, cpu_state.regs[cpu_reg].w, 3);

        CLOCK_CYCLES((is486) ? 6 : 10);
#ifndef IS_DYNAREC
        instr_cycles += ((is486) ? 6 : 10);
        PREFETCH_RUN(instr_cycles, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
#endif
        return 0;
}
static int opBSR_l_a16(uint32_t fetchdat)
{
        uint32_t temp;
#ifndef IS_DYNAREC
        int instr_cycles = 0;
#endif

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteal();                        if (cpu_state.abrt) return 1;

        BS_common(31, -1, -1, cpu_state.regs[cpu_reg].l, 3);

        CLOCK_CYCLES((is486) ? 6 : 10);
#ifndef IS_DYNAREC
        instr_cycles += ((is486) ? 6 : 10);
        PREFETCH_RUN(instr_cycles, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 0);
#endif
        return 0;
}
static int opBSR_l_a32(uint32_t fetchdat)
{
        uint32_t temp;
#ifndef IS_DYNAREC
        int instr_cycles = 0;
#endif

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteal();                        if (cpu_state.abrt) return 1;

        BS_common(31, -1, -1, cpu_state.regs[cpu_reg].l, 3);

        CLOCK_CYCLES((is486) ? 6 : 10);
#ifndef IS_DYNAREC
        instr_cycles += ((is486) ? 6 : 10);
        PREFETCH_RUN(instr_cycles, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 1);
#endif
        return 0;
}

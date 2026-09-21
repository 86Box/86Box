#define OP_ARITH(name, operation, setflags, flagops, gettempc)                                              \
    static int op##name##_b_rmw_a16(uint32_t fetchdat)                                                      \
    {                                                                                                       \
        uint8_t dst;                                                                                        \
        uint8_t src;                                                                                        \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_16(fetchdat);                                                                              \
        if (cpu_mod == 3) {                                                                                 \
            dst = getr8(cpu_rm);                                                                            \
            src = getr8(cpu_reg);                                                                           \
            setflags##8 flagops;                                                                            \
            setr8(cpu_rm, operation);                                                                       \
            CLOCK_CYCLES(timing_rr);                                                                        \
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);                                               \
        } else {                                                                                            \
            SEG_CHECK_WRITE(cpu_state.ea_seg);                                                              \
            dst = geteab();                                                                                 \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            src = getr8(cpu_reg);                                                                           \
            seteab(operation);                                                                              \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            setflags##8 flagops;                                                                            \
            CLOCK_CYCLES(timing_mr);                                                                        \
            PREFETCH_RUN(timing_mr, 2, rmdat, 1, 0, 1, 0, 0);                                               \
        }                                                                                                   \
        return 0;                                                                                           \
    }                                                                                                       \
    static int op##name##_b_rmw_a32(uint32_t fetchdat)                                                      \
    {                                                                                                       \
        uint8_t dst;                                                                                        \
        uint8_t src;                                                                                        \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_32(fetchdat);                                                                              \
        if (cpu_mod == 3) {                                                                                 \
            dst = getr8(cpu_rm);                                                                            \
            src = getr8(cpu_reg);                                                                           \
            setflags##8 flagops;                                                                            \
            setr8(cpu_rm, operation);                                                                       \
            CLOCK_CYCLES(timing_rr);                                                                        \
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);                                               \
        } else {                                                                                            \
            SEG_CHECK_WRITE(cpu_state.ea_seg);                                                              \
            dst = geteab();                                                                                 \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            src = getr8(cpu_reg);                                                                           \
            seteab(operation);                                                                              \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            setflags##8 flagops;                                                                            \
            CLOCK_CYCLES(timing_mr);                                                                        \
            PREFETCH_RUN(timing_mr, 2, rmdat, 1, 0, 1, 0, 1);                                               \
        }                                                                                                   \
        return 0;                                                                                           \
    }                                                                                                       \
                                                                                                            \
    static int op##name##_w_rmw_a16(uint32_t fetchdat)                                                      \
    {                                                                                                       \
        uint16_t dst;                                                                                       \
        uint16_t src;                                                                                       \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_16(fetchdat);                                                                              \
        if (cpu_mod == 3) {                                                                                 \
            dst = cpu_state.regs[cpu_rm].w;                                                                 \
            src = cpu_state.regs[cpu_reg].w;                                                                \
            setflags##16 flagops;                                                                           \
            cpu_state.regs[cpu_rm].w = operation;                                                           \
            CLOCK_CYCLES(timing_rr);                                                                        \
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);                                               \
        } else {                                                                                            \
            SEG_CHECK_WRITE(cpu_state.ea_seg);                                                              \
            dst = geteaw();                                                                                 \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            src = cpu_state.regs[cpu_reg].w;                                                                \
            seteaw(operation);                                                                              \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            setflags##16 flagops;                                                                           \
            CLOCK_CYCLES(timing_mr);                                                                        \
            PREFETCH_RUN(timing_rr, 2, rmdat, 1, 0, 1, 0, 0);                                               \
        }                                                                                                   \
        return 0;                                                                                           \
    }                                                                                                       \
    static int op##name##_w_rmw_a32(uint32_t fetchdat)                                                      \
    {                                                                                                       \
        uint16_t dst;                                                                                       \
        uint16_t src;                                                                                       \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_32(fetchdat);                                                                              \
        if (cpu_mod == 3) {                                                                                 \
            dst = cpu_state.regs[cpu_rm].w;                                                                 \
            src = cpu_state.regs[cpu_reg].w;                                                                \
            setflags##16 flagops;                                                                           \
            cpu_state.regs[cpu_rm].w = operation;                                                           \
            CLOCK_CYCLES(timing_rr);                                                                        \
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);                                               \
        } else {                                                                                            \
            SEG_CHECK_WRITE(cpu_state.ea_seg);                                                              \
            dst = geteaw();                                                                                 \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            src = cpu_state.regs[cpu_reg].w;                                                                \
            seteaw(operation);                                                                              \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            setflags##16 flagops;                                                                           \
            CLOCK_CYCLES(timing_mr);                                                                        \
            PREFETCH_RUN(timing_rr, 2, rmdat, 1, 0, 1, 0, 1);                                               \
        }                                                                                                   \
        return 0;                                                                                           \
    }                                                                                                       \
                                                                                                            \
    static int op##name##_l_rmw_a16(uint32_t fetchdat)                                                      \
    {                                                                                                       \
        uint32_t dst;                                                                                       \
        uint32_t src;                                                                                       \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_16(fetchdat);                                                                              \
        if (cpu_mod == 3) {                                                                                 \
            dst = cpu_state.regs[cpu_rm].l;                                                                 \
            src = cpu_state.regs[cpu_reg].l;                                                                \
            setflags##32 flagops;                                                                           \
            cpu_state.regs[cpu_rm].l = operation;                                                           \
            CLOCK_CYCLES(timing_rr);                                                                        \
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);                                               \
        } else {                                                                                            \
            SEG_CHECK_WRITE(cpu_state.ea_seg);                                                              \
            dst = geteal();                                                                                 \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            src = cpu_state.regs[cpu_reg].l;                                                                \
            seteal(operation);                                                                              \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            setflags##32 flagops;                                                                           \
            CLOCK_CYCLES(timing_mr);                                                                        \
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 1, 0, 1, 0);                                               \
        }                                                                                                   \
        return 0;                                                                                           \
    }                                                                                                       \
    static int op##name##_l_rmw_a32(uint32_t fetchdat)                                                      \
    {                                                                                                       \
        uint32_t dst;                                                                                       \
        uint32_t src;                                                                                       \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_32(fetchdat);                                                                              \
        if (cpu_mod == 3) {                                                                                 \
            dst = cpu_state.regs[cpu_rm].l;                                                                 \
            src = cpu_state.regs[cpu_reg].l;                                                                \
            setflags##32 flagops;                                                                           \
            cpu_state.regs[cpu_rm].l = operation;                                                           \
            CLOCK_CYCLES(timing_rr);                                                                        \
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);                                               \
        } else {                                                                                            \
            SEG_CHECK_WRITE(cpu_state.ea_seg);                                                              \
            dst = geteal();                                                                                 \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            src = cpu_state.regs[cpu_reg].l;                                                                \
            seteal(operation);                                                                              \
            if (cpu_state.abrt)                                                                             \
                return 1;                                                                                   \
            setflags##32 flagops;                                                                           \
            CLOCK_CYCLES(timing_mr);                                                                        \
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 1, 0, 1, 1);                                               \
        }                                                                                                   \
        return 0;                                                                                           \
    }                                                                                                       \
                                                                                                            \
    static int op##name##_b_rm_a16(uint32_t fetchdat)                                                       \
    {                                                                                                       \
        uint8_t dst, src;                                                                                   \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_16(fetchdat);                                                                              \
        if (cpu_mod != 3)                                                                                   \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                               \
        dst = getr8(cpu_reg);                                                                               \
        src = geteab();                                                                                     \
        if (cpu_state.abrt)                                                                                 \
            return 1;                                                                                       \
        setflags##8 flagops;                                                                                \
        setr8(cpu_reg, operation);                                                                          \
        CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rm);                                               \
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 0); \
        return 0;                                                                                           \
    }                                                                                                       \
    static int op##name##_b_rm_a32(uint32_t fetchdat)                                                       \
    {                                                                                                       \
        uint8_t dst, src;                                                                                   \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_32(fetchdat);                                                                              \
        if (cpu_mod != 3)                                                                                   \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                               \
        dst = getr8(cpu_reg);                                                                               \
        src = geteab();                                                                                     \
        if (cpu_state.abrt)                                                                                 \
            return 1;                                                                                       \
        setflags##8 flagops;                                                                                \
        setr8(cpu_reg, operation);                                                                          \
        CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rm);                                               \
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 1); \
        return 0;                                                                                           \
    }                                                                                                       \
                                                                                                            \
    static int op##name##_w_rm_a16(uint32_t fetchdat)                                                       \
    {                                                                                                       \
        uint16_t dst, src;                                                                                  \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_16(fetchdat);                                                                              \
        if (cpu_mod != 3)                                                                                   \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                               \
        dst = cpu_state.regs[cpu_reg].w;                                                                    \
        src = geteaw();                                                                                     \
        if (cpu_state.abrt)                                                                                 \
            return 1;                                                                                       \
        setflags##16 flagops;                                                                               \
        cpu_state.regs[cpu_reg].w = operation;                                                              \
        CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rm);                                               \
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 0); \
        return 0;                                                                                           \
    }                                                                                                       \
    static int op##name##_w_rm_a32(uint32_t fetchdat)                                                       \
    {                                                                                                       \
        uint16_t dst, src;                                                                                  \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_32(fetchdat);                                                                              \
        if (cpu_mod != 3)                                                                                   \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                               \
        dst = cpu_state.regs[cpu_reg].w;                                                                    \
        src = geteaw();                                                                                     \
        if (cpu_state.abrt)                                                                                 \
            return 1;                                                                                       \
        setflags##16 flagops;                                                                               \
        cpu_state.regs[cpu_reg].w = operation;                                                              \
        CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rm);                                               \
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 1); \
        return 0;                                                                                           \
    }                                                                                                       \
                                                                                                            \
    static int op##name##_l_rm_a16(uint32_t fetchdat)                                                       \
    {                                                                                                       \
        uint32_t dst, src;                                                                                  \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_16(fetchdat);                                                                              \
        if (cpu_mod != 3)                                                                                   \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                               \
        dst = cpu_state.regs[cpu_reg].l;                                                                    \
        src = geteal();                                                                                     \
        if (cpu_state.abrt)                                                                                 \
            return 1;                                                                                       \
        setflags##32 flagops;                                                                               \
        cpu_state.regs[cpu_reg].l = operation;                                                              \
        CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rml);                                              \
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 0); \
        return 0;                                                                                           \
    }                                                                                                       \
    static int op##name##_l_rm_a32(uint32_t fetchdat)                                                       \
    {                                                                                                       \
        uint32_t dst, src;                                                                                  \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        fetch_ea_32(fetchdat);                                                                              \
        if (cpu_mod != 3)                                                                                   \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                               \
        dst = cpu_state.regs[cpu_reg].l;                                                                    \
        src = geteal();                                                                                     \
        if (cpu_state.abrt)                                                                                 \
            return 1;                                                                                       \
        setflags##32 flagops;                                                                               \
        cpu_state.regs[cpu_reg].l = operation;                                                              \
        CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rml);                                              \
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 1); \
        return 0;                                                                                           \
    }                                                                                                       \
                                                                                                            \
    static int op##name##_AL_imm(uint32_t fetchdat)                                                         \
    {                                                                                                       \
        uint8_t dst = AL;                                                                                   \
        uint8_t src = getbytef();                                                                           \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        setflags##8 flagops;                                                                                \
        AL = operation;                                                                                     \
        CLOCK_CYCLES(timing_rr);                                                                            \
        PREFETCH_RUN(timing_rr, 2, -1, 0, 0, 0, 0, 0);                                                      \
        return 0;                                                                                           \
    }                                                                                                       \
                                                                                                            \
    static int op##name##_AX_imm(uint32_t fetchdat)                                                         \
    {                                                                                                       \
        uint16_t dst = AX;                                                                                  \
        uint16_t src = getwordf();                                                                          \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        setflags##16 flagops;                                                                               \
        AX = operation;                                                                                     \
        CLOCK_CYCLES(timing_rr);                                                                            \
        PREFETCH_RUN(timing_rr, 3, -1, 0, 0, 0, 0, 0);                                                      \
        return 0;                                                                                           \
    }                                                                                                       \
                                                                                                            \
    static int op##name##_EAX_imm(UNUSED(uint32_t fetchdat))                                                \
    {                                                                                                       \
        uint32_t dst = EAX;                                                                                 \
        uint32_t src = getlong();                                                                           \
        if (cpu_state.abrt)                                                                                 \
            return 1;                                                                                       \
        if (gettempc)                                                                                       \
            tempc = CF_SET() ? 1 : 0;                                                                       \
        setflags##32 flagops;                                                                               \
        EAX = operation;                                                                                    \
        CLOCK_CYCLES(timing_rr);                                                                            \
        PREFETCH_RUN(timing_rr, 5, -1, 0, 0, 0, 0, 0);                                                      \
        return 0;                                                                                           \
    }

OP_ARITH(ADD, dst + src, setadd, (dst, src), 0)
OP_ARITH(ADC, dst + src + tempc, setadc, (dst, src), 1)
OP_ARITH(SUB, dst - src, setsub, (dst, src), 0)
OP_ARITH(SBB, dst - (src + tempc), setsbc, (dst, src), 1)
OP_ARITH(OR, dst | src, setznp, (dst | src), 0)
OP_ARITH(AND, dst &src, setznp, (dst & src), 0)
OP_ARITH(XOR, dst ^ src, setznp, (dst ^ src), 0)

static int
opCMP_b_rmw_a16(uint32_t fetchdat)
{
    uint8_t dst;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    dst = geteab();
    if (cpu_state.abrt)
        return 1;
    setsub8(dst, getr8(cpu_reg));
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 0);
    return 0;
}
static int
opCMP_b_rmw_a32(uint32_t fetchdat)
{
    uint8_t dst;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    dst = geteab();
    if (cpu_state.abrt)
        return 1;
    setsub8(dst, getr8(cpu_reg));
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 1);
    return 0;
}

static int
opCMP_w_rmw_a16(uint32_t fetchdat)
{
    uint16_t dst;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    dst = geteaw();
    if (cpu_state.abrt)
        return 1;
    setsub16(dst, cpu_state.regs[cpu_reg].w);
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 0);
    return 0;
}
static int
opCMP_w_rmw_a32(uint32_t fetchdat)
{
    uint16_t dst;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    dst = geteaw();
    if (cpu_state.abrt)
        return 1;
    setsub16(dst, cpu_state.regs[cpu_reg].w);
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 1);
    return 0;
}

static int
opCMP_l_rmw_a16(uint32_t fetchdat)
{
    uint32_t dst;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    dst = geteal();
    if (cpu_state.abrt)
        return 1;
    setsub32(dst, cpu_state.regs[cpu_reg].l);
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 0);
    return 0;
}
static int
opCMP_l_rmw_a32(uint32_t fetchdat)
{
    uint32_t dst;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    dst = geteal();
    if (cpu_state.abrt)
        return 1;
    setsub32(dst, cpu_state.regs[cpu_reg].l);
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 1);
    return 0;
}

static int
opCMP_b_rm_a16(uint32_t fetchdat)
{
    uint8_t src;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    src = geteab();
    if (cpu_state.abrt)
        return 1;
    setsub8(getr8(cpu_reg), src);
    CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rm);
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 0);
    return 0;
}
static int
opCMP_b_rm_a32(uint32_t fetchdat)
{
    uint8_t src;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    src = geteab();
    if (cpu_state.abrt)
        return 1;
    setsub8(getr8(cpu_reg), src);
    CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rm);
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 1);
    return 0;
}

static int
opCMP_w_rm_a16(uint32_t fetchdat)
{
    uint16_t src;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    src = geteaw();
    if (cpu_state.abrt)
        return 1;
    setsub16(cpu_state.regs[cpu_reg].w, src);
    CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rm);
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 0);
    return 0;
}
static int
opCMP_w_rm_a32(uint32_t fetchdat)
{
    uint16_t src;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    src = geteaw();
    if (cpu_state.abrt)
        return 1;
    setsub16(cpu_state.regs[cpu_reg].w, src);
    CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rm);
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 1);
    return 0;
}

static int
opCMP_l_rm_a16(uint32_t fetchdat)
{
    uint32_t src;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    src = geteal();
    if (cpu_state.abrt)
        return 1;
    setsub32(cpu_state.regs[cpu_reg].l, src);
    CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rml);
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 0);
    return 0;
}
static int
opCMP_l_rm_a32(uint32_t fetchdat)
{
    uint32_t src;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    src = geteal();
    if (cpu_state.abrt)
        return 1;
    setsub32(cpu_state.regs[cpu_reg].l, src);
    CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_rml);
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 1);
    return 0;
}

static int
opCMP_AL_imm(uint32_t fetchdat)
{
    uint8_t src = getbytef();

    setsub8(AL, src);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, -1, 0, 0, 0, 0, 0);
    return 0;
}

static int
opCMP_AX_imm(uint32_t fetchdat)
{
    uint16_t src = getwordf();

    setsub16(AX, src);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, -1, 0, 0, 0, 0, 0);
    return 0;
}

static int
opCMP_EAX_imm(UNUSED(uint32_t fetchdat))
{
    uint32_t src = getlong();

    if (cpu_state.abrt)
        return 1;
    setsub32(EAX, src);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 5, -1, 0, 0, 0, 0, 0);
    return 0;
}

static int
opTEST_b_a16(uint32_t fetchdat)
{
    uint8_t temp;
    uint8_t temp2;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    temp = geteab();
    if (cpu_state.abrt)
        return 1;
    temp2 = getr8(cpu_reg);
    setznp8(temp & temp2);
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 0);
    return 0;
}
static int
opTEST_b_a32(uint32_t fetchdat)
{
    uint8_t temp;
    uint8_t temp2;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    temp = geteab();
    if (cpu_state.abrt)
        return 1;
    temp2 = getr8(cpu_reg);
    setznp8(temp & temp2);
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 1);
    return 0;
}

static int
opTEST_w_a16(uint32_t fetchdat)
{
    uint16_t temp;
    uint16_t temp2;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    temp = geteaw();
    if (cpu_state.abrt)
        return 1;
    temp2 = cpu_state.regs[cpu_reg].w;
    setznp16(temp & temp2);
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 0);
    return 0;
}
static int
opTEST_w_a32(uint32_t fetchdat)
{
    uint16_t temp;
    uint16_t temp2;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    temp = geteaw();
    if (cpu_state.abrt)
        return 1;
    temp2 = cpu_state.regs[cpu_reg].w;
    setznp16(temp & temp2);
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 1);
    return 0;
}

static int
opTEST_l_a16(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t temp2;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    temp = geteal();
    if (cpu_state.abrt)
        return 1;
    temp2 = cpu_state.regs[cpu_reg].l;
    setznp32(temp & temp2);
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 0);
    return 0;
}
static int
opTEST_l_a32(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t temp2;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);
    temp = geteal();
    if (cpu_state.abrt)
        return 1;
    temp2 = cpu_state.regs[cpu_reg].l;
    setznp32(temp & temp2);
    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 2, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 1);
    return 0;
}

static int
opTEST_AL(uint32_t fetchdat)
{
    uint8_t temp = getbytef();
    setznp8(AL & temp);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opTEST_AX(uint32_t fetchdat)
{
    uint16_t temp = getwordf();
    setznp16(AX & temp);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opTEST_EAX(UNUSED(uint32_t fetchdat))
{
    uint32_t temp = getlong();
    if (cpu_state.abrt)
        return 1;
    setznp32(EAX & temp);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 5, -1, 0, 0, 0, 0, 0);
    return 0;
}

#define ARITH_MULTI(ea_width, flag_width)                         \
    dst = getea##ea_width();                                      \
    if (cpu_state.abrt)                                           \
        return 1;                                                 \
    switch (rmdat & 0x38) {                                       \
        case 0x00: /*ADD ea, #*/                                  \
            setea##ea_width(dst + src);                           \
            if (cpu_state.abrt)                                   \
                return 1;                                         \
            setadd##flag_width(dst, src);                         \
            CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mr); \
            break;                                                \
        case 0x08: /*OR ea, #*/                                   \
            dst |= src;                                           \
            setea##ea_width(dst);                                 \
            if (cpu_state.abrt)                                   \
                return 1;                                         \
            setznp##flag_width(dst);                              \
            CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mr); \
            break;                                                \
        case 0x10: /*ADC ea, #*/                                  \
            tempc = CF_SET() ? 1 : 0;                             \
            setea##ea_width(dst + src + tempc);                   \
            if (cpu_state.abrt)                                   \
                return 1;                                         \
            setadc##flag_width(dst, src);                         \
            CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mr); \
            break;                                                \
        case 0x18: /*SBB ea, #*/                                  \
            tempc = CF_SET() ? 1 : 0;                             \
            setea##ea_width(dst - (src + tempc));                 \
            if (cpu_state.abrt)                                   \
                return 1;                                         \
            setsbc##flag_width(dst, src);                         \
            CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mr); \
            break;                                                \
        case 0x20: /*AND ea, #*/                                  \
            dst &= src;                                           \
            setea##ea_width(dst);                                 \
            if (cpu_state.abrt)                                   \
                return 1;                                         \
            setznp##flag_width(dst);                              \
            CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mr); \
            break;                                                \
        case 0x28: /*SUB ea, #*/                                  \
            setea##ea_width(dst - src);                           \
            if (cpu_state.abrt)                                   \
                return 1;                                         \
            setsub##flag_width(dst, src);                         \
            CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mr); \
            break;                                                \
        case 0x30: /*XOR ea, #*/                                  \
            dst ^= src;                                           \
            setea##ea_width(dst);                                 \
            if (cpu_state.abrt)                                   \
                return 1;                                         \
            setznp##flag_width(dst);                              \
            CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mr); \
            break;                                                \
        case 0x38: /*CMP ea, #*/                                  \
            setsub##flag_width(dst, src);                         \
            if (is486) {                                          \
                CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);             \
            } else {                                              \
                CLOCK_CYCLES((cpu_mod == 3) ? 2 : 7);             \
            }                                                     \
            break;                                                \
    }

static int
op80_a16(uint32_t fetchdat)
{
    uint8_t src;
    uint8_t dst;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src = getbyte();
    if (cpu_state.abrt)
        return 1;
    ARITH_MULTI(b, 8);
    if ((rmdat & 0x38) == 0x38) {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mr, 3, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 0);
    } else {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 3, rmdat, (cpu_mod == 3) ? 0 : 1, 0, (cpu_mod == 3) ? 0 : 1, 0, 0);
    }

    return 0;
}
static int
op80_a32(uint32_t fetchdat)
{
    uint8_t src;
    uint8_t dst;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src = getbyte();
    if (cpu_state.abrt)
        return 1;
    ARITH_MULTI(b, 8);
    if ((rmdat & 0x38) == 0x38) {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mr, 3, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 1);
    } else {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 3, rmdat, (cpu_mod == 3) ? 0 : 1, 0, (cpu_mod == 3) ? 0 : 1, 0, 1);
    }

    return 0;
}
static int
op81_w_a16(uint32_t fetchdat)
{
    uint16_t src;
    uint16_t dst;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src = getword();
    if (cpu_state.abrt)
        return 1;
    ARITH_MULTI(w, 16);
    if ((rmdat & 0x38) == 0x38) {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mr, 4, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 0);
    } else {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 4, rmdat, (cpu_mod == 3) ? 0 : 1, 0, (cpu_mod == 3) ? 0 : 1, 0, 0);
    }

    return 0;
}
static int
op81_w_a32(uint32_t fetchdat)
{
    uint16_t src;
    uint16_t dst;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src = getword();
    if (cpu_state.abrt)
        return 1;
    ARITH_MULTI(w, 16);
    if ((rmdat & 0x38) == 0x38) {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mr, 4, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 1);
    } else {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 4, rmdat, (cpu_mod == 3) ? 0 : 1, 0, (cpu_mod == 3) ? 0 : 1, 0, 1);
    }

    return 0;
}
static int
op81_l_a16(uint32_t fetchdat)
{
    uint32_t src;
    uint32_t dst;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src = getlong();
    if (cpu_state.abrt)
        return 1;
    ARITH_MULTI(l, 32);
    if ((rmdat & 0x38) == 0x38) {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mr, 6, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 0);
    } else {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 6, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, (cpu_mod == 3) ? 0 : 1, 0);
    }

    return 0;
}
static int
op81_l_a32(uint32_t fetchdat)
{
    uint32_t src;
    uint32_t dst;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src = getlong();
    if (cpu_state.abrt)
        return 1;
    ARITH_MULTI(l, 32);
    if ((rmdat & 0x38) == 0x38) {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mr, 6, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 1);
    } else {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 6, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, (cpu_mod == 3) ? 0 : 1, 1);
    }

    return 0;
}

static int
op83_w_a16(uint32_t fetchdat)
{
    uint16_t src;
    uint16_t dst;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src = getbyte();
    if (cpu_state.abrt)
        return 1;
    if (src & 0x80)
        src |= 0xff00;
    ARITH_MULTI(w, 16);
    if ((rmdat & 0x38) == 0x38) {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mr, 3, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 0);
    } else {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 3, rmdat, (cpu_mod == 3) ? 0 : 1, 0, (cpu_mod == 3) ? 0 : 1, 0, 0);
    }

    return 0;
}
static int
op83_w_a32(uint32_t fetchdat)
{
    uint16_t src;
    uint16_t dst;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src = getbyte();
    if (cpu_state.abrt)
        return 1;
    if (src & 0x80)
        src |= 0xff00;
    ARITH_MULTI(w, 16);
    if ((rmdat & 0x38) == 0x38) {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mr, 3, rmdat, (cpu_mod == 3) ? 0 : 1, 0, 0, 0, 1);
    } else {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 3, rmdat, (cpu_mod == 3) ? 0 : 1, 0, (cpu_mod == 3) ? 0 : 1, 0, 1);
    }

    return 0;
}

static int
op83_l_a16(uint32_t fetchdat)
{
    uint32_t src;
    uint32_t dst;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src = getbyte();
    if (cpu_state.abrt)
        return 1;
    if (src & 0x80)
        src |= 0xffffff00;
    ARITH_MULTI(l, 32);
    if ((rmdat & 0x38) == 0x38) {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mr, 3, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 0);
    } else {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 3, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, (cpu_mod == 3) ? 0 : 1, 0);
    }

    return 0;
}
static int
op83_l_a32(uint32_t fetchdat)
{
    uint32_t src;
    uint32_t dst;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src = getbyte();
    if (cpu_state.abrt)
        return 1;
    if (src & 0x80)
        src |= 0xffffff00;
    ARITH_MULTI(l, 32);
    if ((rmdat & 0x38) == 0x38) {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mr, 3, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, 0, 1);
    } else {
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_rm, 3, rmdat, 0, (cpu_mod == 3) ? 0 : 1, 0, (cpu_mod == 3) ? 0 : 1, 1);
    }

    return 0;
}

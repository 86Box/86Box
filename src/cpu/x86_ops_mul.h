static int
opIMUL_w_iw_a16(uint32_t fetchdat)
{
    int32_t templ;
    int16_t tempw;
    int16_t tempw2;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    tempw = geteaw();
    if (cpu_state.abrt)
        return 1;
    tempw2 = getword();
    if (cpu_state.abrt)
        return 1;

    templ = ((int) tempw) * ((int) tempw2);
    cpu_state.regs[cpu_reg].w = templ & 0xffff;
    setimul16(tempw, tempw2);

    CLOCK_CYCLES((cpu_mod == 3) ? 14 : 17);
    PREFETCH_RUN((cpu_mod == 3) ? 14 : 17, 4, rmdat, 1, 0, 0, 0, 0);
    return 0;
}
static int
opIMUL_w_iw_a32(uint32_t fetchdat)
{
    int32_t templ;
    int16_t tempw;
    int16_t tempw2;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    tempw = geteaw();
    if (cpu_state.abrt)
        return 1;
    tempw2 = getword();
    if (cpu_state.abrt)
        return 1;

    templ = ((int) tempw) * ((int) tempw2);
    cpu_state.regs[cpu_reg].w = templ & 0xffff;
    setimul16(tempw, tempw2);

    CLOCK_CYCLES((cpu_mod == 3) ? 14 : 17);
    PREFETCH_RUN((cpu_mod == 3) ? 14 : 17, 4, rmdat, 1, 0, 0, 0, 1);
    return 0;
}

static int
opIMUL_l_il_a16(uint32_t fetchdat)
{
    int64_t temp64;
    int32_t templ;
    int32_t templ2;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    templ = geteal();
    if (cpu_state.abrt)
        return 1;
    templ2 = getlong();
    if (cpu_state.abrt)
        return 1;

    temp64 = ((int64_t) templ) * ((int64_t) templ2);
    cpu_state.regs[cpu_reg].l = temp64 & 0xffffffff;
    setimul32(templ, templ2);

    CLOCK_CYCLES(25);
    PREFETCH_RUN(25, 6, rmdat, 0, 1, 0, 0, 0);
    return 0;
}
static int
opIMUL_l_il_a32(uint32_t fetchdat)
{
    int64_t temp64;
    int32_t templ;
    int32_t templ2;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    templ = geteal();
    if (cpu_state.abrt)
        return 1;
    templ2 = getlong();
    if (cpu_state.abrt)
        return 1;

    temp64 = ((int64_t) templ) * ((int64_t) templ2);
    cpu_state.regs[cpu_reg].l = temp64 & 0xffffffff;
    setimul32(templ, templ2);

    CLOCK_CYCLES(25);
    PREFETCH_RUN(25, 6, rmdat, 0, 1, 0, 0, 1);
    return 0;
}

static int
opIMUL_w_ib_a16(uint32_t fetchdat)
{
    int32_t templ;
    int16_t tempw;
    int16_t tempw2;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    tempw = geteaw();
    if (cpu_state.abrt)
        return 1;
    tempw2 = getbyte();
    if (cpu_state.abrt)
        return 1;
    if (tempw2 & 0x80)
        tempw2 |= 0xff00;

    templ = ((int) tempw) * ((int) tempw2);
    cpu_state.regs[cpu_reg].w = templ & 0xffff;
    setimul16(tempw, tempw2);

    CLOCK_CYCLES((cpu_mod == 3) ? 14 : 17);
    PREFETCH_RUN((cpu_mod == 3) ? 14 : 17, 3, rmdat, 1, 0, 0, 0, 0);
    return 0;
}
static int
opIMUL_w_ib_a32(uint32_t fetchdat)
{
    int32_t templ;
    int16_t tempw;
    int16_t tempw2;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    tempw = geteaw();
    if (cpu_state.abrt)
        return 1;
    tempw2 = getbyte();
    if (cpu_state.abrt)
        return 1;
    if (tempw2 & 0x80)
        tempw2 |= 0xff00;

    templ = ((int) tempw) * ((int) tempw2);
    cpu_state.regs[cpu_reg].w = templ & 0xffff;
    setimul16(tempw, tempw2);

    CLOCK_CYCLES((cpu_mod == 3) ? 14 : 17);
    PREFETCH_RUN((cpu_mod == 3) ? 14 : 17, 3, rmdat, 1, 0, 0, 0, 1);
    return 0;
}

static int
opIMUL_l_ib_a16(uint32_t fetchdat)
{
    int64_t temp64;
    int32_t templ;
    int32_t templ2;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    templ = geteal();
    if (cpu_state.abrt)
        return 1;
    templ2 = getbyte();
    if (cpu_state.abrt)
        return 1;
    if (templ2 & 0x80)
        templ2 |= 0xffffff00;

    temp64 = ((int64_t) templ) * ((int64_t) templ2);
    cpu_state.regs[cpu_reg].l = temp64 & 0xffffffff;
    setimul32(templ, templ2);

    CLOCK_CYCLES(20);
    PREFETCH_RUN(20, 3, rmdat, 0, 1, 0, 0, 0);
    return 0;
}
static int
opIMUL_l_ib_a32(uint32_t fetchdat)
{
    int64_t temp64;
    int32_t templ;
    int32_t templ2;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    templ = geteal();
    if (cpu_state.abrt)
        return 1;
    templ2 = getbyte();
    if (cpu_state.abrt)
        return 1;
    if (templ2 & 0x80)
        templ2 |= 0xffffff00;

    temp64 = ((int64_t) templ) * ((int64_t) templ2);
    cpu_state.regs[cpu_reg].l = temp64 & 0xffffffff;
    setimul32(templ, templ2);

    CLOCK_CYCLES(20);
    PREFETCH_RUN(20, 3, rmdat, 0, 1, 0, 0, 1);
    return 0;
}

static int
opIMUL_w_w_a16(uint32_t fetchdat)
{
    int32_t templ;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    int16_t op1 = cpu_state.regs[cpu_reg].w;
    int16_t op2 = geteaw();
    if (cpu_state.abrt)
        return 1;
    templ = (int32_t) op1 * (int32_t) op2;
    cpu_state.regs[cpu_reg].w = templ & 0xFFFF;
    setimul16(op1, op2);

    CLOCK_CYCLES(18);
    PREFETCH_RUN(18, 2, rmdat, 1, 0, 0, 0, 0);
    return 0;
}
static int
opIMUL_w_w_a32(uint32_t fetchdat)
{
    int32_t templ;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    int16_t op1 = cpu_state.regs[cpu_reg].w;
    int16_t op2 = geteaw();
    if (cpu_state.abrt)
        return 1;
    templ = (int32_t) op1 * (int32_t) op2;
    cpu_state.regs[cpu_reg].w = templ & 0xFFFF;
    setimul16(op1, op2);

    CLOCK_CYCLES(18);
    PREFETCH_RUN(18, 2, rmdat, 1, 0, 0, 0, 1);
    return 0;
}

static int
opIMUL_l_l_a16(uint32_t fetchdat)
{
    int64_t temp64;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    int32_t op1 = cpu_state.regs[cpu_reg].l;
    int32_t op2 = geteal();
    if (cpu_state.abrt)
        return 1;
    temp64 = (int64_t) op1 * (int64_t) op2;
    cpu_state.regs[cpu_reg].l = temp64 & 0xFFFFFFFF;
    setimul32(op1, op2);

    CLOCK_CYCLES(30);
    PREFETCH_RUN(30, 2, rmdat, 0, 1, 0, 0, 0);
    return 0;
}
static int
opIMUL_l_l_a32(uint32_t fetchdat)
{
    int64_t temp64;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_READ(cpu_state.ea_seg);

    int32_t op1 = cpu_state.regs[cpu_reg].l;
    int32_t op2 = geteal();
    if (cpu_state.abrt)
        return 1;
    temp64 = (int64_t) op1 * (int64_t) op2;
    cpu_state.regs[cpu_reg].l = temp64 & 0xFFFFFFFF;
    setimul32(op1, op2);

    CLOCK_CYCLES(30);
    PREFETCH_RUN(30, 2, rmdat, 0, 1, 0, 0, 1);
    return 0;
}

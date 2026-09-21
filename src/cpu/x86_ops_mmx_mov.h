static int
opMOVD_l_mm_a16(uint32_t fetchdat)
{
    uint32_t dst;
    MMX_REG *op;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    op = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3) {
        op->l[0] = cpu_state.regs[cpu_rm].l;
        op->l[1] = 0;
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;

        op->l[0] = dst;
        op->l[1] = 0;
        CLOCK_CYCLES(2);
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opMOVD_l_mm_a32(uint32_t fetchdat)
{
    uint32_t dst;
    MMX_REG *op;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    op = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3) {
        op->l[0] = cpu_state.regs[cpu_rm].l;
        op->l[1] = 0;
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;

        op->l[0] = dst;
        op->l[1] = 0;
        CLOCK_CYCLES(2);
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opMOVD_mm_l_a16(uint32_t fetchdat)
{
    MMX_REG *op;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    op = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3) {
        cpu_state.regs[cpu_rm].l = op->l[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
        writememl(easeg, cpu_state.eaaddr, op->l[0]);
        if (cpu_state.abrt)
            return 1;

        CLOCK_CYCLES(2);
    }

    return 0;
}
static int
opMOVD_mm_l_a32(uint32_t fetchdat)
{
    MMX_REG *op;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    op = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3) {
        cpu_state.regs[cpu_rm].l = op->l[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
        writememl(easeg, cpu_state.eaaddr, op->l[0]);
        if (cpu_state.abrt)
            return 1;

        CLOCK_CYCLES(2);
    }

    return 0;
}

/*Cyrix maps both MOVD and SMINT to the same opcode*/
static int
opMOVD_mm_l_a16_cx(uint32_t fetchdat)
{
    const MMX_REG *op;

    if (in_smm)
        return opSMINT(fetchdat);

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    op = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3) {
        cpu_state.regs[cpu_rm].l = op->l[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
        writememl(easeg, cpu_state.eaaddr, op->l[0]);
        if (cpu_state.abrt)
            return 1;

        CLOCK_CYCLES(2);
    }

    return 0;
}
static int
opMOVD_mm_l_a32_cx(uint32_t fetchdat)
{
    const MMX_REG *op;

    if (in_smm)
        return opSMINT(fetchdat);

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    op = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3) {
        cpu_state.regs[cpu_rm].l = op->l[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
        writememl(easeg, cpu_state.eaaddr, op->l[0]);
        if (cpu_state.abrt)
            return 1;

        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVQ_q_mm_a16(uint32_t fetchdat)
{
    uint64_t dst;
    MMX_REG  src;
    MMX_REG *op;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    src = MMX_GETREG(cpu_rm);
    op  = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3) {
        op->q = src.q;
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;

        op->q = dst;
        CLOCK_CYCLES(2);
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opMOVQ_q_mm_a32(uint32_t fetchdat)
{
    uint64_t dst;
    MMX_REG  src;
    MMX_REG *op;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    src = MMX_GETREG(cpu_rm);
    op  = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3) {
        op->q = src.q;
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;

        op->q = dst;
        CLOCK_CYCLES(2);
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opMOVQ_mm_q_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    src = MMX_GETREG(cpu_reg);
    dst = MMX_GETREGP(cpu_rm);

    if (cpu_mod == 3) {
        dst->q = src.q;
        CLOCK_CYCLES(1);

        MMX_SETEXP(cpu_rm);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
        writememq(easeg, cpu_state.eaaddr, src.q);
        if (cpu_state.abrt)
            return 1;

        CLOCK_CYCLES(2);
    }

    return 0;
}
static int
opMOVQ_mm_q_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    src = MMX_GETREG(cpu_reg);
    dst = MMX_GETREGP(cpu_rm);

    if (cpu_mod == 3) {
        dst->q = src.q;
        CLOCK_CYCLES(1);

        MMX_SETEXP(cpu_rm);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
        writememq(easeg, cpu_state.eaaddr, src.q);
        if (cpu_state.abrt)
            return 1;

        CLOCK_CYCLES(2);
    }

    return 0;
}

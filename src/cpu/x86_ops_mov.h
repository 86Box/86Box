static int
opMOV_AL_imm(uint32_t fetchdat)
{
    AL = getbytef();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_AH_imm(uint32_t fetchdat)
{
    AH = getbytef();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_BL_imm(uint32_t fetchdat)
{
    BL = getbytef();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_BH_imm(uint32_t fetchdat)
{
    BH = getbytef();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_CL_imm(uint32_t fetchdat)
{
    CL = getbytef();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_CH_imm(uint32_t fetchdat)
{
    CH = getbytef();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_DL_imm(uint32_t fetchdat)
{
    DL = getbytef();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_DH_imm(uint32_t fetchdat)
{
    DH = getbytef();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, -1, 0, 0, 0, 0, 0);
    return 0;
}

static int
opMOV_AX_imm(uint32_t fetchdat)
{
    AX = getwordf();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_BX_imm(uint32_t fetchdat)
{
    BX = getwordf();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_CX_imm(uint32_t fetchdat)
{
    CX = getwordf();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_DX_imm(uint32_t fetchdat)
{
    DX = getwordf();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_SI_imm(uint32_t fetchdat)
{
    SI = getwordf();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_DI_imm(uint32_t fetchdat)
{
    DI = getwordf();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_BP_imm(uint32_t fetchdat)
{
    BP = getwordf();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_SP_imm(uint32_t fetchdat)
{
    SP = getwordf();
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, -1, 0, 0, 0, 0, 0);
    return 0;
}

static int
opMOV_EAX_imm(UNUSED(uint32_t fetchdat))
{
    uint32_t templ = getlong();
    if (cpu_state.abrt)
        return 1;
    EAX = templ;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 5, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_EBX_imm(UNUSED(uint32_t fetchdat))
{
    uint32_t templ = getlong();
    if (cpu_state.abrt)
        return 1;
    EBX = templ;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 5, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_ECX_imm(UNUSED(uint32_t fetchdat))
{
    uint32_t templ = getlong();
    if (cpu_state.abrt)
        return 1;
    ECX = templ;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 5, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_EDX_imm(UNUSED(uint32_t fetchdat))
{
    uint32_t templ = getlong();
    if (cpu_state.abrt)
        return 1;
    EDX = templ;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 5, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_ESI_imm(UNUSED(uint32_t fetchdat))
{
    uint32_t templ = getlong();
    if (cpu_state.abrt)
        return 1;
    ESI = templ;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 5, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_EDI_imm(UNUSED(uint32_t fetchdat))
{
    uint32_t templ = getlong();
    if (cpu_state.abrt)
        return 1;
    EDI = templ;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 5, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_EBP_imm(UNUSED(uint32_t fetchdat))
{
    uint32_t templ = getlong();
    if (cpu_state.abrt)
        return 1;
    EBP = templ;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 5, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_ESP_imm(UNUSED(uint32_t fetchdat))
{
    uint32_t templ = getlong();
    if (cpu_state.abrt)
        return 1;
    ESP = templ;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 5, -1, 0, 0, 0, 0, 0);
    return 0;
}

static int
opMOV_b_imm_a16(uint32_t fetchdat)
{
    uint8_t temp;
    fetch_ea_16(fetchdat);
    ILLEGAL_ON((rmdat & 0x38) != 0);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = readmemb(cs, cpu_state.pc);
    cpu_state.pc++;
    if (cpu_state.abrt)
        return 1;
    seteab(temp);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, rmdat, 0, 0, (cpu_mod == 3) ? 1 : 0, 0, 0);
    return cpu_state.abrt;
}
static int
opMOV_b_imm_a32(uint32_t fetchdat)
{
    uint8_t temp;
    fetch_ea_32(fetchdat);
    ILLEGAL_ON((rmdat & 0x38) != 0);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = getbyte();
    if (cpu_state.abrt)
        return 1;
    seteab(temp);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 3, rmdat, 0, 0, (cpu_mod == 3) ? 1 : 0, 0, 1);
    return cpu_state.abrt;
}

static int
opMOV_w_imm_a16(uint32_t fetchdat)
{
    uint16_t temp;
    fetch_ea_16(fetchdat);
    ILLEGAL_ON((rmdat & 0x38) != 0);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = getword();
    if (cpu_state.abrt)
        return 1;
    seteaw(temp);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 4, rmdat, 0, 0, (cpu_mod == 3) ? 1 : 0, 0, 0);
    return cpu_state.abrt;
}
static int
opMOV_w_imm_a32(uint32_t fetchdat)
{
    uint16_t temp;
    fetch_ea_32(fetchdat);
    ILLEGAL_ON((rmdat & 0x38) != 0);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = getword();
    if (cpu_state.abrt)
        return 1;
    seteaw(temp);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 4, rmdat, 0, 0, (cpu_mod == 3) ? 1 : 0, 0, 1);
    return cpu_state.abrt;
}
static int
opMOV_l_imm_a16(uint32_t fetchdat)
{
    uint32_t temp;
    fetch_ea_16(fetchdat);
    ILLEGAL_ON((rmdat & 0x38) != 0);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = getlong();
    if (cpu_state.abrt)
        return 1;
    seteal(temp);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 6, rmdat, 0, 0, 0, (cpu_mod == 3) ? 1 : 0, 0);
    return cpu_state.abrt;
}
static int
opMOV_l_imm_a32(uint32_t fetchdat)
{
    uint32_t temp;
    fetch_ea_32(fetchdat);
    ILLEGAL_ON((rmdat & 0x38) != 0);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = getlong();
    if (cpu_state.abrt)
        return 1;
    seteal(temp);
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 6, rmdat, 0, 0, 0, (cpu_mod == 3) ? 1 : 0, 1);
    return cpu_state.abrt;
}

static int
opMOV_AL_a16(uint32_t fetchdat)
{
    uint8_t  temp;
    uint16_t addr = getwordf();
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, addr, addr);
    temp = readmemb(cpu_state.ea_seg->base, addr);
    if (cpu_state.abrt)
        return 1;
    AL = temp;
    CLOCK_CYCLES((is486) ? 1 : 4);
    PREFETCH_RUN(4, 3, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_AL_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t  temp;
    uint32_t addr = getlong();
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, addr, addr);
    temp = readmemb(cpu_state.ea_seg->base, addr);
    if (cpu_state.abrt)
        return 1;
    AL = temp;
    CLOCK_CYCLES((is486) ? 1 : 4);
    PREFETCH_RUN(4, 5, -1, 1, 0, 0, 0, 1);
    return 0;
}
static int
opMOV_AX_a16(uint32_t fetchdat)
{
    uint16_t temp;
    uint16_t addr = getwordf();
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, addr, addr + 1UL);
    temp = readmemw(cpu_state.ea_seg->base, addr);
    if (cpu_state.abrt)
        return 1;
    AX = temp;
    CLOCK_CYCLES((is486) ? 1 : 4);
    PREFETCH_RUN(4, 3, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_AX_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;
    uint32_t addr = getlong();
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, addr, addr + 1);
    temp = readmemw(cpu_state.ea_seg->base, addr);
    if (cpu_state.abrt)
        return 1;
    AX = temp;
    CLOCK_CYCLES((is486) ? 1 : 4);
    PREFETCH_RUN(4, 5, -1, 1, 0, 0, 0, 1);
    return 0;
}
static int
opMOV_EAX_a16(uint32_t fetchdat)
{
    uint32_t temp;
    uint16_t addr = getwordf();
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, addr, addr + 3UL);
    temp = readmeml(cpu_state.ea_seg->base, addr);
    if (cpu_state.abrt)
        return 1;
    EAX = temp;
    CLOCK_CYCLES((is486) ? 1 : 4);
    PREFETCH_RUN(4, 3, -1, 0, 1, 0, 0, 0);
    return 0;
}
static int
opMOV_EAX_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;
    uint32_t addr = getlong();
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, addr, addr + 3);
    temp = readmeml(cpu_state.ea_seg->base, addr);
    if (cpu_state.abrt)
        return 1;
    EAX = temp;
    CLOCK_CYCLES((is486) ? 1 : 4);
    PREFETCH_RUN(4, 5, -1, 0, 1, 0, 0, 1);
    return 0;
}

static int
opMOV_a16_AL(uint32_t fetchdat)
{
    uint16_t addr = getwordf();
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, addr, addr);
    writememb(cpu_state.ea_seg->base, addr, AL);
    CLOCK_CYCLES((is486) ? 1 : 2);
    PREFETCH_RUN(2, 3, -1, 0, 0, 1, 0, 0);
    return cpu_state.abrt;
}
static int
opMOV_a32_AL(UNUSED(uint32_t fetchdat))
{
    uint32_t addr = getlong();
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, addr, addr);
    writememb(cpu_state.ea_seg->base, addr, AL);
    CLOCK_CYCLES((is486) ? 1 : 2);
    PREFETCH_RUN(2, 5, -1, 0, 0, 1, 0, 1);
    return cpu_state.abrt;
}
static int
opMOV_a16_AX(uint32_t fetchdat)
{
    uint16_t addr = getwordf();
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, addr, addr + 1UL);
    writememw(cpu_state.ea_seg->base, addr, AX);
    CLOCK_CYCLES((is486) ? 1 : 2);
    PREFETCH_RUN(2, 3, -1, 0, 0, 1, 0, 0);
    return cpu_state.abrt;
}
static int
opMOV_a32_AX(UNUSED(uint32_t fetchdat))
{
    uint32_t addr = getlong();
    if (cpu_state.abrt)
        return 1;
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, addr, addr + 1);
    writememw(cpu_state.ea_seg->base, addr, AX);
    CLOCK_CYCLES((is486) ? 1 : 2);
    PREFETCH_RUN(2, 5, -1, 0, 0, 1, 0, 1);
    return cpu_state.abrt;
}
static int
opMOV_a16_EAX(uint32_t fetchdat)
{
    uint16_t addr = getwordf();
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, addr, addr + 3UL);
    writememl(cpu_state.ea_seg->base, addr, EAX);
    CLOCK_CYCLES((is486) ? 1 : 2);
    PREFETCH_RUN(2, 3, -1, 0, 0, 0, 1, 0);
    return cpu_state.abrt;
}
static int
opMOV_a32_EAX(UNUSED(uint32_t fetchdat))
{
    uint32_t addr = getlong();
    if (cpu_state.abrt)
        return 1;
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, addr, addr + 3);
    writememl(cpu_state.ea_seg->base, addr, EAX);
    CLOCK_CYCLES((is486) ? 1 : 2);
    PREFETCH_RUN(2, 5, -1, 0, 0, 0, 1, 1);
    return cpu_state.abrt;
}

static int
opLEA_w_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    cpu_state.regs[cpu_reg].w = cpu_state.eaaddr;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);
    return 0;
}
static int
opLEA_w_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    cpu_state.regs[cpu_reg].w = cpu_state.eaaddr;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);
    return 0;
}

static int
opLEA_l_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    cpu_state.regs[cpu_reg].l = cpu_state.eaaddr & 0xffff;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);
    return 0;
}
static int
opLEA_l_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    cpu_state.regs[cpu_reg].l = cpu_state.eaaddr;
    CLOCK_CYCLES(timing_rr);
    PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);
    return 0;
}

static int
opXLAT_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t addr = (BX + AL) & 0xFFFF;
    uint8_t  temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    temp = readmemb(cpu_state.ea_seg->base, addr);
    if (cpu_state.abrt)
        return 1;
    AL = temp;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opXLAT_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t addr = EBX + AL;
    uint8_t  temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    temp = readmemb(cpu_state.ea_seg->base, addr);
    if (cpu_state.abrt)
        return 1;
    AL = temp;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 1);
    return 0;
}

static int
opMOV_b_r_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        setr8(cpu_rm, getr8(cpu_reg));
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteab(getr8(cpu_reg));
        CLOCK_CYCLES(is486 ? 1 : 2);
        PREFETCH_RUN(2, 2, rmdat, 0, 0, 1, 0, 0);
    }
    return cpu_state.abrt;
}
static int
opMOV_b_r_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        setr8(cpu_rm, getr8(cpu_reg));
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteab(getr8(cpu_reg));
        CLOCK_CYCLES(is486 ? 1 : 2);
        PREFETCH_RUN(2, 2, rmdat, 0, 0, 1, 0, 1);
    }
    return cpu_state.abrt;
}
static int
opMOV_w_r_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.regs[cpu_rm].w = cpu_state.regs[cpu_reg].w;
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteaw(cpu_state.regs[cpu_reg].w);
        CLOCK_CYCLES(is486 ? 1 : 2);
        PREFETCH_RUN(2, 2, rmdat, 0, 0, 1, 0, 0);
    }
    return cpu_state.abrt;
}
static int
opMOV_w_r_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.regs[cpu_rm].w = cpu_state.regs[cpu_reg].w;
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteaw(cpu_state.regs[cpu_reg].w);
        CLOCK_CYCLES(is486 ? 1 : 2);
        PREFETCH_RUN(2, 2, rmdat, 0, 0, 1, 0, 1);
    }
    return cpu_state.abrt;
}
static int
opMOV_l_r_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.regs[cpu_rm].l = cpu_state.regs[cpu_reg].l;
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteal(cpu_state.regs[cpu_reg].l);
        CLOCK_CYCLES(is486 ? 1 : 2);
        PREFETCH_RUN(2, 2, rmdat, 0, 0, 0, 1, 0);
    }
    return cpu_state.abrt;
}
static int
opMOV_l_r_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.regs[cpu_rm].l = cpu_state.regs[cpu_reg].l;
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteal(cpu_state.regs[cpu_reg].l);
        CLOCK_CYCLES(is486 ? 1 : 2);
        PREFETCH_RUN(2, 2, rmdat, 0, 0, 0, 1, 1);
    }
    return cpu_state.abrt;
}

static int
opMOV_r_b_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        setr8(cpu_reg, getr8(cpu_rm));
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);
    } else {
        uint8_t temp;
        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr);
        temp = geteab();
        if (cpu_state.abrt)
            return 1;
        setr8(cpu_reg, temp);
        CLOCK_CYCLES(is486 ? 1 : 4);
        PREFETCH_RUN(4, 2, rmdat, 1, 0, 0, 0, 0);
    }
    return 0;
}
static int
opMOV_r_b_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        setr8(cpu_reg, getr8(cpu_rm));
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);
    } else {
        uint8_t temp;
        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr);
        temp = geteab();
        if (cpu_state.abrt)
            return 1;
        setr8(cpu_reg, temp);
        CLOCK_CYCLES(is486 ? 1 : 4);
        PREFETCH_RUN(4, 2, rmdat, 1, 0, 0, 0, 1);
    }
    return 0;
}
static int
opMOV_r_w_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.regs[cpu_reg].w = cpu_state.regs[cpu_rm].w;
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);
    } else {
        uint16_t temp;
        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1);
        temp = geteaw();
        if (cpu_state.abrt)
            return 1;
        cpu_state.regs[cpu_reg].w = temp;
        CLOCK_CYCLES((is486) ? 1 : 4);
        PREFETCH_RUN(4, 2, rmdat, 1, 0, 0, 0, 0);
    }
    return 0;
}
static int
opMOV_r_w_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.regs[cpu_reg].w = cpu_state.regs[cpu_rm].w;
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);
    } else {
        uint16_t temp;
        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1);
        temp = geteaw();
        if (cpu_state.abrt)
            return 1;
        cpu_state.regs[cpu_reg].w = temp;
        CLOCK_CYCLES((is486) ? 1 : 4);
        PREFETCH_RUN(4, 2, rmdat, 1, 0, 0, 0, 1);
    }
    return 0;
}
static int
opMOV_r_l_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.regs[cpu_reg].l = cpu_state.regs[cpu_rm].l;
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);
    } else {
        uint32_t temp;
        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
        temp = geteal();
        if (cpu_state.abrt)
            return 1;
        cpu_state.regs[cpu_reg].l = temp;
        CLOCK_CYCLES(is486 ? 1 : 4);
        PREFETCH_RUN(4, 2, rmdat, 0, 1, 0, 0, 0);
    }
    return 0;
}
static int
opMOV_r_l_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.regs[cpu_reg].l = cpu_state.regs[cpu_rm].l;
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);
    } else {
        uint32_t temp;
        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
        temp = geteal();
        if (cpu_state.abrt)
            return 1;
        cpu_state.regs[cpu_reg].l = temp;
        CLOCK_CYCLES(is486 ? 1 : 4);
        PREFETCH_RUN(4, 2, rmdat, 0, 1, 0, 0, 1);
    }
    return 0;
}

#ifndef OPS_286_386
#    define opCMOV(condition)                                                             \
        static int opCMOV##condition##_w_a16(uint32_t fetchdat)                           \
        {                                                                                 \
            fetch_ea_16(fetchdat);                                                        \
            if (cond_##condition) {                                                       \
                if (cpu_mod == 3)                                                         \
                    cpu_state.regs[cpu_reg].w = cpu_state.regs[cpu_rm].w;                 \
                else {                                                                    \
                    uint16_t temp;                                                        \
                    SEG_CHECK_READ(cpu_state.ea_seg);                                     \
                    CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1); \
                    temp = geteaw();                                                      \
                    if (cpu_state.abrt)                                                   \
                        return 1;                                                         \
                    cpu_state.regs[cpu_reg].w = temp;                                     \
                }                                                                         \
            }                                                                             \
            CLOCK_CYCLES(1);                                                              \
            return 0;                                                                     \
        }                                                                                 \
        static int opCMOV##condition##_w_a32(uint32_t fetchdat)                           \
        {                                                                                 \
            fetch_ea_32(fetchdat);                                                        \
            if (cond_##condition) {                                                       \
                if (cpu_mod == 3)                                                         \
                    cpu_state.regs[cpu_reg].w = cpu_state.regs[cpu_rm].w;                 \
                else {                                                                    \
                    uint16_t temp;                                                        \
                    SEG_CHECK_READ(cpu_state.ea_seg);                                     \
                    CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1); \
                    temp = geteaw();                                                      \
                    if (cpu_state.abrt)                                                   \
                        return 1;                                                         \
                    cpu_state.regs[cpu_reg].w = temp;                                     \
                }                                                                         \
            }                                                                             \
            CLOCK_CYCLES(1);                                                              \
            return 0;                                                                     \
        }                                                                                 \
        static int opCMOV##condition##_l_a16(uint32_t fetchdat)                           \
        {                                                                                 \
            fetch_ea_16(fetchdat);                                                        \
            if (cond_##condition) {                                                       \
                if (cpu_mod == 3)                                                         \
                    cpu_state.regs[cpu_reg].l = cpu_state.regs[cpu_rm].l;                 \
                else {                                                                    \
                    uint32_t temp;                                                        \
                    SEG_CHECK_READ(cpu_state.ea_seg);                                     \
                    CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3); \
                    temp = geteal();                                                      \
                    if (cpu_state.abrt)                                                   \
                        return 1;                                                         \
                    cpu_state.regs[cpu_reg].l = temp;                                     \
                }                                                                         \
            }                                                                             \
            CLOCK_CYCLES(1);                                                              \
            return 0;                                                                     \
        }                                                                                 \
        static int opCMOV##condition##_l_a32(uint32_t fetchdat)                           \
        {                                                                                 \
            fetch_ea_32(fetchdat);                                                        \
            if (cond_##condition) {                                                       \
                if (cpu_mod == 3)                                                         \
                    cpu_state.regs[cpu_reg].l = cpu_state.regs[cpu_rm].l;                 \
                else {                                                                    \
                    uint32_t temp;                                                        \
                    CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3); \
                    SEG_CHECK_READ(cpu_state.ea_seg);                                     \
                    temp = geteal();                                                      \
                    if (cpu_state.abrt)                                                   \
                        return 1;                                                         \
                    cpu_state.regs[cpu_reg].l = temp;                                     \
                }                                                                         \
            }                                                                             \
            CLOCK_CYCLES(1);                                                              \
            return 0;                                                                     \
        }

// clang-format off
opCMOV(O)
opCMOV(NO)
opCMOV(B)
opCMOV(NB)
opCMOV(E)
opCMOV(NE)
opCMOV(BE)
opCMOV(NBE)
opCMOV(S)
opCMOV(NS)
opCMOV(P)
opCMOV(NP)
opCMOV(L)
opCMOV(NL)
opCMOV(LE)
opCMOV(NLE)
// clang-format on
#endif

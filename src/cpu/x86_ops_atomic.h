static int
opCMPXCHG_b_a16(uint32_t fetchdat)
{
    uint8_t temp;
    uint8_t temp2 = AL;

    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = geteab();
    if (cpu_state.abrt)
        return 1;
    if (AL == temp)
        seteab(getr8(cpu_reg));
    else
        AL = temp;
    if (cpu_state.abrt)
        return 1;
    setsub8(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}
static int
opCMPXCHG_b_a32(uint32_t fetchdat)
{
    uint8_t temp;
    uint8_t temp2 = AL;

    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = geteab();
    if (cpu_state.abrt)
        return 1;
    if (AL == temp)
        seteab(getr8(cpu_reg));
    else
        AL = temp;
    if (cpu_state.abrt)
        return 1;
    setsub8(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}

static int
opCMPXCHG_w_a16(uint32_t fetchdat)
{
    uint16_t temp;
    uint16_t temp2 = AX;

    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = geteaw();
    if (cpu_state.abrt)
        return 1;
    if (AX == temp)
        seteaw(cpu_state.regs[cpu_reg].w);
    else
        AX = temp;
    if (cpu_state.abrt)
        return 1;
    setsub16(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}
static int
opCMPXCHG_w_a32(uint32_t fetchdat)
{
    uint16_t temp;
    uint16_t temp2 = AX;

    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = geteaw();
    if (cpu_state.abrt)
        return 1;
    if (AX == temp)
        seteaw(cpu_state.regs[cpu_reg].w);
    else
        AX = temp;
    if (cpu_state.abrt)
        return 1;
    setsub16(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}

static int
opCMPXCHG_l_a16(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t temp2 = EAX;

    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = geteal();
    if (cpu_state.abrt)
        return 1;
    if (EAX == temp)
        seteal(cpu_state.regs[cpu_reg].l);
    else
        EAX = temp;
    if (cpu_state.abrt)
        return 1;
    setsub32(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}
static int
opCMPXCHG_l_a32(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t temp2 = EAX;

    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp = geteal();
    if (cpu_state.abrt)
        return 1;
    if (EAX == temp)
        seteal(cpu_state.regs[cpu_reg].l);
    else
        EAX = temp;
    if (cpu_state.abrt)
        return 1;
    setsub32(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}

#ifndef OPS_286_386
static int
opCMPXCHG8B_a16(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t temp_hi;
    uint32_t temp2    = EAX;
    uint32_t temp2_hi = EDX;

    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp    = geteal();
    temp_hi = readmeml(easeg, cpu_state.eaaddr + 4);
    if (cpu_state.abrt)
        return 0;
    if (EAX == temp && EDX == temp_hi) {
        seteal(EBX);
        writememl(easeg, cpu_state.eaaddr + 4, ECX);
    } else {
        EAX = temp;
        EDX = temp_hi;
    }
    if (cpu_state.abrt)
        return 0;
    flags_rebuild();
    if (temp == temp2 && temp_hi == temp2_hi)
        cpu_state.flags |= Z_FLAG;
    else
        cpu_state.flags &= ~Z_FLAG;
    cycles -= (cpu_mod == 3) ? 6 : 10;
    return 0;
}
static int
opCMPXCHG8B_a32(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t temp_hi;
    uint32_t temp2    = EAX;
    uint32_t temp2_hi = EDX;

    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp    = geteal();
    temp_hi = readmeml(easeg, cpu_state.eaaddr + 4);
    if (cpu_state.abrt)
        return 0;
    if (EAX == temp && EDX == temp_hi) {
        seteal(EBX);
        writememl(easeg, cpu_state.eaaddr + 4, ECX);
    } else {
        EAX = temp;
        EDX = temp_hi;
    }
    if (cpu_state.abrt)
        return 0;
    flags_rebuild();
    if (temp == temp2 && temp_hi == temp2_hi)
        cpu_state.flags |= Z_FLAG;
    else
        cpu_state.flags &= ~Z_FLAG;
    cycles -= (cpu_mod == 3) ? 6 : 10;
    return 0;
}
#endif

/* dest = eab, src = r8 */
static int
opXADD_b_a16(uint32_t fetchdat)
{
    uint8_t temp;
    uint8_t src;
    uint8_t dest;

    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = getr8(cpu_reg);
    dest = geteab();
    if (cpu_state.abrt)
        return 1;
    temp = src + dest;
    seteab(temp);
    if (cpu_state.abrt)
        return 1;
    setadd8(src, dest);
    setr8(cpu_reg, dest);
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}
static int
opXADD_b_a32(uint32_t fetchdat)
{
    uint8_t temp;
    uint8_t src;
    uint8_t dest;

    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = getr8(cpu_reg);
    dest = geteab();
    if (cpu_state.abrt)
        return 1;
    temp = src + dest;
    seteab(temp);
    if (cpu_state.abrt)
        return 1;
    setadd8(src, dest);
    setr8(cpu_reg, dest);
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}

static int
opXADD_w_a16(uint32_t fetchdat)
{
    uint16_t temp;
    uint16_t src;
    uint16_t dest;

    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = cpu_state.regs[cpu_reg].w;
    dest = geteaw();
    if (cpu_state.abrt)
        return 1;
    temp = src + dest;
    seteaw(temp);
    if (cpu_state.abrt)
        return 1;
    setadd16(src, dest);
    cpu_state.regs[cpu_reg].w = dest;
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}
static int
opXADD_w_a32(uint32_t fetchdat)
{
    uint16_t temp;
    uint16_t src;
    uint16_t dest;

    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = cpu_state.regs[cpu_reg].w;
    dest = geteaw();
    if (cpu_state.abrt)
        return 1;
    temp = src + dest;
    seteaw(temp);
    if (cpu_state.abrt)
        return 1;
    setadd16(src, dest);
    cpu_state.regs[cpu_reg].w = dest;
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}

static int
opXADD_l_a16(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t src;
    uint32_t dest;

    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = cpu_state.regs[cpu_reg].l;
    dest = geteal();
    if (cpu_state.abrt)
        return 1;
    temp = src + dest;
    seteal(temp);
    if (cpu_state.abrt)
        return 1;
    setadd32(src, dest);
    cpu_state.regs[cpu_reg].l = dest;
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}
static int
opXADD_l_a32(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t src;
    uint32_t dest;

    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = cpu_state.regs[cpu_reg].l;
    dest = geteal();
    if (cpu_state.abrt)
        return 1;
    temp = src + dest;
    seteal(temp);
    if (cpu_state.abrt)
        return 1;
    setadd32(src, dest);
    cpu_state.regs[cpu_reg].l = dest;
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}

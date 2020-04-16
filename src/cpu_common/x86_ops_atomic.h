static int opCMPXCHG_b_a16(uint32_t fetchdat)
{
        uint8_t temp, temp2 = AL;
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        if (AL == temp) seteab(getr8(cpu_reg));
        else            AL = temp;
        if (cpu_state.abrt) return 1;
        setsub8(temp2, temp);
        CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
        return 0;
}
static int opCMPXCHG_b_a32(uint32_t fetchdat)
{
        uint8_t temp, temp2 = AL;
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        if (AL == temp) seteab(getr8(cpu_reg));
        else            AL = temp;
        if (cpu_state.abrt) return 1;
        setsub8(temp2, temp);
        CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
        return 0;
}

static int opCMPXCHG_w_a16(uint32_t fetchdat)
{
        uint16_t temp, temp2 = AX;
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        if (AX == temp) seteaw(cpu_state.regs[cpu_reg].w);
        else            AX = temp;
        if (cpu_state.abrt) return 1;
        setsub16(temp2, temp);
        CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
        return 0;
}
static int opCMPXCHG_w_a32(uint32_t fetchdat)
{
        uint16_t temp, temp2 = AX;
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        if (AX == temp) seteaw(cpu_state.regs[cpu_reg].w);
        else            AX = temp;
        if (cpu_state.abrt) return 1;
        setsub16(temp2, temp);
        CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
        return 0;
}

static int opCMPXCHG_l_a16(uint32_t fetchdat)
{
        uint32_t temp, temp2 = EAX;
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteal();                        if (cpu_state.abrt) return 1;
        if (EAX == temp) seteal(cpu_state.regs[cpu_reg].l);
        else             EAX = temp;
        if (cpu_state.abrt) return 1;
        setsub32(temp2, temp);
        CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
        return 0;
}
static int opCMPXCHG_l_a32(uint32_t fetchdat)
{
        uint32_t temp, temp2 = EAX;
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteal();                        if (cpu_state.abrt) return 1;
        if (EAX == temp) seteal(cpu_state.regs[cpu_reg].l);
        else             EAX = temp;
        if (cpu_state.abrt) return 1;
        setsub32(temp2, temp);
        CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
        return 0;
}

static int opCMPXCHG8B_a16(uint32_t fetchdat)
{
        uint32_t temp, temp_hi, temp2 = EAX, temp2_hi = EDX;
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteal();
        temp_hi = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 0;
        if (EAX == temp && EDX == temp_hi)
        {
                seteal(EBX);
                writememl(easeg, cpu_state.eaaddr+4, ECX);
        }
        else
        {
                EAX = temp;
                EDX = temp_hi;
        }
        if (cpu_state.abrt) return 0;
        flags_rebuild();
        if (temp == temp2 && temp_hi == temp2_hi)
                cpu_state.flags |= Z_FLAG;
        else
                cpu_state.flags &= ~Z_FLAG;
        cycles -= (cpu_mod == 3) ? 6 : 10;
        return 0;
}
static int opCMPXCHG8B_a32(uint32_t fetchdat)
{
        uint32_t temp, temp_hi, temp2 = EAX, temp2_hi = EDX;
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteal();
        temp_hi = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 0;
        if (EAX == temp && EDX == temp_hi)
        {
                seteal(EBX);
                writememl(easeg, cpu_state.eaaddr+4, ECX);
        }
        else
        {
                EAX = temp;
                EDX = temp_hi;
        }
        if (cpu_state.abrt) return 0;
        flags_rebuild();
        if (temp == temp2 && temp_hi == temp2_hi)
                cpu_state.flags |= Z_FLAG;
        else
                cpu_state.flags &= ~Z_FLAG;
        cycles -= (cpu_mod == 3) ? 6 : 10;
        return 0;
}

static int opXADD_b_a16(uint32_t fetchdat)
{
        uint8_t temp, temp2;
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
	temp2 = getr8(cpu_reg);
        setr8(cpu_reg, temp);
        seteab(temp + temp2);              if (cpu_state.abrt) return 1;
        setadd8(temp, temp2);
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}
static int opXADD_b_a32(uint32_t fetchdat)
{
        uint8_t temp, temp2;
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
	temp2 = getr8(cpu_reg);
        setr8(cpu_reg, temp);
        seteab(temp + temp2);              if (cpu_state.abrt) return 1;
        setadd8(temp, temp2);
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}

static int opXADD_w_a16(uint32_t fetchdat)
{
        uint16_t temp, temp2;
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
	temp2 = cpu_state.regs[cpu_reg].w;
        cpu_state.regs[cpu_reg].w = temp;
        seteaw(temp + temp2);   if (cpu_state.abrt) return 1;
        setadd16(temp, temp2);
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}
static int opXADD_w_a32(uint32_t fetchdat)
{
        uint16_t temp, temp2;
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
	temp2 = cpu_state.regs[cpu_reg].w;
        cpu_state.regs[cpu_reg].w = temp;
        seteaw(temp + temp2);   if (cpu_state.abrt) return 1;
        setadd16(temp, temp2);
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}

static int opXADD_l_a16(uint32_t fetchdat)
{
        uint32_t temp, temp2;
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteal();                        if (cpu_state.abrt) return 1;
	temp2 = cpu_state.regs[cpu_reg].l;
        cpu_state.regs[cpu_reg].l = temp;
        seteal(temp + temp2);   if (cpu_state.abrt) return 1;
        setadd32(temp, temp2);
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}
static int opXADD_l_a32(uint32_t fetchdat)
{
        uint32_t temp, temp2;
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteal();                        if (cpu_state.abrt) return 1;
	temp2 = cpu_state.regs[cpu_reg].l;
        cpu_state.regs[cpu_reg].l = temp;
        seteal(temp + temp2);   if (cpu_state.abrt) return 1;
        setadd32(temp, temp2);
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}

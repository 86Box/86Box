static int opCMPXCHG_b_a16(uint32_t fetchdat)
{
        uint8_t temp, temp2 = AL;
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
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
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
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
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
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
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
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
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
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
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
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
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 0;
        }
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
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 0;
        }
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
        uint8_t temp;
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        seteab(temp + getr8(cpu_reg));              if (cpu_state.abrt) return 1;
        setadd8(temp, getr8(cpu_reg));
        setr8(cpu_reg, temp);
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}
static int opXADD_b_a32(uint32_t fetchdat)
{
        uint8_t temp;
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        seteab(temp + getr8(cpu_reg));              if (cpu_state.abrt) return 1;
        setadd8(temp, getr8(cpu_reg));
        setr8(cpu_reg, temp);
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}

static int opXADD_w_a16(uint32_t fetchdat)
{
        uint16_t temp;
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        seteaw(temp + cpu_state.regs[cpu_reg].w);   if (cpu_state.abrt) return 1;
        setadd16(temp, cpu_state.regs[cpu_reg].w);
        cpu_state.regs[cpu_reg].w = temp;
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}
static int opXADD_w_a32(uint32_t fetchdat)
{
        uint16_t temp;
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        seteaw(temp + cpu_state.regs[cpu_reg].w);   if (cpu_state.abrt) return 1;
        setadd16(temp, cpu_state.regs[cpu_reg].w);
        cpu_state.regs[cpu_reg].w = temp;
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}

static int opXADD_l_a16(uint32_t fetchdat)
{
        uint32_t temp;
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteal();                        if (cpu_state.abrt) return 1;
        seteal(temp + cpu_state.regs[cpu_reg].l);   if (cpu_state.abrt) return 1;
        setadd32(temp, cpu_state.regs[cpu_reg].l);
        cpu_state.regs[cpu_reg].l = temp;
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}
static int opXADD_l_a32(uint32_t fetchdat)
{
        uint32_t temp;
        if (!is486)
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp = geteal();                        if (cpu_state.abrt) return 1;
        seteal(temp + cpu_state.regs[cpu_reg].l);   if (cpu_state.abrt) return 1;
        setadd32(temp, cpu_state.regs[cpu_reg].l);
        cpu_state.regs[cpu_reg].l = temp;
        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
        return 0;
}

static int opMOVZX_w_b_a16(uint32_t fetchdat)
{
        uint8_t temp;
        
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].w = (uint16_t)temp;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
        return 0;
}
static int opMOVZX_w_b_a32(uint32_t fetchdat)
{
        uint8_t temp;
        
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].w = (uint16_t)temp;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
        return 0;
}
static int opMOVZX_l_b_a16(uint32_t fetchdat)
{
        uint8_t temp;
        
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = (uint32_t)temp;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
        return 0;
}
static int opMOVZX_l_b_a32(uint32_t fetchdat)
{
        uint8_t temp;
        
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = (uint32_t)temp;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
        return 0;
}
static int opMOVZX_w_w_a16(uint32_t fetchdat)
{
        uint16_t temp;
        
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].w = temp;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
        return 0;
}
static int opMOVZX_w_w_a32(uint32_t fetchdat)
{
        uint16_t temp;
        
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].w = temp;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
        return 0;
}
static int opMOVZX_l_w_a16(uint32_t fetchdat)
{
        uint16_t temp;
        
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = (uint32_t)temp;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
        return 0;
}
static int opMOVZX_l_w_a32(uint32_t fetchdat)
{
        uint16_t temp;
        
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = (uint32_t)temp;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
        return 0;
}

static int opMOVSX_w_b_a16(uint32_t fetchdat)
{
        uint8_t temp;
        
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].w = (uint16_t)temp;
        if (temp & 0x80)        
                cpu_state.regs[cpu_reg].w |= 0xff00;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
        return 0;
}
static int opMOVSX_w_b_a32(uint32_t fetchdat)
{
        uint8_t temp;
        
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].w = (uint16_t)temp;
        if (temp & 0x80)        
                cpu_state.regs[cpu_reg].w |= 0xff00;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
        return 0;
}
static int opMOVSX_l_b_a16(uint32_t fetchdat)
{
        uint8_t temp;
        
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = (uint32_t)temp;
        if (temp & 0x80)        
                cpu_state.regs[cpu_reg].l |= 0xffffff00;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
        return 0;
}
static int opMOVSX_l_b_a32(uint32_t fetchdat)
{
        uint8_t temp;
        
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteab();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = (uint32_t)temp;
        if (temp & 0x80)        
                cpu_state.regs[cpu_reg].l |= 0xffffff00;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
        return 0;
}
static int opMOVSX_l_w_a16(uint32_t fetchdat)
{
        uint16_t temp;
        
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = (uint32_t)temp;
        if (temp & 0x8000)
                cpu_state.regs[cpu_reg].l |= 0xffff0000;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
        return 0;
}
static int opMOVSX_l_w_a32(uint32_t fetchdat)
{
        uint16_t temp;
        
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        cpu_state.regs[cpu_reg].l = (uint32_t)temp;
        if (temp & 0x8000)
                cpu_state.regs[cpu_reg].l |= 0xffff0000;
        
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
        return 0;
}

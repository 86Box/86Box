static int opMOVD_l_mm_a16(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                MM[cpu_reg].l[0] = cpu_state.regs[cpu_rm].l;
                MM[cpu_reg].l[1] = 0;
                CLOCK_CYCLES(1);
        }
        else
        {
                uint32_t dst;
        
                dst = readmeml(easeg, eaaddr); if (abrt) return 1;
                MM[cpu_reg].l[0] = dst;
                MM[cpu_reg].l[1] = 0;

                CLOCK_CYCLES(2);
        }
        return 0;
}
static int opMOVD_l_mm_a32(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                MM[cpu_reg].l[0] = cpu_state.regs[cpu_rm].l;
                MM[cpu_reg].l[1] = 0;
                CLOCK_CYCLES(1);
        }
        else
        {
                uint32_t dst;
        
                dst = readmeml(easeg, eaaddr); if (abrt) return 1;
                MM[cpu_reg].l[0] = dst;
                MM[cpu_reg].l[1] = 0;

                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opMOVD_mm_l_a16(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                cpu_state.regs[cpu_rm].l = MM[cpu_reg].l[0];
                CLOCK_CYCLES(1);
        }
        else
        {
                CHECK_WRITE(ea_seg, eaaddr, eaaddr + 3);
                writememl(easeg, eaaddr, MM[cpu_reg].l[0]); if (abrt) return 1;
                CLOCK_CYCLES(2);
        }
        return 0;
}
static int opMOVD_mm_l_a32(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                cpu_state.regs[cpu_rm].l = MM[cpu_reg].l[0];
                CLOCK_CYCLES(1);
        }
        else
        {
                CHECK_WRITE(ea_seg, eaaddr, eaaddr + 3);
                writememl(easeg, eaaddr, MM[cpu_reg].l[0]); if (abrt) return 1;
                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opMOVQ_q_mm_a16(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                MM[cpu_reg].q = MM[cpu_rm].q;
                CLOCK_CYCLES(1);
        }
        else
        {
                uint64_t dst;
        
                dst = readmemq(easeg, eaaddr); if (abrt) return 1;
                MM[cpu_reg].q = dst;
                CLOCK_CYCLES(2);
        }
        return 0;
}
static int opMOVQ_q_mm_a32(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                MM[cpu_reg].q = MM[cpu_rm].q;
                CLOCK_CYCLES(1);
        }
        else
        {
                uint64_t dst;
        
                dst = readmemq(easeg, eaaddr); if (abrt) return 1;
                MM[cpu_reg].q = dst;
                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opMOVQ_mm_q_a16(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                MM[cpu_rm].q = MM[cpu_reg].q;
                CLOCK_CYCLES(1);
        }
        else
        {
                CHECK_WRITE(ea_seg, eaaddr, eaaddr + 7);
                writememq(easeg, eaaddr,     MM[cpu_reg].l[0]); if (abrt) return 1;
                CLOCK_CYCLES(2);
        }
        return 0;
}
static int opMOVQ_mm_q_a32(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                MM[cpu_rm].q = MM[cpu_reg].q;
                CLOCK_CYCLES(1);
        }
        else
        {
                CHECK_WRITE(ea_seg, eaaddr, eaaddr + 7);
                writememq(easeg, eaaddr,     MM[cpu_reg].q); if (abrt) return 1;
                CLOCK_CYCLES(2);
        }
        return 0;
}

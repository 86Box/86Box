static int opMOVUPS_q_xmm_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                cpu_state.XMM[cpu_reg].f[0] = cpu_state.XMM[cpu_rm].f[0];
                cpu_state.XMM[cpu_reg].f[1] = cpu_state.XMM[cpu_rm].f[1];
                cpu_state.XMM[cpu_reg].f[2] = cpu_state.XMM[cpu_rm].f[2];
                cpu_state.XMM[cpu_reg].f[3] = cpu_state.XMM[cpu_rm].f[3];
                CLOCK_CYCLES(1);
        }
        else
        {
                uint64_t dst[4];

                SEG_CHECK_READ(cpu_state.ea_seg);
                dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
                dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
                dst[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
                dst[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
                cpu_state.XMM[cpu_reg].l[0] = dst[0];
                cpu_state.XMM[cpu_reg].l[1] = dst[1];
                cpu_state.XMM[cpu_reg].l[2] = dst[2];
                cpu_state.XMM[cpu_reg].l[3] = dst[3];

                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opMOVUPS_q_xmm_a32(uint32_t fetchdat)
{       
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                cpu_state.XMM[cpu_reg].f[0] = cpu_state.XMM[cpu_rm].f[0];
                cpu_state.XMM[cpu_reg].f[1] = cpu_state.XMM[cpu_rm].f[1];
                cpu_state.XMM[cpu_reg].f[2] = cpu_state.XMM[cpu_rm].f[2];
                cpu_state.XMM[cpu_reg].f[3] = cpu_state.XMM[cpu_rm].f[3];
                CLOCK_CYCLES(1);
        }
        else
        {
                uint64_t dst[4];

                SEG_CHECK_READ(cpu_state.ea_seg);
                dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
                dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
                dst[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
                dst[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
                cpu_state.XMM[cpu_reg].l[0] = dst[0];
                cpu_state.XMM[cpu_reg].l[1] = dst[1];
                cpu_state.XMM[cpu_reg].l[2] = dst[2];
                cpu_state.XMM[cpu_reg].l[3] = dst[3];

                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opMOVUPS_xmm_q_a16(uint32_t fetchdat)
{       
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_rm].f[0] = cpu_state.XMM[cpu_reg].f[0];
        cpu_state.XMM[cpu_rm].f[1] = cpu_state.XMM[cpu_reg].f[1];
        cpu_state.XMM[cpu_rm].f[2] = cpu_state.XMM[cpu_reg].f[2];
        cpu_state.XMM[cpu_rm].f[3] = cpu_state.XMM[cpu_reg].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm[4] = {cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3]};
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememl(easeg, cpu_state.eaaddr, rm[0]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 4, rm[1]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 8, rm[2]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 12, rm[3]); if (cpu_state.abrt) return 1;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVUPS_xmm_q_a32(uint32_t fetchdat)
{       
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_rm].f[0] = cpu_state.XMM[cpu_reg].f[0];
        cpu_state.XMM[cpu_rm].f[1] = cpu_state.XMM[cpu_reg].f[1];
        cpu_state.XMM[cpu_rm].f[2] = cpu_state.XMM[cpu_reg].f[2];
        cpu_state.XMM[cpu_rm].f[3] = cpu_state.XMM[cpu_reg].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm[4] = {cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3]};
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememl(easeg, cpu_state.eaaddr, rm[0]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 4, rm[1]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 8, rm[2]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 12, rm[3]); if (cpu_state.abrt) return 1;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVSS_f_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
            cpu_state.XMM[cpu_reg].f[0] = cpu_state.XMM[cpu_rm].f[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst;
        cpu_state.XMM[cpu_reg].l[1] = 0;
        cpu_state.XMM[cpu_reg].l[2] = 0;
        cpu_state.XMM[cpu_reg].l[3] = 0;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVSS_f_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
            cpu_state.XMM[cpu_reg].f[0] = cpu_state.XMM[cpu_rm].f[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst;
        cpu_state.XMM[cpu_reg].l[1] = 0;
        cpu_state.XMM[cpu_reg].l[2] = 0;
        cpu_state.XMM[cpu_reg].l[3] = 0;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVSS_xmm_f_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
            cpu_state.XMM[cpu_rm].f[0] = cpu_state.XMM[cpu_reg].f[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm = cpu_state.XMM[cpu_reg].l[0];

        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememl(easeg, cpu_state.eaaddr, rm); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVSS_xmm_f_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
            cpu_state.XMM[cpu_rm].f[0] = cpu_state.XMM[cpu_reg].f[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm = cpu_state.XMM[cpu_reg].l[0];

        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememl(easeg, cpu_state.eaaddr, rm); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVLPS_f_xmm_MOVHLPS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        //MOVHLPS   
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        //MOVLPS    
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].q[0] = dst;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVLPS_f_xmm_MOVHLPS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        //MOVHLPS   
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        //MOVLPS    
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].q[0] = dst;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVLPS_xmm_f_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_rm].q[0]); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVLPS_xmm_f_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_rm].q[0]); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUNPCKLPS_f_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] = cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[3] = cpu_state.XMM[cpu_rm].l[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst[0];
        cpu_state.XMM[cpu_reg].l[1] = dst[0];
        cpu_state.XMM[cpu_reg].l[2] = dst[1];
        cpu_state.XMM[cpu_reg].l[3] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUNPCKLPS_f_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] = cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[3] = cpu_state.XMM[cpu_rm].l[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst[0];
        cpu_state.XMM[cpu_reg].l[1] = dst[0];
        cpu_state.XMM[cpu_reg].l[2] = dst[1];
        cpu_state.XMM[cpu_reg].l[3] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUNPCKHPS_f_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[1] = cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_rm].l[3];
        cpu_state.XMM[cpu_reg].l[3] = cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst[0];
        cpu_state.XMM[cpu_reg].l[1] = dst[0];
        cpu_state.XMM[cpu_reg].l[2] = dst[1];
        cpu_state.XMM[cpu_reg].l[3] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUNPCKHPS_f_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] = cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[3] = cpu_state.XMM[cpu_rm].l[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst[0];
        cpu_state.XMM[cpu_reg].l[1] = dst[0];
        cpu_state.XMM[cpu_reg].l[2] = dst[1];
        cpu_state.XMM[cpu_reg].l[3] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVHPS_f_xmm_MOVLHPS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        //MOVLHPS   
        cpu_state.XMM[cpu_reg].q[1] = cpu_state.XMM[cpu_rm].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        //MOVHPS    
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].q[1] = dst;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVHPS_f_xmm_MOVLHPS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        //MOVLHPS   
        cpu_state.XMM[cpu_reg].q[1] = cpu_state.XMM[cpu_rm].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        //MOVHPS    
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].q[1] = dst;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVHPS_xmm_f_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_rm].q[1] = cpu_state.XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr + 8, cpu_state.XMM[cpu_rm].q[0]); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVHPS_xmm_f_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_rm].q[1] = cpu_state.XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr + 8, cpu_state.XMM[cpu_rm].q[0]); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVAPS_q_xmm_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                cpu_state.XMM[cpu_reg].f[0] = cpu_state.XMM[cpu_rm].f[0];
                cpu_state.XMM[cpu_reg].f[1] = cpu_state.XMM[cpu_rm].f[1];
                cpu_state.XMM[cpu_reg].f[2] = cpu_state.XMM[cpu_rm].f[2];
                cpu_state.XMM[cpu_reg].f[3] = cpu_state.XMM[cpu_rm].f[3];
                CLOCK_CYCLES(1);
        }
        else
        {
                uint64_t dst[4];

                SEG_CHECK_READ(cpu_state.ea_seg);
                if(cpu_state.eaaddr & 0xf) x86gpf(NULL, 0);
                dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
                dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
                dst[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
                dst[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
                cpu_state.XMM[cpu_reg].l[0] = dst[0];
                cpu_state.XMM[cpu_reg].l[1] = dst[1];
                cpu_state.XMM[cpu_reg].l[2] = dst[2];
                cpu_state.XMM[cpu_reg].l[3] = dst[3];

                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opMOVAPS_q_xmm_a32(uint32_t fetchdat)
{       
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                cpu_state.XMM[cpu_reg].f[0] = cpu_state.XMM[cpu_rm].f[0];
                cpu_state.XMM[cpu_reg].f[1] = cpu_state.XMM[cpu_rm].f[1];
                cpu_state.XMM[cpu_reg].f[2] = cpu_state.XMM[cpu_rm].f[2];
                cpu_state.XMM[cpu_reg].f[3] = cpu_state.XMM[cpu_rm].f[3];
                CLOCK_CYCLES(1);
        }
        else
        {
                uint64_t dst[4];

                SEG_CHECK_READ(cpu_state.ea_seg);
                if(cpu_state.eaaddr & 0xf) x86gpf(NULL, 0);
                dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
                dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
                dst[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
                dst[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
                cpu_state.XMM[cpu_reg].l[0] = dst[0];
                cpu_state.XMM[cpu_reg].l[1] = dst[1];
                cpu_state.XMM[cpu_reg].l[2] = dst[2];
                cpu_state.XMM[cpu_reg].l[3] = dst[3];

                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opMOVAPS_xmm_q_a16(uint32_t fetchdat)
{       
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_rm].f[0] = cpu_state.XMM[cpu_reg].f[0];
        cpu_state.XMM[cpu_rm].f[1] = cpu_state.XMM[cpu_reg].f[1];
        cpu_state.XMM[cpu_rm].f[2] = cpu_state.XMM[cpu_reg].f[2];
        cpu_state.XMM[cpu_rm].f[3] = cpu_state.XMM[cpu_reg].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm[4] = {cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3]};
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        if(cpu_state.eaaddr & 0xf) x86gpf(NULL, 0);
        writememl(easeg, cpu_state.eaaddr, rm[0]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 4, rm[1]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 8, rm[2]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 12, rm[3]); if (cpu_state.abrt) return 1;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVAPS_xmm_q_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_rm].f[0] = cpu_state.XMM[cpu_reg].f[0];
        cpu_state.XMM[cpu_rm].f[1] = cpu_state.XMM[cpu_reg].f[1];
        cpu_state.XMM[cpu_rm].f[2] = cpu_state.XMM[cpu_reg].f[2];
        cpu_state.XMM[cpu_rm].f[3] = cpu_state.XMM[cpu_reg].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm[4] = {cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3]};
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        if(cpu_state.eaaddr & 0xf) x86gpf(NULL, 0);
        writememl(easeg, cpu_state.eaaddr, rm[0]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 4, rm[1]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 8, rm[2]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 12, rm[3]); if (cpu_state.abrt) return 1;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVNTPS_xmm_q_a16(uint32_t fetchdat)
{       
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_rm].f[0] = cpu_state.XMM[cpu_reg].f[0];
        cpu_state.XMM[cpu_rm].f[1] = cpu_state.XMM[cpu_reg].f[1];
        cpu_state.XMM[cpu_rm].f[2] = cpu_state.XMM[cpu_reg].f[2];
        cpu_state.XMM[cpu_rm].f[3] = cpu_state.XMM[cpu_reg].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm[4] = {cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3]};
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        if(cpu_state.eaaddr & 0xf) x86gpf(NULL, 0);
        writememl(easeg, cpu_state.eaaddr, rm[0]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 4, rm[1]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 8, rm[2]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 12, rm[3]); if (cpu_state.abrt) return 1;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVNTPS_xmm_q_a32(uint32_t fetchdat)
{       
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_rm].f[0] = cpu_state.XMM[cpu_reg].f[0];
        cpu_state.XMM[cpu_rm].f[1] = cpu_state.XMM[cpu_reg].f[1];
        cpu_state.XMM[cpu_rm].f[2] = cpu_state.XMM[cpu_reg].f[2];
        cpu_state.XMM[cpu_rm].f[3] = cpu_state.XMM[cpu_reg].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm[4] = {cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3]};
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        if(cpu_state.eaaddr & 0xf) x86gpf(NULL, 0);
        writememl(easeg, cpu_state.eaaddr, rm[0]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 4, rm[1]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 8, rm[2]); if (cpu_state.abrt) return 1;
        writememl(easeg, cpu_state.eaaddr + 12, rm[3]); if (cpu_state.abrt) return 1;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVMSKPS_l_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        uint32_t result = 0;
        if(cpu_state.XMM[cpu_rm].l[0] & (1 << 31)) result |= 1;
        if(cpu_state.XMM[cpu_rm].l[1] & (1 << 31)) result |= 2;
        if(cpu_state.XMM[cpu_rm].l[2] & (1 << 31)) result |= 4;
        if(cpu_state.XMM[cpu_rm].l[3] & (1 << 31)) result |= 8;
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int opPSHUFW_xmm_xmm_a16(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].w[0] = cpu_state.MM[cpu_rm].w[(imm & 3)];
        cpu_state.MM[cpu_reg].w[1] = cpu_state.MM[cpu_rm].w[((imm >> 2) & 3)];
        cpu_state.MM[cpu_reg].w[2] = cpu_state.MM[cpu_rm].w[((imm >> 4) & 3)];
        cpu_state.MM[cpu_reg].w[3] = cpu_state.MM[cpu_rm].w[((imm >> 6) & 3)];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].w[0] = (src >> ((imm & 3) << 16)) & 0xffff;
        cpu_state.MM[cpu_reg].w[1] = (src >> (((imm >> 2) & 3) << 16)) & 0xffff;
        cpu_state.MM[cpu_reg].w[2] = (src >> (((imm >> 4) & 3) << 16)) & 0xffff;
        cpu_state.MM[cpu_reg].w[3] = (src >> (((imm >> 6) & 3) << 16)) & 0xffff;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opPSHUFW_xmm_xmm_a32(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].w[0] = cpu_state.MM[cpu_rm].w[(imm & 3)];
        cpu_state.MM[cpu_reg].w[1] = cpu_state.MM[cpu_rm].w[((imm >> 2) & 3)];
        cpu_state.MM[cpu_reg].w[2] = cpu_state.MM[cpu_rm].w[((imm >> 4) & 3)];
        cpu_state.MM[cpu_reg].w[3] = cpu_state.MM[cpu_rm].w[((imm >> 6) & 3)];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].w[0] = (src >> ((imm & 3) << 4)) & 0xffff;
        cpu_state.MM[cpu_reg].w[1] = (src >> (((imm >> 2) & 3) << 4)) & 0xffff;
        cpu_state.MM[cpu_reg].w[2] = (src >> (((imm >> 4) & 3) << 4)) & 0xffff;
        cpu_state.MM[cpu_reg].w[3] = (src >> (((imm >> 6) & 3) << 4)) & 0xffff;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opLDMXCSR_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    uint32_t src;
    
    SEG_CHECK_READ(cpu_state.ea_seg);
    src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
    if(src & ~0xffbf) x86gpf(NULL, 0);
    cpu_state.mxcsr = src & 0xffbf;
    CLOCK_CYCLES(1);
    return 0;
}

static int opLDMXCSR_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    uint32_t src;
    
    SEG_CHECK_READ(cpu_state.ea_seg);
    src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
    if(src & ~0xffbf) x86gpf(NULL, 0);
    cpu_state.mxcsr = src & 0xffbf;
    CLOCK_CYCLES(1);
    return 0;
}

static int opSTMXCSR_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    writememl(easeg, cpu_state.eaaddr, cpu_state.mxcsr); if (cpu_state.abrt) return 1;
    CLOCK_CYCLES(1);
    return 0;
}

static int opSFENCE(uint32_t fetchdat)
{
    //We don't emulate the cache, so this is a NOP.
    CLOCK_CYCLES(1);
    return 0;
}
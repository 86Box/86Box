static int opMOVUPS_q_xmm_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                XMM[cpu_reg].f[0] = XMM[cpu_rm].f[0];
                XMM[cpu_reg].f[1] = XMM[cpu_rm].f[1];
                XMM[cpu_reg].f[2] = XMM[cpu_rm].f[2];
                XMM[cpu_reg].f[3] = XMM[cpu_rm].f[3];
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
                XMM[cpu_reg].l[0] = dst[0];
                XMM[cpu_reg].l[1] = dst[1];
                XMM[cpu_reg].l[2] = dst[2];
                XMM[cpu_reg].l[3] = dst[3];

                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opMOVUPS_q_xmm_a32(uint32_t fetchdat)
{       
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                XMM[cpu_reg].f[0] = XMM[cpu_rm].f[0];
                XMM[cpu_reg].f[1] = XMM[cpu_rm].f[1];
                XMM[cpu_reg].f[2] = XMM[cpu_rm].f[2];
                XMM[cpu_reg].f[3] = XMM[cpu_rm].f[3];
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
                XMM[cpu_reg].l[0] = dst[0];
                XMM[cpu_reg].l[1] = dst[1];
                XMM[cpu_reg].l[2] = dst[2];
                XMM[cpu_reg].l[3] = dst[3];

                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opMOVUPS_xmm_q_a16(uint32_t fetchdat)
{       
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_rm].f[0] = XMM[cpu_reg].f[0];
        XMM[cpu_rm].f[1] = XMM[cpu_reg].f[1];
        XMM[cpu_rm].f[2] = XMM[cpu_reg].f[2];
        XMM[cpu_rm].f[3] = XMM[cpu_reg].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm[4] = {XMM[cpu_reg].l[0], XMM[cpu_reg].l[1], XMM[cpu_reg].l[2], XMM[cpu_reg].l[3]};
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
        XMM[cpu_rm].f[0] = XMM[cpu_reg].f[0];
        XMM[cpu_rm].f[1] = XMM[cpu_reg].f[1];
        XMM[cpu_rm].f[2] = XMM[cpu_reg].f[2];
        XMM[cpu_rm].f[3] = XMM[cpu_reg].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm[4] = {XMM[cpu_reg].l[0], XMM[cpu_reg].l[1], XMM[cpu_reg].l[2], XMM[cpu_reg].l[3]};
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
            XMM[cpu_reg].f[0] = XMM[cpu_rm].f[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].l[0] = dst;
        XMM[cpu_reg].l[1] = 0;
        XMM[cpu_reg].l[2] = 0;
        XMM[cpu_reg].l[3] = 0;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVSS_f_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
            XMM[cpu_reg].f[0] = XMM[cpu_rm].f[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].l[0] = dst;
        XMM[cpu_reg].l[1] = 0;
        XMM[cpu_reg].l[2] = 0;
        XMM[cpu_reg].l[3] = 0;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVSS_xmm_f_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
            XMM[cpu_rm].f[0] = XMM[cpu_reg].f[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm = XMM[cpu_reg].l[0];

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
            XMM[cpu_rm].f[0] = XMM[cpu_reg].f[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm = XMM[cpu_reg].l[0];

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
        XMM[cpu_reg].q[0] = XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        //MOVLPS    
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].q[0] = dst;
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
        XMM[cpu_reg].q[0] = XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        //MOVLPS    
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].q[0] = dst;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVLPS_xmm_f_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_rm].q[0] = XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, XMM[cpu_rm].q[0]); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVLPS_xmm_f_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_rm].q[0] = XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, XMM[cpu_rm].q[0]); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUNPCKLPS_f_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].l[0] = XMM[cpu_rm].l[0];
        XMM[cpu_reg].l[1] = XMM[cpu_rm].l[0];
        XMM[cpu_reg].l[2] = XMM[cpu_rm].l[1];
        XMM[cpu_reg].l[3] = XMM[cpu_rm].l[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].l[0] = dst[0];
        XMM[cpu_reg].l[1] = dst[0];
        XMM[cpu_reg].l[2] = dst[1];
        XMM[cpu_reg].l[3] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUNPCKLPS_f_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].l[0] = XMM[cpu_rm].l[0];
        XMM[cpu_reg].l[1] = XMM[cpu_rm].l[0];
        XMM[cpu_reg].l[2] = XMM[cpu_rm].l[1];
        XMM[cpu_reg].l[3] = XMM[cpu_rm].l[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].l[0] = dst[0];
        XMM[cpu_reg].l[1] = dst[0];
        XMM[cpu_reg].l[2] = dst[1];
        XMM[cpu_reg].l[3] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUNPCKHPS_f_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].l[0] = XMM[cpu_rm].l[2];
        XMM[cpu_reg].l[1] = XMM[cpu_rm].l[2];
        XMM[cpu_reg].l[2] = XMM[cpu_rm].l[3];
        XMM[cpu_reg].l[3] = XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].l[0] = dst[0];
        XMM[cpu_reg].l[1] = dst[0];
        XMM[cpu_reg].l[2] = dst[1];
        XMM[cpu_reg].l[3] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUNPCKHPS_f_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].l[0] = XMM[cpu_rm].l[0];
        XMM[cpu_reg].l[1] = XMM[cpu_rm].l[0];
        XMM[cpu_reg].l[2] = XMM[cpu_rm].l[1];
        XMM[cpu_reg].l[3] = XMM[cpu_rm].l[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].l[0] = dst[0];
        XMM[cpu_reg].l[1] = dst[0];
        XMM[cpu_reg].l[2] = dst[1];
        XMM[cpu_reg].l[3] = dst[1];
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
        XMM[cpu_reg].q[1] = XMM[cpu_rm].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        //MOVHPS    
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].q[1] = dst;
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
        XMM[cpu_reg].q[1] = XMM[cpu_rm].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        //MOVHPS    
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].q[1] = dst;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVHPS_xmm_f_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_rm].q[1] = XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr + 8, XMM[cpu_rm].q[0]); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVHPS_xmm_f_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_rm].q[1] = XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr + 8, XMM[cpu_rm].q[0]); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVAPS_q_xmm_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                XMM[cpu_reg].f[0] = XMM[cpu_rm].f[0];
                XMM[cpu_reg].f[1] = XMM[cpu_rm].f[1];
                XMM[cpu_reg].f[2] = XMM[cpu_rm].f[2];
                XMM[cpu_reg].f[3] = XMM[cpu_rm].f[3];
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
                XMM[cpu_reg].l[0] = dst[0];
                XMM[cpu_reg].l[1] = dst[1];
                XMM[cpu_reg].l[2] = dst[2];
                XMM[cpu_reg].l[3] = dst[3];

                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opMOVAPS_q_xmm_a32(uint32_t fetchdat)
{       
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                XMM[cpu_reg].f[0] = XMM[cpu_rm].f[0];
                XMM[cpu_reg].f[1] = XMM[cpu_rm].f[1];
                XMM[cpu_reg].f[2] = XMM[cpu_rm].f[2];
                XMM[cpu_reg].f[3] = XMM[cpu_rm].f[3];
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
                XMM[cpu_reg].l[0] = dst[0];
                XMM[cpu_reg].l[1] = dst[1];
                XMM[cpu_reg].l[2] = dst[2];
                XMM[cpu_reg].l[3] = dst[3];

                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opMOVAPS_xmm_q_a16(uint32_t fetchdat)
{       
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_rm].f[0] = XMM[cpu_reg].f[0];
        XMM[cpu_rm].f[1] = XMM[cpu_reg].f[1];
        XMM[cpu_rm].f[2] = XMM[cpu_reg].f[2];
        XMM[cpu_rm].f[3] = XMM[cpu_reg].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm[4] = {XMM[cpu_reg].l[0], XMM[cpu_reg].l[1], XMM[cpu_reg].l[2], XMM[cpu_reg].l[3]};
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
        XMM[cpu_rm].f[0] = XMM[cpu_reg].f[0];
        XMM[cpu_rm].f[1] = XMM[cpu_reg].f[1];
        XMM[cpu_rm].f[2] = XMM[cpu_reg].f[2];
        XMM[cpu_rm].f[3] = XMM[cpu_reg].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t rm[4] = {XMM[cpu_reg].l[0], XMM[cpu_reg].l[1], XMM[cpu_reg].l[2], XMM[cpu_reg].l[3]};
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
    ILLEGAL_ON(cpu_mod == 3);

    uint32_t rm[4] = {XMM[cpu_reg].l[0], XMM[cpu_reg].l[1], XMM[cpu_reg].l[2], XMM[cpu_reg].l[3]};
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if(cpu_state.eaaddr & 0xf) x86gpf(NULL, 0);
    writememl(easeg, cpu_state.eaaddr, rm[0]); if (cpu_state.abrt) return 1;
    writememl(easeg, cpu_state.eaaddr + 4, rm[1]); if (cpu_state.abrt) return 1;
    writememl(easeg, cpu_state.eaaddr + 8, rm[2]); if (cpu_state.abrt) return 1;
    writememl(easeg, cpu_state.eaaddr + 12, rm[3]); if (cpu_state.abrt) return 1;

    CLOCK_CYCLES(2);
    return 0;
}

static int opMOVNTPS_xmm_q_a32(uint32_t fetchdat)
{       
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);

    uint32_t rm[4] = {XMM[cpu_reg].l[0], XMM[cpu_reg].l[1], XMM[cpu_reg].l[2], XMM[cpu_reg].l[3]};
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if(cpu_state.eaaddr & 0xf) x86gpf(NULL, 0);
    writememl(easeg, cpu_state.eaaddr, rm[0]); if (cpu_state.abrt) return 1;
    writememl(easeg, cpu_state.eaaddr + 4, rm[1]); if (cpu_state.abrt) return 1;
    writememl(easeg, cpu_state.eaaddr + 8, rm[2]); if (cpu_state.abrt) return 1;
    writememl(easeg, cpu_state.eaaddr + 12, rm[3]); if (cpu_state.abrt) return 1;

        CLOCK_CYCLES(2);
    return 0;
}

static int opMOVMSKPS_l_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        uint32_t result = 0;
        if(XMM[cpu_rm].l[0] & (1 << 31)) result |= 1;
        if(XMM[cpu_rm].l[1] & (1 << 31)) result |= 2;
        if(XMM[cpu_rm].l[2] & (1 << 31)) result |= 4;
        if(XMM[cpu_rm].l[3] & (1 << 31)) result |= 8;
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int opMOVMSKPS_l_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        uint32_t result = 0;
        if(XMM[cpu_rm].l[0] & (1 << 31)) result |= 1;
        if(XMM[cpu_rm].l[1] & (1 << 31)) result |= 2;
        if(XMM[cpu_rm].l[2] & (1 << 31)) result |= 4;
        if(XMM[cpu_rm].l[3] & (1 << 31)) result |= 8;
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int opPSHUFW_mm_mm_a16(uint32_t fetchdat)
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

static int opPSHUFW_mm_mm_a32(uint32_t fetchdat)
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

static int opPINSRW_xmm_w_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_mod == 3)
    {
        uint16_t rm = getr16(cpu_rm);
        if(sse_xmm) XMM[cpu_reg].w[imm & 7] = rm;
        else
        {
            MMX_ENTER();
            cpu_state.MM[cpu_reg].w[imm & 3] = rm;
        }
        CLOCK_CYCLES(1);
    }
    else
    {
        uint16_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmemw(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        if(sse_xmm) XMM[cpu_reg].w[imm & 7] = src;
        else
        {
            MMX_ENTER();
            cpu_state.MM[cpu_reg].w[imm & 3] = src;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opPINSRW_xmm_w_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_mod == 3)
    {
        uint16_t rm = getr16(cpu_rm);
        if(sse_xmm) XMM[cpu_reg].w[imm & 7] = rm;
        else
        {
            MMX_ENTER();
            cpu_state.MM[cpu_reg].w[imm & 3] = rm;
        }
        CLOCK_CYCLES(1);
    }
    else
    {
        uint16_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmemw(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        if(sse_xmm) XMM[cpu_reg].w[imm & 7] = src;
        else
        {
            MMX_ENTER();
            cpu_state.MM[cpu_reg].w[imm & 3] = src;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opPEXTRW_mm_w_a16(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        setr32(cpu_reg, cpu_state.MM[cpu_rm].w[imm & 3]);
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int opPEXTRW_mm_w_a32(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        setr32(cpu_reg, cpu_state.MM[cpu_rm].w[imm & 3]);
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int opPEXTRW_xmm_w_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        if(sse_xmm) setr32(cpu_reg, XMM[cpu_rm].w[imm & 7]);
        else
        {
            MMX_ENTER();
            setr32(cpu_reg, cpu_state.MM[cpu_rm].w[imm & 3]);
        }
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int opPEXTRW_xmm_w_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        if(sse_xmm) setr32(cpu_reg, XMM[cpu_rm].w[imm & 7]);
        else
        {
            MMX_ENTER();
            setr32(cpu_reg, cpu_state.MM[cpu_rm].w[imm & 3]);
        }
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int opSHUFPS_xmm_w_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = XMM[cpu_rm].f[imm & 3];
        XMM[cpu_reg].f[1] = XMM[cpu_rm].f[(imm >> 2) & 3];
        XMM[cpu_reg].f[2] = XMM[cpu_rm].f[(imm >> 4) & 3];
        XMM[cpu_reg].f[3] = XMM[cpu_rm].f[(imm >> 6) & 3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        src[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].l[0] = src[imm & 3];
        XMM[cpu_reg].l[1] = src[(imm >> 2) & 3];
        XMM[cpu_reg].l[2] = src[(imm >> 4) & 3];
        XMM[cpu_reg].l[3] = src[(imm >> 6) & 3];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opSHUFPS_xmm_w_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = XMM[cpu_rm].f[imm & 3];
        XMM[cpu_reg].f[1] = XMM[cpu_rm].f[(imm >> 2) & 3];
        XMM[cpu_reg].f[2] = XMM[cpu_rm].f[(imm >> 4) & 3];
        XMM[cpu_reg].f[3] = XMM[cpu_rm].f[(imm >> 6) & 3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        src[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].l[0] = src[imm & 3];
        XMM[cpu_reg].l[1] = src[(imm >> 2) & 3];
        XMM[cpu_reg].l[2] = src[(imm >> 4) & 3];
        XMM[cpu_reg].l[3] = src[(imm >> 6) & 3];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opPMOVMSKB_l_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        uint32_t result = 0;
        if(sse_xmm)
        {
            if(XMM[cpu_rm].b[0] & (1 << 7)) result |= 1;
            if(XMM[cpu_rm].b[1] & (1 << 7)) result |= 2;
            if(XMM[cpu_rm].b[2] & (1 << 7)) result |= 4;
            if(XMM[cpu_rm].b[3] & (1 << 7)) result |= 8;
            if(XMM[cpu_rm].b[4] & (1 << 7)) result |= 0x10;
            if(XMM[cpu_rm].b[5] & (1 << 7)) result |= 0x20;
            if(XMM[cpu_rm].b[6] & (1 << 7)) result |= 0x40;
            if(XMM[cpu_rm].b[7] & (1 << 7)) result |= 0x80;
            if(XMM[cpu_rm].b[8] & (1 << 7)) result |= 0x100;
            if(XMM[cpu_rm].b[9] & (1 << 7)) result |= 0x200;
            if(XMM[cpu_rm].b[10] & (1 << 7)) result |= 0x400;
            if(XMM[cpu_rm].b[11] & (1 << 7)) result |= 0x800;
            if(XMM[cpu_rm].b[12] & (1 << 7)) result |= 0x1000;
            if(XMM[cpu_rm].b[13] & (1 << 7)) result |= 0x2000;
            if(XMM[cpu_rm].b[14] & (1 << 7)) result |= 0x4000;
            if(XMM[cpu_rm].b[15] & (1 << 7)) result |= 0x8000;
        }
        else
        {
            MMX_ENTER();
            if(cpu_state.MM[cpu_rm].b[0] & (1 << 7)) result |= 1;
            if(cpu_state.MM[cpu_rm].b[1] & (1 << 7)) result |= 2;
            if(cpu_state.MM[cpu_rm].b[2] & (1 << 7)) result |= 4;
            if(cpu_state.MM[cpu_rm].b[3] & (1 << 7)) result |= 8;
            if(cpu_state.MM[cpu_rm].b[4] & (1 << 7)) result |= 0x10;
            if(cpu_state.MM[cpu_rm].b[5] & (1 << 7)) result |= 0x20;
            if(cpu_state.MM[cpu_rm].b[6] & (1 << 7)) result |= 0x40;
            if(cpu_state.MM[cpu_rm].b[7] & (1 << 7)) result |= 0x80;
        }
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int opPMOVMSKB_l_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        uint32_t result = 0;
        if(sse_xmm)
        {
            if(XMM[cpu_rm].b[0] & (1 << 7)) result |= 1;
            if(XMM[cpu_rm].b[1] & (1 << 7)) result |= 2;
            if(XMM[cpu_rm].b[2] & (1 << 7)) result |= 4;
            if(XMM[cpu_rm].b[3] & (1 << 7)) result |= 8;
            if(XMM[cpu_rm].b[4] & (1 << 7)) result |= 0x10;
            if(XMM[cpu_rm].b[5] & (1 << 7)) result |= 0x20;
            if(XMM[cpu_rm].b[6] & (1 << 7)) result |= 0x40;
            if(XMM[cpu_rm].b[7] & (1 << 7)) result |= 0x80;
            if(XMM[cpu_rm].b[8] & (1 << 7)) result |= 0x100;
            if(XMM[cpu_rm].b[9] & (1 << 7)) result |= 0x200;
            if(XMM[cpu_rm].b[10] & (1 << 7)) result |= 0x400;
            if(XMM[cpu_rm].b[11] & (1 << 7)) result |= 0x800;
            if(XMM[cpu_rm].b[12] & (1 << 7)) result |= 0x1000;
            if(XMM[cpu_rm].b[13] & (1 << 7)) result |= 0x2000;
            if(XMM[cpu_rm].b[14] & (1 << 7)) result |= 0x4000;
            if(XMM[cpu_rm].b[15] & (1 << 7)) result |= 0x8000;
        }
        else
        {
            MMX_ENTER();
            if(cpu_state.MM[cpu_rm].b[0] & (1 << 7)) result |= 1;
            if(cpu_state.MM[cpu_rm].b[1] & (1 << 7)) result |= 2;
            if(cpu_state.MM[cpu_rm].b[2] & (1 << 7)) result |= 4;
            if(cpu_state.MM[cpu_rm].b[3] & (1 << 7)) result |= 8;
            if(cpu_state.MM[cpu_rm].b[4] & (1 << 7)) result |= 0x10;
            if(cpu_state.MM[cpu_rm].b[5] & (1 << 7)) result |= 0x20;
            if(cpu_state.MM[cpu_rm].b[6] & (1 << 7)) result |= 0x40;
            if(cpu_state.MM[cpu_rm].b[7] & (1 << 7)) result |= 0x80;
        }
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int opMOVNTQ_q_mm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    writememl(easeg, cpu_state.eaaddr, cpu_state.MM[cpu_reg].l[0]); if (cpu_state.abrt) return 1;
    writememl(easeg, cpu_state.eaaddr + 4, cpu_state.MM[cpu_reg].l[1]); if (cpu_state.abrt) return 1;
    CLOCK_CYCLES(1);
    return 0;
}

static int opMOVNTQ_q_mm_a32(uint32_t fetchdat)
{
    fetch_ea_32 (fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    writememl(easeg, cpu_state.eaaddr, cpu_state.MM[cpu_reg].l[0]); if (cpu_state.abrt) return 1;
    writememl(easeg, cpu_state.eaaddr + 4, cpu_state.MM[cpu_reg].l[1]); if (cpu_state.abrt) return 1;
    CLOCK_CYCLES(1);
    return 0;
}

static int opMASKMOVQ_l_mm_a16(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        if(cpu_state.MM[cpu_rm].b[0] & (1 << 7))
        {
            writememb(ds, DI, cpu_state.MM[cpu_reg].b[0]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[1] & (1 << 7))
        {
            writememb(ds, DI+1, cpu_state.MM[cpu_reg].b[1]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[2] & (1 << 7))
        {
            writememb(ds, DI+2, cpu_state.MM[cpu_reg].b[2]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[3] & (1 << 7))
        {
            writememb(ds, DI+3, cpu_state.MM[cpu_reg].b[3]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[4] & (1 << 7))
        {
            writememb(ds, DI+4, cpu_state.MM[cpu_reg].b[4]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[5] & (1 << 7))
        {
            writememb(ds, DI+5, cpu_state.MM[cpu_reg].b[5]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[6] & (1 << 7))
        {
            writememb(ds, DI+6, cpu_state.MM[cpu_reg].b[6]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[7] & (1 << 7))
        {
            writememb(ds, DI+7, cpu_state.MM[cpu_reg].b[7]); if (cpu_state.abrt) return 1;
        }
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int opMASKMOVQ_l_mm_a32(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        if(cpu_state.MM[cpu_rm].b[0] & (1 << 7))
        {
            writememb(ds, EDI, cpu_state.MM[cpu_reg].b[0]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[1] & (1 << 7))
        {
            writememb(ds, EDI+1, cpu_state.MM[cpu_reg].b[1]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[2] & (1 << 7))
        {
            writememb(ds, EDI+2, cpu_state.MM[cpu_reg].b[2]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[3] & (1 << 7))
        {
            writememb(ds, EDI+3, cpu_state.MM[cpu_reg].b[3]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[4] & (1 << 7))
        {
            writememb(ds, EDI+4, cpu_state.MM[cpu_reg].b[4]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[5] & (1 << 7))
        {
            writememb(ds, EDI+5, cpu_state.MM[cpu_reg].b[5]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[6] & (1 << 7))
        {
            writememb(ds, EDI+6, cpu_state.MM[cpu_reg].b[6]); if (cpu_state.abrt) return 1;
        }
        if(cpu_state.MM[cpu_rm].b[7] & (1 << 7))
        {
            writememb(ds, EDI+7, cpu_state.MM[cpu_reg].b[7]); if (cpu_state.abrt) return 1;
        }
        CLOCK_CYCLES(1);
    }
    return 0;
}
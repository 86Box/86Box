static int opMOVUPD_q_xmm_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                XMM[cpu_reg].d[0] = XMM[cpu_rm].d[0];
                XMM[cpu_reg].d[1] = XMM[cpu_rm].d[1];
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

static int opMOVUPD_q_xmm_a32(uint32_t fetchdat)
{       
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                XMM[cpu_reg].d[0] = XMM[cpu_rm].d[0];
                XMM[cpu_reg].d[1] = XMM[cpu_rm].d[1];
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

static int opMOVUPD_xmm_q_a16(uint32_t fetchdat)
{       
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_rm].d[0] = XMM[cpu_reg].d[0];
        XMM[cpu_rm].d[1] = XMM[cpu_reg].d[1];
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

static int opMOVUPD_xmm_q_a32(uint32_t fetchdat)
{       
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_rm].d[0] = XMM[cpu_reg].d[0];
        XMM[cpu_rm].d[1] = XMM[cpu_reg].d[1];
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

static int opMOVSD_f_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
            XMM[cpu_reg].d[0] = XMM[cpu_rm].d[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].q[0] = dst;
        XMM[cpu_reg].q[1] = 0;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVSD_f_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
            XMM[cpu_reg].d[0] = XMM[cpu_rm].d[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].q[0] = dst;
        XMM[cpu_reg].q[1] = 0;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVSD_xmm_f_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
            XMM[cpu_rm].d[0] = XMM[cpu_reg].d[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t rm = XMM[cpu_reg].q[0];

        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, rm); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVSD_xmm_f_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
            XMM[cpu_rm].d[0] = XMM[cpu_reg].d[0];
            CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t rm = XMM[cpu_reg].q[0];

        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, rm); if (cpu_state.abrt) return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVLPD_f_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    uint64_t dst;

    SEG_CHECK_READ(cpu_state.ea_seg);
    dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
    XMM[cpu_reg].q[0] = dst;
    CLOCK_CYCLES(2);
    return 0;
}

static int opMOVLPD_f_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    uint64_t dst;

    SEG_CHECK_READ(cpu_state.ea_seg);
    dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
    XMM[cpu_reg].q[0] = dst;
    CLOCK_CYCLES(2);
    return 0;
}

static int opMOVLPD_xmm_f_a16(uint32_t fetchdat)
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

static int opMOVLPD_xmm_f_a32(uint32_t fetchdat)
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

static int opUNPCKLPD_f_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].q[0] = XMM[cpu_rm].q[0];
        XMM[cpu_reg].q[1] = XMM[cpu_rm].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].q[0] = dst[0];
        XMM[cpu_reg].q[1] = dst[0];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUNPCKLPD_f_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].q[0] = XMM[cpu_rm].q[0];
        XMM[cpu_reg].q[1] = XMM[cpu_rm].q[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].q[0] = dst[0];
        XMM[cpu_reg].q[1] = dst[0];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUNPCKHPD_f_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].q[0] = XMM[cpu_rm].q[1];
        XMM[cpu_reg].q[1] = XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].q[0] = dst[1];
        XMM[cpu_reg].q[1] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUNPCKHPD_f_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].q[0] = XMM[cpu_rm].q[1];
        XMM[cpu_reg].q[1] = XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst[2];
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].q[0] = dst[1];
        XMM[cpu_reg].q[1] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opMOVHPD_f_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    uint64_t dst;

    SEG_CHECK_READ(cpu_state.ea_seg);
    dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
    XMM[cpu_reg].q[1] = dst;
    CLOCK_CYCLES(2);
    return 0;
}

static int opMOVHPD_f_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    uint64_t dst;

    SEG_CHECK_READ(cpu_state.ea_seg);
    dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
    XMM[cpu_reg].q[1] = dst;
    CLOCK_CYCLES(2);
    return 0;
}

static int opMOVHPD_xmm_f_a16(uint32_t fetchdat)
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

static int opMOVHPD_xmm_f_a32(uint32_t fetchdat)
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

static int opMOVAPD_q_xmm_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                XMM[cpu_reg].d[0] = XMM[cpu_rm].d[0];
                XMM[cpu_reg].d[1] = XMM[cpu_rm].d[1];
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

static int opMOVAPD_q_xmm_a32(uint32_t fetchdat)
{       
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                XMM[cpu_reg].d[0] = XMM[cpu_rm].d[0];
                XMM[cpu_reg].d[1] = XMM[cpu_rm].d[1];
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

static int opMOVAPD_xmm_q_a16(uint32_t fetchdat)
{       
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_rm].d[0] = XMM[cpu_reg].d[0];
        XMM[cpu_rm].d[1] = XMM[cpu_reg].d[1];
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

static int opMOVAPD_xmm_q_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_rm].d[0] = XMM[cpu_reg].d[0];
        XMM[cpu_rm].d[1] = XMM[cpu_reg].d[1];
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

static int opMOVNTPD_xmm_q_a16(uint32_t fetchdat)
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

static int opMOVNTPD_xmm_q_a32(uint32_t fetchdat)
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

static int opMOVMSKPD_l_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        uint32_t result = 0;
        if(XMM[cpu_rm].q[0] & (1 << 31)) result |= 1;
        if(XMM[cpu_rm].q[1] & (1 << 31)) result |= 2;
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int opMOVMSKPD_l_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3)
    {
        uint32_t result = 0;
        if(XMM[cpu_rm].q[0] & (1 << 31)) result |= 1;
        if(XMM[cpu_rm].q[1] & (1 << 31)) result |= 2;
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }
    return 0;
}
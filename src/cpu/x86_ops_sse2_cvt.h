static int opCVTPI2PD_xmm_mm_a16(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].d[0] = cpu_state.MM[cpu_rm].l[0];
        XMM[cpu_reg].d[1] = cpu_state.MM[cpu_rm].l[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].d[0] = dst[0];
        XMM[cpu_reg].d[1] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTPI2PD_xmm_mm_a32(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].d[0] = cpu_state.MM[cpu_rm].l[0];
        XMM[cpu_reg].d[1] = cpu_state.MM[cpu_rm].l[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].d[0] = dst[0];
        XMM[cpu_reg].d[1] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSI2SD_xmm_l_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].d[0] = getr32(cpu_rm);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].d[0] = dst;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSI2SD_xmm_l_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].d[0] = getr32(cpu_rm);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].d[0] = dst;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTTPD2PI_mm_xmm_a16(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].l[0] = XMM[cpu_rm].d[0];
        cpu_state.MM[cpu_reg].l[1] = XMM[cpu_rm].d[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst[2];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        double dst_real[2];
        dst_real[0] = *(double*)&dst[0];
        dst_real[1] = *(double*)&dst[1];
        cpu_state.MM[cpu_reg].l[0] = dst_real[0];
        cpu_state.MM[cpu_reg].l[1] = dst_real[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTTPD2PI_mm_xmm_a32(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].l[0] = XMM[cpu_rm].d[0];
        cpu_state.MM[cpu_reg].l[1] = XMM[cpu_rm].d[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst[2];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        double dst_real[2];
        dst_real[0] = *(double*)&dst[0];
        dst_real[1] = *(double*)&dst[1];
        cpu_state.MM[cpu_reg].l[0] = dst_real[0];
        cpu_state.MM[cpu_reg].l[1] = dst_real[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTTSD2SI_l_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        setr32(cpu_reg, XMM[cpu_rm].d[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        double dst_real;
        dst_real = *(double*)&dst;
        setr32(cpu_reg, dst_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTTSD2SI_l_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        setr32(cpu_reg, XMM[cpu_rm].d[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        double dst_real;
        dst_real = *(double*)&dst;
        setr32(cpu_reg, dst_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSD2SI_l_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        setr32(cpu_reg, XMM[cpu_rm].d[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        double dst_real;
        dst_real = *(double*)&dst;
        setr32(cpu_reg, dst_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSD2SI_l_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        setr32(cpu_reg, XMM[cpu_rm].d[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        double dst_real;
        dst_real = *(double*)&dst;
        setr32(cpu_reg, dst_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTPS2PD_mm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].d[0] = XMM[cpu_rm].f[0];
        XMM[cpu_reg].d[1] = XMM[cpu_rm].f[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        float dst_real[2];
        dst_real[0] = *(float*)&dst[0];
        dst_real[1] = *(float*)&dst[1];
        XMM[cpu_reg].d[0] = dst_real[0];
        XMM[cpu_reg].d[1] = dst_real[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTPS2PD_mm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].d[0] = XMM[cpu_rm].f[0];
        XMM[cpu_reg].d[1] = XMM[cpu_rm].f[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        float dst_real[2];
        dst_real[0] = *(float*)&dst[0];
        dst_real[1] = *(float*)&dst[1];
        XMM[cpu_reg].d[0] = dst_real[0];
        XMM[cpu_reg].d[1] = dst_real[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTPD2PS_mm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = XMM[cpu_rm].d[0];
        XMM[cpu_reg].f[1] = XMM[cpu_rm].d[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst[2];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        double dst_real[2];
        dst_real[0] = *(double*)&dst[0];
        dst_real[1] = *(double*)&dst[1];
        XMM[cpu_reg].f[0] = dst_real[0];
        XMM[cpu_reg].f[1] = dst_real[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTPD2PS_mm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = XMM[cpu_rm].d[0];
        XMM[cpu_reg].f[1] = XMM[cpu_rm].d[1];
        XMM[cpu_reg].l[2] = 0;
        XMM[cpu_reg].l[3] = 0;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst[2];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        double dst_real[2];
        dst_real[0] = *(double*)&dst[0];
        dst_real[1] = *(double*)&dst[1];
        XMM[cpu_reg].f[0] = dst_real[0];
        XMM[cpu_reg].f[1] = dst_real[1];
        XMM[cpu_reg].l[2] = 0;
        XMM[cpu_reg].l[3] = 0;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSS2SD_mm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].d[0] = XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float dst_real;
        dst_real = *(float*)&dst;
        XMM[cpu_reg].d[0] = dst_real;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSS2SD_mm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].d[0] = XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float dst_real;
        dst_real = *(float*)&dst;
        XMM[cpu_reg].d[0] = dst_real;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSD2SS_mm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = XMM[cpu_rm].d[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        double dst_real;
        dst_real = *(double*)&dst;
        XMM[cpu_reg].f[0] = dst_real;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSD2SS_mm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = XMM[cpu_rm].d[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        double dst_real;
        dst_real = *(double*)&dst;
        XMM[cpu_reg].f[0] = dst_real;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTDQ2PS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = XMM[cpu_rm].l[0];
        XMM[cpu_reg].f[1] = XMM[cpu_rm].l[1];
        XMM[cpu_reg].f[2] = XMM[cpu_rm].l[2];
        XMM[cpu_reg].f[3] = XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        dst[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        dst[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].f[0] = dst[0];
        XMM[cpu_reg].f[1] = dst[1];
        XMM[cpu_reg].f[2] = dst[2];
        XMM[cpu_reg].f[3] = dst[3];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTDQ2PS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = XMM[cpu_rm].l[0];
        XMM[cpu_reg].f[1] = XMM[cpu_rm].l[1];
        XMM[cpu_reg].f[2] = XMM[cpu_rm].l[2];
        XMM[cpu_reg].f[3] = XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        dst[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        dst[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].f[0] = dst[0];
        XMM[cpu_reg].f[1] = dst[1];
        XMM[cpu_reg].f[2] = dst[2];
        XMM[cpu_reg].f[3] = dst[3];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTPS2DQ_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].l[0] = XMM[cpu_rm].f[0];
        XMM[cpu_reg].l[1] = XMM[cpu_rm].f[1];
        XMM[cpu_reg].l[2] = XMM[cpu_rm].f[2];
        XMM[cpu_reg].l[3] = XMM[cpu_rm].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        dst[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        dst[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        float dst_real[4];
        dst_real[0] = *(float*)&dst[0];
        dst_real[1] = *(float*)&dst[1];
        dst_real[2] = *(float*)&dst[2];
        dst_real[3] = *(float*)&dst[3];
        XMM[cpu_reg].l[0] = dst_real[0];
        XMM[cpu_reg].l[1] = dst_real[1];
        XMM[cpu_reg].l[2] = dst_real[2];
        XMM[cpu_reg].l[3] = dst_real[3];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTPS2DQ_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].l[0] = XMM[cpu_rm].f[0];
        XMM[cpu_reg].l[1] = XMM[cpu_rm].f[1];
        XMM[cpu_reg].l[2] = XMM[cpu_rm].f[2];
        XMM[cpu_reg].l[3] = XMM[cpu_rm].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        dst[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        dst[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        float dst_real[4];
        dst_real[0] = *(float*)&dst[0];
        dst_real[1] = *(float*)&dst[1];
        dst_real[2] = *(float*)&dst[2];
        dst_real[3] = *(float*)&dst[3];
        XMM[cpu_reg].l[0] = dst_real[0];
        XMM[cpu_reg].l[1] = dst_real[1];
        XMM[cpu_reg].l[2] = dst_real[2];
        XMM[cpu_reg].l[3] = dst_real[3];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTTPS2DQ_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].l[0] = XMM[cpu_rm].f[0];
        XMM[cpu_reg].l[1] = XMM[cpu_rm].f[1];
        XMM[cpu_reg].l[2] = XMM[cpu_rm].f[2];
        XMM[cpu_reg].l[3] = XMM[cpu_rm].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        dst[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        dst[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        float dst_real[4];
        dst_real[0] = *(float*)&dst[0];
        dst_real[1] = *(float*)&dst[1];
        dst_real[2] = *(float*)&dst[2];
        dst_real[3] = *(float*)&dst[3];
        XMM[cpu_reg].l[0] = dst_real[0];
        XMM[cpu_reg].l[1] = dst_real[1];
        XMM[cpu_reg].l[2] = dst_real[2];
        XMM[cpu_reg].l[3] = dst_real[3];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTTPS2DQ_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].l[0] = XMM[cpu_rm].f[0];
        XMM[cpu_reg].l[1] = XMM[cpu_rm].f[1];
        XMM[cpu_reg].l[2] = XMM[cpu_rm].f[2];
        XMM[cpu_reg].l[3] = XMM[cpu_rm].f[3];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        dst[2] = readmeml(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        dst[3] = readmeml(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        float dst_real[4];
        dst_real[0] = *(float*)&dst[0];
        dst_real[1] = *(float*)&dst[1];
        dst_real[2] = *(float*)&dst[2];
        dst_real[3] = *(float*)&dst[3];
        XMM[cpu_reg].l[0] = dst_real[0];
        XMM[cpu_reg].l[1] = dst_real[1];
        XMM[cpu_reg].l[2] = dst_real[2];
        XMM[cpu_reg].l[3] = dst_real[3];
        CLOCK_CYCLES(2);
    }
    return 0;
}
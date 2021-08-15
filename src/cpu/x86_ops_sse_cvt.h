static int opCVTPI2PS_xmm_mm_a16(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = cpu_state.MM[cpu_rm].l[0];
        XMM[cpu_reg].f[1] = cpu_state.MM[cpu_rm].l[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].f[0] = dst[0];
        XMM[cpu_reg].f[1] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTPI2PS_xmm_mm_a32(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = cpu_state.MM[cpu_rm].l[0];
        XMM[cpu_reg].f[1] = cpu_state.MM[cpu_rm].l[1];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst[2];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].f[0] = dst[0];
        XMM[cpu_reg].f[1] = dst[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSI2SS_xmm_l_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = getr32(cpu_rm);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].f[0] = dst;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSI2SS_xmm_l_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        XMM[cpu_reg].f[0] = getr32(cpu_rm);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        XMM[cpu_reg].f[0] = dst;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTTPS2PI_mm_xmm_a16(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].l[0] = XMM[cpu_rm].f[0];
        cpu_state.MM[cpu_reg].l[1] = XMM[cpu_rm].f[1];
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
        cpu_state.MM[cpu_reg].l[0] = dst_real[0];
        cpu_state.MM[cpu_reg].l[1] = dst_real[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTTPS2PI_mm_xmm_a32(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].l[0] = XMM[cpu_rm].f[0];
        cpu_state.MM[cpu_reg].l[1] = XMM[cpu_rm].f[1];
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
        cpu_state.MM[cpu_reg].l[0] = dst_real[0];
        cpu_state.MM[cpu_reg].l[1] = dst_real[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTTSS2SI_l_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        setr32(cpu_reg, XMM[cpu_rm].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float dst_real;
        dst_real = *(float*)&dst;
        setr32(cpu_reg, dst_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTTSS2SI_l_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        setr32(cpu_reg, XMM[cpu_rm].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float dst_real;
        dst_real = *(float*)&dst;
        setr32(cpu_reg, dst_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTPS2PI_mm_xmm_a16(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].l[0] = XMM[cpu_rm].f[0];
        cpu_state.MM[cpu_reg].l[1] = XMM[cpu_rm].f[1];
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
        cpu_state.MM[cpu_reg].l[0] = dst_real[0];
        cpu_state.MM[cpu_reg].l[1] = dst_real[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTPS2PI_mm_xmm_a32(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].l[0] = XMM[cpu_rm].f[0];
        cpu_state.MM[cpu_reg].l[1] = XMM[cpu_rm].f[1];
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
        cpu_state.MM[cpu_reg].l[0] = dst_real[0];
        cpu_state.MM[cpu_reg].l[1] = dst_real[1];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSS2SI_l_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        setr32(cpu_reg, XMM[cpu_rm].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float dst_real;
        dst_real = *(float*)&dst;
        setr32(cpu_reg, dst_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCVTSS2SI_l_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        setr32(cpu_reg, XMM[cpu_rm].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t dst;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float dst_real;
        dst_real = *(float*)&dst;
        setr32(cpu_reg, dst_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}
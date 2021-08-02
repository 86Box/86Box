#include <math.h>

static int opSQRTPS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = sqrt(cpu_state.XMM[cpu_rm].f[0]);
        cpu_state.XMM[cpu_reg].f[1] = sqrt(cpu_state.XMM[cpu_rm].f[1]);
        cpu_state.XMM[cpu_reg].f[2] = sqrt(cpu_state.XMM[cpu_rm].f[2]);
        cpu_state.XMM[cpu_reg].f[3] = sqrt(cpu_state.XMM[cpu_rm].f[3]);
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] = sqrt(src_real[0]);
        cpu_state.XMM[cpu_reg].f[1] = sqrt(src_real[1]);
        cpu_state.XMM[cpu_reg].f[2] = sqrt(src_real[2]);
        cpu_state.XMM[cpu_reg].f[3] = sqrt(src_real[3]);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opSQRTPS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = sqrt(cpu_state.XMM[cpu_rm].f[0]);
        cpu_state.XMM[cpu_reg].f[1] = sqrt(cpu_state.XMM[cpu_rm].f[1]);
        cpu_state.XMM[cpu_reg].f[2] = sqrt(cpu_state.XMM[cpu_rm].f[2]);
        cpu_state.XMM[cpu_reg].f[3] = sqrt(cpu_state.XMM[cpu_rm].f[3]);
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] = sqrt(src_real[0]);
        cpu_state.XMM[cpu_reg].f[1] = sqrt(src_real[1]);
        cpu_state.XMM[cpu_reg].f[2] = sqrt(src_real[2]);
        cpu_state.XMM[cpu_reg].f[3] = sqrt(src_real[3]);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opSQRTSS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = sqrt(cpu_state.XMM[cpu_rm].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] = sqrt(src_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opSQRTSS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = sqrt(cpu_state.XMM[cpu_rm].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] = sqrt(src_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opRSQRTPS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / sqrt(cpu_state.XMM[cpu_rm].f[0]);
        cpu_state.XMM[cpu_reg].f[1] = 1.0 / sqrt(cpu_state.XMM[cpu_rm].f[1]);
        cpu_state.XMM[cpu_reg].f[2] = 1.0 / sqrt(cpu_state.XMM[cpu_rm].f[2]);
        cpu_state.XMM[cpu_reg].f[3] = 1.0 / sqrt(cpu_state.XMM[cpu_rm].f[3]);
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / sqrt(src_real[0]);
        cpu_state.XMM[cpu_reg].f[1] = 1.0 / sqrt(src_real[1]);
        cpu_state.XMM[cpu_reg].f[2] = 1.0 / sqrt(src_real[2]);
        cpu_state.XMM[cpu_reg].f[3] = 1.0 / sqrt(src_real[3]);
    }
    return 0;
}

static int opRSQRTPS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / sqrt(cpu_state.XMM[cpu_rm].f[0]);
        cpu_state.XMM[cpu_reg].f[1] = 1.0 / sqrt(cpu_state.XMM[cpu_rm].f[1]);
        cpu_state.XMM[cpu_reg].f[2] = 1.0 / sqrt(cpu_state.XMM[cpu_rm].f[2]);
        cpu_state.XMM[cpu_reg].f[3] = 1.0 / sqrt(cpu_state.XMM[cpu_rm].f[3]);
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / sqrt(src_real[0]);
        cpu_state.XMM[cpu_reg].f[1] = 1.0 / sqrt(src_real[1]);
        cpu_state.XMM[cpu_reg].f[2] = 1.0 / sqrt(src_real[2]);
        cpu_state.XMM[cpu_reg].f[3] = 1.0 / sqrt(src_real[3]);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opRSQRTSS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / sqrt(cpu_state.XMM[cpu_rm].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / sqrt(src_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opRSQRTSS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / sqrt(cpu_state.XMM[cpu_rm].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / sqrt(src_real);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opRCPSS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / cpu_state.XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / src_real;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opRCPSS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / cpu_state.XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / src_real;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opRCPPS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / cpu_state.XMM[cpu_rm].f[0];
        cpu_state.XMM[cpu_reg].f[1] = 1.0 / cpu_state.XMM[cpu_rm].f[1];
        cpu_state.XMM[cpu_reg].f[2] = 1.0 / cpu_state.XMM[cpu_rm].f[2];
        cpu_state.XMM[cpu_reg].f[3] = 1.0 / cpu_state.XMM[cpu_rm].f[3];
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / src_real[0];
        cpu_state.XMM[cpu_reg].f[1] = 1.0 / src_real[1];
        cpu_state.XMM[cpu_reg].f[2] = 1.0 / src_real[2];
        cpu_state.XMM[cpu_reg].f[3] = 1.0 / src_real[3];
    }
    return 0;
}

static int opRCPPS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / cpu_state.XMM[cpu_rm].f[0];
        cpu_state.XMM[cpu_reg].f[1] = 1.0 / cpu_state.XMM[cpu_rm].f[1];
        cpu_state.XMM[cpu_reg].f[2] = 1.0 / cpu_state.XMM[cpu_rm].f[2];
        cpu_state.XMM[cpu_reg].f[3] = 1.0 / cpu_state.XMM[cpu_rm].f[3];
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] = 1.0 / src_real[0];
        cpu_state.XMM[cpu_reg].f[1] = 1.0 / src_real[1];
        cpu_state.XMM[cpu_reg].f[2] = 1.0 / src_real[2];
        cpu_state.XMM[cpu_reg].f[3] = 1.0 / src_real[3];
    }
    return 0;
}

static int opADDPS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] += cpu_state.XMM[cpu_rm].f[0];
        cpu_state.XMM[cpu_reg].f[1] += cpu_state.XMM[cpu_rm].f[1];
        cpu_state.XMM[cpu_reg].f[2] += cpu_state.XMM[cpu_rm].f[2];
        cpu_state.XMM[cpu_reg].f[3] += cpu_state.XMM[cpu_rm].f[3];
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] += src_real[0];
        cpu_state.XMM[cpu_reg].f[1] += src_real[1];
        cpu_state.XMM[cpu_reg].f[2] += src_real[2];
        cpu_state.XMM[cpu_reg].f[3] += src_real[3];
    }
    return 0;
}

static int opADDPS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] += cpu_state.XMM[cpu_rm].f[0];
        cpu_state.XMM[cpu_reg].f[1] += cpu_state.XMM[cpu_rm].f[1];
        cpu_state.XMM[cpu_reg].f[2] += cpu_state.XMM[cpu_rm].f[2];
        cpu_state.XMM[cpu_reg].f[3] += cpu_state.XMM[cpu_rm].f[3];
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] += src_real[0];
        cpu_state.XMM[cpu_reg].f[1] += src_real[1];
        cpu_state.XMM[cpu_reg].f[2] += src_real[2];
        cpu_state.XMM[cpu_reg].f[3] += src_real[3];
    }
    return 0;
}

static int opADDSS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] += cpu_state.XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] += src_real;
    return 0;
}

static int opADDSS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] += cpu_state.XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] += src_real;
    return 0;
}

static int opMULPS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] *= cpu_state.XMM[cpu_rm].f[0];
        cpu_state.XMM[cpu_reg].f[1] *= cpu_state.XMM[cpu_rm].f[1];
        cpu_state.XMM[cpu_reg].f[2] *= cpu_state.XMM[cpu_rm].f[2];
        cpu_state.XMM[cpu_reg].f[3] *= cpu_state.XMM[cpu_rm].f[3];
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] *= src_real[0];
        cpu_state.XMM[cpu_reg].f[1] *= src_real[1];
        cpu_state.XMM[cpu_reg].f[2] *= src_real[2];
        cpu_state.XMM[cpu_reg].f[3] *= src_real[3];
    }
    return 0;
}

static int opMULPS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] *= cpu_state.XMM[cpu_rm].f[0];
        cpu_state.XMM[cpu_reg].f[1] *= cpu_state.XMM[cpu_rm].f[1];
        cpu_state.XMM[cpu_reg].f[2] *= cpu_state.XMM[cpu_rm].f[2];
        cpu_state.XMM[cpu_reg].f[3] *= cpu_state.XMM[cpu_rm].f[3];
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] *= src_real[0];
        cpu_state.XMM[cpu_reg].f[1] *= src_real[1];
        cpu_state.XMM[cpu_reg].f[2] *= src_real[2];
        cpu_state.XMM[cpu_reg].f[3] *= src_real[3];
    }
    return 0;
}

static int opMULSS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] *= cpu_state.XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] *= src_real;
    return 0;
}

static int opMULSS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] *= cpu_state.XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] *= src_real;
    return 0;
}

static int opSUBPS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] -= cpu_state.XMM[cpu_rm].f[0];
        cpu_state.XMM[cpu_reg].f[1] -= cpu_state.XMM[cpu_rm].f[1];
        cpu_state.XMM[cpu_reg].f[2] -= cpu_state.XMM[cpu_rm].f[2];
        cpu_state.XMM[cpu_reg].f[3] -= cpu_state.XMM[cpu_rm].f[3];
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] -= src_real[0];
        cpu_state.XMM[cpu_reg].f[1] -= src_real[1];
        cpu_state.XMM[cpu_reg].f[2] -= src_real[2];
        cpu_state.XMM[cpu_reg].f[3] -= src_real[3];
    }
    return 0;
}

static int opSUBPS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] -= cpu_state.XMM[cpu_rm].f[0];
        cpu_state.XMM[cpu_reg].f[1] -= cpu_state.XMM[cpu_rm].f[1];
        cpu_state.XMM[cpu_reg].f[2] -= cpu_state.XMM[cpu_rm].f[2];
        cpu_state.XMM[cpu_reg].f[3] -= cpu_state.XMM[cpu_rm].f[3];
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] -= src_real[0];
        cpu_state.XMM[cpu_reg].f[1] -= src_real[1];
        cpu_state.XMM[cpu_reg].f[2] -= src_real[2];
        cpu_state.XMM[cpu_reg].f[3] -= src_real[3];
    }
    return 0;
}

static int opSUBSS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] -= cpu_state.XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] -= src_real;
    return 0;
}

static int opSUBSS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] -= cpu_state.XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] -= src_real;
    return 0;
}

static int opMINPS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = min(cpu_state.XMM[cpu_rm].f[0], cpu_state.XMM[cpu_reg].f[0]);
        cpu_state.XMM[cpu_reg].f[1] = min(cpu_state.XMM[cpu_rm].f[1], cpu_state.XMM[cpu_reg].f[1]);
        cpu_state.XMM[cpu_reg].f[2] = min(cpu_state.XMM[cpu_rm].f[2], cpu_state.XMM[cpu_reg].f[2]);
        cpu_state.XMM[cpu_reg].f[3] = min(cpu_state.XMM[cpu_rm].f[3], cpu_state.XMM[cpu_reg].f[3]);
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] = min(cpu_state.XMM[cpu_reg].f[0], src_real[0]);
        cpu_state.XMM[cpu_reg].f[1] = min(cpu_state.XMM[cpu_reg].f[0], src_real[1]);
        cpu_state.XMM[cpu_reg].f[2] = min(cpu_state.XMM[cpu_reg].f[0], src_real[2]);
        cpu_state.XMM[cpu_reg].f[3] = min(cpu_state.XMM[cpu_reg].f[0], src_real[3]);
    }
    return 0;
}

static int opMINPS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = min(cpu_state.XMM[cpu_rm].f[0], cpu_state.XMM[cpu_reg].f[0]);
        cpu_state.XMM[cpu_reg].f[1] = min(cpu_state.XMM[cpu_rm].f[1], cpu_state.XMM[cpu_reg].f[1]);
        cpu_state.XMM[cpu_reg].f[2] = min(cpu_state.XMM[cpu_rm].f[2], cpu_state.XMM[cpu_reg].f[2]);
        cpu_state.XMM[cpu_reg].f[3] = min(cpu_state.XMM[cpu_rm].f[3], cpu_state.XMM[cpu_reg].f[3]);
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] = min(cpu_state.XMM[cpu_reg].f[0], src_real[0]);
        cpu_state.XMM[cpu_reg].f[1] = min(cpu_state.XMM[cpu_reg].f[0], src_real[1]);
        cpu_state.XMM[cpu_reg].f[2] = min(cpu_state.XMM[cpu_reg].f[0], src_real[2]);
        cpu_state.XMM[cpu_reg].f[3] = min(cpu_state.XMM[cpu_reg].f[0], src_real[3]);
    }
    return 0;
}

static int opMINSS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = min(cpu_state.XMM[cpu_rm].f[0], cpu_state.XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] = min(cpu_state.XMM[cpu_reg].f[0], src_real);
    }
    return 0;
}

static int opMINSS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = min(cpu_state.XMM[cpu_rm].f[0], cpu_state.XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] = min(cpu_state.XMM[cpu_reg].f[0], src_real);
    }
    return 0;
}

static int opDIVPS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] /= cpu_state.XMM[cpu_rm].f[0];
        cpu_state.XMM[cpu_reg].f[1] /= cpu_state.XMM[cpu_rm].f[1];
        cpu_state.XMM[cpu_reg].f[2] /= cpu_state.XMM[cpu_rm].f[2];
        cpu_state.XMM[cpu_reg].f[3] /= cpu_state.XMM[cpu_rm].f[3];
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] /= src_real[0];
        cpu_state.XMM[cpu_reg].f[1] /= src_real[1];
        cpu_state.XMM[cpu_reg].f[2] /= src_real[2];
        cpu_state.XMM[cpu_reg].f[3] /= src_real[3];
    }
    return 0;
}

static int opDIVPS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] /= cpu_state.XMM[cpu_rm].f[0];
        cpu_state.XMM[cpu_reg].f[1] /= cpu_state.XMM[cpu_rm].f[1];
        cpu_state.XMM[cpu_reg].f[2] /= cpu_state.XMM[cpu_rm].f[2];
        cpu_state.XMM[cpu_reg].f[3] /= cpu_state.XMM[cpu_rm].f[3];
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] /= src_real[0];
        cpu_state.XMM[cpu_reg].f[1] /= src_real[1];
        cpu_state.XMM[cpu_reg].f[2] /= src_real[2];
        cpu_state.XMM[cpu_reg].f[3] /= src_real[3];
    }
    return 0;
}

static int opDIVSS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] /= cpu_state.XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] /= src_real;
    return 0;
}

static int opDIVSS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] /= cpu_state.XMM[cpu_rm].f[0];
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] /= src_real;
    return 0;
}

static int opMAXPS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = max(cpu_state.XMM[cpu_rm].f[0], cpu_state.XMM[cpu_reg].f[0]);
        cpu_state.XMM[cpu_reg].f[1] = max(cpu_state.XMM[cpu_rm].f[1], cpu_state.XMM[cpu_reg].f[1]);
        cpu_state.XMM[cpu_reg].f[2] = max(cpu_state.XMM[cpu_rm].f[2], cpu_state.XMM[cpu_reg].f[2]);
        cpu_state.XMM[cpu_reg].f[3] = max(cpu_state.XMM[cpu_rm].f[3], cpu_state.XMM[cpu_reg].f[3]);
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] = max(cpu_state.XMM[cpu_reg].f[0], src_real[0]);
        cpu_state.XMM[cpu_reg].f[1] = max(cpu_state.XMM[cpu_reg].f[0], src_real[1]);
        cpu_state.XMM[cpu_reg].f[2] = max(cpu_state.XMM[cpu_reg].f[0], src_real[2]);
        cpu_state.XMM[cpu_reg].f[3] = max(cpu_state.XMM[cpu_reg].f[0], src_real[3]);
    }
    return 0;
}

static int opMAXPS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = max(cpu_state.XMM[cpu_rm].f[0], cpu_state.XMM[cpu_reg].f[0]);
        cpu_state.XMM[cpu_reg].f[1] = max(cpu_state.XMM[cpu_rm].f[1], cpu_state.XMM[cpu_reg].f[1]);
        cpu_state.XMM[cpu_reg].f[2] = max(cpu_state.XMM[cpu_rm].f[2], cpu_state.XMM[cpu_reg].f[2]);
        cpu_state.XMM[cpu_reg].f[3] = max(cpu_state.XMM[cpu_rm].f[3], cpu_state.XMM[cpu_reg].f[3]);
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
        float src_real[4];
        src_real[0] = *(float*)&src[0];
        src_real[1] = *(float*)&src[1];
        src_real[2] = *(float*)&src[2];
        src_real[3] = *(float*)&src[3];
        cpu_state.XMM[cpu_reg].f[0] = max(cpu_state.XMM[cpu_reg].f[0], src_real[0]);
        cpu_state.XMM[cpu_reg].f[1] = max(cpu_state.XMM[cpu_reg].f[0], src_real[1]);
        cpu_state.XMM[cpu_reg].f[2] = max(cpu_state.XMM[cpu_reg].f[0], src_real[2]);
        cpu_state.XMM[cpu_reg].f[3] = max(cpu_state.XMM[cpu_reg].f[0], src_real[3]);
    }
    return 0;
}

static int opMAXSS_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = max(cpu_state.XMM[cpu_rm].f[0], cpu_state.XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] = max(cpu_state.XMM[cpu_reg].f[0], src_real);
    }
    return 0;
}

static int opMAXSS_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].f[0] = max(cpu_state.XMM[cpu_rm].f[0], cpu_state.XMM[cpu_reg].f[0]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&src;
        cpu_state.XMM[cpu_reg].f[0] = max(cpu_state.XMM[cpu_reg].f[0], src_real);
    }
    return 0;
}

static int opCMPPS_xmm_xmm_a16(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    uint32_t cmp[4];
    if (cpu_mod == 3)
    {
        switch(imm & 7)
        {
            case 0:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] == cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] == cpu_state.XMM[cpu_rm].f[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] == cpu_state.XMM[cpu_rm].f[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] == cpu_state.XMM[cpu_rm].f[3] ? ~0 : 0;
                break;
            }
            case 1:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] < cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] < cpu_state.XMM[cpu_rm].f[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] < cpu_state.XMM[cpu_rm].f[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] < cpu_state.XMM[cpu_rm].f[3] ? ~0 : 0;
                break;
            }
            case 2:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] <= cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] <= cpu_state.XMM[cpu_rm].f[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] <= cpu_state.XMM[cpu_rm].f[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] <= cpu_state.XMM[cpu_rm].f[3] ? ~0 : 0;
                break;
            }
            case 3:
            {
                //TODO: NaNs
                cmp[0] = 0;
                cmp[1] = 0;
                cmp[2] = 0;
                cmp[3] = 0;
                break;
            }
            case 4:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] != cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] != cpu_state.XMM[cpu_rm].f[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] != cpu_state.XMM[cpu_rm].f[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] != cpu_state.XMM[cpu_rm].f[3] ? ~0 : 0;
                break;
            }
            case 5:
            {
                cmp[0] = !(cpu_state.XMM[cpu_reg].f[0] < cpu_state.XMM[cpu_rm].f[0]) ? ~0 : 0;
                cmp[1] = !(cpu_state.XMM[cpu_reg].f[1] < cpu_state.XMM[cpu_rm].f[1]) ? ~0 : 0;
                cmp[2] = !(cpu_state.XMM[cpu_reg].f[2] < cpu_state.XMM[cpu_rm].f[2]) ? ~0 : 0;
                cmp[3] = !(cpu_state.XMM[cpu_reg].f[3] < cpu_state.XMM[cpu_rm].f[3]) ? ~0 : 0;
                break;
            }
            case 6:
            {
                cmp[0] = !(cpu_state.XMM[cpu_reg].f[0] <= cpu_state.XMM[cpu_rm].f[0]) ? ~0 : 0;
                cmp[1] = !(cpu_state.XMM[cpu_reg].f[1] <= cpu_state.XMM[cpu_rm].f[1]) ? ~0 : 0;
                cmp[2] = !(cpu_state.XMM[cpu_reg].f[2] <= cpu_state.XMM[cpu_rm].f[2]) ? ~0 : 0;
                cmp[3] = !(cpu_state.XMM[cpu_reg].f[3] <= cpu_state.XMM[cpu_rm].f[3]) ? ~0 : 0;
                break;
            }
            case 7:
            {
                //TODO: NaNs
                cmp[0] = ~0;
                cmp[1] = ~0;
                cmp[2] = ~0;
                cmp[3] = ~0;
                break;
            }
        }
        cpu_state.XMM[cpu_reg].l[0] = cmp[0];
        cpu_state.XMM[cpu_reg].l[1] = cmp[1];
        cpu_state.XMM[cpu_reg].l[2] = cmp[2];
        cpu_state.XMM[cpu_reg].l[3] = cmp[3];
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
        float src_real[4];
        src_real[0] = *(float*)&dst[0];
        src_real[1] = *(float*)&dst[1];
        src_real[2] = *(float*)&dst[2];
        src_real[3] = *(float*)&dst[3];
        switch(imm & 7)
        {
            case 0:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] == src_real[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] == src_real[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] == src_real[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] == src_real[3] ? ~0 : 0;
                break;
            }
            case 1:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] < src_real[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] < src_real[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] < src_real[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] < src_real[3] ? ~0 : 0;
                break;
            }
            case 2:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] <= src_real[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] <= src_real[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] <= src_real[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] <= src_real[3] ? ~0 : 0;
                break;
            }
            case 3:
            {
                //TODO: NaNs
                cmp[0] = 0;
                cmp[1] = 0;
                cmp[2] = 0;
                cmp[3] = 0;
                break;
            }
            case 4:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] != src_real[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] != src_real[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] != src_real[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] != src_real[3] ? ~0 : 0;
                break;
            }
            case 5:
            {
                cmp[0] = !(cpu_state.XMM[cpu_reg].f[0] < src_real[0]) ? ~0 : 0;
                cmp[1] = !(cpu_state.XMM[cpu_reg].f[1] < src_real[1]) ? ~0 : 0;
                cmp[2] = !(cpu_state.XMM[cpu_reg].f[2] < src_real[2]) ? ~0 : 0;
                cmp[3] = !(cpu_state.XMM[cpu_reg].f[3] < src_real[3]) ? ~0 : 0;
                break;
            }
            case 6:
            {
                cmp[0] = !(cpu_state.XMM[cpu_reg].f[0] <= src_real[0]) ? ~0 : 0;
                cmp[1] = !(cpu_state.XMM[cpu_reg].f[1] <= src_real[1]) ? ~0 : 0;
                cmp[2] = !(cpu_state.XMM[cpu_reg].f[2] <= src_real[2]) ? ~0 : 0;
                cmp[3] = !(cpu_state.XMM[cpu_reg].f[3] <= src_real[3]) ? ~0 : 0;
                break;
            }
            case 7:
            {
                //TODO: NaNs
                cmp[0] = ~0;
                cmp[1] = ~0;
                cmp[2] = ~0;
                cmp[3] = ~0;
                break;
            }
        }
        cpu_state.XMM[cpu_reg].l[0] = cmp[0];
        cpu_state.XMM[cpu_reg].l[1] = cmp[1];
        cpu_state.XMM[cpu_reg].l[2] = cmp[2];
        cpu_state.XMM[cpu_reg].l[3] = cmp[3];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCMPPS_xmm_xmm_a32(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    uint32_t cmp[4];
    if (cpu_mod == 3)
    {
        switch(imm & 7)
        {
            case 0:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] == cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] == cpu_state.XMM[cpu_rm].f[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] == cpu_state.XMM[cpu_rm].f[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] == cpu_state.XMM[cpu_rm].f[3] ? ~0 : 0;
                break;
            }
            case 1:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] < cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] < cpu_state.XMM[cpu_rm].f[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] < cpu_state.XMM[cpu_rm].f[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] < cpu_state.XMM[cpu_rm].f[3] ? ~0 : 0;
                break;
            }
            case 2:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] <= cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] <= cpu_state.XMM[cpu_rm].f[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] <= cpu_state.XMM[cpu_rm].f[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] <= cpu_state.XMM[cpu_rm].f[3] ? ~0 : 0;
                break;
            }
            case 3:
            {
                //TODO: NaNs
                cmp[0] = 0;
                cmp[1] = 0;
                cmp[2] = 0;
                cmp[3] = 0;
                break;
            }
            case 4:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] != cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] != cpu_state.XMM[cpu_rm].f[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] != cpu_state.XMM[cpu_rm].f[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] != cpu_state.XMM[cpu_rm].f[3] ? ~0 : 0;
                break;
            }
            case 5:
            {
                cmp[0] = !(cpu_state.XMM[cpu_reg].f[0] < cpu_state.XMM[cpu_rm].f[0]) ? ~0 : 0;
                cmp[1] = !(cpu_state.XMM[cpu_reg].f[1] < cpu_state.XMM[cpu_rm].f[1]) ? ~0 : 0;
                cmp[2] = !(cpu_state.XMM[cpu_reg].f[2] < cpu_state.XMM[cpu_rm].f[2]) ? ~0 : 0;
                cmp[3] = !(cpu_state.XMM[cpu_reg].f[3] < cpu_state.XMM[cpu_rm].f[3]) ? ~0 : 0;
                break;
            }
            case 6:
            {
                cmp[0] = !(cpu_state.XMM[cpu_reg].f[0] <= cpu_state.XMM[cpu_rm].f[0]) ? ~0 : 0;
                cmp[1] = !(cpu_state.XMM[cpu_reg].f[1] <= cpu_state.XMM[cpu_rm].f[1]) ? ~0 : 0;
                cmp[2] = !(cpu_state.XMM[cpu_reg].f[2] <= cpu_state.XMM[cpu_rm].f[2]) ? ~0 : 0;
                cmp[3] = !(cpu_state.XMM[cpu_reg].f[3] <= cpu_state.XMM[cpu_rm].f[3]) ? ~0 : 0;
                break;
            }
            case 7:
            {
                //TODO: NaNs
                cmp[0] = ~0;
                cmp[1] = ~0;
                cmp[2] = ~0;
                cmp[3] = ~0;
                break;
            }
        }
        cpu_state.XMM[cpu_reg].l[0] = cmp[0];
        cpu_state.XMM[cpu_reg].l[1] = cmp[1];
        cpu_state.XMM[cpu_reg].l[2] = cmp[2];
        cpu_state.XMM[cpu_reg].l[3] = cmp[3];
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
        float src_real[4];
        src_real[0] = *(float*)&dst[0];
        src_real[1] = *(float*)&dst[1];
        src_real[2] = *(float*)&dst[2];
        src_real[3] = *(float*)&dst[3];
        switch(imm & 7)
        {
            case 0:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] == src_real[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] == src_real[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] == src_real[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] == src_real[3] ? ~0 : 0;
                break;
            }
            case 1:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] < src_real[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] < src_real[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] < src_real[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] < src_real[3] ? ~0 : 0;
                break;
            }
            case 2:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] <= src_real[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] <= src_real[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] <= src_real[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] <= src_real[3] ? ~0 : 0;
                break;
            }
            case 3:
            {
                //TODO: NaNs
                cmp[0] = 0;
                cmp[1] = 0;
                cmp[2] = 0;
                cmp[3] = 0;
                break;
            }
            case 4:
            {
                cmp[0] = cpu_state.XMM[cpu_reg].f[0] != src_real[0] ? ~0 : 0;
                cmp[1] = cpu_state.XMM[cpu_reg].f[1] != src_real[1] ? ~0 : 0;
                cmp[2] = cpu_state.XMM[cpu_reg].f[2] != src_real[2] ? ~0 : 0;
                cmp[3] = cpu_state.XMM[cpu_reg].f[3] != src_real[3] ? ~0 : 0;
                break;
            }
            case 5:
            {
                cmp[0] = !(cpu_state.XMM[cpu_reg].f[0] < src_real[0]) ? ~0 : 0;
                cmp[1] = !(cpu_state.XMM[cpu_reg].f[1] < src_real[1]) ? ~0 : 0;
                cmp[2] = !(cpu_state.XMM[cpu_reg].f[2] < src_real[2]) ? ~0 : 0;
                cmp[3] = !(cpu_state.XMM[cpu_reg].f[3] < src_real[3]) ? ~0 : 0;
                break;
            }
            case 6:
            {
                cmp[0] = !(cpu_state.XMM[cpu_reg].f[0] <= src_real[0]) ? ~0 : 0;
                cmp[1] = !(cpu_state.XMM[cpu_reg].f[1] <= src_real[1]) ? ~0 : 0;
                cmp[2] = !(cpu_state.XMM[cpu_reg].f[2] <= src_real[2]) ? ~0 : 0;
                cmp[3] = !(cpu_state.XMM[cpu_reg].f[3] <= src_real[3]) ? ~0 : 0;
                break;
            }
            case 7:
            {
                //TODO: NaNs
                cmp[0] = ~0;
                cmp[1] = ~0;
                cmp[2] = ~0;
                cmp[3] = ~0;
                break;
            }
        }
        cpu_state.XMM[cpu_reg].l[0] = cmp[0];
        cpu_state.XMM[cpu_reg].l[1] = cmp[1];
        cpu_state.XMM[cpu_reg].l[2] = cmp[2];
        cpu_state.XMM[cpu_reg].l[3] = cmp[3];
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCMPSS_xmm_xmm_a16(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    uint32_t cmp;
    if (cpu_mod == 3)
    {
        switch(imm & 7)
        {
            case 0:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] == cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                break;
            }
            case 1:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] < cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                break;
            }
            case 2:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] <= cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                break;
            }
            case 3:
            {
                //TODO: NaNs
                cmp = 0;
                break;
            }
            case 4:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] != cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                break;
            }
            case 5:
            {
                cmp = !(cpu_state.XMM[cpu_reg].f[0] < cpu_state.XMM[cpu_rm].f[0]) ? ~0 : 0;
                break;
            }
            case 6:
            {
                cmp = !(cpu_state.XMM[cpu_reg].f[0] <= cpu_state.XMM[cpu_rm].f[0]) ? ~0 : 0;
                break;
            }
            case 7:
            {
                //TODO: NaNs
                cmp = ~0;
                break;
            }
        }
        cpu_state.XMM[cpu_reg].l[0] = cmp;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&dst;
        switch(imm & 7)
        {
            case 0:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] == src_real ? ~0 : 0;
                break;
            }
            case 1:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] < src_real ? ~0 : 0;
                break;
            }
            case 2:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] <= src_real ? ~0 : 0;
                break;
            }
            case 3:
            {
                //TODO: NaNs
                cmp = 0;
                break;
            }
            case 4:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] != src_real ? ~0 : 0;
                break;
            }
            case 5:
            {
                cmp = !(cpu_state.XMM[cpu_reg].f[0] < src_real) ? ~0 : 0;
                break;
            }
            case 6:
            {
                cmp = !(cpu_state.XMM[cpu_reg].f[0] <= src_real) ? ~0 : 0;
                break;
            }
            case 7:
            {
                //TODO: NaNs
                cmp = ~0;
                break;
            }
        }
        cpu_state.XMM[cpu_reg].l[0] = cmp;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCMPSS_xmm_xmm_a32(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    uint32_t cmp;
    if (cpu_mod == 3)
    {
        switch(imm & 7)
        {
            case 0:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] == cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                break;
            }
            case 1:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] < cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                break;
            }
            case 2:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] <= cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                break;
            }
            case 3:
            {
                //TODO: NaNs
                cmp = 0;
                break;
            }
            case 4:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] != cpu_state.XMM[cpu_rm].f[0] ? ~0 : 0;
                break;
            }
            case 5:
            {
                cmp = !(cpu_state.XMM[cpu_reg].f[0] < cpu_state.XMM[cpu_rm].f[0]) ? ~0 : 0;
                break;
            }
            case 6:
            {
                cmp = !(cpu_state.XMM[cpu_reg].f[0] <= cpu_state.XMM[cpu_rm].f[0]) ? ~0 : 0;
                break;
            }
            case 7:
            {
                //TODO: NaNs
                cmp = ~0;
                break;
            }
        }
        cpu_state.XMM[cpu_reg].l[0] = cmp;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        float src_real;
        src_real = *(float*)&dst;
        switch(imm & 7)
        {
            case 0:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] == src_real ? ~0 : 0;
                break;
            }
            case 1:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] < src_real ? ~0 : 0;
                break;
            }
            case 2:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] <= src_real ? ~0 : 0;
                break;
            }
            case 3:
            {
                //TODO: NaNs
                cmp = 0;
                break;
            }
            case 4:
            {
                cmp = cpu_state.XMM[cpu_reg].f[0] != src_real ? ~0 : 0;
                break;
            }
            case 5:
            {
                cmp = !(cpu_state.XMM[cpu_reg].f[0] < src_real) ? ~0 : 0;
                break;
            }
            case 6:
            {
                cmp = !(cpu_state.XMM[cpu_reg].f[0] <= src_real) ? ~0 : 0;
                break;
            }
            case 7:
            {
                //TODO: NaNs
                cmp = ~0;
                break;
            }
        }
        cpu_state.XMM[cpu_reg].l[0] = cmp;
        CLOCK_CYCLES(2);
    }
    return 0;
}
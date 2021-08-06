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

static int opPMINUB_mm_mm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].b[0] = min(cpu_state.MM[cpu_reg].b[0], cpu_state.MM[cpu_rm].b[0]);
        cpu_state.MM[cpu_reg].b[1] = min(cpu_state.MM[cpu_reg].b[1], cpu_state.MM[cpu_rm].b[1]);
        cpu_state.MM[cpu_reg].b[2] = min(cpu_state.MM[cpu_reg].b[2], cpu_state.MM[cpu_rm].b[2]);
        cpu_state.MM[cpu_reg].b[3] = min(cpu_state.MM[cpu_reg].b[3], cpu_state.MM[cpu_rm].b[3]);
        cpu_state.MM[cpu_reg].b[4] = min(cpu_state.MM[cpu_reg].b[4], cpu_state.MM[cpu_rm].b[4]);
        cpu_state.MM[cpu_reg].b[5] = min(cpu_state.MM[cpu_reg].b[5], cpu_state.MM[cpu_rm].b[5]);
        cpu_state.MM[cpu_reg].b[6] = min(cpu_state.MM[cpu_reg].b[6], cpu_state.MM[cpu_rm].b[6]);
        cpu_state.MM[cpu_reg].b[7] = min(cpu_state.MM[cpu_reg].b[7], cpu_state.MM[cpu_rm].b[7]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[8];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].b[0] = min(cpu_state.MM[cpu_reg].b[0], src[0]);
        cpu_state.MM[cpu_reg].b[1] = min(cpu_state.MM[cpu_reg].b[1], src[1]);
        cpu_state.MM[cpu_reg].b[2] = min(cpu_state.MM[cpu_reg].b[2], src[2]);
        cpu_state.MM[cpu_reg].b[3] = min(cpu_state.MM[cpu_reg].b[3], src[3]);
        cpu_state.MM[cpu_reg].b[4] = min(cpu_state.MM[cpu_reg].b[4], src[4]);
        cpu_state.MM[cpu_reg].b[5] = min(cpu_state.MM[cpu_reg].b[5], src[5]);
        cpu_state.MM[cpu_reg].b[6] = min(cpu_state.MM[cpu_reg].b[6], src[6]);
        cpu_state.MM[cpu_reg].b[7] = min(cpu_state.MM[cpu_reg].b[7], src[7]);
    return 0;
}

static int opPMINUB_mm_mm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].b[0] = min(cpu_state.MM[cpu_reg].b[0], cpu_state.MM[cpu_rm].b[0]);
        cpu_state.MM[cpu_reg].b[1] = min(cpu_state.MM[cpu_reg].b[1], cpu_state.MM[cpu_rm].b[1]);
        cpu_state.MM[cpu_reg].b[2] = min(cpu_state.MM[cpu_reg].b[2], cpu_state.MM[cpu_rm].b[2]);
        cpu_state.MM[cpu_reg].b[3] = min(cpu_state.MM[cpu_reg].b[3], cpu_state.MM[cpu_rm].b[3]);
        cpu_state.MM[cpu_reg].b[4] = min(cpu_state.MM[cpu_reg].b[4], cpu_state.MM[cpu_rm].b[4]);
        cpu_state.MM[cpu_reg].b[5] = min(cpu_state.MM[cpu_reg].b[5], cpu_state.MM[cpu_rm].b[5]);
        cpu_state.MM[cpu_reg].b[6] = min(cpu_state.MM[cpu_reg].b[6], cpu_state.MM[cpu_rm].b[6]);
        cpu_state.MM[cpu_reg].b[7] = min(cpu_state.MM[cpu_reg].b[7], cpu_state.MM[cpu_rm].b[7]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[8];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].b[0] = min(cpu_state.MM[cpu_reg].b[0], src[0]);
        cpu_state.MM[cpu_reg].b[1] = min(cpu_state.MM[cpu_reg].b[1], src[1]);
        cpu_state.MM[cpu_reg].b[2] = min(cpu_state.MM[cpu_reg].b[2], src[2]);
        cpu_state.MM[cpu_reg].b[3] = min(cpu_state.MM[cpu_reg].b[3], src[3]);
        cpu_state.MM[cpu_reg].b[4] = min(cpu_state.MM[cpu_reg].b[4], src[4]);
        cpu_state.MM[cpu_reg].b[5] = min(cpu_state.MM[cpu_reg].b[5], src[5]);
        cpu_state.MM[cpu_reg].b[6] = min(cpu_state.MM[cpu_reg].b[6], src[6]);
        cpu_state.MM[cpu_reg].b[7] = min(cpu_state.MM[cpu_reg].b[7], src[7]);
    return 0;
}

static int opPMINUB_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].b[0] = min(cpu_state.XMM[cpu_reg].b[0], cpu_state.XMM[cpu_rm].b[0]);
        cpu_state.XMM[cpu_reg].b[1] = min(cpu_state.XMM[cpu_reg].b[1], cpu_state.XMM[cpu_rm].b[1]);
        cpu_state.XMM[cpu_reg].b[2] = min(cpu_state.XMM[cpu_reg].b[2], cpu_state.XMM[cpu_rm].b[2]);
        cpu_state.XMM[cpu_reg].b[3] = min(cpu_state.XMM[cpu_reg].b[3], cpu_state.XMM[cpu_rm].b[3]);
        cpu_state.XMM[cpu_reg].b[4] = min(cpu_state.XMM[cpu_reg].b[4], cpu_state.XMM[cpu_rm].b[4]);
        cpu_state.XMM[cpu_reg].b[5] = min(cpu_state.XMM[cpu_reg].b[5], cpu_state.XMM[cpu_rm].b[5]);
        cpu_state.XMM[cpu_reg].b[6] = min(cpu_state.XMM[cpu_reg].b[6], cpu_state.XMM[cpu_rm].b[6]);
        cpu_state.XMM[cpu_reg].b[7] = min(cpu_state.XMM[cpu_reg].b[7], cpu_state.XMM[cpu_rm].b[7]);
        cpu_state.XMM[cpu_reg].b[8] = min(cpu_state.XMM[cpu_reg].b[8], cpu_state.XMM[cpu_rm].b[8]);
        cpu_state.XMM[cpu_reg].b[9] = min(cpu_state.XMM[cpu_reg].b[9], cpu_state.XMM[cpu_rm].b[9]);
        cpu_state.XMM[cpu_reg].b[10] = min(cpu_state.XMM[cpu_reg].b[10], cpu_state.XMM[cpu_rm].b[10]);
        cpu_state.XMM[cpu_reg].b[11] = min(cpu_state.XMM[cpu_reg].b[11], cpu_state.XMM[cpu_rm].b[11]);
        cpu_state.XMM[cpu_reg].b[12] = min(cpu_state.XMM[cpu_reg].b[12], cpu_state.XMM[cpu_rm].b[12]);
        cpu_state.XMM[cpu_reg].b[13] = min(cpu_state.XMM[cpu_reg].b[13], cpu_state.XMM[cpu_rm].b[13]);
        cpu_state.XMM[cpu_reg].b[14] = min(cpu_state.XMM[cpu_reg].b[14], cpu_state.XMM[cpu_rm].b[14]);
        cpu_state.XMM[cpu_reg].b[15] = min(cpu_state.XMM[cpu_reg].b[15], cpu_state.XMM[cpu_rm].b[15]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[16];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        src[8] = readmemb(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        src[9] = readmemb(easeg, cpu_state.eaaddr + 9); if (cpu_state.abrt) return 1;
        src[10] = readmemb(easeg, cpu_state.eaaddr + 10); if (cpu_state.abrt) return 1;
        src[11] = readmemb(easeg, cpu_state.eaaddr + 11); if (cpu_state.abrt) return 1;
        src[12] = readmemb(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        src[13] = readmemb(easeg, cpu_state.eaaddr + 13); if (cpu_state.abrt) return 1;
        src[14] = readmemb(easeg, cpu_state.eaaddr + 14); if (cpu_state.abrt) return 1;
        src[15] = readmemb(easeg, cpu_state.eaaddr + 15); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].b[0] = min(cpu_state.XMM[cpu_reg].b[0], src[0]);
        cpu_state.XMM[cpu_reg].b[1] = min(cpu_state.XMM[cpu_reg].b[1], src[1]);
        cpu_state.XMM[cpu_reg].b[2] = min(cpu_state.XMM[cpu_reg].b[2], src[2]);
        cpu_state.XMM[cpu_reg].b[3] = min(cpu_state.XMM[cpu_reg].b[3], src[3]);
        cpu_state.XMM[cpu_reg].b[4] = min(cpu_state.XMM[cpu_reg].b[4], src[4]);
        cpu_state.XMM[cpu_reg].b[5] = min(cpu_state.XMM[cpu_reg].b[5], src[5]);
        cpu_state.XMM[cpu_reg].b[6] = min(cpu_state.XMM[cpu_reg].b[6], src[6]);
        cpu_state.XMM[cpu_reg].b[7] = min(cpu_state.XMM[cpu_reg].b[7], src[7]);
        cpu_state.XMM[cpu_reg].b[8] = min(cpu_state.XMM[cpu_reg].b[8], src[8]);
        cpu_state.XMM[cpu_reg].b[9] = min(cpu_state.XMM[cpu_reg].b[9], src[9]);
        cpu_state.XMM[cpu_reg].b[10] = min(cpu_state.XMM[cpu_reg].b[10], src[10]);
        cpu_state.XMM[cpu_reg].b[11] = min(cpu_state.XMM[cpu_reg].b[11], src[11]);
        cpu_state.XMM[cpu_reg].b[12] = min(cpu_state.XMM[cpu_reg].b[12], src[12]);
        cpu_state.XMM[cpu_reg].b[13] = min(cpu_state.XMM[cpu_reg].b[13], src[13]);
        cpu_state.XMM[cpu_reg].b[14] = min(cpu_state.XMM[cpu_reg].b[14], src[14]);
        cpu_state.XMM[cpu_reg].b[15] = min(cpu_state.XMM[cpu_reg].b[15], src[15]);
    return 0;
}

static int opPMINUB_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].b[0] = min(cpu_state.XMM[cpu_reg].b[0], cpu_state.XMM[cpu_rm].b[0]);
        cpu_state.XMM[cpu_reg].b[1] = min(cpu_state.XMM[cpu_reg].b[1], cpu_state.XMM[cpu_rm].b[1]);
        cpu_state.XMM[cpu_reg].b[2] = min(cpu_state.XMM[cpu_reg].b[2], cpu_state.XMM[cpu_rm].b[2]);
        cpu_state.XMM[cpu_reg].b[3] = min(cpu_state.XMM[cpu_reg].b[3], cpu_state.XMM[cpu_rm].b[3]);
        cpu_state.XMM[cpu_reg].b[4] = min(cpu_state.XMM[cpu_reg].b[4], cpu_state.XMM[cpu_rm].b[4]);
        cpu_state.XMM[cpu_reg].b[5] = min(cpu_state.XMM[cpu_reg].b[5], cpu_state.XMM[cpu_rm].b[5]);
        cpu_state.XMM[cpu_reg].b[6] = min(cpu_state.XMM[cpu_reg].b[6], cpu_state.XMM[cpu_rm].b[6]);
        cpu_state.XMM[cpu_reg].b[7] = min(cpu_state.XMM[cpu_reg].b[7], cpu_state.XMM[cpu_rm].b[7]);
        cpu_state.XMM[cpu_reg].b[8] = min(cpu_state.XMM[cpu_reg].b[8], cpu_state.XMM[cpu_rm].b[8]);
        cpu_state.XMM[cpu_reg].b[9] = min(cpu_state.XMM[cpu_reg].b[9], cpu_state.XMM[cpu_rm].b[9]);
        cpu_state.XMM[cpu_reg].b[10] = min(cpu_state.XMM[cpu_reg].b[10], cpu_state.XMM[cpu_rm].b[10]);
        cpu_state.XMM[cpu_reg].b[11] = min(cpu_state.XMM[cpu_reg].b[11], cpu_state.XMM[cpu_rm].b[11]);
        cpu_state.XMM[cpu_reg].b[12] = min(cpu_state.XMM[cpu_reg].b[12], cpu_state.XMM[cpu_rm].b[12]);
        cpu_state.XMM[cpu_reg].b[13] = min(cpu_state.XMM[cpu_reg].b[13], cpu_state.XMM[cpu_rm].b[13]);
        cpu_state.XMM[cpu_reg].b[14] = min(cpu_state.XMM[cpu_reg].b[14], cpu_state.XMM[cpu_rm].b[14]);
        cpu_state.XMM[cpu_reg].b[15] = min(cpu_state.XMM[cpu_reg].b[15], cpu_state.XMM[cpu_rm].b[15]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[16];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        src[8] = readmemb(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        src[9] = readmemb(easeg, cpu_state.eaaddr + 9); if (cpu_state.abrt) return 1;
        src[10] = readmemb(easeg, cpu_state.eaaddr + 10); if (cpu_state.abrt) return 1;
        src[11] = readmemb(easeg, cpu_state.eaaddr + 11); if (cpu_state.abrt) return 1;
        src[12] = readmemb(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        src[13] = readmemb(easeg, cpu_state.eaaddr + 13); if (cpu_state.abrt) return 1;
        src[14] = readmemb(easeg, cpu_state.eaaddr + 14); if (cpu_state.abrt) return 1;
        src[15] = readmemb(easeg, cpu_state.eaaddr + 15); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].b[0] = min(cpu_state.XMM[cpu_reg].b[0], src[0]);
        cpu_state.XMM[cpu_reg].b[1] = min(cpu_state.XMM[cpu_reg].b[1], src[1]);
        cpu_state.XMM[cpu_reg].b[2] = min(cpu_state.XMM[cpu_reg].b[2], src[2]);
        cpu_state.XMM[cpu_reg].b[3] = min(cpu_state.XMM[cpu_reg].b[3], src[3]);
        cpu_state.XMM[cpu_reg].b[4] = min(cpu_state.XMM[cpu_reg].b[4], src[4]);
        cpu_state.XMM[cpu_reg].b[5] = min(cpu_state.XMM[cpu_reg].b[5], src[5]);
        cpu_state.XMM[cpu_reg].b[6] = min(cpu_state.XMM[cpu_reg].b[6], src[6]);
        cpu_state.XMM[cpu_reg].b[7] = min(cpu_state.XMM[cpu_reg].b[7], src[7]);
        cpu_state.XMM[cpu_reg].b[8] = min(cpu_state.XMM[cpu_reg].b[8], src[8]);
        cpu_state.XMM[cpu_reg].b[9] = min(cpu_state.XMM[cpu_reg].b[9], src[9]);
        cpu_state.XMM[cpu_reg].b[10] = min(cpu_state.XMM[cpu_reg].b[10], src[10]);
        cpu_state.XMM[cpu_reg].b[11] = min(cpu_state.XMM[cpu_reg].b[11], src[11]);
        cpu_state.XMM[cpu_reg].b[12] = min(cpu_state.XMM[cpu_reg].b[12], src[12]);
        cpu_state.XMM[cpu_reg].b[13] = min(cpu_state.XMM[cpu_reg].b[13], src[13]);
        cpu_state.XMM[cpu_reg].b[14] = min(cpu_state.XMM[cpu_reg].b[14], src[14]);
        cpu_state.XMM[cpu_reg].b[15] = min(cpu_state.XMM[cpu_reg].b[15], src[15]);
    return 0;
}

static int opPMAXUB_mm_mm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].b[0] = max(cpu_state.MM[cpu_reg].b[0], cpu_state.MM[cpu_rm].b[0]);
        cpu_state.MM[cpu_reg].b[1] = max(cpu_state.MM[cpu_reg].b[1], cpu_state.MM[cpu_rm].b[1]);
        cpu_state.MM[cpu_reg].b[2] = max(cpu_state.MM[cpu_reg].b[2], cpu_state.MM[cpu_rm].b[2]);
        cpu_state.MM[cpu_reg].b[3] = max(cpu_state.MM[cpu_reg].b[3], cpu_state.MM[cpu_rm].b[3]);
        cpu_state.MM[cpu_reg].b[4] = max(cpu_state.MM[cpu_reg].b[4], cpu_state.MM[cpu_rm].b[4]);
        cpu_state.MM[cpu_reg].b[5] = max(cpu_state.MM[cpu_reg].b[5], cpu_state.MM[cpu_rm].b[5]);
        cpu_state.MM[cpu_reg].b[6] = max(cpu_state.MM[cpu_reg].b[6], cpu_state.MM[cpu_rm].b[6]);
        cpu_state.MM[cpu_reg].b[7] = max(cpu_state.MM[cpu_reg].b[7], cpu_state.MM[cpu_rm].b[7]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[8];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].b[0] = max(cpu_state.MM[cpu_reg].b[0], src[0]);
        cpu_state.MM[cpu_reg].b[1] = max(cpu_state.MM[cpu_reg].b[1], src[1]);
        cpu_state.MM[cpu_reg].b[2] = max(cpu_state.MM[cpu_reg].b[2], src[2]);
        cpu_state.MM[cpu_reg].b[3] = max(cpu_state.MM[cpu_reg].b[3], src[3]);
        cpu_state.MM[cpu_reg].b[4] = max(cpu_state.MM[cpu_reg].b[4], src[4]);
        cpu_state.MM[cpu_reg].b[5] = max(cpu_state.MM[cpu_reg].b[5], src[5]);
        cpu_state.MM[cpu_reg].b[6] = max(cpu_state.MM[cpu_reg].b[6], src[6]);
        cpu_state.MM[cpu_reg].b[7] = max(cpu_state.MM[cpu_reg].b[7], src[7]);
    return 0;
}

static int opPMAXUB_mm_mm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].b[0] = max(cpu_state.MM[cpu_reg].b[0], cpu_state.MM[cpu_rm].b[0]);
        cpu_state.MM[cpu_reg].b[1] = max(cpu_state.MM[cpu_reg].b[1], cpu_state.MM[cpu_rm].b[1]);
        cpu_state.MM[cpu_reg].b[2] = max(cpu_state.MM[cpu_reg].b[2], cpu_state.MM[cpu_rm].b[2]);
        cpu_state.MM[cpu_reg].b[3] = max(cpu_state.MM[cpu_reg].b[3], cpu_state.MM[cpu_rm].b[3]);
        cpu_state.MM[cpu_reg].b[4] = max(cpu_state.MM[cpu_reg].b[4], cpu_state.MM[cpu_rm].b[4]);
        cpu_state.MM[cpu_reg].b[5] = max(cpu_state.MM[cpu_reg].b[5], cpu_state.MM[cpu_rm].b[5]);
        cpu_state.MM[cpu_reg].b[6] = max(cpu_state.MM[cpu_reg].b[6], cpu_state.MM[cpu_rm].b[6]);
        cpu_state.MM[cpu_reg].b[7] = max(cpu_state.MM[cpu_reg].b[7], cpu_state.MM[cpu_rm].b[7]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[8];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].b[0] = max(cpu_state.MM[cpu_reg].b[0], src[0]);
        cpu_state.MM[cpu_reg].b[1] = max(cpu_state.MM[cpu_reg].b[1], src[1]);
        cpu_state.MM[cpu_reg].b[2] = max(cpu_state.MM[cpu_reg].b[2], src[2]);
        cpu_state.MM[cpu_reg].b[3] = max(cpu_state.MM[cpu_reg].b[3], src[3]);
        cpu_state.MM[cpu_reg].b[4] = max(cpu_state.MM[cpu_reg].b[4], src[4]);
        cpu_state.MM[cpu_reg].b[5] = max(cpu_state.MM[cpu_reg].b[5], src[5]);
        cpu_state.MM[cpu_reg].b[6] = max(cpu_state.MM[cpu_reg].b[6], src[6]);
        cpu_state.MM[cpu_reg].b[7] = max(cpu_state.MM[cpu_reg].b[7], src[7]);
    return 0;
}

static int opPMAXUB_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].b[0] = max(cpu_state.XMM[cpu_reg].b[0], cpu_state.XMM[cpu_rm].b[0]);
        cpu_state.XMM[cpu_reg].b[1] = max(cpu_state.XMM[cpu_reg].b[1], cpu_state.XMM[cpu_rm].b[1]);
        cpu_state.XMM[cpu_reg].b[2] = max(cpu_state.XMM[cpu_reg].b[2], cpu_state.XMM[cpu_rm].b[2]);
        cpu_state.XMM[cpu_reg].b[3] = max(cpu_state.XMM[cpu_reg].b[3], cpu_state.XMM[cpu_rm].b[3]);
        cpu_state.XMM[cpu_reg].b[4] = max(cpu_state.XMM[cpu_reg].b[4], cpu_state.XMM[cpu_rm].b[4]);
        cpu_state.XMM[cpu_reg].b[5] = max(cpu_state.XMM[cpu_reg].b[5], cpu_state.XMM[cpu_rm].b[5]);
        cpu_state.XMM[cpu_reg].b[6] = max(cpu_state.XMM[cpu_reg].b[6], cpu_state.XMM[cpu_rm].b[6]);
        cpu_state.XMM[cpu_reg].b[7] = max(cpu_state.XMM[cpu_reg].b[7], cpu_state.XMM[cpu_rm].b[7]);
        cpu_state.XMM[cpu_reg].b[8] = max(cpu_state.XMM[cpu_reg].b[8], cpu_state.XMM[cpu_rm].b[8]);
        cpu_state.XMM[cpu_reg].b[9] = max(cpu_state.XMM[cpu_reg].b[9], cpu_state.XMM[cpu_rm].b[9]);
        cpu_state.XMM[cpu_reg].b[10] = max(cpu_state.XMM[cpu_reg].b[10], cpu_state.XMM[cpu_rm].b[10]);
        cpu_state.XMM[cpu_reg].b[11] = max(cpu_state.XMM[cpu_reg].b[11], cpu_state.XMM[cpu_rm].b[11]);
        cpu_state.XMM[cpu_reg].b[12] = max(cpu_state.XMM[cpu_reg].b[12], cpu_state.XMM[cpu_rm].b[12]);
        cpu_state.XMM[cpu_reg].b[13] = max(cpu_state.XMM[cpu_reg].b[13], cpu_state.XMM[cpu_rm].b[13]);
        cpu_state.XMM[cpu_reg].b[14] = max(cpu_state.XMM[cpu_reg].b[14], cpu_state.XMM[cpu_rm].b[14]);
        cpu_state.XMM[cpu_reg].b[15] = max(cpu_state.XMM[cpu_reg].b[15], cpu_state.XMM[cpu_rm].b[15]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[16];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        src[8] = readmemb(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        src[9] = readmemb(easeg, cpu_state.eaaddr + 9); if (cpu_state.abrt) return 1;
        src[10] = readmemb(easeg, cpu_state.eaaddr + 10); if (cpu_state.abrt) return 1;
        src[11] = readmemb(easeg, cpu_state.eaaddr + 11); if (cpu_state.abrt) return 1;
        src[12] = readmemb(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        src[13] = readmemb(easeg, cpu_state.eaaddr + 13); if (cpu_state.abrt) return 1;
        src[14] = readmemb(easeg, cpu_state.eaaddr + 14); if (cpu_state.abrt) return 1;
        src[15] = readmemb(easeg, cpu_state.eaaddr + 15); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].b[0] = max(cpu_state.XMM[cpu_reg].b[0], src[0]);
        cpu_state.XMM[cpu_reg].b[1] = max(cpu_state.XMM[cpu_reg].b[1], src[1]);
        cpu_state.XMM[cpu_reg].b[2] = max(cpu_state.XMM[cpu_reg].b[2], src[2]);
        cpu_state.XMM[cpu_reg].b[3] = max(cpu_state.XMM[cpu_reg].b[3], src[3]);
        cpu_state.XMM[cpu_reg].b[4] = max(cpu_state.XMM[cpu_reg].b[4], src[4]);
        cpu_state.XMM[cpu_reg].b[5] = max(cpu_state.XMM[cpu_reg].b[5], src[5]);
        cpu_state.XMM[cpu_reg].b[6] = max(cpu_state.XMM[cpu_reg].b[6], src[6]);
        cpu_state.XMM[cpu_reg].b[7] = max(cpu_state.XMM[cpu_reg].b[7], src[7]);
        cpu_state.XMM[cpu_reg].b[8] = max(cpu_state.XMM[cpu_reg].b[8], src[8]);
        cpu_state.XMM[cpu_reg].b[9] = max(cpu_state.XMM[cpu_reg].b[9], src[9]);
        cpu_state.XMM[cpu_reg].b[10] = max(cpu_state.XMM[cpu_reg].b[10], src[10]);
        cpu_state.XMM[cpu_reg].b[11] = max(cpu_state.XMM[cpu_reg].b[11], src[11]);
        cpu_state.XMM[cpu_reg].b[12] = max(cpu_state.XMM[cpu_reg].b[12], src[12]);
        cpu_state.XMM[cpu_reg].b[13] = max(cpu_state.XMM[cpu_reg].b[13], src[13]);
        cpu_state.XMM[cpu_reg].b[14] = max(cpu_state.XMM[cpu_reg].b[14], src[14]);
        cpu_state.XMM[cpu_reg].b[15] = max(cpu_state.XMM[cpu_reg].b[15], src[15]);
    return 0;
}

static int opPMAXUB_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].b[0] = max(cpu_state.XMM[cpu_reg].b[0], cpu_state.XMM[cpu_rm].b[0]);
        cpu_state.XMM[cpu_reg].b[1] = max(cpu_state.XMM[cpu_reg].b[1], cpu_state.XMM[cpu_rm].b[1]);
        cpu_state.XMM[cpu_reg].b[2] = max(cpu_state.XMM[cpu_reg].b[2], cpu_state.XMM[cpu_rm].b[2]);
        cpu_state.XMM[cpu_reg].b[3] = max(cpu_state.XMM[cpu_reg].b[3], cpu_state.XMM[cpu_rm].b[3]);
        cpu_state.XMM[cpu_reg].b[4] = max(cpu_state.XMM[cpu_reg].b[4], cpu_state.XMM[cpu_rm].b[4]);
        cpu_state.XMM[cpu_reg].b[5] = max(cpu_state.XMM[cpu_reg].b[5], cpu_state.XMM[cpu_rm].b[5]);
        cpu_state.XMM[cpu_reg].b[6] = max(cpu_state.XMM[cpu_reg].b[6], cpu_state.XMM[cpu_rm].b[6]);
        cpu_state.XMM[cpu_reg].b[7] = max(cpu_state.XMM[cpu_reg].b[7], cpu_state.XMM[cpu_rm].b[7]);
        cpu_state.XMM[cpu_reg].b[8] = max(cpu_state.XMM[cpu_reg].b[8], cpu_state.XMM[cpu_rm].b[8]);
        cpu_state.XMM[cpu_reg].b[9] = max(cpu_state.XMM[cpu_reg].b[9], cpu_state.XMM[cpu_rm].b[9]);
        cpu_state.XMM[cpu_reg].b[10] = max(cpu_state.XMM[cpu_reg].b[10], cpu_state.XMM[cpu_rm].b[10]);
        cpu_state.XMM[cpu_reg].b[11] = max(cpu_state.XMM[cpu_reg].b[11], cpu_state.XMM[cpu_rm].b[11]);
        cpu_state.XMM[cpu_reg].b[12] = max(cpu_state.XMM[cpu_reg].b[12], cpu_state.XMM[cpu_rm].b[12]);
        cpu_state.XMM[cpu_reg].b[13] = max(cpu_state.XMM[cpu_reg].b[13], cpu_state.XMM[cpu_rm].b[13]);
        cpu_state.XMM[cpu_reg].b[14] = max(cpu_state.XMM[cpu_reg].b[14], cpu_state.XMM[cpu_rm].b[14]);
        cpu_state.XMM[cpu_reg].b[15] = max(cpu_state.XMM[cpu_reg].b[15], cpu_state.XMM[cpu_rm].b[15]);
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[16];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        src[8] = readmemb(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        src[9] = readmemb(easeg, cpu_state.eaaddr + 9); if (cpu_state.abrt) return 1;
        src[10] = readmemb(easeg, cpu_state.eaaddr + 10); if (cpu_state.abrt) return 1;
        src[11] = readmemb(easeg, cpu_state.eaaddr + 11); if (cpu_state.abrt) return 1;
        src[12] = readmemb(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        src[13] = readmemb(easeg, cpu_state.eaaddr + 13); if (cpu_state.abrt) return 1;
        src[14] = readmemb(easeg, cpu_state.eaaddr + 14); if (cpu_state.abrt) return 1;
        src[15] = readmemb(easeg, cpu_state.eaaddr + 15); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].b[0] = max(cpu_state.XMM[cpu_reg].b[0], src[0]);
        cpu_state.XMM[cpu_reg].b[1] = max(cpu_state.XMM[cpu_reg].b[1], src[1]);
        cpu_state.XMM[cpu_reg].b[2] = max(cpu_state.XMM[cpu_reg].b[2], src[2]);
        cpu_state.XMM[cpu_reg].b[3] = max(cpu_state.XMM[cpu_reg].b[3], src[3]);
        cpu_state.XMM[cpu_reg].b[4] = max(cpu_state.XMM[cpu_reg].b[4], src[4]);
        cpu_state.XMM[cpu_reg].b[5] = max(cpu_state.XMM[cpu_reg].b[5], src[5]);
        cpu_state.XMM[cpu_reg].b[6] = max(cpu_state.XMM[cpu_reg].b[6], src[6]);
        cpu_state.XMM[cpu_reg].b[7] = max(cpu_state.XMM[cpu_reg].b[7], src[7]);
        cpu_state.XMM[cpu_reg].b[8] = max(cpu_state.XMM[cpu_reg].b[8], src[8]);
        cpu_state.XMM[cpu_reg].b[9] = max(cpu_state.XMM[cpu_reg].b[9], src[9]);
        cpu_state.XMM[cpu_reg].b[10] = max(cpu_state.XMM[cpu_reg].b[10], src[10]);
        cpu_state.XMM[cpu_reg].b[11] = max(cpu_state.XMM[cpu_reg].b[11], src[11]);
        cpu_state.XMM[cpu_reg].b[12] = max(cpu_state.XMM[cpu_reg].b[12], src[12]);
        cpu_state.XMM[cpu_reg].b[13] = max(cpu_state.XMM[cpu_reg].b[13], src[13]);
        cpu_state.XMM[cpu_reg].b[14] = max(cpu_state.XMM[cpu_reg].b[14], src[14]);
        cpu_state.XMM[cpu_reg].b[15] = max(cpu_state.XMM[cpu_reg].b[15], src[15]);
    return 0;
}

static int opPAVGB_mm_mm_a16(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].b[0] = (cpu_state.MM[cpu_reg].b[0] + cpu_state.MM[cpu_rm].b[0] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[1] = (cpu_state.MM[cpu_reg].b[1] + cpu_state.MM[cpu_rm].b[1] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[2] = (cpu_state.MM[cpu_reg].b[2] + cpu_state.MM[cpu_rm].b[2] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[3] = (cpu_state.MM[cpu_reg].b[3] + cpu_state.MM[cpu_rm].b[3] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[4] = (cpu_state.MM[cpu_reg].b[4] + cpu_state.MM[cpu_rm].b[4] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[5] = (cpu_state.MM[cpu_reg].b[5] + cpu_state.MM[cpu_rm].b[5] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[6] = (cpu_state.MM[cpu_reg].b[6] + cpu_state.MM[cpu_rm].b[6] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[7] = (cpu_state.MM[cpu_reg].b[7] + cpu_state.MM[cpu_rm].b[7] + 1) >> 1;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[8];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].b[0] = (cpu_state.MM[cpu_reg].b[0] + src[0] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[1] = (cpu_state.MM[cpu_reg].b[1] + src[1] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[2] = (cpu_state.MM[cpu_reg].b[2] + src[2] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[3] = (cpu_state.MM[cpu_reg].b[3] + src[3] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[4] = (cpu_state.MM[cpu_reg].b[4] + src[4] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[5] = (cpu_state.MM[cpu_reg].b[5] + src[5] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[6] = (cpu_state.MM[cpu_reg].b[6] + src[6] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[7] = (cpu_state.MM[cpu_reg].b[7] + src[7] + 1) >> 1;
    return 0;
}

static int opPAVGB_mm_mm_a32(uint32_t fetchdat)
{
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].b[0] = (cpu_state.MM[cpu_reg].b[0] + cpu_state.MM[cpu_rm].b[0] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[1] = (cpu_state.MM[cpu_reg].b[1] + cpu_state.MM[cpu_rm].b[1] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[2] = (cpu_state.MM[cpu_reg].b[2] + cpu_state.MM[cpu_rm].b[2] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[3] = (cpu_state.MM[cpu_reg].b[3] + cpu_state.MM[cpu_rm].b[3] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[4] = (cpu_state.MM[cpu_reg].b[4] + cpu_state.MM[cpu_rm].b[4] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[5] = (cpu_state.MM[cpu_reg].b[5] + cpu_state.MM[cpu_rm].b[5] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[6] = (cpu_state.MM[cpu_reg].b[6] + cpu_state.MM[cpu_rm].b[6] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[7] = (cpu_state.MM[cpu_reg].b[7] + cpu_state.MM[cpu_rm].b[7] + 1) >> 1;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[8];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].b[0] = (cpu_state.MM[cpu_reg].b[0] + src[0] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[1] = (cpu_state.MM[cpu_reg].b[1] + src[1] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[2] = (cpu_state.MM[cpu_reg].b[2] + src[2] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[3] = (cpu_state.MM[cpu_reg].b[3] + src[3] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[4] = (cpu_state.MM[cpu_reg].b[4] + src[4] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[5] = (cpu_state.MM[cpu_reg].b[5] + src[5] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[6] = (cpu_state.MM[cpu_reg].b[6] + src[6] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[7] = (cpu_state.MM[cpu_reg].b[7] + src[7] + 1) >> 1;
    return 0;
}

static int opPAVGB_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].b[0] = (cpu_state.XMM[cpu_reg].b[0] + cpu_state.XMM[cpu_rm].b[0] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[1] = (cpu_state.XMM[cpu_reg].b[1] + cpu_state.XMM[cpu_rm].b[1] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[2] = (cpu_state.XMM[cpu_reg].b[2] + cpu_state.XMM[cpu_rm].b[2] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[3] = (cpu_state.XMM[cpu_reg].b[3] + cpu_state.XMM[cpu_rm].b[3] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[4] = (cpu_state.XMM[cpu_reg].b[4] + cpu_state.XMM[cpu_rm].b[4] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[5] = (cpu_state.XMM[cpu_reg].b[5] + cpu_state.XMM[cpu_rm].b[5] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[6] = (cpu_state.XMM[cpu_reg].b[6] + cpu_state.XMM[cpu_rm].b[6] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[7] = (cpu_state.XMM[cpu_reg].b[7] + cpu_state.XMM[cpu_rm].b[7] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[8] = (cpu_state.XMM[cpu_reg].b[8] + cpu_state.XMM[cpu_rm].b[8] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[9] = (cpu_state.XMM[cpu_reg].b[9] + cpu_state.XMM[cpu_rm].b[9] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[10] = (cpu_state.XMM[cpu_reg].b[10] + cpu_state.XMM[cpu_rm].b[10] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[11] = (cpu_state.XMM[cpu_reg].b[11] + cpu_state.XMM[cpu_rm].b[11] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[12] = (cpu_state.XMM[cpu_reg].b[12] + cpu_state.XMM[cpu_rm].b[12] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[13] = (cpu_state.XMM[cpu_reg].b[13] + cpu_state.XMM[cpu_rm].b[13] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[14] = (cpu_state.XMM[cpu_reg].b[14] + cpu_state.XMM[cpu_rm].b[14] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[15] = (cpu_state.XMM[cpu_reg].b[15] + cpu_state.XMM[cpu_rm].b[15] + 1) >> 1;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[16];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        src[8] = readmemb(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        src[9] = readmemb(easeg, cpu_state.eaaddr + 9); if (cpu_state.abrt) return 1;
        src[10] = readmemb(easeg, cpu_state.eaaddr + 10); if (cpu_state.abrt) return 1;
        src[11] = readmemb(easeg, cpu_state.eaaddr + 11); if (cpu_state.abrt) return 1;
        src[12] = readmemb(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        src[13] = readmemb(easeg, cpu_state.eaaddr + 13); if (cpu_state.abrt) return 1;
        src[14] = readmemb(easeg, cpu_state.eaaddr + 14); if (cpu_state.abrt) return 1;
        src[15] = readmemb(easeg, cpu_state.eaaddr + 15); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].b[0] = (cpu_state.MM[cpu_reg].b[0] + src[0] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[1] = (cpu_state.MM[cpu_reg].b[1] + src[1] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[2] = (cpu_state.MM[cpu_reg].b[2] + src[2] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[3] = (cpu_state.MM[cpu_reg].b[3] + src[3] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[4] = (cpu_state.MM[cpu_reg].b[4] + src[4] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[5] = (cpu_state.MM[cpu_reg].b[5] + src[5] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[6] = (cpu_state.MM[cpu_reg].b[6] + src[6] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[7] = (cpu_state.MM[cpu_reg].b[7] + src[7] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[8] = (cpu_state.MM[cpu_reg].b[8] + src[8] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[9] = (cpu_state.MM[cpu_reg].b[9] + src[9] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[10] = (cpu_state.MM[cpu_reg].b[10] + src[10] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[11] = (cpu_state.MM[cpu_reg].b[11] + src[11] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[12] = (cpu_state.MM[cpu_reg].b[12] + src[12] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[13] = (cpu_state.MM[cpu_reg].b[13] + src[13] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[14] = (cpu_state.MM[cpu_reg].b[14] + src[14] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[15] = (cpu_state.MM[cpu_reg].b[15] + src[15] + 1) >> 1;
    return 0;
}

static int opPAVGB_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].b[0] = (cpu_state.XMM[cpu_reg].b[0] + cpu_state.XMM[cpu_rm].b[0] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[1] = (cpu_state.XMM[cpu_reg].b[1] + cpu_state.XMM[cpu_rm].b[1] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[2] = (cpu_state.XMM[cpu_reg].b[2] + cpu_state.XMM[cpu_rm].b[2] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[3] = (cpu_state.XMM[cpu_reg].b[3] + cpu_state.XMM[cpu_rm].b[3] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[4] = (cpu_state.XMM[cpu_reg].b[4] + cpu_state.XMM[cpu_rm].b[4] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[5] = (cpu_state.XMM[cpu_reg].b[5] + cpu_state.XMM[cpu_rm].b[5] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[6] = (cpu_state.XMM[cpu_reg].b[6] + cpu_state.XMM[cpu_rm].b[6] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[7] = (cpu_state.XMM[cpu_reg].b[7] + cpu_state.XMM[cpu_rm].b[7] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[8] = (cpu_state.XMM[cpu_reg].b[8] + cpu_state.XMM[cpu_rm].b[8] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[9] = (cpu_state.XMM[cpu_reg].b[9] + cpu_state.XMM[cpu_rm].b[9] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[10] = (cpu_state.XMM[cpu_reg].b[10] + cpu_state.XMM[cpu_rm].b[10] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[11] = (cpu_state.XMM[cpu_reg].b[11] + cpu_state.XMM[cpu_rm].b[11] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[12] = (cpu_state.XMM[cpu_reg].b[12] + cpu_state.XMM[cpu_rm].b[12] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[13] = (cpu_state.XMM[cpu_reg].b[13] + cpu_state.XMM[cpu_rm].b[13] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[14] = (cpu_state.XMM[cpu_reg].b[14] + cpu_state.XMM[cpu_rm].b[14] + 1) >> 1;
        cpu_state.XMM[cpu_reg].b[15] = (cpu_state.XMM[cpu_reg].b[15] + cpu_state.XMM[cpu_rm].b[15] + 1) >> 1;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint8_t src[16];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemb(easeg, cpu_state.eaaddr + 1); if (cpu_state.abrt) return 1;
        src[2] = readmemb(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[3] = readmemb(easeg, cpu_state.eaaddr + 3); if (cpu_state.abrt) return 1;
        src[4] = readmemb(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[5] = readmemb(easeg, cpu_state.eaaddr + 5); if (cpu_state.abrt) return 1;
        src[6] = readmemb(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[7] = readmemb(easeg, cpu_state.eaaddr + 7); if (cpu_state.abrt) return 1;
        src[8] = readmemb(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        src[9] = readmemb(easeg, cpu_state.eaaddr + 9); if (cpu_state.abrt) return 1;
        src[10] = readmemb(easeg, cpu_state.eaaddr + 10); if (cpu_state.abrt) return 1;
        src[11] = readmemb(easeg, cpu_state.eaaddr + 11); if (cpu_state.abrt) return 1;
        src[12] = readmemb(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        src[13] = readmemb(easeg, cpu_state.eaaddr + 13); if (cpu_state.abrt) return 1;
        src[14] = readmemb(easeg, cpu_state.eaaddr + 14); if (cpu_state.abrt) return 1;
        src[15] = readmemb(easeg, cpu_state.eaaddr + 15); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].b[0] = (cpu_state.MM[cpu_reg].b[0] + src[0] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[1] = (cpu_state.MM[cpu_reg].b[1] + src[1] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[2] = (cpu_state.MM[cpu_reg].b[2] + src[2] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[3] = (cpu_state.MM[cpu_reg].b[3] + src[3] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[4] = (cpu_state.MM[cpu_reg].b[4] + src[4] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[5] = (cpu_state.MM[cpu_reg].b[5] + src[5] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[6] = (cpu_state.MM[cpu_reg].b[6] + src[6] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[7] = (cpu_state.MM[cpu_reg].b[7] + src[7] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[8] = (cpu_state.MM[cpu_reg].b[8] + src[8] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[9] = (cpu_state.MM[cpu_reg].b[9] + src[9] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[10] = (cpu_state.MM[cpu_reg].b[10] + src[10] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[11] = (cpu_state.MM[cpu_reg].b[11] + src[11] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[12] = (cpu_state.MM[cpu_reg].b[12] + src[12] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[13] = (cpu_state.MM[cpu_reg].b[13] + src[13] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[14] = (cpu_state.MM[cpu_reg].b[14] + src[14] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[15] = (cpu_state.MM[cpu_reg].b[15] + src[15] + 1) >> 1;
    return 0;
}

static int opPAVGW_mm_mm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].w[0] = (cpu_state.MM[cpu_reg].w[0] + cpu_state.MM[cpu_rm].w[0] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[1] = (cpu_state.MM[cpu_reg].w[1] + cpu_state.MM[cpu_rm].w[1] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[2] = (cpu_state.MM[cpu_reg].w[2] + cpu_state.MM[cpu_rm].w[2] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[3] = (cpu_state.MM[cpu_reg].w[3] + cpu_state.MM[cpu_rm].w[3] + 1) >> 1;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint16_t src[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemw(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemw(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[2] = readmemw(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[3] = readmemw(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].w[0] = (cpu_state.MM[cpu_reg].w[0] + src[0] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[1] = (cpu_state.MM[cpu_reg].w[1] + src[1] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[2] = (cpu_state.MM[cpu_reg].w[2] + src[2] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[3] = (cpu_state.MM[cpu_reg].w[3] + src[3] + 1) >> 1;
    return 0;
}

static int opPAVGW_mm_mm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].w[0] = (cpu_state.MM[cpu_reg].w[0] + cpu_state.MM[cpu_rm].w[0] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[1] = (cpu_state.MM[cpu_reg].w[1] + cpu_state.MM[cpu_rm].w[1] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[2] = (cpu_state.MM[cpu_reg].w[2] + cpu_state.MM[cpu_rm].w[2] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[3] = (cpu_state.MM[cpu_reg].w[3] + cpu_state.MM[cpu_rm].w[3] + 1) >> 1;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint16_t src[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemw(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemw(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[2] = readmemw(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[3] = readmemw(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].w[0] = (cpu_state.MM[cpu_reg].w[0] + src[0] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[1] = (cpu_state.MM[cpu_reg].w[1] + src[1] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[2] = (cpu_state.MM[cpu_reg].w[2] + src[2] + 1) >> 1;
        cpu_state.MM[cpu_reg].w[3] = (cpu_state.MM[cpu_reg].w[3] + src[3] + 1) >> 1;
    return 0;
}

static int opPAVGW_xmm_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].w[0] = (cpu_state.XMM[cpu_reg].w[0] + cpu_state.XMM[cpu_rm].w[0] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[1] = (cpu_state.XMM[cpu_reg].w[1] + cpu_state.XMM[cpu_rm].w[1] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[2] = (cpu_state.XMM[cpu_reg].w[2] + cpu_state.XMM[cpu_rm].w[2] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[3] = (cpu_state.XMM[cpu_reg].w[3] + cpu_state.XMM[cpu_rm].w[3] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[4] = (cpu_state.XMM[cpu_reg].w[4] + cpu_state.XMM[cpu_rm].w[4] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[5] = (cpu_state.XMM[cpu_reg].w[5] + cpu_state.XMM[cpu_rm].w[5] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[6] = (cpu_state.XMM[cpu_reg].w[6] + cpu_state.XMM[cpu_rm].w[6] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[7] = (cpu_state.XMM[cpu_reg].w[7] + cpu_state.XMM[cpu_rm].w[7] + 1) >> 1;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint16_t src[8];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemw(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemw(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[2] = readmemw(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[3] = readmemw(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[4] = readmemw(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        src[5] = readmemw(easeg, cpu_state.eaaddr + 10); if (cpu_state.abrt) return 1;
        src[6] = readmemw(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        src[7] = readmemw(easeg, cpu_state.eaaddr + 14); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].w[0] = (cpu_state.XMM[cpu_reg].w[0] + src[0] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[1] = (cpu_state.XMM[cpu_reg].w[1] + src[1] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[2] = (cpu_state.XMM[cpu_reg].w[2] + src[2] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[3] = (cpu_state.XMM[cpu_reg].w[3] + src[3] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[4] = (cpu_state.XMM[cpu_reg].w[4] + src[4] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[5] = (cpu_state.XMM[cpu_reg].w[5] + src[5] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[6] = (cpu_state.XMM[cpu_reg].w[6] + src[6] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[7] = (cpu_state.XMM[cpu_reg].w[7] + src[7] + 1) >> 1;
    return 0;
}

static int opPAVGW_xmm_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.XMM[cpu_reg].w[0] = (cpu_state.XMM[cpu_reg].w[0] + cpu_state.XMM[cpu_rm].w[0] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[1] = (cpu_state.XMM[cpu_reg].w[1] + cpu_state.XMM[cpu_rm].w[1] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[2] = (cpu_state.XMM[cpu_reg].w[2] + cpu_state.XMM[cpu_rm].w[2] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[3] = (cpu_state.XMM[cpu_reg].w[3] + cpu_state.XMM[cpu_rm].w[3] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[4] = (cpu_state.XMM[cpu_reg].w[4] + cpu_state.XMM[cpu_rm].w[4] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[5] = (cpu_state.XMM[cpu_reg].w[5] + cpu_state.XMM[cpu_rm].w[5] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[6] = (cpu_state.XMM[cpu_reg].w[6] + cpu_state.XMM[cpu_rm].w[6] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[7] = (cpu_state.XMM[cpu_reg].w[7] + cpu_state.XMM[cpu_rm].w[7] + 1) >> 1;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint16_t src[8];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemw(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemw(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[2] = readmemw(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[3] = readmemw(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        src[4] = readmemw(easeg, cpu_state.eaaddr + 8); if (cpu_state.abrt) return 1;
        src[5] = readmemw(easeg, cpu_state.eaaddr + 10); if (cpu_state.abrt) return 1;
        src[6] = readmemw(easeg, cpu_state.eaaddr + 12); if (cpu_state.abrt) return 1;
        src[7] = readmemw(easeg, cpu_state.eaaddr + 14); if (cpu_state.abrt) return 1;
        cpu_state.XMM[cpu_reg].w[0] = (cpu_state.XMM[cpu_reg].w[0] + src[0] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[1] = (cpu_state.XMM[cpu_reg].w[1] + src[1] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[2] = (cpu_state.XMM[cpu_reg].w[2] + src[2] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[3] = (cpu_state.XMM[cpu_reg].w[3] + src[3] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[4] = (cpu_state.XMM[cpu_reg].w[4] + src[4] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[5] = (cpu_state.XMM[cpu_reg].w[5] + src[5] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[6] = (cpu_state.XMM[cpu_reg].w[6] + src[6] + 1) >> 1;
        cpu_state.XMM[cpu_reg].w[7] = (cpu_state.XMM[cpu_reg].w[7] + src[7] + 1) >> 1;
    return 0;
}

static int opPMULHUW_mm_mm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].w[0] = ((uint32_t)cpu_state.MM[cpu_reg].w[0] * (uint32_t)cpu_state.MM[cpu_rm].w[0]) >> 16;
        cpu_state.MM[cpu_reg].w[1] = ((uint32_t)cpu_state.MM[cpu_reg].w[1] * (uint32_t)cpu_state.MM[cpu_rm].w[1]) >> 16;
        cpu_state.MM[cpu_reg].w[2] = ((uint32_t)cpu_state.MM[cpu_reg].w[2] * (uint32_t)cpu_state.MM[cpu_rm].w[2]) >> 16;
        cpu_state.MM[cpu_reg].w[3] = ((uint32_t)cpu_state.MM[cpu_reg].w[3] * (uint32_t)cpu_state.MM[cpu_rm].w[3]) >> 16;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemw(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemw(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[2] = readmemw(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[3] = readmemw(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].w[0] = ((uint32_t)cpu_state.MM[cpu_reg].w[0] * src[0]) >> 16;
        cpu_state.MM[cpu_reg].w[1] = ((uint32_t)cpu_state.MM[cpu_reg].w[1] * src[1]) >> 16;
        cpu_state.MM[cpu_reg].w[2] = ((uint32_t)cpu_state.MM[cpu_reg].w[2] * src[2]) >> 16;
        cpu_state.MM[cpu_reg].w[3] = ((uint32_t)cpu_state.MM[cpu_reg].w[3] * src[3]) >> 16;
    return 0;
}

static int opPMULHUW_mm_mm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3)
    {
        cpu_state.MM[cpu_reg].w[0] = ((uint32_t)cpu_state.MM[cpu_reg].w[0] * (uint32_t)cpu_state.MM[cpu_rm].w[0]) >> 16;
        cpu_state.MM[cpu_reg].w[1] = ((uint32_t)cpu_state.MM[cpu_reg].w[1] * (uint32_t)cpu_state.MM[cpu_rm].w[1]) >> 16;
        cpu_state.MM[cpu_reg].w[2] = ((uint32_t)cpu_state.MM[cpu_reg].w[2] * (uint32_t)cpu_state.MM[cpu_rm].w[2]) >> 16;
        cpu_state.MM[cpu_reg].w[3] = ((uint32_t)cpu_state.MM[cpu_reg].w[3] * (uint32_t)cpu_state.MM[cpu_rm].w[3]) >> 16;
        CLOCK_CYCLES(1);
    }
    else
    {
        uint32_t src[4];
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src[0] = readmemw(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        src[1] = readmemw(easeg, cpu_state.eaaddr + 2); if (cpu_state.abrt) return 1;
        src[2] = readmemw(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 1;
        src[3] = readmemw(easeg, cpu_state.eaaddr + 6); if (cpu_state.abrt) return 1;
        cpu_state.MM[cpu_reg].w[0] = ((uint32_t)cpu_state.MM[cpu_reg].w[0] * src[0]) >> 16;
        cpu_state.MM[cpu_reg].w[1] = ((uint32_t)cpu_state.MM[cpu_reg].w[1] * src[1]) >> 16;
        cpu_state.MM[cpu_reg].w[2] = ((uint32_t)cpu_state.MM[cpu_reg].w[2] * src[2]) >> 16;
        cpu_state.MM[cpu_reg].w[3] = ((uint32_t)cpu_state.MM[cpu_reg].w[3] * src[3]) >> 16;
    return 0;
}
static int opPADDB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].b[0] += src.b[0];
        cpu_state.MM[cpu_reg].b[1] += src.b[1];
        cpu_state.MM[cpu_reg].b[2] += src.b[2];
        cpu_state.MM[cpu_reg].b[3] += src.b[3];
        cpu_state.MM[cpu_reg].b[4] += src.b[4];
        cpu_state.MM[cpu_reg].b[5] += src.b[5];
        cpu_state.MM[cpu_reg].b[6] += src.b[6];
        cpu_state.MM[cpu_reg].b[7] += src.b[7];

        return 0;
}
static int opPADDB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].b[0] += src.b[0];
        cpu_state.MM[cpu_reg].b[1] += src.b[1];
        cpu_state.MM[cpu_reg].b[2] += src.b[2];
        cpu_state.MM[cpu_reg].b[3] += src.b[3];
        cpu_state.MM[cpu_reg].b[4] += src.b[4];
        cpu_state.MM[cpu_reg].b[5] += src.b[5];
        cpu_state.MM[cpu_reg].b[6] += src.b[6];
        cpu_state.MM[cpu_reg].b[7] += src.b[7];

        return 0;
}

static int opPADDW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].w[0] += src.w[0];
        cpu_state.MM[cpu_reg].w[1] += src.w[1];
        cpu_state.MM[cpu_reg].w[2] += src.w[2];
        cpu_state.MM[cpu_reg].w[3] += src.w[3];

        return 0;
}
static int opPADDW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].w[0] += src.w[0];
        cpu_state.MM[cpu_reg].w[1] += src.w[1];
        cpu_state.MM[cpu_reg].w[2] += src.w[2];
        cpu_state.MM[cpu_reg].w[3] += src.w[3];

        return 0;
}

static int opPADDD_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].l[0] += src.l[0];
        cpu_state.MM[cpu_reg].l[1] += src.l[1];

        return 0;
}
static int opPADDD_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].l[0] += src.l[0];
        cpu_state.MM[cpu_reg].l[1] += src.l[1];

        return 0;
}

static int opPADDSB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].sb[0] = SSATB(cpu_state.MM[cpu_reg].sb[0] + src.sb[0]);
        cpu_state.MM[cpu_reg].sb[1] = SSATB(cpu_state.MM[cpu_reg].sb[1] + src.sb[1]);
        cpu_state.MM[cpu_reg].sb[2] = SSATB(cpu_state.MM[cpu_reg].sb[2] + src.sb[2]);
        cpu_state.MM[cpu_reg].sb[3] = SSATB(cpu_state.MM[cpu_reg].sb[3] + src.sb[3]);
        cpu_state.MM[cpu_reg].sb[4] = SSATB(cpu_state.MM[cpu_reg].sb[4] + src.sb[4]);
        cpu_state.MM[cpu_reg].sb[5] = SSATB(cpu_state.MM[cpu_reg].sb[5] + src.sb[5]);
        cpu_state.MM[cpu_reg].sb[6] = SSATB(cpu_state.MM[cpu_reg].sb[6] + src.sb[6]);
        cpu_state.MM[cpu_reg].sb[7] = SSATB(cpu_state.MM[cpu_reg].sb[7] + src.sb[7]);

        return 0;
}
static int opPADDSB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].sb[0] = SSATB(cpu_state.MM[cpu_reg].sb[0] + src.sb[0]);
        cpu_state.MM[cpu_reg].sb[1] = SSATB(cpu_state.MM[cpu_reg].sb[1] + src.sb[1]);
        cpu_state.MM[cpu_reg].sb[2] = SSATB(cpu_state.MM[cpu_reg].sb[2] + src.sb[2]);
        cpu_state.MM[cpu_reg].sb[3] = SSATB(cpu_state.MM[cpu_reg].sb[3] + src.sb[3]);
        cpu_state.MM[cpu_reg].sb[4] = SSATB(cpu_state.MM[cpu_reg].sb[4] + src.sb[4]);
        cpu_state.MM[cpu_reg].sb[5] = SSATB(cpu_state.MM[cpu_reg].sb[5] + src.sb[5]);
        cpu_state.MM[cpu_reg].sb[6] = SSATB(cpu_state.MM[cpu_reg].sb[6] + src.sb[6]);
        cpu_state.MM[cpu_reg].sb[7] = SSATB(cpu_state.MM[cpu_reg].sb[7] + src.sb[7]);

        return 0;
}

static int opPADDUSB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].b[0] = USATB(cpu_state.MM[cpu_reg].b[0] + src.b[0]);
        cpu_state.MM[cpu_reg].b[1] = USATB(cpu_state.MM[cpu_reg].b[1] + src.b[1]);
        cpu_state.MM[cpu_reg].b[2] = USATB(cpu_state.MM[cpu_reg].b[2] + src.b[2]);
        cpu_state.MM[cpu_reg].b[3] = USATB(cpu_state.MM[cpu_reg].b[3] + src.b[3]);
        cpu_state.MM[cpu_reg].b[4] = USATB(cpu_state.MM[cpu_reg].b[4] + src.b[4]);
        cpu_state.MM[cpu_reg].b[5] = USATB(cpu_state.MM[cpu_reg].b[5] + src.b[5]);
        cpu_state.MM[cpu_reg].b[6] = USATB(cpu_state.MM[cpu_reg].b[6] + src.b[6]);
        cpu_state.MM[cpu_reg].b[7] = USATB(cpu_state.MM[cpu_reg].b[7] + src.b[7]);

        return 0;
}
static int opPADDUSB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].b[0] = USATB(cpu_state.MM[cpu_reg].b[0] + src.b[0]);
        cpu_state.MM[cpu_reg].b[1] = USATB(cpu_state.MM[cpu_reg].b[1] + src.b[1]);
        cpu_state.MM[cpu_reg].b[2] = USATB(cpu_state.MM[cpu_reg].b[2] + src.b[2]);
        cpu_state.MM[cpu_reg].b[3] = USATB(cpu_state.MM[cpu_reg].b[3] + src.b[3]);
        cpu_state.MM[cpu_reg].b[4] = USATB(cpu_state.MM[cpu_reg].b[4] + src.b[4]);
        cpu_state.MM[cpu_reg].b[5] = USATB(cpu_state.MM[cpu_reg].b[5] + src.b[5]);
        cpu_state.MM[cpu_reg].b[6] = USATB(cpu_state.MM[cpu_reg].b[6] + src.b[6]);
        cpu_state.MM[cpu_reg].b[7] = USATB(cpu_state.MM[cpu_reg].b[7] + src.b[7]);

        return 0;
}

static int opPADDSW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].sw[0] = SSATW(cpu_state.MM[cpu_reg].sw[0] + src.sw[0]);
        cpu_state.MM[cpu_reg].sw[1] = SSATW(cpu_state.MM[cpu_reg].sw[1] + src.sw[1]);
        cpu_state.MM[cpu_reg].sw[2] = SSATW(cpu_state.MM[cpu_reg].sw[2] + src.sw[2]);
        cpu_state.MM[cpu_reg].sw[3] = SSATW(cpu_state.MM[cpu_reg].sw[3] + src.sw[3]);

        return 0;
}
static int opPADDSW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].sw[0] = SSATW(cpu_state.MM[cpu_reg].sw[0] + src.sw[0]);
        cpu_state.MM[cpu_reg].sw[1] = SSATW(cpu_state.MM[cpu_reg].sw[1] + src.sw[1]);
        cpu_state.MM[cpu_reg].sw[2] = SSATW(cpu_state.MM[cpu_reg].sw[2] + src.sw[2]);
        cpu_state.MM[cpu_reg].sw[3] = SSATW(cpu_state.MM[cpu_reg].sw[3] + src.sw[3]);

        return 0;
}

static int opPADDUSW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].w[0] = USATW(cpu_state.MM[cpu_reg].w[0] + src.w[0]);
        cpu_state.MM[cpu_reg].w[1] = USATW(cpu_state.MM[cpu_reg].w[1] + src.w[1]);
        cpu_state.MM[cpu_reg].w[2] = USATW(cpu_state.MM[cpu_reg].w[2] + src.w[2]);
        cpu_state.MM[cpu_reg].w[3] = USATW(cpu_state.MM[cpu_reg].w[3] + src.w[3]);

        return 0;
}
static int opPADDUSW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].w[0] = USATW(cpu_state.MM[cpu_reg].w[0] + src.w[0]);
        cpu_state.MM[cpu_reg].w[1] = USATW(cpu_state.MM[cpu_reg].w[1] + src.w[1]);
        cpu_state.MM[cpu_reg].w[2] = USATW(cpu_state.MM[cpu_reg].w[2] + src.w[2]);
        cpu_state.MM[cpu_reg].w[3] = USATW(cpu_state.MM[cpu_reg].w[3] + src.w[3]);

        return 0;
}

static int opPMADDWD_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        if (cpu_state.MM[cpu_reg].l[0] == 0x80008000 && src.l[0] == 0x80008000)
                cpu_state.MM[cpu_reg].l[0] = 0x80000000;
        else
                cpu_state.MM[cpu_reg].sl[0] = ((int32_t)cpu_state.MM[cpu_reg].sw[0] * (int32_t)src.sw[0]) + ((int32_t)cpu_state.MM[cpu_reg].sw[1] * (int32_t)src.sw[1]);

        if (cpu_state.MM[cpu_reg].l[1] == 0x80008000 && src.l[1] == 0x80008000)
                cpu_state.MM[cpu_reg].l[1] = 0x80000000;
        else
                cpu_state.MM[cpu_reg].sl[1] = ((int32_t)cpu_state.MM[cpu_reg].sw[2] * (int32_t)src.sw[2]) + ((int32_t)cpu_state.MM[cpu_reg].sw[3] * (int32_t)src.sw[3]);
        
        return 0;
}
static int opPMADDWD_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        if (cpu_state.MM[cpu_reg].l[0] == 0x80008000 && src.l[0] == 0x80008000)
                cpu_state.MM[cpu_reg].l[0] = 0x80000000;
        else
                cpu_state.MM[cpu_reg].sl[0] = ((int32_t)cpu_state.MM[cpu_reg].sw[0] * (int32_t)src.sw[0]) + ((int32_t)cpu_state.MM[cpu_reg].sw[1] * (int32_t)src.sw[1]);

        if (cpu_state.MM[cpu_reg].l[1] == 0x80008000 && src.l[1] == 0x80008000)
                cpu_state.MM[cpu_reg].l[1] = 0x80000000;
        else
                cpu_state.MM[cpu_reg].sl[1] = ((int32_t)cpu_state.MM[cpu_reg].sw[2] * (int32_t)src.sw[2]) + ((int32_t)cpu_state.MM[cpu_reg].sw[3] * (int32_t)src.sw[3]);
        
        return 0;
}


static int opPMULLW_a16(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                cpu_state.MM[cpu_reg].w[0] *= cpu_state.MM[cpu_rm].w[0];
                cpu_state.MM[cpu_reg].w[1] *= cpu_state.MM[cpu_rm].w[1];
                cpu_state.MM[cpu_reg].w[2] *= cpu_state.MM[cpu_rm].w[2];
                cpu_state.MM[cpu_reg].w[3] *= cpu_state.MM[cpu_rm].w[3];
                CLOCK_CYCLES(1);
        }
        else
        {
                MMX_REG src;

                SEG_CHECK_READ(cpu_state.ea_seg);
                src.l[0] = readmeml(easeg, cpu_state.eaaddr);
                src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 0;
                cpu_state.MM[cpu_reg].w[0] *= src.w[0];
                cpu_state.MM[cpu_reg].w[1] *= src.w[1];
                cpu_state.MM[cpu_reg].w[2] *= src.w[2];
                cpu_state.MM[cpu_reg].w[3] *= src.w[3];
                CLOCK_CYCLES(2);
        }
        return 0;
}
static int opPMULLW_a32(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                cpu_state.MM[cpu_reg].w[0] *= cpu_state.MM[cpu_rm].w[0];
                cpu_state.MM[cpu_reg].w[1] *= cpu_state.MM[cpu_rm].w[1];
                cpu_state.MM[cpu_reg].w[2] *= cpu_state.MM[cpu_rm].w[2];
                cpu_state.MM[cpu_reg].w[3] *= cpu_state.MM[cpu_rm].w[3];
                CLOCK_CYCLES(1);
        }
        else
        {
                MMX_REG src;
        
                SEG_CHECK_READ(cpu_state.ea_seg);
                src.l[0] = readmeml(easeg, cpu_state.eaaddr);
                src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 0;
                cpu_state.MM[cpu_reg].w[0] *= src.w[0];
                cpu_state.MM[cpu_reg].w[1] *= src.w[1];
                cpu_state.MM[cpu_reg].w[2] *= src.w[2];
                cpu_state.MM[cpu_reg].w[3] *= src.w[3];
                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opPMULHW_a16(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3)
        {
                cpu_state.MM[cpu_reg].w[0] = ((int32_t)cpu_state.MM[cpu_reg].sw[0] * (int32_t)cpu_state.MM[cpu_rm].sw[0]) >> 16;
                cpu_state.MM[cpu_reg].w[1] = ((int32_t)cpu_state.MM[cpu_reg].sw[1] * (int32_t)cpu_state.MM[cpu_rm].sw[1]) >> 16;
                cpu_state.MM[cpu_reg].w[2] = ((int32_t)cpu_state.MM[cpu_reg].sw[2] * (int32_t)cpu_state.MM[cpu_rm].sw[2]) >> 16;
                cpu_state.MM[cpu_reg].w[3] = ((int32_t)cpu_state.MM[cpu_reg].sw[3] * (int32_t)cpu_state.MM[cpu_rm].sw[3]) >> 16;
                CLOCK_CYCLES(1);
        }
        else
        {
                MMX_REG src;
        
                SEG_CHECK_READ(cpu_state.ea_seg);
                src.l[0] = readmeml(easeg, cpu_state.eaaddr);
                src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 0;
                cpu_state.MM[cpu_reg].w[0] = ((int32_t)cpu_state.MM[cpu_reg].sw[0] * (int32_t)src.sw[0]) >> 16;
                cpu_state.MM[cpu_reg].w[1] = ((int32_t)cpu_state.MM[cpu_reg].sw[1] * (int32_t)src.sw[1]) >> 16;
                cpu_state.MM[cpu_reg].w[2] = ((int32_t)cpu_state.MM[cpu_reg].sw[2] * (int32_t)src.sw[2]) >> 16;
                cpu_state.MM[cpu_reg].w[3] = ((int32_t)cpu_state.MM[cpu_reg].sw[3] * (int32_t)src.sw[3]) >> 16;
                CLOCK_CYCLES(2);
        }
        return 0;
}
static int opPMULHW_a32(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3)
        {
                cpu_state.MM[cpu_reg].w[0] = ((int32_t)cpu_state.MM[cpu_reg].sw[0] * (int32_t)cpu_state.MM[cpu_rm].sw[0]) >> 16;
                cpu_state.MM[cpu_reg].w[1] = ((int32_t)cpu_state.MM[cpu_reg].sw[1] * (int32_t)cpu_state.MM[cpu_rm].sw[1]) >> 16;
                cpu_state.MM[cpu_reg].w[2] = ((int32_t)cpu_state.MM[cpu_reg].sw[2] * (int32_t)cpu_state.MM[cpu_rm].sw[2]) >> 16;
                cpu_state.MM[cpu_reg].w[3] = ((int32_t)cpu_state.MM[cpu_reg].sw[3] * (int32_t)cpu_state.MM[cpu_rm].sw[3]) >> 16;
                CLOCK_CYCLES(1);
        }
        else
        {
                MMX_REG src;
        
                SEG_CHECK_READ(cpu_state.ea_seg);
                src.l[0] = readmeml(easeg, cpu_state.eaaddr);
                src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 0;
                cpu_state.MM[cpu_reg].w[0] = ((int32_t)cpu_state.MM[cpu_reg].sw[0] * (int32_t)src.sw[0]) >> 16;
                cpu_state.MM[cpu_reg].w[1] = ((int32_t)cpu_state.MM[cpu_reg].sw[1] * (int32_t)src.sw[1]) >> 16;
                cpu_state.MM[cpu_reg].w[2] = ((int32_t)cpu_state.MM[cpu_reg].sw[2] * (int32_t)src.sw[2]) >> 16;
                cpu_state.MM[cpu_reg].w[3] = ((int32_t)cpu_state.MM[cpu_reg].sw[3] * (int32_t)src.sw[3]) >> 16;
                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opPSUBB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].b[0] -= src.b[0];
        cpu_state.MM[cpu_reg].b[1] -= src.b[1];
        cpu_state.MM[cpu_reg].b[2] -= src.b[2];
        cpu_state.MM[cpu_reg].b[3] -= src.b[3];
        cpu_state.MM[cpu_reg].b[4] -= src.b[4];
        cpu_state.MM[cpu_reg].b[5] -= src.b[5];
        cpu_state.MM[cpu_reg].b[6] -= src.b[6];
        cpu_state.MM[cpu_reg].b[7] -= src.b[7];

        return 0;
}
static int opPSUBB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].b[0] -= src.b[0];
        cpu_state.MM[cpu_reg].b[1] -= src.b[1];
        cpu_state.MM[cpu_reg].b[2] -= src.b[2];
        cpu_state.MM[cpu_reg].b[3] -= src.b[3];
        cpu_state.MM[cpu_reg].b[4] -= src.b[4];
        cpu_state.MM[cpu_reg].b[5] -= src.b[5];
        cpu_state.MM[cpu_reg].b[6] -= src.b[6];
        cpu_state.MM[cpu_reg].b[7] -= src.b[7];

        return 0;
}

static int opPSUBW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].w[0] -= src.w[0];
        cpu_state.MM[cpu_reg].w[1] -= src.w[1];
        cpu_state.MM[cpu_reg].w[2] -= src.w[2];
        cpu_state.MM[cpu_reg].w[3] -= src.w[3];

        return 0;
}
static int opPSUBW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].w[0] -= src.w[0];
        cpu_state.MM[cpu_reg].w[1] -= src.w[1];
        cpu_state.MM[cpu_reg].w[2] -= src.w[2];
        cpu_state.MM[cpu_reg].w[3] -= src.w[3];

        return 0;
}

static int opPSUBD_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].l[0] -= src.l[0];
        cpu_state.MM[cpu_reg].l[1] -= src.l[1];

        return 0;
}
static int opPSUBD_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].l[0] -= src.l[0];
        cpu_state.MM[cpu_reg].l[1] -= src.l[1];

        return 0;
}

static int opPSUBSB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].sb[0] = SSATB(cpu_state.MM[cpu_reg].sb[0] - src.sb[0]);
        cpu_state.MM[cpu_reg].sb[1] = SSATB(cpu_state.MM[cpu_reg].sb[1] - src.sb[1]);
        cpu_state.MM[cpu_reg].sb[2] = SSATB(cpu_state.MM[cpu_reg].sb[2] - src.sb[2]);
        cpu_state.MM[cpu_reg].sb[3] = SSATB(cpu_state.MM[cpu_reg].sb[3] - src.sb[3]);
        cpu_state.MM[cpu_reg].sb[4] = SSATB(cpu_state.MM[cpu_reg].sb[4] - src.sb[4]);
        cpu_state.MM[cpu_reg].sb[5] = SSATB(cpu_state.MM[cpu_reg].sb[5] - src.sb[5]);
        cpu_state.MM[cpu_reg].sb[6] = SSATB(cpu_state.MM[cpu_reg].sb[6] - src.sb[6]);
        cpu_state.MM[cpu_reg].sb[7] = SSATB(cpu_state.MM[cpu_reg].sb[7] - src.sb[7]);

        return 0;
}
static int opPSUBSB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].sb[0] = SSATB(cpu_state.MM[cpu_reg].sb[0] - src.sb[0]);
        cpu_state.MM[cpu_reg].sb[1] = SSATB(cpu_state.MM[cpu_reg].sb[1] - src.sb[1]);
        cpu_state.MM[cpu_reg].sb[2] = SSATB(cpu_state.MM[cpu_reg].sb[2] - src.sb[2]);
        cpu_state.MM[cpu_reg].sb[3] = SSATB(cpu_state.MM[cpu_reg].sb[3] - src.sb[3]);
        cpu_state.MM[cpu_reg].sb[4] = SSATB(cpu_state.MM[cpu_reg].sb[4] - src.sb[4]);
        cpu_state.MM[cpu_reg].sb[5] = SSATB(cpu_state.MM[cpu_reg].sb[5] - src.sb[5]);
        cpu_state.MM[cpu_reg].sb[6] = SSATB(cpu_state.MM[cpu_reg].sb[6] - src.sb[6]);
        cpu_state.MM[cpu_reg].sb[7] = SSATB(cpu_state.MM[cpu_reg].sb[7] - src.sb[7]);

        return 0;
}

static int opPSUBUSB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].b[0] = USATB(cpu_state.MM[cpu_reg].b[0] - src.b[0]);
        cpu_state.MM[cpu_reg].b[1] = USATB(cpu_state.MM[cpu_reg].b[1] - src.b[1]);
        cpu_state.MM[cpu_reg].b[2] = USATB(cpu_state.MM[cpu_reg].b[2] - src.b[2]);
        cpu_state.MM[cpu_reg].b[3] = USATB(cpu_state.MM[cpu_reg].b[3] - src.b[3]);
        cpu_state.MM[cpu_reg].b[4] = USATB(cpu_state.MM[cpu_reg].b[4] - src.b[4]);
        cpu_state.MM[cpu_reg].b[5] = USATB(cpu_state.MM[cpu_reg].b[5] - src.b[5]);
        cpu_state.MM[cpu_reg].b[6] = USATB(cpu_state.MM[cpu_reg].b[6] - src.b[6]);
        cpu_state.MM[cpu_reg].b[7] = USATB(cpu_state.MM[cpu_reg].b[7] - src.b[7]);

        return 0;
}
static int opPSUBUSB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].b[0] = USATB(cpu_state.MM[cpu_reg].b[0] - src.b[0]);
        cpu_state.MM[cpu_reg].b[1] = USATB(cpu_state.MM[cpu_reg].b[1] - src.b[1]);
        cpu_state.MM[cpu_reg].b[2] = USATB(cpu_state.MM[cpu_reg].b[2] - src.b[2]);
        cpu_state.MM[cpu_reg].b[3] = USATB(cpu_state.MM[cpu_reg].b[3] - src.b[3]);
        cpu_state.MM[cpu_reg].b[4] = USATB(cpu_state.MM[cpu_reg].b[4] - src.b[4]);
        cpu_state.MM[cpu_reg].b[5] = USATB(cpu_state.MM[cpu_reg].b[5] - src.b[5]);
        cpu_state.MM[cpu_reg].b[6] = USATB(cpu_state.MM[cpu_reg].b[6] - src.b[6]);
        cpu_state.MM[cpu_reg].b[7] = USATB(cpu_state.MM[cpu_reg].b[7] - src.b[7]);

        return 0;
}

static int opPSUBSW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].sw[0] = SSATW(cpu_state.MM[cpu_reg].sw[0] - src.sw[0]);
        cpu_state.MM[cpu_reg].sw[1] = SSATW(cpu_state.MM[cpu_reg].sw[1] - src.sw[1]);
        cpu_state.MM[cpu_reg].sw[2] = SSATW(cpu_state.MM[cpu_reg].sw[2] - src.sw[2]);
        cpu_state.MM[cpu_reg].sw[3] = SSATW(cpu_state.MM[cpu_reg].sw[3] - src.sw[3]);

        return 0;
}
static int opPSUBSW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].sw[0] = SSATW(cpu_state.MM[cpu_reg].sw[0] - src.sw[0]);
        cpu_state.MM[cpu_reg].sw[1] = SSATW(cpu_state.MM[cpu_reg].sw[1] - src.sw[1]);
        cpu_state.MM[cpu_reg].sw[2] = SSATW(cpu_state.MM[cpu_reg].sw[2] - src.sw[2]);
        cpu_state.MM[cpu_reg].sw[3] = SSATW(cpu_state.MM[cpu_reg].sw[3] - src.sw[3]);

        return 0;
}

static int opPSUBUSW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].w[0] = USATW(cpu_state.MM[cpu_reg].w[0] - src.w[0]);
        cpu_state.MM[cpu_reg].w[1] = USATW(cpu_state.MM[cpu_reg].w[1] - src.w[1]);
        cpu_state.MM[cpu_reg].w[2] = USATW(cpu_state.MM[cpu_reg].w[2] - src.w[2]);
        cpu_state.MM[cpu_reg].w[3] = USATW(cpu_state.MM[cpu_reg].w[3] - src.w[3]);

        return 0;
}
static int opPSUBUSW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].w[0] = USATW(cpu_state.MM[cpu_reg].w[0] - src.w[0]);
        cpu_state.MM[cpu_reg].w[1] = USATW(cpu_state.MM[cpu_reg].w[1] - src.w[1]);
        cpu_state.MM[cpu_reg].w[2] = USATW(cpu_state.MM[cpu_reg].w[2] - src.w[2]);
        cpu_state.MM[cpu_reg].w[3] = USATW(cpu_state.MM[cpu_reg].w[3] - src.w[3]);

        return 0;
}

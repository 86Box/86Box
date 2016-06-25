static int opPADDB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].b[0] += src.b[0];
        MM[reg].b[1] += src.b[1];
        MM[reg].b[2] += src.b[2];
        MM[reg].b[3] += src.b[3];
        MM[reg].b[4] += src.b[4];
        MM[reg].b[5] += src.b[5];
        MM[reg].b[6] += src.b[6];
        MM[reg].b[7] += src.b[7];

        return 0;
}
static int opPADDB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        MM[reg].b[0] += src.b[0];
        MM[reg].b[1] += src.b[1];
        MM[reg].b[2] += src.b[2];
        MM[reg].b[3] += src.b[3];
        MM[reg].b[4] += src.b[4];
        MM[reg].b[5] += src.b[5];
        MM[reg].b[6] += src.b[6];
        MM[reg].b[7] += src.b[7];

        return 0;
}

static int opPADDW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].w[0] += src.w[0];
        MM[reg].w[1] += src.w[1];
        MM[reg].w[2] += src.w[2];
        MM[reg].w[3] += src.w[3];

        return 0;
}
static int opPADDW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        MM[reg].w[0] += src.w[0];
        MM[reg].w[1] += src.w[1];
        MM[reg].w[2] += src.w[2];
        MM[reg].w[3] += src.w[3];

        return 0;
}

static int opPADDD_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].l[0] += src.l[0];
        MM[reg].l[1] += src.l[1];

        return 0;
}
static int opPADDD_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        MM[reg].l[0] += src.l[0];
        MM[reg].l[1] += src.l[1];

        return 0;
}

static int opPADDSB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].sb[0] = SSATB(MM[reg].sb[0] + src.sb[0]);
        MM[reg].sb[1] = SSATB(MM[reg].sb[1] + src.sb[1]);
        MM[reg].sb[2] = SSATB(MM[reg].sb[2] + src.sb[2]);
        MM[reg].sb[3] = SSATB(MM[reg].sb[3] + src.sb[3]);
        MM[reg].sb[4] = SSATB(MM[reg].sb[4] + src.sb[4]);
        MM[reg].sb[5] = SSATB(MM[reg].sb[5] + src.sb[5]);
        MM[reg].sb[6] = SSATB(MM[reg].sb[6] + src.sb[6]);
        MM[reg].sb[7] = SSATB(MM[reg].sb[7] + src.sb[7]);

        return 0;
}
static int opPADDSB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        MM[reg].sb[0] = SSATB(MM[reg].sb[0] + src.sb[0]);
        MM[reg].sb[1] = SSATB(MM[reg].sb[1] + src.sb[1]);
        MM[reg].sb[2] = SSATB(MM[reg].sb[2] + src.sb[2]);
        MM[reg].sb[3] = SSATB(MM[reg].sb[3] + src.sb[3]);
        MM[reg].sb[4] = SSATB(MM[reg].sb[4] + src.sb[4]);
        MM[reg].sb[5] = SSATB(MM[reg].sb[5] + src.sb[5]);
        MM[reg].sb[6] = SSATB(MM[reg].sb[6] + src.sb[6]);
        MM[reg].sb[7] = SSATB(MM[reg].sb[7] + src.sb[7]);

        return 0;
}

static int opPADDUSB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].b[0] = USATB(MM[reg].b[0] + src.b[0]);
        MM[reg].b[1] = USATB(MM[reg].b[1] + src.b[1]);
        MM[reg].b[2] = USATB(MM[reg].b[2] + src.b[2]);
        MM[reg].b[3] = USATB(MM[reg].b[3] + src.b[3]);
        MM[reg].b[4] = USATB(MM[reg].b[4] + src.b[4]);
        MM[reg].b[5] = USATB(MM[reg].b[5] + src.b[5]);
        MM[reg].b[6] = USATB(MM[reg].b[6] + src.b[6]);
        MM[reg].b[7] = USATB(MM[reg].b[7] + src.b[7]);

        return 0;
}
static int opPADDUSB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        MM[reg].b[0] = USATB(MM[reg].b[0] + src.b[0]);
        MM[reg].b[1] = USATB(MM[reg].b[1] + src.b[1]);
        MM[reg].b[2] = USATB(MM[reg].b[2] + src.b[2]);
        MM[reg].b[3] = USATB(MM[reg].b[3] + src.b[3]);
        MM[reg].b[4] = USATB(MM[reg].b[4] + src.b[4]);
        MM[reg].b[5] = USATB(MM[reg].b[5] + src.b[5]);
        MM[reg].b[6] = USATB(MM[reg].b[6] + src.b[6]);
        MM[reg].b[7] = USATB(MM[reg].b[7] + src.b[7]);

        return 0;
}

static int opPADDSW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].sw[0] = SSATW(MM[reg].sw[0] + src.sw[0]);
        MM[reg].sw[1] = SSATW(MM[reg].sw[1] + src.sw[1]);
        MM[reg].sw[2] = SSATW(MM[reg].sw[2] + src.sw[2]);
        MM[reg].sw[3] = SSATW(MM[reg].sw[3] + src.sw[3]);

        return 0;
}
static int opPADDSW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        MM[reg].sw[0] = SSATW(MM[reg].sw[0] + src.sw[0]);
        MM[reg].sw[1] = SSATW(MM[reg].sw[1] + src.sw[1]);
        MM[reg].sw[2] = SSATW(MM[reg].sw[2] + src.sw[2]);
        MM[reg].sw[3] = SSATW(MM[reg].sw[3] + src.sw[3]);

        return 0;
}

static int opPADDUSW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].w[0] = USATW(MM[reg].w[0] + src.w[0]);
        MM[reg].w[1] = USATW(MM[reg].w[1] + src.w[1]);
        MM[reg].w[2] = USATW(MM[reg].w[2] + src.w[2]);
        MM[reg].w[3] = USATW(MM[reg].w[3] + src.w[3]);

        return 0;
}
static int opPADDUSW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        MM[reg].w[0] = USATW(MM[reg].w[0] + src.w[0]);
        MM[reg].w[1] = USATW(MM[reg].w[1] + src.w[1]);
        MM[reg].w[2] = USATW(MM[reg].w[2] + src.w[2]);
        MM[reg].w[3] = USATW(MM[reg].w[3] + src.w[3]);

        return 0;
}

static int opPMADDWD_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        if (MM[reg].l[0] == 0x80008000 && src.l[0] == 0x80008000)
                MM[reg].l[0] = 0x80000000;
        else
                MM[reg].sl[0] = ((int32_t)MM[reg].sw[0] * (int32_t)src.sw[0]) + ((int32_t)MM[reg].sw[1] * (int32_t)src.sw[1]);

        if (MM[reg].l[1] == 0x80008000 && src.l[1] == 0x80008000)
                MM[reg].l[1] = 0x80000000;
        else
                MM[reg].sl[1] = ((int32_t)MM[reg].sw[2] * (int32_t)src.sw[2]) + ((int32_t)MM[reg].sw[3] * (int32_t)src.sw[3]);
        
        return 0;
}
static int opPMADDWD_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        if (MM[reg].l[0] == 0x80008000 && src.l[0] == 0x80008000)
                MM[reg].l[0] = 0x80000000;
        else
                MM[reg].sl[0] = ((int32_t)MM[reg].sw[0] * (int32_t)src.sw[0]) + ((int32_t)MM[reg].sw[1] * (int32_t)src.sw[1]);

        if (MM[reg].l[1] == 0x80008000 && src.l[1] == 0x80008000)
                MM[reg].l[1] = 0x80000000;
        else
                MM[reg].sl[1] = ((int32_t)MM[reg].sw[2] * (int32_t)src.sw[2]) + ((int32_t)MM[reg].sw[3] * (int32_t)src.sw[3]);
        
        return 0;
}


static int opPMULLW_a16(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        if (mod == 3)
        {
                MM[reg].w[0] *= MM[rm].w[0];
                MM[reg].w[1] *= MM[rm].w[1];
                MM[reg].w[2] *= MM[rm].w[2];
                MM[reg].w[3] *= MM[rm].w[3];
                CLOCK_CYCLES(1);
        }
        else
        {
                MMX_REG src;
        
                src.l[0] = readmeml(easeg, eaaddr);
                src.l[1] = readmeml(easeg, eaaddr + 4); if (abrt) return 0;
                MM[reg].w[0] *= src.w[0];
                MM[reg].w[1] *= src.w[1];
                MM[reg].w[2] *= src.w[2];
                MM[reg].w[3] *= src.w[3];
                CLOCK_CYCLES(2);
        }
        return 0;
}
static int opPMULLW_a32(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        if (mod == 3)
        {
                MM[reg].w[0] *= MM[rm].w[0];
                MM[reg].w[1] *= MM[rm].w[1];
                MM[reg].w[2] *= MM[rm].w[2];
                MM[reg].w[3] *= MM[rm].w[3];
                CLOCK_CYCLES(1);
        }
        else
        {
                MMX_REG src;
        
                src.l[0] = readmeml(easeg, eaaddr);
                src.l[1] = readmeml(easeg, eaaddr + 4); if (abrt) return 0;
                MM[reg].w[0] *= src.w[0];
                MM[reg].w[1] *= src.w[1];
                MM[reg].w[2] *= src.w[2];
                MM[reg].w[3] *= src.w[3];
                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opPMULHW_a16(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        if (mod == 3)
        {
                MM[reg].w[0] = ((int32_t)MM[reg].sw[0] * (int32_t)MM[rm].sw[0]) >> 16;
                MM[reg].w[1] = ((int32_t)MM[reg].sw[1] * (int32_t)MM[rm].sw[1]) >> 16;
                MM[reg].w[2] = ((int32_t)MM[reg].sw[2] * (int32_t)MM[rm].sw[2]) >> 16;
                MM[reg].w[3] = ((int32_t)MM[reg].sw[3] * (int32_t)MM[rm].sw[3]) >> 16;
                CLOCK_CYCLES(1);
        }
        else
        {
                MMX_REG src;
        
                src.l[0] = readmeml(easeg, eaaddr);
                src.l[1] = readmeml(easeg, eaaddr + 4); if (abrt) return 0;
                MM[reg].w[0] = ((int32_t)MM[reg].sw[0] * (int32_t)src.sw[0]) >> 16;
                MM[reg].w[1] = ((int32_t)MM[reg].sw[1] * (int32_t)src.sw[1]) >> 16;
                MM[reg].w[2] = ((int32_t)MM[reg].sw[2] * (int32_t)src.sw[2]) >> 16;
                MM[reg].w[3] = ((int32_t)MM[reg].sw[3] * (int32_t)src.sw[3]) >> 16;
                CLOCK_CYCLES(2);
        }
        return 0;
}
static int opPMULHW_a32(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        if (mod == 3)
        {
                MM[reg].w[0] = ((int32_t)MM[reg].sw[0] * (int32_t)MM[rm].sw[0]) >> 16;
                MM[reg].w[1] = ((int32_t)MM[reg].sw[1] * (int32_t)MM[rm].sw[1]) >> 16;
                MM[reg].w[2] = ((int32_t)MM[reg].sw[2] * (int32_t)MM[rm].sw[2]) >> 16;
                MM[reg].w[3] = ((int32_t)MM[reg].sw[3] * (int32_t)MM[rm].sw[3]) >> 16;
                CLOCK_CYCLES(1);
        }
        else
        {
                MMX_REG src;
        
                src.l[0] = readmeml(easeg, eaaddr);
                src.l[1] = readmeml(easeg, eaaddr + 4); if (abrt) return 0;
                MM[reg].w[0] = ((int32_t)MM[reg].sw[0] * (int32_t)src.sw[0]) >> 16;
                MM[reg].w[1] = ((int32_t)MM[reg].sw[1] * (int32_t)src.sw[1]) >> 16;
                MM[reg].w[2] = ((int32_t)MM[reg].sw[2] * (int32_t)src.sw[2]) >> 16;
                MM[reg].w[3] = ((int32_t)MM[reg].sw[3] * (int32_t)src.sw[3]) >> 16;
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
        
        MM[reg].b[0] -= src.b[0];
        MM[reg].b[1] -= src.b[1];
        MM[reg].b[2] -= src.b[2];
        MM[reg].b[3] -= src.b[3];
        MM[reg].b[4] -= src.b[4];
        MM[reg].b[5] -= src.b[5];
        MM[reg].b[6] -= src.b[6];
        MM[reg].b[7] -= src.b[7];

        return 0;
}
static int opPSUBB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        MM[reg].b[0] -= src.b[0];
        MM[reg].b[1] -= src.b[1];
        MM[reg].b[2] -= src.b[2];
        MM[reg].b[3] -= src.b[3];
        MM[reg].b[4] -= src.b[4];
        MM[reg].b[5] -= src.b[5];
        MM[reg].b[6] -= src.b[6];
        MM[reg].b[7] -= src.b[7];

        return 0;
}

static int opPSUBW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].w[0] -= src.w[0];
        MM[reg].w[1] -= src.w[1];
        MM[reg].w[2] -= src.w[2];
        MM[reg].w[3] -= src.w[3];

        return 0;
}
static int opPSUBW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        MM[reg].w[0] -= src.w[0];
        MM[reg].w[1] -= src.w[1];
        MM[reg].w[2] -= src.w[2];
        MM[reg].w[3] -= src.w[3];

        return 0;
}

static int opPSUBD_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].l[0] -= src.l[0];
        MM[reg].l[1] -= src.l[1];

        return 0;
}
static int opPSUBD_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        MM[reg].l[0] -= src.l[0];
        MM[reg].l[1] -= src.l[1];

        return 0;
}

static int opPSUBSB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].sb[0] = SSATB(MM[reg].sb[0] - src.sb[0]);
        MM[reg].sb[1] = SSATB(MM[reg].sb[1] - src.sb[1]);
        MM[reg].sb[2] = SSATB(MM[reg].sb[2] - src.sb[2]);
        MM[reg].sb[3] = SSATB(MM[reg].sb[3] - src.sb[3]);
        MM[reg].sb[4] = SSATB(MM[reg].sb[4] - src.sb[4]);
        MM[reg].sb[5] = SSATB(MM[reg].sb[5] - src.sb[5]);
        MM[reg].sb[6] = SSATB(MM[reg].sb[6] - src.sb[6]);
        MM[reg].sb[7] = SSATB(MM[reg].sb[7] - src.sb[7]);

        return 0;
}
static int opPSUBSB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        MM[reg].sb[0] = SSATB(MM[reg].sb[0] - src.sb[0]);
        MM[reg].sb[1] = SSATB(MM[reg].sb[1] - src.sb[1]);
        MM[reg].sb[2] = SSATB(MM[reg].sb[2] - src.sb[2]);
        MM[reg].sb[3] = SSATB(MM[reg].sb[3] - src.sb[3]);
        MM[reg].sb[4] = SSATB(MM[reg].sb[4] - src.sb[4]);
        MM[reg].sb[5] = SSATB(MM[reg].sb[5] - src.sb[5]);
        MM[reg].sb[6] = SSATB(MM[reg].sb[6] - src.sb[6]);
        MM[reg].sb[7] = SSATB(MM[reg].sb[7] - src.sb[7]);

        return 0;
}

static int opPSUBUSB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].b[0] = USATB(MM[reg].b[0] - src.b[0]);
        MM[reg].b[1] = USATB(MM[reg].b[1] - src.b[1]);
        MM[reg].b[2] = USATB(MM[reg].b[2] - src.b[2]);
        MM[reg].b[3] = USATB(MM[reg].b[3] - src.b[3]);
        MM[reg].b[4] = USATB(MM[reg].b[4] - src.b[4]);
        MM[reg].b[5] = USATB(MM[reg].b[5] - src.b[5]);
        MM[reg].b[6] = USATB(MM[reg].b[6] - src.b[6]);
        MM[reg].b[7] = USATB(MM[reg].b[7] - src.b[7]);

        return 0;
}
static int opPSUBUSB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        MM[reg].b[0] = USATB(MM[reg].b[0] - src.b[0]);
        MM[reg].b[1] = USATB(MM[reg].b[1] - src.b[1]);
        MM[reg].b[2] = USATB(MM[reg].b[2] - src.b[2]);
        MM[reg].b[3] = USATB(MM[reg].b[3] - src.b[3]);
        MM[reg].b[4] = USATB(MM[reg].b[4] - src.b[4]);
        MM[reg].b[5] = USATB(MM[reg].b[5] - src.b[5]);
        MM[reg].b[6] = USATB(MM[reg].b[6] - src.b[6]);
        MM[reg].b[7] = USATB(MM[reg].b[7] - src.b[7]);

        return 0;
}

static int opPSUBSW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].sw[0] = SSATW(MM[reg].sw[0] - src.sw[0]);
        MM[reg].sw[1] = SSATW(MM[reg].sw[1] - src.sw[1]);
        MM[reg].sw[2] = SSATW(MM[reg].sw[2] - src.sw[2]);
        MM[reg].sw[3] = SSATW(MM[reg].sw[3] - src.sw[3]);

        return 0;
}
static int opPSUBSW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        MM[reg].sw[0] = SSATW(MM[reg].sw[0] - src.sw[0]);
        MM[reg].sw[1] = SSATW(MM[reg].sw[1] - src.sw[1]);
        MM[reg].sw[2] = SSATW(MM[reg].sw[2] - src.sw[2]);
        MM[reg].sw[3] = SSATW(MM[reg].sw[3] - src.sw[3]);

        return 0;
}

static int opPSUBUSW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].w[0] = USATW(MM[reg].w[0] - src.w[0]);
        MM[reg].w[1] = USATW(MM[reg].w[1] - src.w[1]);
        MM[reg].w[2] = USATW(MM[reg].w[2] - src.w[2]);
        MM[reg].w[3] = USATW(MM[reg].w[3] - src.w[3]);

        return 0;
}
static int opPSUBUSW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        MM[reg].w[0] = USATW(MM[reg].w[0] - src.w[0]);
        MM[reg].w[1] = USATW(MM[reg].w[1] - src.w[1]);
        MM[reg].w[2] = USATW(MM[reg].w[2] - src.w[2]);
        MM[reg].w[3] = USATW(MM[reg].w[3] - src.w[3]);

        return 0;
}

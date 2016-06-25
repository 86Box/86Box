static int opPUNPCKLDQ_a16(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        if (mod == 3)
        {
                MM[reg].l[1] = MM[rm].l[0];
                CLOCK_CYCLES(1);
        }
        else
        {
                uint32_t src;
        
                src = readmeml(easeg, eaaddr); if (abrt) return 0;
                MM[reg].l[1] = src;

                CLOCK_CYCLES(2);
        }
        return 0;
}
static int opPUNPCKLDQ_a32(uint32_t fetchdat)
{
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        if (mod == 3)
        {
                MM[reg].l[1] = MM[rm].l[0];
                CLOCK_CYCLES(1);
        }
        else
        {
                uint32_t src;
        
                src = readmeml(easeg, eaaddr); if (abrt) return 0;
                MM[reg].l[1] = src;

                CLOCK_CYCLES(2);
        }
        return 0;
}

static int opPUNPCKHDQ_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        MM[reg].l[0] = MM[reg].l[1];
        MM[reg].l[1] = src.l[1];

        return 0;
}
static int opPUNPCKHDQ_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        MM[reg].l[0] = MM[reg].l[1];
        MM[reg].l[1] = src.l[1];

        return 0;
}

static int opPUNPCKLBW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        MM[reg].b[7] = src.b[3];
        MM[reg].b[6] = MM[reg].b[3];
        MM[reg].b[5] = src.b[2];
        MM[reg].b[4] = MM[reg].b[2];
        MM[reg].b[3] = src.b[1];
        MM[reg].b[2] = MM[reg].b[1];
        MM[reg].b[1] = src.b[0];
        MM[reg].b[0] = MM[reg].b[0];

        return 0;
}
static int opPUNPCKLBW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        MM[reg].b[7] = src.b[3];
        MM[reg].b[6] = MM[reg].b[3];
        MM[reg].b[5] = src.b[2];
        MM[reg].b[4] = MM[reg].b[2];
        MM[reg].b[3] = src.b[1];
        MM[reg].b[2] = MM[reg].b[1];
        MM[reg].b[1] = src.b[0];
        MM[reg].b[0] = MM[reg].b[0];

        return 0;
}

static int opPUNPCKHBW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        MM[reg].b[0] = MM[reg].b[4];
        MM[reg].b[1] = src.b[4];
        MM[reg].b[2] = MM[reg].b[5];
        MM[reg].b[3] = src.b[5];
        MM[reg].b[4] = MM[reg].b[6];
        MM[reg].b[5] = src.b[6];
        MM[reg].b[6] = MM[reg].b[7];
        MM[reg].b[7] = src.b[7];
        
        return 0;
}
static int opPUNPCKHBW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        MM[reg].b[0] = MM[reg].b[4];
        MM[reg].b[1] = src.b[4];
        MM[reg].b[2] = MM[reg].b[5];
        MM[reg].b[3] = src.b[5];
        MM[reg].b[4] = MM[reg].b[6];
        MM[reg].b[5] = src.b[6];
        MM[reg].b[6] = MM[reg].b[7];
        MM[reg].b[7] = src.b[7];
        
        return 0;
}

static int opPUNPCKLWD_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        MM[reg].w[3] = src.w[1];
        MM[reg].w[2] = MM[reg].w[1];
        MM[reg].w[1] = src.w[0];
        MM[reg].w[0] = MM[reg].w[0];

        return 0;
}
static int opPUNPCKLWD_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        MM[reg].w[3] = src.w[1];
        MM[reg].w[2] = MM[reg].w[1];
        MM[reg].w[1] = src.w[0];
        MM[reg].w[0] = MM[reg].w[0];

        return 0;
}

static int opPUNPCKHWD_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        MM[reg].w[0] = MM[reg].w[2];
        MM[reg].w[1] = src.w[2];
        MM[reg].w[2] = MM[reg].w[3];
        MM[reg].w[3] = src.w[3];

        return 0;
}
static int opPUNPCKHWD_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        MM[reg].w[0] = MM[reg].w[2];
        MM[reg].w[1] = src.w[2];
        MM[reg].w[2] = MM[reg].w[3];
        MM[reg].w[3] = src.w[3];

        return 0;
}

static int opPACKSSWB_a16(uint32_t fetchdat)
{
        MMX_REG src, dst;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        dst = MM[reg];

        MM[reg].sb[0] = SSATB(dst.sw[0]);
        MM[reg].sb[1] = SSATB(dst.sw[1]);
        MM[reg].sb[2] = SSATB(dst.sw[2]);
        MM[reg].sb[3] = SSATB(dst.sw[3]);
        MM[reg].sb[4] = SSATB(src.sw[0]);
        MM[reg].sb[5] = SSATB(src.sw[1]);
        MM[reg].sb[6] = SSATB(src.sw[2]);
        MM[reg].sb[7] = SSATB(src.sw[3]);
        
        return 0;
}
static int opPACKSSWB_a32(uint32_t fetchdat)
{
        MMX_REG src, dst;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        dst = MM[reg];

        MM[reg].sb[0] = SSATB(dst.sw[0]);
        MM[reg].sb[1] = SSATB(dst.sw[1]);
        MM[reg].sb[2] = SSATB(dst.sw[2]);
        MM[reg].sb[3] = SSATB(dst.sw[3]);
        MM[reg].sb[4] = SSATB(src.sw[0]);
        MM[reg].sb[5] = SSATB(src.sw[1]);
        MM[reg].sb[6] = SSATB(src.sw[2]);
        MM[reg].sb[7] = SSATB(src.sw[3]);
        
        return 0;
}

static int opPACKUSWB_a16(uint32_t fetchdat)
{
        MMX_REG src, dst;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        dst = MM[reg];

        MM[reg].b[0] = USATB(dst.sw[0]);
        MM[reg].b[1] = USATB(dst.sw[1]);
        MM[reg].b[2] = USATB(dst.sw[2]);
        MM[reg].b[3] = USATB(dst.sw[3]);
        MM[reg].b[4] = USATB(src.sw[0]);
        MM[reg].b[5] = USATB(src.sw[1]);
        MM[reg].b[6] = USATB(src.sw[2]);
        MM[reg].b[7] = USATB(src.sw[3]);
        
        return 0;
}
static int opPACKUSWB_a32(uint32_t fetchdat)
{
        MMX_REG src, dst;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        dst = MM[reg];

        MM[reg].b[0] = USATB(dst.sw[0]);
        MM[reg].b[1] = USATB(dst.sw[1]);
        MM[reg].b[2] = USATB(dst.sw[2]);
        MM[reg].b[3] = USATB(dst.sw[3]);
        MM[reg].b[4] = USATB(src.sw[0]);
        MM[reg].b[5] = USATB(src.sw[1]);
        MM[reg].b[6] = USATB(src.sw[2]);
        MM[reg].b[7] = USATB(src.sw[3]);

        return 0;
}

static int opPACKSSDW_a16(uint32_t fetchdat)
{
        MMX_REG src, dst;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        dst = MM[reg];
        
        MM[reg].sw[0] = SSATW(dst.sl[0]);
        MM[reg].sw[1] = SSATW(dst.sl[1]);
        MM[reg].sw[2] = SSATW(src.sl[0]);
        MM[reg].sw[3] = SSATW(src.sl[1]);
        
        return 0;
}
static int opPACKSSDW_a32(uint32_t fetchdat)
{
        MMX_REG src, dst;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        dst = MM[reg];
        
        MM[reg].sw[0] = SSATW(dst.sl[0]);
        MM[reg].sw[1] = SSATW(dst.sl[1]);
        MM[reg].sw[2] = SSATW(src.sl[0]);
        MM[reg].sw[3] = SSATW(src.sl[1]);
        
        return 0;
}

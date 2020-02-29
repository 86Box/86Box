static int opPCMPEQB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].b[0] = (cpu_state.MM[cpu_reg].b[0] == src.b[0]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[1] = (cpu_state.MM[cpu_reg].b[1] == src.b[1]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[2] = (cpu_state.MM[cpu_reg].b[2] == src.b[2]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[3] = (cpu_state.MM[cpu_reg].b[3] == src.b[3]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[4] = (cpu_state.MM[cpu_reg].b[4] == src.b[4]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[5] = (cpu_state.MM[cpu_reg].b[5] == src.b[5]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[6] = (cpu_state.MM[cpu_reg].b[6] == src.b[6]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[7] = (cpu_state.MM[cpu_reg].b[7] == src.b[7]) ? 0xff : 0;
        
        return 0;
}
static int opPCMPEQB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].b[0] = (cpu_state.MM[cpu_reg].b[0] == src.b[0]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[1] = (cpu_state.MM[cpu_reg].b[1] == src.b[1]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[2] = (cpu_state.MM[cpu_reg].b[2] == src.b[2]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[3] = (cpu_state.MM[cpu_reg].b[3] == src.b[3]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[4] = (cpu_state.MM[cpu_reg].b[4] == src.b[4]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[5] = (cpu_state.MM[cpu_reg].b[5] == src.b[5]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[6] = (cpu_state.MM[cpu_reg].b[6] == src.b[6]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[7] = (cpu_state.MM[cpu_reg].b[7] == src.b[7]) ? 0xff : 0;
        
        return 0;
}

static int opPCMPGTB_a16(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].b[0] = (cpu_state.MM[cpu_reg].sb[0] > src.sb[0]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[1] = (cpu_state.MM[cpu_reg].sb[1] > src.sb[1]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[2] = (cpu_state.MM[cpu_reg].sb[2] > src.sb[2]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[3] = (cpu_state.MM[cpu_reg].sb[3] > src.sb[3]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[4] = (cpu_state.MM[cpu_reg].sb[4] > src.sb[4]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[5] = (cpu_state.MM[cpu_reg].sb[5] > src.sb[5]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[6] = (cpu_state.MM[cpu_reg].sb[6] > src.sb[6]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[7] = (cpu_state.MM[cpu_reg].sb[7] > src.sb[7]) ? 0xff : 0;
        
        return 0;
}
static int opPCMPGTB_a32(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].b[0] = (cpu_state.MM[cpu_reg].sb[0] > src.sb[0]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[1] = (cpu_state.MM[cpu_reg].sb[1] > src.sb[1]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[2] = (cpu_state.MM[cpu_reg].sb[2] > src.sb[2]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[3] = (cpu_state.MM[cpu_reg].sb[3] > src.sb[3]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[4] = (cpu_state.MM[cpu_reg].sb[4] > src.sb[4]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[5] = (cpu_state.MM[cpu_reg].sb[5] > src.sb[5]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[6] = (cpu_state.MM[cpu_reg].sb[6] > src.sb[6]) ? 0xff : 0;
        cpu_state.MM[cpu_reg].b[7] = (cpu_state.MM[cpu_reg].sb[7] > src.sb[7]) ? 0xff : 0;
        
        return 0;
}

static int opPCMPEQW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].w[0] = (cpu_state.MM[cpu_reg].w[0] == src.w[0]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[1] = (cpu_state.MM[cpu_reg].w[1] == src.w[1]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[2] = (cpu_state.MM[cpu_reg].w[2] == src.w[2]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[3] = (cpu_state.MM[cpu_reg].w[3] == src.w[3]) ? 0xffff : 0;
        
        return 0;
}
static int opPCMPEQW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].w[0] = (cpu_state.MM[cpu_reg].w[0] == src.w[0]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[1] = (cpu_state.MM[cpu_reg].w[1] == src.w[1]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[2] = (cpu_state.MM[cpu_reg].w[2] == src.w[2]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[3] = (cpu_state.MM[cpu_reg].w[3] == src.w[3]) ? 0xffff : 0;
        
        return 0;
}

static int opPCMPGTW_a16(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].w[0] = (cpu_state.MM[cpu_reg].sw[0] > src.sw[0]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[1] = (cpu_state.MM[cpu_reg].sw[1] > src.sw[1]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[2] = (cpu_state.MM[cpu_reg].sw[2] > src.sw[2]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[3] = (cpu_state.MM[cpu_reg].sw[3] > src.sw[3]) ? 0xffff : 0;
        
        return 0;
}
static int opPCMPGTW_a32(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].w[0] = (cpu_state.MM[cpu_reg].sw[0] > src.sw[0]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[1] = (cpu_state.MM[cpu_reg].sw[1] > src.sw[1]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[2] = (cpu_state.MM[cpu_reg].sw[2] > src.sw[2]) ? 0xffff : 0;
        cpu_state.MM[cpu_reg].w[3] = (cpu_state.MM[cpu_reg].sw[3] > src.sw[3]) ? 0xffff : 0;
        
        return 0;
}

static int opPCMPEQD_a16(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].l[0] = (cpu_state.MM[cpu_reg].l[0] == src.l[0]) ? 0xffffffff : 0;
        cpu_state.MM[cpu_reg].l[1] = (cpu_state.MM[cpu_reg].l[1] == src.l[1]) ? 0xffffffff : 0;
        
        return 0;
}
static int opPCMPEQD_a32(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].l[0] = (cpu_state.MM[cpu_reg].l[0] == src.l[0]) ? 0xffffffff : 0;
        cpu_state.MM[cpu_reg].l[1] = (cpu_state.MM[cpu_reg].l[1] == src.l[1]) ? 0xffffffff : 0;
        
        return 0;
}

static int opPCMPGTD_a16(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].l[0] = (cpu_state.MM[cpu_reg].sl[0] > src.sl[0]) ? 0xffffffff : 0;
        cpu_state.MM[cpu_reg].l[1] = (cpu_state.MM[cpu_reg].sl[1] > src.sl[1]) ? 0xffffffff : 0;
        
        return 0;
}
static int opPCMPGTD_a32(uint32_t fetchdat)
{
        MMX_REG src;
        
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();

        cpu_state.MM[cpu_reg].l[0] = (cpu_state.MM[cpu_reg].sl[0] > src.sl[0]) ? 0xffffffff : 0;
        cpu_state.MM[cpu_reg].l[1] = (cpu_state.MM[cpu_reg].sl[1] > src.sl[1]) ? 0xffffffff : 0;
        
        return 0;
}

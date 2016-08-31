static int opPAND_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].q &= src.q;
        return 0;
}
static int opPAND_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].q &= src.q;
        return 0;
}

static int opPANDN_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].q = ~cpu_state.MM[cpu_reg].q & src.q;
        return 0;
}
static int opPANDN_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].q = ~cpu_state.MM[cpu_reg].q & src.q;
        return 0;
}

static int opPOR_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].q |= src.q;
        return 0;
}
static int opPOR_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].q |= src.q;
        return 0;
}

static int opPXOR_a16(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_16(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].q ^= src.q;
        return 0;
}
static int opPXOR_a32(uint32_t fetchdat)
{
        MMX_REG src;
        MMX_ENTER();
        
        fetch_ea_32(fetchdat);
        MMX_GETSRC();
        
        cpu_state.MM[cpu_reg].q ^= src.q;
        return 0;
}

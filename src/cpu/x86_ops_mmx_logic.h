static int
opPAND_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->q &= src.q;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPAND_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->q &= src.q;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPANDN_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->q = ~dst->q & src.q;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPANDN_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->q = ~dst->q & src.q;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPOR_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->q |= src.q;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPOR_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->q |= src.q;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPXOR_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->q ^= src.q;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPXOR_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->q ^= src.q;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPCMPEQB_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] = (dst->b[0] == src.b[0]) ? 0xff : 0;
    dst->b[1] = (dst->b[1] == src.b[1]) ? 0xff : 0;
    dst->b[2] = (dst->b[2] == src.b[2]) ? 0xff : 0;
    dst->b[3] = (dst->b[3] == src.b[3]) ? 0xff : 0;
    dst->b[4] = (dst->b[4] == src.b[4]) ? 0xff : 0;
    dst->b[5] = (dst->b[5] == src.b[5]) ? 0xff : 0;
    dst->b[6] = (dst->b[6] == src.b[6]) ? 0xff : 0;
    dst->b[7] = (dst->b[7] == src.b[7]) ? 0xff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPCMPEQB_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] = (dst->b[0] == src.b[0]) ? 0xff : 0;
    dst->b[1] = (dst->b[1] == src.b[1]) ? 0xff : 0;
    dst->b[2] = (dst->b[2] == src.b[2]) ? 0xff : 0;
    dst->b[3] = (dst->b[3] == src.b[3]) ? 0xff : 0;
    dst->b[4] = (dst->b[4] == src.b[4]) ? 0xff : 0;
    dst->b[5] = (dst->b[5] == src.b[5]) ? 0xff : 0;
    dst->b[6] = (dst->b[6] == src.b[6]) ? 0xff : 0;
    dst->b[7] = (dst->b[7] == src.b[7]) ? 0xff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPCMPGTB_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] = (dst->sb[0] > src.sb[0]) ? 0xff : 0;
    dst->b[1] = (dst->sb[1] > src.sb[1]) ? 0xff : 0;
    dst->b[2] = (dst->sb[2] > src.sb[2]) ? 0xff : 0;
    dst->b[3] = (dst->sb[3] > src.sb[3]) ? 0xff : 0;
    dst->b[4] = (dst->sb[4] > src.sb[4]) ? 0xff : 0;
    dst->b[5] = (dst->sb[5] > src.sb[5]) ? 0xff : 0;
    dst->b[6] = (dst->sb[6] > src.sb[6]) ? 0xff : 0;
    dst->b[7] = (dst->sb[7] > src.sb[7]) ? 0xff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPCMPGTB_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] = (dst->sb[0] > src.sb[0]) ? 0xff : 0;
    dst->b[1] = (dst->sb[1] > src.sb[1]) ? 0xff : 0;
    dst->b[2] = (dst->sb[2] > src.sb[2]) ? 0xff : 0;
    dst->b[3] = (dst->sb[3] > src.sb[3]) ? 0xff : 0;
    dst->b[4] = (dst->sb[4] > src.sb[4]) ? 0xff : 0;
    dst->b[5] = (dst->sb[5] > src.sb[5]) ? 0xff : 0;
    dst->b[6] = (dst->sb[6] > src.sb[6]) ? 0xff : 0;
    dst->b[7] = (dst->sb[7] > src.sb[7]) ? 0xff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPCMPEQW_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] = (dst->w[0] == src.w[0]) ? 0xffff : 0;
    dst->w[1] = (dst->w[1] == src.w[1]) ? 0xffff : 0;
    dst->w[2] = (dst->w[2] == src.w[2]) ? 0xffff : 0;
    dst->w[3] = (dst->w[3] == src.w[3]) ? 0xffff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPCMPEQW_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] = (dst->w[0] == src.w[0]) ? 0xffff : 0;
    dst->w[1] = (dst->w[1] == src.w[1]) ? 0xffff : 0;
    dst->w[2] = (dst->w[2] == src.w[2]) ? 0xffff : 0;
    dst->w[3] = (dst->w[3] == src.w[3]) ? 0xffff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPCMPGTW_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] = (dst->sw[0] > src.sw[0]) ? 0xffff : 0;
    dst->w[1] = (dst->sw[1] > src.sw[1]) ? 0xffff : 0;
    dst->w[2] = (dst->sw[2] > src.sw[2]) ? 0xffff : 0;
    dst->w[3] = (dst->sw[3] > src.sw[3]) ? 0xffff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPCMPGTW_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] = (dst->sw[0] > src.sw[0]) ? 0xffff : 0;
    dst->w[1] = (dst->sw[1] > src.sw[1]) ? 0xffff : 0;
    dst->w[2] = (dst->sw[2] > src.sw[2]) ? 0xffff : 0;
    dst->w[3] = (dst->sw[3] > src.sw[3]) ? 0xffff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPCMPEQD_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->l[0] = (dst->l[0] == src.l[0]) ? 0xffffffff : 0;
    dst->l[1] = (dst->l[1] == src.l[1]) ? 0xffffffff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPCMPEQD_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->l[0] = (dst->l[0] == src.l[0]) ? 0xffffffff : 0;
    dst->l[1] = (dst->l[1] == src.l[1]) ? 0xffffffff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPCMPGTD_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->l[0] = (dst->sl[0] > src.sl[0]) ? 0xffffffff : 0;
    dst->l[1] = (dst->sl[1] > src.sl[1]) ? 0xffffffff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPCMPGTD_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->l[0] = (dst->sl[0] > src.sl[0]) ? 0xffffffff : 0;
    dst->l[1] = (dst->sl[1] > src.sl[1]) ? 0xffffffff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}

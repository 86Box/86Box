static int
opPAND_a16(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    }

    dst->q &= src.q;

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPAND_a32(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    }

    dst->q &= src.q;

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPANDN_a16(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    }

    dst->q = ~dst->q & src.q;

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPANDN_a32(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    }

    dst->q = ~dst->q & src.q;

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPOR_a16(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    }

    dst->q |= src.q;

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPOR_a32(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    }

    dst->q |= src.q;

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPXOR_a16(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    }

    dst->q ^= src.q;

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPXOR_a32(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    }

    dst->q ^= src.q;

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPUNPCKLDQ_a16(uint32_t fetchdat)
{
    uint32_t usrc;
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    src = fpu_softfloat ? (*(MMX_REG *) &fpu_state.st_space[cpu_rm].fraction) : cpu_state.MM[cpu_rm];
    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);

    if (cpu_mod == 3) {
        if (fpu_softfloat) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
        }
        dst->l[1] = src.l[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        usrc = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 0;
        if (fpu_softfloat) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
        }
        dst->l[1] = usrc;

        CLOCK_CYCLES(2);
    }

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPUNPCKLDQ_a32(uint32_t fetchdat)
{
    uint32_t usrc;
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    src = fpu_softfloat ? (*(MMX_REG *) &fpu_state.st_space[cpu_rm].fraction) : cpu_state.MM[cpu_rm];
    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);

    if (cpu_mod == 3) {
        if (fpu_softfloat) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
        }
        dst->l[1] = src.l[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        usrc = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 0;
        if (fpu_softfloat) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
        }
        dst->l[1] = usrc;

        CLOCK_CYCLES(2);
    }

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPUNPCKHDQ_a16(uint32_t fetchdat)
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

    dst->l[0] = dst->l[1];
    dst->l[1] = src.l[1];

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPUNPCKHDQ_a32(uint32_t fetchdat)
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

    dst->l[0] = dst->l[1];
    dst->l[1] = src.l[1];

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPUNPCKLBW_a16(uint32_t fetchdat)
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

    dst->b[7] = src.b[3];
    dst->b[6] = dst->b[3];
    dst->b[5] = src.b[2];
    dst->b[4] = dst->b[2];
    dst->b[3] = src.b[1];
    dst->b[2] = dst->b[1];
    dst->b[1] = src.b[0];
    dst->b[0] = dst->b[0];

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPUNPCKLBW_a32(uint32_t fetchdat)
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

    dst->b[7] = src.b[3];
    dst->b[6] = dst->b[3];
    dst->b[5] = src.b[2];
    dst->b[4] = dst->b[2];
    dst->b[3] = src.b[1];
    dst->b[2] = dst->b[1];
    dst->b[1] = src.b[0];
    dst->b[0] = dst->b[0];

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPUNPCKHBW_a16(uint32_t fetchdat)
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

    dst->b[0] = dst->b[4];
    dst->b[1] = src.b[4];
    dst->b[2] = dst->b[5];
    dst->b[3] = src.b[5];
    dst->b[4] = dst->b[6];
    dst->b[5] = src.b[6];
    dst->b[6] = dst->b[7];
    dst->b[7] = src.b[7];

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPUNPCKHBW_a32(uint32_t fetchdat)
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

    dst->b[0] = dst->b[4];
    dst->b[1] = src.b[4];
    dst->b[2] = dst->b[5];
    dst->b[3] = src.b[5];
    dst->b[4] = dst->b[6];
    dst->b[5] = src.b[6];
    dst->b[6] = dst->b[7];
    dst->b[7] = src.b[7];

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPUNPCKLWD_a16(uint32_t fetchdat)
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

    dst->w[3] = src.w[1];
    dst->w[2] = dst->w[1];
    dst->w[1] = src.w[0];
    dst->w[0] = dst->w[0];

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPUNPCKLWD_a32(uint32_t fetchdat)
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

    dst->w[3] = src.w[1];
    dst->w[2] = dst->w[1];
    dst->w[1] = src.w[0];
    dst->w[0] = dst->w[0];

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPUNPCKHWD_a16(uint32_t fetchdat)
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

    dst->w[0] = dst->w[2];
    dst->w[1] = src.w[2];
    dst->w[2] = dst->w[3];
    dst->w[3] = src.w[3];

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPUNPCKHWD_a32(uint32_t fetchdat)
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

    dst->w[0] = dst->w[2];
    dst->w[1] = src.w[2];
    dst->w[2] = dst->w[3];
    dst->w[3] = src.w[3];

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPACKSSWB_a16(uint32_t fetchdat)
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

    dst->sb[0] = SSATB(dst->sw[0]);
    dst->sb[1] = SSATB(dst->sw[1]);
    dst->sb[2] = SSATB(dst->sw[2]);
    dst->sb[3] = SSATB(dst->sw[3]);
    dst->sb[4] = SSATB(src.sw[0]);
    dst->sb[5] = SSATB(src.sw[1]);
    dst->sb[6] = SSATB(src.sw[2]);
    dst->sb[7] = SSATB(src.sw[3]);

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPACKSSWB_a32(uint32_t fetchdat)
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

    dst->sb[0] = SSATB(dst->sw[0]);
    dst->sb[1] = SSATB(dst->sw[1]);
    dst->sb[2] = SSATB(dst->sw[2]);
    dst->sb[3] = SSATB(dst->sw[3]);
    dst->sb[4] = SSATB(src.sw[0]);
    dst->sb[5] = SSATB(src.sw[1]);
    dst->sb[6] = SSATB(src.sw[2]);
    dst->sb[7] = SSATB(src.sw[3]);

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPACKUSWB_a16(uint32_t fetchdat)
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

    dst->b[0] = USATB(dst->sw[0]);
    dst->b[1] = USATB(dst->sw[1]);
    dst->b[2] = USATB(dst->sw[2]);
    dst->b[3] = USATB(dst->sw[3]);
    dst->b[4] = USATB(src.sw[0]);
    dst->b[5] = USATB(src.sw[1]);
    dst->b[6] = USATB(src.sw[2]);
    dst->b[7] = USATB(src.sw[3]);

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPACKUSWB_a32(uint32_t fetchdat)
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

    dst->b[0] = USATB(dst->sw[0]);
    dst->b[1] = USATB(dst->sw[1]);
    dst->b[2] = USATB(dst->sw[2]);
    dst->b[3] = USATB(dst->sw[3]);
    dst->b[4] = USATB(src.sw[0]);
    dst->b[5] = USATB(src.sw[1]);
    dst->b[6] = USATB(src.sw[2]);
    dst->b[7] = USATB(src.sw[3]);

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

static int
opPACKSSDW_a16(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_REG *dst, dst2;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);
    dst2 = *dst;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    }

    dst->sw[0] = SSATW(dst2.sl[0]);
    dst->sw[1] = SSATW(dst2.sl[1]);
    dst->sw[2] = SSATW(src.sl[0]);
    dst->sw[3] = SSATW(src.sl[1]);

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}
static int
opPACKSSDW_a32(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_REG *dst, dst2;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[cpu_reg].fraction) : &(cpu_state.MM[cpu_reg]);
    dst2 = *dst;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    }

    dst->sw[0] = SSATW(dst2.sl[0]);
    dst->sw[1] = SSATW(dst2.sl[1]);
    dst->sw[2] = SSATW(src.sl[0]);
    dst->sw[3] = SSATW(src.sl[1]);

    if (fpu_softfloat)
        fpu_state.st_space[cpu_reg].exp = 0xffff;

    return 0;
}

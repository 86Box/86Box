static int
opPADDB_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] += src.b[0];
    dst->b[1] += src.b[1];
    dst->b[2] += src.b[2];
    dst->b[3] += src.b[3];
    dst->b[4] += src.b[4];
    dst->b[5] += src.b[5];
    dst->b[6] += src.b[6];
    dst->b[7] += src.b[7];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPADDB_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] += src.b[0];
    dst->b[1] += src.b[1];
    dst->b[2] += src.b[2];
    dst->b[3] += src.b[3];
    dst->b[4] += src.b[4];
    dst->b[5] += src.b[5];
    dst->b[6] += src.b[6];
    dst->b[7] += src.b[7];

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPADDW_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] += src.w[0];
    dst->w[1] += src.w[1];
    dst->w[2] += src.w[2];
    dst->w[3] += src.w[3];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPADDW_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] += src.w[0];
    dst->w[1] += src.w[1];
    dst->w[2] += src.w[2];
    dst->w[3] += src.w[3];

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPADDD_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->l[0] += src.l[0];
    dst->l[1] += src.l[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPADDD_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->l[0] += src.l[0];
    dst->l[1] += src.l[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPADDSB_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->sb[0] = SSATB(dst->sb[0] + src.sb[0]);
    dst->sb[1] = SSATB(dst->sb[1] + src.sb[1]);
    dst->sb[2] = SSATB(dst->sb[2] + src.sb[2]);
    dst->sb[3] = SSATB(dst->sb[3] + src.sb[3]);
    dst->sb[4] = SSATB(dst->sb[4] + src.sb[4]);
    dst->sb[5] = SSATB(dst->sb[5] + src.sb[5]);
    dst->sb[6] = SSATB(dst->sb[6] + src.sb[6]);
    dst->sb[7] = SSATB(dst->sb[7] + src.sb[7]);

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPADDSB_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->sb[0] = SSATB(dst->sb[0] + src.sb[0]);
    dst->sb[1] = SSATB(dst->sb[1] + src.sb[1]);
    dst->sb[2] = SSATB(dst->sb[2] + src.sb[2]);
    dst->sb[3] = SSATB(dst->sb[3] + src.sb[3]);
    dst->sb[4] = SSATB(dst->sb[4] + src.sb[4]);
    dst->sb[5] = SSATB(dst->sb[5] + src.sb[5]);
    dst->sb[6] = SSATB(dst->sb[6] + src.sb[6]);
    dst->sb[7] = SSATB(dst->sb[7] + src.sb[7]);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPADDUSB_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] = USATB(dst->b[0] + src.b[0]);
    dst->b[1] = USATB(dst->b[1] + src.b[1]);
    dst->b[2] = USATB(dst->b[2] + src.b[2]);
    dst->b[3] = USATB(dst->b[3] + src.b[3]);
    dst->b[4] = USATB(dst->b[4] + src.b[4]);
    dst->b[5] = USATB(dst->b[5] + src.b[5]);
    dst->b[6] = USATB(dst->b[6] + src.b[6]);
    dst->b[7] = USATB(dst->b[7] + src.b[7]);

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPADDUSB_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] = USATB(dst->b[0] + src.b[0]);
    dst->b[1] = USATB(dst->b[1] + src.b[1]);
    dst->b[2] = USATB(dst->b[2] + src.b[2]);
    dst->b[3] = USATB(dst->b[3] + src.b[3]);
    dst->b[4] = USATB(dst->b[4] + src.b[4]);
    dst->b[5] = USATB(dst->b[5] + src.b[5]);
    dst->b[6] = USATB(dst->b[6] + src.b[6]);
    dst->b[7] = USATB(dst->b[7] + src.b[7]);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPADDSW_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->sw[0] = SSATW(dst->sw[0] + src.sw[0]);
    dst->sw[1] = SSATW(dst->sw[1] + src.sw[1]);
    dst->sw[2] = SSATW(dst->sw[2] + src.sw[2]);
    dst->sw[3] = SSATW(dst->sw[3] + src.sw[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPADDSW_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->sw[0] = SSATW(dst->sw[0] + src.sw[0]);
    dst->sw[1] = SSATW(dst->sw[1] + src.sw[1]);
    dst->sw[2] = SSATW(dst->sw[2] + src.sw[2]);
    dst->sw[3] = SSATW(dst->sw[3] + src.sw[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPADDUSW_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] = USATW(dst->w[0] + src.w[0]);
    dst->w[1] = USATW(dst->w[1] + src.w[1]);
    dst->w[2] = USATW(dst->w[2] + src.w[2]);
    dst->w[3] = USATW(dst->w[3] + src.w[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPADDUSW_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] = USATW(dst->w[0] + src.w[0]);
    dst->w[1] = USATW(dst->w[1] + src.w[1]);
    dst->w[2] = USATW(dst->w[2] + src.w[2]);
    dst->w[3] = USATW(dst->w[3] + src.w[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPMADDWD_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    if (dst->l[0] == 0x80008000 && src.l[0] == 0x80008000)
        dst->l[0] = 0x80000000;
    else
        dst->sl[0] = ((int32_t) dst->sw[0] * (int32_t) src.sw[0]) + ((int32_t) dst->sw[1] * (int32_t) src.sw[1]);

    if (dst->l[1] == 0x80008000 && src.l[1] == 0x80008000)
        dst->l[1] = 0x80000000;
    else
        dst->sl[1] = ((int32_t) dst->sw[2] * (int32_t) src.sw[2]) + ((int32_t) dst->sw[3] * (int32_t) src.sw[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPMADDWD_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    if (dst->l[0] == 0x80008000 && src.l[0] == 0x80008000)
        dst->l[0] = 0x80000000;
    else
        dst->sl[0] = ((int32_t) dst->sw[0] * (int32_t) src.sw[0]) + ((int32_t) dst->sw[1] * (int32_t) src.sw[1]);

    if (dst->l[1] == 0x80008000 && src.l[1] == 0x80008000)
        dst->l[1] = 0x80000000;
    else
        dst->sl[1] = ((int32_t) dst->sw[2] * (int32_t) src.sw[2]) + ((int32_t) dst->sw[3] * (int32_t) src.sw[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPMULLW_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3)
        src = MMX_GETREG(cpu_rm);
    else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        src.l[0] = readmeml(easeg, cpu_state.eaaddr);
        src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4);
        if (cpu_state.abrt)
            return 0;
        CLOCK_CYCLES(1);
    }
    dst->w[0] *= src.w[0];
    dst->w[1] *= src.w[1];
    dst->w[2] *= src.w[2];
    dst->w[3] *= src.w[3];
    CLOCK_CYCLES(1);

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPMULLW_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3)
        src = MMX_GETREG(cpu_rm);
    else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        src.l[0] = readmeml(easeg, cpu_state.eaaddr);
        src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4);
        if (cpu_state.abrt)
            return 0;
        CLOCK_CYCLES(1);
    }
    dst->w[0] *= src.w[0];
    dst->w[1] *= src.w[1];
    dst->w[2] *= src.w[2];
    dst->w[3] *= src.w[3];
    CLOCK_CYCLES(1);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPMULHW_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3)
        src = MMX_GETREG(cpu_rm);
    else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        src.l[0] = readmeml(easeg, cpu_state.eaaddr);
        src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4);
        if (cpu_state.abrt)
            return 0;
        CLOCK_CYCLES(1);
    }
    dst->w[0] = ((int32_t) dst->sw[0] * (int32_t) src.sw[0]) >> 16;
    dst->w[1] = ((int32_t) dst->sw[1] * (int32_t) src.sw[1]) >> 16;
    dst->w[2] = ((int32_t) dst->sw[2] * (int32_t) src.sw[2]) >> 16;
    dst->w[3] = ((int32_t) dst->sw[3] * (int32_t) src.sw[3]) >> 16;
    CLOCK_CYCLES(1);

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPMULHW_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3)
        src = MMX_GETREG(cpu_rm);
    else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        src.l[0] = readmeml(easeg, cpu_state.eaaddr);
        src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4);
        if (cpu_state.abrt)
            return 0;
        CLOCK_CYCLES(1);
    }
    dst->w[0] = ((int32_t) dst->sw[0] * (int32_t) src.sw[0]) >> 16;
    dst->w[1] = ((int32_t) dst->sw[1] * (int32_t) src.sw[1]) >> 16;
    dst->w[2] = ((int32_t) dst->sw[2] * (int32_t) src.sw[2]) >> 16;
    dst->w[3] = ((int32_t) dst->sw[3] * (int32_t) src.sw[3]) >> 16;
    CLOCK_CYCLES(1);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSUBB_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] -= src.b[0];
    dst->b[1] -= src.b[1];
    dst->b[2] -= src.b[2];
    dst->b[3] -= src.b[3];
    dst->b[4] -= src.b[4];
    dst->b[5] -= src.b[5];
    dst->b[6] -= src.b[6];
    dst->b[7] -= src.b[7];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSUBB_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] -= src.b[0];
    dst->b[1] -= src.b[1];
    dst->b[2] -= src.b[2];
    dst->b[3] -= src.b[3];
    dst->b[4] -= src.b[4];
    dst->b[5] -= src.b[5];
    dst->b[6] -= src.b[6];
    dst->b[7] -= src.b[7];

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSUBW_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] -= src.w[0];
    dst->w[1] -= src.w[1];
    dst->w[2] -= src.w[2];
    dst->w[3] -= src.w[3];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSUBW_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] -= src.w[0];
    dst->w[1] -= src.w[1];
    dst->w[2] -= src.w[2];
    dst->w[3] -= src.w[3];

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSUBD_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->l[0] -= src.l[0];
    dst->l[1] -= src.l[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSUBD_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->l[0] -= src.l[0];
    dst->l[1] -= src.l[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSUBSB_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->sb[0] = SSATB(dst->sb[0] - src.sb[0]);
    dst->sb[1] = SSATB(dst->sb[1] - src.sb[1]);
    dst->sb[2] = SSATB(dst->sb[2] - src.sb[2]);
    dst->sb[3] = SSATB(dst->sb[3] - src.sb[3]);
    dst->sb[4] = SSATB(dst->sb[4] - src.sb[4]);
    dst->sb[5] = SSATB(dst->sb[5] - src.sb[5]);
    dst->sb[6] = SSATB(dst->sb[6] - src.sb[6]);
    dst->sb[7] = SSATB(dst->sb[7] - src.sb[7]);

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSUBSB_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->sb[0] = SSATB(dst->sb[0] - src.sb[0]);
    dst->sb[1] = SSATB(dst->sb[1] - src.sb[1]);
    dst->sb[2] = SSATB(dst->sb[2] - src.sb[2]);
    dst->sb[3] = SSATB(dst->sb[3] - src.sb[3]);
    dst->sb[4] = SSATB(dst->sb[4] - src.sb[4]);
    dst->sb[5] = SSATB(dst->sb[5] - src.sb[5]);
    dst->sb[6] = SSATB(dst->sb[6] - src.sb[6]);
    dst->sb[7] = SSATB(dst->sb[7] - src.sb[7]);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSUBUSB_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] = USATB(dst->b[0] - src.b[0]);
    dst->b[1] = USATB(dst->b[1] - src.b[1]);
    dst->b[2] = USATB(dst->b[2] - src.b[2]);
    dst->b[3] = USATB(dst->b[3] - src.b[3]);
    dst->b[4] = USATB(dst->b[4] - src.b[4]);
    dst->b[5] = USATB(dst->b[5] - src.b[5]);
    dst->b[6] = USATB(dst->b[6] - src.b[6]);
    dst->b[7] = USATB(dst->b[7] - src.b[7]);

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSUBUSB_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] = USATB(dst->b[0] - src.b[0]);
    dst->b[1] = USATB(dst->b[1] - src.b[1]);
    dst->b[2] = USATB(dst->b[2] - src.b[2]);
    dst->b[3] = USATB(dst->b[3] - src.b[3]);
    dst->b[4] = USATB(dst->b[4] - src.b[4]);
    dst->b[5] = USATB(dst->b[5] - src.b[5]);
    dst->b[6] = USATB(dst->b[6] - src.b[6]);
    dst->b[7] = USATB(dst->b[7] - src.b[7]);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSUBSW_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->sw[0] = SSATW(dst->sw[0] - src.sw[0]);
    dst->sw[1] = SSATW(dst->sw[1] - src.sw[1]);
    dst->sw[2] = SSATW(dst->sw[2] - src.sw[2]);
    dst->sw[3] = SSATW(dst->sw[3] - src.sw[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSUBSW_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->sw[0] = SSATW(dst->sw[0] - src.sw[0]);
    dst->sw[1] = SSATW(dst->sw[1] - src.sw[1]);
    dst->sw[2] = SSATW(dst->sw[2] - src.sw[2]);
    dst->sw[3] = SSATW(dst->sw[3] - src.sw[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSUBUSW_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] = USATW(dst->w[0] - src.w[0]);
    dst->w[1] = USATW(dst->w[1] - src.w[1]);
    dst->w[2] = USATW(dst->w[2] - src.w[2]);
    dst->w[3] = USATW(dst->w[3] - src.w[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSUBUSW_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] = USATW(dst->w[0] - src.w[0]);
    dst->w[1] = USATW(dst->w[1] - src.w[1]);
    dst->w[2] = USATW(dst->w[2] - src.w[2]);
    dst->w[3] = USATW(dst->w[3] - src.w[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}

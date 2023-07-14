static int
opPADDB_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        dst.b[0] += src.b[0];
        dst.b[1] += src.b[1];
        dst.b[2] += src.b[2];
        dst.b[3] += src.b[3];
        dst.b[4] += src.b[4];
        dst.b[5] += src.b[5];
        dst.b[6] += src.b[6];
        dst.b[7] += src.b[7];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].b[0] += src.b[0];
        cpu_state.MM[cpu_reg].b[1] += src.b[1];
        cpu_state.MM[cpu_reg].b[2] += src.b[2];
        cpu_state.MM[cpu_reg].b[3] += src.b[3];
        cpu_state.MM[cpu_reg].b[4] += src.b[4];
        cpu_state.MM[cpu_reg].b[5] += src.b[5];
        cpu_state.MM[cpu_reg].b[6] += src.b[6];
        cpu_state.MM[cpu_reg].b[7] += src.b[7];
    }

    return 0;
}
static int
opPADDB_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        dst.b[0] += src.b[0];
        dst.b[1] += src.b[1];
        dst.b[2] += src.b[2];
        dst.b[3] += src.b[3];
        dst.b[4] += src.b[4];
        dst.b[5] += src.b[5];
        dst.b[6] += src.b[6];
        dst.b[7] += src.b[7];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].b[0] += src.b[0];
        cpu_state.MM[cpu_reg].b[1] += src.b[1];
        cpu_state.MM[cpu_reg].b[2] += src.b[2];
        cpu_state.MM[cpu_reg].b[3] += src.b[3];
        cpu_state.MM[cpu_reg].b[4] += src.b[4];
        cpu_state.MM[cpu_reg].b[5] += src.b[5];
        cpu_state.MM[cpu_reg].b[6] += src.b[6];
        cpu_state.MM[cpu_reg].b[7] += src.b[7];
    }

    return 0;
}

static int
opPADDW_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        dst.w[0] += src.w[0];
        dst.w[1] += src.w[1];
        dst.w[2] += src.w[2];
        dst.w[3] += src.w[3];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].w[0] += src.w[0];
        cpu_state.MM[cpu_reg].w[1] += src.w[1];
        cpu_state.MM[cpu_reg].w[2] += src.w[2];
        cpu_state.MM[cpu_reg].w[3] += src.w[3];
    }
    return 0;
}
static int
opPADDW_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        dst.w[0] += src.w[0];
        dst.w[1] += src.w[1];
        dst.w[2] += src.w[2];
        dst.w[3] += src.w[3];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].w[0] += src.w[0];
        cpu_state.MM[cpu_reg].w[1] += src.w[1];
        cpu_state.MM[cpu_reg].w[2] += src.w[2];
        cpu_state.MM[cpu_reg].w[3] += src.w[3];
    }
    return 0;
}

static int
opPADDD_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        dst.l[0] += src.l[0];
        dst.l[1] += src.l[1];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].l[0] += src.l[0];
        cpu_state.MM[cpu_reg].l[1] += src.l[1];
    }

    return 0;
}
static int
opPADDD_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        dst.l[0] += src.l[0];
        dst.l[1] += src.l[1];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].l[0] += src.l[0];
        cpu_state.MM[cpu_reg].l[1] += src.l[1];
    }

    return 0;
}

static int
opPADDSB_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.sb[0] = SSATB(dst.sb[0] + src.sb[0]);
        dst.sb[1] = SSATB(dst.sb[1] + src.sb[1]);
        dst.sb[2] = SSATB(dst.sb[2] + src.sb[2]);
        dst.sb[3] = SSATB(dst.sb[3] + src.sb[3]);
        dst.sb[4] = SSATB(dst.sb[4] + src.sb[4]);
        dst.sb[5] = SSATB(dst.sb[5] + src.sb[5]);
        dst.sb[6] = SSATB(dst.sb[6] + src.sb[6]);
        dst.sb[7] = SSATB(dst.sb[7] + src.sb[7]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].sb[0] = SSATB(cpu_state.MM[cpu_reg].sb[0] + src.sb[0]);
        cpu_state.MM[cpu_reg].sb[1] = SSATB(cpu_state.MM[cpu_reg].sb[1] + src.sb[1]);
        cpu_state.MM[cpu_reg].sb[2] = SSATB(cpu_state.MM[cpu_reg].sb[2] + src.sb[2]);
        cpu_state.MM[cpu_reg].sb[3] = SSATB(cpu_state.MM[cpu_reg].sb[3] + src.sb[3]);
        cpu_state.MM[cpu_reg].sb[4] = SSATB(cpu_state.MM[cpu_reg].sb[4] + src.sb[4]);
        cpu_state.MM[cpu_reg].sb[5] = SSATB(cpu_state.MM[cpu_reg].sb[5] + src.sb[5]);
        cpu_state.MM[cpu_reg].sb[6] = SSATB(cpu_state.MM[cpu_reg].sb[6] + src.sb[6]);
        cpu_state.MM[cpu_reg].sb[7] = SSATB(cpu_state.MM[cpu_reg].sb[7] + src.sb[7]);
    }

    return 0;
}
static int
opPADDSB_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.sb[0] = SSATB(dst.sb[0] + src.sb[0]);
        dst.sb[1] = SSATB(dst.sb[1] + src.sb[1]);
        dst.sb[2] = SSATB(dst.sb[2] + src.sb[2]);
        dst.sb[3] = SSATB(dst.sb[3] + src.sb[3]);
        dst.sb[4] = SSATB(dst.sb[4] + src.sb[4]);
        dst.sb[5] = SSATB(dst.sb[5] + src.sb[5]);
        dst.sb[6] = SSATB(dst.sb[6] + src.sb[6]);
        dst.sb[7] = SSATB(dst.sb[7] + src.sb[7]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].sb[0] = SSATB(cpu_state.MM[cpu_reg].sb[0] + src.sb[0]);
        cpu_state.MM[cpu_reg].sb[1] = SSATB(cpu_state.MM[cpu_reg].sb[1] + src.sb[1]);
        cpu_state.MM[cpu_reg].sb[2] = SSATB(cpu_state.MM[cpu_reg].sb[2] + src.sb[2]);
        cpu_state.MM[cpu_reg].sb[3] = SSATB(cpu_state.MM[cpu_reg].sb[3] + src.sb[3]);
        cpu_state.MM[cpu_reg].sb[4] = SSATB(cpu_state.MM[cpu_reg].sb[4] + src.sb[4]);
        cpu_state.MM[cpu_reg].sb[5] = SSATB(cpu_state.MM[cpu_reg].sb[5] + src.sb[5]);
        cpu_state.MM[cpu_reg].sb[6] = SSATB(cpu_state.MM[cpu_reg].sb[6] + src.sb[6]);
        cpu_state.MM[cpu_reg].sb[7] = SSATB(cpu_state.MM[cpu_reg].sb[7] + src.sb[7]);
    }

    return 0;
}

static int
opPADDUSB_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.b[0] = USATB(dst.b[0] + src.b[0]);
        dst.b[1] = USATB(dst.b[1] + src.b[1]);
        dst.b[2] = USATB(dst.b[2] + src.b[2]);
        dst.b[3] = USATB(dst.b[3] + src.b[3]);
        dst.b[4] = USATB(dst.b[4] + src.b[4]);
        dst.b[5] = USATB(dst.b[5] + src.b[5]);
        dst.b[6] = USATB(dst.b[6] + src.b[6]);
        dst.b[7] = USATB(dst.b[7] + src.b[7]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].b[0] = USATB(cpu_state.MM[cpu_reg].b[0] + src.b[0]);
        cpu_state.MM[cpu_reg].b[1] = USATB(cpu_state.MM[cpu_reg].b[1] + src.b[1]);
        cpu_state.MM[cpu_reg].b[2] = USATB(cpu_state.MM[cpu_reg].b[2] + src.b[2]);
        cpu_state.MM[cpu_reg].b[3] = USATB(cpu_state.MM[cpu_reg].b[3] + src.b[3]);
        cpu_state.MM[cpu_reg].b[4] = USATB(cpu_state.MM[cpu_reg].b[4] + src.b[4]);
        cpu_state.MM[cpu_reg].b[5] = USATB(cpu_state.MM[cpu_reg].b[5] + src.b[5]);
        cpu_state.MM[cpu_reg].b[6] = USATB(cpu_state.MM[cpu_reg].b[6] + src.b[6]);
        cpu_state.MM[cpu_reg].b[7] = USATB(cpu_state.MM[cpu_reg].b[7] + src.b[7]);
    }

    return 0;
}
static int
opPADDUSB_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.b[0] = USATB(dst.b[0] + src.b[0]);
        dst.b[1] = USATB(dst.b[1] + src.b[1]);
        dst.b[2] = USATB(dst.b[2] + src.b[2]);
        dst.b[3] = USATB(dst.b[3] + src.b[3]);
        dst.b[4] = USATB(dst.b[4] + src.b[4]);
        dst.b[5] = USATB(dst.b[5] + src.b[5]);
        dst.b[6] = USATB(dst.b[6] + src.b[6]);
        dst.b[7] = USATB(dst.b[7] + src.b[7]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].b[0] = USATB(cpu_state.MM[cpu_reg].b[0] + src.b[0]);
        cpu_state.MM[cpu_reg].b[1] = USATB(cpu_state.MM[cpu_reg].b[1] + src.b[1]);
        cpu_state.MM[cpu_reg].b[2] = USATB(cpu_state.MM[cpu_reg].b[2] + src.b[2]);
        cpu_state.MM[cpu_reg].b[3] = USATB(cpu_state.MM[cpu_reg].b[3] + src.b[3]);
        cpu_state.MM[cpu_reg].b[4] = USATB(cpu_state.MM[cpu_reg].b[4] + src.b[4]);
        cpu_state.MM[cpu_reg].b[5] = USATB(cpu_state.MM[cpu_reg].b[5] + src.b[5]);
        cpu_state.MM[cpu_reg].b[6] = USATB(cpu_state.MM[cpu_reg].b[6] + src.b[6]);
        cpu_state.MM[cpu_reg].b[7] = USATB(cpu_state.MM[cpu_reg].b[7] + src.b[7]);
    }

    return 0;
}

static int
opPADDSW_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.sw[0] = SSATW(dst.sw[0] + src.sw[0]);
        dst.sw[1] = SSATW(dst.sw[1] + src.sw[1]);
        dst.sw[2] = SSATW(dst.sw[2] + src.sw[2]);
        dst.sw[3] = SSATW(dst.sw[3] + src.sw[3]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].sw[0] = SSATW(cpu_state.MM[cpu_reg].sw[0] + src.sw[0]);
        cpu_state.MM[cpu_reg].sw[1] = SSATW(cpu_state.MM[cpu_reg].sw[1] + src.sw[1]);
        cpu_state.MM[cpu_reg].sw[2] = SSATW(cpu_state.MM[cpu_reg].sw[2] + src.sw[2]);
        cpu_state.MM[cpu_reg].sw[3] = SSATW(cpu_state.MM[cpu_reg].sw[3] + src.sw[3]);
    }

    return 0;
}
static int
opPADDSW_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.sw[0] = SSATW(dst.sw[0] + src.sw[0]);
        dst.sw[1] = SSATW(dst.sw[1] + src.sw[1]);
        dst.sw[2] = SSATW(dst.sw[2] + src.sw[2]);
        dst.sw[3] = SSATW(dst.sw[3] + src.sw[3]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].sw[0] = SSATW(cpu_state.MM[cpu_reg].sw[0] + src.sw[0]);
        cpu_state.MM[cpu_reg].sw[1] = SSATW(cpu_state.MM[cpu_reg].sw[1] + src.sw[1]);
        cpu_state.MM[cpu_reg].sw[2] = SSATW(cpu_state.MM[cpu_reg].sw[2] + src.sw[2]);
        cpu_state.MM[cpu_reg].sw[3] = SSATW(cpu_state.MM[cpu_reg].sw[3] + src.sw[3]);
    }

    return 0;
}

static int
opPADDUSW_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.w[0] = USATW(dst.w[0] + src.w[0]);
        dst.w[1] = USATW(dst.w[1] + src.w[1]);
        dst.w[2] = USATW(dst.w[2] + src.w[2]);
        dst.w[3] = USATW(dst.w[3] + src.w[3]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].w[0] = USATW(cpu_state.MM[cpu_reg].w[0] + src.w[0]);
        cpu_state.MM[cpu_reg].w[1] = USATW(cpu_state.MM[cpu_reg].w[1] + src.w[1]);
        cpu_state.MM[cpu_reg].w[2] = USATW(cpu_state.MM[cpu_reg].w[2] + src.w[2]);
        cpu_state.MM[cpu_reg].w[3] = USATW(cpu_state.MM[cpu_reg].w[3] + src.w[3]);
    }

    return 0;
}
static int
opPADDUSW_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.w[0] = USATW(dst.w[0] + src.w[0]);
        dst.w[1] = USATW(dst.w[1] + src.w[1]);
        dst.w[2] = USATW(dst.w[2] + src.w[2]);
        dst.w[3] = USATW(dst.w[3] + src.w[3]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].w[0] = USATW(cpu_state.MM[cpu_reg].w[0] + src.w[0]);
        cpu_state.MM[cpu_reg].w[1] = USATW(cpu_state.MM[cpu_reg].w[1] + src.w[1]);
        cpu_state.MM[cpu_reg].w[2] = USATW(cpu_state.MM[cpu_reg].w[2] + src.w[2]);
        cpu_state.MM[cpu_reg].w[3] = USATW(cpu_state.MM[cpu_reg].w[3] + src.w[3]);
    }

    return 0;
}

static int
opPMADDWD_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        if (dst.l[0] == 0x80008000 && src.l[0] == 0x80008000)
            dst.l[0] = 0x80000000;
        else
            dst.sl[0] = ((int32_t) dst.sw[0] * (int32_t) src.sw[0]) + ((int32_t) dst.sw[1] * (int32_t) src.sw[1]);

        if (dst.l[1] == 0x80008000 && src.l[1] == 0x80008000)
            dst.l[1] = 0x80000000;
        else
            dst.sl[1] = ((int32_t) dst.sw[2] * (int32_t) src.sw[2]) + ((int32_t) dst.sw[3] * (int32_t) src.sw[3]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        if (cpu_state.MM[cpu_reg].l[0] == 0x80008000 && src.l[0] == 0x80008000)
            cpu_state.MM[cpu_reg].l[0] = 0x80000000;
        else
            cpu_state.MM[cpu_reg].sl[0] = ((int32_t) cpu_state.MM[cpu_reg].sw[0] * (int32_t) src.sw[0]) + ((int32_t) cpu_state.MM[cpu_reg].sw[1] * (int32_t) src.sw[1]);

        if (cpu_state.MM[cpu_reg].l[1] == 0x80008000 && src.l[1] == 0x80008000)
            cpu_state.MM[cpu_reg].l[1] = 0x80000000;
        else
            cpu_state.MM[cpu_reg].sl[1] = ((int32_t) cpu_state.MM[cpu_reg].sw[2] * (int32_t) src.sw[2]) + ((int32_t) cpu_state.MM[cpu_reg].sw[3] * (int32_t) src.sw[3]);
    }

    return 0;
}
static int
opPMADDWD_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        if (dst.l[0] == 0x80008000 && src.l[0] == 0x80008000)
            dst.l[0] = 0x80000000;
        else
            dst.sl[0] = ((int32_t) dst.sw[0] * (int32_t) src.sw[0]) + ((int32_t) dst.sw[1] * (int32_t) src.sw[1]);

        if (dst.l[1] == 0x80008000 && src.l[1] == 0x80008000)
            dst.l[1] = 0x80000000;
        else
            dst.sl[1] = ((int32_t) dst.sw[2] * (int32_t) src.sw[2]) + ((int32_t) dst.sw[3] * (int32_t) src.sw[3]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        if (cpu_state.MM[cpu_reg].l[0] == 0x80008000 && src.l[0] == 0x80008000)
            cpu_state.MM[cpu_reg].l[0] = 0x80000000;
        else
            cpu_state.MM[cpu_reg].sl[0] = ((int32_t) cpu_state.MM[cpu_reg].sw[0] * (int32_t) src.sw[0]) + ((int32_t) cpu_state.MM[cpu_reg].sw[1] * (int32_t) src.sw[1]);

        if (cpu_state.MM[cpu_reg].l[1] == 0x80008000 && src.l[1] == 0x80008000)
            cpu_state.MM[cpu_reg].l[1] = 0x80000000;
        else
            cpu_state.MM[cpu_reg].sl[1] = ((int32_t) cpu_state.MM[cpu_reg].sw[2] * (int32_t) src.sw[2]) + ((int32_t) cpu_state.MM[cpu_reg].sw[3] * (int32_t) src.sw[3]);
    }

    return 0;
}

static int
opPMULLW_a16(uint32_t fetchdat)
{
    uint32_t p1, p2, p3, p4;
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat) {
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

        MMX_GETSRC();

        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        p1 = (uint32_t)(dst.w[0]) * (uint32_t)(src.w[0]);
        p2 = (uint32_t)(dst.w[1]) * (uint32_t)(src.w[1]);
        p3 = (uint32_t)(dst.w[2]) * (uint32_t)(src.w[2]);
        p4 = (uint32_t)(dst.w[3]) * (uint32_t)(src.w[3]);

        dst.w[0] = p1 & 0xffff;
        dst.w[1] = p2 & 0xffff;
        dst.w[2] = p3 & 0xffff;
        dst.w[3] = p4 & 0xffff;

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        if (cpu_mod == 3) {
            cpu_state.MM[cpu_reg].w[0] *= cpu_state.MM[cpu_rm].w[0];
            cpu_state.MM[cpu_reg].w[1] *= cpu_state.MM[cpu_rm].w[1];
            cpu_state.MM[cpu_reg].w[2] *= cpu_state.MM[cpu_rm].w[2];
            cpu_state.MM[cpu_reg].w[3] *= cpu_state.MM[cpu_rm].w[3];
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            src.l[0] = readmeml(easeg, cpu_state.eaaddr);
            src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4);
            if (cpu_state.abrt)
                return 0;
            cpu_state.MM[cpu_reg].w[0] *= src.w[0];
            cpu_state.MM[cpu_reg].w[1] *= src.w[1];
            cpu_state.MM[cpu_reg].w[2] *= src.w[2];
            cpu_state.MM[cpu_reg].w[3] *= src.w[3];
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}
static int
opPMULLW_a32(uint32_t fetchdat)
{
    uint32_t p1, p2, p3, p4;
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat) {
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

        MMX_GETSRC();

        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        p1 = (uint32_t)(dst.w[0]) * (uint32_t)(src.w[0]);
        p2 = (uint32_t)(dst.w[1]) * (uint32_t)(src.w[1]);
        p3 = (uint32_t)(dst.w[2]) * (uint32_t)(src.w[2]);
        p4 = (uint32_t)(dst.w[3]) * (uint32_t)(src.w[3]);

        dst.w[0] = p1 & 0xffff;
        dst.w[1] = p2 & 0xffff;
        dst.w[2] = p3 & 0xffff;
        dst.w[3] = p4 & 0xffff;

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        if (cpu_mod == 3) {
            cpu_state.MM[cpu_reg].w[0] *= cpu_state.MM[cpu_rm].w[0];
            cpu_state.MM[cpu_reg].w[1] *= cpu_state.MM[cpu_rm].w[1];
            cpu_state.MM[cpu_reg].w[2] *= cpu_state.MM[cpu_rm].w[2];
            cpu_state.MM[cpu_reg].w[3] *= cpu_state.MM[cpu_rm].w[3];
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            src.l[0] = readmeml(easeg, cpu_state.eaaddr);
            src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4);
            if (cpu_state.abrt)
                return 0;
            cpu_state.MM[cpu_reg].w[0] *= src.w[0];
            cpu_state.MM[cpu_reg].w[1] *= src.w[1];
            cpu_state.MM[cpu_reg].w[2] *= src.w[2];
            cpu_state.MM[cpu_reg].w[3] *= src.w[3];
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}

static int
opPMULHW_a16(uint32_t fetchdat)
{
    int32_t p1, p2, p3, p4;
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat) {
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

        MMX_GETSRC();

        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        p1 = (int32_t)(dst.w[0]) * (int32_t)(src.sw[0]);
        p2 = (int32_t)(dst.w[1]) * (int32_t)(src.sw[1]);
        p3 = (int32_t)(dst.w[2]) * (int32_t)(src.sw[2]);
        p4 = (int32_t)(dst.w[3]) * (int32_t)(src.sw[3]);

        dst.w[0] = (uint16_t)(p1 >> 16);
        dst.w[1] = (uint16_t)(p2 >> 16);
        dst.w[2] = (uint16_t)(p3 >> 16);
        dst.w[3] = (uint16_t)(p4 >> 16);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        if (cpu_mod == 3) {
            cpu_state.MM[cpu_reg].w[0] = ((int32_t) cpu_state.MM[cpu_reg].sw[0] * (int32_t) cpu_state.MM[cpu_rm].sw[0]) >> 16;
            cpu_state.MM[cpu_reg].w[1] = ((int32_t) cpu_state.MM[cpu_reg].sw[1] * (int32_t) cpu_state.MM[cpu_rm].sw[1]) >> 16;
            cpu_state.MM[cpu_reg].w[2] = ((int32_t) cpu_state.MM[cpu_reg].sw[2] * (int32_t) cpu_state.MM[cpu_rm].sw[2]) >> 16;
            cpu_state.MM[cpu_reg].w[3] = ((int32_t) cpu_state.MM[cpu_reg].sw[3] * (int32_t) cpu_state.MM[cpu_rm].sw[3]) >> 16;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            src.l[0] = readmeml(easeg, cpu_state.eaaddr);
            src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4);
            if (cpu_state.abrt)
                return 0;
            cpu_state.MM[cpu_reg].w[0] = ((int32_t) cpu_state.MM[cpu_reg].sw[0] * (int32_t) src.sw[0]) >> 16;
            cpu_state.MM[cpu_reg].w[1] = ((int32_t) cpu_state.MM[cpu_reg].sw[1] * (int32_t) src.sw[1]) >> 16;
            cpu_state.MM[cpu_reg].w[2] = ((int32_t) cpu_state.MM[cpu_reg].sw[2] * (int32_t) src.sw[2]) >> 16;
            cpu_state.MM[cpu_reg].w[3] = ((int32_t) cpu_state.MM[cpu_reg].sw[3] * (int32_t) src.sw[3]) >> 16;
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}
static int
opPMULHW_a32(uint32_t fetchdat)
{
    int32_t p1, p2, p3, p4;
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat) {
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

        MMX_GETSRC();

        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        p1 = (int32_t)(dst.w[0]) * (int32_t)(src.sw[0]);
        p2 = (int32_t)(dst.w[1]) * (int32_t)(src.sw[1]);
        p3 = (int32_t)(dst.w[2]) * (int32_t)(src.sw[2]);
        p4 = (int32_t)(dst.w[3]) * (int32_t)(src.sw[3]);

        dst.w[0] = (uint16_t)(p1 >> 16);
        dst.w[1] = (uint16_t)(p2 >> 16);
        dst.w[2] = (uint16_t)(p3 >> 16);
        dst.w[3] = (uint16_t)(p4 >> 16);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        if (cpu_mod == 3) {
            cpu_state.MM[cpu_reg].w[0] = ((int32_t) cpu_state.MM[cpu_reg].sw[0] * (int32_t) cpu_state.MM[cpu_rm].sw[0]) >> 16;
            cpu_state.MM[cpu_reg].w[1] = ((int32_t) cpu_state.MM[cpu_reg].sw[1] * (int32_t) cpu_state.MM[cpu_rm].sw[1]) >> 16;
            cpu_state.MM[cpu_reg].w[2] = ((int32_t) cpu_state.MM[cpu_reg].sw[2] * (int32_t) cpu_state.MM[cpu_rm].sw[2]) >> 16;
            cpu_state.MM[cpu_reg].w[3] = ((int32_t) cpu_state.MM[cpu_reg].sw[3] * (int32_t) cpu_state.MM[cpu_rm].sw[3]) >> 16;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            src.l[0] = readmeml(easeg, cpu_state.eaaddr);
            src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4);
            if (cpu_state.abrt)
                return 0;
            cpu_state.MM[cpu_reg].w[0] = ((int32_t) cpu_state.MM[cpu_reg].sw[0] * (int32_t) src.sw[0]) >> 16;
            cpu_state.MM[cpu_reg].w[1] = ((int32_t) cpu_state.MM[cpu_reg].sw[1] * (int32_t) src.sw[1]) >> 16;
            cpu_state.MM[cpu_reg].w[2] = ((int32_t) cpu_state.MM[cpu_reg].sw[2] * (int32_t) src.sw[2]) >> 16;
            cpu_state.MM[cpu_reg].w[3] = ((int32_t) cpu_state.MM[cpu_reg].sw[3] * (int32_t) src.sw[3]) >> 16;
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}

static int
opPSUBB_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.b[0] -= src.b[0];
        dst.b[1] -= src.b[1];
        dst.b[2] -= src.b[2];
        dst.b[3] -= src.b[3];
        dst.b[4] -= src.b[4];
        dst.b[5] -= src.b[5];
        dst.b[6] -= src.b[6];
        dst.b[7] -= src.b[7];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].b[0] -= src.b[0];
        cpu_state.MM[cpu_reg].b[1] -= src.b[1];
        cpu_state.MM[cpu_reg].b[2] -= src.b[2];
        cpu_state.MM[cpu_reg].b[3] -= src.b[3];
        cpu_state.MM[cpu_reg].b[4] -= src.b[4];
        cpu_state.MM[cpu_reg].b[5] -= src.b[5];
        cpu_state.MM[cpu_reg].b[6] -= src.b[6];
        cpu_state.MM[cpu_reg].b[7] -= src.b[7];
    }
    return 0;
}
static int
opPSUBB_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.b[0] -= src.b[0];
        dst.b[1] -= src.b[1];
        dst.b[2] -= src.b[2];
        dst.b[3] -= src.b[3];
        dst.b[4] -= src.b[4];
        dst.b[5] -= src.b[5];
        dst.b[6] -= src.b[6];
        dst.b[7] -= src.b[7];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].b[0] -= src.b[0];
        cpu_state.MM[cpu_reg].b[1] -= src.b[1];
        cpu_state.MM[cpu_reg].b[2] -= src.b[2];
        cpu_state.MM[cpu_reg].b[3] -= src.b[3];
        cpu_state.MM[cpu_reg].b[4] -= src.b[4];
        cpu_state.MM[cpu_reg].b[5] -= src.b[5];
        cpu_state.MM[cpu_reg].b[6] -= src.b[6];
        cpu_state.MM[cpu_reg].b[7] -= src.b[7];
    }
    return 0;
}

static int
opPSUBW_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.w[0] -= src.w[0];
        dst.w[1] -= src.w[1];
        dst.w[2] -= src.w[2];
        dst.w[3] -= src.w[3];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].w[0] -= src.w[0];
        cpu_state.MM[cpu_reg].w[1] -= src.w[1];
        cpu_state.MM[cpu_reg].w[2] -= src.w[2];
        cpu_state.MM[cpu_reg].w[3] -= src.w[3];
    }
    return 0;
}
static int
opPSUBW_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.w[0] -= src.w[0];
        dst.w[1] -= src.w[1];
        dst.w[2] -= src.w[2];
        dst.w[3] -= src.w[3];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].w[0] -= src.w[0];
        cpu_state.MM[cpu_reg].w[1] -= src.w[1];
        cpu_state.MM[cpu_reg].w[2] -= src.w[2];
        cpu_state.MM[cpu_reg].w[3] -= src.w[3];
    }
    return 0;
}

static int
opPSUBD_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.l[0] -= src.l[0];
        dst.l[1] -= src.l[1];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].l[0] -= src.l[0];
        cpu_state.MM[cpu_reg].l[1] -= src.l[1];
    }
    return 0;
}
static int
opPSUBD_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.l[0] -= src.l[0];
        dst.l[1] -= src.l[1];

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].l[0] -= src.l[0];
        cpu_state.MM[cpu_reg].l[1] -= src.l[1];
    }
    return 0;
}

static int
opPSUBSB_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.sb[0] = SSATB(dst.sb[0] - src.sb[0]);
        dst.sb[1] = SSATB(dst.sb[1] - src.sb[1]);
        dst.sb[2] = SSATB(dst.sb[2] - src.sb[2]);
        dst.sb[3] = SSATB(dst.sb[3] - src.sb[3]);
        dst.sb[4] = SSATB(dst.sb[4] - src.sb[4]);
        dst.sb[5] = SSATB(dst.sb[5] - src.sb[5]);
        dst.sb[6] = SSATB(dst.sb[6] - src.sb[6]);
        dst.sb[7] = SSATB(dst.sb[7] - src.sb[7]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].sb[0] = SSATB(cpu_state.MM[cpu_reg].sb[0] - src.sb[0]);
        cpu_state.MM[cpu_reg].sb[1] = SSATB(cpu_state.MM[cpu_reg].sb[1] - src.sb[1]);
        cpu_state.MM[cpu_reg].sb[2] = SSATB(cpu_state.MM[cpu_reg].sb[2] - src.sb[2]);
        cpu_state.MM[cpu_reg].sb[3] = SSATB(cpu_state.MM[cpu_reg].sb[3] - src.sb[3]);
        cpu_state.MM[cpu_reg].sb[4] = SSATB(cpu_state.MM[cpu_reg].sb[4] - src.sb[4]);
        cpu_state.MM[cpu_reg].sb[5] = SSATB(cpu_state.MM[cpu_reg].sb[5] - src.sb[5]);
        cpu_state.MM[cpu_reg].sb[6] = SSATB(cpu_state.MM[cpu_reg].sb[6] - src.sb[6]);
        cpu_state.MM[cpu_reg].sb[7] = SSATB(cpu_state.MM[cpu_reg].sb[7] - src.sb[7]);
    }
    return 0;
}
static int
opPSUBSB_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.sb[0] = SSATB(dst.sb[0] - src.sb[0]);
        dst.sb[1] = SSATB(dst.sb[1] - src.sb[1]);
        dst.sb[2] = SSATB(dst.sb[2] - src.sb[2]);
        dst.sb[3] = SSATB(dst.sb[3] - src.sb[3]);
        dst.sb[4] = SSATB(dst.sb[4] - src.sb[4]);
        dst.sb[5] = SSATB(dst.sb[5] - src.sb[5]);
        dst.sb[6] = SSATB(dst.sb[6] - src.sb[6]);
        dst.sb[7] = SSATB(dst.sb[7] - src.sb[7]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].sb[0] = SSATB(cpu_state.MM[cpu_reg].sb[0] - src.sb[0]);
        cpu_state.MM[cpu_reg].sb[1] = SSATB(cpu_state.MM[cpu_reg].sb[1] - src.sb[1]);
        cpu_state.MM[cpu_reg].sb[2] = SSATB(cpu_state.MM[cpu_reg].sb[2] - src.sb[2]);
        cpu_state.MM[cpu_reg].sb[3] = SSATB(cpu_state.MM[cpu_reg].sb[3] - src.sb[3]);
        cpu_state.MM[cpu_reg].sb[4] = SSATB(cpu_state.MM[cpu_reg].sb[4] - src.sb[4]);
        cpu_state.MM[cpu_reg].sb[5] = SSATB(cpu_state.MM[cpu_reg].sb[5] - src.sb[5]);
        cpu_state.MM[cpu_reg].sb[6] = SSATB(cpu_state.MM[cpu_reg].sb[6] - src.sb[6]);
        cpu_state.MM[cpu_reg].sb[7] = SSATB(cpu_state.MM[cpu_reg].sb[7] - src.sb[7]);
    }
    return 0;
}

static int
opPSUBUSB_a16(uint32_t fetchdat)
{
    MMX_REG src, dst, result;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
        result.q = 0;

        result.b[0] = USATB(dst.b[0] - src.b[0]);
        result.b[1] = USATB(dst.b[1] - src.b[1]);
        result.b[2] = USATB(dst.b[2] - src.b[2]);
        result.b[3] = USATB(dst.b[3] - src.b[3]);
        result.b[4] = USATB(dst.b[4] - src.b[4]);
        result.b[5] = USATB(dst.b[5] - src.b[5]);
        result.b[6] = USATB(dst.b[6] - src.b[6]);
        result.b[7] = USATB(dst.b[7] - src.b[7]);

        fpu_state.st_space[cpu_reg].fraction = result.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].b[0] = USATB(cpu_state.MM[cpu_reg].b[0] - src.b[0]);
        cpu_state.MM[cpu_reg].b[1] = USATB(cpu_state.MM[cpu_reg].b[1] - src.b[1]);
        cpu_state.MM[cpu_reg].b[2] = USATB(cpu_state.MM[cpu_reg].b[2] - src.b[2]);
        cpu_state.MM[cpu_reg].b[3] = USATB(cpu_state.MM[cpu_reg].b[3] - src.b[3]);
        cpu_state.MM[cpu_reg].b[4] = USATB(cpu_state.MM[cpu_reg].b[4] - src.b[4]);
        cpu_state.MM[cpu_reg].b[5] = USATB(cpu_state.MM[cpu_reg].b[5] - src.b[5]);
        cpu_state.MM[cpu_reg].b[6] = USATB(cpu_state.MM[cpu_reg].b[6] - src.b[6]);
        cpu_state.MM[cpu_reg].b[7] = USATB(cpu_state.MM[cpu_reg].b[7] - src.b[7]);
    }
    return 0;
}
static int
opPSUBUSB_a32(uint32_t fetchdat)
{
    MMX_REG src, dst, result;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
        result.q = 0;

        result.b[0] = USATB(dst.b[0] - src.b[0]);
        result.b[1] = USATB(dst.b[1] - src.b[1]);
        result.b[2] = USATB(dst.b[2] - src.b[2]);
        result.b[3] = USATB(dst.b[3] - src.b[3]);
        result.b[4] = USATB(dst.b[4] - src.b[4]);
        result.b[5] = USATB(dst.b[5] - src.b[5]);
        result.b[6] = USATB(dst.b[6] - src.b[6]);
        result.b[7] = USATB(dst.b[7] - src.b[7]);

        fpu_state.st_space[cpu_reg].fraction = result.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].b[0] = USATB(cpu_state.MM[cpu_reg].b[0] - src.b[0]);
        cpu_state.MM[cpu_reg].b[1] = USATB(cpu_state.MM[cpu_reg].b[1] - src.b[1]);
        cpu_state.MM[cpu_reg].b[2] = USATB(cpu_state.MM[cpu_reg].b[2] - src.b[2]);
        cpu_state.MM[cpu_reg].b[3] = USATB(cpu_state.MM[cpu_reg].b[3] - src.b[3]);
        cpu_state.MM[cpu_reg].b[4] = USATB(cpu_state.MM[cpu_reg].b[4] - src.b[4]);
        cpu_state.MM[cpu_reg].b[5] = USATB(cpu_state.MM[cpu_reg].b[5] - src.b[5]);
        cpu_state.MM[cpu_reg].b[6] = USATB(cpu_state.MM[cpu_reg].b[6] - src.b[6]);
        cpu_state.MM[cpu_reg].b[7] = USATB(cpu_state.MM[cpu_reg].b[7] - src.b[7]);
    }
    return 0;
}

static int
opPSUBSW_a16(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.sw[0] = SSATW(dst.sw[0] - src.sw[0]);
        dst.sw[1] = SSATW(dst.sw[1] - src.sw[1]);
        dst.sw[2] = SSATW(dst.sw[2] - src.sw[2]);
        dst.sw[3] = SSATW(dst.sw[3] - src.sw[3]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].sw[0] = SSATW(cpu_state.MM[cpu_reg].sw[0] - src.sw[0]);
        cpu_state.MM[cpu_reg].sw[1] = SSATW(cpu_state.MM[cpu_reg].sw[1] - src.sw[1]);
        cpu_state.MM[cpu_reg].sw[2] = SSATW(cpu_state.MM[cpu_reg].sw[2] - src.sw[2]);
        cpu_state.MM[cpu_reg].sw[3] = SSATW(cpu_state.MM[cpu_reg].sw[3] - src.sw[3]);
    }
    return 0;
}
static int
opPSUBSW_a32(uint32_t fetchdat)
{
    MMX_REG src, dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */

        dst.sw[0] = SSATW(dst.sw[0] - src.sw[0]);
        dst.sw[1] = SSATW(dst.sw[1] - src.sw[1]);
        dst.sw[2] = SSATW(dst.sw[2] - src.sw[2]);
        dst.sw[3] = SSATW(dst.sw[3] - src.sw[3]);

        fpu_state.st_space[cpu_reg].fraction = dst.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].sw[0] = SSATW(cpu_state.MM[cpu_reg].sw[0] - src.sw[0]);
        cpu_state.MM[cpu_reg].sw[1] = SSATW(cpu_state.MM[cpu_reg].sw[1] - src.sw[1]);
        cpu_state.MM[cpu_reg].sw[2] = SSATW(cpu_state.MM[cpu_reg].sw[2] - src.sw[2]);
        cpu_state.MM[cpu_reg].sw[3] = SSATW(cpu_state.MM[cpu_reg].sw[3] - src.sw[3]);
    }
    return 0;
}

static int
opPSUBUSW_a16(uint32_t fetchdat)
{
    MMX_REG src, dst, result;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
        result.q = 0;

        result.w[0] = USATW(dst.w[0] - src.w[0]);
        result.w[1] = USATW(dst.w[1] - src.w[1]);
        result.w[2] = USATW(dst.w[2] - src.w[2]);
        result.w[3] = USATW(dst.w[3] - src.w[3]);

        fpu_state.st_space[cpu_reg].fraction = result.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].w[0] = USATW(cpu_state.MM[cpu_reg].w[0] - src.w[0]);
        cpu_state.MM[cpu_reg].w[1] = USATW(cpu_state.MM[cpu_reg].w[1] - src.w[1]);
        cpu_state.MM[cpu_reg].w[2] = USATW(cpu_state.MM[cpu_reg].w[2] - src.w[2]);
        cpu_state.MM[cpu_reg].w[3] = USATW(cpu_state.MM[cpu_reg].w[3] - src.w[3]);
    }
    return 0;
}
static int
opPSUBUSW_a32(uint32_t fetchdat)
{
    MMX_REG src, dst, result;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat)
        dst = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;

    MMX_GETSRC();

    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
        result.q = 0;

        result.w[0] = USATW(dst.w[0] - src.w[0]);
        result.w[1] = USATW(dst.w[1] - src.w[1]);
        result.w[2] = USATW(dst.w[2] - src.w[2]);
        result.w[3] = USATW(dst.w[3] - src.w[3]);

        fpu_state.st_space[cpu_reg].fraction = result.q;
        fpu_state.st_space[cpu_reg].exp = 0xffff;
    } else {
        cpu_state.MM[cpu_reg].w[0] = USATW(cpu_state.MM[cpu_reg].w[0] - src.w[0]);
        cpu_state.MM[cpu_reg].w[1] = USATW(cpu_state.MM[cpu_reg].w[1] - src.w[1]);
        cpu_state.MM[cpu_reg].w[2] = USATW(cpu_state.MM[cpu_reg].w[2] - src.w[2]);
        cpu_state.MM[cpu_reg].w[3] = USATW(cpu_state.MM[cpu_reg].w[3] - src.w[3]);
    }
    return 0;
}

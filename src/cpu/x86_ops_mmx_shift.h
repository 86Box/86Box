#define MMX_GETSHIFT()                             \
    if (cpu_mod == 3) {                            \
        shift = (MMX_GETREG(cpu_rm)).b[0];         \
        CLOCK_CYCLES(1);                           \
    } else {                                       \
        SEG_CHECK_READ(cpu_state.ea_seg);          \
        shift = readmemb(easeg, cpu_state.eaaddr); \
        if (cpu_state.abrt)                        \
            return 0;                              \
        CLOCK_CYCLES(2);                           \
    }

static int
opPSxxW_imm(uint32_t fetchdat)
{
    int      reg   = fetchdat & 7;
    int      op    = fetchdat & 0x38;
    int      shift = (fetchdat >> 8) & 0xff;
    MMX_REG *dst;

    cpu_state.pc += 2;
    MMX_ENTER();
    dst = MMX_GETREGP(reg);

    switch (op) {
        case 0x10: /*PSRLW*/
            if (shift > 15)
                dst->q = 0;
            else {
                dst->w[0] >>= shift;
                dst->w[1] >>= shift;
                dst->w[2] >>= shift;
                dst->w[3] >>= shift;
            }
            break;
        case 0x20: /*PSRAW*/
            if (shift > 15)
                shift = 15;
            dst->sw[0] >>= shift;
            dst->sw[1] >>= shift;
            dst->sw[2] >>= shift;
            dst->sw[3] >>= shift;
            break;
        case 0x30: /*PSLLW*/
            if (shift > 15)
                dst->q = 0;
            else {
                dst->w[0] <<= shift;
                dst->w[1] <<= shift;
                dst->w[2] <<= shift;
                dst->w[3] <<= shift;
            }
            break;
        default:
            cpu_state.pc = cpu_state.oldpc;
            x86illegal();
            return 0;
    }

    MMX_SETEXP(reg);

    CLOCK_CYCLES(1);
    return 0;
}

static int
opPSLLW_a16(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 15)
        dst->q = 0;
    else {
        dst->w[0] <<= shift;
        dst->w[1] <<= shift;
        dst->w[2] <<= shift;
        dst->w[3] <<= shift;
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSLLW_a32(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 15)
        dst->q = 0;
    else {
        dst->w[0] <<= shift;
        dst->w[1] <<= shift;
        dst->w[2] <<= shift;
        dst->w[3] <<= shift;
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSRLW_a16(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 15)
        dst->q = 0;
    else {
        dst->w[0] >>= shift;
        dst->w[1] >>= shift;
        dst->w[2] >>= shift;
        dst->w[3] >>= shift;
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSRLW_a32(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 15)
        dst->q = 0;
    else {
        dst->w[0] >>= shift;
        dst->w[1] >>= shift;
        dst->w[2] >>= shift;
        dst->w[3] >>= shift;
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSRAW_a16(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 15)
        shift = 15;

    dst->sw[0] >>= shift;
    dst->sw[1] >>= shift;
    dst->sw[2] >>= shift;
    dst->sw[3] >>= shift;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSRAW_a32(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 15)
        shift = 15;

    dst->sw[0] >>= shift;
    dst->sw[1] >>= shift;
    dst->sw[2] >>= shift;
    dst->sw[3] >>= shift;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSxxD_imm(uint32_t fetchdat)
{
    int      reg   = fetchdat & 7;
    int      op    = fetchdat & 0x38;
    int      shift = (fetchdat >> 8) & 0xff;
    MMX_REG *dst;

    cpu_state.pc += 2;
    MMX_ENTER();

    dst = MMX_GETREGP(reg);

    switch (op) {
        case 0x10: /*PSRLD*/
            if (shift > 31)
                dst->q = 0;
            else {
                dst->l[0] >>= shift;
                dst->l[1] >>= shift;
            }
            break;
        case 0x20: /*PSRAD*/
            if (shift > 31)
                shift = 31;
            dst->sl[0] >>= shift;
            dst->sl[1] >>= shift;
            break;
        case 0x30: /*PSLLD*/
            if (shift > 31)
                dst->q = 0;
            else {
                dst->l[0] <<= shift;
                dst->l[1] <<= shift;
            }
            break;
        default:
            cpu_state.pc = cpu_state.oldpc;
            x86illegal();
            return 0;
    }

    MMX_SETEXP(reg);

    CLOCK_CYCLES(1);
    return 0;
}

static int
opPSLLD_a16(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 31)
        dst->q = 0;
    else {
        dst->l[0] <<= shift;
        dst->l[1] <<= shift;
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSLLD_a32(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 31)
        dst->q = 0;
    else {
        dst->l[0] <<= shift;
        dst->l[1] <<= shift;
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSRLD_a16(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 31)
        dst->q = 0;
    else {
        dst->l[0] >>= shift;
        dst->l[1] >>= shift;
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSRLD_a32(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 31)
        dst->q = 0;
    else {
        dst->l[0] >>= shift;
        dst->l[1] >>= shift;
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSRAD_a16(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 31)
        shift = 31;

    dst->sl[0] >>= shift;
    dst->sl[1] >>= shift;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSRAD_a32(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 31)
        shift = 31;

    dst->sl[0] >>= shift;
    dst->sl[1] >>= shift;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSxxQ_imm(uint32_t fetchdat)
{
    int      reg   = fetchdat & 7;
    int      op    = fetchdat & 0x38;
    int      shift = (fetchdat >> 8) & 0xff;
    MMX_REG *dst;

    cpu_state.pc += 2;

    MMX_ENTER();

    dst = MMX_GETREGP(reg);

    switch (op) {
        case 0x10: /*PSRLW*/
            if (shift > 63)
                dst->q = 0;
            else
                dst->q >>= shift;
            break;
        case 0x20: /*PSRAW*/
            if (shift > 63)
                shift = 63;

            dst->sq >>= shift;
            break;
        case 0x30: /*PSLLW*/
            if (shift > 63)
                dst->q = 0;
            else
                dst->q <<= shift;
            break;
        default:
            cpu_state.pc = cpu_state.oldpc;
            x86illegal();
            return 0;
    }

    MMX_SETEXP(reg);

    CLOCK_CYCLES(1);
    return 0;
}

static int
opPSLLQ_a16(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 63)
        dst->q = 0;
    else
        dst->q <<= shift;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSLLQ_a32(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 63)
        dst->q = 0;
    else
        dst->q <<= shift;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSRLQ_a16(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 63)
        dst->q = 0;
    else
        dst->q >>= shift;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSRLQ_a32(uint32_t fetchdat)
{
    MMX_REG *dst;
    int      shift;

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSHIFT();

    if (shift > 63)
        dst->q = 0;
    else
        dst->q >>= shift;

    MMX_SETEXP(cpu_reg);

    return 0;
}

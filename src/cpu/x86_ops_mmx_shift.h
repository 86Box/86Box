/* The count is the whole 64-bit operand: anything above 255 shifts as
   far as 255 does (all zeroes, or all sign bits for PSRA) */
#define MMX_GETSHIFT()                                           \
    {                                                            \
        uint64_t count_q;                                        \
        if (cpu_mod == 3) {                                      \
            count_q = (MMX_GETREG(cpu_rm)).q;                    \
            CLOCK_CYCLES(1);                                     \
        } else {                                                 \
            SEG_CHECK_READ(cpu_state.ea_seg);                    \
            CHECK_READ_EA(8);                                    \
            count_q = readmemq(easeg, cpu_state.eaaddr);         \
            if (cpu_state.abrt)                                  \
                return 1;                                        \
            CLOCK_CYCLES(2);                                     \
        }                                                        \
        shift = (count_q > 255) ? 255 : (int) count_q;           \
    }

static int
opPSxxW_imm(uint32_t fetchdat)
{
    int      reg   = fetchdat & 7;
    int      op    = fetchdat & 0x38;
    int      shift = (fetchdat >> 8) & 0xff;
    MMX_REG *dst;

    /* Validate before MMX_ENTER changes TOP/tags or checks pending #MF. */
    if ((fetchdat & 0xc0) != 0xc0 || (op != 0x10 && op != 0x20 && op != 0x30)) {
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 0;
    }

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

    /* Validate before MMX_ENTER changes TOP/tags or checks pending #MF. */
    if ((fetchdat & 0xc0) != 0xc0 || (op != 0x10 && op != 0x20 && op != 0x30)) {
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 0;
    }

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

    /* Only PSRLQ and PSLLQ exist; reject PSRAQ before entering MMX. */
    if ((fetchdat & 0xc0) != 0xc0 || (op != 0x10 && op != 0x30)) {
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 0;
    }

    cpu_state.pc += 2;

    MMX_ENTER();

    dst = MMX_GETREGP(reg);

    /* 0F 73 /4 would be PSRAQ, which doesn't exist: #UD with the rest. */
    switch (op) {
        case 0x10: /*PSRLQ*/
            if (shift > 63)
                dst->q = 0;
            else
                dst->q >>= shift;
            break;
        case 0x30: /*PSLLQ*/
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

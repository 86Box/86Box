#ifdef FPU_8087
static int
opFI(uint32_t fetchdat)
{
    FP_ENTER();
    cpu_state.pc++;
    cpu_state.npxc &= ~0x80;
    if (rmdat == 0xe1)
        cpu_state.npxc |= 0x80;
    wait(3, 0);
    return 0;
}
#else
static int
opFSTSW_AX(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    AX = cpu_state.npxs;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return 0;
}
#endif

static int
opFNOP(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fnop) : (x87_timings.fnop * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fnop) : (x87_concurrency.fnop * cpu_multi));
    return 0;
}

static int
opFXTRACT(UNUSED(uint32_t fetchdat))
{
    x87_conv_t test;
    int64_t    exp80;
    int64_t    exp80final;
    double     mant;

    FP_ENTER();
    cpu_state.pc++;
    test.eind.d = ST(0);
    exp80       = test.eind.ll & 0x7ff0000000000000LL;
    exp80final  = (exp80 >> 52) - BIAS64;
    mant        = test.eind.d / (pow(2.0, (double) exp80final));
    ST(0)       = (double) exp80final;
    FP_TAG_VALID;
    x87_push(mant);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fxtract) : (x87_timings.fxtract * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fxtract) : (x87_concurrency.fxtract * cpu_multi));
    return 0;
}

static int
opFCLEX(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    cpu_state.npxs &= 0xff00;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fnop) : (x87_timings.fnop * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fnop) : (x87_concurrency.fnop * cpu_multi));
    return 0;
}

static int
opFINIT(UNUSED(uint32_t fetchdat))
{
    uint64_t *p;
    FP_ENTER();
    cpu_state.pc++;
#ifdef FPU_8087
    cpu_state.npxc = 0x3FF;
#else
    cpu_state.npxc                                = 0x37F;
#endif
    codegen_set_rounding_mode(X87_ROUNDING_NEAREST);
#ifdef FPU_8087
    cpu_state.npxs &= 0x4700;
#else
    cpu_state.npxs = 0;
#endif
    p              = (uint64_t *) cpu_state.tag;
#ifdef USE_NEW_DYNAREC
    *p = 0;
#else
    *p                                            = 0x0303030303030303LL;
#endif
    cpu_state.TOP   = 0;
    cpu_state.ismmx = 0;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.finit) : (x87_timings.finit * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.finit) : (x87_concurrency.finit * cpu_multi));
    CPU_BLOCK_END();
    return 0;
}

static int
opFFREE(uint32_t fetchdat)
{
    FP_ENTER();
    cpu_state.pc++;
#ifdef USE_NEW_DYNAREC
    cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = TAG_EMPTY;
#else
    cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = 3;
#endif
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.ffree) : (x87_timings.ffree * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.ffree) : (x87_concurrency.ffree * cpu_multi));
    return 0;
}

static int
opFFREEP(uint32_t fetchdat)
{
    FP_ENTER();
    cpu_state.pc++;
    cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = 3;
    if (cpu_state.abrt)
        return 1;
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.ffree) : (x87_timings.ffree * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.ffree) : (x87_concurrency.ffree * cpu_multi));
    return 0;
}

static int
opFST(uint32_t fetchdat)
{
    FP_ENTER();
    cpu_state.pc++;
    ST(fetchdat & 7)                              = ST(0);
    cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = cpu_state.tag[cpu_state.TOP & 7];
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst) : (x87_timings.fst * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst) : (x87_concurrency.fst * cpu_multi));
    return 0;
}

static int
opFSTP(uint32_t fetchdat)
{
    FP_ENTER();
    cpu_state.pc++;
    ST(fetchdat & 7)                              = ST(0);
    cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = cpu_state.tag[cpu_state.TOP & 7];
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst) : (x87_timings.fst * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst) : (x87_concurrency.fst * cpu_multi));
    return 0;
}

static int
FSTOR(void)
{
    uint64_t *p;
    FP_ENTER();
    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000: /*16-bit real mode*/
        case 0x001: /*16-bit protected mode*/
            cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
            codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
            cpu_state.npxs = readmemw(easeg, cpu_state.eaaddr + 2);
            x87_settag(readmemw(easeg, cpu_state.eaaddr + 4));
            cpu_state.TOP = (cpu_state.npxs >> 11) & 7;
            cpu_state.eaaddr += 14;
            break;
        case 0x100: /*32-bit real mode*/
        case 0x101: /*32-bit protected mode*/
            cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
            codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
            cpu_state.npxs = readmemw(easeg, cpu_state.eaaddr + 4);
            x87_settag(readmemw(easeg, cpu_state.eaaddr + 8));
            cpu_state.TOP = (cpu_state.npxs >> 11) & 7;
            cpu_state.eaaddr += 28;
            break;
    }
    x87_ld_frstor(0);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(1);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(2);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(3);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(4);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(5);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(6);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(7);

    cpu_state.ismmx = 0;
    /*Horrible hack, but as PCem doesn't keep the FPU stack in 80-bit precision at all times
      something like this is needed*/
    p = (uint64_t *) cpu_state.tag;
#ifdef USE_NEW_DYNAREC
    if (cpu_state.MM_w4[0] == 0xffff && cpu_state.MM_w4[1] == 0xffff && cpu_state.MM_w4[2] == 0xffff && cpu_state.MM_w4[3] == 0xffff && cpu_state.MM_w4[4] == 0xffff && cpu_state.MM_w4[5] == 0xffff && cpu_state.MM_w4[6] == 0xffff && cpu_state.MM_w4[7] == 0xffff && !cpu_state.TOP && (*p == 0x0101010101010101ULL))
#else
    if (cpu_state.MM_w4[0] == 0xffff && cpu_state.MM_w4[1] == 0xffff && cpu_state.MM_w4[2] == 0xffff && cpu_state.MM_w4[3] == 0xffff && cpu_state.MM_w4[4] == 0xffff && cpu_state.MM_w4[5] == 0xffff && cpu_state.MM_w4[6] == 0xffff && cpu_state.MM_w4[7] == 0xffff && !cpu_state.TOP && !(*p))
#endif
        cpu_state.ismmx = 1;

    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.frstor) : (x87_timings.frstor * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.frstor) : (x87_concurrency.frstor * cpu_multi));
    return cpu_state.abrt;
}
static int
opFSTOR_a16(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    FSTOR();
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTOR_a32(uint32_t fetchdat)
{
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    FSTOR();
    return cpu_state.abrt;
}
#endif

static int
FSAVE(void)
{
    uint64_t *p;

    FP_ENTER();
    cpu_state.npxs = (cpu_state.npxs & ~(7 << 11)) | ((cpu_state.TOP & 7) << 11);

    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000: /*16-bit real mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 2, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 4, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 6, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 10, x87_op_off);
            cpu_state.eaaddr += 14;
            if (cpu_state.ismmx) {
                x87_stmmx(cpu_state.MM[0]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[1]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[2]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[3]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[4]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[5]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[6]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[7]);
            } else {
                x87_st_fsave(0);
                cpu_state.eaaddr += 10;
                x87_st_fsave(1);
                cpu_state.eaaddr += 10;
                x87_st_fsave(2);
                cpu_state.eaaddr += 10;
                x87_st_fsave(3);
                cpu_state.eaaddr += 10;
                x87_st_fsave(4);
                cpu_state.eaaddr += 10;
                x87_st_fsave(5);
                cpu_state.eaaddr += 10;
                x87_st_fsave(6);
                cpu_state.eaaddr += 10;
                x87_st_fsave(7);
            }
            break;
        case 0x001: /*16-bit protected mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 2, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 4, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 6, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 8, x87_pc_seg);
            writememw(easeg, cpu_state.eaaddr + 10, x87_op_off);
            writememw(easeg, cpu_state.eaaddr + 12, x87_op_seg);
            cpu_state.eaaddr += 14;
            if (cpu_state.ismmx) {
                x87_stmmx(cpu_state.MM[0]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[1]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[2]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[3]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[4]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[5]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[6]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[7]);
            } else {
                x87_st_fsave(0);
                cpu_state.eaaddr += 10;
                x87_st_fsave(1);
                cpu_state.eaaddr += 10;
                x87_st_fsave(2);
                cpu_state.eaaddr += 10;
                x87_st_fsave(3);
                cpu_state.eaaddr += 10;
                x87_st_fsave(4);
                cpu_state.eaaddr += 10;
                x87_st_fsave(5);
                cpu_state.eaaddr += 10;
                x87_st_fsave(6);
                cpu_state.eaaddr += 10;
                x87_st_fsave(7);
            }
            break;
        case 0x100: /*32-bit real mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 4, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 8, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 12, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 20, x87_op_off);
            writememl(easeg, cpu_state.eaaddr + 24, (x87_op_off >> 16) << 12);
            cpu_state.eaaddr += 28;
            if (cpu_state.ismmx) {
                x87_stmmx(cpu_state.MM[0]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[1]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[2]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[3]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[4]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[5]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[6]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[7]);
            } else {
                x87_st_fsave(0);
                cpu_state.eaaddr += 10;
                x87_st_fsave(1);
                cpu_state.eaaddr += 10;
                x87_st_fsave(2);
                cpu_state.eaaddr += 10;
                x87_st_fsave(3);
                cpu_state.eaaddr += 10;
                x87_st_fsave(4);
                cpu_state.eaaddr += 10;
                x87_st_fsave(5);
                cpu_state.eaaddr += 10;
                x87_st_fsave(6);
                cpu_state.eaaddr += 10;
                x87_st_fsave(7);
            }
            break;
        case 0x101: /*32-bit protected mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 4, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 8, x87_gettag());
            writememl(easeg, cpu_state.eaaddr + 12, x87_pc_off);
            writememl(easeg, cpu_state.eaaddr + 16, x87_pc_seg);
            writememl(easeg, cpu_state.eaaddr + 20, x87_op_off);
            writememl(easeg, cpu_state.eaaddr + 24, x87_op_seg);
            cpu_state.eaaddr += 28;
            if (cpu_state.ismmx) {
                x87_stmmx(cpu_state.MM[0]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[1]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[2]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[3]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[4]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[5]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[6]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[7]);
            } else {
                x87_st_fsave(0);
                cpu_state.eaaddr += 10;
                x87_st_fsave(1);
                cpu_state.eaaddr += 10;
                x87_st_fsave(2);
                cpu_state.eaaddr += 10;
                x87_st_fsave(3);
                cpu_state.eaaddr += 10;
                x87_st_fsave(4);
                cpu_state.eaaddr += 10;
                x87_st_fsave(5);
                cpu_state.eaaddr += 10;
                x87_st_fsave(6);
                cpu_state.eaaddr += 10;
                x87_st_fsave(7);
            }
            break;
    }

    cpu_state.npxc = 0x37F;
    codegen_set_rounding_mode(X87_ROUNDING_NEAREST);
#ifdef FPU_8087
    cpu_state.npxs &= 0x4700;
#else
    cpu_state.npxs = 0;
#endif
    p              = (uint64_t *) cpu_state.tag;
#ifdef USE_NEW_DYNAREC
    *p = 0;
#else
    *p = 0x0303030303030303LL;
#endif
    cpu_state.TOP   = 0;
    cpu_state.ismmx = 0;

    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsave) : (x87_timings.fsave * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsave) : (x87_concurrency.fsave * cpu_multi));
    return cpu_state.abrt;
}
static int
opFSAVE_a16(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    FSAVE();
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSAVE_a32(uint32_t fetchdat)
{
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    FSAVE();
    return cpu_state.abrt;
}
#endif

static int
opFSTSW_a16(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteaw((cpu_state.npxs & 0xC7FF) | ((cpu_state.TOP & 7) << 11));
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTSW_a32(uint32_t fetchdat)
{
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteaw((cpu_state.npxs & 0xC7FF) | ((cpu_state.TOP & 7) << 11));
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
opFLD(uint32_t fetchdat)
{
    int      old_tag;
    uint64_t old_i64;

    FP_ENTER();
    cpu_state.pc++;
    old_tag = cpu_state.tag[(cpu_state.TOP + fetchdat) & 7];
    old_i64 = cpu_state.MM[(cpu_state.TOP + fetchdat) & 7].q;
    x87_push(ST(fetchdat & 7));
    cpu_state.tag[cpu_state.TOP & 7]  = old_tag;
    cpu_state.MM[cpu_state.TOP & 7].q = old_i64;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld) : (x87_timings.fld * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld) : (x87_concurrency.fld * cpu_multi));
    return 0;
}

static int
opFXCH(uint32_t fetchdat)
{
    double   td;
    uint8_t  old_tag;
    uint64_t old_i64;
    FP_ENTER();
    cpu_state.pc++;
    td                                             = ST(0);
    ST(0)                                          = ST(fetchdat & 7);
    ST(fetchdat & 7)                               = td;
    old_tag                                        = cpu_state.tag[cpu_state.TOP & 7];
    cpu_state.tag[cpu_state.TOP & 7]               = cpu_state.tag[(cpu_state.TOP + fetchdat) & 7];
    cpu_state.tag[(cpu_state.TOP + fetchdat) & 7]  = old_tag;
    old_i64                                        = cpu_state.MM[cpu_state.TOP & 7].q;
    cpu_state.MM[cpu_state.TOP & 7].q              = cpu_state.MM[(cpu_state.TOP + fetchdat) & 7].q;
    cpu_state.MM[(cpu_state.TOP + fetchdat) & 7].q = old_i64;

    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fxch) : (x87_timings.fxch * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fxch) : (x87_concurrency.fxch * cpu_multi));
    return 0;
}

static int
opFCHS(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    ST(0) = -ST(0);
    FP_TAG_VALID;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fchs) : (x87_timings.fchs * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fchs) : (x87_concurrency.fchs * cpu_multi));
    return 0;
}

static int
opFABS(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    ST(0) = fabs(ST(0));
    FP_TAG_VALID;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fabs) : (x87_timings.fabs * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fabs) : (x87_concurrency.fabs * cpu_multi));
    return 0;
}

static int
opFTST(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    cpu_state.npxs &= ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3);
    if (ST(0) == 0.0)
        cpu_state.npxs |= FPU_SW_C3;
    else if (ST(0) < 0.0)
        cpu_state.npxs |= FPU_SW_C0;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.ftst) : (x87_timings.ftst * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.ftst) : (x87_concurrency.ftst * cpu_multi));
    return 0;
}

static int
opFXAM(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    cpu_state.npxs &= ~(FPU_SW_C0 | FPU_SW_C1 | FPU_SW_C2 | FPU_SW_C3);
#ifdef USE_NEW_DYNAREC
    if (cpu_state.tag[cpu_state.TOP & 7] == TAG_EMPTY)
        cpu_state.npxs |= (FPU_SW_C0 | FPU_SW_C3);
#else
    if (cpu_state.tag[cpu_state.TOP & 7] == 3)
        cpu_state.npxs |= (FPU_SW_C0 | FPU_SW_C3);
#endif
    else if (ST(0) == 0.0)
        cpu_state.npxs |= FPU_SW_C3;
    else
        cpu_state.npxs |= FPU_SW_C2;
    if (ST(0) < 0.0)
        cpu_state.npxs |= FPU_SW_C1;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fxam) : (x87_timings.fxam * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fxam) : (x87_concurrency.fxam * cpu_multi));
    return 0;
}

static int
opFLD1(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    x87_push(1.0);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_z1) : (x87_timings.fld_z1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_z1) : (x87_concurrency.fld_z1 * cpu_multi));
    return 0;
}

static int
opFLDL2T(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    x87_push(3.3219280948873623);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
opFLDL2E(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    x87_push(1.4426950408889634);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
opFLDPI(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    x87_push(3.141592653589793);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
opFLDEG2(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    x87_push(0.3010299956639812);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
opFLDLN2(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    x87_push_u64(0x3fe62e42fefa39f0ULL);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
opFLDZ(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    x87_push(0.0);
    FP_TAG_VALID;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_z1) : (x87_timings.fld_z1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_z1) : (x87_concurrency.fld_z1 * cpu_multi));
    return 0;
}

static int
opF2XM1(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    ST(0) = pow(2.0, ST(0)) - 1.0;
    FP_TAG_VALID;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.f2xm1) : (x87_timings.f2xm1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.f2xm1) : (x87_concurrency.f2xm1 * cpu_multi));
    return 0;
}

static int
opFYL2X(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    ST(1) = ST(1) * (log(ST(0)) / log(2.0));
    FP_TAG_VALID_N;
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fyl2x) : (x87_timings.fyl2x * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fyl2x) : (x87_concurrency.fyl2x * cpu_multi));
    return 0;
}

static int
opFYL2XP1(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    ST(1) = ST(1) * (log1p(ST(0)) / log(2.0));
    FP_TAG_VALID_N;
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fyl2xp1) : (x87_timings.fyl2xp1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fyl2xp1) : (x87_concurrency.fyl2xp1 * cpu_multi));
    return 0;
}

static int
opFPTAN(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    ST(0) = tan(ST(0));
    FP_TAG_VALID;
    x87_push(1.0);
    cpu_state.npxs &= ~FPU_SW_C2;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fptan) : (x87_timings.fptan * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fptan) : (x87_concurrency.fptan * cpu_multi));
    return 0;
}

static int
opFPATAN(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    ST(1) = atan2(ST(1), ST(0));
    FP_TAG_VALID_N;
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fpatan) : (x87_timings.fpatan * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fpatan) : (x87_concurrency.fpatan * cpu_multi));
    return 0;
}

static int
opFDECSTP(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
#ifdef USE_NEW_DYNAREC
    cpu_state.TOP--;
#else
    cpu_state.TOP = (cpu_state.TOP - 1) & 7;
#endif
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fincdecstp) : (x87_timings.fincdecstp * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fincdecstp) : (x87_concurrency.fincdecstp * cpu_multi));
    return 0;
}

static int
opFINCSTP(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
#ifdef USE_NEW_DYNAREC
    cpu_state.TOP++;
#else
    cpu_state.TOP = (cpu_state.TOP + 1) & 7;
#endif
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fincdecstp) : (x87_timings.fincdecstp * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fincdecstp) : (x87_concurrency.fincdecstp * cpu_multi));
    return 0;
}

static int
opFPREM(UNUSED(uint32_t fetchdat))
{
    int64_t temp64;
    FP_ENTER();
    cpu_state.pc++;
    temp64 = (int64_t) (ST(0) / ST(1));
    ST(0)  = ST(0) - (ST(1) * (double) temp64);
    FP_TAG_VALID;
    cpu_state.npxs &= ~(FPU_SW_C0 | FPU_SW_C1 | FPU_SW_C2 | FPU_SW_C3);
    if (temp64 & 4)
        cpu_state.npxs |= FPU_SW_C0;
    if (temp64 & 2)
        cpu_state.npxs |= FPU_SW_C3;
    if (temp64 & 1)
        cpu_state.npxs |= FPU_SW_C1;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fprem) : (x87_timings.fprem * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fprem) : (x87_concurrency.fprem * cpu_multi));
    return 0;
}

static int
opFPREM1(UNUSED(uint32_t fetchdat))
{
    int64_t temp64;
    FP_ENTER();
    cpu_state.pc++;
    temp64 = (int64_t) (ST(0) / ST(1));
    ST(0)  = ST(0) - (ST(1) * (double) temp64);
    FP_TAG_VALID;
    cpu_state.npxs &= ~(FPU_SW_C0 | FPU_SW_C1 | FPU_SW_C2 | FPU_SW_C3);
    if (temp64 & 4)
        cpu_state.npxs |= FPU_SW_C0;
    if (temp64 & 2)
        cpu_state.npxs |= FPU_SW_C3;
    if (temp64 & 1)
        cpu_state.npxs |= FPU_SW_C1;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fprem1) : (x87_timings.fprem1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fprem1) : (x87_concurrency.fprem1 * cpu_multi));
    return 0;
}

static int
opFSQRT(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    ST(0) = sqrt(ST(0));
    FP_TAG_VALID;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsqrt) : (x87_timings.fsqrt * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsqrt) : (x87_concurrency.fsqrt * cpu_multi));
    return 0;
}

#ifndef FPU_8087
static int
opFSINCOS(UNUSED(uint32_t fetchdat))
{
    double td;
    FP_ENTER();
    cpu_state.pc++;
    td    = ST(0);
    ST(0) = sin(td);
    FP_TAG_VALID;
    x87_push(cos(td));
    cpu_state.npxs &= ~FPU_SW_C2;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsincos) : (x87_timings.fsincos * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsincos) : (x87_concurrency.fsincos * cpu_multi));
    return 0;
}
#endif

static int
opFRNDINT(UNUSED(uint32_t fetchdat))
{
    double dst0;

    FP_ENTER();
    cpu_state.pc++;
    dst0 = x87_fround(ST(0));
    ST(0) = (double) dst0;
    FP_TAG_VALID;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.frndint) : (x87_timings.frndint * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.frndint) : (x87_concurrency.frndint * cpu_multi));
    return 0;
}

static int
opFSCALE(UNUSED(uint32_t fetchdat))
{
    int64_t temp64;
    FP_ENTER();
    cpu_state.pc++;
    temp64 = (int64_t) ST(1);
    if (ST(0) != 0.0)
        ST(0) = ST(0) * pow(2.0, (double) temp64);
    FP_TAG_VALID;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fscale) : (x87_timings.fscale * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fscale) : (x87_concurrency.fscale * cpu_multi));
    return 0;
}

#ifndef FPU_8087
static int
opFSIN(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    ST(0) = sin(ST(0));
    FP_TAG_VALID;
    cpu_state.npxs &= ~FPU_SW_C2;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsin_cos) : (x87_timings.fsin_cos * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsin_cos) : (x87_concurrency.fsin_cos * cpu_multi));
    return 0;
}

static int
opFCOS(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    ST(0) = cos(ST(0));
    FP_TAG_VALID;
    cpu_state.npxs &= ~FPU_SW_C2;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsin_cos) : (x87_timings.fsin_cos * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsin_cos) : (x87_concurrency.fsin_cos * cpu_multi));
    return 0;
}
#endif

static int
FLDENV(void)
{
    FP_ENTER();
    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000: /*16-bit real mode*/
        case 0x001: /*16-bit protected mode*/
            cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
            codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
            cpu_state.npxs = readmemw(easeg, cpu_state.eaaddr + 2);
            x87_settag(readmemw(easeg, cpu_state.eaaddr + 4));
            cpu_state.TOP = (cpu_state.npxs >> 11) & 7;
            break;
        case 0x100: /*32-bit real mode*/
        case 0x101: /*32-bit protected mode*/
            cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
            codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
            cpu_state.npxs = readmemw(easeg, cpu_state.eaaddr + 4);
            x87_settag(readmemw(easeg, cpu_state.eaaddr + 8));
            cpu_state.TOP = (cpu_state.npxs >> 11) & 7;
            break;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldenv) : (x87_timings.fldenv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldenv) : (x87_concurrency.fldenv * cpu_multi));
    return cpu_state.abrt;
}

static int
opFLDENV_a16(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    FLDENV();
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFLDENV_a32(uint32_t fetchdat)
{
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    FLDENV();
    return cpu_state.abrt;
}
#endif

static int
opFLDCW_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t tempw;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    tempw = geteaw();
    if (cpu_state.abrt)
        return 1;
    cpu_state.npxc = tempw;
    codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldcw) : (x87_timings.fldcw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldcw) : (x87_concurrency.fldcw * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFLDCW_a32(uint32_t fetchdat)
{
    uint16_t tempw;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    tempw = geteaw();
    if (cpu_state.abrt)
        return 1;
    cpu_state.npxc = tempw;
    codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldcw) : (x87_timings.fldcw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldcw) : (x87_concurrency.fldcw * cpu_multi));
    return 0;
}
#endif

static int
FSTENV(void)
{
    FP_ENTER();
    cpu_state.npxs = (cpu_state.npxs & ~(7 << 11)) | ((cpu_state.TOP & 7) << 11);

    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000: /*16-bit real mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 2, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 4, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 6, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 10, x87_op_off);
            break;
        case 0x001: /*16-bit protected mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 2, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 4, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 6, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 8, x87_pc_seg);
            writememw(easeg, cpu_state.eaaddr + 10, x87_op_off);
            writememw(easeg, cpu_state.eaaddr + 12, x87_op_seg);
            break;
        case 0x100: /*32-bit real mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 4, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 8, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 12, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 20, x87_op_off);
            writememl(easeg, cpu_state.eaaddr + 24, (x87_op_off >> 16) << 12);
            break;
        case 0x101: /*32-bit protected mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 4, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 8, x87_gettag());
            writememl(easeg, cpu_state.eaaddr + 12, x87_pc_off);
            writememl(easeg, cpu_state.eaaddr + 16, x87_pc_seg);
            writememl(easeg, cpu_state.eaaddr + 20, x87_op_off);
            writememl(easeg, cpu_state.eaaddr + 24, x87_op_seg);
            break;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstenv) : (x87_timings.fstenv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstenv) : (x87_concurrency.fstenv * cpu_multi));
    return cpu_state.abrt;
}

static int
opFSTENV_a16(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    FSTENV();
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTENV_a32(uint32_t fetchdat)
{
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    FSTENV();
    return cpu_state.abrt;
}
#endif

static int
opFSTCW_a16(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteaw(cpu_state.npxc);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstenv) : (x87_concurrency.fstenv * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTCW_a32(uint32_t fetchdat)
{
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteaw(cpu_state.npxc);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return cpu_state.abrt;
}
#endif

#ifndef FPU_8087
#    ifndef OPS_286_386
#        define opFCMOV(condition)                                                                      \
            static int opFCMOV##condition(uint32_t fetchdat)                                            \
            {                                                                                           \
                FP_ENTER();                                                                             \
                cpu_state.pc++;                                                                         \
                if (cond_##condition) {                                                                 \
                    cpu_state.tag[cpu_state.TOP & 7]  = cpu_state.tag[(cpu_state.TOP + fetchdat) & 7];  \
                    cpu_state.MM[cpu_state.TOP & 7].q = cpu_state.MM[(cpu_state.TOP + fetchdat) & 7].q; \
                    ST(0)                             = ST(fetchdat & 7);                               \
                }                                                                                       \
                CLOCK_CYCLES_FPU(4);                                                                    \
                return 0;                                                                               \
            }

#        define cond_U  (PF_SET())
#        define cond_NU (!PF_SET())

// clang-format off
opFCMOV(B)
opFCMOV(E)
opFCMOV(BE)
opFCMOV(U)
opFCMOV(NB)
opFCMOV(NE)
opFCMOV(NBE)
opFCMOV(NU)
// clang-format on
#    endif
#endif

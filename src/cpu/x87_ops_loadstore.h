/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          x87 FPU instructions core.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */

/* A store from an empty register: the stack fault (IE and SF, C1 clear for
   an underflow). Masked, the format's indefinite is written, and the status
   changed only once it is ("A program-state change does not normally
   accompany a page-fault exception", SDM 1999 vol. 3); unmasked, nothing is
   written or popped and ES is set. 0 means the caller has a valid source
   and should perform its ordinary conversion. */
static int
x87_empty_store(int size, int kind, int pop)
{
#ifdef FPU_8087
    return 0;
#else
    if (!X87_ST_EMPTY(0))
        return 0;
    if (!(cpu_state.npxc & FPU_SW_Invalid)) {
        const int was_es = cpu_state.npxs & FPU_SW_Summary;

        cpu_state.npxs = (cpu_state.npxs & ~FPU_SW_C1) | 0x41 | 0x8080;
        x87_ferr_raised(was_es, FPU_SW_Invalid, x87_op);
        return 1;
    }
    CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, (uint64_t) cpu_state.eaaddr + size - 1);
    if (size == 2) {
        seteaw(0x8000);
    } else if (size == 4) {
        seteal(kind ? 0x80000000 : 0xffc00000);
    } else if (size == 8) {
        seteaq(kind ? UINT64_C(0x8000000000000000) : UINT64_C(0xfff8000000000000));
    } else {
        writememq(easeg, cpu_state.eaaddr, UINT64_C(0xc000000000000000));
        if (!cpu_state.abrt) {
            writememw(easeg, cpu_state.eaaddr + 8, 0xffff);
        }
    }
    if (!cpu_state.abrt) {
        cpu_state.npxs = (cpu_state.npxs & ~FPU_SW_C1) | 0x41;
        if (pop)
            x87_pop();
    }
    return 1;
#endif
}

static int
opFILDiw_a16(UNUSED(uint32_t fetchdat))
{
    int16_t temp;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    temp = geteaw();
    if (cpu_state.abrt)
        return 1;
    x87_push((double) temp);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_16) : (x87_timings.fild_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_16) : (x87_concurrency.fild_16 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFILDiw_a32(uint32_t fetchdat)
{
    int16_t temp;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    temp = geteaw();
    if (cpu_state.abrt)
        return 1;
    x87_push((double) temp);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_16) : (x87_timings.fild_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_16) : (x87_concurrency.fild_16 * cpu_multi));
    return 0;
}
#endif

static int
opFISTiw_a16(UNUSED(uint32_t fetchdat))
{
    int invalid = 0;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(2, 1, 0))
        return cpu_state.abrt;
    invalid = x87_fist_invalid(ST(0), -32768.0, 32768.0);
    if (invalid < 0)
        return 0;
    seteaw(x87_fround16(ST(0)));
    if (!cpu_state.abrt)
        x87_store_done(invalid);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFISTiw_a32(uint32_t fetchdat)
{
    int invalid = 0;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(2, 1, 0))
        return cpu_state.abrt;
    invalid = x87_fist_invalid(ST(0), -32768.0, 32768.0);
    if (invalid < 0)
        return 0;
    seteaw(x87_fround16(ST(0)));
    if (!cpu_state.abrt)
        x87_store_done(invalid);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
opFISTPiw_a16(UNUSED(uint32_t fetchdat))
{
    int invalid = 0;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(2, 1, 1))
        return cpu_state.abrt;
    invalid = x87_fist_invalid(ST(0), -32768.0, 32768.0);
    if (invalid < 0)
        return 0;
    seteaw(x87_fround16(ST(0)));
    if (cpu_state.abrt)
        return 1;
    x87_store_done(invalid);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFISTPiw_a32(uint32_t fetchdat)
{
    int invalid = 0;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(2, 1, 1))
        return cpu_state.abrt;
    invalid = x87_fist_invalid(ST(0), -32768.0, 32768.0);
    if (invalid < 0)
        return 0;
    seteaw(x87_fround16(ST(0)));
    if (cpu_state.abrt)
        return 1;
    x87_store_done(invalid);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return 0;
}
#endif

static int
opFILDiq_a16(UNUSED(uint32_t fetchdat))
{
    int64_t temp64;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    temp64 = geteaq();
    if (cpu_state.abrt)
        return 1;
    x87_push_int64(temp64);

    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_64) : (x87_timings.fild_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_64) : (x87_concurrency.fild_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFILDiq_a32(uint32_t fetchdat)
{
    int64_t temp64;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    temp64 = geteaq();
    if (cpu_state.abrt)
        return 1;
    x87_push_int64(temp64);

    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_64) : (x87_timings.fild_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_64) : (x87_concurrency.fild_64 * cpu_multi));
    return 0;
}
#endif

static int
FBLD_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t              load_reg_hi = 0xffff;
    uint64_t              load_reg_lo = BX_CONST64(0xC000000000000000);
    int64_t               load_val = 0ULL;
    uint64_t              power;
    int                   sign;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ_EA(10);
    load_reg_lo = readmemq(easeg, cpu_state.eaaddr);
    load_reg_hi = readmemw(easeg, cpu_state.eaaddr + 8);
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    sign     = (load_reg_hi & 0x8000) != 0;
    load_val = 0ULL;
    power    = 1;
    for (int i = 0; i < 16; i++) {
        load_val += ((uint64_t) (load_reg_lo & 0xf)) * power;
        load_reg_lo >>= 4;
        power *= 10;
    }
    for (int i = 0; i < 2; i++) {
        load_val += ((uint64_t) (load_reg_hi & 0xf)) * power;
        load_reg_hi >>= 4;
        power *= 10;
    }
    if (sign && !load_val) {
        /* The sign is kept, -0 included */
        x87_push(-0.0);
    } else {
        if (sign)
            load_val = -load_val;
        x87_push_int64(load_val);
    }

    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_64) : (x87_timings.fild_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_64) : (x87_concurrency.fild_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
FBLD_a32(uint32_t fetchdat)
{
    uint16_t              load_reg_hi = 0xffff;
    uint64_t              load_reg_lo = BX_CONST64(0xC000000000000000);
    int64_t               load_val = 0ULL;
    uint64_t              power;
    int                   sign;

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ_EA(10);
    load_reg_lo = readmemq(easeg, cpu_state.eaaddr);
    load_reg_hi = readmemw(easeg, cpu_state.eaaddr + 8);
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    sign     = (load_reg_hi & 0x8000) != 0;
    load_val = 0ULL;
    power    = 1;
    for (int i = 0; i < 16; i++) {
        load_val += ((uint64_t) (load_reg_lo & 0xf)) * power;
        load_reg_lo >>= 4;
        power *= 10;
    }
    for (int i = 0; i < 2; i++) {
        load_val += ((uint64_t) (load_reg_hi & 0xf)) * power;
        load_reg_hi >>= 4;
        power *= 10;
    }
    if (sign && !load_val) {
        /* The sign is kept, -0 included */
        x87_push(-0.0);
    } else {
        if (sign)
            load_val = -load_val;
        x87_push_int64(load_val);
    }

    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_64) : (x87_timings.fild_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_64) : (x87_concurrency.fild_64 * cpu_multi));
    return 0;
}
#endif

/* FBSTP: ST(0) rounded per RC to 18 packed BCD digits and the sign; a
   NaN, an infinity or a value past 18 digits stores the BCD indefinite,
   FFFF C000000000000000h */
static void
x87_fbstp_write(void)
{
    double   r   = x87_fround_rc(ST(0));
    uint64_t v;
    uint8_t  bytes[10];

    if (cpu_state.tag[cpu_state.TOP & 7] & TAG_UINT64) {
        int64_t q = (int64_t) cpu_state.MM[cpu_state.TOP & 7].q;
        r         = (double) q;
        v         = (q < 0) ? (0 - (uint64_t) q) : (uint64_t) q;
    } else
        v = (isnan(r) || (fabs(r) > 999999999999999999.0)) ? 0 : (uint64_t) fabs(r);

    if (isnan(r) || (v > 999999999999999999ULL) || (fabs(r) > 999999999999999999.0)) {
        for (int c = 0; c < 7; c++)
            bytes[c] = 0x00;
        bytes[7] = 0xc0;
        bytes[8] = 0xff;
        bytes[9] = 0xff;
    } else {
        for (int c = 0; c < 9; c++) {
            bytes[c] = v % 10;
            v /= 10;
            bytes[c] |= (v % 10) << 4;
            v /= 10;
        }
        bytes[9] = signbit(ST(0)) ? 0x80 : 0x00;
    }
    for (int c = 0; c < 10; c++)
        writememb(easeg, cpu_state.eaaddr + c, bytes[c]);
}

static int
FBSTP_a16(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_EA(10);
    if (x87_empty_store(10, 2, 1))
        return cpu_state.abrt;
    x87_fbstp_write();
    if (cpu_state.abrt)
        return 1;
    x87_store_done(0);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fbstp) : (x87_timings.fbstp * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fbstp) : (x87_concurrency.fbstp * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
FBSTP_a32(uint32_t fetchdat)
{
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_EA(10);
    if (x87_empty_store(10, 2, 1))
        return cpu_state.abrt;
    x87_fbstp_write();
    if (cpu_state.abrt)
        return 1;
    x87_store_done(0);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fbstp) : (x87_timings.fbstp * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fbstp) : (x87_concurrency.fbstp * cpu_multi));
    return 0;
}
#endif

static int
FISTPiq_a16(UNUSED(uint32_t fetchdat))
{
    int invalid = 0;
    int64_t temp64;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(8, 1, 1))
        return cpu_state.abrt;
    if (cpu_state.tag[cpu_state.TOP & 7] & TAG_UINT64)
        temp64 = cpu_state.MM[cpu_state.TOP & 7].q;
    else {
        invalid = x87_fist_invalid(ST(0), -9223372036854775808.0, 9223372036854775808.0);
        if (invalid < 0)
            return 0;
        temp64 = x87_fround(ST(0));
    }
    seteaq(temp64);
    if (cpu_state.abrt)
        return 1;
    x87_store_done(invalid);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_64) : (x87_timings.fist_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_64) : (x87_concurrency.fist_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
FISTPiq_a32(uint32_t fetchdat)
{
    int invalid = 0;
    int64_t temp64;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(8, 1, 1))
        return cpu_state.abrt;
    if (cpu_state.tag[cpu_state.TOP & 7] & TAG_UINT64)
        temp64 = cpu_state.MM[cpu_state.TOP & 7].q;
    else {
        invalid = x87_fist_invalid(ST(0), -9223372036854775808.0, 9223372036854775808.0);
        if (invalid < 0)
            return 0;
        temp64 = x87_fround(ST(0));
    }
    seteaq(temp64);
    if (cpu_state.abrt)
        return 1;
    x87_store_done(invalid);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_64) : (x87_timings.fist_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_64) : (x87_concurrency.fist_64 * cpu_multi));
    return 0;
}
#endif

static int
opFILDil_a16(UNUSED(uint32_t fetchdat))
{
    int32_t templ;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    templ = geteal();
    if (cpu_state.abrt)
        return 1;
    x87_push((double) templ);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_32) : (x87_timings.fild_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_32) : (x87_concurrency.fild_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFILDil_a32(uint32_t fetchdat)
{
    int32_t templ;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    templ = geteal();
    if (cpu_state.abrt)
        return 1;
    x87_push((double) templ);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_32) : (x87_timings.fild_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_32) : (x87_concurrency.fild_32 * cpu_multi));
    return 0;
}
#endif

static int
opFISTil_a16(UNUSED(uint32_t fetchdat))
{
    int invalid = 0;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(4, 1, 0))
        return cpu_state.abrt;
    invalid = x87_fist_invalid(ST(0), -2147483648.0, 2147483648.0);
    if (invalid < 0)
        return 0;
    seteal(x87_fround32(ST(0)));
    if (!cpu_state.abrt)
        x87_store_done(invalid);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFISTil_a32(uint32_t fetchdat)
{
    int invalid = 0;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(4, 1, 0))
        return cpu_state.abrt;
    invalid = x87_fist_invalid(ST(0), -2147483648.0, 2147483648.0);
    if (invalid < 0)
        return 0;
    seteal(x87_fround32(ST(0)));
    if (!cpu_state.abrt)
        x87_store_done(invalid);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
opFISTPil_a16(UNUSED(uint32_t fetchdat))
{
    int invalid = 0;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(4, 1, 1))
        return cpu_state.abrt;
    invalid = x87_fist_invalid(ST(0), -2147483648.0, 2147483648.0);
    if (invalid < 0)
        return 0;
    seteal(x87_fround32(ST(0)));
    if (cpu_state.abrt)
        return 1;
    x87_store_done(invalid);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFISTPil_a32(uint32_t fetchdat)
{
    int invalid = 0;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(4, 1, 1))
        return cpu_state.abrt;
    invalid = x87_fist_invalid(ST(0), -2147483648.0, 2147483648.0);
    if (invalid < 0)
        return 0;
    seteal(x87_fround32(ST(0)));
    if (cpu_state.abrt)
        return 1;
    x87_store_done(invalid);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return 0;
}
#endif

static int
opFLDe_a16(UNUSED(uint32_t fetchdat))
{
    double t;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ_EA(10);
    t = x87_ld80();
    if (cpu_state.abrt)
        return 1;
    x87_push(t);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFLDe_a32(uint32_t fetchdat)
{
    double t;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ_EA(10);
    t = x87_ld80();
    if (cpu_state.abrt)
        return 1;
    x87_push(t);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
    return 0;
}
#endif

static int
opFSTPe_a16(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_EA(10);
    if (x87_empty_store(10, 0, 1))
        return cpu_state.abrt;
    x87_st80(ST(0));
    if (cpu_state.abrt)
        return 1;
    x87_store_done(0);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFSTPe_a32(uint32_t fetchdat)
{
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_EA(10);
    if (x87_empty_store(10, 0, 1))
        return cpu_state.abrt;
    x87_st80(ST(0));
    if (cpu_state.abrt)
        return 1;
    x87_store_done(0);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
    return 0;
}
#endif

static int
opFLDd_a16(UNUSED(uint32_t fetchdat))
{
    x87_td t;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    t.i = geteaq();
    if (cpu_state.abrt)
        return 1;
    x87_push(t.d);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_64) : (x87_timings.fld_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_64) : (x87_concurrency.fld_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFLDd_a32(uint32_t fetchdat)
{
    x87_td t;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    t.i = geteaq();
    if (cpu_state.abrt)
        return 1;
    x87_push(t.d);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_64) : (x87_timings.fld_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_64) : (x87_concurrency.fld_64 * cpu_multi));
    return 0;
}
#endif

static int
opFSTd_a16(UNUSED(uint32_t fetchdat))
{
    x87_td t;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(8, 0, 0))
        return cpu_state.abrt;
    t.d = ST(0);
    seteaq(t.i);
    if (!cpu_state.abrt)
        x87_store_done(0);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTd_a32(uint32_t fetchdat)
{
    x87_td t;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(8, 0, 0))
        return cpu_state.abrt;
    t.d = ST(0);
    seteaq(t.i);
    if (!cpu_state.abrt)
        x87_store_done(0);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
opFSTPd_a16(UNUSED(uint32_t fetchdat))
{
    x87_td t;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(8, 0, 1))
        return cpu_state.abrt;
    t.d = ST(0);
    seteaq(t.i);
    if (cpu_state.abrt)
        return 1;
    x87_store_done(0);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFSTPd_a32(uint32_t fetchdat)
{
    x87_td t;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(8, 0, 1))
        return cpu_state.abrt;
    t.d = ST(0);
    seteaq(t.i);
    if (cpu_state.abrt)
        return 1;
    x87_store_done(0);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return 0;
}
#endif

static int
opFLDs_a16(UNUSED(uint32_t fetchdat))
{
    x87_ts ts;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    ts.i = geteal();
    if (cpu_state.abrt)
        return 1;
    x87_push((double) ts.s);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFLDs_a32(uint32_t fetchdat)
{
    x87_ts ts;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    ts.i = geteal();
    if (cpu_state.abrt)
        return 1;
    x87_push((double) ts.s);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#endif

static int
opFSTs_a16(UNUSED(uint32_t fetchdat))
{
    x87_ts ts;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(4, 0, 0))
        return cpu_state.abrt;
    ts.s = (float) ST(0);
    seteal(ts.i);
    if (!cpu_state.abrt)
        x87_store_done(0);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTs_a32(uint32_t fetchdat)
{
    x87_ts ts;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(4, 0, 0))
        return cpu_state.abrt;
    ts.s = (float) ST(0);
    seteal(ts.i);
    if (!cpu_state.abrt)
        x87_store_done(0);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
opFSTPs_a16(UNUSED(uint32_t fetchdat))
{
    x87_ts ts;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(4, 0, 1))
        return cpu_state.abrt;
    ts.s = (float) ST(0);
    seteal(ts.i);
    if (cpu_state.abrt)
        return 1;
    x87_store_done(0);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFSTPs_a32(uint32_t fetchdat)
{
    x87_ts ts;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (x87_empty_store(4, 0, 1))
        return cpu_state.abrt;
    ts.s = (float) ST(0);
    seteal(ts.i);
    if (cpu_state.abrt)
        return 1;
    x87_store_done(0);
    x87_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#endif

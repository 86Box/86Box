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

#define swap_values16u(a, b) \
    {                        \
        uint16_t tmp = a;    \
        a            = b;    \
        b            = tmp;  \
    }

static int
sf_FILDiw_a16(uint32_t fetchdat)
{
    floatx80 result;
    int16_t  temp;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    temp = geteaw();
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
    } else {
        result = int32_to_floatx80(temp);
        FPU_push();
        FPU_save_regi(result, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_16) : (x87_timings.fild_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_16) : (x87_concurrency.fild_16 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FILDiw_a32(uint32_t fetchdat)
{
    floatx80 result;
    int16_t  temp;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    temp = geteaw();
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
    } else {
        result = int32_to_floatx80(temp);
        FPU_push();
        FPU_save_regi(result, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_16) : (x87_timings.fild_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_16) : (x87_concurrency.fild_16 * cpu_multi));
    return 0;
}
#endif

static int
sf_FILDil_a16(uint32_t fetchdat)
{
    floatx80 result;
    int32_t  templ;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    templ = geteal();
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
    } else {
        result = int32_to_floatx80(templ);
        FPU_push();
        FPU_save_regi(result, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_32) : (x87_timings.fild_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_32) : (x87_concurrency.fild_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FILDil_a32(uint32_t fetchdat)
{
    floatx80 result;
    int32_t  templ;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    templ = geteal();
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
    } else {
        result = int32_to_floatx80(templ);
        FPU_push();
        FPU_save_regi(result, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_32) : (x87_timings.fild_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_32) : (x87_concurrency.fild_32 * cpu_multi));
    return 0;
}
#endif

static int
sf_FILDiq_a16(uint32_t fetchdat)
{
    floatx80 result;
    int64_t  temp64;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    temp64 = geteaq();
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
    } else {
        result = int64_to_floatx80(temp64);
        FPU_push();
        FPU_save_regi(result, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_64) : (x87_timings.fild_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_64) : (x87_concurrency.fild_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FILDiq_a32(uint32_t fetchdat)
{
    floatx80 result;
    int64_t  temp64;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    temp64 = geteaq();
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
    } else {
        result = int64_to_floatx80(temp64);
        FPU_push();
        FPU_save_regi(result, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_64) : (x87_timings.fild_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_64) : (x87_concurrency.fild_64 * cpu_multi));
    return 0;
}
#endif

static int
sf_FLDs_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    floatx80              result;
    float32               load_reg;
    unsigned              unmasked;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    load_reg = geteal();
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
        goto next_ins;
    }
    status   = i387cw_to_softfloat_status_word(i387_get_control_word());
    result   = float32_to_floatx80(load_reg, &status);
    unmasked = FPU_exception(fetchdat, status.float_exception_flags, 0);
    if (!(unmasked & FPU_CW_Invalid)) {
        FPU_push();
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FLDs_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    floatx80              result;
    float32               load_reg;
    unsigned              unmasked;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    load_reg = geteal();
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
        goto next_ins;
    }
    status   = i387cw_to_softfloat_status_word(i387_get_control_word());
    result   = float32_to_floatx80(load_reg, &status);
    unmasked = FPU_exception(fetchdat, status.float_exception_flags, 0);
    if (!(unmasked & FPU_CW_Invalid)) {
        FPU_push();
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#endif

static int
sf_FLDd_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    floatx80              result;
    float64               load_reg;
    unsigned              unmasked;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    load_reg = geteaq();
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
        goto next_ins;
    }
    status   = i387cw_to_softfloat_status_word(i387_get_control_word());
    result   = float64_to_floatx80(load_reg, &status);
    unmasked = FPU_exception(fetchdat, status.float_exception_flags, 0);
    if (!(unmasked & FPU_CW_Invalid)) {
        FPU_push();
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_64) : (x87_timings.fld_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_64) : (x87_concurrency.fld_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FLDd_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    floatx80              result;
    float64               load_reg;
    unsigned              unmasked;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    load_reg = geteaq();
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
        goto next_ins;
    }
    status   = i387cw_to_softfloat_status_word(i387_get_control_word());
    result   = float64_to_floatx80(load_reg, &status);
    unmasked = FPU_exception(fetchdat, status.float_exception_flags, 0);
    if (!(unmasked & FPU_CW_Invalid)) {
        FPU_push();
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_64) : (x87_timings.fld_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_64) : (x87_concurrency.fld_64 * cpu_multi));
    return 0;
}
#endif

static int
sf_FLDe_a16(uint32_t fetchdat)
{
    floatx80 result;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    result.fraction = readmemq(easeg, cpu_state.eaaddr);
    result.exp      = readmemw(easeg, cpu_state.eaaddr + 8);
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
    } else {
        FPU_push();
        FPU_save_regi(result, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FLDe_a32(uint32_t fetchdat)
{
    floatx80 result;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    result.fraction = readmemq(easeg, cpu_state.eaaddr);
    result.exp      = readmemw(easeg, cpu_state.eaaddr + 8);
    if (cpu_state.abrt)
        return 1;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
    } else {
        FPU_push();
        FPU_save_regi(result, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
    return 0;
}
#endif

static int
sf_FLD_sti(uint32_t fetchdat)
{
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    floatx80       sti_reg;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (!IS_TAG_EMPTY(-1)) {
        FPU_stack_overflow(fetchdat);
        goto next_ins;
    }
    sti_reg = floatx80_default_nan;
    if (IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        sti_reg = FPU_read_regi(fetchdat & 7);
    }

    FPU_push();
    FPU_save_regi(sti_reg, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld) : (x87_timings.fld * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld) : (x87_concurrency.fld * cpu_multi));
    return 0;
}

static int
sf_FISTiw_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    int16_t               save_reg = int16_indefinite;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked()) {
            goto next_ins;
        }
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_int16(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1)) {
            goto next_ins;
        }
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteaw(save_reg);
    fpu_state.swd = sw;

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
sf_FISTiw_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    int16_t               save_reg = int16_indefinite;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_int16(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1))
            goto next_ins;
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteaw(save_reg);
    fpu_state.swd = sw;

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
sf_FISTPiw_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    int16_t               save_reg = int16_indefinite;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_int16(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1)) {
            goto next_ins;
        }
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteaw(save_reg);
    if (cpu_state.abrt)
        return 1;
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FISTPiw_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    int16_t               save_reg = int16_indefinite;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_int16(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1))
            goto next_ins;
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteaw(save_reg);
    if (cpu_state.abrt)
        return 1;
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return 0;
}
#endif

static int
sf_FISTil_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    int32_t               save_reg = int32_indefinite;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_int32(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1)) {
            goto next_ins;
        }
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteal(save_reg);
    fpu_state.swd = sw;

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
sf_FISTil_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    int32_t               save_reg = int32_indefinite;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_int32(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1))
            goto next_ins;
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteal(save_reg);
    fpu_state.swd = sw;

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
sf_FISTPil_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    int32_t               save_reg = int32_indefinite;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_int32(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1)) {
            goto next_ins;
        }
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteal(save_reg);
    if (cpu_state.abrt)
        return 1;
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FISTPil_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    int32_t               save_reg = int32_indefinite;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_int32(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1))
            goto next_ins;
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteal(save_reg);
    if (cpu_state.abrt)
        return 1;
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return 0;
}
#endif

static int
sf_FISTPiq_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    int64_t               save_reg = int64_indefinite;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_int64(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1)) {
            goto next_ins;
        }
    }
    // store to the memory might generate an exception, in this case origial FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteaq(save_reg);
    if (cpu_state.abrt)
        return 1;
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_64) : (x87_timings.fist_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_64) : (x87_concurrency.fist_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FISTPiq_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    int64_t               save_reg = int64_indefinite;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_int64(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1))
            goto next_ins;
    }
    // store to the memory might generate an exception, in this case origial FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteaq(save_reg);
    if (cpu_state.abrt)
        return 1;
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_64) : (x87_timings.fist_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_64) : (x87_concurrency.fist_64 * cpu_multi));
    return 0;
}
#endif

static int
sf_FBSTP_PACKED_BCD_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw          = fpu_state.swd;
    uint16_t              save_reg_hi = 0xffff;
    uint64_t              save_reg_lo = BX_CONST64(0xC000000000000000);
    floatx80              reg;
    int64_t               save_val;
    int                   sign;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        reg      = FPU_read_regi(0);
        save_val = floatx80_to_int64(reg, &status);
        sign     = (reg.exp & 0x8000) != 0;
        if (sign)
            save_val = -save_val;

        if (save_val > BX_CONST64(999999999999999999))
            status.float_exception_flags = float_flag_invalid; // throw away other flags

        if (!(status.float_exception_flags & float_flag_invalid)) {
            save_reg_hi = sign ? 0x8000 : 0;
            save_reg_lo = 0;
            for (int i = 0; i < 16; i++) {
                save_reg_lo += ((uint64_t) (save_val % 10)) << (4 * i);
                save_val /= 10;
            }
            save_reg_hi += (uint16_t) (save_val % 10);
            save_val /= 10;
            save_reg_hi += (uint16_t) (save_val % 10) << 4;
        }
        /* check for fpu arithmetic exceptions */
        if (FPU_exception(fetchdat, status.float_exception_flags, 1)) {
            goto next_ins;
        }
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);

    // write packed bcd to memory
    writememq(easeg, cpu_state.eaaddr, save_reg_lo);
    writememw(easeg, cpu_state.eaaddr + 8, save_reg_hi);
    if (cpu_state.abrt)
        return 1;
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fbstp) : (x87_timings.fbstp * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fbstp) : (x87_concurrency.fbstp * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FBSTP_PACKED_BCD_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw          = fpu_state.swd;
    uint16_t              save_reg_hi = 0xffff;
    uint64_t              save_reg_lo = BX_CONST64(0xC000000000000000);
    floatx80              reg;
    int64_t               save_val;
    int                   sign;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        reg      = FPU_read_regi(0);
        save_val = floatx80_to_int64(reg, &status);
        sign     = (reg.exp & 0x8000) != 0;
        if (sign)
            save_val = -save_val;

        if (save_val > BX_CONST64(999999999999999999))
            status.float_exception_flags = float_flag_invalid; // throw away other flags

        if (!(status.float_exception_flags & float_flag_invalid)) {
            save_reg_hi = sign ? 0x8000 : 0;
            save_reg_lo = 0;
            for (int i = 0; i < 16; i++) {
                save_reg_lo += ((uint64_t) (save_val % 10)) << (4 * i);
                save_val /= 10;
            }
            save_reg_hi += (uint16_t) (save_val % 10);
            save_val /= 10;
            save_reg_hi += (uint16_t) (save_val % 10) << 4;
        }
        /* check for fpu arithmetic exceptions */
        if (FPU_exception(fetchdat, status.float_exception_flags, 1)) {
            goto next_ins;
        }
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);

    // write packed bcd to memory
    writememq(easeg, cpu_state.eaaddr, save_reg_lo);
    writememw(easeg, cpu_state.eaaddr + 8, save_reg_hi);
    if (cpu_state.abrt)
        return 1;
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fbstp) : (x87_timings.fbstp * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fbstp) : (x87_concurrency.fbstp * cpu_multi));
    return 0;
}
#endif

static int
sf_FSTs_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    float32               save_reg = float32_default_nan;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_float32(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1)) {
            goto next_ins;
        }
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteal(save_reg);
    fpu_state.swd = sw;

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
sf_FSTs_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    float32               save_reg = float32_default_nan;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_float32(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1))
            goto next_ins;
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteal(save_reg);
    fpu_state.swd = sw;

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
sf_FSTPs_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    float32               save_reg = float32_default_nan;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_float32(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1)) {
            goto next_ins;
        }
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteal(save_reg);
    if (cpu_state.abrt) {
        return 1;
    }
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FSTPs_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    float32               save_reg = float32_default_nan;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_float32(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1))
            goto next_ins;
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteal(save_reg);
    if (cpu_state.abrt)
        return 1;
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#endif

static int
sf_FSTd_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    float64               save_reg = float64_default_nan;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_float64(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1)) {
            goto next_ins;
        }
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteaq(save_reg);
    fpu_state.swd = sw;

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
sf_FSTd_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    float64               save_reg = float64_default_nan;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_float64(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1))
            goto next_ins;
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteaq(save_reg);
    fpu_state.swd = sw;

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
sf_FSTPd_a16(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    float64               save_reg = float64_default_nan;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked()) {
            goto next_ins;
        }
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_float64(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1)) {
            goto next_ins;
        }
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteaq(save_reg);
    if (cpu_state.abrt)
        return 1;
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FSTPd_a32(uint32_t fetchdat)
{
    struct float_status_t status;
    uint16_t              sw       = fpu_state.swd;
    float64               save_reg = float64_default_nan;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        status   = i387cw_to_softfloat_status_word(i387_get_control_word());
        save_reg = floatx80_to_float64(FPU_read_regi(0), &status);
        if (FPU_exception(fetchdat, status.float_exception_flags, 1))
            goto next_ins;
    }
    // store to the memory might generate an exception, in this case original FPU_SW must be kept
    swap_values16u(sw, fpu_state.swd);
    seteaq(save_reg);
    if (cpu_state.abrt)
        return 1;
    fpu_state.swd = sw;
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return 0;
}
#endif

static int
sf_FSTPe_a16(uint32_t fetchdat)
{
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    floatx80       save_reg;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_state.abrt)
        return 1;
    save_reg = floatx80_default_nan;
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked()) {
            goto next_ins;
        }
    } else {
        save_reg = FPU_read_regi(0);
    }
    writememq(easeg, cpu_state.eaaddr, save_reg.fraction);
    writememw(easeg, cpu_state.eaaddr + 8, save_reg.exp);
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_80) : (x87_timings.fst_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_80) : (x87_concurrency.fst_80 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FSTPe_a32(uint32_t fetchdat)
{
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    floatx80       save_reg;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_state.abrt)
        return 1;
    save_reg = floatx80_default_nan;
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (!is_IA_masked())
            goto next_ins;
    } else {
        save_reg = FPU_read_regi(0);
    }
    writememq(easeg, cpu_state.eaaddr, save_reg.fraction);
    writememw(easeg, cpu_state.eaaddr + 8, save_reg.exp);
    FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_80) : (x87_timings.fst_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_80) : (x87_concurrency.fst_80 * cpu_multi));
    return 0;
}
#endif

static int
sf_FST_sti(uint32_t fetchdat)
{
    floatx80 st0_reg;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 0);
    } else {
        st0_reg = FPU_read_regi(0);
        FPU_save_regi(st0_reg, fetchdat & 7);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst) : (x87_timings.fst * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst) : (x87_concurrency.fst * cpu_multi));
    return 0;
}

static int
sf_FSTP_sti(uint32_t fetchdat)
{
    floatx80 st0_reg;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 1);
    } else {
        st0_reg = FPU_read_regi(0);
        FPU_save_regi(st0_reg, fetchdat & 7);
        FPU_pop();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst) : (x87_timings.fst * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst) : (x87_concurrency.fst * cpu_multi));
    return 0;
}

#ifndef FPU_8087
#    ifndef OPS_286_386
#        define sf_FCMOV(condition)                                    \
            static int sf_FCMOV##condition(uint32_t fetchdat)          \
            {                                                          \
                FP_ENTER();                                            \
                FPU_check_pending_exceptions();                        \
                cpu_state.pc++;                                        \
                if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7))     \
                    FPU_stack_underflow(fetchdat, 0, 0);               \
                else {                                                 \
                    if (cond_##condition) {                            \
                        FPU_save_regi(FPU_read_regi(fetchdat & 7), 0); \
                    }                                                  \
                }                                                      \
                CLOCK_CYCLES_FPU(4);                                   \
                return 0;                                              \
            }

#        define cond_U  (PF_SET())
#        define cond_NU (!PF_SET())

// clang-format off
sf_FCMOV(B)
sf_FCMOV(E)
sf_FCMOV(BE)
sf_FCMOV(U)
sf_FCMOV(NB)
sf_FCMOV(NE)
sf_FCMOV(NBE)
sf_FCMOV(NU)
// clang-format on
#    endif
#endif

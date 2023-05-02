#define sf_FPU(name, optype, a_size, load_var, rw, use_var, is_nan, cycle_postfix)                                                                         \
    static int sf_FADD##name##_a##a_size(uint32_t fetchdat)                                                                                         \
    {                                                                                                                                              \
        floatx80 a, result; \
        struct float_status_t status; \
        optype temp; \
        FP_ENTER();                                                                                                                                \
        FPU_check_pending_exceptions(); \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        load_var = rw; \
        if (cpu_state.abrt) \
            return 1;\
        clear_C1(); \
        if (IS_TAG_EMPTY(0)) { \
            FPU_stack_underflow(fetchdat, 0, 0); \
            goto next_ins; \
        } \
        status = i387cw_to_softfloat_status_word(i387_get_control_word()); \
        a = FPU_read_regi(0); \
        if (!is_nan) \
            result = floatx80_add(a, use_var, &status); \
        \
        if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) \
            FPU_save_regi(result, 0); \
        \
next_ins: \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd##cycle_postfix) : ((x87_timings.fadd##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd##cycle_postfix) : ((x87_concurrency.fadd##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int sf_FDIV##name##_a##a_size(uint32_t fetchdat)                                                                                         \
    {                                                                                                                                              \
        floatx80 a, result; \
        struct float_status_t status; \
        optype temp;                                                                                                                                  \
        FP_ENTER();                                                                                                                                \
        FPU_check_pending_exceptions(); \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        load_var = rw; \
        if (cpu_state.abrt) \
            return 1;\
        clear_C1(); \
        if (IS_TAG_EMPTY(0)) { \
            FPU_stack_underflow(fetchdat, 0, 0); \
            goto next_ins; \
        } \
        status = i387cw_to_softfloat_status_word(i387_get_control_word()); \
        a = FPU_read_regi(0); \
        if (!is_nan) { \
            result = floatx80_div(a, use_var, &status);        \
        } \
        if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) \
            FPU_save_regi(result, 0); \
        \
next_ins: \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv##cycle_postfix) : ((x87_timings.fdiv##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd##cycle_postfix) : ((x87_concurrency.fadd##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int sf_FDIVR##name##_a##a_size(uint32_t fetchdat)                                                                                        \
    {                                                                                                                                              \
        floatx80 a, result; \
        struct float_status_t status; \
        optype temp; \
        FP_ENTER();                                                                                                                                \
        FPU_check_pending_exceptions(); \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        load_var = rw; \
        if (cpu_state.abrt) \
            return 1;\
        clear_C1(); \
        if (IS_TAG_EMPTY(0)) { \
            FPU_stack_underflow(fetchdat, 0, 0); \
            goto next_ins; \
        } \
        status = i387cw_to_softfloat_status_word(i387_get_control_word()); \
        a = FPU_read_regi(0); \
        if (!is_nan) { \
            result = floatx80_div(use_var, a, &status);        \
        } \
        if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) \
            FPU_save_regi(result, 0); \
        \
next_ins: \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv##cycle_postfix) : ((x87_timings.fdiv##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv##cycle_postfix) : ((x87_concurrency.fdiv##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int sf_FMUL##name##_a##a_size(uint32_t fetchdat)                                                                                         \
    {                                                                                                                                              \
        floatx80 a, result; \
        struct float_status_t status; \
        optype temp;                                                                                                                                  \
        FP_ENTER();                                                                                                                                \
        FPU_check_pending_exceptions(); \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        load_var = rw; \
        if (cpu_state.abrt) \
            return 1;\
        clear_C1(); \
        if (IS_TAG_EMPTY(0)) { \
            FPU_stack_underflow(fetchdat, 0, 0); \
            goto next_ins; \
        } \
        status = i387cw_to_softfloat_status_word(i387_get_control_word()); \
        a = FPU_read_regi(0); \
        if (!is_nan) { \
            result = floatx80_mul(a, use_var, &status); \
        } \
        if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) \
            FPU_save_regi(result, 0); \
        \
next_ins: \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul##cycle_postfix) : ((x87_timings.fmul##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul##cycle_postfix) : ((x87_concurrency.fmul##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int sf_FSUB##name##_a##a_size(uint32_t fetchdat)                                                                                         \
    {                                                                                                                                              \
        floatx80 a, result; \
        struct float_status_t status; \
        optype temp;                                                                                                                                  \
        FP_ENTER();                                                                                                                                \
        FPU_check_pending_exceptions(); \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        load_var = rw; \
        if (cpu_state.abrt) \
            return 1;\
        clear_C1(); \
        if (IS_TAG_EMPTY(0)) { \
            FPU_stack_underflow(fetchdat, 0, 0); \
            goto next_ins; \
        } \
        status = i387cw_to_softfloat_status_word(i387_get_control_word()); \
        a = FPU_read_regi(0); \
        if (!is_nan) \
            result = floatx80_sub(a, use_var, &status); \
        \
        if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) \
            FPU_save_regi(result, 0); \
        \
next_ins: \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd##cycle_postfix) : ((x87_timings.fadd##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd##cycle_postfix) : ((x87_concurrency.fadd##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int sf_FSUBR##name##_a##a_size(uint32_t fetchdat)                                                                                        \
    {                                                                                                                                              \
        floatx80 a, result; \
        struct float_status_t status; \
        optype temp;                                                                                                                                  \
        FP_ENTER();                                                                                                                                \
        FPU_check_pending_exceptions(); \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        load_var = rw; \
        if (cpu_state.abrt) \
            return 1;\
        clear_C1(); \
        if (IS_TAG_EMPTY(0)) { \
            FPU_stack_underflow(fetchdat, 0, 0); \
            goto next_ins; \
        } \
        status = i387cw_to_softfloat_status_word(i387_get_control_word()); \
        a = FPU_read_regi(0); \
        if (!is_nan) \
            result = floatx80_sub(use_var, a, &status); \
        \
        if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) \
            FPU_save_regi(result, 0); \
        \
next_ins: \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd##cycle_postfix) : ((x87_timings.fadd##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd##cycle_postfix) : ((x87_concurrency.fadd##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }

// clang-format off
sf_FPU(s, float32, 16, temp, geteal(), float32_to_floatx80(temp, &status), FPU_handle_NaN32(a, temp, &result, &status), _32)
#ifndef FPU_8087
sf_FPU(s, float32, 32, temp, geteal(), float32_to_floatx80(temp, &status), FPU_handle_NaN32(a, temp, &result, &status), _32)
#endif
sf_FPU(d, float64, 16, temp, geteaq(), float64_to_floatx80(temp, &status), FPU_handle_NaN64(a, temp, &result, &status), _64)
#ifndef FPU_8087
sf_FPU(d, float64, 32, temp, geteaq(), float64_to_floatx80(temp, &status), FPU_handle_NaN64(a, temp, &result, &status), _64)
#endif

sf_FPU(iw, uint16_t, 16, temp, geteaw(), int32_to_floatx80((int16_t)temp), 0, _i16)
#ifndef FPU_8087
sf_FPU(iw, uint16_t, 32, temp, geteaw(), int32_to_floatx80((int16_t)temp), 0, _i16)
#endif
sf_FPU(il, uint32_t, 16, temp, geteal(), int32_to_floatx80((int32_t)temp), 0, _i32)
#ifndef FPU_8087
sf_FPU(il, uint32_t, 32, temp, geteal(), int32_to_floatx80((int32_t)temp), 0, _i32)
#endif
// clang-format on

static int
sf_FADD_st0_stj(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    result = floatx80_add(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0))
        FPU_save_regi(result, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}
static int
sf_FADD_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(fetchdat & 7);
    b = FPU_read_regi(0);
    result = floatx80_add(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0))
        FPU_save_regi(result, fetchdat & 7);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}

static int
sf_FADDP_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 1);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(fetchdat & 7);
    b = FPU_read_regi(0);
    result = floatx80_add(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, fetchdat & 7);
        FPU_pop();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}

static int
sf_FDIV_st0_stj(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    result = floatx80_div(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0))
        FPU_save_regi(result, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}

static int
sf_FDIV_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(fetchdat & 7);
    b = FPU_read_regi(0);
    result = floatx80_div(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0))
        FPU_save_regi(result, fetchdat & 7);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}
static int
sf_FDIVP_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 1);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(fetchdat & 7);
    b = FPU_read_regi(0);
    result = floatx80_div(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, fetchdat & 7);
        FPU_pop();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}

static int
sf_FDIVR_st0_stj(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(fetchdat & 7);
    b = FPU_read_regi(0);
    result = floatx80_div(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0))
        FPU_save_regi(result, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}
static int
sf_FDIVR_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    result = floatx80_div(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0))
        FPU_save_regi(result, fetchdat & 7);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}
static int
sf_FDIVRP_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 1);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    result = floatx80_div(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, fetchdat & 7);
        FPU_pop();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}

static int
sf_FMUL_st0_stj(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    result = floatx80_mul(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul) : (x87_timings.fmul * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul) : (x87_concurrency.fmul * cpu_multi));
    return 0;
}
static int
sf_FMUL_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    result = floatx80_mul(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, fetchdat & 7);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul) : (x87_timings.fmul * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul) : (x87_concurrency.fmul * cpu_multi));
    return 0;
}
static int
sf_FMULP_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 1);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(fetchdat & 7);
    b = FPU_read_regi(0);
    result = floatx80_mul(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, fetchdat & 7);
        FPU_pop();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul) : (x87_timings.fmul * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul) : (x87_concurrency.fmul * cpu_multi));
    return 0;
}

static int
sf_FSUB_st0_stj(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    result = floatx80_sub(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}
static int
sf_FSUB_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(fetchdat & 7);
    b = FPU_read_regi(0);
    result = floatx80_sub(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, fetchdat & 7);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}
static int
sf_FSUBP_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 1);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(fetchdat & 7);
    b = FPU_read_regi(0);
    result = floatx80_sub(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, fetchdat & 7);
        FPU_pop();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}

static int
sf_FSUBR_st0_stj(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(fetchdat & 7);
    b = FPU_read_regi(0);
    result = floatx80_sub(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}
static int
sf_FSUBR_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    result = floatx80_sub(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, fetchdat & 7);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}
static int
sf_FSUBRP_sti_st0(uint32_t fetchdat)
{
    floatx80 a, b, result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_stack_underflow(fetchdat, fetchdat & 7, 1);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    result = floatx80_sub(a, b, &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, fetchdat & 7);
        FPU_pop();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}

static int
sf_FSQRT(uint32_t fetchdat)
{
    floatx80 result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    result = floatx80_sqrt(FPU_read_regi(0), &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsqrt) : (x87_timings.fsqrt * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsqrt) : (x87_concurrency.fsqrt * cpu_multi));
    return 0;
}

static int
sf_FRNDINT(uint32_t fetchdat)
{
    floatx80 result;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    result = floatx80_round_to_int(FPU_read_regi(0), &status);

    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.frndint) : (x87_timings.frndint * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.frndint) : (x87_concurrency.frndint * cpu_multi));
    return 0;
}

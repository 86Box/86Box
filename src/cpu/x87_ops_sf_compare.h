#define cmp_FPU(name, optype, a_size, load_var, rw, use_var, is_nan, cycle_postfix)                                                                         \
    static int sf_FCOM##name##_a##a_size(uint32_t fetchdat)                                                                                         \
    {                                                                                                                                              \
        floatx80 a; \
        int rc; \
        struct float_status_t status; \
        optype temp; \
        FP_ENTER();                                                                                                                                \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        load_var = rw; \
        if (cpu_state.abrt) \
            return 1;\
        clear_C1(); \
        if (IS_TAG_EMPTY(0)) { \
            FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0); \
            setcc(C0 | C2 | C3); \
            goto next_ins; \
        } \
        status = i387cw_to_softfloat_status_word(i387_get_control_word()); \
        a = FPU_read_regi(0); \
        if (is_nan) { \
            rc = float_relation_unordered; \
            float_raise(&status, float_flag_invalid); \
        } else { \
            rc = floatx80_compare_two(a, use_var, &status); \
        } \
        setcc(FPU_status_word_flags_fpu_compare(rc)); \
        FPU_exception(fetchdat, status.float_exception_flags, 0); \
        \
next_ins: \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom##cycle_postfix) : ((x87_timings.fcom##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom##cycle_postfix) : ((x87_concurrency.fcom##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int sf_FCOMP##name##_a##a_size(uint32_t fetchdat)                                                                                        \
    {                                                                                                                                              \
        floatx80 a; \
        int rc; \
        struct float_status_t status; \
        optype temp;                                                                                                                                  \
        FP_ENTER();                                                                                                                                \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        load_var = rw; \
        if (cpu_state.abrt) \
            return 1;\
        clear_C1(); \
        if (IS_TAG_EMPTY(0)) { \
            FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0); \
            setcc(C0 | C2 | C3); \
            if (is_IA_masked()) \
                FPU_pop(); \
            \
            goto next_ins; \
        } \
        status = i387cw_to_softfloat_status_word(i387_get_control_word()); \
        a = FPU_read_regi(0); \
        if (is_nan) { \
            rc = float_relation_unordered; \
            float_raise(&status, float_flag_invalid); \
        } else { \
            rc = floatx80_compare_two(a, use_var, &status); \
        } \
        setcc(FPU_status_word_flags_fpu_compare(rc)); \
        if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) \
            FPU_pop(); \
        \
next_ins: \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom##cycle_postfix) : ((x87_timings.fcom##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom##cycle_postfix) : ((x87_concurrency.fcom##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \

// clang-format off
cmp_FPU(s, float32, 16, temp, geteal(), float32_to_floatx80(temp, &status), floatx80_is_nan(a) || floatx80_is_unsupported(a) || float32_is_nan(temp), _32)
#ifndef FPU_8087
cmp_FPU(s, float32, 32, temp, geteal(), float32_to_floatx80(temp, &status), floatx80_is_nan(a) || floatx80_is_unsupported(a) || float32_is_nan(temp), _32)
#endif
cmp_FPU(d, float64, 16, temp, geteaq(), float64_to_floatx80(temp, &status), floatx80_is_nan(a) || floatx80_is_unsupported(a) || float64_is_nan(temp), _64)
#ifndef FPU_8087
cmp_FPU(d, float64, 32, temp, geteaq(), float64_to_floatx80(temp, &status), floatx80_is_nan(a) || floatx80_is_unsupported(a) || float64_is_nan(temp), _64)
#endif

cmp_FPU(iw, int16_t, 16, temp, (int16_t)geteaw(), int32_to_floatx80((int32_t)temp), 0, _i16)
#ifndef FPU_8087
cmp_FPU(iw, int16_t, 32, temp, (int16_t)geteaw(), int32_to_floatx80((int32_t)temp), 0, _i16)
#endif
cmp_FPU(il, int32_t, 16, temp, (int32_t)geteal(), int32_to_floatx80(temp), 0, _i32)
#ifndef FPU_8087
cmp_FPU(il, int32_t, 32, temp, (int32_t)geteal(), int32_to_floatx80(temp), 0, _i32)
#endif
// clang-format on

static int
sf_FCOM_sti(uint32_t fetchdat)
{
    floatx80 a, b;
    struct float_status_t status;
    int rc;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        setcc(C0 | C2 | C3);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    rc = floatx80_compare_two(a, b, &status);
    setcc(FPU_status_word_flags_fpu_compare(rc));
    FPU_exception(fetchdat, status.float_exception_flags, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
    return 0;
}

static int
sf_FCOMP_sti(uint32_t fetchdat)
{
    floatx80 a, b;
    struct float_status_t status;
    int rc;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        setcc(C0 | C2 | C3);
        if (is_IA_masked()) {
            FPU_pop();
        }
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    rc = floatx80_compare_two(a, b, &status);
    setcc(FPU_status_word_flags_fpu_compare(rc));
    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_pop();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
    return 0;
}

static int
sf_FCOMPP(uint32_t fetchdat)
{
    floatx80 a, b;
    struct float_status_t status;
    int rc;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        setcc(C0 | C2 | C3);
        if (is_IA_masked()) {
            FPU_pop();
            FPU_pop();
        }
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(1);
    rc = floatx80_compare_two(a, b, &status);
    setcc(FPU_status_word_flags_fpu_compare(rc));
    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_pop();
        FPU_pop();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
    return 0;
}

#ifndef FPU_8087
static int
sf_FUCOMPP(uint32_t fetchdat)
{
    floatx80 a, b;
    struct float_status_t status;
    int rc;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        setcc(C0 | C2 | C3);
        if (is_IA_masked()) {
            FPU_pop();
            FPU_pop();
        }
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(1);
    rc = floatx80_compare_quiet(a, b, &status);
    setcc(FPU_status_word_flags_fpu_compare(rc));
    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_pop();
        FPU_pop();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
    return 0;
}

static int
sf_FCOMI_st0_stj(uint32_t fetchdat)
{
    floatx80 a, b;
    struct float_status_t status;
    int rc;

    FP_ENTER();
    cpu_state.pc++;
    flags_rebuild();
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        cpu_state.flags |= (Z_FLAG | P_FLAG | C_FLAG);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    rc = floatx80_compare_two(a, b, &status);
    FPU_write_eflags_fpu_compare(rc);
    FPU_exception(fetchdat, status.float_exception_flags, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
    return 0;
}
static int
sf_FCOMIP_st0_stj(uint32_t fetchdat)
{
    floatx80 a, b;
    struct float_status_t status;
    int rc;

    FP_ENTER();
    cpu_state.pc++;
    flags_rebuild();
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        cpu_state.flags |= (Z_FLAG | P_FLAG | C_FLAG);
        if (is_IA_masked()) {
            FPU_pop();
        }
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    rc = floatx80_compare_two(a, b, &status);
    FPU_write_eflags_fpu_compare(rc);
    if (!FPU_exception(fetchdat, status.float_exception_flags, 0)) {
        FPU_pop();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
    return 0;
}

static int
sf_FUCOM_sti(uint32_t fetchdat)
{
    floatx80 a, b;
    struct float_status_t status;
    int rc;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        setcc(C0 | C2 | C3);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    rc = floatx80_compare_quiet(a, b, &status);
    setcc(FPU_status_word_flags_fpu_compare(rc));
    FPU_exception(fetchdat, status.float_exception_flags, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
    return 0;
}

static int
sf_FUCOMP_sti(uint32_t fetchdat)
{
    floatx80 a, b;
    struct float_status_t status;
    int rc;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        setcc(C0 | C2 | C3);
        if (is_IA_masked())
            FPU_pop();

        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    rc = floatx80_compare_quiet(a, b, &status);
    setcc(FPU_status_word_flags_fpu_compare(rc));
    if (!FPU_exception(fetchdat, status.float_exception_flags, 0))
        FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
    return 0;
}

static int
sf_FUCOMI_st0_stj(uint32_t fetchdat)
{
    floatx80 a, b;
    struct float_status_t status;
    int rc;

    FP_ENTER();
    cpu_state.pc++;
    flags_rebuild();
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        cpu_state.flags |= (Z_FLAG | P_FLAG | C_FLAG);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    rc = floatx80_compare_quiet(a, b, &status);
    FPU_write_eflags_fpu_compare(rc);
    FPU_exception(fetchdat, status.float_exception_flags, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
    return 0;
}
static int
sf_FUCOMIP_st0_stj(uint32_t fetchdat)
{
    floatx80 a, b;
    struct float_status_t status;
    int rc;

    FP_ENTER();
    cpu_state.pc++;
    flags_rebuild();
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        cpu_state.flags |= (Z_FLAG | P_FLAG | C_FLAG);
        if (is_IA_masked())
            FPU_pop();

        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a = FPU_read_regi(0);
    b = FPU_read_regi(fetchdat & 7);
    rc = floatx80_compare_quiet(a, b, &status);
    FPU_write_eflags_fpu_compare(rc);
    if (!FPU_exception(fetchdat, status.float_exception_flags, 0))
        FPU_pop();

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
    return 0;
}
#endif

static int
sf_FTST(uint32_t fetchdat)
{
    int rc;
    struct float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        setcc(C0 | C2 | C3);
    } else {
        status = i387cw_to_softfloat_status_word(i387_get_control_word());
        rc = floatx80_compare_two(FPU_read_regi(0), Const_Z, &status);
        setcc(FPU_status_word_flags_fpu_compare(rc));
        FPU_exception(fetchdat, status.float_exception_flags, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.ftst) : (x87_timings.ftst * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.ftst) : (x87_concurrency.ftst * cpu_multi));
    return 0;
}

static int
sf_FXAM(uint32_t fetchdat)
{
    floatx80 reg;
    int sign;
    float_class_t aClass;

    FP_ENTER();
    cpu_state.pc++;
    reg = FPU_read_regi(0);
    sign = floatx80_sign(reg);
  /*
   * Examine the contents of the ST(0) register and sets the condition
   * code flags C0, C2 and C3 in the FPU status word to indicate the
   * class of value or number in the register.
   */
    if (IS_TAG_EMPTY(0)) {
        setcc(C3 | C1 | C0);
    } else {
        aClass = floatx80_class(reg);
        switch (aClass) {
            case float_zero:
                setcc(C3 | C1);
                break;
            case float_SNaN:
            case float_QNaN:
                // unsupported handled as NaNs
                if (floatx80_is_unsupported(reg)) {
                    setcc(C1);
                } else {
                    setcc(C1 | C0);
                }
                break;
            case float_negative_inf:
            case float_positive_inf:
                setcc(C2 | C1 | C0);
                break;
            case float_denormal:
                setcc(C3 | C2 | C1);
                break;
            case float_normalized:
                setcc(C2 | C1);
                break;
        }
    }
  /*
   * The C1 flag is set to the sign of the value in ST(0), regardless
   * of whether the register is empty or full.
   */
    if (!sign)
        clear_C1();

    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fxam) : (x87_timings.fxam * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fxam) : (x87_concurrency.fxam * cpu_multi));
    return 0;
}

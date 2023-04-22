static floatx80
FPU_handle_NaN32_Func(floatx80 a, int aIsNaN, float32 b32, int bIsNaN, float_status_t *status)
{
    int aIsSignalingNaN = floatx80_is_signaling_nan(a);
    int bIsSignalingNaN = float32_is_signaling_nan(b32);

    if (aIsSignalingNaN | bIsSignalingNaN)
        float_raise(status, float_flag_invalid);

    // propagate QNaN to SNaN
    a = propagateFloatx80NaNOne(a, status);

    if (aIsNaN & !bIsNaN) return a;

    // float32 is NaN so conversion will propagate SNaN to QNaN and raise
    // appropriate exception flags
    floatx80 b = float32_to_floatx80(b32, status);

    if (aIsSignalingNaN) {
        if (bIsSignalingNaN) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if (aIsNaN) {
        if (bIsSignalingNaN) return a;
 returnLargerSignificand:
        if (a.fraction < b.fraction) return b;
        if (b.fraction < a.fraction) return a;
        return (a.exp < b.exp) ? a : b;
    }
    else {
        return b;
    }
}

static int
FPU_handle_NaN32(floatx80 a, float32 b, floatx80 *r, float_status_t *status)
{
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    if (floatx80_is_unsupported(a)) {
        pclog("Invalid 32.\n");
        float_raise(status, float_flag_invalid);
        *r = floatx80_default_nan;
        return 1;
    }

    int aIsNaN = floatx80_is_nan(a), bIsNaN = float32_is_nan(b);
    if (aIsNaN | bIsNaN) {
        *r = FPU_handle_NaN32_Func(a, aIsNaN, b, bIsNaN, status);
        return 1;
    }
    return 0;
}

static floatx80
FPU_handle_NaN64_Func(floatx80 a, int aIsNaN, float64 b64, int bIsNaN, float_status_t *status)
{
    int aIsSignalingNaN = floatx80_is_signaling_nan(a);
    int bIsSignalingNaN = float64_is_signaling_nan(b64);

    if (aIsSignalingNaN | bIsSignalingNaN)
        float_raise(status, float_flag_invalid);

    // propagate QNaN to SNaN
    a = propagateFloatx80NaNOne(a, status);

    if (aIsNaN & !bIsNaN) return a;

    // float64 is NaN so conversion will propagate SNaN to QNaN and raise
    // appropriate exception flags
    floatx80 b = float64_to_floatx80(b64, status);

    if (aIsSignalingNaN) {
        if (bIsSignalingNaN) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if (aIsNaN) {
        if (bIsSignalingNaN) return a;
 returnLargerSignificand:
        if (a.fraction < b.fraction) return b;
        if (b.fraction < a.fraction) return a;
        return (a.exp < b.exp) ? a : b;
    }
    else {
        return b;
    }
}

static int
FPU_handle_NaN64(floatx80 a, float64 b, floatx80 *r, float_status_t *status)
{
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    if (floatx80_is_unsupported(a)) {
        pclog("Invalid 64.\n");
        float_raise(status, float_flag_invalid);
        *r = floatx80_default_nan;
        return 1;
    }

    int aIsNaN = floatx80_is_nan(a), bIsNaN = float64_is_nan(b);
    if (aIsNaN | bIsNaN) {
        *r = FPU_handle_NaN64_Func(a, aIsNaN, b, bIsNaN, status);
        return 1;
    }
    return 0;
}


#define opFPU(name, optype, optype_ext, a_size, load_var, load_var_ext, get, rw, use_var, use_var_ext, is_nan, is_nan2, cycle_postfix)                                                                         \
    static int opFADD##name##_a##a_size(uint32_t fetchdat)                                                                                         \
    {                                                                                                                                              \
        floatx80 a, b, result; \
        float_status_t status; \
        optype t;                                                                                                                                  \
        optype_ext temp; \
        FP_ENTER();                                                                                                                                \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        if (cpu_use_dynarec) { \
            load_var = get();                                                                                                                          \
            if (cpu_state.abrt)                                                                                                                        \
                return 1;                                                                                                                              \
            if ((cpu_state.npxc >> 10) & 3)                                                                                                            \
                fesetround(rounding_modes[(cpu_state.npxc >> 10) & 3]);                                                                                \
            ST(0) += use_var;                                                                                                                          \
            if ((cpu_state.npxc >> 10) & 3)                                                                                                            \
                fesetround(FE_TONEAREST);                                                                                                              \
            FP_TAG_VALID;                                                                                                                              \
        } else { \
            pclog("FADDx_a.\n"); \
            load_var_ext = rw; \
            clear_C1(); \
            if (IS_TAG_EMPTY(0)) { \
                x87_stack_underflow(0, 0); \
                pclog("Underflow.\n"); \
                return 1; \
            } \
            i387cw_to_softfloat_status_word(&status, i387_get_control_word()); \
            a = x87_read_reg_ext(0); \
            b = use_var_ext; \
            if (!is_nan2) { \
                result = floatx80_add(a, b, &status); \
            } else \
                pclog("No result on FADDx_a.\n"); \
            \
            if (!x87_checkexceptions(status.float_exception_flags, 0)) { \
                pclog("Save FADDx_a.\n"); \
                x87_save_reg_ext(result, -1, 0, 0); \
            } \
        } \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd##cycle_postfix) : ((x87_timings.fadd##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd##cycle_postfix) : ((x87_concurrency.fadd##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int opFCOM##name##_a##a_size(uint32_t fetchdat)                                                                                         \
    {                                                                                                                                              \
        floatx80 a, b; \
        int rc; \
        float_status_t status; \
        optype t;                                                                                                                                  \
        optype_ext temp; \
        FP_ENTER();                                                                                                                                \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        if (cpu_use_dynarec) { \
            load_var = get();                                                                                                                          \
            if (cpu_state.abrt)                                                                                                                        \
                return 1; \
            cpu_state.npxs &= ~(C0 | C2 | C3);                                                                                                         \
            cpu_state.npxs |= x87_compare(ST(0), (double) use_var);                                                                                    \
        } else { \
            load_var_ext = rw; \
            clear_C1(); \
            if (IS_TAG_EMPTY(0)) { \
                x87_checkexceptions(FPU_EX_Stack_Underflow, 0); \
                setcc(C0 | C2 | C3); \
                return 1; \
            } \
            i387cw_to_softfloat_status_word(&status, i387_get_control_word()); \
            a = x87_read_reg_ext(0); \
            b = use_var_ext; \
            if (is_nan) { \
                rc = float_relation_unordered; \
                float_raise(&status, float_flag_invalid); \
            } else { \
                rc = floatx80_compare_two(a, b, &status); \
            } \
            setcc(x87_status_word_flags_fpu_compare(rc)); \
            if (!x87_checkexceptions(status.float_exception_flags, 0)) \
                ;\
        } \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom##cycle_postfix) : ((x87_timings.fcom##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom##cycle_postfix) : ((x87_concurrency.fcom##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int opFCOMP##name##_a##a_size(uint32_t fetchdat)                                                                                        \
    {                                                                                                                                              \
        floatx80 a, b; \
        int rc; \
        float_status_t status; \
        optype t;                                                                                                                                  \
        optype_ext temp; \
        FP_ENTER();                                                                                                                                \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        if (cpu_use_dynarec) { \
            load_var = get();                                                                                                                          \
            if (cpu_state.abrt)                                                                                                                        \
                return 1; \
            cpu_state.npxs &= ~(C0 | C2 | C3);                                                                                                         \
            cpu_state.npxs |= x87_compare(ST(0), (double) use_var);                                                                                    \
            x87_pop();                                                                                                                                 \
        } else { \
            load_var_ext = rw; \
            clear_C1(); \
            if (IS_TAG_EMPTY(0)) { \
                x87_checkexceptions(FPU_EX_Stack_Underflow, 0); \
                setcc(C0 | C2 | C3); \
                if (is_IA_masked()) \
                    x87_pop_ext(); \
                \
                return 1; \
            } \
            i387cw_to_softfloat_status_word(&status, i387_get_control_word()); \
            a = x87_read_reg_ext(0); \
            b = use_var_ext; \
            if (is_nan) { \
                rc = float_relation_unordered; \
                float_raise(&status, float_flag_invalid); \
            } else { \
                rc = floatx80_compare_two(a, b, &status); \
            } \
            setcc(x87_status_word_flags_fpu_compare(rc)); \
            if (!x87_checkexceptions(status.float_exception_flags, 0)) \
                x87_pop_ext(); \
            \
        } \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom##cycle_postfix) : ((x87_timings.fcom##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom##cycle_postfix) : ((x87_concurrency.fcom##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int opFDIV##name##_a##a_size(uint32_t fetchdat)                                                                                         \
    {                                                                                                                                              \
        floatx80 a, b, result; \
        float_status_t status; \
        optype t;                                                                                                                                  \
        optype_ext temp; \
        FP_ENTER();                                                                                                                                \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        if (cpu_use_dynarec) { \
            load_var = get();                                                                                                                          \
            if (cpu_state.abrt)                                                                                                                        \
                return 1; \
            x87_div(ST(0), ST(0), use_var);                                                                                                            \
            FP_TAG_VALID;                                                                                                                              \
        } else { \
            load_var_ext = rw; \
            clear_C1(); \
            if (IS_TAG_EMPTY(0)) { \
                x87_stack_underflow(0, 0); \
                return 1; \
            } \
            i387cw_to_softfloat_status_word(&status, i387_get_control_word()); \
            a = x87_read_reg_ext(0); \
            b = use_var_ext; \
            if (!is_nan2) { \
                x87_div_80(result, a, b, &status); \
            } \
            if (!x87_checkexceptions(status.float_exception_flags, 0)) \
                x87_save_reg_ext(result, -1, 0, 0); \
            \
        } \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv##cycle_postfix) : ((x87_timings.fdiv##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd##cycle_postfix) : ((x87_concurrency.fadd##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int opFDIVR##name##_a##a_size(uint32_t fetchdat)                                                                                        \
    {                                                                                                                                              \
        floatx80 a, b, result; \
        float_status_t status; \
        optype t;                                                                                                                                  \
        optype_ext temp; \
        FP_ENTER();                                                                                                                                \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        if (cpu_use_dynarec) { \
            load_var = get();                                                                                                                          \
            if (cpu_state.abrt)                                                                                                                        \
                return 1; \
            x87_div(ST(0), use_var, ST(0));                                                                                                            \
            FP_TAG_VALID;                                                                                                                              \
        } else { \
            load_var_ext = rw; \
            clear_C1(); \
            if (IS_TAG_EMPTY(0)) { \
                x87_stack_underflow(0, 0); \
                return 1; \
            } \
            i387cw_to_softfloat_status_word(&status, i387_get_control_word()); \
            b = x87_read_reg_ext(0); \
            a = use_var_ext; \
            if (!is_nan2) { \
                x87_div_80(result, a, b, &status); \
            } \
            if (!x87_checkexceptions(status.float_exception_flags, 0)) \
                x87_save_reg_ext(result, -1, 0, 0); \
            \
        } \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv##cycle_postfix) : ((x87_timings.fdiv##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv##cycle_postfix) : ((x87_concurrency.fdiv##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int opFMUL##name##_a##a_size(uint32_t fetchdat)                                                                                         \
    {                                                                                                                                              \
        floatx80 a, b, result; \
        float_status_t status; \
        optype t;                                                                                                                                  \
        optype_ext temp; \
        FP_ENTER();                                                                                                                                \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        if (cpu_use_dynarec) { \
            load_var = get();                                                                                                                          \
            if (cpu_state.abrt)                                                                                                                        \
                return 1; \
            ST(0) *= use_var;                                                                                                                          \
            FP_TAG_VALID;                                                                                                                              \
        } else { \
            load_var_ext = rw; \
            clear_C1(); \
            if (IS_TAG_EMPTY(0)) { \
                x87_stack_underflow(0, 0); \
                return 1; \
            } \
            i387cw_to_softfloat_status_word(&status, i387_get_control_word()); \
            a = x87_read_reg_ext(0); \
            b = use_var_ext; \
            if (!is_nan2) { \
                result = floatx80_mul(a, b, &status); \
            } \
            if (!x87_checkexceptions(status.float_exception_flags, 0)) \
                x87_save_reg_ext(result, -1, 0, 0); \
            \
        } \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul##cycle_postfix) : ((x87_timings.fmul##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul##cycle_postfix) : ((x87_concurrency.fmul##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int opFSUB##name##_a##a_size(uint32_t fetchdat)                                                                                         \
    {                                                                                                                                              \
        floatx80 a, b, result; \
        float_status_t status; \
        optype t;                                                                                                                                  \
        optype_ext temp; \
        FP_ENTER();                                                                                                                                \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        if (cpu_use_dynarec) { \
            load_var = get();                                                                                                                          \
            if (cpu_state.abrt)                                                                                                                        \
                return 1; \
            ST(0) -= use_var;                                                                                                                          \
            FP_TAG_VALID;                                                                                                                              \
        } else { \
            load_var_ext = rw; \
            clear_C1(); \
            if (IS_TAG_EMPTY(0)) { \
                x87_stack_underflow(0, 0); \
                return 1; \
            } \
            i387cw_to_softfloat_status_word(&status, i387_get_control_word()); \
            a = x87_read_reg_ext(0); \
            b = use_var_ext; \
            if (!is_nan2) { \
                result = floatx80_sub(a, b, &status); \
            } \
            if (!x87_checkexceptions(status.float_exception_flags, 0)) \
                x87_save_reg_ext(result, -1, 0, 0); \
            \
        } \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd##cycle_postfix) : ((x87_timings.fadd##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd##cycle_postfix) : ((x87_concurrency.fadd##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }                                                                                                                                              \
    static int opFSUBR##name##_a##a_size(uint32_t fetchdat)                                                                                        \
    {                                                                                                                                              \
        floatx80 a, b, result; \
        float_status_t status; \
        optype t;                                                                                                                                  \
        optype_ext temp; \
        FP_ENTER();                                                                                                                                \
        fetch_ea_##a_size(fetchdat);                                                                                                               \
        SEG_CHECK_READ(cpu_state.ea_seg);                                                                                                          \
        if (cpu_use_dynarec) { \
            load_var = get();                                                                                                                          \
            if (cpu_state.abrt)                                                                                                                        \
                return 1; \
            ST(0) = use_var - ST(0);                                                                                                                   \
            FP_TAG_VALID;                                                                                                                              \
        } else { \
            load_var_ext = rw; \
            clear_C1(); \
            if (IS_TAG_EMPTY(0)) { \
                x87_stack_underflow(0, 0); \
                return 1; \
            } \
            i387cw_to_softfloat_status_word(&status, i387_get_control_word()); \
            b = x87_read_reg_ext(0); \
            a = use_var_ext; \
            if (!is_nan2) { \
                result = floatx80_sub(a, b, &status); \
            } \
            if (!x87_checkexceptions(status.float_exception_flags, 0)) \
                x87_save_reg_ext(result, -1, 0, 0); \
            \
        } \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd##cycle_postfix) : ((x87_timings.fadd##cycle_postfix) * cpu_multi));           \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd##cycle_postfix) : ((x87_concurrency.fadd##cycle_postfix) * cpu_multi)); \
        return 0;                                                                                                                                  \
    }

// clang-format off
opFPU(s, x87_ts, float32, 16, t.i, temp, geteal, readmeml(easeg, cpu_state.eaaddr), t.s, float32_to_floatx80(temp, &status), floatx80_is_nan(a) || floatx80_is_unsupported(a) || float32_is_nan(temp), FPU_handle_NaN32(a, temp, &result, &status), _32)
#ifndef FPU_8087
opFPU(s, x87_ts, float32, 32, t.i, temp, geteal, readmeml(easeg, cpu_state.eaaddr), t.s, float32_to_floatx80(temp, &status), floatx80_is_nan(a) || floatx80_is_unsupported(a) || float32_is_nan(temp), FPU_handle_NaN32(a, temp, &result, &status), _32)
#endif
opFPU(d, x87_td, float64, 16, t.i, temp, geteaq, readmemq(easeg, cpu_state.eaaddr), t.d, float64_to_floatx80(temp, &status), floatx80_is_nan(a) || floatx80_is_unsupported(a) || float64_is_nan(temp), FPU_handle_NaN64(a, temp, &result, &status), _64)
#ifndef FPU_8087
opFPU(d, x87_td, float64, 32, t.i, temp, geteaq, readmemq(easeg, cpu_state.eaaddr), t.d, float64_to_floatx80(temp, &status), floatx80_is_nan(a) || floatx80_is_unsupported(a) || float64_is_nan(temp), FPU_handle_NaN64(a, temp, &result, &status), _64)
#endif

opFPU(iw, uint16_t, int16_t, 16, t, temp, geteaw, (int16_t)readmemw(easeg, cpu_state.eaaddr), (double) (int16_t) t, int32_to_floatx80((int32_t)temp), 0, 0, _i16)
#ifndef FPU_8087
opFPU(iw, uint16_t, int16_t, 32, t, temp, geteaw, (int16_t)readmemw(easeg, cpu_state.eaaddr), (double) (int16_t) t, int32_to_floatx80((int32_t)temp), 0, 0, _i16)
#endif
opFPU(il, uint32_t, int32_t, 16, t, temp, geteal, (int32_t)readmeml(easeg, cpu_state.eaaddr), (double) (int32_t) t, int32_to_floatx80(temp), 0, 0, _i32)
#ifndef FPU_8087
opFPU(il, uint32_t, int32_t, 32, t, temp, geteal, (int32_t)readmeml(easeg, cpu_state.eaaddr), (double) (int32_t) t, int32_to_floatx80(temp), 0, 0, _i32)
#endif
// clang-format on

static int
opFADD(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = ST(0) + ST(fetchdat & 7);
        FP_TAG_VALID;
    } else {
        pclog("FADD_st_sti.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(0, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        result = floatx80_add(a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, 0, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}
static int
opFADDr(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(fetchdat & 7) = ST(fetchdat & 7) + ST(0);
        FP_TAG_VALID_F;
    } else {
        pclog("FADD_sti_st.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(0, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(fetchdat & 7);
        b = x87_read_reg_ext(0);
        result = floatx80_add(a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}
static int
opFADDP(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;

    pclog("FADDP.\n");
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(fetchdat & 7) = ST(fetchdat & 7) + ST(0);
        FP_TAG_VALID_F;
        x87_pop();
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(0, 1);
            pclog("Underflow.\n");
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(fetchdat & 7);
        b = x87_read_reg_ext(0);
        pclog("FADDP: a = %08x, b = %08x.\n", a.exp, b.exp);
        result = floatx80_add(a, b, &status);
        pclog("Status = %08x.\n", status.float_exception_flags);
        if (!x87_checkexceptions(status.float_exception_flags, 0)) {
            pclog("Saving FADDP (check exceptions).\n");
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
            x87_pop_ext();
        } else
            pclog("Failed to save FADDP.\n");
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}

static int
opFCOM(uint32_t fetchdat)
{
    floatx80 a, b;
    float_status_t status;
    int rc;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        cpu_state.npxs &= ~(C0 | C2 | C3);
        if (ST(0) == ST(fetchdat & 7))
            cpu_state.npxs |= C3;
        else if (ST(0) < ST(fetchdat & 7))
            cpu_state.npxs |= C0;
    } else {
        clear_C1();
        //pclog("FCOM_sti.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            setcc(C0 | C2 | C3);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        rc = floatx80_compare_two(a, b, &status);
        setcc(x87_status_word_flags_fpu_compare(rc));
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            ;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
    return 0;
}

static int
opFCOMP(uint32_t fetchdat)
{
    floatx80 a, b;
    float_status_t status;
    int rc;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        cpu_state.npxs &= ~(C0 | C2 | C3);
        cpu_state.npxs |= x87_compare(ST(0), ST(fetchdat & 7));
        x87_pop();
    } else {
        clear_C1();
        //pclog("FCOMP_sti.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            setcc(C0 | C2 | C3);
            if (is_IA_masked()) {
                //pclog("POP invalid.\n");
                x87_pop_ext();
            }
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        rc = floatx80_compare_two(a, b, &status);
        setcc(x87_status_word_flags_fpu_compare(rc));
        if (!x87_checkexceptions(status.float_exception_flags, 0)) {
            //pclog("POP valid.\n");
            x87_pop_ext();
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
    return 0;
}

static int
opFCOMPP(uint32_t fetchdat)
{
    floatx80 a, b;
    float_status_t status;
    int rc;
    uint64_t *p, *q;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        cpu_state.npxs &= ~(C0 | C2 | C3);
        p = (uint64_t *) &ST(0);
        q = (uint64_t *) &ST(1);
        if ((*p == ((uint64_t) 1 << 63) && *q == 0) && (fpu_type >= FPU_287XL))
            cpu_state.npxs |= C0; /*Nasty hack to fix 80387 detection*/
        else
            cpu_state.npxs |= x87_compare(ST(0), ST(1));

        x87_pop();
        x87_pop();
    } else {
        clear_C1();
        //pclog("FCOMPP.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            setcc(C0 | C2 | C3);
            if (is_IA_masked()) {
                x87_pop_ext();
                x87_pop_ext();
            }
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(1);
        rc = floatx80_compare_two(a, b, &status);
        setcc(x87_status_word_flags_fpu_compare(rc));
        if (!x87_checkexceptions(status.float_exception_flags, 0)) {
            x87_pop_ext();
            x87_pop_ext();
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFUCOMPP(uint32_t fetchdat)
{
    floatx80 a, b;
    float_status_t status;
    int rc;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        cpu_state.npxs &= ~(C0 | C2 | C3);
        cpu_state.npxs |= x87_ucompare(ST(0), ST(1));
        x87_pop();
        x87_pop();
    } else {
        clear_C1();
        //pclog("FUCOMPP.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            setcc(C0 | C2 | C3);
            if (is_IA_masked()) {
                x87_pop_ext();
                x87_pop_ext();
            }
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(1);
        rc = floatx80_compare_quiet(a, b, &status);
        setcc(x87_status_word_flags_fpu_compare(rc));
        if (!x87_checkexceptions(status.float_exception_flags, 0)) {
            x87_pop_ext();
            x87_pop_ext();
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
    return 0;
}

static int
opFCOMI(uint32_t fetchdat)
{
    floatx80 a, b;
    float_status_t status;
    int rc;
    FP_ENTER();
    cpu_state.pc++;
    flags_rebuild();
    if (cpu_use_dynarec) {
        cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        if (ST(0) == ST(fetchdat & 7))
            cpu_state.flags |= Z_FLAG;
        else if (ST(0) < ST(fetchdat & 7))
            cpu_state.flags |= C_FLAG;
    } else {
        clear_C1();
        //pclog("FCOMI_sti.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            cpu_state.flags |= (Z_FLAG | P_FLAG | C_FLAG);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        rc = floatx80_compare_two(a, b, &status);
        x87_write_eflags_fpu_compare(rc);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            ;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
    return 0;
}
static int
opFCOMIP(uint32_t fetchdat)
{
    floatx80 a, b;
    float_status_t status;
    int rc;
    FP_ENTER();
    cpu_state.pc++;
    flags_rebuild();
    if (cpu_use_dynarec) {
        cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        if (ST(0) == ST(fetchdat & 7))
            cpu_state.flags |= Z_FLAG;
        else if (ST(0) < ST(fetchdat & 7))
            cpu_state.flags |= C_FLAG;
        x87_pop();
    } else {
        clear_C1();
        //pclog("FCOMIP.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            cpu_state.flags |= (Z_FLAG | P_FLAG | C_FLAG);
            if (is_IA_masked()) {
                x87_pop_ext();
            }
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        rc = floatx80_compare_two(a, b, &status);
        x87_write_eflags_fpu_compare(rc);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
    return 0;
}
#endif

static int
opFDIV(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        x87_div(ST(0), ST(0), ST(fetchdat & 7));
        FP_TAG_VALID;
    } else {
        clear_C1();
        //pclog("FDIV_st_sti.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(0, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        x87_div_80(result, a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, 0, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}
static int
opFDIVr(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        x87_div(ST(fetchdat & 7), ST(fetchdat & 7), ST(0));
        FP_TAG_VALID_F;
    } else {
        clear_C1();
        //pclog("FDIV_sti_st.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(fetchdat & 7, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(fetchdat & 7);
        b = x87_read_reg_ext(0);
        x87_div_80(result, a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}
static int
opFDIVP(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        x87_div(ST(fetchdat & 7), ST(fetchdat & 7), ST(0));
        FP_TAG_VALID_F;
        x87_pop();
    } else {
        clear_C1();
        //pclog("FDIVP.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(fetchdat & 7, 1);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(fetchdat & 7);
        b = x87_read_reg_ext(0);
        x87_div_80(result, a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0)) {
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
            x87_pop_ext();
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}

static int
opFDIVR(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        x87_div(ST(0), ST(fetchdat & 7), ST(0));
        FP_TAG_VALID;
    } else {
        clear_C1();
        //pclog("FDIVR_st_sti.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(0, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(fetchdat & 7);
        b = x87_read_reg_ext(0);
        x87_div_80(result, a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, 0, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}
static int
opFDIVRr(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        x87_div(ST(fetchdat & 7), ST(0), ST(fetchdat & 7));
        FP_TAG_VALID_F;
    } else {
        clear_C1();
        //pclog("FDIVR_sti_st.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(fetchdat & 7, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        x87_div_80(result, a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}
static int
opFDIVRP(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        x87_div(ST(fetchdat & 7), ST(0), ST(fetchdat & 7));
        FP_TAG_VALID_F;
        x87_pop();
    } else {
        clear_C1();
        //pclog("FDIVRP.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(fetchdat & 7, 1);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        x87_div_80(result, a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0)) {
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
            x87_pop_ext();
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
    return 0;
}

static int
opFMUL(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = ST(0) * ST(fetchdat & 7);
        FP_TAG_VALID;
    } else {
        clear_C1();
        //pclog("FMUL_st_sti.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(0, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        result = floatx80_mul(a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, 0, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul) : (x87_timings.fmul * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul) : (x87_concurrency.fmul * cpu_multi));
    return 0;
}
static int
opFMULr(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(fetchdat & 7) = ST(0) * ST(fetchdat & 7);
        FP_TAG_VALID_F;
    } else {
        clear_C1();
        //pclog("FMUL_sti_st.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(fetchdat & 7, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        result = floatx80_mul(a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul) : (x87_timings.fmul * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul) : (x87_concurrency.fmul * cpu_multi));
    return 0;
}
static int
opFMULP(uint32_t fetchdat)
{
    //pclog("FMULP.\n");
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(fetchdat & 7) = ST(fetchdat & 7) * ST(0);
        FP_TAG_VALID_F;
        x87_pop();
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(fetchdat & 7, 1);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(fetchdat & 7);
        b = x87_read_reg_ext(0);
        result = floatx80_mul(a, b, &status);
        pclog("FMULP result = %08x.\n", &result);
        if (!x87_checkexceptions(status.float_exception_flags, 0)) {
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
            x87_pop_ext();
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul) : (x87_timings.fmul * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul) : (x87_concurrency.fmul * cpu_multi));
    return 0;
}

static int
opFSUB(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = ST(0) - ST(fetchdat & 7);
        FP_TAG_VALID;
    } else {
        clear_C1();
        //pclog("FSUB_st_sti.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(0, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        result = floatx80_sub(a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, 0, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}
static int
opFSUBr(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(fetchdat & 7) = ST(fetchdat & 7) - ST(0);
        FP_TAG_VALID_F;
    } else {
        clear_C1();
        //pclog("FSUB_sti_st.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(fetchdat & 7, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(fetchdat & 7);
        b = x87_read_reg_ext(0);
        result = floatx80_sub(a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}
static int
opFSUBP(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(fetchdat & 7) = ST(fetchdat & 7) - ST(0);
        FP_TAG_VALID_F;
        x87_pop();
    } else {
        clear_C1();
        //pclog("FSUBP.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(fetchdat & 7, 1);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(fetchdat & 7);
        b = x87_read_reg_ext(0);
        result = floatx80_sub(a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0)) {
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
            x87_pop_ext();
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}

static int
opFSUBR(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = ST(fetchdat & 7) - ST(0);
        FP_TAG_VALID;
    } else {
        clear_C1();
        //pclog("FSUBR_st_sti.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(0, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(fetchdat & 7);
        b = x87_read_reg_ext(0);
        result = floatx80_sub(a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, 0, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}
static int
opFSUBRr(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(fetchdat & 7) = ST(0) - ST(fetchdat & 7);
        FP_TAG_VALID_F;
    } else {
        clear_C1();
        //pclog("FSUBR_sti_st.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(fetchdat & 7, 0);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        result = floatx80_sub(a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}
static int
opFSUBRP(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(fetchdat & 7) = ST(0) - ST(fetchdat & 7);
        FP_TAG_VALID_F;
        x87_pop();
    } else {
        clear_C1();
        //pclog("FSUBRP.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_stack_underflow(fetchdat & 7, 1);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        result = floatx80_sub(a, b, &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0)) {
            x87_save_reg_ext(result, -1, fetchdat & 7, 0);
            x87_pop_ext();
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
    return 0;
}

#ifndef FPU_8087
static int
opFUCOM(uint32_t fetchdat)
{
    floatx80 a, b;
    float_status_t status;
    int rc;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        cpu_state.npxs &= ~(C0 | C2 | C3);
        cpu_state.npxs |= x87_ucompare(ST(0), ST(fetchdat & 7));
    } else {
        clear_C1();
        //pclog("FUCOM_sti.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            setcc(C0 | C2 | C3);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        rc = floatx80_compare_quiet(a, b, &status);
        setcc(x87_status_word_flags_fpu_compare(rc));
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            ;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
    return 0;
}

static int
opFUCOMP(uint32_t fetchdat)
{
    floatx80 a, b;
    float_status_t status;
    int rc;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        cpu_state.npxs &= ~(C0 | C2 | C3);
        cpu_state.npxs |= x87_ucompare(ST(0), ST(fetchdat & 7));
        x87_pop();
    } else {
        clear_C1();
        //pclog("FUCOMP.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            setcc(C0 | C2 | C3);
            if (is_IA_masked())
                x87_pop_ext();

            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        rc = floatx80_compare_quiet(a, b, &status);
        setcc(x87_status_word_flags_fpu_compare(rc));
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
    return 0;
}

static int
opFUCOMI(uint32_t fetchdat)
{
    floatx80 a, b;
    float_status_t status;
    int rc;
    FP_ENTER();
    cpu_state.pc++;
    flags_rebuild();
    if (cpu_use_dynarec) {
        cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        if (ST(0) == ST(fetchdat & 7))
            cpu_state.flags |= Z_FLAG;
        else if (ST(0) < ST(fetchdat & 7))
            cpu_state.flags |= C_FLAG;
    } else {
        clear_C1();
        //pclog("FUCOMI_sti.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            cpu_state.flags |= (Z_FLAG | P_FLAG | C_FLAG);
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        rc = floatx80_compare_quiet(a, b, &status);
        x87_write_eflags_fpu_compare(rc);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            ;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
    return 0;
}
static int
opFUCOMIP(uint32_t fetchdat)
{
    floatx80 a, b;
    float_status_t status;
    int rc;
    FP_ENTER();
    cpu_state.pc++;
    flags_rebuild();
    if (cpu_use_dynarec) {
        cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        if (ST(0) == ST(fetchdat & 7))
            cpu_state.flags |= Z_FLAG;
        else if (ST(0) < ST(fetchdat & 7))
            cpu_state.flags |= C_FLAG;
        x87_pop();
    } else {
        clear_C1();
        //pclog("FUCOMIP.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(fetchdat & 7)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            cpu_state.flags |= (Z_FLAG | P_FLAG | C_FLAG);
            if (is_IA_masked())
                x87_pop_ext();

            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        a = x87_read_reg_ext(0);
        b = x87_read_reg_ext(fetchdat & 7);
        rc = floatx80_compare_quiet(a, b, &status);
        x87_write_eflags_fpu_compare(rc);
        if (!x87_checkexceptions(status.float_exception_flags, 0))
            x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
    return 0;
}
#endif

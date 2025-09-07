static int
sf_F2XM1(uint32_t fetchdat)
{
    floatx80                    result;
    struct softfloat_status_t   status;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word() | FPU_PR_80_BITS);
    result = f2xm1(FPU_read_regi(0), &status);
    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0))
        FPU_save_regi(result, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.f2xm1) : (x87_timings.f2xm1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.f2xm1) : (x87_concurrency.f2xm1 * cpu_multi));
    return 0;
}

static int
sf_FYL2X(uint32_t fetchdat)
{
    floatx80                    result;
    struct softfloat_status_t   status;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
        FPU_stack_underflow(fetchdat, 1, 1);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word() | FPU_PR_80_BITS);
    result = fyl2x(FPU_read_regi(0), FPU_read_regi(1), &status);
    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0)) {
        FPU_pop();
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fyl2x) : (x87_timings.fyl2x * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fyl2x) : (x87_concurrency.fyl2x * cpu_multi));
    return 0;
}

static int
sf_FPTAN(uint32_t fetchdat)
{
    const floatx80              floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    floatx80                    y;
    struct softfloat_status_t   status;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    clear_C2();
    if (IS_TAG_EMPTY(0) || !IS_TAG_EMPTY(-1)) {
        if (IS_TAG_EMPTY(0))
            FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        else
            FPU_exception(fetchdat, FPU_EX_Stack_Overflow, 0);

        /* The masked response */
        if (is_IA_masked()) {
            FPU_save_regi(floatx80_default_nan, 0);
            FPU_push();
            FPU_save_regi(floatx80_default_nan, 0);
        }
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word() | FPU_PR_80_BITS);
    y      = FPU_read_regi(0);
    if (ftan(&y, &status) == -1) {
        fpu_state.swd |= FPU_SW_C2;
        goto next_ins;
    }

    if (extF80_isNaN(y)) {
        if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0)) {
            FPU_save_regi(y, 0);
            FPU_push();
            FPU_save_regi(y, 0);
        }
        goto next_ins;
    }

    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0)) {
        FPU_save_regi(y, 0);
        FPU_push();
        FPU_save_regi(Const_1, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fptan) : (x87_timings.fptan * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fptan) : (x87_concurrency.fptan * cpu_multi));
    return 0;
}

static int
sf_FPATAN(uint32_t fetchdat)
{
    floatx80                    a;
    floatx80                    b;
    floatx80                    result;
    struct softfloat_status_t   status;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
        FPU_stack_underflow(fetchdat, 1, 1);
        goto next_ins;
    }
    a      = FPU_read_regi(0);
    b      = FPU_read_regi(1);
    status = i387cw_to_softfloat_status_word(i387_get_control_word() | FPU_PR_80_BITS);
    result = fpatan(a, b, &status);
    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0)) {
        FPU_pop();
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fpatan) : (x87_timings.fpatan * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fpatan) : (x87_concurrency.fpatan * cpu_multi));
    return 0;
}

static int
sf_FXTRACT(uint32_t fetchdat)
{
    struct softfloat_status_t   status;
    floatx80                    a;
    floatx80                    b;
#if 0
    const floatx80              floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
#endif

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();

#if 0 // TODO
    if ((IS_TAG_EMPTY(0) || IS_TAG_EMPTY(-1))) {
        if (IS_TAG_EMPTY(0))
            FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        else
            FPU_exception(fetchdat, FPU_EX_Stack_Overflow, 0);

        /* The masked response */
        if (is_IA_masked()) {
            FPU_save_regi(floatx80_default_nan, 0);
            FPU_push();
            FPU_save_regi(floatx80_default_nan, 0);
        }
        goto next_ins;
    }
#endif

    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a      = FPU_read_regi(0);
    b      = extF80_extract(&a, &status);
    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0)) {
        FPU_save_regi(b, 0); // exponent
        FPU_push();
        FPU_save_regi(a, 0); // fraction
    }

#if 0 // TODO.
next_ins:
#endif
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fxtract) : (x87_timings.fxtract * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fxtract) : (x87_concurrency.fxtract * cpu_multi));
    return 0;
}

static int
sf_FPREM1(uint32_t fetchdat)
{
    floatx80                    a;
    floatx80                    b;
    floatx80                    result;
    struct softfloat_status_t   status;
    uint64_t                    quotient = 0;
    int                         flags;
    int                         cc;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    clear_C2();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a      = FPU_read_regi(0);
    b      = FPU_read_regi(1);
    flags  = floatx80_ieee754_remainder(a, b, &result, &quotient, &status);
    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0)) {
        if (flags >= 0) {
            cc = 0;
            if (flags)
                cc = FPU_SW_C2;
            else {
                if (quotient & 1)
                    cc |= FPU_SW_C1;
                if (quotient & 2)
                    cc |= FPU_SW_C3;
                if (quotient & 4)
                    cc |= FPU_SW_C0;
            }
            setcc(cc);
        }
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fprem1) : (x87_timings.fprem1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fprem1) : (x87_concurrency.fprem1 * cpu_multi));
    return 0;
}

static int
sf_FPREM(uint32_t fetchdat)
{
    floatx80                    a;
    floatx80                    b;
    floatx80                    result;
    struct softfloat_status_t   status;
    uint64_t                    quotient = 0;
    int                         flags;
    int                         cc;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    clear_C2();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    a      = FPU_read_regi(0);
    b      = FPU_read_regi(1);
    // handle unsupported extended double-precision floating encodings
    flags = floatx80_remainder(a, b, &result, &quotient, &status);
    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0)) {
        if (flags >= 0) {
            cc = 0;
            if (flags)
                cc = FPU_SW_C2;
            else {
                if (quotient & 1)
                    cc |= FPU_SW_C1;
                if (quotient & 2)
                    cc |= FPU_SW_C3;
                if (quotient & 4)
                    cc |= FPU_SW_C0;
            }
            setcc(cc);
        }
        FPU_save_regi(result, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fprem) : (x87_timings.fprem * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fprem) : (x87_concurrency.fprem * cpu_multi));
    return 0;
}

static int
sf_FYL2XP1(uint32_t fetchdat)
{
    floatx80                    result;
    struct softfloat_status_t   status;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
        FPU_stack_underflow(fetchdat, 1, 1);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word() | FPU_PR_80_BITS);
    result = fyl2xp1(FPU_read_regi(0), FPU_read_regi(1), &status);
    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0)) {
        FPU_save_regi(result, 1);
        FPU_pop();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fyl2xp1) : (x87_timings.fyl2xp1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fyl2xp1) : (x87_concurrency.fyl2xp1 * cpu_multi));
    return 0;
}

#ifndef FPU_8087
static int
sf_FSINCOS(uint32_t fetchdat)
{
    const floatx80              floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    struct softfloat_status_t   status;
    floatx80                    y;
    floatx80                    sin_y;
    floatx80                    cos_y;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    clear_C2();
    if (IS_TAG_EMPTY(0) || !IS_TAG_EMPTY(-1)) {
        if (IS_TAG_EMPTY(0))
            FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        else
            FPU_exception(fetchdat, FPU_EX_Stack_Overflow, 0);

        /* The masked response */
        if (is_IA_masked()) {
            FPU_save_regi(floatx80_default_nan, 0);
            FPU_push();
            FPU_save_regi(floatx80_default_nan, 0);
        }
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word() | FPU_PR_80_BITS);
    y      = FPU_read_regi(0);
    if (fsincos(y, &sin_y, &cos_y, &status) == -1) {
        fpu_state.swd |= FPU_SW_C2;
        goto next_ins;
    }
    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0)) {
        FPU_save_regi(sin_y, 0);
        FPU_push();
        FPU_save_regi(cos_y, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsincos) : (x87_timings.fsincos * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsincos) : (x87_concurrency.fsincos * cpu_multi));
    return 0;
}
#endif

static int
sf_FSCALE(uint32_t fetchdat)
{
    floatx80                    result;
    struct softfloat_status_t   status;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word());
    result = extF80_scale(FPU_read_regi(0), FPU_read_regi(1), &status);
    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0))
        FPU_save_regi(result, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fscale) : (x87_timings.fscale * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fscale) : (x87_concurrency.fscale * cpu_multi));
    return 0;
}

#ifndef FPU_8087
static int
sf_FSIN(uint32_t fetchdat)
{
    floatx80                    y;
    struct softfloat_status_t   status;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    clear_C2();
    if (IS_TAG_EMPTY(0)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word() | FPU_PR_80_BITS);
    y      = FPU_read_regi(0);
    if (fsin(&y, &status) == -1) {
        fpu_state.swd |= FPU_SW_C2;
        goto next_ins;
    }
    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0))
        FPU_save_regi(y, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsin_cos) : (x87_timings.fsin_cos * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsin_cos) : (x87_concurrency.fsin_cos * cpu_multi));
    return 0;
}

static int
sf_FCOS(uint32_t fetchdat)
{
    floatx80                    y;
    struct softfloat_status_t   status;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    clear_C2();
    if (IS_TAG_EMPTY(0)) {
        FPU_stack_underflow(fetchdat, 0, 0);
        goto next_ins;
    }
    status = i387cw_to_softfloat_status_word(i387_get_control_word() | FPU_PR_80_BITS);
    y      = FPU_read_regi(0);
    if (fcos(&y, &status) == -1) {
        fpu_state.swd |= FPU_SW_C2;
        goto next_ins;
    }
    if (!FPU_exception(fetchdat, status.softfloat_exceptionFlags, 0))
        FPU_save_regi(y, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsin_cos) : (x87_timings.fsin_cos * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsin_cos) : (x87_concurrency.fsin_cos * cpu_multi));
    return 0;
}
#endif

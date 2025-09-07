static int
sf_FXCH_sti(uint32_t fetchdat)
{
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    floatx80       st0_reg;
    floatx80       sti_reg;
    int            st0_tag;
    int            sti_tag;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    st0_tag = FPU_gettagi(0);
    sti_tag = FPU_gettagi(fetchdat & 7);
    st0_reg = FPU_read_regi(0);
    sti_reg = FPU_read_regi(fetchdat & 7);

    clear_C1();
    if ((st0_tag == X87_TAG_EMPTY) || (sti_tag == X87_TAG_EMPTY)) {
        FPU_exception(fetchdat, FPU_EX_Stack_Underflow, 0);
        if (is_IA_masked()) {
            /* Masked response */
            if (st0_tag == X87_TAG_EMPTY)
                st0_reg = floatx80_default_nan;
            if (sti_tag == X87_TAG_EMPTY)
                sti_reg = floatx80_default_nan;
        } else
            goto next_ins;
    }
    FPU_save_regi(st0_reg, fetchdat & 7);
    FPU_save_regi(sti_reg, 0);

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fxch) : (x87_timings.fxch * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fxch) : (x87_concurrency.fxch * cpu_multi));
    return 0;
}

static int
sf_FCHS(uint32_t fetchdat)
{
    floatx80 st0_reg;
    floatx80 result;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    if (IS_TAG_EMPTY(0))
        FPU_stack_underflow(fetchdat, 0, 0);
    else {
        clear_C1();
        st0_reg = FPU_read_regi(0);
        result  = floatx80_chs(st0_reg);
        FPU_save_regi(result, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fchs) : (x87_timings.fchs * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fchs) : (x87_concurrency.fchs * cpu_multi));
    return 0;
}

static int
sf_FABS(uint32_t fetchdat)
{
    floatx80 st0_reg;
    floatx80 result;

    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    if (IS_TAG_EMPTY(0))
        FPU_stack_underflow(fetchdat, 0, 0);
    else {
        clear_C1();
        st0_reg = FPU_read_regi(0);
        result  = floatx80_abs(st0_reg);
        FPU_save_regi(result, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fabs) : (x87_timings.fabs * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fabs) : (x87_concurrency.fabs * cpu_multi));
    return 0;
}

static int
sf_FDECSTP(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    fpu_state.tos = (fpu_state.tos - 1) & 7;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fincdecstp) : (x87_timings.fincdecstp * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fincdecstp) : (x87_concurrency.fincdecstp * cpu_multi));
    return 0;
}

static int
sf_FINCSTP(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    fpu_state.tos = (fpu_state.tos + 1) & 7;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fincdecstp) : (x87_timings.fincdecstp * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fincdecstp) : (x87_concurrency.fincdecstp * cpu_multi));
    return 0;
}

static int
sf_FFREE_sti(uint32_t fetchdat)
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    FPU_settagi(X87_TAG_EMPTY, fetchdat & 7);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.ffree) : (x87_timings.ffree * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.ffree) : (x87_concurrency.ffree * cpu_multi));
    return 0;
}

static int
sf_FFREEP_sti(uint32_t fetchdat)
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    FPU_settagi(X87_TAG_EMPTY, fetchdat & 7);
    if (cpu_state.abrt)
        return 1;

    FPU_pop();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.ffree) : (x87_timings.ffree * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.ffree) : (x87_concurrency.ffree * cpu_multi));
    return 0;
}

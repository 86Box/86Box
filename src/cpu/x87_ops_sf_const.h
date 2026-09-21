/* A fast way to find out whether x is one of RC_DOWN or RC_CHOP
   (and not one of RC_RND or RC_UP).
   */
#define DOWN_OR_CHOP() (fpu_state.cwd & FPU_CW_RC & FPU_RC_DOWN)

static int
sf_FLDL2T(uint32_t fetchdat)
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (!IS_TAG_EMPTY(-1))
        FPU_stack_overflow(fetchdat);
    else {
        FPU_push();
        FPU_save_regi(FPU_round_const(Const_L2T, (fpu_state.cwd & FPU_CW_RC) == FPU_RC_UP), 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
sf_FLDL2E(uint32_t fetchdat)
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (!IS_TAG_EMPTY(-1))
        FPU_stack_overflow(fetchdat);
    else {
        FPU_push();
        FPU_save_regi(FPU_round_const(Const_L2E, DOWN_OR_CHOP() ? -1 : 0), 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
sf_FLDPI(uint32_t fetchdat)
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (!IS_TAG_EMPTY(-1))
        FPU_stack_overflow(fetchdat);
    else {
        FPU_push();
        FPU_save_regi(FPU_round_const(Const_PI, DOWN_OR_CHOP() ? -1 : 0), 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
sf_FLDEG2(uint32_t fetchdat)
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (!IS_TAG_EMPTY(-1))
        FPU_stack_overflow(fetchdat);
    else {
        FPU_push();
        FPU_save_regi(FPU_round_const(Const_LG2, DOWN_OR_CHOP() ? -1 : 0), 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
sf_FLDLN2(uint32_t fetchdat)
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (!IS_TAG_EMPTY(-1))
        FPU_stack_overflow(fetchdat);
    else {
        FPU_push();
        FPU_save_regi(FPU_round_const(Const_LN2, DOWN_OR_CHOP() ? -1 : 0), 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
sf_FLD1(uint32_t fetchdat)
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (!IS_TAG_EMPTY(-1))
        FPU_stack_overflow(fetchdat);
    else {
        FPU_push();
        FPU_save_regi(Const_1, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_z1) : (x87_timings.fld_z1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_z1) : (x87_concurrency.fld_z1 * cpu_multi));
    return 0;
}

static int
sf_FLDZ(uint32_t fetchdat)
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    clear_C1();
    if (!IS_TAG_EMPTY(-1))
        FPU_stack_overflow(fetchdat);
    else {
        FPU_push();
        FPU_save_regi(Const_Z, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_z1) : (x87_timings.fld_z1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_z1) : (x87_concurrency.fld_z1 * cpu_multi));
    return 0;
}

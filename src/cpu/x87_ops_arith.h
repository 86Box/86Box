#define opFPU(name, optype, a_size, load_var, get, use_var, cycle_postfix)     \
static int opFADD ## name ## _a ## a_size(uint32_t fetchdat)    \
{                                                               \
        optype t;                                               \
        FP_ENTER();                                             \
        fetch_ea_ ## a_size(fetchdat);                          \
	SEG_CHECK_READ(cpu_state.ea_seg);                       \
        load_var = get(); if (cpu_state.abrt) return 1;                   \
        if ((cpu_state.npxc >> 10) & 3)                                   \
                fesetround(rounding_modes[(cpu_state.npxc >> 10) & 3]);   \
        ST(0) += use_var;                                       \
        if ((cpu_state.npxc >> 10) & 3)                                   \
                fesetround(FE_TONEAREST);                       \
        FP_TAG_VALID;		\
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd ## cycle_postfix) : ((x87_timings.fadd ## cycle_postfix) * cpu_multi)); \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd ## cycle_postfix) : ((x87_concurrency.fadd ## cycle_postfix) * cpu_multi)); \
        return 0;                                               \
}                                                               \
static int opFCOM ## name ## _a ## a_size(uint32_t fetchdat)    \
{                                                               \
        optype t;                                               \
        FP_ENTER();                                             \
        fetch_ea_ ## a_size(fetchdat);                          \
	SEG_CHECK_READ(cpu_state.ea_seg);                       \
        load_var = get(); if (cpu_state.abrt) return 1;                   \
        cpu_state.npxs &= ~(C0|C2|C3);                                    \
        cpu_state.npxs |= x87_compare(ST(0), (double)use_var);            \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom ## cycle_postfix) : ((x87_timings.fcom ## cycle_postfix) * cpu_multi));                                        \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom ## cycle_postfix) : ((x87_concurrency.fcom ## cycle_postfix) * cpu_multi)); \
        return 0;                                               \
}                                                               \
static int opFCOMP ## name ## _a ## a_size(uint32_t fetchdat)   \
{                                                               \
        optype t;                                               \
        FP_ENTER();                                             \
        fetch_ea_ ## a_size(fetchdat);                          \
	SEG_CHECK_READ(cpu_state.ea_seg);                       \
        load_var = get(); if (cpu_state.abrt) return 1;                   \
        cpu_state.npxs &= ~(C0|C2|C3);                                    \
        cpu_state.npxs |= x87_compare(ST(0), (double)use_var);            \
        x87_pop();                                              \
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom ## cycle_postfix) : ((x87_timings.fcom ## cycle_postfix) * cpu_multi));                                        \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom ## cycle_postfix) : ((x87_concurrency.fcom ## cycle_postfix) * cpu_multi)); \
        return 0;                                               \
}                                                               \
static int opFDIV ## name ## _a ## a_size(uint32_t fetchdat)    \
{                                                               \
        optype t;                                               \
        FP_ENTER();                                             \
        fetch_ea_ ## a_size(fetchdat);                          \
	SEG_CHECK_READ(cpu_state.ea_seg);                       \
        load_var = get(); if (cpu_state.abrt) return 1;                   \
        x87_div(ST(0), ST(0), use_var);                         \
        FP_TAG_VALID;						\
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv ## cycle_postfix) : ((x87_timings.fdiv ## cycle_postfix) * cpu_multi));                                       \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd ## cycle_postfix) : ((x87_concurrency.fadd ## cycle_postfix) * cpu_multi)); \
        return 0;                                               \
}                                                               \
static int opFDIVR ## name ## _a ## a_size(uint32_t fetchdat)   \
{                                                               \
        optype t;                                               \
        FP_ENTER();                                             \
        fetch_ea_ ## a_size(fetchdat);                          \
	SEG_CHECK_READ(cpu_state.ea_seg);                       \
        load_var = get(); if (cpu_state.abrt) return 1;                   \
        x87_div(ST(0), use_var, ST(0));                         \
        FP_TAG_VALID;						\
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv ## cycle_postfix) : ((x87_timings.fdiv ## cycle_postfix) * cpu_multi));                                       \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv ## cycle_postfix) : ((x87_concurrency.fdiv ## cycle_postfix) * cpu_multi)); \
        return 0;                                               \
}                                                               \
static int opFMUL ## name ## _a ## a_size(uint32_t fetchdat)    \
{                                                               \
        optype t;                                               \
        FP_ENTER();                                             \
        fetch_ea_ ## a_size(fetchdat);                          \
	SEG_CHECK_READ(cpu_state.ea_seg);                       \
        load_var = get(); if (cpu_state.abrt) return 1;                   \
        ST(0) *= use_var;                                       \
        FP_TAG_VALID;						\
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul ## cycle_postfix) : ((x87_timings.fmul ## cycle_postfix) * cpu_multi));                                       \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul ## cycle_postfix) : ((x87_concurrency.fmul ## cycle_postfix) * cpu_multi)); \
        return 0;                                               \
}                                                               \
static int opFSUB ## name ## _a ## a_size(uint32_t fetchdat)    \
{                                                               \
        optype t;                                               \
        FP_ENTER();                                             \
        fetch_ea_ ## a_size(fetchdat);                          \
	SEG_CHECK_READ(cpu_state.ea_seg);                       \
        load_var = get(); if (cpu_state.abrt) return 1;                   \
        ST(0) -= use_var;                                       \
        FP_TAG_VALID;						\
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd ## cycle_postfix) : ((x87_timings.fadd ## cycle_postfix) * cpu_multi));                                        \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd ## cycle_postfix) : ((x87_concurrency.fadd ## cycle_postfix) * cpu_multi)); \
        return 0;                                               \
}                                                               \
static int opFSUBR ## name ## _a ## a_size(uint32_t fetchdat)   \
{                                                               \
        optype t;                                               \
        FP_ENTER();                                             \
        fetch_ea_ ## a_size(fetchdat);                          \
	SEG_CHECK_READ(cpu_state.ea_seg);                       \
        load_var = get(); if (cpu_state.abrt) return 1;                   \
        ST(0) = use_var - ST(0);                                \
        FP_TAG_VALID;						\
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd ## cycle_postfix) : ((x87_timings.fadd ## cycle_postfix) * cpu_multi));                                        \
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd ## cycle_postfix) : ((x87_concurrency.fadd ## cycle_postfix) * cpu_multi)); \
        return 0;                                               \
}


opFPU(s, x87_ts, 16, t.i, geteal, t.s, _32)
#ifndef FPU_8087
opFPU(s, x87_ts, 32, t.i, geteal, t.s, _32)
#endif
opFPU(d, x87_td, 16, t.i, geteaq, t.d, _64)
#ifndef FPU_8087
opFPU(d, x87_td, 32, t.i, geteaq, t.d, _64)
#endif

opFPU(iw, uint16_t, 16, t, geteaw, (double)(int16_t)t, _i16)
#ifndef FPU_8087
opFPU(iw, uint16_t, 32, t, geteaw, (double)(int16_t)t, _i16)
#endif
opFPU(il, uint32_t, 16, t, geteal, (double)(int32_t)t, _i32)
#ifndef FPU_8087
opFPU(il, uint32_t, 32, t, geteal, (double)(int32_t)t, _i32)
#endif


static int opFADD(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(0) = ST(0) + ST(fetchdat & 7);
	FP_TAG_VALID;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
        return 0;
}
static int opFADDr(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(fetchdat & 7) = ST(fetchdat & 7) + ST(0);
	FP_TAG_VALID_F;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
        return 0;
}
static int opFADDP(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(fetchdat & 7) = ST(fetchdat & 7) + ST(0);
	FP_TAG_VALID_F;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
        return 0;
}

static int opFCOM(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        cpu_state.npxs &= ~(C0|C2|C3);
        if (ST(0) == ST(fetchdat & 7))     cpu_state.npxs |= C3;
        else if (ST(0) < ST(fetchdat & 7)) cpu_state.npxs |= C0;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
        return 0;
}

static int opFCOMP(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        cpu_state.npxs &= ~(C0|C2|C3);
        cpu_state.npxs |= x87_compare(ST(0), ST(fetchdat & 7));
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
        return 0;
}

static int opFCOMPP(uint32_t fetchdat)
{
	uint64_t *p, *q;
        FP_ENTER();
        cpu_state.pc++;
        cpu_state.npxs &= ~(C0|C2|C3);
	p = (uint64_t *)&ST(0);
	q = (uint64_t *)&ST(1);
        if ((*p == ((uint64_t)1 << 63) && *q == 0) && (fpu_type >= FPU_287XL))
                cpu_state.npxs |= C0; /*Nasty hack to fix 80387 detection*/
        else
                cpu_state.npxs |= x87_compare(ST(0), ST(1));

        x87_pop();
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFUCOMPP(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        cpu_state.npxs &= ~(C0|C2|C3);
        cpu_state.npxs |= x87_ucompare(ST(0), ST(1));
        x87_pop();
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
        return 0;
}

static int opFCOMI(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        flags_rebuild();
        cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        if (ST(0) == ST(fetchdat & 7))     cpu_state.flags |= Z_FLAG;
        else if (ST(0) < ST(fetchdat & 7)) cpu_state.flags |= C_FLAG;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
        return 0;
}
static int opFCOMIP(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        flags_rebuild();
        cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        if (ST(0) == ST(fetchdat & 7))     cpu_state.flags |= Z_FLAG;
        else if (ST(0) < ST(fetchdat & 7)) cpu_state.flags |= C_FLAG;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fcom) : (x87_timings.fcom * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fcom) : (x87_concurrency.fcom * cpu_multi));
        return 0;
}
#endif

static int opFDIV(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        x87_div(ST(0), ST(0), ST(fetchdat & 7));
	FP_TAG_VALID;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
        return 0;
}
static int opFDIVr(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        x87_div(ST(fetchdat & 7), ST(fetchdat & 7), ST(0));
	FP_TAG_VALID_F;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
        return 0;
}
static int opFDIVP(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        x87_div(ST(fetchdat & 7), ST(fetchdat & 7), ST(0));
	FP_TAG_VALID_F;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
        return 0;
}

static int opFDIVR(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        x87_div(ST(0), ST(fetchdat&7), ST(0));
	FP_TAG_VALID;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
        return 0;
}
static int opFDIVRr(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        x87_div(ST(fetchdat & 7), ST(0), ST(fetchdat & 7));
	FP_TAG_VALID_F;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
        return 0;
}
static int opFDIVRP(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        x87_div(ST(fetchdat & 7), ST(0), ST(fetchdat & 7));
	FP_TAG_VALID_F;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fdiv) : (x87_timings.fdiv * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fdiv) : (x87_concurrency.fdiv * cpu_multi));
        return 0;
}

static int opFMUL(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(0) = ST(0) * ST(fetchdat & 7);
	FP_TAG_VALID;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul) : (x87_timings.fmul * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul) : (x87_concurrency.fmul * cpu_multi));
        return 0;
}
static int opFMULr(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(fetchdat & 7) = ST(0) * ST(fetchdat & 7);
	FP_TAG_VALID_F;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul) : (x87_timings.fmul * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul) : (x87_concurrency.fmul * cpu_multi));
        return 0;
}
static int opFMULP(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(fetchdat & 7) = ST(0) * ST(fetchdat & 7);
	FP_TAG_VALID_F;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fmul) : (x87_timings.fmul * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fmul) : (x87_concurrency.fmul * cpu_multi));
        return 0;
}

static int opFSUB(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(0) = ST(0) - ST(fetchdat & 7);
	FP_TAG_VALID;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
        return 0;
}
static int opFSUBr(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(fetchdat & 7) = ST(fetchdat & 7) - ST(0);
	FP_TAG_VALID_F;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
        return 0;
}
static int opFSUBP(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(fetchdat & 7) = ST(fetchdat & 7) - ST(0);
	FP_TAG_VALID_F;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
        return 0;
}

static int opFSUBR(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(0) = ST(fetchdat & 7) - ST(0);
	FP_TAG_VALID;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
        return 0;
}
static int opFSUBRr(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(fetchdat & 7) = ST(0) - ST(fetchdat & 7);
	FP_TAG_VALID_F;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
        return 0;
}
static int opFSUBRP(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        ST(fetchdat & 7) = ST(0) - ST(fetchdat & 7);
	FP_TAG_VALID_F;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fadd) : (x87_timings.fadd * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fadd) : (x87_concurrency.fadd * cpu_multi));
        return 0;
}

#ifndef FPU_8087
static int opFUCOM(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        cpu_state.npxs &= ~(C0|C2|C3);
        cpu_state.npxs |= x87_ucompare(ST(0), ST(fetchdat & 7));
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
        return 0;
}

static int opFUCOMP(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        cpu_state.npxs &= ~(C0|C2|C3);
        cpu_state.npxs |= x87_ucompare(ST(0), ST(fetchdat & 7));
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
        return 0;
}

static int opFUCOMI(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        flags_rebuild();
        cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        if (ST(0) == ST(fetchdat & 7))     cpu_state.flags |= Z_FLAG;
        else if (ST(0) < ST(fetchdat & 7)) cpu_state.flags |= C_FLAG;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
        return 0;
}
static int opFUCOMIP(uint32_t fetchdat)
{
        FP_ENTER();
        cpu_state.pc++;
        flags_rebuild();
        cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        if (ST(0) == ST(fetchdat & 7))     cpu_state.flags |= Z_FLAG;
        else if (ST(0) < ST(fetchdat & 7)) cpu_state.flags |= C_FLAG;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fucom) : (x87_timings.fucom * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fucom) : (x87_concurrency.fucom * cpu_multi));
        return 0;
}
#endif

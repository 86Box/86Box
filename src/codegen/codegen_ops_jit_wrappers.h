/*
 * codegen_ops_jit_wrappers.h — noinline wrappers for JIT function pointers
 *
 * GCC on ARM64 may merge or eliminate out-of-line copies of static __inline
 * functions. When the address of such a function is taken and stored as a
 * JIT uop operand (via uop_CALL_FUNC / uop_CALL_FUNC_RESULT), the emitted
 * code ends up calling a merged symbol that may point to the wrong function
 * or may be eliminated entirely.
 *
 * Every static __inline function whose address is passed to uop_CALL_FUNC*
 * MUST have an noinline wrapper here.  The wrapper is what the JIT calls at
 * runtime — the real inline body is still available for normal C call sites.
 *
 * Include this header in each codegen_ops_*.c that needs one of the wrappers.
 * The including .c file MUST already include x86_flags.h before this header.
 */
#ifndef CODEGEN_OPS_JIT_WRAPPERS_H
#define CODEGEN_OPS_JIT_WRAPPERS_H

#define JIT_WRAPPER __attribute__((noinline, used))

/* --- flags_rebuild family (void → void) --- */

static JIT_WRAPPER void
jit_flags_rebuild(void)
{
    flags_rebuild();
}

static JIT_WRAPPER void
jit_flags_rebuild_c(void)
{
    flags_rebuild_c();
}

static JIT_WRAPPER void
jit_flags_rebuild_zpna(void)
{
    flags_rebuild_zpna();
}

/*flags_rebuild() and flags_rebuild_zpna() write only flags and flags_op, which they leave at
  FLAGS_UNKNOWN. On arm64 and x86-64 they are called with the cached registers written back but
  left loaded, and those two are reloaded after the call.*/
static inline void
call_flags_rebuild(ir_data_t *ir, void (*rebuild)(void))
{
    if ((rebuild == jit_flags_rebuild) && codegen_flags_rebuild_known(ir, 0))
        return;
#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
    uop_CALL_FUNC_ORDERED(ir, rebuild);
    uop_MOVZX_REG_PTR_16(ir, IREG_flags, &cpu_state.flags);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_UNKNOWN);
#else
    uop_CALL_FUNC(ir, rebuild);
#endif
}

/* --- individual flag test functions (void → int) --- */

static JIT_WRAPPER int
jit_CF_SET(void)
{
    return CF_SET();
}

static JIT_WRAPPER int
jit_NF_SET(void)
{
    return NF_SET();
}

static JIT_WRAPPER int
jit_PF_SET(void)
{
    return PF_SET();
}

static JIT_WRAPPER int
jit_VF_SET(void)
{
    return VF_SET();
}

static JIT_WRAPPER int
jit_ZF_SET(void)
{
    return ZF_SET();
}

#endif /* CODEGEN_OPS_JIT_WRAPPERS_H */

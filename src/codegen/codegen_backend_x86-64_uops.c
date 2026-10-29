#if defined __amd64__ || defined _M_X64

#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat_unused.h>

#    include "x86.h"
#    include "x86seg_common.h"
#    include "x86seg.h"
#    include "x87_sf.h"
#    include "x87.h"
#    include "386_common.h"
#    include "codegen.h"
#    include "codegen_backend.h"
#    include "codegen_backend_x86-64_defs.h"
#    include "codegen_backend_x86-64_ops.h"
#    include "codegen_backend_x86-64_ops_sse.h"
#    include "codegen_ir_defs.h"

#    define STACK_ARG0        (0)
#    define STACK_ARG1        (4)
#    define STACK_ARG2        (8)
#    define STACK_ARG3        (12)

/*AH-BH exist only for the first four registers; the allocator keeps
  registers accessed by byte out of R8-R15 (HOST_REG_FLAG_NO_BYTE).*/
static inline int
host_reg_get(int reg)
{
    if (IREG_GET_SIZE(reg) == IREG_SIZE_BH) {
        if (IREG_GET_REG(reg) & ~3)
            fatal("x86-64 codegen: high byte of host register %i\n", IREG_GET_REG(reg));
        return IREG_GET_REG(reg) | 4;
    }
    return IREG_GET_REG(reg) & 15;
}
#    define HOST_REG_GET(reg) host_reg_get(reg)

#    define REG_IS_B(size)    (size == IREG_SIZE_B || size == IREG_SIZE_BH)
#    define REG_IS_BH(size)   (size == IREG_SIZE_BH)
#    define REG_IS_D(size)    (size == IREG_SIZE_D)

static inline int
REG_IS_L(int size)
{
    return size == IREG_SIZE_L;
}

static inline int
REG_IS_W(int size)
{
    return size == IREG_SIZE_W;
}

static inline int
REG_IS_Q(int size)
{
    return size == IREG_SIZE_Q;
}

static int
codegen_ADD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    /*dest == src_b: moving src_a into dest first would clobber src_b; the
      operation commutes, so take the sources the other way round*/
    if ((dest_reg == src_reg_b) && (dest_reg != src_reg_a)) {
        int t     = src_reg_a;
        src_reg_a = src_reg_b;
        src_reg_b = t;
    }

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_LEA_REG_REG(block, dest_reg, src_reg_a, src_reg_b);
        else
            host_x86_ADD32_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
        host_x86_ADD16_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
        host_x86_ADD8_REG_REG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_ADD_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (dest_reg != src_reg)
            host_x86_LEA_REG_IMM(block, dest_reg, src_reg, uop->imm_data);
        else
            host_x86_ADD32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_ADD16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_ADD8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ADD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_ADD_LSHIFT(codeblock_t *block, uop_t *uop)
{
    if (!uop->imm_data) {
        if (uop->dest_reg_a_real == uop->src_reg_a_real)
            host_x86_ADD32_REG_REG(block, uop->dest_reg_a_real, uop->src_reg_b_real);
        else
            host_x86_LEA_REG_REG(block, uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
    } else if (uop->imm_data < 4)
        host_x86_LEA_REG_REG_SHIFT(block, uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real, uop->imm_data);
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_ADD_LSHIFT - shift out of range %i\n", uop->imm_data);
#    endif
    return 0;
}

static int
codegen_AND(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    /*dest == src_b: moving src_a into dest first would clobber src_b; the
      operation commutes, so take the sources the other way round*/
    if ((dest_reg == src_reg_b) && (dest_reg != src_reg_a)) {
        int t     = src_reg_a;
        src_reg_a = src_reg_b;
        src_reg_b = t;
    }

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PAND_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
        host_x86_AND32_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
        host_x86_AND16_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
        host_x86_AND8_REG_REG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("AND %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_AND_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_AND32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_AND16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_AND8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("AND_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_ANDN(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
#if 0
    int src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
#endif
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PANDN_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ANDN %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

/*An exit that adds add to cycles first (uop->imm_data2), with no entry for it
  (codegen_exit_dest): the add in its own arm, which the path that goes on
  branches over. Every exit is an order barrier at least, so cycles is in
  memory. EXIT_ARM_SIZE keeps the branch over the arm and the arm in one
  code chunk: room for the branch, the longest add and the longest jump.*/
#    define EXIT_ARM_SIZE 32

static void
codegen_exit_add(codeblock_t *block, int add)
{
    host_x86_ADD32_ABS_IMM(block, &cpu_state._cycles, add);
}

static void
codegen_exit_arm(codeblock_t *block, int add, void *route)
{
    codegen_exit_add(block, add);
    host_x86_JMP(block, route);
}

/*A conditional branch to route from branch (host_x86_JNZ_long and the like),
  charging what uop->imm_data2 adds on the way. Without an entry the branch
  with the opposite condition, inverse, jumps over the arm. Returns where the
  exit starts, for another branch to the same exit.*/
static void *
codegen_exit_Jcc(codeblock_t *block, uop_t *uop, void *route, uint32_t *(*branch)(codeblock_t *block), uint32_t *(*inverse)(codeblock_t *block))
{
    void     *dest = codegen_exit_dest(route, uop->imm_data2);
    uint32_t *jump_p;

    if (dest) {
        jump_p  = branch(block);
        *jump_p = (uintptr_t) dest - ((uintptr_t) jump_p + 4);
        return dest;
    }
    codegen_alloc(block, EXIT_ARM_SIZE);
    jump_p = inverse(block);
    dest   = &block_write_data[block_pos];
    codegen_exit_arm(block, uop->imm_data2, route);
    *jump_p = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) jump_p + 4);

    return dest;
}

static int
codegen_CALL_FUNC(codeblock_t *block, uop_t *uop)
{
    host_x86_CALL(block, uop->p);

    return 0;
}

/*An order barrier leaves the cached registers loaded, and EAX and EDX are
  not preserved by the callee, so they are saved around the call. Two pushes
  keep RSP 16-byte aligned; Windows also needs the 32-byte home area below
  them, so the callee does not overwrite the saved values.*/
static int
codegen_CALL_FUNC_ORDERED(codeblock_t *block, uop_t *uop)
{
    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_CALL(block, uop->p);
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);

    return 0;
}

/*As codegen_CALL_FUNC_ORDERED. The result goes through ESI, which is not
  allocated, so that EAX and EDX can be put back before dest_reg is set; if
  dest_reg is one of them, its old contents were written back before the
  call and are not needed.*/
static int
codegen_CALL_FUNC_RESULT_ORDERED(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);

    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_CALL(block, uop->p);
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_MOV32_REG_REG(block, REG_ESI, REG_EAX);
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);
    host_x86_MOV32_REG_REG(block, dest_reg, REG_ESI);

    return 0;
}

static int
codegen_CALL_FUNC_RESULT(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);

#    ifdef RECOMPILER_DEBUG
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    if (!REG_IS_L(dest_size))
        fatal("CALL_FUNC_RESULT %02x\n", uop->dest_reg_a_real);
#    endif
    host_x86_CALL(block, uop->p);
    host_x86_MOV32_REG_REG(block, dest_reg, REG_EAX);

    return 0;
}

static int
codegen_CALL_INSTRUCTION_FUNC(codeblock_t *block, uop_t *uop)
{
#    if _WIN64
    host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data);
#    else
    host_x86_MOV32_REG_IMM(block, REG_EDI, uop->imm_data);
#    endif
    host_x86_CALL(block, uop->p);
    host_x86_TEST32_REG(block, REG_EAX, REG_EAX);
    codegen_exit_Jcc(block, uop, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);

    return 0;
}

/*The REP routine keeps RBX, RBP and R12-R15 but not the cached EAX and EDX,
  which are saved around it as for an ordered call. Its result goes through
  ESI, which is not allocated, so the saved registers can be put back first.*/
static int
codegen_CALL_REP_STRING(codeblock_t *block, uop_t *uop)
{
    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x20);
    host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data);
#    else
    host_x86_MOV32_REG_IMM(block, REG_EDI, uop->imm_data);
#    endif
    host_x86_CALL(block, uop->p);
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_MOV32_REG_REG(block, REG_ESI, REG_EAX);
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);
    host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
    codegen_exit_Jcc(block, uop, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);

    return 0;
}

/*Leave the block for an SMI the instruction raised, with pc set after it,
  taken from the immediate for SMI_EXIT_IMM. The branch over the exit is
  the only cost while none is pending.*/
static int
codegen_SMI_EXIT_IMM(codeblock_t *block, uop_t *uop)
{
    void     *dest = codegen_exit_dest(codegen_exit_rout, uop->imm_data2);
    uint32_t *branch_offset;

    host_x86_MOVZX_REG_ABS_32_8(block, REG_ECX, &cpu_state._smi_line);
    host_x86_TEST32_REG(block, REG_ECX, REG_ECX);
    branch_offset = host_x86_JZ_long(block);
    if (uop->type & UOP_TYPE_PARAMS_IMM)
        host_x86_MOV32_ABS_IMM(block, &cpu_state.pc, uop->imm_data);
    /*Charging what the block's end would (uop->imm_data2)*/
    if (dest)
        host_x86_JMP(block, dest);
    else
        codegen_exit_arm(block, uop->imm_data2, codegen_exit_rout);
    *branch_offset = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 4;

    return 0;
}

static int
codegen_CMP_IMM_JZ(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_CMP32_REG_IMM(block, src_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_IMM_JZ %02x\n", uop->src_reg_a_real);
#    endif
    codegen_exit_Jcc(block, uop, uop->p, host_x86_JZ_long, host_x86_JNZ_long);

    return 0;
}

static int
codegen_CMP_IMM_JNZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_CMP32_REG_IMM(block, src_reg, uop->imm_data);
    } else if (REG_IS_W(src_size)) {
        host_x86_CMP16_REG_IMM(block, src_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_IMM_JNZ_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNZ_long(block);

    return 0;
}
static int
codegen_CMP_IMM_JZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_CMP32_REG_IMM(block, src_reg, uop->imm_data);
    } else if (REG_IS_W(src_size)) {
        host_x86_CMP16_REG_IMM(block, src_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_IMM_JZ_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JZ_long(block);

    return 0;
}

static int
codegen_CMP_JB(codeblock_t *block, uop_t *uop)
{
    int       src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int       src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int       src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int       src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JB %02x\n", uop->src_reg_a_real);
#    endif
    codegen_exit_Jcc(block, uop, uop->p, host_x86_JB_long, host_x86_JNB_long);

    return 0;
}
static int
codegen_CMP_JNBE(codeblock_t *block, uop_t *uop)
{
    int       src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int       src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int       src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int       src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNBE %02x\n", uop->src_reg_a_real);
#    endif
    codegen_exit_Jcc(block, uop, uop->p, host_x86_JNBE_long, host_x86_JBE_long);

    return 0;
}

static int
codegen_CMP_JNWITHIN(codeblock_t *block, uop_t *uop)
{
    int       src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int       lo_reg   = HOST_REG_GET(uop->src_reg_b_real);
    int       hi_reg   = HOST_REG_GET(uop->src_reg_c_real);
    int       src_size = IREG_GET_SIZE(uop->src_reg_a_real);
    int       lo_size  = IREG_GET_SIZE(uop->src_reg_b_real);
    int       hi_size  = IREG_GET_SIZE(uop->src_reg_c_real);
    uint32_t *jump_p;
    void     *exit_p;

    if (REG_IS_L(src_size) && REG_IS_L(lo_size) && REG_IS_L(hi_size)) {
        host_x86_CMP32_REG_REG(block, src_reg, lo_reg);
        jump_p = host_x86_JB_long(block);
        if (uop->imm_data) {
            host_x86_LEA_REG_IMM(block, REG_ECX, src_reg, uop->imm_data);
            host_x86_CMP32_REG_REG(block, REG_ECX, hi_reg);
        } else
            host_x86_CMP32_REG_REG(block, src_reg, hi_reg);
        /*Both branches leave through one exit, the second one's arm when it
          has one*/
        exit_p  = codegen_exit_Jcc(block, uop, uop->p, host_x86_JNBE_long, host_x86_JBE_long);
        *jump_p = (uintptr_t) exit_p - ((uintptr_t) jump_p + 4);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNWITHIN %02x %02x %02x\n", uop->src_reg_a_real, uop->src_reg_b_real, uop->src_reg_c_real);
#    endif

    return 0;
}

/*src_reg + imm carries out of 32 bits exactly when src_reg > ~imm, unsigned,
  which holds for imm 0 as well*/
static int
codegen_CMN_IMM_JC(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_CMP32_REG_IMM(block, src_reg, ~uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMN_IMM_JC %02x\n", uop->src_reg_a_real);
#    endif
    codegen_exit_Jcc(block, uop, uop->p, host_x86_JNBE_long, host_x86_JBE_long);

    return 0;
}

/*The low limit test, then the carry test of CMN_IMM_JC*/
static int
codegen_CMP_JNWITHIN_TOP(codeblock_t *block, uop_t *uop)
{
    int       src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int       lo_reg   = HOST_REG_GET(uop->src_reg_b_real);
    int       src_size = IREG_GET_SIZE(uop->src_reg_a_real);
    int       lo_size  = IREG_GET_SIZE(uop->src_reg_b_real);
    uint32_t *jump_p;
    void     *exit_p;

    if (REG_IS_L(src_size) && REG_IS_L(lo_size)) {
        host_x86_CMP32_REG_REG(block, src_reg, lo_reg);
        jump_p = host_x86_JB_long(block);
        host_x86_CMP32_REG_IMM(block, src_reg, ~uop->imm_data);
        /*Both branches leave through one exit, the second one's arm when it
          has one*/
        exit_p  = codegen_exit_Jcc(block, uop, uop->p, host_x86_JNBE_long, host_x86_JBE_long);
        *jump_p = (uintptr_t) exit_p - ((uintptr_t) jump_p + 4);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNWITHIN_TOP %02x %02x\n", uop->src_reg_a_real, uop->src_reg_b_real);
#    endif

    return 0;
}

/*COMISD sets CF, ZF and PF for an unordered pair, so JBE and JNBE take a NaN
  as outside*/
static int
codegen_FCMP_JWITHIN_DEST(codeblock_t *block, uop_t *uop)
{
    int       src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int       lo_reg   = HOST_REG_GET(uop->src_reg_b_real);
    int       hi_reg   = HOST_REG_GET(uop->src_reg_c_real);
    int       src_size = IREG_GET_SIZE(uop->src_reg_a_real);
    int       lo_size  = IREG_GET_SIZE(uop->src_reg_b_real);
    int       hi_size  = IREG_GET_SIZE(uop->src_reg_c_real);
    uint32_t *outside;

    if (REG_IS_D(src_size) && REG_IS_D(lo_size) && REG_IS_D(hi_size)) {
        host_x86_COMISD_XREG_XREG(block, hi_reg, src_reg);
        outside = host_x86_JBE_long(block);
        host_x86_COMISD_XREG_XREG(block, src_reg, lo_reg);
        uop->p   = host_x86_JNBE_long(block);
        *outside = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) outside) - 4;
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("FCMP_JWITHIN_DEST %02x %02x %02x\n", uop->src_reg_a_real, uop->src_reg_b_real, uop->src_reg_c_real);
#    endif

    return 0;
}

/*The exponent window of x87_divide_fast, 553 <= e <= 1493 for both operands,
  a memory operand of the single kind held to the single's normal range
  (e >= 897: a denormal single raises DE, which the software divide does);
  a quotient with a zero fraction, a power of two, goes there as well*/
static int
codegen_FDIV_JSLOW_DEST(codeblock_t *block, uop_t *uop)
{
    int       num_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int       den_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int       q_reg    = HOST_REG_GET(uop->src_reg_c_real);
    int       num_size = IREG_GET_SIZE(uop->src_reg_a_real);
    int       den_size = IREG_GET_SIZE(uop->src_reg_b_real);
    int       q_size   = IREG_GET_SIZE(uop->src_reg_c_real);
    int       kind     = (uop->imm_data >> 11) & 3;
    int       lo_num   = ((uop->imm_data & 0x200) && (kind == 1)) ? 897 : 553;
    int       lo_den   = ((uop->imm_data & 0x400) && (kind == 1)) ? 897 : 553;
    uint32_t *fail_num;
    uint32_t *fail_den;
    uint32_t *fail_q;
    uint32_t *ok;

    if (!(REG_IS_D(num_size) && REG_IS_D(den_size) && REG_IS_D(q_size)))
        fatal("FDIV_JSLOW_DEST %02x %02x %02x\n", uop->src_reg_a_real, uop->src_reg_b_real, uop->src_reg_c_real);

    host_x86_MOVQ_REG_XREG(block, REG_RCX, num_reg);
    host_x86_SHR64_IMM(block, REG_RCX, 52);
    host_x86_AND32_REG_IMM(block, REG_ECX, 0x7ff);
    host_x86_SUB32_REG_IMM(block, REG_ECX, lo_num);
    host_x86_CMP32_REG_IMM(block, REG_ECX, 1493 - lo_num);
    fail_num = host_x86_JNBE_long(block);
    host_x86_MOVQ_REG_XREG(block, REG_RCX, den_reg);
    host_x86_SHR64_IMM(block, REG_RCX, 52);
    host_x86_AND32_REG_IMM(block, REG_ECX, 0x7ff);
    host_x86_SUB32_REG_IMM(block, REG_ECX, lo_den);
    host_x86_CMP32_REG_IMM(block, REG_ECX, 1493 - lo_den);
    fail_den = host_x86_JNBE_long(block);
    host_x86_MOVQ_REG_XREG(block, REG_RCX, q_reg);
    host_x86_MOV64_REG_IMM(block, REG_RSI, 0x000fffffffffffffull);
    host_x86_TEST64_REG(block, REG_RSI, REG_RCX);
    fail_q = host_x86_JZ_long(block);
    ok     = host_x86_JMP_long(block);

    *fail_num = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_num + 4);
    *fail_den = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_den + 4);
    *fail_q   = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_q + 4);
    uop->p    = host_x86_JMP_long(block);
    *ok       = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) ok + 4);

    return 0;
}

/*The status bits of a divide finished from the host quotient
  (x87_divide_fast): r = num - q * den is exact inside the window, PE is
  r != 0 and C1 a rounding up in magnitude, r and num of opposite signs. r is
  Dekker's product, the operations of x87_divide_fast's path without FMA in
  its order, worked in XMM_TEMP and four allocated XMM registers the uop does
  not read: their low halves, all an allocated value occupies, wait in R8-R11
  and are put back before the first branch. At 24 bits the quotient rounds
  once more, to single: a single-precision midpoint with r != 0, or a single
  that is not normal, is the software divide's; when the single differs from
  the quotient the flags are that rounding's, C1 for the larger magnitude. PE
  raised while unmasked is the software divide's too, which raises it as the
  interpreter does.*/
static int
codegen_FDIV_FLAGS_JSLOW_DEST(codeblock_t *block, uop_t *uop)
{
    int       dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int       q_reg     = HOST_REG_GET(uop->src_reg_a_real);
    int       num_reg   = HOST_REG_GET(uop->src_reg_b_real);
    int       den_reg   = HOST_REG_GET(uop->src_reg_c_real);
    int       dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int       q_size    = IREG_GET_SIZE(uop->src_reg_a_real);
    int       num_size  = IREG_GET_SIZE(uop->src_reg_b_real);
    int       den_size  = IREG_GET_SIZE(uop->src_reg_c_real);
    int       pc24      = uop->imm_data & 0x2000;
    int       w[5];
    int       nr_w      = 1;
    uint32_t *fail_mid  = NULL;
    uint32_t *fail_sub  = NULL;
    uint32_t *fail_pe;
    uint32_t *same;
    uint32_t *below;
    uint32_t *ok;

    if (!(REG_IS_L(dest_size) && REG_IS_D(q_size) && REG_IS_D(num_size) && REG_IS_D(den_size)))
        fatal("FDIV_FLAGS_JSLOW_DEST %02x %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real, uop->src_reg_c_real);

    w[0] = REG_XMM_TEMP;
    for (int c = REG_XMM1; (c <= REG_XMM7) && (nr_w < 5); c++) {
        if ((c != q_reg) && (c != num_reg) && (c != den_reg))
            w[nr_w++] = c;
    }
    for (int c = 1; c < 5; c++)
        host_x86_MOVQ_REG_XREG(block, REG_R8 + c - 1, w[c]);

    /* split = 2^27 + 1 */
    host_x86_MOV64_REG_IMM(block, REG_RCX, 0x41a0000002000000ull);
    host_x86_MOVQ_XREG_REG(block, w[0], REG_RCX);
    host_x86_MOVQ_XREG_XREG(block, w[1], q_reg);
    host_x86_MULSD_XREG_XREG(block, w[1], w[0]);
    host_x86_MOVQ_XREG_XREG(block, w[2], w[1]);
    host_x86_SUBSD_XREG_XREG(block, w[2], q_reg);
    host_x86_SUBSD_XREG_XREG(block, w[1], w[2]); /* qh */
    host_x86_MOVQ_XREG_XREG(block, w[2], q_reg);
    host_x86_SUBSD_XREG_XREG(block, w[2], w[1]); /* ql */
    host_x86_MOVQ_XREG_XREG(block, w[3], den_reg);
    host_x86_MULSD_XREG_XREG(block, w[3], w[0]);
    host_x86_MOVQ_XREG_XREG(block, w[4], w[3]);
    host_x86_SUBSD_XREG_XREG(block, w[4], den_reg);
    host_x86_SUBSD_XREG_XREG(block, w[3], w[4]); /* bh */
    host_x86_MOVQ_XREG_XREG(block, w[0], den_reg);
    host_x86_SUBSD_XREG_XREG(block, w[0], w[3]); /* bl */
    host_x86_MOVQ_XREG_XREG(block, w[4], q_reg);
    host_x86_MULSD_XREG_XREG(block, w[4], den_reg); /* p */
    host_x86_MOVQ_REG_XREG(block, REG_RSI, w[0]);
    host_x86_MOVQ_XREG_XREG(block, w[0], w[1]);
    host_x86_MULSD_XREG_XREG(block, w[0], w[3]);
    host_x86_SUBSD_XREG_XREG(block, w[0], w[4]); /* qh * bh - p */
    host_x86_MOVQ_REG_XREG(block, REG_RDI, w[4]);
    host_x86_MOVQ_XREG_REG(block, w[4], REG_RSI);
    host_x86_MULSD_XREG_XREG(block, w[1], w[4]);
    host_x86_ADDSD_XREG_XREG(block, w[0], w[1]); /* + qh * bl */
    host_x86_MULSD_XREG_XREG(block, w[3], w[2]);
    host_x86_ADDSD_XREG_XREG(block, w[0], w[3]); /* + ql * bh */
    host_x86_MULSD_XREG_XREG(block, w[2], w[4]);
    host_x86_ADDSD_XREG_XREG(block, w[0], w[2]); /* + ql * bl = e */
    host_x86_MOVQ_XREG_REG(block, w[3], REG_RDI);
    host_x86_MOVQ_XREG_XREG(block, w[1], num_reg);
    host_x86_SUBSD_XREG_XREG(block, w[1], w[3]);
    host_x86_SUBSD_XREG_XREG(block, w[1], w[0]); /* r = (num - p) - e */
    host_x86_MOVQ_REG_XREG(block, REG_RCX, w[1]);
    for (int c = 1; c < 5; c++)
        host_x86_MOVQ_XREG_REG(block, w[c], REG_R8 + c - 1);

    host_x86_MOVQ_REG_XREG(block, REG_RSI, num_reg);
    host_x86_SHR64_IMM(block, REG_RSI, 63);
    host_x86_MOV64_REG_REG(block, REG_RDI, REG_RCX);
    host_x86_SHR64_IMM(block, REG_RDI, 63);
    host_x86_XOR32_REG_REG(block, REG_EDI, REG_ESI);
    host_x86_SHL32_IMM(block, REG_EDI, 9);
    host_x86_OR32_REG_IMM(block, REG_EDI, FPU_SW_Precision);
    if (pc24) {
        /* zero below the single's midpoint bit: fraction bits 28..0 = 1 0...0 */
        host_x86_MOVD_REG_XREG(block, REG_R8, q_reg);
        host_x86_AND32_REG_IMM(block, REG_R8, 0x1fffffff);
        host_x86_XOR32_REG_IMM(block, REG_R8, 0x10000000);
        host_x86_MOV32_REG_IMM(block, REG_R9, 1);
    }
    host_x86_MOV64_REG_IMM(block, REG_RSI, 0x7fffffffffffffffull);
    host_x86_MOV32_REG_IMM(block, dest_reg, 0);
    host_x86_TEST64_REG(block, REG_RSI, REG_RCX);
    host_x86_CMOVNZ32_REG_REG(block, dest_reg, REG_EDI);
    if (pc24) {
        /* with r == 0 the midpoint is no concern: R9 stays 1 */
        host_x86_CMOVNZ32_REG_REG(block, REG_R9, REG_R8);
        host_x86_TEST32_REG(block, REG_R9, REG_R9);
        fail_mid = host_x86_JZ_long(block);
        host_x86_CVTSD2SS_XREG_XREG(block, REG_XMM_TEMP, q_reg);
        host_x86_MOVD_REG_XREG(block, REG_ECX, REG_XMM_TEMP);
        host_x86_SHR32_IMM(block, REG_ECX, 23);
        host_x86_AND32_REG_IMM(block, REG_ECX, 0xff);
        host_x86_SUB32_REG_IMM(block, REG_ECX, 1);
        host_x86_CMP32_REG_IMM(block, REG_ECX, 253);
        fail_sub = host_x86_JNBE_long(block);
        host_x86_CVTSS2SD_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
        host_x86_MOVQ_REG_XREG(block, REG_RSI, q_reg);
        host_x86_SHR64_IMM(block, REG_RSI, 63);
        host_x86_COMISD_XREG_XREG(block, REG_XMM_TEMP, q_reg);
        same = host_x86_JZ_long(block);
        host_x86_MOV32_REG_IMM(block, dest_reg, FPU_SW_Precision);
        below = host_x86_JB_long(block);
        host_x86_XOR32_REG_IMM(block, REG_ESI, 1);
        *below = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) below + 4);
        /* C1: the single above a positive quotient or below a negative one */
        host_x86_SHL32_IMM(block, REG_ESI, 9);
        host_x86_OR32_REG_REG(block, dest_reg, REG_ESI);
        *same = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) same + 4);
    }
    host_x86_MOVZX_REG_ABS_32_16(block, REG_ECX, &cpu_state.npxc);
    host_x86_XOR32_REG_IMM(block, REG_ECX, FPU_SW_Precision);
    host_x86_AND32_REG_REG(block, REG_ECX, dest_reg);
    host_x86_AND32_REG_IMM(block, REG_ECX, FPU_SW_Precision);
    fail_pe = host_x86_JNZ_long(block);
    ok      = host_x86_JMP_long(block);

    if (fail_mid)
        *fail_mid = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_mid + 4);
    if (fail_sub)
        *fail_sub = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_sub + 4);
    *fail_pe = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_pe + 4);
    uop->p   = host_x86_JMP_long(block);
    *ok      = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) ok + 4);

    return 0;
}

static int
codegen_CMP_JNB_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNB_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNB_long(block);

    return 0;
}
static int
codegen_CMP_JNBE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNBE_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNBE_long(block);

    return 0;
}
static int
codegen_CMP_JNL_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNL_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNL_long(block);

    return 0;
}
static int
codegen_CMP_JNLE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNLE_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNLE_long(block);

    return 0;
}
static int
codegen_CMP_JNO_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNO_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNO_long(block);

    return 0;
}
static int
codegen_CMP_JNZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNZ_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNZ_long(block);

    return 0;
}
static int
codegen_CMP_JB_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JB_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JB_long(block);

    return 0;
}
static int
codegen_CMP_JBE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JBE_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JBE_long(block);

    return 0;
}
static int
codegen_CMP_JL_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JL_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JL_long(block);

    return 0;
}
static int
codegen_CMP_JLE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JLE_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JLE_long(block);

    return 0;
}
static int
codegen_CMP_JO_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JO_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JO_long(block);

    return 0;
}
static int
codegen_CMP_JZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JZ_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JZ_long(block);

    return 0;
}

static int
codegen_FABS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_MOV64_REG_IMM(block, REG_RCX, 0x7fffffffffffffff);
        host_x86_MOVQ_XREG_REG(block, REG_XMM_TEMP, REG_RCX);
        host_x86_PAND_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FABS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_FCHS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_MOV64_REG_IMM(block, REG_RCX, 0x8000000000000000);
        host_x86_MOVQ_XREG_REG(block, REG_XMM_TEMP, REG_RCX);
        host_x86_PXOR_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FCHS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_FP_CONTROL_SET(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_W(src_size)) {
        /*new_fp_control = the host MXCSR with the x87 RC in its RC field*/
        host_x86_MOVZX_REG_32_16(block, REG_ECX, src_reg);
        host_x86_AND32_REG_IMM(block, REG_ECX, 0x0c00);
        host_x86_SHL32_IMM(block, REG_ECX, 3);
        host_x86_MOV32_REG_ABS(block, REG_ESI, &cpu_state.old_fp_control);
        host_x86_AND32_REG_IMM(block, REG_ESI, ~0x6000);
        host_x86_OR32_REG_REG(block, REG_ECX, REG_ESI);
        host_x86_MOV32_ABS_REG(block, &cpu_state.new_fp_control, REG_ECX);
        host_x86_MOV32_ABS_IMM(block, &fpu_ferr, 0);
        host_x86_MOV32_ABS_IMM(block, &fpu_ignne, 0);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FP_CONTROL_SET %02x\n", uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_FSQRT(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        host_x86_SQRTSD_XREG_XREG(block, dest_reg, src_reg_a);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FSQRT %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_FROUND_S(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        host_x86_CVTSD2SS_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_CVTSS2SD_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FROUND_S %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_FTST(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_W(dest_size) && REG_IS_D(src_size_a)) {
        host_x86_PXOR_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
        if (dest_reg != REG_EAX)
            host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
        host_x86_XOR32_REG_REG(block, REG_EAX, REG_EAX);
        host_x86_COMISD_XREG_XREG(block, src_reg_a, REG_XMM_TEMP);
        host_x86_LAHF(block);
        host_x86_AND16_REG_IMM(block, REG_EAX, FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3);
        if (dest_reg != REG_EAX) {
            host_x86_MOV16_REG_REG(block, dest_reg, REG_EAX);
            host_x86_MOV32_REG_REG(block, REG_EAX, REG_ECX);
        }
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FTST %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_FADD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a) {
        host_x86_ADDSD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_FCOM(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_W(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        if (dest_reg != REG_EAX)
            host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
        host_x86_XOR32_REG_REG(block, REG_EAX, REG_EAX);
        host_x86_COMISD_XREG_XREG(block, src_reg_a, src_reg_b);
        host_x86_LAHF(block);
        host_x86_AND16_REG_IMM(block, REG_EAX, FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3);
        if (dest_reg != REG_EAX) {
            host_x86_MOV16_REG_REG(block, dest_reg, REG_EAX);
            host_x86_MOV32_REG_REG(block, REG_EAX, REG_ECX);
        }
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FCOM %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_FDIV(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a) {
        host_x86_DIVSD_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_DIVSD_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FDIV %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_FMUL(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a) {
        host_x86_MULSD_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_MULSD_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FMUL %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_FSUB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a) {
        host_x86_SUBSD_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_SUBSD_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FSUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_FP_ENTER(codeblock_t *block, uop_t *uop)
{
    uint32_t *branch_offset;

    host_x86_MOV32_REG_ABS(block, REG_ECX, &cr0);
    host_x86_TEST32_REG_IMM(block, REG_ECX, 0xc);
    branch_offset = host_x86_JZ_long(block);
    host_x86_MOV32_ABS_IMM(block, &cpu_state.oldpc, uop->imm_data);
    /*The cycles accumulated so far, charged on this exit only (the uop is an
      order barrier, so cycles is in memory)*/
    if (uop->imm_data2) {
        host_x86_MOV32_REG_ABS(block, REG_ECX, &cpu_state._cycles);
        host_x86_ADD32_REG_IMM(block, REG_ECX, uop->imm_data2);
        host_x86_MOV32_ABS_REG(block, &cpu_state._cycles, REG_ECX);
    }
#    if _WIN64
    host_x86_MOV32_REG_IMM(block, REG_ECX, 7);
#    else
    host_x86_MOV32_REG_IMM(block, REG_EDI, 7);
#    endif
    host_x86_CALL(block, x86_int);
    host_x86_JMP(block, codegen_exit_rout);
    *branch_offset = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 4;

    return 0;
}
static int
codegen_MMX_ENTER(codeblock_t *block, uop_t *uop)
{
    uint32_t *branch_offset;

    host_x86_MOV32_REG_ABS(block, REG_ECX, &cr0);
    host_x86_TEST32_REG_IMM(block, REG_ECX, 0xc);
    branch_offset = host_x86_JZ_long(block);
    host_x86_MOV32_ABS_IMM(block, &cpu_state.oldpc, uop->imm_data);
    /*As codegen_FP_ENTER (the uop is a full barrier)*/
    if (uop->imm_data2) {
        host_x86_MOV32_REG_ABS(block, REG_ECX, &cpu_state._cycles);
        host_x86_ADD32_REG_IMM(block, REG_ECX, uop->imm_data2);
        host_x86_MOV32_ABS_REG(block, &cpu_state._cycles, REG_ECX);
    }
    host_x86_CALL(block, codegen_mmx_unavailable);
    host_x86_JMP(block, codegen_exit_rout);
    *branch_offset = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 4;
    host_x86_MOV32_ABS_IMM(block, &cpu_state.tag[0], 0x01010101);
    host_x86_MOV32_ABS_IMM(block, &cpu_state.tag[4], 0x01010101);
    host_x86_MOV32_ABS_IMM(block, &cpu_state.TOP, 0);
    host_x86_MOV8_ABS_IMM(block, &cpu_state.ismmx, 1);

    return 0;
}

static int
codegen_JMP_DEST(codeblock_t *block, uop_t *uop)
{
    uop->p = host_x86_JMP_long(block);

    return 0;
}

static int
codegen_JMP(codeblock_t *block, uop_t *uop)
{
    void *dest;

#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    /*An unconditional jump out of the block is an ordinary hand-off (UOP_JMP
      writes registers back), so it can link like the block's end. Fault
      paths reach codegen_exit_rout by conditional branches or direct jumps
      instead.*/
    if (codegen_block_link && (uop->p == codegen_exit_rout)) {
        if (uop->imm_data2)
            codegen_exit_add(block, uop->imm_data2);
        codegen_link_exit(block);
        return 0;
    }
#    endif
    dest = codegen_exit_dest(uop->p, uop->imm_data2);
    if (!dest) {
        codegen_exit_add(block, uop->imm_data2);
        dest = uop->p;
    }
    host_x86_JMP(block, dest);

    return 0;
}

static int
codegen_LOAD_FUNC_ARG0(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_W(src_size)) {
#    if _WIN64
        host_x86_MOVZX_REG_32_16(block, REG_ECX, src_reg);
#    else
        host_x86_MOVZX_REG_32_16(block, REG_EDI, src_reg);
#    endif
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_LOAD_FUNC_ARG0 %02x\n", uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG1(UNUSED(codeblock_t *block), UNUSED(uop_t *uop))
{
#    ifdef RECOMPILER_DEBUG
    fatal("codegen_LOAD_FUNC_ARG1 %02x\n", uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG2(UNUSED(codeblock_t *block), UNUSED(uop_t *uop))
{
#    ifdef RECOMPILER_DEBUG
    fatal("codegen_LOAD_FUNC_ARG2 %02x\n", uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG3(UNUSED(codeblock_t *block), UNUSED(uop_t *uop))
{
#    ifdef RECOMPILER_DEBUG
    fatal("codegen_LOAD_FUNC_ARG3 %02x\n", uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_LOAD_FUNC_ARG0_IMM(codeblock_t *block, uop_t *uop)
{
#    if _WIN64
    host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data);
#    else
    host_x86_MOV32_REG_IMM(block, REG_EDI, uop->imm_data);
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG1_IMM(codeblock_t *block, uop_t *uop)
{
#    if _WIN64
    host_x86_MOV32_REG_IMM(block, REG_EDX, uop->imm_data);
#    else
    host_x86_MOV32_REG_IMM(block, REG_ESI, uop->imm_data);
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG2_IMM(UNUSED(codeblock_t *block), UNUSED(uop_t *uop))
{
#    ifdef RECOMPILER_DEBUG
    fatal("codegen_LOAD_FUNC_ARG2_IMM\n");
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG3_IMM(UNUSED(codeblock_t *block), UNUSED(uop_t *uop))
{
#    ifdef RECOMPILER_DEBUG
    fatal("codegen_LOAD_FUNC_ARG3_IMM\n");
#    endif
    return 0;
}

static int
codegen_LOAD_SEG(codeblock_t *block, uop_t *uop)
{
    int src_reg = HOST_REG_GET(uop->src_reg_a_real);

#    ifdef RECOMPILER_DEBUG
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);
    if (!REG_IS_W(src_size))
        fatal("LOAD_SEG %02x %p\n", uop->src_reg_a_real, uop->p);
#    endif
#    if _WIN64
    host_x86_MOV16_REG_REG(block, REG_CX, src_reg);
    host_x86_MOV64_REG_IMM(block, REG_EDX, (uint64_t) uop->p);
#    else
    host_x86_MOV16_REG_REG(block, REG_DI, src_reg);
    host_x86_MOV64_REG_IMM(block, REG_ESI, (uint64_t) uop->p);
#    endif
    host_x86_CALL(block, (void *) loadseg);
    host_x86_TEST32_REG(block, REG_EAX, REG_EAX);
    codegen_exit_Jcc(block, uop, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);

    return 0;
}

/*Guest memory accesses. The common case - the page is in the running
  processor's lookup table and the access doesn't cross a size boundary - is
  done inline, the same way the load/store routines start. Anything else
  calls the routine, which looks the page up again and takes its slow path.
  In: ESI = address (and for stores ECX or XMM_TEMP = data). Clobbers RCX,
  RSI, RDI and R8.*/
enum {
    MEM_B,
    MEM_W,
    MEM_L,
    MEM_Q,
    MEM_SINGLE,
    MEM_DOUBLE
};

static inline int
mem_access_size(int kind)
{
    static const int sizes[] = { 1, 2, 4, 8, 4, 8 };

    return sizes[kind];
}

static inline void
jump_here(uint32_t *jump_p)
{
    *jump_p = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) jump_p + 4);
}

static int
codegen_FCOM_SIGNED_ZERO(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_W(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        uint32_t *not_neg_zero_p;
        uint32_t *not_pos_zero_p;

        if (dest_reg != REG_EAX)
            host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
        host_x86_XOR32_REG_REG(block, REG_EAX, REG_EAX);
        host_x86_COMISD_XREG_XREG(block, src_reg_a, src_reg_b);
        host_x86_LAHF(block);
        host_x86_AND16_REG_IMM(block, REG_EAX, FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3);
        host_x86_MOVQ_REG_XREG(block, REG_RSI, src_reg_a);
        host_x86_MOV64_REG_IMM(block, REG_RDI, 0x8000000000000000ULL);
        host_x86_CMP64_REG_REG(block, REG_RSI, REG_RDI);
        not_neg_zero_p = host_x86_JNZ_long(block);
        host_x86_MOVQ_REG_XREG(block, REG_RSI, src_reg_b);
        host_x86_TEST64_REG(block, REG_RSI, REG_RSI);
        not_pos_zero_p = host_x86_JNZ_long(block);
        host_x86_MOV32_REG_IMM(block, REG_EAX, FPU_SW_C0);
        jump_here(not_neg_zero_p);
        jump_here(not_pos_zero_p);
        if (dest_reg != REG_EAX) {
            host_x86_MOV16_REG_REG(block, dest_reg, REG_EAX);
            host_x86_MOV32_REG_REG(block, REG_EAX, REG_ECX);
        }
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FCOM_SIGNED_ZERO %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_FCOM_CHKCOP(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_W(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        uint32_t *not_image_a_p;
        uint32_t *not_image_b_p;

        if (uop->imm_data)
            codegen_FCOM_SIGNED_ZERO(block, uop);
        else
            codegen_FCOM(block, uop);
        /* C3 when a is the first image and b the first plus 2. */
        host_x86_MOVQ_REG_XREG(block, REG_RSI, src_reg_a);
        host_x86_MOV64_REG_IMM(block, REG_RDI, 0x3fec1a6ff866a936ULL);
        host_x86_CMP64_REG_REG(block, REG_RSI, REG_RDI);
        not_image_a_p = host_x86_JNZ_long(block);
        host_x86_MOVQ_REG_XREG(block, REG_RSI, src_reg_b);
        host_x86_ADD64_REG_IMM(block, REG_RDI, 2);
        host_x86_CMP64_REG_REG(block, REG_RSI, REG_RDI);
        not_image_b_p = host_x86_JNZ_long(block);
        host_x86_MOV16_REG_IMM(block, dest_reg, FPU_SW_C3);
        jump_here(not_image_a_p);
        jump_here(not_image_b_p);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FCOM_CHKCOP %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static void
codegen_mem_load(codeblock_t *block, uop_t *uop, int kind, int dest_reg, void *rout)
{
    uint32_t *misaligned_p = NULL;
    uint32_t *miss_p;
    uint32_t *done_p;

    host_x86_MOV32_REG_REG(block, REG_ECX, REG_ESI);
    host_x86_SHR32_IMM(block, REG_ESI, 12);
    host_x86_MOV64_REG_IMM(block, REG_RDI, (uint64_t) (uintptr_t) readlookup2);
    host_x86_MOV64_REG_BASE_INDEX_SHIFT(block, REG_RSI, REG_RDI, REG_RSI, 3);
    if (mem_access_size(kind) != 1) {
        host_x86_TEST32_REG_IMM(block, REG_ECX, mem_access_size(kind) - 1);
        misaligned_p = host_x86_JNZ_long(block);
    }
    host_x86_CMP64_REG_IMM(block, REG_RSI, (uint32_t) -1);
    miss_p = host_x86_JZ_long(block);
    switch (kind) {
        case MEM_B:
            host_x86_MOV8_REG_BASE_INDEX(block, dest_reg, REG_RSI, REG_RCX);
            break;
        case MEM_W:
            host_x86_MOV16_REG_BASE_INDEX(block, dest_reg, REG_RSI, REG_RCX);
            break;
        case MEM_L:
            host_x86_MOV32_REG_BASE_INDEX(block, dest_reg, REG_RSI, REG_RCX);
            break;
        case MEM_SINGLE:
            host_x86_CVTSS2SD_XREG_BASE_INDEX(block, dest_reg, REG_RSI, REG_RCX);
            break;
        default:
            host_x86_MOVQ_XREG_BASE_INDEX(block, dest_reg, REG_RSI, REG_RCX);
            break;
    }
    done_p = host_x86_JMP_long(block);

    if (misaligned_p)
        jump_here(misaligned_p);
    jump_here(miss_p);
    host_x86_MOV32_REG_REG(block, REG_ESI, REG_ECX);
    host_x86_CALL(block, rout);
    host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
    codegen_exit_Jcc(block, uop, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);
    switch (kind) {
        case MEM_B:
            host_x86_MOV8_REG_REG(block, dest_reg, REG_ECX);
            break;
        case MEM_W:
            host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
            break;
        case MEM_L:
            host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);
            break;
        default:
            host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
            break;
    }
    jump_here(done_p);
}

static void
codegen_mem_store(codeblock_t *block, uop_t *uop, int kind, void *rout)
{
    uint32_t *misaligned_p = NULL;
    uint32_t *miss_p;
    uint32_t *done_p;

    host_x86_MOV32_REG_REG(block, REG_EDI, REG_ESI);
    host_x86_SHR32_IMM(block, REG_ESI, 12);
    host_x86_MOV64_REG_IMM(block, REG_R8, (uint64_t) (uintptr_t) writelookup2);
    host_x86_MOV64_REG_BASE_INDEX_SHIFT(block, REG_RSI, REG_R8, REG_RSI, 3);
    if (mem_access_size(kind) != 1) {
        host_x86_TEST32_REG_IMM(block, REG_EDI, mem_access_size(kind) - 1);
        misaligned_p = host_x86_JNZ_long(block);
    }
    host_x86_CMP64_REG_IMM(block, REG_RSI, (uint32_t) -1);
    miss_p = host_x86_JZ_long(block);
    switch (kind) {
        case MEM_B:
            host_x86_MOV8_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
            break;
        case MEM_W:
            host_x86_MOV16_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
            break;
        case MEM_L:
            host_x86_MOV32_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
            break;
        case MEM_SINGLE:
            host_x86_MOVD_BASE_INDEX_XREG(block, REG_RSI, REG_RDI, REG_XMM_TEMP);
            break;
        default:
            host_x86_MOVQ_BASE_INDEX_XREG(block, REG_RSI, REG_RDI, REG_XMM_TEMP);
            break;
    }
    done_p = host_x86_JMP_long(block);

    if (misaligned_p)
        jump_here(misaligned_p);
    jump_here(miss_p);
    host_x86_MOV32_REG_REG(block, REG_ESI, REG_EDI);
    host_x86_CALL(block, rout);
    host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
    codegen_exit_Jcc(block, uop, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);
    jump_here(done_p);
}

static int
codegen_MEM_LOAD_ABS(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    host_x86_LEA_REG_IMM(block, REG_ESI, seg_reg, uop->imm_data);
    if (REG_IS_B(dest_size)) {
        codegen_mem_load(block, uop, MEM_B, dest_reg, codegen_mem_load_byte);
    } else if (REG_IS_W(dest_size)) {
        codegen_mem_load(block, uop, MEM_W, dest_reg, codegen_mem_load_word);
    } else if (REG_IS_L(dest_size)) {
        codegen_mem_load(block, uop, MEM_L, dest_reg, codegen_mem_load_long);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MEM_LOAD_ABS - %02x\n", uop->dest_reg_a_real);
#    endif

    return 0;
}
static int
codegen_MEM_LOAD_REG(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data) {
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
        if (uop->is_a16) {
            host_x86_AND32_REG_IMM(block, REG_ESI, 0x0000ffff);
        }
    }
    if (REG_IS_B(dest_size)) {
        codegen_mem_load(block, uop, MEM_B, dest_reg, codegen_mem_load_byte);
    } else if (REG_IS_W(dest_size)) {
        codegen_mem_load(block, uop, MEM_W, dest_reg, codegen_mem_load_word);
    } else if (REG_IS_L(dest_size)) {
        codegen_mem_load(block, uop, MEM_L, dest_reg, codegen_mem_load_long);
    } else if (REG_IS_Q(dest_size)) {
        codegen_mem_load(block, uop, MEM_Q, dest_reg, codegen_mem_load_quad);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MEM_LOAD_REG - %02x\n", uop->dest_reg_a_real);
#    endif

    return 0;
}
static int
codegen_MEM_LOAD_REG_EA(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (REG_IS_W(dest_size))
        host_x86_CALL(block, codegen_mem_load_word_ea);
    else if (REG_IS_L(dest_size))
        host_x86_CALL(block, codegen_mem_load_long_ea);
    else
        fatal("MEM_LOAD_REG_EA - %02x\n", uop->dest_reg_a_real);
    host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
    codegen_exit_Jcc(block, uop, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);
    if (REG_IS_W(dest_size))
        host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
    else
        host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_MEM_LOAD_SINGLE(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

#    ifdef RECOMPILER_DEBUG
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    if (!REG_IS_D(dest_size))
        fatal("MEM_LOAD_SINGLE - %02x\n", uop->dest_reg_a_real);
#    endif
    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data)
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
    codegen_mem_load(block, uop, MEM_SINGLE, dest_reg, codegen_mem_load_single);

    return 0;
}
static int
codegen_MEM_LOAD_DOUBLE(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

#    ifdef RECOMPILER_DEBUG
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    if (!REG_IS_D(dest_size))
        fatal("MEM_LOAD_DOUBLE - %02x\n", uop->dest_reg_a_real);
#    endif
    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data)
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
    codegen_mem_load(block, uop, MEM_DOUBLE, dest_reg, codegen_mem_load_double);

    return 0;
}

static int
codegen_MEM_STORE_ABS(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_b_real);

    host_x86_LEA_REG_IMM(block, REG_ESI, seg_reg, uop->imm_data);
    if (REG_IS_B(src_size)) {
        host_x86_MOV8_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_B, codegen_mem_store_byte);
    } else if (REG_IS_W(src_size)) {
        host_x86_MOV16_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_W, codegen_mem_store_word);
    } else if (REG_IS_L(src_size)) {
        host_x86_MOV32_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_L, codegen_mem_store_long);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MEM_STORE_ABS - %02x\n", uop->src_reg_b_real);
#    endif

    return 0;
}

static int
codegen_MEM_STORE_IMM_8(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    host_x86_MOV8_REG_IMM(block, REG_ECX, uop->imm_data);
    codegen_mem_store(block, uop, MEM_B, codegen_mem_store_byte);

    return 0;
}
static int
codegen_MEM_STORE_IMM_16(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    host_x86_MOV16_REG_IMM(block, REG_ECX, uop->imm_data);
    codegen_mem_store(block, uop, MEM_W, codegen_mem_store_word);

    return 0;
}
static int
codegen_MEM_STORE_IMM_32(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data);
    codegen_mem_store(block, uop, MEM_L, codegen_mem_store_long);

    return 0;
}

static int
codegen_MEM_STORE_REG(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_c_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_c_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data)
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
    if (REG_IS_B(src_size)) {
        host_x86_MOV8_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_B, codegen_mem_store_byte);
    } else if (REG_IS_W(src_size)) {
        host_x86_MOV16_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_W, codegen_mem_store_word);
    } else if (REG_IS_L(src_size)) {
        host_x86_MOV32_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_L, codegen_mem_store_long);
    } else if (REG_IS_Q(src_size)) {
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg);
        codegen_mem_store(block, uop, MEM_Q, codegen_mem_store_quad);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MEM_STORE_REG - %02x\n", uop->src_reg_b_real);
#    endif

    return 0;
}

static int
codegen_MEM_STORE_SINGLE(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_c_real);

#    ifdef RECOMPILER_DEBUG
    int src_size = IREG_GET_SIZE(uop->src_reg_c_real);
    if (!REG_IS_D(src_size))
        fatal("MEM_STORE_SINGLE - %02x\n", uop->src_reg_b_real);
#    endif
    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data)
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
    host_x86_CVTSD2SS_XREG_XREG(block, REG_XMM_TEMP, src_reg);
    codegen_mem_store(block, uop, MEM_SINGLE, codegen_mem_store_single);

    return 0;
}
static int
codegen_MEM_STORE_DOUBLE(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_c_real);

#    ifdef RECOMPILER_DEBUG
    int src_size = IREG_GET_SIZE(uop->src_reg_c_real);
    if (!REG_IS_D(src_size))
        fatal("MEM_STORE_DOUBLE - %02x\n", uop->src_reg_b_real);
#    endif
    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data)
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
    host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg);
    codegen_mem_store(block, uop, MEM_DOUBLE, codegen_mem_store_double);

    return 0;
}

static int
codegen_MOV(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
    } else if (REG_IS_D(dest_size) && REG_IS_D(src_size)) {
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg);
    } else if (REG_IS_Q(dest_size) && REG_IS_Q(src_size)) {
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOV %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_MOV_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_L(dest_size)) {
        host_x86_MOV32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size)) {
        host_x86_MOV16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size)) {
        host_x86_MOV8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOV_IMM %02x\n", uop->dest_reg_a_real);
#    endif
    return 0;
}
static int
codegen_MOV_PTR(codeblock_t *block, uop_t *uop)
{
    host_x86_MOV64_REG_IMM(block, uop->dest_reg_a_real, (uint64_t) uop->p);
    return 0;
}
static int
codegen_MOV_REG_PTR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_L(dest_size)) {
        host_x86_MOV32_REG_ABS(block, dest_reg, uop->p);
    } else if (REG_IS_Q(dest_size) || REG_IS_D(dest_size)) {
        /*The pointer may be anywhere in the address space*/
        host_x86_MOV64_REG_IMM(block, REG_ECX, (uint64_t) (uintptr_t) uop->p);
        host_x86_MOVQ_XREG_BASE_OFFSET(block, dest_reg, REG_ECX, 0);
    } else
        fatal("MOV_REG_PTR %02x\n", uop->dest_reg_a_real);

    return 0;
}
static int
codegen_MOVZX_REG_PTR_8(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_L(dest_size)) {
        host_x86_MOVZX_REG_ABS_32_8(block, dest_reg, uop->p);
    } else if (REG_IS_W(dest_size)) {
        host_x86_MOVZX_REG_ABS_16_8(block, dest_reg, uop->p);
    } else if (REG_IS_B(dest_size)) {
        host_x86_MOV8_REG_ABS(block, dest_reg, uop->p);
    } else
        fatal("MOVZX_REG_PTR_8 %02x\n", uop->dest_reg_a_real);

    return 0;
}
static int
codegen_MOVZX_REG_PTR_16(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_L(dest_size)) {
        host_x86_MOVZX_REG_ABS_32_16(block, dest_reg, uop->p);
    } else if (REG_IS_W(dest_size)) {
        host_x86_MOV16_REG_ABS(block, dest_reg, uop->p);
    } else
        fatal("MOVZX_REG_PTR_16 %02x\n", uop->dest_reg_a_real);

    return 0;
}
static int
codegen_MOVSX(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_W(src_size)) {
        host_x86_MOVSX_REG_32_16(block, dest_reg, src_reg);
    } else if (REG_IS_BH(src_size) && (dest_reg & 8)) {
        /*AH-BH and R8-R15 can't share an instruction: extend into ECX*/
        host_x86_MOVSX_REG_32_8(block, REG_ECX, src_reg);
        if (REG_IS_L(dest_size))
            host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);
        else
            host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
    } else if (REG_IS_L(dest_size) && REG_IS_B(src_size)) {
        host_x86_MOVSX_REG_32_8(block, dest_reg, src_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_B(src_size)) {
        host_x86_MOVSX_REG_16_8(block, dest_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOVSX %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_MOVZX(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && REG_IS_L(src_size)) {
        host_x86_MOVD_XREG_REG(block, dest_reg, src_reg);
    } else if (REG_IS_L(dest_size) && REG_IS_Q(src_size)) {
        host_x86_MOVD_REG_XREG(block, dest_reg, src_reg);
    } else if (REG_IS_L(dest_size) && REG_IS_W(src_size)) {
        host_x86_MOVZX_REG_32_16(block, dest_reg, src_reg);
    } else if (REG_IS_BH(src_size) && (dest_reg & 8)) {
        /*AH-BH and R8-R15 can't share an instruction: extend into ECX*/
        host_x86_MOVZX_REG_32_8(block, REG_ECX, src_reg);
        if (REG_IS_L(dest_size))
            host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);
        else
            host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
    } else if (REG_IS_L(dest_size) && REG_IS_B(src_size)) {
        host_x86_MOVZX_REG_32_8(block, dest_reg, src_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_B(src_size)) {
        host_x86_MOVZX_REG_16_8(block, dest_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOVZX %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_MOV_DOUBLE_INT(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_L(src_size)) {
        host_x86_CVTSI2SD_XREG_REG(block, dest_reg, src_reg);
    } else if (REG_IS_D(dest_size) && REG_IS_W(src_size)) {
        host_x86_MOVSX_REG_32_16(block, REG_ECX, src_reg);
        host_x86_CVTSI2SD_XREG_REG(block, dest_reg, REG_ECX);
    } else if (REG_IS_D(dest_size) && REG_IS_Q(src_size)) {
        host_x86_MOVQ_REG_XREG(block, REG_RCX, src_reg);
        host_x86_CVTSI2SD_XREG_REG64(block, dest_reg, REG_RCX);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOV_DOUBLE_INT %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_MOV_INT_DOUBLE(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_D(src_size)) {
        host_x86_LDMXCSR(block, &cpu_state.new_fp_control);
        host_x86_CVTSD2SI_REG_XREG(block, dest_reg, src_reg);
        host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
    } else if (REG_IS_W(dest_size) && REG_IS_D(src_size)) {
        host_x86_LDMXCSR(block, &cpu_state.new_fp_control);
        host_x86_CVTSD2SI_REG_XREG(block, REG_ECX, src_reg);
        /* A word overflow stores the integer indefinite, not the low
           word of the dword conversion. This also catches NaN/infinity. */
        host_x86_MOVSX_REG_32_16(block, REG_ESI, REG_ECX);
        host_x86_CMP32_REG_REG(block, REG_ESI, REG_ECX);
        uint32_t *in_range = host_x86_JZ_long(block);
        host_x86_MOV32_REG_IMM(block, REG_ECX, 0x8000);
        *in_range = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) in_range) - 4;
        host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
        host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOV_INT_DOUBLE %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_MOV_INT_DOUBLE_64(codeblock_t *block, uop_t *uop)
{
    int dest_reg    = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg     = HOST_REG_GET(uop->src_reg_a_real);
    int src_64_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int tag_reg     = HOST_REG_GET(uop->src_reg_c_real);
    int dest_size   = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size    = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_64_size = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_D(src_size) && REG_IS_Q(src_64_size)) {
        uint32_t *branch_offset;

        /*If TAG_UINT64 is set then the source is MM[]. Otherwise it is a double in ST()*/
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_64_reg);
        host_x86_TEST8_REG(block, tag_reg, tag_reg);
        branch_offset = host_x86_JS_long(block);

        host_x86_LDMXCSR(block, &cpu_state.new_fp_control);
        host_x86_CVTSD2SI_REG64_XREG(block, REG_RCX, src_reg);
        host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
        host_x86_MOVQ_XREG_REG(block, dest_reg, REG_RCX);

        *branch_offset = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 4;
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOV_INT_DOUBLE_64 %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_NOP(UNUSED(codeblock_t *block), UNUSED(uop_t *uop))
{
    return 0;
}

static int
codegen_OR(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    /*dest == src_b: moving src_a into dest first would clobber src_b; the
      operation commutes, so take the sources the other way round*/
    if ((dest_reg == src_reg_b) && (dest_reg != src_reg_a)) {
        int t     = src_reg_a;
        src_reg_a = src_reg_b;
        src_reg_b = t;
    }

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_POR_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
        host_x86_OR32_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
        host_x86_OR16_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
        host_x86_OR8_REG_REG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("OR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_OR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_x86_OR32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_x86_OR16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_x86_OR8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("OR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_PACKSSWB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PACKSSWB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PACKSSWB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PACKSSDW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PACKSSDW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PACKSSDW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PACKUSWB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PACKUSWB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PACKUSWB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_PADDB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDSB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDSB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDSW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDSW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDUSB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDUSB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDUSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDUSW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDUSW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDUSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_PCMPEQB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPEQB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPEQB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PCMPEQW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPEQW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPEQW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PCMPEQD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPEQD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPEQD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PCMPGTB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPGTB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPGTB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PCMPGTW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPGTW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPGTW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PCMPGTD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPGTD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPGTD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_PF2ID(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a)) {
        host_x86_LDMXCSR(block, &cpu_state.trunc_fp_control);
        host_x86_CVTPS2DQ_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PF2ID %02x %02x\n", uop->dest_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PFADD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_ADDPS_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFCMPEQ(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_CMPPS_XREG_XREG(block, dest_reg, src_reg_b, CMPPS_EQ);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFCMPEQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFCMPGE(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_CMPPS_XREG_XREG(block, dest_reg, src_reg_b, CMPPS_NLT);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFCMPGE %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFCMPGT(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_CMPPS_XREG_XREG(block, dest_reg, src_reg_b, CMPPS_NLE);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFCMPGT %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFMAX(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        /*MAXPS returns its second operand on a tie or a NaN; the interpreter
          keeps the destination unless the source is greater, which is MAXPS
          with the operands the other way round*/
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MAXPS_XREG_XREG(block, REG_XMM_TEMP, dest_reg);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFMAX %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFMIN(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        /*As PFMAX: the destination unless the source is smaller*/
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MINPS_XREG_XREG(block, REG_XMM_TEMP, dest_reg);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFMIN %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFMUL(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_MULPS_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFMUL %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFRCP(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a)) {
        /*TODO: This could be improved (use RCPSS + iteration)*/
        host_x86_MOV32_REG_IMM(block, REG_ECX, 1);
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_CVTSI2SS_XREG_REG(block, dest_reg, REG_ECX);
        host_x86_DIVSS_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
        host_x86_UNPCKLPS_XREG_XREG(block, dest_reg, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFRCP %02x %02x\n", uop->dest_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PFRSQRT(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a)) {
        /*1.0 / sqrt(x) in double precision, rounded to single once, as the
          interpreter does it; in single precision it rounds twice*/
        host_x86_CVTSS2SD_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_SQRTSD_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
        host_x86_MOV32_REG_IMM(block, REG_ECX, 1);
        host_x86_CVTSI2SD_XREG_REG(block, dest_reg, REG_ECX);
        host_x86_DIVSD_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
        host_x86_CVTSD2SS_XREG_XREG(block, dest_reg, dest_reg);
        host_x86_UNPCKLPS_XREG_XREG(block, dest_reg, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFRSQRT %02x %02x\n", uop->dest_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PFSUB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_SUBPS_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) {
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_SUBPS_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFSUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PI2FD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a)) {
        host_x86_CVTDQ2PS_XREG_XREG(block, dest_reg, src_reg_a);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PI2FD %02x %02x\n", uop->dest_reg_a_real);
#    endif
    return 0;
}

static int
codegen_PMADDWD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PMADDWD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PMULHW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PMULHW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PMULHW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PMULHW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PMULLW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PMULLW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PMULLW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_PSLLW_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSLLW_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSLLW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSLLD_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSLLD_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSLLD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSLLQ_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSLLQ_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSLLQ_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSRAW_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSRAW_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSRAW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSRAD_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSRAD_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSRAD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSRLW_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSRLW_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSRLW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSRLD_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSRLD_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSRLD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSRLQ_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSRLQ_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSRLQ_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_PSUBB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBSB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBSB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBSW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBSW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBUSB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBUSB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBUSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBUSW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBUSW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBUSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_PUNPCKHBW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKHBW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKHBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PUNPCKHWD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKHWD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKHWD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PUNPCKHDQ(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKHDQ_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKHDQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PUNPCKLBW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKLBW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKLBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PUNPCKLWD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKLWD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKLWD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PUNPCKLDQ(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKLDQ_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKLDQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_ROL(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL32_CL(block, dest_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL16_CL(block, dest_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL8_CL(block, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ROL %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_ROL_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL32_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL16_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL8_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ROL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_ROR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR32_CL(block, dest_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR16_CL(block, dest_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR8_CL(block, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ROR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_ROR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR32_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR16_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR8_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ROR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_SAR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR32_CL(block, dest_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR16_CL(block, dest_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR8_CL(block, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SAR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_SAR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR32_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR16_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR8_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SAR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_SHL(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL32_CL(block, dest_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL16_CL(block, dest_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL8_CL(block, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SHL %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_SHL_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL32_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL16_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL8_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SHL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_SHR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR32_CL(block, dest_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR16_CL(block, dest_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR8_CL(block, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SHR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_SHR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR32_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR16_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR8_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SHR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_STORE_PTR_IMM(codeblock_t *block, uop_t *uop)
{
#    ifdef RECOMPILER_DEBUG
    if (((uintptr_t) uop->p) >> 32)
        fatal("STORE_PTR_IMM 64-bit addr\n");
#    endif
    host_x86_MOV32_ABS_IMM(block, uop->p, uop->imm_data);
    return 0;
}
static int
codegen_STORE_PTR_IMM_8(codeblock_t *block, uop_t *uop)
{
#    ifdef RECOMPILER_DEBUG
    if (((uintptr_t) uop->p) >> 32)
        fatal("STORE_PTR_IMM_8 64-bit addr\n");
#    endif
    host_x86_MOV8_ABS_IMM(block, uop->p, uop->imm_data);
    return 0;
}

static int
codegen_SUB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    /*dest == src_b: moving src_a into dest first would clobber src_b, so
      src_b goes to the scratch register first*/
    if ((dest_reg == src_reg_b) && (dest_reg != src_reg_a)) {
        if (REG_IS_L(src_size_b))
            host_x86_MOV32_REG_REG(block, REG_ECX, src_reg_b);
        else if (REG_IS_W(src_size_b))
            host_x86_MOV16_REG_REG(block, REG_ECX, src_reg_b);
        else
            host_x86_MOV8_REG_REG(block, REG_ECX, src_reg_b);
        src_reg_b = REG_ECX;
    }

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
        host_x86_SUB32_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
        host_x86_SUB16_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
        host_x86_SUB8_REG_REG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_SUB_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SUB32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SUB16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SUB8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SUB_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_TEST_JNS_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_TEST32_REG(block, src_reg, src_reg);
    } else if (REG_IS_W(src_size)) {
        host_x86_TEST16_REG(block, src_reg, src_reg);
    } else if (REG_IS_B(src_size)) {
        host_x86_TEST8_REG(block, src_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("TEST_JNS_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNS_long(block);

    return 0;
}
static int
codegen_TEST_JS_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_TEST32_REG(block, src_reg, src_reg);
    } else if (REG_IS_W(src_size)) {
        host_x86_TEST16_REG(block, src_reg, src_reg);
    } else if (REG_IS_B(src_size)) {
        host_x86_TEST8_REG(block, src_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("TEST_JS_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JS_long(block);

    return 0;
}

/*The condition goes through CL, which the allocator never hands out, so the
  destination can be any host register*/
static int
codegen_CMP_SET(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    } else
        fatal("CMP_SET %02x %02x\n", uop->src_reg_a_real, uop->src_reg_b_real);
    if (!REG_IS_L(dest_size))
        fatal("CMP_SET dest %02x\n", uop->dest_reg_a_real);

    host_x86_SETcc_REG(block, uop->imm_data, REG_ECX);
    host_x86_MOVZX_REG_32_8(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_TEST_SET(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_TEST32_REG(block, src_reg, src_reg);
    } else if (REG_IS_W(src_size)) {
        host_x86_TEST16_REG(block, src_reg, src_reg);
    } else if (REG_IS_B(src_size)) {
        host_x86_TEST8_REG(block, src_reg, src_reg);
    } else
        fatal("TEST_SET %02x\n", uop->src_reg_a_real);
    if (!REG_IS_L(dest_size))
        fatal("TEST_SET dest %02x\n", uop->dest_reg_a_real);

    host_x86_SETcc_REG(block, uop->imm_data, REG_ECX);
    host_x86_MOVZX_REG_32_8(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_BSWAP(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (!REG_IS_L(dest_size) || !REG_IS_L(src_size))
        fatal("BSWAP %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    if (dest_reg != src_reg)
        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
    host_x86_BSWAP32_REG(block, dest_reg);

    return 0;
}
static int
codegen_IMUL(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (!REG_IS_L(dest_size) || !REG_IS_L(src_size_a) || !REG_IS_L(src_size_b))
        fatal("IMUL %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    if (dest_reg == src_reg_a)
        host_x86_IMUL32_REG_REG(block, dest_reg, src_reg_b);
    else if (dest_reg == src_reg_b)
        host_x86_IMUL32_REG_REG(block, dest_reg, src_reg_a);
    else {
        host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
        host_x86_IMUL32_REG_REG(block, dest_reg, src_reg_b);
    }

    return 0;
}
static int
codegen_IMUL_OV(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (!REG_IS_L(dest_size) || !REG_IS_L(src_size_a) || !REG_IS_L(src_size_b))
        fatal("IMUL_OV %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    /*The product is thrown away; OF says whether it fit*/
    host_x86_MOV32_REG_REG(block, REG_ECX, src_reg_a);
    host_x86_IMUL32_REG_REG(block, REG_ECX, src_reg_b);
    host_x86_SETcc_REG(block, UOP_CC_O, REG_ECX);
    host_x86_MOVZX_REG_32_8(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_MUL_HI_common(codeblock_t *block, uop_t *uop, int is_signed)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (!REG_IS_L(dest_size) || !REG_IS_L(src_size_a) || !REG_IS_L(src_size_b))
        fatal("MUL_HI %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    /*Both operands widened to 64 bits in RCX and RSI, which the allocator
      never hands out; the low 64 bits of that product are the whole product*/
    if (is_signed) {
        host_x86_MOVSXD_REG_REG(block, REG_RCX, src_reg_a);
        host_x86_MOVSXD_REG_REG(block, REG_RSI, src_reg_b);
    } else {
        host_x86_MOV32_REG_REG(block, REG_ECX, src_reg_a);
        host_x86_MOV32_REG_REG(block, REG_ESI, src_reg_b);
    }
    host_x86_IMUL64_REG_REG(block, REG_RCX, REG_RSI);
    host_x86_SHR64_IMM(block, REG_RCX, 32);
    host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_MUL_HI(codeblock_t *block, uop_t *uop)
{
    return codegen_MUL_HI_common(block, uop, 0);
}
static int
codegen_IMUL_HI(codeblock_t *block, uop_t *uop)
{
    return codegen_MUL_HI_common(block, uop, 1);
}
static int
codegen_BS_common(codeblock_t *block, uop_t *uop, int reverse)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (!REG_IS_L(dest_size) || !REG_IS_L(src_size_a) || !REG_IS_L(src_size_b))
        fatal("BSF/BSR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    /*The scan's result when the source is zero is not relied on: the old
      value is kept unless ZF says a bit was found*/
    if (reverse)
        host_x86_BSR32_REG_REG(block, REG_ESI, src_reg_a);
    else
        host_x86_BSF32_REG_REG(block, REG_ESI, src_reg_a);
    host_x86_MOV32_REG_REG(block, REG_ECX, src_reg_b);
    host_x86_CMOVNZ32_REG_REG(block, REG_ECX, REG_ESI);
    host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_BSF(codeblock_t *block, uop_t *uop)
{
    return codegen_BS_common(block, uop, 0);
}
static int
codegen_BSR(codeblock_t *block, uop_t *uop)
{
    return codegen_BS_common(block, uop, 1);
}

static int
codegen_XOR(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    /*dest == src_b: moving src_a into dest first would clobber src_b; the
      operation commutes, so take the sources the other way round*/
    if ((dest_reg == src_reg_b) && (dest_reg != src_reg_a)) {
        int t     = src_reg_a;
        src_reg_a = src_reg_b;
        src_reg_b = t;
    }

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PXOR_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
        host_x86_XOR32_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
        host_x86_XOR16_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
        host_x86_XOR8_REG_REG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("XOR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_XOR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_x86_XOR32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_x86_XOR16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_x86_XOR8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("XOR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

const uOpFn uop_handlers[UOP_MAX] = {
    [UOP_CALL_FUNC & UOP_MASK] = codegen_CALL_FUNC,
    [UOP_CALL_FUNC_ORDERED &
        UOP_MASK]
    = codegen_CALL_FUNC_ORDERED,
    [UOP_CALL_FUNC_RESULT_ORDERED &
        UOP_MASK]
    = codegen_CALL_FUNC_RESULT_ORDERED,
    [UOP_CALL_REP_STRING &
        UOP_MASK]
    = codegen_CALL_REP_STRING,
    [UOP_SMI_EXIT_IMM &
        UOP_MASK]
    = codegen_SMI_EXIT_IMM,
    [UOP_SMI_EXIT &
        UOP_MASK]
    = codegen_SMI_EXIT_IMM,
    [UOP_CALL_FUNC_RESULT &
        UOP_MASK]
    = codegen_CALL_FUNC_RESULT,
    [UOP_CALL_INSTRUCTION_FUNC &
        UOP_MASK]
    = codegen_CALL_INSTRUCTION_FUNC,

    [UOP_JMP &
        UOP_MASK]
    = codegen_JMP,
    [UOP_JMP_DEST &
        UOP_MASK]
    = codegen_JMP_DEST,

    [UOP_LOAD_SEG &
        UOP_MASK]
    = codegen_LOAD_SEG,

    [UOP_LOAD_FUNC_ARG_0 &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG0,
    [UOP_LOAD_FUNC_ARG_1 &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG1,
    [UOP_LOAD_FUNC_ARG_2 &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG2,
    [UOP_LOAD_FUNC_ARG_3 &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG3,

    [UOP_LOAD_FUNC_ARG_0_IMM &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG0_IMM,
    [UOP_LOAD_FUNC_ARG_1_IMM &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG1_IMM,
    [UOP_LOAD_FUNC_ARG_2_IMM &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG2_IMM,
    [UOP_LOAD_FUNC_ARG_3_IMM &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG3_IMM,

    [UOP_STORE_P_IMM &
        UOP_MASK]
    = codegen_STORE_PTR_IMM,
    [UOP_STORE_P_IMM_8 &
        UOP_MASK]
    = codegen_STORE_PTR_IMM_8,

    [UOP_MEM_LOAD_ABS &
        UOP_MASK]
    = codegen_MEM_LOAD_ABS,
    [UOP_MEM_LOAD_REG &
        UOP_MASK]
    = codegen_MEM_LOAD_REG,
    [UOP_MEM_LOAD_REG_EA &
        UOP_MASK]
    = codegen_MEM_LOAD_REG_EA,
    [UOP_MEM_LOAD_SINGLE &
        UOP_MASK]
    = codegen_MEM_LOAD_SINGLE,
    [UOP_MEM_LOAD_DOUBLE &
        UOP_MASK]
    = codegen_MEM_LOAD_DOUBLE,

    [UOP_MEM_STORE_ABS &
        UOP_MASK]
    = codegen_MEM_STORE_ABS,
    [UOP_MEM_STORE_REG &
        UOP_MASK]
    = codegen_MEM_STORE_REG,
    [UOP_MEM_STORE_IMM_8 &
        UOP_MASK]
    = codegen_MEM_STORE_IMM_8,
    [UOP_MEM_STORE_IMM_16 &
        UOP_MASK]
    = codegen_MEM_STORE_IMM_16,
    [UOP_MEM_STORE_IMM_32 &
        UOP_MASK]
    = codegen_MEM_STORE_IMM_32,
    [UOP_MEM_STORE_SINGLE &
        UOP_MASK]
    = codegen_MEM_STORE_SINGLE,
    [UOP_MEM_STORE_DOUBLE &
        UOP_MASK]
    = codegen_MEM_STORE_DOUBLE,

    [UOP_MOV &
        UOP_MASK]
    = codegen_MOV,
    [UOP_MOV_PTR &
        UOP_MASK]
    = codegen_MOV_PTR,
    [UOP_MOV_IMM &
        UOP_MASK]
    = codegen_MOV_IMM,
    [UOP_MOVSX &
        UOP_MASK]
    = codegen_MOVSX,
    [UOP_MOVZX &
        UOP_MASK]
    = codegen_MOVZX,
    [UOP_MOV_DOUBLE_INT &
        UOP_MASK]
    = codegen_MOV_DOUBLE_INT,
    [UOP_MOV_INT_DOUBLE &
        UOP_MASK]
    = codegen_MOV_INT_DOUBLE,
    [UOP_MOV_INT_DOUBLE_64 &
        UOP_MASK]
    = codegen_MOV_INT_DOUBLE_64,
    [UOP_MOV_REG_PTR &
        UOP_MASK]
    = codegen_MOV_REG_PTR,
    [UOP_MOVZX_REG_PTR_8 &
        UOP_MASK]
    = codegen_MOVZX_REG_PTR_8,
    [UOP_MOVZX_REG_PTR_16 &
        UOP_MASK]
    = codegen_MOVZX_REG_PTR_16,

    [UOP_ADD &
        UOP_MASK]
    = codegen_ADD,
    [UOP_ADD_IMM &
        UOP_MASK]
    = codegen_ADD_IMM,
    [UOP_ADD_LSHIFT &
        UOP_MASK]
    = codegen_ADD_LSHIFT,
    [UOP_AND &
        UOP_MASK]
    = codegen_AND,
    [UOP_ANDN &
        UOP_MASK]
    = codegen_ANDN,
    [UOP_AND_IMM &
        UOP_MASK]
    = codegen_AND_IMM,
    [UOP_OR &
        UOP_MASK]
    = codegen_OR,
    [UOP_OR_IMM &
        UOP_MASK]
    = codegen_OR_IMM,
    [UOP_SUB &
        UOP_MASK]
    = codegen_SUB,
    [UOP_SUB_IMM &
        UOP_MASK]
    = codegen_SUB_IMM,
    [UOP_XOR &
        UOP_MASK]
    = codegen_XOR,
    [UOP_XOR_IMM &
        UOP_MASK]
    = codegen_XOR_IMM,

    [UOP_SAR &
        UOP_MASK]
    = codegen_SAR,
    [UOP_SAR_IMM &
        UOP_MASK]
    = codegen_SAR_IMM,
    [UOP_SHL &
        UOP_MASK]
    = codegen_SHL,
    [UOP_SHL_IMM &
        UOP_MASK]
    = codegen_SHL_IMM,
    [UOP_SHR &
        UOP_MASK]
    = codegen_SHR,
    [UOP_SHR_IMM &
        UOP_MASK]
    = codegen_SHR_IMM,
    [UOP_ROL &
        UOP_MASK]
    = codegen_ROL,
    [UOP_ROL_IMM &
        UOP_MASK]
    = codegen_ROL_IMM,
    [UOP_ROR &
        UOP_MASK]
    = codegen_ROR,
    [UOP_ROR_IMM &
        UOP_MASK]
    = codegen_ROR_IMM,

    [UOP_CMP_IMM_JZ &
        UOP_MASK]
    = codegen_CMP_IMM_JZ,

    [UOP_CMP_JB &
        UOP_MASK]
    = codegen_CMP_JB,
    [UOP_CMP_JNBE &
        UOP_MASK]
    = codegen_CMP_JNBE,
    [UOP_FCMP_JWITHIN_DEST &
        UOP_MASK]
    = codegen_FCMP_JWITHIN_DEST,
    [UOP_CMP_JNWITHIN &
        UOP_MASK]
    = codegen_CMP_JNWITHIN,
    [UOP_CMN_IMM_JC &
        UOP_MASK]
    = codegen_CMN_IMM_JC,
    [UOP_CMP_JNWITHIN_TOP &
        UOP_MASK]
    = codegen_CMP_JNWITHIN_TOP,

    [UOP_CMP_JNB_DEST &
        UOP_MASK]
    = codegen_CMP_JNB_DEST,
    [UOP_CMP_JNBE_DEST &
        UOP_MASK]
    = codegen_CMP_JNBE_DEST,
    [UOP_CMP_JNL_DEST &
        UOP_MASK]
    = codegen_CMP_JNL_DEST,
    [UOP_CMP_JNLE_DEST &
        UOP_MASK]
    = codegen_CMP_JNLE_DEST,
    [UOP_CMP_JNO_DEST &
        UOP_MASK]
    = codegen_CMP_JNO_DEST,
    [UOP_CMP_JNZ_DEST &
        UOP_MASK]
    = codegen_CMP_JNZ_DEST,
    [UOP_CMP_JB_DEST &
        UOP_MASK]
    = codegen_CMP_JB_DEST,
    [UOP_CMP_JBE_DEST &
        UOP_MASK]
    = codegen_CMP_JBE_DEST,
    [UOP_CMP_JL_DEST &
        UOP_MASK]
    = codegen_CMP_JL_DEST,
    [UOP_CMP_JLE_DEST &
        UOP_MASK]
    = codegen_CMP_JLE_DEST,
    [UOP_CMP_JO_DEST &
        UOP_MASK]
    = codegen_CMP_JO_DEST,
    [UOP_CMP_JZ_DEST &
        UOP_MASK]
    = codegen_CMP_JZ_DEST,

    [UOP_CMP_IMM_JNZ_DEST &
        UOP_MASK]
    = codegen_CMP_IMM_JNZ_DEST,
    [UOP_CMP_IMM_JZ_DEST &
        UOP_MASK]
    = codegen_CMP_IMM_JZ_DEST,

    [UOP_TEST_JNS_DEST &
        UOP_MASK]
    = codegen_TEST_JNS_DEST,
    [UOP_TEST_JS_DEST &
        UOP_MASK]
    = codegen_TEST_JS_DEST,
    [UOP_CMP_SET &
        UOP_MASK]
    = codegen_CMP_SET,
    [UOP_TEST_SET &
        UOP_MASK]
    = codegen_TEST_SET,
    [UOP_BSWAP &
        UOP_MASK]
    = codegen_BSWAP,
    [UOP_IMUL &
        UOP_MASK]
    = codegen_IMUL,
    [UOP_IMUL_OV &
        UOP_MASK]
    = codegen_IMUL_OV,
    [UOP_MUL_HI &
        UOP_MASK]
    = codegen_MUL_HI,
    [UOP_IMUL_HI &
        UOP_MASK]
    = codegen_IMUL_HI,
    [UOP_BSF &
        UOP_MASK]
    = codegen_BSF,
    [UOP_BSR &
        UOP_MASK]
    = codegen_BSR,

    [UOP_FP_ENTER &
        UOP_MASK]
    = codegen_FP_ENTER,
    [UOP_FP_ENTER_ORDERED &
        UOP_MASK]
    = codegen_FP_ENTER,
    [UOP_FP_CONTROL_SET &
        UOP_MASK]
    = codegen_FP_CONTROL_SET,
    [UOP_MMX_ENTER &
        UOP_MASK]
    = codegen_MMX_ENTER,

    [UOP_FADD &
        UOP_MASK]
    = codegen_FADD,
    [UOP_FCOM &
        UOP_MASK]
    = codegen_FCOM,
    [UOP_FCOM_SIGNED_ZERO &
        UOP_MASK]
    = codegen_FCOM_SIGNED_ZERO,
    [UOP_FCOM_CHKCOP &
        UOP_MASK]
    = codegen_FCOM_CHKCOP,
    [UOP_FDIV &
        UOP_MASK]
    = codegen_FDIV,
    [UOP_FDIV_JSLOW_DEST &
        UOP_MASK]
    = codegen_FDIV_JSLOW_DEST,
    [UOP_FDIV_FLAGS_JSLOW_DEST &
        UOP_MASK]
    = codegen_FDIV_FLAGS_JSLOW_DEST,
    [UOP_FMUL &
        UOP_MASK]
    = codegen_FMUL,
    [UOP_FSUB &
        UOP_MASK]
    = codegen_FSUB,

    [UOP_FABS &
        UOP_MASK]
    = codegen_FABS,
    [UOP_FCHS &
        UOP_MASK]
    = codegen_FCHS,
    [UOP_FSQRT &
        UOP_MASK]
    = codegen_FSQRT,
    [UOP_FROUND_S &
        UOP_MASK]
    = codegen_FROUND_S,
    [UOP_FTST &
        UOP_MASK]
    = codegen_FTST,

    [UOP_PACKSSWB &
        UOP_MASK]
    = codegen_PACKSSWB,
    [UOP_PACKSSDW &
        UOP_MASK]
    = codegen_PACKSSDW,
    [UOP_PACKUSWB &
        UOP_MASK]
    = codegen_PACKUSWB,

    [UOP_PADDB &
        UOP_MASK]
    = codegen_PADDB,
    [UOP_PADDW &
        UOP_MASK]
    = codegen_PADDW,
    [UOP_PADDD &
        UOP_MASK]
    = codegen_PADDD,
    [UOP_PADDSB &
        UOP_MASK]
    = codegen_PADDSB,
    [UOP_PADDSW &
        UOP_MASK]
    = codegen_PADDSW,
    [UOP_PADDUSB &
        UOP_MASK]
    = codegen_PADDUSB,
    [UOP_PADDUSW &
        UOP_MASK]
    = codegen_PADDUSW,

    [UOP_PCMPEQB &
        UOP_MASK]
    = codegen_PCMPEQB,
    [UOP_PCMPEQW &
        UOP_MASK]
    = codegen_PCMPEQW,
    [UOP_PCMPEQD &
        UOP_MASK]
    = codegen_PCMPEQD,
    [UOP_PCMPGTB &
        UOP_MASK]
    = codegen_PCMPGTB,
    [UOP_PCMPGTW &
        UOP_MASK]
    = codegen_PCMPGTW,
    [UOP_PCMPGTD &
        UOP_MASK]
    = codegen_PCMPGTD,

    [UOP_PF2ID &
        UOP_MASK]
    = codegen_PF2ID,
    [UOP_PFADD &
        UOP_MASK]
    = codegen_PFADD,
    [UOP_PFCMPEQ &
        UOP_MASK]
    = codegen_PFCMPEQ,
    [UOP_PFCMPGE &
        UOP_MASK]
    = codegen_PFCMPGE,
    [UOP_PFCMPGT &
        UOP_MASK]
    = codegen_PFCMPGT,
    [UOP_PFMAX &
        UOP_MASK]
    = codegen_PFMAX,
    [UOP_PFMIN &
        UOP_MASK]
    = codegen_PFMIN,
    [UOP_PFMUL &
        UOP_MASK]
    = codegen_PFMUL,
    [UOP_PFRCP &
        UOP_MASK]
    = codegen_PFRCP,
    [UOP_PFRSQRT &
        UOP_MASK]
    = codegen_PFRSQRT,
    [UOP_PFSUB &
        UOP_MASK]
    = codegen_PFSUB,
    [UOP_PI2FD &
        UOP_MASK]
    = codegen_PI2FD,

    [UOP_PMADDWD &
        UOP_MASK]
    = codegen_PMADDWD,
    [UOP_PMULHW &
        UOP_MASK]
    = codegen_PMULHW,
    [UOP_PMULLW &
        UOP_MASK]
    = codegen_PMULLW,

    [UOP_PSLLW_IMM &
        UOP_MASK]
    = codegen_PSLLW_IMM,
    [UOP_PSLLD_IMM &
        UOP_MASK]
    = codegen_PSLLD_IMM,
    [UOP_PSLLQ_IMM &
        UOP_MASK]
    = codegen_PSLLQ_IMM,
    [UOP_PSRAW_IMM &
        UOP_MASK]
    = codegen_PSRAW_IMM,
    [UOP_PSRAD_IMM &
        UOP_MASK]
    = codegen_PSRAD_IMM,
    [UOP_PSRLW_IMM &
        UOP_MASK]
    = codegen_PSRLW_IMM,
    [UOP_PSRLD_IMM &
        UOP_MASK]
    = codegen_PSRLD_IMM,
    [UOP_PSRLQ_IMM &
        UOP_MASK]
    = codegen_PSRLQ_IMM,

    [UOP_PSUBB &
        UOP_MASK]
    = codegen_PSUBB,
    [UOP_PSUBW &
        UOP_MASK]
    = codegen_PSUBW,
    [UOP_PSUBD &
        UOP_MASK]
    = codegen_PSUBD,
    [UOP_PSUBSB &
        UOP_MASK]
    = codegen_PSUBSB,
    [UOP_PSUBSW &
        UOP_MASK]
    = codegen_PSUBSW,
    [UOP_PSUBUSB &
        UOP_MASK]
    = codegen_PSUBUSB,
    [UOP_PSUBUSW &
        UOP_MASK]
    = codegen_PSUBUSW,

    [UOP_PUNPCKHBW &
        UOP_MASK]
    = codegen_PUNPCKHBW,
    [UOP_PUNPCKHWD &
        UOP_MASK]
    = codegen_PUNPCKHWD,
    [UOP_PUNPCKHDQ &
        UOP_MASK]
    = codegen_PUNPCKHDQ,
    [UOP_PUNPCKLBW &
        UOP_MASK]
    = codegen_PUNPCKLBW,
    [UOP_PUNPCKLWD &
        UOP_MASK]
    = codegen_PUNPCKLWD,
    [UOP_PUNPCKLDQ &
        UOP_MASK]
    = codegen_PUNPCKLDQ,

    [UOP_NOP_BARRIER &
        UOP_MASK]
    = codegen_NOP
};

void
codegen_direct_read_8(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOV8_REG_ABS(block, host_reg, p);
}
void
codegen_direct_read_16(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOV16_REG_ABS(block, host_reg, p);
}
void
codegen_direct_read_32(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOV32_REG_ABS(block, host_reg, p);
}
void
codegen_direct_read_64(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOVQ_XREG_ABS(block, host_reg, p);
}
void
codegen_direct_read_pointer(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOV64_REG_ABS(block, host_reg, p);
}
void
codegen_direct_read_double(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOVQ_XREG_ABS(block, host_reg, p);
}
void
codegen_direct_read_st_8(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOV8_REG_ABS_REG_REG_SHIFT(block, host_reg, offset, REG_RBP, REG_ECX, 0);
}
void
codegen_direct_read_st_64(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOVQ_XREG_ABS_REG_REG_SHIFT(block, host_reg, offset, REG_RBP, REG_ECX, 3);
}
void
codegen_direct_read_st_double(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOVQ_XREG_ABS_REG_REG_SHIFT(block, host_reg, offset, REG_RBP, REG_ECX, 3);
}

void
codegen_direct_write_8(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOV8_ABS_REG(block, p, host_reg);
}
void
codegen_direct_write_16(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOV16_ABS_REG(block, p, host_reg);
}
void
codegen_direct_write_32(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOV32_ABS_REG(block, p, host_reg);
}
void
codegen_direct_write_64(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOVQ_ABS_XREG(block, p, host_reg);
}
void
codegen_direct_write_pointer(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOV64_ABS_REG(block, p, host_reg);
}
void
codegen_direct_write_double(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOVQ_ABS_XREG(block, p, host_reg);
}
void
codegen_direct_write_st_8(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOV8_ABS_REG_REG_SHIFT_REG(block, offset, REG_RBP, REG_ECX, 0, host_reg);
}
void
codegen_direct_write_st_64(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOVQ_ABS_REG_REG_SHIFT_XREG(block, offset, REG_RBP, REG_ECX, 3, host_reg);
}
void
codegen_direct_write_st_double(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOVQ_ABS_REG_REG_SHIFT_XREG(block, offset, REG_RBP, REG_ECX, 3, host_reg);
}

void
codegen_direct_write_ptr(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOV64_ABS_REG(block, p, host_reg);
}

void
codegen_direct_read_16_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_x86_MOV16_REG_BASE_OFFSET(block, host_reg, REG_ESP, stack_offset);
}
void
codegen_direct_read_32_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_x86_MOV32_REG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}
void
codegen_direct_read_64_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_x86_MOVQ_XREG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}
void
codegen_direct_read_pointer_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_x86_MOV64_REG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}
void
codegen_direct_read_double_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_x86_MOVQ_XREG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}

void
codegen_direct_write_32_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_x86_MOV32_BASE_OFFSET_REG(block, REG_RSP, stack_offset, host_reg);
}
void
codegen_direct_write_64_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_x86_MOVQ_BASE_OFFSET_XREG(block, REG_RSP, stack_offset, host_reg);
}
void
codegen_direct_write_pointer_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_x86_MOV64_BASE_OFFSET_REG(block, REG_RSP, stack_offset, host_reg);
}
void
codegen_direct_write_double_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_x86_MOVQ_BASE_OFFSET_XREG(block, REG_RSP, stack_offset, host_reg);
}

void
codegen_set_jump_dest(UNUSED(codeblock_t *block), void *p)
{
    *(uint32_t *) p = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) p + 4);
}

void
codegen_direct_write_8_imm(codeblock_t *block, void *p, uint8_t imm_data)
{
    host_x86_MOV8_ABS_IMM(block, p, imm_data);
}
void
codegen_direct_write_16_imm(codeblock_t *block, void *p, uint16_t imm_data)
{
    host_x86_MOV16_ABS_IMM(block, p, imm_data);
}
void
codegen_direct_write_32_imm(codeblock_t *block, void *p, uint32_t imm_data)
{
    host_x86_MOV32_ABS_IMM(block, p, imm_data);
}
void
codegen_direct_write_32_imm_stack(codeblock_t *block, int stack_offset, uint32_t imm_data)
{
    host_x86_MOV32_BASE_OFFSET_IMM(block, REG_ESP, stack_offset, imm_data);
}

#endif

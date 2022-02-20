#if defined __amd64__ || defined _M_X64

#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "x86.h"
#include "x87.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_backend.h"
#include "codegen_backend_x86-64_defs.h"
#include "codegen_backend_x86-64_ops.h"
#include "codegen_backend_x86-64_ops_sse.h"
#include "codegen_ir_defs.h"

#define STACK_ARG0 (0)
#define STACK_ARG1 (4)
#define STACK_ARG2 (8)
#define STACK_ARG3 (12)

#define HOST_REG_GET(reg) ((IREG_GET_SIZE(reg) == IREG_SIZE_BH) ? (IREG_GET_REG((reg) & 3) | 4) : (IREG_GET_REG(reg) & 7))

#define REG_IS_L(size) (size == IREG_SIZE_L)
#define REG_IS_W(size) (size == IREG_SIZE_W)
#define REG_IS_B(size) (size == IREG_SIZE_B || size == IREG_SIZE_BH)
#define REG_IS_BH(size) (size == IREG_SIZE_BH)
#define REG_IS_D(size) (size == IREG_SIZE_D)
#define REG_IS_Q(size) (size == IREG_SIZE_Q)

static int codegen_ADD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_LEA_REG_REG(block, dest_reg, src_reg_a, src_reg_b);
                else
                        host_x86_ADD32_REG_REG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
                host_x86_ADD16_REG_REG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
                host_x86_ADD8_REG_REG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("ADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}

static int codegen_ADD_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (dest_reg != src_reg)
                        host_x86_LEA_REG_IMM(block, dest_reg, src_reg, uop->imm_data);
                else
                        host_x86_ADD32_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (dest_reg != src_reg)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_ADD16_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (dest_reg != src_reg)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_ADD8_REG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("ADD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}

static int codegen_ADD_LSHIFT(codeblock_t *block, uop_t *uop)
{
        if (!uop->imm_data)
        {
                if (uop->dest_reg_a_real == uop->src_reg_a_real)
                        host_x86_ADD32_REG_REG(block, uop->dest_reg_a_real, uop->src_reg_b_real);
                else
                        host_x86_LEA_REG_REG(block, uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
        }
        else if (uop->imm_data < 4)
                host_x86_LEA_REG_REG_SHIFT(block, uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real, uop->imm_data);
#ifdef RECOMPILER_DEBUG
        else
                fatal("codegen_ADD_LSHIFT - shift out of range %i\n", uop->imm_data);
#endif
        return 0;
}

static int codegen_AND(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PAND_XREG_XREG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
                host_x86_AND32_REG_REG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
                host_x86_AND16_REG_REG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
                host_x86_AND8_REG_REG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("AND %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}

static int codegen_AND_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (dest_reg != src_reg)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_AND32_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (dest_reg != src_reg)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_AND16_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (dest_reg != src_reg)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_AND8_REG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("AND_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}

static int codegen_ANDN(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), /*src_reg_a = HOST_REG_GET(uop->src_reg_a_real), */src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PANDN_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("ANDN %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}

static int codegen_CALL_FUNC(codeblock_t *block, uop_t *uop)
{
        host_x86_CALL(block, uop->p);

        return 0;
}

static int codegen_CALL_FUNC_RESULT(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);

#ifdef RECOMPILER_DEBUG
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
        if (!REG_IS_L(dest_size))
                fatal("CALL_FUNC_RESULT %02x\n", uop->dest_reg_a_real);
#endif
        host_x86_CALL(block, uop->p);
        host_x86_MOV32_REG_REG(block, dest_reg, REG_EAX);

        return 0;
}

static int codegen_CALL_INSTRUCTION_FUNC(codeblock_t *block, uop_t *uop)
{
        host_x86_CALL(block, uop->p);
        host_x86_TEST32_REG(block, REG_EAX, REG_EAX);
        host_x86_JNZ(block, codegen_exit_rout);

        return 0;
}

static int codegen_CMP_IMM_JZ(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(src_size))
        {
                host_x86_CMP32_REG_IMM(block, src_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_IMM_JZ %02x\n", uop->src_reg_a_real);
#endif
        host_x86_JZ(block, uop->p);

        return 0;
}

static int codegen_CMP_IMM_JNZ_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(src_size))
        {
                host_x86_CMP32_REG_IMM(block, src_reg, uop->imm_data);
        }
        else if (REG_IS_W(src_size))
        {
                host_x86_CMP16_REG_IMM(block, src_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_IMM_JNZ_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JNZ_long(block);

        return 0;
}
static int codegen_CMP_IMM_JZ_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(src_size))
        {
                host_x86_CMP32_REG_IMM(block, src_reg, uop->imm_data);
        }
        else if (REG_IS_W(src_size))
        {
                host_x86_CMP16_REG_IMM(block, src_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_IMM_JZ_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JZ_long(block);

        return 0;
}

static int codegen_CMP_JB(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
        uint32_t *jump_p;

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JB %02x\n", uop->src_reg_a_real);
#endif
        jump_p = host_x86_JB_long(block);
        *jump_p = (uintptr_t)uop->p - ((uintptr_t)jump_p + 4);

        return 0;
}
static int codegen_CMP_JNBE(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
        uint32_t *jump_p;

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JNBE %02x\n", uop->src_reg_a_real);
#endif
        jump_p = host_x86_JNBE_long(block);
        *jump_p = (uintptr_t)uop->p - ((uintptr_t)jump_p + 4);

        return 0;
}

static int codegen_CMP_JNB_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JNB_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JNB_long(block);

        return 0;
}
static int codegen_CMP_JNBE_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JNBE_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JNBE_long(block);

        return 0;
}
static int codegen_CMP_JNL_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JNL_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JNL_long(block);

        return 0;
}
static int codegen_CMP_JNLE_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JNLE_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JNLE_long(block);

        return 0;
}
static int codegen_CMP_JNO_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JNO_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JNO_long(block);

        return 0;
}
static int codegen_CMP_JNZ_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JNZ_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JNZ_long(block);

        return 0;
}
static int codegen_CMP_JB_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JB_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JB_long(block);

        return 0;
}
static int codegen_CMP_JBE_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JBE_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JBE_long(block);

        return 0;
}
static int codegen_CMP_JL_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JL_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JL_long(block);

        return 0;
}
static int codegen_CMP_JLE_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JLE_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JLE_long(block);

        return 0;
}
static int codegen_CMP_JO_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JO_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JO_long(block);

        return 0;
}
static int codegen_CMP_JZ_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("CMP_JZ_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JZ_long(block);

        return 0;
}

static int codegen_FABS(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && dest_reg == src_reg_a)
        {
                host_x86_PXOR_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
                host_x86_SUBSD_XREG_XREG(block, REG_XMM_TEMP, dest_reg);
                host_x86_MAXSD_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("codegen_FABS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_FCHS(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a))
        {
                host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
                host_x86_PXOR_XREG_XREG(block, dest_reg, dest_reg);
                host_x86_SUBSD_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("codegen_FCHS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_FSQRT(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a))
        {
                host_x86_SQRTSD_XREG_XREG(block, dest_reg, src_reg_a);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("codegen_FSQRT %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_FTST(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_W(dest_size) && REG_IS_D(src_size_a))
        {
                host_x86_PXOR_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
                if (dest_reg != REG_EAX)
                        host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
                host_x86_XOR32_REG_REG(block, REG_EAX, REG_EAX);
                host_x86_COMISD_XREG_XREG(block, src_reg_a, REG_XMM_TEMP);
                host_x86_LAHF(block);
                host_x86_AND16_REG_IMM(block, REG_EAX, C0|C2|C3);
                if (dest_reg != REG_EAX)
                {
                        host_x86_MOV16_REG_REG(block, dest_reg, REG_EAX);
                        host_x86_MOV32_REG_REG(block, REG_EAX, REG_ECX);
                }
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("codegen_FTST %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}

static int codegen_FADD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a)
        {
                host_x86_ADDSD_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("codegen_FADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_FCOM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_W(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b))
        {
                if (dest_reg != REG_EAX)
                        host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
                host_x86_XOR32_REG_REG(block, REG_EAX, REG_EAX);
                host_x86_COMISD_XREG_XREG(block, src_reg_a, src_reg_b);
                host_x86_LAHF(block);
                host_x86_AND16_REG_IMM(block, REG_EAX, C0|C2|C3);
                if (dest_reg != REG_EAX)
                {
                        host_x86_MOV16_REG_REG(block, dest_reg, REG_EAX);
                        host_x86_MOV32_REG_REG(block, REG_EAX, REG_ECX);
                }
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("codegen_FCOM %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_FDIV(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a)
        {
                host_x86_DIVSD_XREG_XREG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b))
        {
                host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
                host_x86_DIVSD_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
                host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("codegen_FDIV %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_FMUL(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a)
        {
                host_x86_MULSD_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("codegen_FMUL %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_FSUB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a)
        {
                host_x86_SUBSD_XREG_XREG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b))
        {
                host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
                host_x86_SUBSD_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
                host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("codegen_FSUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}

static int codegen_FP_ENTER(codeblock_t *block, uop_t *uop)
{
        uint32_t *branch_offset;

        host_x86_MOV32_REG_ABS(block, REG_ECX, &cr0);
        host_x86_TEST32_REG_IMM(block, REG_ECX, 0xc);
        branch_offset = host_x86_JZ_long(block);
        host_x86_MOV32_ABS_IMM(block, &cpu_state.oldpc, uop->imm_data);
#if _WIN64
        host_x86_MOV32_REG_IMM(block, REG_ECX, 7);
#else
        host_x86_MOV32_REG_IMM(block, REG_EDI, 7);
#endif
        host_x86_CALL(block, x86_int);
        host_x86_JMP(block, codegen_exit_rout);
        *branch_offset = (uint32_t)((uintptr_t)&block_write_data[block_pos] - (uintptr_t)branch_offset) - 4;

        return 0;
}
static int codegen_MMX_ENTER(codeblock_t *block, uop_t *uop)
{
        uint32_t *branch_offset;

        host_x86_MOV32_REG_ABS(block, REG_ECX, &cr0);
        host_x86_TEST32_REG_IMM(block, REG_ECX, 0xc);
        branch_offset = host_x86_JZ_long(block);
        host_x86_MOV32_ABS_IMM(block, &cpu_state.oldpc, uop->imm_data);
#if _WIN64
        host_x86_MOV32_REG_IMM(block, REG_ECX, 7);
#else
        host_x86_MOV32_REG_IMM(block, REG_EDI, 7);
#endif
        host_x86_CALL(block, x86_int);
        host_x86_JMP(block, codegen_exit_rout);
        *branch_offset = (uint32_t)((uintptr_t)&block_write_data[block_pos] - (uintptr_t)branch_offset) - 4;
        host_x86_MOV32_ABS_IMM(block, &cpu_state.tag[0], 0x01010101);
        host_x86_MOV32_ABS_IMM(block, &cpu_state.tag[4], 0x01010101);
        host_x86_MOV32_ABS_IMM(block, &cpu_state.TOP, 0);
        host_x86_MOV8_ABS_IMM(block, &cpu_state.ismmx, 1);

        return 0;
}

static int codegen_JMP(codeblock_t *block, uop_t *uop)
{
        host_x86_JMP(block, uop->p);

        return 0;
}

static int codegen_LOAD_FUNC_ARG0(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_W(src_size))
        {
#if _WIN64
                host_x86_MOVZX_REG_32_16(block, REG_ECX, src_reg);
#else
                host_x86_MOVZX_REG_32_16(block, REG_EDI, src_reg);
#endif
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("codegen_LOAD_FUNC_ARG0 %02x\n", uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_LOAD_FUNC_ARG1(codeblock_t *block, uop_t *uop)
{
#ifdef RECOMPILER_DEBUG
        fatal("codegen_LOAD_FUNC_ARG1 %02x\n", uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_LOAD_FUNC_ARG2(codeblock_t *block, uop_t *uop)
{
#ifdef RECOMPILER_DEBUG
        fatal("codegen_LOAD_FUNC_ARG2 %02x\n", uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_LOAD_FUNC_ARG3(codeblock_t *block, uop_t *uop)
{
#ifdef RECOMPILER_DEBUG
        fatal("codegen_LOAD_FUNC_ARG3 %02x\n", uop->src_reg_a_real);
#endif
        return 0;
}

static int codegen_LOAD_FUNC_ARG0_IMM(codeblock_t *block, uop_t *uop)
{
#if _WIN64
        host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data);
#else
        host_x86_MOV32_REG_IMM(block, REG_EDI, uop->imm_data);
#endif
        return 0;
}
static int codegen_LOAD_FUNC_ARG1_IMM(codeblock_t *block, uop_t *uop)
{
#if _WIN64
        host_x86_MOV32_REG_IMM(block, REG_EDX, uop->imm_data);
#else
        host_x86_MOV32_REG_IMM(block, REG_ESI, uop->imm_data);
#endif
        return 0;
}
static int codegen_LOAD_FUNC_ARG2_IMM(codeblock_t *block, uop_t *uop)
{
#ifdef RECOMPILER_DEBUG
        fatal("codegen_LOAD_FUNC_ARG2_IMM\n");
#endif
        return 0;
}
static int codegen_LOAD_FUNC_ARG3_IMM(codeblock_t *block, uop_t *uop)
{
#ifdef RECOMPILER_DEBUG
        fatal("codegen_LOAD_FUNC_ARG3_IMM\n");
#endif
        return 0;
}

static int codegen_LOAD_SEG(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);

#ifdef RECOMPILER_DEBUG
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);
        if (!REG_IS_W(src_size))
                fatal("LOAD_SEG %02x %p\n", uop->src_reg_a_real, uop->p);
#endif
#if _WIN64
        host_x86_MOV16_REG_REG(block, REG_CX, src_reg);
        host_x86_MOV64_REG_IMM(block, REG_EDX, (uint64_t)uop->p);
#else
        host_x86_MOV16_REG_REG(block, REG_DI, src_reg);
        host_x86_MOV64_REG_IMM(block, REG_ESI, (uint64_t)uop->p);
#endif
        host_x86_CALL(block, (void *)loadseg);
        host_x86_TEST32_REG(block, REG_EAX, REG_EAX);
        host_x86_JNZ(block, codegen_exit_rout);

        return 0;
}

static int codegen_MEM_LOAD_ABS(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), seg_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        host_x86_LEA_REG_IMM(block, REG_ESI, seg_reg, uop->imm_data);
        if (REG_IS_B(dest_size))
        {
                host_x86_CALL(block, codegen_mem_load_byte);
        }
        else if (REG_IS_W(dest_size))
        {
                host_x86_CALL(block, codegen_mem_load_word);
        }
        else if (REG_IS_L(dest_size))
        {
                host_x86_CALL(block, codegen_mem_load_long);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("MEM_LOAD_ABS - %02x\n", uop->dest_reg_a_real);
#endif
        host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
        host_x86_JNZ(block, codegen_exit_rout);
        if (REG_IS_B(dest_size))
        {
                host_x86_MOV8_REG_REG(block, dest_reg, REG_ECX);
        }
        else if (REG_IS_W(dest_size))
        {
                host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
        }
        else if (REG_IS_L(dest_size))
        {
                host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);
        }

        return 0;
}
static int codegen_MEM_LOAD_REG(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
        if (uop->imm_data)
                host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
        if (REG_IS_B(dest_size))
        {
                host_x86_CALL(block, codegen_mem_load_byte);
        }
        else if (REG_IS_W(dest_size))
        {
                host_x86_CALL(block, codegen_mem_load_word);
        }
        else if (REG_IS_L(dest_size))
        {
                host_x86_CALL(block, codegen_mem_load_long);
        }
        else if (REG_IS_Q(dest_size))
        {
                host_x86_CALL(block, codegen_mem_load_quad);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("MEM_LOAD_REG - %02x\n", uop->dest_reg_a_real);
#endif
        host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
        host_x86_JNZ(block, codegen_exit_rout);
        if (REG_IS_B(dest_size))
        {
                host_x86_MOV8_REG_REG(block, dest_reg, REG_ECX);
        }
        else if (REG_IS_W(dest_size))
        {
                host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
        }
        else if (REG_IS_L(dest_size))
        {
                host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);
        }
        else if (REG_IS_Q(dest_size))
        {
                host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
        }

        return 0;
}
static int codegen_MEM_LOAD_SINGLE(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);

#ifdef RECOMPILER_DEBUG
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
        if (!REG_IS_D(dest_size))
                fatal("MEM_LOAD_SINGLE - %02x\n", uop->dest_reg_a_real);
#endif
        host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
        if (uop->imm_data)
                host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
        host_x86_CALL(block, codegen_mem_load_single);
        host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
        host_x86_JNZ(block, codegen_exit_rout);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);

        return 0;
}
static int codegen_MEM_LOAD_DOUBLE(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);

#ifdef RECOMPILER_DEBUG
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
        if (!REG_IS_D(dest_size))
                fatal("MEM_LOAD_DOUBLE - %02x\n", uop->dest_reg_a_real);
#endif
        host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
        if (uop->imm_data)
                host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
        host_x86_CALL(block, codegen_mem_load_double);
        host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
        host_x86_JNZ(block, codegen_exit_rout);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);

        return 0;
}

static int codegen_MEM_STORE_ABS(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_b_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_b_real);

        host_x86_LEA_REG_IMM(block, REG_ESI, seg_reg, uop->imm_data);
        if (REG_IS_B(src_size))
        {
                host_x86_MOV8_REG_REG(block, REG_ECX, src_reg);
                host_x86_CALL(block, codegen_mem_store_byte);
        }
        else if (REG_IS_W(src_size))
        {
                host_x86_MOV16_REG_REG(block, REG_ECX, src_reg);
                host_x86_CALL(block, codegen_mem_store_word);
        }
        else if (REG_IS_L(src_size))
        {
                host_x86_MOV32_REG_REG(block, REG_ECX, src_reg);
                host_x86_CALL(block, codegen_mem_store_long);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("MEM_STORE_ABS - %02x\n", uop->src_reg_b_real);
#endif
        host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
        host_x86_JNZ(block, codegen_exit_rout);

        return 0;
}

static int codegen_MEM_STORE_IMM_8(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);

        host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
        host_x86_MOV8_REG_IMM(block, REG_ECX, uop->imm_data);
        host_x86_CALL(block, codegen_mem_store_byte);
        host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
        host_x86_JNZ(block, codegen_exit_rout);

        return 0;
}
static int codegen_MEM_STORE_IMM_16(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);

        host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
        host_x86_MOV16_REG_IMM(block, REG_ECX, uop->imm_data);
        host_x86_CALL(block, codegen_mem_store_word);
        host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
        host_x86_JNZ(block, codegen_exit_rout);

        return 0;
}
static int codegen_MEM_STORE_IMM_32(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);

        host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
        host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data);
        host_x86_CALL(block, codegen_mem_store_long);
        host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
        host_x86_JNZ(block, codegen_exit_rout);

        return 0;
}

static int codegen_MEM_STORE_REG(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real), src_reg = HOST_REG_GET(uop->src_reg_c_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_c_real);

        host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
        if (uop->imm_data)
                host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
        if (REG_IS_B(src_size))
        {
                host_x86_MOV8_REG_REG(block, REG_ECX, src_reg);
                host_x86_CALL(block, codegen_mem_store_byte);
        }
        else if (REG_IS_W(src_size))
        {
                host_x86_MOV16_REG_REG(block, REG_ECX, src_reg);
                host_x86_CALL(block, codegen_mem_store_word);
        }
        else if (REG_IS_L(src_size))
        {
                host_x86_MOV32_REG_REG(block, REG_ECX, src_reg);
                host_x86_CALL(block, codegen_mem_store_long);
        }
        else if (REG_IS_Q(src_size))
        {
                host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg);
                host_x86_CALL(block, codegen_mem_store_quad);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("MEM_STORE_REG - %02x\n", uop->src_reg_b_real);
#endif
        host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
        host_x86_JNZ(block, codegen_exit_rout);

        return 0;
}

static int codegen_MEM_STORE_SINGLE(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real), src_reg = HOST_REG_GET(uop->src_reg_c_real);

#ifdef RECOMPILER_DEBUG
        int src_size = IREG_GET_SIZE(uop->src_reg_c_real);
        if (!REG_IS_D(src_size))
                fatal("MEM_STORE_SINGLE - %02x\n", uop->src_reg_b_real);
#endif
        host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
        if (uop->imm_data)
                host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
        host_x86_CVTSD2SS_XREG_XREG(block, REG_XMM_TEMP, src_reg);
        host_x86_CALL(block, codegen_mem_store_single);
        host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
        host_x86_JNZ(block, codegen_exit_rout);

        return 0;
}
static int codegen_MEM_STORE_DOUBLE(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real), src_reg = HOST_REG_GET(uop->src_reg_c_real);

#ifdef RECOMPILER_DEBUG
        int src_size = IREG_GET_SIZE(uop->src_reg_c_real);
        if (!REG_IS_D(src_size))
                fatal("MEM_STORE_DOUBLE - %02x\n", uop->src_reg_b_real);
#endif
        host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
        if (uop->imm_data)
                host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg);
        host_x86_CALL(block, codegen_mem_store_double);
        host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
        host_x86_JNZ(block, codegen_exit_rout);

        return 0;
}

static int codegen_MOV(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        }
        else if (REG_IS_D(dest_size) && REG_IS_D(src_size))
        {
                host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg);
        }
        else if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
        {
                host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("MOV %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_MOV_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_L(dest_size))
        {
                host_x86_MOV32_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size))
        {
                host_x86_MOV16_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_B(dest_size))
        {
                host_x86_MOV8_REG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("MOV_IMM %02x\n", uop->dest_reg_a_real);
#endif
        return 0;
}
static int codegen_MOV_PTR(codeblock_t *block, uop_t *uop)
{
        host_x86_MOV64_REG_IMM(block, uop->dest_reg_a_real, (uint64_t)uop->p);
        return 0;
}
static int codegen_MOV_REG_PTR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_L(dest_size))
        {
                host_x86_MOV32_REG_ABS(block, dest_reg, uop->p);
        }
        else
                fatal("MOV_REG_PTR %02x\n", uop->dest_reg_a_real);

        return 0;
}
static int codegen_MOVZX_REG_PTR_8(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_L(dest_size))
        {
                host_x86_MOVZX_REG_ABS_32_8(block, dest_reg, uop->p);
        }
        else if (REG_IS_W(dest_size))
        {
                host_x86_MOVZX_REG_ABS_16_8(block, dest_reg, uop->p);
        }
        else if (REG_IS_B(dest_size))
        {
                host_x86_MOV8_REG_ABS(block, dest_reg, uop->p);
        }
        else
                fatal("MOVZX_REG_PTR_8 %02x\n", uop->dest_reg_a_real);

        return 0;
}
static int codegen_MOVZX_REG_PTR_16(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_L(dest_size))
        {
                host_x86_MOVZX_REG_ABS_32_16(block, dest_reg, uop->p);
        }
        else if (REG_IS_W(dest_size))
        {
                host_x86_MOV16_REG_ABS(block, dest_reg, uop->p);
        }
        else
                fatal("MOVZX_REG_PTR_16 %02x\n", uop->dest_reg_a_real);

        return 0;
}
static int codegen_MOVSX(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_W(src_size))
        {
                host_x86_MOVSX_REG_32_16(block, dest_reg, src_reg);
        }
        else if (REG_IS_L(dest_size) && REG_IS_B(src_size))
        {
                host_x86_MOVSX_REG_32_8(block, dest_reg, src_reg);
        }
        else if (REG_IS_W(dest_size) && REG_IS_B(src_size))
        {
                host_x86_MOVSX_REG_16_8(block, dest_reg, src_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("MOVSX %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_MOVZX(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_L(src_size))
        {
                host_x86_MOVD_XREG_REG(block, dest_reg, src_reg);
        }
        else if (REG_IS_L(dest_size) && REG_IS_Q(src_size))
        {
                host_x86_MOVD_REG_XREG(block, dest_reg, src_reg);
        }
        else if (REG_IS_L(dest_size) && REG_IS_W(src_size))
        {
                host_x86_MOVZX_REG_32_16(block, dest_reg, src_reg);
        }
        else if (REG_IS_L(dest_size) && REG_IS_B(src_size))
        {
                host_x86_MOVZX_REG_32_8(block, dest_reg, src_reg);
        }
        else if (REG_IS_W(dest_size) && REG_IS_B(src_size))
        {
                host_x86_MOVZX_REG_16_8(block, dest_reg, src_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("MOVZX %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}

static int codegen_MOV_DOUBLE_INT(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_D(dest_size) && REG_IS_L(src_size))
        {
                host_x86_CVTSI2SD_XREG_REG(block, dest_reg, src_reg);
        }
        else if (REG_IS_D(dest_size) && REG_IS_W(src_size))
        {
                host_x86_MOVSX_REG_32_16(block, REG_ECX, src_reg);
                host_x86_CVTSI2SD_XREG_REG(block, dest_reg, REG_ECX);
        }
        else if (REG_IS_D(dest_size) && REG_IS_Q(src_size))
        {
                host_x86_MOVQ_REG_XREG(block, REG_RCX, src_reg);
                host_x86_CVTSI2SD_XREG_REG64(block, dest_reg, REG_RCX);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("MOV_DOUBLE_INT %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_MOV_INT_DOUBLE(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_D(src_size))
        {
                host_x86_LDMXCSR(block, &cpu_state.new_fp_control);
                host_x86_CVTSD2SI_REG_XREG(block, dest_reg, src_reg);
                host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
        }
        else if (REG_IS_W(dest_size) && REG_IS_D(src_size))
        {
                host_x86_LDMXCSR(block, &cpu_state.new_fp_control);
                host_x86_CVTSD2SI_REG_XREG(block, REG_ECX, src_reg);
                host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
                host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("MOV_INT_DOUBLE %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_MOV_INT_DOUBLE_64(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), src_64_reg = HOST_REG_GET(uop->src_reg_b_real), tag_reg = HOST_REG_GET(uop->src_reg_c_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real), src_64_size = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_D(src_size) && REG_IS_Q(src_64_size))
        {
                uint32_t *branch_offset;

                /*If TAG_UINT64 is set then the source is MM[]. Otherwise it is a double in ST()*/
                host_x86_MOVQ_XREG_XREG(block, dest_reg, src_64_reg);
                host_x86_TEST8_REG(block, tag_reg, tag_reg);
                branch_offset = host_x86_JS_long(block);

                host_x86_LDMXCSR(block, &cpu_state.new_fp_control);
                host_x86_CVTSD2SI_REG64_XREG(block, REG_RCX, src_reg);
                host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
                host_x86_MOVQ_XREG_REG(block, dest_reg, REG_RCX);

                *branch_offset = (uint32_t)((uintptr_t)&block_write_data[block_pos] - (uintptr_t)branch_offset) - 4;
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("MOV_INT_DOUBLE_64 %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}

static int codegen_NOP(codeblock_t *block, uop_t *uop)
{
        return 0;
}

static int codegen_OR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_POR_XREG_XREG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
                host_x86_OR32_REG_REG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
                host_x86_OR16_REG_REG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
                host_x86_OR8_REG_REG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("OR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_OR_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                host_x86_OR32_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                host_x86_OR16_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                host_x86_OR8_REG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("OR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}

static int codegen_PACKSSWB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PACKSSWB_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PACKSSWB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PACKSSDW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PACKSSDW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PACKSSDW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PACKUSWB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PACKUSWB_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PACKUSWB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}

static int codegen_PADDB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PADDB_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PADDB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PADDW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PADDW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PADDW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PADDD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PADDD_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PADDD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PADDSB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PADDSB_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PADDSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PADDSW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PADDSW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PADDSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PADDUSB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PADDUSB_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PADDUSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PADDUSW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PADDUSW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PADDUSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}

static int codegen_PCMPEQB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PCMPEQB_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PCMPEQB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PCMPEQW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PCMPEQW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PCMPEQW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PCMPEQD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PCMPEQD_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PCMPEQD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PCMPGTB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PCMPGTB_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PCMPGTB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PCMPGTW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PCMPGTW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PCMPGTW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PCMPGTD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PCMPGTD_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PCMPGTD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}

static int codegen_PF2ID(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a))
        {
                host_x86_LDMXCSR(block, &cpu_state.trunc_fp_control);
                host_x86_CVTPS2DQ_XREG_XREG(block, dest_reg, src_reg_a);
                host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PF2ID %02x %02x\n", uop->dest_reg_a_real);
#endif
        return 0;
}
static int codegen_PFADD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_ADDPS_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PFADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PFCMPEQ(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_CMPPS_XREG_XREG(block, dest_reg, src_reg_b, CMPPS_EQ);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PFCMPEQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PFCMPGE(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_CMPPS_XREG_XREG(block, dest_reg, src_reg_b, CMPPS_NLT);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PFCMPGE %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PFCMPGT(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_CMPPS_XREG_XREG(block, dest_reg, src_reg_b, CMPPS_NLE);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PFCMPGT %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PFMAX(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_MAXPS_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PFMAX %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PFMIN(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_MINPS_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PFMIN %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PFMUL(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_MULPS_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PFMUL %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PFRCP(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a))
        {
                /*TODO: This could be improved (use RCPSS + iteration)*/
                host_x86_MOV32_REG_IMM(block, REG_ECX, 1);
                host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
                host_x86_CVTSI2SS_XREG_REG(block, dest_reg, REG_ECX);
                host_x86_DIVSS_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
                host_x86_UNPCKLPS_XREG_XREG(block, dest_reg, dest_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PFRCP %02x %02x\n", uop->dest_reg_a_real);
#endif
        return 0;
}
static int codegen_PFRSQRT(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a))
        {
                /*TODO: This could be improved (use RSQRTSS + iteration)*/
                host_x86_SQRTSS_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
                host_x86_MOV32_REG_IMM(block, REG_ECX, 1);
                host_x86_CVTSI2SS_XREG_REG(block, dest_reg, REG_ECX);
                host_x86_DIVSS_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
                host_x86_UNPCKLPS_XREG_XREG(block, dest_reg, dest_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PFRSQRT %02x %02x\n", uop->dest_reg_a_real);
#endif
        return 0;
}
static int codegen_PFSUB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_SUBPS_XREG_XREG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
                host_x86_SUBPS_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
                host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PFSUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PI2FD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a))
        {
                host_x86_CVTDQ2PS_XREG_XREG(block, dest_reg, src_reg_a);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PI2FD %02x %02x\n", uop->dest_reg_a_real);
#endif
        return 0;
}

static int codegen_PMADDWD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PMADDWD_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PMULHW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PMULHW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PMULHW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PMULHW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PMULLW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PMULLW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PMULLW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}

static int codegen_PSLLW_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSLLW_XREG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSLLW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_PSLLD_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSLLD_XREG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSLLD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_PSLLQ_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSLLQ_XREG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSLLQ_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_PSRAW_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSRAW_XREG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSRAW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_PSRAD_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSRAD_XREG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSRAD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_PSRAQ_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSRAQ_XREG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSRAQ_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_PSRLW_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSRLW_XREG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSRLW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_PSRLD_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSRLD_XREG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSRLD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_PSRLQ_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (REG_IS_Q(dest_size) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSRLQ_XREG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSRLQ_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}

static int codegen_PSUBB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSUBB_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSUBB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PSUBW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSUBW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSUBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PSUBD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSUBD_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSUBD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PSUBSB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSUBSB_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSUBSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PSUBSW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSUBSW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSUBSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PSUBUSB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSUBUSB_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSUBUSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PSUBUSW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PSUBUSW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PSUBUSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}

static int codegen_PUNPCKHBW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PUNPCKHBW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PUNPCKHBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PUNPCKHWD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PUNPCKHWD_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PUNPCKHWD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PUNPCKHDQ(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PUNPCKHDQ_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PUNPCKHDQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PUNPCKLBW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PUNPCKLBW_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PUNPCKLBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PUNPCKLWD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PUNPCKLWD_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PUNPCKLWD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_PUNPCKLDQ(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PUNPCKLDQ_XREG_XREG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("PUNPCKLDQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}

static int codegen_ROL(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), shift_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_ROL32_CL(block, dest_reg);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_ROL16_CL(block, dest_reg);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_ROL8_CL(block, dest_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("ROL %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_ROL_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_ROL32_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_ROL16_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_ROL8_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("ROL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_ROR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), shift_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_ROR32_CL(block, dest_reg);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_ROR16_CL(block, dest_reg);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_ROR8_CL(block, dest_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("ROR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_ROR_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_ROR32_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_ROR16_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_ROR8_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("ROR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}

static int codegen_SAR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), shift_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_SAR32_CL(block, dest_reg);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_SAR16_CL(block, dest_reg);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_SAR8_CL(block, dest_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("SAR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_SAR_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_SAR32_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_SAR16_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_SAR8_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("SAR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_SHL(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), shift_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_SHL32_CL(block, dest_reg);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_SHL16_CL(block, dest_reg);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_SHL8_CL(block, dest_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("SHL %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_SHL_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_SHL32_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_SHL16_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_SHL8_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("SHL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_SHR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), shift_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_SHR32_CL(block, dest_reg);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_SHR16_CL(block, dest_reg);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_SHR8_CL(block, dest_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("SHR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}
static int codegen_SHR_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_SHR32_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_SHR16_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (uop->dest_reg_a_real != uop->src_reg_a_real)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_SHR8_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("SHR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}

static int codegen_STORE_PTR_IMM(codeblock_t *block, uop_t *uop)
{
#ifdef RECOMPILER_DEBUG
        if (((uintptr_t)uop->p) >> 32)
                fatal("STORE_PTR_IMM 64-bit addr\n");
#endif
        host_x86_MOV32_ABS_IMM(block, uop->p, uop->imm_data);
        return 0;
}
static int codegen_STORE_PTR_IMM_8(codeblock_t *block, uop_t *uop)
{
#ifdef RECOMPILER_DEBUG
        if (((uintptr_t)uop->p) >> 32)
                fatal("STORE_PTR_IMM_8 64-bit addr\n");
#endif
        host_x86_MOV8_ABS_IMM(block, uop->p, uop->imm_data);
        return 0;
}

static int codegen_SUB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
                host_x86_SUB32_REG_REG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
                host_x86_SUB16_REG_REG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
                if (dest_reg != src_reg_a)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
                host_x86_SUB8_REG_REG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("SUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_SUB_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                if (dest_reg != src_reg)
                        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
                host_x86_SUB32_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                if (dest_reg != src_reg)
                        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
                host_x86_SUB16_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                if (dest_reg != src_reg)
                        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
                host_x86_SUB8_REG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("SUB_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}

static int codegen_TEST_JNS_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(src_size))
        {
                host_x86_TEST32_REG(block, src_reg, src_reg);
        }
        else if (REG_IS_W(src_size))
        {
                host_x86_TEST16_REG(block, src_reg, src_reg);
        }
        else if (REG_IS_B(src_size))
        {
                host_x86_TEST8_REG(block, src_reg, src_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("TEST_JNS_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JNS_long(block);

        return 0;
}
static int codegen_TEST_JS_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(src_size))
        {
                host_x86_TEST32_REG(block, src_reg, src_reg);
        }
        else if (REG_IS_W(src_size))
        {
                host_x86_TEST16_REG(block, src_reg, src_reg);
        }
        else if (REG_IS_B(src_size))
        {
                host_x86_TEST8_REG(block, src_reg, src_reg);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("TEST_JS_DEST %02x\n", uop->src_reg_a_real);
#endif
        uop->p = host_x86_JS_long(block);

        return 0;
}

static int codegen_XOR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_PXOR_XREG_XREG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_XOR32_REG_REG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_XOR16_REG_REG(block, dest_reg, src_reg_b);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real)
        {
                host_x86_XOR8_REG_REG(block, dest_reg, src_reg_b);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("XOR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#endif
        return 0;
}
static int codegen_XOR_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                host_x86_XOR32_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                host_x86_XOR16_REG_IMM(block, dest_reg, uop->imm_data);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                host_x86_XOR8_REG_IMM(block, dest_reg, uop->imm_data);
        }
#ifdef RECOMPILER_DEBUG
        else
                fatal("XOR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#endif
        return 0;
}

const uOpFn uop_handlers[UOP_MAX] =
{
        [UOP_CALL_FUNC & UOP_MASK] = codegen_CALL_FUNC,
        [UOP_CALL_FUNC_RESULT & UOP_MASK] = codegen_CALL_FUNC_RESULT,
        [UOP_CALL_INSTRUCTION_FUNC & UOP_MASK] = codegen_CALL_INSTRUCTION_FUNC,

        [UOP_JMP & UOP_MASK] = codegen_JMP,

        [UOP_LOAD_SEG & UOP_MASK] = codegen_LOAD_SEG,

        [UOP_LOAD_FUNC_ARG_0 & UOP_MASK] = codegen_LOAD_FUNC_ARG0,
        [UOP_LOAD_FUNC_ARG_1 & UOP_MASK] = codegen_LOAD_FUNC_ARG1,
        [UOP_LOAD_FUNC_ARG_2 & UOP_MASK] = codegen_LOAD_FUNC_ARG2,
        [UOP_LOAD_FUNC_ARG_3 & UOP_MASK] = codegen_LOAD_FUNC_ARG3,

        [UOP_LOAD_FUNC_ARG_0_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG0_IMM,
        [UOP_LOAD_FUNC_ARG_1_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG1_IMM,
        [UOP_LOAD_FUNC_ARG_2_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG2_IMM,
        [UOP_LOAD_FUNC_ARG_3_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG3_IMM,

        [UOP_STORE_P_IMM & UOP_MASK] = codegen_STORE_PTR_IMM,
        [UOP_STORE_P_IMM_8 & UOP_MASK] = codegen_STORE_PTR_IMM_8,

        [UOP_MEM_LOAD_ABS & UOP_MASK] = codegen_MEM_LOAD_ABS,
        [UOP_MEM_LOAD_REG & UOP_MASK] = codegen_MEM_LOAD_REG,
        [UOP_MEM_LOAD_SINGLE & UOP_MASK] = codegen_MEM_LOAD_SINGLE,
        [UOP_MEM_LOAD_DOUBLE & UOP_MASK] = codegen_MEM_LOAD_DOUBLE,

        [UOP_MEM_STORE_ABS & UOP_MASK] = codegen_MEM_STORE_ABS,
        [UOP_MEM_STORE_REG & UOP_MASK] = codegen_MEM_STORE_REG,
        [UOP_MEM_STORE_IMM_8 & UOP_MASK] = codegen_MEM_STORE_IMM_8,
        [UOP_MEM_STORE_IMM_16 & UOP_MASK] = codegen_MEM_STORE_IMM_16,
        [UOP_MEM_STORE_IMM_32 & UOP_MASK] = codegen_MEM_STORE_IMM_32,
        [UOP_MEM_STORE_SINGLE & UOP_MASK] = codegen_MEM_STORE_SINGLE,
        [UOP_MEM_STORE_DOUBLE & UOP_MASK] = codegen_MEM_STORE_DOUBLE,

        [UOP_MOV     & UOP_MASK] = codegen_MOV,
        [UOP_MOV_PTR & UOP_MASK] = codegen_MOV_PTR,
        [UOP_MOV_IMM & UOP_MASK] = codegen_MOV_IMM,
        [UOP_MOVSX   & UOP_MASK] = codegen_MOVSX,
        [UOP_MOVZX   & UOP_MASK] = codegen_MOVZX,
        [UOP_MOV_DOUBLE_INT & UOP_MASK] = codegen_MOV_DOUBLE_INT,
        [UOP_MOV_INT_DOUBLE    & UOP_MASK] = codegen_MOV_INT_DOUBLE,
        [UOP_MOV_INT_DOUBLE_64 & UOP_MASK] = codegen_MOV_INT_DOUBLE_64,
        [UOP_MOV_REG_PTR       & UOP_MASK] = codegen_MOV_REG_PTR,
        [UOP_MOVZX_REG_PTR_8   & UOP_MASK] = codegen_MOVZX_REG_PTR_8,
        [UOP_MOVZX_REG_PTR_16  & UOP_MASK] = codegen_MOVZX_REG_PTR_16,

        [UOP_ADD     & UOP_MASK] = codegen_ADD,
        [UOP_ADD_IMM & UOP_MASK] = codegen_ADD_IMM,
        [UOP_ADD_LSHIFT & UOP_MASK] = codegen_ADD_LSHIFT,
        [UOP_AND     & UOP_MASK] = codegen_AND,
        [UOP_ANDN    & UOP_MASK] = codegen_ANDN,
        [UOP_AND_IMM & UOP_MASK] = codegen_AND_IMM,
        [UOP_OR      & UOP_MASK] = codegen_OR,
        [UOP_OR_IMM  & UOP_MASK] = codegen_OR_IMM,
        [UOP_SUB     & UOP_MASK] = codegen_SUB,
        [UOP_SUB_IMM & UOP_MASK] = codegen_SUB_IMM,
        [UOP_XOR     & UOP_MASK] = codegen_XOR,
        [UOP_XOR_IMM & UOP_MASK] = codegen_XOR_IMM,

        [UOP_SAR     & UOP_MASK] = codegen_SAR,
        [UOP_SAR_IMM & UOP_MASK] = codegen_SAR_IMM,
        [UOP_SHL     & UOP_MASK] = codegen_SHL,
        [UOP_SHL_IMM & UOP_MASK] = codegen_SHL_IMM,
        [UOP_SHR     & UOP_MASK] = codegen_SHR,
        [UOP_SHR_IMM & UOP_MASK] = codegen_SHR_IMM,
        [UOP_ROL     & UOP_MASK] = codegen_ROL,
        [UOP_ROL_IMM & UOP_MASK] = codegen_ROL_IMM,
        [UOP_ROR     & UOP_MASK] = codegen_ROR,
        [UOP_ROR_IMM & UOP_MASK] = codegen_ROR_IMM,

        [UOP_CMP_IMM_JZ & UOP_MASK] = codegen_CMP_IMM_JZ,

        [UOP_CMP_JB        & UOP_MASK] = codegen_CMP_JB,
        [UOP_CMP_JNBE      & UOP_MASK] = codegen_CMP_JNBE,

        [UOP_CMP_JNB_DEST  & UOP_MASK] = codegen_CMP_JNB_DEST,
        [UOP_CMP_JNBE_DEST & UOP_MASK] = codegen_CMP_JNBE_DEST,
        [UOP_CMP_JNL_DEST  & UOP_MASK] = codegen_CMP_JNL_DEST,
        [UOP_CMP_JNLE_DEST & UOP_MASK] = codegen_CMP_JNLE_DEST,
        [UOP_CMP_JNO_DEST  & UOP_MASK] = codegen_CMP_JNO_DEST,
        [UOP_CMP_JNZ_DEST  & UOP_MASK] = codegen_CMP_JNZ_DEST,
        [UOP_CMP_JB_DEST   & UOP_MASK] = codegen_CMP_JB_DEST,
        [UOP_CMP_JBE_DEST  & UOP_MASK] = codegen_CMP_JBE_DEST,
        [UOP_CMP_JL_DEST   & UOP_MASK] = codegen_CMP_JL_DEST,
        [UOP_CMP_JLE_DEST  & UOP_MASK] = codegen_CMP_JLE_DEST,
        [UOP_CMP_JO_DEST   & UOP_MASK] = codegen_CMP_JO_DEST,
        [UOP_CMP_JZ_DEST   & UOP_MASK] = codegen_CMP_JZ_DEST,

        [UOP_CMP_IMM_JNZ_DEST & UOP_MASK] = codegen_CMP_IMM_JNZ_DEST,
        [UOP_CMP_IMM_JZ_DEST  & UOP_MASK] = codegen_CMP_IMM_JZ_DEST,

        [UOP_TEST_JNS_DEST & UOP_MASK] = codegen_TEST_JNS_DEST,
        [UOP_TEST_JS_DEST & UOP_MASK] = codegen_TEST_JS_DEST,

        [UOP_FP_ENTER & UOP_MASK] = codegen_FP_ENTER,
        [UOP_MMX_ENTER & UOP_MASK] = codegen_MMX_ENTER,

        [UOP_FADD & UOP_MASK] = codegen_FADD,
        [UOP_FCOM & UOP_MASK] = codegen_FCOM,
        [UOP_FDIV & UOP_MASK] = codegen_FDIV,
        [UOP_FMUL & UOP_MASK] = codegen_FMUL,
        [UOP_FSUB & UOP_MASK] = codegen_FSUB,

        [UOP_FABS & UOP_MASK] = codegen_FABS,
        [UOP_FCHS & UOP_MASK] = codegen_FCHS,
        [UOP_FSQRT & UOP_MASK] = codegen_FSQRT,
        [UOP_FTST & UOP_MASK] = codegen_FTST,

        [UOP_PACKSSWB & UOP_MASK] = codegen_PACKSSWB,
        [UOP_PACKSSDW & UOP_MASK] = codegen_PACKSSDW,
        [UOP_PACKUSWB & UOP_MASK] = codegen_PACKUSWB,

        [UOP_PADDB & UOP_MASK]   = codegen_PADDB,
        [UOP_PADDW & UOP_MASK]   = codegen_PADDW,
        [UOP_PADDD & UOP_MASK]   = codegen_PADDD,
        [UOP_PADDSB & UOP_MASK]  = codegen_PADDSB,
        [UOP_PADDSW & UOP_MASK]  = codegen_PADDSW,
        [UOP_PADDUSB & UOP_MASK] = codegen_PADDUSB,
        [UOP_PADDUSW & UOP_MASK] = codegen_PADDUSW,

        [UOP_PCMPEQB & UOP_MASK] = codegen_PCMPEQB,
        [UOP_PCMPEQW & UOP_MASK] = codegen_PCMPEQW,
        [UOP_PCMPEQD & UOP_MASK] = codegen_PCMPEQD,
        [UOP_PCMPGTB & UOP_MASK] = codegen_PCMPGTB,
        [UOP_PCMPGTW & UOP_MASK] = codegen_PCMPGTW,
        [UOP_PCMPGTD & UOP_MASK] = codegen_PCMPGTD,

        [UOP_PF2ID & UOP_MASK]   = codegen_PF2ID,
        [UOP_PFADD & UOP_MASK]   = codegen_PFADD,
        [UOP_PFCMPEQ & UOP_MASK] = codegen_PFCMPEQ,
        [UOP_PFCMPGE & UOP_MASK] = codegen_PFCMPGE,
        [UOP_PFCMPGT & UOP_MASK] = codegen_PFCMPGT,
        [UOP_PFMAX & UOP_MASK]   = codegen_PFMAX,
        [UOP_PFMIN & UOP_MASK]   = codegen_PFMIN,
        [UOP_PFMUL & UOP_MASK]   = codegen_PFMUL,
        [UOP_PFRCP & UOP_MASK]   = codegen_PFRCP,
        [UOP_PFRSQRT & UOP_MASK] = codegen_PFRSQRT,
        [UOP_PFSUB & UOP_MASK]   = codegen_PFSUB,
        [UOP_PI2FD & UOP_MASK]   = codegen_PI2FD,

        [UOP_PMADDWD & UOP_MASK] = codegen_PMADDWD,
        [UOP_PMULHW & UOP_MASK]  = codegen_PMULHW,
        [UOP_PMULLW & UOP_MASK]  = codegen_PMULLW,

        [UOP_PSLLW_IMM & UOP_MASK] = codegen_PSLLW_IMM,
        [UOP_PSLLD_IMM & UOP_MASK] = codegen_PSLLD_IMM,
        [UOP_PSLLQ_IMM & UOP_MASK] = codegen_PSLLQ_IMM,
        [UOP_PSRAW_IMM & UOP_MASK] = codegen_PSRAW_IMM,
        [UOP_PSRAD_IMM & UOP_MASK] = codegen_PSRAD_IMM,
        [UOP_PSRAQ_IMM & UOP_MASK] = codegen_PSRAQ_IMM,
        [UOP_PSRLW_IMM & UOP_MASK] = codegen_PSRLW_IMM,
        [UOP_PSRLD_IMM & UOP_MASK] = codegen_PSRLD_IMM,
        [UOP_PSRLQ_IMM & UOP_MASK] = codegen_PSRLQ_IMM,

        [UOP_PSUBB & UOP_MASK]   = codegen_PSUBB,
        [UOP_PSUBW & UOP_MASK]   = codegen_PSUBW,
        [UOP_PSUBD & UOP_MASK]   = codegen_PSUBD,
        [UOP_PSUBSB & UOP_MASK]  = codegen_PSUBSB,
        [UOP_PSUBSW & UOP_MASK]  = codegen_PSUBSW,
        [UOP_PSUBUSB & UOP_MASK] = codegen_PSUBUSB,
        [UOP_PSUBUSW & UOP_MASK] = codegen_PSUBUSW,

        [UOP_PUNPCKHBW & UOP_MASK] = codegen_PUNPCKHBW,
        [UOP_PUNPCKHWD & UOP_MASK] = codegen_PUNPCKHWD,
        [UOP_PUNPCKHDQ & UOP_MASK] = codegen_PUNPCKHDQ,
        [UOP_PUNPCKLBW & UOP_MASK] = codegen_PUNPCKLBW,
        [UOP_PUNPCKLWD & UOP_MASK] = codegen_PUNPCKLWD,
        [UOP_PUNPCKLDQ & UOP_MASK] = codegen_PUNPCKLDQ,

        [UOP_NOP_BARRIER & UOP_MASK] = codegen_NOP
};

void codegen_direct_read_8(codeblock_t *block, int host_reg, void *p)
{
        host_x86_MOV8_REG_ABS(block, host_reg, p);
}
void codegen_direct_read_16(codeblock_t *block, int host_reg, void *p)
{
        host_x86_MOV16_REG_ABS(block, host_reg, p);
}
void codegen_direct_read_32(codeblock_t *block, int host_reg, void *p)
{
        host_x86_MOV32_REG_ABS(block, host_reg, p);
}
void codegen_direct_read_64(codeblock_t *block, int host_reg, void *p)
{
        host_x86_MOVQ_XREG_ABS(block, host_reg, p);
}
void codegen_direct_read_pointer(codeblock_t *block, int host_reg, void *p)
{
        host_x86_MOV64_REG_ABS(block, host_reg, p);
}
void codegen_direct_read_double(codeblock_t *block, int host_reg, void *p)
{
        host_x86_MOVQ_XREG_ABS(block, host_reg, p);
}
void codegen_direct_read_st_8(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
        int offset = (uintptr_t)base - (((uintptr_t)&cpu_state) + 128);

        host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
        host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
        host_x86_AND32_REG_IMM(block, REG_ECX, 7);
        host_x86_MOV8_REG_ABS_REG_REG_SHIFT(block, host_reg, offset, REG_RBP, REG_ECX, 0);
}
void codegen_direct_read_st_64(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
        int offset = (uintptr_t)base - (((uintptr_t)&cpu_state) + 128);

        host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
        host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
        host_x86_AND32_REG_IMM(block, REG_ECX, 7);
        host_x86_MOVQ_XREG_ABS_REG_REG_SHIFT(block, host_reg, offset, REG_RBP, REG_ECX, 3);
}
void codegen_direct_read_st_double(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
        int offset = (uintptr_t)base - (((uintptr_t)&cpu_state) + 128);

        host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
        host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
        host_x86_AND32_REG_IMM(block, REG_ECX, 7);
        host_x86_MOVQ_XREG_ABS_REG_REG_SHIFT(block, host_reg, offset, REG_RBP, REG_ECX, 3);
}

void codegen_direct_write_8(codeblock_t *block, void *p, int host_reg)
{
        host_x86_MOV8_ABS_REG(block, p, host_reg);
}
void codegen_direct_write_16(codeblock_t *block, void *p, int host_reg)
{
        host_x86_MOV16_ABS_REG(block, p, host_reg);
}
void codegen_direct_write_32(codeblock_t *block, void *p, int host_reg)
{
        host_x86_MOV32_ABS_REG(block, p, host_reg);
}
void codegen_direct_write_64(codeblock_t *block, void *p, int host_reg)
{
        host_x86_MOVQ_ABS_XREG(block, p, host_reg);
}
void codegen_direct_write_pointer(codeblock_t *block, void *p, int host_reg)
{
        host_x86_MOV64_ABS_REG(block, p, host_reg);
}
void codegen_direct_write_double(codeblock_t *block, void *p, int host_reg)
{
        host_x86_MOVQ_ABS_XREG(block, p, host_reg);
}
void codegen_direct_write_st_8(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
        int offset = (uintptr_t)base - (((uintptr_t)&cpu_state) + 128);

        host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
        host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
        host_x86_AND32_REG_IMM(block, REG_ECX, 7);
        host_x86_MOV8_ABS_REG_REG_SHIFT_REG(block, offset, REG_RBP, REG_ECX, 0, host_reg);
}
void codegen_direct_write_st_64(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
        int offset = (uintptr_t)base - (((uintptr_t)&cpu_state) + 128);

        host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
        host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
        host_x86_AND32_REG_IMM(block, REG_ECX, 7);
        host_x86_MOVQ_ABS_REG_REG_SHIFT_XREG(block, offset, REG_RBP, REG_ECX, 3, host_reg);
}
void codegen_direct_write_st_double(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
        int offset = (uintptr_t)base - (((uintptr_t)&cpu_state) + 128);

        host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
        host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
        host_x86_AND32_REG_IMM(block, REG_ECX, 7);
        host_x86_MOVQ_ABS_REG_REG_SHIFT_XREG(block, offset, REG_RBP, REG_ECX, 3, host_reg);
}

void codegen_direct_write_ptr(codeblock_t *block, void *p, int host_reg)
{
        host_x86_MOV64_ABS_REG(block, p, host_reg);
}

void codegen_direct_read_16_stack(codeblock_t *block, int host_reg, int stack_offset)
{
        host_x86_MOV16_REG_BASE_OFFSET(block, host_reg, REG_ESP, stack_offset);
}
void codegen_direct_read_32_stack(codeblock_t *block, int host_reg, int stack_offset)
{
        host_x86_MOV32_REG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}
void codegen_direct_read_64_stack(codeblock_t *block, int host_reg, int stack_offset)
{
        host_x86_MOVQ_XREG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}
void codegen_direct_read_pointer_stack(codeblock_t *block, int host_reg, int stack_offset)
{
        host_x86_MOV64_REG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}
void codegen_direct_read_double_stack(codeblock_t *block, int host_reg, int stack_offset)
{
        host_x86_MOVQ_XREG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}

void codegen_direct_write_32_stack(codeblock_t *block, int stack_offset, int host_reg)
{
        host_x86_MOV32_BASE_OFFSET_REG(block, REG_RSP, stack_offset, host_reg);
}
void codegen_direct_write_64_stack(codeblock_t *block, int stack_offset, int host_reg)
{
        host_x86_MOVQ_BASE_OFFSET_XREG(block, REG_RSP, stack_offset, host_reg);
}
void codegen_direct_write_pointer_stack(codeblock_t *block, int stack_offset, int host_reg)
{
        host_x86_MOV64_BASE_OFFSET_REG(block, REG_RSP, stack_offset, host_reg);
}
void codegen_direct_write_double_stack(codeblock_t *block, int stack_offset, int host_reg)
{
        host_x86_MOVQ_BASE_OFFSET_XREG(block, REG_RSP, stack_offset, host_reg);
}

void codegen_set_jump_dest(codeblock_t *block, void *p)
{
        *(uint32_t *)p = (uintptr_t)&block_write_data[block_pos] - ((uintptr_t)p + 4);
}

void codegen_direct_write_8_imm(codeblock_t *block, void *p, uint8_t imm_data)
{
        host_x86_MOV8_ABS_IMM(block, p, imm_data);
}
void codegen_direct_write_16_imm(codeblock_t *block, void *p, uint16_t imm_data)
{
        host_x86_MOV16_ABS_IMM(block, p, imm_data);
}
void codegen_direct_write_32_imm(codeblock_t *block, void *p, uint32_t imm_data)
{
        host_x86_MOV32_ABS_IMM(block, p, imm_data);
}
void codegen_direct_write_32_imm_stack(codeblock_t *block, int stack_offset, uint32_t imm_data)
{
        host_x86_MOV32_BASE_OFFSET_IMM(block, REG_ESP, stack_offset, imm_data);
}

#endif

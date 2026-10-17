#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_flags.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_3dnow.h"
#include "codegen_ops_helpers.h"


#define ropParith(func, opid)                                                                      \
    uint32_t rop##func(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),                  \
                       uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                          \
    {                                                                                              \
        int dest_reg = (fetchdat >> 3) & 7;                                                        \
                                                                                                   \
        uop_MMX_ENTER(ir);                                                                         \
        codegen_mark_code_present(block, cs + op_pc, 1);                                           \
        if ((fetchdat & 0xc0) == 0xc0) {                                                           \
            int src_reg = fetchdat & 7;                                                            \
            uop_##func(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_MM(src_reg));                \
        } else {                                                                                   \
            x86seg *target_seg;                                                                    \
                                                                                                   \
            uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
            target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
            codegen_check_seg_read(block, ir, target_seg);                                         \
            CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);                               \
            uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);            \
            uop_##func(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_temp0_Q);                    \
        }                                                                                          \
                                                                                                   \
        codegen_mark_code_present(block, cs + op_pc + 1, 1);                                       \
        return op_pc + 2;                                                                          \
    }


/* AMD 3DNow! Technology Manual (21928), Table 1: an exponent of 00h is
   zero, and results "below this minimum representable value in magnitude
   are held to zero". */
static const uint64_t pf_zero_exp_max = 0x007fffff007fffffULL; /* the largest value with a zero exponent */
static const uint64_t pf_min_normal   = 0x0080000000800000ULL; /* the smallest value with a nonzero exponent */
static const uint64_t pf_max_normal   = 0x7f7fffff7f7fffffULL;

/* PFMAX and PFMIN after the destination-unless-the-source-wins choice:
   a result with a zero exponent is +0 (Tables 11 and 12: zero against a
   negative, or a positive for PFMIN, and two zeros all give +0; an
   exponent-zero operand is zero, so it can only win against operands
   that are zero or on its losing side, which leaves +0 again). */
static void
pf_zero_to_positive(ir_data_t *ir, int reg)
{
    uop_MOV(ir, IREG_temp0_Q, reg);
    uop_PSLLD_IMM(ir, IREG_temp0_Q, IREG_temp0_Q, 1);
    uop_PSRLD_IMM(ir, IREG_temp0_Q, IREG_temp0_Q, 1);
    uop_MOV_REG_PTR(ir, IREG_temp1_Q, (void *) &pf_zero_exp_max);
    uop_PCMPGTD(ir, IREG_temp0_Q, IREG_temp0_Q, IREG_temp1_Q);
    uop_AND(ir, reg, reg, IREG_temp0_Q);
}

/* PFRCP and PFRSQRT: the estimate for the low lane of src, in both lanes,
   made on its magnitude with its sign put back ("Negative operands are
   treated as positive operands ... with the sign of the result the same
   as the sign of the source operand"); a zero exponent gives the maximum
   normal (Tables 14 and 18), and a reciprocal below the smallest normal
   is held to zero (Table 14). src is read before dest is written. */
static void
pf_estimate(ir_data_t *ir, int dest, int src, int rsqrt)
{
    if (src != IREG_temp1_Q)
        uop_MOV(ir, IREG_temp1_Q, src);
    uop_PUNPCKLDQ(ir, IREG_temp1_Q, IREG_temp1_Q, IREG_temp1_Q);
    uop_MOV(ir, IREG_temp0_Q, IREG_temp1_Q);
    uop_PSLLD_IMM(ir, IREG_temp0_Q, IREG_temp0_Q, 1);
    uop_PSRLD_IMM(ir, IREG_temp0_Q, IREG_temp0_Q, 1);
    /* Lanes with a zero exponent */
    uop_MOV_REG_PTR(ir, IREG_temp2_Q, (void *) &pf_min_normal);
    uop_PCMPGTD(ir, IREG_temp2_Q, IREG_temp2_Q, IREG_temp0_Q);

    if (rsqrt)
        uop_PFRSQRT(ir, dest, IREG_temp0_Q);
    else {
        uop_PFRCP(ir, dest, IREG_temp0_Q);
        /* A reciprocal with a zero exponent is held to zero */
        uop_MOV_REG_PTR(ir, IREG_temp0_Q, (void *) &pf_min_normal);
        uop_PCMPGTD(ir, IREG_temp0_Q, IREG_temp0_Q, dest);
        uop_AND(ir, IREG_temp0_Q, IREG_temp0_Q, dest);
        uop_XOR(ir, dest, dest, IREG_temp0_Q);
    }

    /* The maximum normal where the source's exponent was zero */
    uop_MOV_REG_PTR(ir, IREG_temp0_Q, (void *) &pf_max_normal);
    uop_XOR(ir, IREG_temp0_Q, IREG_temp0_Q, dest);
    uop_AND(ir, IREG_temp0_Q, IREG_temp0_Q, IREG_temp2_Q);
    uop_XOR(ir, dest, dest, IREG_temp0_Q);

    /* The source's sign */
    uop_PSRLD_IMM(ir, IREG_temp1_Q, IREG_temp1_Q, 31);
    uop_PSLLD_IMM(ir, IREG_temp1_Q, IREG_temp1_Q, 31);
    uop_OR(ir, dest, dest, IREG_temp1_Q);
}

/* PFMAX and PFMIN: the backend's choice, then the zero rule */
#define ropPFminmax(func)                                                                          \
    uint32_t rop##func(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),                  \
                       uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                          \
    {                                                                                              \
        int dest_reg = (fetchdat >> 3) & 7;                                                        \
                                                                                                   \
        uop_MMX_ENTER(ir);                                                                         \
        codegen_mark_code_present(block, cs + op_pc, 1);                                           \
        if ((fetchdat & 0xc0) == 0xc0) {                                                           \
            int src_reg = fetchdat & 7;                                                            \
            uop_##func(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_MM(src_reg));                \
        } else {                                                                                   \
            x86seg *target_seg;                                                                    \
                                                                                                   \
            uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
            target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
            codegen_check_seg_read(block, ir, target_seg);                                         \
            CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);                               \
            uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);            \
            uop_##func(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_temp0_Q);                    \
        }                                                                                          \
        pf_zero_to_positive(ir, IREG_MM(dest_reg));                                                \
                                                                                                   \
        codegen_mark_code_present(block, cs + op_pc + 1, 1);                                       \
        return op_pc + 2;                                                                          \
    }

// clang-format off
ropParith(PFADD, 0x9e)
ropParith(PFCMPEQ, 0xb0)
ropParith(PFCMPGE, 0x90)
ropParith(PFCMPGT, 0xa0)
ropPFminmax(PFMAX)
ropPFminmax(PFMIN)
ropParith(PFMUL, 0xb4)
ropParith(PFSUB, 0x9a)
ropParith(PMULHRW, 0xb7)
ropParith(PAVGUSB, 0xbf)
    // clang-format on

uint32_t ropPF2ID(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PF2ID(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        /* PF2ID depends only on source, so memory operand can load directly into dst. */
        uop_MEM_LOAD_REG(ir, IREG_MM(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PF2ID(ir, IREG_MM(dest_reg), IREG_MM(dest_reg));
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPF2IW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PF2IW(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PF2IW(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFACC(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PFACC(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFACC(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFNACC(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    /* Snapshot dst before writeback so backend lowering stays alias-safe for ModRM reg overlap. */
    uop_MOV(ir, IREG_temp1_Q, IREG_MM(dest_reg));
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg      = fetchdat & 7;
        int src_reg_ireg = (src_reg == dest_reg) ? IREG_temp1_Q : IREG_MM(src_reg);
        uop_PFNACC(ir, IREG_MM(dest_reg), IREG_temp1_Q, src_reg_ireg);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFNACC(ir, IREG_MM(dest_reg), IREG_temp1_Q, IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFPNACC(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    /* Snapshot dst before writeback so backend lowering stays alias-safe for ModRM reg overlap. */
    uop_MOV(ir, IREG_temp1_Q, IREG_MM(dest_reg));
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg      = fetchdat & 7;
        int src_reg_ireg = (src_reg == dest_reg) ? IREG_temp1_Q : IREG_MM(src_reg);
        uop_PFPNACC(ir, IREG_MM(dest_reg), IREG_temp1_Q, src_reg_ireg);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFPNACC(ir, IREG_MM(dest_reg), IREG_temp1_Q, IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFSUBR(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PFSUB(ir, IREG_MM(dest_reg), IREG_MM(src_reg), IREG_MM(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFSUB(ir, IREG_MM(dest_reg), IREG_temp0_Q, IREG_MM(dest_reg));
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPI2FD(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PI2FD(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        /* PI2FD depends only on source, so memory operand can load directly into dst. */
        uop_MEM_LOAD_REG(ir, IREG_MM(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PI2FD(ir, IREG_MM(dest_reg), IREG_MM(dest_reg));
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPI2FW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PI2FW(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        /* PI2FW depends only on source, so memory operand can load directly into dst. */
        uop_MEM_LOAD_REG(ir, IREG_MM(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PI2FW(ir, IREG_MM(dest_reg), IREG_MM(dest_reg));
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPSWAPD(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PSWAPD(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        /* PSWAPD consumes source only; direct dst load trims one temporary. */
        uop_MEM_LOAD_REG(ir, IREG_MM(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PSWAPD(ir, IREG_MM(dest_reg), IREG_MM(dest_reg));
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFRCPIT(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        if (src_reg != dest_reg)
            uop_MOV(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_LOAD_REG(ir, IREG_MM(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
ropPFRCP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        pf_estimate(ir, IREG_MM(dest_reg), IREG_MM(src_reg), 0);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        /* The operand is mem64 (21928; AMD64 APM Vol. 5); the low lane is used */
        uop_MEM_LOAD_REG(ir, IREG_temp1_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        pf_estimate(ir, IREG_MM(dest_reg), IREG_temp1_Q, 0);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
ropPFRSQRT(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        pf_estimate(ir, IREG_MM(dest_reg), IREG_MM(src_reg), 1);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        /* The operand is mem64 (21928; AMD64 APM Vol. 5); the low lane is used */
        uop_MEM_LOAD_REG(ir, IREG_temp1_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        pf_estimate(ir, IREG_MM(dest_reg), IREG_temp1_Q, 1);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFRSQIT1(codeblock_t *block, ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    /*A memory operand goes to the interpreter, which reads it (and faults)*/
    if ((fetchdat & 0xc0) != 0xc0)
        return 0;

    uop_MMX_ENTER(ir);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return op_pc + 2;
}

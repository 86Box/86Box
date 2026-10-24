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
#include "x87_sf.h"
#include "x87.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_fpu_arith.h"
#include "codegen_ops_helpers.h"

/* Precision control 24-bit: the chip rounds every ADD/SUB/MUL/DIV/SQRT
   result to single. This resolves at compile time: the block key
   (CPU_STATUS_FPU_PC24) or codegen_fpu_rc_nearest's check holds npxc's
   precision to what it is now. */
#define uop_FROUND_PC(ir, reg)                \
    do {                                      \
        if (!(cpu_state.npxc & 0x300))        \
            uop_FROUND_S(ir, reg, reg);       \
    } while (0)

/* The compare of FCOM, FCOMP and FCOMPP, x87_compare() in the interpreter.
   A double from an m32 operand can't be CHKCOP's second image (its low 29
   mantissa bits are zero), so the m32 forms keep FCOM. */
#ifdef CODEGEN_BACKEND_HAS_FCOM_CHKCOP
#    define uop_FCOM_X87(ir, dst_reg, src_reg_a, src_reg_b) uop_FCOM_CHKCOP(ir, dst_reg, src_reg_a, src_reg_b, 0)
#else
#    define uop_FCOM_X87(ir, dst_reg, src_reg_a, src_reg_b) uop_FCOM(ir, dst_reg, src_reg_a, src_reg_b)
#endif

extern int x87_dyn_fdiv(uint32_t imm);
extern int x87_dyn_ftst_nan(uint32_t imm);

/* The FDIV family of the legacy FPU: the quotient comes from the
   interpreter's divide (x87_dyn_fdiv, 386_dynarec.c), so a compiled
   division raises the x87 exceptions and rounds exactly as an interpreted
   one. An unmasked exception that suppresses the result ends the block past
   the instruction, with the destination and the stack left alone and the
   FPU pointers recorded (codegen_fpu_record_pointers, ahead of it); an
   unmasked precision exception alone stores the rounded result and the
   block goes on with ES set. A memory operand goes in the permanent
   IREG_x87_operand. */
#define FDIV_REGS(d, n, q) ((d) | ((n) << 3) | ((q) << 6))
#define FDIV_MEM_NUM       0x200
#define FDIV_MEM_DEN       0x400
#define FDIV_KIND(k)       ((k) << 11)
#define FDIV_PC24          0x2000

static void
fdiv_call(ir_data_t *ir, uint32_t next_pc, uint32_t imm)
{
    codegen_accumulate_flush(ir);
    uop_MOV_IMM(ir, IREG_pc, next_pc);
    /* The instruction's pointers whatever the divide does: the call can
       end the block */
    codegen_fpu_record_pointers(ir);
    uop_CALL_INSTRUCTION_FUNC(ir, x87_dyn_fdiv, imm);
    /* ES may be set now with the block going on: the next waiting
       instruction checks for it before it runs */
    codegen_fpu_es_check = 1;
}

/* The divide itself. On arm64 and x86-64 the common case runs in the block, as
   x87_divide_fast finishes it: both operands loaded, the precision 24 bits
   (the block's class) or 53 bits (checked here: 64 bits keeps the software
   divide's double rounding), the host quotient inside the window and not a
   power of two (UOP_FDIV_JSLOW_DEST), its PE and C1 formed from the exact
   remainder and PE masked (UOP_FDIV_FLAGS_JSLOW_DEST); the status word and
   the tag are set as x87_dyn_fdiv sets them, and each path records the
   FPU pointers itself. Anything else goes to the call, which joins the
   block after the fast path's writes have been written back (the jumps are
   order barriers). */
static void
fdiv_divide(ir_data_t *ir, uint32_t next_pc, uint32_t imm)
{
#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
    const int dest       = imm & 7;
    const int num        = (imm & FDIV_MEM_NUM) ? IREG_x87_operand : IREG_ST((imm >> 3) & 7);
    const int den        = (imm & FDIV_MEM_DEN) ? IREG_x87_operand : IREG_ST((imm >> 6) & 7);
    int       jump_pc    = -1;
    int       jump_num   = -1;
    int       jump_den   = -1;
    int       jump_slow;
    int       jump_flags;
    int       jump_end;

    codegen_accumulate_flush(ir);
    if (cpu_state.npxc & 0x300) {
        uop_MOVZX(ir, IREG_temp2, IREG_NPXC);
        uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 0x300);
        jump_pc = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, 0x200);
    }
    if (!(imm & FDIV_MEM_NUM)) {
        uop_MOVZX(ir, IREG_temp2, IREG_tag_B((imm >> 3) & 7));
        jump_num = uop_CMP_IMM_JZ_DEST(ir, IREG_temp2, TAG_EMPTY);
    }
    if (!(imm & FDIV_MEM_DEN)) {
        uop_MOVZX(ir, IREG_temp2, IREG_tag_B((imm >> 6) & 7));
        jump_den = uop_CMP_IMM_JZ_DEST(ir, IREG_temp2, TAG_EMPTY);
    }
    uop_FDIV(ir, IREG_temp0_D, num, den);
    jump_slow  = uop_FDIV_JSLOW_DEST(ir, num, den, IREG_temp0_D, imm);
    jump_flags = uop_FDIV_FLAGS_JSLOW_DEST(ir, IREG_temp1, IREG_temp0_D, num, den, imm | ((cpu_state.npxc & 0x300) ? 0 : FDIV_PC24));
    uop_FROUND_PC(ir, IREG_temp0_D);
    uop_MOV(ir, IREG_ST(dest), IREG_temp0_D);
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~FPU_SW_C1);
    uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp1_W);
    uop_MOV_IMM(ir, IREG_tag(dest), TAG_VALID);
    codegen_fpu_record_pointers(ir);
    jump_end = uop_JMP_DEST(ir);

    if (jump_pc != -1)
        uop_set_jump_dest(ir, jump_pc);
    if (jump_num != -1)
        uop_set_jump_dest(ir, jump_num);
    if (jump_den != -1)
        uop_set_jump_dest(ir, jump_den);
    uop_set_jump_dest(ir, jump_slow);
    uop_set_jump_dest(ir, jump_flags);
    fdiv_call(ir, next_pc, imm);
    uop_set_jump_dest(ir, jump_end);
#else
    fdiv_call(ir, next_pc, imm);
#endif
}

uint32_t
ropFADD(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(src_reg));
    uop_FADD(ir, IREG_ST(0), IREG_ST(0), IREG_ST(src_reg));
    uop_FROUND_PC(ir, IREG_ST(0));
    uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

    return op_pc;
}
uint32_t
ropFADDr(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(dest_reg));
    uop_FADD(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
    uop_FROUND_PC(ir, IREG_ST(dest_reg));
    uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);

    return op_pc;
}
uint32_t
ropFADDP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(dest_reg));
    uop_FADD(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
    uop_FROUND_PC(ir, IREG_ST(dest_reg));
    uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);
    fpu_POP(block, ir);

    return op_pc;
}

uint32_t
ropFCOM(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = fetchdat & 7;

    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(src_reg));
    uop_FCOM_X87(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(src_reg));
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3));
    uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);

    return op_pc;
}
uint32_t
ropFCOMP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = fetchdat & 7;

    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(src_reg));
    uop_FCOM_X87(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(src_reg));
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3));
    uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);
    fpu_POP(block, ir);

    return op_pc;
}
uint32_t
ropFCOMPP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    /* The interpreter's FCOMPP reports -0 less than +0 on a 287XL or later
       (its 80387 detection case); the fpu type is fixed for the machine. */
#ifndef CODEGEN_BACKEND_HAS_FCOM_SIGNED_ZERO
    if (fpu_type >= FPU_287XL)
        return 0;
#endif
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(1));
#if defined(CODEGEN_BACKEND_HAS_FCOM_CHKCOP)
    uop_FCOM_CHKCOP(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(1), fpu_type >= FPU_287XL);
#else
#ifdef CODEGEN_BACKEND_HAS_FCOM_SIGNED_ZERO
    if (fpu_type >= FPU_287XL)
        uop_FCOM_SIGNED_ZERO(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(1));
    else
#endif
        uop_FCOM(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(1));
#endif
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3));
    uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);
    fpu_POP2(block, ir);

    return op_pc;
}

uint32_t
ropFDIV(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int src_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    fdiv_divide(ir, op_pc, FDIV_REGS(0, 0, src_reg));

    return op_pc;
}
uint32_t
ropFDIVR(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int src_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    fdiv_divide(ir, op_pc, FDIV_REGS(0, src_reg, 0));

    return op_pc;
}
uint32_t
ropFDIVr(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    fdiv_divide(ir, op_pc, FDIV_REGS(dest_reg, dest_reg, 0));

    return op_pc;
}
uint32_t
ropFDIVRr(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    fdiv_divide(ir, op_pc, FDIV_REGS(dest_reg, 0, dest_reg));

    return op_pc;
}
uint32_t
ropFDIVP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    fdiv_divide(ir, op_pc, FDIV_REGS(dest_reg, dest_reg, 0));
    fpu_POP(block, ir);

    return op_pc;
}
uint32_t
ropFDIVRP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    fdiv_divide(ir, op_pc, FDIV_REGS(dest_reg, 0, dest_reg));
    fpu_POP(block, ir);

    return op_pc;
}

uint32_t
ropFMUL(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(src_reg));
    uop_FMUL(ir, IREG_ST(0), IREG_ST(0), IREG_ST(src_reg));
    uop_FROUND_PC(ir, IREG_ST(0));
    uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

    return op_pc;
}
uint32_t
ropFMULr(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(dest_reg));
    uop_FMUL(ir, IREG_ST(dest_reg), IREG_ST(0), IREG_ST(dest_reg));
    uop_FROUND_PC(ir, IREG_ST(dest_reg));
    uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);

    return op_pc;
}
uint32_t
ropFMULP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(dest_reg));
    uop_FMUL(ir, IREG_ST(dest_reg), IREG_ST(0), IREG_ST(dest_reg));
    uop_FROUND_PC(ir, IREG_ST(dest_reg));
    uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);
    fpu_POP(block, ir);

    return op_pc;
}

uint32_t
ropFSUB(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(src_reg));
    uop_FSUB(ir, IREG_ST(0), IREG_ST(0), IREG_ST(src_reg));
    uop_FROUND_PC(ir, IREG_ST(0));
    uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

    return op_pc;
}
uint32_t
ropFSUBR(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(src_reg));
    uop_FSUB(ir, IREG_ST(0), IREG_ST(src_reg), IREG_ST(0));
    uop_FROUND_PC(ir, IREG_ST(0));
    uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

    return op_pc;
}
uint32_t
ropFSUBr(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(dest_reg));
    uop_FSUB(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
    uop_FROUND_PC(ir, IREG_ST(dest_reg));
    uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);

    return op_pc;
}
uint32_t
ropFSUBRr(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(dest_reg));
    uop_FSUB(ir, IREG_ST(dest_reg), IREG_ST(0), IREG_ST(dest_reg));
    uop_FROUND_PC(ir, IREG_ST(dest_reg));
    uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);

    return op_pc;
}
uint32_t
ropFSUBP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(dest_reg));
    uop_FSUB(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
    uop_FROUND_PC(ir, IREG_ST(dest_reg));
    uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);
    fpu_POP(block, ir);

    return op_pc;
}
uint32_t
ropFSUBRP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(dest_reg));
    uop_FSUB(ir, IREG_ST(dest_reg), IREG_ST(0), IREG_ST(dest_reg));
    uop_FROUND_PC(ir, IREG_ST(dest_reg));
    uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);
    fpu_POP(block, ir);

    return op_pc;
}

uint32_t
ropFUCOM(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = fetchdat & 7;

    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(src_reg));
    uop_FCOM(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(src_reg));
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3));
    uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);

    return op_pc;
}
uint32_t
ropFUCOMP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = fetchdat & 7;

    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(src_reg));
    uop_FCOM(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(src_reg));
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3));
    uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);
    fpu_POP(block, ir);

    return op_pc;
}
uint32_t
ropFUCOMPP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0) | FPU_GUARD_ST(1));
    uop_FCOM(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(1));
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3));
    uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);
    fpu_POP2(block, ir);

    return op_pc;
}

#define ropF_arith_mem(name, load_uop, size)                                                   \
    uint32_t ropFADD##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),          \
                           uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                  \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                    \
        uop_FADD(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                    \
        uop_FROUND_PC(ir, IREG_ST(0));                                                         \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                               \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFCOM##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),          \
                           uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                  \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                    \
        if ((size) == 8)                                                                       \
            uop_FCOM_X87(ir, IREG_temp1_W, IREG_ST(0), IREG_temp0_D);                          \
        else                                                                                   \
            uop_FCOM(ir, IREG_temp1_W, IREG_ST(0), IREG_temp0_D);                              \
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3));           \
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp1_W);                                        \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFCOMP##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),         \
                            uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                 \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                    \
        if ((size) == 8)                                                                       \
            uop_FCOM_X87(ir, IREG_temp1_W, IREG_ST(0), IREG_temp0_D);                          \
        else                                                                                   \
            uop_FCOM(ir, IREG_temp1_W, IREG_ST(0), IREG_temp0_D);                              \
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3));           \
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp1_W);                                        \
        fpu_POP(block, ir);                                                                    \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFDIV##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),          \
                           uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                  \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                    \
        uop_MOV(ir, IREG_x87_operand, IREG_temp0_D);                                           \
        fdiv_divide(ir, op_pc + 1, FDIV_MEM_DEN | FDIV_KIND((#name[0] == 's') ? 1 : 2));       \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFDIVR##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),         \
                            uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                 \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                    \
        uop_MOV(ir, IREG_x87_operand, IREG_temp0_D);                                           \
        fdiv_divide(ir, op_pc + 1, FDIV_MEM_NUM | FDIV_KIND((#name[0] == 's') ? 1 : 2));       \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFMUL##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),          \
                           uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                  \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                    \
        uop_FMUL(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                    \
        uop_FROUND_PC(ir, IREG_ST(0));                                                         \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                               \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFSUB##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),          \
                           uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                  \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                    \
        uop_FSUB(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                    \
        uop_FROUND_PC(ir, IREG_ST(0));                                                         \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                               \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFSUBR##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),         \
                            uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                 \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                    \
        uop_FSUB(ir, IREG_ST(0), IREG_temp0_D, IREG_ST(0));                                    \
        uop_FROUND_PC(ir, IREG_ST(0));                                                         \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                               \
                                                                                               \
        return op_pc + 1;                                                                      \
    }

// clang-format off
ropF_arith_mem(s, uop_MEM_LOAD_SINGLE, 4)
ropF_arith_mem(d, uop_MEM_LOAD_DOUBLE, 8)
// clang-format on

#define ropFI_arith_mem(name, temp_reg, size)                                                  \
    uint32_t ropFIADD##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),         \
                            uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                 \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                        \
        uop_FADD(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                    \
        uop_FROUND_PC(ir, IREG_ST(0));                                                         \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                               \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFICOM##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),         \
                            uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                 \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                        \
        uop_FCOM(ir, IREG_temp1_W, IREG_ST(0), IREG_temp0_D);                                  \
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3));           \
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp1_W);                                        \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFICOMP##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),        \
                             uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                        \
        uop_FCOM(ir, IREG_temp1_W, IREG_ST(0), IREG_temp0_D);                                  \
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3));           \
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp1_W);                                        \
        fpu_POP(block, ir);                                                                    \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFIDIV##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),         \
                            uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                 \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                        \
        uop_MOV(ir, IREG_x87_operand, IREG_temp0_D);                                           \
        fdiv_call(ir, op_pc + 1, FDIV_MEM_DEN);                                                \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFIDIVR##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),        \
                             uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                        \
        uop_MOV(ir, IREG_x87_operand, IREG_temp0_D);                                           \
        fdiv_call(ir, op_pc + 1, FDIV_MEM_NUM);                                                \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFIMUL##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),         \
                            uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                 \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                        \
        uop_FMUL(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                    \
        uop_FROUND_PC(ir, IREG_ST(0));                                                         \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                               \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFISUB##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),         \
                            uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                 \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                        \
        uop_FSUB(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                    \
        uop_FROUND_PC(ir, IREG_ST(0));                                                         \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                               \
                                                                                               \
        return op_pc + 1;                                                                      \
    }                                                                                          \
    uint32_t ropFISUBR##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),        \
                             uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                \
    {                                                                                          \
        x86seg *target_seg;                                                                    \
                                                                                               \
        if (!codegen_fpu_rc_nearest(ir))                                                        \
            return 0;                                                                          \
        uop_FP_ENTER(ir);                                                                      \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
        op_pc--;                                                                               \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
        codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_ST(0));      \
        codegen_check_seg_read(block, ir, target_seg);                                         \
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size) - 1);                      \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                        \
        uop_FSUB(ir, IREG_ST(0), IREG_temp0_D, IREG_ST(0));                                    \
        uop_FROUND_PC(ir, IREG_ST(0));                                                         \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                               \
                                                                                               \
        return op_pc + 1;                                                                      \
    }

// clang-format off
ropFI_arith_mem(l, IREG_temp0, 4)
ropFI_arith_mem(w, IREG_temp0_W, 2)
// clang-format on

uint32_t
ropFABS(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0));
    uop_FABS(ir, IREG_ST(0), IREG_ST(0));
    uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

    return op_pc;
}

uint32_t
ropFCHS(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0));
    uop_FCHS(ir, IREG_ST(0), IREG_ST(0));
    uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

    return op_pc;
}
uint32_t
ropFSQRT(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0));
    uop_FSQRT(ir, IREG_ST(0), IREG_ST(0));
    uop_FROUND_PC(ir, IREG_ST(0));
    uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

    return op_pc;
}
uint32_t
ropFTST(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int jump_uop;

    uop_FP_ENTER(ir);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc - 1, NULL, FPU_GUARD_ST(0));
    uop_FTST(ir, IREG_temp0_W, IREG_ST(0));
    /* Unordered, a NaN: the invalid operation the interpreter raises with it
       (x87_dyn_ftst_nan) */
    uop_MOVZX(ir, IREG_temp1, IREG_temp0_W);
    jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp1, FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3);
    uop_CALL_INSTRUCTION_FUNC(ir, x87_dyn_ftst_nan, 0);
    uop_set_jump_dest(ir, jump_uop);
    /* ES may be set now: the block's next waiting FPU instruction checks for
       it, as at an entry */
    codegen_fpu_entered = 0;
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3));
    uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);

    return op_pc;
}

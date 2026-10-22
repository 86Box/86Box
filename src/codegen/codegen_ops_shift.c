#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "x86_flags.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_backend.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_jit_wrappers.h"
#include "codegen_ops_shift.h"

/*
 * Emit flags_rebuild_zpna with compile-time skip optimization.
 * When codegen_flags_changed is set, the previous flags_op is known at
 * compile time.  For ROL/ROR/UNKNOWN, ZPNA is already materialized in
 * cpu_state.flags so the call can be elided entirely — zero extra uops.
 */
static inline void
emit_flags_rebuild_zpna(ir_data_t *ir)
{
    if (codegen_flags_changed) {
        switch (cpu_state.flags_op) {
            case FLAGS_ROL8:  case FLAGS_ROL16: case FLAGS_ROL32:
            case FLAGS_ROR8:  case FLAGS_ROR16: case FLAGS_ROR32:
            case FLAGS_UNKNOWN:
                return;
            default:
                break;
        }
    }
    call_flags_rebuild(ir, jit_flags_rebuild_zpna);
}

/*
 * A rotate on memory stores before its flags are set, so a faulting store
 * must find every flag of the previous instruction in cpu_state.flags:
 * a full rebuild, elided only when nothing is pending.
 */
static inline void
emit_flags_rebuild_mem(ir_data_t *ir)
{
    if (codegen_flags_changed && (cpu_state.flags_op == FLAGS_UNKNOWN))
        return;
    if (!codegen_flags_rebuild_known(ir, 0))
        uop_CALL_FUNC(ir, jit_flags_rebuild);
}

static uint32_t
shift_common_8(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count)
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_zpna(ir);
                uop_ROL_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_zpna(ir);
                uop_ROR_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                uop_SHL_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x28: /*SHR*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                uop_SHR_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x38: /*SAR*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                uop_SAR_IMM(ir, IREG_8(dest_reg), IREG_8(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            default:
                return 0;
        }
    } else {
        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_mem(ir);
                uop_ROL_IMM(ir, IREG_temp0_B, IREG_temp0_B, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_mem(ir);
                uop_ROR_IMM(ir, IREG_temp0_B, IREG_temp0_B, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_SHL_IMM(ir, IREG_temp1_B, IREG_temp0_B, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x28: /*SHR*/
                uop_SHR_IMM(ir, IREG_temp1_B, IREG_temp0_B, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x38: /*SAR*/
                uop_SAR_IMM(ir, IREG_temp1_B, IREG_temp0_B, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    return op_pc + 1;
}

static uint32_t
shift_common_16(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count)
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_zpna(ir);
                uop_ROL_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_zpna(ir);
                uop_ROR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_SHL_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x28: /*SHR*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_SHR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x38: /*SAR*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_SAR_IMM(ir, IREG_16(dest_reg), IREG_16(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            default:
                return 0;
        }
    } else {
        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_mem(ir);
                uop_ROL_IMM(ir, IREG_temp0_W, IREG_temp0_W, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_mem(ir);
                uop_ROR_IMM(ir, IREG_temp0_W, IREG_temp0_W, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_SHL_IMM(ir, IREG_temp1_W, IREG_temp0_W, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x28: /*SHR*/
                uop_SHR_IMM(ir, IREG_temp1_W, IREG_temp0_W, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x38: /*SAR*/
                uop_SAR_IMM(ir, IREG_temp1_W, IREG_temp0_W, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    return op_pc + 1;
}
static uint32_t
shift_common_32(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count)
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_zpna(ir);
                uop_ROL_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_zpna(ir);
                uop_ROR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SHL_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x28: /*SHR*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SHR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x38: /*SAR*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SAR_IMM(ir, IREG_32(dest_reg), IREG_32(dest_reg), count);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            default:
                return 0;
        }
    } else {
        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_mem(ir);
                uop_ROL_IMM(ir, IREG_temp0, IREG_temp0, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_mem(ir);
                uop_ROR_IMM(ir, IREG_temp0, IREG_temp0, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_SHL_IMM(ir, IREG_temp1, IREG_temp0, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x28: /*SHR*/
                uop_SHR_IMM(ir, IREG_temp1, IREG_temp0, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x38: /*SAR*/
                uop_SAR_IMM(ir, IREG_temp1, IREG_temp0, count);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op2, count);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    return op_pc + 1;
}

static uint32_t
shift_common_variable_32(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count_reg)
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_zpna(ir);
                uop_ROL(ir, IREG_32(dest_reg), IREG_32(dest_reg), count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_zpna(ir);
                uop_ROR(ir, IREG_32(dest_reg), IREG_32(dest_reg), count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SHL(ir, IREG_32(dest_reg), IREG_32(dest_reg), count_reg);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x28: /*SHR*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SHR(ir, IREG_32(dest_reg), IREG_32(dest_reg), count_reg);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x38: /*SAR*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SAR(ir, IREG_32(dest_reg), IREG_32(dest_reg), count_reg);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            default:
                return 0;
        }
    } else {
        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_mem(ir);
                uop_ROL(ir, IREG_temp0, IREG_temp0, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_mem(ir);
                uop_ROR(ir, IREG_temp0, IREG_temp0, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_SHL(ir, IREG_temp1, IREG_temp0, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x28: /*SHR*/
                uop_SHR(ir, IREG_temp1, IREG_temp0, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x38: /*SAR*/
                uop_SAR(ir, IREG_temp1, IREG_temp0, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    return op_pc + 1;
}

/*RCL and RCR by 1 (D0/D1 /2 /3), the multi-word shift idiom. As the
  interpreter: the flags are rebuilt, then CF takes the bit shifted out and
  OF is CF XOR the result's top bit (RCL) or the result's top two bits XORed
  (RCR); the other flags keep their values. Worked zero-extended in 32-bit
  temporaries.*/
static uint32_t
rcx1_common(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int size)
{
    x86seg *target_seg = NULL;
    int     is_rcr     = fetchdat & 0x08;
    int     tmp_sz     = (size == 8) ? IREG_temp0_B : ((size == 16) ? IREG_temp0_W : IREG_temp0);
    int     dest       = fetchdat & 7;
    int     reg_sz     = (size == 8) ? IREG_8(dest) : ((size == 16) ? IREG_16(dest) : IREG_32(dest));

    codegen_mark_code_present(block, cs + op_pc, 1);
    if (!codegen_flags_rebuild_known(ir, 0))
        uop_CALL_FUNC(ir, jit_flags_rebuild);
    if ((fetchdat & 0xc0) == 0xc0) {
        if (size == 32)
            uop_MOV(ir, IREG_temp0, reg_sz);
        else
            uop_MOVZX(ir, IREG_temp0, reg_sz);
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, (size / 8) - 1);
        if (size == 32)
            uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        else {
            uop_MEM_LOAD_REG(ir, (size == 8) ? IREG_temp3_B : IREG_temp3_W, ireg_seg_base(target_seg), IREG_eaaddr);
            uop_MOVZX(ir, IREG_temp0, (size == 8) ? IREG_temp3_B : IREG_temp3_W);
        }
    }

    uop_MOVZX(ir, IREG_temp1, IREG_flags);
    uop_AND_IMM(ir, IREG_temp1, IREG_temp1, C_FLAG); /*the old CF, 0 or 1*/
    if (is_rcr) {
        uop_AND_IMM(ir, IREG_temp2, IREG_temp0, 1); /*the new CF*/
        uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 1);
        uop_SHL_IMM(ir, IREG_temp1, IREG_temp1, size - 1);
        uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp1);
        uop_SHR_IMM(ir, IREG_temp3, IREG_temp0, 1); /*OF: the top two bits differ*/
        uop_XOR(ir, IREG_temp3, IREG_temp3, IREG_temp0);
        uop_SHR_IMM(ir, IREG_temp3, IREG_temp3, size - 2);
    } else {
        uop_SHR_IMM(ir, IREG_temp2, IREG_temp0, size - 1); /*the new CF*/
        uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 1);
        uop_SHL_IMM(ir, IREG_temp0, IREG_temp0, 1);
        uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp1);
        uop_SHR_IMM(ir, IREG_temp3, IREG_temp0, size - 1); /*OF: CF XOR the top bit*/
        uop_XOR(ir, IREG_temp3, IREG_temp3, IREG_temp2);
    }
    uop_AND_IMM(ir, IREG_temp3, IREG_temp3, 1);

    if (target_seg)
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, tmp_sz);
    else
        uop_MOV(ir, reg_sz, tmp_sz);

    uop_SHL_IMM(ir, IREG_temp3, IREG_temp3, 11); /*V_FLAG*/
    uop_OR(ir, IREG_temp2, IREG_temp2, IREG_temp3);
    uop_AND_IMM(ir, IREG_flags, IREG_flags, ~(C_FLAG | V_FLAG));
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp2_W);

    return op_pc + 1;
}

static uint32_t shift_common_variable_8(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count_reg);
static uint32_t shift_common_variable_16(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count_reg);
static uint32_t shxd_imm_ram(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int is32, int is_shrd);

/*A shift by an immediate read from RAM as the block runs
  (CODEBLOCK_NO_IMMEDIATES), the operand already in temp0 for a memory
  destination. A count of 0 at run time skips the shift and leaves the
  flags.*/
static uint32_t
shift_imm_ram(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int size)
{
    uint32_t new_pc;
    int      jump_uop;

    LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp2, cs + op_pc + 1);
    uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 0x1f);
    jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp2, 0);

    if (size == 8)
        new_pc = shift_common_variable_8(ir, fetchdat, op_pc, target_seg, IREG_temp2);
    else if (size == 16)
        new_pc = shift_common_variable_16(ir, fetchdat, op_pc, target_seg, IREG_temp2);
    else
        new_pc = shift_common_variable_32(ir, fetchdat, op_pc, target_seg, IREG_temp2);
#ifndef RELEASE_BUILD
    /*Only RCL and RCR have no variable form, and every caller turns them away*/
    if (!new_pc)
        fatal("shift_imm_ram: no variable shift for %02x\n", fetchdat & 0x38);
#endif
    uop_NOP_BARRIER(ir);
    uop_set_jump_dest(ir, jump_uop);
    /*What follows can't assume either state of the flags*/
    codegen_flags_changed = 0;
    return new_pc + 1;
}

uint32_t
ropC0(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;
    uint8_t imm;

    if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
    }
    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        return shift_imm_ram(block, ir, fetchdat, op_pc, target_seg, 8);
    imm = fastreadb(cs + op_pc + 1) & 0x1f;
    codegen_mark_code_present(block, cs + op_pc + 1, 1);

    if (imm)
        return shift_common_8(ir, fetchdat, op_pc, target_seg, imm) + 1;
    /*A count of 0 shifts nothing: the next instruction is past the count*/
    return op_pc + 2;
}
uint32_t
ropC1_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;
    uint8_t imm;

    if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
    }
    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        return shift_imm_ram(block, ir, fetchdat, op_pc, target_seg, 16);
    imm = fastreadb(cs + op_pc + 1) & 0x1f;
    codegen_mark_code_present(block, cs + op_pc + 1, 1);

    if (imm)
        return shift_common_16(ir, fetchdat, op_pc, target_seg, imm) + 1;
    /*A count of 0 shifts nothing: the next instruction is past the count*/
    return op_pc + 2;
}
uint32_t
ropC1_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;
    uint8_t imm;

    if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
        return 0;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
    }
    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        return shift_imm_ram(block, ir, fetchdat, op_pc, target_seg, 32);
    imm = fastreadb(cs + op_pc + 1) & 0x1f;
    codegen_mark_code_present(block, cs + op_pc + 1, 1);

    if (imm)
        return shift_common_32(ir, fetchdat, op_pc, target_seg, imm) + 1;
    /*A count of 0 shifts nothing: the next instruction is past the count*/
    return op_pc + 2;
}

uint32_t
ropD0(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;

    if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
        return rcx1_common(block, ir, fetchdat, op_32, op_pc, 8);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
    }

    return shift_common_8(ir, fetchdat, op_pc, target_seg, 1);
}
uint32_t
ropD1_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;

    if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
        return rcx1_common(block, ir, fetchdat, op_32, op_pc, 16);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
    }

    return shift_common_16(ir, fetchdat, op_pc, target_seg, 1);
}
uint32_t
ropD1_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;

    if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
        return rcx1_common(block, ir, fetchdat, op_32, op_pc, 32);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
    }

    return shift_common_32(ir, fetchdat, op_pc, target_seg, 1);
}

static uint32_t
shift_common_variable_8(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count_reg)
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_zpna(ir);
                uop_ROL(ir, IREG_8(dest_reg), IREG_8(dest_reg), count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_zpna(ir);
                uop_ROR(ir, IREG_8(dest_reg), IREG_8(dest_reg), count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                uop_SHL(ir, IREG_8(dest_reg), IREG_8(dest_reg), count_reg);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x28: /*SHR*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                uop_SHR(ir, IREG_8(dest_reg), IREG_8(dest_reg), count_reg);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            case 0x38: /*SAR*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_8(dest_reg));
                uop_SAR(ir, IREG_8(dest_reg), IREG_8(dest_reg), count_reg);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_8(dest_reg));
                break;

            default:
                return 0;
        }
    } else {
        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_mem(ir);
                uop_ROL(ir, IREG_temp0_B, IREG_temp0_B, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_mem(ir);
                uop_ROR(ir, IREG_temp0_B, IREG_temp0_B, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_B);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_B);
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_SHL(ir, IREG_temp1_B, IREG_temp0_B, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x28: /*SHR*/
                uop_SHR(ir, IREG_temp1_B, IREG_temp0_B, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            case 0x38: /*SAR*/
                uop_SAR(ir, IREG_temp1_B, IREG_temp0_B, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_B);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_B);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR8);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_B);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropD2(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;

    if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
        return 0;

    if (!(CL & 0x1f) || !block->ins)
        return 0;

    uop_AND_IMM(ir, IREG_temp2, REG_ECX, 0x1f);
    uop_CMP_IMM_JZ(ir, IREG_temp2, 0, codegen_exit_rout);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 0);
        uop_MEM_LOAD_REG(ir, IREG_temp0_B, ireg_seg_base(target_seg), IREG_eaaddr);
    }

    return shift_common_variable_8(ir, fetchdat, op_pc, target_seg, IREG_temp2);
}
static uint32_t
shift_common_variable_16(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, x86seg *target_seg, int count_reg)
{
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_zpna(ir);
                uop_ROL(ir, IREG_16(dest_reg), IREG_16(dest_reg), count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_zpna(ir);
                uop_ROR(ir, IREG_16(dest_reg), IREG_16(dest_reg), count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_SHL(ir, IREG_16(dest_reg), IREG_16(dest_reg), count_reg);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x28: /*SHR*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_SHR(ir, IREG_16(dest_reg), IREG_16(dest_reg), count_reg);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            case 0x38: /*SAR*/
                uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
                uop_SAR(ir, IREG_16(dest_reg), IREG_16(dest_reg), count_reg);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
                break;

            default:
                return 0;
        }
    } else {
        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_mem(ir);
                uop_ROL(ir, IREG_temp0_W, IREG_temp0_W, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_mem(ir);
                uop_ROR(ir, IREG_temp0_W, IREG_temp0_W, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_SHL(ir, IREG_temp1_W, IREG_temp0_W, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x28: /*SHR*/
                uop_SHR(ir, IREG_temp1_W, IREG_temp0_W, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            case 0x38: /*SAR*/
                uop_SAR(ir, IREG_temp1_W, IREG_temp0_W, count_reg);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1_W);
                uop_MOVZX(ir, IREG_flags_op1, IREG_temp0_W);
                uop_MOV(ir, IREG_flags_op2, count_reg);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR16);
                uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    return op_pc + 1;
}
uint32_t
ropD3_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;

    if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
        return 0;

    if (!(CL & 0x1f) || !block->ins)
        return 0;

    uop_AND_IMM(ir, IREG_temp2, REG_ECX, 0x1f);
    uop_CMP_IMM_JZ(ir, IREG_temp2, 0, codegen_exit_rout);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
    }

    return shift_common_variable_16(ir, fetchdat, op_pc, target_seg, IREG_temp2);
}
uint32_t
ropD3_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    if ((fetchdat & 0x30) == 0x10) /*RCL/RCR*/
        return 0;

    if (!(CL & 0x1f) || !block->ins)
        return 0;

    uop_AND_IMM(ir, IREG_temp2, REG_ECX, 0x1f);
    uop_CMP_IMM_JZ(ir, IREG_temp2, 0, codegen_exit_rout);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_zpna(ir);
                uop_ROL(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp2);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_zpna(ir);
                uop_ROR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp2);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SHL(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp2);
                uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x28: /*SHR*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SHR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp2);
                uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            case 0x38: /*SAR*/
                uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
                uop_SAR(ir, IREG_32(dest_reg), IREG_32(dest_reg), IREG_temp2);
                uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
                break;

            default:
                return 0;
        }
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);

        switch (fetchdat & 0x38) {
            case 0x00: /*ROL*/
                emit_flags_rebuild_mem(ir);
                uop_ROL(ir, IREG_temp0, IREG_temp0, IREG_temp2);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROL32);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                break;

            case 0x08: /*ROR*/
                emit_flags_rebuild_mem(ir);
                uop_ROR(ir, IREG_temp0, IREG_temp0, IREG_temp2);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_ROR32);
                uop_MOV(ir, IREG_flags_res, IREG_temp0);
                break;

            case 0x20:
            case 0x30: /*SHL*/
                uop_SHL(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x28: /*SHR*/
                uop_SHR(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHR32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            case 0x38: /*SAR*/
                uop_SAR(ir, IREG_temp1, IREG_temp0, IREG_temp2);
                uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp1);
                uop_MOV(ir, IREG_flags_op1, IREG_temp0);
                uop_MOV(ir, IREG_flags_op2, IREG_temp2);
                uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SAR32);
                uop_MOV(ir, IREG_flags_res, IREG_temp1);
                break;

            default:
                return 0;
        }
    }

    codegen_flags_changed = 1;
    return op_pc + 1;
}

uint32_t
ropSHLD_16_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;
    int     src_reg    = (fetchdat >> 3) & 7;
    uint8_t imm;

    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        return shxd_imm_ram(block, ir, fetchdat, op_32, op_pc, 0, 0);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        /*Checked and read whatever the count, as the interpreter does: a
          count of 0 still faults on the destination*/
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp2_W, ireg_seg_base(target_seg), IREG_eaaddr);
    }
    imm = fastreadb(cs + op_pc + 1) & 0x1f;
    codegen_mark_code_present(block, cs + op_pc + 1, 1);

    if (!imm)
        return op_pc + 2;

    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        if (imm == 16) {
            uop_MOV(ir, IREG_16(dest_reg), IREG_16(src_reg));
        } else if (imm >= 16) {
            uop_SHL_IMM(ir, IREG_16(dest_reg), IREG_16(src_reg), (imm - 16));
        } else {
            uop_SHL_IMM(ir, IREG_temp0_W, IREG_16(dest_reg), imm);
            uop_SHR_IMM(ir, IREG_temp1_W, IREG_16(src_reg), 16 - imm);
            uop_OR(ir, IREG_16(dest_reg), IREG_temp0_W, IREG_temp1_W);
        }
        uop_MOV_IMM(ir, IREG_flags_op2, imm);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
    } else {
        if (imm == 16) {
            uop_MOV(ir, IREG_temp0_W, IREG_16(src_reg));
        } else if (imm >= 16) {
            uop_SHL_IMM(ir, IREG_temp0_W, IREG_16(src_reg), (imm - 16));
        } else {
            uop_SHL_IMM(ir, IREG_temp0_W, IREG_temp2_W, imm);
            uop_SHR_IMM(ir, IREG_temp1_W, IREG_16(src_reg), 16 - imm);
            uop_OR(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp1_W);
        }
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);

        uop_MOVZX(ir, IREG_flags_op1, IREG_temp2_W);
        uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
        uop_MOV_IMM(ir, IREG_flags_op2, imm);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL16);
    }

    return op_pc + 2;
}
uint32_t
ropSHLD_32_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;
    int     src_reg    = (fetchdat >> 3) & 7;
    uint8_t imm;

    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        return shxd_imm_ram(block, ir, fetchdat, op_32, op_pc, 1, 0);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        /*Checked and read whatever the count, as the interpreter does: a
          count of 0 still faults on the destination*/
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp2, ireg_seg_base(target_seg), IREG_eaaddr);
    }
    imm = fastreadb(cs + op_pc + 1) & 0x1f;
    codegen_mark_code_present(block, cs + op_pc + 1, 1);

    if (!imm)
        return op_pc + 2;

    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        if (imm == 32) {
            uop_MOV(ir, IREG_32(dest_reg), IREG_32(src_reg));
        } else {
            uop_SHL_IMM(ir, IREG_temp0, IREG_32(dest_reg), imm);
            uop_SHR_IMM(ir, IREG_temp1, IREG_32(src_reg), 32 - imm);
            uop_OR(ir, IREG_32(dest_reg), IREG_temp0, IREG_temp1);
        }
        uop_MOV_IMM(ir, IREG_flags_op2, imm);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
    } else {
        if (imm == 32) {
            uop_MOV(ir, IREG_temp0, IREG_32(src_reg));
        } else {
            uop_SHL_IMM(ir, IREG_temp0, IREG_temp2, imm);
            uop_SHR_IMM(ir, IREG_temp1, IREG_32(src_reg), 32 - imm);
            uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp1);
        }
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);

        uop_MOV(ir, IREG_flags_op1, IREG_temp2);
        uop_MOV(ir, IREG_flags_res, IREG_temp0);
        uop_MOV_IMM(ir, IREG_flags_op2, imm);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHL32);
    }

    return op_pc + 2;
}
uint32_t
ropSHRD_16_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;
    int     src_reg    = (fetchdat >> 3) & 7;
    uint8_t imm;

    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        return shxd_imm_ram(block, ir, fetchdat, op_32, op_pc, 0, 1);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        /*Checked and read whatever the count, as the interpreter does: a
          count of 0 still faults on the destination*/
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp2_W, ireg_seg_base(target_seg), IREG_eaaddr);
    }
    imm = fastreadb(cs + op_pc + 1) & 0x1f;
    codegen_mark_code_present(block, cs + op_pc + 1, 1);

    if (!imm)
        return op_pc + 2;

    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOVZX(ir, IREG_flags_op1, IREG_16(dest_reg));
        if (imm == 16) {
            uop_MOV(ir, IREG_16(dest_reg), IREG_16(src_reg));
        } else if (imm >= 16) {
            uop_SHR_IMM(ir, IREG_16(dest_reg), IREG_16(src_reg), (imm - 16));
        } else {
            uop_SHR_IMM(ir, IREG_temp0_W, IREG_16(dest_reg), imm);
            uop_SHL_IMM(ir, IREG_temp1_W, IREG_16(src_reg), 16 - imm);
            uop_OR(ir, IREG_16(dest_reg), IREG_temp0_W, IREG_temp1_W);
        }
        uop_MOV_IMM(ir, IREG_flags_op2, imm);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHRD16);
        uop_MOVZX(ir, IREG_flags_res, IREG_16(dest_reg));
    } else {
        if (imm == 16) {
            uop_MOV(ir, IREG_temp0_W, IREG_16(src_reg));
        } else if (imm >= 16) {
            uop_SHR_IMM(ir, IREG_temp0_W, IREG_16(src_reg), (imm - 16));
        } else {
            uop_SHR_IMM(ir, IREG_temp0_W, IREG_temp2_W, imm);
            uop_SHL_IMM(ir, IREG_temp1_W, IREG_16(src_reg), 16 - imm);
            uop_OR(ir, IREG_temp0_W, IREG_temp0_W, IREG_temp1_W);
        }
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);

        uop_MOVZX(ir, IREG_flags_op1, IREG_temp2_W);
        uop_MOVZX(ir, IREG_flags_res, IREG_temp0_W);
        uop_MOV_IMM(ir, IREG_flags_op2, imm);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHRD16);
    }

    return op_pc + 2;
}
uint32_t
ropSHRD_32_imm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg = NULL;
    int     src_reg    = (fetchdat >> 3) & 7;
    uint8_t imm;

    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        return shxd_imm_ram(block, ir, fetchdat, op_32, op_pc, 1, 1);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        /*Checked and read whatever the count, as the interpreter does: a
          count of 0 still faults on the destination*/
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp2, ireg_seg_base(target_seg), IREG_eaaddr);
    }
    imm = fastreadb(cs + op_pc + 1) & 0x1f;
    codegen_mark_code_present(block, cs + op_pc + 1, 1);

    if (!imm)
        return op_pc + 2;

    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;

        uop_MOV(ir, IREG_flags_op1, IREG_32(dest_reg));
        if (imm == 32) {
            uop_MOV(ir, IREG_32(dest_reg), IREG_32(src_reg));
        } else {
            uop_SHR_IMM(ir, IREG_temp0, IREG_32(dest_reg), imm);
            uop_SHL_IMM(ir, IREG_temp1, IREG_32(src_reg), 32 - imm);
            uop_OR(ir, IREG_32(dest_reg), IREG_temp0, IREG_temp1);
        }
        uop_MOV_IMM(ir, IREG_flags_op2, imm);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHRD32);
        uop_MOV(ir, IREG_flags_res, IREG_32(dest_reg));
    } else {
        if (imm == 32) {
            uop_MOV(ir, IREG_temp0, IREG_32(src_reg));
        } else {
            uop_SHR_IMM(ir, IREG_temp0, IREG_temp2, imm);
            uop_SHL_IMM(ir, IREG_temp1, IREG_32(src_reg), 32 - imm);
            uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp1);
        }
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);

        uop_MOV(ir, IREG_flags_op1, IREG_temp2);
        uop_MOV(ir, IREG_flags_res, IREG_temp0);
        uop_MOV_IMM(ir, IREG_flags_op2, imm);
        uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SHRD32);
    }

    return op_pc + 2;
}

/*SHLD and SHRD by CL. Worked in 32-bit temporaries, as the interpreter
  computes them: a word SHLD is ((dest << 16 | src) << count) >> 16, which is
  its result for every count from 1 to 31, and a word SHRD (src << 16 | dest)
  >> count. A zero count changes nothing, flags included: as for the other
  shifts by CL, the block is compiled only when CL isn't 0, and exits to the
  interpreter if it is at run time.*/
/*The work of SHLD and SHRD: the destination's value in temp0 and a count
  of 1 to 31 in temp2. Leaves the result in the register or memory, and
  the flags' operands.*/
static void
shxd_variable_common(ir_data_t *ir, uint32_t fetchdat, x86seg *target_seg, int is32, int is_shrd)
{
    int src_reg = (fetchdat >> 3) & 7;

    if (is32) {
        uop_MOV_IMM(ir, IREG_temp3, 32);
        uop_SUB(ir, IREG_temp3, IREG_temp3, IREG_temp2);
        if (is_shrd) {
            uop_SHR(ir, IREG_temp1, IREG_temp0, IREG_temp2);
            uop_SHL(ir, IREG_temp3, IREG_32(src_reg), IREG_temp3);
        } else {
            uop_SHL(ir, IREG_temp1, IREG_temp0, IREG_temp2);
            uop_SHR(ir, IREG_temp3, IREG_32(src_reg), IREG_temp3);
        }
        uop_OR(ir, IREG_temp1, IREG_temp1, IREG_temp3);
    } else {
        uop_MOVZX(ir, IREG_temp3, IREG_16(src_reg));
        if (is_shrd) {
            uop_SHL_IMM(ir, IREG_temp3, IREG_temp3, 16);
            uop_OR(ir, IREG_temp1, IREG_temp0, IREG_temp3);
            uop_SHR(ir, IREG_temp1, IREG_temp1, IREG_temp2);
        } else {
            uop_SHL_IMM(ir, IREG_temp1, IREG_temp0, 16);
            uop_OR(ir, IREG_temp1, IREG_temp1, IREG_temp3);
            uop_SHL(ir, IREG_temp1, IREG_temp1, IREG_temp2);
            uop_SHR_IMM(ir, IREG_temp1, IREG_temp1, 16);
        }
    }

    if (!target_seg) {
        if (is32)
            uop_MOV(ir, IREG_32(fetchdat & 7), IREG_temp1);
        else
            uop_MOV(ir, IREG_16(fetchdat & 7), IREG_temp1_W);
    } else
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, is32 ? IREG_temp1 : IREG_temp1_W);

    uop_MOV(ir, IREG_flags_op1, IREG_temp0);
    uop_MOV(ir, IREG_flags_op2, IREG_temp2);
    if (is32)
        uop_MOV(ir, IREG_flags_res, IREG_temp1);
    else
        uop_MOVZX(ir, IREG_flags_res, IREG_temp1_W);
    if (is_shrd)
        uop_MOV_IMM(ir, IREG_flags_op, is32 ? FLAGS_SHRD32 : FLAGS_SHRD16);
    else
        uop_MOV_IMM(ir, IREG_flags_op, is32 ? FLAGS_SHL32 : FLAGS_SHL16);
}

static uint32_t
shxd_cl_common(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int is32, int is_shrd)
{
    x86seg *target_seg = NULL;

    if (!(CL & 0x1f) || !block->ins)
        return 0;

    uop_AND_IMM(ir, IREG_temp2, IREG_ECX, 0x1f);
    uop_CMP_IMM_JZ(ir, IREG_temp2, 0, codegen_exit_rout);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        if (is32)
            uop_MOV(ir, IREG_temp0, IREG_32(fetchdat & 7));
        else
            uop_MOVZX(ir, IREG_temp0, IREG_16(fetchdat & 7));
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, is32 ? 3 : 1);
        if (is32)
            uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        else {
            uop_MEM_LOAD_REG(ir, IREG_temp3_W, ireg_seg_base(target_seg), IREG_eaaddr);
            uop_MOVZX(ir, IREG_temp0, IREG_temp3_W);
        }
    }

    shxd_variable_common(ir, fetchdat, target_seg, is32, is_shrd);
    codegen_flags_changed = 1;

    return op_pc + 1;
}

/*SHLD and SHRD by an immediate read from RAM as the block runs
  (CODEBLOCK_NO_IMMEDIATES), for code that patches its own counts. The
  destination is checked and read whatever the count, as for a count
  compiled in; a count of 0 then skips the rest, flags included.*/
static uint32_t
shxd_imm_ram(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int is32, int is_shrd)
{
    x86seg *target_seg = NULL;
    int     jump_uop;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        if (is32)
            uop_MOV(ir, IREG_temp0, IREG_32(fetchdat & 7));
        else
            uop_MOVZX(ir, IREG_temp0, IREG_16(fetchdat & 7));
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, is32 ? 3 : 1);
        if (is32)
            uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        else {
            uop_MEM_LOAD_REG(ir, IREG_temp3_W, ireg_seg_base(target_seg), IREG_eaaddr);
            uop_MOVZX(ir, IREG_temp0, IREG_temp3_W);
        }
    }

    LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp2, cs + op_pc + 1);
    uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 0x1f);
    jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp2, 0);
    shxd_variable_common(ir, fetchdat, target_seg, is32, is_shrd);
    uop_NOP_BARRIER(ir);
    uop_set_jump_dest(ir, jump_uop);
    /*A count of 0 at run time leaves the flags: what follows can't assume
      either state*/
    codegen_flags_changed = 0;

    return op_pc + 2;
}

uint32_t
ropSHLD_16_CL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return shxd_cl_common(block, ir, fetchdat, op_32, op_pc, 0, 0);
}
uint32_t
ropSHLD_32_CL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return shxd_cl_common(block, ir, fetchdat, op_32, op_pc, 1, 0);
}
uint32_t
ropSHRD_16_CL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return shxd_cl_common(block, ir, fetchdat, op_32, op_pc, 0, 1);
}
uint32_t
ropSHRD_32_CL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return shxd_cl_common(block, ir, fetchdat, op_32, op_pc, 1, 1);
}

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
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_mov.h"

uint32_t
ropJMP_r8(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uint32_t offset    = (int32_t) (int8_t) fastreadb(cs + op_pc);
    uint32_t dest_addr = op_pc + 1 + offset;

    if (!(op_32 & 0x100))
        dest_addr &= 0xffff;

    if (offset < 0)
        codegen_can_unroll(block, ir, op_pc + 1, dest_addr);
    codegen_mark_code_present(block, cs + op_pc, 1);
    return dest_addr;
}
uint32_t
ropJMP_r16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t offset    = (int32_t) (int16_t) fastreadw(cs + op_pc);
    uint32_t dest_addr = op_pc + 2 + offset;

    dest_addr &= 0xffff;

    if (offset < 0)
        codegen_can_unroll(block, ir, op_pc + 1, dest_addr);
    codegen_mark_code_present(block, cs + op_pc, 2);
    return dest_addr;
}
uint32_t
ropJMP_r32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t offset    = fastreadl(cs + op_pc);
    uint32_t dest_addr = op_pc + 4 + offset;

    if (offset < 0)
        codegen_can_unroll(block, ir, op_pc + 1, dest_addr);
    codegen_mark_code_present(block, cs + op_pc, 4);
    return dest_addr;
}

uint32_t
ropJMP_far_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t new_pc = fastreadw(cs + op_pc);
    uint16_t new_cs = fastreadw(cs + op_pc + 2);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_pc, new_pc);
    uop_LOAD_FUNC_ARG_IMM(ir, 0, new_cs);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 4);
    uop_CALL_FUNC(ir, loadcsjmp);

    codegen_mark_code_present(block, cs + op_pc, 4);
    return -1;
}
uint32_t
ropJMP_far_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t new_pc = fastreadl(cs + op_pc);
    uint16_t new_cs = fastreadw(cs + op_pc + 4);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_pc, new_pc);
    uop_LOAD_FUNC_ARG_IMM(ir, 0, new_cs);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 6);
    uop_CALL_FUNC(ir, loadcsjmp);

    codegen_mark_code_present(block, cs + op_pc, 6);
    return -1;
}

/*CALL far ptr16:16 and ptr16:32 in every mode: dynarec_call_far_w/l run the
  interpreter's CALL_FAR (loadcscall or loadcs, the call and task gates, the
  pushes), with the target offset in the PC. The instruction ends the block,
  after the routine*/
uint32_t
ropCALL_far_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t new_pc = fastreadw(cs + op_pc);
    uint16_t new_cs = fastreadw(cs + op_pc + 2);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_accumulate_flush(ir);
    uop_MOV_IMM(ir, IREG_pc, new_pc);
    uop_LOAD_FUNC_ARG_IMM(ir, 0, new_cs);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 4);
    uop_CALL_FUNC(ir, dynarec_call_far_w);

    codegen_mark_code_present(block, cs + op_pc, 4);
    return -1;
}
uint32_t
ropCALL_far_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t new_pc = fastreadl(cs + op_pc);
    uint16_t new_cs = fastreadw(cs + op_pc + 4);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_accumulate_flush(ir);
    uop_MOV_IMM(ir, IREG_pc, new_pc);
    uop_LOAD_FUNC_ARG_IMM(ir, 0, new_cs);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 6);
    uop_CALL_FUNC(ir, dynarec_call_far_l);

    codegen_mark_code_present(block, cs + op_pc, 6);
    return -1;
}

uint32_t
ropCALL_r16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t offset    = (int32_t) (int16_t) fastreadw(cs + op_pc);
    uint16_t ret_addr  = op_pc + 2;
    uint16_t dest_addr = ret_addr + offset;
    int      sp_reg;

    dest_addr &= 0xffff;

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    sp_reg = LOAD_SP_WITH_OFFSET(ir, -2);
    uop_MEM_STORE_IMM_16(ir, IREG_SS_base, sp_reg, ret_addr);
    SUB_SP(ir, 2);
    uop_MOV_IMM(ir, IREG_pc, dest_addr);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return -1;
}
uint32_t
ropCALL_r32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t offset    = fastreadl(cs + op_pc);
    uint32_t ret_addr  = op_pc + 4;
    uint32_t dest_addr = ret_addr + offset;
    int      sp_reg;

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    sp_reg = LOAD_SP_WITH_OFFSET(ir, -4);
    uop_MEM_STORE_IMM_32(ir, IREG_SS_base, sp_reg, ret_addr);
    SUB_SP(ir, 4);
    uop_MOV_IMM(ir, IREG_pc, dest_addr);

    codegen_mark_code_present(block, cs + op_pc, 4);
    return -1;
}

uint32_t
ropRET_16(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32)
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
    else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
    }
    ADD_SP(ir, 2);
    uop_MOVZX(ir, IREG_pc, IREG_temp0_W);

    return -1;
}
uint32_t
ropRET_32(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32)
        uop_MEM_LOAD_REG(ir, IREG_pc, IREG_SS_base, IREG_ESP);
    else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_pc, IREG_SS_base, IREG_eaaddr);
    }
    ADD_SP(ir, 4);

    return -1;
}

uint32_t
ropRET_imm_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t offset = fastreadw(cs + op_pc);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32)
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
    else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
    }
    ADD_SP(ir, 2 + offset);
    uop_MOVZX(ir, IREG_pc, IREG_temp0_W);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return -1;
}
uint32_t
ropRET_imm_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t offset = fastreadw(cs + op_pc);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32)
        uop_MEM_LOAD_REG(ir, IREG_pc, IREG_SS_base, IREG_ESP);
    else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_pc, IREG_SS_base, IREG_eaaddr);
    }
    ADD_SP(ir, 4 + offset);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return -1;
}

/*Protected mode outside V86: pmoderetf pops CS:EIP and, on a privilege
  change, SS:ESP itself, and puts ESP back on every fault it raises, so
  nothing is popped here and nothing follows the call but the block's end*/
static uint32_t
retf_pm(codeblock_t *block, ir_data_t *ir, uint32_t op_pc, int is32, int has_imm)
{
    uint16_t offset = 0;

    if (has_imm) {
        offset = fastreadw(cs + op_pc);
        codegen_mark_code_present(block, cs + op_pc, 2);
    }

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_accumulate_flush(ir);
    uop_LOAD_FUNC_ARG_IMM(ir, 0, is32);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, offset);
    uop_CALL_FUNC(ir, pmoderetf);

    return -1;
}

uint32_t
ropRETF_16(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG))
        return retf_pm(block, ir, op_pc, 0, 0);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_ESP, 2);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_eaaddr, 2);
    }
    uop_MOVZX(ir, IREG_pc, IREG_temp0_W);
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, loadcs);
    ADD_SP(ir, 4);
    /*The op's own charge, on top of the timing model's, once nothing can
      fault; the block's end flushes it*/
    codegen_accumulate(ir, ACCREG_cycles, -timing_retf_rm);

    return -1;
}
uint32_t
ropRETF_32(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG))
        return retf_pm(block, ir, op_pc, 1, 0);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_ESP);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_ESP, 4);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_eaaddr);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_eaaddr, 4);
    }
    uop_MOV(ir, IREG_pc, IREG_temp0);
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, loadcs);
    ADD_SP(ir, 8);
    codegen_accumulate(ir, ACCREG_cycles, -timing_retf_rm);

    return -1;
}

uint32_t
ropRETF_imm_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t offset;

    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG))
        return retf_pm(block, ir, op_pc, 0, 1);

    offset = fastreadw(cs + op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_ESP, 2);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_eaaddr, 2);
    }
    uop_MOVZX(ir, IREG_pc, IREG_temp0_W);
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, loadcs);
    ADD_SP(ir, 4 + offset);
    codegen_accumulate(ir, ACCREG_cycles, -timing_retf_rm);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return -1;
}
uint32_t
ropRETF_imm_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t offset;

    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG))
        return retf_pm(block, ir, op_pc, 1, 1);

    offset = fastreadw(cs + op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_ESP);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_ESP, 4);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_eaaddr);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_eaaddr, 4);
    }
    uop_MOV(ir, IREG_pc, IREG_temp0);
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, loadcs);
    ADD_SP(ir, 8 + offset);
    codegen_accumulate(ir, ACCREG_cycles, -timing_retf_rm);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return -1;
}

/*IRET and IRETD, as the interpreter's opIRET and opIRETD. In protected
  mode, and in V86 mode at IOPL 3, pmodeiret does the work (dynarec_iret_pm):
  the descriptor checks, the stack and task switches and every fault are its
  own. Below IOPL 3 in V86 mode IRETD is #GP(0), and so is IRET without
  CR4.VME; with VME IRET pops IP, CS and FLAGS itself, is #GP(0) with SP as
  it was when the popped word has TF set or IF while VIP is set, keeps IOPL,
  IF and TF and takes VIF from the popped IF. Blocks are keyed on V86 but not
  on IOPL or CR4, so both are read as the block runs. In real mode the pops
  and the flags are compiled and the CS load is dynarec_iret_cs's. The
  instruction ends the block, after the routine.*/
static void
iret_pm(ir_data_t *ir, int is32)
{
    uop_LOAD_FUNC_ARG_IMM(ir, 0, is32);
    uop_CALL_FUNC(ir, dynarec_iret_pm);
}

static void
iret_v86(ir_data_t *ir, int is32)
{
    int jump_vme;
    int jump_end;

    uop_MOVZX(ir, IREG_temp0, IREG_flags);
    uop_AND_IMM(ir, IREG_temp0, IREG_temp0, 0x3000);
    if (is32) {
        uop_MOV_IMM(ir, IREG_temp1, 0x3000);
        uop_CMP_JB(ir, IREG_temp0, IREG_temp1, codegen_gpf_rout);
        iret_pm(ir, 1);
        return;
    }

    jump_vme = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0x3000);
    iret_pm(ir, 0);
    /*Both ways end in a C call, which leaves no register cached: the join
      below is safe*/
    jump_end = uop_JMP_DEST(ir);

    uop_set_jump_dest(ir, jump_vme);
    uop_MOV_REG_PTR(ir, IREG_temp1, &cr4);
    uop_AND_IMM(ir, IREG_temp1, IREG_temp1, CR4_VME);
    uop_CMP_IMM_JZ(ir, IREG_temp1, 0, codegen_gpf_rout);
    /*IP, CS and FLAGS at SP, SP + 2 and SP + 4, each wrapping at 64 KB*/
    uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
    uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
    uop_ADD_IMM(ir, IREG_eaaddr_W, IREG_SP, 2);
    uop_MOVZX(ir, IREG_eaaddr, IREG_eaaddr_W);
    uop_MEM_LOAD_REG(ir, IREG_temp1_W, IREG_SS_base, IREG_eaaddr);
    uop_ADD_IMM(ir, IREG_eaaddr_W, IREG_SP, 4);
    uop_MOVZX(ir, IREG_eaaddr, IREG_eaaddr_W);
    uop_MEM_LOAD_REG(ir, IREG_temp2_W, IREG_SS_base, IREG_eaaddr);
    uop_MOVZX(ir, IREG_temp2, IREG_temp2_W);
    uop_MOVZX(ir, IREG_temp3, IREG_eflags);
    uop_AND_IMM(ir, IREG_temp3, IREG_temp3, VIP_FLAG);
    uop_SHL_IMM(ir, IREG_temp3, IREG_temp3, 5);
    uop_OR_IMM(ir, IREG_temp3, IREG_temp3, T_FLAG);
    uop_AND(ir, IREG_temp3, IREG_temp3, IREG_temp2);
    uop_TEST_SET(ir, IREG_temp3, IREG_temp3, UOP_CC_NZ);
    uop_CMP_IMM_JZ(ir, IREG_temp3, 1, codegen_gpf_rout);
    uop_ADD_IMM(ir, IREG_SP, IREG_SP, 6);
    uop_AND_IMM(ir, IREG_temp3, IREG_temp2, I_FLAG);
    uop_SHR_IMM(ir, IREG_temp3, IREG_temp3, 6);
    uop_AND_IMM(ir, IREG_eflags, IREG_eflags, (uint16_t) ~VIF_FLAG);
    uop_OR(ir, IREG_eflags, IREG_eflags, IREG_temp3_W);
    uop_MOVZX(ir, IREG_temp3, IREG_flags);
    uop_AND_IMM(ir, IREG_temp3, IREG_temp3, 0x3300);
    uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 0x4cd5);
    uop_OR(ir, IREG_temp2, IREG_temp2, IREG_temp3);
    uop_OR_IMM(ir, IREG_temp2, IREG_temp2, 2);
    uop_MOV(ir, IREG_flags, IREG_temp2_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_UNKNOWN);
    uop_MOVZX(ir, IREG_pc, IREG_temp0_W);
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, dynarec_iret_cs);
    uop_NOP_BARRIER(ir);
    uop_set_jump_dest(ir, jump_end);
}

/*Real mode: the pops cannot fault (no paging, no limit check in the op)*/
static void
iret_real(ir_data_t *ir, int is32)
{
    int sp_reg;

    sp_reg = LOAD_SP(ir);
    if (is32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, sp_reg);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, 4);
        uop_MEM_LOAD_REG(ir, IREG_temp1_W, IREG_SS_base, sp_reg);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, 8);
        uop_MEM_LOAD_REG(ir, IREG_temp2_W, IREG_SS_base, sp_reg);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, 10);
        uop_MEM_LOAD_REG(ir, IREG_temp3_W, IREG_SS_base, sp_reg);
    } else {
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, sp_reg);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, 2);
        uop_MEM_LOAD_REG(ir, IREG_temp1_W, IREG_SS_base, sp_reg);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, 4);
        uop_MEM_LOAD_REG(ir, IREG_temp2_W, IREG_SS_base, sp_reg);
    }
    uop_AND_IMM(ir, IREG_temp2_W, IREG_temp2_W, 0xffd5);
    uop_OR_IMM(ir, IREG_temp2_W, IREG_temp2_W, 2);
    uop_MOV(ir, IREG_flags, IREG_temp2_W);
    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_UNKNOWN);
    if (is32) {
        /*The upper word where the CPU has the bits; ID when CPUID is on,
          which a Cyrix CCR4 write can change under a compiled block, so it
          is read here*/
        uop_MOV_REG_PTR(ir, IREG_temp2, &CPUID);
        uop_TEST_SET(ir, IREG_temp2, IREG_temp2, UOP_CC_NZ);
        uop_SHL_IMM(ir, IREG_temp2, IREG_temp2, 5);
        uop_OR_IMM(ir, IREG_temp2, IREG_temp2, cpu_eflags_implemented() & ~VID_FLAG);
        uop_AND(ir, IREG_eflags, IREG_temp3_W, IREG_temp2_W);
        ADD_SP(ir, 12);
        uop_MOV(ir, IREG_pc, IREG_temp0);
    } else {
        ADD_SP(ir, 6);
        uop_MOVZX(ir, IREG_pc, IREG_temp0_W);
    }
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, dynarec_iret_cs);
}

static uint32_t
iret_common(ir_data_t *ir, int is32)
{
    const int pmode = !!(msw & 1);
    const int v86   = !!(cpu_state.eflags & VM_FLAG);

    if (v86 || !pmode) {
#ifndef CODEGEN_BACKEND_HAS_INT_UOPS
        return 0;
#endif
        /*Room for the register versions below (at most 3 of temp0 and of
          temp1, 7 of temp2, 10 of temp3, 7 of eaaddr) without passing
          REG_VERSION_MAX, which only ends the block after the current
          instruction*/
        if ((reg_last_version[IREG_GET_REG(IREG_temp0)] + 4 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_temp1)] + 4 > REG_VERSION_MAX) ||
            (reg_last_version[IREG_GET_REG(IREG_temp2)] + 8 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_temp3)] + 12 > REG_VERSION_MAX) ||
            (reg_last_version[IREG_GET_REG(IREG_eaaddr)] + 8 > REG_VERSION_MAX))
            return 0;
    }

    /*The cycles so far are charged before anything can fault, as they are
      before an interpreter call*/
    codegen_accumulate_flush(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (v86)
        iret_v86(ir, is32);
    else if (pmode)
        iret_pm(ir, is32);
    else
        iret_real(ir, is32);

    codegen_flags_changed = 0;
    return -1;
}

uint32_t
ropIRET_16(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    return iret_common(ir, 0);
}
uint32_t
ropIRET_32(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    return iret_common(ir, 1);
}

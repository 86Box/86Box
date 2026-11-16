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

    /*The interpreter fetches the pointer before it loads anything*/
    codegen_mark_code_present(block, cs + op_pc, 4);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_pc, new_pc);
    uop_LOAD_FUNC_ARG_IMM(ir, 0, new_cs);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 4);
    uop_CALL_FUNC(ir, loadcsjmp);
    /*A task switch can keep the RF it loads*/
    if (cpu_debug_regs_486)
        uop_CALL_FUNC(ir, dynarec_rf_after_op);

    return -1;
}
uint32_t
ropJMP_far_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t new_pc = fastreadl(cs + op_pc);
    uint16_t new_cs = fastreadw(cs + op_pc + 4);

    codegen_mark_code_present(block, cs + op_pc, 6);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_pc, new_pc);
    uop_LOAD_FUNC_ARG_IMM(ir, 0, new_cs);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 6);
    uop_CALL_FUNC(ir, loadcsjmp);
    if (cpu_debug_regs_486)
        uop_CALL_FUNC(ir, dynarec_rf_after_op);

    return -1;
}

/*CALL far in real and virtual 8086 mode, IREG_pc holding the target offset
  and the selector in sel_reg (a 16-bit register) or, with sel_reg -1, in
  sel: CALL_FAR_w/l with loadcs's (real mode) or loadcscall's (V86) load of
  CS, compiled. The compiled form is taken when, as the block runs, PE and VM
  are those seen here, oldcpl is the CPL the load leaves (no ring 3 flush,
  oldcpl kept), the macro's two mem_held_release() leave the held lookup
  entries alone (V86: CPL 3 before and after the load; real mode: none held),
  the CPU's far CALL tests and real-mode charge are the ones compiled, the
  globals the macro writes already hold what it writes, and the SS base is
  not FFFFFFFFh; any other case calls dynarec_call_far_w/l. The pushes come before the CS fields: the macro loads
  CS first, but in these modes that load raises nothing and changes nothing
  a push reads, and a faulting push puts CS back, so each fault leaves what
  the macro leaves. Returns 0 when the caller is to call the routine
  instead. The instruction ends the block*/
int
call_far_rm(codeblock_t *block, ir_data_t *ir, int is32, int sel_reg, uint16_t sel, uint32_t ret_pc)
{
#ifdef CODEGEN_BACKEND_HAS_CALL_FAR_RM
    const int v86        = !!(cpu_state.eflags & VM_FLAG);
    const int width      = is32 ? 4 : 2;
    const int stack_test = call_far_stack_test;
    const int eip_test   = call_far_checks_eip_limit;
    int       jump_slow[11];
    int       nr_jump_slow = 0;
    int       jump_end;
    int       sp_reg;

    /*Protected mode, and real mode with VM set, call the routine*/
    if ((msw & 1) != v86)
        return 0;
    if ((reg_last_version[IREG_GET_REG(IREG_temp2)] + 24 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_eaaddr)] + 16 > REG_VERSION_MAX) ||
        (reg_last_version[IREG_GET_REG(IREG_temp0)] + 4 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_pc)] + 4 > REG_VERSION_MAX) ||
        (reg_last_version[IREG_GET_REG(IREG_temp3)] + 8 > REG_VERSION_MAX) ||
        (reg_last_version[IREG_GET_REG(IREG_ESP)] + 4 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_CS_seg)] + 4 > REG_VERSION_MAX) ||
        (reg_last_version[IREG_GET_REG(IREG_CS_base)] + 4 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_CS_limit_low)] + 4 > REG_VERSION_MAX) ||
        (reg_last_version[IREG_GET_REG(IREG_CS_limit_high)] + 4 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_cycles)] + 4 > REG_VERSION_MAX))
        return 0;

    uop_MOV_REG_PTR(ir, IREG_temp2, &cr0);
    uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 1);
    jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, v86);
    uop_MOVZX(ir, IREG_temp2, IREG_eflags);
    uop_AND_IMM(ir, IREG_temp2, IREG_temp2, VM_FLAG);
    jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, v86 ? VM_FLAG : 0);
    uop_MOV_REG_PTR(ir, IREG_temp2, &oldcpl);
    jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, v86 ? 3 : 0);
    if (v86) {
        uop_MOVZX_REG_PTR_8(ir, IREG_temp2, &cpu_state.seg_cs.access);
        uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 0x60);
        jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, 0x60);
    } else {
        uop_MOV_REG_PTR(ir, IREG_temp2, &mem_held_num);
        jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, 0);
    }
    /*The tests are the CPU's, which a switch of CPU can change with the
      block kept*/
    uop_MOV_REG_PTR(ir, IREG_temp2, &call_far_stack_test);
    jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, stack_test);
    uop_MOV_REG_PTR(ir, IREG_temp2, &call_far_checks_eip_limit);
    jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, eip_test);
    /*The real-mode charge is the CPU's too*/
    if (!v86) {
        uop_MOV_REG_PTR(ir, IREG_temp2, &timing_call_rm);
        jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, timing_call_rm);
    }
    /*The macro sets oldss to the SS base before its pushes and leaves
      optype, cgate16, cgate32, cgate_task and call_far_a_pending at 0: with
      them already so, it changes none of them*/
    uop_MOV_REG_PTR(ir, IREG_temp2, &oldss);
    jump_slow[nr_jump_slow++] = uop_CMP_JNZ_DEST(ir, IREG_temp2, IREG_SS_base);
    /*With an SS base of FFFFFFFFh the macro's pushes take the memory call
      whatever the lookup tables hold*/
    uop_ADD_IMM(ir, IREG_temp2, IREG_SS_base, 1);
    jump_slow[nr_jump_slow++] = uop_CMP_IMM_JZ_DEST(ir, IREG_temp2, 0);
    uop_MOV_REG_PTR(ir, IREG_temp2, &optype);
    uop_MOV_REG_PTR(ir, IREG_temp3, &cgate16);
    uop_OR(ir, IREG_temp2, IREG_temp2, IREG_temp3);
    uop_MOV_REG_PTR(ir, IREG_temp3, &cgate32);
    uop_OR(ir, IREG_temp2, IREG_temp2, IREG_temp3);
    uop_MOV_REG_PTR(ir, IREG_temp3, &cgate_task);
    uop_OR(ir, IREG_temp2, IREG_temp2, IREG_temp3);
    uop_MOV_REG_PTR(ir, IREG_temp3, &call_far_a_pending);
    uop_OR(ir, IREG_temp2, IREG_temp2, IREG_temp3);
    jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, 0);

    /*The routine runs its first tests with the PC past the instruction, and
      the pushes with the target in it*/
    uop_MOV(ir, IREG_temp0, IREG_pc);
    uop_MOV_IMM(ir, IREG_pc, ret_pc);

    /*Each fault below leaves where dynarec_call_far_w/l would raise it and
      the block's end would charge the timing model's drain*/
    codegen_exit_end_charge_begin(ir);
    /*call_far_rm_fault(): the whole return address within the SS limits,
      then the target within the CS limit as held, before anything is
      written or charged*/
    if (stack_test == CALL_FAR_TEST_ALL_MODES) {
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -width);
        CHECK_STACK_LIMITS(block, ir, sp_reg, width, IREG_temp3);
        sp_reg = LOAD_SP_WITH_OFFSET(ir, -2 * width);
        CHECK_STACK_LIMITS(block, ir, sp_reg, width, IREG_temp3);
    }
    if (eip_test == CALL_FAR_TEST_ALL_MODES)
        uop_CMP_JNBE(ir, IREG_temp0, IREG_CS_limit_high, codegen_gpf_rout);

    /*Charged after the load, kept when a push faults*/
    if (!v86)
        codegen_accumulate(ir, ACCREG_cycles, -timing_call_rm);
    uop_MOV(ir, IREG_pc, IREG_temp0);

    sp_reg = LOAD_SP_WITH_OFFSET(ir, -width);
    if (stack_test != CALL_FAR_TEST_ALL_MODES)
        CHECK_STACK_LIMITS(block, ir, sp_reg, width, IREG_temp3);
    if (is32) {
        uop_MOVZX(ir, IREG_temp2, IREG_CS_seg_W);
        uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, IREG_temp2);
    } else
        uop_MEM_STORE_REG(ir, IREG_SS_base, sp_reg, IREG_CS_seg_W);
    sp_reg = LOAD_SP_WITH_OFFSET(ir, -2 * width);
    if (stack_test != CALL_FAR_TEST_ALL_MODES)
        CHECK_STACK_LIMITS(block, ir, sp_reg, width, IREG_temp3);
    if (is32)
        uop_MEM_STORE_IMM_32(ir, IREG_SS_base, sp_reg, ret_pc);
    else
        uop_MEM_STORE_IMM_16(ir, IREG_SS_base, sp_reg, ret_pc & 0xffff);
    codegen_exit_end_charge_end(ir);
    SUB_SP(ir, 2 * width);

    /*loadcs()'s and loadcscall()'s real and V86 mode load*/
    if (sel_reg == -1) {
        uop_MOV_IMM(ir, IREG_CS_seg_W, sel);
        uop_MOV_IMM(ir, IREG_CS_base, (uint32_t) sel << 4);
    } else {
        uop_MOV(ir, IREG_CS_seg_W, sel_reg);
        uop_MOVZX(ir, IREG_CS_base, sel_reg);
        uop_SHL_IMM(ir, IREG_CS_base, IREG_CS_base, 4);
    }
    uop_MOV_IMM(ir, IREG_CS_limit_low, 0);
    uop_MOV_IMM(ir, IREG_CS_limit_high, 0xffff);
    uop_STORE_PTR_IMM(ir, &cpu_state.seg_cs.limit, 0xffff);
    uop_STORE_PTR_IMM_8(ir, &cpu_state.seg_cs.access, v86 ? 0xe2 : 0x82);
    uop_STORE_PTR_IMM_8(ir, &cpu_state.seg_cs.ar_high, 0x10);
    uop_STORE_PTR_IMM(ir, &cpu_state.seg_cs.checked, 1);
    codegen_accumulate_flush(ir);
    /*Both ways end in a full barrier: the join below reloads nothing*/
    if (cpu_debug_regs_486)
        uop_CALL_FUNC(ir, dynarec_rf_after_op);
    else
        uop_NOP_BARRIER(ir);
    jump_end = uop_JMP_DEST(ir);

    for (int c = 0; c < nr_jump_slow; c++)
        uop_set_jump_dest(ir, jump_slow[c]);
    if (sel_reg == -1)
        uop_LOAD_FUNC_ARG_IMM(ir, 0, sel);
    else
        uop_LOAD_FUNC_ARG_REG(ir, 0, sel_reg);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, ret_pc);
    uop_CALL_FUNC(ir, is32 ? dynarec_call_far_l : dynarec_call_far_w);
    uop_set_jump_dest(ir, jump_end);

    return 1;
#else
    (void) block;
    (void) ir;
    (void) is32;
    (void) sel_reg;
    (void) sel;
    (void) ret_pc;
    return 0;
#endif
}

/*CALL far ptr16:16 and ptr16:32 in every mode: dynarec_call_far_w/l run the
  interpreter's CALL_FAR (loadcscall or loadcs, the call and task gates, the
  pushes), with the target offset in the PC, unless call_far_rm() compiles
  it. The instruction ends the block, after the routine*/
uint32_t
ropCALL_far_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t new_pc = fastreadw(cs + op_pc);
    uint16_t new_cs = fastreadw(cs + op_pc + 2);

    /*The interpreter fetches the pointer before it pushes or loads anything*/
    codegen_mark_code_present(block, cs + op_pc, 4);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_accumulate_flush(ir);
    uop_MOV_IMM(ir, IREG_pc, new_pc);
    if (call_far_rm(block, ir, 0, -1, new_cs, op_pc + 4))
        return -1;
    uop_LOAD_FUNC_ARG_IMM(ir, 0, new_cs);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 4);
    uop_CALL_FUNC(ir, dynarec_call_far_w);

    return -1;
}
uint32_t
ropCALL_far_32(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint32_t new_pc = fastreadl(cs + op_pc);
    uint16_t new_cs = fastreadw(cs + op_pc + 4);

    codegen_mark_code_present(block, cs + op_pc, 6);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_accumulate_flush(ir);
    uop_MOV_IMM(ir, IREG_pc, new_pc);
    if (call_far_rm(block, ir, 1, -1, new_cs, op_pc + 6))
        return -1;
    uop_LOAD_FUNC_ARG_IMM(ir, 0, new_cs);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, op_pc + 6);
    uop_CALL_FUNC(ir, dynarec_call_far_l);

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
    CHECK_STACK_LIMITS(block, ir, sp_reg, 2, IREG_temp3);
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

    codegen_mark_code_present(block, cs + op_pc, 4);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    sp_reg = LOAD_SP_WITH_OFFSET(ir, -4);
    CHECK_STACK_LIMITS(block, ir, sp_reg, 4, IREG_temp3);
    uop_MEM_STORE_IMM_32(ir, IREG_SS_base, sp_reg, ret_addr);
    SUB_SP(ir, 4);
    uop_MOV_IMM(ir, IREG_pc, dest_addr);

    return -1;
}

uint32_t
ropRET_16(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32) {
        CHECK_STACK_LIMITS(block, ir, IREG_ESP, 2, IREG_temp3);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        CHECK_STACK_LIMITS(block, ir, IREG_eaaddr, 2, IREG_temp3);
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

    if (stack32) {
        CHECK_STACK_LIMITS(block, ir, IREG_ESP, 4, IREG_temp3);
        uop_MEM_LOAD_REG(ir, IREG_pc, IREG_SS_base, IREG_ESP);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        CHECK_STACK_LIMITS(block, ir, IREG_eaaddr, 4, IREG_temp3);
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

    if (stack32) {
        CHECK_STACK_LIMITS(block, ir, IREG_ESP, 2, IREG_temp3);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        CHECK_STACK_LIMITS(block, ir, IREG_eaaddr, 2, IREG_temp3);
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

    if (stack32) {
        CHECK_STACK_LIMITS(block, ir, IREG_ESP, 4, IREG_temp3);
        uop_MEM_LOAD_REG(ir, IREG_pc, IREG_SS_base, IREG_ESP);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        CHECK_STACK_LIMITS(block, ir, IREG_eaaddr, 4, IREG_temp3);
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

/*The CS load of a real or virtual 8086 mode far RET, the selector in
  temp1_W: loadcs()'s real and V86 mode branch, compiled. Taken when, as the
  block runs, PE and VM are those seen here and oldcpl is the CPL the load
  leaves, so no ring 3 flush is due and oldcpl is kept, and the held lookup
  entries are left alone (V86: CPL 3 after the load; real mode: none held);
  any other case calls loadcs()*/
static void
retf_load_cs_rm(ir_data_t *ir)
{
#ifdef CODEGEN_BACKEND_HAS_CALL_FAR_RM
    const int v86 = !!(cpu_state.eflags & VM_FLAG);
    int       jump_slow[4];
    int       nr_jump_slow = 0;
    int       jump_end;

    /*Real mode with VM set loads CS as V86 mode does: the routine*/
    if (((msw & 1) != v86) || (reg_last_version[IREG_GET_REG(IREG_temp2)] + 8 > REG_VERSION_MAX) ||
        (reg_last_version[IREG_GET_REG(IREG_CS_seg)] + 4 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_CS_base)] + 4 > REG_VERSION_MAX) ||
        (reg_last_version[IREG_GET_REG(IREG_CS_limit_low)] + 4 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_CS_limit_high)] + 4 > REG_VERSION_MAX)) {
        uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
        uop_CALL_FUNC(ir, loadcs);
        return;
    }

    uop_MOV_REG_PTR(ir, IREG_temp2, &cr0);
    uop_AND_IMM(ir, IREG_temp2, IREG_temp2, 1);
    jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, v86);
    uop_MOVZX(ir, IREG_temp2, IREG_eflags);
    uop_AND_IMM(ir, IREG_temp2, IREG_temp2, VM_FLAG);
    jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, v86 ? VM_FLAG : 0);
    uop_MOV_REG_PTR(ir, IREG_temp2, &oldcpl);
    jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, v86 ? 3 : 0);
    if (!v86) {
        uop_MOV_REG_PTR(ir, IREG_temp2, &mem_held_num);
        jump_slow[nr_jump_slow++] = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp2, 0);
    }

    uop_MOV(ir, IREG_CS_seg_W, IREG_temp1_W);
    uop_MOVZX(ir, IREG_CS_base, IREG_temp1_W);
    uop_SHL_IMM(ir, IREG_CS_base, IREG_CS_base, 4);
    uop_MOV_IMM(ir, IREG_CS_limit_low, 0);
    uop_MOV_IMM(ir, IREG_CS_limit_high, 0xffff);
    uop_STORE_PTR_IMM(ir, &cpu_state.seg_cs.limit, 0xffff);
    uop_STORE_PTR_IMM_8(ir, &cpu_state.seg_cs.access, v86 ? 0xe2 : 0x82);
    uop_STORE_PTR_IMM_8(ir, &cpu_state.seg_cs.ar_high, 0x10);
    uop_STORE_PTR_IMM(ir, &cpu_state.seg_cs.checked, 1);
    /*Both ways end in a full barrier: the join below reloads nothing*/
    uop_NOP_BARRIER(ir);
    jump_end = uop_JMP_DEST(ir);

    for (int c = 0; c < nr_jump_slow; c++)
        uop_set_jump_dest(ir, jump_slow[c]);
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, loadcs);
    uop_set_jump_dest(ir, jump_end);
#else
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp1_W);
    uop_CALL_FUNC(ir, loadcs);
#endif
}

/*The 16-bit form in real and V86 mode, where the CPU's manual has the test:
  the popped IP, in temp0_W, against the CS limit as held, before CS is
  popped. Returns 1 when it was made, with the IP in pc*/
static int
retf_rm_ip_test(ir_data_t *ir)
{
    if (retf_checks_eip_limit != RETF_EIP_TEST_ALL_MODES)
        return 0;

    uop_MOVZX(ir, IREG_pc, IREG_temp0_W);
    uop_CMP_JNBE(ir, IREG_pc, IREG_CS_limit_high, codegen_gpf_rout);
    return 1;
}

uint32_t
ropRETF_16(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    int ip_set;

    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG))
        return retf_pm(block, ir, op_pc, 0, 0);

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
        ip_set = retf_rm_ip_test(ir);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_ESP, 2);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
        ip_set = retf_rm_ip_test(ir);
        /*CS at SP + 2 wrapping at 64 KB, as the op reads it*/
        uop_ADD_IMM(ir, IREG_eaaddr_W, IREG_SP, 2);
        uop_MOVZX(ir, IREG_eaaddr, IREG_eaaddr_W);
        uop_MEM_LOAD_REG(ir, IREG_temp1_W, IREG_SS_base, IREG_eaaddr);
    }
    if (!ip_set)
        uop_MOVZX(ir, IREG_pc, IREG_temp0_W);
    retf_load_cs_rm(ir);
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

    /*CS is read as the dword the op reads, its low word kept: a fault on
      the upper half is taken*/
    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_ESP);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1, IREG_SS_base, IREG_ESP, 4);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_eaaddr);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1, IREG_SS_base, IREG_eaaddr, 4);
    }
    uop_MOV(ir, IREG_pc, IREG_temp0);
    retf_load_cs_rm(ir);
    ADD_SP(ir, 8);
    codegen_accumulate(ir, ACCREG_cycles, -timing_retf_rm);

    return -1;
}

uint32_t
ropRETF_imm_16(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint16_t offset;
    int      ip_set;

    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG))
        return retf_pm(block, ir, op_pc, 0, 1);

    offset = fastreadw(cs + op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);

    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_ESP);
        ip_set = retf_rm_ip_test(ir);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1_W, IREG_SS_base, IREG_ESP, 2);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, IREG_SS_base, IREG_eaaddr);
        ip_set = retf_rm_ip_test(ir);
        /*CS at SP + 2 wrapping at 64 KB, as the op reads it*/
        uop_ADD_IMM(ir, IREG_eaaddr_W, IREG_SP, 2);
        uop_MOVZX(ir, IREG_eaaddr, IREG_eaaddr_W);
        uop_MEM_LOAD_REG(ir, IREG_temp1_W, IREG_SS_base, IREG_eaaddr);
    }
    if (!ip_set)
        uop_MOVZX(ir, IREG_pc, IREG_temp0_W);
    retf_load_cs_rm(ir);
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

    /*CS is read as the dword the op reads, its low word kept: a fault on
      the upper half is taken*/
    if (stack32) {
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_ESP);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1, IREG_SS_base, IREG_ESP, 4);
    } else {
        uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
        uop_MEM_LOAD_REG(ir, IREG_temp0, IREG_SS_base, IREG_eaaddr);
        uop_MEM_LOAD_REG_OFFSET(ir, IREG_temp1, IREG_SS_base, IREG_eaaddr, 4);
    }
    uop_MOV(ir, IREG_pc, IREG_temp0);
    retf_load_cs_rm(ir);
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
iret_common(ir_data_t *ir, int is32, uint32_t op_pc)
{
    const int pmode = !!(msw & 1);
    /*V86 mode needs PE as well: with PE clear the interpreter takes the
      real-mode form whatever VM is*/
    const int v86   = pmode && (cpu_state.eflags & VM_FLAG);

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
    else if (pmode) {
        /*A return to a nested task saves the PC as the outgoing task's EIP,
          which is past the instruction, as in the interpreter*/
        uop_MOV_IMM(ir, IREG_pc, op_pc);
        iret_pm(ir, is32);
    } else
        iret_real(ir, is32);

    codegen_flags_changed = 0;
    return -1;
}

uint32_t
ropIRET_16(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    return iret_common(ir, 0, op_pc);
}
uint32_t
ropIRET_32(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    return iret_common(ir, 1, op_pc);
}

#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_flags.h"
#include "x86_ops.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "x87_sf.h"
#include "x87.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_fpu_misc.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_setcc.h"

uint32_t
ropFFREE(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    uop_FP_ENTER(ir);
    uop_MOV(ir, IREG_tag(dest_reg), TAG_EMPTY);

    return op_pc;
}

uint32_t
ropFLD(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int src_reg = fetchdat & 7;

    uop_FP_ENTER(ir);
    uop_MOV(ir, IREG_ST(-1), IREG_ST(src_reg));
    uop_MOV(ir, IREG_ST_i64(-1), IREG_ST_i64(src_reg));
    uop_MOV(ir, IREG_tag(-1), IREG_tag(src_reg));
    fpu_PUSH(block, ir);

    return op_pc;
}

uint32_t
ropFST(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    uop_FP_ENTER(ir);
    uop_MOV(ir, IREG_ST(dest_reg), IREG_ST(0));
    uop_MOV(ir, IREG_ST_i64(dest_reg), IREG_ST_i64(0));
    uop_MOV(ir, IREG_tag(dest_reg), IREG_tag(0));

    return op_pc;
}
uint32_t
ropFSTP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    uop_FP_ENTER(ir);
    uop_MOV(ir, IREG_ST(dest_reg), IREG_ST(0));
    uop_MOV(ir, IREG_ST_i64(dest_reg), IREG_ST_i64(0));
    uop_MOV(ir, IREG_tag(dest_reg), IREG_tag(0));
    fpu_POP(block, ir);

    return op_pc;
}

uint32_t
ropFSTCW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_NPXC);

    return op_pc + 1;
}
uint32_t
ropFSTSW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_NPXS);

    return op_pc + 1;
}
uint32_t
ropFSTSW_AX(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_FP_ENTER(ir);
    uop_MOV(ir, IREG_AX, IREG_NPXS);

    return op_pc;
}

/*FCMOVcc only exists on 686-class CPUs, but the recompiler uses one set of x87
  tables for every CPU. cpu.c installs the FCMOV-capable interpreter tables for
  exactly the CPUs that have it, so key off those instead of duplicating the CPU
  list here - without this the recompiler would execute FCMOV on (for example) a
  486, where it should raise an illegal opcode exception instead.*/
static int
fcmov_present(void)
{
    return (x86_dynarec_opcodes_da_a16 == dynarec_ops_fpu_686_da_a16);
}

static uint32_t
ropFCMOV_common(ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc,
                void (*gen_cond)(ir_data_t *ir, int invert), int invert)
{
    int src_reg = fetchdat & 7;

    if (!fcmov_present())
        return 0;

    /*FP_ENTER is a barrier, so it has to come before the condition is
      evaluated - a flush would otherwise discard IREG_temp0*/
    uop_FP_ENTER(ir);
    gen_cond(ir, invert);
    uop_CMOVNZ(ir, IREG_ST(0), IREG_ST(0), IREG_ST(src_reg), IREG_temp0);
    uop_CMOVNZ(ir, IREG_ST_i64(0), IREG_ST_i64(0), IREG_ST_i64(src_reg), IREG_temp0);
    uop_CMOVNZ(ir, IREG_tag(0), IREG_tag(0), IREG_tag(src_reg), IREG_temp0);

    return op_pc;
}

// clang-format off
#define ropFCMOV(cond, gen, invert)                                     \
    uint32_t ropFCMOV##cond(UNUSED(codeblock_t *block),                 \
                            ir_data_t *ir,                              \
                            UNUSED(uint8_t opcode),                     \
                            uint32_t fetchdat,                          \
                            UNUSED(uint32_t op_32),                     \
                            uint32_t op_pc)                             \
    {                                                                   \
        return ropFCMOV_common(ir, fetchdat, op_pc, gen, invert);       \
    }

ropFCMOV(B,   setcc_gen_B,  0)
ropFCMOV(E,   setcc_gen_E,  0)
ropFCMOV(BE,  setcc_gen_BE, 0)
ropFCMOV(U,   setcc_gen_P,  0)
ropFCMOV(NB,  setcc_gen_B,  1)
ropFCMOV(NE,  setcc_gen_E,  1)
ropFCMOV(NBE, setcc_gen_BE, 1)
ropFCMOV(NU,  setcc_gen_P,  1)
// clang-format on

uint32_t
ropFXCH(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int dest_reg = fetchdat & 7;

    uop_FP_ENTER(ir);
    uop_MOV(ir, IREG_temp0_D, IREG_ST(0));
    uop_MOV(ir, IREG_temp1_Q, IREG_ST_i64(0));
    uop_MOV(ir, IREG_temp2, IREG_tag(0));
    uop_MOV(ir, IREG_ST(0), IREG_ST(dest_reg));
    uop_MOV(ir, IREG_ST_i64(0), IREG_ST_i64(dest_reg));
    uop_MOV(ir, IREG_tag(0), IREG_tag(dest_reg));
    uop_MOV(ir, IREG_ST(dest_reg), IREG_temp0_D);
    uop_MOV(ir, IREG_ST_i64(dest_reg), IREG_temp1_Q);
    uop_MOV(ir, IREG_tag(dest_reg), IREG_temp2);

    return op_pc;
}

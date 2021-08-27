#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "x86.h"
#include "x86_flags.h"
#include "386_common.h"
#include "x87.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_fpu_arith.h"
#include "codegen_ops_helpers.h"

uint32_t ropFADD(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FADD(ir, IREG_ST(0), IREG_ST(0), IREG_ST(src_reg));
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);
        
        return op_pc;
}
uint32_t ropFADDr(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FADD(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);

        return op_pc;
}
uint32_t ropFADDP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FADD(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);
        fpu_POP(block, ir);
        
        return op_pc;
}

uint32_t ropFCOM(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FCOM(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(src_reg));
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(C0|C2|C3));
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);

        return op_pc;
}
uint32_t ropFCOMP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FCOM(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(src_reg));
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(C0|C2|C3));
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);
        fpu_POP(block, ir);

        return op_pc;
}
uint32_t ropFCOMPP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_FP_ENTER(ir);
        uop_FCOM(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(1));
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(C0|C2|C3));
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);
        fpu_POP2(block, ir);

        return op_pc;
}

uint32_t ropFDIV(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FDIV(ir, IREG_ST(0), IREG_ST(0), IREG_ST(src_reg));
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

        return op_pc;
}
uint32_t ropFDIVR(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FDIV(ir, IREG_ST(0), IREG_ST(src_reg), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

        return op_pc;
}
uint32_t ropFDIVr(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FDIV(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);

        return op_pc;
}
uint32_t ropFDIVRr(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FDIV(ir, IREG_ST(dest_reg), IREG_ST(0), IREG_ST(dest_reg));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);

        return op_pc;
}
uint32_t ropFDIVP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FDIV(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);
        fpu_POP(block, ir);
        
        return op_pc;
}
uint32_t ropFDIVRP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FDIV(ir, IREG_ST(dest_reg), IREG_ST(0), IREG_ST(dest_reg));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);
        fpu_POP(block, ir);

        return op_pc;
}

uint32_t ropFMUL(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FMUL(ir, IREG_ST(0), IREG_ST(0), IREG_ST(src_reg));
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

        return op_pc;
}
uint32_t ropFMULr(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FMUL(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);

        return op_pc;
}
uint32_t ropFMULP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FMUL(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);
        fpu_POP(block, ir);

        return op_pc;
}

uint32_t ropFSUB(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FSUB(ir, IREG_ST(0), IREG_ST(0), IREG_ST(src_reg));
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

        return op_pc;
}
uint32_t ropFSUBR(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FSUB(ir, IREG_ST(0), IREG_ST(src_reg), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

        return op_pc;
}
uint32_t ropFSUBr(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FSUB(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);

        return op_pc;
}
uint32_t ropFSUBRr(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FSUB(ir, IREG_ST(dest_reg), IREG_ST(0), IREG_ST(dest_reg));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);

        return op_pc;
}
uint32_t ropFSUBP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FSUB(ir, IREG_ST(dest_reg), IREG_ST(dest_reg), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);
        fpu_POP(block, ir);

        return op_pc;
}
uint32_t ropFSUBRP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int dest_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FSUB(ir, IREG_ST(dest_reg), IREG_ST(0), IREG_ST(dest_reg));
        uop_MOV_IMM(ir, IREG_tag(dest_reg), TAG_VALID);
        fpu_POP(block, ir);
        
        return op_pc;
}

uint32_t ropFUCOM(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FCOM(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(src_reg));
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(C0|C2|C3));
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);

        return op_pc;
}
uint32_t ropFUCOMP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        int src_reg = fetchdat & 7;

        uop_FP_ENTER(ir);
        uop_FCOM(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(src_reg));
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(C0|C2|C3));
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);
        fpu_POP(block, ir);

        return op_pc;
}
uint32_t ropFUCOMPP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_FP_ENTER(ir);
        uop_FCOM(ir, IREG_temp0_W, IREG_ST(0), IREG_ST(1));
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(C0|C2|C3));
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);
        fpu_POP2(block, ir);

        return op_pc;
}

#define ropF_arith_mem(name, load_uop)                                                                                          \
uint32_t ropFADD ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)  \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        if ((cpu_state.npxc >> 10) & 3)                                                                                         \
                return 0;                                                                                                       \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                                                     \
        uop_FADD(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFCOM ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)  \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                                                     \
        uop_FCOM(ir, IREG_temp1_W, IREG_ST(0), IREG_temp0_D);                                                                   \
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(C0|C2|C3));                                                                     \
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp1_W);                                                                         \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFCOMP ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)  \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                                                     \
        uop_FCOM(ir, IREG_temp1_W, IREG_ST(0), IREG_temp0_D);                                                                   \
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(C0|C2|C3));                                                                     \
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp1_W);                                                                         \
        fpu_POP(block, ir);                                                                                                     \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFDIV ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)  \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                                                     \
        uop_FDIV(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFDIVR ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                                                     \
        uop_FDIV(ir, IREG_ST(0), IREG_temp0_D, IREG_ST(0));                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFMUL ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)  \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                                                     \
        uop_FMUL(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFSUB ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)  \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                                                     \
        uop_FSUB(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFSUBR ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        load_uop(ir, IREG_temp0_D, ireg_seg_base(target_seg), IREG_eaaddr);                                                     \
        uop_FSUB(ir, IREG_ST(0), IREG_temp0_D, IREG_ST(0));                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}

ropF_arith_mem(s, uop_MEM_LOAD_SINGLE)
ropF_arith_mem(d, uop_MEM_LOAD_DOUBLE)

#define ropFI_arith_mem(name, temp_reg)                                                                                         \
uint32_t ropFIADD ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                                                 \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                                                         \
        uop_FADD(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFICOM ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                                                 \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                                                         \
        uop_FCOM(ir, IREG_temp1_W, IREG_ST(0), IREG_temp0_D);                                                                   \
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(C0|C2|C3));                                                                     \
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp1_W);                                                                         \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFICOMP ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                                                 \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                                                         \
        uop_FCOM(ir, IREG_temp1_W, IREG_ST(0), IREG_temp0_D);                                                                   \
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(C0|C2|C3));                                                                     \
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp1_W);                                                                         \
        fpu_POP(block, ir);                                                                                                     \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFIDIV ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                                                 \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                                                         \
        uop_FDIV(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFIDIVR ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)\
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                                                 \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                                                         \
        uop_FDIV(ir, IREG_ST(0), IREG_temp0_D, IREG_ST(0));                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFIMUL ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                                                 \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                                                         \
        uop_FMUL(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFISUB ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                                                 \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                                                         \
        uop_FSUB(ir, IREG_ST(0), IREG_ST(0), IREG_temp0_D);                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}                                                                                                                               \
uint32_t ropFISUBR ## name(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)\
{                                                                                                                               \
        x86seg *target_seg;                                                                                                     \
                                                                                                                                \
        uop_FP_ENTER(ir);                                                                                                       \
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                                                           \
        op_pc--;                                                                                                                \
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);                                  \
        codegen_check_seg_read(block, ir, target_seg);                                                                          \
        uop_MEM_LOAD_REG(ir, temp_reg, ireg_seg_base(target_seg), IREG_eaaddr);                                                 \
        uop_MOV_DOUBLE_INT(ir, IREG_temp0_D, temp_reg);                                                                         \
        uop_FSUB(ir, IREG_ST(0), IREG_temp0_D, IREG_ST(0));                                                                     \
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);                                                                                \
                                                                                                                                \
        return op_pc+1;                                                                                                         \
}

ropFI_arith_mem(l, IREG_temp0)
ropFI_arith_mem(w, IREG_temp0_W)


uint32_t ropFABS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_FP_ENTER(ir);
        uop_FABS(ir, IREG_ST(0), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

        return op_pc;
}

uint32_t ropFCHS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_FP_ENTER(ir);
        uop_FCHS(ir, IREG_ST(0), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

        return op_pc;
}
uint32_t ropFSQRT(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_FP_ENTER(ir);
        uop_FSQRT(ir, IREG_ST(0), IREG_ST(0));
        uop_MOV_IMM(ir, IREG_tag(0), TAG_VALID);

        return op_pc;
}
uint32_t ropFTST(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uop_FP_ENTER(ir);
        uop_FTST(ir, IREG_temp0_W, IREG_ST(0));
        uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~(C0|C2|C3));
        uop_OR(ir, IREG_NPXS, IREG_NPXS, IREG_temp0_W);

        return op_pc;
}

#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "x86.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_ir.h"
#include "codegen_ir_defs.h"
#include "codegen_reg.h"
#include "codegen_ops_helpers.h"

void LOAD_IMMEDIATE_FROM_RAM_16_unaligned(codeblock_t *block, ir_data_t *ir, int dest_reg, uint32_t addr)
{
        /*Word access that crosses two pages. Perform reads from both pages, shift and combine*/
        uop_MOVZX_REG_PTR_8(ir, IREG_temp3_W, get_ram_ptr(addr));
        uop_MOVZX_REG_PTR_8(ir, dest_reg, get_ram_ptr(addr+1));
        uop_SHL_IMM(ir, IREG_temp3_W, IREG_temp3_W, 8);
        uop_OR(ir, dest_reg, dest_reg, IREG_temp3_W);
}

void LOAD_IMMEDIATE_FROM_RAM_32_unaligned(codeblock_t *block, ir_data_t *ir, int dest_reg, uint32_t addr)
{
        /*Dword access that crosses two pages. Perform reads from both pages, shift and combine*/
        uop_MOV_REG_PTR(ir, dest_reg, get_ram_ptr(addr & ~3));
        uop_MOV_REG_PTR(ir, IREG_temp3, get_ram_ptr((addr + 4) & ~3));
        uop_SHR_IMM(ir, dest_reg, dest_reg, (addr & 3) * 8);
        uop_SHL_IMM(ir, IREG_temp3, IREG_temp3, (4 - (addr & 3)) * 8);
        uop_OR(ir, dest_reg, dest_reg, IREG_temp3);
}

#define UNROLL_MAX_REG_REFERENCES 200
#define UNROLL_MAX_UOPS 1000
#define UNROLL_MAX_COUNT 10
int codegen_can_unroll_full(codeblock_t *block, ir_data_t *ir, uint32_t next_pc, uint32_t dest_addr)
{
        int start;
        int max_unroll;
        int first_instruction;
        int TOP = -1;

        /*Check that dest instruction was actually compiled into block*/
        start = codegen_get_instruction_uop(block, dest_addr, &first_instruction, &TOP);

        /*Couldn't find any uOPs corresponding to the destination instruction*/
        if (start == -1)
        {
                /*Is instruction jumping to itself?*/
                if (dest_addr != cpu_state.oldpc)
                {
                        return 0;
                }
                else
                {
                        start = ir->wr_pos;
                        TOP = cpu_state.TOP;
                }
        }
        
        if (TOP != cpu_state.TOP)
                return 0;

        max_unroll = UNROLL_MAX_UOPS / ((ir->wr_pos-start)+6);
        if ((max_version_refcount != 0) && (max_unroll > (UNROLL_MAX_REG_REFERENCES / max_version_refcount)))
                max_unroll = (UNROLL_MAX_REG_REFERENCES / max_version_refcount);
        if (max_unroll > UNROLL_MAX_COUNT)
                max_unroll = UNROLL_MAX_COUNT;
        if (max_unroll <= 1)
                return 0;

        codegen_ir_set_unroll(max_unroll, start, first_instruction);

        return 1;
}

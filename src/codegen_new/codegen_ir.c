#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "codegen.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_ir.h"
#include "codegen_reg.h"

extern int has_ea;
static ir_data_t ir_block;

static int codegen_unroll_start, codegen_unroll_count;
static int codegen_unroll_first_instruction;

ir_data_t *codegen_ir_init()
{
        ir_block.wr_pos = 0;

        codegen_unroll_count = 0;

        return &ir_block;
}

void codegen_ir_set_unroll(int count, int start, int first_instruction)
{
        codegen_unroll_count = count;
        codegen_unroll_start = start;
        codegen_unroll_first_instruction = first_instruction;
}

static void duplicate_uop(ir_data_t *ir, uop_t *uop, int offset)
{
        uop_t *new_uop = uop_alloc(ir, uop->type);

        if (!ir_reg_is_invalid(uop->src_reg_a))
                new_uop->src_reg_a = codegen_reg_read(uop->src_reg_a.reg);
        if (!ir_reg_is_invalid(uop->src_reg_b))
                new_uop->src_reg_b = codegen_reg_read(uop->src_reg_b.reg);
        if (!ir_reg_is_invalid(uop->src_reg_c))
                new_uop->src_reg_c = codegen_reg_read(uop->src_reg_c.reg);
        if (!ir_reg_is_invalid(uop->dest_reg_a))
                new_uop->dest_reg_a = codegen_reg_write(uop->dest_reg_a.reg, ir->wr_pos-1);

        new_uop->type = uop->type;
        new_uop->imm_data = uop->imm_data;
        new_uop->p = uop->p;
        new_uop->pc = uop->pc;
        
        if (uop->jump_dest_uop != -1)
        {
                new_uop->jump_dest_uop = uop->jump_dest_uop + offset;
        }
}

void codegen_ir_compile(ir_data_t *ir, codeblock_t *block)
{
        int jump_target_at_end = -1;
        int c;

        if (codegen_unroll_count)
        {
                int unroll_count;
                int unroll_end;
                
                codegen_set_loop_start(ir, codegen_unroll_first_instruction);
                unroll_end = ir->wr_pos;
                
                for (unroll_count = 1; unroll_count < codegen_unroll_count; unroll_count++)
                {
                        int offset = ir->wr_pos - codegen_unroll_start;
//                        pclog("Unroll from %i to %i, offset %i - iteration %i\n", codegen_unroll_start, ir->wr_pos, offset, unroll_count);
                        for (c = codegen_unroll_start; c < unroll_end; c++)
                        {
//                                pclog(" Duplicate uop %i\n", c);
                                duplicate_uop(ir, &ir->uops[c], offset);
                        }
                }
        }

        codegen_reg_mark_as_required();
        codegen_reg_process_dead_list(ir);
        block_write_data = codeblock_allocator_get_ptr(block->head_mem_block);
        block_pos = 0;
        codegen_backend_prologue(block);

        for (c = 0; c < ir->wr_pos; c++)
        {
                uop_t *uop = &ir->uops[c];
                
//                pclog("uOP %i : %08x\n", c, uop->type);

                if (uop->type & UOP_TYPE_BARRIER)
                        codegen_reg_flush_invalidate(ir, block);

                if (uop->type & UOP_TYPE_JUMP_DEST)
                {
                        uop_t *uop_dest = uop;

                        while (uop_dest->jump_list_next != -1)
                        {
                                uop_dest = &ir->uops[uop_dest->jump_list_next];
                                codegen_set_jump_dest(block, uop_dest->p);
                        }
                }

                if ((uop->type & UOP_MASK) == UOP_INVALID)
                        continue;

#ifdef CODEGEN_BACKEND_HAS_MOV_IMM
                if ((uop->type & UOP_MASK) == (UOP_MOV_IMM & UOP_MASK) && reg_is_native_size(uop->dest_reg_a) && !codegen_reg_is_loaded(uop->dest_reg_a) && reg_version[IREG_GET_REG(uop->dest_reg_a.reg)][uop->dest_reg_a.version].refcount <= 0)
                {
                        /*Special case for UOP_MOV_IMM - if destination not already in host register
                          and won't be used again then just store directly to memory*/
                        codegen_reg_write_imm(block, uop->dest_reg_a, uop->imm_data);
                }
                else
#endif
                if ((uop->type & UOP_MASK) == (UOP_MOV & UOP_MASK) && reg_version[IREG_GET_REG(uop->src_reg_a.reg)][uop->src_reg_a.version].refcount <= 1  &&
                                reg_is_native_size(uop->src_reg_a) && reg_is_native_size(uop->dest_reg_a))
                {
                        /*Special case for UOP_MOV - if source register won't be used again then
                          just rename it to dest register instead of moving*/
                        codegen_reg_alloc_register(invalid_ir_reg, uop->src_reg_a, invalid_ir_reg, invalid_ir_reg);
                        uop->src_reg_a_real = codegen_reg_alloc_read_reg(block, uop->src_reg_a, NULL);
                        codegen_reg_rename(block, uop->src_reg_a, uop->dest_reg_a);
                        if (uop->type & UOP_TYPE_ORDER_BARRIER)
                                codegen_reg_flush(ir, block);
                }
                else
                {
                        if (uop->type & UOP_TYPE_PARAMS_REGS)
                        {
                                codegen_reg_alloc_register(uop->dest_reg_a, uop->src_reg_a, uop->src_reg_b, uop->src_reg_c);
                                if (uop->src_reg_a.reg != IREG_INVALID)
                                {
                                        uop->src_reg_a_real = codegen_reg_alloc_read_reg(block, uop->src_reg_a, NULL);
                                }
                                if (uop->src_reg_b.reg != IREG_INVALID)
                                {
                                        uop->src_reg_b_real = codegen_reg_alloc_read_reg(block, uop->src_reg_b, NULL);
                                }
                                if (uop->src_reg_c.reg != IREG_INVALID)
                                {
                                        uop->src_reg_c_real = codegen_reg_alloc_read_reg(block, uop->src_reg_c, NULL);
                                }
                        }

                        if (uop->type & UOP_TYPE_ORDER_BARRIER)
                                codegen_reg_flush(ir, block);

                        if (uop->type & UOP_TYPE_PARAMS_REGS)
                        {
                                if (uop->dest_reg_a.reg != IREG_INVALID)
                                {
                                        uop->dest_reg_a_real = codegen_reg_alloc_write_reg(block, uop->dest_reg_a);
                                }
                        }
#ifndef RELEASE_BUILD
                        if (!uop_handlers[uop->type & UOP_MASK])
                                fatal("!uop_handlers[uop->type & UOP_MASK] %08x\n", uop->type);
#endif
                        uop_handlers[uop->type & UOP_MASK](block, uop);
                }

                if (uop->type & UOP_TYPE_JUMP)
                {
                        if (uop->jump_dest_uop == ir->wr_pos)
                        {
                                if (jump_target_at_end == -1)
                                        jump_target_at_end = c;
                                else
                                {
                                        uop_t *uop_dest = &ir->uops[jump_target_at_end];

                                        while (uop_dest->jump_list_next != -1)
                                                uop_dest = &ir->uops[uop_dest->jump_list_next];

                                        uop_dest->jump_list_next = c;
                                }
                        }
                        else
                        {
                                uop_t *uop_dest = &ir->uops[uop->jump_dest_uop];
                                
                                while (uop_dest->jump_list_next != -1)
                                        uop_dest = &ir->uops[uop_dest->jump_list_next];

                                uop_dest->jump_list_next = c;
                                ir->uops[uop->jump_dest_uop].type |= UOP_TYPE_JUMP_DEST;
                        }
                }
        }

        codegen_reg_flush_invalidate(ir, block);
        
        if (jump_target_at_end != -1)
        {
                uop_t *uop_dest = &ir->uops[jump_target_at_end];

                while (1)
                {
                        codegen_set_jump_dest(block, uop_dest->p);
                        if (uop_dest->jump_list_next == -1)
                                break;
                        uop_dest = &ir->uops[uop_dest->jump_list_next];
                }
        }

        codegen_backend_epilogue(block);
        block_write_data = NULL;
//        if (has_ea)
//                fatal("IR compilation complete\n");
}

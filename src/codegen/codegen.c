#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/plat_unused.h>

#include "x86_ops.h"
#include "codegen.h"
#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "x87_sf.h"
#include "x87.h"

#include "386_common.h"

#include "codegen_accumulate.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"

#define MAX_INSTRUCTION_COUNT 50
static struct {
    uint32_t pc;
    int      op_ssegs;
    x86seg  *op_ea_seg;
    uint32_t op_32;
    int      first_uop;
    int      TOP;
} codegen_instructions[MAX_INSTRUCTION_COUNT];

int
codegen_get_instruction_uop(codeblock_t *block, uint32_t pc, int *first_instruction, int *TOP)
{
    for (uint8_t c = 0; c <= block->ins; c++) {
        if (codegen_instructions[c].pc == pc) {
            *first_instruction = c;
            *TOP               = codegen_instructions[c].TOP;
            return codegen_instructions[c].first_uop;
        }
    }

    *first_instruction = block->ins;
    return -1;
}


void
codegen_set_loop_start(ir_data_t *ir, int first_instruction)
{
    uop_MOV_IMM(ir, IREG_op32, codegen_instructions[first_instruction].op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) codegen_instructions[first_instruction].op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, codegen_instructions[first_instruction].op_ssegs);
}

int has_ea;
extern uint32_t codegen_endpc;

codeblock_t *codeblock;
uint32_t    *codeblock_hash;

void (*codegen_timing_start)(void);
void (*codegen_timing_prefix)(uint8_t prefix, uint32_t fetchdat);
void (*codegen_timing_opcode)(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc);
void (*codegen_timing_block_start)(void);
void (*codegen_timing_block_end)(void);
int (*codegen_timing_jump_cycles)(void);

void
codegen_timing_set(codegen_timing_t *timing)
{
    codegen_timing_start       = timing->start;
    codegen_timing_prefix      = timing->prefix;
    codegen_timing_opcode      = timing->opcode;
    codegen_timing_block_start = timing->block_start;
    codegen_timing_block_end   = timing->block_end;
    codegen_timing_jump_cycles = timing->jump_cycles;
}

int codegen_in_recompile;

static int      last_op_ssegs;
static x86seg  *last_op_ea_seg;
static uint32_t last_op_32;

void
codegen_generate_reset(void)
{
    last_op_ssegs  = -1;
    last_op_ea_seg = NULL;
    last_op_32     = -1;
    has_ea         = 0;
}

void
codegen_check_seg_read(UNUSED(codeblock_t *block), ir_data_t *ir, x86seg *seg)
{
    /*Segments always valid in real/V86 mode*/
    if (!(cr0 & 1) || (cpu_state.eflags & VM_FLAG))
        return;
    /*SS must always be valid*/
    if (seg == &cpu_state.seg_ss)
        return;
    /*CS is valid, but may be execute-only: #GP(0), as in the interpreter's
      CHECK_READ. Its checked flag isn't reset per block, so no caching*/
    if (seg == &cpu_state.seg_cs) {
        uop_MOVZX_REG_PTR_8(ir, IREG_temp3, &seg->access);
        uop_AND_IMM(ir, IREG_temp3, IREG_temp3, 0x0a);
        uop_CMP_IMM_JZ(ir, IREG_temp3, 0x08, codegen_gpf_rout);
        return;
    }
    if (seg->checked)
        return;
    if (seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS))
        return;
    if (seg == &cpu_state.seg_es && codegen_flat_es && !(cpu_cur_status & CPU_STATUS_NOTFLATES))
        return;

    uop_CMP_IMM_JZ(ir, ireg_seg_base(seg), (uint32_t) -1, codegen_gpf_rout);

    seg->checked = 1;
}
/*Writable data only: a code segment (CS above all) or read-only data is
  #GP(0), as in the interpreter's CHECK_WRITE. Checked at run time, since
  the segment can be reloaded without recompiling the block*/
static void
codegen_check_seg_writable(ir_data_t *ir, x86seg *seg)
{
    int jump_uop;

    uop_MOVZX_REG_PTR_8(ir, IREG_temp3, &seg->access);
    uop_AND_IMM(ir, IREG_temp3, IREG_temp3, 0x0a);
    jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp3, 0x02);
    uop_JMP(ir, codegen_gpf_rout);
    uop_set_jump_dest(ir, jump_uop);
}

void
codegen_check_seg_write(UNUSED(codeblock_t *block), ir_data_t *ir, x86seg *seg)
{
    /*Segments always valid in real/V86 mode*/
    if (!(cr0 & 1) || (cpu_state.eflags & VM_FLAG))
        return;
    /*SS only ever holds writable data*/
    if (seg == &cpu_state.seg_ss)
        return;
    /*CS is never writable; its checked flag isn't reset per block, so no caching*/
    if (seg == &cpu_state.seg_cs) {
        codegen_check_seg_writable(ir, seg);
        return;
    }
    /*checked: 1 = null selector ruled out, 2 = writable too*/
    if (seg->checked == 2)
        return;
    if (!seg->checked && !(seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) && !(seg == &cpu_state.seg_es && codegen_flat_es && !(cpu_cur_status & CPU_STATUS_NOTFLATES)))
        uop_CMP_IMM_JZ(ir, ireg_seg_base(seg), (uint32_t) -1, codegen_gpf_rout);

    codegen_check_seg_writable(ir, seg);

    seg->checked = 2;
}

static x86seg *
codegen_generate_ea_16_long(ir_data_t *ir, x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc)
{
    codeblock_t *block  = ir->block;
    uint32_t     old_pc = (*op_pc) + 1;
    /*In a block rewritten again and again (CODEBLOCK_NO_IMMEDIATES) the
      displacement is read from RAM as the block runs, and not marked: a
      write to it doesn't throw the block away*/
    int          dyn    = !!(block->flags & CODEBLOCK_NO_IMMEDIATES);

    if (!cpu_mod && cpu_rm == 6) {
        if (dyn)
            LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_eaaddr, cs + old_pc);
        else
            uop_MOV_IMM(ir, IREG_eaaddr, (fetchdat >> 8) & 0xffff);
        (*op_pc) += 2;
    } else {
        int base_reg;
        int index_reg;
        int offset;

        switch (cpu_rm & 7) {
            default:
            case 0:
            case 1:
            case 7:
                base_reg = IREG_EBX;
                break;
            case 2:
            case 3:
            case 6:
                base_reg = IREG_EBP;
                break;
            case 4:
                base_reg = IREG_ESI;
                break;
            case 5:
                base_reg = IREG_EDI;
                break;
        }
        uop_MOV(ir, IREG_eaaddr, base_reg);

        if (!(cpu_rm & 4)) {
            if (!(cpu_rm & 1))
                index_reg = IREG_ESI;
            else
                index_reg = IREG_EDI;

            uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, index_reg);
        }

        switch (cpu_mod) {
            case 1:
                if (dyn) {
                    LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0, cs + old_pc);
                    uop_MOVSX(ir, IREG_temp0, IREG_temp0_B);
                    uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, IREG_temp0);
                } else {
                    offset = (int) (int8_t) ((fetchdat >> 8) & 0xff);
                    uop_ADD_IMM(ir, IREG_eaaddr, IREG_eaaddr, offset);
                }
                (*op_pc)++;
                break;
            case 2:
                if (dyn) {
                    LOAD_IMMEDIATE_FROM_RAM_16(block, ir, IREG_temp0, cs + old_pc);
                    uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, IREG_temp0);
                } else {
                    offset = (fetchdat >> 8) & 0xffff;
                    uop_ADD_IMM(ir, IREG_eaaddr, IREG_eaaddr, offset);
                }
                (*op_pc) += 2;
                break;

            default:
                break;
        }

        uop_AND_IMM(ir, IREG_eaaddr, IREG_eaaddr, 0xffff);

        if (mod1seg[cpu_rm] == &ss && !op_ssegs) {
            op_ea_seg = &cpu_state.seg_ss;
        }
    }

    if (!dyn)
        codegen_mark_code_present(block, cs + old_pc, ((*op_pc) + 1) - old_pc);
    return op_ea_seg;
}

static x86seg *
codegen_generate_ea_32_long(ir_data_t *ir, x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, int stack_offset)
{
    codeblock_t *block  = ir->block;
    uint32_t     old_pc = (*op_pc) + 1;
    uint32_t     new_eaaddr;
    int          extra_bytes = 0;

    if (cpu_rm == 4) {
        uint8_t sib = fetchdat >> 8;
        (*op_pc)++;

        switch (cpu_mod) {
            case 0:
                if ((sib & 7) == 5) {
                    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_eaaddr, cs + (*op_pc) + 1);
                        extra_bytes = 1;
                    } else {
                        new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                        uop_MOV_IMM(ir, IREG_eaaddr, new_eaaddr);
                        extra_bytes = 5;
                    }
                    (*op_pc) += 4;
                } else {
                    uop_MOV(ir, IREG_eaaddr, sib & 7);
                    extra_bytes = 1;
                }
                break;
            case 1:
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0, cs + (*op_pc) + 1);
                    uop_MOVSX(ir, IREG_eaaddr, IREG_temp0_B);
                    extra_bytes = 1;
                } else {
                    new_eaaddr = (uint32_t) (int8_t) ((fetchdat >> 16) & 0xff);
                    uop_MOV_IMM(ir, IREG_eaaddr, new_eaaddr);
                    extra_bytes = 2;
                }
                uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, sib & 7);
                (*op_pc)++;
                break;
            case 2:
                if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                    LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_eaaddr, cs + (*op_pc) + 1);
                    extra_bytes = 1;
                } else {
                    new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                    uop_MOV_IMM(ir, IREG_eaaddr, new_eaaddr);
                    extra_bytes = 5;
                }
                (*op_pc) += 4;
                uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, sib & 7);
                break;

            default:
                break;
        }
        if (stack_offset && (sib & 7) == 4 && (cpu_mod || (sib & 7) != 5)) { /*ESP*/
            uop_ADD_IMM(ir, IREG_eaaddr, IREG_eaaddr, stack_offset);
#if 0
            addbyte(0x05);
            addlong(stack_offset);
#endif
        }
        if (((sib & 7) == 4 || (cpu_mod && (sib & 7) == 5)) && !op_ssegs)
            op_ea_seg = &cpu_state.seg_ss;
        if (((sib >> 3) & 7) != 4) {
            switch (sib >> 6) {
                case 0:
                    uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, (sib >> 3) & 7);
                    break;
                case 1:
                    uop_ADD_LSHIFT(ir, IREG_eaaddr, IREG_eaaddr, (sib >> 3) & 7, 1);
                    break;
                case 2:
                    uop_ADD_LSHIFT(ir, IREG_eaaddr, IREG_eaaddr, (sib >> 3) & 7, 2);
                    break;
                case 3:
                    uop_ADD_LSHIFT(ir, IREG_eaaddr, IREG_eaaddr, (sib >> 3) & 7, 3);
                    break;

                default:
                    break;
            }
        }
    } else {
        if (!cpu_mod && cpu_rm == 5) {
            if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_eaaddr, cs + (*op_pc) + 1);
            } else {
                new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                uop_MOV_IMM(ir, IREG_eaaddr, new_eaaddr);
                extra_bytes = 4;
            }

            (*op_pc) += 4;
        } else {
            uop_MOV(ir, IREG_eaaddr, cpu_rm);
            if (cpu_mod) {
                if (cpu_rm == 5 && !op_ssegs)
                    op_ea_seg = &cpu_state.seg_ss;
                if (cpu_mod == 1) {
                    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                        LOAD_IMMEDIATE_FROM_RAM_8(block, ir, IREG_temp0, cs + (*op_pc) + 1);
                        uop_MOVSX(ir, IREG_temp0, IREG_temp0_B);
                        uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, IREG_temp0);
                    } else {
                        uop_ADD_IMM(ir, IREG_eaaddr, IREG_eaaddr, (uint32_t) (int8_t) (fetchdat >> 8));
                        extra_bytes = 1;
                    }
                    (*op_pc)++;
                } else {
                    if (block->flags & CODEBLOCK_NO_IMMEDIATES) {
                        LOAD_IMMEDIATE_FROM_RAM_32(block, ir, IREG_temp0, cs + (*op_pc) + 1);
                        uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, IREG_temp0);
                    } else {
                        new_eaaddr = fastreadl(cs + (*op_pc) + 1);
                        uop_ADD_IMM(ir, IREG_eaaddr, IREG_eaaddr, new_eaaddr);
                        extra_bytes = 4;
                    }
                    (*op_pc) += 4;
                }
            }
        }
    }

    if (extra_bytes)
        codegen_mark_code_present(ir->block, cs + old_pc, extra_bytes);

    return op_ea_seg;
}

/* The segment of the last memory operand codegen_generate_ea() formed, for
   the FPU's data pointer (fpu_postamble) after a compiled instruction. */
static x86seg *codegen_ea_target_seg;

x86seg *
codegen_generate_ea(ir_data_t *ir, x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, uint32_t op_32, int stack_offset)
{
    cpu_mod = (fetchdat >> 6) & 3;
    cpu_reg = (fetchdat >> 3) & 7;
    cpu_rm  = fetchdat & 7;

    if ((fetchdat & 0xc0) == 0xc0)
        return NULL;
    if (op_32 & 0x200)
        codegen_ea_target_seg = codegen_generate_ea_32_long(ir, op_ea_seg, fetchdat, op_ssegs, op_pc, stack_offset);
    else
        codegen_ea_target_seg = codegen_generate_ea_16_long(ir, op_ea_seg, fetchdat, op_ssegs, op_pc);

    return codegen_ea_target_seg;
}

// clang-format off
static uint8_t opcode_modrm[256] = {
    1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0, /*00*/
    1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0, /*10*/
    1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0, /*20*/
    1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0, /*30*/

    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*40*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*50*/
    0, 0, 1, 1,  0, 0, 0, 0,  0, 1, 0, 1,  0, 0, 0, 0, /*60*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*70*/

    1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, /*80*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*90*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*a0*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*b0*/

    1, 1, 0, 0,  1, 1, 1, 1,  0, 0, 0, 0,  0, 0, 0, 0, /*c0*/
    1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  1, 1, 1, 1, /*d0*/
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*e0*/
    0, 0, 0, 0,  0, 0, 1, 1,  0, 0, 0, 0,  0, 0, 1, 1, /*f0*/
};

static uint8_t opcode_0f_modrm[256] = {
    1, 1, 1, 1,  0, 0, 0, 0,  0, 0, 0, 0,  0, 1, 0, 1, /*00*/
    1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  1, 1, 1, 1, /*10*/
    1, 1, 1, 1,  1, 1, 1, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*20*/
    0, 0, 0, 0,  0, 0, 1, 1,  0, 0, 0, 0,  0, 0, 0, 1, /*30*/

    1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, /*40*/
    1, 1, 1, 0,  1, 1, 0, 0,  1, 1, 1, 1,  1, 1, 1, 0, /*50*/
    1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  0, 0, 1, 1, /*60*/
    0, 1, 1, 1,  1, 1, 1, 0,  1, 1, 1, 1,  1, 1, 1, 1, /*70*/

    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, /*80*/
    1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, /*90*/
    0, 0, 0, 1,  1, 1, 0, 0,  0, 0, 0, 1,  1, 1, 1, 1, /*a0*/
    1, 1, 1, 1,  1, 1, 1, 1,  0, 0, 1, 1,  1, 1, 1, 1, /*b0*/

    1, 1, 0, 0,  0, 0, 0, 1,  0, 0, 0, 0,  0, 0, 0, 0, /*c0*/
    0, 1, 1, 1,  0, 1, 0, 0,  1, 1, 0, 1,  1, 1, 0, 1, /*d0*/
    0, 1, 1, 0,  0, 1, 0, 0,  1, 1, 0, 1,  1, 1, 0, 1, /*e0*/
    0, 1, 1, 1,  0, 1, 0, 0,  1, 1, 1, 0,  1, 1, 1, 0  /*f0*/
};
// clang-format on

/* ES set: an unmasked exception of the legacy FPU is pending, taken before
   a waiting x87 or an MMX instruction, as the interpreter takes it
   (x87_legacy_pending_entry). ES is tested inline; the helper is called
   only when it is set. */
void
codegen_fp_pending_check(ir_data_t *ir)
{
    int jump_uop;

    /* The double-based FPU's status word: softfloat's, and the host-x87
       FPU's, take a pending exception in their handlers (the compiled
       host-x87 instructions go to the handler when ES is set). */
    if (fpu_softfloat)
        return;

    uop_MOVZX(ir, IREG_temp0, IREG_NPXS);
    uop_AND_IMM(ir, IREG_temp0, IREG_temp0, 0x80);
    jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_CALL_FUNC_RESULT(ir, IREG_temp0, codegen_fp_pending);
    uop_CMP_IMM_JZ(ir, IREG_temp0, 1, codegen_exit_rout);
    uop_set_jump_dest(ir, jump_uop);
}

/* The x87 instruction being compiled, as the interpreter runs it (set
   before its handler is called), for a case its compiled form hands over */
OpFn codegen_fpu_handler;

/* A store from ST(0) when it is empty, a stack underflow: the interpreter's
   function does it (x87_empty_store, opFST: IE and SF, the indefinite
   stored when masked) and the block ends past the instruction. interp_pc
   is where that function starts (it steps past the ModR/M byte itself); a
   memory operand's address is in IREG_eaaddr already, in target_seg. */
/* The interpreter's version of the x87 instruction being compiled, then
   the block ends past it: for a case the compiled form hands over. interp_pc
   is where that function starts (it steps past the ModR/M byte itself); a
   memory operand's address is in IREG_eaaddr already, in target_seg. */
static void
codegen_fpu_handover(ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t interp_pc, x86seg *target_seg)
{
    uop_MOV_IMM(ir, IREG_rm_mod_reg, (fetchdat & 7) | (((fetchdat >> 6) & 3) << 8) | (((fetchdat >> 3) & 7) << 16));
    uop_MOV_IMM(ir, IREG_pc, interp_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    if (target_seg)
        uop_MOV_PTR(ir, IREG_ea_seg, (void *) target_seg);
    uop_CALL_INSTRUCTION_FUNC(ir, codegen_fpu_handler, fetchdat);
    uop_JMP(ir, codegen_exit_rout);
}

/* A store from ST(0) when it is empty, a stack underflow: the interpreter
   does it (x87_empty_store, opFST: IE and SF, the indefinite stored when
   masked) */
void
codegen_fpu_guard_empty(ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t interp_pc, x86seg *target_seg)
{
    int jump_uop;

    codegen_accumulate_flush(ir);
    uop_MOVZX(ir, IREG_temp0, IREG_tag_B(0));
    jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, TAG_EMPTY);
    codegen_fpu_handover(ir, fetchdat, op_32, interp_pc, target_seg);
    uop_set_jump_dest(ir, jump_uop);
}

/* FIST and FISTP: compiled only for lo < ST(0) < hi, which rounds into the
   integer's range in any rounding mode; a NaN (unordered), or a value at
   or past a bound, goes to the interpreter, which raises the invalid
   operation and stores the integer indefinite (x87_fist_invalid). bounds:
   lo and hi. */
void
codegen_fpu_guard_int_range(ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t interp_pc, x86seg *target_seg, const double *bounds)
{
    int jump_uop;

    codegen_accumulate_flush(ir);
    uop_MOV_REG_PTR(ir, IREG_temp1_D, (void *) &bounds[1]);
    uop_FCOM(ir, IREG_temp1_W, IREG_ST(0), IREG_temp1_D); /* C0 alone: below hi */
    uop_MOV_REG_PTR(ir, IREG_temp2_D, (void *) &bounds[0]);
    uop_FCOM(ir, IREG_temp2_W, IREG_ST(0), IREG_temp2_D); /* none: above lo */
    uop_MOVZX(ir, IREG_temp1, IREG_temp1_W);
    uop_XOR_IMM(ir, IREG_temp1, IREG_temp1, FPU_SW_C0);
    uop_MOVZX(ir, IREG_temp2, IREG_temp2_W);
    uop_OR(ir, IREG_temp1, IREG_temp1, IREG_temp2);
    jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp1, 0);
    codegen_fpu_handover(ir, fetchdat, op_32, interp_pc, target_seg);
    uop_set_jump_dest(ir, jump_uop);
}

/*Escapes the interpreter runs every time: softfloat, no FPU fitted (#NM or
  nothing), and an 8087 or 287, whose projective infinities (IC clear) the
  compiled arithmetic and compares don't know*/
static int
codegen_x87_interpreted(void)
{
    return fpu_softfloat || !hasfpu || (fpu_type < FPU_287XL);
}

/* FIST(P) of a word or doubleword on the x86-64 backend: its conversion
   gives the integer indefinite (8000h, 80000000h) for a NaN or a value out
   of range, as the chip stores with IM masked. Converted first, that value
   hands over to the interpreter, which decides the exception; a true
   -32768 or -2^31 gets there too, and stores the same. */
void
codegen_fpu_guard_int_indefinite(ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t interp_pc, x86seg *target_seg, int src_reg, uint32_t indefinite)
{
    int jump_uop;

    codegen_accumulate_flush(ir);
    if (indefinite == 0x8000) {
        uop_MOVZX(ir, IREG_temp1, src_reg);
        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp1, indefinite);
    } else
        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, src_reg, indefinite);
    codegen_fpu_handover(ir, fetchdat, op_32, interp_pc, target_seg);
    uop_set_jump_dest(ir, jump_uop);
}

/* The compiled FPU arithmetic is built for the control word's precision
   (PC24) and rounding (RC != nearest) classes as they are when it is
   compiled: at the block's start by its key, and after an interpreted
   instruction that may have loaded another control word by a check in the
   first compiled instruction that depends on them, which leaves the block
   before it if they changed. FLDCW, FISTP, FLDCW (_ftol) needs none: FIST
   takes the rounding when it runs. Returns 0 for RC other than nearest,
   left to the interpreter. */
int             codegen_fpu_class_dirty;
static uint32_t codegen_instr_pc;

int
codegen_fpu_rc_nearest(ir_data_t *ir)
{
    int jump_uop;

    if ((cpu_state.npxc >> 10) & 3)
        return 0;
    if (!codegen_fpu_class_dirty)
        return 1;
    codegen_fpu_class_dirty = 0;
    codegen_accumulate_flush(ir);
    uop_MOV_IMM(ir, IREG_pc, codegen_instr_pc);
    uop_MOVZX_REG_PTR_16(ir, IREG_temp0, &cpu_state.npxc);
    if (!(cpu_state.npxc & 0x300)) {
        uop_AND_IMM(ir, IREG_temp0, IREG_temp0, 0xf00);
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
    } else {
        uop_AND_IMM(ir, IREG_temp1, IREG_temp0, 0xc00);
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp1, 0);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        uop_AND_IMM(ir, IREG_temp1, IREG_temp0, 0x300);
        uop_CMP_IMM_JZ(ir, IREG_temp1, 0, codegen_exit_rout);
    }
    return 1;
}

void
codegen_generate_call(uint8_t opcode, OpFn op, uint32_t fetchdat, uint32_t new_pc, uint32_t old_pc)
{
    codeblock_t *block              = &codeblock[block_current];
    ir_data_t   *ir                 = codegen_get_ir_data();
    uint32_t     op_pc              = new_pc;
    const OpFn  *op_table           = x86_dynarec_opcodes;
    RecompOpFn  *recomp_op_table    = recomp_opcodes;
    int          opcode_shift       = 0;
    int          opcode_mask        = 0x3ff;
    uint32_t     recomp_opcode_mask = 0x1ff;
    uint32_t     op_32              = use32;
    int          over               = 0;
    int          test_modrm         = 1;
    int          pc_off             = 0;
    int          in_lock            = 0;
    int          lock_illegal       = 0;
    int          is_fpu             = 0;
    uint32_t     next_pc            = 0;
    uint16_t     op87               = 0x0000;
    uint32_t     fetchdat_pc;
#ifdef DEBUG_EXTRA
    uint8_t last_prefix = 0;
#endif
    op_ea_seg = &cpu_state.seg_ds;
    op_ssegs  = 0;

    codegen_timing_start();

    while (!over) {
        switch (opcode) {
            case 0x0f:
#ifdef DEBUG_EXTRA
                last_prefix = 0x0f;
#endif
                op_table        = x86_dynarec_opcodes_0f;
                recomp_op_table = fpu_softfloat ? recomp_opcodes_0f_no_mmx : recomp_opcodes_0f;
                over            = 1;
                is_fpu          = 0;
                break;

            case 0x26: /*ES:*/
                op_ea_seg = &cpu_state.seg_es;
                op_ssegs  = 1;
                break;
            case 0x2e: /*CS:*/
                op_ea_seg = &cpu_state.seg_cs;
                op_ssegs  = 1;
                break;
            case 0x36: /*SS:*/
                op_ea_seg = &cpu_state.seg_ss;
                op_ssegs  = 1;
                break;
            case 0x3e: /*DS:*/
                op_ea_seg = &cpu_state.seg_ds;
                op_ssegs  = 1;
                break;
            case 0x64: /*FS:*/
                op_ea_seg = &cpu_state.seg_fs;
                op_ssegs  = 1;
                break;
            case 0x65: /*GS:*/
                op_ea_seg = &cpu_state.seg_gs;
                op_ssegs  = 1;
                break;

            case 0x66: /*Data size select*/
                op_32 = ((use32 & 0x100) ^ 0x100) | (op_32 & 0x200);
                break;
            case 0x67: /*Address size select*/
                op_32 = ((use32 & 0x200) ^ 0x200) | (op_32 & 0x100);
                break;

            case 0xd8:
#ifdef DEBUG_EXTRA
                last_prefix = 0xd8;
#endif
                x87_op          = ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op87            = (op87 & 0xf800) | ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_d8_a32 : x86_dynarec_opcodes_d8_a16;
                recomp_op_table = codegen_x87_interpreted() ? NULL : recomp_opcodes_d8;
                opcode_shift    = 3;
                opcode_mask     = 0x1f;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                is_fpu          = 1;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xd9:
#ifdef DEBUG_EXTRA
                last_prefix = 0xd9;
#endif
                x87_op          = ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op87            = (op87 & 0xf800) | ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_d9_a32 : x86_dynarec_opcodes_d9_a16;
                recomp_op_table = codegen_x87_interpreted() ? NULL : recomp_opcodes_d9;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                is_fpu          = 1;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xda:
#ifdef DEBUG_EXTRA
                last_prefix = 0xda;
#endif
                x87_op          = ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op87            = (op87 & 0xf800) | ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_da_a32 : x86_dynarec_opcodes_da_a16;
                recomp_op_table = codegen_x87_interpreted() ? NULL : recomp_opcodes_da;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                is_fpu          = 1;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xdb:
#ifdef DEBUG_EXTRA
                last_prefix = 0xdb;
#endif
                x87_op          = ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op87            = (op87 & 0xf800) | ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_db_a32 : x86_dynarec_opcodes_db_a16;
                recomp_op_table = codegen_x87_interpreted() ? NULL : recomp_opcodes_db;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                is_fpu          = 1;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xdc:
#ifdef DEBUG_EXTRA
                last_prefix = 0xdc;
#endif
                x87_op          = ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op87            = (op87 & 0xf800) | ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_dc_a32 : x86_dynarec_opcodes_dc_a16;
                recomp_op_table = codegen_x87_interpreted() ? NULL : recomp_opcodes_dc;
                opcode_shift    = 3;
                opcode_mask     = 0x1f;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                is_fpu          = 1;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xdd:
#ifdef DEBUG_EXTRA
                last_prefix = 0xdd;
#endif
                x87_op          = ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op87            = (op87 & 0xf800) | ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_dd_a32 : x86_dynarec_opcodes_dd_a16;
                recomp_op_table = codegen_x87_interpreted() ? NULL : recomp_opcodes_dd;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                is_fpu          = 1;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xde:
#ifdef DEBUG_EXTRA
                last_prefix = 0xde;
#endif
                x87_op          = ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op87            = (op87 & 0xf800) | ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_de_a32 : x86_dynarec_opcodes_de_a16;
                recomp_op_table = codegen_x87_interpreted() ? NULL : recomp_opcodes_de;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                is_fpu          = 1;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;
            case 0xdf:
#ifdef DEBUG_EXTRA
                last_prefix = 0xdf;
#endif
                x87_op          = ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op87            = (op87 & 0xf800) | ((opcode & 0x07) << 8) | (fetchdat & 0xff);
                op_table        = (op_32 & 0x200) ? x86_dynarec_opcodes_df_a32 : x86_dynarec_opcodes_df_a16;
                recomp_op_table = codegen_x87_interpreted() ? NULL : recomp_opcodes_df;
                opcode_mask     = 0xff;
                over            = 1;
                pc_off          = -1;
                test_modrm      = 0;
                is_fpu          = 1;
                block->flags |= CODEBLOCK_HAS_FPU;
                break;

            case 0xf0: /*LOCK*/
                in_lock         = 1;
                break;

            case 0xf2: /*REPNE*/
#ifdef DEBUG_EXTRA
                last_prefix = 0xf2;
#endif
                op_table        = x86_dynarec_opcodes_REPNE;
                recomp_op_table = NULL; // recomp_opcodes_REPNE;
                is_fpu          = 0;
                break;
            case 0xf3: /*REPE*/
#ifdef DEBUG_EXTRA
                last_prefix = 0xf3;
#endif
                op_table        = x86_dynarec_opcodes_REPE;
                recomp_op_table = NULL; // recomp_opcodes_REPE;
                is_fpu          = 0;
                break;

            default:
                goto generate_call;
        }
        fetchdat = fastreadl(cs + op_pc);
        codegen_timing_prefix(opcode, fetchdat);
        if (cpu_state.abrt)
            return;
        opcode = fetchdat & 0xff;
        if (!pc_off)
            fetchdat >>= 8;

        op_pc++;
    }

generate_call:
    /*A REP prefix on an instruction that isn't a string operation (PAUSE,
      REP RET, a branch hint or BND on Jcc) is ignored: compile it as the
      plain instruction, and classify it and flush its branch cycles as
      one (the timing modules follow the prefixes themselves)*/
    if ((op_table == x86_dynarec_opcodes_REPNE || op_table == x86_dynarec_opcodes_REPE) && !op_table[opcode | op_32]) {
        op_table        = x86_dynarec_opcodes;
        recomp_op_table = recomp_opcodes;
    }

    fetchdat_pc = op_pc + pc_off;
    codegen_instructions[block->ins].pc        = cpu_state.oldpc;
    codegen_instructions[block->ins].op_ssegs  = last_op_ssegs;
    codegen_instructions[block->ins].op_ea_seg = last_op_ea_seg;
    codegen_instructions[block->ins].op_32     = last_op_32;
    codegen_instructions[block->ins].TOP       = cpu_state.TOP;
    codegen_instructions[block->ins].first_uop = ir->wr_pos;

    codegen_timing_opcode(opcode, fetchdat, op_32, op_pc);

    codegen_accumulate(ir, ACCREG_cycles, -codegen_block_cycles);
    codegen_block_cycles = 0;

    if ((op_table == x86_dynarec_opcodes && ((opcode & 0xf0) == 0x70 || (opcode & 0xfc) == 0xe0 || opcode == 0xc2 || (opcode & 0xfe) == 0xca || (opcode & 0xfc) == 0xcc || (opcode & 0xfc) == 0xe8 || (opcode == 0xff && ((fetchdat & 0x38) >= 0x10 && (fetchdat & 0x38) < 0x30)))) || (op_table == x86_dynarec_opcodes_0f && ((opcode & 0xf0) == 0x80))) {
        /*On some CPUs (eg K6), a jump/branch instruction may be able to pair with
          subsequent instructions, so no cycles may have been deducted for it yet.
          To prevent having zero cycle blocks (eg with a jump instruction pointing
          to itself), apply the cycles that would be taken if this jump is taken,
          then reverse it for subsequent instructions if the jump is not taken*/
        int jump_cycles = 0;

        if (codegen_timing_jump_cycles)
            jump_cycles = codegen_timing_jump_cycles();

        if (jump_cycles)
            codegen_accumulate(ir, ACCREG_cycles, -jump_cycles);
        codegen_accumulate_flush(ir);
        if (jump_cycles)
            codegen_accumulate(ir, ACCREG_cycles, jump_cycles);
    }

    /*Only on a processor with 3DNow!: on the others 0F 0F is ILLEGAL in its
      own table, and the check below leaves it to the interpreter's #UD*/
    if (op_table == x86_dynarec_opcodes_0f && opcode == 0x0f && op_table[((opcode >> opcode_shift) | op_32) & opcode_mask] != dynarec_op_illegal) {
        /*3DNow opcodes are stored after ModR/M, SIB and any offset*/
        uint8_t  modrm     = fetchdat & 0xff;
        uint8_t  sib       = (fetchdat >> 8) & 0xff;
        uint32_t opcode_pc = op_pc + 1;
        uint8_t  opcode_3dnow;

        if ((modrm & 0xc0) != 0xc0) {
            if (op_32 & 0x200) {
                if ((modrm & 7) == 4) {
                    /* Has SIB*/
                    opcode_pc++;
                    if ((modrm & 0xc0) == 0x40)
                        opcode_pc++;
                    else if ((modrm & 0xc0) == 0x80)
                        opcode_pc += 4;
                    else if ((sib & 0x07) == 0x05)
                        opcode_pc += 4;
                } else {
                    if ((modrm & 0xc0) == 0x40)
                        opcode_pc++;
                    else if ((modrm & 0xc0) == 0x80)
                        opcode_pc += 4;
                    else if ((modrm & 0xc7) == 0x05)
                        opcode_pc += 4;
                }
            } else {
                if ((modrm & 0xc0) == 0x40)
                    opcode_pc++;
                else if ((modrm & 0xc0) == 0x80)
                    opcode_pc += 2;
                else if ((modrm & 0xc7) == 0x06)
                    opcode_pc += 2;
            }
        }

        opcode_3dnow = fastreadb(cs + opcode_pc);
        codegen_mark_code_present(block, cs + opcode_pc, 1);
        if (!fpu_softfloat && recomp_opcodes_3DNOW[opcode_3dnow]) {
            next_pc = opcode_pc + 1;

            op_table           = x86_dynarec_opcodes_3DNOW;
            recomp_op_table    = fpu_softfloat ? NULL : recomp_opcodes_3DNOW;
            opcode             = opcode_3dnow;
            recomp_opcode_mask = 0xff;
            opcode_mask        = 0xff;
        }
    }
    /*The prefixes and the opcode; an x87 escape's ModR/M too, as op_pc is
      already past it (pc_off is -1 for those only while op_pc is moved back
      to the ModR/M for the EA below)*/
    codegen_mark_code_present(block, cs + old_pc, op_pc - old_pc);
    /* Every escape stores its opcode, D8 00 (FADD m32 by [BX+SI] or [EAX])
       included: the handlers and the FERR# checks read it */
    if (is_fpu) {
        uop_MOV_IMM(ir, IREG_x87_op, op87);
    }

    /* It is apparently a prefixed instruction. */
#if 0
    if ((recomp_op_table == recomp_opcodes) && (opcode == 0x48))
        goto codegen_skip;
#endif
    /*A LOCK the instruction can't take is #UD: leave it to the interpreter's*/
    if (in_lock) {
        if (op_table == x86_dynarec_opcodes_0f)
            lock_illegal = !x86_lock_legal(1, opcode, fetchdat & 0xff);
        else if ((op_table == x86_dynarec_opcodes) || (op_table == x86_dynarec_opcodes_REPE) || (op_table == x86_dynarec_opcodes_REPNE))
            lock_illegal = !x86_lock_legal(0, opcode, fetchdat & 0xff);
        else
            lock_illegal = 1;
    }
    if (lock_illegal)
        goto codegen_skip;

    /* The recompiler tables are shared by every CPU; an opcode this one
       doesn't have (BSWAP on a 386, MMX on a 486) goes to the interpreter,
       which raises #UD. */
    if (op_table[((opcode >> opcode_shift) | op_32) & opcode_mask] == dynarec_op_illegal)
        goto codegen_skip;
    /*The compiled MMX rows (0F 60-7F, D0-FF) only on a CPU with MMX: without
      it the opcodes are #UD, or something else entirely (the Cyrix 6x86's
      SMM instructions at 0F 78-7E), which only its own table knows*/
    if ((op_table == x86_dynarec_opcodes_0f) && !cpu_has_feature(CPU_FEATURE_MMX) && (((opcode & 0xe0) == 0x60) || (opcode >= 0xd0)))
        goto codegen_skip;

    /* So does an x87 escape this FPU doesn't have: the compiled forms are
       the 387's, and the 8087 has no FUCOM, FUCOMPP or FNSTSW AX, the 287 no
       FUCOM or FUCOMPP. */
    if (is_fpu && ((op_table[((opcode >> opcode_shift) | op_32) & opcode_mask] == dynarec_fpu_illegal_a16) ||
                   (op_table[((opcode >> opcode_shift) | op_32) & opcode_mask] == dynarec_fpu_illegal_a32)))
        goto codegen_skip;

    /* A no-wait instruction asserts FERR# for an error not yet reported, and
       goes on (x87_ferr_nowait) */
    if (is_fpu && x87_op_is_nowait(x87_op)) {
        uop_LOAD_FUNC_ARG_IMM(ir, 0, x87_op);
        uop_CALL_FUNC(ir, x87_ferr_nowait);
    }

    /* A pending exception is taken only with ES set: the call only then */
    if (is_fpu && !fpu_softfloat && !x87_op_is_nowait(x87_op) && !codegen_fpu_entered) {
        int jump_uop;

        uop_MOVZX(ir, IREG_temp3, IREG_NPXS);
        uop_AND_IMM(ir, IREG_temp3, IREG_temp3, FPU_SW_Summary);
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp3, 0);
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        uop_CALL_FUNC_RESULT(ir, IREG_temp3, x87_legacy_pending_entry);
        uop_CMP_IMM_JZ(ir, IREG_temp3, 1, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
    }

    if (recomp_op_table && recomp_op_table[(opcode | op_32) & recomp_opcode_mask]) {
        uint32_t new_pc;

        if (is_fpu)
            codegen_fpu_handler = op_table[((opcode >> opcode_shift) | op_32) & opcode_mask];
        codegen_ea_target_seg = NULL;
        codegen_instr_pc      = old_pc;
        new_pc = recomp_op_table[(opcode | op_32) & recomp_opcode_mask](block, ir, opcode, fetchdat, op_32, op_pc);
        /* The FPU's instruction and data pointers and opcode, for a compiled
           instruction: not for a control instruction (FNSTSW AX here), which
           leaves the last one's, and not for one the recompiler left alone,
           whose handler call below does them after it runs. The data
           pointer's segment is the operand's own, which the block need not
           have set for a handler. */
        if (new_pc && cpu_dyn_accurate_fpu_env && is_fpu) {
            if (!x87_op_is_control(x87_op)) {
                uop_MOV_IMM(ir, IREG_fpu_op, x87_op);
                uop_MOV_IMM(ir, IREG_fpu_CS, cpu_state.temp_CS);
                uop_MOV_IMM(ir, IREG_fpu_cs, cpu_state.temp_cs);
                uop_MOV_IMM(ir, IREG_fpu_pc, cpu_state.temp_pc);
                if ((x87_op & 0xff) < 0xc0) {
                    if (codegen_ea_target_seg != NULL) {
                        uop_MOV_PTR(ir, IREG_ea_seg, (void *) codegen_ea_target_seg);
                        last_op_ea_seg = codegen_ea_target_seg;
                    }
                    uop_CALL_FUNC(ir, fpu_postamble);
                }
            }
        }
        if (new_pc) {
            if (new_pc != -1)
                uop_MOV_IMM(ir, IREG_pc, new_pc);

            codegen_endpc = (cs + cpu_state.pc) + 8;

            block->ins++;

            if (block->ins >= MAX_INSTRUCTION_COUNT)
                CPU_BLOCK_END();

            return;
        }
    }

codegen_skip:

    if (lock_illegal)
        op = dynarec_op_illegal;
    else
        op = op_table[((opcode >> opcode_shift) | op_32) & opcode_mask];

    if (!test_modrm || (op_table == x86_dynarec_opcodes && opcode_modrm[opcode]) || (op_table == x86_dynarec_opcodes_0f && opcode_0f_modrm[opcode]) || (op_table == x86_dynarec_opcodes_3DNOW)) {
        int stack_offset = 0;

        if (op_table == x86_dynarec_opcodes && opcode == 0x8f) /*POP*/
            stack_offset = (op_32 & 0x100) ? 4 : 2;

        cpu_mod = (fetchdat >> 6) & 3;
        cpu_reg = (fetchdat >> 3) & 7;
        cpu_rm  = fetchdat & 7;

        uop_MOV_IMM(ir, IREG_rm_mod_reg, cpu_rm | (cpu_mod << 8) | (cpu_reg << 16));

        op_pc += pc_off;
        if (cpu_mod != 3 && !(op_32 & 0x200)) {
            op_ea_seg = codegen_generate_ea_16_long(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc);
        }
        if (cpu_mod != 3 && (op_32 & 0x200)) {
            op_ea_seg = codegen_generate_ea_32_long(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, stack_offset);
        }
        op_pc -= pc_off;
    }

#ifdef DEBUG_EXTRA
    uop_LOG_INSTR(ir, opcode | (last_prefix << 8));
#endif
    codegen_accumulate_flush(ir);
    if (op_table == x86_dynarec_opcodes_3DNOW)
        uop_MOV_IMM(ir, IREG_pc, next_pc);
    else
        uop_MOV_IMM(ir, IREG_pc, op_pc + pc_off);
    uop_MOV_IMM(ir, IREG_oldpc, old_pc);
    if (op_32 != last_op_32)
        uop_MOV_IMM(ir, IREG_op32, op_32);
    if (op_ea_seg != last_op_ea_seg)
        uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    if (op_ssegs != last_op_ssegs)
        uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, op, fetchdat);
    /*An interpreted x87 or 0F instruction can undo what the block took as
      done (FNINIT, FRSTOR, FXRSTOR and FEMMS empty the tags and leave MMX
      mode, CLTS changes CR0): the next compiled FPU or MMX instruction
      checks and enters again. EMMS then MOVQ read a stale tag word before.*/
    if (is_fpu || (op_table == x86_dynarec_opcodes_0f) || (op_table == x86_dynarec_opcodes_3DNOW)) {
        codegen_fpu_entered = 0;
        codegen_mmx_entered = 0;

        /*It may have loaded another control word (FLDCW, FLDENV, FRSTOR,
          FNINIT, FXRSTOR): the next compiled instruction built for the
          control word's classes checks them (codegen_fpu_rc_nearest).*/
        codegen_fpu_class_dirty = 1;
    }
    /*No softfloat exception check after the instruction: a pending #MF is
      raised by the next waiting FPU instruction (or WAIT), as the book says,
      and the no-wait ones (FNSTSW, FNSTCW, FNCLEX...) don't raise it*/
    if (cpu_dyn_accurate_fpu_env && is_fpu && !x87_op_is_control(x87_op)) {
        uop_MOV_IMM(ir, IREG_fpu_op, x87_op);
        uop_MOV_IMM(ir, IREG_fpu_CS, cpu_state.temp_CS);
        uop_MOV_IMM(ir, IREG_fpu_cs, cpu_state.temp_cs);
        uop_MOV_IMM(ir, IREG_fpu_pc, cpu_state.temp_pc);
        if ((x87_op & 0xff) < 0xc0)
            uop_CALL_FUNC(ir, fpu_postamble);
    }
    codegen_flags_changed = 0;
    /* The handler gets the bytes after the opcode through fetchdat, and reads
       anything further from memory as it runs. Marking more than fetchdat
       covers would claim the data that often follows an instruction such as
       RET or IRET, and every write to it would throw the block away. */
    {
        int fetch_len = pc_off ? 4 : 3;

        /* A one-byte opcode that reads nothing after it (POPF, CLI, the
           string ops) or only an imm8/rel8 uses no more of fetchdat. */
        if (!pc_off && ((op_table == x86_dynarec_opcodes) || (op_table == x86_dynarec_opcodes_REPE) || (op_table == x86_dynarec_opcodes_REPNE)) &&
            (opcode_length[opcode] < 3))
            fetch_len = opcode_length[opcode] - 1;
        codegen_mark_code_present(block, cs + fetchdat_pc, fetch_len);
    }

    last_op_32     = op_32;
    last_op_ea_seg = op_ea_seg;
    last_op_ssegs  = op_ssegs;
#if 0
    codegen_block_ins++;
#endif

    block->ins++;

    if (block->ins >= MAX_INSTRUCTION_COUNT)
        CPU_BLOCK_END();

    codegen_endpc = (cs + cpu_state.pc) + 8;

#if 0
    if (has_ea)
        fatal("Has EA\n");
#endif
}

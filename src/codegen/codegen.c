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
#include "codegen_ops_arith.h"
#include "codegen_ops_helpers.h"

static struct {
    uint32_t pc;
    int      op_ssegs;
    x86seg  *op_ea_seg;
    uint32_t op_32;
    int      first_uop;
    int      first_timing;
    int      TOP;
    int      fpu_entered;  /*codegen_fpu_entered as the instruction was compiled*/
    int      fpu_es_check; /*codegen_fpu_es_check as the instruction was compiled*/
    int      mmx_entered;  /*codegen_mmx_entered as the instruction was compiled*/
    uint8_t  tag_valid;    /*codegen_fpu_tag_valid as the instruction was compiled*/
    uint8_t  tag_empty;    /*codegen_fpu_tag_empty as the instruction was compiled*/
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

/*The x87 and MMX entry state instruction number ins of the block was
  compiled with*/
void
codegen_get_instruction_entered(int ins, int *fpu_entered, int *fpu_es_check, int *mmx_entered)
{
    *fpu_entered  = codegen_instructions[ins].fpu_entered;
    *fpu_es_check = codegen_instructions[ins].fpu_es_check;
    *mmx_entered  = codegen_instructions[ins].mmx_entered;
}

/*The x87 tags known valid and empty as instruction number ins of the block
  was compiled*/
void
codegen_get_instruction_tags(int ins, uint8_t *tag_valid, uint8_t *tag_empty)
{
    *tag_valid = codegen_instructions[ins].tag_valid;
    *tag_empty = codegen_instructions[ins].tag_empty;
}

/*Set with the address of a MOV SS whose handler compiled it as the first of
  a pair: its op compiles the next instruction into the block before running
  it (x86_ops_mov_seg.h). Good only until the next instruction is compiled*/
int      codegen_mov_ss_pair_valid = 0;
uint32_t codegen_mov_ss_pair_pc    = 0;

/*Room in the block for the instruction being compiled and the one after*/
int
codegen_instruction_room(void)
{
    return (codeblock[block_current].ins + 1) < MAX_INSTRUCTION_COUNT;
}


void
codegen_set_loop_start(ir_data_t *ir, int first_instruction)
{
    uop_MOV_IMM(ir, IREG_op32, codegen_instructions[first_instruction].op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) codegen_instructions[first_instruction].op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, codegen_instructions[first_instruction].op_ssegs);
}

int
codegen_get_loop_timing(int first_instruction)
{
    return codegen_instructions[first_instruction].first_timing;
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
void (*codegen_timing_save)(codegen_timing_state_t *state);
void (*codegen_timing_load)(const codegen_timing_state_t *state);

void
codegen_timing_set(codegen_timing_t *timing)
{
    codegen_timing_start       = timing->start;
    codegen_timing_prefix      = timing->prefix;
    codegen_timing_opcode      = timing->opcode;
    codegen_timing_block_start = timing->block_start;
    codegen_timing_block_end   = timing->block_end;
    codegen_timing_jump_cycles = timing->jump_cycles;
    codegen_timing_save        = timing->save;
    codegen_timing_load        = timing->load;
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
    /*A wide DS is never null (CPU_STATUS_NOTWIDEDS)*/
    if (seg == &cpu_state.seg_ds && codegen_wide_ds && !(cpu_cur_status & CPU_STATUS_NOTWIDEDS))
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
    codegen_exit_stamp(ir, EXIT_FAULT_HELD);
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
    /*A wide DS is writable data, so neither null nor read-only
      (CPU_STATUS_NOTWIDEDS)*/
    if (seg == &cpu_state.seg_ds && codegen_wide_ds && !(cpu_cur_status & CPU_STATUS_NOTWIDEDS))
        return;
    if (!seg->checked && !(seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) && !(seg == &cpu_state.seg_es && codegen_flat_es && !(cpu_cur_status & CPU_STATUS_NOTFLATES)))
        uop_CMP_IMM_JZ(ir, ireg_seg_base(seg), (uint32_t) -1, codegen_gpf_rout);

    codegen_check_seg_writable(ir, seg);

    seg->checked = 2;
}

/* fetchdat holds the ModR/M and the byte after it, and no more after a 0F
   (the interpreter's fetch at the 0F ends there): the EA reads any later
   byte of its displacement from the code itself, as its walk does */
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
            uop_MOV_IMM(ir, IREG_eaaddr, fastreadw(cs + old_pc));
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
                    offset = fastreadw(cs + old_pc);
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
                    new_eaaddr = (uint32_t) (int8_t) fastreadb(cs + (*op_pc) + 1);
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
            if (cpu_cur_status & CPU_STATUS_STACK32)
                uop_ADD_IMM(ir, IREG_eaaddr, IREG_eaaddr, stack_offset);
            else {
                /*The pop wraps SP before the 32-bit EA adds its displacement*/
                uop_MOV(ir, IREG_temp0, IREG_ESP);
                uop_ADD_IMM(ir, IREG_temp0_W, IREG_temp0_W, stack_offset);
                uop_SUB(ir, IREG_temp0, IREG_temp0, IREG_ESP);
                uop_ADD(ir, IREG_eaaddr, IREG_eaaddr, IREG_temp0);
            }
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

/* What the bytes of an EA's displacement on the block's second page add to
   its address. When the interpreter's fetch_ea takes the #PF reading them
   it goes on with 0 for those bytes (a split fastreadl or fastreadw reads
   the page's first byte first, and answers 0 from the faulting read on) and
   forms the rest, registers and segment included: its eaaddr is the
   address less this, 16 bits for 16-bit addressing. modrm is the ModR/M
   byte's address, fetchdat the bytes from there. A constant: a displacement
   not wholly on the first page is compiled in, in every kind of block */
static uint32_t
codegen_ea_disp_part(const codeblock_t *block, uint32_t fetchdat, uint32_t modrm, int a32)
{
    const int mod  = (fetchdat >> 6) & 3;
    const int rm   = fetchdat & 7;
    uint32_t  disp = modrm + 1;
    uint32_t  part = 0;
    int       len  = 0;

    if (a32) {
        if (rm == 4) {
            disp++;
            if (mod == 1)
                len = 1;
            else if ((mod == 2) || (((fetchdat >> 8) & 7) == 5))
                len = 4;
        } else if (mod == 1)
            len = 1;
        else if ((mod == 2) || (rm == 5))
            len = 4;
    } else if (mod == 1)
        len = 1;
    else if ((mod == 2) || (rm == 6))
        len = 2;

    for (int c = 0; c < len; c++) {
        if (((disp + c) ^ block->pc) & ~0xfff)
            part |= (uint32_t) fastreadb(disp + c) << (c * 8);
    }
    if (len == 1)
        part = (uint32_t) (int32_t) (int8_t) part;

    return part;
}

/* The segment of the last memory operand codegen_generate_ea() formed, for
   the FPU's data pointer (fpu_postamble) after a compiled instruction. */
static x86seg *codegen_ea_target_seg;

/* Set while a compiled x87 instruction's handler is generated: the walk its
   EA's marks would make is made once the address is formed, so that its #PF
   leaves with the escape wrapper's record (codegen_generate_ea) */
static int codegen_fetch_walk_fpu;

static int codegen_fpu_emit_record(ir_data_t *ir, x86seg *seg);

x86seg *
codegen_generate_ea(ir_data_t *ir, x86seg *op_ea_seg, uint32_t fetchdat, int op_ssegs, uint32_t *op_pc, uint32_t op_32, int stack_offset)
{
    const uint32_t modrm = cs + *op_pc;

    cpu_mod = (fetchdat >> 6) & 3;
    cpu_reg = (fetchdat >> 3) & 7;
    cpu_rm  = fetchdat & 7;

    if ((fetchdat & 0xc0) == 0xc0)
        return NULL;
    if (codegen_fetch_walk_fpu) {
        codegen_fetch_walk_defer    = 1;
        codegen_fetch_walk_deferred = 0;
    }
    if (op_32 & 0x200)
        codegen_ea_target_seg = codegen_generate_ea_32_long(ir, op_ea_seg, fetchdat, op_ssegs, op_pc, stack_offset);
    else
        codegen_ea_target_seg = codegen_generate_ea_16_long(ir, op_ea_seg, fetchdat, op_ssegs, op_pc);
    /* The escape wrapper records the pointers after an op that faulted in
       its fetch_ea, the data pointer from the eaaddr and segment it left
       (x86_ops_fpu.h, fpu_postamble): on the walk's #PF only, after which
       the block is left, the path that goes on joining behind barriers */
    if (codegen_fetch_walk_fpu) {
        codegen_fetch_walk_defer = 0;
        if (codegen_fetch_walk_deferred) {
            const uint32_t part = codegen_ea_disp_part(ir->block, fetchdat, modrm, op_32 & 0x200);
            int            jump_uop;

            uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
            uop_LOAD_FUNC_ARG_IMM(ir, 0, codegen_fetch_walk_deferred & ~0xfff);
            uop_CALL_FUNC_RESULT(ir, IREG_temp3, dynarec_fetch_walk);
            jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp3, 0);
            uop_SUB_IMM(ir, IREG_eaaddr, IREG_eaaddr, part);
            if (!(op_32 & 0x200))
                uop_AND_IMM(ir, IREG_eaaddr, IREG_eaaddr, 0xffff);
            codegen_fpu_emit_record(ir, codegen_ea_target_seg);
            uop_MOV_IMM(ir, IREG_temp3, 1);
            codegen_exit_on_one(ir, IREG_temp3);
            uop_NOP_BARRIER(ir);
            uop_set_jump_dest(ir, jump_uop);
        }
    }

    return codegen_ea_target_seg;
}

/* Set when the compiled x87 instruction's handler recorded its pointers
   itself (codegen_fpu_record_pointers): codegen_generate_call leaves its
   own record out */
static int codegen_fpu_pointers_recorded;

/* The record of an interpreted x87 instruction with a memory operand, made
   before its op is called (the call leaves the block on a nonzero answer),
   its address and segment set. None when the op will leave it unexecuted,
   where the escape wrapper sees X87_OP_NOT_EXECUTED: #NM at its FP_ENTER,
   or, for a softfloat op that checks (bit 17), a pending exception
   reported at it (FPU_check_pending_exceptions). Bits 0-15 are temp_CS. */
static void
codegen_fpu_record_interp(uint32_t cs_flags, uint32_t cs_base, uint32_t pc)
{
    if (cr0 & 0xc)
        return;
    if ((cs_flags & (1 << 17)) && (fpu_state.swd & FPU_SW_Summary) && ((cr0 & 0x20) || !fpu_ignne))
        return;
    cpu_state.fpu_op = x87_op;
    cpu_state.fpu_CS = cs_flags & 0xffff;
    cpu_state.fpu_cs = cs_base;
    cpu_state.fpu_pc = pc;
    fpu_postamble();
}

/* The FPU's instruction and data pointers and opcode as the escape wrapper
   records them: not in the fast environment, and not for a control
   instruction (FNSTSW AX here), which leaves the last one's. For a memory
   operand, IREG_ea_seg is set to seg first when seg is given (the answer
   says so) */
static int
codegen_fpu_emit_record(ir_data_t *ir, x86seg *seg)
{
    if (!cpu_dyn_accurate_fpu_env || x87_op_is_control(x87_op))
        return 0;
    uop_MOV_IMM(ir, IREG_fpu_op, x87_op);
    uop_MOV_IMM(ir, IREG_fpu_CS, cpu_state.temp_CS);
    uop_MOV_IMM(ir, IREG_fpu_cs, cpu_state.temp_cs);
    uop_MOV_IMM(ir, IREG_fpu_pc, cpu_state.temp_pc);
    if ((x87_op & 0xff) >= 0xc0)
        return 0;
    if (seg != NULL)
        uop_MOV_PTR(ir, IREG_ea_seg, (void *) seg);
    uop_CALL_FUNC(ir, fpu_postamble);
    return seg != NULL;
}

/* An undefined escape on the 486 and later: past FP_ENTER it is always #UD,
   which leaves it unexecuted (FPU_ILLEGAL), so it records nothing */
static int
codegen_fpu_undefined_ud(OpFn op)
{
    return ((op == dynarec_fpu_illegal_a16) || (op == dynarec_fpu_illegal_a32)) && (is486 || isibm486);
}

/* The record of an interpreted x87 instruction, before its op is called:
   none for an op that leaves it unexecuted (codegen_fpu_record_interp).
   pending_check: a softfloat op that checks for a pending exception. A
   register form tests that in the block, around its four constants:
   fpu_ignne is 0 or 1. */
static void
codegen_fpu_emit_record_interp(ir_data_t *ir, int mem, int pending_check)
{
    int jump_nm;
    int jump_pending = -1;

    if (!cpu_dyn_accurate_fpu_env || x87_op_is_control(x87_op))
        return;
    if (mem) {
        uop_LOAD_FUNC_ARG_IMM(ir, 0, cpu_state.temp_CS | (pending_check ? (1 << 17) : 0));
        uop_LOAD_FUNC_ARG_IMM(ir, 1, cpu_state.temp_cs);
        uop_LOAD_FUNC_ARG_IMM(ir, 2, cpu_state.temp_pc);
        uop_CALL_FUNC(ir, codegen_fpu_record_interp);
        return;
    }
    uop_MOV_REG_PTR(ir, IREG_temp0, &cr0);
    uop_AND_IMM(ir, IREG_temp1, IREG_temp0, 0xc);
    jump_nm = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp1, 0);
    if (pending_check) {
        /* ES and (CR0.NE or not IGNNE#) */
        uop_SHR_IMM(ir, IREG_temp0, IREG_temp0, 5);
        uop_MOV_REG_PTR(ir, IREG_temp1, &fpu_ignne);
        uop_XOR_IMM(ir, IREG_temp1, IREG_temp1, 1);
        uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp1);
        uop_MOVZX_REG_PTR_16(ir, IREG_temp1, &fpu_state.swd);
        uop_SHR_IMM(ir, IREG_temp1, IREG_temp1, 7);
        uop_AND(ir, IREG_temp0, IREG_temp0, IREG_temp1);
        uop_AND_IMM(ir, IREG_temp0, IREG_temp0, 1);
        jump_pending = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
    }
    uop_MOV_IMM(ir, IREG_fpu_op, x87_op);
    uop_MOV_IMM(ir, IREG_fpu_CS, cpu_state.temp_CS);
    uop_MOV_IMM(ir, IREG_fpu_cs, cpu_state.temp_cs);
    uop_MOV_IMM(ir, IREG_fpu_pc, cpu_state.temp_pc);
    uop_NOP_BARRIER(ir);
    uop_set_jump_dest(ir, jump_nm);
    if (jump_pending != -1)
        uop_set_jump_dest(ir, jump_pending);
}

/* The record for a compiled instruction. The data pointer's segment is the
   operand's own, which the block need not have set for a handler. A handler
   whose code can leave the block before its end emits this ahead of that
   exit. */
void
codegen_fpu_record_pointers(ir_data_t *ir)
{
    codegen_fpu_pointers_recorded = 1;
    if (codegen_fpu_emit_record(ir, codegen_ea_target_seg))
        last_op_ea_seg = codegen_ea_target_seg;
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
   an MMX instruction, as the interpreter takes it
   (x87_legacy_pending_entry). ES is tested inline; the helper is called
   only when it is set. A waiting x87 instruction has the same test in
   codegen_generate_call, so its entry does not repeat it. */
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
    codegen_exit_on_one(ir, IREG_temp0);
    uop_set_jump_dest(ir, jump_uop);
}

/* The exit taken when the helper just called answered 1 in reg (a fault, or
   the FERR# wait). It charges the cycles accumulated so far, the faulting
   instruction's own included, as a call to the interpreter does, but on that
   path only: the accumulator is left to the path that goes on, whose next
   flush still owes them. A backend that charges an exit's imm_data2 does it
   on the branch's exit path (the CMP_IMM_JZ is stamped as it is generated).
   Otherwise the IR charges them in an arm that leaves by a conditional
   branch, always taken there: a fault path goes back to the C loop unlinked.
   It tests a 1 of its own, not the answer, which would then be kept past the
   test for it on the path that goes on. It joins after the helper's call, a
   full barrier, so the join reloads nothing the call had not already
   dropped. */
void
codegen_exit_on_one(ir_data_t *ir, int reg)
{
#ifdef CODEGEN_BACKEND_HAS_EXIT_CYCLES
    uop_CMP_IMM_JZ(ir, reg, 1, codegen_exit_rout);
#else
    int add = codegen_exit_cycles(EXIT_FAULT_HELD);
    int jump_uop;

    if (!add) {
        uop_CMP_IMM_JZ(ir, reg, 1, codegen_exit_rout);
        return;
    }
    jump_uop = uop_CMP_IMM_JNZ_DEST(ir, reg, 1);
    uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, add);
    uop_MOV_IMM(ir, reg, 1);
    uop_CMP_IMM_JZ(ir, reg, 1, codegen_exit_rout);
    uop_NOP_BARRIER(ir);
    uop_set_jump_dest(ir, jump_uop);
#endif
}

/* An exit before the block's end charges what the block's end would have
   charged had the block ended there: the cycles accumulated so far, and the
   cycle the P6, K5 and K6 timing models hold back while their decode buffer
   has uops, which a block's end (and a branch, codegen_generate_call)
   charges. Only on the exit path: the accumulator is left to the path that
   goes on. The value is added to cycles, as the accumulator's count is. */
int
codegen_exit_cycles(int held)
{
    int add = codegen_accumulate_pending(ACCREG_cycles);

    if (held && codegen_timing_jump_cycles)
        add -= codegen_timing_jump_cycles();

    return add;
}

void
codegen_exit_stamp(ir_data_t *ir, int held)
{
    ir->uops[ir->wr_pos - 1].imm_data2 = codegen_exit_cycles(held);
    codegen_record_exit(ir->wr_pos - 1, held);
}

void
codegen_record_exit(int pos, int held)
{
    codegen_accumulate_record(ACC_EXIT, pos, held, 0, 0, 0);
}

void
codegen_record_route(int pos, void *p)
{
    if ((p == codegen_exit_rout) || (p == codegen_gpf_rout) || (p == codegen_ss_rout))
        codegen_accumulate_record(ACC_ROUTE, pos, 0, 0, 0, 0);
}

/* The x87 instruction being compiled, as the interpreter runs it (set
   before its handler is called), for a case its compiled form hands over */
OpFn codegen_fpu_handler;

/* A store from ST(0) when it is empty, a stack underflow: the interpreter's
   function does it (x87_empty_store, opFST: IE and SF, the indefinite
   stored when masked) and the block ends past the instruction. interp_pc
   is where that function starts (it steps past the ModR/M byte itself); a
   memory operand's address is in IREG_eaaddr already, in target_seg. */
/* The branch over a guard's hand-over, which the block runs only for the
   fault: a cold region (UOP_TYPE_COLD), whose stores are made on its way to
   the interpreter's function only */
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
#    define uop_GUARD_JZ_DEST uop_CMP_IMM_JZ_DEST_COLD
#else
#    define uop_GUARD_JZ_DEST uop_CMP_IMM_JZ_DEST
#endif

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
    /* The handler's EA made its displacement's walk: none for the
       function's fetch_ea (codegen_skip's hand-overs store theirs) */
    if (((fetchdat >> 6) & 3) != 3)
        uop_STORE_PTR_IMM(ir, &cpu_state.handover_walk, 0);
    /* The handler is the interpreter's function, not its escape wrapper,
       which records the instruction and data pointers after it whatever it
       answered: recorded before the call, which leaves the block on a fault.
       The function changes none of them, nor eaaddr or ea_seg, and cannot
       take #NM here (the handler's FP_ENTER came first). */
    codegen_fpu_emit_record(ir, NULL);
    uop_CALL_INSTRUCTION_FUNC(ir, codegen_fpu_handler, fetchdat);
    if (cpu_debug_regs_486)
        uop_CALL_FUNC(ir, dynarec_rf_after_op);
    uop_JMP(ir, codegen_exit_rout);
    codegen_exit_stamp(ir, 1);
}

/* A store from ST(0) when it is empty, a stack underflow: the interpreter
   does it (x87_empty_store, opFST: IE and SF, the indefinite stored when
   masked). An extended denormal in ST(0) goes to it too. */
void
codegen_fpu_guard_empty(ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t interp_pc, x86seg *target_seg)
{
    const int st0 = (cpu_state.TOP + 0) & 7;
    int       jump_uop;

    /*The cycles are added here whether or not ST(0) is tested: a device the
      block reaches later (update_tsc) sees the same count either way*/
    codegen_accumulate_flush(ir);
    if (codegen_fpu_tag_valid & (1 << st0))
        return;
    uop_MOVZX(ir, IREG_temp0, IREG_tag_B(0));
    /*Valid and not an extended denormal (codegen_fpu_guard_stack)*/
    uop_AND_IMM(ir, IREG_temp0, IREG_temp0, TAG_VALID | TAG_DENORMAL);
    jump_uop = uop_GUARD_JZ_DEST(ir, IREG_temp0, TAG_VALID);
    codegen_fpu_handover(ir, fetchdat, op_32, interp_pc, target_seg);
    uop_set_jump_dest(ir, jump_uop);
    /*The block goes on only with ST(0) valid*/
    codegen_fpu_tag_valid |= 1 << st0;
    codegen_fpu_tag_empty &= ~(1 << st0);
}

/* A stack fault (#IS): any of the registers in regs (FPU_GUARD_ST(i)) is
   empty, or with FPU_GUARD_PUSH, the push destination ST(7) is not. The
   interpreter's version of the instruction takes it (x87_stack_underflow,
   x87_stack_overflow and their callers in x87_ops.h), and the block ends
   past it. One branch tests them all: a register is empty when its tag's
   TAG_VALID bit is clear (x87.h). A register read that holds an extended
   denormal (TAG_DENORMAL) goes the same way, the interpreter raising DE for
   the instructions that do, so codegen_fpu_tag_valid also says the register
   is not one, here and in codegen_fpu_guard_empty. Taken after the operand's address is
   generated and before it is read: the interpreter does the whole
   instruction again, so a #GP or #PF on the operand still comes first. */
void
codegen_fpu_guard_stack(ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t interp_pc, x86seg *target_seg, int regs)
{
    const int push  = (cpu_state.TOP - 1) & 7;
    uint8_t   valid = 0;
    int       have  = 0;
    int       jump_uop;

    /*The cycles are added here whether or not anything is tested: a device
      the block reaches later (update_tsc) sees the same count either way*/
    codegen_accumulate_flush(ir);
    /*Only what the block's code so far has not proved*/
    for (int i = 0; i < 8; i++) {
        if (!(regs & FPU_GUARD_ST(i)))
            continue;
        valid |= 1 << ((cpu_state.TOP + i) & 7);
        if (codegen_fpu_tag_valid & (1 << ((cpu_state.TOP + i) & 7)))
            regs &= ~FPU_GUARD_ST(i);
    }
    if ((regs & FPU_GUARD_PUSH) && (codegen_fpu_tag_empty & (1 << push)))
        regs &= ~FPU_GUARD_PUSH;
    if (!regs)
        return;

    /*temp0 collects what is wrong: TAG_VALID clear or TAG_DENORMAL set in a
      register read (tag ^ TAG_VALID), TAG_VALID set in the push destination*/
    for (int i = 0; i < 8; i++) {
        if (!(regs & FPU_GUARD_ST(i)))
            continue;
        uop_MOVZX(ir, have ? IREG_temp1 : IREG_temp0, IREG_tag_B(i));
        uop_XOR_IMM(ir, have ? IREG_temp1 : IREG_temp0, have ? IREG_temp1 : IREG_temp0, TAG_VALID);
        if (have)
            uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp1);
        have = 1;
    }
    if (regs & FPU_GUARD_PUSH) {
        uop_MOVZX(ir, have ? IREG_temp1 : IREG_temp0, IREG_tag_B(-1));
        if (have)
            uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp1);
    }
    uop_AND_IMM(ir, IREG_temp0, IREG_temp0, TAG_VALID | TAG_DENORMAL);
    jump_uop = uop_GUARD_JZ_DEST(ir, IREG_temp0, 0);
    codegen_fpu_handover(ir, fetchdat, op_32, interp_pc, target_seg);
    uop_set_jump_dest(ir, jump_uop);
    /*The block goes on only with them valid, and a push's ST(7) empty*/
    codegen_fpu_tag_valid |= valid;
    codegen_fpu_tag_empty &= ~valid;
    if (regs & FPU_GUARD_PUSH) {
        codegen_fpu_tag_empty |= 1 << push;
        codegen_fpu_tag_valid &= ~(1 << push);
    }
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
#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
    uop_MOV_REG_PTR(ir, IREG_temp1_D, (void *) &bounds[0]);
    uop_MOV_REG_PTR(ir, IREG_temp2_D, (void *) &bounds[1]);
#    ifdef CODEGEN_BACKEND_HAS_COLD_REGION
    jump_uop = uop_FCMP_JWITHIN_DEST_COLD(ir, IREG_ST(0), IREG_temp1_D, IREG_temp2_D);
#    else
    jump_uop = uop_FCMP_JWITHIN_DEST(ir, IREG_ST(0), IREG_temp1_D, IREG_temp2_D);
#    endif
#else
    /*Only the arm64 and x86-64 backends implement UOP_FCMP_JWITHIN_DEST*/
    uop_MOV_REG_PTR(ir, IREG_temp1_D, (void *) &bounds[1]);
    uop_FCOM(ir, IREG_temp1_W, IREG_ST(0), IREG_temp1_D); /* C0 alone: below hi */
    uop_MOV_REG_PTR(ir, IREG_temp2_D, (void *) &bounds[0]);
    uop_FCOM(ir, IREG_temp2_W, IREG_ST(0), IREG_temp2_D); /* none: above lo */
    uop_MOVZX(ir, IREG_temp1, IREG_temp1_W);
    uop_XOR_IMM(ir, IREG_temp1, IREG_temp1, FPU_SW_C0);
    uop_MOVZX(ir, IREG_temp2, IREG_temp2_W);
    uop_OR(ir, IREG_temp1, IREG_temp1, IREG_temp2);
    jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp1, 0);
#endif
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

/* FLDCW of a word that leaves a set flag unmasked: the control word goes
   in as the interpreter's does (x87_set_control_word, with the rounding
   mode, then x87_legacy_status_es, which sets ES and B and raises FERR# as
   the model says). The word is the one the block read: the instruction is
   not run again. */
static void
codegen_fldcw_unmasks(uint32_t cw)
{
    const int was_es = cpu_state.npxs & FPU_SW_Summary;

    cpu_state.npxc = cw;
    codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
    x87_legacy_status_es(was_es, x87_op);
}

/* FLDCW: a flag the new word (cw_reg, 16-bit) leaves unmasked that is set
   in the status word is an error condition. codegen_fldcw_unmasks raises
   it and the block ends at next_pc: the compiled instructions after this
   one take no pending error (codegen_fpu_entered). */
void
codegen_fpu_guard_cw_unmasks(ir_data_t *ir, uint32_t next_pc, int cw_reg)
{
    int jump_uop;

    codegen_accumulate_flush(ir);
    uop_MOVZX(ir, IREG_temp1, cw_reg);
    uop_XOR_IMM(ir, IREG_temp1, IREG_temp1, 0x3f);
    uop_MOVZX(ir, IREG_temp2, IREG_NPXS);
    uop_AND(ir, IREG_temp1, IREG_temp1, IREG_temp2);
    uop_AND_IMM(ir, IREG_temp1, IREG_temp1, 0x3f);
    jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp1, 0);
    uop_MOV_IMM(ir, IREG_pc, next_pc);
    uop_LOAD_FUNC_ARG_REG(ir, 0, cw_reg);
    uop_CALL_FUNC(ir, codegen_fldcw_unmasks);
    uop_JMP(ir, codegen_exit_rout);
    codegen_exit_stamp(ir, 1);
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
int             codegen_instr_first_uop;

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
    uop_MOVZX(ir, IREG_temp0, IREG_NPXC);
    if (!(cpu_state.npxc & 0x300)) {
        uop_AND_IMM(ir, IREG_temp0, IREG_temp0, 0xf00);
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
        uop_JMP(ir, codegen_exit_rout);
        codegen_exit_stamp(ir, EXIT_FAULT_HELD);
        uop_set_jump_dest(ir, jump_uop);
    } else {
        uop_AND_IMM(ir, IREG_temp1, IREG_temp0, 0xc00);
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp1, 0);
        uop_JMP(ir, codegen_exit_rout);
        codegen_exit_stamp(ir, EXIT_FAULT_HELD);
        uop_set_jump_dest(ir, jump_uop);
        uop_AND_IMM(ir, IREG_temp1, IREG_temp0, 0x300);
        uop_CMP_IMM_JZ(ir, IREG_temp1, 0, codegen_exit_rout);
    }
    return 1;
}

/* Whether the interpreter's fetch of len bytes at addr, an instruction's or
   a prefix's first byte, reads from the block's second page, and where it
   first does (*walk): addr, or the page's first byte, which a read split at
   the page's end reaches first (fastreadl, fastreadw). fastreadl_fetch
   reads four bytes, near the page's end only as many as opcode_length[]
   gives; the refetch after a prefix is a plain fastreadl. The page after
   0xfffff000 is linear 0 */
static int
codegen_fetch_reach(const codeblock_t *block, uint32_t addr, int len, uint32_t *walk)
{
    if ((addr ^ block->pc) & ~0xfff) {
        *walk = addr;
        return 1;
    }
    if (((addr & 0xfff) + len) > 0x1000) {
        *walk = (addr & ~0xfff) + 0x1000;
        return 1;
    }
    return 0;
}

/*The MediaGX raises an SMI on a trapped VGA memory write (cpu_gxm_smi_mem)
  and takes it after the instruction, as the interpreter does. Compiled code
  looks at smi_line only between blocks, so on the MediaGX an instruction
  that stores, or that runs through its interpreter handler, leaves the block
  for a pending SMI before a later write can overwrite the trap's record.
  Other SMIs come from I/O, which the I/O handlers return for, or from
  memory-mapped stores (IOAPIC, LAPIC, OHCI on some chipsets), which
  compiled code takes at the block end, as before.*/
#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
#    define codegen_smi_exits() is_gxm
#else
#    define codegen_smi_exits() 0
#endif

static int
codegen_ir_stores(ir_data_t *ir, int first_uop)
{
    for (int c = first_uop; c < ir->wr_pos; c++) {
        switch (ir->uops[c].type & UOP_MASK) {
            case UOP_MEM_STORE_ABS & UOP_MASK:
            case UOP_MEM_STORE_REG & UOP_MASK:
            case UOP_MEM_STORE_IMM_8 & UOP_MASK:
            case UOP_MEM_STORE_IMM_16 & UOP_MASK:
            case UOP_MEM_STORE_IMM_32 & UOP_MASK:
            case UOP_MEM_STORE_SINGLE & UOP_MASK:
            case UOP_MEM_STORE_DOUBLE & UOP_MASK:
                return 1;

            default:
                break;
        }
    }
    return 0;
}

/* The four bytes from addr, as fastreadl reads them, but no page fault for
   those on the next page if it isn't mapped: the instruction may end before
   them. They read as 0 then; one that does reach them faults when the
   interpreter fetches them, and the block is discarded. */
static uint32_t
codegen_fetchl_lookahead(uint32_t addr)
{
    uint32_t val = 0;

    if ((addr & 0xfff) <= 0xffc)
        return fastreadl(addr);

    for (int c = 0; c < 4; c++) {
        if (!((addr + c) & 0xfff) && (get_phys_noabrt(addr + c) == 0xffffffff))
            break;
        val |= (uint32_t) fastreadb(addr + c) << (c * 8);
        if (cpu_state.abrt)
            break;
    }
    return val;
}

static int
codegen_is_prefix(uint8_t byte)
{
    return (byte == 0x26) || (byte == 0x2e) || (byte == 0x36) || (byte == 0x3e) || (byte == 0x64) || (byte == 0x65) ||
           (byte == 0x66) || (byte == 0x67) || (byte == 0xf0) || (byte == 0xf2) || (byte == 0xf3);
}

/* Set while the timing model charges an instruction behind a LOCK it can't
   take: what it reads of the instruction past opLOCK's bytes it takes with
   codegen_peekb. */
int codegen_timing_peek = 0;

/* A code byte the compile pass needs only to classify and time an
   instruction where the interpreter never reads it, read with nothing a
   guest can see: the walk is mmutranslate_noabrt's, which sets no accessed
   or dirty bit, adds no lookup entry and raises nothing, and pccache and the
   code marks are left alone. From RAM or ROM, as a fetch finds it
   (getpccache); 0 where the page is not mapped, as the lookahead reads it.
   Anywhere else the byte is a device's: 0, and the block is not kept, as
   after a fetch from there. */
uint8_t
codegen_peekb(uint32_t addr)
{
    uint64_t phys = addr;

    /* The code page fastreadb would take it from without a walk. */
    if ((addr >> 12) == pccache)
        return *((uint8_t *) (((uintptr_t) &pccache2[addr] & 0x00000000ffffffffULL) | ((uintptr_t) &pccache2[0] & 0xffffffff00000000ULL)));
    if (mem_a20_reset_line && !(cr0 >> 31) && ((addr & 0xfffffff0U) == 0xfffffff0U)) {
        cpu_fetch_device = 1;
        return 0;
    }
    if (cr0 >> 31) {
        phys = mmutranslate_noabrt(addr, 0);
        if (phys == 0xffffffffffffffffULL)
            return 0;
    }
    phys &= rammask;
    if (!_mem_exec[phys >> MEM_GRANULARITY_BITS]) {
        cpu_fetch_device = 1;
        return 0;
    }
    return _mem_exec[phys >> MEM_GRANULARITY_BITS][phys & MEM_GRANULARITY_MASK];
}

/* The instructions whose interpreter op can end the block on success, with
   a nonzero answer that leaves the block right after its call: a taken Jcc,
   LOOP or JCXZ, INT3, INT n, INTO and INT1, POP SS and MOV SS (they run the
   next instruction too), SYSENTER, SYSEXIT, and SYSCALL and SYSRET (0F 05 and
   0F 07 in the K6 table; 0F 07 is LOADALL386 in the 386 and 486 ones), and the
   Cyrix SMINT (0F 7E on the Cyrix CPUs without MMX, 0F 3E on the 6x86MX). Ops
   that answer nonzero only on a fault are not here (LSS is compiled; its
   register form only raises #UD), nor are the ones that
   can read the device time inside their call and then go on in the block (IN,
   OUT, RDTSC and RDMSR, which leave only on an SMI, an NMI or a reset; POPF
   and POPFD, which leave only with TF turned on and whose stack read can reach
   a device): charging the held cycle ahead of their call would move the time
   they read. */
static int
codegen_op_ends_block(const OpFn *op_table, uint8_t opcode, uint32_t fetchdat)
{
    if (op_table == x86_dynarec_opcodes)
        return ((opcode & 0xf0) == 0x70) || ((opcode & 0xfc) == 0xe0) || ((opcode >= 0xcc) && (opcode <= 0xce)) || (opcode == 0xf1) ||
               (opcode == 0x17) || ((opcode == 0x8e) && ((fetchdat & 0x38) == 0x10));
    if (op_table == x86_dynarec_opcodes_0f)
        return ((opcode & 0xf0) == 0x80) || (opcode == 0x34) || (opcode == 0x35) ||
               (((opcode == 0x05) || (opcode == 0x07)) && is_k6) || (opcode == 0x3e) ||
               ((opcode == 0x7e) && !cpu_has_feature(CPU_FEATURE_MMX));
    return 0;
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
    int          held_cycles        = 0;
    int          fpu_mem_record     = 0;
    uint32_t     fetchdat_pc;
    uint32_t     fetch_walk         = 0;
    /*The instruction after a compiled MOV SS is fetched by the MOV SS's op
      with a plain fastreadl, four bytes wherever it starts. The MOV SS comes
      earlier in the same block, so a mark left over from a compile that
      faulted is never matched by a block's first instruction*/
    const int    mov_ss_pair        = block->ins && codegen_mov_ss_pair_valid && (codegen_mov_ss_pair_pc == cs + old_pc);
    int          fetch_walk_valid   = codegen_fetch_reach(block, cs + old_pc, (mov_ss_pair || (opcode_length[opcode] > 2)) ? 4 : opcode_length[opcode], &fetch_walk);
    const int    first_fetch_walk   = fetch_walk_valid;
    int          walk_cycles        = 0;
    uint32_t     lock_pc            = 0;
    int          lock_cycles        = 0;
    uint32_t     lock_walk          = 0;
    int          lock_walk_valid    = 0;
    int          lock_walk_cycles   = 0;
    uint32_t     lock_read_end      = 0;
    int          lock_opcode_read   = 1;
    int          jump_cycles        = 0;
#ifdef DEBUG_EXTRA
    uint8_t last_prefix = 0;
#endif
    codegen_mov_ss_pair_valid = 0;
    op_ea_seg = &cpu_state.seg_ds;
    op_ssegs  = 0;

    codegen_timing_start();

    codegen_instructions[block->ins].first_timing = codegen_accumulate_record(ACC_START, 0, 0, 0, 0, 0);

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
                if (!in_lock) {
                    lock_pc       = op_pc;
                    lock_cycles   = codegen_block_cycles;
                    lock_read_end = op_pc + 4;
                }
                in_lock         = 1;
                break;

            case 0xf2: /*REPNE*/
#ifdef DEBUG_EXTRA
                last_prefix = 0xf2;
#endif
                op_table        = x86_dynarec_opcodes_REPNE;
                recomp_op_table = recomp_opcodes_REPNE;
                is_fpu          = 0;
                break;
            case 0xf3: /*REPE*/
#ifdef DEBUG_EXTRA
                last_prefix = 0xf3;
#endif
                op_table        = x86_dynarec_opcodes_REPE;
                recomp_op_table = recomp_opcodes_REPE;
                is_fpu          = 0;
                break;

            default:
                goto generate_call;
        }
        /*After a prefix the interpreter fetches four bytes again here, with
          the cycles of the prefixes before it charged and not its own; not
          after 0F or an escape, whose op takes the byte from the fetch
          before. From a LOCK on, only if the LOCK is legal (below)*/
        if (!over && !fetch_walk_valid) {
            if (!in_lock) {
                fetch_walk_valid = codegen_fetch_reach(block, cs + op_pc, 4, &fetch_walk);
                walk_cycles      = codegen_block_cycles;
            } else if (!lock_walk_valid) {
                lock_walk_valid  = codegen_fetch_reach(block, cs + op_pc, 4, &lock_walk);
                lock_walk_cycles = codegen_block_cycles;
            }
        }
        /*A 0F's or an escape's op takes the bytes of the fetch that read the
          0F or the escape, which fetchdat already holds (the loop's fetch, a
          prefix's read at it, or MOV SS's fetch of the instruction after
          it): they are not read again*/
        if (in_lock && (op_pc != lock_pc)) {
            /*Past the LOCK's own read, opLOCK reads the byte after each
              prefix up to 14 bytes past the LOCK, the byte after a 0F it
              stops on, and the ModR/M of an opcode it can take: the prefix
              ops' reads follow only when the LOCK is legal (generate_call).
              The rest of the four bytes the lookahead would give, which only
              the decoding and the timing here look at, are peeked. The
              prefix timing takes bits 0-7 only*/
            const uint32_t dist = (op_pc - 1) - lock_pc;
            uint32_t       next;
            uint32_t       modrm;

            if ((opcode == 0x0f) || ((opcode & 0xf8) != 0xd8)) {
                const int is_0f = (opcode == 0x0f);

                lock_opcode_read = is_0f ? (dist <= 14) : (dist < 14);
                next             = lock_opcode_read ? fastreadb(cs + op_pc) : codegen_peekb(cs + op_pc);
                if (lock_opcode_read && ((op_pc + 1) > lock_read_end))
                    lock_read_end = op_pc + 1;
                if (!cpu_state.abrt && lock_opcode_read && (is_0f || (!codegen_is_prefix(next) && (next != 0x0f))) && x86_lock_opcode(is_0f, next)) {
                    modrm = fastreadb(cs + op_pc + 1);
                    if ((op_pc + 2) > lock_read_end)
                        lock_read_end = op_pc + 2;
                } else
                    modrm = codegen_peekb(cs + op_pc + 1);
                fetchdat = next | (modrm << 8) | ((uint32_t) codegen_peekb(cs + op_pc + 2) << 16);
                if (!is_0f)
                    fetchdat |= (uint32_t) codegen_peekb(cs + op_pc + 3) << 24;
            }
        } else if ((opcode != 0x0f) && ((opcode & 0xf8) != 0xd8))
            fetchdat = codegen_fetchl_lookahead(cs + op_pc);
        codegen_timing_prefix(opcode, fetchdat);
        codegen_accumulate_record(ACC_PREFIX, 0, opcode, fetchdat, 0, 0);
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

    /*A LOCK the instruction can't take is #UD: leave it to the interpreter's.
      Its op looks for the opcode 14 bytes past the LOCK at most, and with
      more prefixes than that stops on one, which no LOCK applies to*/
    if (in_lock) {
        if (((over ? (op_pc - 2) : (op_pc - 1)) - lock_pc) > 14)
            lock_illegal = 1;
        else if (op_table == x86_dynarec_opcodes_0f)
            lock_illegal = !x86_lock_legal(1, opcode, fetchdat & 0xff);
        else if ((op_table == x86_dynarec_opcodes) || (op_table == x86_dynarec_opcodes_REPE) || (op_table == x86_dynarec_opcodes_REPNE))
            lock_illegal = !x86_lock_legal(0, opcode, fetchdat & 0xff);
        else
            lock_illegal = 1;
        /*A legal LOCK runs the prefix ops after it, which read four bytes
          after each: the bytes of the last of those reads, which reach
          furthest, as the op's fetchdat*/
        if (!lock_illegal) {
            if (over)
                fetchdat = codegen_fetchl_lookahead(cs + op_pc - 2) >> 16;
            else
                fetchdat = codegen_fetchl_lookahead(cs + op_pc - 1) >> 8;
            if (cpu_state.abrt)
                return;
        }
    }

    fetchdat_pc = op_pc + pc_off;
    codegen_instructions[block->ins].pc        = cpu_state.oldpc;
    codegen_instructions[block->ins].op_ssegs  = last_op_ssegs;
    codegen_instructions[block->ins].op_ea_seg = last_op_ea_seg;
    codegen_instructions[block->ins].op_32     = last_op_32;
    codegen_instructions[block->ins].TOP       = cpu_state.TOP;
    codegen_instructions[block->ins].first_uop = ir->wr_pos;
    /*The interpreter reads from the second page before this instruction does
      anything; what it reads later, the handlers' marks walk for. A LOCK
      reads on to the opcode it applies to (14 bytes past it at most), and
      to the ModR/M of an opcode that can be locked, before its #UD (opLOCK);
      the prefixes after it fetch again only when it is legal*/
    if (in_lock && !fetch_walk_valid) {
        const int      is_0f     = (op_table == x86_dynarec_opcodes_0f);
        const uint32_t chain_end = over ? (op_pc - 2) : (op_pc - 1);
        uint32_t       last      = lock_pc + 14;

        if ((chain_end - lock_pc) <= 14) {
            last = chain_end + is_0f;
            if (!is_fpu && x86_lock_opcode(is_0f, opcode))
                last++;
        }
        fetch_walk_valid = codegen_fetch_reach(block, cs + lock_pc, ((last - lock_pc) >= 4) ? (last - lock_pc + 1) : 4, &fetch_walk);
        walk_cycles      = lock_cycles;
        if (!fetch_walk_valid && !lock_illegal && lock_walk_valid) {
            fetch_walk       = lock_walk;
            fetch_walk_valid = 1;
            walk_cycles      = lock_walk_cycles;
        }
    }
    /*RF survives a fault in the first opcode fetch, but is cleared before
      a prefix can fetch again. The option changes only through a hard reset*/
    if (!block->ins && cpu_debug_regs_486) {
        if (first_fetch_walk && !codegen_page2_walked)
            codegen_fetch_walk(fetch_walk);
        uop_AND_IMM(ir, IREG_eflags, IREG_eflags, ~RF_FLAG);
        /*A loop at the block's head does not repeat the entry clear. A head
          with a fetch walk cannot be unrolled (codegen_can_unroll_full)*/
        if (!first_fetch_walk)
            codegen_instructions[block->ins].first_uop = ir->wr_pos;
    }
    /*Its #PF leaves with the cycles of the prefixes the interpreter ran
      before that fetch; the path that goes on charges them with the rest*/
    if (fetch_walk_valid && !codegen_page2_walked) {
        codegen_accumulate(ir, ACCREG_cycles, -walk_cycles);
        codegen_fetch_walk(fetch_walk);
        codegen_accumulate(ir, ACCREG_cycles, walk_cycles);
    }
    /*Whatever cleared the entry (the block's start, an MMX instruction, an
      interpreted one, LMSW) may have changed the tags behind the IR*/
    if (!codegen_fpu_entered) {
        codegen_fpu_tag_valid = 0;
        codegen_fpu_tag_empty = 0;
    }
    codegen_instructions[block->ins].fpu_entered  = codegen_fpu_entered;
    codegen_instructions[block->ins].fpu_es_check = codegen_fpu_es_check;
    codegen_instructions[block->ins].mmx_entered  = codegen_mmx_entered;
    codegen_instructions[block->ins].tag_valid    = codegen_fpu_tag_valid;
    codegen_instructions[block->ins].tag_empty    = codegen_fpu_tag_empty;

    /*An instruction behind a LOCK it can't take is a hand-over, which no
      unrolled copy repeats, so its event is not replayed*/
    codegen_timing_peek = lock_illegal;
    codegen_timing_opcode(opcode, fetchdat, op_32, op_pc);
    codegen_timing_peek = 0;
    codegen_accumulate_record(ACC_OPCODE, 0, opcode, fetchdat, op_32, op_pc);

    codegen_accumulate_timing(ir);

    if ((op_table == x86_dynarec_opcodes && ((opcode & 0xf0) == 0x70 || (opcode & 0xfc) == 0xe0 || opcode == 0xc2 || (opcode & 0xfe) == 0xca || (opcode & 0xfc) == 0xcc || (opcode & 0xfc) == 0xe8 || (opcode == 0xff && ((fetchdat & 0x38) >= 0x10 && (fetchdat & 0x38) < 0x30)))) || (op_table == x86_dynarec_opcodes_0f && ((opcode & 0xf0) == 0x80))) {
        /*On some CPUs (eg K6), a jump/branch instruction may be able to pair with
          subsequent instructions, so no cycles may have been deducted for it yet.
          To prevent having zero cycle blocks (eg with a jump instruction pointing
          to itself), apply the cycles that would be taken if this jump is taken,
          then reverse it for subsequent instructions if the jump is not taken*/
        if (codegen_timing_jump_cycles)
            jump_cycles = codegen_timing_jump_cycles();
        else if (((op_table == x86_dynarec_opcodes) && ((opcode & 0xf0) == 0x70)) || ((op_table == x86_dynarec_opcodes_0f) && ((opcode & 0xf0) == 0x80)))
            jump_cycles = timing_bt; /*A Jcc taken: timing_bt more than the timing_bnt charged, as the interpreter (opJ*)*/

        codegen_accumulate_jump(ir, -1, jump_cycles);
        codegen_accumulate_flush(ir);
        codegen_accumulate_jump(ir, 1, jump_cycles);
    }

    /*Only on a processor with 3DNow!: on the others 0F 0F is ILLEGAL in its
      own table, and the check below leaves it to the interpreter's #UD. Not
      behind a LOCK it can't take: opLOCK reads no suffix*/
    if (op_table == x86_dynarec_opcodes_0f && opcode == 0x0f && !lock_illegal && op_table[((opcode >> opcode_shift) | op_32) & opcode_mask] != dynarec_op_illegal) {
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
        /*The interpreter fetches the suffix after MMX_ENTER and the EA
          (op3DNOW_a32, which a hand-over to it runs): for a suffix taken
          from the 3DNow! table the walk is made there, by its handler or by
          the hand-over below*/
        codegen_fetch_walk_off = 1;
        codegen_mark_code_present(block, cs + opcode_pc, 1);
        codegen_fetch_walk_off = 0;
        if (!fpu_softfloat && recomp_opcodes_3DNOW[opcode_3dnow]) {
            next_pc = opcode_pc + 1;

            op_table           = x86_dynarec_opcodes_3DNOW;
            recomp_op_table    = fpu_softfloat ? NULL : recomp_opcodes_3DNOW;
            opcode             = opcode_3dnow;
            recomp_opcode_mask = 0xff;
            opcode_mask        = 0xff;
        }
    }
    /*An ALU EAX,imm32 in a block that reads those opcodes as it runs: neither
      the opcode nor the immediate is marked. Not the block's first
      instruction, whose opcode keeps a byte of the first chunk marked*/
    if ((block->flags & CODEBLOCK_NO_OPCODES) && block->ins && (op_table == x86_dynarec_opcodes) && ((opcode & 0xc7) == 0x05) && (op_32 & 0x100) &&
        (op_pc == old_pc + 1) && !in_lock && !(((cs + old_pc + 4) ^ block->pc) & ~0xfff)) {
        codegen_ea_target_seg   = NULL;
        codegen_instr_pc        = old_pc;
        codegen_instr_first_uop = ir->wr_pos;
        codegen_alu_eax_imm_family(block, ir, old_pc, opcode);
        block->flags |= CODEBLOCK_OPCODE_FAMILY;
        {
            uint32_t off = (cs + old_pc) - (block->pc & ~0x3f);

            if (off < 128)
                codegen_family_ops[off >> 6] |= (uint64_t) 1 << (off & 63);
        }
        uop_MOV_IMM(ir, IREG_pc, op_pc + 4);

        codegen_endpc = (cs + cpu_state.pc) + 8;

        block->ins++;

        if (block->ins >= MAX_INSTRUCTION_COUNT)
            CPU_BLOCK_END();

        return;
    }
    /*The prefixes and the opcode; an x87 escape's ModR/M too, as op_pc is
      already past it (pc_off is -1 for those only while op_pc is moved back
      to the ModR/M for the EA below). Bytes of the interpreter's own fetches,
      whose walk is decided above. Behind a LOCK it can't take, only those
      opLOCK reads (lock_read_end), not what was only peeked*/
    codegen_fetch_walk_off = 1;
    if (lock_illegal) {
        uint32_t mark_end = op_pc;

        if (mark_end > lock_read_end)
            mark_end = lock_read_end;
        codegen_mark_code_present(block, cs + old_pc, mark_end - old_pc);
    } else
        codegen_mark_code_present(block, cs + old_pc, op_pc - old_pc);
    codegen_fetch_walk_off = 0;
    /* Every escape stores its opcode, D8 00 (FADD m32 by [BX+SI] or [EAX])
       included: the handlers and the FERR# checks read it. Not behind a LOCK
       it can't take: opLOCK's #UD comes first, and its ModR/M isn't read */
    if (is_fpu && !lock_illegal) {
        uop_MOV_IMM(ir, IREG_x87_op, op87);
    }

    /* It is apparently a prefixed instruction. */
#if 0
    if ((recomp_op_table == recomp_opcodes) && (opcode == 0x48))
        goto codegen_skip;
#endif
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
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
        /* With ES clear there is no such error and the call returns at
           once: it is made only with ES set, in a cold region that goes on
           with the registers as they were */
        if (!fpu_softfloat && codegen_reg_read_has_room(IREG_NPXS)) {
            const int first = ir->wr_pos;
            int       jump_uop;

            /*One uop that writes no register: its read is left out of the
              reference counts that end the block, and the uop out of what
              limits unrolling (codegen_can_unroll_full), which stay as they
              were with the bare call; the dead-flags pass passes over it
              too*/
            codegen_reg_read_uncounted(IREG_NPXS);
            jump_uop = uop_TEST_IMM_JZ_DEST_COLD(ir, IREG_NPXS, FPU_SW_Summary);
            codegen_ir_set_uncounted(first);
            uop_LOAD_FUNC_ARG_IMM(ir, 0, x87_op);
            uop_CALL_FUNC(ir, x87_ferr_nowait);
            uop_set_jump_dest(ir, jump_uop);
        } else
#endif
        {
            uop_LOAD_FUNC_ARG_IMM(ir, 0, x87_op);
            uop_CALL_FUNC(ir, x87_ferr_nowait);
        }
    }

    /* A pending exception is taken only with ES set: the call only then. Once
       the block has entered the FPU it is left out, unless a compiled
       instruction since the last check can have set ES and gone on */
    if (is_fpu && !fpu_softfloat && !x87_op_is_nowait(x87_op) && (!codegen_fpu_entered || codegen_fpu_es_check)) {
        int jump_uop;

        codegen_fpu_es_check = 0;
        uop_MOVZX(ir, IREG_temp3, IREG_NPXS);
        uop_AND_IMM(ir, IREG_temp3, IREG_temp3, FPU_SW_Summary);
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
        /* A cold region that goes on, when the helper answers 0, with the
           registers as they were */
        jump_uop = uop_CMP_IMM_JZ_DEST_COLD(ir, IREG_temp3, 0);
#else
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp3, 0);
#endif
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        uop_CALL_FUNC_RESULT(ir, IREG_temp3, x87_legacy_pending_entry);
        codegen_exit_on_one(ir, IREG_temp3);
        uop_set_jump_dest(ir, jump_uop);
    }

    if (recomp_op_table && recomp_op_table[(opcode | op_32) & recomp_opcode_mask]) {
        uint32_t new_pc;

        if (is_fpu)
            codegen_fpu_handler = op_table[((opcode >> opcode_shift) | op_32) & opcode_mask];
        codegen_ea_target_seg         = NULL;
        codegen_fpu_pointers_recorded = 0;
        codegen_instr_pc              = old_pc;
        codegen_fetch_walk_fpu        = is_fpu;
        const int first_uop           = ir->wr_pos;
        codegen_instr_first_uop       = first_uop;
        new_pc = recomp_op_table[(opcode | op_32) & recomp_opcode_mask](block, ir, opcode, fetchdat, op_32, op_pc);
        codegen_fetch_walk_fpu = 0;
        /* The FPU's pointers for a compiled instruction, unless its handler
           recorded them; one the recompiler left alone has them done ahead
           of its handler call below */
        if (new_pc && is_fpu && !codegen_fpu_pointers_recorded)
            codegen_fpu_record_pointers(ir);
        if (new_pc) {
            /*An instruction that sets pc itself (-1) is a jump or call,
              whose only store is to the stack, which nothing traps*/
            if ((new_pc != -1) && codegen_smi_exits() && codegen_ir_stores(ir, first_uop)) {
                /*The exit leaves the block here: charge the cycles of the
                  instructions so far, this one included, as the interpreter
                  does before it takes the SMI*/
                codegen_accumulate_flush(ir);
                uop_SMI_EXIT_IMM(ir, new_pc);
            }
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

    codegen_v86_64k = 0;

    if (lock_illegal)
        op = dynarec_op_illegal;
    else
        op = op_table[((opcode >> opcode_shift) | op_32) & opcode_mask];

    /*A LOCK the instruction can't take is #UD before anything after the
      bytes opLOCK reads: no EA*/
    if (!lock_illegal && (!test_modrm || (op_table == x86_dynarec_opcodes && opcode_modrm[opcode]) || (op_table == x86_dynarec_opcodes_0f && opcode_0f_modrm[opcode]) || (op_table == x86_dynarec_opcodes_3DNOW))) {
        int stack_offset = 0;

        if (op_table == x86_dynarec_opcodes && opcode == 0x8f) /*POP*/
            stack_offset = (op_32 & 0x100) ? 4 : 2;

        cpu_mod = (fetchdat >> 6) & 3;
        cpu_reg = (fetchdat >> 3) & 7;
        cpu_rm  = fetchdat & 7;

        uop_MOV_IMM(ir, IREG_rm_mod_reg, cpu_rm | (cpu_mod << 8) | (cpu_reg << 16));

        /*An x87 op whose data pointer is recorded ahead of its call
          (codegen_fpu_emit_record_interp)*/
        fpu_mem_record = is_fpu && !lock_illegal && (cpu_mod != 3) && cpu_dyn_accurate_fpu_env && !x87_op_is_control(x87_op) && !codegen_fpu_undefined_ud(op);

        /*The op reads the displacement in its fetch_ea, after its own
          checks (MMX_ENTER, FP_ENTER, ILLEGAL_ON; a LOCK it can't take is
          #UD before it): the walk the EA's marks would make is left to that
          fetch_ea, its address stored for it, 0 for none. An op from the
          3DNow! table calls no fetch_ea, and its walk is made here*/
        codegen_fetch_walk_defer    = lock_illegal || (op_table != x86_dynarec_opcodes_3DNOW);
        codegen_fetch_walk_deferred = 0;
        const uint32_t modrm_pc     = cs + op_pc + pc_off;
        op_pc += pc_off;
        if (cpu_mod != 3 && !(op_32 & 0x200)) {
            op_ea_seg = codegen_generate_ea_16_long(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc);
        }
        if (cpu_mod != 3 && (op_32 & 0x200)) {
            op_ea_seg = codegen_generate_ea_32_long(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, stack_offset);
        }
        op_pc -= pc_off;
        codegen_fetch_walk_defer = 0;
        /*With the deferred address, bit 1 when the record before the call
          holds the full address as the data pointer, which the walk's #PF
          (after the op's FP_ENTER and pending check, so that record was
          made) replaces with what the interpreter's fetch_ea left; and what
          that leaves out of the address (dynarec_handover_walk)*/
        if (codegen_fetch_walk_deferred) {
            uop_STORE_PTR_IMM(ir, &cpu_state.handover_walk_part, codegen_ea_disp_part(block, fetchdat, modrm_pc, op_32 & 0x200));
            uop_STORE_PTR_IMM(ir, &cpu_state.handover_walk, codegen_fetch_walk_deferred | (fpu_mem_record ? 2 : 0));
        } else if (cpu_mod != 3)
            uop_STORE_PTR_IMM(ir, &cpu_state.handover_walk, 0);
    }
    /*An op from the 3DNow! table reads no code: the suffix op3DNOW_a32
      fetches after the EA walks here*/
    if ((op_table == x86_dynarec_opcodes_3DNOW) && !lock_illegal)
        codegen_fetch_walk_mark(block, cs + next_pc - 1, 1);

#ifdef DEBUG_EXTRA
    uop_LOG_INSTR(ir, opcode | (last_prefix << 8));
#endif
    /*An op that ends the block on success leaves it after the call, before
      the block end would charge the cycles the timing model still holds:
      charge them with the cycles so far, as the branches above do, and take
      them back for the instructions that follow ahead of the call, so its
      exit (stamped as the call is generated) does not charge them again*/
    if (codegen_timing_jump_cycles && (op != dynarec_op_illegal) && codegen_op_ends_block(op_table, opcode, fetchdat))
        held_cycles = codegen_timing_jump_cycles();
    if (held_cycles)
        codegen_accumulate(ir, ACCREG_cycles, -held_cycles);
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
    /*The escape wrapper records an x87 instruction's pointers after its op
      whatever the op answered, unless it left the instruction unexecuted:
      recorded before the call, which leaves the block on a nonzero answer.
      The op changes none of them, nor eaaddr or ea_seg, which hold its
      operand's address here. A LOCK prefix is #UD before the wrapper. An
      undefined escape and the no-FPU ops check for no pending exception.*/
    if (is_fpu && !lock_illegal && !codegen_fpu_undefined_ud(op))
        codegen_fpu_emit_record_interp(ir, (x87_op & 0xff) < 0xc0,
                                       fpu_softfloat && hasfpu && (op != dynarec_fpu_illegal_a16) && (op != dynarec_fpu_illegal_a32));
    if (held_cycles)
        codegen_accumulate(ir, ACCREG_cycles, held_cycles);
    uop_CALL_INSTRUCTION_FUNC(ir, op, fetchdat);
    /*The interpreted loop's RF handling after the op (RSM and LOADALL386
      load RF, which it then clears). An op that ends the block with its
      answer leaves before this, and the dispatch loop consumes what it
      left (exec386_dynarec_dyn)*/
    if (cpu_debug_regs_486)
        uop_CALL_FUNC(ir, dynarec_rf_after_op);
    /*Nor are the tags known after any interpreted instruction: an x87 one
      can load them, and any can reach a reset through its I/O*/
    codegen_fpu_tag_valid = 0;
    codegen_fpu_tag_empty = 0;
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
    /*After the FPU environment update (recorded ahead of the call), so an
      SMI taken on an x87 store leaves this instruction's pointers for
      FNSTENV or FSAVE*/
    if (codegen_smi_exits())
        uop_SMI_EXIT(ir);
    codegen_flags_changed = 0;
    /* The handler gets the bytes after the opcode through fetchdat, and reads
       anything further from memory as it runs. Marking more than fetchdat
       covers would claim the data that often follows an instruction such as
       RET or IRET, and every write to it would throw the block away. */
    {
        /* After an escape fetchdat starts at the ModR/M, and the fetch that
           read the escape ends two bytes after it. */
        int fetch_len = 3;

        /* A one-byte opcode that reads nothing after it (POPF, CLI, the
           string ops) or only an imm8/rel8 uses no more of fetchdat. */
        if (!pc_off && ((op_table == x86_dynarec_opcodes) || (op_table == x86_dynarec_opcodes_REPE) || (op_table == x86_dynarec_opcodes_REPNE)) &&
            (opcode_length[opcode] < 3))
            fetch_len = opcode_length[opcode] - 1;
        /* After 0F the fetch that read it ends at the byte after the ModR/M. */
        if ((op_table == x86_dynarec_opcodes_0f) || (op_table == x86_dynarec_opcodes_3DNOW))
            fetch_len = 2;
        /* Behind a LOCK it can't take, only what opLOCK read of them. */
        if (lock_illegal)
            fetch_len = (fetchdat_pc >= lock_read_end) ? 0 : (((lock_read_end - fetchdat_pc) < (uint32_t) fetch_len) ? (int) (lock_read_end - fetchdat_pc) : fetch_len);
        /* Those are the interpreter fetch's bytes, and the op fetches what
           lies past them itself: neither walks here */
        codegen_fetch_walk_off = 1;
        codegen_mark_code_present(block, cs + fetchdat_pc, fetch_len);
        codegen_fetch_walk_off = 0;
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

    /*MOV CR0 can switch protected mode, which the block is keyed on, change
      the address translation, or set CD, which the loop tests before it runs
      a block. The write a later run makes can differ from the one this pass
      made, so the block ends after every MOV CR0, whatever this one changed*/
    if ((op_table == x86_dynarec_opcodes_0f) && (opcode == 0x22) && !(fetchdat & 0x38))
        CPU_BLOCK_END();

    /*LOADALL386 loads CS:EIP, CR0, EFLAGS and every segment cache from its
      image, which a later run can give other values: the block ends after
      it. 0F 07 is #UD on the other tables but the K6 ones, where SYSRET
      ends the block already*/
    if ((op_table == x86_dynarec_opcodes_0f) && (opcode == 0x07))
        CPU_BLOCK_END();

    codegen_endpc = (cs + cpu_state.pc) + 8;

#if 0
    if (has_ea)
        fatal("Has EA\n");
#endif
}

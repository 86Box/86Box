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
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ir_defs.h"
#include "codegen_reg.h"
#include "codegen_ops_helpers.h"

void
LOAD_IMMEDIATE_FROM_RAM_16_unaligned(UNUSED(codeblock_t *block), ir_data_t *ir, int dest_reg, uint32_t addr)
{
    /*Word access that crosses two pages. Perform reads from both pages, shift and combine*/
    uop_MOVZX_REG_PTR_8(ir, IREG_temp3_W, get_ram_ptr(addr + 1));
    uop_MOVZX_REG_PTR_8(ir, dest_reg, get_ram_ptr(addr));
    uop_SHL_IMM(ir, IREG_temp3_W, IREG_temp3_W, 8);
    uop_OR(ir, dest_reg, dest_reg, IREG_temp3_W);
}

void
LOAD_IMMEDIATE_FROM_RAM_32_unaligned(UNUSED(codeblock_t *block), ir_data_t *ir, int dest_reg, uint32_t addr)
{
    /*Dword access that crosses two pages. Perform reads from both pages, shift and combine*/
    uop_MOV_REG_PTR(ir, dest_reg, get_ram_ptr(addr & ~3));
    uop_MOV_REG_PTR(ir, IREG_temp3, get_ram_ptr((addr + 4) & ~3));
    uop_SHR_IMM(ir, dest_reg, dest_reg, (addr & 3) * 8);
    uop_SHL_IMM(ir, IREG_temp3, IREG_temp3, (4 - (addr & 3)) * 8);
    uop_OR(ir, dest_reg, dest_reg, IREG_temp3);
}

/*Rebuilds emitted by codegen_flags_rebuild_known() in this block, with the
  uop that set the flags op each one was worked out from*/
#define FLAGS_KNOWN_MAX 64
static int flags_known_nr;
static int flags_known_site[FLAGS_KNOWN_MAX];
static int flags_known_producer[FLAGS_KNOWN_MAX];
/*1 = an INC or DEC that left CF in the flags register as it was: still
  right after any number of INCs and DECs, wrong after any other flags op*/
static int flags_known_cf_kept[FLAGS_KNOWN_MAX];
/*A use that could not be recorded: no loop of the block unrolls*/
static int flags_known_full;

void
codegen_flags_known_reset(void)
{
    flags_known_nr   = 0;
    flags_known_full = 0;
}

/*Records the use at ir->wr_pos with the uop that set the flags op; 0 when
  that uop is not in this block or the table is full*/
static int
flags_known_record(ir_data_t *ir, int cf_kept)
{
    int producer;

    for (producer = ir->wr_pos - 1; producer >= 0; producer--) {
        if (IREG_GET_REG(ir->uops[producer].dest_reg_a.reg) == IREG_GET_REG(IREG_flags_op))
            break;
    }
    if ((producer < 0) || (flags_known_nr == FLAGS_KNOWN_MAX))
        return 0;
    flags_known_site[flags_known_nr]     = ir->wr_pos;
    flags_known_producer[flags_known_nr] = producer;
    flags_known_cf_kept[flags_known_nr]  = cf_kept;
    flags_known_nr++;
    return 1;
}

/*The count of a shift that set the flags op to op: the immediate its
  handler wrote to flags_op2 in the uop just before the one setting the flags
  op, when nothing has written either since. 0 when it is not known*/
static uint32_t
flags_known_count(ir_data_t *ir, int op)
{
    const uop_t *op_uop;
    const uop_t *count_uop;
    int          producer;

    for (producer = ir->wr_pos - 1; producer > 0; producer--) {
        if (IREG_GET_REG(ir->uops[producer].dest_reg_a.reg) == IREG_GET_REG(IREG_flags_op))
            break;
        if (IREG_GET_REG(ir->uops[producer].dest_reg_a.reg) == IREG_GET_REG(IREG_flags_op2))
            return 0;
    }
    if (producer <= 0)
        return 0;
    op_uop    = &ir->uops[producer];
    count_uop = &ir->uops[producer - 1];
    if ((IREG_GET_REG(op_uop->dest_reg_a.reg) != IREG_GET_REG(IREG_flags_op)) || ((op_uop->type & UOP_MASK) != (UOP_MOV_IMM & UOP_MASK)) ||
        (op_uop->imm_data != (uint32_t) op))
        return 0;
    if ((IREG_GET_REG(count_uop->dest_reg_a.reg) != IREG_GET_REG(IREG_flags_op2)) || ((count_uop->type & UOP_MASK) != (UOP_MOV_IMM & UOP_MASK)))
        return 0;
    if ((count_uop->imm_data < 1) || (count_uop->imm_data > 31) || (count_uop->imm_data != cpu_state.flags_op2))
        return 0;
    return count_uop->imm_data;
}

/*A compile-time use of the known flags op other than an inline rebuild: a
  Jcc, SETcc or LOOPcc compiled for that op, a rebuild left out because the
  op needs none, or (cf_kept) an INC or DEC that keeps the flags register's
  CF because the op left it there. An unrolled loop must not repeat it after
  its own flag changes, so it is recorded for codegen_can_unroll_full()*/
void
codegen_flags_known_use(ir_data_t *ir, int cf_kept)
{
    if (!flags_known_record(ir, cf_kept))
        flags_known_full = 1;
}

/*Whether the uops from start on set a flags op (other than INC and DEC with
  incdec_too clear): the loop body changes the flags a copied use reads. A C
  call may set it without a uop writing flags_op (a rebuild the inline
  sequences cannot do, for SAHF, BT, MUL, RCL and the like), so with
  incdec_too any call counts; with it clear a call cannot leave CF stale*/
static int
flags_op_set_from(ir_data_t *ir, int start, int incdec_too)
{
    for (int c = start; c < ir->wr_pos; c++) {
        uint32_t type = ir->uops[c].type & UOP_MASK;
        uint32_t op;

        if (incdec_too && ((type == (UOP_CALL_FUNC & UOP_MASK)) || (type == (UOP_CALL_FUNC_ORDERED & UOP_MASK)) ||
                           (type == (UOP_CALL_FUNC_RESULT & UOP_MASK)) || (type == (UOP_CALL_FUNC_RESULT_ORDERED & UOP_MASK))))
            return 1;
        if (IREG_GET_REG(ir->uops[c].dest_reg_a.reg) != IREG_GET_REG(IREG_flags_op))
            continue;
        if (incdec_too)
            return 1;
        op = ir->uops[c].imm_data;
        if (((ir->uops[c].type & UOP_MASK) != (UOP_MOV_IMM & UOP_MASK)) ||
            !((op == FLAGS_INC8) || (op == FLAGS_INC16) || (op == FLAGS_INC32) || (op == FLAGS_DEC8) || (op == FLAGS_DEC16) || (op == FLAGS_DEC32)))
            return 1;
    }
    return 0;
}

/*flags_rebuild(), or flags_rebuild_c() if carry_only, as uops when the flags
  op is known here and is one whose flags depend only on flags_res,
  flags_op1, flags_op2 and the carry; returns 0, emitting nothing, otherwise.
  Each flag is the expression CF_SET() .. VF_SET() in x86_flags.h use for
  that op, on the same values, ORed into flags in place; a shift's count is
  the constant its handler wrote to flags_op2. Uses temp1 and temp3 only:
  callers hold operands in temp0 and shift counts in temp2.*/
int
codegen_flags_rebuild_known(ir_data_t *ir, int carry_only)
{
#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
    int      op = codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN;
    int      is_add;
    int      is_sub;
    int      is_incdec;
    int      is_shl;
    int      is_shr;
    int      is_sar;
    int      width;
    int      cf_bit = -1;
    uint32_t count  = 0;
    uint32_t msb;

    switch (op) {
        case FLAGS_ZN8:
        case FLAGS_ADD8:
        case FLAGS_SUB8:
        case FLAGS_INC8:
        case FLAGS_DEC8:
        case FLAGS_SHL8:
        case FLAGS_SHR8:
        case FLAGS_SAR8:
            msb   = 0x80;
            width = 8;
            break;
        case FLAGS_ZN16:
        case FLAGS_ADD16:
        case FLAGS_SUB16:
        case FLAGS_INC16:
        case FLAGS_DEC16:
        case FLAGS_SHL16:
        case FLAGS_SHR16:
        case FLAGS_SAR16:
            msb   = 0x8000;
            width = 16;
            break;
        case FLAGS_ZN32:
        case FLAGS_ADD32:
        case FLAGS_SUB32:
        case FLAGS_INC32:
        case FLAGS_DEC32:
        case FLAGS_SHL32:
        case FLAGS_SHR32:
        case FLAGS_SAR32:
            msb   = 0x80000000;
            width = 32;
            break;
        default:
            return 0;
    }
    is_add    = (op == FLAGS_ADD8) || (op == FLAGS_ADD16) || (op == FLAGS_ADD32) || (op == FLAGS_INC8) || (op == FLAGS_INC16) || (op == FLAGS_INC32);
    is_sub    = (op == FLAGS_SUB8) || (op == FLAGS_SUB16) || (op == FLAGS_SUB32) || (op == FLAGS_DEC8) || (op == FLAGS_DEC16) || (op == FLAGS_DEC32);
    is_incdec = (op == FLAGS_INC8) || (op == FLAGS_INC16) || (op == FLAGS_INC32) || (op == FLAGS_DEC8) || (op == FLAGS_DEC16) || (op == FLAGS_DEC32);
    is_shl    = (op == FLAGS_SHL8) || (op == FLAGS_SHL16) || (op == FLAGS_SHL32);
    is_shr    = (op == FLAGS_SHR8) || (op == FLAGS_SHR16) || (op == FLAGS_SHR32);
    is_sar    = (op == FLAGS_SAR8) || (op == FLAGS_SAR16) || (op == FLAGS_SAR32);

    /*flags_rebuild_c() leaves CF as it is after INC and DEC*/
    if (is_incdec && carry_only) {
        codegen_flags_known_use(ir, 1);
        return 1;
    }

    /*Room for this sequence's register versions (at most 20 of temp1, 4 of
      temp3, 8 of flags) without passing REG_VERSION_MAX, which only ends the
      block after the current instruction*/
    if ((reg_last_version[IREG_GET_REG(IREG_temp1)] + 20 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_temp3)] + 4 > REG_VERSION_MAX) ||
        (reg_last_version[IREG_GET_REG(IREG_flags)] + 8 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_flags_op)] + 1 > REG_VERSION_MAX))
        return 0;

    if (is_shl || is_shr || is_sar) {
        count = flags_known_count(ir, op);
        if (!count)
            return 0;
    }

    /*With the uop that set the flags op, for codegen_can_unroll_full()*/
    if (!flags_known_record(ir, 0))
        return 0;

    /*CF into temp1; INC and DEC keep the one in flags*/
    if (op == FLAGS_ADD8 || op == FLAGS_ADD16) {
        uop_ADD(ir, IREG_temp1, IREG_flags_op1, IREG_flags_op2);
        uop_AND_IMM(ir, IREG_temp1, IREG_temp1, (op == FLAGS_ADD8) ? 0x100 : 0x10000);
        uop_SHR_IMM(ir, IREG_temp1, IREG_temp1, (op == FLAGS_ADD8) ? 8 : 16);
    } else if (op == FLAGS_ADD32)
        uop_CMP_SET(ir, IREG_temp1, IREG_flags_res, IREG_flags_op1, UOP_CC_B);
    else if (is_sub && !is_incdec)
        uop_CMP_SET(ir, IREG_temp1, IREG_flags_op1, IREG_flags_op2, UOP_CC_B);
    else if (is_shl || is_shr || is_sar) {
        /*The bit of op1 shifted out last. SHL: bit width-count of
          op1 << (count - 1), none past the width. SAR: the sign
          extension of op1 stands for every bit above width-1*/
        if (is_shl)
            cf_bit = (count <= (uint32_t) width) ? (width - (int) count) : -1;
        else if (is_shr)
            cf_bit = (int) count - 1;
        else
            cf_bit = ((int) count - 1 < width - 1) ? ((int) count - 1) : (width - 1);
        if (cf_bit > 0) {
            uop_SHR_IMM(ir, IREG_temp1, IREG_flags_op1, cf_bit);
            uop_AND_IMM(ir, IREG_temp1, IREG_temp1, 1);
        } else if (cf_bit == 0)
            uop_AND_IMM(ir, IREG_temp1, IREG_flags_op1, 1);
    }

    if (carry_only) {
        uop_AND_IMM(ir, IREG_flags, IREG_flags, (uint16_t) ~C_FLAG);
        if (is_add || is_sub || (cf_bit >= 0))
            uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);
        return 1;
    }

    if (is_incdec)
        uop_AND_IMM(ir, IREG_flags, IREG_flags, (uint16_t) ~(0x8d5 & ~C_FLAG));
    else {
        uop_AND_IMM(ir, IREG_flags, IREG_flags, (uint16_t) ~0x8d5);
        if (is_add || is_sub || (cf_bit >= 0))
            uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);
    }

    /*VF of a shift, from msb to bit 11, before PF uses temp3. SHL:
      ((op1 << count) ^ (op1 << (count - 1))) & msb, 0 past the width.
      SHR: msb of op1 when count is 1. SAR: 0*/
    if (is_shl && (count <= (uint32_t) width)) {
        int src = IREG_flags_op1;

        if (count > 1) {
            uop_SHL_IMM(ir, IREG_temp3, IREG_flags_op1, count - 1);
            src = IREG_temp3;
        }
        uop_SHL_IMM(ir, IREG_temp1, src, 1);
        uop_XOR(ir, IREG_temp1, IREG_temp1, src);
        uop_AND_IMM(ir, IREG_temp1, IREG_temp1, msb);
    } else if (is_shr && (count == 1))
        uop_AND_IMM(ir, IREG_temp1, IREG_flags_op1, msb);
    if ((is_shl && (count <= (uint32_t) width)) || (is_shr && (count == 1))) {
        if (msb == 0x80)
            uop_SHL_IMM(ir, IREG_temp1, IREG_temp1, 4);
        else
            uop_SHR_IMM(ir, IREG_temp1, IREG_temp1, (msb == 0x8000) ? 4 : 20);
        uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);
    }

    /*PF: even parity of the low byte of flags_res, as znptable8[]*/
    uop_SHR_IMM(ir, IREG_temp1, IREG_flags_res, 4);
    uop_XOR(ir, IREG_temp1, IREG_temp1, IREG_flags_res);
    uop_SHR_IMM(ir, IREG_temp3, IREG_temp1, 2);
    uop_XOR(ir, IREG_temp1, IREG_temp1, IREG_temp3);
    uop_SHR_IMM(ir, IREG_temp3, IREG_temp1, 1);
    uop_XOR(ir, IREG_temp1, IREG_temp1, IREG_temp3);
    uop_AND_IMM(ir, IREG_temp1, IREG_temp1, 1);
    uop_XOR_IMM(ir, IREG_temp1, IREG_temp1, 1);
    uop_SHL_IMM(ir, IREG_temp1, IREG_temp1, 2);
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);

    /*AF: ((op1 & 0xf) +/- (op2 & 0xf)) & 0x10*/
    if (is_add || is_sub) {
        uop_AND_IMM(ir, IREG_temp1, IREG_flags_op1, 0xf);
        uop_AND_IMM(ir, IREG_temp3, IREG_flags_op2, 0xf);
        if (is_add)
            uop_ADD(ir, IREG_temp1, IREG_temp1, IREG_temp3);
        else
            uop_SUB(ir, IREG_temp1, IREG_temp1, IREG_temp3);
        uop_AND_IMM(ir, IREG_temp1, IREG_temp1, A_FLAG);
        uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);
    }

    /*ZF: !flags_res*/
    uop_TEST_SET(ir, IREG_temp1, IREG_flags_res, UOP_CC_Z);
    uop_SHL_IMM(ir, IREG_temp1, IREG_temp1, 6);
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);

    /*NF: flags_res & msb, moved to bit 7*/
    uop_AND_IMM(ir, IREG_temp1, IREG_flags_res, msb);
    if (msb != 0x80)
        uop_SHR_IMM(ir, IREG_temp1, IREG_temp1, (msb == 0x8000) ? 8 : 24);
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);

    /*VF, moved from msb to bit 11. Add: !((op1 ^ op2) & msb) &&
      ((op1 ^ res) & msb). Sub: (op1 ^ op2) & (op1 ^ res) & msb.*/
    if (is_add || is_sub) {
        uop_XOR(ir, IREG_temp1, IREG_flags_op1, IREG_flags_op2);
        if (is_add)
            uop_XOR_IMM(ir, IREG_temp1, IREG_temp1, msb);
        uop_XOR(ir, IREG_temp3, IREG_flags_op1, IREG_flags_res);
        uop_AND(ir, IREG_temp1, IREG_temp1, IREG_temp3);
        uop_AND_IMM(ir, IREG_temp1, IREG_temp1, msb);
        if (msb == 0x80)
            uop_SHL_IMM(ir, IREG_temp1, IREG_temp1, 4);
        else
            uop_SHR_IMM(ir, IREG_temp1, IREG_temp1, (msb == 0x8000) ? 4 : 20);
        uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_UNKNOWN);
    return 1;
#else
    /*Other backends lack UOP_CMP_SET and UOP_TEST_SET*/
    return 0;
#endif
}

#define UNROLL_MAX_REG_REFERENCES 200
#define UNROLL_MAX_UOPS           1000
#define UNROLL_MAX_COUNT          10

/*A uop of the FPU pointer record that codegen_fpu_handover emits between its
  call and its exit*/
static int
codegen_uop_is_fpu_record(const uop_t *uop)
{
    if ((uop->type & UOP_MASK) == (UOP_MOV_IMM & UOP_MASK)) {
        int reg = IREG_GET_REG(uop->dest_reg_a.reg);

        return (reg == IREG_fpu_op) || (reg == IREG_fpu_CS) || (reg == IREG_fpu_cs) || (reg == IREG_fpu_pc);
    }
    return ((uop->type & UOP_MASK) == (UOP_CALL_FUNC & UOP_MASK)) && (uop->p == (void *) fpu_postamble);
}

int
codegen_can_unroll_full(codeblock_t *block, ir_data_t *ir, UNUSED(uint32_t next_pc), uint32_t dest_addr)
{
    int start;
    int max_unroll;
    int first_instruction;
    int TOP = -1;
    int head_fpu_entered;
    int head_fpu_es_check;
    int head_mmx_entered;
    uint8_t head_tag_valid;
    uint8_t head_tag_empty;

    /*Check that dest instruction was actually compiled into block*/
    start = codegen_get_instruction_uop(block, dest_addr, &first_instruction, &TOP);

    /*Couldn't find any uOPs corresponding to the destination instruction*/
    if (start == -1) {
        /*Is instruction jumping to itself?*/
        if (dest_addr != cpu_state.oldpc) {
            return 0;
        } else {
            start = ir->wr_pos;
            TOP   = cpu_state.TOP;
        }
    }

    if (TOP != cpu_state.TOP)
        return 0;

    /*An x87 or MMX instruction compiled after the block has entered the FPU
      or MMX leaves out its entry check (and MMX's, its setting of every tag
      valid), and the copies run the loop's first instruction again after its
      last. So an entry in force when the loop's first instruction was
      compiled must still be in force at the branch: an x87 instruction ends
      the MMX one, an MMX instruction the x87 one, and EMMS, FEMMS, LMSW and an
      interpreted x87 or 0F instruction both. A pending exception check left
      out there must be one the branch can leave out too: not after a compiled
      instruction that can set ES and go on (codegen_fpu_es_check). Not found,
      first_instruction is this instruction, whose state is the current one*/
    codegen_get_instruction_entered(first_instruction, &head_fpu_entered, &head_fpu_es_check, &head_mmx_entered);
    if ((head_mmx_entered && !codegen_mmx_entered) || (head_fpu_entered && !codegen_fpu_entered))
        return 0;
    if (head_fpu_entered && !head_fpu_es_check && codegen_fpu_es_check)
        return 0;

    /*A stack check left out because a tag was known valid or empty when the
      loop's first instruction was compiled is left out of the copies too:
      what was known there must still be known at the branch*/
    codegen_get_instruction_tags(first_instruction, &head_tag_valid, &head_tag_empty);
    if ((head_tag_valid & ~codegen_fpu_tag_valid) || (head_tag_empty & ~codegen_fpu_tag_empty))
        return 0;

    /*Not across a flags rebuild or flag reader worked out from a flags op
      set before the loop when the loop sets one itself: the copies would
      repeat it after the loop's own flag changes*/
    if (flags_known_full)
        return 0;
    for (int c = 0; c < flags_known_nr; c++) {
        if ((flags_known_site[c] >= start) && (flags_known_producer[c] < start) && flags_op_set_from(ir, start, !flags_known_cf_kept[c]))
            return 0;
    }

    /*Not across an instruction the interpreter runs in the loop. One that
      only a hand-over path runs, which leaves the block straight after it,
      never returns into the loop*/
    for (int c = start; c < ir->wr_pos; c++) {
        /*Segment checks were built for the segments at compile time. A copy
          after a segment load would use those checks with the new segment*/
        if ((ir->uops[c].type & UOP_MASK) == (UOP_LOAD_SEG & UOP_MASK))
            return 0;
        /*An x87 or MMX instruction after the block has entered leaves out its
          entry check, and LMSW can set EM or TS: a copy after it would run
          such an instruction unchecked. Found by its call, as the segment
          loads are by their uop, so nothing has to mark it*/
        if (((ir->uops[c].type & UOP_MASK) == (UOP_CALL_FUNC & UOP_MASK)) && (ir->uops[c].p == (void *) dynarec_lmsw))
            return 0;
        /*Nor across a compiled MOV CRx or MOV DRx, as across the hand-over
          they replace: a CR3 or CR4 write can change the translation the
          loop's code was read through, which a copy would not see*/
        if ((((ir->uops[c].type & UOP_MASK) == (UOP_CALL_FUNC & UOP_MASK)) || ((ir->uops[c].type & UOP_MASK) == (UOP_CALL_FUNC_RESULT & UOP_MASK))) &&
            ((ir->uops[c].p == (void *) dynarec_mov_cr0) || (ir->uops[c].p == (void *) dynarec_mov_cr2) || (ir->uops[c].p == (void *) dynarec_mov_cr3) ||
             (ir->uops[c].p == (void *) dynarec_mov_cr4) || (ir->uops[c].p == (void *) dynarec_mov_drx)))
            return 0;
        /*x87 arithmetic is built for the precision and rounding classes of the
          control word it was compiled under, checked only by the first such
          instruction after a change: a copy after a compiled FLDCW would run
          it under the new word unchecked*/
        if ((ir->uops[c].type & UOP_MASK) == (UOP_FP_CONTROL_SET & UOP_MASK))
            return 0;
        /*Nor across the second page's fetch walk, made where the interpreter
          first fetches from that page: its later passes stay on the page and
          walk no more, which a copy would*/
        if ((((ir->uops[c].type & UOP_MASK) == (UOP_CALL_INSTRUCTION_FUNC & UOP_MASK)) || ((ir->uops[c].type & UOP_MASK) == (UOP_CALL_FUNC_RESULT & UOP_MASK))) &&
            (ir->uops[c].p == (void *) dynarec_fetch_walk))
            return 0;
        if ((ir->uops[c].type & UOP_MASK) == (UOP_CALL_INSTRUCTION_FUNC & UOP_MASK)) {
            /*The x87 hand-over's pointer record, between its call and its
              exit, sets no compile-time state: stepped over, as is the RF
              handling after the call (dynarec_rf_after_op)*/
            int next = c + 1;

            while ((next < ir->wr_pos) && (codegen_uop_is_fpu_record(&ir->uops[next]) ||
                                           (((ir->uops[next].type & UOP_MASK) == (UOP_CALL_FUNC & UOP_MASK)) && (ir->uops[next].p == (void *) dynarec_rf_after_op))))
                next++;
            if (!((next < ir->wr_pos) && ((ir->uops[next].type & UOP_MASK) == (UOP_JMP & UOP_MASK))))
                return 0;
        }
    }

    max_unroll = UNROLL_MAX_UOPS / ((ir->wr_pos - start) + 6);
    if ((max_version_refcount != 0) && (max_unroll > (UNROLL_MAX_REG_REFERENCES / max_version_refcount)))
        max_unroll = (UNROLL_MAX_REG_REFERENCES / max_version_refcount);
    if (max_unroll > UNROLL_MAX_COUNT)
        max_unroll = UNROLL_MAX_COUNT;
    if (max_unroll <= 1)
        return 0;

    codegen_ir_set_unroll(max_unroll, start, first_instruction);
    /*Each run goes round the loop several times: one run from block->data
      pays for the compile (codeblock_t::runs_left).*/
    block->runs_left = 1;

    return 1;
}

/*#UD, before any operand access, as the interpreter raises it: x86illegal
  delivers it at once from oldpc (no abrt), so the compile pass keeps the
  block and ends it with this instruction, and the block's own end follows,
  linked to the handler's CS:EIP*/
uint32_t
codegen_ud(ir_data_t *ir)
{
    codegen_accumulate_flush(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_CALL_FUNC(ir, x86illegal);

    return -1;
}

/*The largest value the 32-bit addr_reg can hold at this point, from the uop
  that wrote it: a constant (*is_const set), an AND with a mask below 64K, or
  a zero extension of a byte or word. That uop must belong to the instruction
  being recompiled, so that an unrolled copy repeats it, and no jump may land
  between it and here. Returns 0 when nothing is known*/
int
codegen_addr_bound(ir_data_t *ir, int addr_reg, uint32_t *bound, int *is_const)
{
    const int reg     = IREG_GET_REG(addr_reg);
    const int version = reg_last_version[reg];
    int       parent;
    uop_t    *uop;

    if ((IREG_GET_SIZE(addr_reg) != IREG_SIZE_L) || !version)
        return 0;
    parent = reg_version[reg][version].parent_uop;
    if ((parent < codegen_instr_first_uop) || (parent >= ir->wr_pos))
        return 0;
    uop = &ir->uops[parent];
    if ((IREG_GET_REG(uop->dest_reg_a.reg) != reg) || (IREG_GET_SIZE(uop->dest_reg_a.reg) != IREG_SIZE_L) || (uop->dest_reg_a.version != version))
        return 0;
    for (int c = 0; c < ir->wr_pos; c++) {
        if ((ir->uops[c].type & UOP_TYPE_JUMP) && (ir->uops[c].jump_dest_uop > parent) && (ir->uops[c].jump_dest_uop <= ir->wr_pos))
            return 0;
    }

    *is_const = 0;
    switch (uop->type & UOP_MASK) {
        case UOP_MOV_IMM & UOP_MASK:
            *bound    = (uint32_t) uop->imm_data;
            *is_const = 1;
            return 1;
        case UOP_AND_IMM & UOP_MASK:
            if ((uint32_t) uop->imm_data > 0xffff)
                return 0;
            *bound = (uint32_t) uop->imm_data;
            return 1;
        case UOP_MOVZX & UOP_MASK:
            switch (IREG_GET_SIZE(uop->src_reg_a.reg)) {
                case IREG_SIZE_W:
                    *bound = 0xffff;
                    return 1;
                case IREG_SIZE_B:
                case IREG_SIZE_BH:
                    *bound = 0xff;
                    return 1;
                default:
                    return 0;
            }
        default:
            return 0;
    }
}

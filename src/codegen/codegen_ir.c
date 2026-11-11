#include <stdint.h>
#include <string.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "codegen.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_ir.h"
#include "codegen_accumulate.h"
#include "codegen_reg.h"
#include "x86.h"
#include "x86_flags.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_jit_wrappers.h"

extern int       has_ea;
static ir_data_t ir_block;

static int codegen_unroll_start;
static int codegen_unroll_count;
static int codegen_unroll_first_instruction;
static int codegen_unroll_branch;

extern int codegen_get_loop_timing(int first_instruction);

#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
/*Per uop, 1 for the inline ES test codegen_generate_call adds in front of a
  no-wait x87 instruction's FERR# call (and its copies in an unrolled loop):
  left out of what decides the unrolling and of the dead-flags pass's joins,
  so that both decide as they did with the bare call. Cleared up to
  uncounted_end as a block starts*/
static uint8_t codegen_uop_uncounted[UOP_NR_MAX];
static int     uncounted_end;

void
codegen_ir_set_uncounted(int c)
{
    codegen_uop_uncounted[c] = 1;
    if (c >= uncounted_end)
        uncounted_end = c + 1;
}

int
codegen_ir_uncounted_since(ir_data_t *ir, int start)
{
    int nr = 0;

    for (int c = start; c < ir->wr_pos; c++)
        nr += codegen_uop_uncounted[c];
    return nr;
}
#endif

ir_data_t *
codegen_ir_init(void)
{
    ir_block.wr_pos = 0;
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
    memset(codegen_uop_uncounted, 0, uncounted_end);
    uncounted_end = 0;
#endif

    codegen_unroll_count = 0;
    codegen_accumulate_begin();

    return &ir_block;
}

int
codegen_ir_unrolling(void)
{
    return codegen_unroll_count;
}

void
codegen_ir_set_unroll(int count, int start, int first_instruction)
{
    codegen_unroll_count             = count;
    codegen_unroll_start             = start;
    codegen_unroll_first_instruction = first_instruction;
    codegen_unroll_branch            = ir_block.wr_pos;
}

static void
duplicate_uop(ir_data_t *ir, uop_t *uop, int offset)
{
    uop_t *new_uop = uop_alloc_unroll(ir, uop->type);

    if (!ir_reg_is_invalid(uop->src_reg_a))
        new_uop->src_reg_a = codegen_reg_read(uop->src_reg_a.reg);
    if (!ir_reg_is_invalid(uop->src_reg_b))
        new_uop->src_reg_b = codegen_reg_read(uop->src_reg_b.reg);
    if (!ir_reg_is_invalid(uop->src_reg_c))
        new_uop->src_reg_c = codegen_reg_read(uop->src_reg_c.reg);
    if (!ir_reg_is_invalid(uop->dest_reg_a))
        new_uop->dest_reg_a = codegen_reg_write(uop->dest_reg_a.reg, ir->wr_pos - 1);

    new_uop->type      = uop->type;
    new_uop->imm_data  = uop->imm_data;
    new_uop->imm_data2 = uop->imm_data2;
    new_uop->p         = uop->p;
    new_uop->pc        = uop->pc;
#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM || defined __aarch64__ || defined _M_ARM64 || defined __loongarch_lp64
    new_uop->is_a16   = uop->is_a16;
#endif

    if (uop->jump_dest_uop != -1) {
        new_uop->jump_dest_uop = uop->jump_dest_uop + offset;
    }
}

/*A copy of a flag read compiled in place of a call (codegen_flags_read_t).
  Where the loop starts after the uop that set the flags op it was compiled
  for, the copy runs after the loop's own flag changes and calls the
  function, as the copy of the call would. Otherwise it is the same uop,
  with the call's effects on the counts: its reads uncounted, the registers
  marked as the copy of the call's barrier marks them. Returns the register
  the next uop must test in place of the one it tests (a pair whose copy
  calls func), -1 when the next uop is copied as it is*/
static int
duplicate_flags_read(ir_data_t *ir, uop_t *uop, const codegen_flags_read_t *rd)
{
    if ((rd->producer >= 0) && (rd->producer < codegen_unroll_start)) {
#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
        const uint32_t type = UOP_CALL_FUNC_RESULT_ORDERED;
#else
        const uint32_t type = UOP_CALL_FUNC_RESULT;
#endif
        const int dest    = rd->pair ? IREG_GET_REG(uop->dest_reg_a.reg) : uop->dest_reg_a.reg;
        uop_t    *new_uop = uop_alloc_unroll(ir, type);

        new_uop->dest_reg_a = codegen_reg_write(dest, ir->wr_pos - 1);
        new_uop->type       = type;
        new_uop->imm_data   = 0;
        new_uop->imm_data2  = uop->imm_data2;
        new_uop->p          = rd->func;
        new_uop->pc         = uop->pc;
#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM || defined __aarch64__ || defined _M_ARM64 || defined __loongarch_lp64
        new_uop->is_a16 = 0;
#endif
        return rd->pair ? dest : -1;
    }
    codegen_reg_mark_as_required();
    if (!ir_reg_is_invalid(uop->src_reg_a))
        codegen_reg_read_uncounted(uop->src_reg_a.reg);
    if (!ir_reg_is_invalid(uop->src_reg_b))
        codegen_reg_read_uncounted(uop->src_reg_b.reg);
    duplicate_uop(ir, uop, 0);
    return -1;
}

#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
/*An order barrier whose C, if any, reads none of the guest's state on the
  way on, and whose only branch leaves the block for a fault: an access, or
  a check that branches to a fault route. Everything else that orders (the
  ordered calls, the REP routine, the SMI and x87 exits, the jumps whose
  join expects memory current) keeps the full flush.*/
static int
codegen_uop_is_lazy(const uop_t *uop)
{
    switch (uop->type & UOP_MASK) {
        case UOP_MEM_LOAD_ABS & UOP_MASK:
        case UOP_MEM_LOAD_REG & UOP_MASK:
        case UOP_MEM_LOAD_REG_EA & UOP_MASK:
        case UOP_MEM_LOAD_SINGLE & UOP_MASK:
        case UOP_MEM_LOAD_DOUBLE & UOP_MASK:
        case UOP_MEM_STORE_ABS & UOP_MASK:
        case UOP_MEM_STORE_REG & UOP_MASK:
        case UOP_MEM_STORE_IMM_8 & UOP_MASK:
        case UOP_MEM_STORE_IMM_16 & UOP_MASK:
        case UOP_MEM_STORE_IMM_32 & UOP_MASK:
        case UOP_MEM_STORE_SINGLE & UOP_MASK:
        case UOP_MEM_STORE_DOUBLE & UOP_MASK:
            return 1;

        case UOP_CMP_JNWITHIN & UOP_MASK:
        case UOP_CMP_IMM_JZ & UOP_MASK:
        case UOP_CMP_JB & UOP_MASK:
        case UOP_CMP_JNBE & UOP_MASK:
        case UOP_CMN_IMM_JC & UOP_MASK:
            return (uop->p == codegen_gpf_rout) || (uop->p == codegen_ss_rout) || (uop->p == codegen_exit_rout);

        default:
            return 0;
    }
}
#endif

/*Flags rebuilds whose result nothing can see, left out of the block once it
  is generated and unrolled: the C rebuild calls (with the reload of flags
  and the flags_op write after them) and the inline rebuilds of
  codegen_flags_rebuild_known(). A rebuild writes some bits of flags and,
  unless it only rebuilds CF, sets flags_op to FLAGS_UNKNOWN. It is left out
  when, on every path on from it:
  - (flags_op written) no uop can see flags_op before a later uop writes it;
  - (flags) each bit it wrote is rewritten without being read, or is one the
    flags op written after it computes from flags_res/op1/op2 alone, before
    anything can read that bit. When the flags op changes, the bits the old
    op computed and the new one does not are rewritten first, so such a bit
    never shows the rebuild's value again.
  A memory access, a C call, any exit, the block end and every read of flags
  or flags_op count as reads; an AND, OR or XOR of flags with itself keeps
  the bits in place. A uop an in-block jump may skip only narrows what is
  known. A rebuild between a jump and its destination stays, and so does a
  call that is the first full barrier after a join: the join counts on it.*/
enum {
    FLAGS_SITE_CALL_FULL,
    FLAGS_SITE_CALL_C,
    FLAGS_SITE_CALL_ZPNA,
    FLAGS_SITE_INLINE,
    FLAGS_SITE_READ
};

#define FLAGS_SITE_MAX 1024
static struct {
    int      start;
    int      end;
    int      kind;
    int      valid;
    int      kept;
    int      removed;
    uint16_t written;
} flags_site[FLAGS_SITE_MAX];
static int     flags_site_nr;
/*Per uop: 1 + the site it belongs to*/
static int16_t flags_site_of[UOP_NR_MAX];
static uint8_t flags_jump_dest[UOP_NR_MAX];
/*Per uop, noted before the dead list removes any: 1 + the op of a MOV_IMM
  of flags_op, -1 for any other write of flags_op, 0 otherwise*/
static int16_t flags_op_write[UOP_NR_MAX];

#define FLAGS_UNROLL_MAX 16
static int flags_unroll_off[FLAGS_UNROLL_MAX];
static int flags_unroll_nr;

/*The bits of flags the op computes from flags_res/op1/op2 alone*/
static uint16_t
flags_op_computes(int op)
{
    switch (op) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
        case FLAGS_ADD8:
        case FLAGS_ADD16:
        case FLAGS_ADD32:
        case FLAGS_SUB8:
        case FLAGS_SUB16:
        case FLAGS_SUB32:
        case FLAGS_SHL8:
        case FLAGS_SHL16:
        case FLAGS_SHL32:
        case FLAGS_SHR8:
        case FLAGS_SHR16:
        case FLAGS_SHR32:
        case FLAGS_SAR8:
        case FLAGS_SAR16:
        case FLAGS_SAR32:
        case FLAGS_ADC8:
        case FLAGS_ADC16:
        case FLAGS_ADC32:
        case FLAGS_SBC8:
        case FLAGS_SBC16:
        case FLAGS_SBC32:
        case FLAGS_SHRD16:
        case FLAGS_SHRD32:
            return 0x8d5;
        case FLAGS_INC8:
        case FLAGS_INC16:
        case FLAGS_INC32:
        case FLAGS_DEC8:
        case FLAGS_DEC16:
        case FLAGS_DEC32:
            return 0x8d5 & ~C_FLAG;
        case FLAGS_ROL8:
        case FLAGS_ROL16:
        case FLAGS_ROL32:
        case FLAGS_ROR8:
        case FLAGS_ROR16:
        case FLAGS_ROR32:
            return C_FLAG | V_FLAG;
        default:
            return 0;
    }
}

static void
codegen_flags_dead_note_ops(ir_data_t *ir)
{
    for (int c = 0; c < ir->wr_pos; c++) {
        const uop_t *uop = &ir->uops[c];

        flags_op_write[c] = 0;
        if (ir_reg_is_invalid(uop->dest_reg_a) || (IREG_GET_REG(uop->dest_reg_a.reg) != IREG_GET_REG(IREG_flags_op)))
            continue;
        if (((uop->type & UOP_MASK) == (UOP_MOV_IMM & UOP_MASK)) && reg_is_native_size(uop->dest_reg_a) && (uop->imm_data <= FLAGS_SHRD32))
            flags_op_write[c] = 1 + (int) uop->imm_data;
        else
            flags_op_write[c] = -1;
    }
}

static int
ir_reg_is(ir_reg_t ir_reg, int reg)
{
    return !ir_reg_is_invalid(ir_reg) && (IREG_GET_REG(ir_reg.reg) == IREG_GET_REG(reg));
}

/*How a uop outside the sites meets flags: 0 not at all, 1 reads flags or
  flags_op, 2 keeps every bit of flags in place, 3 clears the bits of flags
  not in *keep, 4 writes the whole of flags*/
static int
flags_dead_use(const uop_t *uop, uint16_t *keep)
{
    uint32_t t   = uop->type & UOP_MASK;
    int      dst = ir_reg_is(uop->dest_reg_a, IREG_flags);
    int      a   = ir_reg_is(uop->src_reg_a, IREG_flags);
    int      b   = ir_reg_is(uop->src_reg_b, IREG_flags);
    int      c   = ir_reg_is(uop->src_reg_c, IREG_flags);

    if ((t == (UOP_MOV_REG_PTR & UOP_MASK)) || (t == (UOP_MOVZX_REG_PTR_8 & UOP_MASK)) || (t == (UOP_MOVZX_REG_PTR_16 & UOP_MASK))) {
        if ((uop->p == (void *) &cpu_state.flags) || (uop->p == (void *) &cpu_state.flags_op))
            return 1;
    }
    if (ir_reg_is(uop->src_reg_a, IREG_flags_op) || ir_reg_is(uop->src_reg_b, IREG_flags_op) || ir_reg_is(uop->src_reg_c, IREG_flags_op))
        return 1;
    if (!dst && !a && !b && !c)
        return 0;
    if (dst && reg_is_native_size(uop->dest_reg_a) && a && !b && !c) {
        if (t == (UOP_AND_IMM & UOP_MASK)) {
            *keep = (uint16_t) uop->imm_data;
            return 3;
        }
        if ((t == (UOP_OR_IMM & UOP_MASK)) || (t == (UOP_XOR_IMM & UOP_MASK)) || (t == (UOP_OR & UOP_MASK)))
            return 2;
    }
    if (dst && reg_is_native_size(uop->dest_reg_a) && !a && !b && !c)
        return 4;
    return 1;
}

static int
flags_site_left_out(int s)
{
    return !flags_site[s].valid || flags_site[s].removed;
}

/*Whether site s can be left out (section comment above)*/
static int
flags_site_is_dead(ir_data_t *ir, int s)
{
    uint16_t vis    = flags_site[s].written;
    uint16_t shadow = 0;
    int      op     = (flags_site[s].kind == FLAGS_SITE_CALL_C) || ((flags_site[s].kind == FLAGS_SITE_INLINE) && (flags_site[s].written == C_FLAG)) ? -1 : FLAGS_UNKNOWN;
    int      pend   = -1;
    /*Until flags_op is written on every path*/
    int      op_pending = (op == FLAGS_UNKNOWN);

    for (int c = flags_site[s].end; c < ir->wr_pos; c++) {
        const uop_t *uop = &ir->uops[c];
        uint32_t     t   = uop->type & UOP_MASK;
        int          new_op;
        int          r;

#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
        /*The FERR# test touches no flags state and its branch skips only
          the call: passed over, as the bare call was*/
        if (codegen_uop_uncounted[c])
            continue;
#endif
        if (flags_site_of[c]) {
            int      s2   = flags_site_of[c] - 1;
            int      k2   = flags_site[s2].kind;
            int      at   = flags_site[s2].start;
            uint16_t w    = 0;
            int      sets = 0;

            c = flags_site[s2].end - 1;
            if (!flags_site[s2].valid)
                continue;
            if (k2 == FLAGS_SITE_READ) {
                if (op_pending || (vis & ~shadow & flags_site[s2].written))
                    return 0;
                continue;
            }
            if (op_pending) {
                if (flags_site_left_out(s2))
                    continue;
                return 0;
            }
            if (k2 == FLAGS_SITE_INLINE) {
                /*Its bits come from the op it was compiled for*/
                w    = flags_site[s2].written;
                sets = (w != C_FLAG);
            } else if ((op >= 0) && (op != FLAGS_UNKNOWN)) {
                /*Only the bits the op computes are new: the rest are
                  read from flags and written back as they were*/
                w    = flags_op_computes(op) & flags_site[s2].written;
                sets = (k2 != FLAGS_SITE_CALL_C);
            } else {
                /*No op pending, or not known: a call that may change
                  nothing passes the bits on while it is kept*/
                if (!flags_site_left_out(s2) && (vis & ~shadow))
                    return 0;
                sets = (k2 != FLAGS_SITE_CALL_C);
            }
            if (at >= pend)
                vis &= ~w;
            if (sets) {
                if (at >= pend) {
                    shadow = 0;
                    op     = FLAGS_UNKNOWN;
                } else {
                    shadow = 0;
                    op     = (op == FLAGS_UNKNOWN) ? op : -1;
                }
            }
            if (!(vis & ~shadow))
                return 1;
            continue;
        }
        new_op = flags_op_write[c] ? ((flags_op_write[c] > 0) ? (flags_op_write[c] - 1) : -1) : -2;
        if (new_op != -2) {
            uint16_t computes = (new_op < 0) ? 0 : flags_op_computes(new_op);

            if (c >= pend) {
                shadow     = computes;
                op         = new_op;
                op_pending = 0;
            } else {
                shadow &= computes;
                op = (op == new_op) ? op : -1;
            }
            if (!op_pending && !(vis & ~shadow))
                return 1;
            if ((t == UOP_INVALID) || (flags_op_write[c] > 0))
                continue;
        }
        if (t == UOP_INVALID)
            continue;
        {
            uint16_t keep = 0xffff;

            switch (flags_dead_use(uop, &keep)) {
                case 1:
                    if (op_pending || (vis & ~shadow))
                        return 0;
                    break;
                case 3:
                    if (c >= pend)
                        vis &= keep;
                    break;
                case 4:
                    if (c >= pend)
                        vis = 0;
                    break;
                default:
                    break;
            }
            if (!op_pending && !(vis & ~shadow))
                return 1;
        }
        if ((uop->type & UOP_TYPE_JUMP) && (uop->jump_dest_uop >= 0)) {
            if (uop->jump_dest_uop > pend)
                pend = uop->jump_dest_uop;
            continue;
        }
        switch (t) {
            case UOP_LOAD_FUNC_ARG_0 & UOP_MASK:
            case UOP_LOAD_FUNC_ARG_1 & UOP_MASK:
            case UOP_LOAD_FUNC_ARG_2 & UOP_MASK:
            case UOP_LOAD_FUNC_ARG_3 & UOP_MASK:
            case UOP_LOAD_FUNC_ARG_0_IMM & UOP_MASK:
            case UOP_LOAD_FUNC_ARG_1_IMM & UOP_MASK:
            case UOP_LOAD_FUNC_ARG_2_IMM & UOP_MASK:
            case UOP_LOAD_FUNC_ARG_3_IMM & UOP_MASK:
            case UOP_NOP_BARRIER & UOP_MASK:
                r = 0;
                break;
            case UOP_CALL_INSTRUCTION_FUNC & UOP_MASK:
            case UOP_MEM_LOAD_ABS & UOP_MASK:
            case UOP_MEM_LOAD_REG & UOP_MASK:
            case UOP_MEM_LOAD_REG_EA & UOP_MASK:
            case UOP_MEM_LOAD_SINGLE & UOP_MASK:
            case UOP_MEM_LOAD_DOUBLE & UOP_MASK:
            case UOP_MEM_STORE_ABS & UOP_MASK:
            case UOP_MEM_STORE_REG & UOP_MASK:
            case UOP_MEM_STORE_IMM_8 & UOP_MASK:
            case UOP_MEM_STORE_IMM_16 & UOP_MASK:
            case UOP_MEM_STORE_IMM_32 & UOP_MASK:
            case UOP_MEM_STORE_SINGLE & UOP_MASK:
            case UOP_MEM_STORE_DOUBLE & UOP_MASK:
            case UOP_CALL_FUNC & UOP_MASK:
            case UOP_CALL_FUNC_ORDERED & UOP_MASK:
            case UOP_CALL_FUNC_RESULT & UOP_MASK:
            case UOP_CALL_FUNC_RESULT_ORDERED & UOP_MASK:
            case UOP_LOAD_SEG & UOP_MASK:
            case UOP_CALL_REP_STRING & UOP_MASK:
                r = 1;
                break;
            default:
                r = !!(uop->type & (UOP_TYPE_BARRIER | UOP_TYPE_ORDER_BARRIER));
                break;
        }
        if (r && (op_pending || (vis & ~shadow)))
            return 0;
    }
    return !op_pending && !(vis & ~shadow);
}

static void
flags_site_add(int start, int end, int kind, uint16_t written)
{
    if (flags_site_nr == FLAGS_SITE_MAX)
        return;
    flags_site[flags_site_nr].start   = start;
    flags_site[flags_site_nr].end     = end;
    flags_site[flags_site_nr].kind    = kind;
    flags_site[flags_site_nr].written = written;
    flags_site_nr++;
}

static void
flags_dead_unread(ir_reg_t ir_reg)
{
    if (!ir_reg_is_invalid(ir_reg))
        reg_version[IREG_GET_REG(ir_reg.reg)][ir_reg.version].refcount--;
}

/*Leaves site s out: every uop goes but the last write of flags, which
  becomes a move from the version before the site, so the uops after it read
  the value they would have read had the site changed nothing. 0, changing
  nothing, when a value the site makes is read after it some other way*/
static int
flags_site_remove(ir_data_t *ir, int s)
{
    int last_flags  = -1;
    int first_flags = -1;
    int before;

    for (int c = flags_site[s].start; c < flags_site[s].end; c++) {
        const uop_t *uop = &ir->uops[c];

        if (ir_reg_is(uop->dest_reg_a, IREG_flags)) {
            if (first_flags < 0)
                first_flags = c;
            if ((uop->type & UOP_MASK) != UOP_INVALID)
                last_flags = c;
        }
        if (((uop->type & UOP_MASK) == UOP_INVALID) || ir_reg_is_invalid(uop->dest_reg_a) || ir_reg_is(uop->dest_reg_a, IREG_flags))
            continue;
        {
            int reads = 0;

            for (int d = c + 1; d < flags_site[s].end; d++) {
                const uop_t *u = &ir->uops[d];

                if ((u->type & UOP_MASK) == UOP_INVALID)
                    continue;
                if (!ir_reg_is_invalid(u->src_reg_a) && (IREG_GET_REG(u->src_reg_a.reg) == IREG_GET_REG(uop->dest_reg_a.reg)) && (u->src_reg_a.version == uop->dest_reg_a.version))
                    reads++;
                if (!ir_reg_is_invalid(u->src_reg_b) && (IREG_GET_REG(u->src_reg_b.reg) == IREG_GET_REG(uop->dest_reg_a.reg)) && (u->src_reg_b.version == uop->dest_reg_a.version))
                    reads++;
                if (!ir_reg_is_invalid(u->src_reg_c) && (IREG_GET_REG(u->src_reg_c.reg) == IREG_GET_REG(uop->dest_reg_a.reg)) && (u->src_reg_c.version == uop->dest_reg_a.version))
                    reads++;
            }
            if (reg_version[IREG_GET_REG(uop->dest_reg_a.reg)][uop->dest_reg_a.version].refcount != reads)
                return 0;
        }
    }
    before = (first_flags < 0) ? -1 : (ir->uops[first_flags].dest_reg_a.version - 1);
    if ((last_flags >= 0) && (reg_version[IREG_GET_REG(IREG_flags)][before].flags & REG_FLAGS_DEAD))
        return 0;

    for (int c = flags_site[s].start; c < flags_site[s].end; c++) {
        uop_t *uop = &ir->uops[c];

        if ((uop->type & UOP_MASK) == UOP_INVALID)
            continue;
        flags_dead_unread(uop->src_reg_a);
        flags_dead_unread(uop->src_reg_b);
        flags_dead_unread(uop->src_reg_c);
        if (c == last_flags) {
            uop->type              = UOP_MOV;
            uop->src_reg_a.reg     = uop->dest_reg_a.reg;
            uop->src_reg_a.version = before;
            uop->src_reg_b         = invalid_ir_reg;
            uop->src_reg_c         = invalid_ir_reg;
            reg_version[IREG_GET_REG(IREG_flags)][before].refcount++;
        } else
            uop->type = UOP_INVALID;
    }
    return 1;
}

/*After the dead list: find the sites, judge them from the last back (so a
  later site's fate is known), and leave the dead ones out*/
static void
codegen_flags_dead_pass(ir_data_t *ir, int unroll_start, int unroll_end)
{
    const codegen_flags_inline_t *inl;
    int                           nr_inl   = codegen_flags_inline_sites(&inl);
    const codegen_flags_read_t   *rd;
    int                           nr_rd    = codegen_flags_read_sites(&rd);
    int                           maxdest  = -1;
    int                           join     = 0;
    int                           si       = 0;

    flags_site_nr = 0;
    memset(flags_site_of, 0, sizeof(int16_t) * ir->wr_pos);
    memset(flags_jump_dest, 0, ir->wr_pos);

    /*The inline sites by first uop, the unrolled copies included*/
    for (int i = 0; i < nr_inl; i++) {
        if (inl[i].end > ir->wr_pos)
            continue;
        flags_site_of[inl[i].start] = i + 1;
        if ((inl[i].start >= unroll_start) && (inl[i].end <= unroll_end)) {
            for (int k = 0; k < flags_unroll_nr; k++) {
                if ((inl[i].end + flags_unroll_off[k]) <= ir->wr_pos)
                    flags_site_of[inl[i].start + flags_unroll_off[k]] = i + 1;
            }
        }
    }
    /*The flag reads compiled in place of calls, as negative indexes: each is
      the read its call was, in the copies too*/
    for (int i = 0; i < nr_rd; i++) {
        if (rd[i].uop >= ir->wr_pos)
            continue;
        flags_site_of[rd[i].uop] = -(i + 1);
        if ((rd[i].uop >= unroll_start) && (rd[i].uop < unroll_end)) {
            for (int k = 0; k < flags_unroll_nr; k++) {
                if ((rd[i].uop + flags_unroll_off[k]) < ir->wr_pos)
                    flags_site_of[rd[i].uop + flags_unroll_off[k]] = -(i + 1);
            }
        }
    }
    for (int c = 0; c < ir->wr_pos; c++) {
        const uop_t *uop = &ir->uops[c];
        uint32_t     t   = uop->type & UOP_MASK;

        if (flags_site_of[c] < 0) {
            flags_site_add(c, c + 1, FLAGS_SITE_READ, rd[-flags_site_of[c] - 1].read);
            continue;
        }
        if (flags_site_of[c]) {
            const codegen_flags_inline_t *site = &inl[flags_site_of[c] - 1];
            int                           end  = c + site->end - site->start;

            flags_site_add(c, end, FLAGS_SITE_INLINE, site->written);
            c = end - 1;
            continue;
        }
        if ((t == (UOP_CALL_FUNC & UOP_MASK)) || (t == (UOP_CALL_FUNC_ORDERED & UOP_MASK))) {
            int kind;
            int end = c + 1;

            /*SAHF's jit_flags_rebuild_v rebuilds only OF, but SAHF writes
              bits 0-7 next with nothing between that reads or can fault:
              the same site as the full rebuild*/
            if ((uop->p == (void *) jit_flags_rebuild) || (uop->p == (void *) jit_flags_rebuild_v))
                kind = FLAGS_SITE_CALL_FULL;
            else if (codegen_flags_is_rebuild_c(uop->p))
                kind = FLAGS_SITE_CALL_C;
            else if (codegen_flags_is_rebuild_zpna(uop->p))
                kind = FLAGS_SITE_CALL_ZPNA;
            else
                continue;
            /*The reload of flags and the flags_op write after the call*/
            if ((end < ir->wr_pos) && ((ir->uops[end].type & UOP_MASK) == (UOP_MOVZX_REG_PTR_16 & UOP_MASK)) &&
                ir_reg_is(ir->uops[end].dest_reg_a, IREG_flags) && (ir->uops[end].p == (void *) &cpu_state.flags))
                end++;
            if ((end < ir->wr_pos) && (flags_op_write[end] == 1 + FLAGS_UNKNOWN))
                end++;
            flags_site_add(c, end, kind,
                           (kind == FLAGS_SITE_CALL_FULL) ? 0x8d5 : (kind == FLAGS_SITE_CALL_C) ? C_FLAG : (0x8d5 & ~(C_FLAG | V_FLAG)));
            c = end - 1;
        } else if ((t == (UOP_CALL_FUNC_RESULT & UOP_MASK)) || (t == (UOP_CALL_FUNC_RESULT_ORDERED & UOP_MASK))) {
            uint16_t bit;

            if (uop->p == (void *) jit_CF_SET)
                bit = C_FLAG;
            else if (uop->p == (void *) jit_VF_SET)
                bit = V_FLAG;
            else if (uop->p == (void *) jit_ZF_SET)
                bit = Z_FLAG;
            else if (uop->p == (void *) jit_NF_SET)
                bit = N_FLAG;
            else if (uop->p == (void *) jit_PF_SET)
                bit = P_FLAG;
            else
                continue;
            flags_site_add(c, c + 1, FLAGS_SITE_READ, bit);
        }
    }

    memset(flags_site_of, 0, sizeof(int16_t) * ir->wr_pos);
    for (int s = 0; s < flags_site_nr; s++) {
        flags_site[s].valid   = 0;
        flags_site[s].removed = 0;
        for (int c = flags_site[s].start; c < flags_site[s].end; c++) {
            flags_site_of[c] = s + 1;
            if ((ir->uops[c].type & UOP_MASK) != UOP_INVALID)
                flags_site[s].valid = 1;
        }
        /*A read stays a read when the dead list has taken its uop: the call
          it stands for is a barrier, which the dead list keeps*/
        if (flags_site[s].kind == FLAGS_SITE_READ)
            flags_site[s].valid = 1;
    }

    /*Sites a jump may skip, and calls that would close a join*/
    for (int c = 0; c < ir->wr_pos; c++) {
        const uop_t *uop = &ir->uops[c];

        if (flags_jump_dest[c])
            join = 1;
        while ((si < flags_site_nr) && (flags_site[si].start == c)) {
            flags_site[si].kept = (maxdest > c) || (join && (uop->type & UOP_TYPE_BARRIER));
            si++;
        }
        if (((uop->type & UOP_MASK) != UOP_INVALID) && (uop->type & UOP_TYPE_BARRIER))
            join = 0;
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
        /*The FERR# test's branch skips only the call, which no flags site
          sits in, and its destination counts on no later call (the
          allocator is as it was at the branch there): passed over, as the
          bare call was*/
        if (codegen_uop_uncounted[c])
            continue;
#endif
        if ((uop->type & UOP_TYPE_JUMP) && (uop->jump_dest_uop >= 0)) {
            if (uop->jump_dest_uop > maxdest)
                maxdest = uop->jump_dest_uop;
            if (uop->jump_dest_uop < ir->wr_pos)
                flags_jump_dest[uop->jump_dest_uop] = 1;
        }
    }

    for (int s = flags_site_nr - 1; s >= 0; s--) {
        if (!flags_site[s].valid || flags_site[s].kept || (flags_site[s].kind == FLAGS_SITE_READ))
            continue;
        if (flags_site_is_dead(ir, s))
            flags_site[s].removed = flags_site_remove(ir, s);
    }
}

#if defined(CODEGEN_BACKEND_HAS_COLD_REGION) && !defined(RELEASE_BUILD)
/*Per uop, while a block is compiled: COLD_IN for the uops of a cold region,
  COLD_DEST for a region's destination*/
enum {
    COLD_IN   = 1,
    COLD_DEST = 2
};
static uint8_t cold_mark[UOP_NR_MAX + 1];

/*A cold region (UOP_TYPE_COLD) as the allocator needs it: its branches stay
  inside it, no other branch reaches its destination, it holds no other
  region and ends in an unconditional jump out of the block; no uop past it
  reads a register version written inside it, which the path that skips it
  never made*/
static void
codegen_cold_check(ir_data_t *ir, const uop_t *uop, int c, int region_start, int region_dest)
{
    const ir_reg_t *src[3] = { &uop->src_reg_a, &uop->src_reg_b, &uop->src_reg_c };

    if ((uop->type & UOP_TYPE_COLD) && (region_dest != -1))
        fatal("cold region at %i: uop %i opens another\n", region_start, c);
    if ((uop->type & UOP_TYPE_COLD) && ((uop->jump_dest_uop <= (c + 1)) || (uop->jump_dest_uop > ir->wr_pos)))
        fatal("cold region at %i: destination %i\n", c, uop->jump_dest_uop);
    if ((uop->type & UOP_TYPE_COLD) && (uop->jump_dest_uop < ir->wr_pos) && (ir->uops[uop->jump_dest_uop].type & UOP_TYPE_JUMP_DEST))
        fatal("cold region at %i: destination %i has another branch\n", c, uop->jump_dest_uop);
    if ((uop->type & UOP_TYPE_JUMP) && !(uop->type & UOP_TYPE_COLD) && (uop->jump_dest_uop >= 0)) {
        if ((uop->jump_dest_uop <= ir->wr_pos) && (cold_mark[uop->jump_dest_uop] & COLD_DEST))
            fatal("cold region: uop %i branches to destination %i\n", c, uop->jump_dest_uop);
        if ((region_dest != -1) && ((uop->jump_dest_uop <= c) || (uop->jump_dest_uop >= region_dest)))
            fatal("cold region at %i: uop %i branches out of it to %i\n", region_start, c, uop->jump_dest_uop);
    }
    if (region_dest != -1)
        return;
    for (int i = 0; i < 3; i++) {
        if (!ir_reg_is_invalid(*src[i]) && src[i]->version && (cold_mark[reg_version[IREG_GET_REG(src[i]->reg)][src[i]->version].parent_uop] & COLD_IN))
            fatal("cold region: uop %i reads %02x.%i, written inside one\n", c, src[i]->reg, src[i]->version);
    }
}
#endif

#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
/*Whether the cold region the branch at c opens goes on at its destination:
  one that leaves the block ends in an unconditional jump*/
static int
codegen_cold_rejoins(const ir_data_t *ir, int c)
{
    for (int d = ir->uops[c].jump_dest_uop - 1; d > c; d--) {
        if ((ir->uops[d].type & UOP_MASK) != UOP_INVALID)
            return (ir->uops[d].type & UOP_MASK) != (UOP_JMP & UOP_MASK);
    }
    return 0;
}

/*Before the branch over a region that goes on: the registers it writes
  leave the allocator (codegen_reg_cold_drop)*/
static void
codegen_cold_drop_written(ir_data_t *ir, codeblock_t *block, int c)
{
    for (int d = c + 1; d < ir->uops[c].jump_dest_uop; d++) {
        if (((ir->uops[d].type & UOP_MASK) != UOP_INVALID) && !ir_reg_is_invalid(ir->uops[d].dest_reg_a))
            codegen_reg_cold_drop(block, IREG_GET_REG(ir->uops[d].dest_reg_a.reg));
    }
}
#endif

void
codegen_ir_compile(ir_data_t *ir, codeblock_t *block)
{
    int jump_target_at_end = -1;
    int c;
    int unroll_end         = 0;
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
    /*The destination of the cold region being compiled, and whether the
      region goes on there*/
    int cold_dest   = -1;
    int cold_rejoin = 0;
    /*A branch over a cold region compiled as an order barrier instead*/
    int cold_plain = 0;
#    ifndef RELEASE_BUILD
    int cold_start     = -1;
    int cold_last_type = 0;
#    endif
#endif

    flags_unroll_nr = 0;
    if (codegen_unroll_count) {
        const codegen_flags_read_t *rd_sites;
        int                         nr_rd = codegen_flags_read_sites(&rd_sites);
        int                         taken_cycles;

        codegen_set_loop_start(ir, codegen_unroll_first_instruction);
        unroll_end = ir->wr_pos;
        taken_cycles = codegen_accumulate_unroll(ir, codegen_get_loop_timing(codegen_unroll_first_instruction), codegen_unroll_start, codegen_unroll_branch);

        for (int unroll_count = 1; unroll_count < codegen_unroll_count; unroll_count++) {
            int offset   = ir->wr_pos - codegen_unroll_start;
            int rd       = 0;
            int pair_reg = -1;

            if (flags_unroll_nr < FLAGS_UNROLL_MAX)
                flags_unroll_off[flags_unroll_nr++] = offset;
            //                        pclog("Unroll from %i to %i, offset %i - iteration %i\n", codegen_unroll_start, ir->wr_pos, offset, unroll_count);
            for (c = codegen_unroll_start; c < unroll_end; c++) {
                //                                pclog(" Duplicate uop %i\n", c);
                while ((rd < nr_rd) && (rd_sites[rd].uop < c))
                    rd++;
                if (pair_reg >= 0) {
                    uop_t test = ir->uops[c];

                    test.src_reg_a.reg = pair_reg;
                    duplicate_uop(ir, &test, offset);
                    pair_reg = -1;
                } else if ((rd < nr_rd) && (rd_sites[rd].uop == c))
                    pair_reg = duplicate_flags_read(ir, &ir->uops[c], &rd_sites[rd]);
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
                else if (codegen_uop_uncounted[c]) {
                    const uop_t *uop = &ir->uops[c];

                    if (!ir_reg_is_invalid(uop->src_reg_a))
                        codegen_reg_read_uncounted(uop->src_reg_a.reg);
                    codegen_ir_set_uncounted(ir->wr_pos);
                    duplicate_uop(ir, &ir->uops[c], offset);
                }
#endif
                else
                    duplicate_uop(ir, &ir->uops[c], offset);
                codegen_accumulate_copy(ir, c);
            }
            if (taken_cycles)
                uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, taken_cycles);
        }
    }

    codegen_reg_mark_as_required();
    codegen_flags_dead_note_ops(ir);
    codegen_reg_process_dead_list(ir);
    codegen_flags_dead_pass(ir, codegen_unroll_count ? codegen_unroll_start : 0, unroll_end);
    block_write_data = codeblock_allocator_get_ptr(block->head_mem_block);
    block_pos        = 0;
    codegen_backend_prologue(block);
#if defined(CODEGEN_BACKEND_HAS_COLD_REGION) && !defined(RELEASE_BUILD)
    memset(cold_mark, 0, ir->wr_pos + 1);
#endif

    for (c = 0; c < ir->wr_pos; c++) {
        uop_t *uop = &ir->uops[c];
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
        int at_cold = 0;
#endif

        //                pclog("uOP %i : %08x\n", c, uop->type);

#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
        /*Only the branch over the region gets here, which ran nothing
          else: the allocator is as it was there. A region that goes on
          loads that state first, on its own path. The branch lands after
          those loads and before anything this uop writes back, which the
          path that skips the region has to write too*/
        if (c == cold_dest) {
            if (cold_rejoin)
                codegen_reg_cold_rejoin(block);
            else {
#    ifndef RELEASE_BUILD
                if ((cold_last_type & UOP_MASK) != (UOP_JMP & UOP_MASK))
                    fatal("cold region at %i: ends in %08x\n", cold_start, cold_last_type);
#    endif
                codegen_reg_cold_restore();
            }
            for (uop_t *uop_dest = uop; uop_dest->jump_list_next != -1;) {
                uop_dest = &ir->uops[uop_dest->jump_list_next];
                codegen_set_jump_dest(block, uop_dest->p);
            }
            cold_dest = -1;
            at_cold   = 1;
        }
#endif
        if (uop->type & UOP_TYPE_BARRIER)
            codegen_reg_flush_invalidate(ir, block);
#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
        /*The jumps left memory current: what the path that falls through
          holds back since then is stored on that path, before the join*/
#    ifdef CODEGEN_BACKEND_HAS_COLD_REGION
        else if ((uop->type & UOP_TYPE_JUMP_DEST) && !at_cold)
#    else
        else if (uop->type & UOP_TYPE_JUMP_DEST)
#    endif
            codegen_reg_flush(ir, block);
#endif

#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
        if ((uop->type & UOP_TYPE_JUMP_DEST) && !at_cold) {
#else
        if (uop->type & UOP_TYPE_JUMP_DEST) {
#endif
            uop_t *uop_dest = uop;

            while (uop_dest->jump_list_next != -1) {
                uop_dest = &ir->uops[uop_dest->jump_list_next];
                codegen_set_jump_dest(block, uop_dest->p);
            }
        }

        if ((uop->type & UOP_MASK) == UOP_INVALID)
            continue;
#if defined(CODEGEN_BACKEND_HAS_COLD_REGION) && !defined(RELEASE_BUILD)
        codegen_cold_check(ir, uop, c, cold_start, cold_dest);
        if (cold_dest != -1) {
            cold_mark[c]   = COLD_IN;
            cold_last_type = uop->type;
        }
#endif

#ifdef CODEGEN_BACKEND_HAS_MOV_IMM
        if ((uop->type & UOP_MASK) == (UOP_MOV_IMM & UOP_MASK) && reg_is_native_size(uop->dest_reg_a) && !codegen_reg_is_loaded(uop->dest_reg_a) && reg_version[IREG_GET_REG(uop->dest_reg_a.reg)][uop->dest_reg_a.version].refcount <= 0) {
            /*Special case for UOP_MOV_IMM - if destination not already in host register
              and won't be used again then just store directly to memory*/
#    ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
            codegen_reg_defer_imm(block, uop->dest_reg_a, uop->imm_data);
#    else
            codegen_reg_write_imm(block, uop->dest_reg_a, uop->imm_data);
#    endif
        } else
#endif
            if ((uop->type & UOP_MASK) == (UOP_MOV & UOP_MASK) && reg_version[IREG_GET_REG(uop->src_reg_a.reg)][uop->src_reg_a.version].refcount <= 1 && reg_is_native_size(uop->src_reg_a) && reg_is_native_size(uop->dest_reg_a) && (ireg_is_byte_accessed(IREG_GET_REG(uop->src_reg_a.reg)) || !ireg_is_byte_accessed(IREG_GET_REG(uop->dest_reg_a.reg)))) {
            /*Special case for UOP_MOV - if source register won't be used again then
              just rename it to dest register instead of moving. Not when that could
              leave a register accessed by byte in a host register without byte forms*/
            codegen_reg_alloc_register(invalid_ir_reg, uop->src_reg_a, invalid_ir_reg, invalid_ir_reg);
            uop->src_reg_a_real = codegen_reg_alloc_read_reg(block, uop->src_reg_a, NULL);
            codegen_reg_rename(block, uop->src_reg_a, uop->dest_reg_a);
            if (uop->type & UOP_TYPE_ORDER_BARRIER)
                codegen_reg_flush(ir, block);
        } else {
            if (uop->type & UOP_TYPE_PARAMS_REGS) {
                codegen_reg_alloc_register(uop->dest_reg_a, uop->src_reg_a, uop->src_reg_b, uop->src_reg_c);
                if (uop->src_reg_a.reg != IREG_INVALID) {
                    uop->src_reg_a_real = codegen_reg_alloc_read_reg(block, uop->src_reg_a, NULL);
                }
                if (uop->src_reg_b.reg != IREG_INVALID) {
                    uop->src_reg_b_real = codegen_reg_alloc_read_reg(block, uop->src_reg_b, NULL);
                }
                if (uop->src_reg_c.reg != IREG_INVALID) {
                    uop->src_reg_c_real = codegen_reg_alloc_read_reg(block, uop->src_reg_c, NULL);
                }
            }

            if (uop->type & UOP_TYPE_ORDER_BARRIER) {
#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
                if (codegen_uop_is_lazy(uop))
                    codegen_reg_flush_lazy(ir, block);
                else
#endif
                    codegen_reg_flush(ir, block);
            }
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
            if (uop->type & UOP_TYPE_COLD) {
                /*An order barrier instead: for a region at the block's end,
                  whose branch would land past the end's write-back, and for
                  one that goes on when a register is held twice*/
                cold_plain = (uop->jump_dest_uop >= ir->wr_pos);
                if (!cold_plain && codegen_cold_rejoins(ir, c)) {
                    codegen_cold_drop_written(ir, block, c);
                    cold_plain = !codegen_reg_cold_can_rejoin();
                }
                if (cold_plain)
                    codegen_reg_flush(ir, block);
            }
#endif

            if (uop->type & UOP_TYPE_PARAMS_REGS) {
                if (uop->dest_reg_a.reg != IREG_INVALID) {
                    uop->dest_reg_a_real = codegen_reg_alloc_write_reg(block, uop->dest_reg_a);
                }
            }
#ifndef RELEASE_BUILD
            if (!uop_handlers[uop->type & UOP_MASK])
                fatal("!uop_handlers[uop->type & UOP_MASK] %08x\n", uop->type);
#endif
            uop_handlers[uop->type & UOP_MASK](block, uop);
#ifdef CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
            codegen_lazy_site = NULL;
#endif
            if (uop->type & UOP_TYPE_ORDER_BARRIER)
                codegen_reg_drop_cycles();
#ifdef CODEGEN_BACKEND_HAS_COLD_REGION
            /*Nothing is written back: the path that skips the region goes
              on with the registers as they are, and the region starts from
              them too, storing what it needs before its own calls*/
            if ((uop->type & UOP_TYPE_COLD) && cold_plain)
                codegen_reg_drop_cycles();
            else if (uop->type & UOP_TYPE_COLD) {
                codegen_reg_cold_snapshot();
                cold_dest   = uop->jump_dest_uop;
                cold_rejoin = codegen_cold_rejoins(ir, c);
#    ifndef RELEASE_BUILD
                cold_start     = c;
                cold_last_type = 0;
                cold_mark[cold_dest] = COLD_DEST;
#    endif
            }
#endif
        }

        if (uop->type & UOP_TYPE_JUMP) {
            if (uop->jump_dest_uop == ir->wr_pos) {
                if (jump_target_at_end == -1)
                    jump_target_at_end = c;
                else {
                    uop_t *uop_dest = &ir->uops[jump_target_at_end];

                    while (uop_dest->jump_list_next != -1)
                        uop_dest = &ir->uops[uop_dest->jump_list_next];

                    uop_dest->jump_list_next = c;
                }
            } else {
                uop_t *uop_dest = &ir->uops[uop->jump_dest_uop];

                while (uop_dest->jump_list_next != -1)
                    uop_dest = &ir->uops[uop_dest->jump_list_next];

                uop_dest->jump_list_next = c;
                ir->uops[uop->jump_dest_uop].type |= UOP_TYPE_JUMP_DEST;
            }
        }
    }

    codegen_reg_flush_invalidate(ir, block);

    if (jump_target_at_end != -1) {
        uop_t *uop_dest = &ir->uops[jump_target_at_end];

        while (1) {
            codegen_set_jump_dest(block, uop_dest->p);
            if (uop_dest->jump_list_next == -1)
                break;
            uop_dest = &ir->uops[uop_dest->jump_list_next];
        }
    }

    codegen_backend_epilogue(block);
    block_write_data = NULL;
#if 0
    if (has_ea)
        fatal("IR compilation complete\n");
#endif
}

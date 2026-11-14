#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_backend.h"
#include "codegen_ir.h"

static struct {
    int count;
    int dest_reg;
} acc_regs[] = {
    [ACCREG_cycles] = {0, IREG_cycles}
};

/*The copies start a new timing block. Keep the timing inputs and the places
  their charges are flushed or stamped, rather than the first pass's debt.*/
static struct cycle_event_t {
    int type, pos;
    uint32_t a, b, c, d;
} *cycle_events;
static int cycle_event_count, cycle_event_size;
static struct {
    uintptr_t imm_data;
    int       imm_data2;
} copy_cycles[UOP_NR_MAX];

#ifdef CODEGEN_BACKEND_HAS_ALU_FAMILY
static void retime_acc_event(int type);
#endif

int
codegen_accumulate_record(int type, int pos, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    int index = cycle_event_count++;

#ifdef CODEGEN_BACKEND_HAS_ALU_FAMILY
    retime_acc_event(type);
#endif

    if (cycle_event_count > cycle_event_size) {
        cycle_event_size = cycle_event_size ? cycle_event_size * 2 : 256;
        cycle_events = realloc(cycle_events, cycle_event_size * sizeof(*cycle_events));
        if (!cycle_events)
            fatal("Failed to allocate cycle events\n");
    }
    cycle_events[index] = (struct cycle_event_t) {type, pos, a, b, c, d};
    return index;
}

static codegen_timing_state_t block_start_state;
static codegen_timing_state_t block_end_state;

void
codegen_accumulate_begin(void)
{
    cycle_event_count = 0;
    if (codegen_timing_save)
        codegen_timing_save(&block_start_state);
}

void
codegen_accumulate_timing(UNUSED(ir_data_t *ir))
{
    codegen_accumulate_record(ACC_TIMING, 0, 0, 0, 0, 0);
    acc_regs[ACCREG_cycles].count -= codegen_block_cycles;
    codegen_block_cycles = 0;
}

void
codegen_accumulate_jump(UNUSED(ir_data_t *ir), int scale, int fallback)
{
    int jump_cycles = codegen_timing_jump_cycles ? codegen_timing_jump_cycles() : fallback;

    codegen_accumulate_record(ACC_JUMP, scale, fallback, 0, 0, 0);
    acc_regs[ACCREG_cycles].count += scale * jump_cycles;
}

static void
codegen_unroll_exit(ir_data_t *ir, int branch, int pending, int copy)
{
    for (int c = branch; c < ir->wr_pos; c++) {
        uop_t *uop = &ir->uops[c];

        if (((uop->type & UOP_MASK) == (UOP_JMP & UOP_MASK)) && (uop->p == codegen_exit_rout)) {
            if (copy)
                copy_cycles[c].imm_data2 = pending;
            else
                uop->imm_data2 = pending;
        }
    }
}

int
codegen_accumulate_unroll(ir_data_t *ir, int first, int start, int branch)
{
    int pending = 0;
    int deferred = 0;
    int credit = 0;
    int taken = 0;
    int saved_cycles = codegen_block_cycles;

    codegen_unroll_exit(ir, branch, acc_regs[ACCREG_cycles].count, 0);
    for (int c = 0; c < ir->wr_pos; c++) {
        copy_cycles[c].imm_data = ir->uops[c].imm_data;
        copy_cycles[c].imm_data2 = ir->uops[c].imm_data2;
    }
    /*Not unrolled, every later pass runs the loop head's block, timed from
      what the block compiled before it left, never from this block's tail.
      With no instruction before the head that block is this one, so time
      the copies from this block's start; otherwise it is compiled apart,
      and the copies take the model's cleared state. The next block gets
      the state this block left*/
    if (codegen_timing_save) {
        const codegen_timing_state_t *state = &block_start_state;

        for (int c = 0; c < first; c++) {
            if (cycle_events[c].type == ACC_START)
                state = NULL;
        }
        codegen_timing_save(&block_end_state);
        codegen_timing_load(state);
    }
    codegen_block_cycles = 0;
    codegen_timing_block_start();
    for (int c = first; c < cycle_event_count; c++) {
        struct cycle_event_t *event = &cycle_events[c];

        switch (event->type) {
            case ACC_START:
                codegen_timing_start();
                break;
            case ACC_PREFIX:
                codegen_timing_prefix(event->a, event->b);
                break;
            case ACC_OPCODE:
                codegen_timing_opcode(event->a, event->b, event->c, event->d);
                break;
            case ACC_TIMING:
                pending -= codegen_block_cycles;
                codegen_block_cycles = 0;
                break;
            case ACC_DELTA:
                pending += (int) event->a;
                break;
            case ACC_JUMP: {
                int jump_cycles = codegen_timing_jump_cycles ? codegen_timing_jump_cycles() : (int) event->a;

                pending += event->pos * jump_cycles;
                if (event->pos > 0)
                    credit = jump_cycles;
                break;
            }
            case ACC_FLUSH:
                /*An empty first-pass flush has no uop to rewrite. Its debt
                  stays with the next flush, or with an exit before it.*/
                if (event->pos >= start) {
                    copy_cycles[event->pos].imm_data = pending + deferred;
                    deferred = 0;
                } else
                    deferred += pending;
                pending = 0;
                break;
            case ACC_EXIT:
                copy_cycles[event->pos].imm_data2 = pending + deferred -
                    ((event->a && codegen_timing_jump_cycles) ? codegen_timing_jump_cycles() : 0);
                break;
            case ACC_ROUTE:
                copy_cycles[event->pos].imm_data2 = deferred;
                break;
            case ACC_END:
                taken = pending + deferred - credit;
                codegen_timing_block_end();
                break;
        }
    }
    codegen_unroll_exit(ir, branch, pending + deferred, 1);
    if (codegen_timing_load)
        codegen_timing_load(&block_end_state);
    codegen_block_cycles = saved_cycles;
    codegen_accumulate_reset();
    return taken;
}

void
codegen_accumulate_copy(ir_data_t *ir, int pos)
{
    ir->uops[ir->wr_pos - 1].imm_data = copy_cycles[pos].imm_data;
    ir->uops[ir->wr_pos - 1].imm_data2 = copy_cycles[pos].imm_data2;
}

void
codegen_accumulate(UNUSED(ir_data_t *ir), int acc_reg, int delta)
{
    codegen_accumulate_record(ACC_DELTA, 0, delta, 0, 0, 0);
    acc_regs[acc_reg].count += delta;
}

int
codegen_accumulate_pending(int acc_reg)
{
#ifdef CODEGEN_BACKEND_HAS_ALU_FAMILY
    retime_acc_event(ACC_EXIT);
#endif
    return acc_regs[acc_reg].count;
}

void
codegen_accumulate_flush(ir_data_t *ir)
{
    codegen_accumulate_record(ACC_FLUSH, acc_regs[0].count ? ir->wr_pos : -1, 0, 0, 0, 0);
    if (acc_regs[0].count) {
        uop_ADD_IMM(ir, acc_regs[0].dest_reg, acc_regs[0].dest_reg, acc_regs[0].count);
    }

    acc_regs[0].count = 0;
}

void
codegen_accumulate_reset(void)
{
    acc_regs[0].count = 0;
}

#ifdef CODEGEN_BACKEND_HAS_ALU_FAMILY
/*A CODEBLOCK_FAMILY_RETIME block: the timing model's calls are
  recorded as the block is compiled, from the start of each stretch it times
  as one (codegen_timing_block_start()), and the first stretch whose family
  slots can be retimed keeps them (codegen_family_retime_lump()). As the
  block runs a new mix of the slots' kinds is timed by playing the stretch
  through the model again with the kinds it has now (codegen_family_retime()),
  and the difference to the compiled timing added to the cycles.

  Each instruction's cycles go to the accumulator as it is compiled, to be
  charged at the next flush or stamped into an exit, so after the first slot
  none may be until the stretch's end, which must flush them straight away
  (the block's end, or a followed RET's guard). One instruction that is not
  a slot may come before that end, with exits and flushes of its own: the
  slots' difference up to it is charged as it starts, the rest at the end.
  Each time the model is asked for the cycle it holds back, as an exit or a
  branch is compiled, it must answer as it did, or the block is compiled
  again.*/
enum {
    RETIME_START,
    RETIME_PREFIX,
    RETIME_OPCODE,
    RETIME_QUERY,
    RETIME_END
};
#    define RETIME_EVENTS 256
#    define RETIME_DESCS  64

typedef struct retime_event_t {
    uint8_t  type;
    uint8_t  op;
    /*1 + the slot's retime number (its key shift / 2), 0 for any other*/
    uint8_t  slot;
    int8_t   result;
    uint32_t fetchdat;
    int      op_32;
} retime_event_t;

typedef struct retime_desc_t {
    uint32_t       serial;
    /*The model the stretch was timed with*/
    codegen_timing_t *model;
    int            nr;
    /*The event of the instruction charged on its own, -1 for none*/
    int            point;
    int            f0;
    int            p0;
    /*The state a model that keeps it from block to block (save()) had as the
      stretch started*/
    codegen_timing_state_t start_state;
    retime_event_t ev[RETIME_EVENTS];
} retime_desc_t;

static retime_desc_t retime_descs[RETIME_DESCS];
static uint32_t      retime_serial;

/*What the stretch of a compile came to with a mix of kinds, by its serial
  and the key: a block rewritten between a few mixes finds them here*/
#    define RETIME_MEMO_BITS 12
static struct {
    uint64_t key;
    uint32_t serial;
    int32_t  delta;
    int32_t  delta_end;
} retime_memo[1 << RETIME_MEMO_BITS];

static inline uint32_t
retime_memo_index(uint64_t key, uint32_t serial)
{
    return (uint32_t) (((key ^ ((uint64_t) serial << 40)) * 0x9e3779b97f4a7c15ull) >> (64 - RETIME_MEMO_BITS));
}

static struct {
    int            on;
    int            done;
    int            bad;
    int            nr;
    int            seg_slots;
    int            last_opcode;
    int            point;
    int            in_point;
    int            point_uop;
    codegen_timing_state_t start_state;
    retime_event_t ev[RETIME_EVENTS];
    /*The family slot of each slot event*/
    uint8_t        family_slot[RETIME_EVENTS];
} rec;

/*The cycles the model gave each instruction go to the accumulator as it is
  compiled (codegen_accumulate_timing()), to be charged at the next flush or
  stamped into an exit: after a slot of the stretch none may be, nor moved
  apart from the model, until the stretch's end*/
static void
retime_acc_event(int type)
{
    if (rec.on && !rec.done && rec.seg_slots && !rec.in_point && (type != ACC_TIMING) && (type != ACC_START) && (type != ACC_PREFIX) && (type != ACC_OPCODE) && (type != ACC_END))
        rec.bad = 1;
}

static const uint8_t retime_kind_op[3] = { 0x05, 0x3d, 0x15 };

/*The model a block's compile is timed with while it is recorded*/
static codegen_timing_t *retime_model = &codegen_timing_p6;

uint8_t codegen_alu_family_key_shift[CODEGEN_ALU_FAMILY_MAX];

static void
retime_note(int type, uint8_t op, uint32_t fetchdat, int op_32)
{
    if (rec.done)
        return;
    if (rec.nr >= RETIME_EVENTS) {
        rec.bad = 1;
        return;
    }
    rec.ev[rec.nr] = (retime_event_t) { type, op, 0, 0, fetchdat, op_32 };
    if (type == RETIME_OPCODE)
        rec.last_opcode = rec.nr;
    rec.nr++;
}

static void
retime_rec_start(void)
{
    retime_note(RETIME_START, 0, 0, 0);
    retime_model->start();
}

static void
retime_rec_prefix(uint8_t prefix, uint32_t fetchdat)
{
    retime_note(RETIME_PREFIX, prefix, fetchdat, 0);
    retime_model->prefix(prefix, fetchdat);
}

static void
retime_rec_opcode(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc)
{
    retime_note(RETIME_OPCODE, opcode, fetchdat, op_32);
    retime_model->opcode(opcode, fetchdat, op_32, op_pc);
}

static void
retime_rec_block_start(void)
{
    if (!rec.done) {
        rec.nr          = 0;
        rec.seg_slots   = 0;
        rec.bad         = 0;
        rec.last_opcode = -1;
        rec.point       = -1;
        rec.in_point    = 0;
        if (retime_model->save)
            retime_model->save(&rec.start_state);
    }
    retime_model->block_start();
}

static void
retime_rec_block_end(void)
{
    retime_note(RETIME_END, 0, 0, 0);
    retime_model->block_end();
}

static int
retime_rec_jump_cycles(void)
{
    const int result = retime_model->jump_cycles();

    retime_note(RETIME_QUERY, 0, 0, 0);
    if (!rec.done && !rec.bad)
        rec.ev[rec.nr - 1].result = result;
    return result;
}

void
codegen_family_retime_begin(codeblock_t *block)
{
    codegen_family_retime_end();
    rec.on   = 0;
    rec.done = 0;
    rec.nr   = 0;
    rec.bad  = 1;
    if (!(block->flags & CODEBLOCK_FAMILY_RETIME))
        return;
    retime_model               = codegen_timing_current;
    rec.on                     = 1;
    codegen_timing_start       = retime_rec_start;
    codegen_timing_prefix      = retime_rec_prefix;
    codegen_timing_opcode      = retime_rec_opcode;
    codegen_timing_block_start = retime_rec_block_start;
    codegen_timing_block_end   = retime_rec_block_end;
    /*Only for a model that answers it: the others leave the question to
      the caller's fallback (codegen_accumulate_jump())*/
    codegen_timing_jump_cycles = retime_model->jump_cycles ? retime_rec_jump_cycles : NULL;
}

void
codegen_family_retime_end(void)
{
    if (codegen_timing_opcode == retime_rec_opcode)
        codegen_timing_set(codegen_timing_current);
    rec.on = 0;
}

int
codegen_family_retime_on(void)
{
    return rec.on;
}

/*The family slot just compiled, as the last opcode the model was given*/
void
codegen_family_retime_slot(int slot)
{
    if (!rec.on || rec.done || rec.bad || (rec.last_opcode < 0) || rec.ev[rec.last_opcode].slot)
        return;
    rec.ev[rec.last_opcode].slot          = 0xff;
    rec.family_slot[rec.last_opcode]      = slot;
    rec.seg_slots++;
}

/*The cycles the model gives the stretch with the slots' kinds as in key,
  or -1 where it answers a question for the cycle it holds back otherwise.
  It is played from the stretch's start; a model that keeps state from block
  to block (save()) plays from what it had then, and gets its own back
  after*/
static int
retime_play(const retime_desc_t *d, uint64_t key, int *at_point)
{
    const retime_event_t  *ev    = d->ev;
    const int              nr    = d->nr;
    const int              saved = codegen_block_cycles;
    codegen_timing_t      *model = d->model;
    codegen_timing_state_t kept;
    int                    timed;

    if (model->save)
        model->save(&kept);
    if (model->load)
        model->load(&d->start_state);
    codegen_block_cycles = 0;
    model->block_start();
    for (int c = 0; c < nr; c++) {
        const retime_event_t *e = &ev[c];

        switch (e->type) {
            case RETIME_START:
                model->start();
                break;
            case RETIME_PREFIX:
                model->prefix(e->op, e->fetchdat);
                break;
            case RETIME_OPCODE:
                model->opcode(e->slot ? retime_kind_op[(key >> (2 * (e->slot - 1))) & 3] : e->op, e->fetchdat, e->op_32, 0);
                if ((d->point >= 0) && (c >= d->point) && (*at_point < 0))
                    *at_point = codegen_block_cycles;
                break;
            case RETIME_QUERY:
                if (model->jump_cycles() != e->result) {
                    codegen_block_cycles = saved;
                    if (model->load)
                        model->load(&kept);
                    return -1;
                }
                break;
            case RETIME_END:
                model->block_end();
                break;
        }
    }
    if (model->load)
        model->load(&kept);
    timed                = codegen_block_cycles;
    codegen_block_cycles = saved;
    if (*at_point < 0)
        *at_point = 0;
    return timed;
}

static int32_t retime_zero;

/*At the start of each instruction: the first one after a slot that is not
  one may have exits, which charge the cycles so far. The cycles the slots'
  kinds add up to it are charged before it, so that those come out right;
  the next one that is not a slot has to be at the stretch's end*/
void
codegen_family_retime_ins(ir_data_t *ir, uint8_t opcode)
{
    if (!rec.on || rec.done || !rec.seg_slots || rec.bad)
        return;
    rec.in_point = 0;
    if (codegen_alu_family_kind[opcode] != 0x80)
        return;
    if (rec.point >= 0) {
        rec.bad = 1;
        return;
    }
    rec.point     = rec.nr;
    rec.in_point  = 1;
    rec.point_uop = ir->wr_pos;
    uop_MOV_REG_PTR(ir, IREG_temp0, &retime_zero);
    uop_ADD(ir, IREG_cycles, IREG_cycles, IREG_temp0);
}

/*At the end of a stretch the model times as one, before its cycles are
  charged: the first whose family slots can all be retimed keeps them, and
  its charge takes what the block's start found the kinds to add*/
void
codegen_family_retime_lump(ir_data_t *ir, codeblock_t *block, int site_ok)
{
    retime_desc_t *d;
    uint64_t       key = 0;
    int            j   = 0;

    if (!rec.on || rec.done || !rec.seg_slots)
        return;
    if (rec.bad || !site_ok || (rec.seg_slots > CODEGEN_FAMILY_RETIME_SLOTS)) {
        rec.seg_slots = 0;
        return;
    }
    for (int c = 0; c < rec.nr; c++) {
        if (rec.ev[c].slot) {
            const int slot = rec.family_slot[c];
            const int kind = codegen_alu_family_carry[slot] - 2;

            rec.ev[c].slot = 1 + j;
            key |= (uint64_t) kind << (2 * j);
            j++;
        }
    }
    if (!++retime_serial) {
        retime_serial      = 1;
        memset(retime_memo, 0, sizeof(retime_memo));
    }
    d         = &retime_descs[retime_serial % RETIME_DESCS];
    d->serial = 0;
    d->model  = retime_model;
    d->start_state = rec.start_state;
    d->nr     = rec.nr;
    d->point  = rec.point;
    d->p0     = -1;
    memcpy(d->ev, rec.ev, rec.nr * sizeof(retime_event_t));
    d->f0 = retime_play(d, key, &d->p0);
    if (d->f0 < 0) {
        rec.seg_slots = 0;
        return;
    }
    d->serial = retime_serial;
    for (int c = 0; c < rec.nr; c++) {
        if (rec.ev[c].slot) {
            codegen_alu_family_carry[rec.family_slot[c]]     = 5;
            codegen_alu_family_key_shift[rec.family_slot[c]] = 2 * (rec.ev[c].slot - 1);
        }
    }
    block->family_serial    = retime_serial;
    block->family_key       = key;
    block->family_delta     = 0;
    block->family_delta_end = 0;
    if (rec.point >= 0)
        ir->uops[rec.point_uop].p = &block->family_delta;
    uop_MOV_REG_PTR(ir, IREG_temp0, &block->family_delta_end);
    uop_ADD(ir, IREG_cycles, IREG_cycles, IREG_temp0);
    rec.done = 1;
}

/*Called by a CODEBLOCK_FAMILY_RETIME block's start when its slots' kinds
  are not those it was last timed for: 0 once the cycles they add are in
  family_delta, 1 when the block has to be compiled again*/
int
codegen_family_retime(codeblock_t *block, uint64_t key, uint32_t serial)
{
    const retime_desc_t *d        = &retime_descs[serial % RETIME_DESCS];
    const uint32_t       m        = retime_memo_index(key, serial);
    int                  at_point = -1;
    int                  timed;

    if ((retime_memo[m].serial == serial) && (retime_memo[m].key == key) && (d->serial == serial)) {
        block->family_serial    = serial;
        block->family_key       = key;
        block->family_delta     = retime_memo[m].delta;
        block->family_delta_end = retime_memo[m].delta_end;
        return 0;
    }

    if (d->serial != serial)
        return 1;
    timed = retime_play(d, key, &at_point);
    if (timed < 0)
        return 1;
    block->family_serial = serial;
    block->family_key    = key;
    block->family_delta     = d->p0 - at_point;
    block->family_delta_end = (d->f0 - d->p0) - (timed - at_point);
    retime_memo[m].key       = key;
    retime_memo[m].serial    = serial;
    retime_memo[m].delta     = block->family_delta;
    retime_memo[m].delta_end = block->family_delta_end;
    return 0;
}
#endif

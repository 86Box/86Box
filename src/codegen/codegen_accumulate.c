#include <stdint.h>
#include <stdlib.h>
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

int
codegen_accumulate_record(int type, int pos, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    int index = cycle_event_count++;

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

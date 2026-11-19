/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host registers, DMA and execution timing, ported from rust-dos. */

#include <stdlib.h>
#include <string.h>
#include "internal.h"

enum { AHEAD     = 2000,
       POLLS     = 64,
       MOST_OWED = 200000,
       RISC_MHZ  = 25 };

static uint32_t
core_dword(const verite_core *core, unsigned reg)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; i++)
        value |= (uint32_t) core_byte(core, reg + i) << (8 * i);
    return value;
}

static uint32_t
swap_word(uint32_t word, unsigned mode)
{
    switch (mode) {
        case 0:
            return word;
        case 1:
            return verite_bswap(word);
        case 2:
            return ((word & 0xff00ff00) >> 8) | ((word & 0x00ff00ff) << 8);
        default:
            return (word << 16) | (word >> 16);
    }
}

static void
core_reset(verite_core *c, bool hard)
{
    c->pc   = RESET_PC;
    c->npc  = risc_next(RESET_PC);
    c->flag = 8;
    c->seg = c->clip = c->riscintr = 0;
    c->halted = c->stalled = false;
    pixel_reset(&c->pe);
    c->in_head = c->in_count = c->out_head = c->out_count = 0;
    c->partial_mask                                       = 0;
    c->has_forced                                         = false;
    c->dma                                                = (verite_dma) { 0 };
    verite_loader_reset(c);
    if (hard) {
        memset(c->regs, 0, sizeof(c->regs));
        memset(c->reg, 0, sizeof(c->reg));
        c->regs[VIO_DEBUG] = VDEBUG_HOLD;
        c->regs[VIO_MODE]  = VMODE_VGA;
        c->regs[0x5c]      = 4;
        c->intr            = 0;
        c->slow_precharge  = false;
        c->dram_row        = UINT32_MAX;
    }
}

verite_core *
verite_core_new(void)
{
    verite_core *c = calloc(1, sizeof(*c));
    if (c) {
        verite_init_rom(c);
        core_reset(c, true);
    }
    return c;
}

void
verite_core_free(verite_core *c)
{
    if (c) {
        verite_loader_reset(c);
        free(c->output);
        free(c);
    }
}

void
verite_core_reset(verite_core *c)
{
    core_reset(c, true);
}

uint8_t
verite_core_mode(const verite_core *c)
{
    return core_byte(c, VIO_MODE);
}

uint32_t
verite_core_bank(const verite_core *c)
{
    return core_dword(c, VIO_BANK_SELECT);
}

uint32_t
verite_core_status(verite_core *c)
{
    uint32_t result = !!(c->intr & (uint16_t) core_dword(c, VIO_INTR_EN)) | ((uint32_t) c->drew << 1) | ((uint32_t) (core_byte(c, VIO_MEM_ENDIAN) & 3) << 8);
    c->drew         = false;
    return result;
}

void
verite_core_retrace(verite_core *c)
{
    c->intr |= VINTR_VERT;
    c->riscintr |= 1;
}

void
verite_core_set_precharge(verite_core *c, int slow)
{
    if (c->slow_precharge != !!slow)
        c->dram_row = UINT32_MAX;
    c->slow_precharge = !!slow;
}

void
verite_core_refresh(verite_core *c, unsigned cycles)
{
    if (cycles) {
        c->dram_row = UINT32_MAX;
        c->credit  -= cycles;
        c->timer   += cycles;
    }
}

static bool
runnable(const verite_core *c)
{
    if (c->loader.active)
        return c->in_count != 0;
    return !(core_byte(c, VIO_DEBUG) & (VDEBUG_SOFT_RESET | VDEBUG_HOLD)) && !c->halted && !c->stalled;
}

static void
push_input(verite_core *c, uint32_t word)
{
    if (c->in_count == IN_FIFO) {
        c->intr |= VINTR_FIFO_ERROR;
        return;
    }
    c->input[(c->in_head + c->in_count++) % IN_FIFO] = word;
    c->stalled                                       = false;
}

bool
verite_pop_input(verite_core *c, uint32_t *word)
{
    if (!c->in_count)
        return false;
    *word      = c->input[c->in_head];
    c->in_head = (c->in_head + 1) % IN_FIFO;
    --c->in_count;
    if (IN_FIFO - c->in_count == (unsigned) (core_byte(c, VIO_LOW_WATER_MARK) % 32 + 1))
        c->intr |= VINTR_FIFO_LOW;
    return true;
}

void
verite_push_output(verite_core *c, uint32_t word)
{
    /* The RISC can bypass the eight-word output backpressure. */
    if (c->out_count == c->out_capacity) {
        size_t    capacity = c->out_capacity ? c->out_capacity * 2 : OUT_FIFO;
        uint32_t *queue    = capacity > c->out_capacity && capacity <= SIZE_MAX / sizeof(*queue)
            ? malloc(capacity * sizeof(*queue))
            : NULL;
        if (!queue) {
            c->intr |= VINTR_FIFO_ERROR;
            return;
        }
        for (size_t i = 0; i < c->out_count; i++)
            queue[i] = c->output[(c->out_head + i) % c->out_capacity];
        free(c->output);
        c->output       = queue;
        c->out_capacity = capacity;
        c->out_head     = 0;
    }
    c->output[(c->out_head + c->out_count++) % c->out_capacity] = word;
}

static uint8_t
status_byte(const verite_core *c)
{
    bool held = !!(core_byte(c, VIO_DEBUG) & VDEBUG_HOLD);
    return c->halted | (held << 1) | 0x0c | ((c->stalled && !c->in_count) << 4) | ((c->stalled && c->in_count) << 5) | ((c->halted || held || c->stalled) << 7);
}

static uint32_t
state(const verite_core *c, uint8_t index, verite_memory mem)
{
    if (index == 0x81)
        return c->pc;
    if (index == 0x80 || index == 0x82) {
        uint32_t ir = c->has_forced ? c->forced : risc_fetch(c, c->pc, mem);
        return index == 0x80 ? ir : risc_peek(c, ir);
    }
    return pixel_state(&c->pe, index);
}

uint32_t
verite_core_read(verite_core *c, uint8_t reg, uint8_t len, const uint8_t *vram, size_t size)
{
    if (reg < 0x10) {
        if (!c->out_count) {
            c->intr |= VINTR_FIFO_ERROR;
            return 0;
        }
        uint32_t word = c->output[c->out_head];
        c->out_head   = (c->out_head + 1) % c->out_capacity;
        --c->out_count;
        c->stalled = false;
        return swap_word(word, (reg >> 2) & 3) >> (8 * (reg & 3));
    }
    uint32_t value = 0, state_data = 0;
    if (reg >= VIO_STATE_DATA && reg < VIO_STATE_DATA + 4)
        state_data = state(c, core_byte(c, VIO_STATE_INDEX), (verite_memory) { (uint8_t *) vram, size - 1 });
    for (unsigned i = 0; i < len; i++) {
        uint8_t r = reg + i, byte;
        switch (r) {
            case VIO_FIFO_IN_FREE:
                byte = c->in_count ? IN_FIFO - c->in_count : 31;
                break;
            case VIO_FIFO_OUT_VALID:
                byte = c->out_count < 7 ? c->out_count : 7;
                break;
            case VIO_COMM:
                byte = (core_byte(c, r) & 15) | (c->flag & 0xf0);
                break;
            case VIO_MEM_ENDIAN:
                byte = (core_byte(c, r) & 3) | (c->dma.busy ? 0x80 : 0);
                break;
            case VIO_INTR:
            case VIO_INTR + 1:
                byte = c->intr >> (8 * (r - VIO_INTR));
                break;
            case VIO_STATUS:
                byte = status_byte(c);
                break;
            default:
                if (r >= VIO_DMA_CMD_PTR && r < VIO_DMA_LENGTH + 4) {
                    const uint32_t regs[] = { (c->dma.ptr & ~3U) | c->dma.busy, c->dma.addr, c->dma.left | c->dma.swap };
                    byte                  = regs[(r - VIO_DMA_CMD_PTR) / 4] >> (8 * (r & 3));
                } else if (r >= VIO_STATE_DATA && r < VIO_STATE_DATA + 4)
                    byte = state_data >> (8 * (r & 3));
                else
                    byte = r < 0x80 ? core_byte(c, r) : 0;
                break;
        }
        value |= (uint32_t) byte << (8 * i);
    }
    return value;
}

static void
dma_command(verite_core *c, verite_bus bus)
{
    verite_dma *dma = &c->dma;
    for (unsigned i = 0; i < 0x10000; i++) {
        uint32_t addr = bus.ops->dma_read(bus.opaque, dma->ptr);
        uint32_t len  = bus.ops->dma_read(bus.opaque, dma->ptr + 4);
        if (!addr)
            break;
        if (len & 0x80000000) {
            dma->ptr = addr & ~3U;
            continue;
        }
        dma->addr = addr & ~3U;
        dma->left = len & 0x00fffffc;
        dma->swap = len & 3;
        if (dma->left)
            return;
        dma->ptr += 8;
    }
    dma->busy = false;
    c->intr |= VINTR_DMA;
}

static void
dma_pump(verite_core *c, verite_bus bus)
{
    verite_dma *dma = &c->dma;
    while (dma->busy && c->in_count < IN_FIFO) {
        push_input(c, swap_word(bus.ops->dma_read(bus.opaque, dma->addr), dma->swap));
        dma->addr += 4;
        dma->left -= 4;
        if (!dma->left) {
            dma->ptr += 8;
            dma_command(c, bus);
        }
    }
}

static int64_t
run(verite_core *c, verite_memory mem, verite_bus bus, int64_t budget)
{
    int64_t used = 0;
    c->polls     = 0;
    while (used < budget && c->polls < POLLS) {
        if (c->dma.busy)
            dma_pump(c, bus);
        if (c->loader.active) {
            verite_loader_pump(c, mem);
            if (c->loader.active)
                break;
        }
        if (!runnable(c) || !risc_step(c, false, 0, mem, bus))
            break;
        used += 1 + c->dram_wait;
    }
    return used;
}

static void
debug_written(verite_core *c, verite_memory mem, verite_bus bus)
{
    uint8_t debug = core_byte(c, VIO_DEBUG);
    if (debug & VDEBUG_SOFT_RESET) {
        core_reset(c, false);
        return;
    }
    if (debug & VDEBUG_HOLD)
        c->intr |= VINTR_IDLE;
    else
        c->has_forced = false;
    if (debug & VDEBUG_STEP) {
        if (debug & VDEBUG_HOLD) {
            bool forced   = c->has_forced;
            c->has_forced = false;
            c->halted     = false;
            if (!risc_step(c, forced, c->forced, mem, bus)) {
                c->has_forced = forced;
                return;
            }
        }
        c->regs[VIO_DEBUG] &= ~VDEBUG_STEP;
    }
}

static bool
covers(uint8_t reg, uint8_t len, unsigned target)
{
    return reg <= target && target < (uint8_t) (reg + len);
}

void
verite_core_write(verite_core *c, uint8_t reg, uint32_t value, uint8_t len,
                  uint8_t *vram, size_t size, const verite_board *ops, void *opaque)
{
    verite_memory mem = { vram, size - 1 };
    verite_bus    bus = { ops, opaque };
    if (reg < 0x10) {
        unsigned shift = 8 * (reg & 3);
        uint32_t mask  = len >= 4 ? UINT32_MAX : ((1U << (8 * len)) - 1) << shift;
        c->partial     = (c->partial & ~mask) | ((value << shift) & mask);
        c->partial_mask |= mask;
        if (c->partial_mask == UINT32_MAX) {
            c->partial_mask = 0;
            if (c->in_count == IN_FIFO)
                c->credit -= run(c, mem, bus, 20000);
            push_input(c, swap_word(c->partial, (reg >> 2) & 3));
        }
        return;
    }
    for (unsigned i = 0; i < len; i++) {
        uint8_t r = reg + i, byte = value >> (8 * i);
        if (r == VIO_INTR || r == VIO_INTR + 1) {
            unsigned bits = (unsigned) byte << (8 * (r - VIO_INTR));
            c->intr &= ~bits;
            if (bits & VINTR_RISC)
                c->flag &= ~1U;
        } else if (r < 0x80 && r != VIO_FIFO_IN_FREE && r != VIO_FIFO_OUT_VALID && r != VIO_STATUS && r != VIO_MEM_BASE && !(r >= VIO_DMA_ADDRESS && r < VIO_DMA_LENGTH + 4))
            c->regs[r] = byte;
    }
    if (covers(reg, len, VIO_STATE_DATA + 3)) {
        c->forced     = core_dword(c, VIO_STATE_DATA);
        c->has_forced = true;
    }
    if (covers(reg, len, VIO_DEBUG))
        debug_written(c, mem, bus);
    if (covers(reg, len, VIO_DMA_CMD_PTR + 3)) {
        uint32_t ptr = core_dword(c, VIO_DMA_CMD_PTR) & ~3U;
        c->dma       = (verite_dma) { .busy = ptr && (verite_core_mode(c) & VMODE_DMA), .ptr = ptr };
        if (c->dma.busy)
            dma_command(c, bus);
    }
    if (covers(reg, len, VIO_MODE) && !(verite_core_mode(c) & VMODE_DMA))
        c->dma.busy = false;
}

void
verite_core_run(verite_core *c, uint64_t now_ns, uint8_t *vram, size_t size,
                const verite_board *ops, void *opaque)
{
    uint64_t elapsed = now_ns > c->last_ns ? now_ns - c->last_ns : 0;
    c->last_ns += elapsed;
    c->credit += (int64_t) (elapsed / (1000 / RISC_MHZ));
    if (c->credit > MOST_OWED)
        c->credit = MOST_OWED;
    if (c->credit + AHEAD > 0)
        c->credit -= run(c, (verite_memory) { vram, size - 1 }, (verite_bus) { ops, opaque }, c->credit + AHEAD);
    if ((!runnable(c) || c->polls >= POLLS) && c->credit > 0) {
        c->timer += (uint32_t) c->credit;
        c->credit = 0;
    }
}

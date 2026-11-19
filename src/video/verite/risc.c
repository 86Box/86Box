/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Vérité RISC instruction interpreter, ported from rust-dos. */

#include <math.h>
#include <string.h>
#include "internal.h"

enum {
    SWFIFO = 32,
    FIFO,
    SEED,
    SEG,
    SCALE,
    FLAG,
    CLIP,
    TIMER,
    NPSWFIFO,
    NPFIFO,
    DRAWCTL  = 40,
    WINCLIP  = 46,
    RISCINTR = 47,
    RA       = 254
};
#define LOCAL_END   UINT32_C(0x01000000)
#define CONTROL     UINT32_C(0xffe00000)
#define BOARD_REGS  UINT32_C(0xffe00400)
#define PE_STATE    UINT32_C(0xffe00800)
#define CONTROL_END UINT32_C(0xffe01000)

typedef struct {
    uint32_t even, odd;
} reg_pair;

static const uint32_t constants[32] = {
    0, 0, 0x8000, 0x80000000, 0xff000000, 0x00ff0000, 0x0000ff00, 0xff,
    0xffff0000, 0xffff, 0xff00ff00, 0x00ff00ff, 0, UINT32_MAX, 0, 0x10000,
    UINT32_MAX, 0x10000, 0x007fffff, 0x02000000, 0x00800000, 0x20000,
    0xb000, 0xb800, 0xc000, 0xc800, 0xd000, 0xd800, 0xe000, 0xe800, 0xf000, 0xf800
};

static uint32_t
relative(uint32_t addr, int32_t words)
{
    return (addr & 0xff000000) | ((addr + (uint32_t) words * 4) & 0x00ffffff);
}

static uint32_t
absolute(uint32_t field)
{
    return ((field & 0xffffff) << 2) | ((field & 0x800000) ? 0xfc000000 : 0);
}

static uint32_t
f2i(uint32_t bits, uint32_t shift)
{
    float f;
    memcpy(&f, &bits, sizeof(f));
    double v = (double) f * (UINT64_C(1) << (shift & 31));
    if ((bits & 0x7fffffff) > 0x7f800000)
        return bits & 0x80000000 ? 0x80000000 : 0x7fffffff;
    if (v <= INT32_MIN)
        return 0x80000000;
    if (v >= INT32_MAX)
        return 0x7fffffff;
    return (uint32_t) (int32_t) floor(v);
}

static uint32_t
alu(uint8_t op, uint32_t a, uint32_t b)
{
    static const uint8_t shifts[] = { 0, 8, 16, 24, 32, 31 };
    op &= 15;
    switch (op) {
        case 0:
            return a + b;
        case 1:
            return a - b;
        case 2:
            return a & ~b;
        case 3:
            return b - a;
        case 4:
            return a & b;
        case 5:
            return a | b;
        case 6:
            return ~(a | b);
        case 7:
            return a ^ b;
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
            return (uint32_t) (((int64_t) (int32_t) a * (int32_t) b) >> shifts[op - 8]);
        default:
            return f2i(a, b);
    }
}

static uint32_t
special_value(const verite_core *c, unsigned reg)
{
    if (reg < 32)
        return constants[reg];
    switch (reg) {
        case SEED:
            return c->seed;
        case SEG:
            return (c->seed_index << 19) | (c->seg & 0xffff) | 0x8888;
        case SCALE:
            return c->scale;
        case FLAG:
            return c->flag | ((uint32_t) (c->out_count < OUT_FIFO) << 2) | ((uint32_t) (core_byte(c, VIO_COMM) & 15) << 8) | ((uint32_t) c->pe.pick_valid << 16) | ((uint32_t) c->pe.pick_hit << 17) | ((uint32_t) (c->in_count != 0) << 31);
        case CLIP:
            return c->clip & 0xfc;
        case TIMER:
            return c->timer;
        case WINCLIP:
            return c->clip & 0x3c;
        case RISCINTR:
            return c->riscintr | ((uint32_t) !!(c->riscintr & (c->riscintr >> 16) & 0x3f) << 31);
        case 56:
            return 0xfffff000;
        case 57:
            return 0xfffff800;
        default:
            return 0;
    }
}

static uint32_t
input_front(const verite_core *c)
{
    return c->in_count ? c->input[c->in_head] : 0;
}

uint32_t
risc_peek(const verite_core *c, uint8_t field)
{
    unsigned r = risc_resolve(c, field);
    if (r >= 64)
        return c->reg[r];
    switch (r) {
        case SWFIFO:
        case NPSWFIFO:
            return verite_bswap(input_front(c));
        case FIFO:
        case NPFIFO:
            return input_front(c);
        default:
            return special_value(c, r);
    }
}

uint32_t
risc_special_src(verite_core *c, uint8_t field)
{
    if (field == SWFIFO || field == FIFO) {
        uint32_t value = 0;
        verite_pop_input(c, &value);
        return field == SWFIFO ? verite_bswap(value) : value;
    }
    return risc_peek(c, field);
}

static reg_pair
src_pair(verite_core *c, uint8_t field)
{
    unsigned r = field & ~1;
    if (r == SWFIFO || r == NPSWFIFO) {
        uint32_t word = input_front(c);
        if (r == SWFIFO)
            verite_pop_input(c, &word);
        return (reg_pair) { verite_bswap(word), word };
    }
    uint32_t even = risc_src(c, field & ~1);
    return (reg_pair) { even, risc_src(c, field | 1) };
}

void
risc_special_dst(verite_core *c, uint8_t field, uint32_t value)
{
    switch (field) {
        case FIFO:
            verite_push_output(c, value);
            break;
        case SEG:
            c->seg = value & 0xffff;
            break;
        case FLAG:
            if (value & 1)
                c->intr |= VINTR_RISC;
            c->flag = (c->flag & 1) | (value & 0x400000f9);
            break;
        case TIMER:
            c->timer = value;
            break;
        case DRAWCTL:
            c->pe.drawctl = value & 0xffff;
            break;
        case RISCINTR:
            c->riscintr = (c->riscintr & ~(value & 0x3f) & 0xffff) | (value & 0x3f0000);
            break;
    }
}

static void
dst_pair(verite_core *c, uint8_t field, reg_pair p)
{
    risc_dst(c, field & ~1, p.even);
    risc_dst(c, field | 1, p.odd);
}

static void
seed_op(verite_core *c, uint32_t x)
{
    if (!x) {
        c->scale = c->seed_index = 0;
        c->seed                  = UINT32_MAX;
        return;
    }
    unsigned leading = 0;
    while (!(x & 0x80000000)) {
        x <<= 1;
        leading++;
    }
    c->scale      = 32 - leading;
    c->seed_index = x >> 19;
    c->seed       = (UINT64_C(1) << 62) / x;
}

static uint32_t
rom_word(const verite_core *c, uint32_t addr)
{
    const uint8_t *p = &c->rom[addr & ((ROM_SIZE - 1) & ~3U)];
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

uint32_t
risc_fetch(const verite_core *c, uint32_t addr, verite_memory mem)
{
    return addr < LOCAL_END ? mem_read32(mem, addr) : rom_word(c, addr);
}

static uint32_t
control_read(verite_core *c, uint32_t addr, uint8_t len, verite_bus bus)
{
    if (addr >= PE_STATE)
        return pixel_state(&c->pe, (addr - PE_STATE) >> 3);
    if (addr >= BOARD_REGS) {
        unsigned io = 0x80 + ((addr - BOARD_REGS) >> 3);
        c->polls++;
        return bus.ops->read(bus.opaque, io, io >= 0xb0 && io < 0xc0 ? 1 : len);
    }
    return 0;
}

static inline void
dram_access(verite_core *c, uint32_t addr, verite_memory mem)
{
    if (c->slow_precharge) {
        /* Approximate the extra precharge wait on a local DRAM row change. */
        uint32_t row = (addr & mem.mask) >> 12;
        if (row != c->dram_row) {
            c->dram_row = row;
            c->dram_wait++;
        }
    }
}

static uint32_t
load32(verite_core *c, uint32_t addr, verite_memory mem, verite_bus bus)
{
    if (addr < LOCAL_END) {
        dram_access(c, addr, mem);
        return mem_read32(mem, addr);
    }
    if (addr >= CONTROL && addr < CONTROL_END)
        return control_read(c, addr & ~3U, 4, bus);
    return addr >= 0xff000000 ? rom_word(c, addr) : 0;
}

static uint32_t
load16(verite_core *c, uint32_t addr, verite_memory mem, verite_bus bus)
{
    if (addr < LOCAL_END) {
        dram_access(c, addr, mem);
        return mem_read16(mem, addr);
    }
    return (load32(c, addr & ~3U, mem, bus) >> (16 - (addr & 2) * 8)) & 0xffff;
}

static uint32_t
load8(verite_core *c, uint32_t addr, verite_memory mem, verite_bus bus)
{
    if (addr < LOCAL_END) {
        dram_access(c, addr, mem);
        return mem_read8(mem, addr);
    }
    if (addr >= BOARD_REGS && addr < PE_STATE)
        return control_read(c, addr & ~7U, 1, bus) & 0xff;
    return (load32(c, addr & ~3U, mem, bus) >> (24 - (addr & 3) * 8)) & 0xff;
}

static void
store(verite_core *c, uint32_t addr, uint32_t value, unsigned size, verite_memory mem, verite_bus bus)
{
    if (addr < LOCAL_END) {
        dram_access(c, addr, mem);
        switch (size) {
            case 1:
                mem_write8(mem, addr, value);
                break;
            case 2:
                mem_write16(mem, addr, value);
                break;
            default:
                mem_write32(mem, addr, value);
                break;
        }
        c->drew = true;
    } else if ((addr & ~7U) == LOADER_PORT) {
        verite_loader_begin(c);
    } else if (addr >= BOARD_REGS && addr < PE_STATE) {
        unsigned io = 0x80 + ((addr - BOARD_REGS) >> 3);
        /* The RISC's writes to CRTC timing and control do not take effect. */
        if (io < 0x90)
            return;
        if (io == 0xb1 && size == 4) {
            for (int shift = 16; shift >= 0; shift -= 8)
                bus.ops->write(bus.opaque, io, (value >> shift) & 0xff, 1);
        } else if (io >= 0xb0 && io < 0xc0)
            bus.ops->write(bus.opaque, io, value & 0xff, 1);
        else
            bus.ops->write(bus.opaque, io, value, 4);
    }
}

static bool
is_fifo(uint8_t field)
{
    return field == SWFIFO || field == FIFO;
}

static bool
fifo_wait(const verite_core *c, uint8_t op, uint8_t d, uint8_t s2, uint8_t s1)
{
    /* Segmented operands always resolve to general-purpose registers. */
    if (!is_fifo(s1) && !is_fifo(s2) && d != FIFO)
        return false;
    bool output_blocked = c->out_count >= OUT_FIFO && !(c->flag & 0x40000000);
    /* No instruction checked here needs more than two input words or one output slot. */
    if (c->in_count >= 2 && !output_blocked)
        return false;

    unsigned pops      = 0;
    bool     out       = false;
    if (op <= 0x0f || (op >= 0x40 && op <= 0x4d)) {
        pops = is_fifo(s2);
        out  = op != 0x0e;
    } else if (op == 0x4f) {
        pops = is_fifo(s2);
    } else if ((op >= 0x10 && op <= 0x1f) || (op >= 0x50 && op <= 0x5e)) {
        pops = is_fifo(s1) + (op != 0x52 && is_fifo(s2));
        out  = op != 0x1e;
    } else if (op == 0x5f || (op >= 0x78 && op <= 0x7f) || (op >= 0xd0 && op <= 0xd7)) {
        pops = is_fifo(s2) + is_fifo(s1);
    } else if ((op >= 0x20 && op <= 0x2f) || (op >= 0xd8 && op <= 0xdf)) {
        pops = is_fifo(s2 & ~1) + is_fifo(s1 & ~1);
    } else if (op >= 0x30 && op <= 0x3f) {
        pops = is_fifo(s2 & ~1) + is_fifo(s1);
    } else if ((op >= 0x60 && op <= 0x65) || (op >= 0x6f && op <= 0x75)) {
        pops = is_fifo(s1);
        out  = op >= 0x6f;
    } else if (op == 0x6b || op == 0x76 || op == 0x77) {
        out = true;
    } else if (op >= 0xc0 && op <= 0xc3) {
        pops = is_fifo(s1);
    }
    return pops > c->in_count || (output_blocked && out && d == FIFO);
}

static void
alu_b(verite_core *c, uint8_t op, uint8_t d, uint8_t s2, uint8_t s1)
{
    uint32_t a = risc_src(c, s2);
    uint32_t b = op < 0x50 ? s1 : op == 0x52 ? 0
                                             : risc_src(c, s1);
    uint32_t v;
    switch (op) {
        case 0x40:
            v = a + (b << 16);
            break;
        case 0x41:
            v = a - (b << 16);
            break;
        case 0x42:
        case 0x53:
            seed_op(c, a);
            if (c->scale >= b) {
                unsigned shift = c->scale - b;
                v              = shift < 32 ? c->seed >> shift : 0;
            } else {
                unsigned shift = b - c->scale;
                v              = shift < 32 ? c->seed << shift : 0;
            }
            break;
        case 0x43:
            v = (b << 16) - a;
            break;
        case 0x44:
        case 0x54:
            v = (a >> (b & 31)) | (a << ((0U - b) & 31));
            break;
        case 0x45:
        case 0x55:
            v = a << (b & 31);
            break;
        case 0x46:
        case 0x56:
            v = (uint32_t) ((int32_t) a >> (b & 31));
            break;
        case 0x47:
        case 0x57:
            v = a >> (b & 31);
            break;
        case 0x48:
        case 0x58:
            v = (int32_t) a < (int32_t) b;
            break;
        case 0x49:
        case 0x59:
            v = a < b;
            break;
        case 0x4a:
        case 0x5a:
            v = a == b;
            break;
        case 0x4b:
            v = a + (b << 8);
            break;
        case 0x4c:
        case 0x5c:
            v = (int32_t) a < (int32_t) b ? a : b;
            break;
        case 0x4d:
        case 0x5d:
            v = (int32_t) a > (int32_t) b ? a : b;
            break;
        case 0x52:
            v = risc_src(c, s1);
            if ((int32_t) v < 0)
                v = 0U - v;
            break;
        case 0x5e:
            v = (a + b) & 0xffff0000;
            break;
        default:
            return;
    }
    risc_dst(c, d, v);
}

static void
sort_segments(verite_core *c, uint8_t s3, uint8_t s2, uint8_t s1)
{
    struct entry {
        int32_t value;
        uint8_t field;
    } entries[3];
    uint8_t fields[] = { s1, s2, s3 };
    for (unsigned i = 0; i < 3; i++) {
        struct entry e = { (int32_t) risc_src(c, fields[i]), fields[i] };
        unsigned     j = i;
        while (j > 0 && entries[j - 1].value > e.value) {
            entries[j] = entries[j - 1];
            j--;
        }
        entries[j] = e;
    }
    unsigned bases = 0;
    for (unsigned i = 0; i < 3; i++)
        bases |= ((risc_resolve(c, entries[i].field) >> 4) & 7) << (4 * i);
    c->seg  = (c->seg & ~0x777U) | bases;
    c->clip = ((c->clip << 4) & 0xfc) | ((int64_t) entries[2].value - 1 < entries[0].value ? 0x3c : 0);
}

static void
step_param(verite_core *c, pixel_param param, uint32_t value, uint32_t delta)
{
    bool negative = (int32_t) delta < 0;
    switch (param) {
        case P_XSHADOW:
            c->pe.x_dir = negative;
            break;
        case P_U:
            c->pe.u_dir = negative;
            break;
        case P_V:
            c->pe.v_dir = negative;
            break;
        case P_Q:
            seed_op(c, value);
            break;
        default:
            break;
    }
    pixel_set(&c->pe, param, value);
}

static void
step_op(verite_core *c, uint8_t op, uint8_t d, uint8_t s2, uint8_t s1)
{
    static const pixel_param scalar[8][2] = {
        { P_XSHADOW, P_Y   },
        { P_Q,       P_Q   },
        { P_R,       P_V   },
        { P_I,       P_U   },
        { P_F,       P_F   },
        { P_G,       P_B   },
        { P_Z,       P_A   },
        { P_CNT,     P_CNT }
    };
    static const pixel_param pair[8][2] = {
        { P_XSHADOW, P_Y    },
        { P_Q,       P_U    },
        { P_R,       P_V    },
        { P_I,       P_V    },
        { P_Z,       P_V    },
        { P_G,       P_B    },
        { P_Z,       P_A    },
        { P_CNT,     P_NONE }
    };
    if (op < 0xd8) {
        uint32_t a = risc_src(c, s2);
        uint32_t b = risc_src(c, s1);
        step_param(c, scalar[op & 7][s2 & 1], a, b);
        risc_dst(c, d, a + b);
    } else {
        reg_pair a = src_pair(c, s2);
        reg_pair b = src_pair(c, s1);
        step_param(c, pair[op & 7][0], a.even, b.even);
        step_param(c, pair[op & 7][1], a.odd, b.odd);
        dst_pair(c, d, (reg_pair) { a.even + b.even, a.odd + b.odd });
    }
}

static void
clip(verite_core *c, uint32_t a, uint32_t b)
{
    c->clip = ((c->clip << 1) & 0xfc) | ((int32_t) a > (int32_t) b ? 4 : 0);
}

static void
execute_memory(verite_core *c, uint8_t op, uint8_t d, uint8_t s2, uint8_t s1,
               verite_memory mem, verite_bus bus)
{
    uint32_t addr = risc_src(c, s1);
    if (op <= 0x75) {
        switch (op) {
            case 0x70:
                risc_dst(c, d, load8(c, addr + s2, mem, bus));
                break;
            case 0x71:
                risc_dst(c, d, load16(c, addr + 2U * s2, mem, bus));
                break;
            case 0x72:
                risc_dst(c, d, load32(c, addr + 4U * s2, mem, bus));
                break;
            case 0x73:
                break;
            default:
                {
                    addr          = (addr + 8U * s2) & ~7U;
                    uint32_t even = load32(c, addr, mem, bus);
                    dst_pair(c, d, (reg_pair) { even, load32(c, addr + 4, mem, bus) });
                    break;
                }
        }
        return;
    }
    if (op <= 0x7a) {
        unsigned size = 1U << (op - 0x78);
        store(c, addr + size * d, risc_src(c, s2), size, mem, bus);
    } else if (op == 0x7c) {
        addr       = (addr + 8U * d) & ~7U;
        reg_pair p = src_pair(c, s2);
        store(c, addr, p.even, 4, mem, bus);
        store(c, addr + 4, p.odd, 4, mem, bus);
    } else {
        uint32_t value = risc_src(c, s2);
        addr += 8U * d + (op == 0x7d ? 0 : op == 0x7e ? 1
                                                      : 3);
        unsigned stride = op == 0x7d ? 2 : 4;
        for (unsigned k = 0; k < 4; k++)
            store(c, addr + k * stride, (value >> (24 - 8 * k)) & 0xff, 1, mem, bus);
    }
}

static void
execute(verite_core *c, uint32_t ir, uint8_t op, uint8_t d, uint8_t s2, uint8_t s1,
        uint32_t cur, verite_memory mem, verite_bus bus)
{
    uint32_t link = relative(cur, 2);
    if (op <= 0x1f) {
        if (op == 0x0e || op == 0x1e) {
            uint32_t b = op == 0x0e ? s1 : risc_src(c, s1);
            clip(c, risc_src(c, s2), b);
        } else {
            uint32_t a = risc_src(c, s2);
            uint32_t b = op < 0x10 ? s1 : risc_src(c, s1);
            risc_dst(c, d, alu(op, a, b));
        }
    } else if (op <= 0x3f) {
        if ((op & 15) == 15)
            return;
        reg_pair a = src_pair(c, s2), b;
        if (op < 0x30)
            b = src_pair(c, s1);
        else {
            b.even = risc_src(c, s1);
            b.odd  = b.even;
        }
        if ((op & 15) == 14) {
            clip(c, a.even, b.even);
            clip(c, a.odd, b.odd);
        } else
            dst_pair(c, d, (reg_pair) { alu(op, a.even, b.even), alu(op, a.odd, b.odd) });
    } else if (op <= 0x4d || (op >= 0x52 && op <= 0x5e)) {
        alu_b(c, op, d, s2, s1);
    } else if (op >= 0x60 && op <= 0x65) {
        int32_t v      = (int32_t) risc_src(c, s1);
        bool    take[] = { v == 0, v != 0, v >= 0, v < 0, v > 0, v <= 0 };
        if (take[op - 0x60])
            c->npc = relative(c->pc, (int16_t) (ir >> 8));
    } else if ((op >= 0x70 && op <= 0x75) || (op >= 0x78 && op <= 0x7f && op != 0x7b)) {
        execute_memory(c, op, d, s2, s1, mem, bus);
    } else if (op >= 0xd0 && op <= 0xdf) {
        step_op(c, op, d, s2, s1);
    } else {
        uint32_t value;
        switch (op) {
            case 0x4e:
                pixel_spr(&c->pe, s1, (ir >> 8) & 0xffff, mem);
                break;
            case 0x4f:
                pixel_spr(&c->pe, s1, risc_src(c, s2), mem);
                break;
            case 0x5f:
                value = risc_src(c, s2);
                pixel_spr(&c->pe, risc_src(c, s1), value, mem);
                break;
            case 0x68:
            case 0x6a:
                if (op == 0x6a)
                    risc_dst(c, RA, link);
                c->npc = relative(c->pc, (int32_t) (ir << 8) >> 8);
                break;
            case 0x6b:
                risc_dst(c, d, link);
                break;
            case 0x6c:
            case 0x6e:
                if (op == 0x6e)
                    risc_dst(c, RA, link);
                c->npc = absolute(ir);
                break;
            case 0x6d:
                c->halted = true;
                c->pc     = cur;
                c->npc    = risc_next(cur);
                c->intr |= VINTR_HALT;
                break;
            case 0x6f:
                value = risc_src(c, s1);
                risc_dst(c, d, link);
                c->npc = value;
                break;
            case 0x76:
                risc_dst(c, d, ir & 0xffff);
                break;
            case 0x77:
                risc_dst(c, d, ir << 16);
                break;
            case 0xc0:
            case 0xc1:
            case 0xc2:
            case 0xc3:
                {
                    value            = risc_src(c, s1);
                    reg_pair pairs[] = {
                        { value & 0xffff0000,      value << 16              },
                        { value << 16,             value & 0xffff0000       },
                        { value & 0xff0000,        (value >> 8) & 0xff0000  },
                        { (value << 8) & 0xff0000, (value << 16) & 0xff0000 }
                    };
                    dst_pair(c, d, pairs[op & 3]);
                    break;
                }
            case 0xc4:
                sort_segments(c, d, s2, s1);
                break;
            case 0xe0:
            case 0xe1:
            case 0xe4:
            case 0xe5:
                pixel_draw_parallel(c, op, d, s2, s1, mem);
                break;
            case 0xf0:
            case 0xf1:
            case 0xf2:
            case 0xf3:
            case 0xf8:
            case 0xf9:
            case 0xfa:
            case 0xfb:
            case 0xfc:
            case 0xfd:
                pixel_draw_param(c, op, d, s2, s1, mem);
                break;
            default:
                break;
        }
    }
}

bool
risc_step(verite_core *c, bool forced, uint32_t ir, verite_memory mem, verite_bus bus)
{
    uint32_t cur = c->pc;
    c->dram_wait = 0;
    if (!forced) {
        if (cur < LOCAL_END)
            dram_access(c, cur, mem);
        ir = risc_fetch(c, cur, mem);
    }
    uint8_t op = ir >> 24;
    uint8_t d  = ir >> 16;
    uint8_t s2 = ir >> 8;
    uint8_t s1 = ir;
    c->stalled = fifo_wait(c, op, d, s2, s1);
    if (c->stalled)
        return false;
    /* Branches change NPC, leaving the instruction in the delay slot at PC. */
    c->pc  = c->npc;
    c->npc = risc_next(c->npc);
    c->timer++;
    execute(c, ir, op, d, s2, s1, cur, mem, bus);
    c->timer += c->dram_wait;
    return true;
}

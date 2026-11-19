/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The original rust-dos Vérité regression cases, ported with the core. */

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include the interpreter and pixel engine for their private arithmetic tests. */
#include "../../src/video/verite/risc.c"
#include "../../src/video/verite/pixel.c"

#define HALT               0x6d000000U
#define INS(op, d, s2, s1) (((uint32_t) (op) << 24) | ((uint32_t) (d) << 16) | ((uint32_t) (s2) << 8) | (uint8_t) (s1))
#define IMM(op, d, n)      (INS(op, d, 0, 0) | (uint16_t) (n))
#define COUNT(a)           (sizeof(a) / sizeof((a)[0]))

typedef struct {
    verite_core  *core;
    verite_memory mem;
    uint32_t      ram[256];
    struct {
        uint8_t  port, len;
        uint32_t value;
    } writes[32];
    unsigned write_count;
} fixture;

static uint32_t
board_read(void *opaque, uint8_t port, uint8_t len)
{
    (void) opaque;
    (void) port;
    (void) len;
    return 0;
}

static void
board_write(void *opaque, uint8_t port, uint32_t value, uint8_t len)
{
    fixture *f = opaque;
    unsigned i = f->write_count++;
    assert(i < COUNT(f->writes));
    f->writes[i].port  = port;
    f->writes[i].len   = len;
    f->writes[i].value = value;
}

static uint32_t
board_dma(void *opaque, uint32_t addr)
{
    fixture *f = opaque;
    return addr / 4 < COUNT(f->ram) ? f->ram[addr / 4] : 0;
}

static const verite_board board = { board_read, board_write, board_dma };

static fixture
machine(const uint32_t *program, size_t count, uint32_t addr)
{
    fixture f = {
        .core = verite_core_new(), .mem = { calloc(1, 1 << 20), (1 << 20) - 1 }
    };
    assert(f.core && f.mem.data);
    for (size_t i = 0; i < count; i++)
        mem_write32(f.mem, addr + 4 * (uint32_t) i, program[i]);
    f.core->regs[VIO_DEBUG] = 0;
    f.core->pc              = addr;
    f.core->npc             = risc_next(addr);
    return f;
}

static void
done(fixture *f)
{
    verite_core_free(f->core);
    free(f->mem.data);
}

static void
run_core(fixture *f)
{
    verite_core_run(f->core, f->core->last_ns + 400000, f->mem.data, f->mem.mask + 1, &board, f);
}

static void
write_io(fixture *f, uint8_t reg, unsigned len, uint32_t value)
{
    verite_core_write(f->core, reg, value, len, f->mem.data, f->mem.mask + 1, &board, f);
}

static uint32_t
read_io(fixture *f, uint8_t reg, unsigned len)
{
    return verite_core_read(f->core, reg, len, f->mem.data, f->mem.mask + 1);
}

static void
force(fixture *f, uint32_t ir)
{
    write_io(f, VIO_STATE_INDEX, 1, 0x80);
    write_io(f, VIO_STATE_DATA, 4, ir);
    write_io(f, VIO_DEBUG, 1, VDEBUG_HOLD | VDEBUG_STEP);
    assert(read_io(f, VIO_DEBUG, 1) == VDEBUG_HOLD);
}

static void
dram_precharge_and_refresh(void)
{
    const uint32_t program[] = { 0, 0, HALT };
    fixture f = machine(program, COUNT(program), 0xffc);
    verite_core_set_precharge(f.core, 1);

    assert(risc_step(f.core, false, 0, f.mem, (verite_bus) { &board, &f }));
    assert(f.core->timer == 2);
    assert(risc_step(f.core, false, 0, f.mem, (verite_bus) { &board, &f }));
    assert(f.core->timer == 4);

    verite_core_refresh(f.core, 2);
    assert(f.core->timer == 6 && f.core->credit == -2);
    assert(risc_step(f.core, false, 0, f.mem, (verite_bus) { &board, &f }));
    assert(f.core->timer == 8 && f.core->halted);
    done(&f);
}

static void
alu_and_delay_slots(void)
{
    const uint32_t program[] = {
        IMM(0x76, 128, 0x1234), IMM(0x77, 129, 0x8000), INS(0x10, 130, 128, 129),
        INS(0x46, 131, 129, 4), INS(0x47, 132, 129, 4), INS(0x61, 0, 3, 128),
        INS(0, 133, 0, 7), INS(0, 134, 0, 1), INS(0, 135, 0, 2),
        INS(0x13, 136, 128, 133), HALT
    };
    fixture f = machine(program, COUNT(program), 0x1000);
    run_core(&f);
    assert(f.core->halted && (f.core->intr & VINTR_HALT));
    assert(f.core->reg[130] == 0x80001234 && f.core->reg[131] == 0xf8000000);
    assert(f.core->reg[132] == 0x08000000 && f.core->reg[133] == 7);
    assert(!f.core->reg[134] && !f.core->reg[135] && f.core->reg[136] == 7U - 0x1234);
    done(&f);
}

static void
vector_register_pairs(void)
{
    const uint32_t program[] = {
        IMM(0x76, 128, 10), IMM(0x76, 129, 20), IMM(0x76, 130, 1), IMM(0x76, 131, 2),
        INS(0x20, 132, 128, 130), INS(0x30, 134, 128, 131), INS(0xc0, 136, 0, 3), HALT
    };
    const uint32_t expected[] = { 11, 22, 12, 22, 0x80000000, 0 };
    fixture        f          = machine(program, COUNT(program), 0);
    run_core(&f);
    assert(!memcmp(f.core->reg + 132, expected, sizeof(expected)));
    done(&f);
}

static void
link_addresses(void)
{
    const uint32_t program[] = { INS(0x6b, 128, 0, 0), 0x6e000004, 0, HALT, INS(0x6f, 129, 0, 254), 0 };
    fixture        f         = machine(program, COUNT(program), 0);
    run_core(&f);
    assert(f.core->reg[128] == 8 && f.core->reg[254] == 0xc && f.core->reg[129] == 0x18);
    assert(f.core->halted && f.core->pc == 0xc);
    done(&f);
}

static void
memory_byte_order(void)
{
    const uint32_t program[] = {
        IMM(0x77, 128, 0x1122), INS(0x4b, 128, 128, 0x33), INS(0, 128, 128, 0x44),
        IMM(0x76, 129, 0x800), 0x7a008081, 0x70820181, 0x71830181, 0x78010081, HALT
    };
    fixture f = machine(program, COUNT(program), 0);
    run_core(&f);
    assert(f.core->reg[128] == 0x11223344 && f.core->reg[130] == 0x22 && f.core->reg[131] == 0x3344);
    assert(!memcmp(f.mem.data + 0x800, "\x44\x33\x00\x11", 4));
    done(&f);
}

static void
fifo_stall(void)
{
    const uint32_t program[] = { INS(0x10, 128, 0, 33), INS(0, 33, 128, 1), HALT };
    fixture        f         = machine(program, COUNT(program), 0);
    run_core(&f);
    assert(f.core->stalled && (read_io(&f, VIO_STATUS, 1) & 0x10));
    write_io(&f, VIO_FIFO, 4, 41);
    run_core(&f);
    assert(f.core->halted && read_io(&f, VIO_FIFO_OUT_VALID, 1) == 1);
    assert(read_io(&f, VIO_FIFO, 4) == 42 && !read_io(&f, VIO_FIFO_OUT_VALID, 1));
    done(&f);
}

static void
host_forced_instructions(void)
{
    fixture f = machine(NULL, 0, 0);
    write_io(&f, VIO_DEBUG, 1, VDEBUG_SOFT_RESET | VDEBUG_HOLD);
    write_io(&f, VIO_DEBUG, 1, VDEBUG_HOLD);
    assert(f.core->pc == RESET_PC);
    force(&f, 0x76fe0800);
    force(&f, 0x77ff1234);
    force(&f, 0x4bffff56);
    force(&f, 0x00ffff78);
    force(&f, 0x7a00fffe);
    assert(mem_read32(f.mem, 0x800) == 0x12345678);
    write_io(&f, VIO_STATE_DATA, 4, 0x100000ff);
    write_io(&f, VIO_STATE_INDEX, 1, 0x82);
    assert(read_io(&f, VIO_STATE_DATA, 4) == 0x12345678);
    force(&f, 0x6c000200);
    force(&f, 0);
    write_io(&f, VIO_STATE_INDEX, 1, 0x81);
    assert(read_io(&f, VIO_STATE_DATA, 4) == 0x800);
    done(&f);
}

static void
reciprocal_seed(void)
{
    const uint32_t program[] = { IMM(0x76, 129, 4), INS(0x42, 128, 129, 0), INS(0x42, 130, 129, 2), HALT };
    fixture        f         = machine(program, COUNT(program), 0);
    run_core(&f);
    assert(f.core->scale == 3 && f.core->reg[128] == 1U << 28 && f.core->reg[130] == 1U << 30);
    done(&f);
}

static void
dma_list(void)
{
    const uint32_t program[]  = { INS(0x10, 128, 0, 33), INS(0x10, 129, 0, 33), INS(0x10, 130, 0, 33), HALT };
    const uint32_t expected[] = { 0x44332211, 0x02000000, 0x03000000 };
    fixture        f          = machine(program, COUNT(program), 0);
    f.ram[0x40]               = 0x200;
    f.ram[0x41]               = 13;
    f.ram[0x80]               = 0x11223344;
    f.ram[0x81]               = 2;
    f.ram[0x82]               = 3;
    write_io(&f, VIO_MODE, 1, VMODE_DMA);
    write_io(&f, VIO_DMA_CMD_PTR, 4, 0x100);
    run_core(&f);
    assert(f.core->halted && !f.core->dma.busy && (f.core->intr & VINTR_DMA));
    assert(!memcmp(f.core->reg + 128, expected, sizeof(expected)));
    done(&f);
}

static void
sixteen_bit(verite_core *c)
{
    c->pe.dst_fmt  = 4;
    c->pe.stride   = 0x0300;
    c->pe.dst_base = 0x10000;
}

static void
parallel_fill(void)
{
    const uint32_t program[] = {
        IMM(0x77, 128, 3), IMM(0x77, 129, 2), INS(0xd8, 140, 128, 0),
        IMM(0x77, 130, 5), INS(0xd7, 142, 130, 0), INS(0xe1, 144, 146, 148), HALT
    };
    fixture f = machine(program, COUNT(program), 0);
    sixteen_bit(f.core);
    f.core->pe.fg = 0xf800f800;
    run_core(&f);
    for (unsigned x = 2; x <= 8; x++)
        assert(mem_read16(f.mem, 0x10800 + 2 * x) == (x >= 3 && x <= 7 ? 0xf800U : 0));
    assert(!f.core->pe.cnt && f.core->drew);
    done(&f);
}

static void
parallel_xor_blit(void)
{
    fixture f = machine(NULL, 0, 0);
    sixteen_bit(f.core);
    pixel_engine *pe = &f.core->pe;
    pe->stride       = 0x0303;
    pe->src_base     = 0x20000;
    pe->src_mode     = 1 << 4;
    for (unsigned x = 0; x < 4; x++) {
        mem_write16(f.mem, 0x20000 + 2 * x, 0x100 + x);
        mem_write16(f.mem, 0x10000 + 2 * x, 0xff);
    }
    pe->dst_mode = 6;
    pe->cnt      = 4;
    pixel_set(pe, P_U, 0);
    pixel_draw_parallel(f.core, 0xe1, 200, 202, 204, f.mem);
    for (unsigned x = 0; x < 4; x++)
        assert(mem_read16(f.mem, 0x10000 + 2 * x) == ((0x100 + x) ^ 0xff));
    done(&f);
}

static void
parameter_iteration(void)
{
    fixture f = machine(NULL, 0, 0);
    sixteen_bit(f.core);
    pixel_engine *pe = &f.core->pe;
    pe->dst_fmt      = 0xc;
    pe->stride       = 0x0400;
    pe->drawctl      = 6;
    f.core->reg[161] = 10 << 16;
    f.core->reg[176] = 1 << 16;
    f.core->reg[177] = 20 << 16;
    pe->cnt          = 3;
    pixel_draw_param(f.core, 0xf3, 192, 160, 176, f.mem);
    assert(mem_read32(f.mem, 0x10000) == 0xff0a0000);
    assert(mem_read32(f.mem, 0x10004) == 0xff1e0000);
    assert(mem_read32(f.mem, 0x10008) == 0xff320000 && !pe->cnt);
    pe->cnt = 1;
    pixel_draw_param(f.core, 0xf0, 192, 160, 176, f.mem);
    assert(f.core->reg[192] == 1U << 16 && f.core->reg[193] == 30U << 16);
    done(&f);
}

static void
five_pair_alignment(void)
{
    fixture f = machine(NULL, 0, 0);
    sixteen_bit(f.core);
    f.core->pe.dst_fmt = 0xc;
    f.core->pe.drawctl = 6 | (1 << 4) | (5 << 9) | (5 << 13);
    f.core->reg[161]   = 20 << 16;
    f.core->reg[163]   = 255 << 16;
    f.core->reg[168]   = 40 << 16;
    f.core->reg[169]   = 60 << 16;
    for (unsigned k = 0; k < 10; k++)
        f.core->reg[176 + k] = (k + 1) << 16;
    f.core->pe.cnt = 1;
    pixel_draw_param(f.core, 0xfc, 199, 167, 183, f.mem);
    assert(mem_read32(f.mem, 0x10000) == 0xff14283c && !f.core->pe.cnt);
    assert(f.core->reg[192] == 1U << 16 && f.core->reg[193] == 22U << 16);
    assert(f.core->reg[200] == 49U << 16 && f.core->reg[201] == 70U << 16);
    done(&f);
}

static void
draw_fifo_order(void)
{
    fixture f = machine(NULL, 0, 0);
    sixteen_bit(f.core);
    f.core->pe.dst_fmt     = 0xc;
    f.core->pe.drawctl     = 6;
    f.core->pe.cnt         = 1;
    const uint32_t words[] = { verite_bswap(1 << 16), 10 << 16, verite_bswap(2 << 16), 20 << 16 };
    for (unsigned i = 0; i < COUNT(words); i++)
        write_io(&f, VIO_FIFO, 4, words[i]);
    pixel_draw_param(f.core, 0xf0, 192, 32, 32, f.mem);
    assert(f.core->reg[192] == 3U << 16 && f.core->reg[193] == 30U << 16);
    assert(!f.core->in_count);
    done(&f);
}

static void
texture_masks(void)
{
    fixture f = machine(NULL, 0, 0);
    sixteen_bit(f.core);
    pixel_engine *pe = &f.core->pe;
    pe->src_base     = 0x30000;
    pe->src_mode     = 4 | (1 << 4);
    pe->stride       = 0x0311;
    pe->src_mask     = 0x00030003;
    for (unsigned x = 0; x < 4; x++)
        mem_write16(f.mem, 0x30000 + 2 * x, 0x1000 * (x + 1));
    pixel_set(pe, P_U, 5 << 16);
    pixel_set(pe, P_V, 0);
    pe->cnt = 1;
    pixel_draw_param(f.core, 0xf0, 192, 160, 176, f.mem);
    assert(mem_read16(f.mem, 0x10000) == 0x2000);
    done(&f);
}

static void
perspective_uses_previous_q(void)
{
    fixture f = machine(NULL, 0, 0);
    sixteen_bit(f.core);
    pixel_engine *pe = &f.core->pe;
    pe->src_base     = 0x30000;
    pe->src_mode     = 4 | (1 << 4);
    pe->drawctl      = 5; /* Q and U, with shadow X. */
    pe->cnt          = 2;
    f.core->reg[160] = 2U << 24;
    f.core->reg[161] = 2U << 16;
    mem_write16(f.mem, 0x30002, 0x07e0);
    mem_write16(f.mem, 0x30004, 0xf800);

    pixel_draw_param(f.core, 0xf1, 192, 160, 176, f.mem);

    assert(mem_read16(f.mem, 0x10000) == 0xf800);
    assert(mem_read16(f.mem, 0x10002) == 0x07e0);
    assert(pe->q == (2U << 24) && pe->x_shadow == (2 << 16) && !pe->cnt);
    done(&f);
}

static void
palette_loader(void)
{
    const uint32_t program[] = {
        0x77a0ffe0, 0x4ba0a005, 0x00a0a080, 0x10a10021, 0x47a2a110,
        0x7800a2a0, 0x14a1a109, 0x600005a1, 0, 0x10a30021,
        0x01a1a101, 0x61fffda1, 0x7a02a3a0, 0x6f0000fe, 0, HALT, HALT
    };
    const uint8_t rgb[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    fixture       f     = machine(program, COUNT(program), 0);
    f.core->reg[254]    = 60;
    write_io(&f, VIO_FIFO, 4, 5 << 16 | 2);
    write_io(&f, VIO_FIFO, 4, 0x00112233);
    write_io(&f, VIO_FIFO, 4, 0x00445566);
    run_core(&f);
    assert(f.core->halted && f.write_count == 7);
    assert(f.writes[0].port == 0xb0 && f.writes[0].value == 5 && f.writes[0].len == 1);
    for (unsigned i = 0; i < COUNT(rgb); i++)
        assert(f.writes[i + 1].port == 0xb1 && f.writes[i + 1].value == rgb[i] && f.writes[i + 1].len == 1);
    done(&f);
}

static void
absolute_jumps(void)
{
    assert(risc_absolute(0x200) == 0x800 && risc_absolute(0xfffffc) == RESET_PC);
    assert(risc_next(0x00fffffc) == 0 && risc_next(0xfffffffc) == 0xff000000);
}

static uint32_t
float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void
float_conversion(void)
{
    assert(f2i(float_bits(1.75f), 0) == 1);
    assert(f2i(float_bits(-1.25f), 0) == (uint32_t) -2);
    assert(f2i(float_bits(1.5f), 16) == 0x18000);
    assert(f2i(float_bits(1e20f), 0) == INT32_MAX);
}

static void
signed_products(void)
{
    assert(alu(0x1a, 0x00020000, 0x00030000) == 0x00060000);
    assert(alu(0x18, (uint32_t) -3, 5) == (uint32_t) -15);
    assert(alu(0x1c, 0x80000000, 0x80000000) == 0x40000000);
}

static void
raster_ops(void)
{
    assert(rop(0xc, 0xf0, 0xaa) == 0xf0 && rop(0x6, 0xf0, 0xaa) == 0x5a);
    assert(rop(0x8, 0xf0, 0xaa) == 0xa0 && rop(0xa, 0xf0, 0xaa) == 0xaa);
    assert(!rop_reads(0xc) && !rop_reads(3) && rop_reads(6) && rop_reads(0xa));
}

static void
check_color(color c, int32_t r, int32_t g, int32_t b, int32_t a)
{
    assert(c.channel[0] == r && c.channel[1] == g && c.channel[2] == b && c.channel[3] == a);
}

static void
pixel_formats(void)
{
    assert(encode(4, (color) {
                         { 255, 255, 255, 255 }
    },
                  0, false)
           == 0xffff);
    check_color(decode(4, 0xffff, false, false), 255, 255, 255, 255);
    check_color(decode(4, 0xffff, false, true), 248, 252, 248, 255);
    assert(encode(6, (color) {
                         { 255, 0, 0, 255 }
    },
                  0, false)
           == 0xfc00);
    assert(encode(0xc, (color) {
                           { 1, 2, 3, 4 }
    },
                  0, true)
           == 0x04030201);
    check_color(decode(1, 0xe3, false, false), 255, 0, 255, 255);
}

static void
dither_before_quantizing(void)
{
    const color c = {
        { 6, 0, 0, 255 }
    };
    assert((encode(4, c, 15, false) >> 11) == 1);
    assert((encode(4, c, 0, false) >> 11) == 0);
}

static void
copy_unpadded_565(void)
{
    fixture       f  = machine(NULL, 0, 0);
    pixel_engine *pe = &f.core->pe;
    sixteen_bit(f.core);
    pe->src_mode     = 0x10 | SRC_NO_PAD | SRC_CHROMA;
    pe->dst_mode     = 12 | DST_DITHER | DST_TRANSP_REJECT;
    pe->chroma_color = 0x12341234;
    pe->chroma_mask  = UINT32_MAX;
    pe->drawctl      = 4;
    pe->src_base     = 0x20000;
    pe->src_mask     = 3;
    pe->cnt          = 2;
    f.core->reg[160] = 1 << 16;
    f.core->reg[161] = 5 << 16;
    f.core->reg[176] = 1 << 16;
    f.core->reg[177] = 1 << 16;
    mem_write16(f.mem, pe->src_base + 2, 0x1234);
    mem_write16(f.mem, pe->src_base + 4, 0xdefb);
    mem_write16(f.mem, pe->dst_base + 2, 0xabcd);
    pixel_draw_param(f.core, 0xf0, 160, 160, 176, f.mem);
    assert(mem_read16(f.mem, pe->dst_base + 2) == 0xabcd && !f.core->drew);
    assert(pe->x == 1 << 16 && pe->u == 5 << 16 && pe->u_mask == 3 && pe->cnt == 1);
    pixel_draw_param(f.core, 0xf0, 160, 160, 176, f.mem);
    assert(mem_read16(f.mem, pe->dst_base + 4) == 0xdefb && f.core->drew);
    assert(f.core->reg[160] == 3U << 16 && f.core->reg[161] == 7U << 16 && !pe->cnt);
    done(&f);
}

static void
perspective_division(void)
{
    uint32_t bits = 0x12345678;
    static const int32_t edge[] = { INT32_MIN, INT32_MIN + 1, -1, 0, 1, INT32_MAX - 1, INT32_MAX };
    for (unsigned i = 1; i <= 8192; i++) {
        bits = bits * 1664525U + 1013904223U;
        uint32_t q = i <= 1024 ? i : (bits | 1);
        int32_t u = edge[i % COUNT(edge)];
        int32_t v = (int32_t) bits;
        int32_t ref_u = (int32_t) (((int64_t) u * (INT64_C(1) << 24)) / q);
        int32_t ref_v = (int32_t) (((int64_t) v * (INT64_C(1) << 24)) / q);
        perspective_uv(&u, &v, q);
        assert(u == ref_u && v == ref_v);
    }
}

static void
bilinear_unpadded_565(void)
{
    uint8_t       data[512] = { 0 };
    verite_memory mem = { data, sizeof(data) - 1 };
    pixel_engine  pe;
    pixel_reset(&pe);
    pe.src_base = 508;
    pe.stride   = 4;
    pe.u_mask = pe.v_mask = 1;
    mem_write16(mem, 508, 0xf800);
    mem_write16(mem, 510, 0x07e0);
    mem_write16(mem, 0, 0x001f);
    mem_write16(mem, 2, 0xffff);

    pe.src_mode = 4 | SRC_FILTER | SRC_NO_PAD;
    check_color(sample(&pe, 0x8000, 0x8000, mem), 124, 126, 124, 255);
    check_color(sample(&pe, 0x1c000, -0x8000, mem), 124, 63, 124, 255);
    pe.src_mode |= SRC_U_CLAMP | SRC_V_CLAMP;
    check_color(sample(&pe, 0x1c000, -0x8000, mem), 0, 252, 0, 255);
}

#ifdef VERITE_JIT_X64
static unsigned
jit_compare(fixture *native, fixture *reference, unsigned budget)
{
    verite_core *a = native->core;
    verite_core *b = reference->core;
    unsigned cycles = risc_jit_run(a, native->mem, budget);
    for (unsigned used = 0; used < cycles;) {
        assert(risc_step(b, false, 0, reference->mem, (verite_bus) { &board, reference }));
        used += 1 + b->dram_wait;
        assert(used <= cycles);
    }
    assert(a->pc == b->pc && a->npc == b->npc && a->timer == b->timer);
    assert(a->dram_row == b->dram_row && a->dram_wait == b->dram_wait);
    assert(a->clip == b->clip && a->drew == b->drew && a->stalled == b->stalled);
    assert(!memcmp(a->reg, b->reg, sizeof(a->reg)));
    assert(!memcmp(native->mem.data, reference->mem.data, native->mem.mask + 1));
    return cycles;
}

static void
jit_program(fixture *a, fixture *b, const uint32_t *words, size_t count)
{
    a->core->pc = b->core->pc = 0;
    a->core->npc = b->core->npc = 4;
    for (size_t i = 0; i < count; i++) {
        mem_write32(a->mem, i * 4, words[i]);
        mem_write32(b->mem, i * 4, words[i]);
    }
}

static void
jit_differential(void)
{
    static const uint32_t loop[] = {
        INS(0x01, 128, 128, 1),
        INS(0x61, 0xff, 0xfe, 128),          /* bnez r128, entry */
        INS(0x00, 129, 129, 7),              /* delay slot */
        HALT
    };
    fixture a = machine(loop, COUNT(loop), 0);
    fixture b = machine(loop, COUNT(loop), 0);
    a.core->reg[128] = b.core->reg[128] = 7;
    a.core->timer = b.core->timer = UINT32_MAX - 9;
    unsigned cycles = jit_compare(&a, &b, 11);
    if (a.core->jit_unavailable) {
        done(&a);
        done(&b);
        return;
    }
    assert(cycles == 9 && a.core->pc == 0);
    assert(jit_compare(&a, &b, 40) == 12 && a.core->pc == 12);
    assert(a.core->reg[129] == 49);

    /* Recompile the same PC after VRAM changes. Cover all native arithmetic
       forms, including segmented aliasing and signed multiply edge cases. */
    static const uint8_t ops[] = {
        0x40, 0x41, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d,
        0x52, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5c, 0x5d, 0x5e,
        0x76, 0x77
    };
    static const uint8_t fields[] = { 0, 3, 13, 16, 64, 65, 87, 112, 128, 129, 164, 231, 255 };
    uint32_t value = 0x12345678;
    for (unsigned round = 0; round < 12; round++) {
        uint32_t code[33];
        for (unsigned r = 128; r < 256; r++) {
            value = value * 1664525U + 1013904223U;
            a.core->reg[r] = b.core->reg[r] = value;
        }
        a.core->seg = b.core->seg = round * 0x463U;
        unsigned n = COUNT(fields);
        for (unsigned i = 0; i < 32; i++) {
            unsigned op = round < 8 ? ((round & 3) << 4) | (i % 15)
                                    : ops[i % COUNT(ops)];
            if ((op & 15) == 15)
                op--;
            value = value * 1664525U + 1013904223U;
            unsigned s1 = (op < 0x10 || (op >= 0x40 && op < 0x50)) ? (uint8_t) value : fields[(value >> 24) % n];
            code[i] = INS(op, fields[((value >> 8) + i) % n], fields[(value >> 16) % n], s1);
        }
        code[32] = HALT;
        jit_program(&a, &b, code, 33);
        a.core->slow_precharge = b.core->slow_precharge = round == 0;
        assert(jit_compare(&a, &b, 40) == (round == 0 ? 33 : 32));
    }

    static const uint32_t memory[] = {
        INS(0x70, 129, 0, 128), INS(0x71, 130, 0, 128), INS(0x72, 131, 0, 128),
        INS(0x78, 1, 131, 128), 0, INS(0x79, 2, 131, 128),
        0, INS(0x7a, 8, 132, 128), HALT,
        INS(0x74, 135, 1, 128), INS(0x7c, 16, 135, 128), HALT
    };
    jit_program(&a, &b, memory, COUNT(memory));
    a.core->reg[128] = b.core->reg[128] = 0;
    a.core->reg[132] = b.core->reg[132] = IMM(0x76, 140, 0xface);
    assert(jit_compare(&a, &b, 5) == 4);
    assert(jit_compare(&a, &b, 5) == 2);
    assert(jit_compare(&a, &b, 5) == 2);
    assert(mem_read32(a.mem, 32) == a.core->reg[132]);
    assert(jit_compare(&a, &b, 5) == 3);
    assert(a.core->reg[140] == 0xface);

    static const uint32_t side_exit[] = {
        IMM(0x76, 140, 0x1234),
        INS(0x72, 131, 0, 128),
        IMM(0x76, 140, 0xffff),
        HALT
    };
    jit_program(&a, &b, side_exit, COUNT(side_exit));
    a.core->reg[128] = b.core->reg[128] = 0xffe00400;
    assert(jit_compare(&a, &b, 5) == 1);
    assert(a.core->pc == 4 && a.core->reg[140] == 0x1234);
    assert(jit_compare(&a, &b, 5) == 0);
    done(&a);
    done(&b);
    puts("Vérité x86-64 JIT differential cases passed");
}
#endif

int
main(void)
{
    dram_precharge_and_refresh();
    alu_and_delay_slots();
    vector_register_pairs();
    link_addresses();
    memory_byte_order();
    fifo_stall();
    host_forced_instructions();
    reciprocal_seed();
    dma_list();
    parallel_fill();
    parallel_xor_blit();
    parameter_iteration();
    five_pair_alignment();
    draw_fifo_order();
    texture_masks();
    perspective_uses_previous_q();
    perspective_division();
    palette_loader();
    absolute_jumps();
    float_conversion();
    signed_products();
    raster_ops();
    pixel_formats();
    dither_before_quantizing();
    copy_unpadded_565();
    bilinear_unpadded_565();
    puts("29 Vérité regression cases passed");
#ifdef VERITE_JIT_X64
    jit_differential();
#endif
    return 0;
}

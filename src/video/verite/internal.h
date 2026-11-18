/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Vérité low-level core, ported from rust-dos. */
#ifndef VERITE_INTERNAL_H
#define VERITE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "core.h"

enum {
    VIO_FIFO           = 0x00,
    VIO_FIFO_IN_FREE   = 0x40,
    VIO_FIFO_OUT_VALID = 0x41,
    VIO_COMM           = 0x42,
    VIO_MEM_ENDIAN     = 0x43,
    VIO_INTR           = 0x44,
    VIO_INTR_EN        = 0x46,
    VIO_DEBUG          = 0x48,
    VIO_LOW_WATER_MARK = 0x49,
    VIO_STATUS         = 0x4A,
    VIO_DMA_CMD_PTR    = 0x50,
    VIO_DMA_ADDRESS    = 0x54,
    VIO_DMA_LENGTH     = 0x58,
    VIO_MEM_BASE       = 0x5E,
    VIO_STATE_INDEX    = 0x60,
    VIO_STATE_DATA     = 0x64,
    VIO_MODE           = 0x72,
    VIO_BANK_SELECT    = 0x74,
};

enum {
    VINTR_VERT       = 0x0001,
    VINTR_FIFO_LOW   = 0x0002,
    VINTR_RISC       = 0x0004,
    VINTR_HALT       = 0x0008,
    VINTR_FIFO_ERROR = 0x0010,
    VINTR_DMA        = 0x0040,
    VINTR_IDLE       = 0x0400,
};

enum {
    VDEBUG_SOFT_RESET = 0x01,
    VDEBUG_HOLD       = 0x02,
    VDEBUG_STEP       = 0x04,
};

enum {
    VMODE_VESA = 0x01,
    VMODE_VGA  = 0x02,
    VMODE_DMA  = 0x08,
};

enum { IN_FIFO  = 32,
       OUT_FIFO = 8,
       ROM_SIZE = 128 * 1024 };
#define RESET_PC    UINT32_C(0xfffffff0)
#define LOADER_PORT UINT32_C(0xffe00ff8)

typedef enum {
    P_NONE,
    P_XSHADOW,
    P_X,
    P_Y,
    P_Z,
    P_U,
    P_V,
    P_Q,
    P_R,
    P_G,
    P_B,
    P_A,
    P_F,
    P_I,
    P_CNT
} pixel_param;

typedef struct {
    uint32_t src_base;
    uint32_t src_mode;
    uint32_t src_mask;
    uint32_t stride;
    uint32_t dst_base;
    uint32_t dst_mode;
    uint32_t dst_fmt;
    uint32_t pmask;
    uint32_t pattern;
    uint32_t pat_mode;
    uint32_t pat_offset;
    uint32_t scissor_x;
    uint32_t scissor_y;
    uint32_t z_base;
    uint32_t dither_offset;
    uint32_t fg;
    uint32_t bg;
    uint32_t fog_color;
    uint32_t dst_color;
    uint32_t chroma_color;
    uint32_t yuv_contrast;
    uint32_t chroma_mask;
    uint32_t alpha_thres;
    uint32_t spec_color;
    uint32_t tex4pal[8];
    bool     pick_valid;
    bool     pick_hit;
    int32_t  x_shadow;
    bool     x_dir;
    int32_t  x;
    int32_t  y;
    int32_t  z;
    bool     z_ovf;
    int32_t  u;
    int32_t  v;
    bool     u_dir;
    bool     v_dir;
    uint32_t u_mask;
    uint32_t v_mask;
    uint32_t q;
    int32_t  r;
    int32_t  g;
    int32_t  b;
    int32_t  a;
    int32_t  f;
    uint32_t cnt;
    uint32_t drawctl;
} pixel_engine;

typedef struct {
    uint8_t *data;
    size_t   mask;
} verite_memory;

typedef struct {
    const verite_board *ops;
    void               *opaque;
} verite_bus;

typedef struct {
    bool     busy;
    uint32_t ptr, addr, left;
    uint8_t  swap;
} verite_dma;

typedef enum { LOADER_ADDRESS,
               LOADER_LENGTH,
               LOADER_WORDS } loader_state;
typedef struct {
    bool         active, has_start, keyed;
    loader_state expect;
    uint32_t     address, left, start;
    uint32_t    *words;
    size_t       count, capacity;
    uint8_t      key[128];
} verite_loader;

struct verite_core {
    uint32_t      reg[256], pc, npc, flag, seg, seed, scale, seed_index, clip;
    uint32_t      timer, riscintr;
    bool          halted, stalled, drew, has_forced;
    uint32_t      forced;
    pixel_engine  pe;
    uint32_t      input[IN_FIFO];
    unsigned      in_head, in_count;
    uint32_t     *output;
    size_t        out_head, out_count, out_capacity;
    uint32_t      partial, partial_mask;
    uint8_t       regs[0x80];
    uint16_t      intr;
    verite_dma    dma;
    verite_loader loader;
    uint8_t       rom[ROM_SIZE];
    uint64_t      last_ns;
    int64_t       credit;
    unsigned      polls;
};

static inline uint32_t
verite_bswap(uint32_t v)
{
    return (v << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24);
}

static inline uint8_t
core_byte(const verite_core *core, unsigned reg)
{
    return core->regs[reg & 0x7f];
}

static inline uint32_t
mem_read8(verite_memory mem, uint32_t addr)
{
    return mem.data[(addr & mem.mask) ^ 3];
}

static inline uint32_t
mem_read16(verite_memory mem, uint32_t addr)
{
    size_t i = ((addr & mem.mask) & ~(size_t) 1) ^ 2;
    return mem.data[i] | ((uint32_t) mem.data[i + 1] << 8);
}

static inline uint32_t
mem_read32(verite_memory mem, uint32_t addr)
{
    size_t i = (addr & mem.mask) & ~(size_t) 3;
    return mem.data[i] | ((uint32_t) mem.data[i + 1] << 8) | ((uint32_t) mem.data[i + 2] << 16) | ((uint32_t) mem.data[i + 3] << 24);
}

static inline void
mem_write8(verite_memory mem, uint32_t addr, uint32_t value)
{
    mem.data[(addr & mem.mask) ^ 3] = value;
}

static inline void
mem_write16(verite_memory mem, uint32_t addr, uint32_t value)
{
    size_t i        = ((addr & mem.mask) & ~(size_t) 1) ^ 2;
    mem.data[i]     = value;
    mem.data[i + 1] = value >> 8;
}

static inline void
mem_write32(verite_memory mem, uint32_t addr, uint32_t value)
{
    size_t i = (addr & mem.mask) & ~(size_t) 3;
    for (unsigned byte = 0; byte < 4; byte++)
        mem.data[i + byte] = value >> (8 * byte);
}

static inline uint32_t
risc_next(uint32_t addr)
{
    return (addr & 0xff000000) | ((addr + 4) & 0x00ffffff);
}

bool verite_pop_input(verite_core *core, uint32_t *value);
void verite_push_output(verite_core *core, uint32_t value);
void verite_loader_reset(verite_core *core);
void verite_loader_begin(verite_core *core);
void verite_loader_pump(verite_core *core, verite_memory mem);
void verite_init_rom(verite_core *core);

uint32_t risc_fetch(const verite_core *core, uint32_t addr, verite_memory mem);
uint32_t risc_peek(const verite_core *core, uint8_t reg);
uint32_t risc_src(verite_core *core, uint8_t reg);
void     risc_dst(verite_core *core, uint8_t reg, uint32_t value);
bool     risc_step(verite_core *core, bool forced, uint32_t ir, verite_memory mem, verite_bus bus);

void     pixel_reset(pixel_engine *pe);
void     pixel_set(pixel_engine *pe, pixel_param param, uint32_t value);
uint32_t pixel_state(const pixel_engine *pe, uint8_t index);
void     pixel_spr(pixel_engine *pe, uint32_t index, uint32_t value, verite_memory mem);
void     pixel_draw_parallel(verite_core *core, uint8_t op, uint8_t d, uint8_t s2, uint8_t s1, verite_memory mem);
void     pixel_draw_param(verite_core *core, uint8_t op, uint8_t d, uint8_t s2, uint8_t s1, verite_memory mem);

#endif

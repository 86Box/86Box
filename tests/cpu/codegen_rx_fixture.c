// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../src/codegen/codegen_backend_x86-64.c"

int block_pos;
uint8_t *block_write_data;
codeblock_t *codeblock;

void
fatal(const char *format, ...)
{
    abort();
}

struct mem_block_t *
codegen_allocator_allocate(struct mem_block_t *parent, int owner)
{
    abort();
}

uint8_t *
codeblock_allocator_get_ptr(struct mem_block_t *block)
{
    abort();
}

size_t
codegen_rx_memory(uint8_t *bytes, int p66, int w, uint16_t op, int reg, int base, int32_t disp)
{
    codeblock_t block = { 0 };

    block_write_data = bytes;
    block_pos = 0;
    emit_mem(&block, p66 ? 0x66 : 0, w, 0, op > 0xff ? 2 : 1, op, reg,
             base, NO_INDEX, 0, disp, DISP_AUTO, 0, 0);
    return block_pos;
}

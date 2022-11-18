#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/mem.h>
#include "cpu.h"
#include "x86_ops.h"
#include "codegen.h"

void (*codegen_timing_start)(void);
void (*codegen_timing_prefix)(uint8_t prefix, uint32_t fetchdat);
void (*codegen_timing_opcode)(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc);
void (*codegen_timing_block_start)(void);
void (*codegen_timing_block_end)(void);
int (*codegen_timing_jump_cycles)(void);

void codegen_timing_set(codegen_timing_t *timing)
{
        codegen_timing_start = timing->start;
        codegen_timing_prefix = timing->prefix;
        codegen_timing_opcode = timing->opcode;
        codegen_timing_block_start = timing->block_start;
        codegen_timing_block_end = timing->block_end;
        codegen_timing_jump_cycles = timing->jump_cycles;
}

int codegen_in_recompile;

/* This is for compatibility with new x87 code. */
void codegen_set_rounding_mode(int mode)
{
	/* cpu_state.new_npxc = (cpu_state.old_npxc & ~0xc00) | (cpu_state.npxc & 0xc00); */
	cpu_state.new_npxc = (cpu_state.old_npxc & ~0xc00) | (mode << 10);
}

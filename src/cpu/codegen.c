#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/mem.h>
#include "cpu.h"
#include "x86_ops.h"
#include "codegen.h"

void (*codegen_timing_start)();
void (*codegen_timing_prefix)(uint8_t prefix, uint32_t fetchdat);
void (*codegen_timing_opcode)(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc);
void (*codegen_timing_block_start)();
void (*codegen_timing_block_end)();
int (*codegen_timing_jump_cycles)();

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

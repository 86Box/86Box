#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM

#include <stdint.h>
#include <stdlib.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "codegen.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_backend_arm_defs.h"
#include "codegen_backend_arm_ops.h"
#include "codegen_reg.h"
#include "x86.h"
#include "x87.h"

#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#if defined WIN32 || defined _WIN32 || defined _WIN32
#include <windows.h>
#endif

void *codegen_mem_load_byte;
void *codegen_mem_load_word;
void *codegen_mem_load_long;
void *codegen_mem_load_quad;
void *codegen_mem_load_single;
void *codegen_mem_load_double;

void *codegen_mem_store_byte;
void *codegen_mem_store_word;
void *codegen_mem_store_long;
void *codegen_mem_store_quad;
void *codegen_mem_store_single;
void *codegen_mem_store_double;

void *codegen_fp_round;

void *codegen_gpf_rout;
void *codegen_exit_rout;

host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS] =
{
        {REG_R4, 0},
        {REG_R5, 0},
        {REG_R6, 0},
	{REG_R7, 0},
	{REG_R8, 0},
	{REG_R9, 0},
	{REG_R11, 0}
};

host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS] =
{
        {REG_D8, 0},
        {REG_D9, 0},
        {REG_D10, 0},
	{REG_D11, 0},
	{REG_D12, 0},
	{REG_D13, 0},
	{REG_D14, 0},
	{REG_D15, 0}
};

static void build_load_routine(codeblock_t *block, int size, int is_float)
{
        uint32_t *branch_offset;
        uint32_t *misaligned_offset;

        /*In - R0 = address
          Out - R0 = data, R1 = abrt*/
	/*MOV R1, R0, LSR #12
	  MOV R2, #readlookup2
	  LDR R1, [R2, R1, LSL #2]
	  CMP R1, #-1
	  BNE +
	  LDRB R0, [R1, R0]
	  MOV R1, #0
	  MOV PC, LR
	* STR LR, [SP, -4]!
	  BL readmembl
	  LDRB R1, cpu_state.abrt
	  LDR PC, [SP], #4
	*/
	codegen_alloc(block, 80);
	host_arm_MOV_REG_LSR(block, REG_R1, REG_R0, 12);
	host_arm_MOV_IMM(block, REG_R2, (uint32_t)readlookup2);
	host_arm_LDR_REG_LSL(block, REG_R1, REG_R2, REG_R1, 2);
	if (size != 1)
	{
		host_arm_TST_IMM(block, REG_R0, size-1);
		misaligned_offset = host_arm_BNE_(block);
	}
	host_arm_CMP_IMM(block, REG_R1, -1);
	branch_offset = host_arm_BEQ_(block);
	if (size == 1 && !is_float)
		host_arm_LDRB_REG(block, REG_R0, REG_R1, REG_R0);
	else if (size == 2 && !is_float)
		host_arm_LDRH_REG(block, REG_R0, REG_R1, REG_R0);
	else if (size == 4 && !is_float)
		host_arm_LDR_REG(block, REG_R0, REG_R1, REG_R0);
	else if (size == 4 && is_float)
	{
		host_arm_ADD_REG(block, REG_R0, REG_R0, REG_R1);
		host_arm_VLDR_S(block, REG_D_TEMP, REG_R0, 0);
	}
	else if (size == 8)
	{
		host_arm_ADD_REG(block, REG_R0, REG_R0, REG_R1);
		host_arm_VLDR_D(block, REG_D_TEMP, REG_R0, 0);
	}
	host_arm_MOV_IMM(block, REG_R1, 0);
	host_arm_MOV_REG(block, REG_PC, REG_LR);

        *branch_offset |= ((((uintptr_t)&block_write_data[block_pos] - (uintptr_t)branch_offset) - 8) & 0x3fffffc) >> 2;
	if (size != 1)
	        *misaligned_offset |= ((((uintptr_t)&block_write_data[block_pos] - (uintptr_t)misaligned_offset) - 8) & 0x3fffffc) >> 2;
	host_arm_STR_IMM_WB(block, REG_LR, REG_HOST_SP, -4);
	if (size == 1)
		host_arm_BL(block, (uintptr_t)readmembl);
	else if (size == 2)
		host_arm_BL(block, (uintptr_t)readmemwl);
	else if (size == 4)
		host_arm_BL(block, (uintptr_t)readmemll);
	else if (size == 8)
		host_arm_BL(block, (uintptr_t)readmemql);
	else
		fatal("build_load_routine - unknown size %i\n", size);
	if (size == 4 && is_float)
		host_arm_VMOV_S_32(block, REG_D_TEMP, REG_R0);
	else if (size == 8)
		host_arm_VMOV_D_64(block, REG_D_TEMP, REG_R0, REG_R1);
	host_arm_LDRB_ABS(block, REG_R1, &cpu_state.abrt);
	host_arm_LDR_IMM_POST(block, REG_PC, REG_HOST_SP, 4);
}

static void build_store_routine(codeblock_t *block, int size, int is_float)
{
        uint32_t *branch_offset;
        uint32_t *misaligned_offset;

        /*In - R0 = address
          Out - R0 = data, R1 = abrt*/
	/*MOV R1, R0, LSR #12
	  MOV R2, #readlookup2
	  LDR R1, [R2, R1, LSL #2]
	  CMP R1, #-1
	  BNE +
	  LDRB R0, [R1, R0]
	  MOV R1, #0
	  MOV PC, LR
	* STR LR, [SP, -4]!
	  BL readmembl
	  LDRB R1, cpu_state.abrt
	  LDR PC, [SP], #4
	*/
	codegen_alloc(block, 80);
	host_arm_MOV_REG_LSR(block, REG_R2, REG_R0, 12);
	host_arm_MOV_IMM(block, REG_R3, (uint32_t)writelookup2);
	host_arm_LDR_REG_LSL(block, REG_R2, REG_R3, REG_R2, 2);
	if (size != 1)
	{
		host_arm_TST_IMM(block, REG_R0, size-1);
		misaligned_offset = host_arm_BNE_(block);
	}
	host_arm_CMP_IMM(block, REG_R2, -1);
	branch_offset = host_arm_BEQ_(block);
	if (size == 1 && !is_float)
		host_arm_STRB_REG(block, REG_R1, REG_R2, REG_R0);
	else if (size == 2 && !is_float)
		host_arm_STRH_REG(block, REG_R1, REG_R2, REG_R0);
	else if (size == 4 && !is_float)
		host_arm_STR_REG(block, REG_R1, REG_R2, REG_R0);
	else if (size == 4 && is_float)
	{
		host_arm_ADD_REG(block, REG_R0, REG_R0, REG_R2);
		host_arm_VSTR_S(block, REG_D_TEMP, REG_R0, 0);
	}
	else if (size == 8)
	{
		host_arm_ADD_REG(block, REG_R0, REG_R0, REG_R2);
		host_arm_VSTR_D(block, REG_D_TEMP, REG_R0, 0);
	}
	host_arm_MOV_IMM(block, REG_R1, 0);
	host_arm_MOV_REG(block, REG_PC, REG_LR);

        *branch_offset |= ((((uintptr_t)&block_write_data[block_pos] - (uintptr_t)branch_offset) - 8) & 0x3fffffc) >> 2;
	if (size != 1)
	        *misaligned_offset |= ((((uintptr_t)&block_write_data[block_pos] - (uintptr_t)misaligned_offset) - 8) & 0x3fffffc) >> 2;
	host_arm_STR_IMM_WB(block, REG_LR, REG_HOST_SP, -4);
	if (size == 4 && is_float)
		host_arm_VMOV_32_S(block, REG_R1, REG_D_TEMP);
	else if (size == 8)
		host_arm_VMOV_64_D(block, REG_R2, REG_R3, REG_D_TEMP);
	if (size == 1)
		host_arm_BL(block, (uintptr_t)writemembl);
	else if (size == 2)
		host_arm_BL(block, (uintptr_t)writememwl);
	else if (size == 4)
		host_arm_BL(block, (uintptr_t)writememll);
	else if (size == 8)
		host_arm_BL_r1(block, (uintptr_t)writememql);
	else
		fatal("build_store_routine - unknown size %i\n", size);
	host_arm_LDRB_ABS(block, REG_R1, &cpu_state.abrt);
	host_arm_LDR_IMM_POST(block, REG_PC, REG_HOST_SP, 4);
}

static void build_loadstore_routines(codeblock_t *block)
{
        codegen_mem_load_byte = &block_write_data[block_pos];
        build_load_routine(block, 1, 0);
        codegen_mem_load_word = &block_write_data[block_pos];
        build_load_routine(block, 2, 0);
        codegen_mem_load_long = &block_write_data[block_pos];
        build_load_routine(block, 4, 0);
        codegen_mem_load_quad = &block_write_data[block_pos];
        build_load_routine(block, 8, 0);
        codegen_mem_load_single = &block_write_data[block_pos];
        build_load_routine(block, 4, 1);
        codegen_mem_load_double = &block_write_data[block_pos];
        build_load_routine(block, 8, 1);

        codegen_mem_store_byte = &block_write_data[block_pos];
        build_store_routine(block, 1, 0);
        codegen_mem_store_word = &block_write_data[block_pos];
        build_store_routine(block, 2, 0);
        codegen_mem_store_long = &block_write_data[block_pos];
        build_store_routine(block, 4, 0);
        codegen_mem_store_quad = &block_write_data[block_pos];
        build_store_routine(block, 8, 0);
        codegen_mem_store_single = &block_write_data[block_pos];
        build_store_routine(block, 4, 1);
        codegen_mem_store_double = &block_write_data[block_pos];
        build_store_routine(block, 8, 1);
}

/*VFP has a specific round-to-zero instruction, and the default rounding mode
  is nearest. For round up/down, temporarily change the rounding mode in FPCSR*/
#define FPCSR_ROUNDING_MASK (3 << 22)
#define FPCSR_ROUNDING_UP   (1 << 22)
#define FPCSR_ROUNDING_DOWN (2 << 22)

static void build_fp_round_routine(codeblock_t *block)
{
	uint32_t *jump_table;

	codegen_alloc(block, 80);

	host_arm_MOV_REG(block, REG_TEMP2, REG_LR);
	host_arm_MOV_REG(block, REG_LR, REG_TEMP2);
	host_arm_LDR_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.new_fp_control - (uintptr_t)&cpu_state);
	host_arm_LDR_REG(block, REG_PC, REG_PC, REG_TEMP);
	host_arm_NOP(block);

	jump_table = (uint32_t *)&block_write_data[block_pos];
	host_arm_NOP(block);
	host_arm_NOP(block);
	host_arm_NOP(block);
	host_arm_NOP(block);

	jump_table[X87_ROUNDING_NEAREST] = (uint64_t)(uintptr_t)&block_write_data[block_pos]; //tie even
	host_arm_VCVTR_IS_D(block, REG_D_TEMP, REG_D_TEMP);
	host_arm_MOV_REG(block, REG_PC, REG_LR);

	jump_table[X87_ROUNDING_UP] = (uint64_t)(uintptr_t)&block_write_data[block_pos]; //pos inf
	host_arm_LDR_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.old_fp_control - (uintptr_t)&cpu_state);
	host_arm_BIC_IMM(block, REG_TEMP2, REG_TEMP, FPCSR_ROUNDING_MASK);
	host_arm_ORR_IMM(block, REG_TEMP2, REG_TEMP2, FPCSR_ROUNDING_UP);
	host_arm_VMSR_FPSCR(block, REG_TEMP2);
	host_arm_VCVTR_IS_D(block, REG_D_TEMP, REG_D_TEMP);
	host_arm_VMSR_FPSCR(block, REG_TEMP);
	host_arm_MOV_REG(block, REG_PC, REG_LR);

	jump_table[X87_ROUNDING_DOWN] = (uint64_t)(uintptr_t)&block_write_data[block_pos]; //neg inf
	host_arm_LDR_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.old_fp_control - (uintptr_t)&cpu_state);
	host_arm_BIC_IMM(block, REG_TEMP2, REG_TEMP, FPCSR_ROUNDING_MASK);
	host_arm_ORR_IMM(block, REG_TEMP2, REG_TEMP, FPCSR_ROUNDING_DOWN);
	host_arm_VMSR_FPSCR(block, REG_TEMP2);
	host_arm_VCVTR_IS_D(block, REG_D_TEMP, REG_D_TEMP);
	host_arm_VMSR_FPSCR(block, REG_TEMP);
	host_arm_MOV_REG(block, REG_PC, REG_LR);
	
	jump_table[X87_ROUNDING_CHOP] = (uint64_t)(uintptr_t)&block_write_data[block_pos]; //zero
	host_arm_VCVT_IS_D(block, REG_D_TEMP, REG_D_TEMP);
	host_arm_MOV_REG(block, REG_PC, REG_LR);
}

void codegen_backend_init()
{
	codeblock_t *block;
        int c;

	codeblock = malloc(BLOCK_SIZE * sizeof(codeblock_t));
        codeblock_hash = malloc(HASH_SIZE * sizeof(codeblock_t *));

        memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
        memset(codeblock_hash, 0, HASH_SIZE * sizeof(codeblock_t *));

        for (c = 0; c < BLOCK_SIZE; c++)
                codeblock[c].pc = BLOCK_PC_INVALID;

        block_current = 0;
        block_pos = 0;
        block = &codeblock[block_current];
        block->head_mem_block = codegen_allocator_allocate(NULL, block_current);
        block->data = codeblock_allocator_get_ptr(block->head_mem_block);
        block_write_data = block->data;
        build_loadstore_routines(&codeblock[block_current]);
printf("block_pos=%i\n", block_pos);

        codegen_fp_round = &block_write_data[block_pos];
	build_fp_round_routine(&codeblock[block_current]);

	codegen_alloc(block, 80);
        codegen_gpf_rout = &block_write_data[block_pos];
	host_arm_MOV_IMM(block, REG_R0, 0);
	host_arm_MOV_IMM(block, REG_R1, 0);
	host_arm_call(block, x86gpf);

        codegen_exit_rout = &block_write_data[block_pos];
	host_arm_ADD_IMM(block, REG_HOST_SP, REG_HOST_SP, 0x40);
	host_arm_LDMIA_WB(block, REG_HOST_SP, REG_MASK_LOCAL | REG_MASK_PC);

        block_write_data = NULL;
//fatal("block_pos=%i\n", block_pos);
	asm("vmrs %0, fpscr\n"
                : "=r" (cpu_state.old_fp_control)
	);
	if ((cpu_state.old_fp_control >> 22) & 3)
		fatal("VFP not in nearest rounding mode\n");
}

void codegen_set_rounding_mode(int mode)
{
	if (mode < 0 || mode > 3)
		fatal("codegen_set_rounding_mode - invalid mode\n");
        cpu_state.new_fp_control = mode << 2;
}

/*R10 - cpu_state*/
void codegen_backend_prologue(codeblock_t *block)
{
        block_pos = BLOCK_START;

	/*Entry code*/

	host_arm_STMDB_WB(block, REG_HOST_SP, REG_MASK_LOCAL | REG_MASK_LR);
	host_arm_SUB_IMM(block, REG_HOST_SP, REG_HOST_SP, 0x40);
	host_arm_MOV_IMM(block, REG_CPUSTATE, (uint32_t)&cpu_state);
        if (block->flags & CODEBLOCK_HAS_FPU)
        {
		host_arm_LDR_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.TOP - (uintptr_t)&cpu_state);
		host_arm_SUB_IMM(block, REG_TEMP, REG_TEMP, block->TOP);
		host_arm_STR_IMM(block, REG_TEMP, REG_HOST_SP, IREG_TOP_diff_stack_offset);
        }
}

void codegen_backend_epilogue(codeblock_t *block)
{
	host_arm_ADD_IMM(block, REG_HOST_SP, REG_HOST_SP, 0x40);
	host_arm_LDMIA_WB(block, REG_HOST_SP, REG_MASK_LOCAL | REG_MASK_PC);

	codegen_allocator_clean_blocks(block->head_mem_block);
}

#endif

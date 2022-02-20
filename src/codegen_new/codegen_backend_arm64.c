#if defined __aarch64__ || defined _M_ARM64

#include <stdlib.h>
#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "codegen.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_backend_arm64_defs.h"
#include "codegen_backend_arm64_ops.h"
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
#include <string.h>

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
void *codegen_fp_round_quad;

void *codegen_gpf_rout;
void *codegen_exit_rout;

host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS] =
{
        {REG_X19, 0},
        {REG_X20, 0},
        {REG_X21, 0},
	{REG_X22, 0},
	{REG_X23, 0},
        {REG_X24, 0},
        {REG_X25, 0},
	{REG_X26, 0},
	{REG_X27, 0},
	{REG_X28, 0}
};

host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS] =
{
        {REG_V8, 0},
        {REG_V9, 0},
        {REG_V10, 0},
	{REG_V11, 0},
	{REG_V12, 0},
        {REG_V13, 0},
        {REG_V14, 0},
	{REG_V15, 0}
};

static void build_load_routine(codeblock_t *block, int size, int is_float)
{
        uint32_t *branch_offset;
        uint32_t *misaligned_offset;
	int offset;

        /*In - W0 = address
          Out - W0 = data, W1 = abrt*/
	/*MOV W1, W0, LSR #12
	  MOV X2, #readlookup2
	  LDR X1, [X2, X1, LSL #3]
	  CMP X1, #-1
	  BEQ +
	  LDRB W0, [X1, X0]
	  MOV W1, #0
	  RET
	* STP X29, X30, [SP, #-16]
	  BL readmembl
	  LDRB R1, cpu_state.abrt
	  LDP X29, X30, [SP, #-16]
	  RET
	*/
	codegen_alloc(block, 80);
	host_arm64_MOV_REG_LSR(block, REG_W1, REG_W0, 12);
	host_arm64_MOVX_IMM(block, REG_X2, (uint64_t)readlookup2);
	host_arm64_LDRX_REG_LSL3(block, REG_X1, REG_X2, REG_X1);
	if (size != 1)
	{
		host_arm64_TST_IMM(block, REG_W0, size-1);
		misaligned_offset = host_arm64_BNE_(block);
	}
	host_arm64_CMPX_IMM(block, REG_X1, -1);
	branch_offset = host_arm64_BEQ_(block);
	if (size == 1 && !is_float)
		host_arm64_LDRB_REG(block, REG_W0, REG_W1, REG_W0);
	else if (size == 2 && !is_float)
		host_arm64_LDRH_REG(block, REG_W0, REG_W1, REG_W0);
	else if (size == 4 && !is_float)
		host_arm64_LDR_REG(block, REG_W0, REG_W1, REG_W0);
	else if (size == 4 && is_float)
		host_arm64_LDR_REG_F32(block, REG_V_TEMP, REG_W1, REG_W0);
	else if (size == 8)
		host_arm64_LDR_REG_F64(block, REG_V_TEMP, REG_W1, REG_W0);
	host_arm64_MOVZ_IMM(block, REG_W1, 0);
	host_arm64_RET(block, REG_X30);

	host_arm64_branch_set_offset(branch_offset, &block_write_data[block_pos]);
	if (size != 1)
		host_arm64_branch_set_offset(misaligned_offset, &block_write_data[block_pos]);
	host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
	if (size == 1)
		host_arm64_call(block, (void *)readmembl);
	else if (size == 2)
		host_arm64_call(block, (void *)readmemwl);
	else if (size == 4)
		host_arm64_call(block, (void *)readmemll);
	else if (size == 8)
		host_arm64_call(block, (void *)readmemql);
	else
		fatal("build_load_routine - unknown size %i\n", size);
	codegen_direct_read_8(block, REG_W1, &cpu_state.abrt);
	if (size == 4 && is_float)
		host_arm64_FMOV_S_W(block, REG_V_TEMP, REG_W0);
	else if (size == 8)
		host_arm64_FMOV_D_Q(block, REG_V_TEMP, REG_X0);
	host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
	host_arm64_RET(block, REG_X30);
}

static void build_store_routine(codeblock_t *block, int size, int is_float)
{
        uint32_t *branch_offset;
        uint32_t *misaligned_offset;
	int offset;

        /*In - R0 = address, R1 = data
          Out - R1 = abrt*/
	/*MOV W2, W0, LSR #12
	  MOV X3, #writelookup2
	  LDR X2, [X3, X2, LSL #3]
	  CMP X2, #-1
	  BEQ +
	  STRB W1, [X2, X0]
	  MOV W1, #0
	  RET
	* STP X29, X30, [SP, #-16]
	  BL writemembl
	  LDRB R1, cpu_state.abrt
	  LDP X29, X30, [SP, #-16]
	  RET
	*/
	codegen_alloc(block, 80);
	host_arm64_MOV_REG_LSR(block, REG_W2, REG_W0, 12);
	host_arm64_MOVX_IMM(block, REG_X3, (uint64_t)writelookup2);
	host_arm64_LDRX_REG_LSL3(block, REG_X2, REG_X3, REG_X2);
	if (size != 1)
	{
		host_arm64_TST_IMM(block, REG_W0, size-1);
		misaligned_offset = host_arm64_BNE_(block);
	}
	host_arm64_CMPX_IMM(block, REG_X2, -1);
	branch_offset = host_arm64_BEQ_(block);
	if (size == 1 && !is_float)
		host_arm64_STRB_REG(block, REG_X1, REG_X2, REG_X0);
	else if (size == 2 && !is_float)
		host_arm64_STRH_REG(block, REG_X1, REG_X2, REG_X0);
	else if (size == 4 && !is_float)
		host_arm64_STR_REG(block, REG_X1, REG_X2, REG_X0);
	else if (size == 4 && is_float)
		host_arm64_STR_REG_F32(block, REG_V_TEMP, REG_X2, REG_X0);
	else if (size == 8)
		host_arm64_STR_REG_F64(block, REG_V_TEMP, REG_X2, REG_X0);
	host_arm64_MOVZ_IMM(block, REG_X1, 0);
	host_arm64_RET(block, REG_X30);

	host_arm64_branch_set_offset(branch_offset, &block_write_data[block_pos]);
	if (size != 1)
		host_arm64_branch_set_offset(misaligned_offset, &block_write_data[block_pos]);
	host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
	if (size == 4 && is_float)
		host_arm64_FMOV_W_S(block, REG_W1, REG_V_TEMP);
	else if (size == 8)
		host_arm64_FMOV_Q_D(block, REG_X1, REG_V_TEMP);
	if (size == 1)
		host_arm64_call(block, (void *)writemembl);
	else if (size == 2)
		host_arm64_call(block, (void *)writememwl);
	else if (size == 4)
		host_arm64_call(block, (void *)writememll);
	else if (size == 8)
		host_arm64_call(block, (void *)writememql);
	else
		fatal("build_store_routine - unknown size %i\n", size);
	codegen_direct_read_8(block, REG_W1, &cpu_state.abrt);
	host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
	host_arm64_RET(block, REG_X30);
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

static void build_fp_round_routine(codeblock_t *block, int is_quad)
{
	uint64_t *jump_table;

	codegen_alloc(block, 80);
	host_arm64_LDR_IMM_W(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.new_fp_control - (uintptr_t)&cpu_state);
	host_arm64_ADR(block, REG_TEMP2, 12);
	host_arm64_LDR_REG_X(block, REG_TEMP2, REG_TEMP2, REG_TEMP);
	host_arm64_BR(block, REG_TEMP2);

	jump_table = (uint64_t *)&block_write_data[block_pos];
	block_pos += 4*8;

	jump_table[X87_ROUNDING_NEAREST] = (uint64_t)(uintptr_t)&block_write_data[block_pos]; //tie even
	if (is_quad)
		host_arm64_FCVTNS_X_D(block, REG_TEMP, REG_V_TEMP);
	else
		host_arm64_FCVTNS_W_D(block, REG_TEMP, REG_V_TEMP);
	host_arm64_RET(block, REG_X30);

	jump_table[X87_ROUNDING_UP] = (uint64_t)(uintptr_t)&block_write_data[block_pos]; //pos inf
	if (is_quad)
		host_arm64_FCVTPS_X_D(block, REG_TEMP, REG_V_TEMP);
	else
		host_arm64_FCVTPS_W_D(block, REG_TEMP, REG_V_TEMP);
	host_arm64_RET(block, REG_X30);

	jump_table[X87_ROUNDING_DOWN] = (uint64_t)(uintptr_t)&block_write_data[block_pos]; //neg inf
	if (is_quad)
		host_arm64_FCVTMS_X_D(block, REG_TEMP, REG_V_TEMP);
	else
		host_arm64_FCVTMS_W_D(block, REG_TEMP, REG_V_TEMP);
	host_arm64_RET(block, REG_X30);

	jump_table[X87_ROUNDING_CHOP] = (uint64_t)(uintptr_t)&block_write_data[block_pos]; //zero
	if (is_quad)
		host_arm64_FCVTZS_X_D(block, REG_TEMP, REG_V_TEMP);
	else
		host_arm64_FCVTZS_W_D(block, REG_TEMP, REG_V_TEMP);
	host_arm64_RET(block, REG_X30);
}

void codegen_backend_init()
{
	codeblock_t *block;
        int c;
#if defined(__linux__) || defined(__APPLE__)
	void *start;
	size_t len;
	long pagesize = sysconf(_SC_PAGESIZE);
	long pagemask = ~(pagesize - 1);
#endif

	codeblock = malloc(BLOCK_SIZE * sizeof(codeblock_t));
        codeblock_hash = malloc(HASH_SIZE * sizeof(codeblock_t *));

        memset(codeblock, 0, BLOCK_SIZE * sizeof(codeblock_t));
        memset(codeblock_hash, 0, HASH_SIZE * sizeof(codeblock_t *));

        for (c = 0; c < BLOCK_SIZE; c++)
	{
                codeblock[c].pc = BLOCK_PC_INVALID;
	}

        block_current = 0;
        block_pos = 0;
        block = &codeblock[block_current];
        block->head_mem_block = codegen_allocator_allocate(NULL, block_current);
        block->data = codeblock_allocator_get_ptr(block->head_mem_block);
        block_write_data = block->data;
        build_loadstore_routines(block);

        codegen_fp_round = &block_write_data[block_pos];
	build_fp_round_routine(block, 0);
        codegen_fp_round_quad = &block_write_data[block_pos];
	build_fp_round_routine(block, 1);

	codegen_alloc(block, 80);
        codegen_gpf_rout = &block_write_data[block_pos];
	host_arm64_mov_imm(block, REG_ARG0, 0);
	host_arm64_mov_imm(block, REG_ARG1, 0);
	host_arm64_call(block, (void *)x86gpf);

        codegen_exit_rout = &block_write_data[block_pos];
	host_arm64_LDP_POSTIDX_X(block, REG_X19, REG_X20, REG_XSP, 64);
	host_arm64_LDP_POSTIDX_X(block, REG_X21, REG_X22, REG_XSP, 16);
	host_arm64_LDP_POSTIDX_X(block, REG_X23, REG_X24, REG_XSP, 16);
	host_arm64_LDP_POSTIDX_X(block, REG_X25, REG_X26, REG_XSP, 16);
	host_arm64_LDP_POSTIDX_X(block, REG_X27, REG_X28, REG_XSP, 16);
	host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
	host_arm64_RET(block, REG_X30);

        block_write_data = NULL;

	codegen_allocator_clean_blocks(block->head_mem_block);

	asm("mrs %0, fpcr\n"
                : "=r" (cpu_state.old_fp_control)
	);
}

void codegen_set_rounding_mode(int mode)
{
	if (mode < 0 || mode > 3)
		fatal("codegen_set_rounding_mode - invalid mode\n");
        cpu_state.new_fp_control = mode << 3;
}

/*R10 - cpu_state*/
void codegen_backend_prologue(codeblock_t *block)
{
        block_pos = BLOCK_START;

	/*Entry code*/

	host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
	host_arm64_STP_PREIDX_X(block, REG_X27, REG_X28, REG_XSP, -16);
	host_arm64_STP_PREIDX_X(block, REG_X25, REG_X26, REG_XSP, -16);
	host_arm64_STP_PREIDX_X(block, REG_X23, REG_X24, REG_XSP, -16);
	host_arm64_STP_PREIDX_X(block, REG_X21, REG_X22, REG_XSP, -16);
	host_arm64_STP_PREIDX_X(block, REG_X19, REG_X20, REG_XSP, -64);

	host_arm64_MOVX_IMM(block, REG_CPUSTATE, (uint64_t)&cpu_state);

        if (block->flags & CODEBLOCK_HAS_FPU)
        {
		host_arm64_LDR_IMM_W(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.TOP - (uintptr_t)&cpu_state);
		host_arm64_SUB_IMM(block, REG_TEMP, REG_TEMP, block->TOP);
		host_arm64_STR_IMM_W(block, REG_TEMP, REG_XSP, IREG_TOP_diff_stack_offset);
        }
}

void codegen_backend_epilogue(codeblock_t *block)
{
	host_arm64_LDP_POSTIDX_X(block, REG_X19, REG_X20, REG_XSP, 64);
	host_arm64_LDP_POSTIDX_X(block, REG_X21, REG_X22, REG_XSP, 16);
	host_arm64_LDP_POSTIDX_X(block, REG_X23, REG_X24, REG_XSP, 16);
	host_arm64_LDP_POSTIDX_X(block, REG_X25, REG_X26, REG_XSP, 16);
	host_arm64_LDP_POSTIDX_X(block, REG_X27, REG_X28, REG_XSP, 16);
	host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
	host_arm64_RET(block, REG_X30);

	codegen_allocator_clean_blocks(block->head_mem_block);
}

#endif

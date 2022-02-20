#if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "codegen.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_backend_x86_defs.h"
#include "codegen_backend_x86_ops.h"
#include "codegen_backend_x86_ops_sse.h"
#include "codegen_reg.h"
#include "x86.h"

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

void *codegen_gpf_rout;
void *codegen_exit_rout;

host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS] =
{
        /*Note: while EAX and EDX are normally volatile registers under x86
          calling conventions, the recompiler will explicitly save and restore
          them across funcion calls*/
        {REG_EAX, 0},
        {REG_EBX, 0},
        {REG_EDX, 0}
};

host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS] =
{
        {REG_XMM0, HOST_REG_FLAG_VOLATILE},
        {REG_XMM1, HOST_REG_FLAG_VOLATILE},
        {REG_XMM2, HOST_REG_FLAG_VOLATILE},
        {REG_XMM3, HOST_REG_FLAG_VOLATILE},
        {REG_XMM4, HOST_REG_FLAG_VOLATILE},
        {REG_XMM5, HOST_REG_FLAG_VOLATILE}
};

static void build_load_routine(codeblock_t *block, int size, int is_float)
{
        uint8_t *branch_offset;
        uint8_t *misaligned_offset = NULL;

        /*In - ESI = address
          Out - ECX = data, ESI = abrt*/
        /*MOV ECX, ESI
          SHR ESI, 12
          MOV ESI, [readlookup2+ESI*4]
          CMP ESI, -1
          JNZ +
          MOVZX ECX, B[ESI+ECX]
          XOR ESI,ESI
          RET
        * PUSH EAX
          PUSH EDX
          PUSH ECX
          CALL readmembl
          POP ECX
          POP EDX
          POP EAX
          MOVZX ECX, AL
          RET
        */
        host_x86_MOV32_REG_REG(block, REG_ECX, REG_ESI);
        host_x86_SHR32_IMM(block, REG_ESI, 12);
        host_x86_MOV32_REG_ABS_INDEX_SHIFT(block, REG_ESI, readlookup2, REG_ESI, 2);
        if (size != 1)
        {
                host_x86_TEST32_REG_IMM(block, REG_ECX, size-1);
                misaligned_offset = host_x86_JNZ_short(block);
        }
        host_x86_CMP32_REG_IMM(block, REG_ESI, (uint32_t)-1);
        branch_offset = host_x86_JZ_short(block);
        if (size == 1 && !is_float)
                host_x86_MOVZX_BASE_INDEX_32_8(block, REG_ECX, REG_ESI, REG_ECX);
        else if (size == 2 && !is_float)
                host_x86_MOVZX_BASE_INDEX_32_16(block, REG_ECX, REG_ESI, REG_ECX);
        else if (size == 4 && !is_float)
                host_x86_MOV32_REG_BASE_INDEX(block, REG_ECX, REG_ESI, REG_ECX);
        else if (size == 4 && is_float)
                host_x86_CVTSS2SD_XREG_BASE_INDEX(block, REG_XMM_TEMP, REG_ESI, REG_ECX);
        else if (size == 8)
                host_x86_MOVQ_XREG_BASE_INDEX(block, REG_XMM_TEMP, REG_ESI, REG_ECX);
        else
                fatal("build_load_routine: size=%i\n", size);
        host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
        host_x86_RET(block);

        *branch_offset = (uint8_t)((uintptr_t)&block_write_data[block_pos] - (uintptr_t)branch_offset) - 1;
        if (size != 1)
                *misaligned_offset = (uint8_t)((uintptr_t)&block_write_data[block_pos] - (uintptr_t)misaligned_offset) - 1;
        host_x86_PUSH(block, REG_EAX);
        host_x86_PUSH(block, REG_EDX);
        host_x86_PUSH(block, REG_ECX);
        if (size == 1)
                host_x86_CALL(block, (void *)readmembl);
        else if (size == 2)
                host_x86_CALL(block, (void *)readmemwl);
        else if (size == 4)
                host_x86_CALL(block, (void *)readmemll);
        else if (size == 8)
                host_x86_CALL(block, (void *)readmemql);
        host_x86_POP(block, REG_ECX);
        if (size == 1 && !is_float)
                host_x86_MOVZX_REG_32_8(block, REG_ECX, REG_EAX);
        else if (size == 2 && !is_float)
                host_x86_MOVZX_REG_32_16(block, REG_ECX, REG_EAX);
        else if (size == 4 && !is_float)
                host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
        else if (size == 4 && is_float)
        {
                host_x86_MOVD_XREG_REG(block, REG_XMM_TEMP, REG_EAX);
                host_x86_CVTSS2SD_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
        }
        else if (size == 8)
        {
                host_x86_MOVD_XREG_REG(block, REG_XMM_TEMP, REG_EAX);
                host_x86_MOVD_XREG_REG(block, REG_XMM_TEMP2, REG_EDX);
                host_x86_UNPCKLPS_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP2);
        }
        host_x86_POP(block, REG_EDX);
        host_x86_POP(block, REG_EAX);
        host_x86_MOVZX_REG_ABS_32_8(block, REG_ESI, &cpu_state.abrt);
        host_x86_RET(block);
        block_pos = (block_pos + 63) & ~63;
}

static void build_store_routine(codeblock_t *block, int size, int is_float)
{
        uint8_t *branch_offset;
        uint8_t *misaligned_offset = NULL;

        /*In - ECX = data, ESI = address
          Out - ESI = abrt
          Corrupts EDI*/
        /*MOV EDI, ESI
          SHR ESI, 12
          MOV ESI, [writelookup2+ESI*4]
          CMP ESI, -1
          JNZ +
          MOV [ESI+EDI], ECX
          XOR ESI,ESI
          RET
        * PUSH EAX
          PUSH EDX
          PUSH ECX
          CALL writemembl
          POP ECX
          POP EDX
          POP EAX
          MOVZX ECX, AL
          RET
        */
        host_x86_MOV32_REG_REG(block, REG_EDI, REG_ESI);
        host_x86_SHR32_IMM(block, REG_ESI, 12);
        host_x86_MOV32_REG_ABS_INDEX_SHIFT(block, REG_ESI, writelookup2, REG_ESI, 2);
        if (size != 1)
        {
                host_x86_TEST32_REG_IMM(block, REG_EDI, size-1);
                misaligned_offset = host_x86_JNZ_short(block);
        }
        host_x86_CMP32_REG_IMM(block, REG_ESI, (uint32_t)-1);
        branch_offset = host_x86_JZ_short(block);
        if (size == 1 && !is_float)
                host_x86_MOV8_BASE_INDEX_REG(block, REG_ESI, REG_EDI, REG_ECX);
        else if (size == 2 && !is_float)
                host_x86_MOV16_BASE_INDEX_REG(block, REG_ESI, REG_EDI, REG_ECX);
        else if (size == 4 && !is_float)
                host_x86_MOV32_BASE_INDEX_REG(block, REG_ESI, REG_EDI, REG_ECX);
        else if (size == 4 && is_float)
                host_x86_MOVD_BASE_INDEX_XREG(block, REG_ESI, REG_EDI, REG_XMM_TEMP);
        else if (size == 8)
                host_x86_MOVQ_BASE_INDEX_XREG(block, REG_ESI, REG_EDI, REG_XMM_TEMP);
        else
                fatal("build_store_routine: size=%i is_float=%i\n", size, is_float);
        host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
        host_x86_RET(block);

        *branch_offset = (uint8_t)((uintptr_t)&block_write_data[block_pos] - (uintptr_t)branch_offset) - 1;
        if (size != 1)
                *misaligned_offset = (uint8_t)((uintptr_t)&block_write_data[block_pos] - (uintptr_t)misaligned_offset) - 1;
        if (size == 4 && is_float)
                host_x86_MOVD_REG_XREG(block, REG_ECX, REG_XMM_TEMP);
        host_x86_PUSH(block, REG_EAX);
        host_x86_PUSH(block, REG_EDX);
        host_x86_PUSH(block, REG_ECX);
        if (size == 8)
        {
                host_x86_MOVQ_STACK_OFFSET_XREG(block, -8, REG_XMM_TEMP);
                host_x86_SUB32_REG_IMM(block, REG_ESP, 8);
        }
        host_x86_PUSH(block, REG_EDI);
        if (size == 1)
                host_x86_CALL(block, (void *)writemembl);
        else if (size == 2)
                host_x86_CALL(block, (void *)writememwl);
        else if (size == 4)
                host_x86_CALL(block, (void *)writememll);
        else if (size == 8)
                host_x86_CALL(block, (void *)writememql);
        host_x86_POP(block, REG_EDI);
        if (size == 8)
                host_x86_ADD32_REG_IMM(block, REG_ESP, 8);
        host_x86_POP(block, REG_ECX);
        host_x86_POP(block, REG_EDX);
        host_x86_POP(block, REG_EAX);
        host_x86_MOVZX_REG_ABS_32_8(block, REG_ESI, &cpu_state.abrt);
        host_x86_RET(block);
        block_pos = (block_pos + 63) & ~63;
}

static void build_loadstore_routines(codeblock_t *block)
{
        codegen_mem_load_byte = &codeblock[block_current].data[block_pos];
        build_load_routine(block, 1, 0);
        codegen_mem_load_word = &codeblock[block_current].data[block_pos];
        build_load_routine(block, 2, 0);
        codegen_mem_load_long = &codeblock[block_current].data[block_pos];
        build_load_routine(block, 4, 0);
        codegen_mem_load_quad = &codeblock[block_current].data[block_pos];
        build_load_routine(block, 8, 0);
        codegen_mem_load_single = &codeblock[block_current].data[block_pos];
        build_load_routine(block, 4, 1);
        codegen_mem_load_double = &codeblock[block_current].data[block_pos];
        build_load_routine(block, 8, 1);

        codegen_mem_store_byte = &codeblock[block_current].data[block_pos];
        build_store_routine(block, 1, 0);
        codegen_mem_store_word = &codeblock[block_current].data[block_pos];
        build_store_routine(block, 2, 0);
        codegen_mem_store_long = &codeblock[block_current].data[block_pos];
        build_store_routine(block, 4, 0);
        codegen_mem_store_quad = &codeblock[block_current].data[block_pos];
        build_store_routine(block, 8, 0);
        codegen_mem_store_single = &codeblock[block_current].data[block_pos];
        build_store_routine(block, 4, 1);
        codegen_mem_store_double = &codeblock[block_current].data[block_pos];
        build_store_routine(block, 8, 1);
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
                codeblock[c].pc = BLOCK_PC_INVALID;

        block_current = 0;
        block_pos = 0;
        block = &codeblock[block_current];
        block->head_mem_block = codegen_allocator_allocate(NULL, block_current);
        block->data = codeblock_allocator_get_ptr(block->head_mem_block);
        block_write_data = block->data;
        build_loadstore_routines(block);

        codegen_gpf_rout = &codeblock[block_current].data[block_pos];
        host_x86_MOV32_STACK_IMM(block, STACK_ARG0, 0);
        host_x86_MOV32_STACK_IMM(block, STACK_ARG1, 0);
        host_x86_CALL(block, (void *)x86gpf);
        codegen_exit_rout = &codeblock[block_current].data[block_pos];
        host_x86_ADD32_REG_IMM(block, REG_ESP, 64);
        host_x86_POP(block, REG_EDI);
        host_x86_POP(block, REG_ESI);
        host_x86_POP(block, REG_EBP);
        host_x86_POP(block, REG_EDX);
        host_x86_RET(block);
        block_write_data = NULL;

        cpu_state.old_fp_control = 0;
        asm(
                "fstcw %0\n"
                "stmxcsr %1\n"
                : "=m" (cpu_state.old_fp_control2),
                  "=m" (cpu_state.old_fp_control)
        );
        cpu_state.trunc_fp_control = cpu_state.old_fp_control | 0x6000;
}

void codegen_set_rounding_mode(int mode)
{
        /*SSE*/
        cpu_state.new_fp_control = (cpu_state.old_fp_control & ~0x6000) | (mode << 13);
        /*x87 - used for double -> i64 conversions*/
        cpu_state.new_fp_control2 = (cpu_state.old_fp_control2 & ~0x0c00) | (mode << 10);
}

void codegen_backend_prologue(codeblock_t *block)
{
        block_pos = BLOCK_START; /*Entry code*/
        host_x86_PUSH(block, REG_EBX);
        host_x86_PUSH(block, REG_EBP);
        host_x86_PUSH(block, REG_ESI);
        host_x86_PUSH(block, REG_EDI);
        host_x86_SUB32_REG_IMM(block, REG_ESP, 64);
        host_x86_MOV32_REG_IMM(block, REG_EBP, ((uintptr_t)&cpu_state) + 128);
        if (block->flags & CODEBLOCK_HAS_FPU)
        {
                host_x86_MOV32_REG_ABS(block, REG_EAX, &cpu_state.TOP);
                host_x86_SUB32_REG_IMM(block, REG_EAX, block->TOP);
                host_x86_MOV32_BASE_OFFSET_REG(block, REG_ESP, IREG_TOP_diff_stack_offset, REG_EAX);
        }
}

void codegen_backend_epilogue(codeblock_t *block)
{
        host_x86_ADD32_REG_IMM(block, REG_ESP, 64);
        host_x86_POP(block, REG_EDI);
        host_x86_POP(block, REG_ESI);
        host_x86_POP(block, REG_EBP);
        host_x86_POP(block, REG_EDX);
        host_x86_RET(block);
}

#endif

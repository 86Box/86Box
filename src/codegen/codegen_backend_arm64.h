#include "codegen_backend_arm64_defs.h"

/*
   Raised from 0x4000 (halves recompile thrash, measured on x86-64 - see
   codegen_backend_x86-64.h, raised further there to 0x10000). Kept lower
   here not for memory reasons - codeblock[] below is a plain data mmap
   (see codegen_backend_arm64.c), not executable, so it doesn't compete
   with ARM64's 128MB branch-range-limited code pool in codegen_allocator.c,
   so an untested value would ship to every user by default.
 */
#define BLOCK_SIZE  0x8000
#define BLOCK_MASK  0x7fff
#define BLOCK_START 0

#define HASH_SIZE   0x20000
#define HASH_MASK   0x1ffff

#define HASH(l)     ((l) &0x1ffff)

#define BLOCK_MAX   0x3c0

/* Let generic uop emitters use backend-specific immediate store helpers. */
#define CODEGEN_BACKEND_HAS_MOV_IMM
/* The integer uops of the SETcc, BSWAP, BSF/BSR, CMPXCHG and multiply
   handlers: CMP_SET, TEST_SET, BSF, BSR, BSWAP, IMUL, IMUL_HI, IMUL_OV and
   MUL_HI. A backend without them leaves those instructions to the
   interpreter. */
#define CODEGEN_BACKEND_HAS_INT_UOPS

/* Blocks jump straight to their successor when nothing between them needs
   the C loop (codegen_link.h). The option below adds work to every loop
   iteration that the link routine does not repeat; so do the debug registers
   on 486 and later processors, which turn linking off at run time
   (codegen_init(), codegen_reset()). */
#if !defined(USE_GDBSTUB)
#    define CODEGEN_BACKEND_HAS_BLOCK_LINK
void codegen_link_exit(codeblock_t *block);
/* 0: blocks end with the plain return and the loop never links them. */
extern int codegen_block_link;
#endif

void host_arm64_BLR(codeblock_t *block, int addr_reg);
void host_arm64_CBNZ(codeblock_t *block, int reg, uintptr_t dest);
void host_arm64_MOVK_IMM(codeblock_t *block, int reg, uint32_t imm_data);
void host_arm64_MOVZ_IMM(codeblock_t *block, int reg, uint32_t imm_data);
void host_arm64_LDP_POSTIDX_X(codeblock_t *block, int src_reg1, int src_reg2, int base_reg, int offset);
void host_arm64_LDR_LITERAL_W(codeblock_t *block, int dest_reg, int literal_offset);
void host_arm64_LDR_LITERAL_X(codeblock_t *block, int dest_reg, int literal_offset);
void host_arm64_NOP(codeblock_t *block);
void host_arm64_RET(codeblock_t *block, int reg);
void host_arm64_STP_PREIDX_X(codeblock_t *block, int src_reg1, int src_reg2, int base_reg, int offset);
void host_arm64_STR_IMM_W(codeblock_t *block, int dest_reg, int base_reg, int offset);
void host_arm64_STRB_IMM_W(codeblock_t *block, int dest_reg, int base_reg, int offset);

void host_arm64_call(codeblock_t *block, void *dst_addr);
void host_arm64_mov_imm(codeblock_t *block, int reg, uint32_t imm_data);

uint32_t host_arm64_find_imm(uint32_t data);

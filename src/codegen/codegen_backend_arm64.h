#include "codegen_backend_arm64_defs.h"

/*
   A Windows 2000 boot and 3DMark2001 overran 0x8000, evicting and
   recompiling thousands of blocks a second. codeblock[] below is a plain
   data mmap (see codegen_backend_arm64.c), not part of ARM64's 128MB
   branch-range-limited code pool in codegen_allocator.c, but every live
   block holds at least one pool chunk; at 0x10000 those runs kept the pool
   under two-thirds full. Not above 0x10000: the link routine compares block
   numbers as 16 bits (codegen_link_slot_t.block_nr).
 */
#define BLOCK_SIZE  0x10000
#define BLOCK_MASK  0xffff
#define BLOCK_START 0

#define HASH_SIZE   0x20000
#define HASH_MASK   0x1ffff

#define HASH(l)     ((l) &0x1ffff)

#define BLOCK_MAX   0x3e8

/* Let generic uop emitters use backend-specific immediate store helpers. */
#define CODEGEN_BACKEND_HAS_MOV_IMM
/* The integer uops of the SETcc, BSWAP, BSF/BSR, CMPXCHG and multiply
   handlers: CMP_SET, TEST_SET, BSF, BSR, BSWAP, IMUL, IMUL_HI, IMUL_OV and
   MUL_HI. A backend without them leaves those instructions to the
   interpreter. */
#define CODEGEN_BACKEND_HAS_INT_UOPS
/* FCOM_SIGNED_ZERO, the FCOMPP compare that tells -0 from +0. A backend
   without it leaves FCOMPP to the interpreter on a 287XL or later. */
#define CODEGEN_BACKEND_HAS_FCOM_SIGNED_ZERO
/* FCOM_CHKCOP, the FCOM, FCOMP and FCOMPP compare with x87_compare()'s
   CHKCOP case. A backend without it compiles them with FCOM (FCOMPP with
   FCOM_SIGNED_ZERO), which leave that case out. */
#define CODEGEN_BACKEND_HAS_FCOM_CHKCOP
/* The exits of the uops that can leave the block charge what imm_data2 adds
   to cycles on their exit path (codegen_exit_cycles). A backend without it
   leaves those cycles uncharged there, but for codegen_exit_on_one's, which
   it charges in the IR. */
#define CODEGEN_BACKEND_HAS_EXIT_CYCLES
/* CMN_IMM_JC and CMP_JNWITHIN_TOP, the SS limit tests of a push or pop on a
   flat or wide 32-bit stack with the carry of its end. A backend without
   them makes those tests with an add and a compare (CHECK_STACK_LIMITS). */
#define CODEGEN_BACKEND_HAS_STACK_LIMIT_UOPS
/* The accesses and checks whose only way out of the block is a fault
   (codegen_uop_is_lazy) leave the dirty integer registers and the pending
   immediates in place and store them on that way out only, in a stub after
   the block (codegen_lazy_stubs_emit). A backend without it writes them
   back before each of those uops. */
#define CODEGEN_BACKEND_HAS_LAZY_WRITEBACK
void codegen_lazy_stubs_emit(codeblock_t *block);

/* Blocks jump straight to their successor when nothing between them needs
   the C loop (codegen_link.h). The option below adds work to every loop
   iteration that the link routine does not repeat; so do the debug registers
   on 486 and later processors, which turn linking off at run time
   (codegen_init(), codegen_reset()). */
#if !defined(USE_GDBSTUB)
#    define CODEGEN_BACKEND_HAS_BLOCK_LINK
/* A slot whose target changes keeps the targets it held before in a set,
   tried after a key mismatch (codegen_link.h). */
#    define CODEGEN_BACKEND_HAS_LINK_SETS
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

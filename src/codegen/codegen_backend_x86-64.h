#include "codegen_backend_x86-64_defs.h"

/* Raised from 0x4000 - too small to hold both status variants (16/32-bit,
   flat/non-flat) of hot code at once, causing recompile thrash - and from
   0x10000, which Windows 2000's boot overran: it evicted and compiled again
   15-30 thousand blocks a second. Block indices are 32-bit. */
#define BLOCK_SIZE  0x20000
#define BLOCK_MASK  0x1ffff
#define BLOCK_START 0

#define HASH_SIZE   0x20000
#define HASH_MASK   0x1ffff

/* The block hash takes the physical address's bits above 128K too: with the
   low 17 alone, code at the same offset in different 128K regions (the
   kernel's and a driver's, two processes' copies) fought for one slot and
   went to the tree walk each time. */
#define HASH(l)     (((l) ^ ((l) >> 17)) & 0x1ffff)

#define BLOCK_MAX   0x3c0

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

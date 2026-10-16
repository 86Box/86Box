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

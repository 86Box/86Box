#ifndef _CODEGEN_BACKEND_LOONGARCH64_H_
#define _CODEGEN_BACKEND_LOONGARCH64_H_

#include "codegen_backend_loongarch64_defs.h"

/* Same as the original PCem v15 value. Kept lower than arm64's 0x8000 /
   x86-64's 0x10000 until the block footprint has been measured on real
   Loongson hardware (see codegen_backend_arm64.h for the reasoning). */
#define BLOCK_SIZE  0x4000
#define BLOCK_MASK  0x3fff
#define BLOCK_START 0

#define HASH_SIZE   0x20000
#define HASH_MASK   0x1ffff

#define HASH(l)     ((l) &0x1ffff)

#define BLOCK_MAX   0x3c0

/* Let generic uop emitters use backend-specific immediate store helpers. */
#define CODEGEN_BACKEND_HAS_MOV_IMM

void     host_loong64_mov_imm(codeblock_t *block, int reg, uint64_t imm_data);
uint32_t host_loong64_find_imm(uint32_t data);

#endif

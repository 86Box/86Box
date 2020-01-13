#include "codegen_backend_arm64_defs.h"

#define BLOCK_SIZE 0x4000
#define BLOCK_MASK 0x3fff
#define BLOCK_START 0

#define HASH_SIZE 0x20000
#define HASH_MASK 0x1ffff

#define HASH(l) ((l) & 0x1ffff)

#define BLOCK_MAX 0x3c0


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
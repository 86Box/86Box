#include "codegen_backend_arm_defs.h"

#define BLOCK_SIZE 0x4000
#define BLOCK_MASK 0x3fff
#define BLOCK_START 0

#define HASH_SIZE 0x20000
#define HASH_MASK 0x1ffff

#define HASH(l) ((l) & 0x1ffff)

#define BLOCK_MAX 0x3c0

void host_arm_ADD_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm);
void host_arm_LDMIA_WB(codeblock_t *block, int addr_reg, uint32_t reg_mask);
void host_arm_LDR_IMM(codeblock_t *block, int dst_reg, int addr_reg, int offset);
void host_arm_MOV_IMM(codeblock_t *block, int dst_reg, uint32_t imm);
void host_arm_STMDB_WB(codeblock_t *block, int addr_reg, uint32_t reg_mask);
void host_arm_SUB_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm);

void host_arm_call(codeblock_t *block, void *dst_addr);
void host_arm_nop(codeblock_t *block);

void codegen_alloc(codeblock_t *block, int size);
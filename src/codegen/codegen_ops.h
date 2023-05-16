#ifndef _CODEGEN_OPS_H_
#define _CODEGEN_OPS_H_

#include "codegen.h"

typedef uint32_t (*RecompOpFn)(uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, codeblock_t *block);

extern RecompOpFn recomp_opcodes[512];
extern RecompOpFn recomp_opcodes_0f[512];
extern RecompOpFn recomp_opcodes_d8[512];
extern RecompOpFn recomp_opcodes_d9[512];
extern RecompOpFn recomp_opcodes_da[512];
extern RecompOpFn recomp_opcodes_db[512];
extern RecompOpFn recomp_opcodes_dc[512];
extern RecompOpFn recomp_opcodes_dd[512];
extern RecompOpFn recomp_opcodes_de[512];
extern RecompOpFn recomp_opcodes_df[512];
extern RecompOpFn recomp_opcodes_REPE[512];
extern RecompOpFn recomp_opcodes_REPNE[512];
extern RecompOpFn recomp_opcodes_NULL[512];

#define REG_EAX 0
#define REG_ECX 1
#define REG_EDX 2
#define REG_EBX 3
#define REG_ESP 4
#define REG_EBP 5
#define REG_ESI 6
#define REG_EDI 7
#define REG_AX  0
#define REG_CX  1
#define REG_DX  2
#define REG_BX  3
#define REG_SP  4
#define REG_BP  5
#define REG_SI  6
#define REG_DI  7
#define REG_AL  0
#define REG_AH  4
#define REG_CL  1
#define REG_CH  5
#define REG_DL  2
#define REG_DH  6
#define REG_BL  3
#define REG_BH  7

#endif

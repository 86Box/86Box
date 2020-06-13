void host_x86_ADD8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data);
void host_x86_ADD16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data);
void host_x86_ADD32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data);
void host_x86_ADD64_REG_IMM(codeblock_t *block, int dst_reg, uint64_t imm_data);

void host_x86_ADD8_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_ADD16_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_ADD32_REG_REG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_AND8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data);
void host_x86_AND16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data);
void host_x86_AND32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data);

void host_x86_AND8_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_AND16_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_AND32_REG_REG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_CALL(codeblock_t *block, void *p);

void host_x86_CMP16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data);
void host_x86_CMP32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data);
void host_x86_CMP64_REG_IMM(codeblock_t *block, int dst_reg, uint64_t imm_data);

void host_x86_CMP8_REG_REG(codeblock_t *block, int src_reg_a, int src_reg_b);
void host_x86_CMP16_REG_REG(codeblock_t *block, int src_reg_a, int src_reg_b);
void host_x86_CMP32_REG_REG(codeblock_t *block, int src_reg_a, int src_reg_b);

void host_x86_JMP(codeblock_t *block, void *p);

void host_x86_JNZ(codeblock_t *block, void *p);
void host_x86_JZ(codeblock_t *block, void *p);

uint8_t *host_x86_JNZ_short(codeblock_t *block);
uint8_t *host_x86_JS_short(codeblock_t *block);
uint8_t *host_x86_JZ_short(codeblock_t *block);

uint32_t *host_x86_JNB_long(codeblock_t *block);
uint32_t *host_x86_JNBE_long(codeblock_t *block);
uint32_t *host_x86_JNL_long(codeblock_t *block);
uint32_t *host_x86_JNLE_long(codeblock_t *block);
uint32_t *host_x86_JNO_long(codeblock_t *block);
uint32_t *host_x86_JNS_long(codeblock_t *block);
uint32_t *host_x86_JNZ_long(codeblock_t *block);
uint32_t *host_x86_JB_long(codeblock_t *block);
uint32_t *host_x86_JBE_long(codeblock_t *block);
uint32_t *host_x86_JL_long(codeblock_t *block);
uint32_t *host_x86_JLE_long(codeblock_t *block);
uint32_t *host_x86_JO_long(codeblock_t *block);
uint32_t *host_x86_JS_long(codeblock_t *block);
uint32_t *host_x86_JZ_long(codeblock_t *block);

void host_x86_LAHF(codeblock_t *block);

void host_x86_LEA_REG_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t offset);
void host_x86_LEA_REG_REG(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b);
void host_x86_LEA_REG_REG_SHIFT(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b, int shift);

void host_x86_MOV8_ABS_IMM(codeblock_t *block, void *p, uint32_t imm_data);
void host_x86_MOV32_ABS_IMM(codeblock_t *block, void *p, uint32_t imm_data);

void host_x86_MOV8_ABS_REG(codeblock_t *block, void *p, int src_reg);
void host_x86_MOV16_ABS_REG(codeblock_t *block, void *p, int src_reg);
void host_x86_MOV32_ABS_REG(codeblock_t *block, void *p, int src_reg);
void host_x86_MOV64_ABS_REG(codeblock_t *block, void *p, int src_reg);

void host_x86_MOV8_ABS_REG_REG_SHIFT_REG(codeblock_t *block, uint32_t addr, int base_reg, int index_reg, int shift, int src_reg);

void host_x86_MOV8_BASE_INDEX_REG(codeblock_t *block, int dst_reg, int base_reg, int index_reg);
void host_x86_MOV16_BASE_INDEX_REG(codeblock_t *block, int dst_reg, int base_reg, int index_reg);
void host_x86_MOV32_BASE_INDEX_REG(codeblock_t *block, int dst_reg, int base_reg, int index_reg);

void host_x86_MOV32_BASE_OFFSET_REG(codeblock_t *block, int base_reg, int offset, int src_reg);
void host_x86_MOV64_BASE_OFFSET_REG(codeblock_t *block, int base_reg, int offset, int src_reg);

void host_x86_MOV8_REG_ABS(codeblock_t *block, int dst_reg, void *p);
void host_x86_MOV16_REG_ABS(codeblock_t *block, int dst_reg, void *p);
void host_x86_MOV32_REG_ABS(codeblock_t *block, int dst_reg, void *p);
void host_x86_MOV64_REG_ABS(codeblock_t *block, int dst_reg, void *p);

void host_x86_MOV8_REG_ABS_REG_REG_SHIFT(codeblock_t *block, int dst_reg, uint32_t addr, int base_reg, int index_reg, int shift);

void host_x86_MOV32_REG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int index_reg);

void host_x86_MOV64_REG_BASE_INDEX_SHIFT(codeblock_t *block, int dst_reg, int base_reg, int index_reg, int scale);

void host_x86_MOV16_REG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset);
void host_x86_MOV32_REG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset);
void host_x86_MOV64_REG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset);

void host_x86_MOV8_REG_IMM(codeblock_t *block, int reg, uint16_t imm_data);
void host_x86_MOV16_REG_IMM(codeblock_t *block, int reg, uint16_t imm_data);
void host_x86_MOV32_REG_IMM(codeblock_t *block, int reg, uint32_t imm_data);

void host_x86_MOV64_REG_IMM(codeblock_t *block, int reg, uint64_t imm_data);

void host_x86_MOV8_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_MOV16_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_MOV32_REG_REG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_MOV32_STACK_IMM(codeblock_t *block, int32_t offset, uint32_t imm_data);

void host_x86_MOVSX_REG_16_8(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_MOVSX_REG_32_8(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_MOVSX_REG_32_16(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_MOVZX_BASE_INDEX_32_8(codeblock_t *block, int dst_reg, int base_reg, int index_reg);
void host_x86_MOVZX_BASE_INDEX_32_16(codeblock_t *block, int dst_reg, int base_reg, int index_reg);

void host_x86_MOVZX_REG_16_8(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_MOVZX_REG_32_8(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_MOVZX_REG_32_16(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_MOVZX_REG_ABS_16_8(codeblock_t *block, int dst_reg, void *p);
void host_x86_MOVZX_REG_ABS_32_8(codeblock_t *block, int dst_reg, void *p);
void host_x86_MOVZX_REG_ABS_32_16(codeblock_t *block, int dst_reg, void *p);

void host_x86_NOP(codeblock_t *block);

void host_x86_OR8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data);
void host_x86_OR16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data);
void host_x86_OR32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data);

void host_x86_OR8_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_OR16_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_OR32_REG_REG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_POP(codeblock_t *block, int src_reg);

void host_x86_PUSH(codeblock_t *block, int src_reg);

void host_x86_RET(codeblock_t *block);

void host_x86_ROL8_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_ROL16_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_ROL32_IMM(codeblock_t *block, int dst_reg, int shift);

void host_x86_ROL8_CL(codeblock_t *block, int dst_reg);
void host_x86_ROL16_CL(codeblock_t *block, int dst_reg);
void host_x86_ROL32_CL(codeblock_t *block, int dst_reg);

void host_x86_ROR8_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_ROR16_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_ROR32_IMM(codeblock_t *block, int dst_reg, int shift);

void host_x86_ROR8_CL(codeblock_t *block, int dst_reg);
void host_x86_ROR16_CL(codeblock_t *block, int dst_reg);
void host_x86_ROR32_CL(codeblock_t *block, int dst_reg);

void host_x86_SAR8_CL(codeblock_t *block, int dst_reg);
void host_x86_SAR16_CL(codeblock_t *block, int dst_reg);
void host_x86_SAR32_CL(codeblock_t *block, int dst_reg);

void host_x86_SAR8_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_SAR16_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_SAR32_IMM(codeblock_t *block, int dst_reg, int shift);

void host_x86_SHL8_CL(codeblock_t *block, int dst_reg);
void host_x86_SHL16_CL(codeblock_t *block, int dst_reg);
void host_x86_SHL32_CL(codeblock_t *block, int dst_reg);

void host_x86_SHL8_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_SHL16_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_SHL32_IMM(codeblock_t *block, int dst_reg, int shift);

void host_x86_SHR8_CL(codeblock_t *block, int dst_reg);
void host_x86_SHR16_CL(codeblock_t *block, int dst_reg);
void host_x86_SHR32_CL(codeblock_t *block, int dst_reg);

void host_x86_SHR8_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_SHR16_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_SHR32_IMM(codeblock_t *block, int dst_reg, int shift);

void host_x86_SUB8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data);
void host_x86_SUB16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data);
void host_x86_SUB32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data);
void host_x86_SUB64_REG_IMM(codeblock_t *block, int dst_reg, uint64_t imm_data);

void host_x86_SUB8_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_SUB16_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_SUB32_REG_REG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_TEST8_REG(codeblock_t *block, int src_host_reg, int dst_host_reg);
void host_x86_TEST16_REG(codeblock_t *block, int src_host_reg, int dst_host_reg);
void host_x86_TEST32_REG(codeblock_t *block, int src_reg, int dst_reg);
void host_x86_TEST32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data);

void host_x86_XOR8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data);
void host_x86_XOR16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data);
void host_x86_XOR32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data);

void host_x86_XOR8_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_XOR16_REG_REG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_XOR32_REG_REG(codeblock_t *block, int dst_reg, int src_reg);

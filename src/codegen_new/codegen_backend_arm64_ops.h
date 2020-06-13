void host_arm64_ADD_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data);
void host_arm64_ADD_REG(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift);
void host_arm64_ADD_REG_LSR(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift);
void host_arm64_ADD_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_ADD_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_ADD_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_ADDX_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint64_t imm_data);

void host_arm64_ADDP_V4S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_ADR(codeblock_t *block, int dst_reg, int offset);

void host_arm64_AND_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data);
void host_arm64_AND_REG(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift);
void host_arm64_AND_REG_ASR(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift);
void host_arm64_AND_REG_ROR(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift);
void host_arm64_AND_REG_V(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_ANDS_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data);

void host_arm64_ASR(codeblock_t *block, int dst_reg, int src_n_reg, int shift_reg);

void host_arm64_B(codeblock_t *block, void *dest);

void host_arm64_BFI(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width);

void host_arm64_BLR(codeblock_t *block, int addr_reg);

void host_arm64_BEQ(codeblock_t *block, void *dest);

uint32_t *host_arm64_BCC_(codeblock_t *block);
uint32_t *host_arm64_BCS_(codeblock_t *block);
uint32_t *host_arm64_BEQ_(codeblock_t *block);
uint32_t *host_arm64_BGE_(codeblock_t *block);
uint32_t *host_arm64_BGT_(codeblock_t *block);
uint32_t *host_arm64_BHI_(codeblock_t *block);
uint32_t *host_arm64_BLE_(codeblock_t *block);
uint32_t *host_arm64_BLS_(codeblock_t *block);
uint32_t *host_arm64_BLT_(codeblock_t *block);
uint32_t *host_arm64_BMI_(codeblock_t *block);
uint32_t *host_arm64_BNE_(codeblock_t *block);
uint32_t *host_arm64_BPL_(codeblock_t *block);
uint32_t *host_arm64_BVC_(codeblock_t *block);
uint32_t *host_arm64_BVS_(codeblock_t *block);

void host_arm64_branch_set_offset(uint32_t *opcode, void *dest);

void host_arm64_BR(codeblock_t *block, int addr_reg);

void host_arm64_BIC_REG_V(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_CBNZ(codeblock_t *block, int reg, uintptr_t dest);

void host_arm64_CMEQ_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_CMEQ_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_CMEQ_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_CMGT_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_CMGT_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_CMGT_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_CMN_IMM(codeblock_t *block, int src_n_reg, uint32_t imm_data);
void host_arm64_CMNX_IMM(codeblock_t *block, int src_n_reg, uint64_t imm_data);

void host_arm64_CMP_IMM(codeblock_t *block, int src_n_reg, uint32_t imm_data);
void host_arm64_CMPX_IMM(codeblock_t *block, int src_n_reg, uint64_t imm_data);

#define host_arm64_CMP_REG(block, src_n_reg, src_m_reg) host_arm64_CMP_REG_LSL(block, src_n_reg, src_m_reg, 0)
void host_arm64_CMP_REG_LSL(codeblock_t *block, int src_n_reg, int src_m_reg, int shift);

void host_arm64_CSEL_CC(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_CSEL_EQ(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_CSEL_VS(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_DUP_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int element);

void host_arm64_EOR_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data);
void host_arm64_EOR_REG(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift);
void host_arm64_EOR_REG_V(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_FABS_D(codeblock_t *block, int dst_reg, int src_reg);

void host_arm64_FADD_D(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FADD_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FCMEQ_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FCMGE_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FCMGT_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FCMP_D(codeblock_t *block, int src_n_reg, int src_m_reg);
void host_arm64_FDIV_D(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FDIV_S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FMAX_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FMIN_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FMUL_D(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FMUL_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FSUB_D(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_FSUB_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_FCVT_D_S(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FCVT_S_D(codeblock_t *block, int dst_reg, int src_reg);

void host_arm64_FCVTMS_W_D(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FCVTMS_X_D(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FCVTNS_W_D(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FCVTNS_X_D(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FCVTPS_W_D(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FCVTPS_X_D(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FCVTZS_W_D(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FCVTZS_X_D(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FCVTZS_V2S(codeblock_t *block, int dst_reg, int src_reg);

void host_arm64_FMOV_D_D(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FMOV_D_Q(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FMOV_Q_D(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FMOV_S_W(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FMOV_W_S(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FMOV_S_ONE(codeblock_t *block, int dst_reg);

void host_arm64_FNEG_D(codeblock_t *block, int dst_reg, int src_reg);

void host_arm64_FRINTX_D(codeblock_t *block, int dst_reg, int src_reg);

void host_arm64_FSQRT_D(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_FSQRT_S(codeblock_t *block, int dst_reg, int src_reg);

void host_arm64_LDP_POSTIDX_X(codeblock_t *block, int src_reg1, int src_reg2, int base_reg, int offset);

void host_arm64_LDR_IMM_W(codeblock_t *block, int dest_reg, int base_reg, int offset);
void host_arm64_LDR_IMM_X(codeblock_t *block, int dest_reg, int base_reg, int offset);
void host_arm64_LDR_REG(codeblock_t *block, int dest_reg, int base_reg, int offset_reg);
void host_arm64_LDR_REG_X(codeblock_t *block, int dest_reg, int base_reg, int offset_reg);

void host_arm64_LDR_REG_F32(codeblock_t *block, int dest_reg, int base_reg, int offset_reg);
void host_arm64_LDR_IMM_F64(codeblock_t *block, int dest_reg, int base_reg, int offset);
void host_arm64_LDR_REG_F64(codeblock_t *block, int dest_reg, int base_reg, int offset_reg);
void host_arm64_LDR_REG_F64_S(codeblock_t *block, int dest_reg, int base_reg, int offset_reg);

void host_arm64_LDRB_IMM_W(codeblock_t *block, int dest_reg, int base_reg, int offset);
void host_arm64_LDRB_REG(codeblock_t *block, int dest_reg, int base_reg, int offset_reg);

void host_arm64_LDRH_IMM(codeblock_t *block, int dest_reg, int base_reg, int offset);
void host_arm64_LDRH_REG(codeblock_t *block, int dest_reg, int base_reg, int offset_reg);

void host_arm64_LDRX_REG_LSL3(codeblock_t *block, int dest_reg, int base_reg, int offset_reg);

void host_arm64_LSL(codeblock_t *block, int dst_reg, int src_n_reg, int shift_reg);
void host_arm64_LSR(codeblock_t *block, int dst_reg, int src_n_reg, int shift_reg);

void host_arm64_MOV_REG_ASR(codeblock_t *block, int dst_reg, int src_m_reg, int shift);
void host_arm64_MOV_REG(codeblock_t *block, int dst_reg, int src_m_reg, int shift);
void host_arm64_MOV_REG_LSR(codeblock_t *block, int dst_reg, int src_m_reg, int shift);
void host_arm64_MOV_REG_ROR(codeblock_t *block, int dst_reg, int src_m_reg, int shift);

void host_arm64_MOVX_IMM(codeblock_t *block, int reg, uint64_t imm_data);
void host_arm64_MOVX_REG(codeblock_t *block, int dst_reg, int src_m_reg, int shift);

void host_arm64_MOVZ_IMM(codeblock_t *block, int reg, uint32_t imm_data);
void host_arm64_MOVK_IMM(codeblock_t *block, int reg, uint32_t imm_data);

void host_arm64_MSR_FPCR(codeblock_t *block, int src_reg);

void host_arm64_MUL_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_NOP(codeblock_t *block);

void host_arm64_ORR_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data);
void host_arm64_ORR_REG(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift);
void host_arm64_ORR_REG_V(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_RET(codeblock_t *block, int reg);

void host_arm64_ROR(codeblock_t *block, int dst_reg, int src_n_reg, int shift_reg);

void host_arm64_SADDLP_V2S_4H(codeblock_t *block, int dst_reg, int src_n_reg);

void host_arm64_SBFX(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width);

void host_arm64_SCVTF_D_Q(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_SCVTF_D_W(codeblock_t *block, int dst_reg, int src_reg);

void host_arm64_SCVTF_V2S(codeblock_t *block, int dst_reg, int src_reg);

void host_arm64_SQADD_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_SQADD_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_SQSUB_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_SQSUB_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_SQXTN_V8B_8H(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_SQXTN_V4H_4S(codeblock_t *block, int dst_reg, int src_reg);

void host_arm64_SHL_V4H(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_arm64_SHL_V2S(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_arm64_SHL_V2D(codeblock_t *block, int dst_reg, int src_reg, int shift);

void host_arm64_SHRN_V4H_4S(codeblock_t *block, int dst_reg, int src_n_reg, int shift);

void host_arm64_SMULL_V4S_4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_SSHR_V4H(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_arm64_SSHR_V2S(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_arm64_SSHR_V2D(codeblock_t *block, int dst_reg, int src_reg, int shift);

void host_arm64_STP_PREIDX_X(codeblock_t *block, int src_reg1, int src_reg2, int base_reg, int offset);

void host_arm64_STR_IMM_W(codeblock_t *block, int dest_reg, int base_reg, int offset);
void host_arm64_STR_IMM_Q(codeblock_t *block, int dest_reg, int base_reg, int offset);
void host_arm64_STR_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg);

void host_arm64_STR_REG_F32(codeblock_t *block, int src_reg, int base_reg, int offset_reg);
void host_arm64_STR_IMM_F64(codeblock_t *block, int src_reg, int base_reg, int offset);
void host_arm64_STR_REG_F64(codeblock_t *block, int src_reg, int base_reg, int offset_reg);
void host_arm64_STR_REG_F64_S(codeblock_t *block, int src_reg, int base_reg, int offset_reg);

void host_arm64_STRB_IMM(codeblock_t *block, int dest_reg, int base_reg, int offset);
void host_arm64_STRB_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg);

void host_arm64_STRH_IMM(codeblock_t *block, int dest_reg, int base_reg, int offset);
void host_arm64_STRH_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg);

void host_arm64_SUB_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data);
void host_arm64_SUB_REG(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift);
void host_arm64_SUB_REG_LSR(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift);
void host_arm64_SUB_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_SUB_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_SUB_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

uint32_t *host_arm64_TBNZ(codeblock_t *block, int reg, int bit);

#define host_arm64_TST_IMM(block, src_n_reg, imm_data) host_arm64_ANDS_IMM(block, REG_XZR, src_n_reg, imm_data)

void host_arm64_UBFX(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width);

void host_arm64_UQADD_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_UQADD_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_UQSUB_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_UQSUB_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_UQXTN_V8B_8H(codeblock_t *block, int dst_reg, int src_reg);
void host_arm64_UQXTN_V4H_4S(codeblock_t *block, int dst_reg, int src_reg);

void host_arm64_USHR_V4H(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_arm64_USHR_V2S(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_arm64_USHR_V2D(codeblock_t *block, int dst_reg, int src_reg, int shift);

void host_arm64_ZIP1_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_ZIP1_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_ZIP1_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_ZIP2_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_ZIP2_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);
void host_arm64_ZIP2_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg);

void host_arm64_call(codeblock_t *block, void *dst_addr);
void host_arm64_jump(codeblock_t *block, uintptr_t dst_addr);
void host_arm64_mov_imm(codeblock_t *block, int reg, uint32_t imm_data);


#define in_range7_x(offset) (((offset) >= -0x200) && ((offset) < (0x200)) && !((offset) & 7))
#define in_range12_b(offset) (((offset) >= 0) && ((offset) < 0x1000))
#define in_range12_h(offset) (((offset) >= 0) && ((offset) < 0x2000) && !((offset) & 1))
#define in_range12_w(offset) (((offset) >= 0) && ((offset) < 0x4000) && !((offset) & 3))
#define in_range12_q(offset) (((offset) >= 0) && ((offset) < 0x8000) && !((offset) & 7))


void codegen_direct_read_8(codeblock_t *block, int host_reg, void *p);

void codegen_alloc(codeblock_t *block, int size);
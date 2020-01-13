#define COND_SHIFT 28
#define COND_EQ (0x0 << COND_SHIFT)
#define COND_NE (0x1 << COND_SHIFT)
#define COND_CS (0x2 << COND_SHIFT)
#define COND_CC (0x3 << COND_SHIFT)
#define COND_MI (0x4 << COND_SHIFT)
#define COND_PL (0x5 << COND_SHIFT)
#define COND_VS (0x6 << COND_SHIFT)
#define COND_VC (0x7 << COND_SHIFT)
#define COND_HI (0x8 << COND_SHIFT)
#define COND_LS (0x9 << COND_SHIFT)
#define COND_GE (0xa << COND_SHIFT)
#define COND_LT (0xb << COND_SHIFT)
#define COND_GT (0xc << COND_SHIFT)
#define COND_LE (0xd << COND_SHIFT)
#define COND_AL (0xe << COND_SHIFT)

void host_arm_ADD_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm);
#define host_arm_ADD_REG(block, dst_reg, src_reg_n, src_reg_m) host_arm_ADD_REG_LSL(block, dst_reg, src_reg_n, src_reg_m, 0)
void host_arm_ADD_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);
void host_arm_ADD_REG_LSR(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);

void host_arm_AND_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm);
void host_arm_AND_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);
void host_arm_AND_REG_LSR(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);

void host_arm_B(codeblock_t *block, uintptr_t dest_addr);

void host_arm_BFI(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width);

void host_arm_BIC_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm);
void host_arm_BIC_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);
void host_arm_BIC_REG_LSR(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);

void host_arm_BL(codeblock_t *block, uintptr_t dest_addr);
void host_arm_BL_r1(codeblock_t *block, uintptr_t dest_addr);
void host_arm_BLX(codeblock_t *block, int addr_reg);

uint32_t *host_arm_BCC_(codeblock_t *block);
uint32_t *host_arm_BCS_(codeblock_t *block);
uint32_t *host_arm_BEQ_(codeblock_t *block);
uint32_t *host_arm_BGE_(codeblock_t *block);
uint32_t *host_arm_BGT_(codeblock_t *block);
uint32_t *host_arm_BHI_(codeblock_t *block);
uint32_t *host_arm_BLE_(codeblock_t *block);
uint32_t *host_arm_BLS_(codeblock_t *block);
uint32_t *host_arm_BLT_(codeblock_t *block);
uint32_t *host_arm_BMI_(codeblock_t *block);
uint32_t *host_arm_BNE_(codeblock_t *block);
uint32_t *host_arm_BPL_(codeblock_t *block);
uint32_t *host_arm_BVC_(codeblock_t *block);
uint32_t *host_arm_BVS_(codeblock_t *block);

void host_arm_BEQ(codeblock_t *block, uintptr_t dest_addr);
void host_arm_BNE(codeblock_t *block, uintptr_t dest_addr);

void host_arm_BX(codeblock_t *block, int addr_reg);

void host_arm_CMN_IMM(codeblock_t *block, int src_reg, uint32_t imm);
void host_arm_CMN_REG_LSL(codeblock_t *block, int src_reg_n, int src_reg_m, int shift);

void host_arm_CMP_IMM(codeblock_t *block, int src_reg, uint32_t imm);
#define host_arm_CMP_REG(block, src_reg_n, src_reg_m) host_arm_CMP_REG_LSL(block, src_reg_n, src_reg_m, 0)
void host_arm_CMP_REG_LSL(codeblock_t *block, int src_reg_n, int src_reg_m, int shift);

void host_arm_EOR_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm);
void host_arm_EOR_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);

void host_arm_LDMIA_WB(codeblock_t *block, int addr_reg, uint32_t reg_mask);

void host_arm_LDR_IMM(codeblock_t *block, int dst_reg, int addr_reg, int offset);
void host_arm_LDR_IMM_POST(codeblock_t *block, int dst_reg, int addr_reg, int offset);
#define host_arm_LDR_REG(block, dst_reg, addr_reg, offset_reg) host_arm_LDR_REG_LSL(block, dst_reg, addr_reg, offset_reg, 0)
void host_arm_LDR_REG_LSL(codeblock_t *block, int dst_reg, int addr_reg, int offset_reg, int shift);

void host_arm_LDRB_ABS(codeblock_t *block, int dst, void *p);
void host_arm_LDRB_IMM(codeblock_t *block, int dst_reg, int addr_reg, int offset);
#define host_arm_LDRB_REG(block, dst_reg, addr_reg, offset_reg) host_arm_LDRB_REG_LSL(block, dst_reg, addr_reg, offset_reg, 0)
void host_arm_LDRB_REG_LSL(codeblock_t *block, int dst_reg, int addr_reg, int offset_reg, int shift);

void host_arm_LDRH_IMM(codeblock_t *block, int dst_reg, int addr_reg, int offset);
void host_arm_LDRH_REG(codeblock_t *block, int dst_reg, int addr_reg, int offset_reg);

void host_arm_MOV_IMM(codeblock_t *block, int dst_reg, uint32_t imm);
#define host_arm_MOV_REG(block, dst_reg, src_reg) host_arm_MOV_REG_LSL(block, dst_reg, src_reg, 0)
void host_arm_MOV_REG_ASR(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_arm_MOV_REG_ASR_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg);
void host_arm_MOV_REG_LSL(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_arm_MOV_REG_LSL_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg);
void host_arm_MOV_REG_LSR(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_arm_MOV_REG_LSR_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg);
void host_arm_MOV_REG_ROR(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_arm_MOV_REG_ROR_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg);
void host_arm_MOVT_IMM(codeblock_t *block, int dst_reg, uint16_t imm);
void host_arm_MOVW_IMM(codeblock_t *block, int dst_reg, uint16_t imm);

void host_arm_MVN_REG_LSL(codeblock_t *block, int dst_reg, int src_reg, int shift);

#define host_arm_NOP(block) host_arm_MOV_REG(block, REG_R0, REG_R0)

void host_arm_ORR_IMM_cond(codeblock_t *block, uint32_t cond, int dst_reg, int src_reg, uint32_t imm);
void host_arm_ORR_REG_LSL_cond(codeblock_t *block, uint32_t cond, int dst_reg, int src_reg_n, int src_reg_m, int shift);

#define host_arm_ORR_IMM(block, dst_reg, src_reg, imm) host_arm_ORR_IMM_cond(block, COND_AL, dst_reg, src_reg, imm)
#define host_arm_ORR_REG_LSL(block, dst_reg, src_reg_a, src_reg_b, shift) host_arm_ORR_REG_LSL_cond(block, COND_AL, dst_reg, src_reg_a, src_reg_b, shift)

#define host_arm_ORRCC_IMM(block, dst_reg, src_reg, imm) host_arm_ORR_IMM_cond(block, COND_CC, dst_reg, src_reg, imm)
#define host_arm_ORREQ_IMM(block, dst_reg, src_reg, imm) host_arm_ORR_IMM_cond(block, COND_EQ, dst_reg, src_reg, imm)
#define host_arm_ORRVS_IMM(block, dst_reg, src_reg, imm) host_arm_ORR_IMM_cond(block, COND_VS, dst_reg, src_reg, imm)

void host_arm_RSB_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm);
void host_arm_RSB_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);
void host_arm_RSB_REG_LSR(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);

void host_arm_STMDB_WB(codeblock_t *block, int addr_reg, uint32_t reg_mask);

void host_arm_STR_IMM(codeblock_t *block, int src_reg, int addr_reg, int offset);
void host_arm_STR_IMM_WB(codeblock_t *block, int src_reg, int addr_reg, int offset);
#define host_arm_STR_REG(block, src_reg, addr_reg, offset_reg) host_arm_STR_REG_LSL(block, src_reg, addr_reg, offset_reg, 0)
void host_arm_STR_REG_LSL(codeblock_t *block, int src_reg, int addr_reg, int offset_reg, int shift);

void host_arm_STRB_IMM(codeblock_t *block, int src_reg, int addr_reg, int offset);
#define host_arm_STRB_REG(block, src_reg, addr_reg, offset_reg) host_arm_STRB_REG_LSL(block, src_reg, addr_reg, offset_reg, 0)
void host_arm_STRB_REG_LSL(codeblock_t *block, int src_reg, int addr_reg, int offset_reg, int shift);

void host_arm_STRH_IMM(codeblock_t *block, int src_reg, int addr_reg, int offset);
void host_arm_STRH_REG(codeblock_t *block, int src_reg, int addr_reg, int offset_reg);

void host_arm_SUB_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm);
void host_arm_SUB_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);
void host_arm_SUB_REG_LSR(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);

void host_arm_SXTB(codeblock_t *block, int dst_reg, int src_reg, int rotate);
void host_arm_SXTH(codeblock_t *block, int dst_reg, int src_reg, int rotate);

void host_arm_TST_IMM(codeblock_t *block, int src_reg1, uint32_t imm);
void host_arm_TST_REG(codeblock_t *block, int src_reg1, int src_reg2);

void host_arm_UADD8(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b);
void host_arm_UADD16(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b);

void host_arm_USUB8(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b);
void host_arm_USUB16(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b);

void host_arm_UXTB(codeblock_t *block, int dst_reg, int src_reg, int rotate);
void host_arm_UXTH(codeblock_t *block, int dst_reg, int src_reg, int rotate);

void host_arm_VABS_D(codeblock_t *block, int dest_reg, int src_reg);

void host_arm_VADD_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VADD_I8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VADD_I16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VADD_I32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VADD_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VAND_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VBIC_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VCMP_D(codeblock_t *block, int src_reg_d, int src_reg_m);

void host_arm_VCEQ_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VCEQ_I8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VCEQ_I16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VCEQ_I32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VCGE_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VCGT_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VCGT_S8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VCGT_S16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VCGT_S32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);

void host_arm_VCHS_D(codeblock_t *block, int dest_reg, int src_reg);

void host_arm_VCVT_D_IS(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VCVT_D_S(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VCVT_F32_S32(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VCVT_IS_D(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VCVT_S32_F32(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VCVT_S_D(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VCVTR_IS_D(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VDIV_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VDIV_S(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VDUP_32(codeblock_t *block, int dst_reg, int src_reg_m, int imm);
void host_arm_VEOR_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VLDR_D(codeblock_t *block, int dest_reg, int base_reg, int offset);
void host_arm_VLDR_S(codeblock_t *block, int dest_reg, int base_reg, int offset);

void host_arm_VMOV_32_S(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VMOV_64_D(codeblock_t *block, int dest_reg_low, int dest_reg_high, int src_reg);
void host_arm_VMOV_D_64(codeblock_t *block, int dest_reg, int src_reg_low, int src_reg_high);
void host_arm_VMOV_S_32(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VMOV_D_D(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VMOVN_I32(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VMOVN_I64(codeblock_t *block, int dest_reg, int src_reg);

void host_arm_VMRS_APSR(codeblock_t *block);
void host_arm_VMSR_FPSCR(codeblock_t *block, int src_reg);

void host_arm_VMAX_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VMIN_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);

void host_arm_VMOV_F32_ONE(codeblock_t *block, int dst_reg);

void host_arm_VMUL_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VMUL_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VMUL_S16(codeblock_t *block, int dest_reg, int src_reg_n, int src_reg_m);
void host_arm_VMULL_S16(codeblock_t *block, int dest_reg, int src_reg_n, int src_reg_m);

void host_arm_VNEG_D(codeblock_t *block, int dest_reg, int src_reg);

void host_arm_VORR_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);

void host_arm_VPADDL_S16(codeblock_t *block, int dst_reg, int src_reg);
void host_arm_VPADDL_S32(codeblock_t *block, int dst_reg, int src_reg);
void host_arm_VPADDL_Q_S32(codeblock_t *block, int dst_reg, int src_reg);

void host_arm_VQADD_S8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VQADD_U8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VQADD_S16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VQADD_U16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VQSUB_S8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VQSUB_U8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VQSUB_S16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VQSUB_U16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);

void host_arm_VQMOVN_S16(codeblock_t *block, int dst_reg, int src_reg);
void host_arm_VQMOVN_S32(codeblock_t *block, int dst_reg, int src_reg);
void host_arm_VQMOVN_U16(codeblock_t *block, int dst_reg, int src_reg);

void host_arm_VSHL_D_IMM_16(codeblock_t *block, int dest_reg, int src_reg, int shift);
void host_arm_VSHL_D_IMM_32(codeblock_t *block, int dest_reg, int src_reg, int shift);
void host_arm_VSHL_D_IMM_64(codeblock_t *block, int dest_reg, int src_reg, int shift);
void host_arm_VSHR_D_S16(codeblock_t *block, int dest_reg, int src_reg, int shift);
void host_arm_VSHR_D_S32(codeblock_t *block, int dest_reg, int src_reg, int shift);
void host_arm_VSHR_D_S64(codeblock_t *block, int dest_reg, int src_reg, int shift);
void host_arm_VSHR_D_U16(codeblock_t *block, int dest_reg, int src_reg, int shift);
void host_arm_VSHR_D_U32(codeblock_t *block, int dest_reg, int src_reg, int shift);
void host_arm_VSHR_D_U64(codeblock_t *block, int dest_reg, int src_reg, int shift);
void host_arm_VSHRN_32(codeblock_t *block, int dest_reg, int src_reg, int shift);

void host_arm_VSQRT_D(codeblock_t *block, int dest_reg, int src_reg);
void host_arm_VSQRT_S(codeblock_t *block, int dest_reg, int src_reg);

void host_arm_VSTR_D(codeblock_t *block, int src_reg, int base_reg, int offset);
void host_arm_VSTR_S(codeblock_t *block, int src_reg, int base_reg, int offset);
void host_arm_VSUB_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VSUB_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VSUB_S(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VSUB_I8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VSUB_I16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);
void host_arm_VSUB_I32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m);

void host_arm_VZIP_D8(codeblock_t *block, int d_reg, int m_reg);
void host_arm_VZIP_D16(codeblock_t *block, int d_reg, int m_reg);
void host_arm_VZIP_D32(codeblock_t *block, int d_reg, int m_reg);

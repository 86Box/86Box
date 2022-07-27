void host_x86_ADDPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_ADDSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

#define CMPPS_EQ  0
#define CMPPS_NLT 5
#define CMPPS_NLE 6
void host_x86_CMPPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg, int type);

void host_x86_COMISD_XREG_XREG(codeblock_t *block, int src_reg_a, int src_reg_b);

void host_x86_CVTDQ2PS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_CVTPS2DQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_CVTSD2SI_REG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_CVTSD2SS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_CVTSI2SD_XREG_REG(codeblock_t *block, int dest_reg, int src_reg);
void host_x86_CVTSI2SS_XREG_REG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_CVTSS2SD_XREG_XREG(codeblock_t *block, int dest_reg, int src_reg);
void host_x86_CVTSS2SD_XREG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int idx_reg);

void host_x86_DIVSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_DIVSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_LDMXCSR(codeblock_t *block, void *p);

void host_x86_MAXSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_MOVD_BASE_INDEX_XREG(codeblock_t *block, int base_reg, int idx_reg, int src_reg);
void host_x86_MOVD_REG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_MOVD_XREG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int idx_reg);
void host_x86_MOVD_XREG_REG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_MOVQ_ABS_XREG(codeblock_t *block, void *p, int src_reg);
void host_x86_MOVQ_ABS_REG_REG_SHIFT_XREG(codeblock_t *block, uint32_t addr, int src_reg_a, int src_reg_b, int shift, int src_reg);
void host_x86_MOVQ_BASE_INDEX_XREG(codeblock_t *block, int base_reg, int idx_reg, int src_reg);
void host_x86_MOVQ_BASE_OFFSET_XREG(codeblock_t *block, int base_reg, int offset, int src_reg);
void host_x86_MOVQ_STACK_OFFSET_XREG(codeblock_t *block, int offset, int src_reg);

void host_x86_MOVQ_XREG_ABS(codeblock_t *block, int dst_reg, void *p);
void host_x86_MOVQ_XREG_ABS_REG_REG_SHIFT(codeblock_t *block, int dst_reg, uint32_t addr, int src_reg_a, int src_reg_b, int shift);
void host_x86_MOVQ_XREG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int idx_reg);
void host_x86_MOVQ_XREG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset);
void host_x86_MOVQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_MAXPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_MINPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_MULPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_MULSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_PACKSSWB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PACKSSDW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PACKUSWB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_PADDB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PADDW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PADDD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PADDSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PADDSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PADDUSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PADDUSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_PAND_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PANDN_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_POR_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PXOR_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_PCMPEQB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PCMPEQW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PCMPEQD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PCMPGTB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PCMPGTW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PCMPGTD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_PMADDWD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PMULHW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PMULLW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_PSLLW_XREG_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_PSLLD_XREG_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_PSLLQ_XREG_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_PSRAW_XREG_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_PSRAD_XREG_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_PSRAQ_XREG_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_PSRLW_XREG_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_PSRLD_XREG_IMM(codeblock_t *block, int dst_reg, int shift);
void host_x86_PSRLQ_XREG_IMM(codeblock_t *block, int dst_reg, int shift);

void host_x86_PSHUFD_XREG_XREG_IMM(codeblock_t *block, int dst_reg, int src_reg, uint8_t shuffle);

void host_x86_PSUBB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PSUBW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PSUBD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PSUBSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PSUBSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PSUBUSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PSUBUSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_PUNPCKLBW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PUNPCKLWD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_PUNPCKLDQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_SQRTSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_SQRTSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_SUBPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);
void host_x86_SUBSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

void host_x86_UNPCKLPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg);

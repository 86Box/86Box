#if defined __aarch64__ || defined _M_ARM64

#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "codegen.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_backend_arm64_defs.h"
#include "codegen_backend_arm64_ops.h"


#define Rt(x)   (x)
#define Rd(x)   (x)
#define Rn(x)  ((x) << 5)
#define Rt2(x) ((x) << 10)
#define Rm(x)  ((x) << 16)

#define shift_imm6(x) ((x) << 10)

#define DATA_OFFSET_UP   (1 << 23)
#define DATA_OFFSET_DOWN (0 << 23)

#define COND_EQ (0x0)
#define COND_NE (0x1)
#define COND_CS (0x2)
#define COND_CC (0x3)
#define COND_MI (0x4)
#define COND_PL (0x5)
#define COND_VS (0x6)
#define COND_VC (0x7)
#define COND_HI (0x8)
#define COND_LS (0x9)
#define COND_GE (0xa)
#define COND_LT (0xb)
#define COND_GT (0xc)
#define COND_LE (0xd)

#define CSEL_COND(cond) ((cond) << 12)

#define OPCODE_SHIFT 24
#define OPCODE_ADD_IMM       (0x11 << OPCODE_SHIFT)
#define OPCODE_ADDX_IMM      (0x91 << OPCODE_SHIFT)
#define OPCODE_ADR           (0x10 << OPCODE_SHIFT)
#define OPCODE_B             (0x14 << OPCODE_SHIFT)
#define OPCODE_BCOND         (0x54 << OPCODE_SHIFT)
#define OPCODE_CBNZ          (0xb5 << OPCODE_SHIFT)
#define OPCODE_CBZ           (0xb4 << OPCODE_SHIFT)
#define OPCODE_CMN_IMM       (0x31 << OPCODE_SHIFT)
#define OPCODE_CMNX_IMM      (0xb1 << OPCODE_SHIFT)
#define OPCODE_CMP_IMM       (0x71 << OPCODE_SHIFT)
#define OPCODE_CMPX_IMM      (0xf1 << OPCODE_SHIFT)
#define OPCODE_SUB_IMM       (0x51 << OPCODE_SHIFT)
#define OPCODE_SUBX_IMM      (0xd1 << OPCODE_SHIFT)
#define OPCODE_TBNZ          (0x37 << OPCODE_SHIFT)
#define OPCODE_TBZ           (0x36 << OPCODE_SHIFT)

#define OPCODE_AND_IMM       (0x024 << 23)
#define OPCODE_ANDS_IMM      (0x0e4 << 23)
#define OPCODE_EOR_IMM       (0x0a4 << 23)
#define OPCODE_MOVK_W        (0x0e5 << 23)
#define OPCODE_MOVK_X        (0x1e5 << 23)
#define OPCODE_MOVZ_W        (0x0a5 << 23)
#define OPCODE_MOVZ_X        (0x1a5 << 23)
#define OPCODE_ORR_IMM       (0x064 << 23)

#define OPCODE_BFI           (0x0cc << 22)
#define OPCODE_LDR_IMM_W     (0x2e5 << 22)
#define OPCODE_LDR_IMM_X     (0x3e5 << 22)
#define OPCODE_LDR_IMM_F64   (0x3f5 << 22)
#define OPCODE_LDRB_IMM_W    (0x0e5 << 22)
#define OPCODE_LDRH_IMM      (0x1e5 << 22)
#define OPCODE_LDP_POSTIDX_X (0x2a3 << 22)
#define OPCODE_SBFX          (0x04c << 22)
#define OPCODE_STP_PREIDX_X  (0x2a6 << 22)
#define OPCODE_STR_IMM_W     (0x2e4 << 22)
#define OPCODE_STR_IMM_Q     (0x3e4 << 22)
#define OPCODE_STR_IMM_F64   (0x3f4 << 22)
#define OPCODE_STRB_IMM      (0x0e4 << 22)
#define OPCODE_STRH_IMM      (0x1e4 << 22)
#define OPCODE_UBFX          (0x14c << 22)

#define OPCODE_ADD_LSL       (0x058 << 21)
#define OPCODE_ADD_LSR       (0x05a << 21)
#define OPCODE_ADDX_LSL      (0x458 << 21)
#define OPCODE_AND_ASR       (0x054 << 21)
#define OPCODE_AND_LSL       (0x050 << 21)
#define OPCODE_AND_ROR       (0x056 << 21)
#define OPCODE_ANDS_LSL      (0x350 << 21)
#define OPCODE_CMP_LSL       (0x358 << 21)
#define OPCODE_CSEL          (0x0d4 << 21)
#define OPCODE_EOR_LSL       (0x250 << 21)
#define OPCODE_ORR_ASR       (0x154 << 21)
#define OPCODE_ORR_LSL       (0x150 << 21)
#define OPCODE_ORR_LSR       (0x152 << 21)
#define OPCODE_ORR_ROR       (0x156 << 21)
#define OPCODE_ORRX_LSL      (0x550 << 21)
#define OPCODE_SUB_LSL       (0x258 << 21)
#define OPCODE_SUB_LSR       (0x25a << 21)
#define OPCODE_SUBX_LSL      (0x658 << 21)

#define OPCODE_ADD_V8B       (0x0e208400)
#define OPCODE_ADD_V4H       (0x0e608400)
#define OPCODE_ADD_V2S       (0x0ea08400)
#define OPCODE_ADDP_V4S      (0x4ea0bc00)
#define OPCODE_AND_V         (0x0e201c00)
#define OPCODE_ASR           (0x1ac02800)
#define OPCODE_BIC_V         (0x0e601c00)
#define OPCODE_BLR           (0xd63f0000)
#define OPCODE_BR            (0xd61f0000)
#define OPCODE_CMEQ_V8B      (0x2e208c00)
#define OPCODE_CMEQ_V4H      (0x2e608c00)
#define OPCODE_CMEQ_V2S      (0x2ea08c00)
#define OPCODE_CMGT_V8B      (0x0e203400)
#define OPCODE_CMGT_V4H      (0x0e603400)
#define OPCODE_CMGT_V2S      (0x0ea03400)
#define OPCODE_DUP_V2S       (0x0e040400)
#define OPCODE_EOR_V         (0x2e201c00)
#define OPCODE_FABS_D        (0x1e60c000)
#define OPCODE_FADD_D        (0x1e602800)
#define OPCODE_FADD_V2S      (0x0e20d400)
#define OPCODE_FCMEQ_V2S     (0x0e20e400)
#define OPCODE_FCMGE_V2S     (0x2e20e400)
#define OPCODE_FCMGT_V2S     (0x2ea0e400)
#define OPCODE_FCMP_D        (0x1e602000)
#define OPCODE_FCVT_D_S      (0x1e22c000)
#define OPCODE_FCVT_S_D      (0x1e624000)
#define OPCODE_FCVTMS_W_D    (0x1e700000)
#define OPCODE_FCVTMS_X_D    (0x9e700000)
#define OPCODE_FCVTNS_W_D    (0x1e600000)
#define OPCODE_FCVTNS_X_D    (0x9e600000)
#define OPCODE_FCVTPS_W_D    (0x1e680000)
#define OPCODE_FCVTPS_X_D    (0x9e680000)
#define OPCODE_FCVTZS_W_D    (0x1e780000)
#define OPCODE_FCVTZS_X_D    (0x9e780000)
#define OPCODE_FCVTZS_V2S    (0x0ea1b800)
#define OPCODE_FDIV_D        (0x1e601800)
#define OPCODE_FDIV_S        (0x1e201800)
#define OPCODE_FMAX_V2S      (0x0e20f400)
#define OPCODE_FMIN_V2S      (0x0ea0f400)
#define OPCODE_FMOV_D_D      (0x1e604000)
#define OPCODE_FMOV_D_Q      (0x9e670000)
#define OPCODE_FMOV_Q_D      (0x9e660000)
#define OPCODE_FMOV_S_W      (0x1e270000)
#define OPCODE_FMOV_W_S      (0x1e260000)
#define OPCODE_FMOV_S_ONE    (0x1e2e1000)
#define OPCODE_FMUL_D        (0x1e600800)
#define OPCODE_FMUL_V2S      (0x2e20dc00)
#define OPCODE_FNEG_D        (0x1e614000)
#define OPCODE_FRINTX_D      (0x1e674000)
#define OPCODE_FSQRT_D       (0x1e61c000)
#define OPCODE_FSQRT_S       (0x1e21c000)
#define OPCODE_FSUB_D        (0x1e603800)
#define OPCODE_FSUB_V2S      (0x0ea0d400)
#define OPCODE_LDR_REG       (0xb8606800)
#define OPCODE_LDRX_REG      (0xf8606800)
#define OPCODE_LDRB_REG      (0x38606800)
#define OPCODE_LDRH_REG      (0x78606800)
#define OPCODE_LDRX_REG_LSL3 (0xf8607800)
#define OPCODE_LDR_REG_F32   (0xbc606800)
#define OPCODE_LDR_REG_F64   (0xfc606800)
#define OPCODE_LDR_REG_F64_S (0xfc607800)
#define OPCODE_LSL           (0x1ac02000)
#define OPCODE_LSR           (0x1ac02400)
#define OPCODE_MSR_FPCR      (0xd51b4400)
#define OPCODE_MUL_V4H       (0x0e609c00)
#define OPCODE_NOP           (0xd503201f)
#define OPCODE_ORR_V         (0x0ea01c00)
#define OPCODE_RET           (0xd65f0000)
#define OPCODE_ROR           (0x1ac02c00)
#define OPCODE_SADDLP_V2S_4H (0x0e602800)
#define OPCODE_SCVTF_D_Q     (0x9e620000)
#define OPCODE_SCVTF_D_W     (0x1e620000)
#define OPCODE_SCVTF_V2S     (0x0e21d800)
#define OPCODE_SQADD_V8B     (0x0e200c00)
#define OPCODE_SQADD_V4H     (0x0e600c00)
#define OPCODE_SQSUB_V8B     (0x0e202c00)
#define OPCODE_SQSUB_V4H     (0x0e602c00)
#define OPCODE_SQXTN_V8B_8H  (0x0e214800)
#define OPCODE_SQXTN_V4H_4S  (0x0e614800)
#define OPCODE_SHL_VD        (0x0f005400)
#define OPCODE_SHL_VQ        (0x4f005400)
#define OPCODE_SHRN          (0x0f008400)
#define OPCODE_SMULL_V4S_4H  (0x0e60c000)
#define OPCODE_SSHR_VD       (0x0f000400)
#define OPCODE_SSHR_VQ       (0x4f000400)
#define OPCODE_STR_REG       (0xb8206800)
#define OPCODE_STRB_REG      (0x38206800)
#define OPCODE_STRH_REG      (0x78206800)
#define OPCODE_STR_REG_F32   (0xbc206800)
#define OPCODE_STR_REG_F64   (0xfc206800)
#define OPCODE_STR_REG_F64_S (0xfc207800)
#define OPCODE_SUB_V8B       (0x2e208400)
#define OPCODE_SUB_V4H       (0x2e608400)
#define OPCODE_SUB_V2S       (0x2ea08400)
#define OPCODE_UQADD_V8B     (0x2e200c00)
#define OPCODE_UQADD_V4H     (0x2e600c00)
#define OPCODE_UQSUB_V8B     (0x2e202c00)
#define OPCODE_UQSUB_V4H     (0x2e602c00)
#define OPCODE_UQXTN_V8B_8H  (0x2e214800)
#define OPCODE_UQXTN_V4H_4S  (0x2e614800)
#define OPCODE_USHR_VD       (0x2f000400)
#define OPCODE_USHR_VQ       (0x6f000400)
#define OPCODE_ZIP1_V8B      (0x0e003800)
#define OPCODE_ZIP1_V4H      (0x0e403800)
#define OPCODE_ZIP1_V2S      (0x0e803800)
#define OPCODE_ZIP2_V8B      (0x0e007800)
#define OPCODE_ZIP2_V4H      (0x0e407800)
#define OPCODE_ZIP2_V2S      (0x0e807800)

#define DATPROC_SHIFT(sh) (sh << 10)
#define DATPROC_IMM_SHIFT(sh) (sh << 22)
#define MOV_WIDE_HW(hw) (hw << 21)

#define IMM7_X(imm_data)  (((imm_data >> 3) & 0x7f) << 15)
#define IMM12(imm_data) ((imm_data) << 10)
#define IMM16(imm_data) ((imm_data) << 5)

#define IMMN(immn) ((immn) << 22)
#define IMMR(immr) ((immr) << 16)
#define IMMS(imms) ((imms) << 10)

#define IMM_LOGICAL(imm) ((imm) << 10)

#define BIT_TBxZ(bit) ((((bit) & 0x1f) << 19) | (((bit) & 0x20) ? (1 << 31) : 0))

#define OFFSET14(offset) (((offset >> 2) << 5) & 0x0007ffe0)
#define OFFSET19(offset) (((offset >> 2) << 5) & 0x00ffffe0)
#define OFFSET20(offset) (((offset & 3) << 29) | ((((offset) & 0x1fffff) >> 2) << 5))
#define OFFSET26(offset) ((offset >> 2) & 0x03ffffff)

#define OFFSET12_B(offset)    (offset << 10)
#define OFFSET12_H(offset) ((offset >> 1) << 10)
#define OFFSET12_W(offset) ((offset >> 2) << 10)
#define OFFSET12_Q(offset) ((offset >> 3) << 10)

#define SHIFT_IMM_V4H(shift) (((shift) | 0x10) << 16)
#define SHIFT_IMM_V2S(shift) (((shift) | 0x20) << 16)
#define SHIFT_IMM_V2D(shift) (((shift) | 0x40) << 16)

#define SHRN_SHIFT_IMM_V4S(shift) (((shift) | 0x10) << 16)

#define DUP_ELEMENT(element) ((element) << 19)

/*Returns true if offset fits into 19 bits*/
static int offset_is_19bit(int offset)
{
	if (offset >= (1 << (18+2)))
		return 0;
	if (offset < -(1 << (18+2)))
		return 0;
	return 1;
}

/*Returns true if offset fits into 26 bits*/
static int offset_is_26bit(int offset)
{
	if (offset >= (1 << (25+2)))
		return 0;
	if (offset < -(1 << (25+2)))
		return 0;
	return 1;
}

static inline int imm_is_imm16(uint32_t imm_data)
{
	if (!(imm_data & 0xffff0000) || !(imm_data & 0x0000ffff))
		return 1;
	return 0;
}

static void codegen_allocate_new_block(codeblock_t *block);

static inline void codegen_addlong(codeblock_t *block, uint32_t val)
{
        if (block_pos >= (BLOCK_MAX-4))
		codegen_allocate_new_block(block);
        *(uint32_t *)&block_write_data[block_pos] = val;
        block_pos += 4;
}

static void codegen_allocate_new_block(codeblock_t *block)
{
        /*Current block is full. Allocate a new block*/
        struct mem_block_t *new_block = codegen_allocator_allocate(block->head_mem_block, get_block_nr(block));
        uint8_t *new_ptr = codeblock_allocator_get_ptr(new_block);
	uint32_t offset = (uintptr_t)new_ptr - (uintptr_t)&block_write_data[block_pos];

	if (!offset_is_26bit(offset))
		fatal("codegen_allocate_new_block - offset out of range %x\n", offset);
        /*Add a jump instruction to the new block*/
	*(uint32_t *)&block_write_data[block_pos] = OPCODE_B | OFFSET26(offset);

        /*Set write address to start of new block*/
        block_pos = 0;
        block_write_data = new_ptr;
}

void codegen_alloc(codeblock_t *block, int size)
{
        if (block_pos >= (BLOCK_MAX-size))
		codegen_allocate_new_block(block);
}

void host_arm64_ADD_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data)
{
	if (!imm_data)
		host_arm64_MOV_REG(block, dst_reg, src_n_reg, 0);
	else if ((int32_t)imm_data < 0 && imm_data != 0x80000000)
	{
		host_arm64_SUB_IMM(block, dst_reg, src_n_reg, -(int32_t)imm_data);
	}
	else if (!(imm_data & 0xff000000))
	{
		if (imm_data & 0xfff)
		{
			codegen_addlong(block, OPCODE_ADD_IMM | Rd(dst_reg) | Rn(src_n_reg) | IMM12(imm_data & 0xfff) | DATPROC_IMM_SHIFT(0));
			if (imm_data & 0xfff000)
				codegen_addlong(block, OPCODE_ADD_IMM | Rd(dst_reg) | Rn(dst_reg) | IMM12((imm_data >> 12) & 0xfff) | DATPROC_IMM_SHIFT(1));
		}
		else if (imm_data & 0xfff000)
			codegen_addlong(block, OPCODE_ADD_IMM | Rd(dst_reg) | Rn(src_n_reg) | IMM12((imm_data >> 12) & 0xfff) | DATPROC_IMM_SHIFT(1));
	}
	else
	{
		host_arm64_MOVZ_IMM(block, REG_W16, imm_data & 0xffff);
		host_arm64_MOVK_IMM(block, REG_W16, imm_data & 0xffff0000);
		codegen_addlong(block, OPCODE_ADD_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(REG_W16) | DATPROC_SHIFT(0));
	}
}
void host_arm64_ADDX_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint64_t imm_data)
{
	if (!(imm_data & ~0xffffffull))
	{
		if (imm_data & 0xfff)
		{
			codegen_addlong(block, OPCODE_ADDX_IMM | Rd(dst_reg) | Rn(src_n_reg) | IMM12(imm_data & 0xfff) | DATPROC_IMM_SHIFT(0));
			if (imm_data & 0xfff000)
				codegen_addlong(block, OPCODE_ADDX_IMM | Rd(dst_reg) | Rn(dst_reg) | IMM12((imm_data >> 12) & 0xfff) | DATPROC_IMM_SHIFT(1));
		}
		else if (imm_data & 0xfff000)
			codegen_addlong(block, OPCODE_ADDX_IMM | Rd(dst_reg) | Rn(src_n_reg) | IMM12((imm_data >> 12) & 0xfff) | DATPROC_IMM_SHIFT(1));
	}
	else
		fatal("ADD_IMM_X %016llx\n", imm_data);
}
void host_arm64_ADD_REG(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_ADD_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}
void host_arm64_ADD_REG_LSR(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_ADD_LSR | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}
void host_arm64_ADD_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_ADD_V8B | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_ADD_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_ADD_V4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_ADD_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_ADD_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_ADDP_V4S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_ADDP_V4S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_ADR(codeblock_t *block, int dst_reg, int offset)
{
	codegen_addlong(block, OPCODE_ADR | Rd(dst_reg) | OFFSET20(offset));
}

void host_arm64_AND_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data)
{
	uint32_t imm_encoding = host_arm64_find_imm(imm_data);

	if (imm_encoding)
	{
		codegen_addlong(block, OPCODE_AND_IMM | Rd(dst_reg) | Rn(src_n_reg) | IMM_LOGICAL(imm_encoding));
	}
	else
	{
		host_arm64_mov_imm(block, REG_W16, imm_data);
		codegen_addlong(block, OPCODE_AND_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(REG_W16) | DATPROC_SHIFT(0));
	}
}
void host_arm64_AND_REG(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_AND_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}
void host_arm64_AND_REG_ASR(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_AND_ASR | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}
void host_arm64_AND_REG_ROR(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_AND_ROR | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}
void host_arm64_AND_REG_V(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_AND_V | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_ANDS_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data)
{
	uint32_t imm_encoding = host_arm64_find_imm(imm_data);

	if (imm_encoding)
	{
		codegen_addlong(block, OPCODE_ANDS_IMM | Rd(dst_reg) | Rn(src_n_reg) | IMM_LOGICAL(imm_encoding));
	}
	else
	{
		host_arm64_mov_imm(block, REG_W16, imm_data);
		codegen_addlong(block, OPCODE_ANDS_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(REG_W16) | DATPROC_SHIFT(0));
	}
}

void host_arm64_ASR(codeblock_t *block, int dst_reg, int src_n_reg, int shift_reg)
{
	codegen_addlong(block, OPCODE_ASR | Rd(dst_reg) | Rn(src_n_reg) | Rm(shift_reg));
}

void host_arm64_B(codeblock_t *block, void *dest)
{
	int offset;

	codegen_alloc(block, 4);
	offset = (uintptr_t)dest - (uintptr_t)&block_write_data[block_pos];

	if (!offset_is_26bit(offset))
		fatal("host_arm64_B - offset out of range %x\n", offset);
	codegen_addlong(block, OPCODE_B | OFFSET26(offset));
}

void host_arm64_BFI(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width)
{
	codegen_addlong(block, OPCODE_BFI | Rd(dst_reg) | Rn(src_reg) | IMMN(0) | IMMR((32 - lsb) & 31) | IMMS((width-1) & 31));
}

void host_arm64_BLR(codeblock_t *block, int addr_reg)
{
	codegen_addlong(block, OPCODE_BLR | Rn(addr_reg));
}

uint32_t *host_arm64_BCC_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_CS | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BCS_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_CC | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BEQ_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_NE | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BGE_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_LT | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BGT_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_LE | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BHI_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_LS | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BLE_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_GT | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BLS_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_HI | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BLT_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_GE | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BMI_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_PL | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BNE_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_EQ | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BPL_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_MI | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BVC_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_VS | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}
uint32_t *host_arm64_BVS_(codeblock_t *block)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_BCOND | COND_VC | OFFSET19(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}

void host_arm64_branch_set_offset(uint32_t *opcode, void *dest)
{
	int offset = (uintptr_t)dest - (uintptr_t)opcode;
	*opcode |= OFFSET26(offset);
}

void host_arm64_BR(codeblock_t *block, int addr_reg)
{
	codegen_addlong(block, OPCODE_BR | Rn(addr_reg));
}

void host_arm64_BEQ(codeblock_t *block, void *dest)
{
	uint32_t *opcode = host_arm64_BEQ_(block);
	host_arm64_branch_set_offset(opcode, dest);
}

void host_arm64_BIC_REG_V(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_BIC_V | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_CBNZ(codeblock_t *block, int reg, uintptr_t dest)
{
	int offset;

	codegen_alloc(block, 4);
	offset = dest - (uintptr_t)&block_write_data[block_pos];
	if (offset_is_19bit(offset))
	{
		codegen_addlong(block, OPCODE_CBNZ | OFFSET19(offset) | Rt(reg));
	}
	else
	{
		codegen_alloc(block, 12);
		codegen_addlong(block, OPCODE_CBZ | OFFSET19(8) | Rt(reg));
		offset = (uintptr_t)dest - (uintptr_t)&block_write_data[block_pos];
		codegen_addlong(block, OPCODE_B | OFFSET26(offset));
	}
}

void host_arm64_CMEQ_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_CMEQ_V8B | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_CMEQ_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_CMEQ_V4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_CMEQ_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_CMEQ_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_CMGT_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_CMGT_V8B | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_CMGT_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_CMGT_V4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_CMGT_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_CMGT_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_CMN_IMM(codeblock_t *block, int src_n_reg, uint32_t imm_data)
{
	if ((int32_t)imm_data < 0 && imm_data != (1ull << 31))
	{
		host_arm64_CMP_IMM(block, src_n_reg, -(int32_t)imm_data);
	}
	else if (!(imm_data & 0xfffff000))
	{
		codegen_addlong(block, OPCODE_CMN_IMM | Rd(REG_WZR) | Rn(src_n_reg) | IMM12(imm_data & 0xfff) | DATPROC_IMM_SHIFT(0));
	}
	else
		fatal("CMN_IMM %08x\n", imm_data);
}
void host_arm64_CMNX_IMM(codeblock_t *block, int src_n_reg, uint64_t imm_data)
{
	if ((int64_t)imm_data < 0 && imm_data != (1ull << 63))
	{
		host_arm64_CMPX_IMM(block, src_n_reg, -(int64_t)imm_data);
	}
	else if (!(imm_data & 0xfffffffffffff000ull))
	{
		codegen_addlong(block, OPCODE_CMNX_IMM | Rd(REG_XZR) | Rn(src_n_reg) | IMM12(imm_data & 0xfff) | DATPROC_IMM_SHIFT(0));
	}
	else
		fatal("CMNX_IMM %08x\n", imm_data);
}

void host_arm64_CMP_IMM(codeblock_t *block, int src_n_reg, uint32_t imm_data)
{
	if ((int32_t)imm_data < 0 && imm_data != (1ull << 31))
	{
		host_arm64_CMN_IMM(block, src_n_reg, -(int32_t)imm_data);
	}
	else if (!(imm_data & 0xfffff000))
	{
		codegen_addlong(block, OPCODE_CMP_IMM | Rd(REG_WZR) | Rn(src_n_reg) | IMM12(imm_data & 0xfff) | DATPROC_IMM_SHIFT(0));
	}
	else
		fatal("CMP_IMM %08x\n", imm_data);
}
void host_arm64_CMPX_IMM(codeblock_t *block, int src_n_reg, uint64_t imm_data)
{
	if ((int64_t)imm_data < 0 && imm_data != (1ull << 63))
	{
		host_arm64_CMNX_IMM(block, src_n_reg, -(int64_t)imm_data);
	}
	else if (!(imm_data & 0xfffffffffffff000ull))
	{
		codegen_addlong(block, OPCODE_CMPX_IMM | Rd(REG_XZR) | Rn(src_n_reg) | IMM12(imm_data & 0xfff) | DATPROC_IMM_SHIFT(0));
	}
	else
		fatal("CMPX_IMM %08x\n", imm_data);
}

void host_arm64_CMP_REG_LSL(codeblock_t *block, int src_n_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_CMP_LSL | Rd(0x1f) | Rn(src_n_reg) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}

void host_arm64_CSEL_CC(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_CSEL | CSEL_COND(COND_CC) | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_CSEL_EQ(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_CSEL | CSEL_COND(COND_EQ) | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_CSEL_VS(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_CSEL | CSEL_COND(COND_VS) | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_DUP_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int element)
{
	codegen_addlong(block, OPCODE_DUP_V2S | Rd(dst_reg) | Rn(src_n_reg) | DUP_ELEMENT(element));
}

void host_arm64_EOR_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data)
{
	uint32_t imm_encoding = host_arm64_find_imm(imm_data);

	if (imm_encoding)
	{
		codegen_addlong(block, OPCODE_EOR_IMM | Rd(dst_reg) | Rn(src_n_reg) | IMM_LOGICAL(imm_encoding));
	}
	else
	{
		host_arm64_mov_imm(block, REG_W16, imm_data);
		codegen_addlong(block, OPCODE_EOR_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(REG_W16) | DATPROC_SHIFT(0));
	}
}
void host_arm64_EOR_REG(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_EOR_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}
void host_arm64_EOR_REG_V(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_EOR_V | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_FABS_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FABS_D | Rd(dst_reg) | Rn(src_reg));
}

void host_arm64_FADD_D(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FADD_D | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_FADD_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FADD_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_FCMEQ_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FCMEQ_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_FCMGE_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FCMGE_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_FCMGT_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FCMGT_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_FCMP_D(codeblock_t *block, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FCMP_D | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_FCVT_D_S(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FCVT_D_S | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FCVT_S_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FCVT_S_D | Rd(dst_reg) | Rn(src_reg));
}

void host_arm64_FCVTMS_W_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FCVTMS_W_D | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FCVTMS_X_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FCVTMS_X_D | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FCVTNS_W_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FCVTNS_W_D | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FCVTNS_X_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FCVTNS_X_D | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FCVTPS_W_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FCVTPS_W_D | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FCVTPS_X_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FCVTPS_X_D | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FCVTZS_W_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FCVTZS_W_D | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FCVTZS_X_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FCVTZS_X_D | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FCVTZS_V2S(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FCVTZS_V2S | Rd(dst_reg) | Rn(src_reg));
}

void host_arm64_FDIV_D(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FDIV_D | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_FDIV_S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FDIV_S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_FMAX_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FMAX_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_FMIN_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FMIN_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_FMUL_D(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FMUL_D | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_FMUL_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FMUL_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_FSUB_D(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FSUB_D | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_FSUB_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_FSUB_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_FMOV_D_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FMOV_D_D | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FMOV_D_Q(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FMOV_D_Q | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FMOV_Q_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FMOV_Q_D | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FMOV_S_W(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FMOV_S_W | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FMOV_W_S(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FMOV_W_S | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FMOV_S_ONE(codeblock_t *block, int dst_reg)
{
	codegen_addlong(block, OPCODE_FMOV_S_ONE | Rd(dst_reg));
}

void host_arm64_FNEG_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FNEG_D | Rd(dst_reg) | Rn(src_reg));
}

void host_arm64_FRINTX_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FRINTX_D | Rd(dst_reg) | Rn(src_reg));
}

void host_arm64_FSQRT_D(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FSQRT_D | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_FSQRT_S(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_FSQRT_S | Rd(dst_reg) | Rn(src_reg));
}

void host_arm64_LDP_POSTIDX_X(codeblock_t *block, int src_reg1, int src_reg2, int base_reg, int offset)
{
	if (!in_range7_x(offset))
		fatal("host_arm64_LDP_POSTIDX out of range7 %i\n", offset);
	codegen_addlong(block, OPCODE_LDP_POSTIDX_X | IMM7_X(offset) | Rn(base_reg) | Rt(src_reg1) | Rt2(src_reg2));
}

void host_arm64_LDR_IMM_W(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
	if (!in_range12_w(offset))
		fatal("host_arm64_LDR_IMM_W out of range12 %i\n", offset);
	codegen_addlong(block, OPCODE_LDR_IMM_W | OFFSET12_W(offset) | Rn(base_reg) | Rt(dest_reg));
}
void host_arm64_LDR_IMM_X(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
	if (!in_range12_q(offset))
		fatal("host_arm64_LDR_IMM_X out of range12 %i\n", offset);
	codegen_addlong(block, OPCODE_LDR_IMM_X | OFFSET12_Q(offset) | Rn(base_reg) | Rt(dest_reg));
}

void host_arm64_LDR_REG(codeblock_t *block, int dest_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_LDR_REG | Rn(base_reg) | Rm(offset_reg) | Rt(dest_reg));
}
void host_arm64_LDR_REG_X(codeblock_t *block, int dest_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_LDRX_REG | Rn(base_reg) | Rm(offset_reg) | Rt(dest_reg));
}

void host_arm64_LDR_REG_F32(codeblock_t *block, int dest_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_LDR_REG_F32 | Rn(base_reg) | Rm(offset_reg) | Rt(dest_reg));
}
void host_arm64_LDR_IMM_F64(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
	codegen_addlong(block, OPCODE_LDR_IMM_F64 | OFFSET12_Q(offset) | Rn(base_reg) | Rt(dest_reg));
}
void host_arm64_LDR_REG_F64(codeblock_t *block, int dest_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_LDR_REG_F64 | Rn(base_reg) | Rm(offset_reg) | Rt(dest_reg));
}
void host_arm64_LDR_REG_F64_S(codeblock_t *block, int dest_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_LDR_REG_F64_S | Rn(base_reg) | Rm(offset_reg) | Rt(dest_reg));
}

void host_arm64_LDRB_IMM_W(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
	if (!in_range12_b(offset))
		fatal("host_arm64_LDRB_IMM_W out of range12 %i\n", offset);
	codegen_addlong(block, OPCODE_LDRB_IMM_W | OFFSET12_B(offset) | Rn(base_reg) | Rt(dest_reg));
}
void host_arm64_LDRB_REG(codeblock_t *block, int dest_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_LDRB_REG | Rn(base_reg) | Rm(offset_reg) | Rt(dest_reg));
}

void host_arm64_LDRH_IMM(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
	if (!in_range12_h(offset))
		fatal("host_arm64_LDRH_IMM out of range12 %i\n", offset);
	codegen_addlong(block, OPCODE_LDRH_IMM | OFFSET12_H(offset) | Rn(base_reg) | Rt(dest_reg));
}
void host_arm64_LDRH_REG(codeblock_t *block, int dest_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_LDRH_REG | Rn(base_reg) | Rm(offset_reg) | Rt(dest_reg));
}

void host_arm64_LDRX_REG_LSL3(codeblock_t *block, int dest_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_LDRX_REG_LSL3 | Rn(base_reg) | Rm(offset_reg) | Rt(dest_reg));
}

void host_arm64_LSL(codeblock_t *block, int dst_reg, int src_n_reg, int shift_reg)
{
	codegen_addlong(block, OPCODE_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(shift_reg));
}

void host_arm64_LSR(codeblock_t *block, int dst_reg, int src_n_reg, int shift_reg)
{
	codegen_addlong(block, OPCODE_LSR | Rd(dst_reg) | Rn(src_n_reg) | Rm(shift_reg));
}

void host_arm64_MOV_REG_ASR(codeblock_t *block, int dst_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_ORR_ASR | Rd(dst_reg) | Rn(REG_WZR) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}

void host_arm64_MOV_REG(codeblock_t *block, int dst_reg, int src_m_reg, int shift)
{
	if (dst_reg != src_m_reg || shift)
		codegen_addlong(block, OPCODE_ORR_LSL | Rd(dst_reg) | Rn(REG_WZR) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}

void host_arm64_MOV_REG_LSR(codeblock_t *block, int dst_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_ORR_LSR | Rd(dst_reg) | Rn(REG_WZR) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}
void host_arm64_MOV_REG_ROR(codeblock_t *block, int dst_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_ORR_ROR | Rd(dst_reg) | Rn(REG_WZR) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}

void host_arm64_MOVX_IMM(codeblock_t *block, int reg, uint64_t imm_data)
{
	codegen_addlong(block, OPCODE_MOVZ_X | MOV_WIDE_HW(0) | IMM16(imm_data & 0xffff) | Rd(reg));
	if ((imm_data >> 16) & 0xffff)
		codegen_addlong(block, OPCODE_MOVK_X | MOV_WIDE_HW(1) | IMM16((imm_data >> 16) & 0xffff) | Rd(reg));
	if ((imm_data >> 32) & 0xffff)
		codegen_addlong(block, OPCODE_MOVK_X | MOV_WIDE_HW(2) | IMM16((imm_data >> 32) & 0xffff) | Rd(reg));
	if ((imm_data >> 48) & 0xffff)
		codegen_addlong(block, OPCODE_MOVK_X | MOV_WIDE_HW(3) | IMM16((imm_data >> 48) & 0xffff) | Rd(reg));
}
void host_arm64_MOVX_REG(codeblock_t *block, int dst_reg, int src_m_reg, int shift)
{
	if (dst_reg != src_m_reg)
		codegen_addlong(block, OPCODE_ORRX_LSL | Rd(dst_reg) | Rn(REG_XZR) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}

void host_arm64_MOVZ_IMM(codeblock_t *block, int reg, uint32_t imm_data)
{
	int hw;

	if (!imm_is_imm16(imm_data))
		fatal("MOVZ_IMM - imm not representable %08x\n", imm_data);

	hw = (imm_data & 0xffff0000) ? 1 : 0;
	if (hw)
		imm_data >>= 16;

	codegen_addlong(block, OPCODE_MOVZ_W | MOV_WIDE_HW(hw) | IMM16(imm_data) | Rd(reg));
}

void host_arm64_MOVK_IMM(codeblock_t *block, int reg, uint32_t imm_data)
{
	int hw;

	if (!imm_is_imm16(imm_data))
		fatal("MOVK_IMM - imm not representable %08x\n", imm_data);

	hw = (imm_data & 0xffff0000) ? 1 : 0;
	if (hw)
		imm_data >>= 16;

	codegen_addlong(block, OPCODE_MOVK_W | MOV_WIDE_HW(hw) | IMM16(imm_data) | Rd(reg));
}

void host_arm64_MSR_FPCR(codeblock_t *block, int src_reg)
{
	codegen_addlong(block, OPCODE_MSR_FPCR | Rd(src_reg));
}

void host_arm64_MUL_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_MUL_V4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_NOP(codeblock_t *block)
{
	codegen_addlong(block, OPCODE_NOP);
}

void host_arm64_ORR_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data)
{
	uint32_t imm_encoding = host_arm64_find_imm(imm_data);

	if (imm_encoding)
	{
		codegen_addlong(block, OPCODE_ORR_IMM | Rd(dst_reg) | Rn(src_n_reg) | IMM_LOGICAL(imm_encoding));
	}
	else
	{
		host_arm64_mov_imm(block, REG_W16, imm_data);
		codegen_addlong(block, OPCODE_ORR_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(REG_W16) | DATPROC_SHIFT(0));
	}
}
void host_arm64_ORR_REG(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_ORR_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}
void host_arm64_ORR_REG_V(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_ORR_V | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_RET(codeblock_t *block, int reg)
{
	codegen_addlong(block, OPCODE_RET | Rn(reg));
}

void host_arm64_ROR(codeblock_t *block, int dst_reg, int src_n_reg, int shift_reg)
{
	codegen_addlong(block, OPCODE_ROR | Rd(dst_reg) | Rn(src_n_reg) | Rm(shift_reg));
}

void host_arm64_SADDLP_V2S_4H(codeblock_t *block, int dst_reg, int src_n_reg)
{
	codegen_addlong(block, OPCODE_SADDLP_V2S_4H | Rd(dst_reg) | Rn(src_n_reg));
}

void host_arm64_SBFX(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width)
{
	codegen_addlong(block, OPCODE_SBFX | Rd(dst_reg) | Rn(src_reg) | IMMN(0) | IMMR(lsb) | IMMS((lsb+width-1) & 31));
}

void host_arm64_SCVTF_D_Q(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_SCVTF_D_Q | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_SCVTF_D_W(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_SCVTF_D_W | Rd(dst_reg) | Rn(src_reg));
}

void host_arm64_SCVTF_V2S(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_SCVTF_V2S | Rd(dst_reg) | Rn(src_reg));
}

void host_arm64_SHRN_V4H_4S(codeblock_t *block, int dst_reg, int src_n_reg, int shift)
{
	if (shift > 16)
                fatal("host_arm64_SHRN_V4H_4S : shift > 16\n");
	codegen_addlong(block, OPCODE_SHRN | Rd(dst_reg) | Rn(src_n_reg) | SHRN_SHIFT_IMM_V4S(16-shift));
}

void host_arm64_SMULL_V4S_4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_SMULL_V4S_4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_SQADD_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_SQADD_V8B | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_SQADD_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_SQADD_V4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_SQSUB_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_SQSUB_V8B | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_SQSUB_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_SQSUB_V4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_SQXTN_V8B_8H(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_SQXTN_V8B_8H | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_SQXTN_V4H_4S(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_SQXTN_V4H_4S | Rd(dst_reg) | Rn(src_reg));
}

void host_arm64_SHL_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int shift)
{
	codegen_addlong(block, OPCODE_SHL_VD | Rd(dst_reg) | Rn(src_n_reg) | SHIFT_IMM_V4H(shift));
}
void host_arm64_SHL_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int shift)
{
	codegen_addlong(block, OPCODE_SHL_VD | Rd(dst_reg) | Rn(src_n_reg) | SHIFT_IMM_V2S(shift));
}
void host_arm64_SHL_V2D(codeblock_t *block, int dst_reg, int src_n_reg, int shift)
{
	codegen_addlong(block, OPCODE_SHL_VQ | Rd(dst_reg) | Rn(src_n_reg) | SHIFT_IMM_V2D(shift));
}

void host_arm64_SSHR_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int shift)
{
        if (shift > 16)
                fatal("host_arm_USHR_V4H : shift > 16\n");
	codegen_addlong(block, OPCODE_SSHR_VD | Rd(dst_reg) | Rn(src_n_reg) | SHIFT_IMM_V4H(16-shift));
}
void host_arm64_SSHR_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int shift)
{
        if (shift > 32)
                fatal("host_arm_SSHR_V2S : shift > 32\n");
	codegen_addlong(block, OPCODE_SSHR_VD | Rd(dst_reg) | Rn(src_n_reg) | SHIFT_IMM_V2S(32-shift));
}
void host_arm64_SSHR_V2D(codeblock_t *block, int dst_reg, int src_n_reg, int shift)
{
        if (shift > 64)
                fatal("host_arm_SSHR_V2D : shift > 64\n");
	codegen_addlong(block, OPCODE_SSHR_VQ | Rd(dst_reg) | Rn(src_n_reg) | SHIFT_IMM_V2D(64-shift));
}

void host_arm64_STP_PREIDX_X(codeblock_t *block, int src_reg1, int src_reg2, int base_reg, int offset)
{
	if (!in_range7_x(offset))
		fatal("host_arm64_STP_PREIDX out of range7 %i\n", offset);
	codegen_addlong(block, OPCODE_STP_PREIDX_X | IMM7_X(offset) | Rn(base_reg) | Rt(src_reg1) | Rt2(src_reg2));
}

void host_arm64_STR_IMM_W(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
	if (!in_range12_w(offset))
		fatal("host_arm64_STR_IMM_W out of range12 %i\n", offset);
	codegen_addlong(block, OPCODE_STR_IMM_W | OFFSET12_W(offset) | Rn(base_reg) | Rt(dest_reg));
}
void host_arm64_STR_IMM_Q(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
	if (!in_range12_q(offset))
		fatal("host_arm64_STR_IMM_W out of range12 %i\n", offset);
	codegen_addlong(block, OPCODE_STR_IMM_Q | OFFSET12_Q(offset) | Rn(base_reg) | Rt(dest_reg));
}
void host_arm64_STR_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_STR_REG | Rn(base_reg) | Rm(offset_reg) | Rt(src_reg));
}

void host_arm64_STR_REG_F32(codeblock_t *block, int src_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_STR_REG_F32 | Rn(base_reg) | Rm(offset_reg) | Rt(src_reg));
}
void host_arm64_STR_IMM_F64(codeblock_t *block, int src_reg, int base_reg, int offset)
{
	codegen_addlong(block, OPCODE_STR_IMM_F64 | OFFSET12_Q(offset) | Rn(base_reg) | Rt(src_reg));
}
void host_arm64_STR_REG_F64(codeblock_t *block, int src_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_STR_REG_F64 | Rn(base_reg) | Rm(offset_reg) | Rt(src_reg));
}
void host_arm64_STR_REG_F64_S(codeblock_t *block, int src_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_STR_REG_F64_S | Rn(base_reg) | Rm(offset_reg) | Rt(src_reg));
}

void host_arm64_STRB_IMM(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
	if (!in_range12_b(offset))
		fatal("host_arm64_STRB_IMM out of range12 %i\n", offset);
	codegen_addlong(block, OPCODE_STRB_IMM | OFFSET12_B(offset) | Rn(base_reg) | Rt(dest_reg));
}
void host_arm64_STRB_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_STRB_REG | Rn(base_reg) | Rm(offset_reg) | Rt(src_reg));
}

void host_arm64_STRH_IMM(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
	if (!in_range12_h(offset))
		fatal("host_arm64_STRH_IMM out of range12 %i\n", offset);
	codegen_addlong(block, OPCODE_STRH_IMM | OFFSET12_H(offset) | Rn(base_reg) | Rt(dest_reg));
}
void host_arm64_STRH_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg)
{
	codegen_addlong(block, OPCODE_STRH_REG | Rn(base_reg) | Rm(offset_reg) | Rt(src_reg));
}

void host_arm64_SUB_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data)
{
	if (!imm_data)
		host_arm64_MOV_REG(block, dst_reg, src_n_reg, 0);
	else if ((int32_t)imm_data < 0 && imm_data != 0x80000000)
	{
		host_arm64_ADD_IMM(block, dst_reg, src_n_reg, -(int32_t)imm_data);
	}
	else if (!(imm_data & 0xff000000))
	{
		if (imm_data & 0xfff)
		{
			codegen_addlong(block, OPCODE_SUB_IMM | Rd(dst_reg) | Rn(src_n_reg) | IMM12(imm_data & 0xfff) | DATPROC_IMM_SHIFT(0));
			if (imm_data & 0xfff000)
				codegen_addlong(block, OPCODE_SUB_IMM | Rd(dst_reg) | Rn(dst_reg) | IMM12((imm_data >> 12) & 0xfff) | DATPROC_IMM_SHIFT(1));
		}
		else if (imm_data & 0xfff000)
			codegen_addlong(block, OPCODE_SUB_IMM | Rd(dst_reg) | Rn(src_n_reg) | IMM12((imm_data >> 12) & 0xfff) | DATPROC_IMM_SHIFT(1));
	}
	else
	{
		host_arm64_MOVZ_IMM(block, REG_W16, imm_data & 0xffff);
		host_arm64_MOVK_IMM(block, REG_W16, imm_data & 0xffff0000);
		codegen_addlong(block, OPCODE_SUB_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(REG_W16) | DATPROC_SHIFT(0));
	}
}
void host_arm64_SUB_REG(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_SUB_LSL | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}
void host_arm64_SUB_REG_LSR(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg, int shift)
{
	codegen_addlong(block, OPCODE_SUB_LSR | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg) | DATPROC_SHIFT(shift));
}
void host_arm64_SUB_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_SUB_V8B | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_SUB_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_SUB_V4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_SUB_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_SUB_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

uint32_t *host_arm64_TBNZ(codeblock_t *block, int reg, int bit)
{
	codegen_alloc(block, 12);
	codegen_addlong(block, OPCODE_TBZ | Rt(reg) | BIT_TBxZ(bit) | OFFSET14(8));
	codegen_addlong(block, OPCODE_B);
	return (uint32_t *)&block_write_data[block_pos-4];
}

void host_arm64_UBFX(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width)
{
	codegen_addlong(block, OPCODE_UBFX | Rd(dst_reg) | Rn(src_reg) | IMMN(0) | IMMR(lsb) | IMMS((lsb+width-1) & 31));
}

void host_arm64_UQADD_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_UQADD_V8B | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_UQADD_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_UQADD_V4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_UQSUB_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_UQSUB_V8B | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_UQSUB_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_UQSUB_V4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_UQXTN_V8B_8H(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_UQXTN_V8B_8H | Rd(dst_reg) | Rn(src_reg));
}
void host_arm64_UQXTN_V4H_4S(codeblock_t *block, int dst_reg, int src_reg)
{
	codegen_addlong(block, OPCODE_UQXTN_V4H_4S | Rd(dst_reg) | Rn(src_reg));
}

void host_arm64_USHR_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int shift)
{
        if (shift > 16)
                fatal("host_arm_USHR_V4H : shift > 16\n");
	codegen_addlong(block, OPCODE_USHR_VD | Rd(dst_reg) | Rn(src_n_reg) | SHIFT_IMM_V4H(16-shift));
}
void host_arm64_USHR_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int shift)
{
        if (shift > 32)
                fatal("host_arm_USHR_V4S : shift > 32\n");
	codegen_addlong(block, OPCODE_USHR_VD | Rd(dst_reg) | Rn(src_n_reg) | SHIFT_IMM_V2S(32-shift));
}
void host_arm64_USHR_V2D(codeblock_t *block, int dst_reg, int src_n_reg, int shift)
{
        if (shift > 64)
                fatal("host_arm_USHR_V2D : shift > 64\n");
	codegen_addlong(block, OPCODE_USHR_VQ | Rd(dst_reg) | Rn(src_n_reg) | SHIFT_IMM_V2D(64-shift));
}

void host_arm64_ZIP1_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_ZIP1_V8B | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_ZIP1_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_ZIP1_V4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_ZIP1_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_ZIP1_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_ZIP2_V8B(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_ZIP2_V8B | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_ZIP2_V4H(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_ZIP2_V4H | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
void host_arm64_ZIP2_V2S(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
	codegen_addlong(block, OPCODE_ZIP2_V2S | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}

void host_arm64_call(codeblock_t *block, void *dst_addr)
{
	host_arm64_MOVX_IMM(block, REG_X16, (uint64_t)dst_addr);
	host_arm64_BLR(block, REG_X16);
}

void host_arm64_jump(codeblock_t *block, uintptr_t dst_addr)
{
	host_arm64_MOVX_IMM(block, REG_X16, (uint64_t)dst_addr);
	host_arm64_BR(block, REG_X16);
}

void host_arm64_mov_imm(codeblock_t *block, int reg, uint32_t imm_data)
{
	if (imm_is_imm16(imm_data))
		host_arm64_MOVZ_IMM(block, reg, imm_data);
	else
	{
		host_arm64_MOVZ_IMM(block, reg, imm_data & 0xffff);
		host_arm64_MOVK_IMM(block, reg, imm_data & 0xffff0000);
	}
}

#endif

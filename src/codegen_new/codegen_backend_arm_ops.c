#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM

#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_arm_defs.h"
#    include "codegen_backend_arm_ops.h"

#    define Rm(x)                (x)
#    define Rs(x)                ((x) << 8)
#    define Rd(x)                ((x) << 12)
#    define Rt(x)                ((x) << 12)
#    define Rn(x)                ((x) << 16)
#    define Rt2(x)               ((x) << 16)

#    define Vm(x)                (x)
#    define Vd(x)                ((x) << 12)
#    define Vn(x)                ((x) << 16)

#    define DATA_OFFSET_UP       (1 << 23)
#    define DATA_OFFSET_DOWN     (0 << 23)

#    define OPCODE_SHIFT         20
#    define OPCODE_ADD_IMM       (0x28 << OPCODE_SHIFT)
#    define OPCODE_ADD_REG       (0x08 << OPCODE_SHIFT)
#    define OPCODE_AND_IMM       (0x20 << OPCODE_SHIFT)
#    define OPCODE_AND_REG       (0x00 << OPCODE_SHIFT)
#    define OPCODE_B             (0xa0 << OPCODE_SHIFT)
#    define OPCODE_BIC_IMM       (0x3c << OPCODE_SHIFT)
#    define OPCODE_BIC_REG       (0x1c << OPCODE_SHIFT)
#    define OPCODE_BL            (0xb0 << OPCODE_SHIFT)
#    define OPCODE_CMN_IMM       (0x37 << OPCODE_SHIFT)
#    define OPCODE_CMN_REG       (0x17 << OPCODE_SHIFT)
#    define OPCODE_CMP_IMM       (0x35 << OPCODE_SHIFT)
#    define OPCODE_CMP_REG       (0x15 << OPCODE_SHIFT)
#    define OPCODE_EOR_IMM       (0x22 << OPCODE_SHIFT)
#    define OPCODE_EOR_REG       (0x02 << OPCODE_SHIFT)
#    define OPCODE_LDMIA_WB      (0x8b << OPCODE_SHIFT)
#    define OPCODE_LDR_IMM       (0x51 << OPCODE_SHIFT)
#    define OPCODE_LDR_IMM_POST  (0x41 << OPCODE_SHIFT)
#    define OPCODE_LDR_REG       (0x79 << OPCODE_SHIFT)
#    define OPCODE_LDRB_IMM      (0x55 << OPCODE_SHIFT)
#    define OPCODE_LDRB_REG      (0x7d << OPCODE_SHIFT)
#    define OPCODE_MOV_IMM       (0x3a << OPCODE_SHIFT)
#    define OPCODE_MOVT_IMM      (0x34 << OPCODE_SHIFT)
#    define OPCODE_MOVW_IMM      (0x30 << OPCODE_SHIFT)
#    define OPCODE_MOV_REG       (0x1a << OPCODE_SHIFT)
#    define OPCODE_MVN_REG       (0x1e << OPCODE_SHIFT)
#    define OPCODE_ORR_IMM       (0x38 << OPCODE_SHIFT)
#    define OPCODE_ORR_REG       (0x18 << OPCODE_SHIFT)
#    define OPCODE_RSB_IMM       (0x26 << OPCODE_SHIFT)
#    define OPCODE_RSB_REG       (0x06 << OPCODE_SHIFT)
#    define OPCODE_STMDB_WB      (0x92 << OPCODE_SHIFT)
#    define OPCODE_STR_IMM       (0x50 << OPCODE_SHIFT)
#    define OPCODE_STR_IMM_WB    (0x52 << OPCODE_SHIFT)
#    define OPCODE_STR_REG       (0x78 << OPCODE_SHIFT)
#    define OPCODE_STRB_IMM      (0x54 << OPCODE_SHIFT)
#    define OPCODE_STRB_REG      (0x7c << OPCODE_SHIFT)
#    define OPCODE_SUB_IMM       (0x24 << OPCODE_SHIFT)
#    define OPCODE_SUB_REG       (0x04 << OPCODE_SHIFT)
#    define OPCODE_TST_IMM       (0x31 << OPCODE_SHIFT)
#    define OPCODE_TST_REG       (0x11 << OPCODE_SHIFT)

#    define OPCODE_BFI           0xe7c00010
#    define OPCODE_BLX           0xe12fff30
#    define OPCODE_BX            0xe12fff10
#    define OPCODE_LDRH_IMM      0xe1d000b0
#    define OPCODE_LDRH_REG      0xe19000b0
#    define OPCODE_STRH_IMM      0xe1c000b0
#    define OPCODE_STRH_REG      0xe18000b0
#    define OPCODE_SXTB          0xe6af0070
#    define OPCODE_SXTH          0xe6bf0070
#    define OPCODE_UADD8         0xe6500f90
#    define OPCODE_UADD16        0xe6500f10
#    define OPCODE_USUB8         0xe6500ff0
#    define OPCODE_USUB16        0xe6500f70
#    define OPCODE_UXTB          0xe6ef0070
#    define OPCODE_UXTH          0xe6ff0070
#    define OPCODE_VABS_D        0xeeb00bc0
#    define OPCODE_VADD          0xee300b00
#    define OPCODE_VADD_I8       0xf2000800
#    define OPCODE_VADD_I16      0xf2100800
#    define OPCODE_VADD_I32      0xf2200800
#    define OPCODE_VADD_F32      0xf2000d00
#    define OPCODE_VAND_D        0xf2000110
#    define OPCODE_VBIC_D        0xf2100110
#    define OPCODE_VCEQ_F32      0xf2000e00
#    define OPCODE_VCEQ_I8       0xf3000810
#    define OPCODE_VCEQ_I16      0xf3100810
#    define OPCODE_VCEQ_I32      0xf3200810
#    define OPCODE_VCGE_F32      0xf3000e00
#    define OPCODE_VCGT_F32      0xf3200e00
#    define OPCODE_VCGT_S8       0xf2000300
#    define OPCODE_VCGT_S16      0xf2100300
#    define OPCODE_VCGT_S32      0xf2200300
#    define OPCODE_VCMP_D        0xeeb40b40
#    define OPCODE_VCVT_D_IS     0xeeb80bc0
#    define OPCODE_VCVT_D_S      0xeeb70ac0
#    define OPCODE_VCVT_F32_S32  0xf3bb0700
#    define OPCODE_VCVT_IS_D     0xeebd0bc0
#    define OPCODE_VCVT_S32_F32  0xf3bb0600
#    define OPCODE_VCVT_S_D      0xeeb70bc0
#    define OPCODE_VCVTR_IS_D    0xeebd0b40
#    define OPCODE_VDIV          0xee800b00
#    define OPCODE_VDIV_S        0xee800a00
#    define OPCODE_VDUP_32       0xf3b40c00
#    define OPCODE_VEOR_D        0xf3000110
#    define OPCODE_VLDR_D        0xed900b00
#    define OPCODE_VLDR_S        0xed900a00
#    define OPCODE_VMAX_F32      0xf200f00
#    define OPCODE_VMIN_F32      0xf220f00
#    define OPCODE_VMOV_32_S     0xee100a10
#    define OPCODE_VMOV_64_D     0xec500b10
#    define OPCODE_VMOV_D_64     0xec400b10
#    define OPCODE_VMOV_S_32     0xee000a10
#    define OPCODE_VMOV_D_D      0xeeb00b40
#    define OPCODE_VMOVN_I32     0xf3b60200
#    define OPCODE_VMOVN_I64     0xf3ba0200
#    define OPCODE_VMOV_F32_ONE  0xf2870f10
#    define OPCODE_VMRS_APSR     0xeef1fa10
#    define OPCODE_VMSR_FPSCR    0xeee10a10
#    define OPCODE_VMUL          0xee200b00
#    define OPCODE_VMUL_F32      0xf3000d10
#    define OPCODE_VMUL_S16      0xf2100910
#    define OPCODE_VMULL_S16     0xf2900c00
#    define OPCODE_VNEG_D        0xeeb10b40
#    define OPCODE_VORR_D        0xf2200110
#    define OPCODE_VPADDL_S16    0xf3b40200
#    define OPCODE_VPADDL_S32    0xf3b80200
#    define OPCODE_VPADDL_Q_S32  0xf3b80240
#    define OPCODE_VQADD_S8      0xf2000010
#    define OPCODE_VQADD_S16     0xf2100010
#    define OPCODE_VQADD_U8      0xf3000010
#    define OPCODE_VQADD_U16     0xf3100010
#    define OPCODE_VQMOVN_S16    0xf3b20280
#    define OPCODE_VQMOVN_S32    0xf3b60280
#    define OPCODE_VQMOVN_U16    0xf3b202c0
#    define OPCODE_VQSUB_S8      0xf2000210
#    define OPCODE_VQSUB_S16     0xf2100210
#    define OPCODE_VQSUB_U8      0xf3000210
#    define OPCODE_VQSUB_U16     0xf3100210
#    define OPCODE_VSHL_D_IMM_16 0xf2900510
#    define OPCODE_VSHL_D_IMM_32 0xf2a00510
#    define OPCODE_VSHL_D_IMM_64 0xf2800590
#    define OPCODE_VSHR_D_S16    0xf2900010
#    define OPCODE_VSHR_D_S32    0xf2a00010
#    define OPCODE_VSHR_D_S64    0xf2800090
#    define OPCODE_VSHR_D_U16    0xf3900010
#    define OPCODE_VSHR_D_U32    0xf3a00010
#    define OPCODE_VSHR_D_U64    0xf3800090
#    define OPCODE_VSHRN         0xf2800810
#    define OPCODE_VSQRT_D       0xeeb10bc0
#    define OPCODE_VSQRT_S       0xeeb10ac0
#    define OPCODE_VSTR_D        0xed800b00
#    define OPCODE_VSTR_S        0xed800a00
#    define OPCODE_VSUB          0xee300b40
#    define OPCODE_VSUB_I8       0xf3000800
#    define OPCODE_VSUB_I16      0xf3100800
#    define OPCODE_VSUB_I32      0xf3200800
#    define OPCODE_VSUB_F32      0xf3000d00
#    define OPCODE_VZIP_D8       0xf3b20180
#    define OPCODE_VZIP_D16      0xf3b60180
#    define OPCODE_VZIP_D32      0xf3ba0080

#    define B_OFFSET(x)          (((x) >> 2) & 0xffffff)

#    define SHIFT_TYPE_SHIFT     5
#    define SHIFT_TYPE_LSL       (0 << SHIFT_TYPE_SHIFT)
#    define SHIFT_TYPE_LSR       (1 << SHIFT_TYPE_SHIFT)
#    define SHIFT_TYPE_ASR       (2 << SHIFT_TYPE_SHIFT)
#    define SHIFT_TYPE_ROR       (3 << SHIFT_TYPE_SHIFT)

#    define SHIFT_TYPE_IMM       (0 << 4)
#    define SHIFT_TYPE_REG       (1 << 4)

#    define SHIFT_IMM_SHIFT      7
#    define SHIFT_ASR_IMM(x)     (SHIFT_TYPE_ASR | SHIFT_TYPE_IMM | ((x) << SHIFT_IMM_SHIFT))
#    define SHIFT_LSL_IMM(x)     (SHIFT_TYPE_LSL | SHIFT_TYPE_IMM | ((x) << SHIFT_IMM_SHIFT))
#    define SHIFT_LSR_IMM(x)     (SHIFT_TYPE_LSR | SHIFT_TYPE_IMM | ((x) << SHIFT_IMM_SHIFT))
#    define SHIFT_ROR_IMM(x)     (SHIFT_TYPE_ROR | SHIFT_TYPE_IMM | ((x) << SHIFT_IMM_SHIFT))

#    define SHIFT_ASR_REG(x)     (SHIFT_TYPE_ASR | SHIFT_TYPE_REG | Rs(x))
#    define SHIFT_LSL_REG(x)     (SHIFT_TYPE_LSL | SHIFT_TYPE_REG | Rs(x))
#    define SHIFT_LSR_REG(x)     (SHIFT_TYPE_LSR | SHIFT_TYPE_REG | Rs(x))
#    define SHIFT_ROR_REG(x)     (SHIFT_TYPE_ROR | SHIFT_TYPE_REG | Rs(x))

#    define BFI_lsb(lsb)         ((lsb) << 7)
#    define BFI_msb(msb)         ((msb) << 16)

#    define UXTB_ROTATE(rotate)  (((rotate) >> 3) << 10)

#    define MOVT_IMM(imm)        (((imm) &0xfff) | (((imm) &0xf000) << 4))
#    define MOVW_IMM(imm)        (((imm) &0xfff) | (((imm) &0xf000) << 4))

#    define LDRH_IMM(imm)        (((imm) &0xf) | (((imm) &0xf0) << 4))
#    define STRH_IMM(imm)        LDRH_IMM(imm)

#    define VSHIFT_IMM(shift)    ((shift) << 16)

#    define VSHIFT_IMM_32(shift) (((16 - (shift)) | 0x10) << 16)

#    define VDUP_32_IMM(imm)     ((imm) << 19)

static void codegen_allocate_new_block(codeblock_t *block);

static inline void
codegen_addlong(codeblock_t *block, uint32_t val)
{
    if (block_pos >= (BLOCK_MAX - 4))
        codegen_allocate_new_block(block);
    *(uint32_t *) &block_write_data[block_pos] = val;
    block_pos += 4;
}

static void
codegen_allocate_new_block(codeblock_t *block)
{
    /*Current block is full. Allocate a new block*/
    struct mem_block_t *new_block = codegen_allocator_allocate(block->head_mem_block, get_block_nr(block));
    uint8_t            *new_ptr   = codeblock_allocator_get_ptr(new_block);
    uint32_t            offset    = ((uintptr_t) new_ptr - (uintptr_t) &block_write_data[block_pos]) - 8;

    /*Add a jump instruction to the new block*/
    *(uint32_t *) &block_write_data[block_pos] = COND_AL | OPCODE_B | B_OFFSET(offset);

    /*Set write address to start of new block*/
    block_pos        = 0;
    block_write_data = new_ptr;
}

static inline void
codegen_alloc_4(codeblock_t *block)
{
    if (block_pos >= (BLOCK_MAX - 4))
        codegen_allocate_new_block(block);
}

void
codegen_alloc(codeblock_t *block, int size)
{
    if (block_pos >= (BLOCK_MAX - size))
        codegen_allocate_new_block(block);
}

static inline uint32_t
arm_data_offset(int offset)
{
    if (offset < -0xffc || offset > 0xffc)
        fatal("arm_data_offset out of range - %i\n", offset);

    if (offset >= 0)
        return offset | DATA_OFFSET_UP;
    return (-offset) | DATA_OFFSET_DOWN;
}

static inline int
get_arm_imm(uint32_t imm_data, uint32_t *arm_imm)
{
    int shift = 0;
    if (!(imm_data & 0xffff)) {
        shift += 16;
        imm_data >>= 16;
    }
    if (!(imm_data & 0xff)) {
        shift += 8;
        imm_data >>= 8;
    }
    if (!(imm_data & 0xf)) {
        shift += 4;
        imm_data >>= 4;
    }
    if (!(imm_data & 0x3)) {
        shift += 2;
        imm_data >>= 2;
    }
    if (imm_data > 0xff) /*Note - should handle rotation round the word*/
        return 0;
    *arm_imm = imm_data | ((((32 - shift) >> 1) & 15) << 8);
    return 1;
}

static inline int
in_range(void *addr, void *base)
{
    int diff = (uintptr_t) addr - (uintptr_t) base;

    if (diff < -4095 || diff > 4095)
        return 0;
    return 1;
}

void host_arm_ADD_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);
void host_arm_AND_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);
void host_arm_EOR_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);
// void host_arm_ORR_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);
void host_arm_SUB_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift);

void
host_arm_ADD_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm)
{
    uint32_t arm_imm;

    if ((int32_t) imm < 0 && imm != 0x80000000) {
        host_arm_SUB_IMM(block, dst_reg, src_reg, -(int32_t) imm);
    } else if (get_arm_imm(imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_ADD_IMM | Rd(dst_reg) | Rn(src_reg) | arm_imm);
    } else {
        host_arm_MOV_IMM(block, REG_TEMP, imm);
        host_arm_ADD_REG_LSL(block, dst_reg, src_reg, REG_TEMP, 0);
    }
}

void
host_arm_ADD_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_ADD_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSL_IMM(shift));
}
void
host_arm_ADD_REG_LSR(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_ADD_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSR_IMM(shift));
}

void
host_arm_AND_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm)
{
    uint32_t arm_imm;

    if (get_arm_imm(imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_AND_IMM | Rd(dst_reg) | Rn(src_reg) | arm_imm);
    } else if (get_arm_imm(~imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_BIC_IMM | Rd(dst_reg) | Rn(src_reg) | arm_imm);
    } else {
        host_arm_MOV_IMM(block, REG_TEMP, imm);
        host_arm_AND_REG_LSL(block, dst_reg, src_reg, REG_TEMP, 0);
    }
}

void
host_arm_AND_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_AND_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSL_IMM(shift));
}
void
host_arm_AND_REG_LSR(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_AND_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSR_IMM(shift));
}

void
host_arm_B(codeblock_t *block, uintptr_t dest_addr)
{
    uint32_t offset;

    codegen_alloc_4(block);
    offset = (dest_addr - (uintptr_t) &block_write_data[block_pos]) - 8;

    if ((offset & 0xfe000000) && (offset & 0xfe000000) != 0xfe000000) {
        host_arm_MOV_IMM(block, REG_R3, dest_addr);
        host_arm_BX(block, REG_R3);
    } else
        codegen_addlong(block, COND_AL | OPCODE_B | B_OFFSET(offset));
}

void
host_arm_BFI(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width)
{
    codegen_addlong(block, OPCODE_BFI | Rd(dst_reg) | Rm(src_reg) | BFI_lsb(lsb) | BFI_msb((lsb + width) - 1));
}

void
host_arm_BIC_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm)
{
    uint32_t arm_imm;

    if (get_arm_imm(imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_BIC_IMM | Rd(dst_reg) | Rn(src_reg) | arm_imm);
    } else if (get_arm_imm(~imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_AND_IMM | Rd(dst_reg) | Rn(src_reg) | arm_imm);
    } else {
        host_arm_MOV_IMM(block, REG_TEMP, imm);
        host_arm_BIC_REG_LSL(block, dst_reg, src_reg, REG_TEMP, 0);
    }
}
void
host_arm_BIC_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_BIC_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSL_IMM(shift));
}
void
host_arm_BIC_REG_LSR(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_BIC_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSR_IMM(shift));
}

void
host_arm_BL(codeblock_t *block, uintptr_t dest_addr)
{
    uint32_t offset;

    codegen_alloc_4(block);
    offset = (dest_addr - (uintptr_t) &block_write_data[block_pos]) - 8;

    if ((offset & 0xfe000000) && (offset & 0xfe000000) != 0xfe000000) {
        host_arm_MOV_IMM(block, REG_R3, dest_addr);
        host_arm_BLX(block, REG_R3);
    } else
        codegen_addlong(block, COND_AL | OPCODE_BL | B_OFFSET(offset));
}
void
host_arm_BL_r1(codeblock_t *block, uintptr_t dest_addr)
{
    uint32_t offset;

    codegen_alloc_4(block);
    offset = (dest_addr - (uintptr_t) &block_write_data[block_pos]) - 8;

    if ((offset & 0xfe000000) && (offset & 0xfe000000) != 0xfe000000) {
        host_arm_MOV_IMM(block, REG_R1, dest_addr);
        host_arm_BLX(block, REG_R1);
    } else
        codegen_addlong(block, COND_AL | OPCODE_BL | B_OFFSET(offset));
}
void
host_arm_BLX(codeblock_t *block, int addr_reg)
{
    codegen_addlong(block, OPCODE_BLX | Rm(addr_reg));
}

uint32_t *
host_arm_BCC_(codeblock_t *block)
{
    codegen_addlong(block, COND_CC | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BCS_(codeblock_t *block)
{
    codegen_addlong(block, COND_CS | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BEQ_(codeblock_t *block)
{
    codegen_addlong(block, COND_EQ | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BGE_(codeblock_t *block)
{
    codegen_addlong(block, COND_GE | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BGT_(codeblock_t *block)
{
    codegen_addlong(block, COND_GT | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BHI_(codeblock_t *block)
{
    codegen_addlong(block, COND_HI | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BLE_(codeblock_t *block)
{
    codegen_addlong(block, COND_LE | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BLS_(codeblock_t *block)
{
    codegen_addlong(block, COND_LS | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BLT_(codeblock_t *block)
{
    codegen_addlong(block, COND_LT | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BMI_(codeblock_t *block)
{
    codegen_addlong(block, COND_MI | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BNE_(codeblock_t *block)
{
    codegen_addlong(block, COND_NE | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BPL_(codeblock_t *block)
{
    codegen_addlong(block, COND_PL | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BVC_(codeblock_t *block)
{
    codegen_addlong(block, COND_VC | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_arm_BVS_(codeblock_t *block)
{
    codegen_addlong(block, COND_VS | OPCODE_B);

    return (uint32_t *) &block_write_data[block_pos - 4];
}

void
host_arm_BEQ(codeblock_t *block, uintptr_t dest_addr)
{
    uint32_t offset;

    codegen_alloc_4(block);
    offset = (dest_addr - (uintptr_t) &block_write_data[block_pos]) - 8;

    if ((offset & 0xfe000000) && (offset & 0xfe000000) != 0xfe000000)
        fatal("host_arm_BEQ - out of range %08x %i\n", offset, offset);

    codegen_addlong(block, COND_EQ | OPCODE_B | B_OFFSET(offset));
}
void
host_arm_BNE(codeblock_t *block, uintptr_t dest_addr)
{
    uint32_t offset;

    codegen_alloc_4(block);
    offset = (dest_addr - (uintptr_t) &block_write_data[block_pos]) - 8;

    if ((offset & 0xfe000000) && (offset & 0xfe000000) != 0xfe000000)
        fatal("host_arm_BNE - out of range %08x %i\n", offset, offset);

    codegen_addlong(block, COND_NE | OPCODE_B | B_OFFSET(offset));
}

void
host_arm_BX(codeblock_t *block, int addr_reg)
{
    codegen_addlong(block, OPCODE_BLX | Rm(addr_reg));
}

void
host_arm_CMN_IMM(codeblock_t *block, int src_reg, uint32_t imm)
{
    uint32_t arm_imm;

    if ((int32_t) imm < 0 && imm != 0x80000000) {
        host_arm_CMP_IMM(block, src_reg, -(int32_t) imm);
    } else if (get_arm_imm(imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_CMN_IMM | Rn(src_reg) | arm_imm);
    } else {
        host_arm_MOV_IMM(block, REG_TEMP, imm);
        host_arm_CMN_REG_LSL(block, src_reg, REG_TEMP, 0);
    }
}
void
host_arm_CMN_REG_LSL(codeblock_t *block, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_CMN_REG | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSL_IMM(shift));
}

void
host_arm_CMP_IMM(codeblock_t *block, int src_reg, uint32_t imm)
{
    uint32_t arm_imm;

    if ((int32_t) imm < 0 && imm != 0x80000000) {
        host_arm_CMN_IMM(block, src_reg, -(int32_t) imm);
    } else if (get_arm_imm(imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_CMP_IMM | Rn(src_reg) | arm_imm);
    } else {
        host_arm_MOV_IMM(block, REG_TEMP, imm);
        host_arm_CMP_REG_LSL(block, src_reg, REG_TEMP, 0);
    }
}
void
host_arm_CMP_REG_LSL(codeblock_t *block, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_CMP_REG | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSL_IMM(shift));
}

void
host_arm_EOR_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm)
{
    uint32_t arm_imm;

    if (get_arm_imm(imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_EOR_IMM | Rd(dst_reg) | Rn(src_reg) | arm_imm);
    } else {
        host_arm_MOV_IMM(block, REG_TEMP, imm);
        host_arm_EOR_REG_LSL(block, dst_reg, src_reg, REG_TEMP, 0);
    }
}

void
host_arm_EOR_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_EOR_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSL_IMM(shift));
}

void
host_arm_LDMIA_WB(codeblock_t *block, int addr_reg, uint32_t reg_mask)
{
    codegen_addlong(block, COND_AL | OPCODE_LDMIA_WB | Rn(addr_reg) | reg_mask);
}

void
host_arm_LDR_IMM(codeblock_t *block, int dst_reg, int addr_reg, int offset)
{
    codegen_addlong(block, COND_AL | OPCODE_LDR_IMM | Rn(addr_reg) | Rd(dst_reg) | arm_data_offset(offset));
}
void
host_arm_LDR_IMM_POST(codeblock_t *block, int dst_reg, int addr_reg, int offset)
{
    codegen_addlong(block, COND_AL | OPCODE_LDR_IMM_POST | Rn(addr_reg) | Rd(dst_reg) | arm_data_offset(offset));
}
void
host_arm_LDR_REG_LSL(codeblock_t *block, int dst_reg, int addr_reg, int offset_reg, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_LDR_REG | Rn(addr_reg) | Rd(dst_reg) | Rm(offset_reg) | SHIFT_LSL_IMM(shift));
}

void
host_arm_LDRB_ABS(codeblock_t *block, int dst_reg, void *p)
{
    if (in_range(p, &cpu_state))
        host_arm_LDRB_IMM(block, dst_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
    else
        fatal("LDRB_ABS - not in range\n");
}
void
host_arm_LDRB_IMM(codeblock_t *block, int dst_reg, int addr_reg, int offset)
{
    codegen_addlong(block, COND_AL | OPCODE_LDRB_IMM | Rn(addr_reg) | Rd(dst_reg) | arm_data_offset(offset));
}
void
host_arm_LDRB_REG_LSL(codeblock_t *block, int dst_reg, int addr_reg, int offset_reg, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_LDRB_REG | Rn(addr_reg) | Rd(dst_reg) | Rm(offset_reg) | SHIFT_LSL_IMM(shift));
}

void
host_arm_LDRH_IMM(codeblock_t *block, int dst_reg, int addr_reg, int offset)
{
    codegen_addlong(block, COND_AL | OPCODE_LDRH_IMM | Rn(addr_reg) | Rd(dst_reg) | LDRH_IMM(offset));
}
void
host_arm_LDRH_REG(codeblock_t *block, int dst_reg, int addr_reg, int offset_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_LDRH_REG | Rn(addr_reg) | Rd(dst_reg) | Rm(offset_reg));
}

void
host_arm_MOV_IMM(codeblock_t *block, int dst_reg, uint32_t imm)
{
    uint32_t arm_imm;

    if (get_arm_imm(imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_MOV_IMM | Rd(dst_reg) | arm_imm);
    } else {
        host_arm_MOVW_IMM(block, dst_reg, imm & 0xffff);
        if (imm >> 16)
            host_arm_MOVT_IMM(block, dst_reg, imm >> 16);
    }
}

void
host_arm_MOV_REG_ASR(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_MOV_REG | Rd(dst_reg) | Rm(src_reg) | SHIFT_ASR_IMM(shift));
}
void
host_arm_MOV_REG_ASR_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_MOV_REG | Rd(dst_reg) | Rm(src_reg) | SHIFT_ASR_REG(shift_reg));
}
void
host_arm_MOV_REG_LSL(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_MOV_REG | Rd(dst_reg) | Rm(src_reg) | SHIFT_LSL_IMM(shift));
}
void
host_arm_MOV_REG_LSL_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_MOV_REG | Rd(dst_reg) | Rm(src_reg) | SHIFT_LSL_REG(shift_reg));
}
void
host_arm_MOV_REG_LSR(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_MOV_REG | Rd(dst_reg) | Rm(src_reg) | SHIFT_LSR_IMM(shift));
}
void
host_arm_MOV_REG_LSR_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_MOV_REG | Rd(dst_reg) | Rm(src_reg) | SHIFT_LSR_REG(shift_reg));
}
void
host_arm_MOV_REG_ROR(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_MOV_REG | Rd(dst_reg) | Rm(src_reg) | SHIFT_ROR_IMM(shift));
}
void
host_arm_MOV_REG_ROR_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_MOV_REG | Rd(dst_reg) | Rm(src_reg) | SHIFT_ROR_REG(shift_reg));
}

void
host_arm_MOVT_IMM(codeblock_t *block, int dst_reg, uint16_t imm)
{
    codegen_addlong(block, COND_AL | OPCODE_MOVT_IMM | Rd(dst_reg) | MOVT_IMM(imm));
}
void
host_arm_MOVW_IMM(codeblock_t *block, int dst_reg, uint16_t imm)
{
    codegen_addlong(block, COND_AL | OPCODE_MOVW_IMM | Rd(dst_reg) | MOVW_IMM(imm));
}

void
host_arm_MVN_REG_LSL(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_MVN_REG | Rd(dst_reg) | Rm(src_reg) | SHIFT_LSL_IMM(shift));
}

void
host_arm_ORR_IMM_cond(codeblock_t *block, uint32_t cond, int dst_reg, int src_reg, uint32_t imm)
{
    uint32_t arm_imm;

    if (get_arm_imm(imm, &arm_imm)) {
        codegen_addlong(block, cond | OPCODE_ORR_IMM | Rd(dst_reg) | Rn(src_reg) | arm_imm);
    } else {
        host_arm_MOV_IMM(block, REG_TEMP, imm);
        host_arm_ORR_REG_LSL_cond(block, cond, dst_reg, src_reg, REG_TEMP, 0);
    }
}

void
host_arm_ORR_REG_LSL_cond(codeblock_t *block, uint32_t cond, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, cond | OPCODE_ORR_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSL_IMM(shift));
}

void
host_arm_RSB_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm)
{
    uint32_t arm_imm;

    if (get_arm_imm(imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_RSB_IMM | Rd(dst_reg) | Rn(src_reg) | arm_imm);
    } else {
        host_arm_MOV_IMM(block, REG_TEMP, imm);
        host_arm_RSB_REG_LSL(block, dst_reg, src_reg, REG_TEMP, 0);
    }
}
void
host_arm_RSB_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_RSB_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSL_IMM(shift));
}
void
host_arm_RSB_REG_LSR(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_RSB_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSR_IMM(shift));
}

void
host_arm_STMDB_WB(codeblock_t *block, int addr_reg, uint32_t reg_mask)
{
    codegen_addlong(block, COND_AL | OPCODE_STMDB_WB | Rn(addr_reg) | reg_mask);
}

void
host_arm_STR_IMM(codeblock_t *block, int src_reg, int addr_reg, int offset)
{
    codegen_addlong(block, COND_AL | OPCODE_STR_IMM | Rn(addr_reg) | Rd(src_reg) | arm_data_offset(offset));
}
void
host_arm_STR_IMM_WB(codeblock_t *block, int src_reg, int addr_reg, int offset)
{
    codegen_addlong(block, COND_AL | OPCODE_STR_IMM_WB | Rn(addr_reg) | Rd(src_reg) | arm_data_offset(offset));
}
void
host_arm_STR_REG_LSL(codeblock_t *block, int src_reg, int addr_reg, int offset_reg, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_STR_REG | Rn(addr_reg) | Rd(src_reg) | Rm(offset_reg) | SHIFT_LSL_IMM(shift));
}

void
host_arm_STRB_IMM(codeblock_t *block, int src_reg, int addr_reg, int offset)
{
    codegen_addlong(block, COND_AL | OPCODE_STRB_IMM | Rn(addr_reg) | Rd(src_reg) | arm_data_offset(offset));
}
void
host_arm_STRB_REG_LSL(codeblock_t *block, int src_reg, int addr_reg, int offset_reg, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_STRB_REG | Rn(addr_reg) | Rd(src_reg) | Rm(offset_reg) | SHIFT_LSL_IMM(shift));
}

void
host_arm_STRH_IMM(codeblock_t *block, int dst_reg, int addr_reg, int offset)
{
    codegen_addlong(block, COND_AL | OPCODE_STRH_IMM | Rn(addr_reg) | Rd(dst_reg) | STRH_IMM(offset));
}
void
host_arm_STRH_REG(codeblock_t *block, int src_reg, int addr_reg, int offset_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_STRH_REG | Rn(addr_reg) | Rd(src_reg) | Rm(offset_reg));
}

void
host_arm_SUB_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm)
{
    uint32_t arm_imm;

    if ((int32_t) imm < 0 && imm != 0x80000000) {
        host_arm_ADD_IMM(block, dst_reg, src_reg, -(int32_t) imm);
    } else if (get_arm_imm(imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_SUB_IMM | Rd(dst_reg) | Rn(src_reg) | arm_imm);
    } else {
        host_arm_MOV_IMM(block, REG_TEMP, imm);
        host_arm_SUB_REG_LSL(block, dst_reg, src_reg, REG_TEMP, 0);
    }
}

void
host_arm_SUB_REG_LSL(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_SUB_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSL_IMM(shift));
}
void
host_arm_SUB_REG_LSR(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m, int shift)
{
    codegen_addlong(block, COND_AL | OPCODE_SUB_REG | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m) | SHIFT_LSR_IMM(shift));
}

void
host_arm_SXTB(codeblock_t *block, int dst_reg, int src_reg, int rotate)
{
    codegen_addlong(block, OPCODE_SXTB | Rd(dst_reg) | Rm(src_reg) | UXTB_ROTATE(rotate));
}
void
host_arm_SXTH(codeblock_t *block, int dst_reg, int src_reg, int rotate)
{
    codegen_addlong(block, OPCODE_SXTH | Rd(dst_reg) | Rm(src_reg) | UXTB_ROTATE(rotate));
}

void
host_arm_TST_IMM(codeblock_t *block, int src_reg, uint32_t imm)
{
    uint32_t arm_imm;

    if (get_arm_imm(imm, &arm_imm)) {
        codegen_addlong(block, COND_AL | OPCODE_TST_IMM | Rn(src_reg) | arm_imm);
    } else {
        host_arm_MOV_IMM(block, REG_TEMP, imm);
        host_arm_TST_REG(block, src_reg, REG_TEMP);
    }
}
void
host_arm_TST_REG(codeblock_t *block, int src_reg1, int src_reg2)
{
    codegen_addlong(block, COND_AL | OPCODE_TST_REG | Rn(src_reg1) | Rm(src_reg2));
}

void
host_arm_UADD8(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b)
{
    codegen_addlong(block, COND_AL | OPCODE_UADD8 | Rd(dst_reg) | Rn(src_reg_a) | Rm(src_reg_b));
}

void
host_arm_UADD16(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b)
{
    codegen_addlong(block, COND_AL | OPCODE_UADD16 | Rd(dst_reg) | Rn(src_reg_a) | Rm(src_reg_b));
}

void
host_arm_USUB8(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b)
{
    codegen_addlong(block, COND_AL | OPCODE_USUB8 | Rd(dst_reg) | Rn(src_reg_a) | Rm(src_reg_b));
}

void
host_arm_USUB16(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b)
{
    codegen_addlong(block, COND_AL | OPCODE_USUB16 | Rd(dst_reg) | Rn(src_reg_a) | Rm(src_reg_b));
}

void
host_arm_UXTB(codeblock_t *block, int dst_reg, int src_reg, int rotate)
{
    codegen_addlong(block, OPCODE_UXTB | Rd(dst_reg) | Rm(src_reg) | UXTB_ROTATE(rotate));
}

void
host_arm_UXTH(codeblock_t *block, int dst_reg, int src_reg, int rotate)
{
    codegen_addlong(block, OPCODE_UXTH | Rd(dst_reg) | Rm(src_reg) | UXTB_ROTATE(rotate));
}

void
host_arm_VABS_D(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VABS_D | Vd(dest_reg) | Vm(src_reg));
}

void
host_arm_VADD_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, COND_AL | OPCODE_VADD | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VADD_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, COND_AL | OPCODE_VADD_F32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VADD_I8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VADD_I8 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VADD_I16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VADD_I16 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VADD_I32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VADD_I32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VAND_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VAND_D | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VBIC_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VBIC_D | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VCMP_D(codeblock_t *block, int src_reg_d, int src_reg_m)
{
    codegen_addlong(block, COND_AL | OPCODE_VCMP_D | Rd(src_reg_d) | Rm(src_reg_m));
}

void
host_arm_VCEQ_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VCEQ_F32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VCEQ_I8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VCEQ_I8 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VCEQ_I16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VCEQ_I16 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VCEQ_I32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VCEQ_I32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VCGE_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VCGE_F32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VCGT_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VCGT_F32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VCGT_S8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VCGT_S8 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VCGT_S16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VCGT_S16 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VCGT_S32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VCGT_S32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}

void
host_arm_VCVT_D_IS(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VCVT_D_IS | Vd(dest_reg) | Vm(src_reg));
}
void
host_arm_VCVT_D_S(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VCVT_D_S | Vd(dest_reg) | Vm(src_reg));
}
void
host_arm_VCVT_F32_S32(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VCVT_F32_S32 | Vd(dest_reg) | Vm(src_reg));
}
void
host_arm_VCVT_IS_D(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VCVT_IS_D | Vd(dest_reg) | Vm(src_reg));
}
void
host_arm_VCVT_S32_F32(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VCVT_S32_F32 | Vd(dest_reg) | Vm(src_reg));
}
void
host_arm_VCVT_S_D(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VCVT_S_D | Vd(dest_reg) | Vm(src_reg));
}
void
host_arm_VCVTR_IS_D(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VCVTR_IS_D | Vd(dest_reg) | Vm(src_reg));
}
void
host_arm_VDIV_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, COND_AL | OPCODE_VDIV | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VDIV_S(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, COND_AL | OPCODE_VDIV_S | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VDUP_32(codeblock_t *block, int dst_reg, int src_reg_m, int imm)
{
    codegen_addlong(block, COND_AL | OPCODE_VDUP_32 | Rd(dst_reg) | Rm(src_reg_m) | VDUP_32_IMM(imm));
}
void
host_arm_VEOR_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VEOR_D | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VLDR_D(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
    if ((offset > 1020) || (offset & 3))
        fatal("VLDR_D bad offset %i\n", offset);
    codegen_addlong(block, COND_AL | OPCODE_VLDR_D | Rd(dest_reg) | Rn(base_reg) | (offset >> 2));
}
void
host_arm_VLDR_S(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
    if ((offset > 1020) || (offset & 3))
        fatal("VLDR_S bad offset %i\n", offset);
    codegen_addlong(block, COND_AL | OPCODE_VLDR_S | Rd(dest_reg) | Rn(base_reg) | (offset >> 2));
}
void
host_arm_VMOV_32_S(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VMOV_32_S | Rt(dest_reg) | Vn(src_reg));
}
void
host_arm_VMOV_64_D(codeblock_t *block, int dest_reg_low, int dest_reg_high, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VMOV_64_D | Rt(dest_reg_low) | Rt2(dest_reg_high) | Vm(src_reg));
}
void
host_arm_VMOV_D_64(codeblock_t *block, int dest_reg, int src_reg_low, int src_reg_high)
{
    codegen_addlong(block, COND_AL | OPCODE_VMOV_D_64 | Vm(dest_reg) | Rt(src_reg_low) | Rt2(src_reg_high));
}
void
host_arm_VMOV_S_32(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VMOV_S_32 | Vn(dest_reg) | Rt(src_reg));
}
void
host_arm_VMOV_D_D(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VMOV_D_D | Vd(dest_reg) | Vm(src_reg));
}
void
host_arm_VMOVN_I32(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_VMOVN_I32 | Vd(dest_reg) | Vm(src_reg));
}
void
host_arm_VMOVN_I64(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_VMOVN_I64 | Vd(dest_reg) | Vm(src_reg));
}
void
host_arm_VMOV_F32_ONE(codeblock_t *block, int dst_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VMOV_F32_ONE | Rd(dst_reg));
}
void
host_arm_VMSR_FPSCR(codeblock_t *block, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VMSR_FPSCR | Rd(src_reg));
}
void
host_arm_VMRS_APSR(codeblock_t *block)
{
    codegen_addlong(block, COND_AL | OPCODE_VMRS_APSR);
}

void
host_arm_VMAX_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VMAX_F32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VMIN_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VMIN_F32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}

void
host_arm_VMUL_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, COND_AL | OPCODE_VMUL | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VMUL_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, COND_AL | OPCODE_VMUL_F32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VMUL_S16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VMUL_S16 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VMULL_S16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VMULL_S16 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}

void
host_arm_VNEG_D(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VNEG_D | Vd(dest_reg) | Vm(src_reg));
}

void
host_arm_VORR_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VORR_D | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}

void
host_arm_VPADDL_S16(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_VPADDL_S16 | Vd(dst_reg) | Vm(src_reg));
}
void
host_arm_VPADDL_S32(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_VPADDL_S32 | Vd(dst_reg) | Vm(src_reg));
}
void
host_arm_VPADDL_Q_S32(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_VPADDL_Q_S32 | Vd(dst_reg) | Vm(src_reg));
}

void
host_arm_VQADD_S8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VQADD_S8 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VQADD_S16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VQADD_S16 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VQADD_U8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VQADD_U8 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VQADD_U16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VQADD_U16 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VQSUB_S8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VQSUB_S8 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VQSUB_S16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VQSUB_S16 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VQSUB_U8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VQSUB_U8 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VQSUB_U16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VQSUB_U16 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}

void
host_arm_VQMOVN_S16(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_VQMOVN_S16 | Vd(dst_reg) | Vm(src_reg));
}
void
host_arm_VQMOVN_S32(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_VQMOVN_S32 | Vd(dst_reg) | Vm(src_reg));
}
void
host_arm_VQMOVN_U16(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_VQMOVN_U16 | Vd(dst_reg) | Vm(src_reg));
}

void
host_arm_VSHL_D_IMM_16(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift > 15)
        fatal("host_arm_VSHL_D_IMM_16 : shift > 15\n");
    codegen_addlong(block, OPCODE_VSHL_D_IMM_16 | Vd(dst_reg) | Vm(src_reg) | VSHIFT_IMM(shift));
}
void
host_arm_VSHL_D_IMM_32(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift > 31)
        fatal("host_arm_VSHL_D_IMM_32 : shift > 31\n");
    codegen_addlong(block, OPCODE_VSHL_D_IMM_32 | Vd(dst_reg) | Vm(src_reg) | VSHIFT_IMM(shift));
}
void
host_arm_VSHL_D_IMM_64(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift > 63)
        fatal("host_arm_VSHL_D_IMM_64 : shift > 63\n");
    codegen_addlong(block, OPCODE_VSHL_D_IMM_64 | Vd(dst_reg) | Vm(src_reg) | VSHIFT_IMM(shift));
}
void
host_arm_VSHR_D_S16(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift > 15)
        fatal("host_arm_VSHR_SD_IMM_16 : shift > 15\n");
    codegen_addlong(block, OPCODE_VSHR_D_S16 | Vd(dst_reg) | Vm(src_reg) | VSHIFT_IMM(16 - shift));
}
void
host_arm_VSHR_D_S32(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift > 31)
        fatal("host_arm_VSHR_SD_IMM_32 : shift > 31\n");
    codegen_addlong(block, OPCODE_VSHR_D_S32 | Vd(dst_reg) | Vm(src_reg) | VSHIFT_IMM(32 - shift));
}
void
host_arm_VSHR_D_S64(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift > 63)
        fatal("host_arm_VSHR_SD_IMM_64 : shift > 63\n");
    codegen_addlong(block, OPCODE_VSHR_D_S64 | Vd(dst_reg) | Vm(src_reg) | VSHIFT_IMM(64 - shift));
}
void
host_arm_VSHR_D_U16(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift > 15)
        fatal("host_arm_VSHR_UD_IMM_16 : shift > 15\n");
    codegen_addlong(block, OPCODE_VSHR_D_U16 | Vd(dst_reg) | Vm(src_reg) | VSHIFT_IMM(16 - shift));
}
void
host_arm_VSHR_D_U32(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift > 31)
        fatal("host_arm_VSHR_UD_IMM_32 : shift > 31\n");
    codegen_addlong(block, OPCODE_VSHR_D_U32 | Vd(dst_reg) | Vm(src_reg) | VSHIFT_IMM(32 - shift));
}
void
host_arm_VSHR_D_U64(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift > 63)
        fatal("host_arm_VSHR_UD_IMM_64 : shift > 63\n");
    codegen_addlong(block, OPCODE_VSHR_D_U64 | Vd(dst_reg) | Vm(src_reg) | VSHIFT_IMM(64 - shift));
}
void
host_arm_VSHRN_32(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift > 16)
        fatal("host_arm_VSHRN_32 : shift > 16\n");
    codegen_addlong(block, OPCODE_VSHRN | Vd(dst_reg) | Vm(src_reg) | VSHIFT_IMM_32(16 - shift));
}

void
host_arm_VSQRT_D(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VSQRT_D | Vd(dest_reg) | Vm(src_reg));
}
void
host_arm_VSQRT_S(codeblock_t *block, int dest_reg, int src_reg)
{
    codegen_addlong(block, COND_AL | OPCODE_VSQRT_S | Vd(dest_reg) | Vm(src_reg));
}

void
host_arm_VSTR_D(codeblock_t *block, int src_reg, int base_reg, int offset)
{
    if ((offset > 1020) || (offset & 3))
        fatal("VSTR_D bad offset %i\n", offset);
    codegen_addlong(block, COND_AL | OPCODE_VSTR_D | Rd(src_reg) | Rn(base_reg) | (offset >> 2));
}
void
host_arm_VSTR_S(codeblock_t *block, int src_reg, int base_reg, int offset)
{
    if ((offset > 1020) || (offset & 3))
        fatal("VSTR_S bad offset %i\n", offset);
    codegen_addlong(block, COND_AL | OPCODE_VSTR_S | Rd(src_reg) | Rn(base_reg) | (offset >> 2));
}
void
host_arm_VSUB_D(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, COND_AL | OPCODE_VSUB | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VSUB_F32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, COND_AL | OPCODE_VSUB_F32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VSUB_I8(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VSUB_I8 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VSUB_I16(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VSUB_I16 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}
void
host_arm_VSUB_I32(codeblock_t *block, int dst_reg, int src_reg_n, int src_reg_m)
{
    codegen_addlong(block, OPCODE_VSUB_I32 | Rd(dst_reg) | Rn(src_reg_n) | Rm(src_reg_m));
}

void
host_arm_VZIP_D8(codeblock_t *block, int d_reg, int m_reg)
{
    codegen_addlong(block, OPCODE_VZIP_D8 | Vd(d_reg) | Vm(m_reg));
}
void
host_arm_VZIP_D16(codeblock_t *block, int d_reg, int m_reg)
{
    codegen_addlong(block, OPCODE_VZIP_D16 | Vd(d_reg) | Vm(m_reg));
}
void
host_arm_VZIP_D32(codeblock_t *block, int d_reg, int m_reg)
{
    codegen_addlong(block, OPCODE_VZIP_D32 | Vd(d_reg) | Vm(m_reg));
}

#endif

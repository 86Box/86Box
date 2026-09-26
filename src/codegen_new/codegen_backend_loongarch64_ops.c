#if defined __loongarch_lp64

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          LoongArch64 backend for the "new" dynamic recompiler -
 *          primitive emitters and the codegen_direct_* accessors.
 *
 *          Encodings follow the binutils 2.47 opcode table (plan section
 *          7); instruction-selection patterns follow QEMU's TCG LoongArch
 *          backend (plan section 8).
 */

#    include <inttypes.h>
#    include <stdint.h>
#    include <stdlib.h>
#    include <string.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat_unused.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_loongarch64_defs.h"
#    include "codegen_backend_loongarch64.h"
#    include "codegen_ir_defs.h"

#    define Rd(x)                    (x)
#    define Rj(x)                    ((x) << 5)
#    define Rk(x)                    ((x) << 10)

#    define IMM12(x)                 (((x) & 0xfff) << 10)
#    define IMM20(x)                 (((x) & 0xfffff) << 5)
#    define IMM14_LDPT(x)            (((x) & 0x3fff) << 10)
#    define UIMM10_5(x)              (((x) & 0x1f) << 10)
#    define UIMM10_6(x)              (((x) & 0x3f) << 10)
#    define BSTR_MSB_W(x)            (((x) & 0x1f) << 16)
#    define BSTR_LSB_W(x)            (((x) & 0x1f) << 10)
#    define BSTR_MSB_D(x)            (((x) & 0x3f) << 16)
#    define BSTR_LSB_D(x)            (((x) & 0x3f) << 10)
#    define ALSL_SA2(x)              ((((x) -1) & 3) << 15)

/*DJSk16 conditional branches (plan section 7.5).*/
#    define OPCODE_BEQ               0x58000000
#    define OPCODE_BNE               0x5c000000
#    define OPCODE_BLT               0x60000000
#    define OPCODE_BGE               0x64000000
#    define OPCODE_BLTU              0x68000000
#    define OPCODE_BGEU              0x6c000000
#    define OPCODE_B                 0x50000000
#    define OPCODE_BL                0x54000000
#    define OPCODE_JIRL              0x4c000000

/*3-register ALU (plan section 7.1).*/
#    define OPCODE_ADD_W             0x00100000
#    define OPCODE_ADD_D             0x00108000
#    define OPCODE_SUB_W             0x00110000
#    define OPCODE_SUB_D             0x00118000
#    define OPCODE_SLT               0x00120000
#    define OPCODE_SLTU              0x00128000
#    define OPCODE_NOR               0x00140000
#    define OPCODE_AND               0x00148000
#    define OPCODE_OR                0x00150000
#    define OPCODE_XOR               0x00158000
#    define OPCODE_SLL_W             0x00170000
#    define OPCODE_SRL_W             0x00178000
#    define OPCODE_SRA_W             0x00180000
#    define OPCODE_SLL_D             0x00188000
#    define OPCODE_SRL_D             0x00190000
#    define OPCODE_SRA_D             0x00198000
#    define OPCODE_ROTR_W            0x001b0000
#    define OPCODE_ROTR_D            0x001b8000
#    define OPCODE_ALSL_D            0x002c0000

/*Register formats with only two operands (plan section 7.2).*/
#    define OPCODE_EXT_W_H           0x00005800
#    define OPCODE_EXT_W_B           0x00005c00
#    define OPCODE_BSTRINS_W         0x00600000
#    define OPCODE_BSTRPICK_W        0x00608000
#    define OPCODE_BSTRINS_D         0x00800000
#    define OPCODE_BSTRPICK_D        0x00c00000

/*Immediates (plan section 7.3).*/
#    define OPCODE_SLTI              0x02000000
#    define OPCODE_SLTUI             0x02400000
#    define OPCODE_ADDI_W            0x02800000
#    define OPCODE_ADDI_D            0x02c00000
#    define OPCODE_LU52I_D           0x03000000
#    define OPCODE_ANDI              0x03400000
#    define OPCODE_ORI               0x03800000
#    define OPCODE_XORI              0x03c00000
#    define OPCODE_LU12I_W           0x14000000
#    define OPCODE_LU32I_D           0x16000000

#    define OPCODE_SLLI_W            0x00408000
#    define OPCODE_SLLI_D            0x00410000
#    define OPCODE_SRLI_W            0x00448000
#    define OPCODE_SRLI_D            0x00450000
#    define OPCODE_SRAI_W            0x00488000
#    define OPCODE_SRAI_D            0x00490000
#    define OPCODE_ROTRI_W           0x004c8000
#    define OPCODE_ROTRI_D           0x004d0000

/*Loads / stores (plan section 7.4).*/
#    define OPCODE_LD_B              0x28000000
#    define OPCODE_LD_H              0x28400000
#    define OPCODE_LD_W              0x28800000
#    define OPCODE_LD_D              0x28c00000
#    define OPCODE_ST_B              0x29000000
#    define OPCODE_ST_H              0x29400000
#    define OPCODE_ST_W              0x29800000
#    define OPCODE_ST_D              0x29c00000
#    define OPCODE_LD_BU             0x2a000000
#    define OPCODE_LD_HU             0x2a400000
#    define OPCODE_LD_WU             0x2a800000
#    define OPCODE_LDPTR_W           0x24000000
#    define OPCODE_STPTR_W           0x25000000
#    define OPCODE_LDPTR_D           0x26000000
#    define OPCODE_STPTR_D           0x27000000
#    define OPCODE_LDX_B             0x38000000
#    define OPCODE_LDX_H             0x38040000
#    define OPCODE_LDX_W             0x38080000
#    define OPCODE_LDX_D             0x380c0000
#    define OPCODE_STX_B             0x38100000
#    define OPCODE_STX_H             0x38140000
#    define OPCODE_STX_W             0x38180000
#    define OPCODE_STX_D             0x381c0000
#    define OPCODE_LDX_BU            0x38200000
#    define OPCODE_LDX_HU            0x38240000
#    define OPCODE_LDX_WU            0x38280000

/*FP loads/stores and GPR<->FPR moves (fld/fst use si12 only).*/
#    define OPCODE_FLD_S             0x2b000000
#    define OPCODE_FLD_D             0x2b800000
#    define OPCODE_FST_S             0x2b400000
#    define OPCODE_FST_D             0x2bc00000
#    define OPCODE_MOVGR2FR_W        0x0114a400
#    define OPCODE_MOVGR2FR_D        0x0114a800
#    define OPCODE_MOVFR2GR_S        0x0114b400
#    define OPCODE_MOVFR2GR_D        0x0114b800

/*vori.b vd, vj, 0 - the LA64 register-register FP/vector move (LA64 has
  no fmov; the f regs alias the low 64 bits of the v regs).*/
#    define OPCODE_VORI_B            0x73d40000

#    define OPCODE_NOP               0x03400000

/*Branch offsets are byte deltas; SK16 fields hold delta>>2 in bits
  [25:10], B/BL split delta>>2 across [25:10] and [9:0].*/
#    define BR16(x)                  ((((x) >> 2) & 0xffff) << 10)
#    define BR26(x)                  (((((x) >> 2) & 0xffff) << 10) | (((x) >> 18) & 0x3ff))

/*Invert a DJSk16 condition: BEQ<->BNE, BLT<->BGE, BLTU<->BGEU all differ
  in bit 26.*/
#    define BRANCH_INVERT(op)        ((op) ^ 0x04000000)

/*Signed 12-bit si12 displacement range: -2048..2047 (negative offsets
  allowed, unlike arm64's unsigned imm12).*/
#define IN_RANGE_SI12(off)  ((off) >= -0x800 && (off) <= 0x7ff)
/*ldptr/stptr displacement: signed 16-bit byte range, 4-aligned.*/
#define IN_RANGE_LDPTR(off) (!(((off) &3)) && (off) >= -0x8000 && (off) <= 0x7ffc)

/*SK16 conditional branch range: +-128 KiB, 4-aligned.*/
static inline int
can_branch_sk16(const uint8_t *src_insn_addr, const void *dst)
{
    intptr_t offset = (intptr_t) dst - (intptr_t) src_insn_addr;

    if (offset & 3)
        return 0;
    return offset >= -(1 << 15) && offset <= ((1 << 15) - 4);
}

static void codegen_allocate_new_block(codeblock_t *block);
static inline uint32_t br26_packed(intptr_t offset);

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
    uint8_t            *jump_src  = &block_write_data[block_pos];
    intptr_t            offset    = (uintptr_t) new_ptr - (uintptr_t) jump_src;

    if (!codegen_allocator_can_branch_imm26(jump_src, new_ptr))
        fatal("codegen_allocate_new_block - offset out of range %" PRIxPTR "\n", (uintptr_t) offset);
    /*Add a jump instruction to the new block. B takes the 26-bit layout
      (imm[15:0] at bits[25:10], imm[25:16] at bits[9:0]); BR16 packs a
      16-bit conditional-branch field, so any bridge offset >= 256KB
      (recycled continuation slots can sit megabytes away) is truncated
      and the jump lands in never-allocated JIT memory -> SIGILL. */
    *(uint32_t *) &block_write_data[block_pos] = OPCODE_B | br26_packed(offset);

    /*Set write address to start of new block*/
    block_pos        = 0;
    block_write_data = new_ptr;
}

void
codegen_alloc(codeblock_t *block, int size)
{
    if (block_pos >= (BLOCK_MAX - size))
        codegen_allocate_new_block(block);
}

/*Raw immediate encoders.*/

void
host_loong64_ANDI(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data)
{
    codegen_addlong(block, OPCODE_ANDI | Rd(dst_reg) | Rj(src_reg) | IMM12(imm_data));
}
void
host_loong64_ORI(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data)
{
    codegen_addlong(block, OPCODE_ORI | Rd(dst_reg) | Rj(src_reg) | IMM12(imm_data));
}
void
host_loong64_ADDI_W(codeblock_t *block, int dst_reg, int src_reg, int32_t imm_data)
{
    codegen_addlong(block, OPCODE_ADDI_W | Rd(dst_reg) | Rj(src_reg) | IMM12(imm_data));
}
void
host_loong64_ADDI_D(codeblock_t *block, int dst_reg, int src_reg, int32_t imm_data)
{
    codegen_addlong(block, OPCODE_ADDI_D | Rd(dst_reg) | Rj(src_reg) | IMM12(imm_data));
}
void
host_loong64_LU12I_W(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    codegen_addlong(block, OPCODE_LU12I_W | Rd(dst_reg) | IMM20(imm_data));
}
void
host_loong64_LU32I_D(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    codegen_addlong(block, OPCODE_LU32I_D | Rd(dst_reg) | IMM20(imm_data));
}
void
host_loong64_LU52I_D(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data)
{
    codegen_addlong(block, OPCODE_LU52I_D | Rd(dst_reg) | Rj(src_reg) | IMM12(imm_data));
}

/*Moves / field ops.*/

void
host_loong64_MOV_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    if (dst_reg != src_reg)
        codegen_addlong(block, OPCODE_OR | Rd(dst_reg) | Rj(src_reg) | Rk(REG_ZERO));
}
void
host_loong64_MOV_W(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_ADDI_W | Rd(dst_reg) | Rj(src_reg) | IMM12(0));
}
void
host_loong64_BFI_W(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width)
{
    if (width < 1 || lsb < 0 || lsb + width > 32)
        fatal("host_loong64_BFI_W - bad field lsb=%i width=%i\n", lsb, width);
    codegen_addlong(block, OPCODE_BSTRINS_W | Rd(dst_reg) | Rj(src_reg) | BSTR_MSB_W(lsb + width - 1) | BSTR_LSB_W(lsb));
}
void
host_loong64_BFI_D(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width)
{
    if (width < 1 || lsb < 0 || lsb + width > 64)
        fatal("host_loong64_BFI_D - bad field lsb=%i width=%i\n", lsb, width);
    codegen_addlong(block, OPCODE_BSTRINS_D | Rd(dst_reg) | Rj(src_reg) | BSTR_MSB_D(lsb + width - 1) | BSTR_LSB_D(lsb));
}
void
host_loong64_UBFX_D(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width)
{
    if (width < 1 || lsb < 0 || lsb + width > 64)
        fatal("host_loong64_UBFX_D - bad field lsb=%i width=%i\n", lsb, width);
    codegen_addlong(block, OPCODE_BSTRPICK_D | Rd(dst_reg) | Rj(src_reg) | BSTR_MSB_D(lsb + width - 1) | BSTR_LSB_D(lsb));
}
void
host_loong64_SEXT_B(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_EXT_W_B | Rd(dst_reg) | Rj(src_reg));
}
void
host_loong64_SEXT_H(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_EXT_W_H | Rd(dst_reg) | Rj(src_reg));
}

/*Integer ALU.*/

void
host_loong64_ADD_W_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg)
{
    codegen_addlong(block, OPCODE_ADD_W | Rd(dst_reg) | Rj(src_a_reg) | Rk(src_b_reg));
}
void
host_loong64_SUB_W_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg)
{
    codegen_addlong(block, OPCODE_SUB_W | Rd(dst_reg) | Rj(src_a_reg) | Rk(src_b_reg));
}
void
host_loong64_ADDX_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg)
{
    codegen_addlong(block, OPCODE_ADD_D | Rd(dst_reg) | Rj(src_a_reg) | Rk(src_b_reg));
}
void
host_loong64_SUBX_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg)
{
    codegen_addlong(block, OPCODE_SUB_D | Rd(dst_reg) | Rj(src_a_reg) | Rk(src_b_reg));
}
void
host_loong64_ALSL_D(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg, int shift)
{
    if (shift < 1 || shift > 4)
        fatal("host_loong64_ALSL_D - bad shift %i\n", shift);
    codegen_addlong(block, OPCODE_ALSL_D | Rd(dst_reg) | Rj(src_a_reg) | Rk(src_b_reg) | ALSL_SA2(shift));
}
void
host_loong64_AND_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg)
{
    codegen_addlong(block, OPCODE_AND | Rd(dst_reg) | Rj(src_a_reg) | Rk(src_b_reg));
}
void
host_loong64_OR_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg)
{
    codegen_addlong(block, OPCODE_OR | Rd(dst_reg) | Rj(src_a_reg) | Rk(src_b_reg));
}
void
host_loong64_XOR_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg)
{
    codegen_addlong(block, OPCODE_XOR | Rd(dst_reg) | Rj(src_a_reg) | Rk(src_b_reg));
}
/*slt/sltu operate on the full 64-bit registers - callers must canonicalise
  32-bit operands first (sext.w / bstrpick.d).*/
void
host_loong64_SLT(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg)
{
    codegen_addlong(block, OPCODE_SLT | Rd(dst_reg) | Rj(src_a_reg) | Rk(src_b_reg));
}
void
host_loong64_SLTU(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg)
{
    codegen_addlong(block, OPCODE_SLTU | Rd(dst_reg) | Rj(src_a_reg) | Rk(src_b_reg));
}
void
host_loong64_SLTI(codeblock_t *block, int dst_reg, int src_reg, int32_t imm_data)
{
    codegen_addlong(block, OPCODE_SLTI | Rd(dst_reg) | Rj(src_reg) | IMM12(imm_data));
}
void
host_loong64_SLTUI(codeblock_t *block, int dst_reg, int src_reg, int32_t imm_data)
{
    codegen_addlong(block, OPCODE_SLTUI | Rd(dst_reg) | Rj(src_reg) | IMM12(imm_data));
}

void
host_loong64_ADD_W_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data)
{
    int64_t imm = (int32_t) imm_data;

    if (!imm)
        host_loong64_MOV_W(block, dst_reg, src_reg);
    else if (imm >= -0x800 && imm <= 0x7ff)
        codegen_addlong(block, OPCODE_ADDI_W | Rd(dst_reg) | Rj(src_reg) | IMM12(imm));
    else {
        host_loong64_mov_imm_w(block, REG_TEMP2, imm_data);
        codegen_addlong(block, OPCODE_ADD_W | Rd(dst_reg) | Rj(src_reg) | Rk(REG_TEMP2));
    }
}
void
host_loong64_SUB_W_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data)
{
    int64_t imm = (int32_t) imm_data;

    if (!imm)
        host_loong64_MOV_W(block, dst_reg, src_reg);
    else if (imm >= -0x7ff && imm <= 0x800)
        codegen_addlong(block, OPCODE_ADDI_W | Rd(dst_reg) | Rj(src_reg) | IMM12(-imm));
    else {
        host_loong64_mov_imm_w(block, REG_TEMP2, imm_data);
        codegen_addlong(block, OPCODE_SUB_W | Rd(dst_reg) | Rj(src_reg) | Rk(REG_TEMP2));
    }
}
void
host_loong64_ADDX_IMM(codeblock_t *block, int dst_reg, int src_reg, uint64_t imm_data)
{
    if (!imm_data)
        host_loong64_MOV_REG(block, dst_reg, src_reg);
    else if ((int64_t) imm_data >= -0x800 && (int64_t) imm_data <= 0x7ff)
        codegen_addlong(block, OPCODE_ADDI_D | Rd(dst_reg) | Rj(src_reg) | IMM12(imm_data));
    else {
        host_loong64_mov_imm(block, REG_TEMP2, imm_data);
        codegen_addlong(block, OPCODE_ADD_D | Rd(dst_reg) | Rj(src_reg) | Rk(REG_TEMP2));
    }
}
void
host_loong64_SUBX_IMM(codeblock_t *block, int dst_reg, int src_reg, uint64_t imm_data)
{
    int64_t imm = (int64_t) imm_data;

    if (!imm)
        host_loong64_MOV_REG(block, dst_reg, src_reg);
    else if (imm >= -0x7ff && imm <= 0x800)
        codegen_addlong(block, OPCODE_ADDI_D | Rd(dst_reg) | Rj(src_reg) | IMM12(-imm));
    else {
        host_loong64_mov_imm(block, REG_TEMP2, imm_data);
        codegen_addlong(block, OPCODE_SUB_D | Rd(dst_reg) | Rj(src_reg) | Rk(REG_TEMP2));
    }
}
void
host_loong64_AND_IMM(codeblock_t *block, int dst_reg, int src_reg, uint64_t imm_data)
{
    if (imm_data <= 0xfff)
        host_loong64_ANDI(block, dst_reg, src_reg, (uint32_t) imm_data);
    else {
        host_loong64_mov_imm(block, REG_TEMP2, imm_data);
        host_loong64_AND_REG(block, dst_reg, src_reg, REG_TEMP2);
    }
}
void
host_loong64_OR_IMM(codeblock_t *block, int dst_reg, int src_reg, uint64_t imm_data)
{
    if (imm_data <= 0xfff)
        host_loong64_ORI(block, dst_reg, src_reg, (uint32_t) imm_data);
    else {
        host_loong64_mov_imm(block, REG_TEMP2, imm_data);
        host_loong64_OR_REG(block, dst_reg, src_reg, REG_TEMP2);
    }
}
void
host_loong64_XOR_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data)
{
    if (imm_data <= 0xfff)
        codegen_addlong(block, OPCODE_XORI | Rd(dst_reg) | Rj(src_reg) | IMM12(imm_data));
    else {
        host_loong64_mov_imm_w(block, REG_TEMP2, imm_data);
        host_loong64_XOR_REG(block, dst_reg, src_reg, REG_TEMP2);
    }
}

/*Shifts.*/

void
host_loong64_SHL_W_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift < 0 || shift > 31)
        fatal("host_loong64_SHL_W_IMM - shift %i out of range\n", shift);
    codegen_addlong(block, OPCODE_SLLI_W | Rd(dst_reg) | Rj(src_reg) | UIMM10_5(shift));
}
void
host_loong64_SHR_W_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift < 0 || shift > 31)
        fatal("host_loong64_SHR_W_IMM - shift %i out of range\n", shift);
    codegen_addlong(block, OPCODE_SRLI_W | Rd(dst_reg) | Rj(src_reg) | UIMM10_5(shift));
}
void
host_loong64_SAR_W_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift < 0 || shift > 31)
        fatal("host_loong64_SAR_W_IMM - shift %i out of range\n", shift);
    codegen_addlong(block, OPCODE_SRAI_W | Rd(dst_reg) | Rj(src_reg) | UIMM10_5(shift));
}
void
host_loong64_SHL_D_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift < 0 || shift > 63)
        fatal("host_loong64_SHL_D_IMM - shift %i out of range\n", shift);
    codegen_addlong(block, OPCODE_SLLI_D | Rd(dst_reg) | Rj(src_reg) | UIMM10_6(shift));
}
void
host_loong64_SHR_D_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift < 0 || shift > 63)
        fatal("host_loong64_SHR_D_IMM - shift %i out of range\n", shift);
    codegen_addlong(block, OPCODE_SRLI_D | Rd(dst_reg) | Rj(src_reg) | UIMM10_6(shift));
}
void
host_loong64_SAR_D_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift < 0 || shift > 63)
        fatal("host_loong64_SAR_D_IMM - shift %i out of range\n", shift);
    codegen_addlong(block, OPCODE_SRAI_D | Rd(dst_reg) | Rj(src_reg) | UIMM10_6(shift));
}

void
host_loong64_SHL_W_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, OPCODE_SLL_W | Rd(dst_reg) | Rj(src_reg) | Rk(shift_reg));
}
void
host_loong64_SHR_W_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, OPCODE_SRL_W | Rd(dst_reg) | Rj(src_reg) | Rk(shift_reg));
}
void
host_loong64_SAR_W_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, OPCODE_SRA_W | Rd(dst_reg) | Rj(src_reg) | Rk(shift_reg));
}
void
host_loong64_SHL_D_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, OPCODE_SLL_D | Rd(dst_reg) | Rj(src_reg) | Rk(shift_reg));
}
void
host_loong64_SHR_D_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, OPCODE_SRL_D | Rd(dst_reg) | Rj(src_reg) | Rk(shift_reg));
}
void
host_loong64_SAR_D_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, OPCODE_SRA_D | Rd(dst_reg) | Rj(src_reg) | Rk(shift_reg));
}

/*Register rotates (rotr.w/d take rk[4:0] - matching x86's 5-bit count mask
  for the 32-bit forms; the shift uops are only emitted with counts 1..31).*/
void
host_loong64_ROTR_W_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, OPCODE_ROTR_W | Rd(dst_reg) | Rj(src_reg) | Rk(shift_reg));
}
void
host_loong64_ROTR_D_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg)
{
    codegen_addlong(block, OPCODE_ROTR_D | Rd(dst_reg) | Rj(src_reg) | Rk(shift_reg));
}
void
host_loong64_ROTR_W_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift < 1 || shift > 31)
        fatal("host_loong64_ROTR_W_IMM - shift %i out of range\n", shift);
    codegen_addlong(block, OPCODE_ROTRI_W | Rd(dst_reg) | Rj(src_reg) | UIMM10_5(shift));
}
void
host_loong64_ROTR_D_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift)
{
    if (shift < 1 || shift > 63)
        fatal("host_loong64_ROTR_D_IMM - shift %i out of range\n", shift);
    codegen_addlong(block, OPCODE_ROTRI_D | Rd(dst_reg) | Rj(src_reg) | UIMM10_6(shift));
}

/*Loads / stores.*/

/*Selects si12, ldptr/stptr (4-aligned +-32 KiB) or mov_imm+add+ldx/stx.
  op_ldptr is 0 for the access sizes ldptr does not cover (b/h).*/
static void
emit_ld_gpr(codeblock_t *block, int op_si12, int op_ldptr, int op_ldx, int dst_reg, int base_reg, int64_t offset)
{
    if (IN_RANGE_SI12(offset)) {
        codegen_addlong(block, op_si12 | Rd(dst_reg) | Rj(base_reg) | IMM12(offset));
    } else if (op_ldptr && IN_RANGE_LDPTR(offset)) {
        codegen_addlong(block, op_ldptr | Rd(dst_reg) | Rj(base_reg) | IMM14_LDPT(offset >> 2));
    } else {
        host_loong64_mov_imm(block, REG_TEMP2, (uint64_t) offset);
        codegen_addlong(block, OPCODE_ADD_D | Rd(REG_TEMP2) | Rj(REG_TEMP2) | Rk(base_reg));
        codegen_addlong(block, op_ldx | Rd(dst_reg) | Rj(REG_TEMP2) | Rk(REG_ZERO));
    }
}

static void
emit_st_gpr(codeblock_t *block, int op_si12, int op_stptr, int op_stx, int src_reg, int base_reg, int64_t offset)
{
    if (IN_RANGE_SI12(offset)) {
        codegen_addlong(block, op_si12 | Rd(src_reg) | Rj(base_reg) | IMM12(offset));
    } else if (op_stptr && IN_RANGE_LDPTR(offset)) {
        codegen_addlong(block, op_stptr | Rd(src_reg) | Rj(base_reg) | IMM14_LDPT(offset >> 2));
    } else {
        host_loong64_mov_imm(block, REG_TEMP2, (uint64_t) offset);
        codegen_addlong(block, OPCODE_ADD_D | Rd(REG_TEMP2) | Rj(REG_TEMP2) | Rk(base_reg));
        codegen_addlong(block, op_stx | Rd(src_reg) | Rj(REG_TEMP2) | Rk(REG_ZERO));
    }
}

void
host_loong64_LDRB_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    emit_ld_gpr(block, OPCODE_LD_BU, 0, OPCODE_LDX_BU, dst_reg, base_reg, offset);
}
void
host_loong64_LDRSB_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    emit_ld_gpr(block, OPCODE_LD_B, 0, OPCODE_LDX_B, dst_reg, base_reg, offset);
}
void
host_loong64_LDRH_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    emit_ld_gpr(block, OPCODE_LD_HU, 0, OPCODE_LDX_HU, dst_reg, base_reg, offset);
}
void
host_loong64_LDRSH_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    emit_ld_gpr(block, OPCODE_LD_H, 0, OPCODE_LDX_H, dst_reg, base_reg, offset);
}
void
host_loong64_LDR_W_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    emit_ld_gpr(block, OPCODE_LD_WU, OPCODE_LDPTR_W, OPCODE_LDX_WU, dst_reg, base_reg, offset);
}
void
host_loong64_LDRSW_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    emit_ld_gpr(block, OPCODE_LD_W, OPCODE_LDPTR_W, OPCODE_LDX_W, dst_reg, base_reg, offset);
}
void
host_loong64_LDRX_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    emit_ld_gpr(block, OPCODE_LD_D, OPCODE_LDPTR_D, OPCODE_LDX_D, dst_reg, base_reg, offset);
}

void
host_loong64_LDRB_REG(codeblock_t *block, int dst_reg, int base_reg, int offset_reg)
{
    codegen_addlong(block, OPCODE_LDX_BU | Rd(dst_reg) | Rj(base_reg) | Rk(offset_reg));
}
void
host_loong64_LDRH_REG(codeblock_t *block, int dst_reg, int base_reg, int offset_reg)
{
    codegen_addlong(block, OPCODE_LDX_HU | Rd(dst_reg) | Rj(base_reg) | Rk(offset_reg));
}
void
host_loong64_LDR_REG(codeblock_t *block, int dst_reg, int base_reg, int offset_reg)
{
    codegen_addlong(block, OPCODE_LDX_WU | Rd(dst_reg) | Rj(base_reg) | Rk(offset_reg));
}
void
host_loong64_LDRX_REG(codeblock_t *block, int dst_reg, int base_reg, int offset_reg)
{
    codegen_addlong(block, OPCODE_LDX_D | Rd(dst_reg) | Rj(base_reg) | Rk(offset_reg));
}

void
host_loong64_STRB_IMM(codeblock_t *block, int src_reg, int base_reg, int offset)
{
    emit_st_gpr(block, OPCODE_ST_B, 0, OPCODE_STX_B, src_reg, base_reg, offset);
}
void
host_loong64_STRH_IMM(codeblock_t *block, int src_reg, int base_reg, int offset)
{
    emit_st_gpr(block, OPCODE_ST_H, 0, OPCODE_STX_H, src_reg, base_reg, offset);
}
void
host_loong64_STR_W_IMM(codeblock_t *block, int src_reg, int base_reg, int offset)
{
    emit_st_gpr(block, OPCODE_ST_W, OPCODE_STPTR_W, OPCODE_STX_W, src_reg, base_reg, offset);
}
void
host_loong64_STRX_IMM(codeblock_t *block, int src_reg, int base_reg, int offset)
{
    emit_st_gpr(block, OPCODE_ST_D, OPCODE_STPTR_D, OPCODE_STX_D, src_reg, base_reg, offset);
}

void
host_loong64_STRB_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg)
{
    codegen_addlong(block, OPCODE_STX_B | Rd(src_reg) | Rj(base_reg) | Rk(offset_reg));
}
void
host_loong64_STRH_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg)
{
    codegen_addlong(block, OPCODE_STX_H | Rd(src_reg) | Rj(base_reg) | Rk(offset_reg));
}
void
host_loong64_STR_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg)
{
    codegen_addlong(block, OPCODE_STX_W | Rd(src_reg) | Rj(base_reg) | Rk(offset_reg));
}
void
host_loong64_STRX_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg)
{
    codegen_addlong(block, OPCODE_STX_D | Rd(src_reg) | Rj(base_reg) | Rk(offset_reg));
}

/*FP loads/stores have no ldptr or indexed forms - si12 directly, or
  materialise the address into REG_TEMP2 and use offset 0.*/
static void
emit_fp_ldst(codeblock_t *block, int op, int freg, int base_reg, int64_t offset)
{
    if (IN_RANGE_SI12(offset)) {
        codegen_addlong(block, op | Rd(freg) | Rj(base_reg) | IMM12(offset));
    } else {
        host_loong64_mov_imm(block, REG_TEMP2, (uint64_t) offset);
        codegen_addlong(block, OPCODE_ADD_D | Rd(REG_TEMP2) | Rj(REG_TEMP2) | Rk(base_reg));
        codegen_addlong(block, op | Rd(freg) | Rj(REG_TEMP2) | IMM12(0));
    }
}

void
host_loong64_FLD_S_IMM(codeblock_t *block, int dst_freg, int base_reg, int offset)
{
    emit_fp_ldst(block, OPCODE_FLD_S, dst_freg, base_reg, offset);
}
void
host_loong64_FLD_D_IMM(codeblock_t *block, int dst_freg, int base_reg, int offset)
{
    emit_fp_ldst(block, OPCODE_FLD_D, dst_freg, base_reg, offset);
}
void
host_loong64_FST_S_IMM(codeblock_t *block, int src_freg, int base_reg, int offset)
{
    emit_fp_ldst(block, OPCODE_FST_S, src_freg, base_reg, offset);
}
void
host_loong64_FST_D_IMM(codeblock_t *block, int src_freg, int base_reg, int offset)
{
    emit_fp_ldst(block, OPCODE_FST_D, src_freg, base_reg, offset);
}

void
host_loong64_MOVGR2FR_W(codeblock_t *block, int dst_freg, int src_greg)
{
    codegen_addlong(block, OPCODE_MOVGR2FR_W | Rd(dst_freg) | Rj(src_greg));
}
void
host_loong64_MOVGR2FR_D(codeblock_t *block, int dst_freg, int src_greg)
{
    codegen_addlong(block, OPCODE_MOVGR2FR_D | Rd(dst_freg) | Rj(src_greg));
}
void
host_loong64_MOVFR2GR_S(codeblock_t *block, int dst_greg, int src_freg)
{
    codegen_addlong(block, OPCODE_MOVFR2GR_S | Rd(dst_greg) | Rj(src_freg));
}
void
host_loong64_MOVFR2GR_D(codeblock_t *block, int dst_greg, int src_freg)
{
    codegen_addlong(block, OPCODE_MOVFR2GR_D | Rd(dst_greg) | Rj(src_freg));
}
void
host_loong64_VMOV_F(codeblock_t *block, int dst_freg, int src_freg)
{
    /*FP<->FP register copy: vori.b dst, src, 0 (copies all 128 bits; the
      f regs alias the low 64).*/
    codegen_addlong(block, OPCODE_VORI_B | Rd(dst_freg) | Rj(src_freg) | IMM12(0));
}

/*Branches / jumps.*/

/*Pack a signed byte offset into the B/BL d10k16 fields.*/
static inline uint32_t
br26_packed(intptr_t offset)
{
    return ((((offset) >> 2) & 0xffff) << 10) | (((offset) >> 18) & 0x3ff);
}

void
host_loong64_B(codeblock_t *block, void *dest)
{
    uint8_t *src;

    codegen_alloc(block, 4);
    src = &block_write_data[block_pos];
    if (!codegen_allocator_can_branch_imm26(src, dest))
        fatal("host_loong64_B - offset out of range\n");
    codegen_addlong(block, OPCODE_B | br26_packed((intptr_t) dest - (intptr_t) src));
}

static void
emit_branch_reg(codeblock_t *block, int opc, int src_a_reg, int src_b_reg, void *dest)
{
    uint8_t *src;

    codegen_alloc(block, 8);
    src = &block_write_data[block_pos];
    if (can_branch_sk16(src, dest)) {
        codegen_addlong(block, opc | Rj(src_a_reg) | Rd(src_b_reg) | BR16((intptr_t) dest - (intptr_t) src));
    } else {
        /*Out of SK16 range: invert the condition, skip over a B that
          carries the far target (always in range inside the JIT pool).*/
        codegen_addlong(block, BRANCH_INVERT(opc) | Rj(src_a_reg) | Rd(src_b_reg) | BR16(8));
        host_loong64_B(block, dest);
    }
}

void
host_loong64_branch_reg_eq(codeblock_t *block, int src_a_reg, int src_b_reg, void *dest)
{
    emit_branch_reg(block, OPCODE_BEQ, src_a_reg, src_b_reg, dest);
}
void
host_loong64_branch_reg_ne(codeblock_t *block, int src_a_reg, int src_b_reg, void *dest)
{
    emit_branch_reg(block, OPCODE_BNE, src_a_reg, src_b_reg, dest);
}

/*'if (cond) skip; B <patched>' templates - the B is patched later by
  host_loong64_branch_set_offset(), which collapses the pair back into a
  direct conditional branch when the target lands in SK16 range.*/
static uint32_t *
branch_template(codeblock_t *block, int opc, int src_a_reg, int src_b_reg)
{
    codegen_alloc(block, 12);
    codegen_addlong(block, BRANCH_INVERT(opc) | Rj(src_a_reg) | Rd(src_b_reg) | BR16(8));
    codegen_addlong(block, OPCODE_B);
    return (uint32_t *) &block_write_data[block_pos - 4];
}

uint32_t *
host_loong64_BEQ_(codeblock_t *block, int src_a_reg, int src_b_reg)
{
    return branch_template(block, OPCODE_BEQ, src_a_reg, src_b_reg);
}
uint32_t *
host_loong64_BNE_(codeblock_t *block, int src_a_reg, int src_b_reg)
{
    return branch_template(block, OPCODE_BNE, src_a_reg, src_b_reg);
}
uint32_t *
host_loong64_BLT_(codeblock_t *block, int src_a_reg, int src_b_reg)
{
    return branch_template(block, OPCODE_BLT, src_a_reg, src_b_reg);
}
uint32_t *
host_loong64_BGE_(codeblock_t *block, int src_a_reg, int src_b_reg)
{
    return branch_template(block, OPCODE_BGE, src_a_reg, src_b_reg);
}
uint32_t *
host_loong64_BLTU_(codeblock_t *block, int src_a_reg, int src_b_reg)
{
    return branch_template(block, OPCODE_BLTU, src_a_reg, src_b_reg);
}
uint32_t *
host_loong64_BGEU_(codeblock_t *block, int src_a_reg, int src_b_reg)
{
    return branch_template(block, OPCODE_BGEU, src_a_reg, src_b_reg);
}

void
host_loong64_branch_set_offset(uint32_t *opcode, void *dest)
{
    uint32_t *cond_opcode = opcode - 1;
    uint32_t  cond_insn   = *cond_opcode;
    intptr_t  offset26    = (intptr_t) dest - (intptr_t) opcode;

    /*Collapse the 'inverse-cond +8; B' template into a direct SK16 branch
      when possible (mirrors the arm64 backend).*/
    switch (cond_insn & 0xfc000000u) {
        case OPCODE_BEQ:
        case OPCODE_BNE:
        case OPCODE_BLT:
        case OPCODE_BGE:
        case OPCODE_BLTU:
        case OPCODE_BGEU:
            if (((cond_insn >> 10) & 0xffffu) == 2) {
                uint8_t *cond_src = (uint8_t *) cond_opcode;

                if (can_branch_sk16(cond_src, dest)) {
                    int offset16 = (int) ((intptr_t) dest - (intptr_t) cond_opcode);

                    /*Clear the template's stale SK16 field (BR16(8)) before
                      OR-ing in the real offset - OR-ing over it leaves the
                      old bits set and makes the collapsed branch overshoot
                      its target by up to 8 bytes.*/
                    *cond_opcode = ((cond_insn ^ 0x04000000u) & 0xfc0003ffu) | BR16(offset16);
                    *opcode      = OPCODE_NOP;
                    return;
                }
            }
            break;
        default:
            break;
    }

    *opcode = OPCODE_B | br26_packed(offset26);
}

void
host_loong64_jump(codeblock_t *block, uintptr_t dst_addr)
{
    uint8_t *src;
    void    *dst_ptr = (void *) dst_addr;

    codegen_alloc(block, 4);
    src = &block_write_data[block_pos];
    if (codegen_allocator_contains_host_ptr(dst_ptr) && codegen_allocator_can_branch_imm26(src, dst_ptr)) {
        codegen_addlong(block, OPCODE_B | br26_packed((intptr_t) dst_ptr - (intptr_t) src));
        return;
    }

    host_loong64_mov_imm(block, REG_T8, dst_addr);
    codegen_addlong(block, OPCODE_JIRL | Rd(REG_ZERO) | Rj(REG_T8) | IMM12(0));
}

void
host_loong64_call(codeblock_t *block, void *dst_addr)
{
    uint8_t *src;

    codegen_alloc(block, 4);
    src = &block_write_data[block_pos];
    if (codegen_allocator_contains_host_ptr(dst_addr) && codegen_allocator_can_branch_imm26(src, dst_addr)) {
        codegen_addlong(block, OPCODE_BL | br26_packed((intptr_t) dst_addr - (intptr_t) src));
        return;
    }

    host_loong64_mov_imm(block, REG_T8, (uint64_t) (uintptr_t) dst_addr);
    codegen_addlong(block, OPCODE_JIRL | Rd(REG_RA) | Rj(REG_T8) | IMM12(0));
}

void
host_loong64_NOP(codeblock_t *block)
{
    codegen_addlong(block, OPCODE_NOP);
}

void
host_loong64_RET(codeblock_t *block)
{
    codegen_addlong(block, OPCODE_JIRL | Rd(REG_ZERO) | Rj(REG_RA) | IMM12(0));
}

/*Reads from cpu_state / the host frame. All accesses go through the
  si12/ldptr/mov+ldx selection in the load/store emitters, so there is no
  separate range check here (unlike arm64, whose si12 is unsigned).*/

void
codegen_direct_read_8(codeblock_t *block, int host_reg, void *p)
{
    host_loong64_LDRB_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_read_16(codeblock_t *block, int host_reg, void *p)
{
    host_loong64_LDRH_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_read_32(codeblock_t *block, int host_reg, void *p)
{
    host_loong64_LDR_W_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_read_64(codeblock_t *block, int host_reg, void *p)
{
    host_loong64_FLD_D_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_read_pointer(codeblock_t *block, int host_reg, void *p)
{
    host_loong64_LDRX_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_read_double(codeblock_t *block, int host_reg, void *p)
{
    host_loong64_FLD_D_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_read_st_8(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
    host_loong64_LDR_W_IMM(block, REG_TEMP, REG_R3, IREG_TOP_diff_stack_offset);
    host_loong64_ADD_W_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
    host_loong64_ADDX_IMM(block, REG_TEMP2, REG_CPUSTATE, (uintptr_t) base - (uintptr_t) &cpu_state);
    host_loong64_ANDI(block, REG_TEMP, REG_TEMP, 7);
    host_loong64_LDRB_REG(block, host_reg, REG_TEMP2, REG_TEMP);
}
void
codegen_direct_read_st_64(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
    host_loong64_LDR_W_IMM(block, REG_TEMP, REG_R3, IREG_TOP_diff_stack_offset);
    host_loong64_ADD_W_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
    host_loong64_ADDX_IMM(block, REG_TEMP2, REG_CPUSTATE, (uintptr_t) base - (uintptr_t) &cpu_state);
    host_loong64_ANDI(block, REG_TEMP, REG_TEMP, 7);
    host_loong64_ADDX_REG(block, REG_TEMP2, REG_TEMP2, REG_TEMP);
    host_loong64_FLD_D_IMM(block, host_reg, REG_TEMP2, 0);
}
void
codegen_direct_read_st_double(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
    host_loong64_LDR_W_IMM(block, REG_TEMP, REG_R3, IREG_TOP_diff_stack_offset);
    host_loong64_ADD_W_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
    host_loong64_ADDX_IMM(block, REG_TEMP2, REG_CPUSTATE, (uintptr_t) base - (uintptr_t) &cpu_state);
    host_loong64_ANDI(block, REG_TEMP, REG_TEMP, 7);
    host_loong64_ADDX_REG(block, REG_TEMP2, REG_TEMP2, REG_TEMP);
    host_loong64_FLD_D_IMM(block, host_reg, REG_TEMP2, 0);
}

void
codegen_direct_read_16_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_loong64_LDRH_IMM(block, host_reg, REG_R3, stack_offset);
}
void
codegen_direct_read_32_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_loong64_LDR_W_IMM(block, host_reg, REG_R3, stack_offset);
}
void
codegen_direct_read_pointer_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_loong64_LDRX_IMM(block, host_reg, REG_R3, stack_offset);
}
void
codegen_direct_read_64_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_loong64_FLD_D_IMM(block, host_reg, REG_R3, stack_offset);
}
void
codegen_direct_read_double_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_loong64_FLD_D_IMM(block, host_reg, REG_R3, stack_offset);
}

/*Writes to cpu_state / the host frame.*/

void
codegen_direct_write_8(codeblock_t *block, void *p, int host_reg)
{
    host_loong64_STRB_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_write_16(codeblock_t *block, void *p, int host_reg)
{
    host_loong64_STRH_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_write_32(codeblock_t *block, void *p, int host_reg)
{
    host_loong64_STR_W_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_write_64(codeblock_t *block, void *p, int host_reg)
{
    host_loong64_FST_D_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_write_pointer(codeblock_t *block, void *p, int host_reg)
{
    host_loong64_STRX_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_write_ptr(codeblock_t *block, void *p, int host_reg)
{
    host_loong64_STRX_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_write_double(codeblock_t *block, void *p, int host_reg)
{
    host_loong64_FST_D_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_write_st_8(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
    host_loong64_LDR_W_IMM(block, REG_TEMP, REG_R3, IREG_TOP_diff_stack_offset);
    host_loong64_ADD_W_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
    host_loong64_ADDX_IMM(block, REG_TEMP2, REG_CPUSTATE, (uintptr_t) base - (uintptr_t) &cpu_state);
    host_loong64_ANDI(block, REG_TEMP, REG_TEMP, 7);
    host_loong64_STRB_REG(block, host_reg, REG_TEMP2, REG_TEMP);
}
void
codegen_direct_write_st_64(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
    host_loong64_LDR_W_IMM(block, REG_TEMP, REG_R3, IREG_TOP_diff_stack_offset);
    host_loong64_ADD_W_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
    host_loong64_ADDX_IMM(block, REG_TEMP2, REG_CPUSTATE, (uintptr_t) base - (uintptr_t) &cpu_state);
    host_loong64_ANDI(block, REG_TEMP, REG_TEMP, 7);
    host_loong64_ADDX_REG(block, REG_TEMP2, REG_TEMP2, REG_TEMP);
    host_loong64_FST_D_IMM(block, host_reg, REG_TEMP2, 0);
}
void
codegen_direct_write_st_double(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
    host_loong64_LDR_W_IMM(block, REG_TEMP, REG_R3, IREG_TOP_diff_stack_offset);
    host_loong64_ADD_W_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
    host_loong64_ADDX_IMM(block, REG_TEMP2, REG_CPUSTATE, (uintptr_t) base - (uintptr_t) &cpu_state);
    host_loong64_ANDI(block, REG_TEMP, REG_TEMP, 7);
    host_loong64_ADDX_REG(block, REG_TEMP2, REG_TEMP2, REG_TEMP);
    host_loong64_FST_D_IMM(block, host_reg, REG_TEMP2, 0);
}

void
codegen_direct_write_32_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_loong64_STR_W_IMM(block, host_reg, REG_R3, stack_offset);
}
void
codegen_direct_write_64_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_loong64_FST_D_IMM(block, host_reg, REG_R3, stack_offset);
}
void
codegen_direct_write_pointer_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_loong64_STRX_IMM(block, host_reg, REG_R3, stack_offset);
}
void
codegen_direct_write_double_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_loong64_FST_D_IMM(block, host_reg, REG_R3, stack_offset);
}

/*Immediate stores (enabled by CODEGEN_BACKEND_HAS_MOV_IMM).*/

void
codegen_direct_write_8_imm(codeblock_t *block, void *p, uint8_t imm_data)
{
    /*Used by MOV_IMM-style uops to skip temp register materialization in the IR layer.*/
    host_loong64_mov_imm(block, REG_TEMP2, imm_data);
    host_loong64_STRB_IMM(block, REG_TEMP2, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_write_16_imm(codeblock_t *block, void *p, uint16_t imm_data)
{
    host_loong64_mov_imm(block, REG_TEMP2, imm_data);
    host_loong64_STRH_IMM(block, REG_TEMP2, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_write_32_imm(codeblock_t *block, void *p, uint32_t imm_data)
{
    host_loong64_mov_imm_w(block, REG_TEMP2, imm_data);
    host_loong64_STR_W_IMM(block, REG_TEMP2, REG_CPUSTATE, (uintptr_t) p - (uintptr_t) &cpu_state);
}
void
codegen_direct_write_32_imm_stack(codeblock_t *block, int stack_offset, uint32_t imm_data)
{
    host_loong64_mov_imm_w(block, REG_TEMP2, imm_data);
    host_loong64_STR_W_IMM(block, REG_TEMP2, REG_R3, stack_offset);
}

/*Branch patch-up used by codegen_ir_compile().*/

void
codegen_set_jump_dest(codeblock_t *block, void *p)
{
    host_loong64_branch_set_offset(p, &block_write_data[block_pos]);
}

#endif
#ifndef _CODEGEN_BACKEND_LOONGARCH64_H_
#define _CODEGEN_BACKEND_LOONGARCH64_H_

#include "codegen_backend_loongarch64_defs.h"

/* Same as the original PCem v15 value. Kept lower than arm64's 0x8000 /
   x86-64's 0x10000 until the block footprint has been measured on real
   Loongson hardware (see codegen_backend_arm64.h for the reasoning). */
#define BLOCK_SIZE  0x4000
#define BLOCK_MASK  0x3fff
#define BLOCK_START 0

#define HASH_SIZE   0x20000
#define HASH_MASK   0x1ffff

#define HASH(l)     ((l) &0x1ffff)

#define BLOCK_MAX   0x3c0

/* Let generic uop emitters use backend-specific immediate store helpers. */
#define CODEGEN_BACKEND_HAS_MOV_IMM

/* Host frame used by the block prologue/epilogue (plan section 8.1).
   16 bytes at the bottom hold $ra and REG_CPUSTATE, SP+16..SP+56 is left
   free for the generic IREG_temp/TOP-diff slots, the allocatable integer
   registers follow at SP+56..SP+128 and the FP set at SP+128..SP+192. */
#define LOONG64_PROLOGUE_FRAME   0xc0
#define LOONG64_STACKOFF_RA      0
#define LOONG64_STACKOFF_CPUSTATE 8
#define LOONG64_STACKOFF_INT(reg) (56 + ((reg) - REG_S0) * 8)
#define LOONG64_STACKOFF_FP(freg) (128 + ((freg) - REG_F24) * 8)

/*Branch/jump.*/

void host_loong64_B(codeblock_t *block, void *dest);
void host_loong64_jump(codeblock_t *block, uintptr_t dst_addr);
void host_loong64_call(codeblock_t *block, void *dst_addr);

/*Conditional branch on two registers (beq/bne/blt/bge/bltu/bgeu), with the
  inverse-condition skip + B template used when the target is out of the
  +-128 KiB SK16 range.*/
void host_loong64_branch_reg_eq(codeblock_t *block, int src_a_reg, int src_b_reg, void *dest);
void host_loong64_branch_reg_ne(codeblock_t *block, int src_a_reg, int src_b_reg, void *dest);

/*Emit 'if (cond) skip next; B <patched>' templates, return pointer to the
  B for later patching by host_loong64_branch_set_offset() (arm64 B??_
  pattern; needed by the *_DEST uops from M2 on).*/
uint32_t *host_loong64_BEQ_(codeblock_t *block, int src_a_reg, int src_b_reg);
uint32_t *host_loong64_BNE_(codeblock_t *block, int src_a_reg, int src_b_reg);
uint32_t *host_loong64_BLT_(codeblock_t *block, int src_a_reg, int src_b_reg);
uint32_t *host_loong64_BGE_(codeblock_t *block, int src_a_reg, int src_b_reg);
uint32_t *host_loong64_BLTU_(codeblock_t *block, int src_a_reg, int src_b_reg);
uint32_t *host_loong64_BGEU_(codeblock_t *block, int src_a_reg, int src_b_reg);

void host_loong64_branch_set_offset(uint32_t *opcode, void *dest);

void host_loong64_NOP(codeblock_t *block);
void host_loong64_RET(codeblock_t *block);

/*Moves.*/

/*64-bit register copy (or rd, rj, r0).*/
void host_loong64_MOV_REG(codeblock_t *block, int dst_reg, int src_reg);
/*32-bit canonicalising copy (addi.w rd, rj, 0 - sign-extends to 64).*/
void host_loong64_MOV_W(codeblock_t *block, int dst_reg, int src_reg);
/*Insert a bit field of src into dst without touching the other bits
  (bstrins.w / bstrins.d).*/
void host_loong64_BFI_W(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width);
void host_loong64_BFI_D(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width);
/*Zero-extend field src[lsb+width-1:lsb] into the full register
  (bstrpick.d).*/
void host_loong64_UBFX_D(codeblock_t *block, int dst_reg, int src_reg, int lsb, int width);
/*Sign-extend the low byte / halfword to 64 bits (ext.w.b / ext.w.h).*/
void host_loong64_SEXT_B(codeblock_t *block, int dst_reg, int src_reg);
void host_loong64_SEXT_H(codeblock_t *block, int dst_reg, int src_reg);

/*Immediates.*/

void host_loong64_MOVX_IMM(codeblock_t *block, int reg, uint64_t imm_data);
void host_loong64_mov_imm(codeblock_t *block, int reg, uint64_t imm_data);
/*Materialise a canonical sign-extended 32-bit constant (lu12i.w [+ ori]).*/
void host_loong64_mov_imm_w(codeblock_t *block, int reg, uint32_t imm_data);
/*Returns the immediate if it can be used directly by andi/ori/xori
  (12-bit unsigned), 0 otherwise.*/
uint32_t host_loong64_find_imm(uint32_t data);

/*Raw immediate encoders (used by the mov_imm decomposition above and the
  block prologue/epilogue).*/
void host_loong64_ORI(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data);
void host_loong64_ADDI_W(codeblock_t *block, int dst_reg, int src_reg, int32_t imm_data);
void host_loong64_ADDI_D(codeblock_t *block, int dst_reg, int src_reg, int32_t imm_data);
void host_loong64_LU12I_W(codeblock_t *block, int dst_reg, uint32_t imm_data);
void host_loong64_LU32I_D(codeblock_t *block, int dst_reg, uint32_t imm_data);
void host_loong64_LU52I_D(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data);

/*Integer ALU.*/

/*32-bit forms (add.w/sub.w/sll.w/... - results are sign-extended to 64).*/
void host_loong64_ADD_W_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg);
void host_loong64_SUB_W_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg);
void host_loong64_ADD_W_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data);
void host_loong64_SUB_W_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data);
/*64-bit forms (add.d/sub.d).*/
void host_loong64_ADDX_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg);
void host_loong64_SUBX_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg);
void host_loong64_ADDX_IMM(codeblock_t *block, int dst_reg, int src_reg, uint64_t imm_data);
void host_loong64_SUBX_IMM(codeblock_t *block, int dst_reg, int src_reg, uint64_t imm_data);
/*dst = a + (b << shift), shift in 1..4 (alsl.d; shift 0 degenerates to add.d).*/
void host_loong64_ALSL_D(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg, int shift);
/*64-bit logic ops (and/or/xor have no 32-bit form on LA64).*/
void host_loong64_AND_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg);
void host_loong64_OR_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg);
void host_loong64_XOR_REG(codeblock_t *block, int dst_reg, int src_a_reg, int src_b_reg);
/*Logic-immediate forms: raw andi (imm <= 0xfff) and mask forms that go
  through mov_imm when the immediate does not fit uimm12.*/
void host_loong64_ANDI(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data);
void host_loong64_AND_IMM(codeblock_t *block, int dst_reg, int src_reg, uint64_t imm_data);
void host_loong64_OR_IMM(codeblock_t *block, int dst_reg, int src_reg, uint64_t imm_data);
void host_loong64_XOR_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t imm_data);

/*Shifts (immediate and register forms; 32-bit forms sign-extend the result).*/
void host_loong64_SHL_W_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_loong64_SHR_W_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_loong64_SAR_W_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_loong64_SHL_D_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_loong64_SHR_D_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_loong64_SAR_D_IMM(codeblock_t *block, int dst_reg, int src_reg, int shift);
void host_loong64_SHL_W_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg);
void host_loong64_SHR_W_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg);
void host_loong64_SAR_W_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg);
void host_loong64_SHL_D_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg);
void host_loong64_SHR_D_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg);
void host_loong64_SAR_D_REG(codeblock_t *block, int dst_reg, int src_reg, int shift_reg);

/*Loads / stores. GPR destinations. Offsets are byte displacements from the
  base register; si12, ldptr and mov_imm+ldx fallbacks are selected
  internally.*/

void host_loong64_LDRB_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset);  /*zero-extend*/
void host_loong64_LDRSB_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset); /*sign-extend*/
void host_loong64_LDRH_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset);
void host_loong64_LDRSH_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset);
void host_loong64_LDR_W_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset); /*zero-extend*/
void host_loong64_LDRSW_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset); /*sign-extend*/
void host_loong64_LDRX_IMM(codeblock_t *block, int dst_reg, int base_reg, int offset);

void host_loong64_LDRB_REG(codeblock_t *block, int dst_reg, int base_reg, int offset_reg);
void host_loong64_LDRH_REG(codeblock_t *block, int dst_reg, int base_reg, int offset_reg);
void host_loong64_LDR_REG(codeblock_t *block, int dst_reg, int base_reg, int offset_reg);
void host_loong64_LDRX_REG(codeblock_t *block, int dst_reg, int base_reg, int offset_reg);

void host_loong64_STRB_IMM(codeblock_t *block, int src_reg, int base_reg, int offset);
void host_loong64_STRH_IMM(codeblock_t *block, int src_reg, int base_reg, int offset);
void host_loong64_STR_W_IMM(codeblock_t *block, int src_reg, int base_reg, int offset);
void host_loong64_STRX_IMM(codeblock_t *block, int src_reg, int base_reg, int offset);

void host_loong64_STRB_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg);
void host_loong64_STRH_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg);
void host_loong64_STR_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg);
void host_loong64_STRX_REG(codeblock_t *block, int src_reg, int base_reg, int offset_reg);

/*FP loads/stores (si12 only - no indexed FP forms; callers needing a
  computed address add it into a scratch GPR first).*/
void host_loong64_FLD_S_IMM(codeblock_t *block, int dst_freg, int base_reg, int offset);
void host_loong64_FLD_D_IMM(codeblock_t *block, int dst_freg, int base_reg, int offset);
void host_loong64_FST_S_IMM(codeblock_t *block, int src_freg, int base_reg, int offset);
void host_loong64_FST_D_IMM(codeblock_t *block, int src_freg, int base_reg, int offset);

/*GPR <-> FPR bit copies, and the FP<->FP register copy.*/
void host_loong64_MOVGR2FR_W(codeblock_t *block, int dst_freg, int src_greg);
void host_loong64_MOVGR2FR_D(codeblock_t *block, int dst_freg, int src_greg);
void host_loong64_MOVFR2GR_S(codeblock_t *block, int dst_greg, int src_freg);
void host_loong64_MOVFR2GR_D(codeblock_t *block, int dst_greg, int src_freg);
void host_loong64_VMOV_F(codeblock_t *block, int dst_freg, int src_freg);

/*Block allocation chaining (mirrors the arm64 backend).*/
void codegen_alloc(codeblock_t *block, int size);

#endif

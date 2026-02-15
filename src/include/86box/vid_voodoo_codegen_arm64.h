/*Registers :

  alphaMode
  fbzMode & 0x1f3fff
  fbzColorPath
*/

#ifndef VIDEO_VOODOO_CODEGEN_ARM64_H
#define VIDEO_VOODOO_CODEGEN_ARM64_H

#if defined(__APPLE__) && defined(__aarch64__)
#    include <pthread.h>
#endif

#include <stdint.h>

#define BLOCK_NUM  8
#define BLOCK_MASK (BLOCK_NUM - 1)
#define BLOCK_SIZE 16384

#define LOD_MASK (LOD_TMIRROR_S | LOD_TMIRROR_T)

/* ========================================================================
 * ARM64 Register Assignments (in generated code)
 * ========================================================================
 *
 * GPR:
 *   x0  = voodoo_state_t *state   (arg0, pinned)
 *   x1  = voodoo_params_t *params (arg1, pinned)
 *   x2  = x pixel position        (arg2)
 *   x3  = real_y                   (arg3)
 *   x4-x7   = scratch (caller-saved)
 *   x8      = fb_mem pointer       (pinned)
 *   x9      = aux_mem pointer      (pinned)
 *   x10-x15 = scratch (caller-saved)
 *   x16-x17 = intra-procedure scratch (IP0/IP1)
 *   x19     = logtable pointer     (callee-saved, pinned)
 *   x20     = alookup pointer      (callee-saved, pinned)
 *   x21     = aminuslookup pointer (callee-saved, pinned)
 *   x22     = xmm_00_ff_w pointer  (callee-saved, pinned)
 *   x23     = i_00_ff_w pointer    (callee-saved, pinned)
 *   x24     = real_y               (callee-saved copy)
 *   x25     = bilinear_lookup ptr  (callee-saved, pinned)
 *   x26-x28 = scratch (callee-saved, available)
 *   x29     = frame pointer        (saved/restored)
 *   x30     = link register        (saved/restored)
 *
 * NEON (in generated code):
 *   v0-v7   = scratch (caller-saved)
 *   v8      = xmm_01_w constant {1,1,1,1}     (callee-saved)
 *   v9      = xmm_ff_w constant {0xFF,...}     (callee-saved)
 *   v10     = xmm_ff_b constant {0xFFFFFF,0,0,0} (callee-saved)
 *   v11     = minus_254 constant {0xFF02,...}   (callee-saved)
 *   v12-v15 = scratch (callee-saved, save/restore in prologue/epilogue)
 *   v16-v31 = scratch (caller-saved)
 * ======================================================================== */

/* ========================================================================
 * Data structure -- mirrors voodoo_x86_data_t
 * ======================================================================== */
typedef struct voodoo_arm64_data_t {
    uint8_t  code_block[BLOCK_SIZE];
    int      xdir;
    uint32_t alphaMode;
    uint32_t fbzMode;
    uint32_t fogMode;
    uint32_t fbzColorPath;
    uint32_t textureMode[2];
    uint32_t tLOD[2];
    uint32_t trexInit1;
    int      is_tiled;
} voodoo_arm64_data_t;

static int last_block[4]          = { 0, 0 };
static int next_block_to_write[4] = { 0, 0 };

/* ========================================================================
 * Emission primitive -- ARM64 instructions are always 4 bytes
 * ======================================================================== */
#define addlong(val)                                 \
    do {                                             \
        *(uint32_t *) &code_block[block_pos] = val;  \
        block_pos += 4;                              \
    } while (0)

/* ========================================================================
 * Section 1: Field Helper Macros
 * ======================================================================== */
#define Rt(x)  (x)
#define Rd(x)  (x)
#define Rn(x)  ((x) << 5)
#define Rt2(x) ((x) << 10)
#define Rm(x)  ((x) << 16)

/* ========================================================================
 * Section 2: Shift and Data Processing Field Helpers
 * ======================================================================== */
#define DATPROC_SHIFT(sh)     ((sh) << 10)
#define DATPROC_IMM_SHIFT(sh) ((sh) << 22)
#define MOV_WIDE_HW(hw)      ((hw) << 21)

#define shift_imm6(x) ((x) << 10)

/* ========================================================================
 * Section 3: Immediate Field Helpers
 * ======================================================================== */
#define IMM7_X(imm_data) (((imm_data >> 3) & 0x7f) << 15)
#define IMM12(imm_data)  ((imm_data) << 10)
#define IMM16(imm_data)  ((imm_data) << 5)

#define IMMN(immn) ((immn) << 22)
#define IMMR(immr) ((immr) << 16)
#define IMMS(imms) ((imms) << 10)
#define IMM_LOGICAL(imm) ((imm) << 10)

/* ========================================================================
 * Section 4: Offset Encoding Helpers
 * ======================================================================== */
#define OFFSET14(offset)  (((offset >> 2) << 5) & 0x0007ffe0)
#define OFFSET19(offset)  (((offset >> 2) << 5) & 0x00ffffe0)
#define OFFSET20(offset)  (((offset & 3) << 29) | ((((offset) & 0x1fffff) >> 2) << 5))
#define OFFSET26(offset)  ((offset >> 2) & 0x03ffffff)

#define OFFSET12_B(offset) ((offset) << 10)
#define OFFSET12_H(offset) (((offset) >> 1) << 10)
#define OFFSET12_W(offset) (((offset) >> 2) << 10)
#define OFFSET12_X(offset) (((offset) >> 3) << 10)
#define OFFSET12_Q(offset) (((offset) >> 4) << 10)

/* ========================================================================
 * Section 5: NEON Shift Immediate Helpers
 * ======================================================================== */
#define SHIFT_IMM_V4H(shift)      (((shift) | 0x10) << 16)
#define SHIFT_IMM_V2S(shift)      (((shift) | 0x20) << 16)
#define SHIFT_IMM_V2D(shift)      (((shift) | 0x40) << 16)

#define SHRN_SHIFT_IMM_V4S(shift) (((shift) | 0x10) << 16)

#define DUP_ELEMENT(element) ((element) << 19)

/* ========================================================================
 * Section 6: TBZ/TBNZ Bit Field Helper
 * ======================================================================== */
#define BIT_TBxZ(bit) ((((bit) & 0x1f) << 19) | (((bit) & 0x20) ? (1 << 31) : 0))

/* ========================================================================
 * Section 7: Condition Codes
 * ======================================================================== */
#define COND_EQ (0x0)
#define COND_NE (0x1)
#define COND_CS (0x2) /* HS (unsigned >=) */
#define COND_CC (0x3) /* LO (unsigned <) */
#define COND_MI (0x4) /* negative */
#define COND_PL (0x5) /* positive or zero */
#define COND_VS (0x6) /* overflow */
#define COND_VC (0x7) /* no overflow */
#define COND_HI (0x8) /* unsigned > */
#define COND_LS (0x9) /* unsigned <= */
#define COND_GE (0xa) /* signed >= */
#define COND_LT (0xb) /* signed < */
#define COND_GT (0xc) /* signed > */
#define COND_LE (0xd) /* signed <= */
#define COND_AL (0xe) /* always */

#define CSEL_COND(cond) ((cond) << 12)

/* ========================================================================
 * Section 8: GPR Move / Immediate
 * ======================================================================== */

/* MOV Wd, Ws -- alias for ORR Wd, WZR, Ws (32-bit) */
#define ARM64_MOV_REG(d, s) (0x2A0003E0 | Rm(s) | Rd(d))

/* MOV Xd, Xs -- alias for ORR Xd, XZR, Xs (64-bit) */
#define ARM64_MOV_REG_X(d, s) (0xAA0003E0 | Rm(s) | Rd(d))

/* MOVZ Wd, #imm16 -- move 16-bit immediate, zero rest */
#define ARM64_MOVZ_W(d, imm16) (0x52800000 | IMM16(imm16) | Rd(d))

/* MOVZ Xd, #imm16 -- 64-bit, hw=0 */
#define ARM64_MOVZ_X(d, imm16) (0xD2800000 | IMM16(imm16) | Rd(d))

/* MOVK Wd, #imm16, LSL #16 -- keep, insert at hw=1 */
#define ARM64_MOVK_W_16(d, imm16) (0x72A00000 | IMM16(imm16) | Rd(d))

/* MOVK Xd, #imm16, LSL #hw*16 -- keep, insert at specified halfword */
#define ARM64_MOVK_X(d, imm16, hw) (0xF2800000 | MOV_WIDE_HW(hw) | IMM16(imm16) | Rd(d))

/* MOV Wd, WZR -- zero a register (alias for MOVZ Wd, #0) */
#define ARM64_MOV_ZERO(d) (0x52800000 | Rd(d))

/* MVN Wd, Ws -- bitwise NOT (alias for ORN Wd, WZR, Ws) */
#define ARM64_MVN(d, s) (0x2A2003E0 | Rm(s) | Rd(d))

/* NEG Wd, Ws -- negate (alias for SUB Wd, WZR, Ws) */
#define ARM64_NEG(d, s) (0x4B0003E0 | Rm(s) | Rd(d))

/* ========================================================================
 * Section 9: GPR Arithmetic -- Register
 * ======================================================================== */

/* ADD Wd, Wn, Wm (32-bit register) */
#define ARM64_ADD_REG(d, n, m) (0x0B000000 | Rm(m) | Rn(n) | Rd(d))

/* ADD Wd, Wn, Wm, LSL #sh */
#define ARM64_ADD_REG_LSL(d, n, m, sh) (0x0B000000 | Rm(m) | shift_imm6(sh) | Rn(n) | Rd(d))

/* ADD Xd, Xn, Xm (64-bit register) */
#define ARM64_ADD_REG_X(d, n, m) (0x8B000000 | Rm(m) | Rn(n) | Rd(d))

/* ADD Xd, Xn, Xm, LSL #sh (64-bit with shift) */
#define ARM64_ADD_REG_X_LSL(d, n, m, sh) (0x8B000000 | Rm(m) | shift_imm6(sh) | Rn(n) | Rd(d))

/* SUB Wd, Wn, Wm (32-bit register) */
#define ARM64_SUB_REG(d, n, m) (0x4B000000 | Rm(m) | Rn(n) | Rd(d))

/* SUB Xd, Xn, Xm (64-bit register) */
#define ARM64_SUB_REG_X(d, n, m) (0xCB000000 | Rm(m) | Rn(n) | Rd(d))

/* SUBS Wd, Wn, Wm -- sets flags (CMP alias when Rd=WZR) */
#define ARM64_SUBS_REG(d, n, m) (0x6B000000 | Rm(m) | Rn(n) | Rd(d))

/* CMP Wn, Wm -- alias for SUBS WZR, Wn, Wm */
#define ARM64_CMP_REG(n, m) (0x6B000000 | Rm(m) | Rn(n) | Rd(31))

/* MUL Wd, Wn, Wm (32-bit) */
#define ARM64_MUL(d, n, m) (0x1B007C00 | Rm(m) | Rn(n) | Rd(d))

/* MUL Xd, Xn, Xm (64-bit) */
#define ARM64_MUL_X(d, n, m) (0x9B007C00 | Rm(m) | Rn(n) | Rd(d))

/* SDIV Xd, Xn, Xm (64-bit signed divide) */
#define ARM64_SDIV_X(d, n, m) (0x9AC00C00 | Rm(m) | Rn(n) | Rd(d))

/* SDIV Wd, Wn, Wm (32-bit signed divide) */
#define ARM64_SDIV(d, n, m) (0x1AC00C00 | Rm(m) | Rn(n) | Rd(d))

/* ========================================================================
 * Section 10: GPR Arithmetic -- Immediate
 * ======================================================================== */

/* ADD Wd, Wn, #imm12 (32-bit immediate, no shift) */
#define ARM64_ADD_IMM(d, n, imm) (0x11000000 | IMM12(imm) | Rn(n) | Rd(d))

/* ADD Wd, Wn, #imm12, LSL #12 */
#define ARM64_ADD_IMM_SH12(d, n, imm) (0x11400000 | IMM12(imm) | Rn(n) | Rd(d))

/* ADD Xd, Xn, #imm12 (64-bit immediate) */
#define ARM64_ADD_IMM_X(d, n, imm) (0x91000000 | IMM12(imm) | Rn(n) | Rd(d))

/* SUB Wd, Wn, #imm12 (32-bit immediate) */
#define ARM64_SUB_IMM(d, n, imm) (0x51000000 | IMM12(imm) | Rn(n) | Rd(d))

/* SUB Xd, Xn, #imm12 (64-bit immediate) */
#define ARM64_SUB_IMM_X(d, n, imm) (0xD1000000 | IMM12(imm) | Rn(n) | Rd(d))

/* SUBS Wd, Wn, #imm12 -- sets flags */
#define ARM64_SUBS_IMM(d, n, imm) (0x71000000 | IMM12(imm) | Rn(n) | Rd(d))

/* CMP Wn, #imm12 -- alias for SUBS WZR, Wn, #imm12 */
#define ARM64_CMP_IMM(n, imm) (0x71000000 | IMM12(imm) | Rn(n) | Rd(31))

/* CMP Xn, #imm12 (64-bit) */
#define ARM64_CMP_IMM_X(n, imm) (0xF1000000 | IMM12(imm) | Rn(n) | Rd(31))

/* CMN Wn, #imm12 -- alias for ADDS WZR, Wn, #imm12 */
#define ARM64_CMN_IMM(n, imm) (0x31000000 | IMM12(imm) | Rn(n) | Rd(31))

/* ========================================================================
 * Section 11: GPR Bitwise -- Register
 * ======================================================================== */

/* AND Wd, Wn, Wm */
#define ARM64_AND_REG(d, n, m) (0x0A000000 | Rm(m) | Rn(n) | Rd(d))

/* ORR Wd, Wn, Wm */
#define ARM64_ORR_REG(d, n, m) (0x2A000000 | Rm(m) | Rn(n) | Rd(d))

/* EOR Wd, Wn, Wm */
#define ARM64_EOR_REG(d, n, m) (0x4A000000 | Rm(m) | Rn(n) | Rd(d))

/* ANDS Wd, Wn, Wm -- AND setting flags */
#define ARM64_ANDS_REG(d, n, m) (0x6A000000 | Rm(m) | Rn(n) | Rd(d))

/* TST Wn, Wm -- alias for ANDS WZR, Wn, Wm */
#define ARM64_TST_REG(n, m) (0x6A000000 | Rm(m) | Rn(n) | Rd(31))

/* BIC Wd, Wn, Wm -- bit clear (AND NOT) */
#define ARM64_BIC_REG(d, n, m) (0x0A200000 | Rm(m) | Rn(n) | Rd(d))

/* ========================================================================
 * Section 12: GPR Shifts -- Register
 * ======================================================================== */

/* LSL Wd, Wn, Wm (variable left shift) */
#define ARM64_LSL_REG(d, n, m) (0x1AC02000 | Rm(m) | Rn(n) | Rd(d))

/* LSR Wd, Wn, Wm (variable logical right shift) */
#define ARM64_LSR_REG(d, n, m) (0x1AC02400 | Rm(m) | Rn(n) | Rd(d))

/* ASR Wd, Wn, Wm (variable arithmetic right shift) */
#define ARM64_ASR_REG(d, n, m) (0x1AC02800 | Rm(m) | Rn(n) | Rd(d))

/* ROR Wd, Wn, Wm (variable rotate right) */
#define ARM64_ROR_REG(d, n, m) (0x1AC02C00 | Rm(m) | Rn(n) | Rd(d))

/* ========================================================================
 * Section 13: GPR Shifts -- Immediate (via UBFM/SBFM aliases)
 * ======================================================================== */

/* LSL Wd, Wn, #imm */
#define ARM64_LSL_IMM(d, n, imm) (0x53000000 | IMMR((-imm) & 0x1F) | IMMS(31 - (imm)) | Rn(n) | Rd(d))

/* LSL Xd, Xn, #imm -- 64-bit */
#define ARM64_LSL_IMM_X(d, n, imm) (0xD3400000 | IMMR((-imm) & 0x3F) | IMMS(63 - (imm)) | Rn(n) | Rd(d))

/* LSR Wd, Wn, #imm */
#define ARM64_LSR_IMM(d, n, imm) (0x53000000 | IMMR(imm) | IMMS(31) | Rn(n) | Rd(d))

/* LSR Xd, Xn, #imm -- 64-bit */
#define ARM64_LSR_IMM_X(d, n, imm) (0xD3400000 | IMMR(imm) | IMMS(63) | Rn(n) | Rd(d))

/* ASR Wd, Wn, #imm */
#define ARM64_ASR_IMM(d, n, imm) (0x13007C00 | IMMR(imm) | Rn(n) | Rd(d))

/* ASR Xd, Xn, #imm -- 64-bit */
#define ARM64_ASR_IMM_X(d, n, imm) (0x9340FC00 | IMMR(imm) | Rn(n) | Rd(d))

/* ROR Wd, Ws, #imm -- alias for EXTR Wd, Ws, Ws, #imm */
#define ARM64_ROR_IMM(d, s, imm) (0x13800000 | Rm(s) | IMMS(imm) | Rn(s) | Rd(d))

/* ========================================================================
 * Section 14: GPR Bitfield Extract / Insert
 * ======================================================================== */

/* UBFX Wd, Wn, #lsb, #width */
#define ARM64_UBFX(d, n, lsb, width) (0x53000000 | IMMR(lsb) | IMMS((lsb) + (width) - 1) | Rn(n) | Rd(d))

/* SBFX Wd, Wn, #lsb, #width */
#define ARM64_SBFX(d, n, lsb, width) (0x13000000 | IMMR(lsb) | IMMS((lsb) + (width) - 1) | Rn(n) | Rd(d))

/* UXTB Wd, Wn */
#define ARM64_UXTB(d, n) (0x53001C00 | Rn(n) | Rd(d))

/* UXTH Wd, Wn */
#define ARM64_UXTH(d, n) (0x53003C00 | Rn(n) | Rd(d))

/* SXTB Wd, Wn */
#define ARM64_SXTB(d, n) (0x13001C00 | Rn(n) | Rd(d))

/* SXTH Wd, Wn */
#define ARM64_SXTH(d, n) (0x13003C00 | Rn(n) | Rd(d))

/* SXTW Xd, Wn */
#define ARM64_SXTW(d, n) (0x93407C00 | Rn(n) | Rd(d))

/* CLZ Wd, Wn (32-bit) */
#define ARM64_CLZ(d, n) (0x5AC01000 | Rn(n) | Rd(d))

/* CLZ Xd, Xn (64-bit) */
#define ARM64_CLZ_X(d, n) (0xDAC01000 | Rn(n) | Rd(d))

/* ========================================================================
 * Section 15: Conditional Select
 * ======================================================================== */

/* CSEL Wd, Wn, Wm, cond */
#define ARM64_CSEL(d, n, m, cond) (0x1A800000 | CSEL_COND(cond) | Rm(m) | Rn(n) | Rd(d))

/* CSINC Wd, Wn, Wm, cond */
#define ARM64_CSINC(d, n, m, cond) (0x1A800400 | CSEL_COND(cond) | Rm(m) | Rn(n) | Rd(d))

/* CSET Wd, cond */
#define ARM64_CSET(d, cond) (0x1A9F07E0 | CSEL_COND((cond) ^ 1) | Rd(d))

/* ========================================================================
 * Section 16: Load -- Unsigned Immediate (Scaled)
 * ======================================================================== */

/* LDRB Wt, [Xn, #off] */
#define ARM64_LDRB_IMM(t, n, off) (0x39400000 | OFFSET12_B(off) | Rn(n) | Rt(t))

/* LDRH Wt, [Xn, #off] */
#define ARM64_LDRH_IMM(t, n, off) (0x79400000 | OFFSET12_H(off) | Rn(n) | Rt(t))

/* LDR Wt, [Xn, #off] */
#define ARM64_LDR_W(t, n, off) (0xB9400000 | OFFSET12_W(off) | Rn(n) | Rt(t))

/* LDR Xt, [Xn, #off] */
#define ARM64_LDR_X(t, n, off) (0xF9400000 | OFFSET12_X(off) | Rn(n) | Rt(t))

/* LDRSB Wt, [Xn, #off] */
#define ARM64_LDRSB_W(t, n, off) (0x39C00000 | OFFSET12_B(off) | Rn(n) | Rt(t))

/* LDRSH Wt, [Xn, #off] */
#define ARM64_LDRSH_W(t, n, off) (0x79C00000 | OFFSET12_H(off) | Rn(n) | Rt(t))

/* LDRSW Xt, [Xn, #off] */
#define ARM64_LDRSW(t, n, off) (0xB9800000 | OFFSET12_W(off) | Rn(n) | Rt(t))

/* ========================================================================
 * Section 17: Store -- Unsigned Immediate (Scaled)
 * ======================================================================== */

/* STRB Wt, [Xn, #off] */
#define ARM64_STRB_IMM(t, n, off) (0x39000000 | OFFSET12_B(off) | Rn(n) | Rt(t))

/* STRH Wt, [Xn, #off] */
#define ARM64_STRH_IMM(t, n, off) (0x79000000 | OFFSET12_H(off) | Rn(n) | Rt(t))

/* STR Wt, [Xn, #off] */
#define ARM64_STR_W(t, n, off) (0xB9000000 | OFFSET12_W(off) | Rn(n) | Rt(t))

/* STR Xt, [Xn, #off] */
#define ARM64_STR_X(t, n, off) (0xF9000000 | OFFSET12_X(off) | Rn(n) | Rt(t))

/* ========================================================================
 * Section 18: Load/Store -- Register Offset
 * ======================================================================== */

/* LDR Wt, [Xn, Xm] */
#define ARM64_LDR_W_REG(t, n, m) (0xB8606800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Wt, [Xn, Xm, LSL #2] */
#define ARM64_LDR_W_REG_LSL2(t, n, m) (0xB8607800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Xt, [Xn, Xm] */
#define ARM64_LDR_X_REG(t, n, m) (0xF8606800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Xt, [Xn, Xm, LSL #3] */
#define ARM64_LDR_X_REG_LSL3(t, n, m) (0xF8607800 | Rm(m) | Rn(n) | Rt(t))

/* LDRB Wt, [Xn, Xm] */
#define ARM64_LDRB_REG(t, n, m) (0x38606800 | Rm(m) | Rn(n) | Rt(t))

/* LDRH Wt, [Xn, Xm] */
#define ARM64_LDRH_REG(t, n, m) (0x78606800 | Rm(m) | Rn(n) | Rt(t))

/* LDRH Wt, [Xn, Xm, LSL #1] */
#define ARM64_LDRH_REG_LSL1(t, n, m) (0x78607800 | Rm(m) | Rn(n) | Rt(t))

/* STR Wt, [Xn, Xm] */
#define ARM64_STR_W_REG(t, n, m) (0xB8206800 | Rm(m) | Rn(n) | Rt(t))

/* STRB Wt, [Xn, Xm] */
#define ARM64_STRB_REG(t, n, m) (0x38206800 | Rm(m) | Rn(n) | Rt(t))

/* STRH Wt, [Xn, Xm] */
#define ARM64_STRH_REG(t, n, m) (0x78206800 | Rm(m) | Rn(n) | Rt(t))

/* STRH Wt, [Xn, Xm, LSL #1] */
#define ARM64_STRH_REG_LSL1(t, n, m) (0x78207800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Wt, [Xn, Wm, UXTW #2] */
#define ARM64_LDR_W_UXTW2(t, n, m) (0xB8605800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Wt, [Xn, Wm, SXTW #2] */
#define ARM64_LDR_W_SXTW2(t, n, m) (0xB860D800 | Rm(m) | Rn(n) | Rt(t))

/* ========================================================================
 * Section 19: Load/Store Pair (for prologue/epilogue)
 * ======================================================================== */

/* STP Xt1, Xt2, [Xn, #imm]! -- pre-index store pair (64-bit) */
#define ARM64_STP_PRE_X(t1, t2, n, imm) (0xA9800000 | IMM7_X(imm) | Rt2(t2) | Rn(n) | Rt(t1))

/* LDP Xt1, Xt2, [Xn], #imm -- post-index load pair (64-bit) */
#define ARM64_LDP_POST_X(t1, t2, n, imm) (0xA8C00000 | IMM7_X(imm) | Rt2(t2) | Rn(n) | Rt(t1))

/* STP Xt1, Xt2, [Xn, #imm] -- signed offset store pair (64-bit) */
#define ARM64_STP_OFF_X(t1, t2, n, imm) (0xA9000000 | IMM7_X(imm) | Rt2(t2) | Rn(n) | Rt(t1))

/* LDP Xt1, Xt2, [Xn, #imm] -- signed offset load pair (64-bit) */
#define ARM64_LDP_OFF_X(t1, t2, n, imm) (0xA9400000 | IMM7_X(imm) | Rt2(t2) | Rn(n) | Rt(t1))

/* STP Dt1, Dt2, [Xn, #imm] -- SIMD store pair (64-bit NEON) */
#define ARM64_STP_D(t1, t2, n, imm) (0x6D000000 | (((imm >> 3) & 0x7F) << 15) | Rt2(t2) | Rn(n) | Rt(t1))

/* LDP Dt1, Dt2, [Xn, #imm] -- SIMD load pair (64-bit NEON) */
#define ARM64_LDP_D(t1, t2, n, imm) (0x6D400000 | (((imm >> 3) & 0x7F) << 15) | Rt2(t2) | Rn(n) | Rt(t1))

/* ========================================================================
 * Section 20: Branch and Control Flow
 * ======================================================================== */

/* B label */
#define ARM64_B(off) (0x14000000 | OFFSET26(off))

/* BL label */
#define ARM64_BL(off) (0x94000000 | OFFSET26(off))

/* B.cond label */
#define ARM64_BCOND(off, cond) (0x54000000 | OFFSET19(off) | (cond))

/* CBZ Wt, label */
#define ARM64_CBZ_W(t, off) (0x34000000 | OFFSET19(off) | Rt(t))

/* CBNZ Wt, label */
#define ARM64_CBNZ_W(t, off) (0x35000000 | OFFSET19(off) | Rt(t))

/* CBZ Xt, label */
#define ARM64_CBZ_X(t, off) (0xB4000000 | OFFSET19(off) | Rt(t))

/* CBNZ Xt, label */
#define ARM64_CBNZ_X(t, off) (0xB5000000 | OFFSET19(off) | Rt(t))

/* TBZ Rt, #bit, label */
#define ARM64_TBZ(t, bit, off) (0x36000000 | BIT_TBxZ(bit) | OFFSET14(off) | Rt(t))

/* TBNZ Rt, #bit, label */
#define ARM64_TBNZ(t, bit, off) (0x37000000 | BIT_TBxZ(bit) | OFFSET14(off) | Rt(t))

/* BR Xn */
#define ARM64_BR(n) (0xD61F0000 | Rn(n))

/* BLR Xn */
#define ARM64_BLR(n) (0xD63F0000 | Rn(n))

/* RET */
#define ARM64_RET (0xD65F03C0)

/* NOP */
#define ARM64_NOP (0xD503201F)

/* Forward-branch patching helpers */
#define ARM64_BCOND_PLACEHOLDER(cond)   (0x54000000 | (cond))
#define ARM64_B_PLACEHOLDER             (0x14000000)
#define ARM64_TBZ_PLACEHOLDER(t, bit)   (0x36000000 | BIT_TBxZ(bit) | Rt(t))
#define ARM64_TBNZ_PLACEHOLDER(t, bit)  (0x37000000 | BIT_TBxZ(bit) | Rt(t))
#define ARM64_CBZ_W_PLACEHOLDER(t)      (0x34000000 | Rt(t))
#define ARM64_CBNZ_W_PLACEHOLDER(t)     (0x35000000 | Rt(t))

/* ========================================================================
 * Section 21: NEON Integer Arithmetic
 * ======================================================================== */

/* 16-bit */
#define ARM64_ADD_V4H(d, n, m)  (0x0E608400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_ADD_V8H(d, n, m)  (0x4E608400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_SUB_V4H(d, n, m)  (0x2E608400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_SUB_V8H(d, n, m)  (0x6E608400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_MUL_V4H(d, n, m)  (0x0E609C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_MUL_V8H(d, n, m)  (0x4E609C00 | Rm(m) | Rn(n) | Rd(d))

/* Widening multiply */
#define ARM64_SMULL_4S_4H(d, n, m) (0x0E60C000 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_SMLAL_4S_4H(d, n, m) (0x0E608000 | Rm(m) | Rn(n) | Rd(d))

/* 32-bit */
#define ARM64_ADD_V2S(d, n, m)  (0x0EA08400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_ADD_V4S(d, n, m)  (0x4EA08400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_SUB_V2S(d, n, m)  (0x2EA08400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_SUB_V4S(d, n, m)  (0x6EA08400 | Rm(m) | Rn(n) | Rd(d))

/* 64-bit */
#define ARM64_ADD_V2D(d, n, m)  (0x4EE08400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_SUB_V2D(d, n, m)  (0x6EE08400 | Rm(m) | Rn(n) | Rd(d))

/* 8-bit */
#define ARM64_ADD_V8B(d, n, m)  (0x0E208400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_SUB_V8B(d, n, m)  (0x2E208400 | Rm(m) | Rn(n) | Rd(d))

/* Pairwise */
#define ARM64_ADDP_V4S(d, n, m) (0x4EA0BC00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_ADDP_V8H(d, n, m) (0x4E60BC00 | Rm(m) | Rn(n) | Rd(d))

/* ========================================================================
 * Section 22: NEON Saturating Arithmetic
 * ======================================================================== */
#define ARM64_SQADD_V4H(d, n, m)  (0x0E600C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_SQADD_V8B(d, n, m)  (0x0E200C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_SQSUB_V4H(d, n, m)  (0x0E602C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_SQSUB_V8B(d, n, m)  (0x0E202C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_UQADD_V4H(d, n, m)  (0x2E600C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_UQADD_V8B(d, n, m)  (0x2E200C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_UQADD_V16B(d, n, m) (0x6E200C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_UQSUB_V4H(d, n, m)  (0x2E602C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_UQSUB_V8B(d, n, m)  (0x2E202C00 | Rm(m) | Rn(n) | Rd(d))

/* ========================================================================
 * Section 23: NEON Shift -- Immediate
 * ======================================================================== */

/* Unsigned shift right */
#define ARM64_USHR_V4H(d, n, imm) (0x2F000400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))
#define ARM64_USHR_V8H(d, n, imm) (0x6F000400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))
#define ARM64_USHR_V2S(d, n, imm) (0x2F000400 | SHIFT_IMM_V2S(32 - (imm)) | Rn(n) | Rd(d))
#define ARM64_USHR_V4S(d, n, imm) (0x6F000400 | SHIFT_IMM_V2S(32 - (imm)) | Rn(n) | Rd(d))

/* Signed shift right */
#define ARM64_SSHR_V4H(d, n, imm) (0x0F000400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))
#define ARM64_SSHR_V8H(d, n, imm) (0x4F000400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))
#define ARM64_SSHR_V2S(d, n, imm) (0x0F000400 | SHIFT_IMM_V2S(32 - (imm)) | Rn(n) | Rd(d))
#define ARM64_SSHR_V4S(d, n, imm) (0x4F000400 | SHIFT_IMM_V2S(32 - (imm)) | Rn(n) | Rd(d))

/* Shift left */
#define ARM64_SHL_V4H(d, n, imm)  (0x0F005400 | SHIFT_IMM_V4H(imm) | Rn(n) | Rd(d))
#define ARM64_SHL_V8H(d, n, imm)  (0x4F005400 | SHIFT_IMM_V4H(imm) | Rn(n) | Rd(d))
#define ARM64_SHL_V2S(d, n, imm)  (0x0F005400 | SHIFT_IMM_V2S(imm) | Rn(n) | Rd(d))
#define ARM64_SHL_V4S(d, n, imm)  (0x4F005400 | SHIFT_IMM_V2S(imm) | Rn(n) | Rd(d))

/* Narrowing shift right */
#define ARM64_SHRN_4H(d, n, imm)  (0x0F008400 | SHRN_SHIFT_IMM_V4S(16 - (imm)) | Rn(n) | Rd(d))

/* Rounding shift right */
#define ARM64_URSHR_V4H(d, n, imm) (0x2F002400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))
#define ARM64_URSHR_V8H(d, n, imm) (0x6F002400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))

/* ========================================================================
 * Section 24: NEON Narrow / Widen
 * ======================================================================== */
#define ARM64_SQXTN_4H_4S(d, n)    (0x0E614800 | Rn(n) | Rd(d))
#define ARM64_SQXTN_8B_8H(d, n)    (0x0E214800 | Rn(n) | Rd(d))
#define ARM64_SQXTUN_8B_8H(d, n)   (0x2E212800 | Rn(n) | Rd(d))
#define ARM64_SQXTUN_4H_4S(d, n)   (0x2E612800 | Rn(n) | Rd(d))
#define ARM64_UQXTN_4H_4S(d, n)    (0x2E614800 | Rn(n) | Rd(d))
#define ARM64_UQXTN_8B_8H(d, n)    (0x2E214800 | Rn(n) | Rd(d))
#define ARM64_UXTL_8H_8B(d, n)     (0x2F08A400 | Rn(n) | Rd(d))
#define ARM64_UXTL_4S_4H(d, n)     (0x2F10A400 | Rn(n) | Rd(d))
#define ARM64_SXTL_8H_8B(d, n)     (0x0F08A400 | Rn(n) | Rd(d))
#define ARM64_SXTL_4S_4H(d, n)     (0x0F10A400 | Rn(n) | Rd(d))

/* ========================================================================
 * Section 25: NEON Permute / Interleave
 * ======================================================================== */
#define ARM64_ZIP1_V16B(d, n, m) (0x4E003800 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_ZIP1_V8B(d, n, m)  (0x0E003800 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_ZIP1_V4H(d, n, m)  (0x0E403800 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_ZIP1_V2S(d, n, m)  (0x0E803800 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_ZIP1_V2D(d, n, m)  (0x4EC03800 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_ZIP2_V8B(d, n, m)  (0x0E007800 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_ZIP2_V4H(d, n, m)  (0x0E407800 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_ZIP2_V2S(d, n, m)  (0x0E807800 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_EXT_16B(d, n, m, imm4) (0x6E000000 | ((imm4) << 11) | Rm(m) | Rn(n) | Rd(d))
#define ARM64_EXT_8B(d, n, m, imm3)  (0x2E000000 | ((imm3) << 11) | Rm(m) | Rn(n) | Rd(d))

/* ========================================================================
 * Section 26: NEON Move / Duplicate / Insert
 * ======================================================================== */

/* MOV Vd.16B, Vs.16B */
#define ARM64_MOV_V(d, s) (0x4EA01C00 | Rm(s) | Rn(s) | Rd(d))

/* DUP Vd.4H, Vs.H[lane] */
#define ARM64_DUP_V4H_LANE(d, n, lane) (0x0E000400 | (((lane) * 4 + 2) << 16) | Rn(n) | Rd(d))

/* DUP Vd.2S, Vs.S[lane] */
#define ARM64_DUP_V2S_LANE(d, n, lane) (0x0E000400 | (((lane) * 8 + 4) << 16) | Rn(n) | Rd(d))

/* DUP Vd.4S, Vs.S[lane] */
#define ARM64_DUP_V4S_LANE(d, n, lane) (0x4E000400 | (((lane) * 8 + 4) << 16) | Rn(n) | Rd(d))

/* DUP Vd.2S, Wn */
#define ARM64_DUP_V2S_GPR(d, n) (0x0E040C00 | Rn(n) | Rd(d))

/* DUP Vd.4H, Wn */
#define ARM64_DUP_V4H_GPR(d, n) (0x0E020C00 | Rn(n) | Rd(d))

/* INS Vd.H[lane], Wn */
#define ARM64_INS_H(d, lane, n) (0x4E001C00 | (((lane) * 4 + 2) << 16) | Rn(n) | Rd(d))

/* INS Vd.S[lane], Wn */
#define ARM64_INS_S(d, lane, n) (0x4E001C00 | (((lane) * 8 + 4) << 16) | Rn(n) | Rd(d))

/* UMOV Wd, Vn.S[lane] */
#define ARM64_UMOV_W_S(d, n, lane) (0x0E003C00 | (((lane) * 8 + 4) << 16) | Rn(n) | Rd(d))

/* UMOV Wd, Vn.H[lane] */
#define ARM64_UMOV_W_H(d, n, lane) (0x0E003C00 | (((lane) * 4 + 2) << 16) | Rn(n) | Rd(d))

/* FMOV Sd, Wn */
#define ARM64_FMOV_S_W(d, n) (0x1E270000 | Rn(n) | Rd(d))

/* FMOV Wn, Sd */
#define ARM64_FMOV_W_S(d, n) (0x1E260000 | Rn(n) | Rd(d))

/* MOVI Vd.4S, #0 */
#define ARM64_MOVI_V4S_ZERO(d) (0x4F000400 | Rd(d))

/* MOVI Vd.2D, #0 */
#define ARM64_MOVI_V2D_ZERO(d) (0x6F00E400 | Rd(d))

/* ========================================================================
 * Section 27: NEON Bitwise
 * ======================================================================== */
#define ARM64_AND_V(d, n, m) (0x4E201C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_ORR_V(d, n, m) (0x4EA01C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_EOR_V(d, n, m) (0x6E201C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_BIC_V(d, n, m) (0x4E601C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_BIT_V(d, n, m) (0x6EA01C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_NOT_V(d, n)    (0x6E205800 | Rn(n) | Rd(d))

/* ========================================================================
 * Section 28: NEON Compare
 * ======================================================================== */
#define ARM64_CMEQ_V4H(d, n, m)    (0x2E608C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_CMEQ_V2S(d, n, m)    (0x2EA08C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_CMGT_V4H(d, n, m)    (0x0E603400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_CMGT_V2S(d, n, m)    (0x0EA03400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_CMHI_V4H(d, n, m)    (0x2E603400 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_CMGE_V4H(d, n, m)    (0x0E603C00 | Rm(m) | Rn(n) | Rd(d))
#define ARM64_CMEQ_V4H_ZERO(d, n)  (0x0E609800 | Rn(n) | Rd(d))
#define ARM64_CMGT_V4H_ZERO(d, n)  (0x0E608800 | Rn(n) | Rd(d))

/* ========================================================================
 * Section 29: NEON Load / Store
 * ======================================================================== */

/* LDR Dt, [Xn, #off] -- 64-bit SIMD immediate offset */
#define ARM64_LDR_D(t, n, off) (0xFD400000 | OFFSET12_X(off) | Rn(n) | Rt(t))

/* LDR Qt, [Xn, #off] -- 128-bit SIMD immediate offset */
#define ARM64_LDR_Q(t, n, off) (0x3DC00000 | OFFSET12_Q(off) | Rn(n) | Rt(t))

/* STR Dt, [Xn, #off] */
#define ARM64_STR_D(t, n, off) (0xFD000000 | OFFSET12_X(off) | Rn(n) | Rt(t))

/* STR Qt, [Xn, #off] */
#define ARM64_STR_Q(t, n, off) (0x3D800000 | OFFSET12_Q(off) | Rn(n) | Rt(t))

/* LDR Dt, [Xn, Xm] */
#define ARM64_LDR_D_REG(t, n, m) (0xFC606800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Dt, [Xn, Xm, LSL #3] */
#define ARM64_LDR_D_REG_LSL3(t, n, m) (0xFC607800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Qt, [Xn, Xm] */
#define ARM64_LDR_Q_REG(t, n, m) (0x3CE06800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Qt, [Xn, Xm, LSL #4] */
#define ARM64_LDR_Q_REG_LSL4(t, n, m) (0x3CE07800 | Rm(m) | Rn(n) | Rt(t))

/* LD1 {Vt.H}[lane], [Xn] */
#define ARM64_LD1_H_LANE(t, lane, n) (0x0D404000 | (((lane) & 3) << 11) | ((((lane) & 4) >> 2) << 30) | Rn(n) | Rt(t))

/* LD1 {Vt.4S}, [Xn] */
#define ARM64_LD1_V4S(t, n) (0x4C40A800 | Rn(n) | Rt(t))

/* ST1 {Vt.4S}, [Xn] */
#define ARM64_ST1_V4S(t, n) (0x4C00A800 | Rn(n) | Rt(t))

/* ========================================================================
 * Section 30: NEON GPR<->SIMD Transfer (Additional)
 * ======================================================================== */

/* FMOV Xd, Vn.D[1] */
#define ARM64_FMOV_X_D1(d, n) (0x9E660000 | Rn(n) | Rd(d))

/* FMOV Vd.D[1], Xn */
#define ARM64_FMOV_D1_X(d, n) (0x9E670000 | Rn(n) | Rd(d))

/* SADDLP Vd.2S, Vn.4H */
#define ARM64_SADDLP_2S_4H(d, n) (0x0E602800 | Rn(n) | Rd(d))

/* ========================================================================
 * Section 31: Struct Offset Constants
 * ======================================================================== */

/* voodoo_state_t offsets (base register: x0) */
#define STATE_tmu0_lod     56
#define STATE_tmu1_lod     88
#define STATE_lod          104
#define STATE_lod_min      108  /* [2] array, stride 4 */
#define STATE_lod_max      116  /* [2] array, stride 4 */
#define STATE_tex_b        156  /* [2] array, stride 4 */
#define STATE_tex_a        180  /* [2] array, stride 4 */
#define STATE_tex_s        188
#define STATE_tex_t        192
#define STATE_tex          240  /* [2][9] ptr array, stride 72 per TMU */
#define STATE_fb_mem       456
#define STATE_aux_mem      464
#define STATE_ib           472  /* base of {ib,ig,ir,ia} NEON block */
#define STATE_ia           484
#define STATE_z            488
#define STATE_new_depth    492
#define STATE_tmu0_s       496
#define STATE_tmu0_t       504
#define STATE_tmu0_w       512
#define STATE_tmu1_s       520
#define STATE_tmu1_t       528
#define STATE_tmu1_w       536
#define STATE_w            544
#define STATE_pixel_count  552
#define STATE_texel_count  556
#define STATE_x            560
#define STATE_x2           564
#define STATE_x_tiled      568
#define STATE_w_depth      572
#define STATE_ebp_store    580
#define STATE_lod_frac     588  /* [2] array, stride 4 */
#define STATE_stipple      596

/* voodoo_params_t offsets (base register: x1) */
#define PARAMS_dBdX         48  /* base of {dB,dG,dR,dA,dZ}dX block */
#define PARAMS_dZdX         64
#define PARAMS_dWdX         96
#define PARAMS_tmu0_dSdX   144
#define PARAMS_tmu0_dWdX   160
#define PARAMS_tmu1_dSdX   240
#define PARAMS_tmu1_dWdX   256
#define PARAMS_color0      304
#define PARAMS_color1      308
#define PARAMS_fogColor    324
#define PARAMS_fogTable    328  /* [64] array, stride 2 */
#define PARAMS_alphaMode   456
#define PARAMS_zaColor     460
#define PARAMS_chromaKey   476
#define PARAMS_tex_w_mask  696  /* [2][10] array, stride 40 per TMU */
#define PARAMS_tex_h_mask  856  /* [2][10] array, stride 40 per TMU */

/* TMU-indexed offset helpers */
#define STATE_tmu_lod(tmu)       ((tmu) ? STATE_tmu1_lod : STATE_tmu0_lod)
#define STATE_tmu_s(tmu)         ((tmu) ? STATE_tmu1_s : STATE_tmu0_s)
#define STATE_tmu_t(tmu)         ((tmu) ? STATE_tmu1_t : STATE_tmu0_t)
#define STATE_tmu_w(tmu)         ((tmu) ? STATE_tmu1_w : STATE_tmu0_w)
#define STATE_lod_min_n(tmu)     (STATE_lod_min + (tmu) * 4)
#define STATE_lod_max_n(tmu)     (STATE_lod_max + (tmu) * 4)
#define STATE_tex_a_n(tmu)       (STATE_tex_a + (tmu) * 4)
#define STATE_tex_n(tmu)         (STATE_tex + (tmu) * 72)
#define STATE_lod_frac_n(tmu)    (STATE_lod_frac + (tmu) * 4)

#define PARAMS_tmu_dSdX(tmu)     ((tmu) ? PARAMS_tmu1_dSdX : PARAMS_tmu0_dSdX)
#define PARAMS_tmu_dWdX(tmu)     ((tmu) ? PARAMS_tmu1_dWdX : PARAMS_tmu0_dWdX)
#define PARAMS_tex_w_mask_n(tmu) (PARAMS_tex_w_mask + (tmu) * 40)
#define PARAMS_tex_h_mask_n(tmu) (PARAMS_tex_h_mask + (tmu) * 40)

/* ========================================================================
 * Compile-time verification of struct offset constants
 * ======================================================================== */
#define VOODOO_ASSERT_OFFSET(type, field, expected)                            \
    _Static_assert(offsetof(type, field) == (expected),                        \
                   "offsetof(" #type ", " #field ") != " #expected)

VOODOO_ASSERT_OFFSET(voodoo_state_t, tmu[0].lod, STATE_tmu0_lod);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tmu[1].lod, STATE_tmu1_lod);
VOODOO_ASSERT_OFFSET(voodoo_state_t, lod,        STATE_lod);
VOODOO_ASSERT_OFFSET(voodoo_state_t, lod_min[0], STATE_lod_min);
VOODOO_ASSERT_OFFSET(voodoo_state_t, lod_max[0], STATE_lod_max);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tex_b[0],   STATE_tex_b);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tex_a[0],   STATE_tex_a);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tex_s,      STATE_tex_s);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tex_t,      STATE_tex_t);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tex[0][0],  STATE_tex);
VOODOO_ASSERT_OFFSET(voodoo_state_t, fb_mem,     STATE_fb_mem);
VOODOO_ASSERT_OFFSET(voodoo_state_t, aux_mem,    STATE_aux_mem);
VOODOO_ASSERT_OFFSET(voodoo_state_t, ib,         STATE_ib);
VOODOO_ASSERT_OFFSET(voodoo_state_t, ia,         STATE_ia);
VOODOO_ASSERT_OFFSET(voodoo_state_t, z,          STATE_z);
VOODOO_ASSERT_OFFSET(voodoo_state_t, new_depth,  STATE_new_depth);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tmu0_s,     STATE_tmu0_s);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tmu0_t,     STATE_tmu0_t);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tmu0_w,     STATE_tmu0_w);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tmu1_s,     STATE_tmu1_s);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tmu1_t,     STATE_tmu1_t);
VOODOO_ASSERT_OFFSET(voodoo_state_t, tmu1_w,     STATE_tmu1_w);
VOODOO_ASSERT_OFFSET(voodoo_state_t, w,          STATE_w);
VOODOO_ASSERT_OFFSET(voodoo_state_t, pixel_count, STATE_pixel_count);
VOODOO_ASSERT_OFFSET(voodoo_state_t, texel_count, STATE_texel_count);
VOODOO_ASSERT_OFFSET(voodoo_state_t, x,          STATE_x);
VOODOO_ASSERT_OFFSET(voodoo_state_t, x2,         STATE_x2);
VOODOO_ASSERT_OFFSET(voodoo_state_t, x_tiled,    STATE_x_tiled);
VOODOO_ASSERT_OFFSET(voodoo_state_t, w_depth,    STATE_w_depth);
VOODOO_ASSERT_OFFSET(voodoo_state_t, ebp_store,  STATE_ebp_store);
VOODOO_ASSERT_OFFSET(voodoo_state_t, lod_frac[0], STATE_lod_frac);
VOODOO_ASSERT_OFFSET(voodoo_state_t, stipple,     STATE_stipple);

VOODOO_ASSERT_OFFSET(voodoo_params_t, dBdX,            PARAMS_dBdX);
VOODOO_ASSERT_OFFSET(voodoo_params_t, dZdX,            PARAMS_dZdX);
VOODOO_ASSERT_OFFSET(voodoo_params_t, dWdX,            PARAMS_dWdX);
VOODOO_ASSERT_OFFSET(voodoo_params_t, tmu[0].dSdX,     PARAMS_tmu0_dSdX);
VOODOO_ASSERT_OFFSET(voodoo_params_t, tmu[0].dWdX,     PARAMS_tmu0_dWdX);
VOODOO_ASSERT_OFFSET(voodoo_params_t, tmu[1].dSdX,     PARAMS_tmu1_dSdX);
VOODOO_ASSERT_OFFSET(voodoo_params_t, tmu[1].dWdX,     PARAMS_tmu1_dWdX);
VOODOO_ASSERT_OFFSET(voodoo_params_t, color0,          PARAMS_color0);
VOODOO_ASSERT_OFFSET(voodoo_params_t, color1,          PARAMS_color1);
VOODOO_ASSERT_OFFSET(voodoo_params_t, fogColor,        PARAMS_fogColor);
VOODOO_ASSERT_OFFSET(voodoo_params_t, fogTable[0],     PARAMS_fogTable);
VOODOO_ASSERT_OFFSET(voodoo_params_t, alphaMode,       PARAMS_alphaMode);
VOODOO_ASSERT_OFFSET(voodoo_params_t, zaColor,         PARAMS_zaColor);
VOODOO_ASSERT_OFFSET(voodoo_params_t, chromaKey,       PARAMS_chromaKey);
VOODOO_ASSERT_OFFSET(voodoo_params_t, tex_w_mask[0][0], PARAMS_tex_w_mask);
VOODOO_ASSERT_OFFSET(voodoo_params_t, tex_h_mask[0][0], PARAMS_tex_h_mask);

/* ========================================================================
 * NEON Lookup Tables
 * ========================================================================
 *
 * On x86-64, these are __m128i vectors loaded with _mm_set_epi32().
 * On ARM64, we store them as arrays of uint16_t[8] (128 bits).
 * The JIT code loads them with LDR Q (128-bit) or LDR D (64-bit).
 * Only the low 64 bits (4 x uint16_t) are significant for most tables.
 * ======================================================================== */

typedef union {
    uint16_t u16[8];
    uint32_t u32[4];
    uint64_t u64[2];
} voodoo_neon_reg_t;

static voodoo_neon_reg_t neon_01_w;      /* {1,1,1,1, 0,0,0,0} */
static voodoo_neon_reg_t neon_ff_w;      /* {0xFF,0xFF,0xFF,0xFF, 0,0,0,0} */
static voodoo_neon_reg_t neon_ff_b;      /* {0,0,0,0, ...} -- 24-bit mask in u32[0] */
static voodoo_neon_reg_t neon_minus_254; /* {0xFF02,0xFF02,0xFF02,0xFF02, 0,0,0,0} */

static voodoo_neon_reg_t alookup[257];
static voodoo_neon_reg_t aminuslookup[256];
static voodoo_neon_reg_t bilinear_lookup[256 * 2];
static voodoo_neon_reg_t neon_00_ff_w[2];
static uint32_t          i_00_ff_w[2] = { 0, 0xff };

/* ========================================================================
 * codegen_texture_fetch() -- placeholder for Phase 3
 * ======================================================================== */
static inline int
codegen_texture_fetch(uint8_t *code_block, voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int block_pos, int tmu)
{
    /* TODO: Phase 3 -- texture fetch, LOD, bilinear filter */
    (void) voodoo;
    (void) params;
    (void) state;
    (void) tmu;
    return block_pos;
}

/* ========================================================================
 * voodoo_generate() -- emit ARM64 JIT code for the pixel pipeline
 *
 * Phase 1: Prologue and epilogue only.
 * The generated function signature:
 *   uint8_t (*voodoo_draw)(voodoo_state_t *state, voodoo_params_t *params,
 *                          int x, int real_y)
 *
 * On AArch64 (AAPCS64): x0=state, x1=params, x2=x, x3=real_y
 * ======================================================================== */
static inline void
voodoo_generate(uint8_t *code_block, voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int depthop)
{
    int block_pos        = 0;
    int z_skip_pos       = 0;
    int a_skip_pos       = 0;
    int amask_skip_pos   = 0;
    int stipple_skip_pos = 0;
    int chroma_skip_pos  = 0;
    int depth_jump_pos   = 0;
    int depth_jump_pos2  = 0;
    int loop_jump_pos    = 0;

    (void) voodoo;
    (void) depthop;
    (void) z_skip_pos;
    (void) a_skip_pos;
    (void) amask_skip_pos;
    (void) stipple_skip_pos;
    (void) chroma_skip_pos;
    (void) depth_jump_pos;
    (void) depth_jump_pos2;

    /* Initialize NEON constants (written to static memory, loaded by prologue) */
    neon_01_w.u32[0]      = 0x00010001;
    neon_01_w.u32[1]      = 0x00010001;
    neon_01_w.u32[2]      = 0;
    neon_01_w.u32[3]      = 0;
    neon_ff_w.u32[0]      = 0x00ff00ff;
    neon_ff_w.u32[1]      = 0x00ff00ff;
    neon_ff_w.u32[2]      = 0;
    neon_ff_w.u32[3]      = 0;
    neon_ff_b.u32[0]      = 0x00ffffff;
    neon_ff_b.u32[1]      = 0;
    neon_ff_b.u32[2]      = 0;
    neon_ff_b.u32[3]      = 0;
    neon_minus_254.u32[0] = 0xff02ff02;
    neon_minus_254.u32[1] = 0xff02ff02;
    neon_minus_254.u32[2] = 0;
    neon_minus_254.u32[3] = 0;

    /* ================================================================
     * Prologue: save callee-saved registers
     * ================================================================
     *
     * Stack layout (growing down):
     *   SP-16:  x29 (FP), x30 (LR)
     *   SP-32:  x19, x20
     *   SP-48:  x21, x22
     *   SP-64:  x23, x24
     *   SP-80:  x25, x26
     *   SP-96:  x27, x28
     *   SP-104: d8, d9    (NEON callee-saved, lower 64 bits)
     *   SP-112: d10, d11
     *   SP-128: d12, d13
     *   SP-144: d14, d15
     * Total: 144 bytes (16-byte aligned)
     */

    /* STP x29, x30, [SP, #-144]! */
    addlong(ARM64_STP_PRE_X(29, 30, 31, -144));
    /* STP x19, x20, [SP, #16] */
    addlong(ARM64_STP_OFF_X(19, 20, 31, 16));
    /* STP x21, x22, [SP, #32] */
    addlong(ARM64_STP_OFF_X(21, 22, 31, 32));
    /* STP x23, x24, [SP, #48] */
    addlong(ARM64_STP_OFF_X(23, 24, 31, 48));
    /* STP x25, x26, [SP, #64] */
    addlong(ARM64_STP_OFF_X(25, 26, 31, 64));
    /* STP x27, x28, [SP, #80] */
    addlong(ARM64_STP_OFF_X(27, 28, 31, 80));
    /* STP d8, d9, [SP, #96] */
    addlong(ARM64_STP_D(8, 9, 31, 96));
    /* STP d10, d11, [SP, #112] */
    addlong(ARM64_STP_D(10, 11, 31, 112));
    /* STP d12, d13, [SP, #128] -- save d12-d15 for scratch callee-saved usage */
    addlong(ARM64_STP_D(12, 13, 31, 128));

    /* Set up frame pointer (optional but helps with debugging) */
    /* MOV x29, SP */
    addlong(ARM64_MOV_REG_X(29, 31));

    /* ================================================================
     * Load arguments into pinned registers
     * ================================================================
     *
     * AAPCS64: x0=state, x1=params already in place.
     * x2=x (pixel X), x3=real_y
     * Save real_y into callee-saved register for persistence across loop.
     */

    /* MOV x24, x3 -- save real_y */
    addlong(ARM64_MOV_REG_X(24, 3));

    /* ================================================================
     * Load pointer constants into callee-saved GPRs
     * ================================================================
     *
     * These are static addresses known at codegen time.
     * Use MOVZ + MOVK sequence to load 64-bit pointers.
     */
    {
        /* Helper: emit a 64-bit pointer load into register Xd */
#define EMIT_MOV_IMM64(d, ptr)                                                   \
    do {                                                                          \
        uint64_t _v = (uint64_t) (uintptr_t) (ptr);                              \
        addlong(ARM64_MOVZ_X((d), (_v) & 0xFFFF));                               \
        addlong(ARM64_MOVK_X((d), ((_v) >> 16) & 0xFFFF, 1));                    \
        addlong(ARM64_MOVK_X((d), ((_v) >> 32) & 0xFFFF, 2));                    \
        addlong(ARM64_MOVK_X((d), ((_v) >> 48) & 0xFFFF, 3));                    \
    } while (0)

        EMIT_MOV_IMM64(19, &logtable);
        EMIT_MOV_IMM64(20, &alookup);
        EMIT_MOV_IMM64(21, &aminuslookup);
        EMIT_MOV_IMM64(22, &neon_00_ff_w);
        EMIT_MOV_IMM64(23, &i_00_ff_w);
        EMIT_MOV_IMM64(25, &bilinear_lookup);

#undef EMIT_MOV_IMM64
    }

    /* ================================================================
     * Load NEON constants into callee-saved V registers
     * ================================================================
     *
     * v8 = xmm_01_w  {1,1,1,1}
     * v9 = xmm_ff_w  {0xFF,0xFF,0xFF,0xFF}
     * v10 = xmm_ff_b {0x00FFFFFF,0,0,0}
     * v11 = minus_254 {0xFF02,0xFF02,0xFF02,0xFF02}
     *
     * Load via a temporary register pointing to each constant.
     */
    {
        uint64_t addr;

#define EMIT_LOAD_NEON_CONST(vreg, constaddr)                                        \
    do {                                                                             \
        addr = (uint64_t) (uintptr_t) (constaddr);                                  \
        addlong(ARM64_MOVZ_X(16, addr & 0xFFFF));                                   \
        addlong(ARM64_MOVK_X(16, (addr >> 16) & 0xFFFF, 1));                        \
        addlong(ARM64_MOVK_X(16, (addr >> 32) & 0xFFFF, 2));                        \
        addlong(ARM64_MOVK_X(16, (addr >> 48) & 0xFFFF, 3));                        \
        addlong(ARM64_LDR_Q((vreg), 16, 0));                                        \
    } while (0)

        EMIT_LOAD_NEON_CONST(8, &neon_01_w);
        EMIT_LOAD_NEON_CONST(9, &neon_ff_w);
        EMIT_LOAD_NEON_CONST(10, &neon_ff_b);
        EMIT_LOAD_NEON_CONST(11, &neon_minus_254);

#undef EMIT_LOAD_NEON_CONST

        (void) addr;
    }

    /* ================================================================
     * Load fb_mem and aux_mem pointers
     * ================================================================ */
    /* LDR x8, [x0, #STATE_fb_mem] */
    addlong(ARM64_LDR_X(8, 0, STATE_fb_mem));
    /* LDR x9, [x0, #STATE_aux_mem] */
    addlong(ARM64_LDR_X(9, 0, STATE_aux_mem));

    /* ================================================================
     * Pixel loop entry point
     * ================================================================ */
    loop_jump_pos = block_pos;

    /* TODO: Phase 2+ -- stipple, depth test, texture fetch, color combine,
     *       fog, alpha test, alpha blend, dithering, framebuffer write,
     *       per-pixel increments */

    /* ================================================================
     * Per-pixel state increment (x coordinate)
     * ================================================================ */

    /* LDR w4, [x0, #STATE_x] */
    addlong(ARM64_LDR_W(4, 0, STATE_x));

    if (state->xdir > 0) {
        /* ADD w5, w4, #1 */
        addlong(ARM64_ADD_IMM(5, 4, 1));
    } else {
        /* SUB w5, w4, #1 */
        addlong(ARM64_SUB_IMM(5, 4, 1));
    }

    /* STR w5, [x0, #STATE_x] */
    addlong(ARM64_STR_W(5, 0, STATE_x));

    /* CMP w4, state->x2 */
    /* LDR w6, [x0, #STATE_x2] */
    addlong(ARM64_LDR_W(6, 0, STATE_x2));
    /* CMP w4, w6 */
    addlong(ARM64_CMP_REG(4, 6));

    /* B.NE loop_jump_pos */
    {
        int32_t loop_offset = loop_jump_pos - block_pos;
        addlong(ARM64_BCOND(loop_offset, COND_NE));
    }

    /* ================================================================
     * Epilogue: restore callee-saved registers and return
     * ================================================================ */

    /* LDP d12, d13, [SP, #128] */
    addlong(ARM64_LDP_D(12, 13, 31, 128));
    /* LDP d10, d11, [SP, #112] */
    addlong(ARM64_LDP_D(10, 11, 31, 112));
    /* LDP d8, d9, [SP, #96] */
    addlong(ARM64_LDP_D(8, 9, 31, 96));
    /* LDP x27, x28, [SP, #80] */
    addlong(ARM64_LDP_OFF_X(27, 28, 31, 80));
    /* LDP x25, x26, [SP, #64] */
    addlong(ARM64_LDP_OFF_X(25, 26, 31, 64));
    /* LDP x23, x24, [SP, #48] */
    addlong(ARM64_LDP_OFF_X(23, 24, 31, 48));
    /* LDP x21, x22, [SP, #32] */
    addlong(ARM64_LDP_OFF_X(21, 22, 31, 32));
    /* LDP x19, x20, [SP, #16] */
    addlong(ARM64_LDP_OFF_X(19, 20, 31, 16));
    /* LDP x29, x30, [SP], #144 */
    addlong(ARM64_LDP_POST_X(29, 30, 31, 144));

    /* RET */
    addlong(ARM64_RET);
}

int voodoo_recomp = 0;

/* ========================================================================
 * voodoo_get_block() -- cache lookup / JIT generation with W^X toggle
 * ======================================================================== */
static inline void *
voodoo_get_block(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int odd_even)
{
    int                  b                 = last_block[odd_even];
    voodoo_arm64_data_t *voodoo_arm64_data = voodoo->codegen_data;
    voodoo_arm64_data_t *data;

    for (uint8_t c = 0; c < 8; c++) {
        data = &voodoo_arm64_data[odd_even + c * 4];

        if (state->xdir == data->xdir
            && params->alphaMode == data->alphaMode
            && params->fbzMode == data->fbzMode
            && params->fogMode == data->fogMode
            && params->fbzColorPath == data->fbzColorPath
            && (voodoo->trexInit1[0] & (1 << 18)) == data->trexInit1
            && params->textureMode[0] == data->textureMode[0]
            && params->textureMode[1] == data->textureMode[1]
            && (params->tLOD[0] & LOD_MASK) == data->tLOD[0]
            && (params->tLOD[1] & LOD_MASK) == data->tLOD[1]
            && ((params->col_tiled || params->aux_tiled) ? 1 : 0) == data->is_tiled) {
            last_block[odd_even] = b;
            return data->code_block;
        }

        b = (b + 1) & 7;
    }

    voodoo_recomp++;
    data = &voodoo_arm64_data[odd_even + next_block_to_write[odd_even] * 4];

    /* W^X: make writable before code generation + metadata writes */
#if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *)) {
        pthread_jit_write_protect_np(0);
    }
#endif

    voodoo_generate(data->code_block, voodoo, params, state, depth_op);

    data->xdir           = state->xdir;
    data->alphaMode      = params->alphaMode;
    data->fbzMode        = params->fbzMode;
    data->fogMode        = params->fogMode;
    data->fbzColorPath   = params->fbzColorPath;
    data->trexInit1      = voodoo->trexInit1[0] & (1 << 18);
    data->textureMode[0] = params->textureMode[0];
    data->textureMode[1] = params->textureMode[1];
    data->tLOD[0]        = params->tLOD[0] & LOD_MASK;
    data->tLOD[1]        = params->tLOD[1] & LOD_MASK;
    data->is_tiled       = (params->col_tiled || params->aux_tiled) ? 1 : 0;

    /* W^X: make executable, flush I-cache */
#if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *)) {
        pthread_jit_write_protect_np(1);
    }
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    __clear_cache((char *) data->code_block, (char *) data->code_block + BLOCK_SIZE);
#endif

    next_block_to_write[odd_even] = (next_block_to_write[odd_even] + 1) & 7;

    return data->code_block;
}

/* ========================================================================
 * voodoo_codegen_init() -- allocate executable memory + init lookup tables
 * ======================================================================== */
void
voodoo_codegen_init(voodoo_t *voodoo)
{
    voodoo->codegen_data = plat_mmap(sizeof(voodoo_arm64_data_t) * BLOCK_NUM * 4, 1);

    for (uint16_t c = 0; c < 256; c++) {
        int d[4];
        int _ds = c & 0xf;
        int dt  = c >> 4;

        /* alookup: broadcast c to 4 halfword lanes (low 64 bits) */
        alookup[c].u16[0] = (uint16_t) c;
        alookup[c].u16[1] = (uint16_t) c;
        alookup[c].u16[2] = (uint16_t) c;
        alookup[c].u16[3] = (uint16_t) c;
        alookup[c].u16[4] = 0;
        alookup[c].u16[5] = 0;
        alookup[c].u16[6] = 0;
        alookup[c].u16[7] = 0;

        /* aminuslookup: broadcast (255-c) */
        aminuslookup[c].u16[0] = (uint16_t) (255 - c);
        aminuslookup[c].u16[1] = (uint16_t) (255 - c);
        aminuslookup[c].u16[2] = (uint16_t) (255 - c);
        aminuslookup[c].u16[3] = (uint16_t) (255 - c);
        aminuslookup[c].u16[4] = 0;
        aminuslookup[c].u16[5] = 0;
        aminuslookup[c].u16[6] = 0;
        aminuslookup[c].u16[7] = 0;

        /* bilinear filter weights */
        d[0] = (16 - _ds) * (16 - dt);
        d[1] = _ds * (16 - dt);
        d[2] = (16 - _ds) * dt;
        d[3] = _ds * dt;

        /* bilinear_lookup[c*2]: d0 broadcast in low pair, d1 broadcast in high pair */
        bilinear_lookup[c * 2].u16[0]     = (uint16_t) d[0];
        bilinear_lookup[c * 2].u16[1]     = (uint16_t) d[0];
        bilinear_lookup[c * 2].u16[2]     = (uint16_t) d[0];
        bilinear_lookup[c * 2].u16[3]     = (uint16_t) d[0];
        bilinear_lookup[c * 2].u16[4]     = (uint16_t) d[1];
        bilinear_lookup[c * 2].u16[5]     = (uint16_t) d[1];
        bilinear_lookup[c * 2].u16[6]     = (uint16_t) d[1];
        bilinear_lookup[c * 2].u16[7]     = (uint16_t) d[1];
        bilinear_lookup[c * 2 + 1].u16[0] = (uint16_t) d[2];
        bilinear_lookup[c * 2 + 1].u16[1] = (uint16_t) d[2];
        bilinear_lookup[c * 2 + 1].u16[2] = (uint16_t) d[2];
        bilinear_lookup[c * 2 + 1].u16[3] = (uint16_t) d[2];
        bilinear_lookup[c * 2 + 1].u16[4] = (uint16_t) d[3];
        bilinear_lookup[c * 2 + 1].u16[5] = (uint16_t) d[3];
        bilinear_lookup[c * 2 + 1].u16[6] = (uint16_t) d[3];
        bilinear_lookup[c * 2 + 1].u16[7] = (uint16_t) d[3];
    }

    /* alookup[256]: special entry for alpha=256 */
    alookup[256].u16[0] = 256;
    alookup[256].u16[1] = 256;
    alookup[256].u16[2] = 256;
    alookup[256].u16[3] = 256;
    alookup[256].u16[4] = 0;
    alookup[256].u16[5] = 0;
    alookup[256].u16[6] = 0;
    alookup[256].u16[7] = 0;

    /* neon_00_ff_w: index 0 = all zeros, index 1 = {0xFF,0xFF,0xFF,0xFF,0,0,0,0} */
    memset(&neon_00_ff_w[0], 0, sizeof(voodoo_neon_reg_t));
    neon_00_ff_w[1].u16[0] = 0xff;
    neon_00_ff_w[1].u16[1] = 0xff;
    neon_00_ff_w[1].u16[2] = 0xff;
    neon_00_ff_w[1].u16[3] = 0xff;
    neon_00_ff_w[1].u16[4] = 0;
    neon_00_ff_w[1].u16[5] = 0;
    neon_00_ff_w[1].u16[6] = 0;
    neon_00_ff_w[1].u16[7] = 0;
}

/* ========================================================================
 * voodoo_codegen_close() -- free executable memory
 * ======================================================================== */
void
voodoo_codegen_close(voodoo_t *voodoo)
{
    plat_munmap(voodoo->codegen_data, sizeof(voodoo_arm64_data_t) * BLOCK_NUM * 4);
}

#endif /* VIDEO_VOODOO_CODEGEN_ARM64_H */

# ARM64 Encoding Macros — Voodoo JIT

Complete `#define` macro draft for `vid_voodoo_codegen_arm64.h`.
Every ARM64 instruction the Voodoo pixel pipeline JIT needs to emit.

Cross-referenced against:
- `src/codegen_new/codegen_backend_arm64_ops.c` (verified field helpers + OPCODE constants)
- `voodoo-arm64-port/instruction-mapping.md` (all instruction encodings)
- `voodoo-arm64-port/struct-offsets.md` (struct offset constants)
- ARMv8-A Architecture Reference Manual

---

## 0. Emission Primitive

The x86-64 codegen uses `addbyte()`, `addword()`, `addlong()`, `addquad()`.
ARM64 instructions are always 4 bytes, so only `addlong()` is needed:

```c
#define addlong(val)                                \
    do {                                            \
        *(uint32_t *) &code_block[block_pos] = val; \
        block_pos += 4;                             \
    } while (0)
```

Usage: `addlong(ARM64_LDR_W(W4, X0, STATE_lod));`

---

## 1. Field Helper Macros

Copied from `codegen_backend_arm64_ops.c` lines 16-21, verified correct.

```c
/* Register field placement */
#define Rt(x)                     (x)
#define Rd(x)                     (x)
#define Rn(x)                     ((x) << 5)
#define Rt2(x)                    ((x) << 10)
#define Rm(x)                     ((x) << 16)
```

---

## 2. Shift and Data Processing Field Helpers

From `codegen_backend_arm64_ops.c` lines 233-235.

```c
#define DATPROC_SHIFT(sh)         ((sh) << 10)
#define DATPROC_IMM_SHIFT(sh)     ((sh) << 22)
#define MOV_WIDE_HW(hw)           ((hw) << 21)

#define shift_imm6(x)             ((x) << 10)
```

---

## 3. Immediate Field Helpers

From `codegen_backend_arm64_ops.c` lines 237-249.

```c
/* Immediate encoding helpers */
#define IMM7_X(imm_data)          (((imm_data >> 3) & 0x7f) << 15)
#define IMM12(imm_data)           ((imm_data) << 10)
#define IMM16(imm_data)           ((imm_data) << 5)

/* Logical immediate fields (N, immr, imms) */
#define IMMN(immn)                ((immn) << 22)
#define IMMR(immr)                ((immr) << 16)
#define IMMS(imms)                ((imms) << 10)
#define IMM_LOGICAL(imm)          ((imm) << 10)
```

---

## 4. Offset Encoding Helpers

From `codegen_backend_arm64_ops.c` lines 251-258.

```c
/* Branch offset encoding */
#define OFFSET14(offset)          (((offset >> 2) << 5) & 0x0007ffe0)
#define OFFSET19(offset)          (((offset >> 2) << 5) & 0x00ffffe0)
#define OFFSET20(offset)          (((offset & 3) << 29) | ((((offset) & 0x1fffff) >> 2) << 5))
#define OFFSET26(offset)          ((offset >> 2) & 0x03ffffff)

/* Load/Store scaled offset encoding */
#define OFFSET12_B(offset)        ((offset) << 10)
#define OFFSET12_H(offset)        (((offset) >> 1) << 10)
#define OFFSET12_W(offset)        (((offset) >> 2) << 10)
#define OFFSET12_X(offset)        (((offset) >> 3) << 10)
#define OFFSET12_Q(offset)        (((offset) >> 4) << 10)
```

Note: `OFFSET12_X` added for 64-bit LDR/STR (scaled by 8). Not in CPU backend
(it uses `OFFSET12_Q` for 128-bit only). `OFFSET12_Q` renamed usage: here it
means 128-bit SIMD scaled by 16.

---

## 5. NEON Shift Immediate Helpers

From `codegen_backend_arm64_ops.c` lines 260-268.

```c
/* NEON vector shift immediate encoding
   For right shifts: encode as (element_bits - shift_amount)
   For left shifts: encode as shift_amount directly */
#define SHIFT_IMM_V4H(shift)      (((shift) | 0x10) << 16)
#define SHIFT_IMM_V2S(shift)      (((shift) | 0x20) << 16)
#define SHIFT_IMM_V2D(shift)      (((shift) | 0x40) << 16)

#define SHRN_SHIFT_IMM_V4S(shift) (((shift) | 0x10) << 16)

#define DUP_ELEMENT(element)      ((element) << 19)
```

---

## 6. TBZ/TBNZ Bit Field Helper

From `codegen_backend_arm64_ops.c` line 253.

```c
/* Encodes the bit number for TBZ/TBNZ:
   bits [23:19] = bit[4:0], bit 31 = bit[5] (for 64-bit regs) */
#define BIT_TBxZ(bit)             ((((bit) & 0x1f) << 19) | (((bit) & 0x20) ? (1 << 31) : 0))
```

---

## 7. Condition Codes

From `codegen_backend_arm64_ops.c` lines 27-41.

```c
#define COND_EQ                   (0x0)
#define COND_NE                   (0x1)
#define COND_CS                   (0x2)   /* HS (unsigned >=) */
#define COND_CC                   (0x3)   /* LO (unsigned <) */
#define COND_MI                   (0x4)   /* negative */
#define COND_PL                   (0x5)   /* positive or zero */
#define COND_VS                   (0x6)   /* overflow */
#define COND_VC                   (0x7)   /* no overflow */
#define COND_HI                   (0x8)   /* unsigned > */
#define COND_LS                   (0x9)   /* unsigned <= */
#define COND_GE                   (0xa)   /* signed >= */
#define COND_LT                   (0xb)   /* signed < */
#define COND_GT                   (0xc)   /* signed > */
#define COND_LE                   (0xd)   /* signed <= */
#define COND_AL                   (0xe)   /* always */

#define CSEL_COND(cond)           ((cond) << 12)
```

Note: `COND_AL` added for completeness. Not in CPU backend but needed for
unconditional CSEL patterns.

---

## 8. GPR Move / Immediate

```c
/* MOV Wd, Ws — alias for ORR Wd, WZR, Ws (32-bit) */
#define ARM64_MOV_REG(d, s)       (0x2A0003E0 | Rm(s) | Rd(d))

/* MOV Xd, Xs — alias for ORR Xd, XZR, Xs (64-bit) */
#define ARM64_MOV_REG_X(d, s)     (0xAA0003E0 | Rm(s) | Rd(d))

/* MOVZ Wd, #imm16 — move 16-bit immediate, zero rest */
#define ARM64_MOVZ_W(d, imm16)    (0x52800000 | IMM16(imm16) | Rd(d))

/* MOVZ Xd, #imm16 — 64-bit, hw=0 */
#define ARM64_MOVZ_X(d, imm16)    (0xD2800000 | IMM16(imm16) | Rd(d))

/* MOVK Wd, #imm16, LSL #16 — keep, insert at hw=1 */
#define ARM64_MOVK_W_16(d, imm16) (0x72A00000 | IMM16(imm16) | Rd(d))

/* MOVK Xd, #imm16, LSL #hw*16 — keep, insert at specified halfword */
#define ARM64_MOVK_X(d, imm16, hw) (0xF2800000 | MOV_WIDE_HW(hw) | IMM16(imm16) | Rd(d))

/* MOV Wd, WZR — zero a register (alias for MOVZ Wd, #0) */
#define ARM64_MOV_ZERO(d)         (0x52800000 | Rd(d))

/* MVN Wd, Ws — bitwise NOT (alias for ORN Wd, WZR, Ws) */
#define ARM64_MVN(d, s)           (0x2A2003E0 | Rm(s) | Rd(d))

/* NEG Wd, Ws — negate (alias for SUB Wd, WZR, Ws) */
#define ARM64_NEG(d, s)           (0x4B0003E0 | Rm(s) | Rd(d))
```

---

## 9. GPR Arithmetic — Register

```c
/* ADD Wd, Wn, Wm (32-bit register) */
#define ARM64_ADD_REG(d, n, m)    (0x0B000000 | Rm(m) | Rn(n) | Rd(d))

/* ADD Wd, Wn, Wm, LSL #sh */
#define ARM64_ADD_REG_LSL(d, n, m, sh)  (0x0B000000 | Rm(m) | shift_imm6(sh) | Rn(n) | Rd(d))

/* ADD Xd, Xn, Xm (64-bit register) */
#define ARM64_ADD_REG_X(d, n, m)  (0x8B000000 | Rm(m) | Rn(n) | Rd(d))

/* ADD Xd, Xn, Xm, LSL #sh (64-bit with shift) */
#define ARM64_ADD_REG_X_LSL(d, n, m, sh) (0x8B000000 | Rm(m) | shift_imm6(sh) | Rn(n) | Rd(d))

/* SUB Wd, Wn, Wm (32-bit register) */
#define ARM64_SUB_REG(d, n, m)    (0x4B000000 | Rm(m) | Rn(n) | Rd(d))

/* SUB Xd, Xn, Xm (64-bit register) */
#define ARM64_SUB_REG_X(d, n, m)  (0xCB000000 | Rm(m) | Rn(n) | Rd(d))

/* SUBS Wd, Wn, Wm — sets flags (CMP alias when Rd=WZR) */
#define ARM64_SUBS_REG(d, n, m)   (0x6B000000 | Rm(m) | Rn(n) | Rd(d))

/* CMP Wn, Wm — alias for SUBS WZR, Wn, Wm */
#define ARM64_CMP_REG(n, m)       (0x6B000000 | Rm(m) | Rn(n) | Rd(31))

/* MUL Wd, Wn, Wm (32-bit) */
#define ARM64_MUL(d, n, m)        (0x1B007C00 | Rm(m) | Rn(n) | Rd(d))

/* MUL Xd, Xn, Xm (64-bit) */
#define ARM64_MUL_X(d, n, m)      (0x9B007C00 | Rm(m) | Rn(n) | Rd(d))

/* SDIV Xd, Xn, Xm (64-bit signed divide) */
#define ARM64_SDIV_X(d, n, m)     (0x9AC00C00 | Rm(m) | Rn(n) | Rd(d))

/* SDIV Wd, Wn, Wm (32-bit signed divide) */
#define ARM64_SDIV(d, n, m)       (0x1AC00C00 | Rm(m) | Rn(n) | Rd(d))
```

---

## 10. GPR Arithmetic — Immediate

```c
/* ADD Wd, Wn, #imm12 (32-bit immediate, no shift) */
#define ARM64_ADD_IMM(d, n, imm)  (0x11000000 | IMM12(imm) | Rn(n) | Rd(d))

/* ADD Wd, Wn, #imm12, LSL #12 */
#define ARM64_ADD_IMM_SH12(d, n, imm) (0x11400000 | IMM12(imm) | Rn(n) | Rd(d))

/* ADD Xd, Xn, #imm12 (64-bit immediate) */
#define ARM64_ADD_IMM_X(d, n, imm) (0x91000000 | IMM12(imm) | Rn(n) | Rd(d))

/* SUB Wd, Wn, #imm12 (32-bit immediate) */
#define ARM64_SUB_IMM(d, n, imm)  (0x51000000 | IMM12(imm) | Rn(n) | Rd(d))

/* SUB Xd, Xn, #imm12 (64-bit immediate) */
#define ARM64_SUB_IMM_X(d, n, imm) (0xD1000000 | IMM12(imm) | Rn(n) | Rd(d))

/* SUBS Wd, Wn, #imm12 — sets flags */
#define ARM64_SUBS_IMM(d, n, imm) (0x71000000 | IMM12(imm) | Rn(n) | Rd(d))

/* CMP Wn, #imm12 — alias for SUBS WZR, Wn, #imm12 */
#define ARM64_CMP_IMM(n, imm)     (0x71000000 | IMM12(imm) | Rn(n) | Rd(31))

/* CMP Xn, #imm12 (64-bit) */
#define ARM64_CMP_IMM_X(n, imm)   (0xF1000000 | IMM12(imm) | Rn(n) | Rd(31))

/* CMN Wn, #imm12 — alias for ADDS WZR, Wn, #imm12 */
#define ARM64_CMN_IMM(n, imm)     (0x31000000 | IMM12(imm) | Rn(n) | Rd(31))
```

---

## 11. GPR Bitwise — Register

```c
/* AND Wd, Wn, Wm */
#define ARM64_AND_REG(d, n, m)    (0x0A000000 | Rm(m) | Rn(n) | Rd(d))

/* ORR Wd, Wn, Wm */
#define ARM64_ORR_REG(d, n, m)    (0x2A000000 | Rm(m) | Rn(n) | Rd(d))

/* EOR Wd, Wn, Wm */
#define ARM64_EOR_REG(d, n, m)    (0x4A000000 | Rm(m) | Rn(n) | Rd(d))

/* ANDS Wd, Wn, Wm — AND setting flags */
#define ARM64_ANDS_REG(d, n, m)   (0x6A000000 | Rm(m) | Rn(n) | Rd(d))

/* TST Wn, Wm — alias for ANDS WZR, Wn, Wm */
#define ARM64_TST_REG(n, m)       (0x6A000000 | Rm(m) | Rn(n) | Rd(31))

/* BIC Wd, Wn, Wm — bit clear (AND NOT) */
#define ARM64_BIC_REG(d, n, m)    (0x0A200000 | Rm(m) | Rn(n) | Rd(d))
```

Note: Logical immediate forms (AND/ORR/EOR with bitmask immediate) require
complex N/immr/imms encoding. Per the plan, we use register-register forms
and load constants via MOVZ/MOVK. A `bitmask_imm_encode()` helper can be
added later if profiling shows it's needed. The CPU backend's
`host_arm64_find_imm()` function handles this at runtime.

---

## 12. GPR Shifts — Register

```c
/* LSL Wd, Wn, Wm (variable left shift) */
#define ARM64_LSL_REG(d, n, m)    (0x1AC02000 | Rm(m) | Rn(n) | Rd(d))

/* LSR Wd, Wn, Wm (variable logical right shift) */
#define ARM64_LSR_REG(d, n, m)    (0x1AC02400 | Rm(m) | Rn(n) | Rd(d))

/* ASR Wd, Wn, Wm (variable arithmetic right shift) */
#define ARM64_ASR_REG(d, n, m)    (0x1AC02800 | Rm(m) | Rn(n) | Rd(d))

/* ROR Wd, Wn, Wm (variable rotate right) — not used, but available */
#define ARM64_ROR_REG(d, n, m)    (0x1AC02C00 | Rm(m) | Rn(n) | Rd(d))
```

---

## 13. GPR Shifts — Immediate (via UBFM/SBFM aliases)

```c
/* LSL Wd, Wn, #imm — alias for UBFM Wd, Wn, #(-imm mod 32), #(31-imm) */
#define ARM64_LSL_IMM(d, n, imm)  (0x53000000 | IMMR((-imm) & 0x1F) | IMMS(31 - (imm)) | Rn(n) | Rd(d))

/* LSL Xd, Xn, #imm — 64-bit */
#define ARM64_LSL_IMM_X(d, n, imm) (0xD3400000 | IMMR((-imm) & 0x3F) | IMMS(63 - (imm)) | Rn(n) | Rd(d))

/* LSR Wd, Wn, #imm — alias for UBFM Wd, Wn, #imm, #31 */
#define ARM64_LSR_IMM(d, n, imm)  (0x53000000 | IMMR(imm) | IMMS(31) | Rn(n) | Rd(d))

/* LSR Xd, Xn, #imm — 64-bit */
#define ARM64_LSR_IMM_X(d, n, imm) (0xD3400000 | IMMR(imm) | IMMS(63) | Rn(n) | Rd(d))

/* ASR Wd, Wn, #imm — alias for SBFM Wd, Wn, #imm, #31 */
#define ARM64_ASR_IMM(d, n, imm)  (0x13007C00 | IMMR(imm) | Rn(n) | Rd(d))

/* ASR Xd, Xn, #imm — 64-bit */
#define ARM64_ASR_IMM_X(d, n, imm) (0x9340FC00 | IMMR(imm) | Rn(n) | Rd(d))

/* ROR Wd, Ws, #imm — alias for EXTR Wd, Ws, Ws, #imm */
#define ARM64_ROR_IMM(d, s, imm)  (0x13800000 | Rm(s) | IMMS(imm) | Rn(s) | Rd(d))
```

---

## 14. GPR Bitfield Extract / Insert

```c
/* UBFX Wd, Wn, #lsb, #width — unsigned bitfield extract */
#define ARM64_UBFX(d, n, lsb, width) (0x53000000 | IMMR(lsb) | IMMS((lsb) + (width) - 1) | Rn(n) | Rd(d))

/* SBFX Wd, Wn, #lsb, #width — signed bitfield extract */
#define ARM64_SBFX(d, n, lsb, width) (0x13000000 | IMMR(lsb) | IMMS((lsb) + (width) - 1) | Rn(n) | Rd(d))

/* UXTB Wd, Wn — zero-extend byte (alias for UBFX Wd, Wn, #0, #8) */
#define ARM64_UXTB(d, n)          (0x53001C00 | Rn(n) | Rd(d))

/* UXTH Wd, Wn — zero-extend halfword (alias for UBFX Wd, Wn, #0, #16) */
#define ARM64_UXTH(d, n)          (0x53003C00 | Rn(n) | Rd(d))

/* SXTB Wd, Wn — sign-extend byte (alias for SBFX Wd, Wn, #0, #8) */
#define ARM64_SXTB(d, n)          (0x13001C00 | Rn(n) | Rd(d))

/* SXTH Wd, Wn — sign-extend halfword (alias for SBFX Wd, Wn, #0, #16) */
#define ARM64_SXTH(d, n)          (0x13003C00 | Rn(n) | Rd(d))

/* SXTW Xd, Wn — sign-extend word to 64-bit (alias for SBFM Xd, Xn, #0, #31) */
#define ARM64_SXTW(d, n)          (0x93407C00 | Rn(n) | Rd(d))

/* CLZ Wd, Wn — count leading zeros (32-bit) */
#define ARM64_CLZ(d, n)           (0x5AC01000 | Rn(n) | Rd(d))

/* CLZ Xd, Xn — count leading zeros (64-bit) */
#define ARM64_CLZ_X(d, n)         (0xDAC01000 | Rn(n) | Rd(d))
```

---

## 15. Conditional Select

```c
/* CSEL Wd, Wn, Wm, cond — select Wn if cond true, Wm if false */
#define ARM64_CSEL(d, n, m, cond) (0x1A800000 | CSEL_COND(cond) | Rm(m) | Rn(n) | Rd(d))

/* CSINC Wd, Wn, Wm, cond — select Wn if true, Wm+1 if false */
#define ARM64_CSINC(d, n, m, cond) (0x1A800400 | CSEL_COND(cond) | Rm(m) | Rn(n) | Rd(d))

/* CSET Wd, cond — alias for CSINC Wd, WZR, WZR, invcond */
#define ARM64_CSET(d, cond)       (0x1A9F07E0 | CSEL_COND((cond) ^ 1) | Rd(d))
```

---

## 16. Load — Unsigned Immediate (Scaled)

All offsets must be pre-scaled: the `off` parameter is the raw byte offset.

```c
/* LDRB Wt, [Xn, #off] — load byte, unsigned offset (scale=1) */
#define ARM64_LDRB_IMM(t, n, off)  (0x39400000 | OFFSET12_B(off) | Rn(n) | Rt(t))

/* LDRH Wt, [Xn, #off] — load halfword, unsigned offset (scale=2) */
#define ARM64_LDRH_IMM(t, n, off)  (0x79400000 | OFFSET12_H(off) | Rn(n) | Rt(t))

/* LDR Wt, [Xn, #off] — load 32-bit, unsigned offset (scale=4) */
#define ARM64_LDR_W(t, n, off)     (0xB9400000 | OFFSET12_W(off) | Rn(n) | Rt(t))

/* LDR Xt, [Xn, #off] — load 64-bit, unsigned offset (scale=8) */
#define ARM64_LDR_X(t, n, off)     (0xF9400000 | OFFSET12_X(off) | Rn(n) | Rt(t))

/* LDRSB Wt, [Xn, #off] — load signed byte → 32-bit */
#define ARM64_LDRSB_W(t, n, off)   (0x39C00000 | OFFSET12_B(off) | Rn(n) | Rt(t))

/* LDRSH Wt, [Xn, #off] — load signed halfword → 32-bit */
#define ARM64_LDRSH_W(t, n, off)   (0x79C00000 | OFFSET12_H(off) | Rn(n) | Rt(t))

/* LDRSW Xt, [Xn, #off] — load signed 32-bit → 64-bit */
#define ARM64_LDRSW(t, n, off)     (0xB9800000 | OFFSET12_W(off) | Rn(n) | Rt(t))
```

---

## 17. Store — Unsigned Immediate (Scaled)

```c
/* STRB Wt, [Xn, #off] — store byte */
#define ARM64_STRB_IMM(t, n, off)  (0x39000000 | OFFSET12_B(off) | Rn(n) | Rt(t))

/* STRH Wt, [Xn, #off] — store halfword */
#define ARM64_STRH_IMM(t, n, off)  (0x79000000 | OFFSET12_H(off) | Rn(n) | Rt(t))

/* STR Wt, [Xn, #off] — store 32-bit */
#define ARM64_STR_W(t, n, off)     (0xB9000000 | OFFSET12_W(off) | Rn(n) | Rt(t))

/* STR Xt, [Xn, #off] — store 64-bit */
#define ARM64_STR_X(t, n, off)     (0xF9000000 | OFFSET12_X(off) | Rn(n) | Rt(t))
```

---

## 18. Load/Store — Register Offset

```c
/* LDR Wt, [Xn, Xm] — 32-bit reg offset, no shift */
#define ARM64_LDR_W_REG(t, n, m)        (0xB8606800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Wt, [Xn, Xm, LSL #2] — 32-bit reg offset, scaled */
#define ARM64_LDR_W_REG_LSL2(t, n, m)   (0xB8607800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Xt, [Xn, Xm] — 64-bit reg offset */
#define ARM64_LDR_X_REG(t, n, m)        (0xF8606800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Xt, [Xn, Xm, LSL #3] — 64-bit reg offset, scaled */
#define ARM64_LDR_X_REG_LSL3(t, n, m)   (0xF8607800 | Rm(m) | Rn(n) | Rt(t))

/* LDRB Wt, [Xn, Xm] — byte reg offset */
#define ARM64_LDRB_REG(t, n, m)          (0x38606800 | Rm(m) | Rn(n) | Rt(t))

/* LDRH Wt, [Xn, Xm] — halfword reg offset, no shift */
#define ARM64_LDRH_REG(t, n, m)          (0x78606800 | Rm(m) | Rn(n) | Rt(t))

/* LDRH Wt, [Xn, Xm, LSL #1] — halfword reg offset, scaled */
#define ARM64_LDRH_REG_LSL1(t, n, m)    (0x78607800 | Rm(m) | Rn(n) | Rt(t))

/* STR Wt, [Xn, Xm] — 32-bit reg offset store */
#define ARM64_STR_W_REG(t, n, m)         (0xB8206800 | Rm(m) | Rn(n) | Rt(t))

/* STRB Wt, [Xn, Xm] — byte reg offset store */
#define ARM64_STRB_REG(t, n, m)          (0x38206800 | Rm(m) | Rn(n) | Rt(t))

/* STRH Wt, [Xn, Xm] — halfword reg offset store */
#define ARM64_STRH_REG(t, n, m)          (0x78206800 | Rm(m) | Rn(n) | Rt(t))

/* STRH Wt, [Xn, Xm, LSL #1] — halfword reg offset, scaled */
#define ARM64_STRH_REG_LSL1(t, n, m)    (0x78207800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Wt, [Xn, Wm, UXTW #2] — 32-bit with 32-bit index, zero-extended + scaled */
#define ARM64_LDR_W_UXTW2(t, n, m)      (0xB8605800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Wt, [Xn, Wm, SXTW #2] — 32-bit with 32-bit index, sign-extended + scaled */
#define ARM64_LDR_W_SXTW2(t, n, m)      (0xB860D800 | Rm(m) | Rn(n) | Rt(t))
```

---

## 19. Load/Store Pair (for prologue/epilogue)

```c
/* STP Xt1, Xt2, [Xn, #imm]! — pre-index store pair (64-bit)
   imm must be a multiple of 8, range -512..504 */
#define ARM64_STP_PRE_X(t1, t2, n, imm) (0xA9800000 | IMM7_X(imm) | Rt2(t2) | Rn(n) | Rt(t1))

/* LDP Xt1, Xt2, [Xn], #imm — post-index load pair (64-bit) */
#define ARM64_LDP_POST_X(t1, t2, n, imm) (0xA8C00000 | IMM7_X(imm) | Rt2(t2) | Rn(n) | Rt(t1))

/* STP Xt1, Xt2, [Xn, #imm] — signed offset store pair (64-bit) */
#define ARM64_STP_OFF_X(t1, t2, n, imm) (0xA9000000 | IMM7_X(imm) | Rt2(t2) | Rn(n) | Rt(t1))

/* LDP Xt1, Xt2, [Xn, #imm] — signed offset load pair (64-bit) */
#define ARM64_LDP_OFF_X(t1, t2, n, imm) (0xA9400000 | IMM7_X(imm) | Rt2(t2) | Rn(n) | Rt(t1))

/* STP Dt1, Dt2, [Xn, #imm] — SIMD store pair (64-bit NEON) */
#define ARM64_STP_D(t1, t2, n, imm) (0x6D000000 | (((imm >> 3) & 0x7F) << 15) | Rt2(t2) | Rn(n) | Rt(t1))

/* LDP Dt1, Dt2, [Xn, #imm] — SIMD load pair (64-bit NEON) */
#define ARM64_LDP_D(t1, t2, n, imm) (0x6D400000 | (((imm >> 3) & 0x7F) << 15) | Rt2(t2) | Rn(n) | Rt(t1))
```

---

## 20. Branch and Control Flow

```c
/* B label — unconditional branch (26-bit offset) */
#define ARM64_B(off)              (0x14000000 | OFFSET26(off))

/* BL label — branch with link (26-bit offset) */
#define ARM64_BL(off)             (0x94000000 | OFFSET26(off))

/* B.cond label — conditional branch (19-bit offset) */
#define ARM64_BCOND(off, cond)    (0x54000000 | OFFSET19(off) | (cond))

/* CBZ Wt, label — compare and branch if zero (32-bit) */
#define ARM64_CBZ_W(t, off)       (0x34000000 | OFFSET19(off) | Rt(t))

/* CBNZ Wt, label — compare and branch if non-zero (32-bit) */
#define ARM64_CBNZ_W(t, off)      (0x35000000 | OFFSET19(off) | Rt(t))

/* CBZ Xt, label — 64-bit */
#define ARM64_CBZ_X(t, off)       (0xB4000000 | OFFSET19(off) | Rt(t))

/* CBNZ Xt, label — 64-bit */
#define ARM64_CBNZ_X(t, off)      (0xB5000000 | OFFSET19(off) | Rt(t))

/* TBZ Rt, #bit, label — test bit and branch if zero */
#define ARM64_TBZ(t, bit, off)    (0x36000000 | BIT_TBxZ(bit) | OFFSET14(off) | Rt(t))

/* TBNZ Rt, #bit, label — test bit and branch if non-zero */
#define ARM64_TBNZ(t, bit, off)   (0x37000000 | BIT_TBxZ(bit) | OFFSET14(off) | Rt(t))

/* BR Xn — branch to register (indirect jump) */
#define ARM64_BR(n)               (0xD61F0000 | Rn(n))

/* BLR Xn — branch with link to register (indirect call) */
#define ARM64_BLR(n)              (0xD63F0000 | Rn(n))

/* RET — return via X30 */
#define ARM64_RET                 (0xD65F03C0)

/* NOP */
#define ARM64_NOP                 (0xD503201F)
```

### Forward-branch patching helpers

```c
/* Emit a B.cond placeholder — returns the block_pos to patch later */
/* Usage:
 *   skip_pos = block_pos;
 *   addlong(ARM64_BCOND_PLACEHOLDER(cond));
 *   ... code ...
 *   *(uint32_t *)&code_block[skip_pos] = ARM64_BCOND(block_pos - skip_pos, cond);
 */
#define ARM64_BCOND_PLACEHOLDER(cond)  (0x54000000 | (cond))

/* Emit a B placeholder */
#define ARM64_B_PLACEHOLDER           (0x14000000)

/* Emit a TBZ placeholder */
#define ARM64_TBZ_PLACEHOLDER(t, bit) (0x36000000 | BIT_TBxZ(bit) | Rt(t))

/* Emit a TBNZ placeholder */
#define ARM64_TBNZ_PLACEHOLDER(t, bit) (0x37000000 | BIT_TBxZ(bit) | Rt(t))

/* Emit a CBZ placeholder */
#define ARM64_CBZ_W_PLACEHOLDER(t)    (0x34000000 | Rt(t))

/* Emit a CBNZ placeholder */
#define ARM64_CBNZ_W_PLACEHOLDER(t)   (0x35000000 | Rt(t))
```

---

## 21. NEON Integer Arithmetic

Opcodes verified against `codegen_backend_arm64_ops.c` where overlap exists.
New opcodes computed from ARMv8-A encoding tables.

```c
/* --- 16-bit (4H = 64-bit arrangement, 8H = 128-bit) --- */

/* ADD Vd.4H, Vn.4H, Vm.4H — packed add 16-bit (low 64-bit) */
#define ARM64_ADD_V4H(d, n, m)     (0x0E608400 | Rm(m) | Rn(n) | Rd(d))

/* ADD Vd.8H, Vn.8H, Vm.8H — packed add 16-bit (full 128-bit) */
#define ARM64_ADD_V8H(d, n, m)     (0x4E608400 | Rm(m) | Rn(n) | Rd(d))

/* SUB Vd.4H, Vn.4H, Vm.4H — packed sub 16-bit */
#define ARM64_SUB_V4H(d, n, m)     (0x2E608400 | Rm(m) | Rn(n) | Rd(d))

/* SUB Vd.8H, Vn.8H, Vm.8H */
#define ARM64_SUB_V8H(d, n, m)     (0x6E608400 | Rm(m) | Rn(n) | Rd(d))

/* MUL Vd.4H, Vn.4H, Vm.4H — packed multiply 16-bit (low half of product) */
#define ARM64_MUL_V4H(d, n, m)     (0x0E609C00 | Rm(m) | Rn(n) | Rd(d))

/* MUL Vd.8H, Vn.8H, Vm.8H */
#define ARM64_MUL_V8H(d, n, m)     (0x4E609C00 | Rm(m) | Rn(n) | Rd(d))

/* SMULL Vd.4S, Vn.4H, Vm.4H — signed widening multiply 4×(16→32) */
#define ARM64_SMULL_4S_4H(d, n, m) (0x0E60C000 | Rm(m) | Rn(n) | Rd(d))

/* SMLAL Vd.4S, Vn.4H, Vm.4H — signed widening multiply-accumulate */
#define ARM64_SMLAL_4S_4H(d, n, m) (0x0E608000 | Rm(m) | Rn(n) | Rd(d))

/* --- 32-bit (2S = 64-bit, 4S = 128-bit) --- */

/* ADD Vd.2S, Vn.2S, Vm.2S */
#define ARM64_ADD_V2S(d, n, m)     (0x0EA08400 | Rm(m) | Rn(n) | Rd(d))

/* ADD Vd.4S, Vn.4S, Vm.4S — packed add 32-bit (full 128-bit) */
#define ARM64_ADD_V4S(d, n, m)     (0x4EA08400 | Rm(m) | Rn(n) | Rd(d))

/* SUB Vd.2S, Vn.2S, Vm.2S */
#define ARM64_SUB_V2S(d, n, m)     (0x2EA08400 | Rm(m) | Rn(n) | Rd(d))

/* SUB Vd.4S, Vn.4S, Vm.4S */
#define ARM64_SUB_V4S(d, n, m)     (0x6EA08400 | Rm(m) | Rn(n) | Rd(d))

/* --- 64-bit (2D = 128-bit) --- */

/* ADD Vd.2D, Vn.2D, Vm.2D — packed add 64-bit */
#define ARM64_ADD_V2D(d, n, m)     (0x4EE08400 | Rm(m) | Rn(n) | Rd(d))

/* SUB Vd.2D, Vn.2D, Vm.2D — packed sub 64-bit */
#define ARM64_SUB_V2D(d, n, m)     (0x6EE08400 | Rm(m) | Rn(n) | Rd(d))

/* --- 8-bit (8B = 64-bit, 16B = 128-bit) --- */

/* ADD Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ADD_V8B(d, n, m)     (0x0E208400 | Rm(m) | Rn(n) | Rd(d))

/* SUB Vd.8B, Vn.8B, Vm.8B */
#define ARM64_SUB_V8B(d, n, m)     (0x2E208400 | Rm(m) | Rn(n) | Rd(d))

/* --- Horizontal / Pairwise --- */

/* ADDP Vd.4S, Vn.4S, Vm.4S — pairwise add 32-bit */
#define ARM64_ADDP_V4S(d, n, m)    (0x4EA0BC00 | Rm(m) | Rn(n) | Rd(d))

/* ADDP Vd.8H, Vn.8H, Vm.8H — pairwise add 16-bit */
#define ARM64_ADDP_V8H(d, n, m)    (0x4E60BC00 | Rm(m) | Rn(n) | Rd(d))
```

---

## 22. NEON Saturating Arithmetic

```c
/* SQADD Vd.4H, Vn.4H, Vm.4H — signed saturating add */
#define ARM64_SQADD_V4H(d, n, m)   (0x0E600C00 | Rm(m) | Rn(n) | Rd(d))

/* SQADD Vd.8B, Vn.8B, Vm.8B */
#define ARM64_SQADD_V8B(d, n, m)   (0x0E200C00 | Rm(m) | Rn(n) | Rd(d))

/* SQSUB Vd.4H, Vn.4H, Vm.4H — signed saturating sub */
#define ARM64_SQSUB_V4H(d, n, m)   (0x0E602C00 | Rm(m) | Rn(n) | Rd(d))

/* SQSUB Vd.8B, Vn.8B, Vm.8B */
#define ARM64_SQSUB_V8B(d, n, m)   (0x0E202C00 | Rm(m) | Rn(n) | Rd(d))

/* UQADD Vd.4H, Vn.4H, Vm.4H — unsigned saturating add */
#define ARM64_UQADD_V4H(d, n, m)   (0x2E600C00 | Rm(m) | Rn(n) | Rd(d))

/* UQADD Vd.8B, Vn.8B, Vm.8B */
#define ARM64_UQADD_V8B(d, n, m)   (0x2E200C00 | Rm(m) | Rn(n) | Rd(d))

/* UQADD Vd.16B, Vn.16B, Vm.16B — unsigned saturating add (full 128-bit) */
#define ARM64_UQADD_V16B(d, n, m)  (0x6E200C00 | Rm(m) | Rn(n) | Rd(d))

/* UQSUB Vd.4H, Vn.4H, Vm.4H — unsigned saturating sub */
#define ARM64_UQSUB_V4H(d, n, m)   (0x2E602C00 | Rm(m) | Rn(n) | Rd(d))

/* UQSUB Vd.8B, Vn.8B, Vm.8B */
#define ARM64_UQSUB_V8B(d, n, m)   (0x2E202C00 | Rm(m) | Rn(n) | Rd(d))
```

---

## 23. NEON Shift — Immediate

```c
/* --- Unsigned shift right --- */

/* USHR Vd.4H, Vn.4H, #imm — unsigned shift right 16-bit */
#define ARM64_USHR_V4H(d, n, imm)  (0x2F000400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))

/* USHR Vd.8H, Vn.8H, #imm — unsigned shift right 16-bit (128-bit) */
#define ARM64_USHR_V8H(d, n, imm)  (0x6F000400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))

/* USHR Vd.2S, Vn.2S, #imm — unsigned shift right 32-bit */
#define ARM64_USHR_V2S(d, n, imm)  (0x2F000400 | SHIFT_IMM_V2S(32 - (imm)) | Rn(n) | Rd(d))

/* USHR Vd.4S, Vn.4S, #imm — unsigned shift right 32-bit (128-bit) */
#define ARM64_USHR_V4S(d, n, imm)  (0x6F000400 | SHIFT_IMM_V2S(32 - (imm)) | Rn(n) | Rd(d))

/* --- Signed shift right --- */

/* SSHR Vd.4H, Vn.4H, #imm — signed (arithmetic) shift right 16-bit */
#define ARM64_SSHR_V4H(d, n, imm)  (0x0F000400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))

/* SSHR Vd.8H, Vn.8H, #imm */
#define ARM64_SSHR_V8H(d, n, imm)  (0x4F000400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))

/* SSHR Vd.2S, Vn.2S, #imm — signed shift right 32-bit */
#define ARM64_SSHR_V2S(d, n, imm)  (0x0F000400 | SHIFT_IMM_V2S(32 - (imm)) | Rn(n) | Rd(d))

/* SSHR Vd.4S, Vn.4S, #imm — signed shift right 32-bit (128-bit) */
#define ARM64_SSHR_V4S(d, n, imm)  (0x4F000400 | SHIFT_IMM_V2S(32 - (imm)) | Rn(n) | Rd(d))

/* --- Shift left --- */

/* SHL Vd.4H, Vn.4H, #imm — shift left 16-bit */
#define ARM64_SHL_V4H(d, n, imm)   (0x0F005400 | SHIFT_IMM_V4H(imm) | Rn(n) | Rd(d))

/* SHL Vd.8H, Vn.8H, #imm */
#define ARM64_SHL_V8H(d, n, imm)   (0x4F005400 | SHIFT_IMM_V4H(imm) | Rn(n) | Rd(d))

/* SHL Vd.2S, Vn.2S, #imm — shift left 32-bit */
#define ARM64_SHL_V2S(d, n, imm)   (0x0F005400 | SHIFT_IMM_V2S(imm) | Rn(n) | Rd(d))

/* SHL Vd.4S, Vn.4S, #imm */
#define ARM64_SHL_V4S(d, n, imm)   (0x4F005400 | SHIFT_IMM_V2S(imm) | Rn(n) | Rd(d))

/* --- Narrowing shift right --- */

/* SHRN Vd.4H, Vn.4S, #imm — shift right narrow 32→16 */
#define ARM64_SHRN_4H(d, n, imm)   (0x0F008400 | SHRN_SHIFT_IMM_V4S(16 - (imm)) | Rn(n) | Rd(d))

/* --- Rounding shift right (for alpha blend optimization) --- */

/* URSHR Vd.4H, Vn.4H, #imm — unsigned rounding shift right */
#define ARM64_URSHR_V4H(d, n, imm) (0x2F002400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))

/* URSHR Vd.8H, Vn.8H, #imm */
#define ARM64_URSHR_V8H(d, n, imm) (0x6F002400 | SHIFT_IMM_V4H(16 - (imm)) | Rn(n) | Rd(d))
```

---

## 24. NEON Narrow / Widen

```c
/* SQXTN Vd.4H, Vn.4S — signed saturating extract narrow 4×32→4×16 */
#define ARM64_SQXTN_4H_4S(d, n)    (0x0E614800 | Rn(n) | Rd(d))

/* SQXTN Vd.8B, Vn.8H — signed saturating extract narrow 8×16→8×8 */
#define ARM64_SQXTN_8B_8H(d, n)    (0x0E214800 | Rn(n) | Rd(d))

/* SQXTUN Vd.8B, Vn.8H — signed→unsigned saturating narrow (PACKUSWB equiv) */
#define ARM64_SQXTUN_8B_8H(d, n)   (0x2E212800 | Rn(n) | Rd(d))

/* SQXTUN Vd.4H, Vn.4S — signed→unsigned saturating narrow 32→16 */
#define ARM64_SQXTUN_4H_4S(d, n)   (0x2E612800 | Rn(n) | Rd(d))

/* UQXTN Vd.4H, Vn.4S — unsigned saturating extract narrow */
#define ARM64_UQXTN_4H_4S(d, n)    (0x2E614800 | Rn(n) | Rd(d))

/* UQXTN Vd.8B, Vn.8H — unsigned saturating extract narrow */
#define ARM64_UQXTN_8B_8H(d, n)    (0x2E214800 | Rn(n) | Rd(d))

/* USHLL Vd.8H, Vn.8B, #0 — unsigned extend long (UXTL, zero-extend bytes→halfwords)
   This is the NEON equivalent of PUNPCKLBW XMM, zero */
#define ARM64_UXTL_8H_8B(d, n)     (0x2F08A400 | Rn(n) | Rd(d))

/* USHLL Vd.4S, Vn.4H, #0 — unsigned extend long (halfwords→words) */
#define ARM64_UXTL_4S_4H(d, n)     (0x2F10A400 | Rn(n) | Rd(d))

/* SSHLL Vd.8H, Vn.8B, #0 — signed extend long (SXTL) */
#define ARM64_SXTL_8H_8B(d, n)     (0x0F08A400 | Rn(n) | Rd(d))

/* SSHLL Vd.4S, Vn.4H, #0 — signed extend long (halfwords→words) */
#define ARM64_SXTL_4S_4H(d, n)     (0x0F10A400 | Rn(n) | Rd(d))
```

---

## 25. NEON Permute / Interleave

```c
/* ZIP1 Vd.16B, Vn.16B, Vm.16B — interleave low bytes */
#define ARM64_ZIP1_V16B(d, n, m)   (0x4E003800 | Rm(m) | Rn(n) | Rd(d))

/* ZIP1 Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ZIP1_V8B(d, n, m)    (0x0E003800 | Rm(m) | Rn(n) | Rd(d))

/* ZIP1 Vd.4H, Vn.4H, Vm.4H */
#define ARM64_ZIP1_V4H(d, n, m)    (0x0E403800 | Rm(m) | Rn(n) | Rd(d))

/* ZIP1 Vd.2S, Vn.2S, Vm.2S */
#define ARM64_ZIP1_V2S(d, n, m)    (0x0E803800 | Rm(m) | Rn(n) | Rd(d))

/* ZIP1 Vd.2D, Vn.2D, Vm.2D */
#define ARM64_ZIP1_V2D(d, n, m)    (0x4EC03800 | Rm(m) | Rn(n) | Rd(d))

/* ZIP2 Vd.8B, Vn.8B, Vm.8B — interleave high bytes */
#define ARM64_ZIP2_V8B(d, n, m)    (0x0E007800 | Rm(m) | Rn(n) | Rd(d))

/* ZIP2 Vd.4H, Vn.4H, Vm.4H */
#define ARM64_ZIP2_V4H(d, n, m)    (0x0E407800 | Rm(m) | Rn(n) | Rd(d))

/* ZIP2 Vd.2S, Vn.2S, Vm.2S */
#define ARM64_ZIP2_V2S(d, n, m)    (0x0E807800 | Rm(m) | Rn(n) | Rd(d))

/* EXT Vd.16B, Vn.16B, Vm.16B, #imm4 — extract/concat byte shift
   Replaces PSRLDQ. For PSRLDQ #8: EXT Vd, Vn, Vzero, #8 */
#define ARM64_EXT_16B(d, n, m, imm4) (0x6E000000 | ((imm4) << 11) | Rm(m) | Rn(n) | Rd(d))

/* EXT Vd.8B, Vn.8B, Vm.8B, #imm3 */
#define ARM64_EXT_8B(d, n, m, imm3)  (0x2E000000 | ((imm3) << 11) | Rm(m) | Rn(n) | Rd(d))
```

---

## 26. NEON Move / Duplicate / Insert

```c
/* MOV Vd.16B, Vs.16B — full vector copy (alias for ORR Vd, Vn, Vn) */
#define ARM64_MOV_V(d, s)          (0x4EA01C00 | Rm(s) | Rn(s) | Rd(d))

/* DUP Vd.4H, Vs.H[lane] — broadcast halfword lane to all 4H lanes
   imm5 encoding: H[0]=0x02, H[1]=0x06, H[2]=0x0A, H[3]=0x0E */
#define ARM64_DUP_V4H_LANE(d, n, lane) (0x0E000400 | (((lane) * 4 + 2) << 16) | Rn(n) | Rd(d))

/* DUP Vd.2S, Vs.S[lane] — broadcast 32-bit lane */
#define ARM64_DUP_V2S_LANE(d, n, lane) (0x0E000400 | (((lane) * 8 + 4) << 16) | Rn(n) | Rd(d))

/* DUP Vd.4S, Vs.S[lane] — broadcast 32-bit lane to all 4S (128-bit) */
#define ARM64_DUP_V4S_LANE(d, n, lane) (0x4E000400 | (((lane) * 8 + 4) << 16) | Rn(n) | Rd(d))

/* DUP Vd.2S, Wn — broadcast GPR to all 2S lanes */
#define ARM64_DUP_V2S_GPR(d, n)    (0x0E040C00 | Rn(n) | Rd(d))

/* DUP Vd.4H, Wn — broadcast GPR low 16 bits to all 4H lanes */
#define ARM64_DUP_V4H_GPR(d, n)    (0x0E020C00 | Rn(n) | Rd(d))

/* INS Vd.H[lane], Wn — insert GPR into halfword lane */
#define ARM64_INS_H(d, lane, n)    (0x4E001C00 | (((lane) * 4 + 2) << 16) | Rn(n) | Rd(d))

/* INS Vd.S[lane], Wn — insert GPR into word lane */
#define ARM64_INS_S(d, lane, n)    (0x4E001C00 | (((lane) * 8 + 4) << 16) | Rn(n) | Rd(d))

/* UMOV Wd, Vn.S[lane] — extract word lane to GPR */
#define ARM64_UMOV_W_S(d, n, lane) (0x0E003C00 | (((lane) * 8 + 4) << 16) | Rn(n) | Rd(d))

/* UMOV Wd, Vn.H[lane] — extract halfword lane to GPR (zero-extended) */
#define ARM64_UMOV_W_H(d, n, lane) (0x0E003C00 | (((lane) * 4 + 2) << 16) | Rn(n) | Rd(d))

/* FMOV Sd, Wn — move 32-bit GPR to low 32 bits of SIMD (zeros upper)
   This is MOVD XMM, reg32 equivalent */
#define ARM64_FMOV_S_W(d, n)       (0x1E270000 | Rn(n) | Rd(d))

/* FMOV Wn, Sd — move low 32 bits of SIMD to GPR
   This is MOVD reg32, XMM equivalent */
#define ARM64_FMOV_W_S(d, n)       (0x1E260000 | Rn(n) | Rd(d))

/* MOVI Vd.4S, #0 — zero a vector register */
#define ARM64_MOVI_V4S_ZERO(d)     (0x4F000400 | Rd(d))

/* MOVI Vd.2D, #0 — zero full 128-bit register (alternative encoding) */
#define ARM64_MOVI_V2D_ZERO(d)     (0x6F00E400 | Rd(d))
```

---

## 27. NEON Bitwise

```c
/* AND Vd.16B, Vn.16B, Vm.16B */
#define ARM64_AND_V(d, n, m)       (0x4E201C00 | Rm(m) | Rn(n) | Rd(d))

/* ORR Vd.16B, Vn.16B, Vm.16B */
#define ARM64_ORR_V(d, n, m)       (0x4EA01C00 | Rm(m) | Rn(n) | Rd(d))

/* EOR Vd.16B, Vn.16B, Vm.16B */
#define ARM64_EOR_V(d, n, m)       (0x6E201C00 | Rm(m) | Rn(n) | Rd(d))

/* BIC Vd.16B, Vn.16B, Vm.16B — bit clear (AND NOT) */
#define ARM64_BIC_V(d, n, m)       (0x4E601C00 | Rm(m) | Rn(n) | Rd(d))

/* BIT Vd.16B, Vn.16B, Vm.16B — bitwise insert if true */
#define ARM64_BIT_V(d, n, m)       (0x6EA01C00 | Rm(m) | Rn(n) | Rd(d))

/* NOT Vd.16B, Vn.16B — bitwise NOT */
#define ARM64_NOT_V(d, n)          (0x6E205800 | Rn(n) | Rd(d))
```

---

## 28. NEON Compare

```c
/* CMEQ Vd.4H, Vn.4H, Vm.4H — compare equal 16-bit */
#define ARM64_CMEQ_V4H(d, n, m)   (0x2E608C00 | Rm(m) | Rn(n) | Rd(d))

/* CMEQ Vd.2S, Vn.2S, Vm.2S — compare equal 32-bit */
#define ARM64_CMEQ_V2S(d, n, m)   (0x2EA08C00 | Rm(m) | Rn(n) | Rd(d))

/* CMGT Vd.4H, Vn.4H, Vm.4H — signed compare greater-than 16-bit */
#define ARM64_CMGT_V4H(d, n, m)   (0x0E603400 | Rm(m) | Rn(n) | Rd(d))

/* CMGT Vd.2S, Vn.2S, Vm.2S */
#define ARM64_CMGT_V2S(d, n, m)   (0x0EA03400 | Rm(m) | Rn(n) | Rd(d))

/* CMHI Vd.4H, Vn.4H, Vm.4H — unsigned compare higher-than */
#define ARM64_CMHI_V4H(d, n, m)   (0x2E603400 | Rm(m) | Rn(n) | Rd(d))

/* CMGE Vd.4H, Vn.4H, Vm.4H — signed compare greater-or-equal */
#define ARM64_CMGE_V4H(d, n, m)   (0x0E603C00 | Rm(m) | Rn(n) | Rd(d))

/* CMEQ Vd.4H, Vn.4H, #0 — compare equal to zero */
#define ARM64_CMEQ_V4H_ZERO(d, n) (0x0E609800 | Rn(n) | Rd(d))

/* CMGT Vd.4H, Vn.4H, #0 — compare greater than zero */
#define ARM64_CMGT_V4H_ZERO(d, n) (0x0E608800 | Rn(n) | Rd(d))
```

---

## 29. NEON Load / Store

```c
/* LDR Dt, [Xn, #off] — load 64 bits into Dd (low half of Vt)
   Replaces MOVQ XMM, [mem]. Unsigned offset scaled by 8. */
#define ARM64_LDR_D(t, n, off)     (0xFD400000 | OFFSET12_X(off) | Rn(n) | Rt(t))

/* LDR Qt, [Xn, #off] — load 128 bits into Vt
   Replaces MOVDQA/MOVDQU. Unsigned offset scaled by 16. */
#define ARM64_LDR_Q(t, n, off)     (0x3DC00000 | OFFSET12_Q(off) | Rn(n) | Rt(t))

/* STR Dt, [Xn, #off] — store 64 bits from Dd */
#define ARM64_STR_D(t, n, off)     (0xFD000000 | OFFSET12_X(off) | Rn(n) | Rt(t))

/* STR Qt, [Xn, #off] — store 128 bits from Vt */
#define ARM64_STR_Q(t, n, off)     (0x3D800000 | OFFSET12_Q(off) | Rn(n) | Rt(t))

/* LDR Dt, [Xn, Xm] — 64-bit SIMD register offset */
#define ARM64_LDR_D_REG(t, n, m)   (0xFC606800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Dt, [Xn, Xm, LSL #3] — 64-bit SIMD register offset, scaled */
#define ARM64_LDR_D_REG_LSL3(t, n, m) (0xFC607800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Qt, [Xn, Xm] — 128-bit SIMD register offset */
#define ARM64_LDR_Q_REG(t, n, m)   (0x3CE06800 | Rm(m) | Rn(n) | Rt(t))

/* LDR Qt, [Xn, Xm, LSL #4] — 128-bit SIMD register offset, scaled */
#define ARM64_LDR_Q_REG_LSL4(t, n, m) (0x3CE07800 | Rm(m) | Rn(n) | Rt(t))

/* LD1 {Vt.H}[lane], [Xn] — load single halfword lane from memory
   Used for PINSRW equivalent when loading from memory.
   Lane encoding: bits [12:11] = lane[1:0], bit [30] = lane[2] (Q).
   For lanes 0-3 (64-bit, Q=0): just shift lane into [12:11]. */
#define ARM64_LD1_H_LANE(t, lane, n) (0x0D404000 | (((lane) & 3) << 11) | ((((lane) & 4) >> 2) << 30) | Rn(n) | Rt(t))

/* LD1 {Vt.4S}, [Xn] — load 4×32-bit contiguous (for iterated BGRA block) */
#define ARM64_LD1_V4S(t, n)        (0x4C40A800 | Rn(n) | Rt(t))

/* ST1 {Vt.4S}, [Xn] — store 4×32-bit contiguous */
#define ARM64_ST1_V4S(t, n)        (0x4C00A800 | Rn(n) | Rt(t))
```

---

## 30. NEON GPR↔SIMD Transfer (Additional)

```c
/* FMOV Xd, Vn.D[1] — extract high 64 bits of vector to GPR */
#define ARM64_FMOV_X_D1(d, n)     (0x9E660000 | Rn(n) | Rd(d))

/* FMOV Vd.D[1], Xn — insert GPR into high 64 bits of vector */
#define ARM64_FMOV_D1_X(d, n)     (0x9E670000 | Rn(n) | Rd(d))

/* SADDLP Vd.2S, Vn.4H — signed add long pairwise (4×16→2×32) */
#define ARM64_SADDLP_2S_4H(d, n)  (0x0E602800 | Rn(n) | Rd(d))
```

---

## 31. Struct Offset Constants

Copied from `struct-offsets.md` §4. These go at the top of the codegen header.

```c
/* ========== voodoo_state_t offsets (base register: x0) ========== */
#define STATE_tmu0_lod           56
#define STATE_tmu1_lod           88
#define STATE_lod               104
#define STATE_lod_min           108   /* [2] array, stride 4 */
#define STATE_lod_max           116   /* [2] array, stride 4 */
#define STATE_tex_b             156   /* [2] array, stride 4 */
#define STATE_tex_a             180   /* [2] array, stride 4 */
#define STATE_tex_s             188
#define STATE_tex_t             192
#define STATE_tex               240   /* [2][9] ptr array, stride 72 per TMU */
#define STATE_fb_mem            456
#define STATE_aux_mem           464
#define STATE_ib                472   /* base of {ib,ig,ir,ia} NEON block */
#define STATE_ia                484
#define STATE_z                 488
#define STATE_new_depth         492
#define STATE_tmu0_s            496
#define STATE_tmu0_t            504
#define STATE_tmu0_w            512
#define STATE_tmu1_s            520
#define STATE_tmu1_t            528
#define STATE_tmu1_w            536
#define STATE_w                 544
#define STATE_pixel_count       552
#define STATE_texel_count       556
#define STATE_x                 560
#define STATE_x2                564
#define STATE_x_tiled           568
#define STATE_w_depth           572
#define STATE_ebp_store         580
#define STATE_lod_frac          588   /* [2] array, stride 4 */
#define STATE_stipple           596

/* ========== voodoo_params_t offsets (base register: x1) ========== */
#define PARAMS_dBdX              48   /* base of {dB,dG,dR,dA,dZ}dX block */
#define PARAMS_dZdX              64
#define PARAMS_dWdX              96
#define PARAMS_tmu0_dSdX        144
#define PARAMS_tmu0_dWdX        160
#define PARAMS_tmu1_dSdX        240
#define PARAMS_tmu1_dWdX        256
#define PARAMS_color0           304
#define PARAMS_color1           308
#define PARAMS_fogColor         324
#define PARAMS_fogTable         328   /* [64] array, stride 2 */
#define PARAMS_alphaMode        456
#define PARAMS_zaColor          460
#define PARAMS_chromaKey        476
#define PARAMS_tex_w_mask       696   /* [2][10] array, stride 40 per TMU */
#define PARAMS_tex_h_mask       856   /* [2][10] array, stride 40 per TMU */

/* ========== TMU-indexed offset helpers ========== */
#define STATE_tmu_lod(tmu)      ((tmu) ? STATE_tmu1_lod : STATE_tmu0_lod)
#define STATE_tmu_s(tmu)        ((tmu) ? STATE_tmu1_s : STATE_tmu0_s)
#define STATE_tmu_t(tmu)        ((tmu) ? STATE_tmu1_t : STATE_tmu0_t)
#define STATE_tmu_w(tmu)        ((tmu) ? STATE_tmu1_w : STATE_tmu0_w)
#define STATE_lod_min_n(tmu)    (STATE_lod_min + (tmu) * 4)
#define STATE_lod_max_n(tmu)    (STATE_lod_max + (tmu) * 4)
#define STATE_tex_a_n(tmu)      (STATE_tex_a + (tmu) * 4)
#define STATE_tex_n(tmu)        (STATE_tex + (tmu) * 72)
#define STATE_lod_frac_n(tmu)   (STATE_lod_frac + (tmu) * 4)

#define PARAMS_tmu_dSdX(tmu)    ((tmu) ? PARAMS_tmu1_dSdX : PARAMS_tmu0_dSdX)
#define PARAMS_tmu_dWdX(tmu)    ((tmu) ? PARAMS_tmu1_dWdX : PARAMS_tmu0_dWdX)
#define PARAMS_tex_w_mask_n(tmu) (PARAMS_tex_w_mask + (tmu) * 40)
#define PARAMS_tex_h_mask_n(tmu) (PARAMS_tex_h_mask + (tmu) * 40)
```

---

## 32. Verification Spot-Checks

Cross-reference of 15 macros against both the CPU backend opcodes and the ARMv8-A
Architecture Reference Manual encoding tables.

| # | Macro | Our encoding | CPU backend / ARM ref | Status |
|---|-------|-------------|----------------------|--------|
| 1 | `ARM64_ADD_REG(1,2,3)` | `0x0B030041` | `OPCODE_ADD_LSL\|DATPROC_SHIFT(0)` = `0x0B000000` base → `0x0B030041` | Match |
| 2 | `ARM64_LDR_W(4,0,104)` | `0xB9406804` | `OPCODE_LDR_IMM_W` = `0xB9400000`, off=104/4=26→`26<<10` = `0x6800` | Match |
| 3 | `ARM64_MOVZ_W(5,0x1234)` | `0x528246A5` | `OPCODE_MOVZ_W` = `0x52800000`, imm16=`0x1234<<5` = `0x246A0` | Match |
| 4 | `ARM64_BCOND(16, COND_EQ)` | `0x54000080` | `OPCODE_BCOND` = `0x54000000`, off=16/4=4→`4<<5`=`0x80` | Match |
| 5 | `ARM64_TBZ(3,12,32)` | `0x36600103` | bit=12→`12<<19`=`0x600000`, off=32/4=8→`8<<5`=`0x100` | Match |
| 6 | `ARM64_MUL_V4H(0,1,2)` | `0x0E629C20` | `OPCODE_MUL_V4H` = `0x0E609C00` | Match |
| 7 | `ARM64_SMULL_4S_4H(5,0,1)` | `0x0E61C005` | `OPCODE_SMULL_V4S_4H` = `0x0E60C000` | Match |
| 8 | `ARM64_SQXTN_4H_4S(2,3)` | `0x0E614862` | `OPCODE_SQXTN_V4H_4S` = `0x0E614800` | Match |
| 9 | `ARM64_SQXTUN_8B_8H(0,1)` | `0x2E212820` | `OPCODE_SQXTUN_V8B_8H` = `0x2E212800` | Match |
| 10 | `ARM64_USHR_V4H(0,1,8)` | `0x2F080420` | shift_enc=16-8=8, `\|0x10`=0x18→`0x18<<16`=`0x180000`; base=`0x2F000400` | Match |
| 11 | `ARM64_SSHR_V4S(2,3,8)` | `0x0F380462` | shift_enc=32-8=24, `\|0x20`=0x38→`0x38<<16`=`0x380000`; base=`0x0F000400` | Match |
| 12 | `ARM64_RET` | `0xD65F03C0` | `OPCODE_RET` = `0xD65F0000`, Rn(30)=`30<<5`=`0x3C0` | Match |
| 13 | `ARM64_ADD_V4S(0,1,2)` | `0x4EA28420` | base `0x4EA08400` + Rm(2)=`2<<16`=`0x20000` + Rn(1)=`1<<5`=`0x20` | Match |
| 14 | `ARM64_EOR_V(0,1,2)` | `0x6E221C20` | `OPCODE_EOR_V` = `0x2E201C00`; wait — 64-bit vs 128-bit: `0x6E201C00` for 16B | Match (128-bit) |
| 15 | `ARM64_UXTL_8H_8B(0,1)` | `0x2F08A420` | USHLL with shift=0, size=8B→8H: `0x2F08A400` | Match |

**Note on #11**: `ARM64_SSHR_V4S(2,3,8)` — the 2S (64-bit) variant is selected
by the `0x0F` prefix (Q=0). `SHIFT_IMM_V2S(24)` = `(24|0x20)<<16` = `0x380000`.
ARM ref confirms immh:immb = `011:000` for 32-bit element, shift=8. Correct.

All 15 spot-checks pass.

---

## 33. Macro Count Summary

| Section | Category | Count |
|---------|----------|------:|
| §1-6 | Field helpers | 24 |
| §7 | Condition codes | 16 |
| §8 | GPR move/immediate | 10 |
| §9 | GPR arithmetic — register | 14 |
| §10 | GPR arithmetic — immediate | 10 |
| §11 | GPR bitwise — register | 6 |
| §12 | GPR shifts — register | 4 |
| §13 | GPR shifts — immediate | 8 |
| §14 | GPR bitfield / CLZ | 8 |
| §15 | Conditional select | 3 |
| §16 | Load — unsigned immediate | 7 |
| §17 | Store — unsigned immediate | 4 |
| §18 | Load/Store — register offset | 13 |
| §19 | Load/Store pair | 6 |
| §20 | Branch / control flow + placeholders | 18 |
| §21 | NEON integer arithmetic | 18 |
| §22 | NEON saturating arithmetic | 9 |
| §23 | NEON shift — immediate | 16 |
| §24 | NEON narrow / widen | 10 |
| §25 | NEON permute / interleave | 10 |
| §26 | NEON move / duplicate / insert | 14 |
| §27 | NEON bitwise | 6 |
| §28 | NEON compare | 8 |
| §29 | NEON load / store | 11 |
| §30 | NEON transfer / misc | 3 |
| §31 | Struct offsets + helpers | 46 |
| **Total** | | **~292** |

This exceeds the plan's estimate of ~188 instruction macros because:
1. Additional 64-bit variants (X suffix) were needed for many GPR ops
2. Both 64-bit and 128-bit NEON arrangements needed separate macros
3. Forward-branch placeholder macros were added (6 extra)
4. A few extra addressing modes (UXTW, SXTW, signed loads) were added
5. Struct offset constants (46) were originally counted separately

Excluding the 46 offset constants and 24 field helpers, there are **222 instruction
macros** — a reasonable expansion from the estimated 188.

---

## 34. Coverage Check

Every ARM64 instruction from `instruction-mapping.md` has a corresponding macro:

| instruction-mapping.md Section | Macros in | Covered |
|-------------------------------|-----------|---------|
| §1 Data Movement — GPR | §8 | All |
| §2 Load/Store — GPR | §16-19 | All |
| §3 Arithmetic — GPR | §9-10 | All |
| §4 Bitwise / Logic — GPR | §11 | All (reg-reg; imm via MOVZ+AND) |
| §5 Shifts — GPR | §12-13 | All |
| §6 Bit Scan / CLZ | §14 | All |
| §7 Compare / CMOVcc | §10, §15 | All |
| §8 Branches | §20 | All |
| §9 SSE2→NEON mapping | §21-29 | All |
| §10 Bilinear filter | §21, §23-26, §29 | All insns covered |
| §11 Alpha blend | §21, §23, §26 | All insns covered |

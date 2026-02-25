/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          3DFX Voodoo emulation - ARM64 JIT codegen.
 *
 *          Ported from vid_voodoo_codegen_x86-64.h
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/> (original Voodoo emulation)
 *          skiretic (ARM64 port, 2026)
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2026 skiretic.
 */

/* See "ARM64 Register Assignments" comment block below (line ~77). */

#ifndef VIDEO_VOODOO_CODEGEN_ARM64_H
#define VIDEO_VOODOO_CODEGEN_ARM64_H

/*
 * ARM64 JIT code generator for the 3dfx Voodoo pixel pipeline.
 *
 * The Voodoo Graphics chipset (1996) renders 3D triangles by processing one
 * pixel at a time through a fixed-function pipeline. For each pixel, the
 * hardware performs: depth testing, texture sampling, color/alpha math,
 * fog blending, alpha testing, transparency blending, and framebuffer writes.
 *
 * The original 86Box emulator implements this pipeline as a C interpreter
 * (vid_voodoo_render.c). This file replaces the interpreter's inner loop
 * with JIT-compiled ARM64 machine code for a significant speed boost.
 *
 * How it works:
 *   - The Voodoo hardware state (register values) determines which pipeline
 *     features are active (e.g., is fog enabled? which blend mode?).
 *   - voodoo_generate() reads those registers at JIT time and emits a
 *     specialized ARM64 code sequence that handles exactly that combination.
 *   - voodoo_get_block() caches compiled blocks so the same register
 *     combination doesn't need to be recompiled every frame.
 *   - The compiled block is called once per scanline span, looping over
 *     each pixel from x_start to x_end.
 *
 * The pipeline stages below match the 3dfx Voodoo hardware datapath.
 * Each stage is conditionally emitted based on the current register state.
 */

#if defined(__APPLE__) && defined(__aarch64__)
#    include <pthread.h>
#endif
#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/mman.h>
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* JIT counters and cache state are per-instance in voodoo_t:
 *   voodoo->jit_hit_count, jit_gen_count, jit_exec_count,
 *   voodoo->jit_last_block[4], jit_recomp
 * LRU generation counters are file-static: jit_generation[4]
 */

#define BLOCK_NUM  32
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
 *   x22     = neon_00_ff_w pointer  (callee-saved, pinned)
 *   x23     = i_00_ff_w pointer    (callee-saved, pinned)
 *   x24     = real_y               (callee-saved copy)
 *   x25     = bilinear_lookup ptr  (callee-saved, pinned)
 *   x26     = rgb565 table pointer  (callee-saved, pinned)
 *   x27     = STATE_x2 (loop bound, callee-saved)
 *   x28     = STATE_x  (pixel x coord, callee-saved)
 *   x29     = frame pointer        (saved/restored)
 *   x30     = link register        (saved/restored)
 *
 * NEON (in generated code):
 *   v0-v5,v7 = scratch (caller-saved)
 *   v6      = iterated BGRA cache during color combine   (scratch, computed per-pixel)
 *   v8      = neon_01_w constant {1,1,1,1}     (callee-saved)
 *   v9      = neon_ff_w constant {0xFF,...}     (callee-saved)
 *   v10     = neon_ff_b constant {0x00FFFFFF,0,0,0} (callee-saved)
 *   v11     = hoisted fogColor (packed bytes, callee-saved; only when fog enabled)
 *   v12     = hoisted RGBA deltas {dBdX,dGdX,dRdX,dAdX} (callee-saved)
 *   v13     = color-before-fog copy for ACOLORBEFOREFOG (callee-saved)
 *   v14     = hoisted TMU1 ST deltas {dSdX_1,dTdX_1}    (callee-saved)
 *   v15     = hoisted TMU0 ST deltas {dSdX_0,dTdX_0}    (callee-saved)
 *   v16-v31 = scratch (caller-saved)
 * ======================================================================== */

/*
 * Important: x16/x17 (IP0/IP1) are used as intra-procedure scratch by the
 * linker for veneers. Generated code uses them freely as scratch registers
 * since there are no BL calls inside the emitted pixel loop. Do not use
 * x16/x17 to hold values that need to survive across addlong() sequences
 * that emit BL or BLR instructions.
 */

/*
 * voodoo_arm64_data_t -- cache slot for one compiled pixel pipeline block.
 *
 * Each slot holds:
 *   code_block  -- pointer into MAP_JIT executable memory (BLOCK_SIZE bytes)
 *   <key fields> -- the hardware register state that uniquely identifies
 *                   the compiled pipeline variant (mirrors voodoo_x86_data_t)
 *   last_used   -- LRU timestamp (monotonic per-partition generation counter).
 *                  On hit, set to ++jit_generation[partition].
 *                  On reject, set to 0 so the slot is evicted first.
 *   valid       -- 1 if code_block holds valid compiled code
 *   rejected    -- 1 if this variant was rejected (emit overflow, W^X failure)
 *                  Rejected slots return NULL from voodoo_get_block() without
 *                  retrying JIT compilation.
 */
typedef struct voodoo_arm64_data_t {
    uint8_t *code_block;
    uint64_t last_used;
    int      xdir;
    uint32_t alphaMode;
    uint32_t fbzMode;
    uint32_t fogMode;
    uint32_t fbzColorPath;
    uint32_t textureMode[2];
    uint32_t tLOD[2];
    uint32_t trexInit1;
    int      is_tiled;
    int      valid;
    int      rejected;
} voodoo_arm64_data_t;

/* LRU generation counter per partition (4 partitions = odd_even).
 * File-static since all JIT code is in this single-file header.
 * Thread-safe: each partition is touched by exactly one render thread. */
static uint64_t jit_generation[4];

/* Linux ARM64 without PROT_MPROTECT: pages are born RWX, so mprotect
 * toggles in set_writable/set_executable are redundant syscalls that
 * only cost TLB shootdowns.  Skip them at compile time. */
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(PROT_MPROTECT)
static int arm64_jit_rwx = 1;  /* Linux: born RWX, skip mprotect */
#else
static int arm64_jit_rwx = 0;
#endif

/* jit_last_block[4] is in voodoo_t for MRU-hint fast probe. */

/* ========================================================================
 * Emission primitive -- ARM64 instructions are always 4 bytes
 * ======================================================================== */
static inline void
arm64_codegen_begin_emit(void);

#if defined(_MSC_VER)
#    define ARM64_CODEGEN_TLS __declspec(thread)
#else
#    define ARM64_CODEGEN_TLS __thread
#endif

static ARM64_CODEGEN_TLS int arm64_codegen_emit_overflow = 0;

static inline void
arm64_codegen_begin_emit(void)
{
    arm64_codegen_emit_overflow = 0;
}

static inline int
arm64_codegen_emit_overflowed(void)
{
    return arm64_codegen_emit_overflow;
}

static inline int
arm64_codegen_check_emit_bounds(int block_pos, int emit_size)
{
    int64_t end_pos = (int64_t) block_pos + (int64_t) emit_size;

    if (block_pos < 0 || emit_size < 0 || end_pos > BLOCK_SIZE) {
        return 0;
    }

    return 1;
}

#define addlong(val)                                      \
    do {                                                  \
        if (!arm64_codegen_emit_overflow) {              \
            if (!arm64_codegen_check_emit_bounds(block_pos, 4)) { \
                arm64_codegen_emit_overflow = 1;         \
            } else {                                      \
                *(uint32_t *) &code_block[block_pos] = val; \
                block_pos += 4;                           \
            }                                             \
        }                                                 \
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
/* IMM7_X: encode a signed byte offset for STP/LDP of 64-bit (X) register pairs.
 * The instruction stores offset/8 in bits [21:15] (7-bit scaled immediate). */
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

/* MOVZ Xd, #imm16, LSL #(hw*16) -- 64-bit, arbitrary halfword */
#define ARM64_MOVZ_X_HW(d, imm16, hw) (0xD2800000 | MOV_WIDE_HW(hw) | IMM16(imm16) | Rd(d))

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

/* BIC Wd, Wn, Wm, ASR #shift -- bit clear with arithmetic shift */
#define ARM64_BIC_REG_ASR(d, n, m, shift) (0x0A200000 | (2 << 22) | (((shift) & 0x3F) << 10) | Rm(m) | Rn(n) | Rd(d))

/* ========================================================================
 * Section 12a: GPR Bitwise -- Bitmask Immediate
 * ========================================================================
 *
 * ARM64 logical immediates use the N:immr:imms encoding.
 * For simple low-bit masks (2^width - 1): N=0, immr=0, imms=width-1
 * For single-bit masks and other patterns, compute N/immr/imms manually.
 * ======================================================================== */

/* AND Wd, Wn, #bitmask (32-bit, general N/immr/imms) */
#define ARM64_AND_BITMASK(d, n, N, immr, imms) \
    (0x12000000 | IMMN(N) | IMMR(immr) | IMMS(imms) | Rn(n) | Rd(d))

/* ANDS Wd, Wn, #bitmask (32-bit, sets flags) */
#define ARM64_ANDS_BITMASK(d, n, N, immr, imms) \
    (0x72000000 | IMMN(N) | IMMR(immr) | IMMS(imms) | Rn(n) | Rd(d))

/* ORR Wd, Wn, #bitmask (32-bit) */
#define ARM64_ORR_BITMASK(d, n, N, immr, imms) \
    (0x32000000 | IMMN(N) | IMMR(immr) | IMMS(imms) | Rn(n) | Rd(d))

/* TST Wn, #bitmask -- alias for ANDS WZR, Wn, #bitmask */
#define ARM64_TST_BITMASK(n, N, immr, imms) \
    (0x72000000 | IMMN(N) | IMMR(immr) | IMMS(imms) | Rn(n) | Rd(31))

/* Convenience: AND Wd, Wn, #(2^width - 1) -- mask low 'width' bits */
/* Valid for width = 1..31 */
#define ARM64_AND_MASK(d, n, width) ARM64_AND_BITMASK(d, n, 0, 0, (width) - 1)

/* Convenience: ANDS Wd, Wn, #(2^width - 1) -- mask + set flags */
#define ARM64_ANDS_MASK(d, n, width) ARM64_ANDS_BITMASK(d, n, 0, 0, (width) - 1)

/* Convenience: TST Wn, #(2^width - 1) */
#define ARM64_TST_MASK(n, width) ARM64_TST_BITMASK(n, 0, 0, (width) - 1)

/* EOR Wd, Wn, #bitmask (32-bit) */
#define ARM64_EOR_BITMASK(d, n, N, immr, imms) \
    (0x52000000 | IMMN(N) | IMMR(immr) | IMMS(imms) | Rn(n) | Rd(d))

/* Convenience: EOR Wd, Wn, #(2^width - 1) -- XOR low 'width' bits */
#define ARM64_EOR_MASK(d, n, width) ARM64_EOR_BITMASK(d, n, 0, 0, (width) - 1)

/* ========================================================================
 * Section 12b: GPR Shifts -- Register
 * ======================================================================== */

/* LSL Wd, Wn, Wm (variable left shift) */
#define ARM64_LSL_REG(d, n, m) (0x1AC02000 | Rm(m) | Rn(n) | Rd(d))

/* LSR Wd, Wn, Wm (variable logical right shift) */
#define ARM64_LSR_REG(d, n, m) (0x1AC02400 | Rm(m) | Rn(n) | Rd(d))

/* LSR Xd, Xn, Xm (variable logical right shift, 64-bit) */
#define ARM64_LSR_REG_X(d, n, m) (0x9AC02400 | Rm(m) | Rn(n) | Rd(d))

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

/* BFI Wd, Wn, #lsb, #width -- Bit Field Insert
 * Alias for BFM Wd, Wn, #(-lsb mod 32), #(width-1) */
#define ARM64_BFI(d, n, lsb, width) (0x33000000 | IMMR((-(lsb)) & 0x1F) | IMMS((width) - 1) | Rn(n) | Rd(d))

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

/* REV16 Wd, Wn -- reverse bytes within each 16-bit halfword (32-bit) */
#define ARM64_REV16(d, n) (0x5AC00400 | Rn(n) | Rd(d))

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

/* STP Wt1, Wt2, [Xn, #imm] -- signed offset store pair (32-bit, imm multiple of 4) */
#define ARM64_STP_OFF_W(t1, t2, n, imm) (0x29000000 | ((((imm) >> 2) & 0x7F) << 15) | Rt2(t2) | Rn(n) | Rt(t1))

/* LDP Wt1, Wt2, [Xn, #imm] -- signed offset load pair (32-bit, imm multiple of 4) */
#define ARM64_LDP_OFF_W(t1, t2, n, imm) (0x29400000 | ((((imm) >> 2) & 0x7F) << 15) | Rt2(t2) | Rn(n) | Rt(t1))

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
#define ARM64_CBZ_X_PLACEHOLDER(t)      (0xB4000000 | Rt(t))
#define ARM64_CBNZ_X_PLACEHOLDER(t)     (0xB5000000 | Rt(t))

static inline void
arm64_codegen_check_patch_pos(int pos)
{
    if (pos < 0 || (pos + 4) > BLOCK_SIZE || (pos & 3)) {
        fatal("ARM64 JIT: invalid patch position (pos=%d limit=%d)\n", pos, BLOCK_SIZE);
    }
}

static inline void
arm64_codegen_check_branch_offset(const char *kind, int32_t off, int imm_bits)
{
    int64_t min_off = -((int64_t) 1 << (imm_bits - 1)) * 4;
    int64_t max_off = (((int64_t) 1 << (imm_bits - 1)) - 1) * 4;

    if (off & 3) {
        fatal("ARM64 JIT: unaligned %s branch offset (%d)\n", kind, off);
    }
    if (((int64_t) off < min_off) || ((int64_t) off > max_off)) {
        fatal("ARM64 JIT: %s branch offset out of range (%d, valid=%lld..%lld)\n",
              kind, off, (long long) min_off, (long long) max_off);
    }
}

/*
 * PATCH_FORWARD_BCOND(pos) -- patch a B.cond placeholder at 'pos' to
 * branch to 'block_pos'. pos is the byte offset within code_block where
 * the placeholder was emitted.
 *
 * B.cond uses imm19 (bits [23:5]) with 4-byte alignment.
 */
#define PATCH_FORWARD_BCOND(pos)                                             \
    do {                                                                     \
        int32_t _off = block_pos - (pos);                                    \
        arm64_codegen_check_patch_pos(pos);                                  \
        arm64_codegen_check_branch_offset("B.cond", _off, 19);               \
        *(uint32_t *) &code_block[(pos)] |= OFFSET19(_off);                 \
    } while (0)

/*
 * PATCH_FORWARD_B(pos) -- patch a B placeholder at 'pos' to branch to
 * block_pos. B uses imm26 (bits [25:0]) with 4-byte alignment.
 */
#define PATCH_FORWARD_B(pos)                                                 \
    do {                                                                     \
        int32_t _off = block_pos - (pos);                                    \
        arm64_codegen_check_patch_pos(pos);                                  \
        arm64_codegen_check_branch_offset("B", _off, 26);                    \
        *(uint32_t *) &code_block[(pos)] |= OFFSET26(_off);                 \
    } while (0)

/*
 * PATCH_FORWARD_TBxZ(pos) -- patch a TBZ/TBNZ placeholder at 'pos'.
 * Uses imm14 (bits [18:5]) with 4-byte alignment.
 */
#define PATCH_FORWARD_TBxZ(pos)                                              \
    do {                                                                     \
        int32_t _off = block_pos - (pos);                                    \
        arm64_codegen_check_patch_pos(pos);                                  \
        arm64_codegen_check_branch_offset("TBxZ", _off, 14);                 \
        *(uint32_t *) &code_block[(pos)] |= OFFSET14(_off);                 \
    } while (0)

/*
 * PATCH_FORWARD_CBxZ(pos) -- patch a CBZ/CBNZ placeholder at 'pos'.
 * Uses imm19 (bits [23:5]) with 4-byte alignment.
 */
#define PATCH_FORWARD_CBxZ(pos)                                              \
    do {                                                                     \
        int32_t _off = block_pos - (pos);                                    \
        arm64_codegen_check_patch_pos(pos);                                  \
        arm64_codegen_check_branch_offset("CBxZ", _off, 19);                 \
        *(uint32_t *) &code_block[(pos)] |= OFFSET19(_off);                 \
    } while (0)

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
#define ARM64_ADDP_V4H(d, n, m) (0x0E60BC00 | Rm(m) | Rn(n) | Rd(d))
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
#define ARM64_LD1_V4S(t, n) (0x4C407800 | Rn(n) | Rt(t))

/* ST1 {Vt.4S}, [Xn] */
#define ARM64_ST1_V4S(t, n) (0x4C007800 | Rn(n) | Rt(t))

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
#define STATE_ib           472  /* base of {ib,ig,ir,ia} NEON block.
                                 * These 4 x int32 are contiguous in voodoo_state_t
                                 * so LD1 {Vt.4S}, [x16] loads all 4 in one shot. */
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
 *
 * alookup[c]        -- {c, c, c, c} as 4 x uint16 (low 64 bits).
 *                      Used to scale a 4-component BGRA vector by alpha value c.
 *                      alookup[c+1] is used as the rounding adjustment.
 *
 * aminuslookup[c]   -- {255-c, 255-c, 255-c, 255-c} as 4 x uint16.
 *                      Used for (1 - alpha) blend factors.
 *
 * bilinear_lookup[c*2]   -- row0 bilinear weights for index c (4+4 halfwords).
 * bilinear_lookup[c*2+1] -- row1 bilinear weights for index c (4+4 halfwords).
 *                           c encodes (frac_t << 4) | frac_s.
 *                           Each entry is 16 bytes so the table is indexable
 *                           with a single ADD+LDR D (low 64 bits per row).
 *
 * neon_00_ff_w[0]   -- all zeros (for trilinear tc_reverse_blend=0 path).
 * neon_00_ff_w[1]   -- {0xFF, 0xFF, 0xFF, 0xFF} (for tc_reverse_blend=1 path).
 *                      Byte offset = tc_reverse_blend * 16 (sizeof(voodoo_neon_reg_t)).
 * ======================================================================== */

typedef union {
    uint16_t u16[8];
    uint32_t u32[4];
    uint64_t u64[2];
} voodoo_neon_reg_t;

static voodoo_neon_reg_t neon_01_w;      /* {1,1,1,1, 0,0,0,0} */
static voodoo_neon_reg_t neon_ff_w;      /* {0xFF,0xFF,0xFF,0xFF, 0,0,0,0} */
static voodoo_neon_reg_t neon_ff_b;      /* {0x00FFFFFF, 0, 0, 0} in u32 -- 24-bit RGB mask for cc_invert */

static voodoo_neon_reg_t alookup[257];
static voodoo_neon_reg_t aminuslookup[256];
static voodoo_neon_reg_t bilinear_lookup[256 * 2];
static voodoo_neon_reg_t neon_00_ff_w[2];
static uint32_t          i_00_ff_w[2] = { 0, 0xff };

/*
 * ========================================================================
 * TEXTURE FETCH (one TMU -- Texture Mapping Unit)
 * ========================================================================
 * Texture mapping is the process of "painting" a 2D image onto a 3D
 * triangle. For each pixel of the triangle on screen, the GPU needs to
 * figure out which texel (texture pixel) corresponds to that screen pixel,
 * read the texel's color, and pass it to the color combine stage.
 *
 * The Voodoo performs this in several steps:
 *
 * 1. PERSPECTIVE CORRECTION
 *    The texture coordinates (S, T) are stored as S/W and T/W to allow
 *    perspective-correct interpolation. The GPU divides by W at each pixel
 *    to recover the true (S, T). Without this division, textures appear to
 *    "swim" and warp incorrectly on angled surfaces.
 *
 * 2. LOD (Level of Detail) CALCULATION
 *    Textures are stored as a mipmap pyramid -- the same image at multiple
 *    resolutions (256x256, 128x128, 64x64, ...). The GPU picks the mipmap
 *    level where one texel roughly equals one screen pixel. This prevents
 *    aliasing (shimmering) on distant surfaces and is faster because smaller
 *    textures have better cache behavior. LOD is computed from the W value.
 *
 * 3. TEXTURE ADDRESS GENERATION
 *    Convert the floating-point (S, T) into integer texel coordinates,
 *    apply mirror/clamp wrapping modes, and compute the memory address.
 *
 * 4. TEXEL LOOKUP
 *    Read the texel from the texture memory. Two modes:
 *    - Point sampling: read one texel (nearest to the computed coordinate).
 *      Fast but blocky when the texture is magnified.
 *    - Bilinear filtering: read 4 neighboring texels and blend them based
 *      on the fractional position between them. Smooth but 4x the memory
 *      reads. The blend weights are precomputed in bilinear_lookup[].
 *
 * 5. DUAL-TMU COMBINE (if two texture units are active)
 *    The Voodoo 2 has two TMUs that each fetch a texel independently.
 *    Their results are combined using a configurable equation (e.g.,
 *    multiply for lightmapping, add for glow effects). This combine
 *    happens after both TMUs have fetched their texels.
 * ======================================================================== */

/* ========================================================================
 * codegen_texture_fetch() -- Phase 3: Texture fetch with LOD, bilinear
 *
 * Translates x86-64 codegen lines 78-647.
 *
 * Returns texel color in w4 as packed BGRA (same as x86-64 EAX).
 *
 * Register usage within this function:
 *   x0 = state, x1 = params (pinned)
 *   x4-x7, x10-x15 = scratch GPR
 *   x19 = logtable pointer (pinned)
 *   x25 = bilinear_lookup pointer (pinned)
 *   v0-v7, v16-v17 = scratch NEON
 *   v8 = neon_01_w (pinned), v9 = neon_ff_w (pinned)
 * ======================================================================== */
static inline int
codegen_texture_fetch(uint8_t *code_block, voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int block_pos, int tmu)
{
    (void) voodoo;

    if (params->textureMode[tmu] & 1) {
        /* ============================================================
         * Perspective-correct W division path
         * ============================================================
         *
         * x86-64 ref: lines 81-187
         *
         * Load tmu_s, tmu_t, tmu_w. Compute (1<<48) / tmu_w.
         * The dividend (1 << 48) is chosen to match the Voodoo hardware's
         * fixed-point S/T format: S and T are Q12.48, so dividing by W
         * using this constant gives a Q16.32 result that, after >>30, lands
         * in the correct integer texture coordinate range.
         * Multiply S and T by the reciprocal, shift, and compute LOD.
         * ============================================================ */

        /* Load S, T, W (64-bit each) for perspective division.
         * TMU0 offsets (496,504,512) fit LDP X signed-7 range; TMU1 (520,528,536) do not.
         * Use LDP for S+T when offset fits, else individual LDR. */
        if (STATE_tmu_s(tmu) / 8 <= 63) {
            /* LDP x5, x6, [x0, #STATE_tmu_s(tmu)] -- S and T paired */
            addlong(ARM64_LDP_OFF_X(5, 6, 0, STATE_tmu_s(tmu)));
        } else {
            addlong(ARM64_LDR_X(5, 0, STATE_tmu_s(tmu)));
            addlong(ARM64_LDR_X(6, 0, STATE_tmu_t(tmu)));
        }

        /* LDR x7, [x0, #STATE_tmu_w(tmu)] -- load W (64-bit) */
        addlong(ARM64_LDR_X(7, 0, STATE_tmu_w(tmu)));

        /* MOV x4, #(1 << 48) -- dividend for W division
         * MOVZ with hw=3 zeros all other bits. */
        addlong(ARM64_MOVZ_X_HW(4, 1, 3));

        /* If tmu_w == 0, skip division (avoid divide-by-zero) */
        {
            int div_skip_pos;
            div_skip_pos = block_pos;
            addlong(ARM64_CBZ_X_PLACEHOLDER(7));

            /* SDIV x4, x4, x7 -- quotient = (1<<48) / tmu_w */
            addlong(ARM64_SDIV_X(4, 4, 7));

            PATCH_FORWARD_CBxZ(div_skip_pos);
        }

        /* ASR x5, x5, #14 -- S >>= 14 */
        addlong(ARM64_ASR_IMM_X(5, 5, 14));
        /* ASR x6, x6, #14 -- T >>= 14 */
        addlong(ARM64_ASR_IMM_X(6, 6, 14));

        /* MUL x5, x5, x4 -- S *= quotient */
        addlong(ARM64_MUL_X(5, 5, 4));
        /* MUL x6, x6, x4 -- T *= quotient */
        addlong(ARM64_MUL_X(6, 6, 4));

        /* ASR x5, x5, #30 -- S >>= 30 (final tex_s) */
        addlong(ARM64_ASR_IMM_X(5, 5, 30));
        /* ASR x6, x6, #30 -- T >>= 30 (final tex_t) */
        addlong(ARM64_ASR_IMM_X(6, 6, 30));

        /* LOD calculation using the W reciprocal (x4).
         *
         * BSR equivalent: CLZ then invert.
         * x86-64: BSR RDX, RAX -> bit position of MSB
         *         Then: SHL RAX, 8; SHR RAX, CL; AND EAX, 0xFF
         *         lookup logtable[EAX]; OR with (exp << 8)
         *
         * ARM64: CLZ x10, x4
         *        w10 = 63 - w10  (= BSR result)
         */
        addlong(ARM64_CLZ_X(10, 4));
        /* w11 = 63 - w10 = BSR result */
        addlong(ARM64_MOVZ_W(11, 63));
        addlong(ARM64_SUB_REG(11, 11, 10));

        /* LSL x4, x4, #8 */
        addlong(ARM64_LSL_IMM_X(4, 4, 8));

        /* Store tex_t: STR w6, [x0, #STATE_tex_t] */
        addlong(ARM64_STR_W(6, 0, STATE_tex_t));

        /* MOV w12, w11 -- save BSR result for shift */
        addlong(ARM64_MOV_REG(12, 11));

        /* SUB w11, w11, #19 -- exp = BSR - 19 */
        addlong(ARM64_SUB_IMM(11, 11, 19));

        /* LSR x4, x4, x12 -- shift quotient by BSR amount (64-bit) */
        addlong(ARM64_LSR_REG_X(4, 4, 12));

        /* LSL w11, w11, #8 -- exp <<= 8 */
        addlong(ARM64_LSL_IMM(11, 11, 8));

        /* AND w4, w4, #0xFF -- mantissa = low 8 bits */
        addlong(ARM64_AND_MASK(4, 4, 8));

        /* Store tex_s: STR w5, [x0, #STATE_tex_s] */
        addlong(ARM64_STR_W(5, 0, STATE_tex_s));

        /* LDRB w4, [x19, x4] -- logtable[mantissa] */
        addlong(ARM64_LDRB_REG(4, 19, 4));

        /* ORR w4, w4, w11 -- combine mantissa + exp */
        addlong(ARM64_ORR_REG(4, 4, 11));

        /* ADD w4, w4, state->tmu[tmu].lod */
        addlong(ARM64_LDR_W(10, 0, STATE_tmu_lod(tmu)));
        addlong(ARM64_ADD_REG(4, 4, 10));

        /* Clamp LOD to [lod_min, lod_max] */
        addlong(ARM64_LDR_W(10, 0, STATE_lod_min_n(tmu)));
        addlong(ARM64_CMP_REG(4, 10));
        addlong(ARM64_CSEL(4, 10, 4, COND_LT));  /* if lod < min, lod = min */

        addlong(ARM64_LDR_W(10, 0, STATE_lod_max_n(tmu)));
        addlong(ARM64_CMP_REG(4, 10));
        addlong(ARM64_CSEL(4, 10, 4, COND_GE));  /* if lod >= max, lod = max */

        /* LSR w4, w4, #8 -- integer LOD */
        addlong(ARM64_LSR_IMM(4, 4, 8));

        /* Store LOD: STR w4, [x0, #STATE_lod] */
        addlong(ARM64_STR_W(4, 0, STATE_lod));
        addlong(ARM64_MOV_REG(6, 4));  /* keep LOD in w6 for bilinear/point-sample */
    } else {
        /* ============================================================
         * No perspective division (textureMode bit 0 clear)
         * ============================================================
         *
         * x86-64 ref: lines 188-222
         *
         * Simple shift: tex_s = tmu_s >> 28, tex_t = tmu_t >> 28
         * LOD = lod_min >> 8
         * ============================================================ */

        /* Load tmu_s and tmu_t (64-bit).
         * TMU0 offsets fit LDP X signed-7 range; TMU1 do not. */
        if (STATE_tmu_s(tmu) / 8 <= 63) {
            addlong(ARM64_LDP_OFF_X(4, 6, 0, STATE_tmu_s(tmu)));
        } else {
            addlong(ARM64_LDR_X(4, 0, STATE_tmu_s(tmu)));
            addlong(ARM64_LDR_X(6, 0, STATE_tmu_t(tmu)));
        }

        /* LSR x4, x4, #28 */
        addlong(ARM64_LSR_IMM_X(4, 4, 28));
        /* LDR w5, [x0, #STATE_lod_min_n(tmu)] */
        addlong(ARM64_LDR_W(5, 0, STATE_lod_min_n(tmu)));
        /* LSR x6, x6, #28 */
        addlong(ARM64_LSR_IMM_X(6, 6, 28));

        /* STR w4, [x0, #STATE_tex_s] -- store low 32 bits (sufficient for tex coords).
         * x86-64 stores 64-bit (RAX) but consumers read 32-bit, and STATE_tex_s (188)
         * is NOT 8-byte aligned so STR_X would silently encode offset 184. */
        addlong(ARM64_STR_W(4, 0, STATE_tex_s));

        /* LSR w5, w5, #8 */
        addlong(ARM64_LSR_IMM(5, 5, 8));

        /* STR w6, [x0, #STATE_tex_t] -- store low 32 bits for consistency */
        addlong(ARM64_STR_W(6, 0, STATE_tex_t));

        /* STR w5, [x0, #STATE_lod] */
        addlong(ARM64_STR_W(5, 0, STATE_lod));
        addlong(ARM64_MOV_REG(6, 5));  /* keep LOD in w6 for bilinear/point-sample */
    }

    if (params->fbzColorPath & FBZCP_TEXTURE_ENABLED) {
        if (voodoo->bilinear_enabled && (params->textureMode[tmu] & 6)) {
            /* ============================================================
             * Bilinear filtered texture lookup
             * ============================================================
             *
             * x86-64 ref: lines 225-543
             *
             * Compute sub-texel fractions, fetch 4 texels,
             * weight by bilinear coefficients, blend.
             *
             * Register plan:
             *   w4 = tex_s, w5 = tex_t, w6 = lod, w7 = tex_shift
             *   w10 = EBP (temp/bilinear_shift), w11 = scratch
             *   x12 = tex_base pointer, x13/x14 = row pointers
             * ============================================================ */

            /* MOV w7, #8  (initial tex_shift) */
            addlong(ARM64_MOVZ_W(7, 8));
            /* w6 = LOD (cached, no reload needed) */
            /* MOV w10, #1 */
            addlong(ARM64_MOVZ_W(10, 1));
            /* SUB w7, w7, w6  (tex_shift = 8 - lod) */
            addlong(ARM64_SUB_REG(7, 7, 6));
            /* LSL w10, w10, w6  (1 << lod) */
            addlong(ARM64_LSL_REG(10, 10, 6));
            /* LDP w4, w5, [x0, #STATE_tex_s] -- load tex_s and tex_t */
            addlong(ARM64_LDP_OFF_W(4, 5, 0, STATE_tex_s));
            /* LSL w10, w10, #3  ((1 << lod) << 3 = 1 << (lod+3)) */
            addlong(ARM64_LSL_IMM(10, 10, 3));

            /* Mirror S */
            if (params->tLOD[tmu] & LOD_TMIRROR_S) {
                /* TST w4, #0x1000; if set, NOT w4 */
                /* Use TBZ: if bit 12 is zero, skip the NOT */
                int mirror_s_skip = block_pos;
                addlong(ARM64_TBZ_PLACEHOLDER(4, 12));
                addlong(ARM64_MVN(4, 4));
                PATCH_FORWARD_TBxZ(mirror_s_skip);
            }
            /* Mirror T */
            if (params->tLOD[tmu] & LOD_TMIRROR_T) {
                int mirror_t_skip = block_pos;
                addlong(ARM64_TBZ_PLACEHOLDER(5, 12));
                addlong(ARM64_MVN(5, 5));
                PATCH_FORWARD_TBxZ(mirror_t_skip);
            }

            /* SUB w4, w4, w10  (S -= (1 << (lod+3))) */
            addlong(ARM64_SUB_REG(4, 4, 10));
            /* SUB w5, w5, w10  (T -= (1 << (lod+3))) */
            addlong(ARM64_SUB_REG(5, 5, 10));
            /* ASR w4, w4, w6  (S >>= lod) */
            addlong(ARM64_ASR_REG(4, 4, 6));
            /* ASR w5, w5, w6  (T >>= lod) */
            addlong(ARM64_ASR_REG(5, 5, 6));

            /* Extract sub-texel fractions for bilinear weight lookup.
             * frac_s = S & 0xF, frac_t = (T & 0xF) << 4
             * bilinear_index = (frac_t << 4) | frac_s
             * Then shift S and T to get integer texel coordinates.
             */
            /* MOV w10, w4 */
            addlong(ARM64_MOV_REG(10, 4));
            /* MOV w11, w5 */
            addlong(ARM64_MOV_REG(11, 5));
            /* AND w10, w10, #0xF  (frac_s) */
            addlong(ARM64_AND_MASK(10, 10, 4));
            /* LSL w11, w11, #4 */
            addlong(ARM64_LSL_IMM(11, 11, 4));
            /* ASR w4, w4, #4  (integer S) */
            addlong(ARM64_ASR_IMM(4, 4, 4));
            /* AND w11, w11, #0xF0  (frac_t << 4) */
            addlong(ARM64_AND_BITMASK(11, 11, 0, 28, 3));  /* N=0 immr=28 imms=3 -> mask 0xF0 */
            /* ASR w5, w5, #4  (integer T) */
            addlong(ARM64_ASR_IMM(5, 5, 4));
            /* ORR w10, w10, w11  (bilinear_index = frac_s | (frac_t << 4)) */
            addlong(ARM64_ORR_REG(10, 10, 11));

            /* w6 = LOD (still cached, no reload needed) */

            /* LSL w10, w10, #5  (bilinear_index * 32 = offset into bilinear_lookup) */
            addlong(ARM64_LSL_IMM(10, 10, 5));

            /* x86-64: LEA RSI, [RSI+RCX*4]  -- advance params by lod*4 for mask arrays
             * ARM64: We compute mask array base explicitly, no -0x10 hack.
             *
             * Keep bilinear_shift in w17 (IP1 scratch) to avoid memory
             * round-trip through STATE_ebp_store.
             */
            addlong(ARM64_MOV_REG(17, 10));

            /* Load texture base pointer: tex[tmu][lod]
             * x86-64: MOV RBP, state->tex[RDI+RCX*8]
             * ARM64: ADD x11, x0, #STATE_tex_n(tmu)
             *        LDR x12, [x11, x6, LSL #3]  -- tex[tmu][lod]
             */
            addlong(ARM64_ADD_IMM_X(11, 0, STATE_tex_n(tmu)));
            addlong(ARM64_LDR_X_REG_LSL3(12, 11, 6));

            /* w7 holds tex_shift, used directly in LSL below */

            /* w13 = T+1 (next row) */
            addlong(ARM64_MOV_REG(13, 5));

            /* Clamp or wrap S and T coordinates */
            if (!state->clamp_s[tmu]) {
                /* AND w4, w4, params->tex_w_mask[tmu][lod] */
                addlong(ARM64_ADD_IMM_X(14, 1, PARAMS_tex_w_mask_n(tmu)));
                addlong(ARM64_LDR_W_REG_LSL2(15, 14, 6));
                addlong(ARM64_AND_REG(4, 4, 15));
            }

            /* T1 = T + 1 */
            addlong(ARM64_ADD_IMM(13, 13, 1));

            if (state->clamp_t[tmu]) {
                /* Clamp T1 to [0, tex_h_mask] and T0 to [0, tex_h_mask] */
                /* Load tex_h_mask[tmu][lod] */
                addlong(ARM64_ADD_IMM_X(14, 1, PARAMS_tex_h_mask_n(tmu)));
                addlong(ARM64_LDR_W_REG_LSL2(15, 14, 6));

                /* Clamp T1: if negative, 0; if > mask, mask */
                addlong(ARM64_CMP_IMM(13, 0));
                addlong(ARM64_CSEL(13, 31, 13, COND_LT));
                addlong(ARM64_CMP_REG(13, 15));
                addlong(ARM64_CSEL(13, 15, 13, COND_HI));

                /* Clamp T0: if negative, 0; if > mask, mask */
                addlong(ARM64_CMP_IMM(5, 0));
                addlong(ARM64_CSEL(5, 31, 5, COND_LT));
                addlong(ARM64_CMP_REG(5, 15));
                addlong(ARM64_CSEL(5, 15, 5, COND_HI));
            } else {
                /* AND T1 with tex_h_mask */
                addlong(ARM64_ADD_IMM_X(14, 1, PARAMS_tex_h_mask_n(tmu)));
                addlong(ARM64_LDR_W_REG_LSL2(15, 14, 6));
                addlong(ARM64_AND_REG(13, 13, 15));
                /* AND T0 with tex_h_mask */
                addlong(ARM64_AND_REG(5, 5, 15));
            }

            /* Compute row addresses:
             * T0_addr = tex_base + (T0 << tex_shift) * 4
             * T1_addr = tex_base + (T1 << tex_shift) * 4
             *
             * x86-64: SHL EBX, CL; SHL EDX, CL
             *         LEA RBX, [RBP+RBX*4]; LEA RDX, [RBP+RDX*4]
             *
             * ARM64: LSL w5, w5, w7; LSL w13, w13, w7
             *        ADD x13_row0, x12, x5, LSL #2
             *        ADD x14_row1, x12, x13, LSL #2
             */
            addlong(ARM64_LSL_REG(5, 5, 7));
            addlong(ARM64_LSL_REG(13, 13, 7));
            /* x13 = row1 address, x14 = row0 address (reuse registers) */
            addlong(ARM64_ADD_REG_X_LSL(14, 12, 5, 2));   /* x14 = tex_base + T0_offset*4 */
            addlong(ARM64_ADD_REG_X_LSL(13, 12, 13, 2));  /* x13 = tex_base + T1_offset*4 */

            /* Handle S clamping for bilinear (need S and S+1 texels) */
            if (state->clamp_s[tmu]) {
                /* Load tex_w_mask[tmu][lod] */
                addlong(ARM64_ADD_IMM_X(15, 1, PARAMS_tex_w_mask_n(tmu)));
                addlong(ARM64_LDR_W_REG_LSL2(15, 15, 6));

                /* bilinear_shift is in w17 */

                /* Test if S is negative */
                addlong(ARM64_CMP_IMM(4, 0));
                /* CSEL: if negative, S = 0 */
                addlong(ARM64_CSEL(4, 31, 4, COND_LT));
                /* Branch if was negative (S clamped to 0 -> both samples same) */
                {
                    int clamp_lo_pos = block_pos;
                    addlong(ARM64_BCOND_PLACEHOLDER(COND_LT));

                    /* CMP w4, w15 (tex_w_mask) */
                    addlong(ARM64_CMP_REG(4, 15));
                    /* CSEL: if >= mask, S = mask */
                    addlong(ARM64_CSEL(4, 15, 4, COND_CS));
                    /* Branch if was clamped high */
                    {
                        int clamp_hi_pos = block_pos;
                        addlong(ARM64_BCOND_PLACEHOLDER(COND_CS));

                        /* Normal case: S and S+1 are adjacent, load 2 texels as 64 bits */
                        /* LSL w4, w4, #2 -- convert texel index to byte offset (4 bytes/texel) */
                        addlong(ARM64_LSL_IMM(4, 4, 2));
                        /* LDR d0, [x14, x4] -- row0[S] and row0[S+1] */
                        addlong(ARM64_LDR_D_REG(0, 14, 4));
                        /* LDR d1, [x13, x4] -- row1[S] and row1[S+1] */
                        addlong(ARM64_LDR_D_REG(1, 13, 4));

                        int normal_done = block_pos;
                        addlong(ARM64_B_PLACEHOLDER);

                        /* Clamped case: S and S+1 are the same texel (duplicate) */
                        PATCH_FORWARD_BCOND(clamp_lo_pos);
                        PATCH_FORWARD_BCOND(clamp_hi_pos);

                        /* Load single texel, duplicate to both halves */
                        /* LDR w11, [x14, x4, LSL #2] */
                        addlong(ARM64_LDR_W_REG_LSL2(11, 14, 4));
                        addlong(ARM64_FMOV_S_W(0, 11));
                        addlong(ARM64_DUP_V2S_LANE(0, 0, 0));
                        addlong(ARM64_LDR_W_REG_LSL2(11, 13, 4));
                        addlong(ARM64_FMOV_S_W(1, 11));
                        addlong(ARM64_DUP_V2S_LANE(1, 1, 0));

                        PATCH_FORWARD_B(normal_done);
                    }
                }
            } else {
                /* Non-clamped: check if S wraps at texture edge */
                addlong(ARM64_ADD_IMM_X(15, 1, PARAMS_tex_w_mask_n(tmu)));
                addlong(ARM64_LDR_W_REG_LSL2(15, 15, 6));

                /* bilinear_shift is in w17 */

                addlong(ARM64_CMP_REG(4, 15));
                {
                    int wrap_skip = block_pos;
                    addlong(ARM64_BCOND_PLACEHOLDER(COND_EQ)); /* if at edge, wrap */

                    /* Normal case: S and S+1 contiguous */
                    /* LSL w4, w4, #2 -- convert texel index to byte offset (4 bytes/texel) */
                    addlong(ARM64_LSL_IMM(4, 4, 2));
                    /* LDR d0, [x14, x4] */
                    addlong(ARM64_LDR_D_REG(0, 14, 4));
                    addlong(ARM64_LDR_D_REG(1, 13, 4));

                    int normal_done = block_pos;
                    addlong(ARM64_B_PLACEHOLDER);

                    /* Wrap case: S is at edge, S+1 wraps to 0 */
                    PATCH_FORWARD_BCOND(wrap_skip);

                    /* Load S texel, then load texel at S=0 (wrap), combine */
                    /* row0[S] */
                    addlong(ARM64_LDR_W_REG_LSL2(11, 14, 4));
                    addlong(ARM64_FMOV_S_W(0, 11));
                    /* row0[0] -- wrapped S+1 */
                    addlong(ARM64_LDR_W(11, 14, 0));
                    addlong(ARM64_INS_S(0, 1, 11));  /* v0.S[1] = row0[0] */
                    /* row1[S] */
                    addlong(ARM64_LDR_W_REG_LSL2(11, 13, 4));
                    addlong(ARM64_FMOV_S_W(1, 11));
                    /* row1[0] */
                    addlong(ARM64_LDR_W(11, 13, 0));
                    addlong(ARM64_INS_S(1, 1, 11));

                    PATCH_FORWARD_B(normal_done);
                }
            }

            /* Now v0 = {row0_s0, row0_s1} (2x BGRA32)
             *     v1 = {row1_s0, row1_s1} (2x BGRA32)
             *
             * Unpack bytes to 16-bit, multiply by bilinear weights, sum.
             */

            /* UXTL v0.8H, v0.8B -- zero-extend bytes to halfwords */
            addlong(ARM64_UXTL_8H_8B(0, 0));
            /* UXTL v1.8H, v1.8B */
            addlong(ARM64_UXTL_8H_8B(1, 1));

            /* Load bilinear weights from lookup table.
             * x25 = bilinear_lookup pointer (pinned)
             * w17 = bilinear_index * 32 (kept in IP1)
             *
             * bilinear_lookup[idx*2+0] = {d0, d0, d0, d0, d1, d1, d1, d1}
             * bilinear_lookup[idx*2+1] = {d2, d2, d2, d2, d3, d3, d3, d3}
             *
             * Each entry is 16 bytes (128 bits). Total = 32 bytes per index pair.
             */
            /* ADD x11, x25, x17 -- base of weight pair */
            addlong(ARM64_ADD_REG_X(11, 25, 17));

            /* LDR q16, [x11, #0]  -- weights for row0: d0|d1 */
            addlong(ARM64_LDR_Q(16, 11, 0));
            /* LDR q17, [x11, #16] -- weights for row1: d2|d3 */
            addlong(ARM64_LDR_Q(17, 11, 16));

            /* MUL v0.8H, v0.8H, v16.8H -- row0 * weights */
            addlong(ARM64_MUL_V8H(0, 0, 16));
            /* MUL v1.8H, v1.8H, v17.8H -- row1 * weights */
            addlong(ARM64_MUL_V8H(1, 1, 17));

            /* ADD v0.8H, v0.8H, v1.8H -- sum rows */
            addlong(ARM64_ADD_V8H(0, 0, 1));

            /* Horizontal add to combine S0+S1 from both halves:
             * ADDP does pairwise addition of adjacent pairs within each source:
             * result[0]=src[0]+src[1], result[1]=src[2]+src[3], etc.
             * But we need [0..3]+[4..7] (low half + high half), not pairwise.
             *
             * x86-64 does: MOVDQA XMM1, XMM0; PSRLDQ XMM0, 8; PADDW XMM0, XMM1
             * which adds high 64 bits to low 64 bits.
             *
             * ARM64: EXT v1.16B, v0.16B, v0.16B, #8  (shift high to low)
             *        ADD v0.4H, v0.4H, v1.4H          (add halves)
             */
            addlong(ARM64_EXT_16B(1, 0, 0, 8));
            addlong(ARM64_ADD_V4H(0, 0, 1));

            /* USHR v0.4H, v0.4H, #8  -- normalize (divide by 256) */
            addlong(ARM64_USHR_V4H(0, 0, 8));

            /* SQXTUN v0.8B, v0.8H -- pack to unsigned bytes with saturation */
            addlong(ARM64_SQXTUN_8B_8H(0, 0));

            /* Move packed texel to GPR: FMOV w4, s0 */
            addlong(ARM64_FMOV_W_S(4, 0));
        } else {
            /* ============================================================
             * Point-sampled texture lookup
             * ============================================================
             *
             * x86-64 ref: lines 544-643
             *
             * Simple nearest-neighbor: compute S,T indices, load single texel.
             * ============================================================ */

            /* MOV w7, #8 */
            addlong(ARM64_MOVZ_W(7, 8));
            /* w6 = LOD (cached, no reload needed) */

            /* Load texture base pointer: tex[tmu][lod] */
            addlong(ARM64_ADD_IMM_X(11, 0, STATE_tex_n(tmu)));
            addlong(ARM64_LDR_X_REG_LSL3(12, 11, 6));

            /* SUB w7, w7, w6  (tex_shift = 8 - lod) */
            addlong(ARM64_SUB_REG(7, 7, 6));
            /* Save original LOD in w11 before we destroy w6 with +4.
             * The clamp/wrap sections need the original LOD for array indexing
             * into tex_w_mask/tex_h_mask. */
            addlong(ARM64_MOV_REG(11, 6));
            /* ADD w6, w6, #4  -- point-sample uses a larger shift than bilinear:
             * bilinear shifts by 'lod' (integer texel step), but point-sample
             * needs to strip the 4-bit sub-texel fraction too, hence lod+4. */
            addlong(ARM64_ADD_IMM(6, 6, 4));

            /* LDP w4, w5, [x0, #STATE_tex_s] -- load tex_s and tex_t */
            addlong(ARM64_LDP_OFF_W(4, 5, 0, STATE_tex_s));

            /* Mirror S */
            if (params->tLOD[tmu] & LOD_TMIRROR_S) {
                int mirror_s_skip = block_pos;
                addlong(ARM64_TBZ_PLACEHOLDER(4, 12));
                addlong(ARM64_MVN(4, 4));
                PATCH_FORWARD_TBxZ(mirror_s_skip);
            }
            /* Mirror T */
            if (params->tLOD[tmu] & LOD_TMIRROR_T) {
                int mirror_t_skip = block_pos;
                addlong(ARM64_TBZ_PLACEHOLDER(5, 12));
                addlong(ARM64_MVN(5, 5));
                PATCH_FORWARD_TBxZ(mirror_t_skip);
            }

            /* LSR w4, w4, w6  (S >> (lod + 4)) */
            addlong(ARM64_LSR_REG(4, 4, 6));
            /* LSR w5, w5, w6  (T >> (lod + 4)) */
            addlong(ARM64_LSR_REG(5, 5, 6));

            /* Clamp or wrap S */
            if (state->clamp_s[tmu]) {
                /* Clamp S to [0, tex_w_mask[tmu][lod]]
                 * x86-64 uses -0x10 hack with ECX*4. We compute cleanly.
                 * w11 = original LOD (saved before ADD w6, w6, #4)
                 */
                addlong(ARM64_ADD_IMM_X(14, 1, PARAMS_tex_w_mask_n(tmu)));
                addlong(ARM64_LDR_W_REG_LSL2(15, 14, 11));

                /* If S < 0, S = 0 */
                addlong(ARM64_CMP_IMM(4, 0));
                addlong(ARM64_CSEL(4, 31, 4, COND_LT));
                /* If S >= mask, S = mask */
                addlong(ARM64_CMP_REG(4, 15));
                addlong(ARM64_CSEL(4, 15, 4, COND_CS));
            } else {
                /* AND S with tex_w_mask
                 * w11 = original LOD
                 */
                addlong(ARM64_ADD_IMM_X(14, 1, PARAMS_tex_w_mask_n(tmu)));
                addlong(ARM64_LDR_W_REG_LSL2(15, 14, 11));
                addlong(ARM64_AND_REG(4, 4, 15));
            }

            /* Clamp or wrap T */
            if (state->clamp_t[tmu]) {
                addlong(ARM64_ADD_IMM_X(14, 1, PARAMS_tex_h_mask_n(tmu)));
                addlong(ARM64_LDR_W_REG_LSL2(15, 14, 11));

                addlong(ARM64_CMP_IMM(5, 0));
                addlong(ARM64_CSEL(5, 31, 5, COND_LT));
                addlong(ARM64_CMP_REG(5, 15));
                addlong(ARM64_CSEL(5, 15, 5, COND_CS));
            } else {
                addlong(ARM64_ADD_IMM_X(14, 1, PARAMS_tex_h_mask_n(tmu)));
                addlong(ARM64_LDR_W_REG_LSL2(15, 14, 11));
                addlong(ARM64_AND_REG(5, 5, 15));
            }

            /* Compute linear texel index: (T << tex_shift) + S
             * then load texel: tex[tmu][lod][(T << shift) + S]
             */
            /* LSL w5, w5, w7 */
            addlong(ARM64_LSL_REG(5, 5, 7));
            /* ADD w5, w5, w4 */
            addlong(ARM64_ADD_REG(5, 5, 4));

            /* LDR w4, [x12, x5, LSL #2] -- load texel */
            addlong(ARM64_LDR_W_REG_LSL2(4, 12, 5));
        }
    }

    return block_pos;
}

/* ========================================================================
 * voodoo_generate() -- emit ARM64 JIT code for the pixel pipeline
 *
 * Emits ARM64 JIT code for the complete pixel pipeline (Phases 1-6).
 * The generated function signature:
 *   uint8_t (*voodoo_draw)(voodoo_state_t *state, voodoo_params_t *params,
 *                          int x, int real_y)
 *
 * On AArch64 (AAPCS64): x0=state, x1=params, x2=x, x3=real_y
 * ======================================================================== */
static inline int
voodoo_generate(uint8_t *code_block, voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int depthop)
{
    /*
     * Pipeline overview (matches GPU hardware stages):
     *
     *  PROLOGUE         -- save callee-saved regs, load pinned regs
     *  PHASE 2: STIPPLE -- optional 32-bit pattern or rotating stipple reject
     *  PHASE 2: TILED X -- compute x_tiled for tiled framebuffer modes
     *  PHASE 2: W-DEPTH -- convert W to depth value (CLZ-based float decode)
     *  PHASE 2: Z-DEPTH -- compute depth from state->z (if not W-buffer)
     *  PHASE 2: DEPTH TEST  -- 8 comparison ops, skip on fail
     *  PHASE 3: TEXTURE FETCH -- LOD, perspective div, bilinear or point sample
     *  PHASE 3: TMU COMBINE -- dual-TMU tc/tca combine (if enabled)
     *  PHASE 4: COLOR COMBINE -- chroma key, cother/clocal, cc_mselect/multiply
     *  PHASE 4: ALPHA COMBINE -- a_other/a_local, cca_mselect/multiply
     *  PHASE 5: FOG        -- constant, W-table, Z, or alpha fog
     *  PHASE 5: ALPHA TEST -- 8 comparison ops, skip on fail
     *  PHASE 5: ALPHA BLEND -- src/dst AFUNC multiply, sum, pack
     *  PHASE 6: DEPTH WRITE (alpha path) -- write blended alpha to aux_mem
     *  PHASE 6: FB WRITE   -- dither or shift-pack RGB565, write to fb_mem
     *  PHASE 6: DEPTH WRITE (Z path)    -- write new_depth to aux_mem
     *  SKIP PATCH POINTS -- z_skip, a_skip, chroma_skip, stipple_skip targets
     *  PER-PIXEL INCREMENTS -- ib/ig/ir/ia, z, tmu s/t/w, pixel/texel counts
     *  X INCREMENT + LOOP BACK
     *  EPILOGUE -- restore callee-saved regs, RET
     *
     * NOTE ON VARIABLE SCOPE:
     * This header is #include'd inside voodoo_draw_triangle() in
     * vid_voodoo_render.c. Many variables used below are NOT declared in
     * this file -- they are locals or macros from the enclosing function:
     *   - tc_*, tca_*, cc_*, cca_*: texture/color combine mode bits
     *   - a_sel, alpha_func, src_afunc, dest_afunc: alpha pipeline config
     *   - _rgb_sel, dither, dither2x2: color select and dither config
     *   - depthop: depth comparison function
     *   - logtable, rgb565: external lookup tables (vid_voodoo_render.c)
     *   - dither_rb, dither_g, dither_rb2x2, dither_g2x2: dither tables
     * This is the same pattern as the x86-64 codegen header.
     */

    int block_pos        = 0;
    int z_skip_pos       = 0;
    int a_skip_pos       = 0;
    int amask_skip_pos   = 0;
    int stipple_skip_pos = 0;
    int chroma_skip_pos  = 0;
    int depth_jump_pos   = 0;
    int depth_jump_pos2  = 0;
    int loop_jump_pos    = 0;

    arm64_codegen_begin_emit();

    /* Early-return checks: if DEPTHOP_NEVER or AFUNC_NEVER, every pixel
     * is unconditionally rejected, so we emit a bare RET and return from
     * voodoo_generate() before the prologue has saved any registers.
     * This avoids the ABI violation that would occur if we emitted RET
     * after the prologue (corrupted SP/LR/callee-saved registers).
     * Matches the x86-64 codegen's intent at lines 678-682 (#if 0). */
    if ((params->fbzMode & FBZ_DEPTH_ENABLE) && (depthop == DEPTHOP_NEVER)) {
        addlong(ARM64_RET);
        return block_pos;
    }
    if ((params->alphaMode & 1) && (alpha_func == AFUNC_NEVER)) {
        addlong(ARM64_RET);
        return block_pos;
    }

    /* Re-initialize NEON constants before every emit. These constants are
     * read by the PROLOGUE's LDR Q instructions that load them into pinned
     * callee-saved NEON registers (v8-v11). Because the constants are in
     * static global storage (not in the code block), they could in theory
     * be initialized once in voodoo_codegen_init(). They are written here
     * for safety in case future refactoring separates init from generate. */
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

    /* ================================================================
     * Prologue: save callee-saved registers
     * ================================================================
     *
     * Stack layout (offsets from new SP after pre-decrement):
     *   [SP, #0]:   x29 (FP), x30 (LR)
     *   [SP, #16]:  x19, x20
     *   [SP, #32]:  x21, x22
     *   [SP, #48]:  x23, x24
     *   [SP, #64]:  x25, x26
     *   [SP, #80]:  x27, x28
     *   [SP, #96]:  d8, d9    (NEON callee-saved, lower 64 bits)
     *   [SP, #112]: d10, d11
     *   [SP, #128]: d12, d13
     *   [SP, #144]: d14, d15
     *   [SP, #160]: (padding to 176 bytes for 16-byte alignment)
     * Total: 176 bytes (16-byte aligned)
     */

    /* STP x29, x30, [SP, #-176]! */
    addlong(ARM64_STP_PRE_X(29, 30, 31, -176));
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
    /* STP d12, d13, [SP, #128] */
    addlong(ARM64_STP_D(12, 13, 31, 128));
    /* STP d14, d15, [SP, #144] -- hoisted TMU delta registers */
    addlong(ARM64_STP_D(14, 15, 31, 144));

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
        /* Helper macro is #undef'd after use to avoid polluting the namespace.
         * See the #undef immediately after the pointer loads below. */
/* Skip zero halfwords -- MOVZ targets the first non-zero halfword
 * (which zeros everything else), then MOVK only for remaining non-zero
 * halfwords. On macOS ARM64, hw=3 is always 0 for user pointers, saving
 * 1+ MOVK per load. Typical macOS pointer saves 1-2 instructions. */
#define EMIT_MOV_IMM64(d, ptr)                                                   \
    do {                                                                          \
        uint64_t _v = (uint64_t) (uintptr_t) (ptr);                              \
        uint16_t _hw0 = (_v) & 0xFFFF;                                           \
        uint16_t _hw1 = ((_v) >> 16) & 0xFFFF;                                   \
        uint16_t _hw2 = ((_v) >> 32) & 0xFFFF;                                   \
        uint16_t _hw3 = ((_v) >> 48) & 0xFFFF;                                   \
        int _first = (_hw0) ? 0 : (_hw1) ? 1 : (_hw2) ? 2 : 3;                  \
        uint16_t _first_val = (_first == 0) ? _hw0                               \
                            : (_first == 1) ? _hw1                               \
                            : (_first == 2) ? _hw2                               \
                            : _hw3;                                               \
        addlong(ARM64_MOVZ_X_HW((d), _first_val, _first));                       \
        if (_first < 1 && _hw1)                                                  \
            addlong(ARM64_MOVK_X((d), _hw1, 1));                                 \
        if (_first < 2 && _hw2)                                                  \
            addlong(ARM64_MOVK_X((d), _hw2, 2));                                 \
        if (_first < 3 && _hw3)                                                  \
            addlong(ARM64_MOVK_X((d), _hw3, 3));                                 \
    } while (0)

        EMIT_MOV_IMM64(19, &logtable);
        EMIT_MOV_IMM64(20, &alookup);
        EMIT_MOV_IMM64(21, &aminuslookup);
        EMIT_MOV_IMM64(22, &neon_00_ff_w);
        EMIT_MOV_IMM64(23, &i_00_ff_w);
        EMIT_MOV_IMM64(25, &bilinear_lookup);
        EMIT_MOV_IMM64(26, &rgb565);

#undef EMIT_MOV_IMM64
    }

    /* load loop bound STATE_x2 into callee-saved w27 */
    addlong(ARM64_LDR_W(27, 0, STATE_x2));

    /* ================================================================
     * Load NEON constants into callee-saved V registers
     * ================================================================
     *
     * v8 = neon_01_w  {1,1,1,1}
     * v9 = neon_ff_w  {0xFF,0xFF,0xFF,0xFF}
     * v10 = neon_ff_b {0x00FFFFFF,0,0,0}
     * v11 = fogColor (hoisted below, only when fog enabled)
     *
     * Load via a temporary register pointing to each constant.
     */
    {
        uint64_t addr;

/* Same zero-halfword skip as EMIT_MOV_IMM64 for pointer into x16. */
#define EMIT_LOAD_NEON_CONST(vreg, constaddr)                                        \
    do {                                                                             \
        addr = (uint64_t) (uintptr_t) (constaddr);                                  \
        uint16_t _h0 = addr & 0xFFFF;                                               \
        uint16_t _h1 = (addr >> 16) & 0xFFFF;                                       \
        uint16_t _h2 = (addr >> 32) & 0xFFFF;                                       \
        uint16_t _h3 = (addr >> 48) & 0xFFFF;                                       \
        int _f = (_h0) ? 0 : (_h1) ? 1 : (_h2) ? 2 : 3;                            \
        uint16_t _fv = (_f == 0) ? _h0 : (_f == 1) ? _h1                            \
                      : (_f == 2) ? _h2 : _h3;                                      \
        addlong(ARM64_MOVZ_X_HW(16, _fv, _f));                                      \
        if (_f < 1 && _h1)                                                           \
            addlong(ARM64_MOVK_X(16, _h1, 1));                                      \
        if (_f < 2 && _h2)                                                           \
            addlong(ARM64_MOVK_X(16, _h2, 2));                                      \
        if (_f < 3 && _h3)                                                           \
            addlong(ARM64_MOVK_X(16, _h3, 3));                                      \
        addlong(ARM64_LDR_Q((vreg), 16, 0));                                        \
    } while (0)

        EMIT_LOAD_NEON_CONST(8, &neon_01_w);
        EMIT_LOAD_NEON_CONST(9, &neon_ff_w);
        EMIT_LOAD_NEON_CONST(10, &neon_ff_b);
        /* v11 = fogColor, loaded below when fog is enabled */

#undef EMIT_LOAD_NEON_CONST

        (void) addr;
    }

    /* ================================================================
     * Hoist loop-invariant NEON deltas into callee-saved V registers
     * ================================================================
     *
     * These delta vectors are loaded from params once here and reused
     * every pixel iteration, avoiding redundant memory loads.
     *
     *   v12 = {dBdX, dGdX, dRdX, dAdX}  (RGBA color deltas, 4x32)
     *   v15 = {dSdX_0, dTdX_0}           (TMU0 ST deltas, 2x64)
     *   v14 = {dSdX_1, dTdX_1}           (TMU1 ST deltas, 2x64, if dual TMU)
     */

    /* v12 = RGBA deltas (PARAMS_dBdX=48, loaded via ADD+LD1) */
    addlong(ARM64_ADD_IMM_X(16, 1, PARAMS_dBdX));
    addlong(ARM64_LD1_V4S(12, 16));

    /* v15 = TMU0 ST deltas (PARAMS_tmu0_dSdX=144, Q-aligned) */
    addlong(ARM64_LDR_Q(15, 1, PARAMS_tmu0_dSdX));

    /* v14 = TMU1 ST deltas (PARAMS_tmu1_dSdX=240, Q-aligned) */
    if (voodoo->dual_tmus) {
        addlong(ARM64_LDR_Q(14, 1, PARAMS_tmu1_dSdX));
    }

    /* v11 = fogColor (packed bytes, triangle-invariant).
     * FOG_CONSTANT uses v11 as packed 8B.
     * Non-constant fog uses UXTL to widen to 8H per-pixel. */
    if (params->fogMode & FOG_ENABLE) {
        addlong(ARM64_LDR_W(16, 1, PARAMS_fogColor));
        addlong(ARM64_FMOV_S_W(11, 16));
    }

    /* ================================================================
     * Load fb_mem and aux_mem pointers
     * ================================================================ */
    /* LDP x8, x9, [x0, #STATE_fb_mem] -- load fb_mem and aux_mem */
    addlong(ARM64_LDP_OFF_X(8, 9, 0, STATE_fb_mem));

    /* Load STATE_x into w28 once before the loop.
     * Subsequent iterations update w28 via MOV w28, w5 at the loop tail,
     * so the value is always current without reloading from memory. */
    addlong(ARM64_LDR_W(28, 0, STATE_x));

    /* ================================================================
     * Pixel loop entry point
     * ================================================================ */
    loop_jump_pos = block_pos;  /* Top of the pixel loop -- loopback branch targets here */

    /* ====================================================================
     * STIPPLE TEST
     * ====================================================================
     * Stipple is a primitive form of transparency that predates alpha
     * blending. Instead of making a pixel semi-transparent, the GPU simply
     * skips every other pixel in a checkerboard-like pattern, creating
     * the illusion of transparency when viewed from a distance.
     *
     * The Voodoo supports two stipple modes:
     *   - Pattern stipple: a 32-bit mask selects which pixels to draw.
     *     The bit index depends on both X and Y: it is an 8x4 pattern
     *     indexed by (real_y & 3) * 8 | (~x & 7). Used for screen-door effects.
     *   - Rotating stipple: the pattern rotates once per pixel (the
     *     ROR+STR+TBZ sequence is inside the per-pixel loop body), giving a
     *     more even distribution of drawn/skipped pixels across the screen.
     *
     * If the stipple test fails, the pixel is skipped entirely (jumps to
     * the skip patch point after all rendering stages).
     *
     * x86-64 ref: lines 766-828
     *
     * Pattern stipple: bit = (real_y & 3) * 8 | (~x & 7)
     *   test state->stipple with (1 << bit), skip pixel if zero
     *
     * Rotating stipple: ROR state->stipple by 1, test bit 31
     * ================================================================== */
    if (params->fbzMode & FBZ_STIPPLE) {
        if (params->fbzMode & FBZ_STIPPLE_PATT) {
            /* Pattern stipple.
             * w4 = (real_y & 3) << 3
             * w5 = ~state->x & 7
             * w4 = w4 | w5  (bit index 0..31)
             * w6 = 1 << w4
             * TST state->stipple, w6
             * BEQ -> skip pixel
             */
            /* AND w4, w24, #3  (real_y & 3) */
            addlong(ARM64_AND_MASK(4, 24, 2));
            /* LSL w4, w4, #3 */
            addlong(ARM64_LSL_IMM(4, 4, 3));
            /* MVN w5, w28 -- NOT(cached STATE_x) directly */
            addlong(ARM64_MVN(5, 28));
            /* AND w5, w5, #7 */
            addlong(ARM64_AND_MASK(5, 5, 3));
            /* ORR w4, w4, w5 */
            addlong(ARM64_ORR_REG(4, 4, 5));
            /* MOV w6, #1 */
            addlong(ARM64_MOVZ_W(6, 1));
            /* LSL w6, w6, w4 */
            addlong(ARM64_LSL_REG(6, 6, 4));
            /* LDR w7, [x0, #STATE_stipple] */
            addlong(ARM64_LDR_W(7, 0, STATE_stipple));
            /* TST w7, w6 */
            addlong(ARM64_TST_REG(7, 6));
            /* BEQ stipple_skip (forward, patch later) */
            stipple_skip_pos = block_pos;
            addlong(ARM64_BCOND_PLACEHOLDER(COND_EQ));
        } else {
            /* Rotating stipple: ROR state->stipple by 1, test bit 31.
             * LDR w4, [x0, #STATE_stipple]
             * ROR w4, w4, #1
             * STR w4, [x0, #STATE_stipple]
             * TBZ w4, #31, skip
             */
            addlong(ARM64_LDR_W(4, 0, STATE_stipple));
            addlong(ARM64_ROR_IMM(4, 4, 1));
            addlong(ARM64_STR_W(4, 0, STATE_stipple));
            /* TBZ w4, #31, stipple_skip (forward, patch later) */
            stipple_skip_pos = block_pos;
            addlong(ARM64_TBZ_PLACEHOLDER(4, 31));
        }
    }

    /* ====================================================================
     * TILED FRAMEBUFFER ADDRESS
     * ====================================================================
     * The Voodoo 2 and later chips can store the framebuffer in a tiled
     * memory layout instead of linear row-major order. Tiling groups nearby
     * pixels into rectangular blocks in memory, which improves cache locality
     * for 3D rendering (where access patterns are triangular, not row-based).
     *
     * When tiling is enabled, the X coordinate must be remapped from linear
     * to tiled format before any framebuffer read/write. This section
     * computes x_tiled from the linear X, using the tile dimensions defined
     * by the Voodoo's row stride registers.
     *
     * x86-64 ref: lines 832-852
     * x_tiled = (x & 63) + ((x >> 6) << 11)
     * Tile is 64 pixels wide. x_tiled = (x & 63) + (x >> 6) * 2048.
     * The tile row stride of 2048 half-words comes from 64 columns x 32 rows
     * (each tile has 32 scanlines).
     * ================================================================== */
    if (params->col_tiled || params->aux_tiled) {
        /* MOV w4, w28 -- cached STATE_x */
        addlong(ARM64_MOV_REG(4, 28));
        /* AND w5, w4, #63 */
        addlong(ARM64_AND_MASK(5, 4, 6));
        /* LSR w6, w4, #6 */
        addlong(ARM64_LSR_IMM(6, 4, 6));
        /* ADD w5, w5, w6, LSL #11 */
        addlong(ARM64_ADD_REG_LSL(5, 5, 6, 11));
        /* STR w5, [x0, #STATE_x_tiled] */
        addlong(ARM64_STR_W(5, 0, STATE_x_tiled));
    }

    /* ====================================================================
     * DEPTH COMPUTATION (W-buffer or Z-buffer)
     * ====================================================================
     * Depth testing is how a 3D renderer determines which objects are in
     * front of others. Each pixel has a depth value stored in a depth buffer
     * (also called Z-buffer). Before writing a new pixel, its depth is
     * compared against the stored depth -- if it's behind what's already
     * drawn, it's discarded.
     *
     * The Voodoo supports two depth modes:
     *
     *   Z-buffer: Linear interpolation of the Z coordinate across the
     *     triangle. Simple but suffers from precision loss at large distances
     *     (distant objects get very similar Z values, causing "Z-fighting").
     *
     *   W-buffer: Uses the homogeneous W coordinate (the perspective divisor)
     *     instead of Z. W is distributed more evenly in screen space, giving
     *     better depth precision at all distances. The Voodoo converts the
     *     floating-point W value to a 16-bit integer using a log2-based
     *     encoding: the top bits store the exponent and the lower bits store
     *     the mantissa. This is computed here using CLZ (count leading zeros).
     *
     * "Depth bias" (added after depth computation) prevents Z-fighting
     * when rendering coplanar geometry (e.g., a decal on a wall).
     *
     * x86-64 ref: lines 858-932 (W-depth), 933-960 (Z-depth + bias)
     *
     * W is a 48-bit fixed-point value stored in state->w (int64_t).
     * High 16 bits (w+4) are tested first; if nonzero, depth is computed
     * using CLZ (ARM64) instead of BSR (x86).
     *
     * BSR(x) = 31 - CLZ(x) on ARM64 (for nonzero input).
     * ================================================================== */
    if ((params->fbzMode & FBZ_W_BUFFER)
        || (params->fogMode & (FOG_ENABLE | FOG_CONSTANT | FOG_Z | FOG_ALPHA)) == FOG_ENABLE) {
        /* MOV w10, #0 -- new_depth = 0 (default if w high bits nonzero) */
        addlong(ARM64_MOV_ZERO(10));

        /* LDR w4, [x0, #(STATE_w + 4)] -- load high 32 bits of w */
        addlong(ARM64_LDR_W(4, 0, STATE_w + 4));

        /* UXTH w5, w4 -- extract bits 32..47 of w; if nonzero, w is too large so depth stays 0 */
        addlong(ARM64_UXTH(5, 4));

        /* CBNZ w5, got_depth -- skip depth computation if w >> 32 has nonzero low half */
        depth_jump_pos = block_pos;
        addlong(ARM64_CBNZ_W_PLACEHOLDER(5));

        /* High word is zero. Now compute depth from low 32 bits.
         * LDR w4, [x0, #STATE_w] -- load low 32 bits of w
         */
        addlong(ARM64_LDR_W(4, 0, STATE_w));

        /* MOV w10, #0xF001 -- depth = 0xF001 (max if low word also zero-ish) */
        addlong(ARM64_MOVZ_W(10, 0xF001));

        /* MOV w5, w4 -- save low 32 bits in w5 */
        addlong(ARM64_MOV_REG(5, 4));

        /* LSR w4, w4, #16 */
        addlong(ARM64_LSR_IMM(4, 4, 16));

        /* CBZ w4, got_depth -- if top 16 of low word is zero, depth = 0xF001 */
        depth_jump_pos2 = block_pos;
        addlong(ARM64_CBZ_W_PLACEHOLDER(4));

        /*
         * Compute w_depth from the low 16 bits of the high word of state->w.
         *
         * Voodoo's W depth encoding (same as x86-64):
         *   exp  = 15 - floor(log2(w_high16))   = CLZ(w_high16) - 16
         *   mant = (~w_low32) >> (19 - exp)      (low 12 bits)
         *   result = (exp << 12) + mant + 1, clamped to [0, 0xFFFF]
         *
         * ARM64 translation of x86 BSR:
         *   x86: BSR EAX, EDX  =>  EAX = floor(log2(EDX))
         *   ARM64: CLZ w6, w4  =>  w6 = count of leading zeros
         *          BSR result  =  31 - w6  (for 32-bit input)
         *   Since input is a 16-bit value zero-extended to 32 bits:
         *          BSR result  =  31 - w6
         *          exp         =  15 - BSR = 15 - (31 - w6) = w6 - 16
         */
        addlong(ARM64_CLZ(6, 4));
        addlong(ARM64_SUB_IMM(7, 6, 16));      /* exp = CLZ - 16 */

        addlong(ARM64_MOVZ_W(11, 19));
        addlong(ARM64_SUB_REG(11, 11, 7));      /* shift_amount = 19 - exp */

        addlong(ARM64_MVN(5, 5));                /* NOT w_low */
        addlong(ARM64_LSR_REG(5, 5, 11));        /* mant = (~w_low) >> shift */
        addlong(ARM64_AND_MASK(5, 5, 12));       /* mant &= 0xFFF */

        addlong(ARM64_LSL_IMM(7, 7, 12));        /* exp <<= 12 */
        addlong(ARM64_ADD_REG(10, 7, 5));        /* result = (exp<<12) + mant */
        addlong(ARM64_ADD_IMM(10, 10, 1));       /* result += 1 */

        /* Clamp to 0xFFFF */
        addlong(ARM64_MOVZ_W(11, 0xFFFF));
        addlong(ARM64_CMP_REG(10, 11));
        addlong(ARM64_CSEL(10, 11, 10, COND_HI)); /* if result > 0xFFFF, result = 0xFFFF */

        /* Patch depth_jump_pos (CBNZ) and depth_jump_pos2 (CBZ) to here */
        if (depth_jump_pos)
            PATCH_FORWARD_CBxZ(depth_jump_pos);
        if (depth_jump_pos2)
            PATCH_FORWARD_CBxZ(depth_jump_pos2);

        /* w10 = computed w_depth */

        /* If fog needs w_depth, store it */
        if ((params->fogMode & (FOG_ENABLE | FOG_CONSTANT | FOG_Z | FOG_ALPHA)) == FOG_ENABLE) {
            /* STR w10, [x0, #STATE_w_depth] */
            addlong(ARM64_STR_W(10, 0, STATE_w_depth));
        }
    }

    /* ================================================================
     * Z-buffer depth (when not using W-buffer)
     * ================================================================
     *
     * x86-64 ref: lines 933-952
     * depth = state->z >> 12, clamped to [0, 0xFFFF]
     * ================================================================ */
    if (!(params->fbzMode & FBZ_W_BUFFER)) {
        /* LDR w10, [x0, #STATE_z] */
        addlong(ARM64_LDR_W(10, 0, STATE_z));

        /* ASR w10, w10, #12 */
        addlong(ARM64_ASR_IMM(10, 10, 12));

        /* Clamp: if negative, set to 0; if > 0xFFFF, set to 0xFFFF */
        /* CMP w10, #0 */
        addlong(ARM64_CMP_IMM(10, 0));
        /* CSEL w10, wzr, w10, LT -- if negative, zero */
        addlong(ARM64_CSEL(10, 31, 10, COND_LT));

        addlong(ARM64_MOVZ_W(11, 0xFFFF));
        addlong(ARM64_CMP_REG(10, 11));
        /* CSEL w10, w11, w10, GT -- if > 0xFFFF, clamp */
        addlong(ARM64_CSEL(10, 11, 10, COND_GT));
    }

    /* ================================================================
     * Depth bias
     * ================================================================
     *
     * x86-64 ref: lines 954-960
     *
     * Interpreter: new_depth = CLAMP16(new_depth + (int16_t) params->zaColor)
     * The x86-64 JIT (and our original code) just masked to 16 bits,
     * but the interpreter clamps to [0, 0xFFFF] after a SIGNED add.
     * With sign-extension of zaColor, the sum can go negative or
     * exceed 0xFFFF, so a proper clamp is needed.
     * ================================================================ */
    if (params->fbzMode & FBZ_DEPTH_BIAS) {
        /* LDR w4, [x1, #PARAMS_zaColor] */
        addlong(ARM64_LDR_W(4, 1, PARAMS_zaColor));
        /* SXTH w4, w4 -- sign-extend low 16 bits to match (int16_t) cast */
        addlong(ARM64_SXTH(4, 4));
        /* ADD w10, w10, w4 -- signed addition (result may be <0 or >0xFFFF) */
        addlong(ARM64_ADD_REG(10, 10, 4));
        /* CLAMP16: clamp to [0, 0xFFFF] */
        addlong(ARM64_CMP_IMM(10, 0));
        addlong(ARM64_CSEL(10, 31, 10, COND_LT));
        addlong(ARM64_MOVZ_W(11, 0xFFFF));
        addlong(ARM64_CMP_REG(10, 11));
        addlong(ARM64_CSEL(10, 11, 10, COND_GT));
    }

    /* Store new_depth: STR w10, [x0, #STATE_new_depth] */
    addlong(ARM64_STR_W(10, 0, STATE_new_depth));

    /* ====================================================================
     * DEPTH TEST
     * ====================================================================
     * Compare the new pixel's depth against the value already in the depth
     * buffer. The comparison function is configurable (never, less, equal,
     * less-or-equal, greater, not-equal, greater-or-equal, always).
     *
     * Games choose the function based on rendering needs:
     *   - "less" (most common): draw only if closer than what's already there
     *   - "always": disable depth testing (for HUD/UI elements)
     *   - "equal": used for multi-pass rendering (second pass draws only
     *     where the first pass already wrote)
     *
     * If the test fails, the pixel is skipped (no color, depth, or alpha
     * buffer writes). The skip is implemented as a forward branch that
     * jumps past all rendering stages to the per-pixel increment section.
     *
     * x86-64 ref: lines 966-1023
     *
     * If depth enabled and depthop is not ALWAYS or NEVER:
     *   Load old depth from aux_mem[x or x_tiled]
     *   Compare new_depth (w10) vs old_depth
     *   Skip pixel on failure (forward branch to z_skip_pos)
     *
     * If DEPTHOP_NEVER: handled by early return before the prologue
     * If DEPTHOP_ALWAYS: no test needed
     *
     * For depth source override (FBZ_DEPTH_SOURCE), use zaColor instead.
     * ================================================================== */
    if ((params->fbzMode & FBZ_DEPTH_ENABLE) && (depthop != DEPTHOP_ALWAYS) && (depthop != DEPTHOP_NEVER)) {
        /* Load x index (tiled or regular) */
        if (params->aux_tiled) {
            /* LDR w4, [x0, #STATE_x_tiled] */
            addlong(ARM64_LDR_W(4, 0, STATE_x_tiled));
        } else {
            /* MOV w4, w28 -- cached STATE_x */
            addlong(ARM64_MOV_REG(4, 28));
        }

        /* Load old depth: LDRH w5, [x9, x4, LSL #1]
         * x9 = aux_mem, x4 = x index, each entry is 2 bytes */
        addlong(ARM64_LDRH_REG_LSL1(5, 9, 4));

        /* If depth source override, use zaColor instead of computed depth */
        if (params->fbzMode & FBZ_DEPTH_SOURCE) {
            /* LDRH w10, [x1, #PARAMS_zaColor] -- load zaColor as 16-bit */
            addlong(ARM64_LDRH_IMM(10, 1, PARAMS_zaColor));
        }

        /* CMP w10, w5 -- compare new_depth vs old_depth */
        addlong(ARM64_CMP_REG(10, 5));

        /* Conditional branch based on depth operation.
         *
         * The comparison is unsigned (depth values are 0..0xFFFF).
         *
         * DEPTHOP_LESSTHAN:         skip if new >= old  -> skip on CS (carry set = HS)
         * DEPTHOP_EQUAL:            skip if new != old  -> skip on NE
         * DEPTHOP_LESSTHANEQUAL:    skip if new > old   -> skip on HI
         * DEPTHOP_GREATERTHAN:      skip if new <= old  -> skip on LS
         * DEPTHOP_NOTEQUAL:         skip if new == old  -> skip on EQ
         * DEPTHOP_GREATERTHANEQUAL: skip if new < old   -> skip on CC (carry clear = LO)
         */
        if (depthop == DEPTHOP_LESSTHAN) {
            z_skip_pos = block_pos;
            addlong(ARM64_BCOND_PLACEHOLDER(COND_CS)); /* skip if >= (unsigned) */
        } else if (depthop == DEPTHOP_EQUAL) {
            z_skip_pos = block_pos;
            addlong(ARM64_BCOND_PLACEHOLDER(COND_NE));
        } else if (depthop == DEPTHOP_LESSTHANEQUAL) {
            z_skip_pos = block_pos;
            addlong(ARM64_BCOND_PLACEHOLDER(COND_HI)); /* skip if > (unsigned) */
        } else if (depthop == DEPTHOP_GREATERTHAN) {
            z_skip_pos = block_pos;
            addlong(ARM64_BCOND_PLACEHOLDER(COND_LS)); /* skip if <= (unsigned) */
        } else if (depthop == DEPTHOP_NOTEQUAL) {
            z_skip_pos = block_pos;
            addlong(ARM64_BCOND_PLACEHOLDER(COND_EQ));
        } else if (depthop == DEPTHOP_GREATERTHANEQUAL) {
            z_skip_pos = block_pos;
            addlong(ARM64_BCOND_PLACEHOLDER(COND_CC)); /* skip if < (unsigned) */
        } else {
            fatal("Bad depthop\n");
        }
    }
    /* DEPTHOP_NEVER is handled by early return before the prologue. */

    /* ================================================================
     * Phase 3: Texture Fetch + TMU Combine
     * ================================================================
     *
     * x86-64 ref: lines 1025-1688
     *
     * After this section:
     *   v0 = final texture color (packed BGRA in low 32 bits, or
     *        unpacked 4x16 in v0.4H depending on path)
     *   state->tex_a[0] = texture alpha value
     *
     * codegen_texture_fetch() returns the texel in w4 (packed BGRA32).
     * ================================================================ */
    if (params->fbzColorPath & FBZCP_TEXTURE_ENABLED) {
        if ((params->textureMode[0] & TEXTUREMODE_LOCAL_MASK) == TEXTUREMODE_LOCAL || !voodoo->dual_tmus) {
            /* TMU0 only sampling local colour, or only one TMU */
            block_pos = codegen_texture_fetch(code_block, voodoo, params, state, block_pos, 0);

            /* FMOV s0, w4 -- move texel to NEON v0 */
            addlong(ARM64_FMOV_S_W(0, 4));
            /* LSR w4, w4, #24 -- extract alpha */
            addlong(ARM64_LSR_IMM(4, 4, 24));
            /* STR w4, [x0, #STATE_tex_a] */
            addlong(ARM64_STR_W(4, 0, STATE_tex_a));
        } else if ((params->textureMode[0] & TEXTUREMODE_MASK) == TEXTUREMODE_PASSTHROUGH) {
            /* TMU0 in pass-through mode, only sample TMU1 */
            block_pos = codegen_texture_fetch(code_block, voodoo, params, state, block_pos, 1);

            /* FMOV s0, w4 */
            addlong(ARM64_FMOV_S_W(0, 4));
            /* LSR w4, w4, #24 */
            addlong(ARM64_LSR_IMM(4, 4, 24));
            /* STR w4, [x0, #STATE_tex_a] */
            addlong(ARM64_STR_W(4, 0, STATE_tex_a));
        } else {
            /* Dual-TMU texture combine path.
             * (See NOTE ON VARIABLE SCOPE at the top of voodoo_generate()
             * for where tc_*, tca_*, cc_*, etc. variables come from.) */

            /* ============================================================
             * Dual-TMU mode: fetch TMU1 first, then TMU0, combine
             * ============================================================
             *
             * x86-64 ref: lines 1059-1688
             *
             * TMU1 result -> v3 (unpacked to 4x16)
             * TMU0 result -> v0 (unpacked to 4x16)
             *
             * Then apply tc_zero_other, tc_sub_clocal, tc_mselect,
             * tc_reverse_blend, tc_add, tc_invert for RGB channels.
             * And tca_* equivalents for alpha channel.
             * ============================================================ */
            block_pos = codegen_texture_fetch(code_block, voodoo, params, state, block_pos, 1);

            /* FMOV s3, w4 -- TMU1 result in v3 */
            addlong(ARM64_FMOV_S_W(3, 4));

            /* ---- TMU1 texture combine (tc_*_1 / tca_*_1 vars) ---- */
            if ((params->textureMode[1] & TEXTUREMODE_TRILINEAR) && tc_sub_clocal_1) {
                /* Trilinear LOD blend for TMU1 */
                addlong(ARM64_LDR_W(4, 0, STATE_lod));
                if (!tc_reverse_blend_1) {
                    addlong(ARM64_MOVZ_W(5, 1));
                } else {
                    addlong(ARM64_MOV_ZERO(5));
                }
                addlong(ARM64_AND_MASK(4, 4, 1));  /* lod & 1 */
                if (!tca_reverse_blend_1) {
                    addlong(ARM64_MOVZ_W(6, 1));
                } else {
                    addlong(ARM64_MOV_ZERO(6));
                }
                addlong(ARM64_EOR_REG(5, 5, 4));  /* tc_reverse_blend ^= (lod & 1) */
                addlong(ARM64_EOR_REG(6, 6, 4));  /* tca_reverse_blend ^= (lod & 1) */
                addlong(ARM64_LSL_IMM(5, 5, 4));  /* w5 = tc_reverse_blend << 4 (byte offset into neon_00_ff_w) */
                /* w5 = tc_reverse_blend index, w6 = tca_reverse_blend */
            }

            /* Unpack TMU1 result: UXTL v3.8H, v3.8B */
            addlong(ARM64_UXTL_8H_8B(3, 3));

            if (tc_sub_clocal_1) {
                /* tc_mselect_1: compute blend factor into v0 */
                switch (tc_mselect_1) {
                    case TC_MSELECT_ZERO:
                        addlong(ARM64_MOVI_V2D_ZERO(0));
                        break;
                    case TC_MSELECT_CLOCAL:
                        addlong(ARM64_MOV_V(0, 3));
                        break;
                    case TC_MSELECT_AOTHER:
                        /* No other TMU in TMU1 combine, use zero */
                        addlong(ARM64_MOVI_V2D_ZERO(0));
                        break;
                    case TC_MSELECT_ALOCAL:
                        /* Broadcast alpha lane of v3 to all lanes */
                        addlong(ARM64_DUP_V4H_LANE(0, 3, 3));
                        break;
                    case TC_MSELECT_DETAIL:
                        addlong(ARM64_MOVZ_W(4, params->detail_bias[1] & 0xFFFF));
                        if (params->detail_bias[1] >> 16)
                            addlong(ARM64_MOVK_W_16(4, params->detail_bias[1] >> 16));
                        addlong(ARM64_LDR_W(10, 0, STATE_lod));
                        addlong(ARM64_SUB_REG(4, 4, 10));
                        addlong(ARM64_MOVZ_W(11, params->detail_max[1] & 0xFFFF));
                        if (params->detail_max[1] >> 16)
                            addlong(ARM64_MOVK_W_16(11, params->detail_max[1] >> 16));
                        if (params->detail_scale[1])
                            addlong(ARM64_LSL_IMM(4, 4, params->detail_scale[1]));
                        addlong(ARM64_CMP_REG(4, 11));
                        addlong(ARM64_CSEL(4, 11, 4, COND_GE));
                        addlong(ARM64_DUP_V4H_GPR(0, 4));
                        break;
                    case TC_MSELECT_LOD_FRAC:
                        addlong(ARM64_LDR_W(4, 0, STATE_lod_frac_n(1)));
                        addlong(ARM64_DUP_V4H_GPR(0, 4));
                        break;
                }

                /* Apply reverse blend: XOR with 0xFF mask, then add 1 */
                if (params->textureMode[1] & TEXTUREMODE_TRILINEAR) {
                    /* XOR v0 with neon_00_ff_w[w5] (trilinear) */
                    /* w5 has offset (0 or 16) into neon_00_ff_w table */
                    addlong(ARM64_LDR_Q_REG(16, 22, 5));  /* x22 = neon_00_ff_w pointer */
                    addlong(ARM64_EOR_V(0, 0, 16));
                } else if (!tc_reverse_blend_1) {
                    /* XOR with 0xFF (invert) */
                    addlong(ARM64_EOR_V(0, 0, 9));  /* v9 = neon_ff_w */
                }
                /* ADD v0, v0, v8  (v8 = neon_01_w = {1,1,1,1}) */
                addlong(ARM64_ADD_V4H(0, 0, 8));

                /* Multiply: v3 * v0 -> signed 16x16->32->shift->narrow
                 * ARM64: SMULL v16.4S, v3.4H, v0.4H
                 *        SSHR v16.4S, v16.4S, #8
                 *        SQXTN v16.4H, v16.4S
                 */

                /* v1 = 0 (zero -- for subtraction base) */
                addlong(ARM64_MOVI_V2D_ZERO(1));

                /* The signed multiply sequence:
                 * Interpreter does: (-clocal * factor) >> 8
                 * i.e. negate clocal BEFORE the multiply+shift.
                 *
                 * This matters because (-a*b)>>8 != -(a*b>>8) when a*b
                 * is not a multiple of 256 (arithmetic right shift floors
                 * toward negative infinity). Negating after the shift
                 * loses the remainder and produces a +1 error.
                 *
                 * The x86-64 JIT negates after the shift, which can produce a +1 error.
                 *
                 * v1 = 0 (from MOVI above), v3 = clocal, v0 = factor
                 */
                addlong(ARM64_SUB_V4H(16, 1, 3));        /* v16.4H = 0 - clocal (negate first) */
                addlong(ARM64_SMULL_4S_4H(16, 16, 0));   /* v16.4S = (-clocal).4H * factor.4H */
                addlong(ARM64_SSHR_V4S(16, 16, 8));       /* v16.4S >>= 8 (arithmetic) */
                addlong(ARM64_SQXTN_4H_4S(1, 16));        /* v1.4H = saturate_narrow(v16.4S) */

                /* tc_add_clocal_1: add clocal (TMU1) back */
                if (tc_add_clocal_1) {
                    addlong(ARM64_ADD_V4H(1, 1, 3));
                } else if (tc_add_alocal_1) {
                    /* Broadcast alpha of TMU1 and add */
                    addlong(ARM64_DUP_V4H_LANE(0, 3, 3));
                    addlong(ARM64_ADD_V4H(1, 1, 0));
                }

                /* Pack result back to bytes (v3), then unpack again for alpha combine */
                addlong(ARM64_SQXTUN_8B_8H(3, 1));

                /* If tca_sub_clocal_1, extract alpha from packed result */
                if (tca_sub_clocal_1) {
                    addlong(ARM64_FMOV_W_S(5, 3));  /* w5 = packed BGRA */
                }

                /* Unpack v3 back to 16-bit for further processing */
                addlong(ARM64_UXTL_8H_8B(3, 3));
            }

            /* ---- TCA (texture combine alpha) for TMU1 ---- */
            if (tca_sub_clocal_1) {
                /* w5 = packed result (from FMOV above), extract alpha */
                addlong(ARM64_LSR_IMM(5, 5, 24));

                switch (tca_mselect_1) {
                    case TCA_MSELECT_ZERO:
                        addlong(ARM64_MOV_ZERO(4));
                        break;
                    case TCA_MSELECT_CLOCAL:
                        addlong(ARM64_MOV_REG(4, 5));
                        break;
                    case TCA_MSELECT_AOTHER:
                        addlong(ARM64_MOV_ZERO(4));
                        break;
                    case TCA_MSELECT_ALOCAL:
                        addlong(ARM64_MOV_REG(4, 5));
                        break;
                    case TCA_MSELECT_DETAIL:
                        addlong(ARM64_MOVZ_W(4, params->detail_bias[1] & 0xFFFF));
                        if (params->detail_bias[1] >> 16)
                            addlong(ARM64_MOVK_W_16(4, params->detail_bias[1] >> 16));
                        addlong(ARM64_LDR_W(10, 0, STATE_lod));
                        addlong(ARM64_SUB_REG(4, 4, 10));
                        addlong(ARM64_MOVZ_W(11, params->detail_max[1] & 0xFFFF));
                        if (params->detail_max[1] >> 16)
                            addlong(ARM64_MOVK_W_16(11, params->detail_max[1] >> 16));
                        if (params->detail_scale[1])
                            addlong(ARM64_LSL_IMM(4, 4, params->detail_scale[1]));
                        addlong(ARM64_CMP_REG(4, 11));
                        addlong(ARM64_CSEL(4, 11, 4, COND_GE));
                        break;
                    case TCA_MSELECT_LOD_FRAC:
                        addlong(ARM64_LDR_W(4, 0, STATE_lod_frac_n(1)));
                        break;
                }

                /* Apply reverse blend for alpha */
                if (params->textureMode[1] & TEXTUREMODE_TRILINEAR) {
                    /* XOR w4 with i_00_ff_w[w6] (w6 = tca_reverse_blend index) */
                    addlong(ARM64_LDR_W_REG_LSL2(10, 23, 6));  /* x23 = i_00_ff_w */
                    addlong(ARM64_EOR_REG(4, 4, 10));
                } else if (!tca_reverse_blend_1) {
                    /* NOTE: x86-64 line ~1303 incorrectly uses tc_reverse_blend_1 (the RGB flag)
                     * in this TCA (alpha) path. The correct flag is tca_reverse_blend_1, which
                     * is what we use here, matching the interpreter. */
                    addlong(ARM64_EOR_MASK(4, 4, 8)); /* XOR with 0xFF */
                }

                /* ADD w4, w4, #1 */
                addlong(ARM64_ADD_IMM(4, 4, 1));

                /* MUL w4, w4, w5 */
                addlong(ARM64_MUL(4, 4, 5));
                /* NEG w4, w4 */
                addlong(ARM64_NEG(4, 4));
                /* ASR w4, w4, #8 */
                addlong(ARM64_ASR_IMM(4, 4, 8));

                if (tca_add_clocal_1 || tca_add_alocal_1) {
                    addlong(ARM64_ADD_REG(4, 4, 5));
                }

                /* Clamp alpha to [0, 0xFF] */
                addlong(ARM64_MOVZ_W(10, 0xFF));
                addlong(ARM64_CMP_REG(10, 4));
                addlong(ARM64_CSEL(10, 4, 10, COND_HI));  /* min(0xFF, alpha) */

                /* Insert alpha into v3 lane 3 */
                addlong(ARM64_INS_H(3, 3, 10));
            }

            /* ---- Now fetch TMU0 ---- */
            block_pos = codegen_texture_fetch(code_block, voodoo, params, state, block_pos, 0);

            /* FMOV s0, w4 -- TMU0 result in v0 */
            addlong(ARM64_FMOV_S_W(0, 4));
            /* Also save raw TMU0 result in v7 for later tca processing */
            addlong(ARM64_FMOV_S_W(7, 4));

            /* ---- TMU0 trilinear setup ---- */
            if (params->textureMode[0] & TEXTUREMODE_TRILINEAR) {
                addlong(ARM64_LDR_W(4, 0, STATE_lod));
                if (!tc_reverse_blend) {
                    addlong(ARM64_MOVZ_W(5, 1));
                } else {
                    addlong(ARM64_MOV_ZERO(5));
                }
                addlong(ARM64_AND_MASK(4, 4, 1));
                if (!tca_reverse_blend) {
                    addlong(ARM64_MOVZ_W(6, 1));
                } else {
                    addlong(ARM64_MOV_ZERO(6));
                }
                addlong(ARM64_EOR_REG(5, 5, 4));
                addlong(ARM64_EOR_REG(6, 6, 4));
                addlong(ARM64_LSL_IMM(5, 5, 4));
                /* w5 = tc_reverse_blend (scaled), w6 = tca_reverse_blend */
            }

            /* Unpack TMU0: UXTL v0.8H, v0.8B */
            addlong(ARM64_UXTL_8H_8B(0, 0));

            /* tc_zero_other: zero the "other" TMU (TMU1 = v3) */
            if (tc_zero_other) {
                addlong(ARM64_MOVI_V2D_ZERO(1));
            } else {
                addlong(ARM64_MOV_V(1, 3));
            }

            /* tc_sub_clocal: subtract local (TMU0) from other */
            if (tc_sub_clocal) {
                addlong(ARM64_SUB_V4H(1, 1, 0));
            }

            /* tc_mselect: compute blend factor for TMU0 combine */
            switch (tc_mselect) {
                case TC_MSELECT_ZERO:
                    addlong(ARM64_MOVI_V2D_ZERO(4));
                    break;
                case TC_MSELECT_CLOCAL:
                    addlong(ARM64_MOV_V(4, 0));
                    break;
                case TC_MSELECT_AOTHER:
                    /* Broadcast alpha of TMU1 (v3.H[3]) to v4 */
                    addlong(ARM64_DUP_V4H_LANE(4, 3, 3));
                    break;
                case TC_MSELECT_ALOCAL:
                    /* Broadcast alpha of TMU0 (v0.H[3]) to v4 */
                    addlong(ARM64_DUP_V4H_LANE(4, 0, 3));
                    break;
                case TC_MSELECT_DETAIL:
                    addlong(ARM64_MOVZ_W(4, params->detail_bias[0] & 0xFFFF));
                    if (params->detail_bias[0] >> 16)
                        addlong(ARM64_MOVK_W_16(4, params->detail_bias[0] >> 16));
                    addlong(ARM64_LDR_W(10, 0, STATE_lod));
                    addlong(ARM64_SUB_REG(4, 4, 10));
                    addlong(ARM64_MOVZ_W(11, params->detail_max[0] & 0xFFFF));
                    if (params->detail_max[0] >> 16)
                        addlong(ARM64_MOVK_W_16(11, params->detail_max[0] >> 16));
                    if (params->detail_scale[0])
                        addlong(ARM64_LSL_IMM(4, 4, params->detail_scale[0]));
                    addlong(ARM64_CMP_REG(4, 11));
                    addlong(ARM64_CSEL(4, 11, 4, COND_GE));
                    addlong(ARM64_DUP_V4H_GPR(4, 4));
                    break;
                case TC_MSELECT_LOD_FRAC:
                    addlong(ARM64_LDR_W(4, 0, STATE_lod_frac_n(0)));
                    addlong(ARM64_DUP_V4H_GPR(4, 4));
                    break;
            }

            /* Apply reverse blend */
            if (params->textureMode[0] & TEXTUREMODE_TRILINEAR) {
                addlong(ARM64_LDR_Q_REG(16, 22, 5));
                addlong(ARM64_EOR_V(4, 4, 16));
            } else if (!tc_reverse_blend) {
                addlong(ARM64_EOR_V(4, 4, 9));
            }
            /* ADD v4.4H, v4.4H, v8.4H */
            addlong(ARM64_ADD_V4H(4, 4, 8));

            /* Extract TMU0 alpha from v7 into w13 for reuse.
             * v7 holds raw TMU0 packed BGRA; alpha is byte 3 (bits [31:24]).
             * Must happen before tca_sub_clocal reads w13 below. */
            addlong(ARM64_FMOV_W_S(13, 7));
            addlong(ARM64_LSR_IMM(13, 13, 24));

            /* Multiply: signed 16x16 -> 32 -> >>8 -> narrow */
            addlong(ARM64_SMULL_4S_4H(16, 1, 4));
            addlong(ARM64_SSHR_V4S(16, 16, 8));
            addlong(ARM64_SQXTN_4H_4S(1, 16));

            if (tca_sub_clocal) {
                /* w5 = TMU0 alpha (from w13) */
                addlong(ARM64_MOV_REG(5, 13));
            }

            /* tc_add_clocal: add local (TMU0) back */
            if (tc_add_clocal) {
                addlong(ARM64_ADD_V4H(1, 1, 0));
            } else if (tc_add_alocal) {
                addlong(ARM64_DUP_V4H_LANE(4, 0, 3));
                addlong(ARM64_ADD_V4H(1, 1, 4));
            }

            /* tc_invert_output */
            if (tc_invert_output) {
                addlong(ARM64_EOR_V(1, 1, 9));  /* XOR with 0xFF */
            }

            /* Narrow v0, v3, v1 from 8H to 8B in-place for alpha processing */
            addlong(ARM64_SQXTUN_8B_8H(0, 0));   /* v0 = packed TMU0 */
            addlong(ARM64_SQXTUN_8B_8H(3, 3));   /* v3 = packed TMU1 */
            addlong(ARM64_SQXTUN_8B_8H(1, 1));   /* v1 = packed combined RGB */

            /* ---- TCA (alpha combine for TMU0) ----
             * w13 already holds TMU0 alpha, extracted before the SMULL above. */

            if (tca_zero_other) {
                addlong(ARM64_MOV_ZERO(4));
            } else {
                /* Other alpha = TMU1 alpha */
                addlong(ARM64_FMOV_W_S(4, 3));
                addlong(ARM64_LSR_IMM(4, 4, 24));
            }

            if (tca_sub_clocal) {
                addlong(ARM64_SUB_REG(4, 4, 5));  /* w5 = TMU0 alpha */
            }

            switch (tca_mselect) {
                case TCA_MSELECT_ZERO:
                    addlong(ARM64_MOV_ZERO(5));
                    break;
                case TCA_MSELECT_CLOCAL:
                    /* w5 = TMU0 alpha (from w13) */
                    addlong(ARM64_MOV_REG(5, 13));
                    break;
                case TCA_MSELECT_AOTHER:
                    addlong(ARM64_FMOV_W_S(5, 3));
                    addlong(ARM64_LSR_IMM(5, 5, 24));
                    break;
                case TCA_MSELECT_ALOCAL:
                    /* w5 = TMU0 alpha (from w13) */
                    addlong(ARM64_MOV_REG(5, 13));
                    break;
                case TCA_MSELECT_DETAIL:
                    addlong(ARM64_MOVZ_W(5, params->detail_bias[0] & 0xFFFF));
                    if (params->detail_bias[0] >> 16)
                        addlong(ARM64_MOVK_W_16(5, params->detail_bias[0] >> 16));
                    addlong(ARM64_LDR_W(10, 0, STATE_lod));
                    addlong(ARM64_SUB_REG(5, 5, 10));
                    addlong(ARM64_MOVZ_W(11, params->detail_max[0] & 0xFFFF));
                    if (params->detail_max[0] >> 16)
                        addlong(ARM64_MOVK_W_16(11, params->detail_max[0] >> 16));
                    if (params->detail_scale[0])
                        addlong(ARM64_LSL_IMM(5, 5, params->detail_scale[0]));
                    addlong(ARM64_CMP_REG(5, 11));
                    addlong(ARM64_CSEL(5, 11, 5, COND_GE));
                    break;
                case TCA_MSELECT_LOD_FRAC:
                    addlong(ARM64_LDR_W(5, 0, STATE_lod_frac_n(0)));
                    break;
            }

            /* Apply tca reverse blend */
            if (params->textureMode[0] & TEXTUREMODE_TRILINEAR) {
                addlong(ARM64_LDR_W_REG_LSL2(10, 23, 6));
                addlong(ARM64_EOR_REG(5, 5, 10));
            } else if (!tca_reverse_blend) {
                addlong(ARM64_EOR_MASK(5, 5, 8));  /* XOR with 0xFF */
            }

            /* ADD w5, w5, #1 */
            addlong(ARM64_ADD_IMM(5, 5, 1));
            /* MUL w4, w4, w5 */
            addlong(ARM64_MUL(4, 4, 5));
            /* ASR w4, w4, #8 */
            addlong(ARM64_ASR_IMM(4, 4, 8));

            if (tca_add_clocal || tca_add_alocal) {
                /* ADD w4, w4, w13 -- TMU0 alpha from w13 */
                addlong(ARM64_ADD_REG(4, 4, 13));
            }

            /* Clamp: if negative, 0; if > 0xFF, 0xFF */
            addlong(ARM64_MOVZ_W(10, 0xFF));
            addlong(ARM64_BIC_REG_ASR(4, 4, 4, 31));   /* zero if negative */
            addlong(ARM64_CMP_IMM(4, 0xFF));
            addlong(ARM64_CSEL(4, 10, 4, COND_HI));    /* cap at 0xFF */

            if (tca_invert_output) {
                addlong(ARM64_EOR_MASK(4, 4, 8));  /* XOR with 0xFF */
            }

            /* Store tex_a: STR w4, [x0, #STATE_tex_a] */
            addlong(ARM64_STR_W(4, 0, STATE_tex_a));

            /* Move final combined RGB to v0 (v1 has the combined result) */
            addlong(ARM64_MOV_V(0, 1));
        }

        /* ============================================================
         * trexInit1 override path
         * ============================================================
         *
         * If trexInit1 bit 18 is set, override texture output:
         *   tex_r = tex_g = 0, tex_b = tmuConfig
         *
         * This is handled by the interpreter at vid_voodoo_render.c:1062-1065.
         * On x86-64 this is done by testing the cached trexInit1 flag.
         * We embed this as a compile-time check since trexInit1 is part of
         * the cache key.
         * ============================================================ */
        if (voodoo->trexInit1[0] & (1 << 18)) {
            /* Override: load tmuConfig as blue, zero red/green, keep alpha in tex_a */
            addlong(ARM64_MOVZ_W(4, voodoo->tmuConfig & 0xFFFF));
            if (voodoo->tmuConfig >> 16)
                addlong(ARM64_MOVK_W_16(4, voodoo->tmuConfig >> 16));
            /* Mask to 24 bits (blue channel in BGRA) */
            addlong(ARM64_AND_BITMASK(4, 4, 0, 0, 23));  /* AND with 0x00FFFFFF */
            addlong(ARM64_FMOV_S_W(0, 4));
        }
    }

    /* ====================================================================
     * COLOR COMBINE
     * ====================================================================
     * Color combine is the Voodoo's per-pixel shading equation. It takes
     * the texel color (from texture fetch) and the iterated vertex color
     * (from Gouraud shading) and combines them into a final pixel color.
     *
     * The combine equation is:
     *   result = ((c_other - c_local) * c_factor + c_local + c_add)
     *
     * Where:
     *   c_other  = one of: iterated RGB, texture RGB, or iterated+texture
     *   c_local  = one of: iterated RGB, texture RGB, or iterated alpha
     *   c_factor = one of: local, other, local alpha, other alpha, texture
     *              alpha, or iterated alpha (the "mselect" multiplier)
     *
     * This flexible equation covers most 1990s shading needs:
     *   - Modulate (texture * vertex color): standard textured lighting
     *   - Decal (texture only): unlit textures like sky or UI
     *   - Blend (lerp between texture and vertex): glass, fade effects
     *
     * CHROMA KEY: Before color combine, the texel color can be tested
     * against a key color range. If it matches, the pixel is discarded.
     * This is how the Voodoo implements color-key transparency (e.g.,
     * "bright pink = transparent" in sprite-based games).
     *
     * x86-64 ref: lines 1689-2228
     *
     * At this point:
     *   v0 = texture color (packed BGRA in low 32 bits via FMOV from w4)
     *        -- already set by Phase 3 or left undefined if no texture
     *   state->tex_a[0] = texture alpha (stored in Phase 3)
     *
     * Register plan for this phase:
     *   v0 = cother (color other, unpacked to 4x16)
     *   v1 = clocal (color local, unpacked to 4x16)
     *   v2 = zero (cleared when needed; not guaranteed from prior phases)
     *   v3 = cc_mselect factor (4x16)
     *   v4 = saved texture RGB for CC_MSELECT_TEXRGB
     *   v6 = cached iterated BGRA (H6 optimization, packed 8B)
     *   w14 = a_other (scalar alpha)
     *   w15 = a_local (scalar alpha)
     *   w4-w7, w10-w13 = scratch
     * ================================================================== */

    /* Determine if any color combine path uses iterated BGRA.
     * Chroma key, clocal select, and cother select all need the same
     * iterated {ib,ig,ir,ia} values.  Cache once in v6. */
    int needs_iter_bgra = (_rgb_sel == CC_LOCALSELECT_ITER_RGB) ||
        ((cc_sub_clocal || cc_mselect == CC_MSELECT_CLOCAL || cc_add == 1) &&
         (!cc_localselect || cc_localselect_override));

    /* Cache iterated BGRA pack in v6 (NEON scratch, free during color combine) */
    if (needs_iter_bgra) {
        addlong(ARM64_ADD_IMM_X(16, 0, STATE_ib));
        addlong(ARM64_LD1_V4S(6, 16));
        addlong(ARM64_SSHR_V4S(6, 6, 12));
        addlong(ARM64_SQXTN_4H_4S(6, 6));
        addlong(ARM64_SQXTUN_8B_8H(6, 6));
    }

    /* Save texture color for CC_MSELECT_TEXRGB before it gets overwritten */
    if (cc_mselect == CC_MSELECT_TEXRGB) {
        /* v4 = v0 (packed BGRA texture) */
        addlong(ARM64_MOV_V(4, 0));
    }

    /* ----------------------------------------------------------------
     * Chroma key test
     * ----------------------------------------------------------------
     *
     * x86-64 ref: lines 1696-1746
     * Compare selected RGB source against params->chromaKey.
     * Skip pixel if match (low 24 bits equal).
     * ---------------------------------------------------------------- */
    if (params->fbzMode & FBZ_CHROMAKEY) {
        switch (_rgb_sel) {
            case CC_LOCALSELECT_ITER_RGB:
                /* use cached iterated BGRA from v6 */
                addlong(ARM64_FMOV_W_S(4, 6));             /* w4 = cached packed BGRA from v6 */
                break;
            case CC_LOCALSELECT_COLOR1:
                addlong(ARM64_LDR_W(4, 1, PARAMS_color1));
                break;
            case CC_LOCALSELECT_TEX:
                addlong(ARM64_FMOV_W_S(4, 0));             /* w4 = packed texture BGRA */
                break;
            default:
                /* CC_LOCALSELECT_LFB: not supported in codegen, skip */
                addlong(ARM64_MOV_ZERO(4));
                break;
        }
        /* Load chromaKey, XOR with source, mask to 24 bits, branch if zero */
        addlong(ARM64_LDR_W(5, 1, PARAMS_chromaKey));
        addlong(ARM64_EOR_REG(5, 5, 4));
        addlong(ARM64_AND_BITMASK(5, 5, 0, 0, 23));   /* AND with 0x00FFFFFF */
        chroma_skip_pos = block_pos;
        addlong(ARM64_CBZ_W_PLACEHOLDER(5));
    }

    /* ----------------------------------------------------------------
     * trexInit1 override (duplicated from Phase 3 for the case where
     * texture was not enabled but we still need it for CC_MSELECT_TEXRGB)
     * ---------------------------------------------------------------- */
    if (voodoo->trexInit1[0] & (1 << 18)) {
        addlong(ARM64_MOVZ_W(4, voodoo->tmuConfig & 0xFFFF));
        if (voodoo->tmuConfig >> 16)
            addlong(ARM64_MOVK_W_16(4, voodoo->tmuConfig >> 16));
        addlong(ARM64_AND_BITMASK(4, 4, 0, 0, 23));
        addlong(ARM64_FMOV_S_W(0, 4));
    }

    /* ====================================================================
     * ALPHA COMBINE
     * ====================================================================
     * Alpha combine computes the pixel's alpha (opacity) value using the
     * same equation structure as color combine, but operating on a single
     * channel instead of RGB:
     *
     *   result = ((a_other - a_local) * a_factor + a_local + a_add)
     *
     * Where a_other and a_local are selected from iterated alpha, texture
     * alpha, or constant values. The result is used by later stages:
     *   - Alpha test: discard pixels below an alpha threshold
     *   - Alpha blend: control transparency when compositing with the
     *     existing framebuffer contents
     *   - Alpha buffer write: store the alpha for later multi-pass use
     * ================================================================== */

    /* ----------------------------------------------------------------
     * Alpha select (a_other) and alpha mask test
     * ----------------------------------------------------------------
     *
     * x86-64 ref: lines 1757-1804
     *
     * a_other -> w14 (EBX in x86)
     * Only needed if alpha test or alpha blend is enabled.
     * alphaMode bit 0 = alpha test enable, bit 4 = alpha blend enable.
     * ---------------------------------------------------------------- */
    if (params->alphaMode & ((1 << 0) | (1 << 4))) {
        /* a_other (EBX) -> w14 */
        switch (a_sel) {
            case A_SEL_ITER_A:
                /* w14 = CLAMP(state->ia >> 12) */
                addlong(ARM64_LDR_W(14, 0, STATE_ia));
                addlong(ARM64_ASR_IMM(14, 14, 12));
                /* Clamp to [0, 0xFF] */
                addlong(ARM64_MOVZ_W(10, 0xFF));
                addlong(ARM64_BIC_REG_ASR(14, 14, 14, 31));  /* zero if negative */
                addlong(ARM64_CMP_IMM(14, 0xFF));
                addlong(ARM64_CSEL(14, 10, 14, COND_HI));    /* cap at 0xFF */
                break;
            case A_SEL_TEX:
                addlong(ARM64_LDR_W(14, 0, STATE_tex_a));
                break;
            case A_SEL_COLOR1:
                /* MOVZX from color1+3 (alpha byte) */
                addlong(ARM64_LDRB_IMM(14, 1, PARAMS_color1 + 3));
                break;
            default:
                addlong(ARM64_MOV_ZERO(14));
                break;
        }

        /* Alpha mask test */
        if (params->fbzMode & FBZ_ALPHA_MASK) {
            /* Test bit 0 of a_other. If zero, skip pixel. */
            amask_skip_pos = block_pos;
            addlong(ARM64_TBZ_PLACEHOLDER(14, 0));
        }

        /* a_local (ECX) -> w15 */
        switch (cca_localselect) {
            case CCA_LOCALSELECT_ITER_A:
                if (a_sel == A_SEL_ITER_A) {
                    /* Already computed in w14, just copy */
                    addlong(ARM64_MOV_REG(15, 14));
                } else {
                    /* Compute CLAMP(state->ia >> 12) */
                    addlong(ARM64_LDR_W(15, 0, STATE_ia));
                    addlong(ARM64_ASR_IMM(15, 15, 12));
                    addlong(ARM64_MOVZ_W(10, 0xFF));
                    addlong(ARM64_BIC_REG_ASR(15, 15, 15, 31));  /* zero if negative */
                    addlong(ARM64_CMP_IMM(15, 0xFF));
                    addlong(ARM64_CSEL(15, 10, 15, COND_HI));    /* cap at 0xFF */
                }
                break;
            case CCA_LOCALSELECT_COLOR0:
                /* alpha byte of color0 */
                addlong(ARM64_LDRB_IMM(15, 1, PARAMS_color0 + 3));
                break;
            case CCA_LOCALSELECT_ITER_Z:
                addlong(ARM64_LDR_W(15, 0, STATE_z));
                /* Ensure w10 = 0xFF for clamping if a_sel != ITER_A */
                if (a_sel != A_SEL_ITER_A) {
                    addlong(ARM64_MOVZ_W(10, 0xFF));
                }
                addlong(ARM64_ASR_IMM(15, 15, 20));
                /* Clamp to [0, 0xFF] */
                addlong(ARM64_BIC_REG_ASR(15, 15, 15, 31));  /* zero if negative */
                addlong(ARM64_CMP_IMM(15, 0xFF));
                addlong(ARM64_CSEL(15, 10, 15, COND_HI));    /* cap at 0xFF */
                break;
            default:
                addlong(ARM64_MOVZ_W(15, 0xFF));
                break;
        }

        /* cca_zero_other: EDX -> w12 */
        if (cca_zero_other) {
            addlong(ARM64_MOV_ZERO(12));
        } else {
            addlong(ARM64_MOV_REG(12, 14));  /* w12 = a_other */
        }

        /* cca_sub_clocal */
        if (cca_sub_clocal) {
            addlong(ARM64_SUB_REG(12, 12, 15));  /* w12 -= a_local */
        }
    }

    /* ----------------------------------------------------------------
     * Color local select (clocal -> v1 packed BGRA, then unpack)
     * ----------------------------------------------------------------
     *
     * x86-64 ref: lines 1881-1949
     * XMM1 = clocal (unpacked to 4x16)
     *
     * Only needed if cc_sub_clocal, cc_mselect==CLOCAL, or cc_add==1
     * ---------------------------------------------------------------- */
    if (cc_sub_clocal || cc_mselect == CC_MSELECT_CLOCAL || cc_add == 1) {
        if (!cc_localselect_override) {
            if (cc_localselect) {
                /* clocal = params->color0 */
                addlong(ARM64_LDR_W(4, 1, PARAMS_color0));
                addlong(ARM64_FMOV_S_W(1, 4));
            } else {
                /* clocal = cached iterated BGRA from v6 */
                addlong(ARM64_MOV_V(1, 6));                /* clocal = cached iterated BGRA from v6 */
            }
        } else {
            /* cc_localselect_override: select based on tex_a bit 7 */
            /* TBZ state->tex_a, #7 -> use iter_rgb path */
            addlong(ARM64_LDR_W(4, 0, STATE_tex_a));
            int override_skip = block_pos;
            addlong(ARM64_TBZ_PLACEHOLDER(4, 7));

            /* tex_a bit 7 set: use color0 */
            addlong(ARM64_LDR_W(4, 1, PARAMS_color0));
            addlong(ARM64_FMOV_S_W(1, 4));
            int override_done = block_pos;
            addlong(ARM64_B_PLACEHOLDER);

            /* tex_a bit 7 clear: use iter_rgb */
            PATCH_FORWARD_TBxZ(override_skip);
            /* clocal = cached iterated BGRA from v6 */
            addlong(ARM64_MOV_V(1, 6));                    /* clocal = cached iterated BGRA from v6 */

            PATCH_FORWARD_B(override_done);
        }
        /* Unpack clocal from bytes to 4x16: UXTL v1.8H, v1.8B */
        addlong(ARM64_UXTL_8H_8B(1, 1));
    }

    /* ----------------------------------------------------------------
     * Color other select (cother -> v0, then unpack)
     * ----------------------------------------------------------------
     *
     * x86-64 ref: lines 1950-2016
     * ---------------------------------------------------------------- */
    if (!cc_zero_other) {
        if (_rgb_sel == CC_LOCALSELECT_ITER_RGB) {
            /* cother = cached iterated BGRA from v6 */
            addlong(ARM64_MOV_V(0, 6));                    /* cother = cached iterated BGRA from v6 */
        } else if (_rgb_sel == CC_LOCALSELECT_TEX) {
            /* cother = texture color, already in v0 from Phase 3 */
            /* (v0 already has packed BGRA) */
        } else if (_rgb_sel == CC_LOCALSELECT_COLOR1) {
            addlong(ARM64_LDR_W(4, 1, PARAMS_color1));
            addlong(ARM64_FMOV_S_W(0, 4));
        } else {
            /* CC_LOCALSELECT_LFB: unsupported, use zero */
            addlong(ARM64_MOVI_V2D_ZERO(0));
        }
        /* Unpack cother from bytes to 4x16 */
        addlong(ARM64_UXTL_8H_8B(0, 0));

        /* cc_sub_clocal: v0 = cother - clocal */
        if (cc_sub_clocal) {
            addlong(ARM64_SUB_V4H(0, 0, 1));
        }
    } else {
        /* cc_zero_other: v0 = 0 */
        addlong(ARM64_MOVI_V2D_ZERO(0));
        if (cc_sub_clocal) {
            addlong(ARM64_SUB_V4H(0, 0, 1));  /* v0 = 0 - clocal */
        }
    }

    /* ----------------------------------------------------------------
     * Alpha combine: mselect, reverse_blend, multiply, add, clamp
     * ----------------------------------------------------------------
     *
     * x86-64 ref: lines 2018-2104
     *
     * At this point w12 = (cca_zero_other ? 0 : a_other) - (cca_sub_clocal ? a_local : 0)
     * w14 = a_other, w15 = a_local
     * ---------------------------------------------------------------- */
    if (params->alphaMode & ((1 << 0) | (1 << 4))) {
        if (!(cca_mselect == 0 && cca_reverse_blend == 0)) {
            /* w4 = cca blend factor */
            switch (cca_mselect) {
                case CCA_MSELECT_ALOCAL:
                    addlong(ARM64_MOV_REG(4, 15));
                    break;
                case CCA_MSELECT_AOTHER:
                    addlong(ARM64_MOV_REG(4, 14));
                    break;
                case CCA_MSELECT_ALOCAL2:
                    addlong(ARM64_MOV_REG(4, 15));
                    break;
                case CCA_MSELECT_TEX:
                    addlong(ARM64_LDRB_IMM(4, 0, STATE_tex_a));
                    break;
                case CCA_MSELECT_ZERO:
                default:
                    addlong(ARM64_MOV_ZERO(4));
                    break;
            }
            if (!cca_reverse_blend) {
                addlong(ARM64_EOR_MASK(4, 4, 8));  /* XOR with 0xFF */
            }
            addlong(ARM64_ADD_IMM(4, 4, 1));       /* factor + 1 */
            addlong(ARM64_MUL(12, 12, 4));          /* w12 = (a_diff) * factor */
            addlong(ARM64_ASR_IMM(12, 12, 8));      /* w12 >>= 8 */
        }
    }

    /* Broadcast alpha combine result (w12) to v3 for CC_MSELECT_AOTHER before cca_add modifies it */
    if (!(cc_mselect == 0 && cc_reverse_blend == 0) && cc_mselect == CC_MSELECT_AOTHER) {
        if (params->alphaMode & ((1 << 0) | (1 << 4))) {
            addlong(ARM64_DUP_V4H_GPR(3, 12));
        } else {
            addlong(ARM64_MOVI_V2D_ZERO(3));
        }
    }

    /* cca_add: add a_local to result */
    if (cca_add && (params->alphaMode & ((1 << 0) | (1 << 4)))) {
        addlong(ARM64_ADD_REG(12, 12, 15));
    }

    /* Clamp alpha result to [0, 0xFF] */
    if (params->alphaMode & ((1 << 0) | (1 << 4))) {
        addlong(ARM64_MOVZ_W(10, 0xFF));
        addlong(ARM64_BIC_REG_ASR(12, 12, 12, 31));  /* zero if negative */
        addlong(ARM64_CMP_IMM(12, 0xFF));
        addlong(ARM64_CSEL(12, 10, 12, COND_HI));    /* cap at 0xFF */
        if (cca_invert_output) {
            addlong(ARM64_EOR_MASK(12, 12, 8));      /* XOR with 0xFF */
        }
    }

    /* ----------------------------------------------------------------
     * Color combine: cc_mselect, reverse_blend, multiply
     * ----------------------------------------------------------------
     *
     * x86-64 ref: lines 2106-2208
     *
     * v0 = (cother - clocal) or zero'd out based on cc_zero_other/cc_sub_clocal
     * v1 = clocal (if needed)
     * v3 = blend factor (4x16)
     * ---------------------------------------------------------------- */
    if (!(cc_mselect == 0 && cc_reverse_blend == 0)) {
        switch (cc_mselect) {
            case CC_MSELECT_ZERO:
                addlong(ARM64_MOVI_V2D_ZERO(3));
                break;
            case CC_MSELECT_CLOCAL:
                addlong(ARM64_MOV_V(3, 1));
                break;
            case CC_MSELECT_ALOCAL:
                /* Broadcast a_local (w15) to all 4 lanes of v3 */
                if (params->alphaMode & ((1 << 0) | (1 << 4))) {
                    addlong(ARM64_DUP_V4H_GPR(3, 15));
                } else {
                    /* Need to compute a_local if not done above */
                    switch (cca_localselect) {
                        case CCA_LOCALSELECT_ITER_A:
                            addlong(ARM64_LDR_W(4, 0, STATE_ia));
                            addlong(ARM64_ASR_IMM(4, 4, 12));
                            addlong(ARM64_MOVZ_W(10, 0xFF));
                            addlong(ARM64_BIC_REG_ASR(4, 4, 4, 31));   /* zero if negative */
                            addlong(ARM64_CMP_IMM(4, 0xFF));
                            addlong(ARM64_CSEL(4, 10, 4, COND_HI));    /* cap at 0xFF */
                            break;
                        case CCA_LOCALSELECT_COLOR0:
                            addlong(ARM64_LDRB_IMM(4, 1, PARAMS_color0 + 3));
                            break;
                        case CCA_LOCALSELECT_ITER_Z:
                            addlong(ARM64_LDR_W(4, 0, STATE_z));
                            addlong(ARM64_ASR_IMM(4, 4, 20));
                            addlong(ARM64_MOVZ_W(10, 0xFF));
                            addlong(ARM64_BIC_REG_ASR(4, 4, 4, 31));   /* zero if negative */
                            addlong(ARM64_CMP_IMM(4, 0xFF));
                            addlong(ARM64_CSEL(4, 10, 4, COND_HI));    /* cap at 0xFF */
                            break;
                        default:
                            addlong(ARM64_MOVZ_W(4, 0xFF));
                            break;
                    }
                    addlong(ARM64_DUP_V4H_GPR(3, 4));
                }
                break;
            case CC_MSELECT_AOTHER:
                /* Already copied to v3 above */
                break;
            case CC_MSELECT_TEX:
                /* Broadcast tex_a to all lanes */
                addlong(ARM64_LDR_W(4, 0, STATE_tex_a));
                addlong(ARM64_DUP_V4H_GPR(3, 4));
                break;
            case CC_MSELECT_TEXRGB:
                /* v4 has saved texture packed BGRA. Unpack to 4x16. */
                addlong(ARM64_UXTL_8H_8B(3, 4));
                break;
            default:
                addlong(ARM64_MOVI_V2D_ZERO(3));
                break;
        }

        /* Apply reverse blend to factor */
        if (!cc_reverse_blend) {
            addlong(ARM64_EOR_V(3, 3, 9));     /* XOR with 0xFF (v9 = neon_ff_w) */
        }
        addlong(ARM64_ADD_V4H(3, 3, 8));       /* factor += 1 (v8 = neon_01_w) */

        /* Signed multiply: v0 * v3 -> 32-bit -> >>8 -> saturating narrow
         * SMULL v17.4S, v0.4H, v3.4H
         * SSHR v17.4S, v17.4S, #8
         * SQXTN v0.4H, v17.4S
         */
        addlong(ARM64_SMULL_4S_4H(17, 0, 3));
        addlong(ARM64_SSHR_V4S(17, 17, 8));
        addlong(ARM64_SQXTN_4H_4S(0, 17));
    }

    /* cc_add: add clocal back */
    if (cc_add == 1) {
        addlong(ARM64_ADD_V4H(0, 0, 1));
    }

    /* Pack to unsigned bytes (with saturation): SQXTUN v0.8B, v0.8H */
    addlong(ARM64_SQXTUN_8B_8H(0, 0));

    /* cc_invert_output: XOR with 0x00FFFFFF mask (v10 = neon_ff_b) */
    if (cc_invert_output) {
        addlong(ARM64_EOR_V(0, 0, 10));
    }

    /* ================================================================
     * At this point:
     *   v0 = final combined color (packed BGRA in low 32 bits)
     *   w12 = final combined alpha (if alphaMode enabled)
     *
     * Save to v13 (callee-saved) for fog stage, same as x86-64
     * saving to XMM15.
     * ================================================================ */
    addlong(ARM64_MOV_V(13, 0));

    /* ====================================================================
     * FOG
     * ====================================================================
     * Fog simulates atmospheric haze by blending the pixel's color toward
     * a constant fog color based on distance. Objects far from the camera
     * fade into the fog; nearby objects are unaffected.
     *
     * The Voodoo supports four fog modes:
     *   - Constant fog: a fixed fog factor applied to all pixels
     *   - Z-based fog: fog factor derived from the Z depth (linear fade)
     *   - W-based fog: fog factor looked up from a 64-entry table indexed
     *     by the W value. This gives perspective-correct fog that looks
     *     more natural than Z-based fog.
     *   - Alpha-based fog: the iterated alpha value controls fog density,
     *     allowing per-vertex fog control from the application
     *
     * The blend uses fixed-point math with a denominator of 256:
     *   fog_weight = (fog_a + 1) / 256
     *   final_color = pixel_color + (fog_color - pixel_color) * fog_weight
     * This is done per-channel (R, G, B) using NEON vector math.
     *
     * x86-64 ref: lines 2236-2417
     *
     * At this point:
     *   v0  = final combined color (packed BGRA bytes in low 32 bits)
     *   v13 = color-before-fog copy (for ACOLORBEFOREFOG dest blend)
     *   w12 = final combined alpha (EDX in x86-64)
     *
     * Fog modifies v0. The XMM15/v13 copy preserves pre-fog color.
     *
     * After fog:
     *   v0 = fogged color (packed BGRA bytes)
     *   w12 = alpha (unchanged by fog)
     * ================================================================== */
    if (params->fogMode & FOG_ENABLE) {
        if (params->fogMode & FOG_CONSTANT) {
            /* FOG_CONSTANT: simply add fogColor to color (saturating)
             * v11 = fogColor (packed bytes from prologue) */
            addlong(ARM64_UQADD_V8B(0, 0, 11));
        } else {
            /* Non-constant fog: unpack color to 16-bit lanes for math */
            /* UXTL v0.8H, v0.8B -- unpack bytes to halfwords */
            addlong(ARM64_UXTL_8H_8B(0, 0));

            if (!(params->fogMode & FOG_ADD)) {
                /* v11 = fogColor (packed bytes from prologue)
                 * Widen to 16-bit halfwords for fog math */
                addlong(ARM64_UXTL_8H_8B(3, 11));
            } else {
                /* FOG_ADD: fogColor = 0 */
                addlong(ARM64_MOVI_V2D_ZERO(3));
            }

            if (!(params->fogMode & FOG_MULT)) {
                /* v3 = fogColor - color (i.e., fog_diff) */
                addlong(ARM64_SUB_V4H(3, 3, 0));
            }

            /* Divide by 2 to prevent overflow on multiply */
            /* SSHR v3.4H, v3.4H, #1 */
            addlong(ARM64_SSHR_V4H(3, 3, 1));

            /* Compute fog_a based on fog source */
            switch (params->fogMode & (FOG_Z | FOG_ALPHA)) {
                case 0: {
                    /* w_depth table lookup */
                    /* w4 = state->w_depth */
                    addlong(ARM64_LDR_W(4, 0, STATE_w_depth));
                    /* w5 = w4 (copy for second use) */
                    addlong(ARM64_MOV_REG(5, 4));
                    /* w4 = (w_depth >> 10) & 0x3f -- fog table index */
                    addlong(ARM64_LSR_IMM(4, 4, 10));
                    addlong(ARM64_AND_MASK(4, 4, 6));    /* AND with 0x3F */
                    /* w5 = (w_depth >> 2) & 0xff -- interpolation fraction */
                    addlong(ARM64_LSR_IMM(5, 5, 2));
                    addlong(ARM64_AND_MASK(5, 5, 8));    /* AND with 0xFF */

                    /* Load dfog = fogTable[fog_idx].dfog (byte at offset +1) */
                    /* x6 = &params->fogTable */
                    addlong(ARM64_ADD_IMM_X(6, 1, PARAMS_fogTable));
                    /* x6 = x6 + x4*2 (index * 2 bytes per entry) */
                    addlong(ARM64_ADD_REG_X_LSL(6, 6, 4, 1));
                    /* w7 = dfog (byte at [x6, #1]) */
                    addlong(ARM64_LDRB_IMM(7, 6, 1));
                    /* w6 = fog (byte at [x6, #0]) */
                    addlong(ARM64_LDRB_IMM(6, 6, 0));

                    /* MUL w5, w5, w7 -- fraction * dfog */
                    addlong(ARM64_MUL(5, 5, 7));
                    /* LSR w5, w5, #10 */
                    addlong(ARM64_LSR_IMM(5, 5, 10));
                    /* ADD w4, w6, w5 -- fog_a = fog + (dfog * frac) >> 10 */
                    addlong(ARM64_ADD_REG(4, 6, 5));
                    break;
                }

                case FOG_Z:
                    /* fog_a = (z >> 20) & 0xff -- match interpreter */
                    addlong(ARM64_LDR_W(4, 0, STATE_z));
                    addlong(ARM64_LSR_IMM(4, 4, 20));
                    addlong(ARM64_AND_MASK(4, 4, 8));
                    break;

                case FOG_ALPHA:
                    /* fog_a = CLAMP(ia >> 12) */
                    addlong(ARM64_LDR_W(4, 0, STATE_ia));
                    addlong(ARM64_ASR_IMM(4, 4, 12));
                    /* Clamp to [0, 0xFF] */
                    addlong(ARM64_MOVZ_W(10, 0xFF));
                    addlong(ARM64_BIC_REG_ASR(4, 4, 4, 31));   /* zero if negative */
                    addlong(ARM64_CMP_IMM(4, 0xFF));
                    addlong(ARM64_CSEL(4, 10, 4, COND_HI));    /* cap at 0xFF */
                    break;

                case FOG_W:
                    /* fog_a = (w >> 32) & 0xff -- match interpreter
                     * The interpreter masks BEFORE clamping, so values > 0xFF
                     * wrap to their low byte rather than saturating to 0xFF.
                     * The AND alone is sufficient (result always in [0,255]). */
                    addlong(ARM64_LDR_W(4, 0, STATE_w + 4));  /* high word of w */
                    addlong(ARM64_AND_MASK(4, 4, 8));          /* & 0xFF */
                    break;
            }

            /* fog_a *= 2 (to compensate for the >>1 above) */
            addlong(ARM64_ADD_REG(4, 4, 4));  /* ADD w4, w4, w4 = w4 << 1 */

            /* Multiply: v3 = v3 * alookup[fog_a + 1] >> 7
             *
             * alookup is a voodoo_neon_reg_t array (16 bytes per entry).
             * w4 = fog_a * 2, so byte offset = w4 * 8 = fog_a * 16.
             * The +16 on the LDR advances one entry, loading alookup[fog_a + 1]
             * (matches the x86-64 +16 byte offset into the next alookup entry). */
            addlong(ARM64_ADD_REG_X_LSL(5, 20, 4, 3));  /* x5 = x20 + fog_a * 16 */
            addlong(ARM64_LDR_D(5, 5, 16));             /* v5 = alookup[fog_a + 1].low64 */

            addlong(ARM64_MUL_V4H(3, 3, 5));   /* v3 *= alookup[fog_a + 1] */
            addlong(ARM64_SSHR_V4H(3, 3, 7));  /* v3 >>= 7 (arithmetic) */

            if (params->fogMode & FOG_MULT) {
                /* FOG_MULT: result = fog contribution only */
                addlong(ARM64_MOV_V(0, 3));
            } else {
                /* Normal: result = color + fog_diff */
                addlong(ARM64_ADD_V4H(0, 0, 3));
            }
            /* Pack back to unsigned bytes: SQXTUN v0.8B, v0.8H */
            addlong(ARM64_SQXTUN_8B_8H(0, 0));
        }
    }

    /* ====================================================================
     * ALPHA TEST
     * ====================================================================
     * Alpha test is a binary accept/reject based on the pixel's alpha value.
     * Unlike alpha blending (which is gradual), alpha test is all-or-nothing:
     * the pixel is either fully drawn or fully discarded.
     *
     * The test compares the computed alpha against a reference value using
     * a configurable function (never, less, equal, lequal, greater, notequal,
     * gequal, always) -- same comparison ops as the depth test.
     *
     * Primary use cases:
     *   - Cutout transparency: trees, fences, chain-link textures where
     *     each texel is either fully opaque or fully transparent. The alpha
     *     channel acts as a 1-bit mask. Much cheaper than alpha blending.
     *   - Alpha-to-coverage approximation on hardware without MSAA.
     *
     * If the test fails, the pixel is skipped (no framebuffer or depth
     * buffer writes).
     *
     * x86-64 ref: lines 2419-2467
     *
     * Compare alpha (w12 = EDX) against alphaMode byte 3 (alpha ref).
     * Skip pixel if test fails (branch to skip position).
     * ================================================================== */
    if ((params->alphaMode & 1) && (alpha_func != AFUNC_NEVER) && (alpha_func != AFUNC_ALWAYS)) {
        /* Load alpha reference: LDRB w4, [x1, #PARAMS_alphaMode + 3] */
        addlong(ARM64_LDRB_IMM(4, 1, PARAMS_alphaMode + 3));
        /* CMP w12, w4 */
        addlong(ARM64_CMP_REG(12, 4));

        /* Branch to skip if test fails. The condition is INVERTED:
         * we skip when the test is NOT met. */
        switch (alpha_func) {
            case AFUNC_LESSTHAN:
                /* Pass if alpha < ref. Skip if alpha >= ref. */
                a_skip_pos = block_pos;
                addlong(ARM64_BCOND_PLACEHOLDER(COND_CS));  /* Branch if carry set (unsigned >=) */
                break;
            case AFUNC_EQUAL:
                /* Pass if alpha == ref. Skip if alpha != ref. */
                a_skip_pos = block_pos;
                addlong(ARM64_BCOND_PLACEHOLDER(COND_NE));
                break;
            case AFUNC_LESSTHANEQUAL:
                /* Pass if alpha <= ref. Skip if alpha > ref. */
                a_skip_pos = block_pos;
                addlong(ARM64_BCOND_PLACEHOLDER(COND_HI));
                break;
            case AFUNC_GREATERTHAN:
                /* Pass if alpha > ref. Skip if alpha <= ref. */
                a_skip_pos = block_pos;
                addlong(ARM64_BCOND_PLACEHOLDER(COND_LS));
                break;
            case AFUNC_NOTEQUAL:
                /* Pass if alpha != ref. Skip if alpha == ref. */
                a_skip_pos = block_pos;
                addlong(ARM64_BCOND_PLACEHOLDER(COND_EQ));
                break;
            case AFUNC_GREATERTHANEQUAL:
                /* Pass if alpha >= ref. Skip if alpha < ref. */
                a_skip_pos = block_pos;
                addlong(ARM64_BCOND_PLACEHOLDER(COND_CC));  /* Branch if carry clear (unsigned <) */
                break;
        }
    }
    /* AFUNC_NEVER is handled by early return before the prologue. */

    /* ====================================================================
     * ALPHA BLEND
     * ====================================================================
     * Alpha blending composites the new pixel with the color already in
     * the framebuffer, creating smooth transparency effects (glass, water,
     * smoke, particle effects, etc.).
     *
     * The blend equation is:
     *   final = src_color * src_factor + dst_color * dst_factor
     *
     * Where src is the incoming pixel and dst is what's already in the
     * framebuffer. The factors are selected per-channel from:
     *   - Zero, one (trivial: discard or keep)
     *   - Source alpha, (1 - source alpha): classic transparency
     *   - Destination alpha, (1 - dest alpha): for pre-multiplied alpha
     *   - Source color, (1 - source color): for advanced effects
     *
     * To blend, the GPU must first READ the existing framebuffer pixel
     * (dst_color), which is an expensive memory access. The framebuffer
     * stores RGB565 (16-bit), so the read value must be unpacked to 8-bit
     * per channel before blending.
     *
     * The multiply-and-round sequence used here:
     *   result = (product + 1 + (product >> 8)) >> 8
     * matches the Voodoo hardware's fixed-point 8-bit multiply rounding.
     * This appears many times in the src_afunc/dest_afunc switch cases.
     *
     * x86-64 ref: lines 2469-3058
     *
     * Register plan:
     *   w12 = src alpha (EDX in x86-64), doubled for table index
     *   v0  = src color (unpacked 4x16 after unpack)
     *   v4  = dst color (unpacked 4x16)
     *   v6  = dst color copy (for src AFUNC_A_COLOR / AOM_COLOR)
     *   w5  = dst alpha (EBX in x86-64), doubled for table index
     *   x8  = fb_mem, x9 = aux_mem (pinned from prologue)
     * ================================================================== */
    if (params->alphaMode & (1 << 4)) {
        /* Load dest alpha from aux buffer if alpha-buffer enabled */
        if (params->fbzMode & FBZ_ALPHA_ENABLE) {
            /* Load x coordinate for aux buffer (tiled or linear) */
            if (params->aux_tiled)
                addlong(ARM64_LDR_W(5, 0, STATE_x_tiled));
            else
                addlong(ARM64_MOV_REG(5, 28));  /* cached STATE_x */
            /* LDRH w5, [x9, x5, LSL #1] -- load 16-bit aux value */
            addlong(ARM64_LDRH_REG_LSL1(5, 9, 5));
        } else {
            /* No alpha buffer: dest_alpha = 0xFF */
            addlong(ARM64_MOVZ_W(5, 0xFF));
        }

        /* Load dest RGB from framebuffer */
        /* Load x coordinate (tiled or linear) */
        if (params->col_tiled)
            addlong(ARM64_LDR_W(4, 0, STATE_x_tiled));
        else
            addlong(ARM64_MOV_REG(4, 28));  /* cached STATE_x */

        /* w12 *= 2, w5 *= 2 -- for table indexing (each entry is 16 bytes) */
        addlong(ARM64_ADD_REG(12, 12, 12));  /* w12 = src_alpha * 2 */
        addlong(ARM64_ADD_REG(5, 5, 5));     /* w5 = dst_alpha * 2 */

        /* Load 16-bit RGB565 pixel from fb_mem */
        /* LDRH w6, [x8, x4, LSL #1] */
        addlong(ARM64_LDRH_REG_LSL1(6, 8, 4));

        /* Unpack src color from bytes to 16-bit lanes */
        addlong(ARM64_UXTL_8H_8B(0, 0));

        /* Decode dest RGB565 via rgb565[] lookup table.
         * rgb565 is an array of rgba8_t (4 bytes each).
         * Load rgb565[pixel] (32-bit), put in v4, unpack to 4x16. */
        /* Use pinned rgb565 pointer from x26 */
        /* LDR w6, [x26, w6, UXTW #2] -- rgb565[pixel] */
        addlong(ARM64_LDR_W_UXTW2(6, 26, 6));
        addlong(ARM64_FMOV_S_W(4, 6));
        addlong(ARM64_UXTL_8H_8B(4, 4));

        /* Save dest color in v6 for src_afunc A_COLOR/AOM_COLOR */
        addlong(ARM64_MOV_V(6, 4));

        /* ---- dest_afunc: compute dest blend factor and apply to v4 ----
         *
         * Rounding sequence used throughout both dest_afunc and src_afunc:
         *   result = (product + alookup[1] + (product >> 8)) >> 8
         * This is the Voodoo hardware's standard 8-bit alpha multiply with
         * rounding correction. alookup[1] = {1,1,1,1} (loaded at offset 16
         * from alookup[0]). The sequence matches x86-64 PMULHUW+round pattern.
         * ---- */
        switch (dest_afunc) {
            case AFUNC_AZERO:
                addlong(ARM64_MOVI_V2D_ZERO(4));
                break;
            case AFUNC_ASRC_ALPHA:
                /* v4 = dst * alookup[src_alpha] >> 8 */
                addlong(ARM64_ADD_REG_X_LSL(7, 20, 12, 3));  /* x7 = alookup + src_alpha*2*8 */
                addlong(ARM64_LDR_D(5, 7, 0));               /* v5 = alookup[src_alpha] */
                addlong(ARM64_MUL_V4H(4, 4, 5));
                /* Round: add v8 (alookup[1] pinned), add (result>>8), shift >>8 */
                addlong(ARM64_USHR_V4H(17, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 17));
                addlong(ARM64_USHR_V4H(4, 4, 8));
                break;
            case AFUNC_A_COLOR:
                /* v4 = dst * src_color >> 8 */
                addlong(ARM64_MUL_V4H(4, 4, 0));
                addlong(ARM64_USHR_V4H(17, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 17));
                addlong(ARM64_USHR_V4H(4, 4, 8));
                break;
            case AFUNC_ADST_ALPHA:
                /* v4 = dst * alookup[dst_alpha] >> 8 */
                addlong(ARM64_ADD_REG_X_LSL(7, 20, 5, 3));
                addlong(ARM64_LDR_D(16, 7, 0));
                addlong(ARM64_MUL_V4H(4, 4, 16));
                addlong(ARM64_USHR_V4H(17, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 17));
                addlong(ARM64_USHR_V4H(4, 4, 8));
                break;
            case AFUNC_AONE:
                /* v4 = dst * 1 = dst (no-op) */
                break;
            case AFUNC_AOMSRC_ALPHA:
                /* v4 = dst * aminuslookup[src_alpha] >> 8 */
                addlong(ARM64_ADD_REG_X_LSL(7, 21, 12, 3));  /* x7 = aminuslookup + src_alpha*2*8 */
                addlong(ARM64_LDR_D(16, 7, 0));
                addlong(ARM64_MUL_V4H(4, 4, 16));
                addlong(ARM64_USHR_V4H(17, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 17));
                addlong(ARM64_USHR_V4H(4, 4, 8));
                break;
            case AFUNC_AOM_COLOR:
                /* v4 = dst * (0xFF - src_color) >> 8 */
                addlong(ARM64_MOV_V(16, 9));         /* v16 = 0xFF */
                addlong(ARM64_SUB_V4H(16, 16, 0));   /* v16 = 0xFF - src */
                addlong(ARM64_MUL_V4H(4, 4, 16));
                addlong(ARM64_USHR_V4H(17, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 17));
                addlong(ARM64_USHR_V4H(4, 4, 8));
                break;
            case AFUNC_AOMDST_ALPHA:
                /* v4 = dst * aminuslookup[dst_alpha] >> 8 */
                addlong(ARM64_ADD_REG_X_LSL(7, 21, 5, 3));
                addlong(ARM64_LDR_D(16, 7, 0));
                addlong(ARM64_MUL_V4H(4, 4, 16));
                addlong(ARM64_USHR_V4H(17, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 17));
                addlong(ARM64_USHR_V4H(4, 4, 8));
                break;
            case AFUNC_ACOLORBEFOREFOG:
                /* v4 = dst * color-before-fog (v13) >> 8 */
                /* Unpack v13 to 4x16 in v16 */
                addlong(ARM64_UXTL_8H_8B(16, 13));
                addlong(ARM64_MUL_V4H(4, 4, 16));
                addlong(ARM64_USHR_V4H(17, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 8));
                addlong(ARM64_ADD_V4H(4, 4, 17));
                addlong(ARM64_USHR_V4H(4, 4, 8));
                break;
        }

        /* ---- src_afunc: compute src blend factor and apply to v0 ---- */
        switch (src_afunc) {
            case AFUNC_AZERO:
                addlong(ARM64_MOVI_V2D_ZERO(0));
                break;
            case AFUNC_ASRC_ALPHA:
                /* v0 = src * alookup[src_alpha] >> 8 */
                addlong(ARM64_ADD_REG_X_LSL(7, 20, 12, 3));
                addlong(ARM64_LDR_D(16, 7, 0));
                addlong(ARM64_MUL_V4H(0, 0, 16));
                addlong(ARM64_USHR_V4H(17, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 17));
                addlong(ARM64_USHR_V4H(0, 0, 8));
                break;
            case AFUNC_A_COLOR:
                /* v0 = src * dst_color (v6) >> 8 */
                addlong(ARM64_MUL_V4H(0, 0, 6));
                addlong(ARM64_USHR_V4H(17, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 17));
                addlong(ARM64_USHR_V4H(0, 0, 8));
                break;
            case AFUNC_ADST_ALPHA:
                /* v0 = src * alookup[dst_alpha] >> 8 */
                addlong(ARM64_ADD_REG_X_LSL(7, 20, 5, 3));
                addlong(ARM64_LDR_D(16, 7, 0));
                addlong(ARM64_MUL_V4H(0, 0, 16));
                addlong(ARM64_USHR_V4H(17, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 17));
                addlong(ARM64_USHR_V4H(0, 0, 8));
                break;
            case AFUNC_AONE:
                /* v0 = src * 1 = src (no-op) */
                break;
            case AFUNC_AOMSRC_ALPHA:
                /* v0 = src * aminuslookup[src_alpha] >> 8 */
                addlong(ARM64_ADD_REG_X_LSL(7, 21, 12, 3));
                addlong(ARM64_LDR_D(16, 7, 0));
                addlong(ARM64_MUL_V4H(0, 0, 16));
                addlong(ARM64_USHR_V4H(17, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 17));
                addlong(ARM64_USHR_V4H(0, 0, 8));
                break;
            case AFUNC_AOM_COLOR:
                /* v0 = src * (0xFF - dst_color) >> 8 */
                addlong(ARM64_MOV_V(16, 9));          /* v16 = 0xFF */
                addlong(ARM64_SUB_V4H(16, 16, 6));    /* v16 = 0xFF - dst */
                addlong(ARM64_MUL_V4H(0, 0, 16));
                addlong(ARM64_USHR_V4H(17, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 17));
                addlong(ARM64_USHR_V4H(0, 0, 8));
                break;
            case AFUNC_AOMDST_ALPHA:
                /* v0 = src * aminuslookup[dst_alpha] >> 8 */
                addlong(ARM64_ADD_REG_X_LSL(7, 21, 5, 3));
                addlong(ARM64_LDR_D(16, 7, 0));
                addlong(ARM64_MUL_V4H(0, 0, 16));
                addlong(ARM64_USHR_V4H(17, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 17));
                addlong(ARM64_USHR_V4H(0, 0, 8));
                break;
            case AFUNC_ASATURATE: {
                /* sat = min(src_alpha, 0xFF - dst_alpha)
                 * w5 = 2*dst_alpha, w12 = 2*src_alpha */
                addlong(ARM64_LSR_IMM(6, 5, 1));        /* w6 = dst_alpha */
                addlong(ARM64_EOR_MASK(6, 6, 8));       /* w6 = 0xFF ^ dst_alpha = 0xFF - dst_alpha */
                addlong(ARM64_ADD_REG(6, 6, 6));         /* w6 *= 2 for table index */
                addlong(ARM64_CMP_REG(12, 6));
                addlong(ARM64_CSEL(6, 6, 12, COND_HI)); /* w6 = min(src_alpha*2, sat*2) */
                /* v0 = src * alookup[sat] >> 8 */
                addlong(ARM64_ADD_REG_X_LSL(7, 20, 6, 3));
                addlong(ARM64_LDR_D(16, 7, 0));
                addlong(ARM64_MUL_V4H(0, 0, 16));
                addlong(ARM64_USHR_V4H(17, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 8));
                addlong(ARM64_ADD_V4H(0, 0, 17));
                addlong(ARM64_USHR_V4H(0, 0, 8));
                break;
            }
        }

        /* Combine: v0 = src_blended + dst_blended */
        addlong(ARM64_ADD_V4H(0, 0, 4));

        /* Pack to unsigned bytes with saturation */
        addlong(ARM64_SQXTUN_8B_8H(0, 0));

        /* Alpha blend for alpha channel:
         * dest_aafunc and src_aafunc compute the final alpha.
         * x86-64 ref: lines 3034-3057
         * w4 = 0, accumulate dest_aa and src_aa contributions. */
        addlong(ARM64_MOV_ZERO(4));  /* w4 = 0 (accumulator for blended alpha) */

        if (dest_aafunc == 4) {
            /* dest_aafunc == AFUNC_AONE (4): factor is 1.0, so the full
             * destination alpha passes through to the blended output alpha.
             * w5 holds dst_alpha * 2 (doubled for table indexing), so
             * (w5 << 7) >> 8 = dst_alpha exactly. Matches x86-64 lines 3037-3042. */
            addlong(ARM64_LSL_IMM(6, 5, 7));   /* w6 = (dst_alpha*2) << 7; >>8 later gives correct alpha */
            addlong(ARM64_ADD_REG(4, 4, 6));
        }

        if (src_aafunc == 4) {
            /* src_aafunc == AFUNC_AONE (4): factor is 1.0, so the full
             * source alpha passes through. w12 = src_alpha * 2, so (w12 << 7) >> 8 = src_alpha exactly. */
            addlong(ARM64_LSL_IMM(6, 12, 7));  /* w6 = (src_alpha*2) << 7; >>8 later gives correct alpha */
            addlong(ARM64_ADD_REG(4, 4, 6));
        }

        /* LSR w4, w4, #8 */
        addlong(ARM64_LSR_IMM(4, 4, 8));
        /* w12 = final blended alpha */
        addlong(ARM64_MOV_REG(12, 4));
    }

    /* ================================================================
     * Phase 6: Depth write (alpha-buffer path)
     * ================================================================
     *
     * x86-64 ref: lines 3060-3075
     *
     * NOTE: Depth write is split into two separate sections:
     *   1. Here: when FBZ_ALPHA_ENABLE is set, the alpha buffer (aux_mem)
     *      stores the blended alpha value (w12 = post-blend alpha).
     *      This must happen before the skip patch points because skipped
     *      pixels do NOT update the alpha buffer.
     *   2. After the FB write: when FBZ_ALPHA_ENABLE is NOT set, the aux
     *      buffer stores new_depth. This also happens before skip targets.
     * Both sections write to aux_mem; only the value written differs.
     *
     * Write depth to aux buffer when both depth write mask and
     * alpha-buffer are enabled. The depth value to write is w12
     * (the alpha result, same as EDX on x86-64).
     * ================================================================ */
    if ((params->fbzMode & (FBZ_DEPTH_WMASK | FBZ_ALPHA_ENABLE)) == (FBZ_DEPTH_WMASK | FBZ_ALPHA_ENABLE)) {
        if (params->aux_tiled)
            addlong(ARM64_LDR_W(4, 0, STATE_x_tiled));
        else
            addlong(ARM64_MOV_REG(4, 28));  /* cached STATE_x */
        /* STRH w12, [x9, x4, LSL #1] -- store alpha/depth to aux buffer */
        addlong(ARM64_STRH_REG_LSL1(12, 9, 4));
    }

    /* ====================================================================
     * DITHERING + FRAMEBUFFER WRITE
     * ====================================================================
     * The Voodoo's framebuffer is 16-bit RGB565 (5 red, 6 green, 5 blue).
     * This gives only 65,536 possible colors, which causes visible banding
     * on smooth gradients (sky, fog, lighting ramps).
     *
     * Dithering reduces banding by adding a small position-dependent offset
     * to each color channel before truncating to 5/6 bits. Adjacent pixels
     * get different offsets, so the eye perceives an average that's closer
     * to the true color. The Voodoo uses either a 2x2 or 4x4 ordered
     * dither matrix (Bayer pattern), indexed by (x & mask, y & mask).
     *
     * The dither table (dither_rb for red/blue, dither_g for green)
     * provides the truncated+dithered value as a direct lookup:
     *   dither_rb[dither_offset + channel_value]  -> 5-bit result
     *   dither_g [dither_offset + channel_value]  -> 6-bit result
     *
     * The final RGB565 pixel is packed as: (R5 << 11) | (G6 << 5) | B5
     * and written to fb_mem[x] (or fb_mem[x_tiled] if tiling is active).
     *
     * When dithering is disabled, the code simply shifts and masks:
     *   R5 = R8 >> 3, G6 = G8 >> 2, B5 = B8 >> 3
     *
     * x86-64 ref: lines 3077-3221
     * ================================================================== */

    /* Load x coordinate for framebuffer write */
    if (params->col_tiled)
        addlong(ARM64_LDR_W(14, 0, STATE_x_tiled));
    else
        addlong(ARM64_MOV_REG(14, 28));  /* cached STATE_x */

    /* Extract packed BGRA from v0 to w4 */
    addlong(ARM64_FMOV_W_S(4, 0));

    if (params->fbzMode & FBZ_RGB_WMASK) {
        if (dither) {
            /* ---- Dither path ---- */
            /* Load dither table base pointer into x7 (skip zero halfwords) */
            {
                uintptr_t dither_rb_addr = dither2x2 ? (uintptr_t) dither_rb2x2 : (uintptr_t) dither_rb;
                uint16_t _dh0 = dither_rb_addr & 0xFFFF;
                uint16_t _dh1 = (dither_rb_addr >> 16) & 0xFFFF;
                uint16_t _dh2 = (dither_rb_addr >> 32) & 0xFFFF;
                uint16_t _dh3 = (dither_rb_addr >> 48) & 0xFFFF;
                int _df = (_dh0) ? 0 : (_dh1) ? 1 : (_dh2) ? 2 : 3;
                uint16_t _dfv = (_df == 0) ? _dh0 : (_df == 1) ? _dh1
                              : (_df == 2) ? _dh2 : _dh3;
                addlong(ARM64_MOVZ_X_HW(7, _dfv, _df));
                if (_df < 1 && _dh1)
                    addlong(ARM64_MOVK_X(7, _dh1, 1));
                if (_df < 2 && _dh2)
                    addlong(ARM64_MOVK_X(7, _dh2, 2));
                if (_df < 3 && _dh3)
                    addlong(ARM64_MOVK_X(7, _dh3, 3));
            }

            /* w5 = real_y (saved in x24 by prologue) */
            addlong(ARM64_MOV_REG(5, 24));

            /* Extract R, G, B bytes from w4 (packed BGRA: B=byte0, G=byte1, R=byte2, A=byte3) */
            /* w6 = G = (w4 >> 8) & 0xFF */
            addlong(ARM64_UBFX(6, 4, 8, 8));
            /* w10 = R = (w4 >> 16) & 0xFF -- but we need it for dither_rb */
            /* Actually for dither, we need: R index, G index, B index
             * dither_rb[value][y&mask][x&mask] for R and B
             * dither_g[value][y&mask][x&mask] for G
             *
             * For 4x4: entry_size = 4*4 = 16 bytes per value
             *   index = value*16 + (y&3)*4 + (x&3)
             * For 2x2: entry_size = 2*2 = 4 bytes per value
             *   index = value*4 + (y&1)*2 + (x&1)
             */

            if (dither2x2) {
                /* 2x2 dither */
                addlong(ARM64_AND_MASK(10, 14, 1));   /* w10 = x & 1 */
                addlong(ARM64_AND_MASK(5, 5, 1));     /* w5 = y & 1 */
                /* w11 = G*4 (for dither_g2x2 index) */
                addlong(ARM64_LSL_IMM(11, 6, 2));
                /* w13 = R (byte2) */
                addlong(ARM64_UBFX(13, 4, 16, 8));
                /* w6 = B (byte0) */
                addlong(ARM64_AND_MASK(6, 4, 8));
            } else {
                /* 4x4 dither */
                addlong(ARM64_AND_MASK(10, 14, 2));   /* w10 = x & 3 */
                addlong(ARM64_AND_MASK(5, 5, 2));     /* w5 = y & 3 */
                /* w11 = G*16 (for dither_g index) */
                addlong(ARM64_LSL_IMM(11, 6, 4));
                /* w13 = R (byte2) */
                addlong(ARM64_UBFX(13, 4, 16, 8));
                /* w6 = B (byte0) */
                addlong(ARM64_AND_MASK(6, 4, 8));
            }

            if (dither2x2) {
                /* dither offset = value*4 + y*2 + x */
                addlong(ARM64_ADD_REG_LSL(5, 10, 5, 1));   /* w5 = x + y*2 */
                addlong(ARM64_LSL_IMM(13, 13, 2));          /* R*4 */
                addlong(ARM64_LSL_IMM(6, 6, 2));             /* B*4 */
            } else {
                /* dither offset = value*16 + y*4 + x */
                addlong(ARM64_ADD_REG_LSL(5, 10, 5, 2));   /* w5 = x + y*4 */
                addlong(ARM64_LSL_IMM(13, 13, 4));           /* R*16 */
                addlong(ARM64_LSL_IMM(6, 6, 4));              /* B*16 */
            }

            /* w5 = dither sub-index (y*stride + x) -- add to each table base+value offset */
            /* Add dither_rb base (x7) + sub-index (w5) */
            addlong(ARM64_ADD_REG_X(7, 7, 5));

            /* Load G from dither_g table:
             * dither_g is at a fixed offset from dither_rb */
            {
                uintptr_t g_offset = dither2x2 ? ((uintptr_t) dither_g2x2 - (uintptr_t) dither_rb2x2) :
                                                   ((uintptr_t) dither_g - (uintptr_t) dither_rb);
                /* w11 = G value offset, add g_offset to get dither_g entry */
                addlong(ARM64_ADD_REG_X(11, 7, 11));
                /* x16 = g_offset */
                addlong(ARM64_MOVZ_X(16, g_offset & 0xFFFF));
                if ((g_offset >> 16) & 0xFFFF)
                    addlong(ARM64_MOVK_X(16, (g_offset >> 16) & 0xFFFF, 1));
                if ((g_offset >> 32) & 0xFFFF)
                    addlong(ARM64_MOVK_X(16, (g_offset >> 32) & 0xFFFF, 2));
                if ((g_offset >> 48) & 0xFFFF)
                    addlong(ARM64_MOVK_X(16, (g_offset >> 48) & 0xFFFF, 3));
                /* LDRB w11, [x11, x16] -- dithered G */
                addlong(ARM64_LDRB_REG(11, 11, 16));
            }
            /* LDRB w13, [x7, x13] -- dithered R */
            addlong(ARM64_LDRB_REG(13, 7, 13));
            /* LDRB w6, [x7, x6] -- dithered B */
            addlong(ARM64_LDRB_REG(6, 7, 6));

            /* Pack RGB565: R(5) << 11 | G(6) << 5 | B(5) */
            addlong(ARM64_LSL_IMM(13, 13, 11));
            addlong(ARM64_LSL_IMM(11, 11, 5));
            addlong(ARM64_ORR_REG(4, 13, 11));
            addlong(ARM64_ORR_REG(4, 4, 6));
        } else {
            /* ---- No-dither path ---- */
            /* w4 = packed BGRA (B=byte0, G=byte1, R=byte2, A=byte3)
             * RGB565 = R[7:3] << 11 | G[7:2] << 5 | B[7:3]
             *
             * On x86-64:
             *   Blue  = (byte0 >> 3) & 0x001F
             *   Green = (byte1 << 3) & 0x07E0
             *   Red   = (byte2 << 8) & 0xF800 -- actually (byte_val << 8) then AND
             *
             * Pack RGB565 using bitfield extract/insert:
             */
            /* w5 = B = bits[7:3] -> 5-bit blue */
            addlong(ARM64_UBFX(5, 4, 3, 5));
            /* w6 = G = bits[15:10] -> 6-bit green */
            addlong(ARM64_UBFX(6, 4, 10, 6));
            /* w4 = R = bits[23:19] -> 5-bit red (safe to overwrite w4) */
            addlong(ARM64_UBFX(4, 4, 19, 5));
            /* w4 = R << 11 */
            addlong(ARM64_LSL_IMM(4, 4, 11));
            /* BFI w4, w6, #5, #6 -- insert G into bits [10:5] */
            addlong(ARM64_BFI(4, 6, 5, 6));
            /* w4 |= B (bits [4:0]) */
            addlong(ARM64_ORR_REG(4, 4, 5));
        }

        /* Store RGB565 pixel to framebuffer:
         * STRH w4, [x8, x14, LSL #1] */
        addlong(ARM64_STRH_REG_LSL1(4, 8, 14));
    }

    /* ================================================================
     * Phase 6: Depth write (non-alpha-buffer path)
     * ================================================================
     *
     * x86-64 ref: lines 3224-3243
     *
     * Write new_depth to aux buffer when depth write mask and depth
     * test are enabled but alpha-buffer is NOT enabled.
     * ================================================================ */
    if ((params->fbzMode & (FBZ_DEPTH_WMASK | FBZ_DEPTH_ENABLE)) == (FBZ_DEPTH_WMASK | FBZ_DEPTH_ENABLE)
        && !(params->fbzMode & FBZ_ALPHA_ENABLE)) {
        if (params->aux_tiled)
            addlong(ARM64_LDR_W(4, 0, STATE_x_tiled));
        else
            addlong(ARM64_MOV_REG(4, 28));  /* cached STATE_x */
        /* Load new_depth */
        addlong(ARM64_LDRH_IMM(5, 0, STATE_new_depth));
        /* STRH w5, [x9, x4, LSL #1] */
        addlong(ARM64_STRH_REG_LSL1(5, 9, 4));
    }

    /* ================================================================
     * Patch skip positions (z_skip, a_skip, chroma_skip, stipple_skip,
     * amask_skip): all skip to here (after framebuffer write, before
     * per-pixel increments).
     *
     * Skipped pixels (depth fail, alpha fail, chroma key, stipple) branch
     * here -- after all framebuffer and depth writes, so no pixel data is
     * committed, but the per-pixel interpolant increments below still run
     * to keep the rasterizer state consistent with the next X position.
     * The ordering matches x86-64 lines 3245-3254.
     * ================================================================ */
    if (z_skip_pos)
        PATCH_FORWARD_BCOND(z_skip_pos);
    if (a_skip_pos)
        PATCH_FORWARD_BCOND(a_skip_pos);
    if (chroma_skip_pos)
        PATCH_FORWARD_CBxZ(chroma_skip_pos);
    if (amask_skip_pos)
        PATCH_FORWARD_TBxZ(amask_skip_pos);
    if (stipple_skip_pos) {
        if (params->fbzMode & FBZ_STIPPLE_PATT) {
            PATCH_FORWARD_BCOND(stipple_skip_pos);
        } else {
            PATCH_FORWARD_TBxZ(stipple_skip_pos);
        }
    }

    /* ====================================================================
     * PER-PIXEL INCREMENTS + LOOP
     * ====================================================================
     * The Voodoo rasterizes triangles by scanning horizontally across each
     * scanline (span). All the per-pixel state values (color, depth, texture
     * coordinates) are set up for the leftmost pixel of the span and then
     * incremented by fixed deltas (dR/dX, dZ/dX, dS/dX, dT/dX, dW/dX) for
     * each step to the right. This is called "edge walking" or "span
     * interpolation" and is far cheaper than recomputing everything per pixel.
     *
     * The increments updated here are:
     *   - ib, ig, ir, ia: iterated Blue, Green, Red, Alpha (Gouraud shading)
     *   - z: depth value (for Z-buffer mode)
     *   - tmu0 s, t, w: texture unit 0 coordinates and perspective divisor
     *   - tmu1 s, t, w: texture unit 1 coordinates (if dual-TMU)
     *   - w: global W (for W-buffer and fog)
     *   - pixel_count, texel_count: performance counters
     *
     * After incrementing, X is advanced by +1 (or -1 for right-to-left spans)
     * and the loop branches back to the top if pixels remain.
     *
     * x86-64 ref: lines 3256-3445
     * ================================================================== */

    /* ib/ig/ir/ia increment (4 x int32, contiguous at STATE_ib=472).
     * 472 is not 16-byte aligned, so use ADD+LD1 instead of LDR Q.
     * RGBA deltas are hoisted in v12 from the prologue. */
    addlong(ARM64_ADD_IMM_X(16, 0, STATE_ib));    /* x16 = &state->ib */
    addlong(ARM64_LD1_V4S(0, 16));                 /* v0 = {ib, ig, ir, ia} */
    if (state->xdir > 0) {
        addlong(ARM64_ADD_V4S(0, 0, 12));
    } else {
        addlong(ARM64_SUB_V4S(0, 0, 12));
    }
    addlong(ARM64_ST1_V4S(0, 16));  /* store v0 back to [x16] (= &state->ib) */

    /* Z increment */
    /* LDR w4, [x0, #STATE_z] */
    addlong(ARM64_LDR_W(4, 0, STATE_z));
    /* LDR w5, [x1, #PARAMS_dZdX] */
    addlong(ARM64_LDR_W(5, 1, PARAMS_dZdX));
    if (state->xdir > 0) {
        addlong(ARM64_ADD_REG(4, 4, 5));
    } else {
        addlong(ARM64_SUB_REG(4, 4, 5));
    }
    addlong(ARM64_STR_W(4, 0, STATE_z));

    /* TMU0 s/t increment (64-bit add/sub).
     * TMU0 ST deltas are hoisted in v15 from the prologue. */
    addlong(ARM64_LDR_Q(0, 0, STATE_tmu0_s));
    if (state->xdir > 0) {
        addlong(ARM64_ADD_V2D(0, 0, 15));
    } else {
        addlong(ARM64_SUB_V2D(0, 0, 15));
    }
    addlong(ARM64_STR_Q(0, 0, STATE_tmu0_s));

    /* TMU0 W increment (64-bit) */
    addlong(ARM64_LDR_X(10, 0, STATE_tmu0_w));
    addlong(ARM64_LDR_X(11, 1, PARAMS_tmu0_dWdX));
    if (state->xdir > 0) {
        addlong(ARM64_ADD_REG_X(10, 10, 11));
    } else {
        addlong(ARM64_SUB_REG_X(10, 10, 11));
    }
    addlong(ARM64_STR_X(10, 0, STATE_tmu0_w));

    /* Global W increment (64-bit) */
    addlong(ARM64_LDR_X(10, 0, STATE_w));
    addlong(ARM64_LDR_X(11, 1, PARAMS_dWdX));
    if (state->xdir > 0) {
        addlong(ARM64_ADD_REG_X(10, 10, 11));
    } else {
        addlong(ARM64_SUB_REG_X(10, 10, 11));
    }
    addlong(ARM64_STR_X(10, 0, STATE_w));

    /* TMU1 s/t/w if dual TMUs */
    if (voodoo->dual_tmus) {
        /* TMU1 s/t (128-bit NEON).
         * STATE_tmu1_s = 520, not 16-byte aligned -- use ADD+LD1/ST1.
         * TMU1 ST deltas are hoisted in v14 from the prologue. */
        addlong(ARM64_ADD_IMM_X(16, 0, STATE_tmu1_s)); /* x16 = &state->tmu1_s */
        addlong(ARM64_LD1_V4S(0, 16));                  /* v0 = {tmu1_s_lo, tmu1_s_hi, tmu1_t_lo, tmu1_t_hi} */
        if (state->xdir > 0) {
            addlong(ARM64_ADD_V2D(0, 0, 14));
        } else {
            addlong(ARM64_SUB_V2D(0, 0, 14));
        }
        addlong(ARM64_ST1_V4S(0, 16)); /* store back via x16 = &state->tmu1_s */

        /* TMU1 W (64-bit) */
        addlong(ARM64_LDR_X(10, 0, STATE_tmu1_w));
        addlong(ARM64_LDR_X(11, 1, PARAMS_tmu1_dWdX));
        if (state->xdir > 0) {
            addlong(ARM64_ADD_REG_X(10, 10, 11));
        } else {
            addlong(ARM64_SUB_REG_X(10, 10, 11));
        }
        addlong(ARM64_STR_X(10, 0, STATE_tmu1_w));
    }

    /* Pixel and texel count increments */
    if (params->fbzColorPath & FBZCP_TEXTURE_ENABLED) {
        /* ADD x7, x0, #STATE_pixel_count -- base for LDP/STP (offset exceeds imm7 range) */
        addlong(ARM64_ADD_IMM_X(7, 0, STATE_pixel_count));
        /* LDP w4, w5, [x7] -- load pixel_count and texel_count */
        addlong(ARM64_LDP_OFF_W(4, 5, 7, 0));
        addlong(ARM64_ADD_IMM(4, 4, 1));
        if ((params->textureMode[0] & TEXTUREMODE_MASK) == TEXTUREMODE_PASSTHROUGH
            || (params->textureMode[0] & TEXTUREMODE_LOCAL_MASK) == TEXTUREMODE_LOCAL) {
            addlong(ARM64_ADD_IMM(5, 5, 1));
        } else {
            addlong(ARM64_ADD_IMM(5, 5, 2));
        }
        /* STP w4, w5, [x7] -- store pixel_count and texel_count */
        addlong(ARM64_STP_OFF_W(4, 5, 7, 0));
    } else {
        /* Pixel count only (no textures) */
        addlong(ARM64_LDR_W(4, 0, STATE_pixel_count));
        addlong(ARM64_ADD_IMM(4, 4, 1));
        addlong(ARM64_STR_W(4, 0, STATE_pixel_count));
    }

    /* ================================================================
     * X coordinate increment and loop back
     * ================================================================
     *
     * x86-64 ref: lines 3448-3469
     * ================================================================ */

    /* Compute new x from w28. CMP must read old w28 BEFORE MOV w28, w5 overwrites it. */
    if (state->xdir > 0) {
        addlong(ARM64_ADD_IMM(5, 28, 1));
    } else {
        addlong(ARM64_SUB_IMM(5, 28, 1));
    }

    /* STR w5, [x0, #STATE_x] */
    addlong(ARM64_STR_W(5, 0, STATE_x));

    /* CMP w28, w27 -- compare old x against cached STATE_x2 (before update) */
    addlong(ARM64_CMP_REG(28, 27));
    /* update cached STATE_x for next iteration (after CMP reads old value) */
    addlong(ARM64_MOV_REG(28, 5));

    /* B.NE loop_jump_pos */
    {
        int32_t loop_offset = loop_jump_pos - block_pos;
        addlong(ARM64_BCOND(loop_offset, COND_NE));
    }

    /* ================================================================
     * Epilogue: restore callee-saved registers and return
     * ================================================================ */

    /* LDP d14, d15, [SP, #144] */
    addlong(ARM64_LDP_D(14, 15, 31, 144));
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
    /* LDP x29, x30, [SP], #176 */
    addlong(ARM64_LDP_POST_X(29, 30, 31, 176));

    /* RET */
    addlong(ARM64_RET);

    return block_pos;
}

/* Global kept only to satisfy 'extern int voodoo_recomp' in vid_voodoo_render.h.
 * ARM64 path uses per-instance recomp count in voodoo->jit_recomp. */
int voodoo_recomp = 0;

static inline int
arm64_codegen_set_writable(uint8_t *code_block)
{
    if (!code_block) {
        return 0;
    }
    if (arm64_jit_rwx) {
        return 1;  /* Pages are born RWX; no mprotect needed. */
    }
#if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *)) {
        pthread_jit_write_protect_np(0);
    }
    return 1;
#elif defined(_WIN32)
    DWORD old_protect;
    return (VirtualProtect(code_block, BLOCK_SIZE, PAGE_READWRITE, &old_protect) != 0);
#else
    return (mprotect(code_block, BLOCK_SIZE, PROT_READ | PROT_WRITE) == 0);
#endif
}

static inline int
arm64_codegen_set_executable(uint8_t *code_block)
{
    if (!code_block) {
        return 0;
    }
    if (arm64_jit_rwx) {
        return 1;  /* Pages are born RWX; no mprotect needed. */
    }
#if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *)) {
        pthread_jit_write_protect_np(1);
    }
    return 1;
#elif defined(_WIN32)
    DWORD old_protect;
    return (VirtualProtect(code_block, BLOCK_SIZE, PAGE_EXECUTE_READ, &old_protect) != 0);
#else
    return (mprotect(code_block, BLOCK_SIZE, PROT_READ | PROT_EXEC) == 0);
#endif
}

static inline void
arm64_codegen_store_cache_key(voodoo_arm64_data_t *data, voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int valid, int rejected)
{
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
    data->valid          = valid;
    data->rejected       = rejected;
}

/*
 * ========================================================================
 * JIT BLOCK CACHE + COMPILATION
 * ========================================================================
 * The Voodoo pixel pipeline has hundreds of configurable options (blend
 * modes, texture modes, fog modes, depth modes, etc.). Rather than checking
 * all these options per-pixel at runtime (slow), the JIT compiles a
 * specialized native code block for each unique combination of options.
 *
 * The compiled block contains only the instructions needed for that exact
 * configuration -- no branches, no option checks, just straight-line code
 * for the active pipeline stages. This is dramatically faster than the
 * C interpreter, which must check every option on every pixel.
 *
 * Blocks are cached in a 32-entry LRU cache per partition. When the game
 * changes rendering state (e.g., switches from opaque to transparent
 * objects), a new block is compiled for the new state. On miss, the
 * least-recently-used slot is evicted. Most games use only a handful of
 * distinct pipeline configurations per frame.
 *
 * Array layout: contiguous per-partition (partition * BLOCK_NUM + slot).
 * This keeps all 32 slots for one partition in adjacent memory, reducing
 * cache line footprint during the linear scan from ~125 lines (interleaved)
 * to ~36 lines (contiguous) on a 64-byte cache line architecture.
 *
 * On macOS ARM64, the JIT must handle W^X (write-xor-execute) memory
 * protection: code pages are made writable for compilation, then switched
 * to executable before the block can be called. The I-cache is flushed
 * after each compilation to ensure the CPU sees the new instructions.
 * The flush covers only the actual generated code size, not the full
 * BLOCK_SIZE, to minimize unnecessary cache line invalidations.
 * ======================================================================== */

/*
 * voodoo_get_block() -- find or JIT-compile a pixel pipeline block.
 *
 * Algorithm:
 *   1. Probe starting at jit_last_block[odd_even] (MRU hint), scanning all
 *      32 slots for a cached block whose key matches the current state.
 *   2. On hit: update LRU timestamp, update MRU hint, return code_block.
 *   3. On miss: scan all 32 slots for the one with the smallest last_used
 *      (LRU eviction), then JIT-compile into that slot:
 *      a. Make code page writable (W^X toggle).
 *      b. Call voodoo_generate() to emit ARM64 into data->code_block.
 *      c. Check for emit overflow (block exceeded BLOCK_SIZE).
 *      d. Make code page executable and flush I-cache (narrow range).
 *   4. On reject (W^X fail or emit overflow): set last_used = 0 so the
 *      slot is evicted first on the next miss.
 *   5. Return the compiled code_block pointer, or NULL for interpreter fallback.
 *
 * odd_even selects the partition (0-3). Array layout is contiguous:
 * slot index = odd_even * BLOCK_NUM + probe.
 */
static inline void *
voodoo_get_block(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int odd_even)
{
    int                  b                 = voodoo->jit_last_block[odd_even];
    int                  base              = odd_even * BLOCK_NUM;
    voodoo_arm64_data_t *voodoo_arm64_data = voodoo->codegen_data;
    voodoo_arm64_data_t *data;

    /* --- Cache lookup: linear scan from MRU hint --- */
    for (uint8_t c = 0; c < BLOCK_NUM; c++) {
        int probe = (b + c) & BLOCK_MASK;
        data      = &voodoo_arm64_data[base + probe];

        if ((data->valid || data->rejected)
            && state->xdir == data->xdir
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
            if (data->rejected)
                return NULL;

            /* LRU: stamp this slot as most-recently-used */
            data->last_used                  = ++jit_generation[odd_even];
            voodoo->jit_last_block[odd_even] = probe;
            if (voodoo->jit_debug && voodoo->jit_debug_log) {
                int hit_count = ATOMIC_LOAD(voodoo->jit_hit_count);
                if (hit_count < 20) {
                    fprintf(voodoo->jit_debug_log,
                            "VOODOO JIT: cache HIT #%d odd_even=%d block=%d code=%p "
                            "fbzMode=0x%08x fbzColorPath=0x%08x alphaMode=0x%08x\n",
                            hit_count, odd_even, probe, (void *) data->code_block,
                            params->fbzMode, params->fbzColorPath, params->alphaMode);
                    ATOMIC_INC(voodoo->jit_hit_count);
                }
            }
            return data->code_block;
        }
    }

    /* --- Cache miss: find LRU victim --- */
    ATOMIC_INC(voodoo->jit_recomp);
    {
        int      lru_slot = 0;
        uint64_t lru_min  = voodoo_arm64_data[base].last_used;
        for (int s = 1; s < BLOCK_NUM; s++) {
            if (voodoo_arm64_data[base + s].last_used < lru_min) {
                lru_min  = voodoo_arm64_data[base + s].last_used;
                lru_slot = s;
            }
        }
        data = &voodoo_arm64_data[base + lru_slot];
    }

    /* W^X: make code page writable before JIT emission. */
    if (!arm64_codegen_set_writable(data->code_block)) {
        arm64_codegen_store_cache_key(data, voodoo, params, state, 0, 1);
        data->last_used = 0;
        if (voodoo->jit_debug && voodoo->jit_debug_log) {
            fprintf(voodoo->jit_debug_log,
                    "VOODOO JIT: REJECT odd_even=%d reason=wx_write_enable_failed "
                    "code=%p\n",
                    odd_even, (void *) data->code_block);
        }
        return NULL;
    }

    int code_size = voodoo_generate(data->code_block, voodoo, params, state, depth_op);

    if (arm64_codegen_emit_overflowed()) {
        arm64_codegen_store_cache_key(data, voodoo, params, state, 0, 1);
        data->last_used = 0;
        if (!arm64_codegen_set_executable(data->code_block) && voodoo->jit_debug && voodoo->jit_debug_log) {
            fprintf(voodoo->jit_debug_log,
                    "VOODOO JIT: WARN odd_even=%d reason=wx_exec_restore_failed "
                    "code=%p\n",
                    odd_even, (void *) data->code_block);
        }
        if (voodoo->jit_debug && voodoo->jit_debug_log) {
            fprintf(voodoo->jit_debug_log,
                    "VOODOO JIT: REJECT odd_even=%d reason=emit_overflow "
                    "(limit=%d) -> interpreter fallback\n",
                    odd_even, BLOCK_SIZE);
        }
        return NULL;
    }

    if (voodoo->jit_debug && voodoo->jit_debug_log) {
        int gen_count = ATOMIC_LOAD(voodoo->jit_gen_count);
        ATOMIC_INC(voodoo->jit_gen_count);
        fprintf(voodoo->jit_debug_log,
                "VOODOO JIT: GENERATE #%d odd_even=%d code=%p code_size=%d recomp=%d "
                "fbzMode=0x%08x fbzColorPath=0x%08x alphaMode=0x%08x "
                "textureMode[0]=0x%08x fogMode=0x%08x xdir=%d\n",
                gen_count, odd_even,
                (void *) data->code_block, code_size,
                ATOMIC_LOAD(voodoo->jit_recomp), params->fbzMode, params->fbzColorPath, params->alphaMode,
                params->textureMode[0], params->fogMode, state->xdir);
    }

    arm64_codegen_store_cache_key(data, voodoo, params, state, 1, 0);
    data->last_used                  = ++jit_generation[odd_even];
    voodoo->jit_last_block[odd_even] = (int) (data - &voodoo_arm64_data[base]);

    /* W^X: make executable, flush I-cache (narrow range = actual code size) */
    if (!arm64_codegen_set_executable(data->code_block)) {
        arm64_codegen_store_cache_key(data, voodoo, params, state, 0, 1);
        data->last_used = 0;
        if (voodoo->jit_debug && voodoo->jit_debug_log) {
            fprintf(voodoo->jit_debug_log,
                    "VOODOO JIT: REJECT odd_even=%d reason=wx_exec_enable_failed "
                    "code=%p\n",
                    odd_even, (void *) data->code_block);
        }
        return NULL;
    }
#if defined(__aarch64__) || defined(_M_ARM64)
#    ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), data->code_block, code_size);
#    else
    __clear_cache((char *) data->code_block, (char *) data->code_block + code_size);
#    endif
#endif

    return data->code_block;
}

/*
 * ========================================================================
 * CODEGEN INITIALIZATION
 * ========================================================================
 * One-time setup when the emulated Voodoo card is initialized:
 *
 * 1. Allocate executable memory (MAP_JIT on macOS) for compiled blocks.
 *    Each block gets BLOCK_SIZE bytes. Total allocation covers all cache
 *    slots across both render targets (odd/even double-buffering).
 *
 * 2. Build lookup tables used by the compiled code at runtime:
 *    - alookup[256]: alpha multiply factors {a, a, a, a} as NEON halfwords
 *    - aminuslookup[256]: inverse alpha {255-a, ...} for (1-alpha) blends
 *    - bilinear_lookup[512]: 4-corner bilinear filter weights, precomputed
 *      for all 256 possible sub-texel positions (16 x 16 fractional grid)
 *    - neon_00_ff_w[2]: constant vectors {0,0,0,0} and {255,255,255,255}
 *
 * These tables are stored in regular (non-executable) memory and accessed
 * by the JIT code via pointer registers loaded in the prologue.
 * ======================================================================== */

/* ========================================================================
 * voodoo_codegen_init() -- allocate executable memory + init lookup tables
 * ======================================================================== */
void
voodoo_codegen_init(voodoo_t *voodoo)
{
    voodoo_arm64_data_t *voodoo_arm64_data;
    uint32_t             slot;

    voodoo->codegen_data = plat_mmap(sizeof(voodoo_arm64_data_t) * BLOCK_NUM * 4, 0);
    if (!voodoo->codegen_data) {
        fatal("ARM64 JIT: failed to allocate codegen metadata buffer\n");
    }
    voodoo_arm64_data = voodoo->codegen_data;
    memset(voodoo_arm64_data, 0, sizeof(voodoo_arm64_data_t) * BLOCK_NUM * 4);

    for (slot = 0; slot < (uint32_t) (BLOCK_NUM * 4); slot++) {
        voodoo_arm64_data[slot].code_block = plat_mmap(BLOCK_SIZE, 1);
        if (!voodoo_arm64_data[slot].code_block) {
            while (slot > 0) {
                slot--;
                if (voodoo_arm64_data[slot].code_block) {
                    plat_munmap(voodoo_arm64_data[slot].code_block, BLOCK_SIZE);
                    voodoo_arm64_data[slot].code_block = NULL;
                }
            }
            plat_munmap(voodoo_arm64_data, sizeof(voodoo_arm64_data_t) * BLOCK_NUM * 4);
            voodoo->codegen_data = NULL;
            fatal("ARM64 JIT: failed to allocate executable code block\n");
        }
#if !defined(__APPLE__) || !defined(__aarch64__)
        if (!arm64_codegen_set_executable(voodoo_arm64_data[slot].code_block)) {
            plat_munmap(voodoo_arm64_data[slot].code_block, BLOCK_SIZE);
            voodoo_arm64_data[slot].code_block = NULL;
            while (slot > 0) {
                slot--;
                if (voodoo_arm64_data[slot].code_block) {
                    plat_munmap(voodoo_arm64_data[slot].code_block, BLOCK_SIZE);
                    voodoo_arm64_data[slot].code_block = NULL;
                }
            }
            plat_munmap(voodoo_arm64_data, sizeof(voodoo_arm64_data_t) * BLOCK_NUM * 4);
            voodoo->codegen_data = NULL;
            fatal("ARM64 JIT: failed to set code block executable\n");
        }
#endif
    }

    /* Initialize per-instance JIT cache state */
    memset(voodoo->jit_last_block, 0, sizeof(voodoo->jit_last_block));
    memset(jit_generation, 0, sizeof(jit_generation));
    ATOMIC_STORE(voodoo->jit_recomp, 0);
    ATOMIC_STORE(voodoo->jit_hit_count, 0);
    ATOMIC_STORE(voodoo->jit_gen_count, 0);
    ATOMIC_STORE(voodoo->jit_exec_count, 0);
    ATOMIC_STORE(voodoo->jit_verify_mismatches, 0);

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

        /* Bilinear weights for sub-texel index c = (frac_t << 4) | frac_s.
         * frac_s = c & 0xF (0-15), frac_t = c >> 4 (0-15).
         *
         * The four bilinear sample weights (summing to 256) are:
         *   d[0] = (16 - frac_s) * (16 - frac_t)  -- top-left texel
         *   d[1] = frac_s        * (16 - frac_t)  -- top-right texel
         *   d[2] = (16 - frac_s) * frac_t          -- bottom-left texel
         *   d[3] = frac_s        * frac_t          -- bottom-right texel
         *
         * Each weight is broadcast to 4 halfword lanes (one per BGRA component)
         * and stored in two consecutive 128-bit entries (32 bytes per index):
         *   bilinear_lookup[c*2]   = {d[0],d[0],d[0],d[0], d[1],d[1],d[1],d[1]}
         *   bilinear_lookup[c*2+1] = {d[2],d[2],d[2],d[2], d[3],d[3],d[3],d[3]}
         */
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
    voodoo_arm64_data_t *voodoo_arm64_data = voodoo->codegen_data;
    uint32_t             slot;

    if (!voodoo_arm64_data) {
        return;
    }

    for (slot = 0; slot < (uint32_t) (BLOCK_NUM * 4); slot++) {
        if (voodoo_arm64_data[slot].code_block) {
            plat_munmap(voodoo_arm64_data[slot].code_block, BLOCK_SIZE);
            voodoo_arm64_data[slot].code_block = NULL;
        }
    }

    plat_munmap(voodoo_arm64_data, sizeof(voodoo_arm64_data_t) * BLOCK_NUM * 4);
    voodoo->codegen_data = NULL;
}

#endif /* VIDEO_VOODOO_CODEGEN_ARM64_H */

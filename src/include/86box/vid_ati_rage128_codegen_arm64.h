/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- span JIT code generator for ARM64.
 *
 *          For each rage128_draw_state_t (the per-draw state decoded
 *          from the 3D registers) this file emits one function that
 *          rasterizes one scanline of a triangle. Every value that
 *          follows from the draw state is built into the code as an
 *          immediate; per-triangle values come in through r128_jit_tri_t
 *          and per-row values as arguments (r128_jit_span_fn in
 *          vid_ati_rage128.h).
 *
 *          The result must match the interpreter's pixel loop in
 *          vid_ati_rage128_3d.c bit for bit, so the emitted code repeats
 *          the C operation by operation: the same float or double width
 *          for each expression, the same order of additions, and no
 *          fused multiply-add (FMLA), because the interpreter is built
 *          without FMA contraction. The ARGB pack ORs the four shifted
 *          channel values as the C does, so a channel above 255 spills
 *          into the byte above it in both. Textured blocks either sample
 *          inline (vid_ati_rage128_codegen_arm64_tex.h, and the SoA loop
 *          of vid_ati_rage128_codegen_arm64_soa.h, which shades four
 *          pixels at a time with one pixel per vector lane) when their
 *          gates accept the state, or call the interpreter's own
 *          rage128_texstage_run for each pixel, with the live registers
 *          saved to the frame around the call. A state the generator
 *          cannot reproduce is refused and runs on the interpreter.
 *
 *          Included by vid_ati_rage128_jit.c on ARM64 builds and by the
 *          host JIT test harness (tests/video/rage128/jit-harness) on
 *          every host.
 *
 *          The emitted code is limited to base ARMv8.0-A with AdvSIMD.
 *          No instruction from v8.1 or later (LSE atomics, FP16
 *          arithmetic, dot product, ...) may be emitted.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 Software Development Guide",
 *              SDK-G04000 Rev 0.01, August 1999. Cited as "SDK: ...".
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEO_ATI_RAGE128_CODEGEN_ARM64_H
#define VIDEO_ATI_RAGE128_CODEGEN_ARM64_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <86box/vid_ati_rage128_codegen_rules.h>

/* ------------------------------------------------------------------------
 * Register map of the emitted span function. It is called through
 * r128_jit_span_fn, so AAPCS64 passes tri in x0, e0..e2 in x1..x3, drow,
 * zrow and py in w4..w6 and zline in d0. The prologue saves x19..x28.
 *
 *   x0  tri                           x14 zptr (staged Z base or 0)
 *   x1  e0 accumulator                w15 z_base
 *   x2  e1 accumulator                w19 z_lim
 *   x3  e2 accumulator                x20 cptr (staged color base or 0)
 *   w4  drow                          w21 c_base
 *   w5  zrow                          w22 c_lim
 *   w6  py, then the row's            w23 rx0 (-1 = nothing written)
 *       packed dither thresholds
 *   x7  e0dxi                         w24 rx1
 *   x8  e1dxi                         x16, x17, x25..x28 scratch; w17
 *   x9  e2dxi                             holds zi from the Z test to
 *   x10 vram base                         the Z write, x26 the Z cell
 *   w11 vram_mask                         and x27 the color cell
 *   w12 px                            w13 x1 (last column, inclusive)
 *
 *   d0  zline    s1 invs    d2 dZdx    d3 zc    d16 zq
 *   v4  vca      v5 vcb     v6 vcc
 *   s16..s18 barycentric weights w0/w1/w2
 *   v25 col {r,g,b,a}       v26, v27, v29..v31 scratch
 *   v28 255 in each 32-bit lane (dither clamp)
 *   d19 0.0     d20 1.0     d21 (double)zmax    d22 0.5
 *   v23 255.0f in all lanes v24 0.5f in all lanes
 *
 * x18 is never touched: it is the platform register on some ABIs. The
 * scalar path leaves v8-v15 alone; inline-texture and SoA blocks use
 * them and save d8-d15, the callee-saved low halves, in their own
 * prologue code.
 * ---------------------------------------------------------------------- */

#define R128_A64_BLOCK_SIZE 16384

typedef struct r128_a64_emit_t {
    uint8_t *base;
    int      pos;        /* bytes */
    int      overflow;   /* set when an emit would pass R128_A64_BLOCK_SIZE */
    int      tex_sub[2]; /* per-stage S3TC/YUV decode subroutine offset, or
                            -1. SoA blocks emit it once ahead of both loops
                            and the scalar texture stage calls the same
                            copy. */
    int soa_csub[2];     /* per-stage SoA coordinate/gather subroutine
                            offset, or -1. Heavy dual-stage blocks emit
                            each trilinear stage's filtered pipeline once
                            and call it with BL from pass A, pass B and
                            the nearest-texel pass of the chroma key. */
} r128_a64_emit_t;

static void
r128_a64_e32(r128_a64_emit_t *e, uint32_t insn)
{
    if (e->pos + 4 > R128_A64_BLOCK_SIZE) {
        e->overflow = 1;
        return;
    }
    memcpy(e->base + e->pos, &insn, 4);
    e->pos += 4;
}

/* ---- branches: emitted with a zero offset, patched once the target is
   known. Offsets are in instructions (bytes / 4). ---- */

static int
r128_a64_here(const r128_a64_emit_t *e)
{
    return e->pos;
}

/* B.cond, CBZ and CBNZ hold a signed 19-bit offset in bits [23:5]. */
static void
r128_a64_patch19(r128_a64_emit_t *e, int at, int target)
{
    uint32_t insn;
    int32_t  off = (target - at) >> 2;

    if (e->overflow)
        return;
    memcpy(&insn, e->base + at, 4);
    insn |= ((uint32_t) off & 0x7ffff) << 5;
    memcpy(e->base + at, &insn, 4);
}

static void
r128_a64_patch14(r128_a64_emit_t *e, int at, int target) /* TBZ/TBNZ: imm14 in [18:5] */
{
    uint32_t insn;
    int32_t  off = (target - at) >> 2;

    if (e->overflow)
        return;
    memcpy(&insn, e->base + at, 4);
    insn |= ((uint32_t) off & 0x3fff) << 5;
    memcpy(e->base + at, &insn, 4);
}

static void
r128_a64_patch26(r128_a64_emit_t *e, int at, int target) /* B: imm26 in [25:0] */
{
    uint32_t insn;
    int32_t  off = (target - at) >> 2;

    if (e->overflow)
        return;
    memcpy(&insn, e->base + at, 4);
    insn |= (uint32_t) off & 0x3ffffff;
    memcpy(e->base + at, &insn, 4);
}

/* ---- condition codes ---- */
#define A64_EQ 0
#define A64_NE 1
#define A64_HS 2
#define A64_LO 3
#define A64_MI 4
#define A64_HI 8
#define A64_LS 9
#define A64_GE 10
#define A64_LT 11
#define A64_GT 12
#define A64_LE 13

/* ---- instruction emitters (only what the generator needs) ---- */

/* Loads and stores with an unsigned immediate offset. off is in bytes
   and must be a multiple of the access size; imm12 holds it divided by
   that size. */
static void
r128_a64_ldr_x(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0xF9400000 | ((uint32_t) (off >> 3) << 10) | (rn << 5) | rt);
}

static void
r128_a64_ldr_w(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0xB9400000 | ((uint32_t) (off >> 2) << 10) | (rn << 5) | rt);
}

static void
r128_a64_ldp_w(r128_a64_emit_t *e, int rt1, int rt2, int rn, int off)
{
    r128_a64_e32(e, 0x29400000 | (((uint32_t) (off >> 2) & 0x7f) << 15) | (rt2 << 10) | (rn << 5) | rt1);
}

static void
r128_a64_ldr_s(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0xBD400000 | ((uint32_t) (off >> 2) << 10) | (rn << 5) | rt);
}

static void
r128_a64_ldr_d(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0xFD400000 | ((uint32_t) (off >> 3) << 10) | (rn << 5) | rt);
}

/* LDUR Qt: signed 9-bit byte offset with no scaling, so the offset need
   not be a multiple of 16 */
static void
r128_a64_ldur_q(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0x3CC00000 | (((uint32_t) off & 0x1ff) << 12) | (rn << 5) | rt);
}

static void
r128_a64_ldrh(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0x79400000 | ((uint32_t) (off >> 1) << 10) | (rn << 5) | rt);
}

static void
r128_a64_strh(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0x79000000 | ((uint32_t) (off >> 1) << 10) | (rn << 5) | rt);
}

/* LDRB Wt, [Xn, Xm]: register offset with option LSL and no shift
   (S = 0); the byte is zero-extended into Wt */
static void
r128_a64_ldrb_reg(r128_a64_emit_t *e, int rt, int rn, int rm)
{
    r128_a64_e32(e, 0x38606800 | (rm << 16) | (rn << 5) | rt);
}

static void
r128_a64_str_w(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0xB9000000 | ((uint32_t) (off >> 2) << 10) | (rn << 5) | rt);
}

static void
r128_a64_str_x(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0xF9000000 | ((uint32_t) (off >> 3) << 10) | (rn << 5) | rt);
}

static void
r128_a64_str_d(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0xFD000000 | ((uint32_t) (off >> 3) << 10) | (rn << 5) | rt);
}

static void
r128_a64_str_q(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0x3D800000 | ((uint32_t) (off >> 4) << 10) | (rn << 5) | rt);
}

static void
r128_a64_ldr_q(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0x3DC00000 | ((uint32_t) (off >> 4) << 10) | (rn << 5) | rt);
}

/* D and Q register pairs to and from SP, signed-offset form; imm7 is
   scaled by the register size */
static void
r128_a64_stp_d_sp(r128_a64_emit_t *e, int rt1, int rt2, int off)
{
    r128_a64_e32(e, 0x6D000000 | (((uint32_t) (off >> 3) & 0x7f) << 15) | (rt2 << 10) | (31 << 5) | rt1);
}

static void
r128_a64_ldp_d_sp(r128_a64_emit_t *e, int rt1, int rt2, int off)
{
    r128_a64_e32(e, 0x6D400000 | (((uint32_t) (off >> 3) & 0x7f) << 15) | (rt2 << 10) | (31 << 5) | rt1);
}

static void
r128_a64_stp_q_sp(r128_a64_emit_t *e, int rt1, int rt2, int off)
{
    r128_a64_e32(e, 0xAD000000 | (((uint32_t) (off >> 4) & 0x7f) << 15) | (rt2 << 10) | (31 << 5) | rt1);
}

static void
r128_a64_ldp_q_sp(r128_a64_emit_t *e, int rt1, int rt2, int off)
{
    r128_a64_e32(e, 0xAD400000 | (((uint32_t) (off >> 4) & 0x7f) << 15) | (rt2 << 10) | (31 << 5) | rt1);
}

/* X register pairs at SP: STP pre-index (frame allocate), the plain
   signed-offset STP and LDP, and LDP post-index (frame release). imm7
   is scaled by 8, so every form reaches -512..504 bytes. */
static void
r128_a64_stp_x_pre(r128_a64_emit_t *e, int rt1, int rt2, int frame)
{
    r128_a64_e32(e, 0xA9800000 | ((((uint32_t) (-frame / 8)) & 0x7f) << 15) | (rt2 << 10) | (31 << 5) | rt1);
}

static void
r128_a64_stp_x(r128_a64_emit_t *e, int rt1, int rt2, int off)
{
    r128_a64_e32(e, 0xA9000000 | (((uint32_t) (off >> 3) & 0x7f) << 15) | (rt2 << 10) | (31 << 5) | rt1);
}

static void
r128_a64_ldp_x(r128_a64_emit_t *e, int rt1, int rt2, int off)
{
    r128_a64_e32(e, 0xA9400000 | (((uint32_t) (off >> 3) & 0x7f) << 15) | (rt2 << 10) | (31 << 5) | rt1);
}

static void
r128_a64_ldp_x_post(r128_a64_emit_t *e, int rt1, int rt2, int frame)
{
    r128_a64_e32(e, 0xA8C00000 | (((uint32_t) (frame / 8) & 0x7f) << 15) | (rt2 << 10) | (31 << 5) | rt1);
}

/* Integer arithmetic and logic. ADD, CMP and CMN immediates are the
   unshifted 12-bit form, 0..4095. MOV and MVN are ORR and ORN with WZR
   as the first source; CMP and CMN are SUBS and ADDS with WZR as the
   destination. */
static void
r128_a64_add_x_reg(r128_a64_emit_t *e, int rd, int rn, int rm)
{
    r128_a64_e32(e, 0x8B000000 | (rm << 16) | (rn << 5) | rd);
}

static void
r128_a64_add_w_lsl(r128_a64_emit_t *e, int rd, int rn, int rm, int sh)
{
    r128_a64_e32(e, 0x0B000000 | (rm << 16) | (sh << 10) | (rn << 5) | rd);
}

static void
r128_a64_add_x_uxtw(r128_a64_emit_t *e, int rd, int rn, int rm)
{
    r128_a64_e32(e, 0x8B204000 | (rm << 16) | (rn << 5) | rd);
}

static void
r128_a64_add_w_imm(r128_a64_emit_t *e, int rd, int rn, int imm)
{
    r128_a64_e32(e, 0x11000000 | ((uint32_t) imm << 10) | (rn << 5) | rd);
}

static void
r128_a64_add_x_imm(r128_a64_emit_t *e, int rd, int rn, int imm)
{
    r128_a64_e32(e, 0x91000000 | ((uint32_t) imm << 10) | (rn << 5) | rd);
}

static void
r128_a64_cmp_w_imm(r128_a64_emit_t *e, int rn, int imm)
{
    r128_a64_e32(e, 0x7100001F | ((uint32_t) imm << 10) | (rn << 5));
}

static void
r128_a64_sub_w_reg(r128_a64_emit_t *e, int rd, int rn, int rm)
{
    r128_a64_e32(e, 0x4B000000 | (rm << 16) | (rn << 5) | rd);
}

static void
r128_a64_cmp_w_reg(r128_a64_emit_t *e, int rn, int rm)
{
    r128_a64_e32(e, 0x6B00001F | (rm << 16) | (rn << 5));
}

static void
r128_a64_cmn_w_imm(r128_a64_emit_t *e, int rn, int imm)
{
    r128_a64_e32(e, 0x3100001F | ((uint32_t) imm << 10) | (rn << 5));
}

static void
r128_a64_orr_x_lsl(r128_a64_emit_t *e, int rd, int rn, int rm, int sh)
{
    r128_a64_e32(e, 0xAA000000 | (rm << 16) | (sh << 10) | (rn << 5) | rd);
}

static void
r128_a64_orr_w_lsl(r128_a64_emit_t *e, int rd, int rn, int rm, int sh)
{
    r128_a64_e32(e, 0x2A000000 | (rm << 16) | (sh << 10) | (rn << 5) | rd);
}

static void
r128_a64_and_w_reg(r128_a64_emit_t *e, int rd, int rn, int rm)
{
    r128_a64_e32(e, 0x0A000000 | (rm << 16) | (rn << 5) | rd);
}

static void
r128_a64_mov_w(r128_a64_emit_t *e, int rd, int rm)
{
    r128_a64_e32(e, 0x2A0003E0 | (rm << 16) | rd);
}

static void
r128_a64_mvn_w(r128_a64_emit_t *e, int rd, int rm)
{
    r128_a64_e32(e, 0x2A2003E0 | (rm << 16) | rd); /* ORN wd, wzr, wm */
}

static void
r128_a64_movz_w(r128_a64_emit_t *e, int rd, uint32_t imm16, int hw)
{
    r128_a64_e32(e, 0x52800000 | ((uint32_t) hw << 21) | (imm16 << 5) | rd);
}

static void
r128_a64_movk_w(r128_a64_emit_t *e, int rd, uint32_t imm16, int hw)
{
    r128_a64_e32(e, 0x72800000 | ((uint32_t) hw << 21) | (imm16 << 5) | rd);
}

static void
r128_a64_movn_x(r128_a64_emit_t *e, int rd, uint32_t imm16)
{
    r128_a64_e32(e, 0x92800000 | (imm16 << 5) | rd);
}

static void
r128_a64_bic_w_reg(r128_a64_emit_t *e, int rd, int rn, int rm)
{
    r128_a64_e32(e, 0x0A200000 | (rm << 16) | (rn << 5) | rd);
}

static void
r128_a64_movn_w(r128_a64_emit_t *e, int rd, uint32_t imm16)
{
    r128_a64_e32(e, 0x12800000 | (imm16 << 5) | rd);
}

/* Load any 64-bit constant into Xd: MOVZ for bits [15:0], then one MOVK
   for each higher 16-bit chunk that is not zero */
static void
r128_a64_mov_x_imm64(r128_a64_emit_t *e, int rd, uint64_t v)
{
    r128_a64_e32(e, 0xD2800000 | ((uint32_t) (v & 0xffff) << 5) | rd);
    if ((v >> 16) & 0xffff)
        r128_a64_e32(e, 0xF2800000 | (1u << 21) | ((uint32_t) ((v >> 16) & 0xffff) << 5) | rd);
    if ((v >> 32) & 0xffff)
        r128_a64_e32(e, 0xF2800000 | (2u << 21) | ((uint32_t) ((v >> 32) & 0xffff) << 5) | rd);
    if ((v >> 48) & 0xffff)
        r128_a64_e32(e, 0xF2800000 | (3u << 21) | ((uint32_t) ((v >> 48) & 0xffff) << 5) | rd);
}

/* LSL, LSR and UBFX are aliases of UBFM, where immr is the right
   rotation and imms the top bit of the source field */
static void
r128_a64_lsl_w(r128_a64_emit_t *e, int rd, int rn, int sh)
{
    r128_a64_e32(e, 0x53000000 | ((uint32_t) ((32 - sh) & 31) << 16) | ((uint32_t) (31 - sh) << 10) | (rn << 5) | rd);
}

static void
r128_a64_lsr_w(r128_a64_emit_t *e, int rd, int rn, int sh)
{
    r128_a64_e32(e, 0x53000000 | ((uint32_t) sh << 16) | (31u << 10) | (rn << 5) | rd);
}

static void
r128_a64_ubfx_w(r128_a64_emit_t *e, int rd, int rn, int lsb, int width)
{
    r128_a64_e32(e, 0x53000000 | ((uint32_t) lsb << 16) | ((uint32_t) (lsb + width - 1) << 10) | (rn << 5) | rd);
}

static void
r128_a64_csel_w(r128_a64_emit_t *e, int rd, int rn, int rm, int cond)
{
    r128_a64_e32(e, 0x1A800000 | (rm << 16) | (cond << 12) | (rn << 5) | rd);
}

static void
r128_a64_csel_x(r128_a64_emit_t *e, int rd, int rn, int rm, int cond)
{
    r128_a64_e32(e, 0x9A800000 | (rm << 16) | (cond << 12) | (rn << 5) | rd);
}

static void
r128_a64_cset_w(r128_a64_emit_t *e, int rd, int cond)
{
    /* CSET rd, cond is CSINC rd, WZR, WZR with the inverted condition.
       Flipping bit 0 of a condition code inverts it (EQ and NE, HS and
       LO, ...). */
    r128_a64_e32(e, 0x1A9F07E0 | (((uint32_t) (cond ^ 1) & 0xf) << 12) | rd);
}

static void
r128_a64_lsrv_w(r128_a64_emit_t *e, int rd, int rn, int rm)
{
    r128_a64_e32(e, 0x1AC02400 | (rm << 16) | (rn << 5) | rd);
}

static void
r128_a64_lsr_x(r128_a64_emit_t *e, int rd, int rn, int sh)
{
    r128_a64_e32(e, 0xD3400000 | ((uint32_t) sh << 16) | (63u << 10) | (rn << 5) | rd);
}

/* Logical-immediate encoding for the 32-bit AND and TST below. The
   search covers a run of 1 to 31 set bits rotated right by 0 to 31 and
   returns the rotation as immr and the run length minus one as imms.
   Patterns that repeat a smaller element, such as 0x00ff00ff, are
   encodable on ARM64 but not found here; 0 and 0xffffffff have no
   encoding at all. Returns 0 when no encoding is found. */
static int
r128_a64_bitmask32(uint32_t v, uint32_t *immr_out, uint32_t *imms_out)
{
    for (uint32_t len = 1; len <= 31; len++) {
        uint32_t ones = (len == 32) ? 0xffffffffu : ((1u << len) - 1);

        for (uint32_t rot = 0; rot < 32; rot++) {
            uint32_t cand = rot ? ((ones >> rot) | (ones << (32 - rot))) : ones;

            if (cand == v) {
                *immr_out = rot;
                *imms_out = len - 1;
                return 1;
            }
        }
    }
    return 0;
}

static void
r128_a64_and_w_bitmask(r128_a64_emit_t *e, int rd, int rn, uint32_t v)
{
    uint32_t immr, imms;

    if (!r128_a64_bitmask32(v, &immr, &imms)) {
        e->overflow = 1; /* a gate should have refused this state; the
                            failed block leaves it on the interpreter */
        return;
    }
    r128_a64_e32(e, 0x12000000 | (immr << 16) | (imms << 10) | (rn << 5) | rd);
}

/* Branches, emitted with a zero offset. Each returns its own position
   for the patch functions above. */
static int
r128_a64_b(r128_a64_emit_t *e)
{
    int at = e->pos;

    r128_a64_e32(e, 0x14000000);
    return at;
}

static int
r128_a64_bcond(r128_a64_emit_t *e, int cond)
{
    int at = e->pos;

    r128_a64_e32(e, 0x54000000 | cond);
    return at;
}

static int
r128_a64_cbz_x(r128_a64_emit_t *e, int rt)
{
    int at = e->pos;

    r128_a64_e32(e, 0xB4000000 | rt);
    return at;
}

static int
r128_a64_cbz_w(r128_a64_emit_t *e, int rt)
{
    int at = e->pos;

    r128_a64_e32(e, 0x34000000 | rt);
    return at;
}

/* TBNZ Xt on bit 63 (b5 = 1, b40 = 31): taken when Xt is negative */
static int
r128_a64_tbnz63(r128_a64_emit_t *e, int rt)
{
    int at = e->pos;

    r128_a64_e32(e, 0xB7000000 | (31u << 19) | rt);
    return at;
}

static int
r128_a64_tbz(r128_a64_emit_t *e, int rt, int bit) /* bit < 32: b5 stays 0 */
{
    int at = e->pos;

    r128_a64_e32(e, 0x36000000 | ((uint32_t) bit << 19) | rt);
    return at;
}

/* Load a 14-bit signed coordinate (-8192..8191) in one instruction:
   MOVZ for a value of zero or more, MOVN of the complement otherwise */
static void
r128_a64_mov_w_s14(r128_a64_emit_t *e, int rd, int32_t v)
{
    if (v >= 0)
        r128_a64_movz_w(e, rd, (uint32_t) v, 0);
    else
        r128_a64_movn_w(e, rd, (uint32_t) ~v & 0xffff);
}

static void
r128_a64_b_to(r128_a64_emit_t *e, int target)
{
    int at = r128_a64_b(e);

    r128_a64_patch26(e, at, target);
}

static void
r128_a64_ret(r128_a64_emit_t *e)
{
    r128_a64_e32(e, 0xD65F03C0);
}

static void
r128_a64_blr(r128_a64_emit_t *e, int rn)
{
    r128_a64_e32(e, 0xD63F0000 | (rn << 5));
}

/* FP / NEON */
static void
r128_a64_scvtf_s_x(r128_a64_emit_t *e, int sd, int xn)
{
    r128_a64_e32(e, 0x9E220000 | (xn << 5) | sd);
}

static void
r128_a64_fmul_s(r128_a64_emit_t *e, int sd, int sn, int sm)
{
    r128_a64_e32(e, 0x1E200800 | (sm << 16) | (sn << 5) | sd);
}

static void
r128_a64_fmul_d(r128_a64_emit_t *e, int dd, int dn, int dm)
{
    r128_a64_e32(e, 0x1E600800 | (dm << 16) | (dn << 5) | dd);
}

static void
r128_a64_fadd_d(r128_a64_emit_t *e, int dd, int dn, int dm)
{
    r128_a64_e32(e, 0x1E602800 | (dm << 16) | (dn << 5) | dd);
}

static void
r128_a64_fcmp_d0(r128_a64_emit_t *e, int dn)
{
    r128_a64_e32(e, 0x1E602008 | (dn << 5));
}

static void
r128_a64_fcmp_d(r128_a64_emit_t *e, int dn, int dm)
{
    r128_a64_e32(e, 0x1E602000 | (dm << 16) | (dn << 5));
}

static void
r128_a64_fcsel_d(r128_a64_emit_t *e, int dd, int dn, int dm, int cond)
{
    r128_a64_e32(e, 0x1E600C00 | (dm << 16) | (cond << 12) | (dn << 5) | dd);
}

static void
r128_a64_fcvtzu_w_d(r128_a64_emit_t *e, int wd, int dn)
{
    r128_a64_e32(e, 0x1E790000 | (dn << 5) | wd);
}

static void
r128_a64_fcvtzu_x_d(r128_a64_emit_t *e, int xd, int dn)
{
    r128_a64_e32(e, 0x9E790000 | (dn << 5) | xd);
}

/* xd = cond ? xn : xm + 1 */
static void
r128_a64_csinc_x(r128_a64_emit_t *e, int xd, int xn, int xm, int cond)
{
    r128_a64_e32(e, 0x9A800400 | (xm << 16) | (cond << 12) | (xn << 5) | xd);
}

static void
r128_a64_fmov_d_x(r128_a64_emit_t *e, int dd, int xn)
{
    r128_a64_e32(e, 0x9E670000 | (xn << 5) | dd);
}

static void
r128_a64_fmov_s_w(r128_a64_emit_t *e, int sd, int wn)
{
    r128_a64_e32(e, 0x1E270000 | (wn << 5) | sd);
}

static void
r128_a64_fmov_s_s(r128_a64_emit_t *e, int sd, int sn)
{
    r128_a64_e32(e, 0x1E204000 | (sn << 5) | sd);
}

static void
r128_a64_fadd_s(r128_a64_emit_t *e, int sd, int sn, int sm)
{
    r128_a64_e32(e, 0x1E202800 | (sm << 16) | (sn << 5) | sd);
}

static void
r128_a64_fcvtzu_w_s(r128_a64_emit_t *e, int wd, int sn)
{
    r128_a64_e32(e, 0x1E390000 | (sn << 5) | wd);
}

/* FMOV with the 8-bit floating-point immediate: 0x70 is 1.0, 0x60 is
   0.5 */
static void
r128_a64_fmov_d_imm(r128_a64_emit_t *e, int dd, uint32_t imm8)
{
    r128_a64_e32(e, 0x1E601000 | (imm8 << 13) | dd);
}

static void
r128_a64_fmov_s_imm(r128_a64_emit_t *e, int sd, uint32_t imm8)
{
    r128_a64_e32(e, 0x1E201000 | (imm8 << 13) | sd);
}

static void
r128_a64_dup_4s_lane0(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x4E040400 | (vn << 5) | vd);
}

static void
r128_a64_dup_4s_lane(r128_a64_emit_t *e, int vd, int vn, int lane)
{
    r128_a64_e32(e, 0x4E000400 | ((uint32_t) ((lane << 3) | 4) << 16) | (vn << 5) | vd);
}

static void
r128_a64_mov_16b(r128_a64_emit_t *e, int vd, int vn) /* ORR Vd.16B, Vn.16B, Vn.16B */
{
    r128_a64_e32(e, 0x4EA01C00 | (vn << 16) | (vn << 5) | vd);
}

static void
r128_a64_and_16b(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4E201C00 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_fsub_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4EA0D400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_fmax_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4E20F400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_fmin_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4EA0F400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_fdiv_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6E20FC00 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_ucvtf_4s(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x6E21D800 | (vn << 5) | vd);
}

static void
r128_a64_fcvtns_4s(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x4E21A800 | (vn << 5) | vd);
}

static void
r128_a64_fcmp_s(r128_a64_emit_t *e, int sn, int sm)
{
    r128_a64_e32(e, 0x1E202000 | (sm << 16) | (sn << 5));
}

static void
r128_a64_fcmp_s0(r128_a64_emit_t *e, int sn)
{
    r128_a64_e32(e, 0x1E202008 | (sn << 5));
}

static void
r128_a64_fcsel_s(r128_a64_emit_t *e, int sd, int sn, int sm, int cond)
{
    r128_a64_e32(e, 0x1E200C00 | (sm << 16) | (cond << 12) | (sn << 5) | sd);
}

static void
r128_a64_fsub_s(r128_a64_emit_t *e, int sd, int sn, int sm)
{
    r128_a64_e32(e, 0x1E203800 | (sm << 16) | (sn << 5) | sd);
}

static void
r128_a64_movi_4s_zero(r128_a64_emit_t *e, int vd)
{
    r128_a64_e32(e, 0x4F000400 | vd); /* all-zero lanes == +0.0f */
}

static void
r128_a64_ins_elem_s(r128_a64_emit_t *e, int vd, int dlane, int vn, int slane)
{
    r128_a64_e32(e, 0x6E000400 | ((uint32_t) ((dlane << 3) | 4) << 16) | ((uint32_t) (slane << 2) << 11) | (vn << 5) | vd);
}

/* Blend factor vector for one ALPHA_BLND_SRC or ALPHA_BLND_DST code
   (RRG: MISC_3D_STATE_CNTL_REG, pp. 3-258-3-259 / PDF 276-277), the
   same values r3d_blend_factor produces. vd is built from col (v25),
   the unpacked destination dc (v29) and 1.0f in all lanes (v27); code
   0xa also uses v26. Codes 0xb and 0xc set both factors at once and
   are handled by r128_a64_gen_dcell_blend. Any other code without a
   case here, reserved ones included, gives ONE, as in the C. */
static void
r128_a64_emit_blend_factor(r128_a64_emit_t *e, int vd, uint32_t code)
{
    switch (code & 0xf) {
        case 0x0:
            r128_a64_movi_4s_zero(e, vd);
            break;
        case 0x2:
            r128_a64_mov_16b(e, vd, 25);
            break;
        case 0x3:
            r128_a64_fsub_4s(e, vd, 27, 25);
            break;
        case 0x4:
            r128_a64_dup_4s_lane(e, vd, 25, 3);
            break;
        case 0x5:
            r128_a64_dup_4s_lane(e, vd, 25, 3);
            r128_a64_fsub_4s(e, vd, 27, vd);
            break;
        case 0x6:
            r128_a64_dup_4s_lane(e, vd, 29, 3);
            break;
        case 0x7:
            r128_a64_dup_4s_lane(e, vd, 29, 3);
            r128_a64_fsub_4s(e, vd, 27, vd);
            break;
        case 0x8:
            r128_a64_mov_16b(e, vd, 29);
            break;
        case 0x9:
            r128_a64_fsub_4s(e, vd, 27, 29);
            break;
        case 0xa:
            /* BLEND_SRCALPHASAT, valid for source and destination:
               {f,f,f,1} with f = sc[3] < 1-dc[3] ? sc[3] : 1-dc[3].
               FCMP and FCSEL on MI make the same choice as the C
               ternary, including when an operand is NaN. */
            r128_a64_dup_4s_lane(e, vd, 29, 3);
            r128_a64_fsub_4s(e, vd, 27, vd);    /* 1-dc3 splat  */
            r128_a64_dup_4s_lane(e, 26, 25, 3); /* sc3 splat    */
            r128_a64_fcmp_s(e, 26, vd);
            r128_a64_fcsel_s(e, vd, 26, vd, A64_MI);
            r128_a64_dup_4s_lane0(e, vd, vd);
            r128_a64_ins_elem_s(e, vd, 3, 27, 0);
            break;
        case 0x1:
        default:
            r128_a64_mov_16b(e, vd, 27);
            break;
    }
}

static void
r128_a64_fmul_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6E20DC00 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_fadd_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4E20D400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_fcvtzu_4s(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x6EA1B800 | (vn << 5) | vd);
}

static void
r128_a64_umov_w_s(r128_a64_emit_t *e, int wd, int vn, int lane)
{
    r128_a64_e32(e, 0x0E003C00 | ((uint32_t) ((lane << 3) | 4) << 16) | (vn << 5) | wd);
}

static void
r128_a64_ins_s_w(r128_a64_emit_t *e, int vd, int lane, int wn)
{
    r128_a64_e32(e, 0x4E001C00 | ((uint32_t) ((lane << 3) | 4) << 16) | (wn << 5) | vd);
}

static void
r128_a64_add_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4EA08400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_umin_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6EA06C00 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_movi_4s_imm8(r128_a64_emit_t *e, int vd, uint32_t imm8)
{
    r128_a64_e32(e, 0x4F000400 | (((imm8 >> 5) & 7) << 16) | ((imm8 & 31) << 5) | vd);
}

static void
r128_a64_tst_w_imm(r128_a64_emit_t *e, int rn, uint32_t v)
{
    uint32_t immr, imms;

    if (!r128_a64_bitmask32(v, &immr, &imms)) {
        e->overflow = 1;
        return;
    }
    r128_a64_e32(e, 0x7200001F | (immr << 16) | (imms << 10) | (rn << 5));
}

/* The inline texture stage: its gate (r128_a64_texinline_can) and its
   emitters, which use the base emitters above. */
#include <86box/vid_ati_rage128_codegen_arm64_tex.h>

/* Compare-function codes as r3d_cmp(fn, a, b) implements them, one code
   book for the Z test, the stencil test and the alpha test (SDK: Table
   6-23, p. 6-55 / PDF 167; SDK: Table 6-25, p. 6-56 / PDF 168; RRG:
   MISC_3D_STATE_CNTL_REG, p. 3-259 / PDF 277, ALPHA_TEST_OP). This table
   gives, for CMP a, b, the unsigned branch condition that is true when
   the test fails. Codes 0 (never) and 7 (always) have no condition; the
   callers handle them. */
static const int r128_a64_cmp_inv[8] = {
    /* 0 never  */ 0,
    /* 1 a<b    */ A64_HS,
    /* 2 a<=b   */ A64_HI,
    /* 3 a==b   */ A64_NE,
    /* 4 a>=b   */ A64_LO,
    /* 5 a>b    */ A64_LS,
    /* 6 a!=b   */ A64_EQ,
    /* 7 always */ 0
};

/* The same code book as the condition that is true when the test
   passes, for CSET after CMP a, b. Stencil blocks need the depth and
   stencil results as values, not branches, because the discard waits
   until after the alpha test. Codes 0 and 7 are loaded as the constants
   0 and 1 by the callers. */
static const int r128_a64_cmp_fwd[8] = {
    /* 0 never  */ 0,
    /* 1 a<b    */ A64_LO,
    /* 2 a<=b   */ A64_LS,
    /* 3 a==b   */ A64_EQ,
    /* 4 a>=b   */ A64_HS,
    /* 5 a>b    */ A64_HI,
    /* 6 a!=b   */ A64_NE,
    /* 7 always */ 0
};

/* Apply one stencil operation code (Z_STEN_CNTL_C STEN_SFAIL_OP,
   STEN_ZPASS_OP or STEN_ZFAIL_OP) to the stencil byte in rs, result in
   rd, as the switch in the interpreter's stencil update does. The SDK
   lists codes 0 to 5 (SDK: Table 6-26, p. 6-57 / PDF 169); the
   interpreter treats 6 and 7 as increment and decrement with wrap. Bits
   above 7 of rd do not matter: the write-mask merge that follows keeps
   only bits under swmask, which is 8 bits wide, so the wrapping ops need
   no AND with 0xff and INVERT may leave the upper bits set. op and sref
   are state constants. */
static void
r128_a64_emit_sten_op(r128_a64_emit_t *e, int rd, int rs, uint32_t op, uint32_t sref)
{
    switch (op & 7) {
        case 0: /* KEEP    */
            r128_a64_mov_w(e, rd, rs);
            break;
        case 1: /* ZERO    */
            r128_a64_movz_w(e, rd, 0, 0);
            break;
        case 2: /* REPLACE */
            r128_a64_mov_w_imm32(e, rd, sref & 0xff);
            break;
        case 3: /* INCR sat: rs==0xff ? 0xff : rs+1 */
            r128_a64_add_w_imm(e, rd, rs, 1);
            r128_a64_cmp_w_imm(e, rs, 0xff);
            r128_a64_csel_w(e, rd, rs, rd, A64_EQ);
            break;
        case 4: /* DECR sat: rs==0 ? 0 : rs-1 */
            r128_a64_cmp_w_imm(e, rs, 0);
            r128_a64_sub_w_imm(e, rd, rs, 1);
            r128_a64_csel_w(e, rd, 31, rd, A64_EQ); /* wzr when rs==0 */
            break;
        case 5: /* INVERT   */
            r128_a64_mvn_w(e, rd, rs);
            break;
        case 6: /* INCR wrap */
            r128_a64_add_w_imm(e, rd, rs, 1);
            break;
        default: /* 7 DECR wrap */
            r128_a64_sub_w_imm(e, rd, rs, 1);
            break;
    }
}

/* A state where no pixel can be written: see r128_jit_never_pass in
   vid_ati_rage128_codegen_rules.h. */
static int
r128_a64_never_pass(const rage128_draw_state_t *ds)
{
    return r128_jit_never_pass(ds);
}

/* ------------------------------------------------------------------------
 * Coverage gate: the draw states this generator reproduces exactly. The
 * rest run on the interpreter. r128_jit_state_can is the part shared
 * with the x86-64 generator; this adds one ARM64 limit, on the 32-bit Z
 * write.
 * ---------------------------------------------------------------------- */
static int
r128_jit_arm64_can(const rage128_draw_state_t *ds)
{
    uint32_t immr, imms, zwrmask;

    if (!r128_jit_state_can(ds))
        return 0;
    if (r128_a64_never_pass(ds))
        return 1;
    /* A never-pass state gets the empty block in
       r128_jit_arm64_generate_1, so the Z check below does not apply to
       it. The generator covers Gouraud shading, Z, stencil, dither for
       16-bit destinations (8888 has nothing to dither, as in
       r3d_dst_write), alpha blending with every factor code and
       ALPHA_COMB_FCN, both texture
       stages (inline, or by calling the interpreter's
       rage128_texstage_run), specular, vertex and table fog, the alpha
       test, the auxiliary scissors and a partial PLANE_3D_MASK, which
       merges in the store. The block reads the fog table through
       r128_jit_tri_t.fog_table at run time, so the code depends on
       FOG_TABLE_EN but not on the table contents.

       The 32-bit Z write keeps the cell bits outside zmax << zshift with
       an AND, and that keep mask must have a logical-immediate encoding
       (r128_a64_bitmask32). An empty keep mask needs none: the write is
       then a plain store. */
    if (ds->z_en && ds->zbpp == 4) {
        /* the keep mask of the 32-bit Z read-modify-write */
        zwrmask = ~(ds->zmax << ds->zshift);
        if (zwrmask != 0 && !r128_a64_bitmask32(zwrmask, &immr, &imms))
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * Frame layout of the scalar block, offsets from SP. 0..95 holds x29,
 * x30 and x19..x28. 96..255 is used by blocks that call
 * rage128_texstage_run: the per-row constants are saved once before the
 * pixel loop and the per-pixel state around each call. Inline-texture
 * and SoA blocks give 96..255 their own layouts (the _tex.h and _soa.h
 * headers). R128_A64_SP_AUX holds the auxiliary-scissor row mask in
 * every block that tests those scissors.
 * ---------------------------------------------------------------------- */
#define R128_A64_SP_TRI  96  /* stp x0, x4: tri + drow (per row)          */
#define R128_A64_SP_ROW  112 /* stp x5, x6: zrow + dither row (per row)   */
#define R128_A64_SP_D19  128 /* stp d19, d20: 0.0 / 1.0 (per row, z_on)   */
#define R128_A64_SP_D21  144 /* stp d21, d22: zmax / 0.5 (per row, z_on)  */
#define R128_A64_SP_Q23  160 /* stp q23, q24: 255.0f / 0.5f splats        */
#define R128_A64_SP_COL  192 /* col vector: helper input/output           */
#define R128_A64_SP_E01  208 /* stp x1, x2: e0, e1                        */
#define R128_A64_SP_E2PX 224 /* stp x3, x12: e2, px                       */
#define R128_A64_SP_ZI   240 /* w17: zi (z_on && z_wr)                    */
#define R128_A64_SP_ZL   248 /* d0: zline (z_on)                          */
#define R128_A64_SP_AUX  256 /* w16: aux rect y-active mask (aux_on)      */
#define R128_A64_SP_STIP 260
#define R128_A64_FRAME   272

/* Stencil blocks (sten_on) keep one more word at sten_off, the top of
   the base frame (R128_A64_FRAME, or R128_A64_FRAME_TI for inline
   texture), and r128_a64_gen_setup adds 16 bytes to the frame for it.
   The word carries {sbuf | sres<<8 | zres<<9} from the Z and stencil
   tests to the stencil update after the alpha test. Blocks without
   stencil do not reserve it. */

/* The SoA row loop: its gates and emitters. It uses the base emitters
   and frame slots above; untextured SoA blocks, which never make the
   texture call, put the d8-d15 save and the masked-store bounce in that
   call's spill slots. */
#include <86box/vid_ati_rage128_codegen_arm64_soa.h>

/* ------------------------------------------------------------------------
 * Generator: emit the span function for ds into code, a buffer of
 * R128_A64_BLOCK_SIZE bytes. r128_jit_arm64_generate_1 returns the length
 * in bytes, -2 for a refused state or -1 when the buffer overflowed.
 * no_soa forces a block without the SoA loop (the overflow retry in
 * r128_jit_arm64_generate). Each phase of the block has its own static
 * emitter below; they share state through r128_a64_gen_t and
 * r128_jit_arm64_generate_1 runs them in order.
 * ---------------------------------------------------------------------- */
typedef struct {
    r128_a64_emit_t             e;
    const rage128_draw_state_t *ds;
    int                         bppsh, z_on, z_step, sten_on, dith_on, tex_on;
    int                         tex_inline, soa_on;
    int                         soa_tri, soa_split, soa_tri2, soa_split2;
    int                         soa_dsub;
    int                         frame, sten_off;
    int                         aux_on, aux_add_mask, aux_row_out;
    int                         bails[12], nbail;
    int                         skips[24], nskip; /* forward branches to pix_skip */
    int                         loop_head, b_done;
} r128_a64_gen_t;

static void
r128_a64_gen_setup(r128_a64_gen_t *g, uint8_t *code,
                   const rage128_draw_state_t *ds, int no_soa)
{
    int bppsh   = (ds->bpp == 4) ? 2 : 1;
    int z_on    = ds->z_en;
    int sten_on = ds->sten_on;
    int dith_on = ds->dither && ds->dst_dt != 6;
    int tex_on  = ds->tex_en || ds->sec_en;
    /* States the inline gate accepts sample inline
       (vid_ati_rage128_codegen_arm64_tex.h); the rest call
       rage128_texstage_run for each pixel. Table fog always takes the
       call: r128_jit_texinline_can leaves that refusal to its callers,
       and the SoA gate refuses it as well. */
    int tex_inline = tex_on && r128_a64_texinline_can(ds)
        && !(ds->fog_en && ds->fog_table_en);
    /* States the SoA gates accept get the vector loop ahead of the
       scalar loop, which then handles the pixels left at the end of the
       row and any group the vector loop gives up on. soa_selftex, set
       at state capture, marks a draw whose texture reads overlap the
       rows it writes: the vector loop reads a whole group's texels
       before storing any of them, so it could miss a store that the
       interpreter's pixel-by-pixel order would see. Such draws keep
       the scalar block. */
    int soa_on = !no_soa
        && (tex_on ? (!ds->soa_selftex && tex_inline
                      && r128_a64_soa_tex_can(ds))
                   : r128_a64_soa_can(ds));
    /* soa_tri and soa_split are set when stage 0 of a textured SoA
       block filters trilinearly or runs separate min and mag filter
       passes. Both need the R128_A64_FRAME_ST3 slots for the s/t and
       pass-A stashes. */
    int soa_tri = soa_on && tex_on
        && r128_jit_minb_tri(ds->sh[0].minb,
                             ds->sh[0].mipdis,
                             ds->need_lod);
    int soa_split = soa_on && tex_on
        && r128_jit_minb_split(ds->sh[0].minb,
                               ds->sh[0].mipdis,
                               ds->sh[0].mag,
                               ds->need_lod);
    /* Stage 1 of a dual-stage block runs its own trilinear or split
       pipeline. The shared decode subroutines must leave the
       R128_A64_SP_SOA_WM slot alone when either stage keeps it live
       across its passes. */
    int soa_tri2 = soa_on && ds->sec_en
        && r128_jit_minb_tri(ds->sh[1].minb,
                             ds->sh[1].mipdis,
                             ds->need_lod2);
    int soa_split2 = soa_on && ds->sec_en
        && r128_jit_minb_split(ds->sh[1].minb,
                               ds->sh[1].mipdis,
                               ds->sh[1].mag,
                               ds->need_lod2);
    /* Stage 0 is mipmapped, so the per-lane level sizes occupy the
       R128_A64_SP_SOA_VLW slot. A chroma key then needs the dedicated
       cover-mask slot instead, as do split groups (their s/t stash is in
       VLW) and every alpha-test group. */
    int soa_mip = soa_on && tex_on
        && r128_jit_minb_mip_on(ds->sh[0].minb,
                                ds->sh[0].mipdis,
                                ds->need_lod);
    /* Frame size. Textured SoA blocks start from one of three shapes:
       R128_A64_FRAME_ST2 (656) for dual-stage, specular or fog groups,
       which stash stage-0 output and the weights; R128_A64_FRAME_ST3
       (512) for trilinear or split groups; R128_A64_FRAME_ST (480)
       otherwise. On top of the shape they add 16 bytes for the
       dedicated cover-mask slot (r128_a64_soa_cover_off) when the group
       has an alpha test or stencil, or a chroma key whose VLW slot is
       taken; 16 for the return-address slot of the coordinate
       subroutines (soa_dsub); and 16 for the stencil vector stash.
       Untextured SoA blocks with blending, specular, fog or stencil take
       R128_A64_FRAME_ST for the source channel slots. The rest use
       R128_A64_FRAME_TI with inline texture or R128_A64_FRAME. */
    int soa_sf = ds->spec_en || ds->fog_en;
    /* A dual-stage pair over the inline size estimate
       (r128_a64_soa_dual_sub) that the SoA gate still admits with
       subroutines: each trilinear stage's coordinate and gather code
       becomes a BL subroutine, and the frame gains a slot above the
       cover mask to park the return address. */
    int soa_dsub = soa_on && r128_a64_soa_dual_sub(ds);
    int frame    = (soa_on && tex_inline)
           ? (((ds->sec_en || soa_sf)
                   ? R128_A64_FRAME_ST2
                   : ((soa_tri || soa_split)
                          ? R128_A64_FRAME_ST3
                          : R128_A64_FRAME_ST))
           + ((r128_a64_soa_atest_on(ds)
               || ds->sten_on
               || (ds->need_ck
                   && (ds->sec_en || soa_sf
                       || soa_mip || soa_split)))
                     ? 16
                     : 0)
           + (soa_dsub ? 16 : 0)
           + (ds->sten_on ? 16 : 0))
           : ((soa_on && (ds->alpha_en || soa_sf || ds->sten_on))
                  ? R128_A64_FRAME_ST
                  : (tex_inline ? R128_A64_FRAME_TI
                                : R128_A64_FRAME));
    /* The scalar loop's stencil word sits at the base-frame top,
       R128_A64_FRAME or R128_A64_FRAME_TI. In SoA blocks that offset is
       inside the larger frame and may share bytes with a vector-loop
       slot, but the scalar loop writes and reads the word within one
       pixel and never while the vector loop runs. */
    int sten_off = tex_inline ? R128_A64_FRAME_TI : R128_A64_FRAME;
    if (ds->sten_on)
        frame += 16;
    int aux_on       = ds->aux_on;
    int aux_add_mask = 0, aux_row_out = -1;

    if (aux_on)
        for (int i = 0; i < 3; i++)
            if ((ds->aux_cntl & (1u << (i * 2))) && !(ds->aux_cntl & (2u << (i * 2))))
                aux_add_mask |= 1 << i;

    g->e = (r128_a64_emit_t) {
        code, 0, 0, { -1, -1 },
           { -1, -1 }
    };
    g->ds           = ds;
    g->bppsh        = bppsh;
    g->z_on         = z_on;
    /* Table fog reads interpolated depth even without the Z test, so
       its scalar loop loads and steps the same double DDA without
       enabling the Z test or its writes. */
    g->z_step       = z_on || (ds->fog_en && ds->fog_table_en);
    g->sten_on      = sten_on;
    g->dith_on      = dith_on;
    g->tex_on       = tex_on;
    g->tex_inline   = tex_inline;
    g->soa_on       = soa_on;
    g->soa_tri      = soa_tri;
    g->soa_split    = soa_split;
    g->soa_tri2     = soa_tri2;
    g->soa_split2   = soa_split2;
    g->soa_dsub     = soa_dsub;
    g->frame        = frame;
    g->sten_off     = sten_off;
    g->aux_on       = aux_on;
    g->aux_add_mask = aux_add_mask;
    g->aux_row_out  = aux_row_out;
    g->nbail        = 0;
    g->nskip        = 0;
}

/* Prologue: allocate the frame, save x29, x30 and x19..x28, load the
   per-triangle values from tri and build the FP constants. */
static void
r128_a64_gen_prologue(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds    = g->ds;
    int                         frame = g->frame, z_on = g->z_on, sten_on = g->sten_on;
    int                         tex_inline = g->tex_inline;

    if (frame > 512) {
        /* STP pre-index reaches at most -512, so a larger frame takes
           an explicit SUB SP first (the epilogue splits likewise above
           504) */
        r128_a64_sub_x_imm(&g->e, 31, 31, frame);
        r128_a64_stp_x(&g->e, 29, 30, 0);
    } else
        r128_a64_stp_x_pre(&g->e, 29, 30, frame);
    r128_a64_e32(&g->e, 0x910003FD); /* mov x29, sp */
    r128_a64_stp_x(&g->e, 19, 20, 16);
    r128_a64_stp_x(&g->e, 21, 22, 32);
    r128_a64_stp_x(&g->e, 23, 24, 48);
    r128_a64_stp_x(&g->e, 25, 26, 64);
    r128_a64_stp_x(&g->e, 27, 28, 80);

    /* per-triangle constants */
    r128_a64_ldr_x(&g->e, 7, 0, (int) offsetof(r128_jit_tri_t, e0dxi));
    r128_a64_ldr_x(&g->e, 8, 0, (int) offsetof(r128_jit_tri_t, e1dxi));
    r128_a64_ldr_x(&g->e, 9, 0, (int) offsetof(r128_jit_tri_t, e2dxi));
    r128_a64_ldr_x(&g->e, 10, 0, (int) offsetof(r128_jit_tri_t, vram));
    r128_a64_ldr_w(&g->e, 11, 0, (int) offsetof(r128_jit_tri_t, vram_mask));
    r128_a64_ldp_w(&g->e, 12, 13, 0, (int) offsetof(r128_jit_tri_t, x0));
    r128_a64_ldr_s(&g->e, 1, 0, (int) offsetof(r128_jit_tri_t, invs));
    r128_a64_ldur_q(&g->e, 4, 0, (int) offsetof(r128_jit_tri_t, vca));
    r128_a64_ldur_q(&g->e, 5, 0, (int) offsetof(r128_jit_tri_t, vcb));
    r128_a64_ldur_q(&g->e, 6, 0, (int) offsetof(r128_jit_tri_t, vcc));
    if (g->z_step)
        r128_a64_ldr_d(&g->e, 2, 0, (int) offsetof(r128_jit_tri_t, dZdx));
    if (z_on || sten_on) {
        /* The stencil byte lives in the Z cell, so a stencil block
           resolves that cell even with the Z test off and needs the Z
           staging pointer, base and limit either way. */
        r128_a64_ldr_x(&g->e, 14, 0, (int) offsetof(r128_jit_tri_t, zptr));
        r128_a64_ldr_w(&g->e, 15, 0, (int) offsetof(r128_jit_tri_t, z_base));
        r128_a64_ldr_w(&g->e, 19, 0, (int) offsetof(r128_jit_tri_t, z_lim));
    }
    r128_a64_ldr_x(&g->e, 20, 0, (int) offsetof(r128_jit_tri_t, cptr));
    r128_a64_ldr_w(&g->e, 21, 0, (int) offsetof(r128_jit_tri_t, c_base));
    r128_a64_ldr_w(&g->e, 22, 0, (int) offsetof(r128_jit_tri_t, c_lim));
    r128_a64_movn_w(&g->e, 23, 0); /* rx0 = -1 */
    r128_a64_movn_w(&g->e, 24, 0);

    /* FP constants */
    if (z_on) {
        uint64_t zmaxd_bits;
        double   zmaxd = (double) ds->zmax;

        memcpy(&zmaxd_bits, &zmaxd, 8);
        r128_a64_fmov_d_x(&g->e, 19, 31);     /* d19 = 0.0 (xzr)  */
        r128_a64_fmov_d_imm(&g->e, 20, 0x70); /* d20 = 1.0      */
        r128_a64_mov_x_imm64(&g->e, 16, zmaxd_bits);
        r128_a64_fmov_d_x(&g->e, 21, 16);     /* d21 = (double)zmax */
        r128_a64_fmov_d_imm(&g->e, 22, 0x60); /* d22 = 0.5      */
    }
    r128_a64_movz_w(&g->e, 16, 0x437F, 1); /* 255.0f bits      */
    r128_a64_fmov_s_w(&g->e, 23 + 0, 16);  /* s23 = 255.0f     */
    /* v23 is not w23 (rx0): the FP/SIMD and general registers are
       separate files */
    r128_a64_dup_4s_lane0(&g->e, 23, 23);
    r128_a64_fmov_s_imm(&g->e, 24, 0x60); /* 0.5f             */
    r128_a64_dup_4s_lane0(&g->e, 24, 24);

    if (tex_inline)
        r128_a64_emit_tex_prologue(&g->e, ds); /* d8-d15 save + const banks */
}

/* Auxiliary scissors (RRG: AUX_SC_CNTL, p. 3-156 / PDF 174), the row
   part: bit i of the mask is set when rect i is enabled and py lies in
   its inclusive y range. The rect coordinates are immediates from the
   draw state. This runs before the dither setup replaces py in w6. The
   mask goes to a frame slot, read back for each pixel, because the
   pixel loop has no spare register to hold it. */
static void
r128_a64_gen_row_aux(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds           = g->ds;
    int                         aux_add_mask = g->aux_add_mask;

    r128_a64_movz_w(&g->e, 16, 0, 0);
    for (int i = 0; i < 3; i++) {
        int b1, b2;

        if (!(ds->aux_cntl & (1u << (i * 2))))
            continue;
        r128_a64_mov_w_s14(&g->e, 17, ds->aux_y0[i]);
        r128_a64_cmp_w_reg(&g->e, 6, 17);
        b1 = r128_a64_bcond(&g->e, A64_LT);
        r128_a64_mov_w_s14(&g->e, 17, ds->aux_y1[i]);
        r128_a64_cmp_w_reg(&g->e, 6, 17);
        b2 = r128_a64_bcond(&g->e, A64_GT);
        r128_a64_add_w_imm(&g->e, 16, 16, 1u << i);
        r128_a64_patch19(&g->e, b1, r128_a64_here(&g->e));
        r128_a64_patch19(&g->e, b2, r128_a64_here(&g->e));
    }
    if (aux_add_mask) {
        /* Additive rects are enabled but none covers this row: the
           pixel must lie in one, so the row draws nothing. */
        r128_a64_movz_w(&g->e, 17, (uint32_t) aux_add_mask, 0);
        r128_a64_and_w_reg(&g->e, 17, 16, 17);
        g->aux_row_out = r128_a64_cbz_w(&g->e, 17);
    }
    r128_a64_str_w(&g->e, 16, 31, R128_A64_SP_AUX);
}

/* The modeled pattern follows destination coordinates and stays outside
   the block key. Load its row through the captured raster state before
   dither consumes py, and keep it in a frame word across texture calls. */
static void
r128_a64_gen_row_stipple(r128_a64_gen_t *g)
{
    r128_a64_ldr_x(&g->e, 16, 0, (int) offsetof(r128_jit_tri_t, texctx));
    r128_a64_ldr_x(&g->e, 16, 16, (int) offsetof(r3d_texctx_t, rs));
    r128_a64_and_w_bitmask(&g->e, 17, 6, 31);
    r128_a64_add_x_lsl(&g->e, 16, 16, 17, 2);
    r128_a64_ldr_w(&g->e, 16, 16, (int) offsetof(rage128_raster_state_t, stipple));
    r128_a64_str_w(&g->e, 16, 31, R128_A64_SP_STIP);
}

/* Reduce r3d_bayer4 to this row's four thresholds, one byte each with
   column 0 lowest, in w6 (nothing after this needs py). The four table
   rows are 32-bit words in two 64-bit constants; bit 1 of py picks the
   constant and bit 0 the half. v28 gets 255 in each lane for the
   saturating add of r3d_dq. */
static void
r128_a64_gen_row_dither(r128_a64_gen_t *g)
{
    r128_a64_mov_x_imm64(&g->e, 16, 0x060E040C0A020800ull); /* rows 1:0 */
    r128_a64_mov_x_imm64(&g->e, 17, 0x050D070F09010B03ull); /* rows 3:2 */
    r128_a64_tst_w_imm(&g->e, 6, 2);
    r128_a64_csel_x(&g->e, 16, 17, 16, A64_NE);
    r128_a64_tst_w_imm(&g->e, 6, 1);
    r128_a64_lsr_x(&g->e, 17, 16, 32);
    r128_a64_csel_w(&g->e, 6, 17, 16, A64_NE);
    r128_a64_movi_4s_imm8(&g->e, 28, 255);
}

/* Per-row values the texture call clobbers (caller-saved under AAPCS64):
   saved once here, reloaded after every call. */
static void
r128_a64_gen_row_texspill(r128_a64_gen_t *g)
{
    r128_a64_stp_x(&g->e, 0, 4, R128_A64_SP_TRI);
    r128_a64_stp_x(&g->e, 5, 6, R128_A64_SP_ROW);
    if (g->z_on) {
        r128_a64_stp_d_sp(&g->e, 19, 20, R128_A64_SP_D19);
        r128_a64_stp_d_sp(&g->e, 21, 22, R128_A64_SP_D21);
    }
    r128_a64_stp_q_sp(&g->e, 23, 24, R128_A64_SP_Q23);
}

/* SoA vector loop ahead of the scalar loop: the shared decode and
   coordinate subroutines first, then the SoA prologue and the loop
   itself. */
static void
r128_a64_gen_soa(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds     = g->ds;
    int                         soa_on = g->soa_on, tex_inline = g->tex_inline, soa_dsub = g->soa_dsub;
    int                         soa_tri = g->soa_tri, soa_split = g->soa_split;
    int                         soa_tri2 = g->soa_tri2, soa_split2 = g->soa_split2;

    if (soa_on && tex_inline) {
        /* S3TC and YUV stages decode texels through a per-stage BL
           subroutine. SoA blocks emit it once here, ahead of both the
           vector loop and the scalar loop, which share the copy; blocks
           without the SoA loop emit it inside the texture stage. A
           branch skips over it on entry. When either stage is
           trilinear or split, the subroutine keeps its third scratch
           word in v31.s[2] instead of the frame word at 184, because
           184 is the R128_A64_SP_SOA_WM slot, live across the pass A
           and pass B fetches. */
        int b_over = -1;
        int vecwm  = soa_tri || soa_split || soa_tri2 || soa_split2;

        for (int st = 0; st < 2; st++) {
            const r3d_stage_hdr_t *h = &ds->sh[st];

            if (!(st ? ds->sec_en : ds->tex_en))
                continue;
            if (h->dt != 0 && h->dt != 11 && h->dt != 12 && h->dt != 14)
                continue;
            if (b_over < 0)
                b_over = r128_a64_b(&g->e);
            g->e.tex_sub[st] = (h->dt == 0)
                ? r128_a64_emit_dxt_sub(&g->e, h, vecwm)
                : r128_a64_emit_yuv_sub(&g->e, h, vecwm);
        }
        if (soa_dsub) {
            /* Heavy dual-stage pair: each trilinear stage's filtered
               pipeline becomes a BL subroutine shared by its passes. It
               is emitted after the decode subroutines because its body
               calls them. */
            for (int st = 0; st < 2; st++) {
                if (!(st ? soa_tri2 : soa_tri))
                    continue;
                if (b_over < 0)
                    b_over = r128_a64_b(&g->e);
                g->e.soa_csub[st] = r128_a64_emit_soa_coord_sub(&g->e, ds, st);
            }
        }
        if (b_over >= 0)
            r128_a64_patch26(&g->e, b_over, r128_a64_here(&g->e));
    }

    if (soa_on) {
        r128_a64_emit_soa_prologue(&g->e, ds, tex_inline);
        r128_a64_emit_soa_loop(&g->e, ds, g->bails, &g->nbail);
    }
}

/* Pixel loop head: groups the SoA loop gives up on continue here, and
   px > x1 leaves the loop. */
static void
r128_a64_gen_loop_head(r128_a64_gen_t *g)
{
    int loop_body;

    if (g->soa_on && g->tex_inline) {
        /* The vector loop uses v8-v13 for lane offsets, so entry to the
           scalar loop, at the end of the vector loop or from a group it
           gives up on, first rebuilds the texture constants there. The
           per-pixel branch back goes to loop_head below and skips this. */
        int soa_exit = r128_a64_here(&g->e);

        r128_a64_emit_tex_banks(&g->e, g->ds);
        for (int i = 0; i < g->nbail; i++)
            r128_a64_patch19(&g->e, g->bails[i], soa_exit);
        g->nbail = 0;
    }
    g->loop_head = r128_a64_here(&g->e);
    for (int i = 0; i < g->nbail; i++)
        r128_a64_patch19(&g->e, g->bails[i], g->loop_head);
    r128_a64_cmp_w_reg(&g->e, 12, 13);
    g->b_done = r128_a64_bcond(&g->e, A64_GT);
    loop_body = r128_a64_here(&g->e);
    (void) loop_body;
}

/* Coverage: the edge values arrive winding-normalized, so a pixel is
   outside when any of e0, e1, e2 is negative (one TBNZ on bit 63 of
   their OR). Then the auxiliary-scissor rejects. */
static void
r128_a64_gen_coverage(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds           = g->ds;
    int                         aux_add_mask = g->aux_add_mask;

    r128_a64_orr_x_lsl(&g->e, 16, 1, 2, 0);
    r128_a64_orr_x_lsl(&g->e, 16, 16, 3, 0);
    g->skips[g->nskip++] = r128_a64_tbnz63(&g->e, 16);

    if (g->aux_on) {
        /* Same order and rule as the interpreter's
           rage128_aux_sc_pass, after coverage and before Z: a pixel in
           an enabled subtractive rect (mode bit set) is discarded,
           and when any additive rect is enabled the pixel must lie in
           one. The row mask already holds the y test, so only x is
           compared here. w17 is free until the Z block sets zi. */
        int okbr[3], nok = 0;

        r128_a64_ldr_w(&g->e, 17, 31, R128_A64_SP_AUX);
        for (int i = 0; i < 3; i++) {
            int b1, b2;

            if (!(ds->aux_cntl & (2u << (i * 2))))
                continue; /* enabled subtractive rects only */
            b1 = r128_a64_tbz(&g->e, 17, i);
            r128_a64_mov_w_s14(&g->e, 16, ds->aux_x0[i]);
            r128_a64_cmp_w_reg(&g->e, 12, 16);
            b2 = r128_a64_bcond(&g->e, A64_LT);
            r128_a64_mov_w_s14(&g->e, 16, ds->aux_x1[i]);
            r128_a64_cmp_w_reg(&g->e, 12, 16);
            g->skips[g->nskip++] = r128_a64_bcond(&g->e, A64_LE);
            r128_a64_patch14(&g->e, b1, r128_a64_here(&g->e));
            r128_a64_patch19(&g->e, b2, r128_a64_here(&g->e));
        }
        if (aux_add_mask) {
            for (int i = 0; i < 3; i++) {
                int b1, b2;

                if (!(aux_add_mask & (1 << i)))
                    continue;
                b1 = r128_a64_tbz(&g->e, 17, i);
                r128_a64_mov_w_s14(&g->e, 16, ds->aux_x0[i]);
                r128_a64_cmp_w_reg(&g->e, 12, 16);
                b2 = r128_a64_bcond(&g->e, A64_LT);
                r128_a64_mov_w_s14(&g->e, 16, ds->aux_x1[i]);
                r128_a64_cmp_w_reg(&g->e, 12, 16);
                okbr[nok++] = r128_a64_bcond(&g->e, A64_LE);
                r128_a64_patch14(&g->e, b1, r128_a64_here(&g->e));
                r128_a64_patch19(&g->e, b2, r128_a64_here(&g->e));
            }
            g->skips[g->nskip++] = r128_a64_b(&g->e); /* no additive rect hit */
            for (int k = 0; k < nok; k++)
                r128_a64_patch19(&g->e, okbr[k], r128_a64_here(&g->e));
        }
    }
    if (ds->stip_en) {
        /* A clear modeled pattern bit discards before Z and stencil.
           Variable shifts use the low five count bits, so complementing
           px selects bit 31 - (px & 31) without changing live px. */
        r128_a64_ldr_w(&g->e, 16, 31, R128_A64_SP_STIP);
        r128_a64_mvn_w(&g->e, 17, 12);
        r128_a64_lsrv_w(&g->e, 16, 16, 17);
        g->skips[g->nskip++] = r128_a64_tbz(&g->e, 16, 0);
    }
}

/* Z and stencil: quantize the depth, resolve the Z cell, then either
   the depth test as a branch or, with stencil on, the depth and stencil
   test results as values packed into the stencil frame word. */
static void
r128_a64_gen_zsten(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds   = g->ds;
    int                         z_on = g->z_on, sten_on = g->sten_on, sten_off = g->sten_off;
    int                         b_zvram, b_zres;

    if (z_on) {
        /* zc = zline clamped to [0,1], as the C does with
           if (!(zc > 0.0)) and if (zc > 1.0): NaN and values <= 0 both
           become 0.0, since GT is false for an unordered compare.
           Stencil-only blocks skip the quantize: the interpreter
           computes zi there too, but uses it only for the Z test and
           the Z write, which are both off. */
        r128_a64_fcmp_d0(&g->e, 0);
        r128_a64_fcsel_d(&g->e, 3, 0, 19, A64_GT);
        r128_a64_fcmp_d(&g->e, 3, 20);
        r128_a64_fcsel_d(&g->e, 3, 20, 3, A64_GT);
        /* zq = zc * zmax + 0.5 in double, clamped to zmax; zi =
           (uint32_t) zq */
        r128_a64_fmul_d(&g->e, 16, 3, 21);
        r128_a64_fadd_d(&g->e, 16, 16, 22);
        r128_a64_fcmp_d(&g->e, 16, 21);
        r128_a64_fcsel_d(&g->e, 16, 21, 16, A64_GT);
        r128_a64_fcvtzu_w_d(&g->e, 17, 16); /* w17 = zi (live into z write) */
    }
    /* zaddr = zrow + px * zbpp; stencil implies zbpp == 4, since the
       draw state turns stencil off for 16-bit Z */
    r128_a64_add_w_lsl(&g->e, 25, 5, 12, (ds->zbpp == 4) ? 2 : 1);
    /* Resolve the Z cell into x26, as the interpreter does: with a
       staged arena (zptr != 0) the cell is zptr + (zaddr - z_base) and a
       pixel whose cell would pass z_lim is skipped; otherwise it is
       vram + (zaddr & vram_mask). */
    b_zvram = r128_a64_cbz_x(&g->e, 14);
    r128_a64_sub_w_reg(&g->e, 26, 25, 15);
    r128_a64_add_w_imm(&g->e, 28, 26, ds->zbpp);
    r128_a64_cmp_w_reg(&g->e, 28, 19);
    g->skips[g->nskip++] = r128_a64_bcond(&g->e, A64_HI);
    r128_a64_add_x_uxtw(&g->e, 26, 14, 26);
    b_zres = r128_a64_b(&g->e);
    r128_a64_patch19(&g->e, b_zvram, r128_a64_here(&g->e));
    r128_a64_and_w_reg(&g->e, 26, 25, 11);
    r128_a64_add_x_uxtw(&g->e, 26, 10, 26);
    r128_a64_patch26(&g->e, b_zres, r128_a64_here(&g->e));
    if (!sten_on) {
        /* Z test; code 7 (always) emits no load and no compare. The
           16-bit load needs no mask: the gate allows 16-bit Z only with
           zmax = 0xffff. */
        if (ds->zfn != 7) {
            if (ds->zbpp == 2)
                r128_a64_ldrh(&g->e, 28, 26, 0);
            else {
                r128_a64_ldr_w(&g->e, 28, 26, 0);
                if (ds->zshift)
                    r128_a64_lsr_w(&g->e, 28, 28, ds->zshift);
                if (ds->zmax != 0xffffffffu)
                    r128_a64_and_w_bitmask(&g->e, 28, 28, ds->zmax);
            }
            r128_a64_cmp_w_reg(&g->e, 17, 28);
            g->skips[g->nskip++] = r128_a64_bcond(&g->e, r128_a64_cmp_inv[ds->zfn & 7]);
        }
    } else {
        /* Stencil on (zbpp == 4): read the 32-bit cell, which holds 24
           bits of depth and the 8-bit stencil, once, and compute both
           test results as values. The interpreter applies the stencil
           operation and the discard only after the alpha test, so
           neither test may branch here. The packed
           {sbuf | sres<<8 | zres<<9} goes to the stencil frame word for
           r128_a64_gen_sten_update. w25 (zaddr, dead after the resolve)
           holds the raw cell word. */
        uint32_t sva = (ds->sref & ds->svmask) & 0xff;

        r128_a64_ldr_w(&g->e, 25, 26, 0);
        /* zres -> w27: 1 with the Z test off or code 7, 0 for code 0 */
        if (!z_on || ds->zfn == 7)
            r128_a64_movz_w(&g->e, 27, 1, 0);
        else if (ds->zfn == 0)
            r128_a64_movz_w(&g->e, 27, 0, 0);
        else {
            if (ds->zshift)
                r128_a64_lsr_w(&g->e, 28, 25, ds->zshift);
            else
                r128_a64_mov_w(&g->e, 28, 25);
            if (ds->zmax != 0xffffffffu)
                r128_a64_and_w_bitmask(&g->e, 28, 28, ds->zmax);
            r128_a64_cmp_w_reg(&g->e, 17, 28); /* cmp zi(a), zbuf(b) */
            r128_a64_cset_w(&g->e, 27, r128_a64_cmp_fwd[ds->zfn & 7]);
        }
        /* sbuf -> w28 = (word >> sshift) & 0xff */
        if (ds->sshift)
            r128_a64_lsr_w(&g->e, 28, 25, ds->sshift);
        else
            r128_a64_mov_w(&g->e, 28, 25);
        r128_a64_and_w_bitmask(&g->e, 28, 28, 0xff);
        /* sres -> w16 = r3d_cmp(sfn, sref&svmask, sbuf&svmask) */
        if (ds->sfn == 7)
            r128_a64_movz_w(&g->e, 16, 1, 0);
        else if (ds->sfn == 0)
            r128_a64_movz_w(&g->e, 16, 0, 0);
        else {
            r128_a64_mov_w_imm32(&g->e, 25, ds->svmask); /* svmask reg   */
            r128_a64_and_w_reg(&g->e, 16, 28, 25);       /* b = sbuf&svmask */
            r128_a64_mov_w_imm32(&g->e, 25, sva);        /* a = sref&svmask */
            r128_a64_cmp_w_reg(&g->e, 25, 16);           /* cmp a, b     */
            r128_a64_cset_w(&g->e, 16, r128_a64_cmp_fwd[ds->sfn & 7]);
        }
        r128_a64_orr_w_lsl(&g->e, 28, 28, 16, 8); /* sbuf | sres<<8   */
        r128_a64_orr_w_lsl(&g->e, 28, 28, 27, 9); /* | zres<<9        */
        r128_a64_str_w(&g->e, 28, 31, sten_off);
    }
}

/* Barycentric weights w_i = (float) (e_i + b_i) * invs into s16-s18,
   with b_i the per-triangle fill-rule bias flag from tri. The bias
   decides coverage only, so the accumulators x1-x3 keep it and each
   weight adds it back in x16, a scratch register here, before the
   64-bit convert; the integer add is exact, so the convert rounds the
   same value the interpreter converts. */
static void
r128_a64_gen_weights(r128_a64_gen_t *g)
{
    static const int boff[3] = { (int) offsetof(r128_jit_tri_t, e0b),
                                 (int) offsetof(r128_jit_tri_t, e1b),
                                 (int) offsetof(r128_jit_tri_t, e2b) };

    for (int k = 0; k < 3; k++) {
        r128_a64_ldr_x(&g->e, 16, 0, boff[k]);
        r128_a64_add_x_reg(&g->e, 16, 1 + k, 16);
        r128_a64_scvtf_s_x(&g->e, 16 + k, 16);
    }
    r128_a64_fmul_s(&g->e, 16, 16, 1);
    r128_a64_fmul_s(&g->e, 17, 17, 1);
    r128_a64_fmul_s(&g->e, 18, 18, 1);
}

/* The barycentric weights, then col = w0*vca + w1*vcb + w2*vcc per
   lane, summed left to right like the interpreter and with separate
   multiplies and adds (no FMLA). The C computes the weights before the
   Z test; here they come after it because the Z block uses d16, which
   shares its register with s16. Nothing reads the weights before this
   point, so the result is the same. */
static void
r128_a64_gen_color(r128_a64_gen_t *g)
{
    r128_a64_gen_weights(g);

    r128_a64_dup_4s_lane0(&g->e, 25, 16);
    r128_a64_dup_4s_lane0(&g->e, 26, 17);
    r128_a64_dup_4s_lane0(&g->e, 27, 18);
    r128_a64_fmul_4s(&g->e, 25, 25, 4);
    r128_a64_fmul_4s(&g->e, 26, 26, 5);
    r128_a64_fadd_4s(&g->e, 25, 25, 26);
    r128_a64_fmul_4s(&g->e, 27, 27, 6);
    r128_a64_fadd_4s(&g->e, 25, 25, 27);
}

/* Texture stage: sampled inline when the inline gate accepts the state,
   otherwise a call to rage128_texstage_run with the live registers saved
   around it. After the call the weights that specular and fog read are
   recomputed. */
static void
r128_a64_gen_texture(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds   = g->ds;
    int                         z_on = g->z_on, sten_on = g->sten_on, dith_on = g->dith_on;
    int                         tex_on = g->tex_on, tex_inline = g->tex_inline;

    if (tex_inline) {
        /* Inline sampling repeats rage128_texstage_run operation by
           operation for the states the inline gate accepts. There is no
           call, so x0, the weights (s16-s18) and every per-row register
           stay live. A chroma-key reject branches to pix_skip like a
           failed Z test. */
        r128_a64_emit_texstage_inline(&g->e, ds, g->skips, &g->nskip);
    } else if (tex_on) {
        /* rage128_texstage_run(texctx, w0, w1, w2, &col) is the
           interpreter's own texture-stage code (formats, filters,
           mipmaps, chroma keys, combine), so the emitted work is only
           keeping the live state across an AAPCS64 call. col goes
           through the R128_A64_SP_COL frame slot, and a return of 0
           means a chroma-key discard. Afterwards the per-row values come
           back from the frame, the per-triangle values from tri, and
           the per-pixel values from the slots saved here. w19..w24 and
           x26 (Z cell) are callee-saved and survive the call. */
        r128_a64_str_q(&g->e, 25, 31, R128_A64_SP_COL);
        r128_a64_stp_x(&g->e, 1, 2, R128_A64_SP_E01);
        r128_a64_stp_x(&g->e, 3, 12, R128_A64_SP_E2PX);
        if (z_on && ds->z_wr)
            r128_a64_str_x(&g->e, 17, 31, R128_A64_SP_ZI);
        if (g->z_step)
            r128_a64_str_d(&g->e, 0, 31, R128_A64_SP_ZL);
        r128_a64_fmov_s_s(&g->e, 0, 16);
        r128_a64_fmov_s_s(&g->e, 1, 17);
        r128_a64_fmov_s_s(&g->e, 2, 18);
        r128_a64_add_x_imm(&g->e, 1, 31, R128_A64_SP_COL);
        r128_a64_mov_x_imm64(&g->e, 16, (uint64_t) (uintptr_t) rage128_texstage_run);
        r128_a64_ldr_x(&g->e, 0, 0, (int) offsetof(r128_jit_tri_t, texctx));
        r128_a64_blr(&g->e, 16);
        r128_a64_mov_w(&g->e, 16, 0); /* keep/discard, tested after reloads */

        r128_a64_ldp_x(&g->e, 0, 4, R128_A64_SP_TRI);
        r128_a64_ldp_x(&g->e, 5, 6, R128_A64_SP_ROW);
        r128_a64_ldr_x(&g->e, 7, 0, (int) offsetof(r128_jit_tri_t, e0dxi));
        r128_a64_ldr_x(&g->e, 8, 0, (int) offsetof(r128_jit_tri_t, e1dxi));
        r128_a64_ldr_x(&g->e, 9, 0, (int) offsetof(r128_jit_tri_t, e2dxi));
        r128_a64_ldr_x(&g->e, 10, 0, (int) offsetof(r128_jit_tri_t, vram));
        r128_a64_ldr_w(&g->e, 11, 0, (int) offsetof(r128_jit_tri_t, vram_mask));
        r128_a64_ldr_w(&g->e, 13, 0, (int) offsetof(r128_jit_tri_t, x1));
        r128_a64_ldr_s(&g->e, 1, 0, (int) offsetof(r128_jit_tri_t, invs));
        r128_a64_ldur_q(&g->e, 4, 0, (int) offsetof(r128_jit_tri_t, vca));
        r128_a64_ldur_q(&g->e, 5, 0, (int) offsetof(r128_jit_tri_t, vcb));
        r128_a64_ldur_q(&g->e, 6, 0, (int) offsetof(r128_jit_tri_t, vcc));
        if (g->z_step)
            r128_a64_ldr_d(&g->e, 2, 0, (int) offsetof(r128_jit_tri_t, dZdx));
        if (z_on || sten_on) {
            /* zptr (x14) and z_base (w15) are caller-saved, so the call
               may clobber them; reload them for the next pixel's Z cell
               resolve. z_lim (w19) is callee-saved and survives. */
            r128_a64_ldr_x(&g->e, 14, 0, (int) offsetof(r128_jit_tri_t, zptr));
            r128_a64_ldr_w(&g->e, 15, 0, (int) offsetof(r128_jit_tri_t, z_base));
        }
        if (z_on) {
            r128_a64_ldp_d_sp(&g->e, 19, 20, R128_A64_SP_D19);
            r128_a64_ldp_d_sp(&g->e, 21, 22, R128_A64_SP_D21);
        }
        if (g->z_step)
            r128_a64_ldr_d(&g->e, 0, 31, R128_A64_SP_ZL);
        r128_a64_ldp_q_sp(&g->e, 23, 24, R128_A64_SP_Q23);
        if (dith_on)
            r128_a64_movi_4s_imm8(&g->e, 28, 255);
        r128_a64_ldp_x(&g->e, 1, 2, R128_A64_SP_E01);
        r128_a64_ldp_x(&g->e, 3, 12, R128_A64_SP_E2PX);
        if (z_on && ds->z_wr)
            r128_a64_ldr_x(&g->e, 17, 31, R128_A64_SP_ZI);
        r128_a64_ldr_q(&g->e, 25, 31, R128_A64_SP_COL);
        g->skips[g->nskip++] = r128_a64_cbz_w(&g->e, 16);
    }

    if ((ds->spec_en || (ds->fog_en && !ds->fog_table_en)) && tex_on && !tex_inline) {
        /* Specular and vertex fog read the barycentric weights, which
           the call may clobber. Recompute them from the reloaded tri,
           e0..e2 and invs with the same operations as
           r128_a64_gen_color, so the values are identical; w16, the
           keep flag, is tested before this point, so x16 is free. The
           inline texture stage keeps s16-s18 and skips this; table fog
           does not read them. */
        r128_a64_gen_weights(g);
    }
}

/* Specular: col[rgb] += w0*spa + w1*spb + w2*spc, then values above 1.0
   become 1.0. Each lane does the C's per-channel operations: the sum
   left to right, added to col, then FMIN for the if (> 1.0f) clamp,
   which matches the C for every input (a NaN stays a NaN). The alpha
   lane is computed too and then replaced by the original alpha. */
static void
r128_a64_gen_spec(r128_a64_gen_t *g)
{
    r128_a64_dup_4s_lane0(&g->e, 26, 16);
    r128_a64_dup_4s_lane0(&g->e, 27, 17);
    r128_a64_dup_4s_lane0(&g->e, 29, 18);
    r128_a64_ldur_q(&g->e, 30, 0, (int) offsetof(r128_jit_tri_t, spa));
    r128_a64_fmul_4s(&g->e, 30, 26, 30);
    r128_a64_ldur_q(&g->e, 31, 0, (int) offsetof(r128_jit_tri_t, spb));
    r128_a64_fmul_4s(&g->e, 31, 27, 31);
    r128_a64_fadd_4s(&g->e, 30, 30, 31);
    r128_a64_ldur_q(&g->e, 31, 0, (int) offsetof(r128_jit_tri_t, spc));
    r128_a64_fmul_4s(&g->e, 31, 29, 31);
    r128_a64_fadd_4s(&g->e, 30, 30, 31);
    r128_a64_mov_16b(&g->e, 31, 25); /* original col (alpha)     */
    r128_a64_fadd_4s(&g->e, 25, 25, 30);
    r128_a64_fmov_s_imm(&g->e, 26, 0x70); /* 1.0f                     */
    r128_a64_dup_4s_lane0(&g->e, 26, 26);
    r128_a64_fmin_4s(&g->e, 25, 25, 26);
    r128_a64_ins_elem_s(&g->e, 25, 3, 31, 3);
}

/* Fog: col[rgb] = col*f + fogc*(1-f). FOG_TABLE_EN (RRG:
   MISC_3D_STATE_CNTL_REG, p. 3-257 / PDF 275) selects the factor f: 0
   takes it from the vertices, 1 from the fog table indexed by depth.
   Each path leaves f in s27 and 1.0f in s26; the shared blend then runs
   on all four lanes (the fog color's lane 3 is 0) and the original
   alpha is put back. Table fog uses the serial depth DDA in d0.
   With the Z test off, the fog stage supplies its own clamp constants
   so neither a depth cell nor Z quantization is needed. */
static void
r128_a64_gen_fog(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds = g->ds;
    uint32_t                    fbits;

    if (!ds->fog_table_en) {
        /* Vertex fog: f = w0*fga + w1*fgb + w2*fgc, summed left to
           right as in the C, then clamped to [0,1] with FCMP and FCSEL
           in the C's order (f < 0 first, then f > 1). */
        r128_a64_ldur_q(&g->e, 26, 0, (int) offsetof(r128_jit_tri_t, fog));
        r128_a64_dup_4s_lane(&g->e, 29, 26, 1);
        r128_a64_dup_4s_lane(&g->e, 30, 26, 2);
        r128_a64_fmul_s(&g->e, 27, 16, 26); /* w0*fga                   */
        r128_a64_fmul_s(&g->e, 29, 17, 29); /* w1*fgb                   */
        r128_a64_fadd_s(&g->e, 27, 27, 29);
        r128_a64_fmul_s(&g->e, 29, 18, 30); /* w2*fgc                   */
        r128_a64_fadd_s(&g->e, 27, 27, 29);
        r128_a64_movi_4s_zero(&g->e, 26);
        r128_a64_fcmp_s0(&g->e, 27);
        r128_a64_fcsel_s(&g->e, 27, 26, 27, A64_MI); /* f<0 -> 0            */
        r128_a64_fmov_s_imm(&g->e, 26, 0x70);        /* 1.0f                */
        r128_a64_fcmp_s(&g->e, 27, 26);
        r128_a64_fcsel_s(&g->e, 27, 26, 27, A64_GT); /* f>1 -> 1            */
    } else {
        /* Table fog, blending between adjacent entries the way the
           interpreter splits the depth: q = (uint64_t) (zc * 255.0 *
           2^32), one FCVTZU from double, holds the entry index i in
           its high word and a 32-bit fraction in its low word. Then
           f = (T[i] + (T[i1] - T[i]) * t) / 255.0f with
           t = (float) fraction * 2^-32 and i1 = i + 1, or i at 255.
           zc is recomputed here from zline (d0) with the Z block's
           clamp, in d3. This code uses only x16 and x25 as general
           scratch (w17 holds zi and x26 the Z cell): the fraction
           moves to s27 before x16 takes the index, and x25 steps
           from &T[i] to &T[i1] with CSINC, which keeps &T[i] when
           i is 255. f is already within [0,1], so the C's clamp
           needs no code here. */
        if (!g->z_on) {
            r128_a64_fmov_d_x(&g->e, 19, 31);
            r128_a64_fmov_d_imm(&g->e, 20, 0x70);
        }
        r128_a64_fcmp_d0(&g->e, 0);
        r128_a64_fcsel_d(&g->e, 3, 0, 19, A64_GT); /* zc = (zline>0)?..:0 */
        r128_a64_fcmp_d(&g->e, 3, 20);
        r128_a64_fcsel_d(&g->e, 3, 20, 3, A64_GT);              /* zc = (zc>1)?1:zc    */
        r128_a64_mov_x_imm64(&g->e, 16, 0x406FE00000000000ull); /* 255.0    */
        r128_a64_fmov_d_x(&g->e, 29, 16);
        r128_a64_fmul_d(&g->e, 3, 3, 29);
        r128_a64_mov_x_imm64(&g->e, 16, 0x41F0000000000000ull); /* 2^32     */
        r128_a64_fmov_d_x(&g->e, 29, 16);
        r128_a64_fmul_d(&g->e, 3, 3, 29);
        r128_a64_fcvtzu_x_d(&g->e, 16, 3); /* q = i<<32 | fr      */
        r128_a64_ucvtf_s_w(&g->e, 27, 16); /* float(fr) (low w)   */
        r128_a64_lsr_x(&g->e, 16, 16, 32); /* i                   */
        r128_a64_ldr_x(&g->e, 25, 0, (int) offsetof(r128_jit_tri_t, fog_table));
        r128_a64_add_x_reg(&g->e, 25, 25, 16); /* &T[i]               */
        r128_a64_cmp_w_imm(&g->e, 16, 255);
        r128_a64_ldrb_reg(&g->e, 16, 25, 31);        /* T[i]                */
        r128_a64_csinc_x(&g->e, 25, 25, 25, A64_HS); /* &T[i1]              */
        r128_a64_ldrb_reg(&g->e, 25, 25, 31);        /* T[i1]               */
        r128_a64_sub_w_reg(&g->e, 25, 25, 16);       /* T[i1]-T[i]          */
        r128_a64_scvtf_s_w(&g->e, 29, 25);           /* fd                  */
        r128_a64_ucvtf_s_w(&g->e, 30, 16);           /* fa                  */
        r128_a64_movz_w(&g->e, 16, 0x2F80, 1);       /* 2^-32f bits         */
        r128_a64_fmov_s_w(&g->e, 26, 16);
        r128_a64_fmul_s(&g->e, 27, 27, 26);    /* t                   */
        r128_a64_fmul_s(&g->e, 29, 29, 27);    /* fd*t                */
        r128_a64_fadd_s(&g->e, 27, 30, 29);    /* fa + fd*t           */
        r128_a64_movz_w(&g->e, 16, 0x437F, 1); /* 255.0f bits         */
        r128_a64_fmov_s_w(&g->e, 29, 16);
        r128_a64_fdiv_s(&g->e, 27, 27, 29);   /* f = .../255.0f      */
        r128_a64_fmov_s_imm(&g->e, 26, 0x70); /* 1.0f (shared lerp)  */
    }
    r128_a64_dup_4s_lane0(&g->e, 29, 27); /* f splat             */
    r128_a64_dup_4s_lane0(&g->e, 26, 26); /* 1.0f splat          */
    r128_a64_fsub_4s(&g->e, 30, 26, 29);  /* (1-f) splat         */
    /* fog color vector {fogr, fogg, fogb, 0} from state immediates */
    {
        uint64_t rg;
        uint32_t fr, fg;

        memcpy(&fr, &ds->fogr, 4);
        memcpy(&fg, &ds->fogg, 4);
        memcpy(&fbits, &ds->fogb, 4);
        rg = ((uint64_t) fg << 32) | fr;
        r128_a64_mov_x_imm64(&g->e, 16, rg);
        r128_a64_fmov_d_x(&g->e, 31, 16); /* lanes 0,1; 2,3 zeroed */
        r128_a64_movz_w(&g->e, 16, fbits & 0xffff, 0);
        if (fbits >> 16)
            r128_a64_movk_w(&g->e, 16, fbits >> 16, 1);
        r128_a64_ins_s_w(&g->e, 31, 2, 16);
    }
    r128_a64_mov_16b(&g->e, 26, 25);     /* original col (alpha)     */
    r128_a64_fmul_4s(&g->e, 25, 25, 29); /* col*f                    */
    r128_a64_fmul_4s(&g->e, 31, 31, 30); /* fogc*(1-f)               */
    r128_a64_fadd_4s(&g->e, 25, 25, 31);
    r128_a64_ins_elem_s(&g->e, 25, 3, 26, 3);
}

/* Alpha test (REF_ALPHA and ALPHA_TEST_OP, RRG: MISC_3D_STATE_CNTL_REG,
   pp. 3-256-3-259 / PDF 274-277): r3d_cmp(fn, (uint32_t) (col[3] * 255.0f + 0.5f),
   ref), branching to pix_skip when the test fails. The float expression
   is done in scalar single precision on a copy of the alpha lane, as in
   the C. */
static void
r128_a64_gen_atest(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds = g->ds;

    r128_a64_dup_4s_lane(&g->e, 26, 25, 3);
    r128_a64_fmul_s(&g->e, 26, 26, 23);
    r128_a64_fadd_s(&g->e, 26, 26, 24);
    r128_a64_fcvtzu_w_s(&g->e, 16, 26);
    r128_a64_cmp_w_imm(&g->e, 16, (int) (ds->atest_ref & 0xff));
    g->skips[g->nskip++] = r128_a64_bcond(&g->e, r128_a64_cmp_inv[ds->atest_fn & 7]);
}

/* Stencil update and the deferred discard, in the interpreter's order:
   after the alpha test, so a pixel that fails it leaves the stencil
   alone, and before the color cell is resolved. The operation is
   sop = !sres ? sfail_op : (zres ? zpass_op : zfail_op); all three
   results are computed and two CSELs pick one. The new byte is
   (sbuf & ~swmask) | (op(sbuf) & swmask), swmask being the stencil
   write mask (SDK: STEN_WRITE_MSK, p. 6-57 / PDF 169). The interpreter
   writes the cell only when the byte changed; this always writes it
   back, which leaves the same memory contents. Then a pixel with
   !sres || !zres is discarded before the color and Z writes. x26 is
   the cell; the general temporaries w16, w25, w27 and w28 are not the
   vector register v25 that holds col. */
static void
r128_a64_gen_sten_update(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds       = g->ds;
    int                         sten_off = g->sten_off;
    uint32_t                    keep     = ~(0xffu << ds->sshift);

    r128_a64_ldr_w(&g->e, 25, 31, sten_off);     /* packed sbuf/sres/zres */
    r128_a64_and_w_bitmask(&g->e, 28, 25, 0xff); /* sbuf -> w28           */
    r128_a64_emit_sten_op(&g->e, 16, 28, ds->zfail_op, ds->sref);
    r128_a64_emit_sten_op(&g->e, 27, 28, ds->zpass_op, ds->sref);
    r128_a64_tst_w_imm(&g->e, 25, 1u << 9);     /* zres bit             */
    r128_a64_csel_w(&g->e, 16, 27, 16, A64_NE); /* tmp = zres?zpass:zfail */
    r128_a64_emit_sten_op(&g->e, 27, 28, ds->sfail_op, ds->sref);
    r128_a64_tst_w_imm(&g->e, 25, 1u << 8);     /* sres bit             */
    r128_a64_csel_w(&g->e, 16, 16, 27, A64_NE); /* snew = sres?tmp:sfail  */
    /* merged = (sbuf & ~swmask) | (snew & swmask) */
    r128_a64_mov_w_imm32(&g->e, 25, ds->swmask);
    r128_a64_and_w_reg(&g->e, 16, 16, 25);
    r128_a64_mov_w_imm32(&g->e, 25, (~ds->swmask) & 0xff);
    r128_a64_and_w_reg(&g->e, 27, 28, 25);
    r128_a64_orr_w_lsl(&g->e, 16, 16, 27, 0);
    /* write the stencil byte back into the cell, keeping the depth bits */
    r128_a64_ldr_w(&g->e, 27, 26, 0);
    r128_a64_and_w_bitmask(&g->e, 27, 27, keep);
    r128_a64_orr_w_lsl(&g->e, 27, 27, 16, ds->sshift);
    r128_a64_str_w(&g->e, 27, 26, 0);
    /* deferred discard: skip unless both tests passed */
    r128_a64_ldr_w(&g->e, 25, 31, sten_off);
    r128_a64_and_w_bitmask(&g->e, 25, 25, 3u << 8);
    r128_a64_cmp_w_imm(&g->e, 25, (int) (3u << 8));
    g->skips[g->nskip++] = r128_a64_bcond(&g->e, A64_NE);
}

/* daddr = drow + px * bpp, and the color cell resolved into x27 the same
   way as the Z cell (staged arena with its c_lim check, or local VRAM).
   This happens before the pack because blending reads the cell first,
   as in the C. Then the blend against the unpacked destination
   (ALPHA_BLND_SRC, ALPHA_BLND_DST and ALPHA_COMB_FCN, RRG:
   MISC_3D_STATE_CNTL_REG, pp. 3-257-3-259 / PDF 275-277). */
static void
r128_a64_gen_dcell_blend(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds = g->ds;

    r128_a64_add_w_lsl(&g->e, 25, 4, 12, g->bppsh);
    {
        int b_cvram, b_cres;

        b_cvram = r128_a64_cbz_x(&g->e, 20);
        r128_a64_sub_w_reg(&g->e, 28, 25, 21);
        r128_a64_add_w_imm(&g->e, 25, 28, ds->bpp);
        r128_a64_cmp_w_reg(&g->e, 25, 22);
        g->skips[g->nskip++] = r128_a64_bcond(&g->e, A64_HI);
        r128_a64_add_x_uxtw(&g->e, 27, 20, 28);
        b_cres = r128_a64_b(&g->e);
        r128_a64_patch19(&g->e, b_cvram, r128_a64_here(&g->e));
        r128_a64_and_w_reg(&g->e, 28, 25, 11);
        r128_a64_add_x_uxtw(&g->e, 27, 10, 28);
        r128_a64_patch26(&g->e, b_cres, r128_a64_here(&g->e));
    }

    if (ds->alpha_en) {
        /* dc = the destination as float lanes {r,g,b,a}, each 8-bit
           field converted and divided by 255.0f, as the C does with the
           r3d_dst_read result. */
        if (ds->dst_dt == 6) {
            r128_a64_ldr_w(&g->e, 16, 27, 0);
            r128_a64_ubfx_w(&g->e, 25, 16, 16, 8);
            r128_a64_ins_s_w(&g->e, 29, 0, 25);
            r128_a64_ubfx_w(&g->e, 25, 16, 8, 8);
            r128_a64_ins_s_w(&g->e, 29, 1, 25);
            r128_a64_ubfx_w(&g->e, 25, 16, 0, 8);
            r128_a64_ins_s_w(&g->e, 29, 2, 25);
            r128_a64_lsr_w(&g->e, 25, 16, 24);
            r128_a64_ins_s_w(&g->e, 29, 3, 25);
        } else if (ds->dst_dt == 3 || ds->dst_dt == 15) {
            /* Modeled destination channels widen by shifts alone. The
               single alpha bit expands to 0 or 255 for blend factors;
               four-bit alpha expands to its high nibble, like RGB. */
            int bits = ds->dst_dt == 3 ? 5 : 4;
            int shift = 8 - bits;

            r128_a64_ldrh(&g->e, 16, 27, 0);
            for (int ch = 0; ch < 3; ch++) {
                r128_a64_ubfx_w(&g->e, 25, 16, (2 - ch) * bits, bits);
                r128_a64_lsl_w(&g->e, 25, 25, shift);
                r128_a64_ins_s_w(&g->e, 29, ch, 25);
            }
            if (ds->dst_dt == 3) {
                r128_a64_ubfx_w(&g->e, 25, 16, 15, 1);
                r128_a64_sub_w_reg(&g->e, 25, 31, 25);
                r128_a64_and_w_bitmask(&g->e, 25, 25, 0xff);
            } else {
                r128_a64_ubfx_w(&g->e, 25, 16, 12, 4);
                r128_a64_lsl_w(&g->e, 25, 25, 4);
            }
            r128_a64_ins_s_w(&g->e, 29, 3, 25);
        } else {
            /* 565 to 8-bit fields by shifting alone, as r3d_dst_read
               does: r and b shifted left 3, g left 2, alpha 255 */
            r128_a64_ldrh(&g->e, 16, 27, 0);
            r128_a64_ubfx_w(&g->e, 25, 16, 11, 5);
            r128_a64_lsl_w(&g->e, 25, 25, 3);
            r128_a64_ins_s_w(&g->e, 29, 0, 25);
            r128_a64_ubfx_w(&g->e, 25, 16, 5, 6);
            r128_a64_lsl_w(&g->e, 25, 25, 2);
            r128_a64_ins_s_w(&g->e, 29, 1, 25);
            r128_a64_ubfx_w(&g->e, 25, 16, 0, 5);
            r128_a64_lsl_w(&g->e, 25, 25, 3);
            r128_a64_ins_s_w(&g->e, 29, 2, 25);
            r128_a64_movz_w(&g->e, 25, 255, 0);
            r128_a64_ins_s_w(&g->e, 29, 3, 25);
        }
        r128_a64_ucvtf_4s(&g->e, 29, 29);
        r128_a64_fdiv_4s(&g->e, 29, 29, 23);

        /* v27 = 1.0f in all lanes, for the factors and the clamp */
        r128_a64_fmov_s_imm(&g->e, 27, 0x70);
        r128_a64_dup_4s_lane0(&g->e, 27, 27);

        if (ds->bsrc == 0xb || ds->bsrc == 0xc) {
            /* Codes 0xb and 0xc set both factors: fs = sa in all lanes
               and fd = 1 - sa, where sa is the source alpha for 0xb
               and 1 minus it for 0xc. */
            r128_a64_dup_4s_lane(&g->e, 30, 25, 3);
            if (ds->bsrc == 0xc)
                r128_a64_fsub_4s(&g->e, 30, 27, 30);
            r128_a64_fsub_4s(&g->e, 31, 27, 30);
        } else {
            r128_a64_emit_blend_factor(&g->e, 30, ds->bsrc);
            r128_a64_emit_blend_factor(&g->e, 31, ds->bdst);
        }
        r128_a64_fmul_4s(&g->e, 25, 25, 30);
        r128_a64_fmul_4s(&g->e, 29, 29, 31);
        if (ds->bfcn & 2)
            r128_a64_fsub_4s(&g->e, 25, 25, 29);
        else
            r128_a64_fadd_4s(&g->e, 25, 25, 29);
        if (ds->bfcn & 1) {
            /* ALPHA_COMB_FCN without clamp, which the interpreter models
               as an 8-bit wrap: (float) (lrintf(v * 255) & 0xff) / 255.
               FCVTNS rounds to nearest with ties to even, as lrintf
               does in the default rounding mode. */
            r128_a64_fmul_4s(&g->e, 25, 25, 23);
            r128_a64_fcvtns_4s(&g->e, 25, 25);
            r128_a64_movi_4s_imm8(&g->e, 26, 255);
            r128_a64_and_16b(&g->e, 25, 25, 26);
            r128_a64_ucvtf_4s(&g->e, 25, 25);
            r128_a64_fdiv_4s(&g->e, 25, 25, 23);
        } else {
            /* Clamp to [0,1]. FMAX and FMIN match the C ternary except
               that -0.0 comes out as +0.0, and both pack to the same
               byte; a NaN stays a NaN in both. */
            r128_a64_movi_4s_zero(&g->e, 26);
            r128_a64_fmax_4s(&g->e, 25, 25, 26);
            r128_a64_fmin_4s(&g->e, 25, 25, 27);
        }
    }
}

/* out = (a<<24)|(r<<16)|(g<<8)|b with each channel (uint32_t)
   (col * 255.0f + 0.5f), built with 32-bit shifts and ORs as the C
   builds it, so a channel above 255 spills into the next byte the same
   way. Then the dither, the store under the write mask, and the
   rx0/rx1 update, which the C also does between the color and Z
   writes. */
static void
r128_a64_gen_pack_store(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds = g->ds;

    r128_a64_fmul_4s(&g->e, 25, 25, 23);
    r128_a64_fadd_4s(&g->e, 25, 25, 24);
    r128_a64_fcvtzu_4s(&g->e, 25, 25);
    if (g->dith_on && ds->dst_dt == 4) {
        /* r3d_dq on the integer lanes before the pack: add {bay>>1,
           bay>>2, bay>>1, 0} to {r,g,b,a} and saturate at 255, the 565
           case of r3d_dst_write. The C instead dithers bytes taken back
           out of the packed out word. The two agree as long as no
           channel exceeds 255 before the pack, which needs col >=
           255.5/255 (about 1.002). Nothing in the pixel path gets that
           far above 1.0: e0+e1+e2 <= area2i with invs = 1/area2i keeps
           the sum of the shading weights within a few ulp of 1; the
           texture combine and specular clamp at 1.0 or interpolate
           between values in [0,1]; fog interpolates; and blending clamps
           or wraps into [0,1]. */
        r128_a64_and_w_bitmask(&g->e, 25, 12, 3); /* px & 3       */
        r128_a64_lsl_w(&g->e, 25, 25, 3);
        r128_a64_lsrv_w(&g->e, 25, 6, 25);
        r128_a64_and_w_bitmask(&g->e, 25, 25, 0xff); /* bay         */
        r128_a64_lsr_w(&g->e, 28, 25, 1);            /* bay >> 1    */
        r128_a64_lsr_w(&g->e, 25, 25, 2);            /* bay >> 2    */
        r128_a64_ins_s_w(&g->e, 26, 0, 28);
        r128_a64_ins_s_w(&g->e, 26, 1, 25);
        r128_a64_ins_s_w(&g->e, 26, 2, 28);
        r128_a64_ins_s_w(&g->e, 26, 3, 31); /* a untouched */
        r128_a64_add_4s(&g->e, 25, 25, 26);
        r128_a64_umin_4s(&g->e, 25, 25, 28);
    }
    r128_a64_umov_w_s(&g->e, 16, 25, 3); /* a */
    r128_a64_lsl_w(&g->e, 16, 16, 24);
    r128_a64_umov_w_s(&g->e, 28, 25, 0); /* r */
    r128_a64_orr_w_lsl(&g->e, 16, 16, 28, 16);
    r128_a64_umov_w_s(&g->e, 28, 25, 1); /* g */
    r128_a64_orr_w_lsl(&g->e, 16, 16, 28, 8);
    r128_a64_umov_w_s(&g->e, 28, 25, 2); /* b */
    r128_a64_orr_w_lsl(&g->e, 16, 16, 28, 0);

    /* Store the color. A partial PLANE_3D_MASK (RRG: PLANE_3D_MASK_C,
       p. 3-260 / PDF 278) keeps the destination bits outside the mask,
       applied to the packed pixel after dithering as in r3d_dst_write. */
    if (ds->dst_dt == 6) {
        if (ds->wmask != 0xffffffffu) {
            r128_a64_ldr_w(&g->e, 28, 27, 0);
            r128_a64_mov_w_imm32(&g->e, 25, ds->wmask);
            r128_a64_and_w_reg(&g->e, 16, 16, 25);
            r128_a64_bic_w_reg(&g->e, 28, 28, 25);
            r128_a64_orr_w_lsl(&g->e, 16, 16, 28, 0);
        }
        r128_a64_str_w(&g->e, 16, 27, 0);
    } else if (ds->dst_dt == 3 || ds->dst_dt == 15) {
        /* Quantization uses the bytes of the packed ARGB word so any
           channel carry matches the interpreter before dithering.
           Alpha stays undithered for 1555 and uses the full Bayer
           threshold for 4444. The mask merges the final 16-bit pixel. */
        int bits = ds->dst_dt == 3 ? 5 : 4;

        for (int ch = 0; ch < 4; ch++) {
            int byte = ch == 3 ? 24 : (2 - ch) * 8;

            r128_a64_ubfx_w(&g->e, 25, 16, byte, 8);
            r128_a64_ins_s_w(&g->e, 26, ch, 25);
        }
        if (g->dith_on) {
            r128_a64_and_w_bitmask(&g->e, 25, 12, 3);
            r128_a64_lsl_w(&g->e, 25, 25, 3);
            r128_a64_lsrv_w(&g->e, 25, 6, 25);
            r128_a64_and_w_bitmask(&g->e, 25, 25, 0xff);
            if (ds->dst_dt == 3)
                r128_a64_lsr_w(&g->e, 25, 25, 1);
            r128_a64_dup_4s_w(&g->e, 25, 25);
            if (ds->dst_dt == 3)
                r128_a64_ins_s_w(&g->e, 25, 3, 31);
            r128_a64_add_4s(&g->e, 26, 26, 25);
            r128_a64_umin_4s(&g->e, 26, 26, 28);
        }
        r128_a64_umov_w_s(&g->e, 25, 26, 3);
        r128_a64_lsr_w(&g->e, 25, 25, ds->dst_dt == 3 ? 7 : 4);
        r128_a64_lsl_w(&g->e, 25, 25, bits * 3);
        for (int ch = 0; ch < 3; ch++) {
            r128_a64_umov_w_s(&g->e, 28, 26, ch);
            r128_a64_lsr_w(&g->e, 28, 28, 8 - bits);
            r128_a64_orr_w_lsl(&g->e, 25, 25, 28, (2 - ch) * bits);
        }
        if ((ds->wmask & 0xffff) != 0xffff) {
            r128_a64_ldrh(&g->e, 28, 27, 0);
            r128_a64_mov_w_imm32(&g->e, 16, ds->wmask & 0xffff);
            r128_a64_and_w_reg(&g->e, 25, 25, 16);
            r128_a64_bic_w_reg(&g->e, 28, 28, 16);
            r128_a64_orr_w_lsl(&g->e, 25, 25, 28, 0);
        }
        r128_a64_strh(&g->e, 25, 27, 0);
    } else {
        /* 565: raw = out[23:19]<<11 | out[15:10]<<5 | out[7:3], which is
           what the C's ((r << 8) & 0xf800) | ((g << 3) & 0x07e0) |
           (b >> 3) gives for the bytes r, g and b of out */
        r128_a64_ubfx_w(&g->e, 25, 16, 19, 5);
        r128_a64_lsl_w(&g->e, 25, 25, 11);
        r128_a64_ubfx_w(&g->e, 28, 16, 10, 6);
        r128_a64_orr_w_lsl(&g->e, 25, 25, 28, 5);
        r128_a64_ubfx_w(&g->e, 28, 16, 3, 5);
        r128_a64_orr_w_lsl(&g->e, 25, 25, 28, 0);
        if ((ds->wmask & 0xffff) != 0xffff) {
            r128_a64_ldrh(&g->e, 28, 27, 0);
            r128_a64_mov_w_imm32(&g->e, 16, ds->wmask & 0xffff);
            r128_a64_and_w_reg(&g->e, 25, 25, 16);
            r128_a64_bic_w_reg(&g->e, 28, 28, 16);
            r128_a64_orr_w_lsl(&g->e, 25, 25, 28, 0);
        }
        r128_a64_strh(&g->e, 25, 27, 0);
    }

    /* rx0 = px if rx0 is still -1; rx1 = px */
    r128_a64_cmn_w_imm(&g->e, 23, 1);
    r128_a64_csel_w(&g->e, 23, 12, 23, A64_EQ);
    r128_a64_mov_w(&g->e, 24, 12);
}

/* Z write through the cell the test read. The 32-bit form keeps the
   bits outside zmax << zshift (the stencil byte) with an AND, or is a
   plain store when there are none. */
static void
r128_a64_gen_zwrite(r128_a64_gen_t *g)
{
    const rage128_draw_state_t *ds = g->ds;

    if (ds->zbpp == 2)
        r128_a64_strh(&g->e, 17, 26, 0);
    else {
        uint32_t keep = ~(ds->zmax << ds->zshift);

        if (keep == 0) {
            if (ds->zshift) {
                r128_a64_lsl_w(&g->e, 28, 17, ds->zshift);
                r128_a64_str_w(&g->e, 28, 26, 0);
            } else
                r128_a64_str_w(&g->e, 17, 26, 0);
        } else {
            r128_a64_ldr_w(&g->e, 28, 26, 0);
            r128_a64_and_w_bitmask(&g->e, 28, 28, keep);
            r128_a64_orr_w_lsl(&g->e, 28, 28, 17, ds->zshift);
            r128_a64_str_w(&g->e, 28, 26, 0);
        }
    }
}

/* pix_skip: patch every recorded skip branch to here, then step the
   accumulators as the C loop's increment does (e0..e2 by their x steps,
   zline by dZdx, px by 1). The branch kind is read back from the
   emitted word: the test-and-branch pair TBZ and TBNZ (bits [30:25] are
   011011; bit 24 is the op bit that tells them apart and bit 31 the b5
   half of the bit number, so both are left out of the mask) take the
   14-bit field, B the 26-bit field, and otherwise B.cond or CBZ, which
   share the 19-bit field. The mask keeps CBZ and CBNZ (bits [30:25]
   011010) and B.cond (0x54 at [31:24]) out of the 14-bit case. The read
   is skipped once the block has overflowed, because a branch recorded
   after the overflow was never written and its position can be at the
   end of the buffer; the patch functions do nothing on an overflowed
   block anyway. */
static void
r128_a64_gen_pixskip(r128_a64_gen_t *g)
{
    int fix = r128_a64_here(&g->e);

    for (int i = 0; i < g->nskip && !g->e.overflow; i++) {
        uint32_t insn;

        memcpy(&insn, g->e.base + g->skips[i], 4);
        if ((insn & 0x7E000000u) == 0x36000000u)
            r128_a64_patch14(&g->e, g->skips[i], fix);
        else if ((insn & 0xFC000000u) == 0x14000000u)
            r128_a64_patch26(&g->e, g->skips[i], fix);
        else
            r128_a64_patch19(&g->e, g->skips[i], fix);
    }
    r128_a64_add_x_reg(&g->e, 1, 1, 7);
    r128_a64_add_x_reg(&g->e, 2, 2, 8);
    r128_a64_add_x_reg(&g->e, 3, 3, 9);
    if (g->z_step)
        r128_a64_fadd_d(&g->e, 0, 0, 2);
    r128_a64_add_w_imm(&g->e, 12, 12, 1);
    r128_a64_b_to(&g->e, g->loop_head);
}

/* Exit: return (rx1 << 32) | rx0 and restore the saved registers. Blocks
   with an untextured SoA loop get a second exit for rows the auxiliary
   scissors reject. */
static void
r128_a64_gen_epilogue(r128_a64_gen_t *g)
{
    int frame = g->frame, soa_on = g->soa_on, tex_inline = g->tex_inline;
    int aux_row_out = g->aux_row_out;

    r128_a64_patch19(&g->e, g->b_done, r128_a64_here(&g->e));
    if (aux_row_out >= 0 && !(soa_on && !tex_inline))
        r128_a64_patch19(&g->e, aux_row_out, r128_a64_here(&g->e));
    r128_a64_mov_w(&g->e, 0, 23);
    r128_a64_orr_x_lsl(&g->e, 0, 0, 24, 32);
    if (tex_inline)
        r128_a64_emit_tex_epilogue(&g->e); /* d8-d15 */
    if (soa_on && !tex_inline)
        r128_a64_emit_soa_epilogue(&g->e); /* d8-d15 (textured SoA blocks
                                              restore them in the texture
                                              epilogue) */
    r128_a64_ldp_x(&g->e, 19, 20, 16);
    r128_a64_ldp_x(&g->e, 21, 22, 32);
    r128_a64_ldp_x(&g->e, 23, 24, 48);
    r128_a64_ldp_x(&g->e, 25, 26, 64);
    r128_a64_ldp_x(&g->e, 27, 28, 80);
    if (frame > 504) {
        /* LDP post-index reaches at most +504 while STP pre-index
           reaches -512, so a 512-byte frame splits only here, in the
           epilogue */
        r128_a64_ldp_x(&g->e, 29, 30, 0);
        r128_a64_add_x_imm(&g->e, 31, 31, frame);
    } else
        r128_a64_ldp_x_post(&g->e, 29, 30, frame);
    r128_a64_ret(&g->e);

    if (aux_row_out >= 0 && soa_on && !tex_inline) {
        /* Untextured SoA blocks save d8-d15 in the SoA prologue, which
           runs after the auxiliary-scissor row test. A row the test
           rejects leaves before that save, so the main epilogue's
           d8-d15 restore would load unsaved frame bytes into the
           caller's callee-saved registers. This exit is the same
           epilogue without the d8-d15 restore; the block has not
           touched those registers on this path. Inline-texture blocks
           save d8-d15 and load their texture constants into them in the
           prologue, before the row test, so their rejected rows use the
           main epilogue. */
        r128_a64_patch19(&g->e, aux_row_out, r128_a64_here(&g->e));
        r128_a64_mov_w(&g->e, 0, 23);
        r128_a64_orr_x_lsl(&g->e, 0, 0, 24, 32);
        r128_a64_ldp_x(&g->e, 19, 20, 16);
        r128_a64_ldp_x(&g->e, 21, 22, 32);
        r128_a64_ldp_x(&g->e, 23, 24, 48);
        r128_a64_ldp_x(&g->e, 25, 26, 64);
        r128_a64_ldp_x(&g->e, 27, 28, 80);
        if (frame > 504) {
            r128_a64_ldp_x(&g->e, 29, 30, 0);
            r128_a64_add_x_imm(&g->e, 31, 31, frame);
        } else
            r128_a64_ldp_x_post(&g->e, 29, 30, frame);
        r128_a64_ret(&g->e);
    }
}

static int
r128_jit_arm64_generate_1(uint8_t *code, const rage128_draw_state_t *ds,
                          int no_soa)
{
    r128_a64_gen_t g;

    r128_a64_gen_setup(&g, code, ds, no_soa);

    if (!r128_jit_arm64_can(ds))
        return -2; /* rejected state; -1 is reserved for overflow */

    if (r128_a64_never_pass(ds)) {
        /* Nothing can be written: return rx0 = rx1 = -1 at once, with
           no frame. */
        r128_a64_movn_x(&g.e, 0, 0);
        r128_a64_ret(&g.e);
        return g.e.pos;
    }

    r128_a64_gen_prologue(&g);
    if (g.aux_on)
        r128_a64_gen_row_aux(&g);
    if (ds->stip_en)
        r128_a64_gen_row_stipple(&g);
    if (g.dith_on)
        r128_a64_gen_row_dither(&g);
    if (g.tex_on && !g.tex_inline)
        r128_a64_gen_row_texspill(&g);
    r128_a64_gen_soa(&g);
    r128_a64_gen_loop_head(&g);
    r128_a64_gen_coverage(&g);
    if (g.z_on || g.sten_on)
        r128_a64_gen_zsten(&g);
    r128_a64_gen_color(&g);
    r128_a64_gen_texture(&g);
    if (ds->spec_en)
        r128_a64_gen_spec(&g);
    if (ds->fog_en)
        r128_a64_gen_fog(&g);
    if (ds->atest_en && (ds->atest_fn & 7) != 7)
        r128_a64_gen_atest(&g);
    if (g.sten_on)
        r128_a64_gen_sten_update(&g);
    r128_a64_gen_dcell_blend(&g);
    r128_a64_gen_pack_store(&g);
    if (g.z_on && ds->z_wr)
        r128_a64_gen_zwrite(&g);
    r128_a64_gen_pixskip(&g);
    r128_a64_gen_epilogue(&g);

    if (g.e.overflow)
        return -1;
    return g.e.pos;
}

/* The SoA gates size a block from per-stage estimates
   (r128_a64_soa_stage_weight) meant to keep it under the 16 KB buffer.
   They are estimates, so a state they admit can still overflow. A block
   that overflows (-1) is generated again without the SoA loop rather
   than leaving the state to the interpreter. A block that fits is
   returned from the first pass, and a refused state (-2) is not
   retried, since the second pass would refuse it too. */
static int
r128_jit_arm64_generate(uint8_t *code, const rage128_draw_state_t *ds)
{
    int len = r128_jit_arm64_generate_1(code, ds, 0);

    if (len == -1)
        len = r128_jit_arm64_generate_1(code, ds, 1);
    return len;
}

#endif /* VIDEO_ATI_RAGE128_CODEGEN_ARM64_H */

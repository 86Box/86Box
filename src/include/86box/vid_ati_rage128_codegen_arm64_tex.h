/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- span JIT code generator for ARM64, inline
 *          texture stage.
 *
 *          A compiled span either calls the interpreter's
 *          rage128_texstage_run (vid_ati_rage128_3d.c) for each pixel or
 *          samples and combines in line with the code emitted here. The
 *          inline code repeats rage128_texstage_run operation by
 *          operation: the texel decode of every datatype the interpreter
 *          decodes, the four clamp modes, nearest and bilinear texel
 *          filters, the mipmap paths, the whole texture combine code
 *          book, the texture-lighting pass and both chroma keys. A state
 *          with a stage of datatype 10 or 13 keeps the call.
 *
 *          The emitted code reads the per-triangle values (coordinate
 *          planes, gradients, mip-level descriptors) from the
 *          r3d_texctx_t that r128_jit_tri_t.texctx points to. It reads
 *          the level descriptors in r3d_stage_desc_t.slot[] directly,
 *          without the call that fills a slot on first use, so
 *          rage128_3d_tri fills slots 0 to top of each enabled stage
 *          before it runs a block for which rage128_jit_tex_inline is
 *          true.
 *
 *          vid_ati_rage128_codegen_x86_64_tex.h is the x86-64
 *          counterpart. Both take the same gate, r128_jit_texinline_can
 *          in vid_ati_rage128_codegen_rules.h.
 *
 *          Included only by vid_ati_rage128_codegen_arm64.h.
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
#ifndef VIDEO_ATI_RAGE128_CODEGEN_ARM64_TEX_H
#define VIDEO_ATI_RAGE128_CODEGEN_ARM64_TEX_H

/* ------------------------------------------------------------------------
 * Register map of the inline texture stage. It adds to the map in
 * vid_ati_rage128_codegen_arm64.h and applies only in blocks that sample
 * inline. Those blocks save d8-d15 in their prologue (AAPCS64 makes the
 * low 64 bits of v8-v15 callee-saved) and use x29 and x30 as scratch
 * between prologue and epilogue. The block's own BL subroutines use w30
 * inside, so they park the return address elsewhere: the S3TC and YUV
 * decode subroutines in v31.d[0], the SoA coordinate subroutines in a
 * frame slot. x29 is not a valid frame pointer while such a block runs;
 * the block calls no outside function, so only a debugger or profiler
 * walking the stack would notice.
 *
 *   x16 texctx, loaded for each pixel  v8  0x00ff00ff in each lane
 *   x25, x27..x30 scratch                  (lerp channel mask)
 *   w17 (zi) and x26 (Z cell) are      v9  0x00800080 in each lane
 *       not touched                        (lerp rounding term)
 *   s16..s18 weights, read only        v10 {texw0, texh0, texw1, texh1}
 *   s14 ir (1/rhw), s15 rhw            v11 {4096.0f, 2^-12, lod_bias,
 *   v25 running color {r,g,b,a}             -1000.0f}
 *   v23 255.0f in each lane (parent)   v12 log2 polynomial {c0..c3}
 *   v3, v7, v26, v27, v29..v31 scratch v13 {256.0f, 0.5f, 1.0f, 127.0f}
 * ---------------------------------------------------------------------- */

/* Frame slots of the inline stage, offsets from SP. They reuse the area
   from 96 up that blocks calling rage128_texstage_run spend on their
   spills; a block that samples inline makes no such call. LW, LH, BASE
   and MASK hold the sampled level's descriptor values, UU/VV the
   wrapped texel coordinates of the bilinear footprint, WU/WV the 8.8
   lerp weights, S/T the stage's texture coordinates, F the mip-blend
   fraction, CA the first level's texel and SLOTB the second level's
   slot while a two-level blend runs. */
/* clang-format off */
#define R128_A64_TS_UU0   96
#define R128_A64_TS_UU1   100
#define R128_A64_TS_VV0   104
#define R128_A64_TS_VV1   108
#define R128_A64_TS_BASE  112
#define R128_A64_TS_MASK  116
#define R128_A64_TS_LH    120
#define R128_A64_TS_WU    124
#define R128_A64_TS_WV    128
#define R128_A64_TS_S     132
#define R128_A64_TS_T     136
#define R128_A64_TS_F     140
#define R128_A64_TS_CA    144
#define R128_A64_TS_SLOTB 148
#define R128_A64_TS_LW    152
#define R128_A64_TS_TNEAR 156 /* chroma-key compare value of the nearest
                                 texel */
#define R128_A64_TS_INTC  160 /* q: the color entering stage 0, stored
                                 for each pixel; the combine's
                                 interpolated-color selects read it */
#define R128_A64_TS_PAL   176 /* x: CI4/CI8 palette pointer */
/* clang-format on */
/* d8-d15 save area, above the 272 bytes of R128_A64_FRAME, and the
   frame size of a block that samples inline */
#define R128_A64_SP_D8    272
#define R128_A64_FRAME_TI 336

/* The emitted code reads a level descriptor (struct r3d_slot_desc_t) at
   literal offsets: lw at 0, lh at 4, texbase at 8, base at 16, mask at
   20. The SoA emitters of both architectures use the same literals, so
   a layout change must be checked in each of them. The stride through
   slot[] comes from sizeof; this typedef stops the build when that size
   is not 24. */
typedef char r128_a64_slotdesc_size_check[(sizeof(struct r3d_slot_desc_t) == 24) ? 1 : -1];

/* Instruction emitters this stage adds to the parent's. Each writes one
   A64 instruction (one or two for the mov helpers at the end), named
   by its mnemonic and operand form: _w a 32-bit general register, _x a
   64-bit one, _s a single-precision scalar, _d a double, _4s four
   32-bit lanes, _16b the whole 128-bit vector. */

static void
r128_a64_ucvtf_s_w(r128_a64_emit_t *e, int sd, int wn)
{
    r128_a64_e32(e, 0x1E230000 | (wn << 5) | sd);
}

static void
r128_a64_scvtf_s_w(r128_a64_emit_t *e, int sd, int wn)
{
    r128_a64_e32(e, 0x1E220000 | (wn << 5) | sd);
}

static void
r128_a64_fcvtms_w_s(r128_a64_emit_t *e, int wd, int sn) /* (int) floorf: round toward minus infinity */
{
    r128_a64_e32(e, 0x1E300000 | (sn << 5) | wd);
}

static void
r128_a64_fcvtzs_w_s(r128_a64_emit_t *e, int wd, int sn) /* (int) cast: round toward zero */
{
    r128_a64_e32(e, 0x1E380000 | (sn << 5) | wd);
}

static void
r128_a64_frintn_s(r128_a64_emit_t *e, int sd, int sn) /* nearbyintf: nearest, ties to even */
{
    r128_a64_e32(e, 0x1E244000 | (sn << 5) | sd);
}

static void
r128_a64_csinv_w(r128_a64_emit_t *e, int rd, int rn, int rm, int cond)
{
    r128_a64_e32(e, 0x5A800000 | (rm << 16) | (cond << 12) | (rn << 5) | rd);
}

static void
r128_a64_fdiv_s(r128_a64_emit_t *e, int sd, int sn, int sm)
{
    r128_a64_e32(e, 0x1E201800 | (sm << 16) | (sn << 5) | sd);
}

static void
r128_a64_fmov_w_s(r128_a64_emit_t *e, int wd, int sn)
{
    r128_a64_e32(e, 0x1E260000 | (sn << 5) | wd);
}

/* rd = ra + rn*rm; ra = 31 reads WZR, which makes it a plain multiply */
static void
r128_a64_madd_w(r128_a64_emit_t *e, int rd, int rn, int rm, int ra)
{
    r128_a64_e32(e, 0x1B000000 | (rm << 16) | (ra << 10) | (rn << 5) | rd);
}

static void
r128_a64_msub_w(r128_a64_emit_t *e, int rd, int rn, int rm, int ra) /* rd = ra - rn*rm */
{
    r128_a64_e32(e, 0x1B008000 | (rm << 16) | (ra << 10) | (rn << 5) | rd);
}

/* ASR Wd, Wn by the immediate sh, encoded as the SBFM it aliases, with
   immr = sh and imms = 31 */
static void
r128_a64_asr_w(r128_a64_emit_t *e, int rd, int rn, int sh)
{
    r128_a64_e32(e, 0x13007C00 | ((uint32_t) sh << 16) | (rn << 5) | rd);
}

static void
r128_a64_sub_w_imm(r128_a64_emit_t *e, int rd, int rn, int imm)
{
    r128_a64_e32(e, 0x51000000 | ((uint32_t) imm << 10) | (rn << 5) | rd);
}

static void
r128_a64_eor_w_reg(r128_a64_emit_t *e, int rd, int rn, int rm)
{
    r128_a64_e32(e, 0x4A000000 | (rm << 16) | (rn << 5) | rd);
}

/* ORR Wd, Wn, #v with v as a logical immediate, found by the parent's
   r128_a64_bitmask32. A value it cannot encode sets e->overflow, so the
   block fails and the state stays on the interpreter; the constants
   this file passes all encode. */
static void
r128_a64_orr_w_bitmask(r128_a64_emit_t *e, int rd, int rn, uint32_t v)
{
    uint32_t immr, imms;

    if (!r128_a64_bitmask32(v, &immr, &imms)) {
        e->overflow = 1;
        return;
    }
    r128_a64_e32(e, 0x32000000 | (immr << 16) | (imms << 10) | (rn << 5) | rd);
}

static void
r128_a64_dup_4s_w(r128_a64_emit_t *e, int vd, int wn)
{
    r128_a64_e32(e, 0x4E040C00 | (wn << 5) | vd);
}

static void
r128_a64_mul_4s(r128_a64_emit_t *e, int vd, int vn, int vm) /* integer */
{
    r128_a64_e32(e, 0x4EA09C00 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_mla_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4EA09400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_sub_4s(r128_a64_emit_t *e, int vd, int vn, int vm) /* integer */
{
    r128_a64_e32(e, 0x6EA08400 | (vm << 16) | (vn << 5) | vd);
}

/* Vector shifts by an immediate on 32-bit lanes. The shift sits in
   immh:immb (bits 22:16): USHR encodes 64 - sh, SHL encodes 32 + sh. */
static void
r128_a64_ushr_4s(r128_a64_emit_t *e, int vd, int vn, int sh)
{
    r128_a64_e32(e, 0x6F000400 | ((uint32_t) (64 - sh) << 16) | (vn << 5) | vd);
}

static void
r128_a64_shl_4s(r128_a64_emit_t *e, int vd, int vn, int sh)
{
    r128_a64_e32(e, 0x4F005400 | ((uint32_t) (32 + sh) << 16) | (vn << 5) | vd);
}

static void
r128_a64_orr_16b(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4EA01C00 | (vm << 16) | (vn << 5) | vd);
}

/* MOVI Vd.4S with imm8 shifted left by 8 in each lane (cmode 0010):
   imm8 bits 7:5 go to bits 18:16 of the instruction, bits 4:0 to bits
   9:5. */
static void
r128_a64_movi_4s_lsl8(r128_a64_emit_t *e, int vd, uint32_t imm8)
{
    r128_a64_e32(e, 0x4F002400 | (((imm8 >> 5) & 7) << 16) | ((imm8 & 31) << 5) | vd);
}

/* STR St, [Xn, #off] and LDRB Wt, [Xn, #off], unsigned-offset forms:
   the 12-bit offset field is scaled by the access size, 4 and 1. */
static void
r128_a64_str_s(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0xBD000000 | ((uint32_t) (off >> 2) << 10) | (rn << 5) | rt);
}

static void
r128_a64_ldrb(r128_a64_emit_t *e, int rt, int rn, int off)
{
    r128_a64_e32(e, 0x39400000 | ((uint32_t) off << 10) | (rn << 5) | rt);
}

static void
r128_a64_udiv_w(r128_a64_emit_t *e, int rd, int rn, int rm)
{
    r128_a64_e32(e, 0x1AC00800 | (rm << 16) | (rn << 5) | rd);
}

static void
r128_a64_bl_to(r128_a64_emit_t *e, int target) /* BL to an offset already emitted */
{
    r128_a64_e32(e, 0x94000000 | (((uint32_t) ((target - e->pos) >> 2)) & 0x3ffffff));
}

/* LSRV Wd, Wn, Wm: the shift amount is Wm modulo 32 */
static void
r128_a64_lsr_w_reg(r128_a64_emit_t *e, int rd, int rn, int rm)
{
    r128_a64_e32(e, 0x1AC02400 | (rm << 16) | (rn << 5) | rd);
}

static void
r128_a64_fmov_x_d(r128_a64_emit_t *e, int xd, int dn)
{
    r128_a64_e32(e, 0x9E660000 | (dn << 5) | xd);
}

/* Wd = v: MOVZ of the low half, then MOVK of the high half when it is
   not zero */
static void
r128_a64_mov_w_imm32(r128_a64_emit_t *e, int rd, uint32_t v)
{
    r128_a64_movz_w(e, rd, v & 0xffff, 0);
    if (v >> 16)
        r128_a64_movk_w(e, rd, v >> 16, 1);
}

/* Wd = the bit pattern of f: MOVZ of the low half and MOVK of the high
   half, or a single MOVZ into the high half when the low half is
   zero. */
static void
r128_a64_mov_w_fbits(r128_a64_emit_t *e, int rd, float f)
{
    uint32_t b;

    memcpy(&b, &f, 4);
    if (b & 0xffff) {
        r128_a64_movz_w(e, rd, b & 0xffff, 0);
        r128_a64_movk_w(e, rd, b >> 16, 1);
    } else
        r128_a64_movz_w(e, rd, b >> 16, 1);
}

/* ------------------------------------------------------------------------
 * S3TC texel decode (datatype 0), r3d_texel case 0 operation by
 * operation. It is emitted once per stage as a subroutine inside the
 * block and entered with BL from every fetch site. A stage has up to 14
 * fetch sites (five for the first level of a mip blend, four for the
 * second, five for the magnify path), so a copy at each site would not
 * fit the 16 KB block. BL and RET are safe because blocks that sample
 * inline treat x29 and x30 as scratch and the subroutine calls nothing.
 *
 * In: w25 = u, w27 = v (wrapped, or -1 on a border-mode miss, whose
 * result the caller then replaces with the border color); R128_A64_TS_LW,
 * R128_A64_TS_BASE and R128_A64_TS_MASK hold the sampled level;
 * x28 = texbase. Out: w27 = the texel as ARGB8888. Clobbers w25, x29,
 * x30, all of q31 and the frame words at 176 and 180. Those words are
 * the CI palette pointer's slot: a stage is either CI or S3TC, and a CI
 * stage stores its palette pointer each time it sets up a level.
 *
 * The third scratch word is the frame word at 184 except where that
 * word is the SoA loop's R128_A64_SP_SOA_WM slot, live across the two
 * fetch passes: in blocks whose SoA loop has a trilinear or
 * filter-split stage (vecwm), the word is kept in v31.s[2] instead,
 * beside the return address in v31.d[0].
 *
 * The block class h->s3tc is part of the draw state, so each class gets
 * its own body. Classes 0 and 1 both take the DXT1 decode, as in
 * r3d_texel.
 * ---------------------------------------------------------------------- */
/* clang-format off */
#define R128_A64_TS_DXT0 176 /* block offset, then color-block offset,
                                then the first expanded endpoint */
#define R128_A64_TS_DXT1 180 /* texel index in the block, then the
                                packed ARGB result being built */
#define R128_A64_TS_DXT2 184 /* class 3: the alpha selector's bit
                                position, then the decoded alpha (class
                                2 and 3); v31.s[2] when vecwm */
/* clang-format on */

/* Store and load of the third scratch word: the frame word at 184, or
   v31.s[2] under vecwm. Writing the return address with FMOV Dd, Xn
   zeroes v31 bits 127:64, so lane 2 is free once it is parked. */
static void
r128_a64_emit_dxt_sw_st(r128_a64_emit_t *e, int wn, int vecwm)
{
    if (vecwm)
        r128_a64_ins_s_w(e, 31, 2, wn);
    else
        r128_a64_str_w(e, wn, 31, R128_A64_TS_DXT2);
}

static void
r128_a64_emit_dxt_sw_ld(r128_a64_emit_t *e, int wd, int vecwm)
{
    if (vecwm)
        r128_a64_umov_w_s(e, wd, 31, 2);
    else
        r128_a64_ldr_w(e, wd, 31, R128_A64_TS_DXT2);
}

/* w29 = the byte (size 0), halfword (1) or word (2) at
   texbase + ((base + w29) & mask). As in r3d_texel, only the start
   address is masked; the bytes after it are read unmasked. Clobbers
   w30. */
static void
r128_a64_emit_dxt_fetch(r128_a64_emit_t *e, int size)
{
    r128_a64_ldr_w(e, 30, 31, R128_A64_TS_BASE);
    r128_a64_add_w_lsl(e, 29, 30, 29, 0);
    r128_a64_ldr_w(e, 30, 31, R128_A64_TS_MASK);
    r128_a64_and_w_reg(e, 29, 29, 30);
    r128_a64_add_x_uxtw(e, 29, 28, 29);
    if (size == 0)
        r128_a64_ldrb(e, 29, 29, 0);
    else if (size == 1)
        r128_a64_ldrh(e, 29, 29, 0);
    else
        r128_a64_ldr_w(e, 29, 29, 0);
}

/* w29 = ((src >> lo) & (2^width - 1)) * 255 / (2^width - 1), the same
   integer multiply and UDIV as the C endpoint expansion. Clobbers
   w30. */
static void
r128_a64_emit_dxt_expand(r128_a64_emit_t *e, int src, int lo, int width)
{
    r128_a64_ubfx_w(e, 29, src, lo, width);
    r128_a64_movz_w(e, 30, 255, 0);
    r128_a64_madd_w(e, 29, 29, 30, 31);
    r128_a64_movz_w(e, 30, (1u << width) - 1u, 0);
    r128_a64_udiv_w(e, 29, 29, 30);
}

/* The result word at R128_A64_TS_DXT1 |= w29 << shift */
static void
r128_a64_emit_dxt_pack(r128_a64_emit_t *e, int shift)
{
    r128_a64_ldr_w(e, 30, 31, R128_A64_TS_DXT1);
    r128_a64_orr_w_lsl(e, 30, 30, 29, shift);
    r128_a64_str_w(e, 30, 31, R128_A64_TS_DXT1);
}

/* One channel of the two-thirds blend for selectors 2 and 3:
   w29 = (2 * e_hi + e_lo) / 3, where e_hi is the channel expanded from
   the RGB565 endpoint in register whi and e_lo the one from wlo. chan
   picks the field: 0 red (bits 15:11), 1 green (10:5), 2 blue (4:0). */
static void
r128_a64_emit_dxt_third(r128_a64_emit_t *e, int whi, int wlo, int chan)
{
    static const int lo[3] = { 11, 5, 0 }, wd[3] = { 5, 6, 5 };

    r128_a64_emit_dxt_expand(e, whi, lo[chan], wd[chan]);
    r128_a64_str_w(e, 29, 31, R128_A64_TS_DXT0);
    r128_a64_emit_dxt_expand(e, wlo, lo[chan], wd[chan]);
    r128_a64_ldr_w(e, 30, 31, R128_A64_TS_DXT0);
    r128_a64_add_w_lsl(e, 29, 29, 30, 1); /* e_lo + 2*e_hi */
    r128_a64_movz_w(e, 30, 3, 0);
    r128_a64_udiv_w(e, 29, 29, 30);
}

/* One channel of the DXT1 selector-2 average when c0 <= c1:
   w29 = (e0 + e1) >> 1, endpoints in w25 and w27 */
static void
r128_a64_emit_dxt_half(r128_a64_emit_t *e, int chan)
{
    static const int lo[3] = { 11, 5, 0 }, wd[3] = { 5, 6, 5 };

    r128_a64_emit_dxt_expand(e, 25, lo[chan], wd[chan]);
    r128_a64_str_w(e, 29, 31, R128_A64_TS_DXT0);
    r128_a64_emit_dxt_expand(e, 27, lo[chan], wd[chan]);
    r128_a64_ldr_w(e, 30, 31, R128_A64_TS_DXT0);
    r128_a64_add_w_lsl(e, 29, 29, 30, 0);
    r128_a64_lsr_w(e, 29, 29, 1);
}

/* Start the result word of a color arm with the alpha byte: the decoded
   alpha for classes 2 and 3, 0xff for the others. The arm then ORs in
   red, green and blue. */
static void
r128_a64_emit_dxt_arm_seed(r128_a64_emit_t *e, uint32_t cls, int vecwm)
{
    if (cls >= 2) {
        r128_a64_emit_dxt_sw_ld(e, 30, vecwm);
        r128_a64_lsl_w(e, 30, 30, 24);
    } else
        r128_a64_movz_w(e, 30, 0xff00, 1);
    r128_a64_str_w(e, 30, 31, R128_A64_TS_DXT1);
}

/* Emit the S3TC decode subroutine for one stage; returns its entry
   offset. */
static int
r128_a64_emit_dxt_sub(r128_a64_emit_t *e, const r3d_stage_hdr_t *h, int vecwm)
{
    uint32_t cls   = h->s3tc;
    int      entry = r128_a64_here(e);
    int      b_s0, b_s1, b_s2h = -1, b_s3z = -1;
    int      arm2, arm3;

    /* w30 is scratch in the body, so the return address waits in
       v31.d[0] until each RET; the callers keep nothing in v31 across a
       fetch */
    r128_a64_fmov_d_x(e, 31, 30);
    /* boff = ((v >> 2) * bpitch + (u >> 2)) * block size (8 bytes, or
       16 for classes 2 and 3), bpitch = (lw + 3) >> 2 blocks;
       texidx = (v & 3) << 2 | (u & 3) */
    r128_a64_ldr_w(e, 29, 31, R128_A64_TS_LW);
    r128_a64_add_w_imm(e, 29, 29, 3);
    r128_a64_lsr_w(e, 29, 29, 2); /* bpitch */
    r128_a64_lsr_w(e, 30, 27, 2);
    r128_a64_madd_w(e, 30, 30, 29, 31);
    r128_a64_lsr_w(e, 29, 25, 2);
    r128_a64_add_w_lsl(e, 30, 30, 29, 0);
    r128_a64_lsl_w(e, 30, 30, (cls >= 2) ? 4 : 3);
    r128_a64_str_w(e, 30, 31, R128_A64_TS_DXT0);
    r128_a64_and_w_bitmask(e, 29, 27, 3);
    r128_a64_and_w_bitmask(e, 30, 25, 3);
    r128_a64_orr_w_lsl(e, 30, 30, 29, 2);
    r128_a64_str_w(e, 30, 31, R128_A64_TS_DXT1);

    if (cls == 2) { /* DXT2/3: 4-bit alpha per texel, two per byte, low
                       nibble first; a = nibble * 0x11 */
        r128_a64_ldr_w(e, 29, 31, R128_A64_TS_DXT0);
        r128_a64_lsr_w(e, 25, 30, 1);
        r128_a64_add_w_lsl(e, 29, 29, 25, 0);
        r128_a64_emit_dxt_fetch(e, 0);
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_DXT1);
        r128_a64_tst_w_imm(e, 30, 1);
        r128_a64_lsr_w(e, 25, 29, 4);
        r128_a64_and_w_bitmask(e, 29, 29, 0xfu);
        r128_a64_csel_w(e, 29, 25, 29, A64_NE);
        r128_a64_movz_w(e, 25, 0x11, 0);
        r128_a64_madd_w(e, 29, 29, 25, 31);
        r128_a64_emit_dxt_sw_st(e, 29, vecwm);
    } else if (cls == 3) { /* DXT4/5: two alpha endpoints and a 3-bit
                              selector per texel */
        int a_end[4];
        int n = 0, b_gt, b_c6, b_c7;

        r128_a64_ldr_w(e, 29, 31, R128_A64_TS_DXT0);
        r128_a64_emit_dxt_fetch(e, 0);
        r128_a64_mov_w(e, 25, 29); /* a0 */
        r128_a64_ldr_w(e, 29, 31, R128_A64_TS_DXT0);
        r128_a64_add_w_imm(e, 29, 29, 1);
        r128_a64_emit_dxt_fetch(e, 0);
        r128_a64_mov_w(e, 27, 29); /* a1 */
        /* ac = (word at boff + 2 + (3 * texidx >> 3))
                >> (3 * texidx & 7) & 7 */
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_DXT1);
        r128_a64_add_w_lsl(e, 30, 30, 30, 1); /* bp = 3*texidx */
        r128_a64_emit_dxt_sw_st(e, 30, vecwm);
        r128_a64_lsr_w(e, 29, 30, 3);
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_DXT0);
        r128_a64_add_w_lsl(e, 29, 30, 29, 0);
        r128_a64_add_w_imm(e, 29, 29, 2);
        r128_a64_emit_dxt_fetch(e, 2);
        r128_a64_emit_dxt_sw_ld(e, 30, vecwm);
        r128_a64_and_w_bitmask(e, 30, 30, 7);
        r128_a64_lsr_w_reg(e, 29, 29, 30);
        r128_a64_and_w_bitmask(e, 29, 29, 7); /* ac */
        /* the selector cases, tested in the order of the C if-chain */
        r128_a64_mov_w(e, 30, 25); /* default a = a0 */
        b_s0 = r128_a64_cbz_w(e, 29);
        r128_a64_cmp_w_imm(e, 29, 1);
        r128_a64_csel_w(e, 30, 27, 30, A64_EQ); /* a = a1 */
        b_s1 = r128_a64_bcond(e, A64_EQ);
        r128_a64_cmp_w_reg(e, 25, 27);
        b_gt = r128_a64_bcond(e, A64_HI); /* a0 > a1: the /7 case */
        r128_a64_cmp_w_imm(e, 29, 6);
        r128_a64_csel_w(e, 30, 31, 30, A64_EQ); /* a = 0 */
        b_c6 = r128_a64_bcond(e, A64_EQ);
        r128_a64_movz_w(e, 30, 255, 0);
        r128_a64_cmp_w_imm(e, 29, 7);
        b_c7 = r128_a64_bcond(e, A64_EQ); /* a = 255 */
        /* a = ((6-ac)*a0 + (ac-1)*a1)/5 */
        r128_a64_movz_w(e, 30, 6, 0);
        r128_a64_sub_w_reg(e, 30, 30, 29);
        r128_a64_madd_w(e, 30, 30, 25, 31);
        r128_a64_sub_w_imm(e, 29, 29, 1);
        r128_a64_madd_w(e, 30, 29, 27, 30);
        r128_a64_movz_w(e, 29, 5, 0);
        r128_a64_udiv_w(e, 30, 30, 29);
        a_end[n++] = r128_a64_b(e);
        r128_a64_patch19(e, b_gt, r128_a64_here(e));
        /* a = ((8-ac)*a0 + (ac-1)*a1)/7 */
        r128_a64_movz_w(e, 30, 8, 0);
        r128_a64_sub_w_reg(e, 30, 30, 29);
        r128_a64_madd_w(e, 30, 30, 25, 31);
        r128_a64_sub_w_imm(e, 29, 29, 1);
        r128_a64_madd_w(e, 30, 29, 27, 30);
        r128_a64_movz_w(e, 29, 7, 0);
        r128_a64_udiv_w(e, 30, 30, 29);
        a_end[n++] = r128_a64_b(e);
        r128_a64_patch19(e, b_s0, r128_a64_here(e));
        r128_a64_patch19(e, b_s1, r128_a64_here(e));
        r128_a64_patch19(e, b_c6, r128_a64_here(e));
        r128_a64_patch19(e, b_c7, r128_a64_here(e));
        for (int k = 0; k < n; k++)
            r128_a64_patch26(e, a_end[k], r128_a64_here(e));
        r128_a64_emit_dxt_sw_st(e, 30, vecwm);
    }

    /* color block: c0 -> w25, c1 -> w27, sel -> w29 */
    r128_a64_ldr_w(e, 29, 31, R128_A64_TS_DXT0);
    if (cls >= 2) {
        r128_a64_add_w_imm(e, 29, 29, 8); /* coff */
        r128_a64_str_w(e, 29, 31, R128_A64_TS_DXT0);
    }
    r128_a64_emit_dxt_fetch(e, 1);
    r128_a64_mov_w(e, 25, 29);
    r128_a64_ldr_w(e, 29, 31, R128_A64_TS_DXT0);
    r128_a64_add_w_imm(e, 29, 29, 2);
    r128_a64_emit_dxt_fetch(e, 1);
    r128_a64_mov_w(e, 27, 29);
    r128_a64_ldr_w(e, 29, 31, R128_A64_TS_DXT0);
    r128_a64_add_w_imm(e, 29, 29, 4);
    r128_a64_emit_dxt_fetch(e, 2);
    r128_a64_ldr_w(e, 30, 31, R128_A64_TS_DXT1);
    r128_a64_lsl_w(e, 30, 30, 1);
    r128_a64_lsr_w_reg(e, 29, 29, 30);
    r128_a64_and_w_bitmask(e, 29, 29, 3); /* sel */
    /* texidx is not needed again: R128_A64_TS_DXT1 now takes the
       result word */

    r128_a64_cmp_w_imm(e, 29, 2);
    arm2 = r128_a64_bcond(e, A64_EQ);
    r128_a64_cmp_w_imm(e, 29, 3);
    arm3 = r128_a64_bcond(e, A64_EQ);
    r128_a64_cmp_w_imm(e, 29, 1);
    r128_a64_csel_w(e, 25, 27, 25, A64_EQ); /* sel 1: endpoint = c1 */
    /* selectors 0 and 1: the expanded endpoint */
    r128_a64_emit_dxt_arm_seed(e, cls, vecwm);
    r128_a64_emit_dxt_expand(e, 25, 11, 5);
    r128_a64_emit_dxt_pack(e, 16);
    r128_a64_emit_dxt_expand(e, 25, 5, 6);
    r128_a64_emit_dxt_pack(e, 8);
    r128_a64_emit_dxt_expand(e, 25, 0, 5);
    r128_a64_emit_dxt_pack(e, 0);
    r128_a64_ldr_w(e, 27, 31, R128_A64_TS_DXT1);
    r128_a64_fmov_x_d(e, 30, 31);
    r128_a64_ret(e);

    /* selector 2: (2 * c0 + c1) / 3 per channel; for classes 0 and 1
       with c0 <= c1, (c0 + c1) >> 1 */
    r128_a64_patch19(e, arm2, r128_a64_here(e));
    if (cls < 2) {
        r128_a64_cmp_w_reg(e, 25, 27);
        b_s2h = r128_a64_bcond(e, A64_LS); /* c0 <= c1: average */
    }
    r128_a64_emit_dxt_arm_seed(e, cls, vecwm);
    r128_a64_emit_dxt_third(e, 25, 27, 0);
    r128_a64_emit_dxt_pack(e, 16);
    r128_a64_emit_dxt_third(e, 25, 27, 1);
    r128_a64_emit_dxt_pack(e, 8);
    r128_a64_emit_dxt_third(e, 25, 27, 2);
    r128_a64_emit_dxt_pack(e, 0);
    r128_a64_ldr_w(e, 27, 31, R128_A64_TS_DXT1);
    r128_a64_fmov_x_d(e, 30, 31);
    r128_a64_ret(e);
    if (cls < 2) {
        r128_a64_patch19(e, b_s2h, r128_a64_here(e));
        r128_a64_emit_dxt_arm_seed(e, cls, vecwm);
        r128_a64_emit_dxt_half(e, 0);
        r128_a64_emit_dxt_pack(e, 16);
        r128_a64_emit_dxt_half(e, 1);
        r128_a64_emit_dxt_pack(e, 8);
        r128_a64_emit_dxt_half(e, 2);
        r128_a64_emit_dxt_pack(e, 0);
        r128_a64_ldr_w(e, 27, 31, R128_A64_TS_DXT1);
        r128_a64_fmov_x_d(e, 30, 31);
        r128_a64_ret(e);
    }

    /* selector 3: (c0 + 2 * c1) / 3 per channel; for classes 0 and 1
       with c0 <= c1, transparent black (0) */
    r128_a64_patch19(e, arm3, r128_a64_here(e));
    if (cls < 2) {
        r128_a64_cmp_w_reg(e, 25, 27);
        b_s3z = r128_a64_bcond(e, A64_LS); /* c0 <= c1: black */
    }
    r128_a64_emit_dxt_arm_seed(e, cls, vecwm);
    r128_a64_emit_dxt_third(e, 27, 25, 0); /* endpoints swapped */
    r128_a64_emit_dxt_pack(e, 16);
    r128_a64_emit_dxt_third(e, 27, 25, 1);
    r128_a64_emit_dxt_pack(e, 8);
    r128_a64_emit_dxt_third(e, 27, 25, 2);
    r128_a64_emit_dxt_pack(e, 0);
    r128_a64_ldr_w(e, 27, 31, R128_A64_TS_DXT1);
    r128_a64_fmov_x_d(e, 30, 31);
    r128_a64_ret(e);
    if (cls < 2) {
        r128_a64_patch19(e, b_s3z, r128_a64_here(e));
        r128_a64_mov_w(e, 27, 31); /* transparent black */
        r128_a64_fmov_x_d(e, 30, 31);
        r128_a64_ret(e);
    }
    return entry;
}

/* ------------------------------------------------------------------------
 * YUV texel decode: datatypes 11 and 12 (4:2:2, two texels per 4-byte
 * group) and 14 (AYUV 4:4:4, one word per texel). It repeats r3d_texel
 * cases 11, 12 and 14 and the integer BT.601 matrix of r128_yuv_to_rgb
 * operation by operation, and like the S3TC decode it is emitted once
 * per stage as a BL subroutine. The matrix terms can be negative, so
 * their >> 8 is an ASR and the clamps to 0..255 are signed compares.
 *
 * Same contract as the S3TC subroutine: in w25 = u, w27 = v,
 * R128_A64_TS_LW, R128_A64_TS_BASE and R128_A64_TS_MASK for the level,
 * x28 = texbase; out w27 = ARGB8888. Clobbers w25, x29, x30, all of q31
 * and the frame words at 176 and 180, which no other decode of the same
 * stage uses. The datatype 14 alpha byte goes to the third scratch word
 * (the frame word at 184, or v31.s[2] under vecwm).
 * ---------------------------------------------------------------------- */
#define R128_A64_TS_YUV0 176 /* group offset, then the unclamped red */
#define R128_A64_TS_YUV1 180 /* t - 100 * d, the first part of green */

static int
r128_a64_emit_yuv_sub(r128_a64_emit_t *e, const r3d_stage_hdr_t *h, int vecwm)
{
    uint32_t dt    = h->dt;
    int      entry = r128_a64_here(e);

    /* the return address waits in v31.d[0], as in the S3TC subroutine */
    r128_a64_fmov_d_x(e, 31, 30);

    if (dt == 14) {
        /* one word at (base + (v * lw + u) * 4) & mask, bytes A Y U V
           from bit 31 down */
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_LW);
        r128_a64_madd_w(e, 29, 27, 30, 25);
        r128_a64_lsl_w(e, 29, 29, 2);
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_BASE);
        r128_a64_add_w_lsl(e, 29, 30, 29, 0);
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_MASK);
        r128_a64_and_w_reg(e, 29, 29, 30);
        r128_a64_add_x_uxtw(e, 29, 28, 29);
        r128_a64_ldr_w(e, 27, 29, 0);
        r128_a64_lsr_w(e, 30, 27, 24);
        r128_a64_emit_dxt_sw_st(e, 30, vecwm);
        r128_a64_ubfx_w(e, 25, 27, 16, 8); /* y                 */
        r128_a64_ubfx_w(e, 29, 27, 0, 8);  /* cr                */
        r128_a64_ubfx_w(e, 27, 27, 8, 8);  /* cb                */
    } else {
        /* group offset poff = (base + (v * lw + (u & ~1)) * 2) & mask,
           kept in the frame; each byte is then read at (poff + k) & mask,
           as in the C. Datatype 11 holds Y0 U Y1 V, datatype 12 U Y0 V Y1,
           the orders r3d_texel uses. */
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_LW);
        r128_a64_madd_w(e, 29, 27, 30, 31);
        r128_a64_and_w_bitmask(e, 30, 25, 0xfffffffeu);
        r128_a64_add_w_lsl(e, 29, 29, 30, 0);
        r128_a64_lsl_w(e, 29, 29, 1);
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_BASE);
        r128_a64_add_w_lsl(e, 29, 30, 29, 0);
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_MASK);
        r128_a64_and_w_reg(e, 29, 29, 30);
        r128_a64_str_w(e, 29, 31, R128_A64_TS_YUV0);
        /* y at poff + (u&1)*2 (dt 11) / poff + 1 + (u&1)*2 (dt 12) */
        r128_a64_and_w_bitmask(e, 30, 25, 1u);
        r128_a64_add_w_lsl(e, 25, 29, 30, 1);
        if (dt == 12)
            r128_a64_add_w_imm(e, 25, 25, 1);
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_MASK);
        r128_a64_and_w_reg(e, 25, 25, 30);
        r128_a64_add_x_uxtw(e, 29, 28, 25);
        r128_a64_ldrb(e, 25, 29, 0); /* y                 */
        /* cb at poff + 1 (dt 11) / poff (dt 12) */
        r128_a64_ldr_w(e, 29, 31, R128_A64_TS_YUV0);
        if (dt == 11)
            r128_a64_add_w_imm(e, 27, 29, 1);
        else
            r128_a64_mov_w(e, 27, 29);
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_MASK);
        r128_a64_and_w_reg(e, 27, 27, 30);
        r128_a64_add_x_uxtw(e, 29, 28, 27);
        r128_a64_ldrb(e, 27, 29, 0); /* cb                */
        /* cr at poff + 3 (dt 11) / poff + 2 (dt 12) */
        r128_a64_ldr_w(e, 29, 31, R128_A64_TS_YUV0);
        r128_a64_add_w_imm(e, 29, 29, (dt == 11) ? 3 : 2);
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_MASK);
        r128_a64_and_w_reg(e, 29, 29, 30);
        r128_a64_add_x_uxtw(e, 29, 28, 29);
        r128_a64_ldrb(e, 29, 29, 0); /* cr                */
    }

    /* c = y - 16, d = cb - 128, e = cr - 128. All three channels add
       298 * c and the rounding term 128, so t = 298 * c + 128 is formed
       once. */
    r128_a64_sub_w_imm(e, 25, 25, 16);
    r128_a64_sub_w_imm(e, 27, 27, 128);
    r128_a64_sub_w_imm(e, 29, 29, 128);
    r128_a64_movz_w(e, 30, 298, 0);
    r128_a64_madd_w(e, 25, 25, 30, 31);
    r128_a64_add_w_imm(e, 25, 25, 128); /* t                 */
    r128_a64_movz_w(e, 30, 409, 0);
    r128_a64_madd_w(e, 30, 29, 30, 25);
    r128_a64_asr_w(e, 30, 30, 8); /* r = (t+409e)>>8   */
    r128_a64_str_w(e, 30, 31, R128_A64_TS_YUV0);
    r128_a64_movz_w(e, 30, 100, 0);
    r128_a64_msub_w(e, 30, 27, 30, 25); /* t - 100d          */
    r128_a64_str_w(e, 30, 31, R128_A64_TS_YUV1);
    r128_a64_movz_w(e, 30, 208, 0);
    r128_a64_madd_w(e, 30, 29, 30, 31); /* 208e (e dead)     */
    r128_a64_ldr_w(e, 29, 31, R128_A64_TS_YUV1);
    r128_a64_sub_w_reg(e, 29, 29, 30);
    r128_a64_asr_w(e, 29, 29, 8); /* g                 */
    r128_a64_movz_w(e, 30, 516, 0);
    r128_a64_madd_w(e, 30, 27, 30, 25); /* d dead            */
    r128_a64_asr_w(e, 30, 30, 8);       /* b                 */

    /* clamp r, g, b to 0..255 with signed compares and pack them under
       the alpha byte (0xff for datatypes 11 and 12) */
    r128_a64_movz_w(e, 25, 255, 0);
    r128_a64_ldr_w(e, 27, 31, R128_A64_TS_YUV0);
    r128_a64_cmp_w_imm(e, 27, 0);
    r128_a64_csel_w(e, 27, 31, 27, A64_LT);
    r128_a64_cmp_w_imm(e, 27, 255);
    r128_a64_csel_w(e, 27, 25, 27, A64_GT);
    r128_a64_cmp_w_imm(e, 29, 0);
    r128_a64_csel_w(e, 29, 31, 29, A64_LT);
    r128_a64_cmp_w_imm(e, 29, 255);
    r128_a64_csel_w(e, 29, 25, 29, A64_GT);
    r128_a64_cmp_w_imm(e, 30, 0);
    r128_a64_csel_w(e, 30, 31, 30, A64_LT);
    r128_a64_cmp_w_imm(e, 30, 255);
    r128_a64_csel_w(e, 30, 25, 30, A64_GT);
    if (dt == 14) {
        r128_a64_emit_dxt_sw_ld(e, 25, vecwm);
        r128_a64_lsl_w(e, 25, 25, 24);
    } else
        r128_a64_movz_w(e, 25, 0xff00, 1); /* opaque            */
    r128_a64_orr_w_lsl(e, 25, 25, 27, 16);
    r128_a64_orr_w_lsl(e, 25, 25, 29, 8);
    r128_a64_orr_w_lsl(e, 27, 25, 30, 0);
    r128_a64_fmov_x_d(e, 30, 31);
    r128_a64_ret(e);
    return entry;
}

/* ------------------------------------------------------------------------
 * Inline gate: true when every enabled stage has a datatype that
 * r128_jit_dt_inline_family accepts (r128_jit_texinline_can, shared with
 * the x86-64 backend). Any other textured state keeps the
 * rage128_texstage_run call. The datatype is the only per-stage test
 * because everything else is emitted for every state: the CI palette
 * pointer is read from the per-triangle descriptor at run time, and the
 * combine covers every color and alpha function code, the variants
 * selected by R128_COMB_FCN_MSB, and every factor and input select.
 * CONSTANT_COLOR (cc[]) is part of the draw state, so its values are
 * emitted as immediates.
 * ---------------------------------------------------------------------- */
static int
r128_a64_texinline_can(const rage128_draw_state_t *ds)
{
    return r128_jit_texinline_can(ds);
}

/* ------------------------------------------------------------------------
 * Emission helpers
 * ---------------------------------------------------------------------- */

/* One integer texel coordinate through r3d_tex_wrap. mode is the clamp
   field, PRIM_TEXTURE_CLAMP_MODE_S or _T (SDK: Texture Mapping, p. 6-41
   / PDF 153, Table 6-6): 0 wrap, 1 mirror, 2 clamp, 3 border color.
   In: the coordinate in rc, the level dimension (a power of two) in rn.
   Out: the result in rc; a coordinate outside the level in border mode
   gives -1, and the texel fetch then substitutes the border color. rs1
   is scratch. */
static void
r128_a64_emit_wrap(r128_a64_emit_t *e, uint32_t mode, int rc, int rn, int rs1)
{
    switch (mode & 3) {
        case 0: /* wrap: c & (n - 1) */
            r128_a64_sub_w_imm(e, rs1, rn, 1);
            r128_a64_and_w_reg(e, rc, rc, rs1);
            break;
        case 1: /* mirror: m = c & (2n - 1); m < n ? m : (2n - 1) - m.
                   m has no bit outside the all-ones mask 2n - 1, so the
                   subtraction never borrows and EOR gives the same
                   result. */
            r128_a64_lsl_w(e, rs1, rn, 1);
            r128_a64_sub_w_imm(e, rs1, rs1, 1);
            r128_a64_and_w_reg(e, rc, rc, rs1);
            r128_a64_eor_w_reg(e, rs1, rc, rs1);
            r128_a64_cmp_w_reg(e, rc, rn);
            r128_a64_csel_w(e, rc, rc, rs1, A64_LT);
            break;
        case 3: /* border: (c < 0 || c >= n) ? -1 : c. One unsigned
                   compare covers both tests, since a negative c compares
                   above any n; CSINV with WZR gives ~0 = -1. */
            r128_a64_cmp_w_reg(e, rc, rn);
            r128_a64_csinv_w(e, rc, rc, 31, A64_LO);
            break;
        default: /* clamp: c < 0 ? 0 : c >= n ? n - 1 : c */
            /* keeps c only when c > 0; c == 0 gives 0 either way */
            r128_a64_cmp_w_imm(e, rc, 0);
            r128_a64_csel_w(e, rc, rc, 31, A64_GT);
            r128_a64_sub_w_imm(e, rs1, rn, 1);
            r128_a64_cmp_w_reg(e, rc, rn);
            r128_a64_csel_w(e, rc, rc, rs1, A64_LT);
            break;
    }
}

/* One texel fetch and decode to ARGB8888, r3d_texel for every datatype
   the gate accepts. In: the frame offsets of the wrapped u and v
   (uu_off, vv_off); R128_A64_TS_LW, R128_A64_TS_BASE and
   R128_A64_TS_MASK for the level; x28 = texbase; tex_sub, the stage's
   decode subroutine for S3TC and YUV. Out: w27. Clobbers w25, x29, x30.
   Addressing is linear only: r3d_texel's tile transform is never needed,
   because a draw with a texture level read through it is not compiled
   (tex_tiled in vid_ati_rage128_jit.c).

   The interpreter does not fetch when a border-mode coordinate is -1; it
   takes the border color instead. This code fetches anyway, at an
   address still masked into the texture's memory, and then replaces the
   result with the border color, so the output is the same. */
static void
r128_a64_emit_texel(r128_a64_emit_t *e, const r3d_stage_hdr_t *h,
                    int uu_off, int vv_off, int tex_sub)
{
    uint32_t dt        = h->dt;
    int      border_en = ((h->clamp_s & 3) == 3) || ((h->clamp_t & 3) == 3);

    if (dt == 0 || dt == 11 || dt == 12 || dt == 14) {
        /* S3TC and YUV: u and v in registers, decoded by the stage's
           subroutine */
        r128_a64_ldr_w(e, 25, 31, uu_off);
        r128_a64_ldr_w(e, 27, 31, vv_off);
        r128_a64_bl_to(e, tex_sub);
        goto border;
    }
    r128_a64_ldr_w(e, 25, 31, R128_A64_TS_LW);
    r128_a64_ldr_w(e, 27, 31, vv_off);
    r128_a64_ldr_w(e, 29, 31, uu_off);
    r128_a64_madd_w(e, 27, 27, 25, 29); /* off = v * lw + u, in texels */
    if (dt == 5)
        r128_a64_add_w_lsl(e, 27, 27, 27, 1); /* 3 bytes per texel */
    else if (dt == 6)
        r128_a64_lsl_w(e, 27, 27, 2);
    else if (dt == 3 || dt == 4 || dt == 15)
        r128_a64_lsl_w(e, 27, 27, 1);
    /* CI4, CI8, RGB332, Y8, RGB8: one byte per texel, off unscaled */
    r128_a64_ldr_w(e, 29, 31, R128_A64_TS_BASE);
    r128_a64_add_w_lsl(e, 27, 29, 27, 0);
    r128_a64_ldr_w(e, 29, 31, R128_A64_TS_MASK);
    r128_a64_and_w_reg(e, 27, 27, 29);
    r128_a64_add_x_uxtw(e, 29, 28, 27);
    switch (dt) {
        case 6:
            r128_a64_ldr_w(e, 27, 29, 0);
            break;
        case 5: /* 3 packed bytes; only the first address is masked, as
                   in the C */
            r128_a64_ldrh(e, 27, 29, 0);
            r128_a64_ldrb(e, 30, 29, 2);
            break;
        case 1:
        case 2:
        case 7:
        case 8:
        case 9:
            r128_a64_ldrb(e, 27, 29, 0);
            break;
        default:
            r128_a64_ldrh(e, 27, 29, 0);
            break;
    }
    switch (dt) {
        case 6: /* ARGB8888 as stored */
            break;
        case 1: /* CI4: one texel per byte, the low nibble indexes the
                   palette */
            r128_a64_and_w_bitmask(e, 27, 27, 0xfu);
            /* fall through */
        case 2: /* CI8: pal[idx], palette pointer at R128_A64_TS_PAL. The
                   chroma keys compare the index, not the color, so the
                   index is also returned in w25, which is free once the
                   address is formed. A frame word would not do: 184 is
                   the SoA loop's R128_A64_SP_SOA_WM slot. */
            r128_a64_orr_w_lsl(e, 25, 31, 27, 0);
            r128_a64_lsl_w(e, 27, 27, 2);
            r128_a64_ldr_x(e, 29, 31, R128_A64_TS_PAL);
            r128_a64_add_x_uxtw(e, 29, 29, 27);
            r128_a64_ldr_w(e, 27, 29, 0);
            break;
        case 5: /* RGB888, bytes B G R in memory order, opaque */
            r128_a64_orr_w_lsl(e, 27, 27, 30, 16);
            r128_a64_orr_w_bitmask(e, 27, 27, 0xff000000u);
            break;
        case 7: /* RGB332, opaque. Channels are shifted into place with
                   the low bits zero; no bit replication, as in the C
                   (the same holds for 1555, 565 and 4444 below). */
            r128_a64_ubfx_w(e, 29, 27, 5, 3);
            r128_a64_lsl_w(e, 29, 29, 21);
            r128_a64_ubfx_w(e, 30, 27, 2, 3);
            r128_a64_orr_w_lsl(e, 29, 29, 30, 13);
            r128_a64_and_w_bitmask(e, 30, 27, 0x3u);
            r128_a64_orr_w_lsl(e, 29, 29, 30, 6);
            r128_a64_orr_w_bitmask(e, 27, 29, 0xff000000u);
            break;
        case 8: /* Y8: the byte in red, green and blue, opaque */
            r128_a64_orr_w_lsl(e, 29, 27, 27, 8);
            r128_a64_orr_w_lsl(e, 29, 29, 27, 16);
            r128_a64_orr_w_bitmask(e, 27, 29, 0xff000000u);
            break;
        case 9: /* RGB8: the byte in all four channels, alpha included */
            r128_a64_orr_w_lsl(e, 29, 27, 27, 8);
            r128_a64_orr_w_lsl(e, 27, 29, 29, 16);
            break;
        case 3: /* ARGB1555: alpha 0xff when bit 15 is set, else 0 */
            r128_a64_ubfx_w(e, 29, 27, 10, 5);
            r128_a64_lsl_w(e, 29, 29, 19);
            r128_a64_ubfx_w(e, 30, 27, 5, 5);
            r128_a64_orr_w_lsl(e, 29, 29, 30, 11);
            r128_a64_ubfx_w(e, 30, 27, 0, 5);
            r128_a64_orr_w_lsl(e, 29, 29, 30, 3);
            r128_a64_movz_w(e, 30, 0xff00, 1);
            r128_a64_tst_w_imm(e, 27, 0x8000);
            r128_a64_csel_w(e, 30, 30, 31, A64_NE);
            r128_a64_orr_w_lsl(e, 27, 29, 30, 0);
            break;
        case 4: /* RGB565, opaque */
            r128_a64_ubfx_w(e, 29, 27, 11, 5);
            r128_a64_lsl_w(e, 29, 29, 19);
            r128_a64_ubfx_w(e, 30, 27, 5, 6);
            r128_a64_orr_w_lsl(e, 29, 29, 30, 10);
            r128_a64_ubfx_w(e, 30, 27, 0, 5);
            r128_a64_orr_w_lsl(e, 29, 29, 30, 3);
            r128_a64_orr_w_bitmask(e, 27, 29, 0xff000000u);
            break;
        default: /* 15: ARGB4444 */
            r128_a64_ubfx_w(e, 29, 27, 12, 4);
            r128_a64_lsl_w(e, 29, 29, 28);
            r128_a64_ubfx_w(e, 30, 27, 8, 4);
            r128_a64_orr_w_lsl(e, 29, 29, 30, 20);
            r128_a64_ubfx_w(e, 30, 27, 4, 4);
            r128_a64_orr_w_lsl(e, 29, 29, 30, 12);
            r128_a64_ubfx_w(e, 30, 27, 0, 4);
            r128_a64_orr_w_lsl(e, 27, 29, 30, 4);
            break;
    }
border:
    /* TEX_MAP_AEN clear (h->aone) makes the fetched texel opaque before
       filtering, the S3TC and YUV subroutine results included. The border
       substitution comes after it, so the border color keeps its own
       alpha (Registers for CCE 3D Packets, SCALE_3D_CNTL). */
    if (h->aone)
        r128_a64_orr_w_bitmask(e, 27, 27, 0xff000000u);
    if (border_en) {
        /* u | v is negative exactly when one of them is -1, since the
           other modes return non-negative values; the texel is then
           the stage's border color (h->border), unconverted */
        r128_a64_ldr_w(e, 29, 31, uu_off);
        r128_a64_ldr_w(e, 30, 31, vv_off);
        r128_a64_orr_w_lsl(e, 29, 29, 30, 0);
        r128_a64_cmp_w_imm(e, 29, 0);
        r128_a64_movz_w(e, 30, h->border & 0xffff, 0);
        if (h->border >> 16)
            r128_a64_movk_w(e, 30, h->border >> 16, 1);
        r128_a64_csel_w(e, 27, 30, 27, A64_LT);
        if (h->dt == 1 || h->dt == 2)
            /* the chroma-key value is the border color too, as the
               interpreter's nearest texel is */
            r128_a64_csel_w(e, 25, 30, 25, A64_LT);
    }
}

/* r3d_lerp_packed(x, y, w) on lanes 0 and 1 at once: x from vx, y from
   vy, w the weight (0..256) in the frame word at w_off; the result
   replaces vx. The C works in uint32 with no saturation, and the vector
   MUL, MLA, ADD and USHR on 32-bit lanes compute the same values
   (lanes 2 and 3 compute values nobody reads). M is the 0x00ff00ff mask
   in v8, K the 0x00800080 rounding term in v9. Clobbers w29, v26, v27,
   v29, v30, v31. */
static void
r128_a64_emit_lerp_pair(r128_a64_emit_t *e, int vx, int vy, int w_off)
{
    r128_a64_ldr_w(e, 29, 31, w_off);
    r128_a64_dup_4s_w(e, 29, 29);    /* v29 = w splat            */
    r128_a64_movi_4s_lsl8(e, 30, 1); /* v30 = 256 splat          */
    r128_a64_sub_4s(e, 30, 30, 29);  /* v30 = iw = 256 - w       */
    r128_a64_and_16b(e, 26, vx, 8);  /* rb: (x & M)              */
    r128_a64_mul_4s(e, 26, 26, 30);  /*   * iw                   */
    r128_a64_and_16b(e, 27, vy, 8);
    r128_a64_mla_4s(e, 26, 27, 29); /*   + (y & M) * w          */
    r128_a64_add_4s(e, 26, 26, 9);  /*   + K                    */
    r128_a64_ushr_4s(e, 26, 26, 8); /*   >> 8                   */
    r128_a64_ushr_4s(e, 27, vx, 8); /* ag: (x >> 8) & M         */
    r128_a64_and_16b(e, 27, 27, 8);
    r128_a64_mul_4s(e, 27, 27, 30);
    r128_a64_ushr_4s(e, 31, vy, 8);
    r128_a64_and_16b(e, 31, 31, 8);
    r128_a64_mla_4s(e, 27, 31, 29);
    r128_a64_add_4s(e, 27, 27, 9);
    r128_a64_ushr_4s(e, 27, 27, 8);
    r128_a64_and_16b(e, 26, 26, 8); /* (rb & M)                 */
    r128_a64_and_16b(e, 27, 27, 8); /* (ag & M) << 8            */
    r128_a64_shl_4s(e, 27, 27, 8);
    r128_a64_orr_16b(e, vx, 26, 27);
}

/* Store the chroma-key compare value of the texel just fetched to
   R128_A64_TS_TNEAR: the palette index (w25 from emit_texel) for CI4
   and CI8, the decoded texel (w27) for every other datatype, as in
   r3d_tex_level. */
static void
r128_a64_emit_store_tnear(r128_a64_emit_t *e, const r3d_stage_hdr_t *h)
{
    r128_a64_str_w(e, (h->dt == 1 || h->dt == 2) ? 25 : 27, 31, R128_A64_TS_TNEAR);
}

/* Sample one mip level, r3d_tex_level. In: the slot index in w27, s in
   s26 and t in s27 (both consumed), x16 = texctx. Out: w27 = the
   filtered texel. With want_near the nearest texel's chroma-key value
   also goes to R128_A64_TS_TNEAR, the border color on a border-mode
   miss as in the interpreter. Clobbers every scratch register of the
   stage. */
static void
r128_a64_emit_tex_level(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                        int st, int linear, int want_near, int tex_sub)
{
    const r3d_stage_hdr_t *h      = &ds->sh[st];
    int                    sd_off = (int) (st ? offsetof(r3d_texctx_t, sd1)
                                              : offsetof(r3d_texctx_t, sd0));
    int                    sl_off = sd_off + (int) offsetof(r3d_stage_desc_t, slot);

    /* x28 = &texctx->sdN.slot[slot]; copy the level's lw, lh, base and
       mask to the frame and load texbase into x28 */
    r128_a64_add_x_imm(e, 28, 16, sl_off);
    r128_a64_movz_w(e, 29, (int) sizeof(struct r3d_slot_desc_t), 0);
    r128_a64_madd_w(e, 29, 27, 29, 31);
    r128_a64_add_x_uxtw(e, 28, 28, 29);
    r128_a64_ldp_w(e, 25, 30, 28, 0); /* lw, lh            */
    r128_a64_str_w(e, 25, 31, R128_A64_TS_LW);
    r128_a64_str_w(e, 30, 31, R128_A64_TS_LH);
    r128_a64_ldr_w(e, 29, 28, 16); /* base              */
    r128_a64_str_w(e, 29, 31, R128_A64_TS_BASE);
    r128_a64_ldr_w(e, 29, 28, 20); /* mask              */
    r128_a64_str_w(e, 29, 31, R128_A64_TS_MASK);
    r128_a64_ldr_x(e, 28, 28, 8); /* x28 = texbase     */
    if (h->dt == 1 || h->dt == 2) {
        /* the stage's palette pointer for the CI decodes */
        r128_a64_ldr_x(e, 29, 16, sd_off + (int) offsetof(r3d_stage_desc_t, pal));
        r128_a64_str_x(e, 29, 31, R128_A64_TS_PAL);
    }

    /* fx = r3d_texcoord_fx(s * (float) lw) =
       nearbyintf(s * lw * 4096.0f) * (1.0f / 4096.0f), the same for
       fy from t and lh */
    r128_a64_ucvtf_s_w(e, 29, 25);
    r128_a64_fmul_s(e, 26, 26, 29);
    r128_a64_dup_4s_lane(e, 30, 11, 0); /* 4096.0f           */
    r128_a64_fmul_s(e, 26, 26, 30);
    r128_a64_frintn_s(e, 26, 26);
    r128_a64_dup_4s_lane(e, 30, 11, 1); /* 2^-12             */
    r128_a64_fmul_s(e, 26, 26, 30);
    r128_a64_ldr_w(e, 29, 31, R128_A64_TS_LH);
    r128_a64_ucvtf_s_w(e, 29, 29);
    r128_a64_fmul_s(e, 27, 27, 29);
    r128_a64_dup_4s_lane(e, 30, 11, 0);
    r128_a64_fmul_s(e, 27, 27, 30);
    r128_a64_frintn_s(e, 27, 27);
    r128_a64_dup_4s_lane(e, 30, 11, 1);
    r128_a64_fmul_s(e, 27, 27, 30);

    if (!linear) {
        /* nearest: u = wrap((int) floorf(fx)), v likewise */
        r128_a64_fcvtms_w_s(e, 29, 26);
        r128_a64_emit_wrap(e, h->clamp_s, 29, 25, 30);
        r128_a64_str_w(e, 29, 31, R128_A64_TS_UU0);
        r128_a64_fcvtms_w_s(e, 29, 27);
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_LH);
        r128_a64_emit_wrap(e, h->clamp_t, 29, 30, 27);
        r128_a64_str_w(e, 29, 31, R128_A64_TS_VV0);
        r128_a64_emit_texel(e, h, R128_A64_TS_UU0, R128_A64_TS_VV0, tex_sub);
        if (want_near)
            r128_a64_emit_store_tnear(e, h);
        return;
    }

    if (want_near) {
        /* The nearest-texel part of r3d_tex_level, for the key compare:
           fetched at fx, fy before the bilinear -0.5. fx and fy stay in
           s26 and s27, since a fetch touches only general registers and
           v31. The fetch and the v wrap overwrite w25, so lw is loaded
           again for the bilinear u wraps. */
        r128_a64_fcvtms_w_s(e, 29, 26);
        r128_a64_emit_wrap(e, h->clamp_s, 29, 25, 30);
        r128_a64_str_w(e, 29, 31, R128_A64_TS_UU0);
        r128_a64_fcvtms_w_s(e, 29, 27);
        r128_a64_ldr_w(e, 30, 31, R128_A64_TS_LH);
        r128_a64_emit_wrap(e, h->clamp_t, 29, 30, 25);
        r128_a64_str_w(e, 29, 31, R128_A64_TS_VV0);
        r128_a64_emit_texel(e, h, R128_A64_TS_UU0, R128_A64_TS_VV0, tex_sub);
        r128_a64_emit_store_tnear(e, h);
        r128_a64_ldr_w(e, 25, 31, R128_A64_TS_LW); /* lw back for the wraps */
    }

    /* bilinear: fu = fx - 0.5f; u0 = (int) floorf(fu);
       wu = (uint32_t) ((fu - (float) u0) * 256.0f + 0.5f); u1 = u0 + 1,
       each wrapped; the same for v */
    r128_a64_dup_4s_lane(e, 30, 13, 1); /* 0.5f              */
    r128_a64_fsub_s(e, 26, 26, 30);
    r128_a64_fsub_s(e, 27, 27, 30);
    r128_a64_fcvtms_w_s(e, 29, 26); /* u0                */
    r128_a64_scvtf_s_w(e, 30, 29);
    r128_a64_fsub_s(e, 30, 26, 30);
    r128_a64_dup_4s_lane(e, 26, 13, 0); /* 256.0f            */
    r128_a64_fmul_s(e, 30, 30, 26);
    r128_a64_dup_4s_lane(e, 26, 13, 1);
    r128_a64_fadd_s(e, 30, 30, 26);
    r128_a64_fcvtzu_w_s(e, 30, 30);
    r128_a64_str_w(e, 30, 31, R128_A64_TS_WU);
    r128_a64_mov_w(e, 30, 29); /* raw u0            */
    r128_a64_emit_wrap(e, h->clamp_s, 29, 25, 27);
    r128_a64_str_w(e, 29, 31, R128_A64_TS_UU0);
    /* Clamp caps the raw coordinate before adding the successor; the
       base wrap leaves n - 1 in the scratch register. */
    if ((h->clamp_s & 3) == 2) {
        r128_a64_cmp_w_reg(e, 30, 27);
        r128_a64_csel_w(e, 30, 30, 27, A64_LT);
    }
    r128_a64_add_w_imm(e, 30, 30, 1);
    r128_a64_emit_wrap(e, h->clamp_s, 30, 25, 29);
    r128_a64_str_w(e, 30, 31, R128_A64_TS_UU1);
    /* v side */
    r128_a64_fcvtms_w_s(e, 29, 27); /* v0                */
    r128_a64_scvtf_s_w(e, 30, 29);
    r128_a64_fsub_s(e, 30, 27, 30);
    r128_a64_dup_4s_lane(e, 26, 13, 0);
    r128_a64_fmul_s(e, 30, 30, 26);
    r128_a64_dup_4s_lane(e, 26, 13, 1);
    r128_a64_fadd_s(e, 30, 30, 26);
    r128_a64_fcvtzu_w_s(e, 30, 30);
    r128_a64_str_w(e, 30, 31, R128_A64_TS_WV);
    r128_a64_mov_w(e, 30, 29);
    r128_a64_ldr_w(e, 25, 31, R128_A64_TS_LH); /* lh for the v wraps */
    r128_a64_emit_wrap(e, h->clamp_t, 29, 25, 27);
    r128_a64_str_w(e, 29, 31, R128_A64_TS_VV0);
    /* Clamp caps the raw coordinate before adding the successor; the
       base wrap leaves n - 1 in the scratch register. */
    if ((h->clamp_t & 3) == 2) {
        r128_a64_cmp_w_reg(e, 30, 27);
        r128_a64_csel_w(e, 30, 30, 27, A64_LT);
    }
    r128_a64_add_w_imm(e, 30, 30, 1);
    r128_a64_emit_wrap(e, h->clamp_t, 30, 25, 29);
    r128_a64_str_w(e, 30, 31, R128_A64_TS_VV1);

    /* the 2x2 footprint in r3d_tex_level's order c0 (u0,v0), c1 (u1,v0),
       c2 (u0,v1), c3 (u1,v1), placed as v3 = {c0, c2}, v7 = {c1, c3} */
    r128_a64_emit_texel(e, h, R128_A64_TS_UU0, R128_A64_TS_VV0, tex_sub);
    r128_a64_ins_s_w(e, 3, 0, 27);
    r128_a64_emit_texel(e, h, R128_A64_TS_UU1, R128_A64_TS_VV0, tex_sub);
    r128_a64_ins_s_w(e, 7, 0, 27);
    r128_a64_emit_texel(e, h, R128_A64_TS_UU0, R128_A64_TS_VV1, tex_sub);
    r128_a64_ins_s_w(e, 3, 1, 27);
    r128_a64_emit_texel(e, h, R128_A64_TS_UU1, R128_A64_TS_VV1, tex_sub);
    r128_a64_ins_s_w(e, 7, 1, 27);

    /* lerp(c0, c1, wu) and lerp(c2, c3, wu) in one pass, then the two
       results by wv */
    r128_a64_emit_lerp_pair(e, 3, 7, R128_A64_TS_WU);
    r128_a64_dup_4s_lane(e, 7, 3, 1);
    r128_a64_emit_lerp_pair(e, 3, 7, R128_A64_TS_WV);
    r128_a64_umov_w_s(e, 27, 3, 0);
}

/* s and t from the frame back into s26 and s27, where emit_tex_level
   takes them */
static void
r128_a64_emit_st_reload(r128_a64_emit_t *e)
{
    r128_a64_ldr_s(e, 26, 31, R128_A64_TS_S);
    r128_a64_ldr_s(e, 27, 31, R128_A64_TS_T);
}

/* Per-pixel LOD of one stage, the need_lod block of
   rage128_texstage_run. With perspective each screen derivative of s
   and t is (dSdx * rhw - sp * dWdx) / rhw^2 (zero when rhw is 0), then
   scaled by the stage's texture width or height (v10);
   lod = 0.5 * log2(max(ax2, ay2)) + lod_bias, where ax2 and ay2 are the
   squared x and y derivative lengths, or -1000 when that maximum is not
   above 0. log2 is r3d_log2f_fast: the exponent plus a cubic in the
   mantissa (coefficients in v12). Each C ternary becomes an FCSEL or a
   branch with the same outcome for NaN. In: sp in s3, tp in s7, the
   stage's rhw in s15. Out: lod in s3. Stage 1 follows its own
   perspective enable, and under SEC_SRC_SEL_W the gradients of its own
   W (dW2dx, dW2dy), as the interpreter's need_lod2 block does. */
static void
r128_a64_emit_lod(r128_a64_emit_t *e, const rage128_draw_state_t *ds, int st)
{
    int sel    = st && ds->sec_sel;
    int selw   = st && ds->sel_w;
    int persp  = r128_jit_stage_persp(ds, st);
    int gs_x   = (int) (sel ? offsetof(r3d_texctx_t, dS2dx) : offsetof(r3d_texctx_t, dSdx));
    int gs_y   = (int) (sel ? offsetof(r3d_texctx_t, dS2dy) : offsetof(r3d_texctx_t, dSdy));
    int gt_x   = (int) (sel ? offsetof(r3d_texctx_t, dT2dx) : offsetof(r3d_texctx_t, dTdx));
    int gt_y   = (int) (sel ? offsetof(r3d_texctx_t, dT2dy) : offsetof(r3d_texctx_t, dTdy));
    int gw_x   = (int) (selw ? offsetof(r3d_texctx_t, dW2dx) : offsetof(r3d_texctx_t, dWdx));
    int gw_y   = (int) (selw ? offsetof(r3d_texctx_t, dW2dy) : offsetof(r3d_texctx_t, dWdy));
    int w_lane = st ? 2 : 0; /* lanes of v10: texw, texh of the stage */
    int h_lane = st ? 3 : 1;
    int b_skip;

    if (persp) {
        /* iw2 = (wp != 0) ? 1 / (wp * wp) : 0, wp = rhw; NE also holds
           for an unordered compare, as != does in C */
        r128_a64_fmul_s(e, 29, 15, 15);
        r128_a64_dup_4s_lane(e, 30, 13, 2); /* 1.0f */
        r128_a64_fdiv_s(e, 29, 30, 29);
        r128_a64_fmov_s_w(e, 30, 31); /* 0.0f */
        r128_a64_fcmp_s0(e, 15);
        r128_a64_fcsel_s(e, 29, 29, 30, A64_NE);
    }
    /* dsx -> s30, dtx -> s31 (scaled into texel space), ax2 -> s30 */
    if (persp) {
        r128_a64_ldr_s(e, 30, 16, gs_x);
        r128_a64_fmul_s(e, 30, 30, 15);
        r128_a64_ldr_s(e, 31, 16, gw_x);
        r128_a64_fmul_s(e, 31, 3, 31);
        r128_a64_fsub_s(e, 30, 30, 31);
        r128_a64_fmul_s(e, 30, 30, 29);
        r128_a64_ldr_s(e, 31, 16, gt_x);
        r128_a64_fmul_s(e, 31, 31, 15);
        r128_a64_ldr_s(e, 26, 16, gw_x);
        r128_a64_fmul_s(e, 26, 7, 26);
        r128_a64_fsub_s(e, 31, 31, 26);
        r128_a64_fmul_s(e, 31, 31, 29);
    } else {
        r128_a64_ldr_s(e, 30, 16, gs_x);
        r128_a64_ldr_s(e, 31, 16, gt_x);
    }
    r128_a64_dup_4s_lane(e, 26, 10, w_lane);
    r128_a64_fmul_s(e, 30, 30, 26);
    r128_a64_dup_4s_lane(e, 26, 10, h_lane);
    r128_a64_fmul_s(e, 31, 31, 26);
    r128_a64_fmul_s(e, 30, 30, 30);
    r128_a64_fmul_s(e, 31, 31, 31);
    r128_a64_fadd_s(e, 30, 30, 31);
    /* dsy -> s31, dty -> s26, ay2 -> s31 */
    if (persp) {
        r128_a64_ldr_s(e, 31, 16, gs_y);
        r128_a64_fmul_s(e, 31, 31, 15);
        r128_a64_ldr_s(e, 26, 16, gw_y);
        r128_a64_fmul_s(e, 26, 3, 26);
        r128_a64_fsub_s(e, 31, 31, 26);
        r128_a64_fmul_s(e, 31, 31, 29);
        r128_a64_ldr_s(e, 26, 16, gt_y);
        r128_a64_fmul_s(e, 26, 26, 15);
        r128_a64_ldr_s(e, 27, 16, gw_y);
        r128_a64_fmul_s(e, 27, 7, 27);
        r128_a64_fsub_s(e, 26, 26, 27);
        r128_a64_fmul_s(e, 26, 26, 29);
    } else {
        r128_a64_ldr_s(e, 31, 16, gs_y);
        r128_a64_ldr_s(e, 26, 16, gt_y);
    }
    r128_a64_dup_4s_lane(e, 27, 10, w_lane);
    r128_a64_fmul_s(e, 31, 31, 27);
    r128_a64_dup_4s_lane(e, 27, 10, h_lane);
    r128_a64_fmul_s(e, 26, 26, 27);
    r128_a64_fmul_s(e, 31, 31, 31);
    r128_a64_fmul_s(e, 26, 26, 26);
    r128_a64_fadd_s(e, 31, 31, 26);
    /* rho2 = ax2 > ay2 ? ax2 : ay2 */
    r128_a64_fcmp_s(e, 30, 31);
    r128_a64_fcsel_s(e, 30, 30, 31, A64_GT);
    /* lod = rho2 > 0 ? 0.5f * r3d_log2f_fast(rho2) + lod_bias : -1000 */
    r128_a64_dup_4s_lane(e, 3, 11, 3); /* -1000.0f                 */
    r128_a64_fcmp_s0(e, 30);
    b_skip = r128_a64_bcond(e, A64_LE); /* LE holds for NaN too     */
    r128_a64_fmov_w_s(e, 29, 30);
    r128_a64_ubfx_w(e, 30, 29, 23, 8);
    r128_a64_scvtf_s_w(e, 31, 30);
    r128_a64_dup_4s_lane(e, 26, 13, 3); /* 127.0f                   */
    r128_a64_fsub_s(e, 31, 31, 26);     /* e                        */
    r128_a64_and_w_bitmask(e, 29, 29, 0x007fffffu);
    r128_a64_orr_w_bitmask(e, 29, 29, 0x3f800000u);
    r128_a64_fmov_s_w(e, 30, 29); /* m                        */
    r128_a64_dup_4s_lane(e, 26, 12, 3);
    r128_a64_fmul_s(e, 26, 30, 26); /* m*c3                     */
    r128_a64_dup_4s_lane(e, 27, 12, 2);
    r128_a64_fadd_s(e, 26, 27, 26);
    r128_a64_fmul_s(e, 26, 30, 26);
    r128_a64_dup_4s_lane(e, 27, 12, 1);
    r128_a64_fadd_s(e, 26, 27, 26);
    r128_a64_fmul_s(e, 26, 30, 26);
    r128_a64_dup_4s_lane(e, 27, 12, 0);
    r128_a64_fadd_s(e, 26, 27, 26);
    r128_a64_fadd_s(e, 31, 31, 26);     /* log2                     */
    r128_a64_dup_4s_lane(e, 26, 13, 1); /* 0.5f                     */
    r128_a64_fmul_s(e, 31, 26, 31);
    r128_a64_dup_4s_lane(e, 26, 11, 2); /* lod_bias                 */
    r128_a64_fadd_s(e, 3, 31, 26);
    r128_a64_patch19(e, b_skip, r128_a64_here(e));
}

/* One texture stage: coordinates and LOD, the r3d_tex_sample dispatch,
   the stage-0 chroma keys and the combine, with the filter, mipmap and
   combine choices of the draw state fixed at compile time. In: weights
   in s16-s18 (left intact), ir in s14, rhw in s15, x16 = texctx, the
   running color in v25. Out: the combined color in v25. A chroma-key
   reject adds a branch to the caller's skip list, which the parent
   points at pix_skip. Each phase below is a static emitter; they share
   the stage state through r128_a64_tex_t, and r128_a64_emit_texstage
   runs them in order. */
typedef struct {
    r128_a64_emit_t            *e;
    const rage128_draw_state_t *ds;
    const r3d_stage_hdr_t      *h;
    const r3d_comb_desc_t      *cd;
    int                        *skips, *nskip;
    int                         st, sa, ta, has_lod, lin_min, lin_mag;
    int                         want_near, tex_sub;
} r128_a64_tex_t;

/* Fill in the stage state, and for an S3TC or YUV stage make sure a
   decode subroutine exists. sa and ta are the offsets of the s and t
   planes: the second coordinate set for stage 1 when
   SEC_TEX_CNTL_C:SEC_SRC_SEL_ST selects it (SDK: Texture Mapping,
   p. 6-47 / PDF 159), the first set otherwise, as in
   rage128_texstage_run. */
static void
r128_a64_tex_setup(r128_a64_tex_t *t, r128_a64_emit_t *e,
                   const rage128_draw_state_t *ds, int st,
                   int *skips, int *nskip)
{
    const r3d_stage_hdr_t *h       = &ds->sh[st];
    int                    sel     = st && ds->sec_sel;
    int                    has_lod = st ? ds->need_lod2 : ds->need_lod;

    t->e       = e;
    t->ds      = ds;
    t->h       = h;
    t->cd      = &ds->comb[st];
    t->skips   = skips;
    t->nskip   = nskip;
    t->st      = st;
    t->sa      = (int) (sel ? offsetof(r3d_texctx_t, s2a) : offsetof(r3d_texctx_t, sta));
    t->ta      = (int) (sel ? offsetof(r3d_texctx_t, t2a) : offsetof(r3d_texctx_t, tta));
    t->has_lod = has_lod;
    /* The minification texel filter as r3d_tex_sample picks it: codes
       3 and 5 on its mipmap path, bit 0 of the code off it (see
       r128_jit_minb_lin_min in the rules header). */
    t->lin_min   = r128_jit_minb_lin_min(h->minb, h->mipdis, has_lod);
    t->lin_mag   = (h->mag == 1);
    t->want_near = (st == 0) && ds->need_ck;
    t->tex_sub   = -1;

    if (h->dt == 0 || h->dt == 11 || h->dt == 12 || h->dt == 14) {
        if (e->tex_sub[st] >= 0)
            /* SoA block: r128_a64_gen_soa emitted the subroutine ahead
               of both loops, and the scalar loop calls that copy */
            t->tex_sub = e->tex_sub[st];
        else {
            /* any other block: emit the subroutine here with a branch
               around it. No SoA loop shares the frame, so the word at
               184 is free (vecwm 0). */
            int b_over = r128_a64_b(e);

            t->tex_sub = r128_a64_here(e);
            if (h->dt == 0)
                r128_a64_emit_dxt_sub(e, h, 0);
            else
                r128_a64_emit_yuv_sub(e, h, 0);
            r128_a64_patch26(e, b_over, r128_a64_here(e));
        }
    }
}

/* sp = w0 * a + w1 * b + w2 * c into s3 and tp the same into s7, added
   left to right as in the C (a, b, c are consecutive floats in
   r3d_texctx_t); then s = sp * ir and t = tp * ir to the frame, and the
   per-pixel LOD into s3 when the stage has one */
static void
r128_a64_tex_coords(const r128_a64_tex_t *t)
{
    r128_a64_emit_t *e  = t->e;
    int              sa = t->sa, ta = t->ta;

    r128_a64_ldr_s(e, 3, 16, sa);
    r128_a64_fmul_s(e, 3, 16, 3);
    r128_a64_ldr_s(e, 26, 16, sa + 4);
    r128_a64_fmul_s(e, 26, 17, 26);
    r128_a64_fadd_s(e, 3, 3, 26);
    r128_a64_ldr_s(e, 26, 16, sa + 8);
    r128_a64_fmul_s(e, 26, 18, 26);
    r128_a64_fadd_s(e, 3, 3, 26);
    r128_a64_ldr_s(e, 7, 16, ta);
    r128_a64_fmul_s(e, 7, 16, 7);
    r128_a64_ldr_s(e, 26, 16, ta + 4);
    r128_a64_fmul_s(e, 26, 17, 26);
    r128_a64_fadd_s(e, 7, 7, 26);
    r128_a64_ldr_s(e, 26, 16, ta + 8);
    r128_a64_fmul_s(e, 26, 18, 26);
    r128_a64_fadd_s(e, 7, 7, 26);
    /* s = sp * ir and t = tp * ir go to the frame: the LOD code reuses
       the scratch registers, and every sampled level reloads them */
    r128_a64_fmul_s(e, 26, 3, 14);
    r128_a64_str_s(e, 26, 31, R128_A64_TS_S);
    r128_a64_fmul_s(e, 26, 7, 14);
    r128_a64_str_s(e, 26, 31, R128_A64_TS_T);

    if (t->has_lod)
        r128_a64_emit_lod(e, t->ds, t->st); /* lod -> s3 */
}

/* r3d_tex_sample with minb, mag, mipdis and top fixed at compile time.
   Only the LOD comparisons remain as run-time branches. minb is
   PRIM_TEX_CNTL_C:PRIM_MIN_BLEND_FCN (SDK: Texture Mapping, p. 6-40 /
   PDF 152, Table 6-4) and mag bit 0 of PRIM_MAG_BLEND_FCN; the rules
   header lists the codes. The base level sits in slot top. */
static void
r128_a64_tex_sample(const r128_a64_tex_t *t)
{
    r128_a64_emit_t            *e  = t->e;
    const rage128_draw_state_t *ds = t->ds;
    const r3d_stage_hdr_t      *h  = t->h;
    int                         st = t->st, has_lod = t->has_lod;
    int                         lin_min = t->lin_min, lin_mag = t->lin_mag;
    int                         want_near = t->want_near, tex_sub = t->tex_sub;

    if (!has_lod || h->mipdis || h->minb < 2) {
        if (!has_lod) {
            r128_a64_emit_st_reload(e);
            r128_a64_movz_w(e, 27, (uint32_t) h->top, 0);
            r128_a64_emit_tex_level(e, ds, st, !(h->minb == 0 && h->mag == 0), want_near, tex_sub);
        } else if (lin_min == lin_mag) {
            r128_a64_emit_st_reload(e);
            r128_a64_movz_w(e, 27, (uint32_t) h->top, 0);
            r128_a64_emit_tex_level(e, ds, st, lin_mag, want_near, tex_sub);
        } else {
            /* linear = lod > 0 ? (minb & 1) : (mag == 1); GT is false
               for NaN, as > is in C */
            int b_min, b_end;

            r128_a64_fcmp_s0(e, 3);
            b_min = r128_a64_bcond(e, A64_GT);
            r128_a64_emit_st_reload(e);
            r128_a64_movz_w(e, 27, (uint32_t) h->top, 0);
            r128_a64_emit_tex_level(e, ds, st, lin_mag, want_near, tex_sub);
            b_end = r128_a64_b(e);
            r128_a64_patch19(e, b_min, r128_a64_here(e));
            r128_a64_emit_st_reload(e);
            r128_a64_movz_w(e, 27, (uint32_t) h->top, 0);
            r128_a64_emit_tex_level(e, ds, st, lin_min, want_near, tex_sub);
            r128_a64_patch26(e, b_end, r128_a64_here(e));
        }
    } else {
        int mip_linear = (h->minb == 4 || h->minb == 5);
        int b_mag, b_end;

        /* lod <= 0: the magnify path. LS is false for NaN, as <= is in
           C, so a NaN lod minifies in both. */
        r128_a64_fcmp_s0(e, 3);
        b_mag = r128_a64_bcond(e, A64_LS);
        /* lvl = lod > top ? top : lod */
        r128_a64_mov_w_fbits(e, 29, (float) h->top);
        r128_a64_fmov_s_w(e, 29, 29);
        r128_a64_fcmp_s(e, 3, 29);
        r128_a64_fcsel_s(e, 3, 29, 3, A64_GT);
        if (mip_linear) {
            /* codes 4 and 5: two levels, slotA = top - floor(lvl) and
               slotB = max(slotA - 1, 0), blended by the fraction f */
            r128_a64_fcvtms_w_s(e, 27, 3); /* l0           */
            r128_a64_scvtf_s_w(e, 29, 27);
            r128_a64_fsub_s(e, 29, 3, 29); /* f            */
            r128_a64_str_s(e, 29, 31, R128_A64_TS_F);
            r128_a64_movz_w(e, 29, (uint32_t) h->top, 0);
            r128_a64_sub_w_reg(e, 27, 29, 27); /* slotA        */
            r128_a64_sub_w_imm(e, 29, 27, 1);  /* slotB        */
            r128_a64_cmp_w_imm(e, 29, 0);
            r128_a64_csel_w(e, 29, 29, 31, A64_GE);
            r128_a64_str_w(e, 29, 31, R128_A64_TS_SLOTB);
            r128_a64_emit_st_reload(e);
            r128_a64_emit_tex_level(e, ds, st, lin_min, want_near, tex_sub);
            r128_a64_str_w(e, 27, 31, R128_A64_TS_CA);
            r128_a64_ldr_w(e, 27, 31, R128_A64_TS_SLOTB);
            r128_a64_emit_st_reload(e);
            /* slotB: the interpreter asks for no nearest texel here */
            r128_a64_emit_tex_level(e, ds, st, lin_min, 0, tex_sub);
            r128_a64_ldr_w(e, 29, 31, R128_A64_TS_CA);
            r128_a64_ins_s_w(e, 3, 0, 29);
            r128_a64_ins_s_w(e, 7, 0, 27);
            r128_a64_ldr_s(e, 29, 31, R128_A64_TS_F); /* w = (u32) (f * 256 + 0.5) */
            r128_a64_dup_4s_lane(e, 30, 13, 0);
            r128_a64_fmul_s(e, 29, 29, 30);
            r128_a64_dup_4s_lane(e, 30, 13, 1);
            r128_a64_fadd_s(e, 29, 29, 30);
            r128_a64_fcvtzu_w_s(e, 29, 29);
            r128_a64_str_w(e, 29, 31, R128_A64_TS_WU);
            r128_a64_emit_lerp_pair(e, 3, 7, R128_A64_TS_WU);
            r128_a64_umov_w_s(e, 27, 3, 0);
        } else {
            /* codes 2, 3, 6 and 7: one level,
               slot = top - (int) (lvl + 0.5f) */
            r128_a64_dup_4s_lane(e, 29, 13, 1);
            r128_a64_fadd_s(e, 29, 3, 29);
            r128_a64_fcvtzs_w_s(e, 27, 29);
            r128_a64_movz_w(e, 29, (uint32_t) h->top, 0);
            r128_a64_sub_w_reg(e, 27, 29, 27);
            r128_a64_emit_st_reload(e);
            r128_a64_emit_tex_level(e, ds, st, lin_min, want_near, tex_sub);
        }
        b_end = r128_a64_b(e);
        r128_a64_patch19(e, b_mag, r128_a64_here(e));
        r128_a64_emit_st_reload(e);
        r128_a64_movz_w(e, 27, (uint32_t) h->top, 0);
        r128_a64_emit_tex_level(e, ds, st, lin_mag, want_near, tex_sub);
        r128_a64_patch26(e, b_end, r128_a64_here(e));
    }
}

/* The chroma keys, stage 0 only, compared against the nearest texel's
   key value in R128_A64_TS_TNEAR before filtering, as in
   rage128_texstage_run. Keys and masks come from the draw state (already
   converted by r3d_ck_to_argb) and are emitted as immediates.
   (texel & msk) == (key & msk) is the match. For the CLR_CMP_FCN_3D key
   the interpreter rejects the pixel when the compare the field names
   is true: code 3 "Texel = CLR_CMP_CLR_3D", code 2 "Texel !=
   CLR_CMP_CLR_3D" (RRG: MISC_3D_STATE_CNTL_REG, p. 3-260 / PDF 278).
   The draw state turns code 1 ("True") into code 3 with a zero key and
   mask, so every texel matches. The R128_TEX_CHROMA_KEY_ENABLE key
   rejects on a match. A reject is a branch on the caller's skip list.
   Only w29 and w30 are used, so the texel in w27 survives. */
static void
r128_a64_tex_ck(const r128_a64_tex_t *t)
{
    r128_a64_emit_t            *e     = t->e;
    const rage128_draw_state_t *ds    = t->ds;
    int                        *skips = t->skips, *nskip = t->nskip;

    if (ds->ck3d_on) {
        r128_a64_ldr_w(e, 29, 31, R128_A64_TS_TNEAR);
        r128_a64_mov_w_imm32(e, 30, ds->ck3d_msk);
        r128_a64_and_w_reg(e, 29, 29, 30);
        r128_a64_mov_w_imm32(e, 30, ds->ck3d_clr & ds->ck3d_msk);
        r128_a64_cmp_w_reg(e, 29, 30);
        skips[(*nskip)++] = r128_a64_bcond(e, ds->ckfn == 3 ? A64_EQ : A64_NE);
    }
    if (ds->ckc_on) {
        r128_a64_ldr_w(e, 29, 31, R128_A64_TS_TNEAR);
        r128_a64_mov_w_imm32(e, 30, ds->ckc_msk);
        r128_a64_and_w_reg(e, 29, 29, 30);
        r128_a64_mov_w_imm32(e, 30, ds->ckc_clr & ds->ckc_msk);
        r128_a64_cmp_w_reg(e, 29, 30);
        skips[(*nskip)++] = r128_a64_bcond(e, A64_EQ);
    }
}

/* Combine input: the texel in w27 to floats {tr, tg, tb, ta} in v26,
   each byte converted and divided by 255.0f (v23) as the C does. v26
   keeps the texel for the whole combine, because the R128_COMB_FCN_MSB
   variants and the texture-alpha blends read it whatever the factor
   selects are. */
static void
r128_a64_tex_unpack(const r128_a64_tex_t *t)
{
    r128_a64_emit_t *e = t->e;

    r128_a64_ubfx_w(e, 29, 27, 16, 8);
    r128_a64_ins_s_w(e, 26 /* vec */, 0, 29);
    /* v26 and w27 are in separate register files, so filling the lanes
       leaves the packed texel in w27 intact */
    r128_a64_ubfx_w(e, 29, 27, 8, 8);
    r128_a64_ins_s_w(e, 26, 1, 29);
    r128_a64_and_w_bitmask(e, 29, 27, 0xffu);
    r128_a64_ins_s_w(e, 26, 2, 29);
    r128_a64_lsr_w(e, 29, 27, 24);
    r128_a64_ins_s_w(e, 26, 3, 29);
    r128_a64_ucvtf_4s(e, 26, 26);
    r128_a64_fdiv_4s(e, 26, 26, 23);
}

/* The combine, r3d_tex_combine, in three steps. This one builds the
   operands: v27 = {fc, fa} from the COLOR_FACTOR and ALPHA_FACTOR
   selects, v7 = {ci, ia} from INPUT_FACTOR and INPUT_FACTOR_ALPHA
   (SDK: Texture Mapping, p. 6-43 / PDF 155, Tables 6-8 and 6-9;
   SDK: Texture Mapping, p. 6-44 / PDF 156, Table 6-11;
   SDK: Texture Mapping, p. 6-45 / PDF 157, Tables 6-12 to 6-14). Then
   tex_comb writes lanes 0-2 of v3 and tex_comba lane 3. v25 keeps the
   color entering the stage (prev in the C) until v3 replaces it at the
   end. The case labels name the xf86-video-r128 macro for each code; a
   code the C has no case for takes its default case here too.
   cc[] is CONSTANT_COLOR, emitted as immediates; the interpolated color
   is reloaded from R128_A64_TS_INTC. Constants: 1.0 in v13 lane 2, 0.5
   in lane 1; v29 and v30 are scratch. Each formula is the C's, with the
   same operation order and no fused multiply-add; the host test harness
   (tests/video/rage128/jit-harness, rr_tex_combine) carries a copy of the
   same formulas. */
static void
r128_a64_tex_operands(const r128_a64_tex_t *t)
{
    r128_a64_emit_t            *e  = t->e;
    const rage128_draw_state_t *ds = t->ds;
    const r3d_comb_desc_t      *cd = t->cd;

    switch (cd->cfac) {
        case 0: /* R128_COLOR_FACTOR_CONST_COLOR */
        case 1: /* R128_COLOR_FACTOR_NCONST_COLOR: 1.0f - cc[i] is
                   computed here, the same float subtraction the C does
                   per pixel */
            for (int i = 0; i < 3; i++) {
                r128_a64_mov_w_fbits(e, 29, cd->cfac ? 1.0f - ds->cc[i] : ds->cc[i]);
                r128_a64_ins_s_w(e, 27, i, 29);
            }
            break;
        case 5: /* R128_COLOR_FACTOR_NTEX: 1 - t */
            r128_a64_dup_4s_lane(e, 29, 13, 2);
            r128_a64_fsub_4s(e, 27, 29, 26);
            break;
        case 6: /* R128_COLOR_FACTOR_ALPHA: ta in each lane */
            r128_a64_dup_4s_lane(e, 27, 26, 3);
            break;
        case 7: /* R128_COLOR_FACTOR_NALPHA: 1 - ta in each lane */
            r128_a64_dup_4s_lane(e, 29, 13, 2);
            r128_a64_fsub_4s(e, 27, 29, 26);
            r128_a64_dup_4s_lane(e, 27, 27, 3);
            break;
        case 8: /* R128_COLOR_FACTOR_PREV_COLOR */
            r128_a64_mov_16b(e, 27, 25);
            break;
        default: /* 4, R128_COLOR_FACTOR_TEX, and unnamed codes */
            r128_a64_mov_16b(e, 27, 26);
            break;
    }
    if (cd->afac == 7) { /* R128_ALPHA_FACTOR_NTEX_ALPHA: fa = 1 - ta */
        r128_a64_dup_4s_lane(e, 29, 26, 3);
        r128_a64_dup_4s_lane(e, 30, 13, 2);
        r128_a64_fsub_s(e, 29, 30, 29);
        r128_a64_ins_elem_s(e, 27, 3, 29, 0);
    } else /* R128_ALPHA_FACTOR_TEX_ALPHA and every other code: fa = ta */
        r128_a64_ins_elem_s(e, 27, 3, 26, 3);
    switch (cd->ifac) {
        case 2: /* R128_INPUT_FACTOR_CONST_COLOR */
            for (int i = 0; i < 3; i++) {
                r128_a64_mov_w_fbits(e, 29, ds->cc[i]);
                r128_a64_ins_s_w(e, 7, i, 29);
            }
            break;
        case 3: /* R128_INPUT_FACTOR_CONST_ALPHA: cc[3] in each lane */
            r128_a64_mov_w_fbits(e, 29, ds->cc[3]);
            r128_a64_dup_4s_w(e, 7, 29);
            break;
        case 5: /* R128_INPUT_FACTOR_INT_ALPHA: interpolated alpha in
                   each lane */
            r128_a64_ldr_q(e, 7, 31, R128_A64_TS_INTC);
            r128_a64_dup_4s_lane(e, 7, 7, 3);
            break;
        case 8: /* R128_INPUT_FACTOR_PREV_COLOR */
            r128_a64_mov_16b(e, 7, 25);
            break;
        case 9: /* R128_INPUT_FACTOR_PREV_ALPHA: prev alpha in each lane */
            r128_a64_dup_4s_lane(e, 7, 25, 3);
            break;
        default: /* 4, R128_INPUT_FACTOR_INT_COLOR, and unnamed codes */
            r128_a64_ldr_q(e, 7, 31, R128_A64_TS_INTC);
            break;
    }
    switch (cd->ifaca) {
        case 1: /* R128_INP_FACTOR_A_CONST_ALPHA */
            r128_a64_mov_w_fbits(e, 29, ds->cc[3]);
            r128_a64_ins_s_w(e, 7, 3, 29);
            break;
        case 2: /* R128_INP_FACTOR_A_INT_ALPHA */
            r128_a64_ldr_q(e, 29, 31, R128_A64_TS_INTC);
            r128_a64_ins_elem_s(e, 7, 3, 29, 3);
            break;
        default: /* 4, R128_INP_FACTOR_A_PREV_ALPHA, and unnamed codes */
            r128_a64_ins_elem_s(e, 7, 3, 25, 3);
            break;
    }
}

/* The color function, PRIMARY_COMB_FCN (SDK: Texture Mapping, p. 6-42 /
   PDF 154, Table 6-7): lanes 0-2 of v3 from fc (v27), ci (v7) and the
   texel t (v26); v25 still holds prev. The SDK writes the add-signed
   bias as 128; on these 0..1 floats it is 0.5. R128_COMB_FCN_MSB
   (cd->fmsb) is bit 8 of the combine register in xf86-video-r128 and
   Mesa r128; the SDK table does not show it. With it set, codes 0, 4,
   5 and 6 take the other formulas below, as the interpreter models
   them. Mesa r128 r128_texstate.c r128UpdateTextureEnv sets it with
   code 4 for GL_BLEND on a Rage 128 Pro or M3, with the formula
   C = Cf(1-Ct)+CcCt. */
static void
r128_a64_tex_comb(const r128_a64_tex_t *t)
{
    r128_a64_emit_t            *e  = t->e;
    const rage128_draw_state_t *ds = t->ds;
    const r3d_comb_desc_t      *cd = t->cd;

    switch (cd->comb) {
        case 2: /* R128_COMB_COPY_INP: C = ci */
            r128_a64_mov_16b(e, 3, 7);
            break;
        case 0:
            if (cd->fmsb) { /* with the MSB: C = max(fc - ci, 0) */
                r128_a64_fsub_4s(e, 3, 27, 7);
                r128_a64_movi_4s_zero(e, 29);
                r128_a64_fmax_4s(e, 3, 3, 29);
            } else /* R128_COMB_DIS: C = t, the texel itself, whatever
                      COLOR_FACTOR selects */
                r128_a64_mov_16b(e, 3, 26);
            break;
        case 1: /* R128_COMB_COPY: C = fc */
            r128_a64_mov_16b(e, 3, 27);
            break;
        case 4:
            if (cd->fmsb) {                         /* with the MSB: C = ci*(1-t) + fc*t */
                r128_a64_dup_4s_lane(e, 29, 13, 2); /* 1.0 */
                r128_a64_fsub_4s(e, 30, 29, 26);    /* 1-t */
                r128_a64_fmul_4s(e, 30, 7, 30);     /* ci*(1-t) */
                r128_a64_fmul_4s(e, 29, 27, 26);    /* fc*t */
                r128_a64_fadd_4s(e, 3, 30, 29);
            } else { /* R128_COMB_MODULATE2X: C = min(2*ci*fc, 1); the
                        doubling is an add, which is exact */
                r128_a64_fmul_4s(e, 3, 7, 27);
                r128_a64_fadd_4s(e, 3, 3, 3);
                r128_a64_dup_4s_lane(e, 29, 13, 2);
                r128_a64_fmin_4s(e, 3, 3, 29);
            }
            break;
        case 5:
            if (cd->fmsb) {                         /* with the MSB: C = min(fc + ci*(1-t), 1) */
                r128_a64_dup_4s_lane(e, 29, 13, 2); /* 1.0 */
                r128_a64_fsub_4s(e, 30, 29, 26);    /* 1-t */
                r128_a64_fmul_4s(e, 30, 7, 30);     /* ci*(1-t) */
                r128_a64_fadd_4s(e, 3, 27, 30);     /* fc + ... */
                r128_a64_fmin_4s(e, 3, 3, 29);
            } else { /* R128_COMB_MODULATE4X: C = min(4*ci*fc, 1) */
                r128_a64_fmul_4s(e, 3, 7, 27);
                r128_a64_fadd_4s(e, 3, 3, 3);
                r128_a64_fadd_4s(e, 3, 3, 3);
                r128_a64_dup_4s_lane(e, 29, 13, 2);
                r128_a64_fmin_4s(e, 3, 3, 29);
            }
            break;
        case 6:
            if (cd->fmsb) {                     /* with the MSB: C = min(fc + ci*t, 1) */
                r128_a64_fmul_4s(e, 30, 7, 26); /* ci*t */
                r128_a64_fadd_4s(e, 3, 27, 30); /* fc + ci*t */
            } else                              /* R128_COMB_ADD: C = min(ci + fc, 1) */
                r128_a64_fadd_4s(e, 3, 7, 27);
            r128_a64_dup_4s_lane(e, 29, 13, 2);
            r128_a64_fmin_4s(e, 3, 3, 29);
            break;
        case 7: /* R128_COMB_ADD_SIGNED: C = clamp(ci + fc - 0.5, 0, 1) */
            r128_a64_fadd_4s(e, 3, 7, 27);
            r128_a64_dup_4s_lane(e, 30, 13, 1); /* 0.5 */
            r128_a64_fsub_4s(e, 3, 3, 30);
            r128_a64_movi_4s_zero(e, 29);
            r128_a64_fmax_4s(e, 3, 3, 29);
            r128_a64_dup_4s_lane(e, 29, 13, 2);
            r128_a64_fmin_4s(e, 3, 3, 29);
            break;
        case 14: /* R128_COMB_ADD_SIGNED2X:
                    C = clamp(2*(ci + fc - 0.5), 0, 1) */
            r128_a64_fadd_4s(e, 3, 7, 27);
            r128_a64_dup_4s_lane(e, 30, 13, 1); /* 0.5 */
            r128_a64_fsub_4s(e, 3, 3, 30);
            r128_a64_fadd_4s(e, 3, 3, 3); /* *2 */
            r128_a64_movi_4s_zero(e, 29);
            r128_a64_fmax_4s(e, 3, 3, 29);
            r128_a64_dup_4s_lane(e, 29, 13, 2);
            r128_a64_fmin_4s(e, 3, 3, 29);
            break;
        case 8:  /* R128_COMB_BLEND_VERTEX: by the interpolated alpha */
        case 9:  /* R128_COMB_BLEND_TEXTURE: by ta */
        case 10: /* R128_COMB_BLEND_CONST: by cc[3] */
        case 12: /* R128_COMB_BLEND_PREV: by prev alpha. The SDK text for
                    code 12 gives the texel alpha, as for code 9; the
                    interpreter uses the previous alpha the name says. */
        case 15: /* R128_COMB_BLEND_CONST_COLOR: by cc[i] per channel */
            /* the weight w into v29, then C = ci*(1-w) + fc*w */
            switch (cd->comb) {
                case 8:
                    r128_a64_ldr_q(e, 29, 31, R128_A64_TS_INTC);
                    r128_a64_dup_4s_lane(e, 29, 29, 3);
                    break;
                case 9:
                    r128_a64_dup_4s_lane(e, 29, 26, 3);
                    break;
                case 10:
                    r128_a64_mov_w_fbits(e, 29, ds->cc[3]);
                    r128_a64_dup_4s_w(e, 29, 29);
                    break;
                case 12:
                    r128_a64_dup_4s_lane(e, 29, 25, 3);
                    break;
                default: /* 15 */
                    for (int i = 0; i < 3; i++) {
                        r128_a64_mov_w_fbits(e, 30, ds->cc[i]);
                        r128_a64_ins_s_w(e, 29, i, 30);
                    }
                    break;
            }
            r128_a64_dup_4s_lane(e, 30, 13, 2); /* 1.0 */
            r128_a64_fsub_4s(e, 30, 30, 29);    /* 1-w */
            r128_a64_fmul_4s(e, 30, 7, 30);     /* ci*(1-w) */
            r128_a64_fmul_4s(e, 29, 27, 29);    /* fc*w */
            r128_a64_fadd_4s(e, 3, 30, 29);
            break;
        case 11:                                /* R128_COMB_BLEND_PREMULT: C = min(fc + ci*(1-ta), 1) */
            r128_a64_dup_4s_lane(e, 29, 26, 3); /* ta */
            r128_a64_dup_4s_lane(e, 30, 13, 2); /* 1.0 */
            r128_a64_fsub_4s(e, 29, 30, 29);    /* 1-ta */
            r128_a64_fmul_4s(e, 29, 7, 29);     /* ci*(1-ta) */
            r128_a64_fadd_4s(e, 3, 27, 29);
            r128_a64_fmin_4s(e, 3, 3, 30);
            break;
        case 13:                                /* R128_COMB_BLEND_PREMULT_INV: C = min(fc + ci*ta, 1) */
            r128_a64_dup_4s_lane(e, 29, 26, 3); /* ta */
            r128_a64_fmul_4s(e, 29, 7, 29);     /* ci*ta */
            r128_a64_fadd_4s(e, 3, 27, 29);
            r128_a64_dup_4s_lane(e, 30, 13, 2);
            r128_a64_fmin_4s(e, 3, 3, 30);
            break;
        case 3:  /* R128_COMB_MODULATE: C = ci * fc */
        default: /* no other code exists in the 4-bit field */
            r128_a64_fmul_4s(e, 3, 7, 27);
            break;
    }
}

/* The alpha function, COMB_FCN_ALPHA (SDK: Texture Mapping, p. 6-44 /
   PDF 156, Table 6-10): lane 3 of v3 from fa (v27 lane 3), ia (v7
   lane 3) and the texel alpha (v26 lane 3), then v3 becomes the running
   color in v25. The SDK lists codes 0-7 and 14; the C takes every other
   code as modulate. */
static void
r128_a64_tex_comba(const r128_a64_tex_t *t)
{
    r128_a64_emit_t       *e  = t->e;
    const r3d_comb_desc_t *cd = t->cd;

    switch (cd->comba) {
        case 0: /* R128_COMB_ALPHA_DIS: on the first stage A = ta, the
                   texel alpha itself, whatever ALPHA_FACTOR selects;
                   after it the incoming alpha in v25, as in the C */
            r128_a64_ins_elem_s(e, 3, 3, cd == &t->ds->comb[0] ? 26 : 25, 3);
            break;
        case 1: /* R128_COMB_ALPHA_COPY: A = fa */
            r128_a64_ins_elem_s(e, 3, 3, 27, 3);
            break;
        case 2: /* R128_COMB_ALPHA_COPY_INP: A = ia */
            r128_a64_ins_elem_s(e, 3, 3, 7, 3);
            break;
        case 4: /* R128_COMB_ALPHA_MODULATE2X: A = min(2*ia*fa, 1) */
            r128_a64_fmul_4s(e, 29, 7, 27);
            r128_a64_fadd_4s(e, 29, 29, 29);
            r128_a64_dup_4s_lane(e, 30, 13, 2);
            r128_a64_fmin_4s(e, 29, 29, 30);
            r128_a64_ins_elem_s(e, 3, 3, 29, 3);
            break;
        case 5: /* R128_COMB_ALPHA_MODULATE4X: A = min(4*ia*fa, 1) */
            r128_a64_fmul_4s(e, 29, 7, 27);
            r128_a64_fadd_4s(e, 29, 29, 29);
            r128_a64_fadd_4s(e, 29, 29, 29);
            r128_a64_dup_4s_lane(e, 30, 13, 2);
            r128_a64_fmin_4s(e, 29, 29, 30);
            r128_a64_ins_elem_s(e, 3, 3, 29, 3);
            break;
        case 6: /* R128_COMB_ALPHA_ADD: A = min(ia + fa, 1) */
            r128_a64_fadd_4s(e, 29, 7, 27);
            r128_a64_dup_4s_lane(e, 30, 13, 2);
            r128_a64_fmin_4s(e, 29, 29, 30);
            r128_a64_ins_elem_s(e, 3, 3, 29, 3);
            break;
        case 7: /* R128_COMB_ALPHA_ADD_SIGNED:
                   A = clamp(ia + fa - 0.5, 0, 1); the 0 for the lower
                   clamp is formed as 0.5 - 0.5 */
            r128_a64_fadd_4s(e, 29, 7, 27);
            r128_a64_dup_4s_lane(e, 30, 13, 1);
            r128_a64_fsub_4s(e, 29, 29, 30);
            r128_a64_fsub_4s(e, 30, 30, 30);
            r128_a64_fmax_4s(e, 29, 29, 30);
            r128_a64_dup_4s_lane(e, 30, 13, 2);
            r128_a64_fmin_4s(e, 29, 29, 30);
            r128_a64_ins_elem_s(e, 3, 3, 29, 3);
            break;
        case 14: /* R128_COMB_ALPHA_ADD_SIGNED2X:
                    A = clamp(2*(ia + fa - 0.5), 0, 1) */
            r128_a64_fadd_4s(e, 29, 7, 27);
            r128_a64_dup_4s_lane(e, 30, 13, 1);
            r128_a64_fsub_4s(e, 29, 29, 30);
            r128_a64_fadd_4s(e, 29, 29, 29);
            r128_a64_fsub_4s(e, 30, 30, 30);
            r128_a64_fmax_4s(e, 29, 29, 30);
            r128_a64_dup_4s_lane(e, 30, 13, 2);
            r128_a64_fmin_4s(e, 29, 29, 30);
            r128_a64_ins_elem_s(e, 3, 3, 29, 3);
            break;
        case 3:  /* R128_COMB_ALPHA_MODULATE: A = ia * fa */
        default: /* codes 8-13 and 15, which the SDK does not list */
            r128_a64_fmul_4s(e, 29, 7, 27);
            r128_a64_ins_elem_s(e, 3, 3, 29, 3);
            break;
    }
    r128_a64_mov_16b(e, 25, 3);
}

/* One stage, phase by phase; the chroma keys only on stage 0 when a
   key is on */
static void
r128_a64_emit_texstage(r128_a64_emit_t *e, const rage128_draw_state_t *ds, int st,
                       int *skips, int *nskip)
{
    r128_a64_tex_t t;

    r128_a64_tex_setup(&t, e, ds, st, skips, nskip);
    r128_a64_tex_coords(&t);
    r128_a64_tex_sample(&t);
    if (t.want_near)
        r128_a64_tex_ck(&t);
    r128_a64_tex_unpack(&t);
    r128_a64_tex_operands(&t);
    r128_a64_tex_comb(&t);
    r128_a64_tex_comba(&t);
}

/* A stage's W head: rhw into s15 and ir into s14. With perspective,
   rhw = w0*a + w1*b + w2*c over the three floats at rh (arhw for the
   primary W, a2rhw for the secondary stage's own W) and
   ir = rhw != 0 ? 1/rhw : 1, where NE holds for NaN as != does in C.
   Without it ir = 1.0f (FMOV immediate 0x70) and rhw = 0.0f, as in the
   C; nothing reads rhw then. In: weights in s16-s18, x16 = texctx. */
static void
r128_a64_emit_w_head(r128_a64_emit_t *e, int persp, int rh)
{
    if (persp) {
        r128_a64_ldr_s(e, 15, 16, rh);
        r128_a64_fmul_s(e, 15, 16, 15);
        r128_a64_ldr_s(e, 26, 16, rh + 4);
        r128_a64_fmul_s(e, 26, 17, 26);
        r128_a64_fadd_s(e, 15, 15, 26);
        r128_a64_ldr_s(e, 26, 16, rh + 8);
        r128_a64_fmul_s(e, 26, 18, 26);
        r128_a64_fadd_s(e, 15, 15, 26);
        r128_a64_dup_4s_lane(e, 14, 13, 2); /* 1.0f */
        r128_a64_fdiv_s(e, 26, 14, 15);
        r128_a64_fcmp_s0(e, 15);
        r128_a64_fcsel_s(e, 14, 26, 14, A64_NE);
    } else {
        r128_a64_fmov_s_imm(e, 14, 0x70);
        r128_a64_fmov_s_w(e, 15, 31);
    }
}

/* The whole inline texture code for one pixel, in place of the
   rage128_texstage_run call: rhw and ir once, the color snapshot, each
   enabled stage, then the texture-lighting pass. A secondary stage with
   a W of its own (r128_jit_sec_w_own) gets a second head before it, as
   the interpreter forms rhw2 and ir2 then; otherwise it reads the
   primary stage's s14 and s15, which stage 0 leaves intact. Chroma-key
   rejects go onto the caller's skip list. */
static void
r128_a64_emit_texstage_inline(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                              int *skips, int *nskip)
{
    r128_a64_ldr_x(e, 16, 0, (int) offsetof(r128_jit_tri_t, texctx));
    r128_a64_emit_w_head(e, ds->do_persp, (int) offsetof(r3d_texctx_t, arhw));
    /* int_color in the C: the interpolated-color and -alpha selects and
       the R128_COMB_BLEND_VERTEX weight read the color as it was before
       stage 0, on stage 1 and in the lighting pass too */
    r128_a64_str_q(e, 25, 31, R128_A64_TS_INTC);
    if (ds->tex_en)
        r128_a64_emit_texstage(e, ds, 0, skips, nskip);
    if (ds->sec_en) {
        if (r128_jit_sec_w_own(ds))
            r128_a64_emit_w_head(e, r128_jit_stage_persp(ds, 1),
                                 (int) (ds->sel_w ? offsetof(r3d_texctx_t, a2rhw)
                                                  : offsetof(r3d_texctx_t, arhw)));
        r128_a64_emit_texstage(e, ds, 1, skips, nskip);
    }
    /* Texture lighting, the interpreter's third combine pass on
       ds->lcomb: the output of the stages (v25, copied to v26) stands in
       for the texel, and the input is the color from before stage 0
       (SDK: Texture Mapping, p. 6-42 / PDF 154: "the first argument is
       implicitly the output of the texture combine units, and the
       second is the interpolated color or alpha values of the
       primitive"). Only the combine phases run, so t needs no sampling
       fields. */
    if (ds->light_on) {
        r128_a64_tex_t t = { .e = e, .ds = ds, .cd = &ds->lcomb };

        r128_a64_mov_16b(e, 26, 25);
        r128_a64_tex_operands(&t);
        r128_a64_tex_comb(&t);
        r128_a64_tex_comba(&t);
    }
}

/* Load the constants v8-v13 of the register map, using w16 as scratch
   (x16 is loaded again for each pixel). The polynomial must stay equal
   to the coefficients of r3d_log2f_fast, and 4096.0f and 1/4096 to
   those of r3d_texcoord_fx. SoA blocks with inline texture run this
   again at the head of the scalar loop, because their vector loop uses
   v8-v13 for other values. */
static void
r128_a64_emit_tex_banks(r128_a64_emit_t *e, const rage128_draw_state_t *ds)
{
    static const float poly[4]  = { -2.133847707f, 3.010783972f, -1.029521946f, 0.153918478f };
    const float        misc[4]  = { 4096.0f, 1.0f / 4096.0f, ds->lod_bias, -1000.0f };
    static const float misc2[4] = { 256.0f, 0.5f, 1.0f, 127.0f };
    const float        texwh[4] = { ds->texw0, ds->texh0, ds->texw1, ds->texh1 };

    r128_a64_movz_w(e, 16, 0x00ff, 0);
    r128_a64_movk_w(e, 16, 0x00ff, 1);
    r128_a64_dup_4s_w(e, 8, 16);
    r128_a64_movz_w(e, 16, 0x0080, 0);
    r128_a64_movk_w(e, 16, 0x0080, 1);
    r128_a64_dup_4s_w(e, 9, 16);
    for (int k = 0; k < 4; k++) {
        r128_a64_mov_w_fbits(e, 16, texwh[k]);
        r128_a64_ins_s_w(e, 10, k, 16);
        r128_a64_mov_w_fbits(e, 16, misc[k]);
        r128_a64_ins_s_w(e, 11, k, 16);
        r128_a64_mov_w_fbits(e, 16, poly[k]);
        r128_a64_ins_s_w(e, 12, k, 16);
        r128_a64_mov_w_fbits(e, 16, misc2[k]);
        r128_a64_ins_s_w(e, 13, k, 16);
    }
}

/* Prologue addition of a block that samples inline: save d8-d15, the
   callee-saved low halves of v8-v15 under AAPCS64, then load the
   constants. The epilogue below restores d8-d15. */
static void
r128_a64_emit_tex_prologue(r128_a64_emit_t *e, const rage128_draw_state_t *ds)
{
    r128_a64_stp_d_sp(e, 8, 9, R128_A64_SP_D8);
    r128_a64_stp_d_sp(e, 10, 11, R128_A64_SP_D8 + 16);
    r128_a64_stp_d_sp(e, 12, 13, R128_A64_SP_D8 + 32);
    r128_a64_stp_d_sp(e, 14, 15, R128_A64_SP_D8 + 48);
    r128_a64_emit_tex_banks(e, ds);
}

static void
r128_a64_emit_tex_epilogue(r128_a64_emit_t *e)
{
    r128_a64_ldp_d_sp(e, 8, 9, R128_A64_SP_D8);
    r128_a64_ldp_d_sp(e, 10, 11, R128_A64_SP_D8 + 16);
    r128_a64_ldp_d_sp(e, 12, 13, R128_A64_SP_D8 + 32);
    r128_a64_ldp_d_sp(e, 14, 15, R128_A64_SP_D8 + 48);
}

#endif /* VIDEO_ATI_RAGE128_CODEGEN_ARM64_TEX_H */

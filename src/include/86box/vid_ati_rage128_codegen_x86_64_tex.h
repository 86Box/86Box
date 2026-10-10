/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- x86-64 span JIT, inline texture stage.
 *
 *          Emits the texture stage of a compiled span: texel fetch and
 *          decode, the four clamp modes, nearest, bilinear and mipmapped
 *          filtering, the chroma-key test and the texture combine,
 *          including the texture-lighting pass. The emitted code must
 *          give the same result, bit for bit, as the interpreter's
 *          rage128_texstage_run in vid_ati_rage128_3d.c, and it follows
 *          the ARM64 emitter in vid_ati_rage128_codegen_arm64_tex.h
 *          operation for operation. It covers every texture datatype
 *          except 10, the SDK's 16-bpp pseudo color, and 13, which the
 *          SDK's datatype table leaves out. A draw with either keeps the
 *          per-pixel call to rage128_texstage_run the parent header emits.
 *
 *          Included only by vid_ati_rage128_codegen_x86_64.h. The
 *          vector loop's texture code in
 *          vid_ati_rage128_codegen_x86_64_soa_tex.h reuses the texel
 *          fetch, the decode subroutines and the packed lerp from this
 *          file.
 *
 *          The host must have SSE4.1 (roundss, pmulld, pinsrd,
 *          insertps). No AVX, FMA or BMI instruction is emitted.
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
#ifndef VIDEO_ATI_RAGE128_CODEGEN_X86_64_TEX_H
#define VIDEO_ATI_RAGE128_CODEGEN_X86_64_TEX_H

/* ------------------------------------------------------------------------
 * Register use of the inline texture stage. The stage runs after the
 * vertex color compute and before the specular and fog stages, and adds
 * these roles to the parent header's register map.
 *
 * General registers: the stage writes only the scratch registers rax,
 * rcx, rdx, r10 and r11, so every per-row register stays live.
 *   r11d  mip slot index into a level fetch, packed texel out of it
 *         (ARM64 w27)
 *   r10d  level width in the sampler, u inside a decode subroutine
 *         (ARM64 w25). In the pixel loop r10 holds the tested Z or
 *         stencil cell, so the generator saves it to R128_X64_TS_ZCELL
 *         around the stage when Z or stencil is on.
 *   rax, rcx, rdx  scratch (ARM64 w29, w30, x29). The decode
 *         subroutines' div takes its dividend in edx:eax and leaves the
 *         remainder in edx.
 *
 * XMM registers: xmm2..xmm10 (zline, dZdx, invs, vca, vcb, vcc and the
 * weights w0..w2) are not written; the weights are read all through the
 * stage, as ARM64 reads v16-v18. xmm11 carries the running color in and
 * the combined color out.
 *   xmm12  sp, then lod, then the {c0, c2} texel pair, then the
 *          combine result (ARM64 v3)
 *   xmm13  tp, then the {c1, c3} pair, then the combine input operand
 *          (ARM64 v7)
 *   xmm0   the texel as four floats, live through the combine
 *          (ARM64 v26)
 *   xmm1   the combine factor operand, or scratch (ARM64 v27)
 *   xmm14, xmm15  s and t into a level fetch, otherwise scratch
 *          (ARM64 v29-v31)
 * ARM64 keeps its constants in v8-v13. Here they are RIP-relative loads
 * from the block's constant pool: R128_X64_CP_LERPM, R128_X64_CP_LERPK,
 * R128_X64_CP_256I, R128_X64_CP_POLY, R128_X64_CP_TEXF,
 * R128_X64_CP_HALFF, R128_X64_CP_ONEF, and the per-state texture size
 * and LOD bias entries at cp_texwh and cp_lodm. ir and rhw (ARM64 s14,
 * s15) live in the frame.
 *
 * DXT and YUV texels decode in subroutines entered by CALL. The return
 * address is then on the stack, so every frame access inside a
 * subroutine adds R128_X64_SUBF (8) to the offset.
 *   in:  r10d = u, r11d = v, already wrapped. Under BORDER clamping
 *        either can be -1; the caller then replaces the result with the
 *        border color. R128_X64_TS_LW, R128_X64_TS_BASE,
 *        R128_X64_TS_MASK and R128_X64_TS_TEXP describe the sampled
 *        level.
 *   out: r11d = the texel as ARGB8888.
 *   Clobbers rax, rcx, rdx, r10d and the frame words R128_X64_TS_DXT0
 *   to R128_X64_TS_DXT2.
 * ---------------------------------------------------------------------- */

/* Frame words of the stage. They sit in the base frame's vector-loop
   area, offsets 48..207. A textured block that also has a vector loop
   keeps the loop's slots that must survive a texel fetch in an
   extension of R128_X64_SOAT_EXT bytes above the base frame (see the
   frame layout in the parent header), so the scalar stage needs no
   frame space beyond the base frame. */
/* clang-format off */
#define R128_X64_TS_UU0   48
#define R128_X64_TS_UU1   52
#define R128_X64_TS_VV0   56
#define R128_X64_TS_VV1   60
#define R128_X64_TS_BASE  64
#define R128_X64_TS_MASK  68
#define R128_X64_TS_LH    72
#define R128_X64_TS_LW    76
#define R128_X64_TS_WU    80
#define R128_X64_TS_WV    84
#define R128_X64_TS_S     88
#define R128_X64_TS_T     92
#define R128_X64_TS_F     96
#define R128_X64_TS_CA    100
#define R128_X64_TS_SLOTB 104
#define R128_X64_TS_TNEAR 108 /* nearest texel (chroma-key pre-filter) */
#define R128_X64_TS_IR    112 /* ir  (ARM64 s14)                       */
#define R128_X64_TS_RHW   116 /* rhw (ARM64 s15)                       */
#define R128_X64_TS_DXT0  120 /* block offset, then color-block offset,
                                 then one expanded endpoint channel    */
#define R128_X64_TS_DXT1  124 /* texel index in the block, then the
                                 packed result being built             */
#define R128_X64_TS_INTC  128 /* 16 bytes, 16-aligned: the color at
                                 block entry                           */
#define R128_X64_TS_PAL   144 /* qword: CI4/CI8 palette pointer        */
#define R128_X64_TS_TEXP  152 /* qword: host pointer to the level's
                                 backing store                         */
#define R128_X64_TS_TCTX  160 /* qword: texctx pointer                 */
#define R128_X64_TS_DXT2  168 /* class 3: selector bit position, then
                                 the alpha (classes 2 and 3);
                                 datatype 14: the alpha byte           */
/* Spills around the per-pixel helper call. R128_X64_TS_ZCELL also
   saves r10 around the inline stage. */
#define R128_X64_TS_RX0   172 /* dword: rx0 across the helper call     */
#define R128_X64_TS_ZCELL 176 /* qword: the tested Z/stencil cell      */
#define R128_X64_TS_E0    184 /* qword: e0 (SysV only)                 */
#define R128_X64_TS_E1    192 /* qword: e1 (SysV only)                 */
#define R128_X64_TS_TRAW  200 /* dword: CI index of the last texel, or
                                 the border color on a BORDER miss     */

#define R128_X64_TS_YUV0  R128_X64_TS_DXT0 /* pair offset, then the
                                              unclamped r              */
#define R128_X64_TS_YUV1  R128_X64_TS_DXT1 /* partial g, t - 100d      */
/* clang-format on */

/* Offset added to frame accesses inside a subroutine entered by CALL:
   the return address takes 8 bytes of stack. */
#define R128_X64_SUBF 8

/* The emitted code reads struct r3d_slot_desc_t at fixed offsets (lw 0,
   lh 4, texbase 8, base 16, mask 20) and steps through slot[] by its
   24-byte size, here and in the texture and vector-loop emitters of
   both architectures. The array below fails to compile if the size
   changes; any layout change needs every such site checked. */
typedef char r128_x64_slotdesc_size_check[(sizeof(struct r3d_slot_desc_t) == 24) ? 1 : -1];

/* ---- additional instruction emitters ---- */

/* pmulld xd, xs (66 0F 38 40 /r, SSE4.1): the low 32 bits of each lane's
   product, the same value as a uint32_t multiply in C. */
#define r128_x64_pmulld(e, d, s) r128_x64_sse38_rr(e, 0x40, d, s)

/* roundss xd, xs, imm8 (66 0F 3A 0A /r ib, SSE4.1). Mode 8 rounds to
   nearest even and mode 9 rounds down, both with the inexact exception
   suppressed: the interpreter's nearbyintf (in the default rounding
   mode) and floorf. */
static void
r128_x64_roundss(r128_x64_emit_t *e, int xd, int xs, int mode)
{
    r128_x64_e8(e, 0x66);
    r128_x64_rex(e, 0, xd, 0, xs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x3A);
    r128_x64_e8(e, 0x0A);
    r128_x64_modrm_reg(e, xd, xs);
    r128_x64_e8(e, (uint8_t) mode);
}

/* cvttss2si rd64, xs (F3 REX.W 0F 2C /r): float to int64, truncating.
   Exact for every float below 2^63 in magnitude; NaN and anything
   larger give 0x8000000000000000. */
static void
r128_x64_cvttss2si_r64(r128_x64_emit_t *e, int rd, int xs)
{
    r128_x64_e8(e, 0xF3);
    r128_x64_e8(e, (uint8_t) (0x48 | (((rd >> 3) & 1) << 2) | ((xs >> 3) & 1)));
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x2C);
    r128_x64_modrm_reg(e, rd, xs);
}

/* imul rd32, rs32 (0F AF /r) */
static void
r128_x64_imul_r_r(r128_x64_emit_t *e, int rd, int rs)
{
    r128_x64_rex(e, 0, rd, 0, rs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0xAF);
    r128_x64_modrm_reg(e, rd, rs);
}

/* imul rd32, rs32, imm32 (69 /r id) */
static void
r128_x64_imul_r_imm(r128_x64_emit_t *e, int rd, int rs, int32_t imm)
{
    r128_x64_rex(e, 0, rd, 0, rs);
    r128_x64_e8(e, 0x69);
    r128_x64_modrm_reg(e, rd, rs);
    r128_x64_e32(e, (uint32_t) imm);
}

/* div rs32 (F7 /6): unsigned edx:eax / rs, quotient to eax, remainder
   to edx. Callers clear edx first. */
static void
r128_x64_div_r32(r128_x64_emit_t *e, int rs)
{
    r128_x64_rex(e, 0, 0, 0, rs);
    r128_x64_e8(e, 0xF7);
    r128_x64_modrm_reg(e, 6, rs);
}

/* call rel32 (E8 cd) to a target already emitted in this block */
static void
r128_x64_call_to(r128_x64_emit_t *e, int target)
{
    int32_t rel;

    r128_x64_e8(e, 0xE8);
    rel = target - (e->pos + 4);
    r128_x64_e32(e, (uint32_t) rel);
}

/* eax = r3d_f2i(xmm0): truncate toward zero, saturate, NaN gives 0.
   The conversion is a subroutine emitted once per block (jumped over
   in line) and reached by a 5-byte CALL from each site, which keeps
   the scalar texture stage inside the 16 KB block. It touches no frame
   slot, so it works at any stack depth. cvttss2si returns 0x80000000
   for NaN and for anything out of range, which is already the answer
   below -2^31, so only that value takes a second look: cmp eax, 1
   overflows only for it. Then NaN (comiss of xmm0 with itself sets PF)
   gives 0, a clear sign bit gives 0x7fffffff, a set one keeps
   0x80000000. Clobbers rdx and the flags; xmm0 is only read. */
static void
r128_x64_emit_cvt_i32(r128_x64_emit_t *e)
{
    if (!e->cvt_i32_sub) {
        int b_over = r128_x64_jmp(e);
        int b_ok, b_nan, b_neg;

        e->cvt_i32_sub = r128_x64_here(e);
        r128_x64_cvttss2si_r32(e, X64_RAX, 0);
        r128_x64_alu_r_imm(e, 7, 0, X64_RAX, 1); /* cmp eax, 1 */
        b_ok = r128_x64_jcc(e, X64_CC_NO);
        r128_x64_comiss(e, 0, 0);
        b_nan = r128_x64_jcc(e, X64_CC_P);
        r128_x64_movd_r_x(e, X64_RDX, 0);
        r128_x64_alu_r_r(e, 0x85, 0, X64_RDX, X64_RDX);
        b_neg = r128_x64_jcc(e, X64_CC_S);
        r128_x64_mov_r32_imm32(e, X64_RAX, 0x7fffffffu);
        r128_x64_patch32(e, b_ok, r128_x64_here(e));
        r128_x64_patch32(e, b_neg, r128_x64_here(e));
        r128_x64_ret(e);
        r128_x64_patch32(e, b_nan, r128_x64_here(e));
        r128_x64_alu_r_r(e, 0x31, 0, X64_RAX, X64_RAX);
        r128_x64_ret(e);
        r128_x64_patch32(e, b_over, r128_x64_here(e));
    }
    r128_x64_call_to(e, e->cvt_i32_sub);
}

/* rcx = r3d_f2u(xmm1), zero-extended: 0 unless xmm1 > 0, 0xffffffff
   from 2^32 up. A once-per-block subroutine reached by CALL, as in
   r128_x64_emit_cvt_i32. The 64-bit cvttss2si is exact for every float
   below 2^63, so a result that already fits in 32 bits (it equals its
   own zero-extended low half) is the answer. Anything else is NaN, at
   most -1, or at least 2^32: a float bit pattern at or below 0x7f800000
   (unsigned) is a positive number or +infinity and saturates, above it
   is a negative number or a NaN and gives 0. Clobbers rdx and the
   flags; xmm1 is only read. */
static void
r128_x64_emit_cvt_u32(r128_x64_emit_t *e)
{
    if (!e->cvt_u32_sub) {
        int b_over = r128_x64_jmp(e);
        int b_ok, b_pos;

        e->cvt_u32_sub = r128_x64_here(e);
        r128_x64_cvttss2si_r64(e, X64_RCX, 1);
        r128_x64_mov_r_r(e, 0, X64_RDX, X64_RCX); /* edx = ecx */
        r128_x64_alu_r_r(e, 0x39, 1, X64_RDX, X64_RCX);
        b_ok = r128_x64_jcc(e, X64_CC_E);
        r128_x64_movd_r_x(e, X64_RDX, 1);
        r128_x64_mov_r32_imm32(e, X64_RCX, 0xffffffffu);
        r128_x64_alu_r_imm(e, 7, 0, X64_RDX, (int32_t) 0x7f800000);
        b_pos = r128_x64_jcc(e, X64_CC_BE);
        r128_x64_alu_r_r(e, 0x31, 0, X64_RCX, X64_RCX);
        r128_x64_patch32(e, b_ok, r128_x64_here(e));
        r128_x64_patch32(e, b_pos, r128_x64_here(e));
        r128_x64_ret(e);
        r128_x64_patch32(e, b_over, r128_x64_here(e));
    }
    r128_x64_call_to(e, e->cvt_u32_sub);
}

/* lea rd64, [base + disp] (REX.W 8D /r) */
static void
r128_x64_lea_disp(r128_x64_emit_t *e, int rd, int base, int32_t disp)
{
    r128_x64_rex(e, 1, rd, 0, base);
    r128_x64_e8(e, 0x8D);
    r128_x64_modrm_mem(e, rd, base, disp);
}

/* lea rd64, [base + idx*2^scale] (REX.W 8D /r). With zero-extended
   32-bit operands the low 32 bits of the result are the 32-bit sum.
   Every use in this file reads only those low 32 bits afterwards, so
   it matches the 32-bit arithmetic of the interpreter and of ARM64.
   The vector loop's texture code also uses it to add to a 64-bit
   pointer. */
static void
r128_x64_lea_sib(r128_x64_emit_t *e, int rd, int base, int idx, int scale)
{
    r128_x64_rex(e, 1, rd, idx, base);
    r128_x64_e8(e, 0x8D);
    r128_x64_modrm_sib(e, rd, base, idx, scale, 0);
}

/* Loads from [base + idx*2^scale + disp]: movzx rd32, byte (0F B6),
   movzx rd32, word (0F B7) and mov rd32, dword (8B). */
static void
r128_x64_ldzx8_sibd(r128_x64_emit_t *e, int rd, int base, int idx, int scale,
                    int32_t disp)
{
    r128_x64_rex(e, 0, rd, idx, base);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0xB6);
    r128_x64_modrm_sib(e, rd, base, idx, scale, disp);
}

static void
r128_x64_ldzx16_sibd(r128_x64_emit_t *e, int rd, int base, int idx, int scale,
                     int32_t disp)
{
    r128_x64_rex(e, 0, rd, idx, base);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0xB7);
    r128_x64_modrm_sib(e, rd, base, idx, scale, disp);
}

static void
r128_x64_ld32_sibd(r128_x64_emit_t *e, int rd, int base, int idx, int scale,
                   int32_t disp)
{
    r128_x64_rex(e, 0, rd, idx, base);
    r128_x64_e8(e, 0x8B);
    r128_x64_modrm_sib(e, rd, base, idx, scale, disp);
}

/* movss [b + o], xs (F3 0F 11) and movd xd, [b + o] (66 0F 6E) */
#define r128_x64_movss_st(e, s, b, o) r128_x64_sse_rm(e, 0xF3, 0x11, s, b, o)
#define r128_x64_movd_x_m(e, x, b, o) r128_x64_sse_rm(e, 0x66, 0x6E, x, b, o)

/* ------------------------------------------------------------------------
 * S3TC (DXT) texel decode for datatype 0. It is emitted once per stage
 * as a subroutine inside the block and reached by CALL from every fetch
 * site. A stage has up to 14 fetch sites (both levels of a trilinear
 * 2x2, the nearest texel for the chroma key, and the magnification
 * path) and the decode runs to a few hundred instructions, so a copy at
 * each site would not fit the 16 KB block (R128_X64_BLOCK_SIZE). Frame
 * accesses inside the subroutine carry the R128_X64_SUBF bias. The code
 * follows case 0 of the interpreter's r3d_texel operation for
 * operation. The block class h->s3tc (1 DXT1, 2 DXT2/3, 3 DXT4/5) is
 * fixed when the block is compiled; class 0 takes the DXT1 path, as in
 * the interpreter.
 * ---------------------------------------------------------------------- */

/* The third scratch word, R128_X64_TS_DXT2, is always a frame word on
   x86-64. The ARM64 twin moves it into a vector lane in blocks whose
   vector loop keeps a live slot at that frame offset; here the vector
   loop's live slots sit in the extension above the base frame, so the
   frame word is free. */
static void
r128_x64_emit_dxt_sw_st(r128_x64_emit_t *e, int rs)
{
    r128_x64_st(e, 0, rs, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT2);
}

static void
r128_x64_emit_dxt_sw_ld(r128_x64_emit_t *e, int rd)
{
    r128_x64_ld(e, 0, rd, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT2);
}

/* eax = the byte (size 0), 16-bit word (1) or 32-bit word (2) at
   texbase[(base + eax) & mask], with texbase, base and mask from the
   level's frame words. As in the interpreter only the start address is
   masked; a wider read takes the following bytes unmasked. Clobbers
   rdx. */
static void
r128_x64_emit_dxt_fetch(r128_x64_emit_t *e, int size)
{
    r128_x64_alu_r_mem(e, 0x03, 0, X64_RAX, X64_RSP,
                       R128_X64_SUBF + R128_X64_TS_BASE);
    r128_x64_alu_r_mem(e, 0x23, 0, X64_RAX, X64_RSP,
                       R128_X64_SUBF + R128_X64_TS_MASK);
    r128_x64_ld(e, 1, X64_RDX, X64_RSP, R128_X64_SUBF + R128_X64_TS_TEXP);
    if (size == 0)
        r128_x64_ldzx8_sibd(e, X64_RAX, X64_RDX, X64_RAX, 0, 0);
    else if (size == 1)
        r128_x64_ldzx16_sibd(e, X64_RAX, X64_RDX, X64_RAX, 0, 0);
    else
        r128_x64_ld32_sibd(e, X64_RAX, X64_RDX, X64_RAX, 0, 0);
}

/* eax = ((src >> lo) & (2^width - 1)) * 255 / (2^width - 1), the integer
   expansion of a 565 endpoint channel the interpreter uses, with a real
   division rather than a shift approximation. Clobbers ecx and edx.
   src is only read, so a caller that reuses it must not pass eax, ecx
   or edx. */
static void
r128_x64_emit_dxt_expand(r128_x64_emit_t *e, int src, int lo, int width)
{
    r128_x64_mov_r_r(e, 0, X64_RAX, src);
    if (lo)
        r128_x64_shift_imm(e, 5, 0, X64_RAX, lo);
    r128_x64_alu_r_imm(e, 4, 0, X64_RAX, (int32_t) ((1u << width) - 1u));
    r128_x64_imul_r_imm(e, X64_RAX, X64_RAX, 255);
    r128_x64_mov_r32_imm32(e, X64_RCX, (1u << width) - 1u);
    r128_x64_alu_r_r(e, 0x31, 0, X64_RDX, X64_RDX);
    r128_x64_div_r32(e, X64_RCX);
}

/* The packed result at R128_X64_TS_DXT1 |= eax << shift. */
static void
r128_x64_emit_dxt_pack(r128_x64_emit_t *e, int shift)
{
    if (shift)
        r128_x64_shift_imm(e, 4, 0, X64_RAX, shift);
    r128_x64_alu_r_mem(e, 0x0B, 0, X64_RAX, X64_RSP,
                       R128_X64_SUBF + R128_X64_TS_DXT1);
    r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT1);
}

/* One channel of a two-thirds blend, selectors 2 and 3:
   eax = (2 * e_hi + e_lo) / 3, where e_hi and e_lo are the channel
   expanded from the endpoint registers whi and wlo (c0 and c1 are in
   r10d and r11d). chan picks the field: 0 red (bit 11, 5 bits), 1 green
   (bit 5, 6 bits), 2 blue (bit 0, 5 bits). */
static void
r128_x64_emit_dxt_third(r128_x64_emit_t *e, int whi, int wlo, int chan)
{
    static const int lo[3] = { 11, 5, 0 }, wd[3] = { 5, 6, 5 };

    r128_x64_emit_dxt_expand(e, whi, lo[chan], wd[chan]);
    r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT0);
    r128_x64_emit_dxt_expand(e, wlo, lo[chan], wd[chan]);
    r128_x64_ld(e, 0, X64_RCX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT0);
    r128_x64_lea_sib(e, X64_RAX, X64_RAX, X64_RCX, 1); /* e_lo + 2*e_hi */
    r128_x64_mov_r32_imm32(e, X64_RCX, 3);
    r128_x64_alu_r_r(e, 0x31, 0, X64_RDX, X64_RDX);
    r128_x64_div_r32(e, X64_RCX);
}

/* One channel of the DXT1 three-color mode, selector 2:
   eax = (e0 + e1) >> 1, from the endpoints in r10d and r11d. */
static void
r128_x64_emit_dxt_half(r128_x64_emit_t *e, int chan)
{
    static const int lo[3] = { 11, 5, 0 }, wd[3] = { 5, 6, 5 };

    r128_x64_emit_dxt_expand(e, X64_R10, lo[chan], wd[chan]);
    r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT0);
    r128_x64_emit_dxt_expand(e, X64_R11, lo[chan], wd[chan]);
    r128_x64_alu_r_mem(e, 0x03, 0, X64_RAX, X64_RSP,
                       R128_X64_SUBF + R128_X64_TS_DXT0);
    r128_x64_shift_imm(e, 5, 0, X64_RAX, 1);
}

/* Starts the packed result of a color arm with the alpha in bits 31:24:
   the decoded alpha for classes 2 and 3, 0xff for DXT1. */
static void
r128_x64_emit_dxt_arm_seed(r128_x64_emit_t *e, uint32_t cls)
{
    if (cls >= 2) {
        r128_x64_emit_dxt_sw_ld(e, X64_RAX);
        r128_x64_shift_imm(e, 4, 0, X64_RAX, 24);
    } else
        r128_x64_mov_r32_imm32(e, X64_RAX, 0xff000000u);
    r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT1);
}

/* Emit the whole per-stage DXT subroutine; returns its entry offset. */
static int
r128_x64_emit_dxt_sub(r128_x64_emit_t *e, const r3d_stage_hdr_t *h)
{
    uint32_t cls   = h->s3tc;
    int      entry = r128_x64_here(e);
    int      b_s0, b_s1, b_s2h = -1, b_s3z = -1;
    int      arm2, arm3;

    /* boff = ((v >> 2) * blocks per row + (u >> 2)) * block size (8 bytes
       for DXT1, 16 for classes 2 and 3) and texidx = (v & 3) << 2 |
       (u & 3). The shifts here are logical and the interpreter's are
       arithmetic; they differ only for a -1 BORDER coordinate, whose
       texel the caller discards. */
    r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_LW);
    r128_x64_alu_r_imm(e, 0, 0, X64_RAX, 3);
    r128_x64_shift_imm(e, 5, 0, X64_RAX, 2); /* bpitch          */
    r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
    r128_x64_shift_imm(e, 5, 0, X64_RCX, 2); /* v>>2            */
    r128_x64_imul_r_r(e, X64_RCX, X64_RAX);
    r128_x64_mov_r_r(e, 0, X64_RAX, X64_R10);
    r128_x64_shift_imm(e, 5, 0, X64_RAX, 2);
    r128_x64_alu_r_r(e, 0x01, 0, X64_RCX, X64_RAX);
    r128_x64_shift_imm(e, 4, 0, X64_RCX, (cls >= 2) ? 4 : 3);
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT0);
    r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
    r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 3);
    r128_x64_shift_imm(e, 4, 0, X64_RAX, 2);
    r128_x64_mov_r_r(e, 0, X64_RCX, X64_R10);
    r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 3);
    r128_x64_alu_r_r(e, 0x09, 0, X64_RCX, X64_RAX);
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT1);

    if (cls == 2) { /* DXT2/3: 4-bit alpha, two per byte in texel order,
                       low nibble first, scaled by 0x11 */
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT0);
        r128_x64_mov_r_r(e, 0, X64_R10, X64_RCX);
        r128_x64_shift_imm(e, 5, 0, X64_R10, 1);
        r128_x64_alu_r_r(e, 0x01, 0, X64_RAX, X64_R10);
        r128_x64_emit_dxt_fetch(e, 0);
        r128_x64_ld(e, 0, X64_R10, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT1);
        r128_x64_mov_r_r(e, 0, X64_RCX, X64_RAX);
        r128_x64_shift_imm(e, 5, 0, X64_RCX, 4);
        r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 0xf);
        r128_x64_test_r32_imm(e, X64_R10, 1);
        r128_x64_cmov(e, X64_CC_NE, 0, X64_RAX, X64_RCX);
        r128_x64_imul_r_imm(e, X64_RAX, X64_RAX, 0x11);
        r128_x64_emit_dxt_sw_st(e, X64_RAX);
    } else if (cls == 3) { /* DXT4/5: alpha endpoints a0, a1 and a 3-bit
                              selector per texel */
        int a_end[4];
        int n = 0, b_gt, b_c6, b_c7;

        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT0);
        r128_x64_emit_dxt_fetch(e, 0);
        r128_x64_mov_r_r(e, 0, X64_R10, X64_RAX); /* a0          */
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT0);
        r128_x64_alu_r_imm(e, 0, 0, X64_RAX, 1);
        r128_x64_emit_dxt_fetch(e, 0);
        r128_x64_mov_r_r(e, 0, X64_R11, X64_RAX); /* a1          */
        /* ac = (32-bit word at boff + 2 + (3 * texidx >> 3))
                >> (3 * texidx & 7) & 7. The bit position 3 * texidx
           waits in R128_X64_TS_DXT2 across the fetch. */
        r128_x64_ld(e, 0, X64_RCX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT1);
        r128_x64_lea_sib(e, X64_RCX, X64_RCX, X64_RCX, 1); /* bp=3*texidx */
        r128_x64_emit_dxt_sw_st(e, X64_RCX);
        r128_x64_mov_r_r(e, 0, X64_RAX, X64_RCX);
        r128_x64_shift_imm(e, 5, 0, X64_RAX, 3);
        r128_x64_alu_r_mem(e, 0x03, 0, X64_RAX, X64_RSP,
                           R128_X64_SUBF + R128_X64_TS_DXT0);
        r128_x64_alu_r_imm(e, 0, 0, X64_RAX, 2);
        r128_x64_emit_dxt_fetch(e, 2);
        r128_x64_emit_dxt_sw_ld(e, X64_RCX);
        r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 7);
        r128_x64_shr_cl(e, 0, X64_RAX);
        r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 7); /* ac          */
        /* The alpha a goes to ecx, tested in the order of the
           interpreter's if-chain: ac 0, ac 1, a0 > a1 (unsigned), ac 6,
           ac 7, then the five-step blend. */
        r128_x64_mov_r_r(e, 0, X64_RCX, X64_R10); /* default a0  */
        r128_x64_alu_r_r(e, 0x85, 0, X64_RAX, X64_RAX);
        b_s0 = r128_x64_jcc(e, X64_CC_E);
        r128_x64_alu_r_imm(e, 7, 0, X64_RAX, 1);
        r128_x64_cmov(e, X64_CC_E, 0, X64_RCX, X64_R11); /* a = a1      */
        b_s1 = r128_x64_jcc(e, X64_CC_E);
        r128_x64_alu_r_r(e, 0x39, 0, X64_R10, X64_R11);
        b_gt = r128_x64_jcc(e, X64_CC_A); /* a0>a1: /7   */
        r128_x64_mov_r32_imm32(e, X64_RDX, 0);
        r128_x64_alu_r_imm(e, 7, 0, X64_RAX, 6);
        r128_x64_cmov(e, X64_CC_E, 0, X64_RCX, X64_RDX); /* a = 0       */
        b_c6 = r128_x64_jcc(e, X64_CC_E);
        r128_x64_mov_r32_imm32(e, X64_RCX, 255);
        r128_x64_alu_r_imm(e, 7, 0, X64_RAX, 7);
        b_c7 = r128_x64_jcc(e, X64_CC_E); /* a = 255     */
        /* a = ((6-ac)*a0 + (ac-1)*a1)/5 */
        r128_x64_mov_r32_imm32(e, X64_RCX, 6);
        r128_x64_alu_r_r(e, 0x29, 0, X64_RCX, X64_RAX);
        r128_x64_imul_r_r(e, X64_RCX, X64_R10);
        r128_x64_alu_r_imm(e, 5, 0, X64_RAX, 1);
        r128_x64_imul_r_r(e, X64_RAX, X64_R11);
        r128_x64_alu_r_r(e, 0x01, 0, X64_RAX, X64_RCX);
        r128_x64_mov_r32_imm32(e, X64_RCX, 5);
        r128_x64_alu_r_r(e, 0x31, 0, X64_RDX, X64_RDX);
        r128_x64_div_r32(e, X64_RCX);
        r128_x64_mov_r_r(e, 0, X64_RCX, X64_RAX);
        a_end[n++] = r128_x64_jmp(e);
        r128_x64_patch32(e, b_gt, r128_x64_here(e));
        /* a = ((8-ac)*a0 + (ac-1)*a1)/7 */
        r128_x64_mov_r32_imm32(e, X64_RCX, 8);
        r128_x64_alu_r_r(e, 0x29, 0, X64_RCX, X64_RAX);
        r128_x64_imul_r_r(e, X64_RCX, X64_R10);
        r128_x64_alu_r_imm(e, 5, 0, X64_RAX, 1);
        r128_x64_imul_r_r(e, X64_RAX, X64_R11);
        r128_x64_alu_r_r(e, 0x01, 0, X64_RAX, X64_RCX);
        r128_x64_mov_r32_imm32(e, X64_RCX, 7);
        r128_x64_alu_r_r(e, 0x31, 0, X64_RDX, X64_RDX);
        r128_x64_div_r32(e, X64_RCX);
        r128_x64_mov_r_r(e, 0, X64_RCX, X64_RAX);
        a_end[n++] = r128_x64_jmp(e);
        r128_x64_patch32(e, b_s0, r128_x64_here(e));
        r128_x64_patch32(e, b_s1, r128_x64_here(e));
        r128_x64_patch32(e, b_c6, r128_x64_here(e));
        r128_x64_patch32(e, b_c7, r128_x64_here(e));
        for (int k = 0; k < n; k++)
            r128_x64_patch32(e, a_end[k], r128_x64_here(e));
        r128_x64_emit_dxt_sw_st(e, X64_RCX);
    }

    /* color block: c0 -> r10d, c1 -> r11d, sel -> eax */
    r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT0);
    if (cls >= 2) {
        r128_x64_alu_r_imm(e, 0, 0, X64_RAX, 8); /* coff=boff+8 */
        r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT0);
    }
    r128_x64_emit_dxt_fetch(e, 1);
    r128_x64_mov_r_r(e, 0, X64_R10, X64_RAX);
    r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT0);
    r128_x64_alu_r_imm(e, 0, 0, X64_RAX, 2);
    r128_x64_emit_dxt_fetch(e, 1);
    r128_x64_mov_r_r(e, 0, X64_R11, X64_RAX);
    r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT0);
    r128_x64_alu_r_imm(e, 0, 0, X64_RAX, 4);
    r128_x64_emit_dxt_fetch(e, 2);
    r128_x64_ld(e, 0, X64_RCX, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT1);
    r128_x64_alu_r_r(e, 0x01, 0, X64_RCX, X64_RCX);
    r128_x64_shr_cl(e, 0, X64_RAX);
    r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 3); /* sel         */
    /* texidx is not read again: R128_X64_TS_DXT1 now holds the packed
       result */

    r128_x64_alu_r_imm(e, 7, 0, X64_RAX, 2);
    arm2 = r128_x64_jcc(e, X64_CC_E);
    r128_x64_alu_r_imm(e, 7, 0, X64_RAX, 3);
    arm3 = r128_x64_jcc(e, X64_CC_E);
    r128_x64_alu_r_imm(e, 7, 0, X64_RAX, 1);
    r128_x64_cmov(e, X64_CC_E, 0, X64_R10, X64_R11); /* sel 1: use c1     */
    /* selectors 0 and 1: the endpoint itself */
    r128_x64_emit_dxt_arm_seed(e, cls);
    r128_x64_emit_dxt_expand(e, X64_R10, 11, 5);
    r128_x64_emit_dxt_pack(e, 16);
    r128_x64_emit_dxt_expand(e, X64_R10, 5, 6);
    r128_x64_emit_dxt_pack(e, 8);
    r128_x64_emit_dxt_expand(e, X64_R10, 0, 5);
    r128_x64_emit_dxt_pack(e, 0);
    r128_x64_ld(e, 0, X64_R11, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT1);
    r128_x64_ret(e);

    /* selector 2: (2 * c0 + c1) / 3 per channel, or for DXT1 with
       c0 <= c1 (three-color mode) the average (c0 + c1) >> 1 */
    r128_x64_patch32(e, arm2, r128_x64_here(e));
    if (cls < 2) {
        r128_x64_alu_r_r(e, 0x39, 0, X64_R10, X64_R11);
        b_s2h = r128_x64_jcc(e, X64_CC_BE); /* c0 <= c1: average   */
    }
    r128_x64_emit_dxt_arm_seed(e, cls);
    r128_x64_emit_dxt_third(e, X64_R10, X64_R11, 0);
    r128_x64_emit_dxt_pack(e, 16);
    r128_x64_emit_dxt_third(e, X64_R10, X64_R11, 1);
    r128_x64_emit_dxt_pack(e, 8);
    r128_x64_emit_dxt_third(e, X64_R10, X64_R11, 2);
    r128_x64_emit_dxt_pack(e, 0);
    r128_x64_ld(e, 0, X64_R11, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT1);
    r128_x64_ret(e);
    if (cls < 2) {
        r128_x64_patch32(e, b_s2h, r128_x64_here(e));
        r128_x64_emit_dxt_arm_seed(e, cls);
        r128_x64_emit_dxt_half(e, 0);
        r128_x64_emit_dxt_pack(e, 16);
        r128_x64_emit_dxt_half(e, 1);
        r128_x64_emit_dxt_pack(e, 8);
        r128_x64_emit_dxt_half(e, 2);
        r128_x64_emit_dxt_pack(e, 0);
        r128_x64_ld(e, 0, X64_R11, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT1);
        r128_x64_ret(e);
    }

    /* selector 3: (c0 + 2 * c1) / 3 per channel, or for DXT1 with
       c0 <= c1 transparent black (0) */
    r128_x64_patch32(e, arm3, r128_x64_here(e));
    if (cls < 2) {
        r128_x64_alu_r_r(e, 0x39, 0, X64_R10, X64_R11);
        b_s3z = r128_x64_jcc(e, X64_CC_BE); /* c0 <= c1: black      */
    }
    r128_x64_emit_dxt_arm_seed(e, cls);
    r128_x64_emit_dxt_third(e, X64_R11, X64_R10, 0); /* endpoints swapped */
    r128_x64_emit_dxt_pack(e, 16);
    r128_x64_emit_dxt_third(e, X64_R11, X64_R10, 1);
    r128_x64_emit_dxt_pack(e, 8);
    r128_x64_emit_dxt_third(e, X64_R11, X64_R10, 2);
    r128_x64_emit_dxt_pack(e, 0);
    r128_x64_ld(e, 0, X64_R11, X64_RSP, R128_X64_SUBF + R128_X64_TS_DXT1);
    r128_x64_ret(e);
    if (cls < 2) {
        r128_x64_patch32(e, b_s3z, r128_x64_here(e));
        r128_x64_alu_r_r(e, 0x31, 0, X64_R11, X64_R11); /* r11d = 0      */
        r128_x64_ret(e);
    }
    return entry;
}

/* ------------------------------------------------------------------------
 * YUV texel decode for datatypes 11 and 12 (YUV 422, a Y per texel and
 * a U and V shared by each pair) and 14 (AYUV 444) (SDK: Texture
 * Mapping, p. 6-40 / PDF 152, Table 6-3). Emitted once per stage like
 * the DXT subroutine, with the same contract. It follows r3d_texel
 * cases 11, 12 and 14 and the shared r128_yuv_to_rgb operation for
 * operation. The matrix terms can be negative, so >> 8 is an
 * arithmetic shift (sar) and the clamps to 0..255 are signed compares.
 * ---------------------------------------------------------------------- */
static int
r128_x64_emit_yuv_sub(r128_x64_emit_t *e, const r3d_stage_hdr_t *h)
{
    uint32_t dt    = h->dt;
    int      entry = r128_x64_here(e);

    if (dt == 14) {
        /* One 32-bit word at (base + (v * lw + u) * 4) & mask holding
           A, Y, U (cb) and V (cr) from bit 31 down. The alpha byte
           waits in R128_X64_TS_DXT2. */
        r128_x64_ld(e, 0, X64_RCX, X64_RSP, R128_X64_SUBF + R128_X64_TS_LW);
        r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
        r128_x64_imul_r_r(e, X64_RAX, X64_RCX);
        r128_x64_alu_r_r(e, 0x01, 0, X64_RAX, X64_R10);
        r128_x64_shift_imm(e, 4, 0, X64_RAX, 2);
        r128_x64_emit_dxt_fetch(e, 2);
        r128_x64_mov_r_r(e, 0, X64_R11, X64_RAX);
        r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
        r128_x64_shift_imm(e, 5, 0, X64_RCX, 24);
        r128_x64_emit_dxt_sw_st(e, X64_RCX);
        r128_x64_mov_r_r(e, 0, X64_R10, X64_R11); /* y           */
        r128_x64_shift_imm(e, 5, 0, X64_R10, 16);
        r128_x64_alu_r_imm(e, 4, 0, X64_R10, 0xff);
        r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11); /* cr          */
        r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 0xff);
        r128_x64_shift_imm(e, 5, 0, X64_R11, 8); /* cb          */
        r128_x64_alu_r_imm(e, 4, 0, X64_R11, 0xff);
    } else {
        /* Pair offset poff = (base + (v * lw + (u & ~1)) * 2) & mask,
           kept in R128_X64_TS_YUV0. Each byte is then read at
           (poff + k) & mask, as in the interpreter. The interpreter's
           byte order is Y0 U Y1 V for datatype 11 and U Y0 V Y1 for
           datatype 12; the SDK gives no order. */
        r128_x64_ld(e, 0, X64_RCX, X64_RSP, R128_X64_SUBF + R128_X64_TS_LW);
        r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
        r128_x64_imul_r_r(e, X64_RAX, X64_RCX);
        r128_x64_mov_r_r(e, 0, X64_RCX, X64_R10);
        r128_x64_alu_r_imm(e, 4, 0, X64_RCX, (int32_t) 0xfffffffeu);
        r128_x64_alu_r_r(e, 0x01, 0, X64_RAX, X64_RCX);
        r128_x64_alu_r_r(e, 0x01, 0, X64_RAX, X64_RAX);
        r128_x64_alu_r_mem(e, 0x03, 0, X64_RAX, X64_RSP,
                           R128_X64_SUBF + R128_X64_TS_BASE);
        r128_x64_alu_r_mem(e, 0x23, 0, X64_RAX, X64_RSP,
                           R128_X64_SUBF + R128_X64_TS_MASK);
        r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_YUV0);
        /* y at poff + (u&1)*2 (dt 11) / poff + 1 + (u&1)*2 (dt 12) */
        r128_x64_mov_r_r(e, 0, X64_RCX, X64_R10);
        r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 1);
        r128_x64_lea_sib(e, X64_RAX, X64_RAX, X64_RCX, 1);
        if (dt == 12)
            r128_x64_alu_r_imm(e, 0, 0, X64_RAX, 1);
        r128_x64_alu_r_mem(e, 0x23, 0, X64_RAX, X64_RSP,
                           R128_X64_SUBF + R128_X64_TS_MASK);
        r128_x64_ld(e, 1, X64_RDX, X64_RSP, R128_X64_SUBF + R128_X64_TS_TEXP);
        r128_x64_ldzx8_sibd(e, X64_R10, X64_RDX, X64_RAX, 0, 0); /* y     */
        /* cb at poff + 1 (dt 11) / poff (dt 12) */
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_YUV0);
        if (dt == 11)
            r128_x64_alu_r_imm(e, 0, 0, X64_RAX, 1);
        r128_x64_alu_r_mem(e, 0x23, 0, X64_RAX, X64_RSP,
                           R128_X64_SUBF + R128_X64_TS_MASK);
        r128_x64_ldzx8_sibd(e, X64_R11, X64_RDX, X64_RAX, 0, 0); /* cb    */
        /* cr at poff + 3 (dt 11) / poff + 2 (dt 12) */
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_YUV0);
        r128_x64_alu_r_imm(e, 0, 0, X64_RAX, (dt == 11) ? 3 : 2);
        r128_x64_alu_r_mem(e, 0x23, 0, X64_RAX, X64_RSP,
                           R128_X64_SUBF + R128_X64_TS_MASK);
        r128_x64_ldzx8_sibd(e, X64_RAX, X64_RDX, X64_RAX, 0, 0); /* cr    */
    }

    /* c = y - 16, d = cb - 128, e = cr - 128, t = 298c + 128. The +128
       rounding term is common to r, g and b, so it is added once into
       t; the integer sums come out the same as the interpreter's. */
    r128_x64_alu_r_imm(e, 5, 0, X64_R10, 16);
    r128_x64_alu_r_imm(e, 5, 0, X64_R11, 128);
    r128_x64_alu_r_imm(e, 5, 0, X64_RAX, 128);
    r128_x64_imul_r_imm(e, X64_R10, X64_R10, 298);
    r128_x64_alu_r_imm(e, 0, 0, X64_R10, 128); /* t           */
    r128_x64_imul_r_imm(e, X64_RCX, X64_RAX, 409);
    r128_x64_alu_r_r(e, 0x01, 0, X64_RCX, X64_R10);
    r128_x64_shift_imm(e, 7, 0, X64_RCX, 8); /* r           */
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_SUBF + R128_X64_TS_YUV0);
    r128_x64_imul_r_imm(e, X64_RCX, X64_R11, 100);
    r128_x64_mov_r_r(e, 0, X64_RDX, X64_R10);
    r128_x64_alu_r_r(e, 0x29, 0, X64_RDX, X64_RCX); /* t - 100d    */
    r128_x64_st(e, 0, X64_RDX, X64_RSP, R128_X64_SUBF + R128_X64_TS_YUV1);
    r128_x64_imul_r_imm(e, X64_RCX, X64_RAX, 208); /* 208e (e dead) */
    r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SUBF + R128_X64_TS_YUV1);
    r128_x64_alu_r_r(e, 0x29, 0, X64_RAX, X64_RCX);
    r128_x64_shift_imm(e, 7, 0, X64_RAX, 8);       /* g           */
    r128_x64_imul_r_imm(e, X64_RCX, X64_R11, 516); /* d dead      */
    r128_x64_alu_r_r(e, 0x01, 0, X64_RCX, X64_R10);
    r128_x64_shift_imm(e, 7, 0, X64_RCX, 8); /* b           */

    /* clamp r/g/b to 0..255 (signed) and pack under the alpha byte */
    r128_x64_mov_r32_imm32(e, X64_R10, 255);
    r128_x64_mov_r32_imm32(e, X64_RDX, 0);
    r128_x64_ld(e, 0, X64_R11, X64_RSP, R128_X64_SUBF + R128_X64_TS_YUV0);
    r128_x64_alu_r_r(e, 0x85, 0, X64_R11, X64_R11);
    r128_x64_cmov(e, X64_CC_S, 0, X64_R11, X64_RDX);
    r128_x64_alu_r_imm(e, 7, 0, X64_R11, 255);
    r128_x64_cmov(e, X64_CC_G, 0, X64_R11, X64_R10);
    r128_x64_alu_r_r(e, 0x85, 0, X64_RAX, X64_RAX);
    r128_x64_cmov(e, X64_CC_S, 0, X64_RAX, X64_RDX);
    r128_x64_alu_r_imm(e, 7, 0, X64_RAX, 255);
    r128_x64_cmov(e, X64_CC_G, 0, X64_RAX, X64_R10);
    r128_x64_alu_r_r(e, 0x85, 0, X64_RCX, X64_RCX);
    r128_x64_cmov(e, X64_CC_S, 0, X64_RCX, X64_RDX);
    r128_x64_alu_r_imm(e, 7, 0, X64_RCX, 255);
    r128_x64_cmov(e, X64_CC_G, 0, X64_RCX, X64_R10);
    if (dt == 14) {
        r128_x64_emit_dxt_sw_ld(e, X64_RDX);
        r128_x64_shift_imm(e, 4, 0, X64_RDX, 24);
    } else
        r128_x64_mov_r32_imm32(e, X64_RDX, 0xff000000u); /* opaque      */
    r128_x64_shift_imm(e, 4, 0, X64_R11, 16);
    r128_x64_alu_r_r(e, 0x09, 0, X64_RDX, X64_R11);
    r128_x64_shift_imm(e, 4, 0, X64_RAX, 8);
    r128_x64_alu_r_r(e, 0x09, 0, X64_RDX, X64_RAX);
    r128_x64_alu_r_r(e, 0x09, 0, X64_RDX, X64_RCX);
    r128_x64_mov_r_r(e, 0, X64_R11, X64_RDX);
    r128_x64_ret(e);
    return entry;
}

/* ------------------------------------------------------------------------
 * The states this file samples inline: the rule shared with ARM64
 * (r128_jit_texinline_can, which tests each enabled stage's datatype
 * with r128_jit_dt_inline_family). The generator also keeps table fog
 * on the helper call. Any other textured state keeps the per-pixel
 * rage128_texstage_run call.
 * ---------------------------------------------------------------------- */
static int
r128_x64_texinline_can(const rage128_draw_state_t *ds)
{
    return r128_jit_texinline_can(ds);
}

/* ------------------------------------------------------------------------
 * Emission helpers
 * ---------------------------------------------------------------------- */

/* Wraps one integer texel coordinate like r3d_tex_wrap. The mode is the
   PRIM_TEXTURE_CLAMP_MODE_S or PRIM_TEXTURE_CLAMP_MODE_T code (SDK:
   Texture Mapping, p. 6-41 / PDF 153, Table 6-6): 0 wrap, 1 mirror,
   2 clamp, 3 border color. BORDER returns -1 for a coordinate outside
   the level, and the texel fetch then substitutes the border color.
   in: coordinate in rc, level dimension (a power of two) in rn.
   out: the wrapped coordinate in rc. rs1 is scratch. */
static void
r128_x64_emit_wrap(r128_x64_emit_t *e, uint32_t mode, int rc, int rn, int rs1)
{
    switch (mode & 3) {
        case 0: /* WRAP: c & (n - 1) */
            r128_x64_mov_r_r(e, 0, rs1, rn);
            r128_x64_alu_r_imm(e, 5, 0, rs1, 1);
            r128_x64_alu_r_r(e, 0x21, 0, rc, rs1);
            break;
        case 1: /* MIRROR: m = c & (2n-1); m < n ? m : (2n-1) - m. The
                   bits of m are a subset of the mask 2n-1, so the
                   subtraction never borrows and equals the XOR. */
            r128_x64_mov_r_r(e, 0, rs1, rn);
            r128_x64_alu_r_r(e, 0x01, 0, rs1, rs1);
            r128_x64_alu_r_imm(e, 5, 0, rs1, 1);
            r128_x64_alu_r_r(e, 0x21, 0, rc, rs1);
            r128_x64_alu_r_r(e, 0x31, 0, rs1, rc);
            r128_x64_alu_r_r(e, 0x39, 0, rc, rn);
            r128_x64_cmov(e, X64_CC_GE, 0, rc, rs1);
            break;
        case 3: /* BORDER: (c < 0 || c >= n) -> -1 in one unsigned
                   compare: a negative c is above any positive n when
                   read as unsigned */
            r128_x64_mov_r32_imm32(e, rs1, 0xffffffffu);
            r128_x64_alu_r_r(e, 0x39, 0, rc, rn);
            r128_x64_cmov(e, X64_CC_AE, 0, rc, rs1);
            break;
        default: /* CLAMP: c <= 0 -> 0 (the same as c < 0, since 0 maps to
                    itself), c >= n -> n-1 */
            r128_x64_alu_r_r(e, 0x31, 0, rs1, rs1);
            r128_x64_alu_r_r(e, 0x85, 0, rc, rc);
            r128_x64_cmov(e, X64_CC_LE, 0, rc, rs1);
            r128_x64_mov_r_r(e, 0, rs1, rn);
            r128_x64_alu_r_imm(e, 5, 0, rs1, 1);
            r128_x64_alu_r_r(e, 0x39, 0, rc, rn);
            r128_x64_cmov(e, X64_CC_GE, 0, rc, rs1);
            break;
    }
}

/* One texel fetch and decode to packed ARGB8888, as r3d_texel does for
   each inline datatype. Addressing is linear: a draw whose texture
   level must be read through the tile transform is never compiled
   (rage128_jit_get_block refuses it). A BORDER coordinate arrives as
   -1. The fetch still happens, at an address masked like any other,
   and its result is then replaced by the stage's border color value
   as stored. The interpreter picks the border color without fetching,
   so the texel is the same.
   in: frame offsets of u and v; R128_X64_TS_LW, R128_X64_TS_BASE,
   R128_X64_TS_MASK and R128_X64_TS_TEXP describe the level.
   out: r11d = the texel. For CI4 and CI8, R128_X64_TS_TRAW gets the
   palette index (or the border color) for the chroma-key compare.
   Clobbers rax, rcx, rdx and r10d, plus what the decode subroutines
   clobber. */
static void
r128_x64_emit_texel(r128_x64_emit_t *e, const r3d_stage_hdr_t *h,
                    int uu_off, int vv_off, int tex_sub)
{
    uint32_t dt        = h->dt;
    int      border_en = ((h->clamp_s & 3) == 3) || ((h->clamp_t & 3) == 3);

    if (dt == 0 || dt == 11 || dt == 12 || dt == 14) {
        /* S3TC and YUV: u, v in r10d, r11d, decoded by the stage's
           subroutine */
        r128_x64_ld(e, 0, X64_R10, X64_RSP, uu_off);
        r128_x64_ld(e, 0, X64_R11, X64_RSP, vv_off);
        r128_x64_call_to(e, tex_sub);
        goto border;
    }
    r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_TS_LW);
    r128_x64_ld(e, 0, X64_R11, X64_RSP, vv_off);
    r128_x64_imul_r_r(e, X64_R11, X64_RAX); /* off = v*lw + u    */
    r128_x64_alu_r_mem(e, 0x03, 0, X64_R11, X64_RSP, uu_off);
    if (dt == 5)
        r128_x64_lea_sib(e, X64_R11, X64_R11, X64_R11, 1); /* *3 bytes */
    else if (dt == 6)
        r128_x64_shift_imm(e, 4, 0, X64_R11, 2);
    else if (dt == 3 || dt == 4 || dt == 15)
        r128_x64_alu_r_r(e, 0x01, 0, X64_R11, X64_R11);
    /* off is now a byte offset: times 3 for RGB888, 4 for ARGB8888, 2
       for the 16-bit formats; CI4, CI8, RGB332, Y8 and RGB8 take one
       byte per texel */
    r128_x64_alu_r_mem(e, 0x03, 0, X64_R11, X64_RSP, R128_X64_TS_BASE);
    r128_x64_alu_r_mem(e, 0x23, 0, X64_R11, X64_RSP, R128_X64_TS_MASK);
    r128_x64_ld(e, 1, X64_RAX, X64_RSP, R128_X64_TS_TEXP);
    switch (dt) {
        case 6:
            r128_x64_ld32_sibd(e, X64_R11, X64_RAX, X64_R11, 0, 0);
            break;
        case 5: /* 3 packed bytes; as in the interpreter only the first
                   byte's address is masked */
            r128_x64_ldzx8_sibd(e, X64_RCX, X64_RAX, X64_R11, 0, 2);
            r128_x64_ldzx16_sibd(e, X64_R11, X64_RAX, X64_R11, 0, 0);
            break;
        case 1:
        case 2:
        case 7:
        case 8:
        case 9:
            r128_x64_ldzx8_sibd(e, X64_R11, X64_RAX, X64_R11, 0, 0);
            break;
        default:
            r128_x64_ldzx16_sibd(e, X64_R11, X64_RAX, X64_R11, 0, 0);
            break;
    }
    switch (dt) {
        case 6: /* ARGB8888 as stored */
            break;
        case 1: /* CI4: one texel per byte, the low nibble indexes the
                   palette */
            r128_x64_alu_r_imm(e, 4, 0, X64_R11, 0xf);
            /* fall through */
        case 2: /* CI8: pal[idx], with the palette pointer from the
                   frame; the index goes to R128_X64_TS_TRAW */
            r128_x64_st(e, 0, X64_R11, X64_RSP, R128_X64_TS_TRAW);
            r128_x64_ld(e, 1, X64_RAX, X64_RSP, R128_X64_TS_PAL);
            r128_x64_ld32_sibd(e, X64_R11, X64_RAX, X64_R11, 2, 0);
            break;
        case 5: /* RGB888: bytes B, G, R in memory order, opaque; ecx
                   holds the R byte */
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 16);
            r128_x64_alu_r_r(e, 0x09, 0, X64_R11, X64_RCX);
            r128_x64_alu_r_imm(e, 1, 0, X64_R11, (int32_t) 0xff000000u);
            break;
        case 7: /* RGB332: shift-only expansion */
            r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
            r128_x64_shift_imm(e, 5, 0, X64_RAX, 5);
            r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 7);
            r128_x64_shift_imm(e, 4, 0, X64_RAX, 21);
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
            r128_x64_shift_imm(e, 5, 0, X64_RCX, 2);
            r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 7);
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 13);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_RCX);
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
            r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 3);
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 6);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_RCX);
            r128_x64_alu_r_imm(e, 1, 0, X64_RAX, (int32_t) 0xff000000u);
            r128_x64_mov_r_r(e, 0, X64_R11, X64_RAX);
            break;
        case 8: /* Y8: intensity to RGB, opaque */
            r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
            r128_x64_shift_imm(e, 4, 0, X64_RAX, 8);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_R11);
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 16);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_RCX);
            r128_x64_alu_r_imm(e, 1, 0, X64_RAX, (int32_t) 0xff000000u);
            r128_x64_mov_r_r(e, 0, X64_R11, X64_RAX);
            break;
        case 9: /* RGB8: intensity in all four channels */
            r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
            r128_x64_shift_imm(e, 4, 0, X64_RAX, 8);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_R11);
            r128_x64_mov_r_r(e, 0, X64_R11, X64_RAX);
            r128_x64_shift_imm(e, 4, 0, X64_R11, 16);
            r128_x64_alu_r_r(e, 0x09, 0, X64_R11, X64_RAX);
            break;
        case 3: /* ARGB1555. The 16-bit formats expand by shifting each
                   field to the top of its byte with the low bits zero,
                   as the interpreter does; 1555 alpha is 0 or 0xff. */
            r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
            r128_x64_shift_imm(e, 5, 0, X64_RAX, 10);
            r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 0x1f);
            r128_x64_shift_imm(e, 4, 0, X64_RAX, 19);
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
            r128_x64_shift_imm(e, 5, 0, X64_RCX, 5);
            r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 0x1f);
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 11);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_RCX);
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
            r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 0x1f);
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 3);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_RCX);
            r128_x64_mov_r32_imm32(e, X64_RCX, 0xff000000u);
            r128_x64_mov_r32_imm32(e, X64_RDX, 0);
            r128_x64_test_r32_imm(e, X64_R11, 0x8000);
            r128_x64_cmov(e, X64_CC_E, 0, X64_RCX, X64_RDX);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_RCX);
            r128_x64_mov_r_r(e, 0, X64_R11, X64_RAX);
            break;
        case 4: /* RGB565 */
            r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
            r128_x64_shift_imm(e, 5, 0, X64_RAX, 11);
            r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 0x1f);
            r128_x64_shift_imm(e, 4, 0, X64_RAX, 19);
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
            r128_x64_shift_imm(e, 5, 0, X64_RCX, 5);
            r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 0x3f);
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 10);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_RCX);
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
            r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 0x1f);
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 3);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_RCX);
            r128_x64_alu_r_imm(e, 1, 0, X64_RAX, (int32_t) 0xff000000u);
            r128_x64_mov_r_r(e, 0, X64_R11, X64_RAX);
            break;
        default: /* 15: ARGB4444 */
            r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
            r128_x64_shift_imm(e, 5, 0, X64_RAX, 12);
            r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 0xf);
            r128_x64_shift_imm(e, 4, 0, X64_RAX, 28);
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
            r128_x64_shift_imm(e, 5, 0, X64_RCX, 8);
            r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 0xf);
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 20);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_RCX);
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
            r128_x64_shift_imm(e, 5, 0, X64_RCX, 4);
            r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 0xf);
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 12);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_RCX);
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
            r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 0xf);
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 4);
            r128_x64_alu_r_r(e, 0x09, 0, X64_RAX, X64_RCX);
            r128_x64_mov_r_r(e, 0, X64_R11, X64_RAX);
            break;
    }
border:
    /* With TEX_MAP_AEN clear (h->aone) the fetched texel's alpha is forced
       to opaque before filtering, the S3TC and YUV subroutine results
       included. The border substitution comes after it, so the border
       color keeps its own alpha (Registers for CCE 3D Packets,
       SCALE_3D_CNTL). */
    if (h->aone)
        r128_x64_alu_r_imm(e, 1, 0, X64_R11, (int32_t) 0xff000000u);
    if (border_en) {
        /* If either coordinate is -1 the border color replaces the
           texel. The OR of the two has its sign bit set exactly then,
           since every other wrap result is non-negative. */
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, uu_off);
        r128_x64_alu_r_mem(e, 0x0B, 0, X64_RAX, X64_RSP, vv_off);
        r128_x64_mov_r32_imm32(e, X64_RCX, h->border);
        r128_x64_alu_r_r(e, 0x85, 0, X64_RAX, X64_RAX);
        r128_x64_cmov(e, X64_CC_S, 0, X64_R11, X64_RCX);
        if (h->dt == 1 || h->dt == 2) {
            /* The CI chroma-key compare also sees the border color, as
               it does in the interpreter. The load does not change the
               flags of the test above. */
            r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_TS_TRAW);
            r128_x64_cmov(e, X64_CC_S, 0, X64_RAX, X64_RCX);
            r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_TS_TRAW);
        }
    }
}

/* r3d_lerp_packed on four ARGB8888 lanes: vx = lerp(vx, vy, w), with the
   8.8 weight w of each lane in xmm14. M is 0x00ff00ff and K 0x00800080,
   so each step handles two channels at once. pmulld keeps the low 32
   bits of each product, the same value as the interpreter's uint32_t
   arithmetic, and nothing is rounded, so the result matches bit for
   bit. Writes vx, xmm0, xmm1 and xmm15; reads vy and xmm14 without
   changing them. vx and vy must be none of xmm0, xmm1, xmm14, xmm15. */
static void
r128_x64_emit_lerp_w(r128_x64_emit_t *e, int vx, int vy)
{
    r128_x64_sse_rip(e, 0, 0x28, 15, R128_X64_CP_256I);
    r128_x64_psubd(e, 15, 14);    /* iw = 256 - w      */
    r128_x64_movaps_rr(e, 0, vx); /* rb: (x & M)       */
    r128_x64_sse_rip(e, 0x66, 0xDB, 0, R128_X64_CP_LERPM);
    r128_x64_pmulld(e, 0, 15); /*   * iw            */
    r128_x64_movaps_rr(e, 1, vy);
    r128_x64_sse_rip(e, 0x66, 0xDB, 1, R128_X64_CP_LERPM);
    r128_x64_pmulld(e, 1, 14); /*   + (y & M) * w   */
    r128_x64_paddd(e, 0, 1);
    r128_x64_sse_rip(e, 0x66, 0xFE, 0, R128_X64_CP_LERPK); /* + K        */
    r128_x64_psrld(e, 0, 8);                               /*   >> 8            */
    r128_x64_movaps_rr(e, 1, vx);                          /* ag: (x >> 8) & M  */
    r128_x64_psrld(e, 1, 8);
    r128_x64_sse_rip(e, 0x66, 0xDB, 1, R128_X64_CP_LERPM);
    r128_x64_pmulld(e, 1, 15);
    r128_x64_movaps_rr(e, vx, vy); /* vx dead: (y>>8)&M */
    r128_x64_psrld(e, vx, 8);
    r128_x64_sse_rip(e, 0x66, 0xDB, vx, R128_X64_CP_LERPM);
    r128_x64_pmulld(e, vx, 14);
    r128_x64_paddd(e, 1, vx);
    r128_x64_sse_rip(e, 0x66, 0xFE, 1, R128_X64_CP_LERPK);
    r128_x64_psrld(e, 1, 8);
    r128_x64_sse_rip(e, 0x66, 0xDB, 0, R128_X64_CP_LERPM); /* rb & M     */
    r128_x64_sse_rip(e, 0x66, 0xDB, 1, R128_X64_CP_LERPM); /* ag & M     */
    r128_x64_pslld(e, 1, 8);
    r128_x64_por(e, 0, 1);
    r128_x64_movaps_rr(e, vx, 0);
}

/* The scalar stage's form: the one 8.8 weight at frame offset w_off is
   broadcast to every lane, and lanes 0 and 1 of vx and vy carry two
   texel pairs blended at once. Also writes xmm14. */
static void
r128_x64_emit_lerp_pair(r128_x64_emit_t *e, int vx, int vy, int w_off)
{
    r128_x64_movd_x_m(e, 14, X64_RSP, w_off);
    r128_x64_splat0(e, 14, 14); /* w splat           */
    r128_x64_emit_lerp_w(e, vx, vy);
}

/* Stores the chroma-key compare value of the texel just fetched in
   R128_X64_TS_TNEAR. CI formats compare the palette index
   (R128_X64_TS_TRAW, written by r128_x64_emit_texel), every other
   format the converted texel in r11d, as in the interpreter. rax is
   scratch. */
static void
r128_x64_emit_store_tnear(r128_x64_emit_t *e, const r3d_stage_hdr_t *h)
{
    if (h->dt == 1 || h->dt == 2) {
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_TS_TRAW);
        r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_TS_TNEAR);
    } else
        r128_x64_st(e, 0, X64_R11, X64_RSP, R128_X64_TS_TNEAR);
}

/* Samples one mip level, like r3d_tex_level.
   in: mip slot index in r11d, s in xmm14, t in xmm15 (both consumed).
   out: r11d = the filtered texel. With want_near it also stores the
   nearest texel's compare value for the chroma key (see
   r128_x64_emit_store_tnear); on a BORDER miss that value is the border
   color, as in the interpreter. Clobbers every scratch register of the
   stage. */
static void
r128_x64_emit_tex_level(r128_x64_emit_t *e, const rage128_draw_state_t *ds,
                        int st, int linear, int want_near, int tex_sub)
{
    const r3d_stage_hdr_t *h      = &ds->sh[st];
    int                    sd_off = (int) (st ? offsetof(r3d_texctx_t, sd1)
                                              : offsetof(r3d_texctx_t, sd0));
    int                    sl_off = sd_off + (int) offsetof(r3d_stage_desc_t, slot);

    /* rax = texctx + slot * 24, so [rax + sl_off] is
       texctx->sdN.slot[slot] */
    r128_x64_ld(e, 1, X64_RAX, X64_RSP, R128_X64_TS_TCTX);
    r128_x64_imul_r_imm(e, X64_RCX, X64_R11, (int) sizeof(struct r3d_slot_desc_t));
    r128_x64_alu_r_r(e, 0x01, 1, X64_RAX, X64_RCX);
    r128_x64_ld(e, 0, X64_R10, X64_RAX, sl_off + 0); /* lw          */
    r128_x64_st(e, 0, X64_R10, X64_RSP, R128_X64_TS_LW);
    r128_x64_ld(e, 0, X64_RCX, X64_RAX, sl_off + 4); /* lh          */
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_LH);
    r128_x64_ld(e, 0, X64_RCX, X64_RAX, sl_off + 16); /* base        */
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_BASE);
    r128_x64_ld(e, 0, X64_RCX, X64_RAX, sl_off + 20); /* mask        */
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_MASK);
    r128_x64_ld(e, 1, X64_RCX, X64_RAX, sl_off + 8); /* texbase     */
    r128_x64_st(e, 1, X64_RCX, X64_RSP, R128_X64_TS_TEXP);
    if (h->dt == 1 || h->dt == 2) {
        /* the stage's palette pointer for the CI decodes */
        r128_x64_ld(e, 1, X64_RAX, X64_RSP, R128_X64_TS_TCTX);
        r128_x64_ld(e, 1, X64_RCX, X64_RAX,
                    sd_off + (int) offsetof(r3d_stage_desc_t, pal));
        r128_x64_st(e, 1, X64_RCX, X64_RSP, R128_X64_TS_PAL);
    }

    /* fx = r3d_texcoord_fx(s * (float) lw), the product rounded to the
       nearest 1/4096; fy the same from t and lh */
    r128_x64_cvtsi2ss_r64(e, 0, X64_R10);
    r128_x64_mulss(e, 14, 0);
    r128_x64_sse_rip(e, 0xF3, 0x59, 14, R128_X64_CP_TEXF + 0); /* 4096  */
    r128_x64_roundss(e, 14, 14, 8);
    r128_x64_sse_rip(e, 0xF3, 0x59, 14, R128_X64_CP_TEXF + 4); /* 2^-12 */
    r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_TS_LH);
    r128_x64_cvtsi2ss_r64(e, 0, X64_RAX);
    r128_x64_mulss(e, 15, 0);
    r128_x64_sse_rip(e, 0xF3, 0x59, 15, R128_X64_CP_TEXF + 0);
    r128_x64_roundss(e, 15, 15, 8);
    r128_x64_sse_rip(e, 0xF3, 0x59, 15, R128_X64_CP_TEXF + 4);

    if (!linear) {
        /* nearest: u = wrap((int)floorf(fx)) */
        r128_x64_roundss(e, 0, 14, 9);
        r128_x64_emit_cvt_i32(e);
        r128_x64_emit_wrap(e, h->clamp_s, X64_RAX, X64_R10, X64_RCX);
        r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_TS_UU0);
        r128_x64_roundss(e, 0, 15, 9);
        r128_x64_emit_cvt_i32(e);
        r128_x64_ld(e, 0, X64_RCX, X64_RSP, R128_X64_TS_LH);
        r128_x64_emit_wrap(e, h->clamp_t, X64_RAX, X64_RCX, X64_RDX);
        r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_TS_VV0);
        r128_x64_emit_texel(e, h, R128_X64_TS_UU0, R128_X64_TS_VV0, tex_sub);
        if (want_near)
            r128_x64_emit_store_tnear(e, h);
        return;
    }

    if (want_near) {
        /* As in r3d_tex_level, the nearest texel for the chroma key is
           fetched first, from fx and fy before the bilinear -0.5
           offset. fx and fy stay in xmm14 and xmm15 across the fetch,
           which uses only general registers and the frame. */
        r128_x64_roundss(e, 0, 14, 9);
        r128_x64_emit_cvt_i32(e);
        r128_x64_emit_wrap(e, h->clamp_s, X64_RAX, X64_R10, X64_RCX);
        r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_TS_UU0);
        r128_x64_roundss(e, 0, 15, 9);
        r128_x64_emit_cvt_i32(e);
        r128_x64_ld(e, 0, X64_RCX, X64_RSP, R128_X64_TS_LH);
        r128_x64_emit_wrap(e, h->clamp_t, X64_RAX, X64_RCX, X64_RDX);
        r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_TS_VV0);
        r128_x64_emit_texel(e, h, R128_X64_TS_UU0, R128_X64_TS_VV0, tex_sub);
        r128_x64_emit_store_tnear(e, h);
        r128_x64_ld(e, 0, X64_R10, X64_RSP, R128_X64_TS_LW); /* lw back  */
    }

    /* bilinear: fu = fx - 0.5, u0 = floor(fu),
       wu = (uint32_t)((fu - u0) * 256 + 0.5); the same for v */
    r128_x64_sse_rip(e, 0xF3, 0x5C, 14, R128_X64_CP_HALFF);
    r128_x64_sse_rip(e, 0xF3, 0x5C, 15, R128_X64_CP_HALFF);
    r128_x64_roundss(e, 0, 14, 9);
    r128_x64_emit_cvt_i32(e); /* u0 */
    r128_x64_cvtsi2ss_r32(e, 0, X64_RAX);
    r128_x64_movaps_rr(e, 1, 14);
    r128_x64_subss(e, 1, 0);
    r128_x64_sse_rip(e, 0xF3, 0x59, 1, R128_X64_CP_TEXF + 8); /* 256   */
    r128_x64_sse_rip(e, 0xF3, 0x58, 1, R128_X64_CP_HALFF);
    r128_x64_emit_cvt_u32(e);
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_WU);
    r128_x64_mov_r_r(e, 0, X64_RCX, X64_RAX); /* raw u0      */
    r128_x64_emit_wrap(e, h->clamp_s, X64_RAX, X64_R10, X64_RDX);
    r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_TS_UU0);
    /* Clamp keeps the successor at the upper edge even for a saturated
       coordinate. The base wrap leaves n - 1 in the scratch register. */
    if ((h->clamp_s & 3) == 2) {
        r128_x64_alu_r_r(e, 0x39, 0, X64_RCX, X64_RDX);
        r128_x64_cmov(e, X64_CC_G, 0, X64_RCX, X64_RDX);
    }
    r128_x64_alu_r_imm(e, 0, 0, X64_RCX, 1);
    r128_x64_emit_wrap(e, h->clamp_s, X64_RCX, X64_R10, X64_RDX);
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_UU1);
    /* v side */
    r128_x64_roundss(e, 0, 15, 9);
    r128_x64_emit_cvt_i32(e); /* v0 */
    r128_x64_cvtsi2ss_r32(e, 0, X64_RAX);
    r128_x64_movaps_rr(e, 1, 15);
    r128_x64_subss(e, 1, 0);
    r128_x64_sse_rip(e, 0xF3, 0x59, 1, R128_X64_CP_TEXF + 8);
    r128_x64_sse_rip(e, 0xF3, 0x58, 1, R128_X64_CP_HALFF);
    r128_x64_emit_cvt_u32(e);
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_WV);
    r128_x64_mov_r_r(e, 0, X64_RCX, X64_RAX);
    r128_x64_ld(e, 0, X64_R10, X64_RSP, R128_X64_TS_LH); /* lh for v    */
    r128_x64_emit_wrap(e, h->clamp_t, X64_RAX, X64_R10, X64_RDX);
    r128_x64_st(e, 0, X64_RAX, X64_RSP, R128_X64_TS_VV0);
    /* Clamp keeps the successor at the upper edge even for a saturated
       coordinate. The base wrap leaves n - 1 in the scratch register. */
    if ((h->clamp_t & 3) == 2) {
        r128_x64_alu_r_r(e, 0x39, 0, X64_RCX, X64_RDX);
        r128_x64_cmov(e, X64_CC_G, 0, X64_RCX, X64_RDX);
    }
    r128_x64_alu_r_imm(e, 0, 0, X64_RCX, 1);
    r128_x64_emit_wrap(e, h->clamp_t, X64_RCX, X64_R10, X64_RDX);
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_VV1);

    /* fetch 2x2 into lanes: xmm12 = {c0, c2}, xmm13 = {c1, c3} */
    r128_x64_emit_texel(e, h, R128_X64_TS_UU0, R128_X64_TS_VV0, tex_sub);
    r128_x64_movd_x_r(e, 12, X64_R11);
    r128_x64_emit_texel(e, h, R128_X64_TS_UU1, R128_X64_TS_VV0, tex_sub);
    r128_x64_movd_x_r(e, 13, X64_R11);
    r128_x64_emit_texel(e, h, R128_X64_TS_UU0, R128_X64_TS_VV1, tex_sub);
    r128_x64_pinsrd(e, 12, X64_R11, 1);
    r128_x64_emit_texel(e, h, R128_X64_TS_UU1, R128_X64_TS_VV1, tex_sub);
    r128_x64_pinsrd(e, 13, X64_R11, 1);

    /* lerp(c0, c1) and lerp(c2, c3) by wu in one call, then the two
       results by wv */
    r128_x64_emit_lerp_pair(e, 12, 13, R128_X64_TS_WU);
    r128_x64_pshufd(e, 13, 12, 0x55);
    r128_x64_emit_lerp_pair(e, 12, 13, R128_X64_TS_WV);
    r128_x64_movd_r_x(e, X64_R11, 12);
}

/* reload the stashed s/t into the sampler input registers */
static void
r128_x64_emit_st_reload(r128_x64_emit_t *e)
{
    r128_x64_movss_ld(e, 14, X64_RSP, R128_X64_TS_S);
    r128_x64_movss_ld(e, 15, X64_RSP, R128_X64_TS_T);
}

/* Per-pixel LOD of one stage: the need_lod block of rage128_texstage_run
   for stage 0, the need_lod2 block for stage 1 (whose gradients follow
   ds->sec_sel, whose W gradients are its own under SEC_SRC_SEL_W, whose
   perspective enable is its own and whose texture size is stage 1's).
   in: sp in xmm12, tp in xmm13 (tp is consumed); the stage's rhw and
   the texctx pointer in the frame. out: lod in xmm12.
   The arithmetic follows the C operation for operation, and each C
   select becomes comiss and a branch. An unordered compare sets both
   parity and zero, so jp selects the quotient for a NaN rhw before je
   keeps zero for either signed zero, matching wp != 0 in C.
   The rho2 > 0 test needs no such care: a NaN
   rho2 fails it in the C, and jbe is taken on an unordered compare. */
static void
r128_x64_emit_lod(r128_x64_emit_t *e, const rage128_draw_state_t *ds, int st)
{
    int sel   = st && ds->sec_sel;
    int selw  = st && ds->sel_w;
    int persp = r128_jit_stage_persp(ds, st);
    int gs_x  = (int) (sel ? offsetof(r3d_texctx_t, dS2dx) : offsetof(r3d_texctx_t, dSdx));
    int gs_y  = (int) (sel ? offsetof(r3d_texctx_t, dS2dy) : offsetof(r3d_texctx_t, dSdy));
    int gt_x  = (int) (sel ? offsetof(r3d_texctx_t, dT2dx) : offsetof(r3d_texctx_t, dTdx));
    int gt_y  = (int) (sel ? offsetof(r3d_texctx_t, dT2dy) : offsetof(r3d_texctx_t, dTdy));
    int gw_x  = (int) (selw ? offsetof(r3d_texctx_t, dW2dx) : offsetof(r3d_texctx_t, dWdx));
    int gw_y  = (int) (selw ? offsetof(r3d_texctx_t, dW2dy) : offsetof(r3d_texctx_t, dWdy));
    int wh    = e->cp_texwh + (st ? 8 : 0); /* this stage's texw, texh */
    int b1, b_nan, b_skip;

    r128_x64_ld(e, 1, X64_RAX, X64_RSP, R128_X64_TS_TCTX);
    if (persp) {
        /* iw2 = (wp != 0) ? 1 / (wp * wp) : 0 */
        r128_x64_movss_ld(e, 14, X64_RSP, R128_X64_TS_RHW);
        r128_x64_mulss(e, 14, 14);
        r128_x64_sse_rip(e, 0xF3, 0x10, 0, R128_X64_CP_ONEF);
        r128_x64_divss(e, 0, 14);
        r128_x64_xorps(e, 14, 14);
        r128_x64_movss_ld(e, 1, X64_RSP, R128_X64_TS_RHW);
        r128_x64_comiss(e, 1, 14);
        b_nan = r128_x64_jcc(e, X64_CC_P);
        b1    = r128_x64_jcc(e, X64_CC_E);
        r128_x64_patch32(e, b_nan, r128_x64_here(e));
        r128_x64_movss_rr(e, 14, 0);
        r128_x64_patch32(e, b1, r128_x64_here(e)); /* iw2: xmm14  */
    }
    /* ax2 -> xmm15 */
    if (persp) {
        r128_x64_movss_ld(e, 15, X64_RAX, gs_x);
        r128_x64_sse_rm(e, 0xF3, 0x59, 15, X64_RSP, R128_X64_TS_RHW);
        r128_x64_movss_ld(e, 0, X64_RAX, gw_x);
        r128_x64_mulss(e, 0, 12); /* sp*dWdx     */
        r128_x64_subss(e, 15, 0);
        r128_x64_mulss(e, 15, 14); /* dsx         */
        r128_x64_movss_ld(e, 0, X64_RAX, gt_x);
        r128_x64_sse_rm(e, 0xF3, 0x59, 0, X64_RSP, R128_X64_TS_RHW);
        r128_x64_movss_ld(e, 1, X64_RAX, gw_x);
        r128_x64_mulss(e, 1, 13); /* tp*dWdx     */
        r128_x64_subss(e, 0, 1);
        r128_x64_mulss(e, 0, 14); /* dtx         */
    } else {
        r128_x64_movss_ld(e, 15, X64_RAX, gs_x);
        r128_x64_movss_ld(e, 0, X64_RAX, gt_x);
    }
    r128_x64_sse_rip(e, 0xF3, 0x59, 15, wh + 0); /* * texw      */
    r128_x64_sse_rip(e, 0xF3, 0x59, 0, wh + 4);  /* * texh      */
    r128_x64_mulss(e, 15, 15);
    r128_x64_mulss(e, 0, 0);
    r128_x64_addss(e, 15, 0);
    /* ay2 -> xmm0. dty is built in xmm13 after the last read of tp. */
    if (persp) {
        r128_x64_movss_ld(e, 0, X64_RAX, gs_y);
        r128_x64_sse_rm(e, 0xF3, 0x59, 0, X64_RSP, R128_X64_TS_RHW);
        r128_x64_movss_ld(e, 1, X64_RAX, gw_y);
        r128_x64_mulss(e, 1, 12); /* sp*dWdy     */
        r128_x64_subss(e, 0, 1);
        r128_x64_mulss(e, 0, 14); /* dsy         */
        r128_x64_movss_ld(e, 1, X64_RAX, gw_y);
        r128_x64_mulss(e, 1, 13); /* tp*dWdy     */
        r128_x64_movss_ld(e, 13, X64_RAX, gt_y);
        r128_x64_sse_rm(e, 0xF3, 0x59, 13, X64_RSP, R128_X64_TS_RHW);
        r128_x64_subss(e, 13, 1);
        r128_x64_mulss(e, 13, 14); /* dty         */
    } else {
        r128_x64_movss_ld(e, 0, X64_RAX, gs_y);
        r128_x64_movss_ld(e, 13, X64_RAX, gt_y);
    }
    r128_x64_sse_rip(e, 0xF3, 0x59, 0, wh + 0);
    r128_x64_sse_rip(e, 0xF3, 0x59, 13, wh + 4);
    r128_x64_mulss(e, 0, 0);
    r128_x64_mulss(e, 13, 13);
    r128_x64_addss(e, 0, 13);
    /* rho2 = ax2 > ay2 ? ax2 : ay2 */
    r128_x64_comiss(e, 15, 0);
    b1 = r128_x64_jcc(e, X64_CC_A);
    r128_x64_movss_rr(e, 15, 0);
    r128_x64_patch32(e, b1, r128_x64_here(e));
    /* lod = rho2 > 0 ? 0.5f * r3d_log2f_fast(rho2) + lod_bias : -1000.
       r3d_log2f_fast is the exponent field minus 127 plus a cubic in
       the mantissa m (forced into [1, 2)), evaluated in the C's order.
       Its result is finite for any bit pattern, so lod is never NaN. */
    r128_x64_sse_rip(e, 0xF3, 0x10, 12, e->cp_lodm + 4); /* -1000.0f    */
    r128_x64_xorps(e, 0, 0);
    r128_x64_comiss(e, 15, 0);
    b_skip = r128_x64_jcc(e, X64_CC_BE); /* also taken for NaN        */
    r128_x64_movd_r_x(e, X64_RCX, 15);
    r128_x64_mov_r_r(e, 0, X64_RAX, X64_RCX);
    r128_x64_shift_imm(e, 5, 0, X64_RAX, 23);
    r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 0xff);
    r128_x64_cvtsi2ss_r32(e, 0, X64_RAX);
    r128_x64_sse_rip(e, 0xF3, 0x5C, 0, R128_X64_CP_TEXF + 12); /* e-127  */
    r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 0x007fffff);
    r128_x64_alu_r_imm(e, 1, 0, X64_RCX, 0x3f800000);
    r128_x64_movd_x_r(e, 1, X64_RCX); /* m           */
    r128_x64_movss_rr(e, 15, 1);
    r128_x64_sse_rip(e, 0xF3, 0x59, 15, R128_X64_CP_POLY + 12); /* m*c3  */
    r128_x64_sse_rip(e, 0xF3, 0x58, 15, R128_X64_CP_POLY + 8);
    r128_x64_mulss(e, 15, 1);
    r128_x64_sse_rip(e, 0xF3, 0x58, 15, R128_X64_CP_POLY + 4);
    r128_x64_mulss(e, 15, 1);
    r128_x64_sse_rip(e, 0xF3, 0x58, 15, R128_X64_CP_POLY + 0);
    r128_x64_addss(e, 0, 15); /* log2        */
    r128_x64_sse_rip(e, 0xF3, 0x59, 0, R128_X64_CP_HALFF);
    r128_x64_sse_rip(e, 0xF3, 0x58, 0, e->cp_lodm + 0); /* + lod_bias  */
    r128_x64_movss_rr(e, 12, 0);
    r128_x64_patch32(e, b_skip, r128_x64_here(e));
}

/* One texture stage: coordinates and LOD, the r3d_tex_sample dispatch,
   the stage-0 chroma-key test and the combine, with the filter, mip and
   combine choices of the draw state fixed at compile time.
   in: weights in xmm8-xmm10 (left unchanged), ir, rhw and the texctx
   pointer in the frame, the running color in xmm11.
   out: the combined color in xmm11. A chroma-key reject adds a branch
   to the caller's pixel-skip list.
   Each phase below is one emitter; they share the stage state through
   r128_x64_tex_t, and r128_x64_emit_texstage runs them in order. */
typedef struct {
    r128_x64_emit_t            *e;
    const rage128_draw_state_t *ds;
    const r3d_stage_hdr_t      *h;
    const r3d_comb_desc_t      *cd;
    int                        *skips, *nskip;
    int                         st, sa, ta, has_lod, lin_min, lin_mag;
    int                         want_near, tex_sub;
} r128_x64_tex_t;

/* Fills the stage state, then emits the stage's S3TC or YUV decode
   subroutine if it needs one and the block's vector loop has not
   already emitted it. */
static void
r128_x64_tex_setup(r128_x64_tex_t *t, r128_x64_emit_t *e,
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
    /* The minification texel filter of r3d_tex_sample, the interpreter
       function the emitters are checked against: on the mip path
       bilinear for PRIM_MIN_BLEND_FCN codes 3 and 5 only, off it bit 0
       of the code (SDK: Texture Mapping, p. 6-40 / PDF 152,
       Table 6-4). r128_jit_minb_lin_min in the shared rules makes that
       choice for both backends. */
    t->lin_min   = r128_jit_minb_lin_min(h->minb, h->mipdis, has_lod);
    t->lin_mag   = (h->mag == 1);
    t->want_near = (st == 0) && ds->need_ck;
    t->tex_sub   = -1;

    if (e->tex_sub[st] >= 0)
        /* a block with a textured vector loop emitted the subroutine
           ahead of both loops */
        t->tex_sub = e->tex_sub[st];
    else if (h->dt == 0 || h->dt == 11 || h->dt == 12 || h->dt == 14) {
        /* emit the subroutine here, with a jump over it */
        int b_over = r128_x64_jmp(e);

        t->tex_sub = r128_x64_here(e);
        if (h->dt == 0)
            r128_x64_emit_dxt_sub(e, h);
        else
            r128_x64_emit_yuv_sub(e, h);
        r128_x64_patch32(e, b_over, r128_x64_here(e));
    }
}

/* sp = w0*a + w1*b + w2*c -> xmm12 and tp the same -> xmm13, from three
   consecutive floats in texctx (sta/stb/stc, or s2a/s2b/s2c for stage 1
   when ds->sec_sel is set). Then s = sp * ir and t = tp * ir go to the
   frame, and the per-pixel LOD follows if the stage has one. */
static void
r128_x64_tex_coords(const r128_x64_tex_t *t)
{
    r128_x64_emit_t *e  = t->e;
    int              sa = t->sa, ta = t->ta;

    r128_x64_ld(e, 1, X64_RAX, X64_RSP, R128_X64_TS_TCTX);
    r128_x64_movss_ld(e, 12, X64_RAX, sa);
    r128_x64_mulss(e, 12, 8);
    r128_x64_movss_ld(e, 0, X64_RAX, sa + 4);
    r128_x64_mulss(e, 0, 9);
    r128_x64_addss(e, 12, 0);
    r128_x64_movss_ld(e, 0, X64_RAX, sa + 8);
    r128_x64_mulss(e, 0, 10);
    r128_x64_addss(e, 12, 0);
    r128_x64_movss_ld(e, 13, X64_RAX, ta);
    r128_x64_mulss(e, 13, 8);
    r128_x64_movss_ld(e, 0, X64_RAX, ta + 4);
    r128_x64_mulss(e, 0, 9);
    r128_x64_addss(e, 13, 0);
    r128_x64_movss_ld(e, 0, X64_RAX, ta + 8);
    r128_x64_mulss(e, 0, 10);
    r128_x64_addss(e, 13, 0);
    /* s = sp * ir and t = tp * ir go to the frame: the LOD block uses
       the scratch registers, and each level fetch reloads s and t */
    r128_x64_movss_rr(e, 0, 12);
    r128_x64_sse_rm(e, 0xF3, 0x59, 0, X64_RSP, R128_X64_TS_IR);
    r128_x64_movss_st(e, 0, X64_RSP, R128_X64_TS_S);
    r128_x64_movss_rr(e, 0, 13);
    r128_x64_sse_rm(e, 0xF3, 0x59, 0, X64_RSP, R128_X64_TS_IR);
    r128_x64_movss_st(e, 0, X64_RSP, R128_X64_TS_T);

    if (t->has_lod)
        r128_x64_emit_lod(e, t->ds, t->st); /* lod -> xmm12 */
}

/* The r3d_tex_sample dispatch, with minb, mag and mipdis fixed at
   compile time. Off the mip path (no LOD, mipmapping disabled, or a
   minification code below 2) the top slot (the largest level) is
   sampled; when there is a LOD and the two filters differ, its sign
   picks the filter at run time. On the mip path lod <= 0 samples the
   top slot with the magnification filter, and lod > 0 samples one level
   or blends two. */
static void
r128_x64_tex_sample(const r128_x64_tex_t *t)
{
    r128_x64_emit_t            *e  = t->e;
    const rage128_draw_state_t *ds = t->ds;
    const r3d_stage_hdr_t      *h  = t->h;
    int                         st = t->st, has_lod = t->has_lod;
    int                         lin_min = t->lin_min, lin_mag = t->lin_mag;
    int                         want_near = t->want_near, tex_sub = t->tex_sub;

    if (!has_lod || h->mipdis || h->minb < 2) {
        if (!has_lod) {
            r128_x64_emit_st_reload(e);
            r128_x64_mov_r32_imm32(e, X64_R11, (uint32_t) h->top);
            r128_x64_emit_tex_level(e, ds, st, !(h->minb == 0 && h->mag == 0), want_near, tex_sub);
        } else if (lin_min == lin_mag) {
            r128_x64_emit_st_reload(e);
            r128_x64_mov_r32_imm32(e, X64_R11, (uint32_t) h->top);
            r128_x64_emit_tex_level(e, ds, st, lin_mag, want_near, tex_sub);
        } else {
            /* linear = lod > 0 ? (minb & 1) : (mag == 1) */
            int b_min, b_end;

            r128_x64_xorps(e, 0, 0);
            r128_x64_comiss(e, 12, 0);
            b_min = r128_x64_jcc(e, X64_CC_A);
            r128_x64_emit_st_reload(e);
            r128_x64_mov_r32_imm32(e, X64_R11, (uint32_t) h->top);
            r128_x64_emit_tex_level(e, ds, st, lin_mag, want_near, tex_sub);
            b_end = r128_x64_jmp(e);
            r128_x64_patch32(e, b_min, r128_x64_here(e));
            r128_x64_emit_st_reload(e);
            r128_x64_mov_r32_imm32(e, X64_R11, (uint32_t) h->top);
            r128_x64_emit_tex_level(e, ds, st, lin_min, want_near, tex_sub);
            r128_x64_patch32(e, b_end, r128_x64_here(e));
        }
    } else {
        int      mip_linear = (h->minb == 4 || h->minb == 5);
        int      b_mag, b_end, b1;
        float    topf = (float) h->top;
        uint32_t topb;

        memcpy(&topb, &topf, 4);
        /* lod <= 0: magnify. lod is never NaN, so jbe's unordered case
           does not arise. */
        r128_x64_xorps(e, 0, 0);
        r128_x64_comiss(e, 12, 0);
        b_mag = r128_x64_jcc(e, X64_CC_BE);
        /* lvl = lod > top ? top : lod */
        r128_x64_mov_r32_imm32(e, X64_RAX, topb);
        r128_x64_movd_x_r(e, 0, X64_RAX);
        r128_x64_comiss(e, 12, 0);
        b1 = r128_x64_jcc(e, X64_CC_BE);
        r128_x64_movss_rr(e, 12, 0);
        r128_x64_patch32(e, b1, r128_x64_here(e));
        if (mip_linear) {
            /* l0 = floor(lvl), f = lvl - l0, slotA = top - l0,
               slotB = max(slotA - 1, 0); the texels of the two slots
               are blended by (uint32_t)(f * 256 + 0.5), as
               r3d_lerp_argb does */
            r128_x64_roundss(e, 0, 12, 9);
            r128_x64_cvttss2si_r32(e, X64_RAX, 0); /* l0          */
            r128_x64_cvtsi2ss_r32(e, 1, X64_RAX);
            r128_x64_movss_rr(e, 0, 12);
            r128_x64_subss(e, 0, 1); /* f           */
            r128_x64_movss_st(e, 0, X64_RSP, R128_X64_TS_F);
            r128_x64_mov_r32_imm32(e, X64_R11, (uint32_t) h->top);
            r128_x64_alu_r_r(e, 0x29, 0, X64_R11, X64_RAX); /* slotA      */
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R11);
            r128_x64_alu_r_imm(e, 5, 0, X64_RCX, 1); /* slotB      */
            r128_x64_mov_r32_imm32(e, X64_RDX, 0);
            r128_x64_alu_r_r(e, 0x85, 0, X64_RCX, X64_RCX);
            r128_x64_cmov(e, X64_CC_S, 0, X64_RCX, X64_RDX);
            r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_SLOTB);
            r128_x64_emit_st_reload(e);
            r128_x64_emit_tex_level(e, ds, st, lin_min, want_near, tex_sub);
            r128_x64_st(e, 0, X64_R11, X64_RSP, R128_X64_TS_CA);
            r128_x64_ld(e, 0, X64_R11, X64_RSP, R128_X64_TS_SLOTB);
            r128_x64_emit_st_reload(e);
            /* slotB: no chroma-key texel, the interpreter passes
               nearest = NULL here */
            r128_x64_emit_tex_level(e, ds, st, lin_min, 0, tex_sub);
            r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_TS_CA);
            r128_x64_movd_x_r(e, 12, X64_RAX);
            r128_x64_movd_x_r(e, 13, X64_R11);
            r128_x64_movss_ld(e, 1, X64_RSP, R128_X64_TS_F); /* weight from f */
            r128_x64_sse_rip(e, 0xF3, 0x59, 1, R128_X64_CP_TEXF + 8);
            r128_x64_sse_rip(e, 0xF3, 0x58, 1, R128_X64_CP_HALFF);
            r128_x64_cvttss2si_r64(e, X64_RCX, 1);
            r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_WU);
            r128_x64_emit_lerp_pair(e, 12, 13, R128_X64_TS_WU);
            r128_x64_movd_r_x(e, X64_R11, 12);
        } else {
            /* slot = top - (int)(lvl + 0.5f) */
            r128_x64_movss_rr(e, 0, 12);
            r128_x64_sse_rip(e, 0xF3, 0x58, 0, R128_X64_CP_HALFF);
            r128_x64_cvttss2si_r32(e, X64_RAX, 0);
            r128_x64_mov_r32_imm32(e, X64_R11, (uint32_t) h->top);
            r128_x64_alu_r_r(e, 0x29, 0, X64_R11, X64_RAX);
            r128_x64_emit_st_reload(e);
            r128_x64_emit_tex_level(e, ds, st, lin_min, want_near, tex_sub);
        }
        b_end = r128_x64_jmp(e);
        r128_x64_patch32(e, b_mag, r128_x64_here(e));
        r128_x64_emit_st_reload(e);
        r128_x64_mov_r32_imm32(e, X64_R11, (uint32_t) h->top);
        r128_x64_emit_tex_level(e, ds, st, lin_mag, want_near, tex_sub);
        r128_x64_patch32(e, b_end, r128_x64_here(e));
    }
}

/* Chroma key on stage 0's nearest texel, before filtering, with the
   keys and masks of the draw state as immediates. Two independent
   tests, as in rage128_texstage_run:
   - CLR_CMP_FCN_3D: code 2 compares texel != CLR_CMP_CLR_3D and code 3
     texel == CLR_CMP_CLR_3D under CLR_CMP_MSK_3D (RRG:
     MISC_3D_STATE_CNTL_REG, p. 3-260 / PDF 278). The interpreter
     rejects the pixel when the compare is true, and its draw-state
     setup turns code 1 into code 3 with key and mask 0.
   - the TEX_CNTL_C chroma key (R128_TEX_CHROMA_KEY_ENABLE in
     xf86-video-r128) rejects on a masked match.
   A reject jumps to the pixel-skip path. Only rax and rcx are used, so
   the texel in r11d is kept. */
static void
r128_x64_tex_ck(const r128_x64_tex_t *t)
{
    r128_x64_emit_t            *e     = t->e;
    const rage128_draw_state_t *ds    = t->ds;
    int                        *skips = t->skips, *nskip = t->nskip;

    if (ds->ck3d_on) {
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_TS_TNEAR);
        r128_x64_mov_r32_imm32(e, X64_RCX, ds->ck3d_msk);
        r128_x64_alu_r_r(e, 0x21, 0, X64_RAX, X64_RCX);
        r128_x64_alu_r_imm(e, 7, 0, X64_RAX,
                           (int32_t) (ds->ck3d_clr & ds->ck3d_msk));
        skips[(*nskip)++] = r128_x64_jcc(e, ds->ckfn == 3 ? X64_CC_E : X64_CC_NE);
    }
    if (ds->ckc_on) {
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_TS_TNEAR);
        r128_x64_mov_r32_imm32(e, X64_RCX, ds->ckc_msk);
        r128_x64_alu_r_r(e, 0x21, 0, X64_RAX, X64_RCX);
        r128_x64_alu_r_imm(e, 7, 0, X64_RAX,
                           (int32_t) (ds->ckc_clr & ds->ckc_msk));
        skips[(*nskip)++] = r128_x64_jcc(e, X64_CC_E);
    }
}

/* The texture combine, as r3d_tex_combine does it, starts here: the
   texel in r11d becomes {tr, tg, tb, ta} in xmm0, each channel
   converted to float and divided by 255.0f like the interpreter does.
   xmm0 keeps the unmodified texel through the whole combine, because
   some ops read it whatever the factor selects say: the variants with
   the R128_COMB_FCN_MSB bit set and the blends by texel alpha. */
static void
r128_x64_tex_unpack(const r128_x64_tex_t *t)
{
    r128_x64_emit_t *e = t->e;

    r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
    r128_x64_shift_imm(e, 5, 0, X64_RAX, 16);
    r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 0xff);
    r128_x64_movd_x_r(e, 0, X64_RAX);
    r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
    r128_x64_shift_imm(e, 5, 0, X64_RAX, 8);
    r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 0xff);
    r128_x64_pinsrd(e, 0, X64_RAX, 1);
    r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
    r128_x64_alu_r_imm(e, 4, 0, X64_RAX, 0xff);
    r128_x64_pinsrd(e, 0, X64_RAX, 2);
    r128_x64_mov_r_r(e, 0, X64_RAX, X64_R11);
    r128_x64_shift_imm(e, 5, 0, X64_RAX, 24);
    r128_x64_pinsrd(e, 0, X64_RAX, 3);
    r128_x64_cvtdq2ps(e, 0, 0);
    r128_x64_sse_rip(e, 0, 0x5E, 0, R128_X64_CP_255F);
}

/* The combine operands, built first: xmm1 = {fc, fa}, the COLOR_FACTOR
   and ALPHA_FACTOR operands, and xmm13 = {ci, ia}, the INPUT_FACTOR and
   INPUT_FACTOR_ALPHA operands. The color op then writes lanes 0-2 of
   xmm12 and the alpha op lane 3. xmm11 holds the color entering the
   stage (prev) until the result replaces it at the end. The constant
   color cc[] goes in as immediates from the draw state; int_color, the
   color at block entry, is reloaded from R128_X64_TS_INTC.
   The codes are those of SDK Tables 6-8 and 6-9 (SDK: Texture Mapping,
   p. 6-43 / PDF 155), Table 6-11 (SDK: Texture Mapping, p. 6-44 /
   PDF 156) and Tables 6-12 to 6-14 (SDK: Texture Mapping, p. 6-45 /
   PDF 157). COLOR_FACTOR codes 0, 1 and 8, which the SDK does not
   list, are xf86-video-r128's R128_COLOR_FACTOR_CONST_COLOR,
   R128_COLOR_FACTOR_NCONST_COLOR and R128_COLOR_FACTOR_PREV_COLOR. The
   interpreter treats every other unlisted code as its default case,
   and so does this code. Every formula gives the interpreter's result
   bit for bit: the same float operations in the same order, and no
   fused multiply-add. The only changes are swapped operands of a
   multiply or add and the multiplies by 2 and 4 done as additions
   (see the color op), neither of which changes the result. */
static void
r128_x64_tex_operands(const r128_x64_tex_t *t)
{
    r128_x64_emit_t            *e  = t->e;
    const rage128_draw_state_t *ds = t->ds;
    const r3d_comb_desc_t      *cd = t->cd;

    switch (cd->cfac) {
        case 0: /* constant color */
        case 1: /* 1 - constant color, computed once at compile time
                   with the same float subtraction the interpreter does
                   per pixel */
            for (int i = 0; i < 3; i++) {
                float    f = cd->cfac ? 1.0f - ds->cc[i] : ds->cc[i];
                uint32_t b;

                memcpy(&b, &f, 4);
                r128_x64_mov_r32_imm32(e, X64_RAX, b);
                if (i == 0)
                    r128_x64_movd_x_r(e, 1, X64_RAX);
                else
                    r128_x64_pinsrd(e, 1, X64_RAX, i);
            }
            break;
        case 5: /* 1 - texel color */
            r128_x64_sse_rip(e, 0, 0x28, 1, R128_X64_CP_ONEF);
            r128_x64_subps(e, 1, 0);
            break;
        case 6: /* texel alpha in every lane */
            r128_x64_pshufd(e, 1, 0, 0xFF);
            break;
        case 7: /* 1 - texel alpha in every lane */
            r128_x64_sse_rip(e, 0, 0x28, 1, R128_X64_CP_ONEF);
            r128_x64_subps(e, 1, 0);
            r128_x64_pshufd(e, 1, 1, 0xFF);
            break;
        case 8: /* previous color */
            r128_x64_movaps_rr(e, 1, 11);
            break;
        default: /* 4, texel color, and unlisted codes */
            r128_x64_movaps_rr(e, 1, 0);
            break;
    }
    if (cd->afac == 7) { /* fa = 1 - ta */
        r128_x64_pshufd(e, 14, 0, 0xFF);
        r128_x64_sse_rip(e, 0xF3, 0x10, 15, R128_X64_CP_ONEF);
        r128_x64_subss(e, 15, 14);
        r128_x64_insertps(e, 1, 15, 3, 0);
    } else /* fa = ta: code 6 and every other code */
        r128_x64_insertps(e, 1, 0, 3, 3);
    switch (cd->ifac) {
        case 2: /* constant color */
            for (int i = 0; i < 3; i++) {
                uint32_t b;

                memcpy(&b, &ds->cc[i], 4);
                r128_x64_mov_r32_imm32(e, X64_RAX, b);
                if (i == 0)
                    r128_x64_movd_x_r(e, 13, X64_RAX);
                else
                    r128_x64_pinsrd(e, 13, X64_RAX, i);
            }
            break;
        case 3: /* constant alpha in every lane */
            {
                uint32_t b;

                memcpy(&b, &ds->cc[3], 4);
                r128_x64_mov_r32_imm32(e, X64_RAX, b);
                r128_x64_movd_x_r(e, 13, X64_RAX);
                r128_x64_splat0(e, 13, 13);
            }
            break;
        case 5: /* interpolator alpha (int_color) in every lane */
            r128_x64_movaps_ld(e, 13, X64_RSP, R128_X64_TS_INTC);
            r128_x64_pshufd(e, 13, 13, 0xFF);
            break;
        case 8: /* previous color */
            r128_x64_movaps_rr(e, 13, 11);
            break;
        case 9: /* previous alpha in every lane */
            r128_x64_pshufd(e, 13, 11, 0xFF);
            break;
        default: /* 4, interpolator color (int_color), and unlisted
                    codes */
            r128_x64_movaps_ld(e, 13, X64_RSP, R128_X64_TS_INTC);
            break;
    }
    switch (cd->ifaca) {
        case 1: /* constant alpha */
            {
                uint32_t b;

                memcpy(&b, &ds->cc[3], 4);
                r128_x64_mov_r32_imm32(e, X64_RAX, b);
                r128_x64_movd_x_r(e, 14, X64_RAX);
                r128_x64_insertps(e, 13, 14, 3, 0);
            }
            break;
        case 2: /* interpolator alpha (int_color) */
            r128_x64_movaps_ld(e, 14, X64_RSP, R128_X64_TS_INTC);
            r128_x64_insertps(e, 13, 14, 3, 3);
            break;
        default: /* 4, previous alpha, and unlisted codes */
            r128_x64_insertps(e, 13, 11, 3, 3);
            break;
    }
}

/* The color op, with the PRIMARY_COMB_FCN codes (SDK: Texture Mapping,
   p. 6-42 / PDF 154, Table 6-7; xf86-video-r128 names them
   R128_COMB_DIS to R128_COMB_BLEND_CONST_COLOR), which the interpreter
   also applies to stage 1 and the lighting pass. It fills lanes 0-2 of
   xmm12 from fc (xmm1), ci (xmm13) and the texel t (xmm0); xmm11 still
   holds prev.
   With the R128_COMB_FCN_MSB bit set, which the SDK does not describe,
   the interpreter gives codes 0, 4, 5 and 6 other functions; the labels
   below show both. 2x and 4x are formed as x + x and (x + x) + (x + x),
   which are exact and equal the interpreter's multiplies by 2 and 4. */
static void
r128_x64_tex_comb(const r128_x64_tex_t *t)
{
    r128_x64_emit_t            *e  = t->e;
    const rage128_draw_state_t *ds = t->ds;
    const r3d_comb_desc_t      *cd = t->cd;

    switch (cd->comb) {
        case 2: /* copy input: C = ci */
            r128_x64_movaps_rr(e, 12, 13);
            break;
        case 0:
            if (cd->fmsb) { /* subtract: C = max(fc - ci, 0) */
                r128_x64_movaps_rr(e, 12, 1);
                r128_x64_subps(e, 12, 13);
                r128_x64_sse_rip(e, 0, 0x5F, 12, R128_X64_CP_ZERO);
            } else /* disable: C = t, the texel itself, whatever
                      COLOR_FACTOR selects */
                r128_x64_movaps_rr(e, 12, 0);
            break;
        case 1: /* copy: C = fc */
            r128_x64_movaps_rr(e, 12, 1);
            break;
        case 4:
            if (cd->fmsb) { /* blend by texel color: C = ci*(1-t) + fc*t */
                r128_x64_sse_rip(e, 0, 0x28, 15, R128_X64_CP_ONEF);
                r128_x64_subps(e, 15, 0);  /* 1-t */
                r128_x64_mulps(e, 15, 13); /* ci*(1-t) */
                r128_x64_movaps_rr(e, 14, 1);
                r128_x64_mulps(e, 14, 0); /* fc*t */
                r128_x64_movaps_rr(e, 12, 15);
                r128_x64_addps(e, 12, 14);
            } else { /* modulate x2: C = min(2*ci*fc, 1) */
                r128_x64_movaps_rr(e, 12, 13);
                r128_x64_mulps(e, 12, 1);
                r128_x64_addps(e, 12, 12);
                r128_x64_sse_rip(e, 0, 0x5D, 12, R128_X64_CP_ONEF);
            }
            break;
        case 5:
            if (cd->fmsb) { /* C = min(fc + ci*(1-t), 1) */
                r128_x64_sse_rip(e, 0, 0x28, 15, R128_X64_CP_ONEF);
                r128_x64_subps(e, 15, 0);  /* 1-t */
                r128_x64_mulps(e, 15, 13); /* ci*(1-t) */
                r128_x64_movaps_rr(e, 12, 1);
                r128_x64_addps(e, 12, 15); /* fc + ... */
                r128_x64_sse_rip(e, 0, 0x5D, 12, R128_X64_CP_ONEF);
            } else { /* modulate x4: C = min(4*ci*fc, 1) */
                r128_x64_movaps_rr(e, 12, 13);
                r128_x64_mulps(e, 12, 1);
                r128_x64_addps(e, 12, 12);
                r128_x64_addps(e, 12, 12);
                r128_x64_sse_rip(e, 0, 0x5D, 12, R128_X64_CP_ONEF);
            }
            break;
        case 6:
            if (cd->fmsb) { /* C = min(fc + ci*t, 1) */
                r128_x64_movaps_rr(e, 15, 13);
                r128_x64_mulps(e, 15, 0); /* ci*t */
                r128_x64_movaps_rr(e, 12, 1);
                r128_x64_addps(e, 12, 15); /* fc + ci*t */
            } else {                       /* add: C = min(ci + fc, 1) */
                r128_x64_movaps_rr(e, 12, 13);
                r128_x64_addps(e, 12, 1);
            }
            r128_x64_sse_rip(e, 0, 0x5D, 12, R128_X64_CP_ONEF);
            break;
        case 7: /* add signed: C = clamp(ci + fc - 0.5, 0, 1) */
            r128_x64_movaps_rr(e, 12, 13);
            r128_x64_addps(e, 12, 1);
            r128_x64_sse_rip(e, 0, 0x5C, 12, R128_X64_CP_HALFF);
            r128_x64_sse_rip(e, 0, 0x5F, 12, R128_X64_CP_ZERO);
            r128_x64_sse_rip(e, 0, 0x5D, 12, R128_X64_CP_ONEF);
            break;
        case 14: /* add signed x2: C = clamp(2*(ci + fc - 0.5), 0, 1) */
            r128_x64_movaps_rr(e, 12, 13);
            r128_x64_addps(e, 12, 1);
            r128_x64_sse_rip(e, 0, 0x5C, 12, R128_X64_CP_HALFF);
            r128_x64_addps(e, 12, 12);
            r128_x64_sse_rip(e, 0, 0x5F, 12, R128_X64_CP_ZERO);
            r128_x64_sse_rip(e, 0, 0x5D, 12, R128_X64_CP_ONEF);
            break;
        case 8:  /* blend vertex: by the int_color alpha */
        case 9:  /* blend texture: by ta */
        case 10: /* blend constant: by cc[3] */
        case 12: /* blend previous: by the prev alpha */
        case 15: /* blend constant color: per channel by cc[i] */
            /* weights w -> xmm14, then C = ci*(1-w) + fc*w */
            switch (cd->comb) {
                case 8:
                    r128_x64_movaps_ld(e, 14, X64_RSP, R128_X64_TS_INTC);
                    r128_x64_pshufd(e, 14, 14, 0xFF);
                    break;
                case 9:
                    r128_x64_pshufd(e, 14, 0, 0xFF);
                    break;
                case 10:
                    {
                        uint32_t b;

                        memcpy(&b, &ds->cc[3], 4);
                        r128_x64_mov_r32_imm32(e, X64_RAX, b);
                        r128_x64_movd_x_r(e, 14, X64_RAX);
                        r128_x64_splat0(e, 14, 14);
                    }
                    break;
                case 12:
                    r128_x64_pshufd(e, 14, 11, 0xFF);
                    break;
                default: /* 15 */
                    for (int i = 0; i < 3; i++) {
                        uint32_t b;

                        memcpy(&b, &ds->cc[i], 4);
                        r128_x64_mov_r32_imm32(e, X64_RAX, b);
                        if (i == 0)
                            r128_x64_movd_x_r(e, 14, X64_RAX);
                        else
                            r128_x64_pinsrd(e, 14, X64_RAX, i);
                    }
                    break;
            }
            r128_x64_sse_rip(e, 0, 0x28, 15, R128_X64_CP_ONEF);
            r128_x64_subps(e, 15, 14); /* 1-w */
            r128_x64_mulps(e, 15, 13); /* ci*(1-w) */
            r128_x64_mulps(e, 14, 1);  /* fc*w */
            r128_x64_movaps_rr(e, 12, 15);
            r128_x64_addps(e, 12, 14);
            break;
        case 11:                             /* blend premultiply: C = min(fc + ci*(1-ta), 1) */
            r128_x64_pshufd(e, 14, 0, 0xFF); /* ta */
            r128_x64_sse_rip(e, 0, 0x28, 15, R128_X64_CP_ONEF);
            r128_x64_subps(e, 15, 14); /* 1-ta */
            r128_x64_mulps(e, 15, 13); /* ci*(1-ta) */
            r128_x64_movaps_rr(e, 12, 1);
            r128_x64_addps(e, 12, 15);
            r128_x64_sse_rip(e, 0, 0x5D, 12, R128_X64_CP_ONEF);
            break;
        case 13:                             /* blend premultiply inverse: C = min(fc + ci*ta, 1) */
            r128_x64_pshufd(e, 14, 0, 0xFF); /* ta */
            r128_x64_mulps(e, 14, 13);       /* ci*ta */
            r128_x64_movaps_rr(e, 12, 1);
            r128_x64_addps(e, 12, 14);
            r128_x64_sse_rip(e, 0, 0x5D, 12, R128_X64_CP_ONEF);
            break;
        case 3:  /* modulate: C = ci * fc */
        default: /* not reached for a 4-bit code; modulate, as in the
                    interpreter */
            r128_x64_movaps_rr(e, 12, 13);
            r128_x64_mulps(e, 12, 1);
            break;
    }
}

/* The alpha op, COMB_FCN_ALPHA (SDK: Texture Mapping, p. 6-44 /
   PDF 156, Table 6-10, codes 0 to 7 and 14): lane 3 of xmm12 from fa
   (lane 3 of xmm1), ia (lane 3 of xmm13) and the texel alpha (lane 3
   of xmm0). xmm12 then becomes the running color in xmm11. */
static void
r128_x64_tex_comba(const r128_x64_tex_t *t)
{
    r128_x64_emit_t       *e  = t->e;
    const r3d_comb_desc_t *cd = t->cd;

    switch (cd->comba) {
        case 0: /* disable: on the first stage A = ta, the texel alpha
                   itself, whatever ALPHA_FACTOR selects; after it the
                   incoming alpha in xmm11, as in the interpreter */
            r128_x64_insertps(e, 12, cd == &t->ds->comb[0] ? 0 : 11, 3, 3);
            break;
        case 1: /* copy: A = fa */
            r128_x64_insertps(e, 12, 1, 3, 3);
            break;
        case 2: /* copy input: A = ia */
            r128_x64_insertps(e, 12, 13, 3, 3);
            break;
        case 4: /* modulate x2: A = min(2 * ia * fa, 1) */
            r128_x64_movaps_rr(e, 14, 13);
            r128_x64_mulps(e, 14, 1);
            r128_x64_addps(e, 14, 14);
            r128_x64_sse_rip(e, 0, 0x5D, 14, R128_X64_CP_ONEF);
            r128_x64_insertps(e, 12, 14, 3, 3);
            break;
        case 5: /* modulate x4: A = min(4 * ia * fa, 1) */
            r128_x64_movaps_rr(e, 14, 13);
            r128_x64_mulps(e, 14, 1);
            r128_x64_addps(e, 14, 14);
            r128_x64_addps(e, 14, 14);
            r128_x64_sse_rip(e, 0, 0x5D, 14, R128_X64_CP_ONEF);
            r128_x64_insertps(e, 12, 14, 3, 3);
            break;
        case 6: /* add: A = min(ia + fa, 1) */
            r128_x64_movaps_rr(e, 14, 13);
            r128_x64_addps(e, 14, 1);
            r128_x64_sse_rip(e, 0, 0x5D, 14, R128_X64_CP_ONEF);
            r128_x64_insertps(e, 12, 14, 3, 3);
            break;
        case 7: /* add signed: A = clamp(ia + fa - 0.5, 0, 1) */
            r128_x64_movaps_rr(e, 14, 13);
            r128_x64_addps(e, 14, 1);
            r128_x64_sse_rip(e, 0, 0x5C, 14, R128_X64_CP_HALFF);
            r128_x64_sse_rip(e, 0, 0x5F, 14, R128_X64_CP_ZERO);
            r128_x64_sse_rip(e, 0, 0x5D, 14, R128_X64_CP_ONEF);
            r128_x64_insertps(e, 12, 14, 3, 3);
            break;
        case 14: /* add signed x2: A = clamp(2*(ia + fa - 0.5), 0, 1) */
            r128_x64_movaps_rr(e, 14, 13);
            r128_x64_addps(e, 14, 1);
            r128_x64_sse_rip(e, 0, 0x5C, 14, R128_X64_CP_HALFF);
            r128_x64_addps(e, 14, 14);
            r128_x64_sse_rip(e, 0, 0x5F, 14, R128_X64_CP_ZERO);
            r128_x64_sse_rip(e, 0, 0x5D, 14, R128_X64_CP_ONEF);
            r128_x64_insertps(e, 12, 14, 3, 3);
            break;
        case 3:  /* modulate: A = ia * fa */
        default: /* unlisted codes 8-13 and 15: modulate, as in the
                    interpreter */
            r128_x64_movaps_rr(e, 14, 13);
            r128_x64_mulps(e, 14, 1);
            r128_x64_insertps(e, 12, 14, 3, 3);
            break;
    }
    r128_x64_movaps_rr(e, 11, 12);
}

static void
r128_x64_emit_texstage(r128_x64_emit_t *e, const rage128_draw_state_t *ds, int st,
                       int *skips, int *nskip)
{
    r128_x64_tex_t t;

    r128_x64_tex_setup(&t, e, ds, st, skips, nskip);
    r128_x64_tex_coords(&t);
    r128_x64_tex_sample(&t);
    if (t.want_near)
        r128_x64_tex_ck(&t);
    r128_x64_tex_unpack(&t);
    r128_x64_tex_operands(&t);
    r128_x64_tex_comb(&t);
    r128_x64_tex_comba(&t);
}

/* The whole inline texture block, in place of the rage128_texstage_run
   call: rhw and ir once, the color at block entry saved for the
   combine, each enabled stage, then the texture-lighting pass.
   Chroma-key rejects go onto the caller's pixel-skip list. */
/* A stage's W head, into the frame words R128_X64_TS_RHW and
   R128_X64_TS_IR (ARM64 s15 and s14). With perspective,
   rhw = w0*a + w1*b + w2*c over the three floats at rh (arhw for the
   primary W, a2rhw for the secondary stage's own W) and
   ir = rhw != 0 ? 1/rhw : 1. An unordered compare sets both parity and
   zero, so jp selects the quotient for a NaN rhw before je keeps 1 for
   either signed zero, matching rhw != 0 in C. Without perspective
   ir = 1.0f, and rhw = 0.0f is stored to match the C, but only the
   perspective LOD reads it. In: rax = texctx, weights in xmm8-xmm10.
   Scratch xmm0, xmm1, xmm14, xmm15. */
static void
r128_x64_emit_w_head(r128_x64_emit_t *e, int persp, int rh)
{
    if (persp) {
        int b1, b_nan;

        r128_x64_movss_ld(e, 0, X64_RAX, rh);
        r128_x64_mulss(e, 0, 8);
        r128_x64_movss_ld(e, 1, X64_RAX, rh + 4);
        r128_x64_mulss(e, 1, 9);
        r128_x64_addss(e, 0, 1);
        r128_x64_movss_ld(e, 1, X64_RAX, rh + 8);
        r128_x64_mulss(e, 1, 10);
        r128_x64_addss(e, 0, 1);
        r128_x64_movss_st(e, 0, X64_RSP, R128_X64_TS_RHW);
        r128_x64_sse_rip(e, 0xF3, 0x10, 14, R128_X64_CP_ONEF);
        r128_x64_movss_rr(e, 1, 14);
        r128_x64_divss(e, 1, 0);
        r128_x64_xorps(e, 15, 15);
        r128_x64_comiss(e, 0, 15);
        b_nan = r128_x64_jcc(e, X64_CC_P);
        b1    = r128_x64_jcc(e, X64_CC_E);
        r128_x64_patch32(e, b_nan, r128_x64_here(e));
        r128_x64_movss_rr(e, 14, 1);
        r128_x64_patch32(e, b1, r128_x64_here(e));
        r128_x64_movss_st(e, 14, X64_RSP, R128_X64_TS_IR);
    } else {
        r128_x64_sse_rip(e, 0xF3, 0x10, 0, R128_X64_CP_ONEF);
        r128_x64_movss_st(e, 0, X64_RSP, R128_X64_TS_IR);
        r128_x64_xorps(e, 0, 0);
        r128_x64_movss_st(e, 0, X64_RSP, R128_X64_TS_RHW);
    }
}

/* The whole inline texture code for one pixel, in place of the
   rage128_texstage_run call: rhw and ir once, the color snapshot, each
   enabled stage, then the texture-lighting pass. A secondary stage with
   a W of its own (r128_jit_sec_w_own) gets a second head before it, as
   the interpreter forms rhw2 and ir2 then; the texctx pointer comes
   back from the frame because the stages use rax as scratch. Otherwise
   stage 1 reads the primary stage's frame words. */
static void
r128_x64_emit_texstage_inline(r128_x64_emit_t *e, const rage128_draw_state_t *ds,
                              int *skips, int *nskip)
{
    r128_x64_ld(e, 1, X64_RAX, X64_RBX, (int) offsetof(r128_jit_tri_t, texctx));
    r128_x64_st(e, 1, X64_RAX, X64_RSP, R128_X64_TS_TCTX);
    r128_x64_emit_w_head(e, ds->do_persp, (int) offsetof(r3d_texctx_t, arhw));
    /* Save the color at block entry (int_color). The interpolator color
       and alpha selects and the blend-vertex weight read it on stage 1
       too, where xmm11 already holds stage 0's output. */
    r128_x64_movaps_st(e, 11, X64_RSP, R128_X64_TS_INTC);
    if (ds->tex_en)
        r128_x64_emit_texstage(e, ds, 0, skips, nskip);
    if (ds->sec_en) {
        if (r128_jit_sec_w_own(ds)) {
            r128_x64_ld(e, 1, X64_RAX, X64_RSP, R128_X64_TS_TCTX);
            r128_x64_emit_w_head(e, r128_jit_stage_persp(ds, 1),
                                 (int) (ds->sel_w ? offsetof(r3d_texctx_t, a2rhw)
                                                  : offsetof(r3d_texctx_t, arhw)));
        }
        r128_x64_emit_texstage(e, ds, 1, skips, nskip);
    }
    /* Texture lighting (SDK: Texture Mapping, p. 6-46 / PDF 158,
       Table 6-15): one more combine, on ds->lcomb, whose texel is the
       output of the stages (xmm11 copied to xmm0) and whose input is
       int_color, the same pass the interpreter runs. */
    if (ds->light_on) {
        r128_x64_tex_t t = { .e = e, .ds = ds, .cd = &ds->lcomb };

        r128_x64_movaps_rr(e, 0, 11);
        r128_x64_tex_operands(&t);
        r128_x64_tex_comb(&t);
        r128_x64_tex_comba(&t);
    }
}

#endif /* VIDEO_ATI_RAGE128_CODEGEN_X86_64_TEX_H */

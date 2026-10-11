/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- x86-64 span JIT, texture stage of the SoA
 *          vector loop.
 *
 *          The vector loop shades four pixels per group, one per lane of
 *          an XMM register. For the textured states that
 *          r128_jit_x64_soa_tex_can admits, this file emits the group's
 *          texture work: the interpolated vertex color, the texture
 *          coordinates (multiplied by 1/rhw when the draw is
 *          perspective correct), the per-pixel LOD and mip level,
 *          coordinate quantization and wrap, the texel fetches, the
 *          bilinear and trilinear blends, the stage-0 chroma key, the
 *          texture combine and the color pack. With two stages
 *          (sec_en), stage 0 hands its float output to stage 1 as the
 *          previous color, as rage128_texstage_run does.
 *
 *          The coordinate, LOD, filter and combine arithmetic runs on all
 *          four lanes at once. The texel fetches and texel decode do
 *          not: SSE4.1 has no gather instruction, so each
 *          lane's texels are fetched one at a time by the scalar stage's
 *          fetch code (r128_x64_emit_texel), through coordinate slots in
 *          the stack frame.
 *
 *          Every lane must match the interpreter bit for bit. The code
 *          follows the ARM64 r128_a64_emit_soa_texstage_one step for
 *          step. Where x86 and ARM64 results can differ, it uses the
 *          same instructions as the scalar x86 texture stage
 *          (vid_ati_rage128_codegen_x86_64_tex.h) and so matches the
 *          interpreter as compiled for x86-64: roundps for nearbyintf and
 *          floorf, the saturating conversions of the texture coordinates
 *          and weights (the interpreter's r3d_f2i and r3d_f2u, built on
 *          cvttps2dq with the out-of-range lanes fixed up), and the
 *          LOD's maxps with the operand order that gives the C's result
 *          when an input is NaN. The combine clamps use minps and maxps against 1.0 and
 *          0.0 as the scalar stage does.
 *
 *          Registers: the barycentric weights in xmm8, xmm9 and xmm10
 *          are read. xmm8 and xmm10 always survive the stage. xmm9
 *          survives when the stage leaves float channels for the blend,
 *          specular or fog stage (the specular and fog stages read the
 *          weights again); otherwise it returns the packed color lanes.
 *          xmm0, xmm1 and xmm11 to xmm15 are scratch. Each texel fetch
 *          clobbers rax, rcx, rdx and r10 and returns the texel in r11d,
 *          so the caller saves r11, and r10 when it holds the Z cell,
 *          around the stage.
 *
 *          Frame: the slots of the textured SoA extension
 *          (R128_X64_SP_SOAT_C0 and the others above the base frame),
 *          plus the scalar stage's level words (R128_X64_TS_LW and the
 *          others) that the fetch code reads. The SRC channel slots
 *          share offsets with those level words, which is safe because
 *          the level words are written again for every group and the
 *          SRC slots only after the group's last fetch.
 *
 *          Included only by vid_ati_rage128_codegen_x86_64.h, after the
 *          scalar texture stage.
 *
 *          The emitted code uses instructions up to SSE4.1 (roundps,
 *          pminsd, pmaxsd, blendvps, pinsrd and packusdw in this file,
 *          pmulld in the shared lerp r128_x64_emit_lerp_w) and nothing
 *          newer: no AVX, FMA or BMI.
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
#ifndef VIDEO_ATI_RAGE128_CODEGEN_X86_64_SOA_TEX_H
#define VIDEO_ATI_RAGE128_CODEGEN_X86_64_SOA_TEX_H

/* Whether the vector loop handles texture stage st. The texel datatype
   must be one the inline stage decodes. A stage without per-pixel LOD
   samples its base level, nearest or bilinear. A stage with LOD is
   handled when the mip level is chosen per lane (r128_jit_minb_mip_on:
   mip-nearest, trilinear, and the undocumented codes 6 and 7, which
   the shared rules sample like code 2) or when the minification and
   magnification texel filters differ (r128_jit_minb_split), so that
   the LOD sign picks the filter per lane. Either kind may combine with
   the other. minb is PRIM_TEX_CNTL_C:PRIM_MIN_BLEND_FCN (SDK: Texture
   Mapping, p. 6-40 / PDF 152, Table 6-4). A dual pair is also weighed
   against the block size, below. */
static int
r128_x64_soa_stage_can(const rage128_draw_state_t *ds, int st)
{
    const r3d_stage_hdr_t *h       = &ds->sh[st];
    int                    has_lod = st ? ds->need_lod2 : ds->need_lod;

    if (has_lod) {
        int mip_on = r128_jit_minb_mip_on(h->minb, h->mipdis, has_lod);
        int split  = r128_jit_minb_split(h->minb, h->mipdis, h->mag, has_lod);

        if (!split && !mip_on)
            return 0;
    }
    return r128_jit_dt_inline_family(h->dt);
}

/* Block-size model for dual-stage blocks, which must fit the 16 KB
   block (R128_X64_BLOCK_SIZE). The numbers come from the sizes mode of
   the JIT host harness (tests/video/rage128/jit-harness), taking the worst
   case over the inline texel datatypes, all 16 pairs of s and t wrap
   modes and the heaviest optional stages (dither, blend, alpha test,
   partial write mask, 24-bit Z in a 32-bit cell, perspective, the
   largest combine ops).

   A stage weighs the bytes its SoA pipeline adds over a no-LOD nearest
   stage. The part every dual block shares (constant pool, prologue,
   dual head with two nearest stages, blend, alpha test, store and
   scalar tail) measured at most R128_X64_SOA_DUAL_BASE bytes, so the
   two stage weights must fit in 16384 - 10538 - 256 = 5590 bytes, the
   last term being a safety margin. The stage-0 chroma key adds a
   weight per class (ck below), the size difference between the same
   state with and without the key. Specular or vertex fog (693), aux
   scissors (470) and stencil (762) add to the shared part; they are
   same-state differences too and are added in
   r128_x64_soa_pair_weight_gated.

   A trilinear stage with a bilinear texel filter weighs 7143 or 7195
   bytes, more than the whole pair budget, so no pair holding one is
   admitted, and its chroma-key weight cannot be measured in an
   admitted block; the code uses 1637 for it. */
#define R128_X64_SOA_DUAL_BASE   10538
#define R128_X64_SOA_DUAL_MARGIN 256

static int
r128_x64_soa_stage_weight(const rage128_draw_state_t *ds, int st)
{
    const r3d_stage_hdr_t *h       = &ds->sh[st];
    int                    has_lod = st ? ds->need_lod2 : ds->need_lod;
    int                    w, ck;

    if (!has_lod) {
        int linear = !(h->minb == 0 && h->mag == 0);

        w  = linear ? 2612 : 0;
        ck = linear ? 1199 : 157;
    } else {
        int linear = r128_jit_minb_lin_min(h->minb, h->mipdis, has_lod);
        int mip_on = r128_jit_minb_mip_on(h->minb, h->mipdis, has_lod);
        int split  = r128_jit_minb_split(h->minb, h->mipdis, h->mag, has_lod);
        int tri    = r128_jit_minb_tri(h->minb, h->mipdis, has_lod);

        if (!mip_on) {
            w  = 4748; /* base-level split   */
            ck = 1204;
        } else if (tri) {
            w  = split ? (linear ? 7195 : 6278) : (linear ? 7143 : 2533);
            ck = linear ? 1637 : (split ? 354 : 162);
        } else if (split) {
            w  = linear ? 5355 : 5337; /* split, mip-nearest */
            ck = 1445;
        } else {
            w  = linear ? 5285 : 1665; /* mip-nearest        */
            ck = linear ? 1637 : 162;
        }
    }
    if (!st && ds->need_ck)
        w += ck;
    return w;
}

/* A single trilinear stage with a bilinear texel filter (minb 5) can
   overflow the 16 KB block once the stage-0 chroma key or stencil is
   added, as measured by the harness sizes mode in its single-stage
   sweep. Every such state keeps its scalar-loop block. */
static int
r128_x64_soa_single_gated(const rage128_draw_state_t *ds)
{
    const r3d_stage_hdr_t *h = &ds->sh[0];

    return (ds->need_ck || ds->sten_on)
        && r128_jit_minb_tri(h->minb, h->mipdis, ds->need_lod)
        && r128_jit_minb_lin_min(h->minb, h->mipdis, ds->need_lod);
}

/* True when a dual pair's two stage weights plus its shared-part
   additions exceed the stage-pair budget (R128_X64_BLOCK_SIZE minus
   R128_X64_SOA_DUAL_BASE and R128_X64_SOA_DUAL_MARGIN). Such a pair
   keeps its scalar-loop block, since the vector loop would overflow
   the 16 KB block. */
static int
r128_x64_soa_pair_weight_gated(const rage128_draw_state_t *ds)
{
    int w = r128_x64_soa_stage_weight(ds, 0) + r128_x64_soa_stage_weight(ds, 1);

    if (ds->spec_en || ds->fog_en)
        w += 693;
    if (ds->aux_on)
        w += 470;
    if (ds->sten_on)
        w += 762;
    return w > R128_X64_BLOCK_SIZE - R128_X64_SOA_DUAL_BASE - R128_X64_SOA_DUAL_MARGIN;
}

/* Gate for the textured vector loop. r128_x64_gen_setup calls it only
   for states that r128_jit_x64_can accepts and whose texture stages
   sample inline. Stage 0 must be on, and each stage must pass
   r128_x64_soa_stage_can. Refused: a chroma key on a palette texture,
   a single minb 5 trilinear stage with the key or stencil, a dual pair
   over the stage-pair budget, textured table fog and texture lighting.
   Alpha test, alpha blend, specular, vertex fog, aux scissors and
   stencil otherwise run in the vector loop, in the stages it shares
   with the untextured loop. */
static int
r128_jit_x64_soa_tex_can(const rage128_draw_state_t *ds)
{
    const r3d_stage_hdr_t *h = &ds->sh[0];

    if (ds->stip_en || !ds->tex_en)
        return 0;
    /* The textured vector tails pack only 565 or 8888; alpha-bearing
       16-bit targets keep scalar sampling, blending and packing. */
    if (ds->dst_dt == 3 || ds->dst_dt == 15)
        return 0;
    /* Chroma key on a CI4 or CI8 texture (dt 1, 2): the interpreter
       compares the raw palette index, but the vector gather keeps only
       the converted texels, so these draws keep the scalar loop. */
    if (ds->need_ck && (h->dt == 1 || h->dt == 2))
        return 0;
    if (!r128_x64_soa_stage_can(ds, 0))
        return 0;
    if (!ds->sec_en && r128_x64_soa_single_gated(ds))
        return 0;
    if (ds->sec_en) {
        if (!r128_x64_soa_stage_can(ds, 1))
            return 0;
        if (r128_x64_soa_pair_weight_gated(ds))
            return 0;
        /* The dual head computes one rhw and ir that both stages share.
           A secondary stage with a W of its own (its own perspective
           enable, or the vertex rhw2 under SEC_SRC_SEL_W) keeps the
           scalar loop, whose inline texture code emits a second head
           for it. */
        if (r128_jit_sec_w_own(ds))
            return 0;
    }
    if (ds->fog_en && ds->fog_table_en)
        return 0;
    /* Texture lighting is a third combine pass, which the vector code
       does not emit; lit draws keep the scalar loop, which runs it
       inline. */
    if (ds->light_on)
        return 0;
    return 1;
}

/* Texel filter of the stage's main pass, 1 for bilinear. With per-pixel
   LOD it is the minification filter; a split stage samples its
   magnified lanes again later with the magnification filter. Without
   LOD it is r3d_tex_sample's single-level rule: bilinear unless both
   filters are nearest. */
static int
r128_x64_soa_tex_linear(const r3d_stage_hdr_t *h, int has_lod)
{
    if (has_lod)
        return r128_jit_minb_lin_min(h->minb, h->mipdis, has_lod);
    return !(h->minb == 0 && h->mag == 0);
}

/* ---- additional instruction emitters ----
   Raw opcode bytes passed to r128_x64_sse_rr, r128_x64_sse_rm and
   r128_x64_sse_rip in this file (the prefix, then the byte after 0F):
     0x28 movaps    0x58 addps    0x59 mulps    0x5C subps
     0x5D minps     0x5E divps    0x5F maxps
     0x5B cvtdq2ps (no prefix: int32 lanes to float)
     66 0xDB pand   66 0xEB por   66 0xFE paddd
   r128_x64_sse38_rr with 0x14 is blendvps (66 0F 38 14), which takes
   its lane mask implicitly in xmm0. ---- */

/* pminsd and pmaxsd (66 0F 38 39 and 3D, SSE4.1): signed dword min and
   max. divps (0F 5E): packed single divide. */
#define r128_x64_pminsd(e, d, s) r128_x64_sse38_rr(e, 0x39, d, s)
#define r128_x64_pmaxsd(e, d, s) r128_x64_sse38_rr(e, 0x3D, d, s)
#define r128_x64_divps(e, d, s)  r128_x64_sse_rr(e, 0, 0x5E, d, s)

/* roundps xd, xs, imm8 (66 0F 3A 08 /r ib, SSE4.1), the packed form of
   the scalar stage's roundss. With bit 2 of the immediate clear the
   rounding mode comes from bits 1:0 rather than MXCSR, and bit 3
   suppresses the precision exception. Mode 8 rounds to nearest even
   (nearbyintf in the default rounding mode); mode 9 rounds toward
   minus infinity (floorf). */
static void
r128_x64_roundps(r128_x64_emit_t *e, int xd, int xs, int mode)
{
    r128_x64_e8(e, 0x66);
    r128_x64_rex(e, 0, xd, 0, xs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x3A);
    r128_x64_e8(e, 0x08);
    r128_x64_modrm_reg(e, xd, xs);
    r128_x64_e8(e, (uint8_t) mode);
}

/* r3d_tex_wrap on four lanes. mode is the stage's
   PRIM_TEXTURE_CLAMP_MODE_S or PRIM_TEXTURE_CLAMP_MODE_T value (SDK:
   Texture Mapping, p. 6-41 / PDF 153, Table 6-6), known when the block
   is generated. The signed coordinate lanes in xc are replaced by the
   wrapped ones; the dimension n, broadcast in xn, is preserved; xs1
   and xs2 are scratch. n is a power of two (a mip level size). Each
   case gives the value of the interpreter and of the scalar
   r128_x64_emit_wrap:

   WRAP    c & (n - 1).
   MIRROR  m = c & (2n - 1), then m < n ? m : m ^ (2n - 1). For n a
           power of two and n <= m < 2n, m ^ (2n - 1) equals the C's
           2n - 1 - m.
   CLAMP   pmaxsd with 0, then pminsd with n - 1.
   BORDER  a lane with c >= n compared as unsigned (which also catches
           c < 0) is ORed with all ones and becomes -1, the value that
           tells the fetch to use the border color. The unsigned compare
           flips bit 31 of both sides and uses the signed pcmpgtd. */
static void
r128_x64_soa_wrap_v(r128_x64_emit_t *e, uint32_t mode, int xc, int xn,
                    int xs1, int xs2)
{
    switch (mode & 3) {
        case 0:                            /* WRAP: c & (n-1) */
            r128_x64_pcmpeqd(e, xs1, xs1); /* -1 lanes           */
            r128_x64_paddd(e, xs1, xn);    /* n - 1              */
            r128_x64_pand(e, xc, xs1);
            break;
        case 1: /* MIRROR: m = c & (2n-1); m < n ? m : m ^ (2n-1) */
            r128_x64_pcmpeqd(e, xs2, xs2);
            r128_x64_movaps_rr(e, xs1, xn);
            r128_x64_paddd(e, xs1, xs1); /* 2n                 */
            r128_x64_paddd(e, xs1, xs2); /* 2n - 1             */
            r128_x64_pand(e, xc, xs1);   /* m                  */
            r128_x64_pxor(e, xs1, xc);   /* m ^ (2n-1)         */
            r128_x64_movaps_rr(e, xs2, xn);
            r128_x64_pcmpgtd(e, xs2, xc); /* n > m              */
            r128_x64_pand(e, xc, xs2);
            r128_x64_pandn(e, xs2, xs1);
            r128_x64_por(e, xc, xs2);
            break;
        case 3: /* BORDER: (unsigned) c >= n -> -1 */
            r128_x64_sse_rip(e, 0, 0x28, xs2, R128_X64_CP_BIAS31);
            r128_x64_movaps_rr(e, xs1, xc);
            r128_x64_pxor(e, xs1, xs2);    /* c ^ bias           */
            r128_x64_pxor(e, xs2, xn);     /* n ^ bias           */
            r128_x64_pcmpgtd(e, xs2, xs1); /* c < n (unsigned)   */
            r128_x64_pcmpeqd(e, xs1, xs1);
            r128_x64_pxor(e, xs2, xs1); /* c >= n             */
            r128_x64_por(e, xc, xs2);
            break;
        default: /* CLAMP: c<0 -> 0; c>=n -> n-1 */
            r128_x64_sse_rip(e, 0, 0x28, xs1, R128_X64_CP_ZERO);
            r128_x64_pmaxsd(e, xc, xs1);
            r128_x64_pcmpeqd(e, xs1, xs1);
            r128_x64_paddd(e, xs1, xn); /* n - 1              */
            r128_x64_pminsd(e, xc, xs1);
            break;
    }
}

/* Offset in r3d_texctx_t of stage st's sampler descriptor (sd0 or
   sd1). */
static int
r128_x64_soa_tex_sd_off(int st)
{
    return (int) (st ? offsetof(r3d_texctx_t, sd1) : offsetof(r3d_texctx_t, sd0));
}

/* Level words of the stage's largest level (slot[top] of its
   descriptor) into the frame words the texel fetch reads:
   R128_X64_TS_LW, R128_X64_TS_LH, R128_X64_TS_BASE, R128_X64_TS_MASK
   and R128_X64_TS_TEXP, plus R128_X64_TS_PAL for the palette
   datatypes. Used by the base-level classes, once per group because
   the SRC channel slots share those offsets, and again before a split
   stage's magnification pass when a per-lane pass has overwritten
   them. Out: r10d = lw, edx = lh. Scratch rax, rcx. */
static void
r128_x64_soa_tex_desc(r128_x64_emit_t *e, const rage128_draw_state_t *ds, int st)
{
    const r3d_stage_hdr_t *h      = &ds->sh[st];
    int                    sd_off = r128_x64_soa_tex_sd_off(st);
    int                    sl_off = sd_off + (int) offsetof(r3d_stage_desc_t, slot)
        + h->top * (int) sizeof(struct r3d_slot_desc_t);

    r128_x64_ld(e, 1, X64_RAX, X64_RBX, (int) offsetof(r128_jit_tri_t, texctx));
    r128_x64_ld(e, 0, X64_R10, X64_RAX, sl_off + 0); /* lw       */
    r128_x64_st(e, 0, X64_R10, X64_RSP, R128_X64_TS_LW);
    r128_x64_ld(e, 0, X64_RDX, X64_RAX, sl_off + 4); /* lh       */
    r128_x64_st(e, 0, X64_RDX, X64_RSP, R128_X64_TS_LH);
    r128_x64_ld(e, 0, X64_RCX, X64_RAX, sl_off + 16); /* base     */
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_BASE);
    r128_x64_ld(e, 0, X64_RCX, X64_RAX, sl_off + 20); /* mask     */
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_MASK);
    r128_x64_ld(e, 1, X64_RCX, X64_RAX, sl_off + 8); /* texbase  */
    r128_x64_st(e, 1, X64_RCX, X64_RSP, R128_X64_TS_TEXP);
    if (h->dt == 1 || h->dt == 2) {
        r128_x64_ld(e, 1, X64_RCX, X64_RAX,
                    sd_off + (int) offsetof(r3d_stage_desc_t, pal));
        r128_x64_st(e, 1, X64_RCX, X64_RSP, R128_X64_TS_PAL);
    }
}

/* Per-lane level classes store only the palette pointer once per
   group; each lane's level words are written ahead of its fetches by
   r128_x64_soa_tex_lane_desc. Scratch rax, rcx. */
static void
r128_x64_soa_tex_pal(r128_x64_emit_t *e, const rage128_draw_state_t *ds, int st)
{
    if (ds->sh[st].dt != 1 && ds->sh[st].dt != 2)
        return;
    r128_x64_ld(e, 1, X64_RAX, X64_RBX, (int) offsetof(r128_jit_tri_t, texctx));
    r128_x64_ld(e, 1, X64_RCX, X64_RAX,
                r128_x64_soa_tex_sd_off(st)
                    + (int) offsetof(r3d_stage_desc_t, pal));
    r128_x64_st(e, 1, X64_RCX, X64_RSP, R128_X64_TS_PAL);
}

/* rax = &texctx->sd<st>.slot[s], where s is lane k's mip slot from the
   R128_X64_SP_SOAT_SL vector. Scratch rcx. */
static void
r128_x64_soa_tex_lane_slot(r128_x64_emit_t *e, int st, int k)
{
    int sl_off = r128_x64_soa_tex_sd_off(st)
        + (int) offsetof(r3d_stage_desc_t, slot);

    r128_x64_ld(e, 1, X64_RAX, X64_RBX, (int) offsetof(r128_jit_tri_t, texctx));
    r128_x64_ld(e, 0, X64_RCX, X64_RSP, R128_X64_SP_SOAT_SL + 4 * k);
    r128_x64_imul_r_imm(e, X64_RCX, X64_RCX, (int) sizeof(struct r3d_slot_desc_t));
    r128_x64_lea_sib(e, X64_RAX, X64_RAX, X64_RCX, 0);
    if (sl_off)
        r128_x64_alu_r_imm(e, 0, 1, X64_RAX, sl_off);
}

/* Level words of lane k's mip slot into the fetch's frame words (lw,
   base, mask and texbase; the fetch does not read lh). All the lane's
   corner fetches use them. Scratch rax and rcx, which the fetch
   clobbers anyway. ARM64 counterpart: r128_a64_soa_lane_desc. */
static void
r128_x64_soa_tex_lane_desc(r128_x64_emit_t *e, int st, int k)
{
    r128_x64_soa_tex_lane_slot(e, st, k);
    r128_x64_ld(e, 0, X64_RCX, X64_RAX, 0); /* lw       */
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_LW);
    r128_x64_ld(e, 0, X64_RCX, X64_RAX, 16); /* base     */
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_BASE);
    r128_x64_ld(e, 0, X64_RCX, X64_RAX, 20); /* mask     */
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_TS_MASK);
    r128_x64_ld(e, 1, X64_RCX, X64_RAX, 8); /* texbase  */
    r128_x64_st(e, 1, X64_RCX, X64_RSP, R128_X64_TS_TEXP);
}

/* Per-lane level sizes for the slots in the SL vector: each lane's lw
   into R128_X64_SP_SOAT_VLW and its lh into R128_X64_SP_SOAT_VLH, read
   by r128_x64_soa_tex_axis. Scratch rax, rcx, xmm0, xmm15. ARM64
   counterpart: r128_a64_emit_soa_dims. */
static void
r128_x64_soa_tex_dims(r128_x64_emit_t *e, int st)
{
    for (int k = 0; k < 4; k++) {
        r128_x64_soa_tex_lane_slot(e, st, k);
        r128_x64_ld(e, 0, X64_RCX, X64_RAX, 0); /* lw       */
        if (k)
            r128_x64_pinsrd(e, 15, X64_RCX, k);
        else
            r128_x64_movd_x_r(e, 15, X64_RCX);
        r128_x64_ld(e, 0, X64_RCX, X64_RAX, 4); /* lh       */
        if (k)
            r128_x64_pinsrd(e, 0, X64_RCX, k);
        else
            r128_x64_movd_x_r(e, 0, X64_RCX);
    }
    r128_x64_movaps_st(e, 15, X64_RSP, R128_X64_SP_SOAT_VLW);
    r128_x64_movaps_st(e, 0, X64_RSP, R128_X64_SP_SOAT_VLH);
}

/* Load the float at texctx (rax) + off and broadcast it to the four
   lanes of xd. */
static void
r128_x64_soa_tex_gsplat(r128_x64_emit_t *e, int xd, int off)
{
    r128_x64_movss_ld(e, xd, X64_RAX, off);
    r128_x64_pshufd(e, xd, xd, 0x00);
}

/* Stage-0 chroma key on four lanes. xmm11 holds each lane's nearest
   texel before filtering, border color included: the value the
   interpreter's sampler returns in tnear. The draw state has converted
   each key and mask to the ARGB8888 texel layout (r3d_ck_to_argb), and
   each compare is made under its mask. Two keys:

   - ck3d: CLR_CMP_FCN_3D code 3 compares texel equal to CLR_CMP_CLR_3D,
     code 2 not equal (RRG: MISC_3D_STATE_CNTL_REG, p. 3-260 / PDF 278).
     The interpreter discards the pixel when the compare is true. The
     draw state turns code 1 into code 3 with key and mask 0, so only
     codes 2 and 3 reach here.
   - ckc: the texture chroma key (xf86-video-r128 macro
     R128_TEX_CHROMA_KEY_ENABLE), which discards on equal.

   The lanes that pass are ANDed into the lane cover mask at
   R128_X64_SP_KMASK, so a discarded lane neither stores nor widens the
   written range (rx0/rx1). Scratch xmm0, xmm1, xmm15, rcx. ARM64
   counterpart: r128_a64_soa_ck_mask. */
static void
r128_x64_soa_tex_ck(r128_x64_emit_t *e, const rage128_draw_state_t *ds)
{
    int have = 0;

    if (ds->ck3d_on) {
        r128_x64_splat_imm(e, 0, ds->ck3d_msk, X64_RCX);
        r128_x64_pand(e, 0, 11);
        r128_x64_splat_imm(e, 1, ds->ck3d_clr & ds->ck3d_msk, X64_RCX);
        r128_x64_pcmpeqd(e, 0, 1); /* eq           */
        if (ds->ckfn == 3) {       /* reject on eq */
            r128_x64_pcmpeqd(e, 1, 1);
            r128_x64_pxor(e, 0, 1);
        }
        have = 1;
    }
    if (ds->ckc_on) {
        r128_x64_splat_imm(e, 15, ds->ckc_msk, X64_RCX);
        r128_x64_pand(e, 15, 11);
        r128_x64_splat_imm(e, 1, ds->ckc_clr & ds->ckc_msk, X64_RCX);
        r128_x64_pcmpeqd(e, 15, 1);
        r128_x64_pcmpeqd(e, 1, 1);
        r128_x64_pxor(e, 15, 1); /* eq-reject    */
        if (have)
            r128_x64_pand(e, 0, 15);
        else
            r128_x64_movaps_rr(e, 0, 15);
    }
    r128_x64_movmskps(e, X64_RCX, 0);
    r128_x64_alu_mem_r(e, 0x21, 0, X64_RCX, X64_RSP, R128_X64_SP_KMASK);
}

/* Per-pixel LOD on four lanes, the need_lod block of
   rage128_texstage_run. With the perspective-corrected screen
   gradients (stage 1 takes the second coordinate set's when sec_sel,
   SEC_TEX_CNTL_C:SEC_SRC_SEL_ST, selects it):
     rho2 = max over x and y of (ds * texw)^2 + (dt * texh)^2
     lod  = rho2 > 0 ? 0.5 * r3d_log2f_fast(rho2) + lod_bias : -1000
   In: sp in xmm12 and tp in xmm14 (kept; xmm13 is not touched), and
   the rhw lanes at R128_X64_SP_SOAT_RHW when the draw is perspective
   correct. Out: lod in xmm11. Scratch xmm0, xmm1, xmm15, rax, and the
   U0 and U1 slots, which hold no coordinates yet.

   The arithmetic is the scalar r128_x64_emit_lod's, in the same order;
   its branches become lane masks. maxps returns its second operand
   when the operands are equal or either is NaN, so maxps(ax2, ay2)
   gives the C's ax2 > ay2 ? ax2 : ay2. cmpltps is false on NaN, as the
   C's rho2 > 0 test is. */
static void
r128_x64_soa_tex_lod(r128_x64_emit_t *e, const rage128_draw_state_t *ds, int st)
{
    int sel   = st && ds->sec_sel;
    int gs[2] = { (int) (sel ? offsetof(r3d_texctx_t, dS2dx) : offsetof(r3d_texctx_t, dSdx)),
                  (int) (sel ? offsetof(r3d_texctx_t, dS2dy) : offsetof(r3d_texctx_t, dSdy)) };
    int gt[2] = { (int) (sel ? offsetof(r3d_texctx_t, dT2dx) : offsetof(r3d_texctx_t, dTdx)),
                  (int) (sel ? offsetof(r3d_texctx_t, dT2dy) : offsetof(r3d_texctx_t, dTdy)) };
    int gw[2] = { (int) offsetof(r3d_texctx_t, dWdx),
                  (int) offsetof(r3d_texctx_t, dWdy) };
    int lodv  = e->cp_lodv[st];
    int tw    = lodv + 0;
    int th    = lodv + 16;
    int axis;

    r128_x64_ld(e, 1, X64_RAX, X64_RBX, (int) offsetof(r128_jit_tri_t, texctx));
    if (ds->do_persp) {
        /* iw2 = wp != 0 ? 1 / (wp*wp) : 0 -> U0; wp stays in xmm15 */
        r128_x64_movaps_ld(e, 15, X64_RSP, R128_X64_SP_SOAT_RHW);
        r128_x64_movaps_rr(e, 0, 15);
        r128_x64_mulps(e, 0, 0);
        r128_x64_sse_rip(e, 0, 0x28, 1, R128_X64_CP_ONEF);
        r128_x64_divps(e, 1, 0);
        r128_x64_xorps(e, 0, 0);
        r128_x64_cmpp_imm(e, 0, 0, 15, 0); /* wp == 0      */
        r128_x64_pandn(e, 0, 1);
        r128_x64_movaps_st(e, 0, X64_RSP, R128_X64_SP_SOAT_U0);
    }
    for (axis = 0; axis < 2; axis++) {
        if (ds->do_persp) {
            /* ds = (dS*wp - sp*dW) * iw2 -> xmm1; dt likewise -> xmm11 */
            r128_x64_soa_tex_gsplat(e, 1, gs[axis]);
            r128_x64_mulps(e, 1, 15);
            r128_x64_soa_tex_gsplat(e, 0, gw[axis]);
            r128_x64_movaps_rr(e, 11, 0);
            r128_x64_mulps(e, 11, 12);
            r128_x64_subps(e, 1, 11);
            r128_x64_sse_rm(e, 0, 0x59, 1, X64_RSP, R128_X64_SP_SOAT_U0);
            r128_x64_mulps(e, 0, 14);
            r128_x64_soa_tex_gsplat(e, 11, gt[axis]);
            r128_x64_mulps(e, 11, 15);
            r128_x64_subps(e, 11, 0);
            r128_x64_sse_rm(e, 0, 0x59, 11, X64_RSP, R128_X64_SP_SOAT_U0);
        } else {
            r128_x64_soa_tex_gsplat(e, 1, gs[axis]);
            r128_x64_soa_tex_gsplat(e, 11, gt[axis]);
        }
        r128_x64_sse_rip(e, 0, 0x59, 1, tw);  /* * texw   */
        r128_x64_sse_rip(e, 0, 0x59, 11, th); /* * texh   */
        r128_x64_mulps(e, 1, 1);
        r128_x64_mulps(e, 11, 11);
        r128_x64_addps(e, 1, 11); /* ax2, ay2 */
        if (!axis)
            r128_x64_movaps_st(e, 1, X64_RSP, R128_X64_SP_SOAT_U1);
    }
    r128_x64_movaps_ld(e, 0, X64_RSP, R128_X64_SP_SOAT_U1);
    r128_x64_sse_rr(e, 0, 0x5F, 0, 1); /* maxps: rho2 */
    /* r3d_log2f_fast on every lane: exponent from bits 30:23 minus 127,
       mantissa forced into [1, 2) by keeping bits 22:0 and ORing in the
       bits of 1.0f, then the cubic. Lanes with rho2 <= 0 take -1000
       below. */
    r128_x64_xorps(e, 15, 15);
    r128_x64_cmpltps(e, 15, 0); /* rho2 > 0 */
    r128_x64_movaps_st(e, 15, X64_RSP, R128_X64_SP_SOAT_U0);
    r128_x64_movaps_rr(e, 1, 0);
    r128_x64_psrld(e, 1, 23);
    r128_x64_sse_rip(e, 0x66, 0xDB, 1, R128_X64_CP_255I);
    r128_x64_cvtdq2ps(e, 1, 1);
    r128_x64_sse_rip(e, 0, 0x5C, 1, R128_X64_CP_127F); /* e        */
    r128_x64_sse_rip(e, 0x66, 0xDB, 0, R128_X64_CP_MANT);
    r128_x64_sse_rip(e, 0x66, 0xEB, 0, R128_X64_CP_ONEF); /* m        */
    r128_x64_movaps_rr(e, 11, 0);
    r128_x64_sse_rip(e, 0, 0x59, 11, R128_X64_CP_POLYV + 48);
    r128_x64_sse_rip(e, 0, 0x58, 11, R128_X64_CP_POLYV + 32);
    r128_x64_mulps(e, 11, 0);
    r128_x64_sse_rip(e, 0, 0x58, 11, R128_X64_CP_POLYV + 16);
    r128_x64_mulps(e, 11, 0);
    r128_x64_sse_rip(e, 0, 0x58, 11, R128_X64_CP_POLYV + 0);
    r128_x64_addps(e, 1, 11); /* log2     */
    r128_x64_sse_rip(e, 0, 0x59, 1, R128_X64_CP_HALFF);
    r128_x64_sse_rip(e, 0, 0x58, 1, lodv + 32); /* + bias   */
    r128_x64_movaps_ld(e, 15, X64_RSP, R128_X64_SP_SOAT_U0);
    r128_x64_sse_rip(e, 0, 0x28, 0, lodv + 48); /* -1000    */
    r128_x64_pand(e, 1, 15);
    r128_x64_pandn(e, 15, 0);
    r128_x64_por(e, 1, 15);
    r128_x64_movaps_rr(e, 11, 1);
}

/* Per-lane mip slot for the mip-nearest classes (minb 2 and 3, and the
   undocumented 6 and 7), as r3d_tex_sample picks it: a lane with
   lod <= 0 samples the largest level, slot top; a lane with lod > 0
   samples slot top - (int) (lvl + 0.5) with lvl = min(lod, top).
   cvttps2dq truncates like r3d_f2i: a lane that keeps the result has
   0 < lvl <= top, inside int range. In: lod in xmm11, which
   is not needed afterwards. Out: slot indices at R128_X64_SP_SOAT_SL.
   Scratch xmm0, xmm1, xmm15. ARM64 counterpart:
   r128_a64_emit_soa_slots. */
static void
r128_x64_soa_tex_slots(r128_x64_emit_t *e, int st)
{
    int lodv = e->cp_lodv[st];

    r128_x64_sse_rip(e, 0, 0x28, 0, lodv + 64); /* (float)top */
    r128_x64_movaps_rr(e, 15, 0);
    r128_x64_cmpltps(e, 15, 11); /* top < lod  */
    r128_x64_movaps_rr(e, 1, 0);
    r128_x64_pand(e, 1, 15);
    r128_x64_pandn(e, 15, 11);
    r128_x64_por(e, 1, 15); /* lvl        */
    r128_x64_sse_rip(e, 0, 0x58, 1, R128_X64_CP_HALFF);
    r128_x64_cvttps2dq(e, 1, 1);                /* l          */
    r128_x64_sse_rip(e, 0, 0x28, 0, lodv + 80); /* top        */
    r128_x64_psubd(e, 0, 1);                    /* top - l    */
    r128_x64_xorps(e, 15, 15);
    r128_x64_cmpltps(e, 15, 11); /* minify     */
    r128_x64_sse_rip(e, 0, 0x28, 1, lodv + 80);
    r128_x64_pand(e, 0, 15);
    r128_x64_pandn(e, 15, 1);
    r128_x64_por(e, 0, 15);
    r128_x64_movaps_st(e, 0, X64_RSP, R128_X64_SP_SOAT_SL);
}

/* Per-lane slot pair and 8.8 level weight for trilinear (minb 4 and 5),
   the mip_linear branch of r3d_tex_sample: lvl = min(lod, top),
   l0 = floor(lvl), f = lvl - l0, slotA = top - l0,
   slotB = max(slotA - 1, 0), and the weight
   w = (u32) (f * 256 + 0.5) that r3d_lerp_argb applies.

   Magnified lanes (lod <= 0) get slotA = slotB = top. The packed lerp
   of two equal texels returns that texel for any weight from 0 to 256,
   and f < 1 keeps w in that range, so these lanes come out as one
   sample of the largest level, as in the C. (They use the minification
   texel filter; when the magnification filter differs, the split pass
   resamples them.) The minify mask is used by two selects; blendvps
   takes its mask implicitly in xmm0, so the mask stays there for
   both. In: lod in xmm11 (overwritten). Out: slotA
   at R128_X64_SP_SOAT_SL, slotB at R128_X64_SP_SOAT_SLB, w at
   R128_X64_SP_SOAT_WM. Scratch xmm0, xmm1, xmm15. ARM64 counterpart:
   r128_a64_emit_soa_slots_tri. */
static void
r128_x64_soa_tex_slots_tri(r128_x64_emit_t *e, int st)
{
    int lodv = e->cp_lodv[st];

    r128_x64_sse_rip(e, 0, 0x28, 0, lodv + 64); /* (float)top */
    r128_x64_movaps_rr(e, 15, 0);
    r128_x64_cmpltps(e, 15, 11); /* top < lod  */
    r128_x64_movaps_rr(e, 1, 0);
    r128_x64_pand(e, 1, 15);
    r128_x64_pandn(e, 15, 11);
    r128_x64_por(e, 1, 15);                             /* lvl        */
    r128_x64_roundps(e, 0, 1, 9);                       /* floor(lvl) */
    r128_x64_subps(e, 1, 0);                            /* f          */
    r128_x64_cvttps2dq(e, 0, 0);                        /* l0         */
    r128_x64_sse_rip(e, 0, 0x5B, 15, R128_X64_CP_256I); /* cvtdq2ps: 256.0f */
    r128_x64_mulps(e, 1, 15);
    r128_x64_sse_rip(e, 0, 0x58, 1, R128_X64_CP_HALFF);
    r128_x64_cvttps2dq(e, 1, 1); /* w          */
    r128_x64_movaps_st(e, 1, X64_RSP, R128_X64_SP_SOAT_WM);
    r128_x64_sse_rip(e, 0, 0x28, 1, lodv + 80); /* top        */
    r128_x64_psubd(e, 1, 0);                    /* A = top-l0 */
    r128_x64_xorps(e, 0, 0);
    r128_x64_cmpltps(e, 0, 11);                  /* minify     */
    r128_x64_sse_rip(e, 0, 0x28, 15, lodv + 80); /* top        */
    r128_x64_movaps_rr(e, 11, 15);
    r128_x64_sse38_rr(e, 0x14, 11, 1); /* blendvps   */
    r128_x64_movaps_st(e, 11, X64_RSP, R128_X64_SP_SOAT_SL);
    r128_x64_pcmpeqd(e, 11, 11);
    r128_x64_paddd(e, 1, 11); /* A - 1      */
    r128_x64_sse_rip(e, 0, 0x28, 11, R128_X64_CP_ZERO);
    r128_x64_pmaxsd(e, 1, 11);         /* B          */
    r128_x64_sse38_rr(e, 0x14, 15, 1); /* blendvps   */
    r128_x64_movaps_st(e, 15, X64_RSP, R128_X64_SP_SOAT_SLB);
}

/* The SoA forms of r3d_f2i and r3d_f2u for r128_x64_soa_tex_axis, as
   subroutines emitted once per block (jumped over in line) and reached
   by CALL, like the scalar ones. They work on the registers every
   caller of r128_x64_soa_tex_axis passes: xmm11 (xs1), xmm15 (xs2) and
   xmm13 (xs3), with xmm0 as scratch.

   f2i: xmm15 = r3d_f2i of each lane of xmm11 (read only). cvttps2dq
   returns 0x80000000 for NaN and for anything out of range, which is
   already the answer below -2^31. A sentinel lane with an ordered
   xmm11 > 0 flips to 0x7fffffff by xor with its all-ones mask, and the
   ordered mask clears the NaN lanes. Clobbers xmm0 and xmm13. */
static int
r128_x64_soa_cvt_i32_sub(r128_x64_emit_t *e)
{
    if (!e->cvt_i32v_sub) {
        int b_over = r128_x64_jmp(e);

        e->cvt_i32v_sub = r128_x64_here(e);
        r128_x64_cvttps2dq(e, 15, 11);
        r128_x64_sse_rip(e, 0, 0x28, 0, R128_X64_CP_BIAS31);
        r128_x64_pcmpeqd(e, 0, 15); /* sentinel   */
        r128_x64_xorps(e, 13, 13);
        r128_x64_cmpltps(e, 13, 11); /* 0 < x      */
        r128_x64_pand(e, 0, 13);
        r128_x64_pxor(e, 15, 0);
        r128_x64_movaps_rr(e, 13, 11);
        r128_x64_cmpp_imm(e, 0, 13, 11, 7); /* ordered    */
        r128_x64_pand(e, 15, 13);
        r128_x64_ret(e);
        r128_x64_patch32(e, b_over, r128_x64_here(e));
    }
    return e->cvt_i32v_sub;
}

/* f2u: xmm11 = r3d_f2u of each lane of xmm11: 0 unless the lane is
   > 0, 0xffffffff from 2^32 up. Below 2^31 cvttps2dq is exact (the low
   form). From 2^31 to 2^32 every float is an even integer, so the lane
   converts x * 0.5 (exact, below 2^31) and shifts it back up (the high
   form); from 2^32 up that half conversion is itself the sentinel
   0x80000000, whose all-ones compare mask ORed in saturates the lane.
   Both forms are first masked by the ordered x > 0 test, which clears
   negative and NaN lanes and leaves only lanes of 2^31 and up with the
   low form's sentinel, so that sentinel then selects the high form.
   Clobbers xmm0 and xmm13. */
static int
r128_x64_soa_cvt_u32_sub(r128_x64_emit_t *e)
{
    if (!e->cvt_u32v_sub) {
        int b_over = r128_x64_jmp(e);

        e->cvt_u32v_sub = r128_x64_here(e);
        r128_x64_movaps_rr(e, 0, 11);
        r128_x64_sse_rip(e, 0, 0x59, 0, R128_X64_CP_HALFF);
        r128_x64_cvttps2dq(e, 0, 0);
        r128_x64_sse_rip(e, 0, 0x28, 13, R128_X64_CP_BIAS31);
        r128_x64_pcmpeqd(e, 13, 0); /* x >= 2^32  */
        r128_x64_pslld(e, 0, 1);
        r128_x64_por(e, 0, 13); /* high form  */
        r128_x64_xorps(e, 13, 13);
        r128_x64_cmpltps(e, 13, 11);   /* 0 < x      */
        r128_x64_cvttps2dq(e, 11, 11); /* low form   */
        r128_x64_pand(e, 11, 13);
        r128_x64_pand(e, 0, 13);
        r128_x64_sse_rip(e, 0, 0x28, 13, R128_X64_CP_BIAS31);
        r128_x64_pcmpeqd(e, 13, 11); /* x >= 2^31  */
        r128_x64_pand(e, 0, 13);
        r128_x64_pandn(e, 13, 11);
        r128_x64_por(e, 0, 13);
        r128_x64_movaps_rr(e, 11, 0);
        r128_x64_ret(e);
        r128_x64_patch32(e, b_over, r128_x64_here(e));
    }
    return e->cvt_u32v_sub;
}

/* One texture coordinate axis (axis 0: s and lw, axis 1: t and lh),
   the coordinate step of r3d_tex_level on four lanes.
   fx = r3d_texcoord_fx(coord * (float) dim): multiply by 4096, round to
   nearest even, multiply by 2^-12.
   Nearest: c0 = wrap((int) floorf(fx)) into the U0 or V0 slot.
   Bilinear: fu = fx - 0.5, u0 = (int) floorf(fu); wrap(u0) into U0 or
   V0, wrap(u0 + 1) into U1 or V1, and the 8.8 weight
   (u32) ((fu - (float) u0) * 256 + 0.5) into WU or WV.

   In: coordinate lanes in xc (overwritten). The dimension comes from
   GPR rdim (a zero-extended 32-bit value, only read), or with perlane
   set from the per-lane level sizes in the VLW or VLH slot. Scratch
   xmm0, xs1, xs2, xs3. The conversion subroutines above fix xs1, xs2
   and xs3 to xmm11, xmm15 and xmm13, which is what every caller
   passes.

   The operations are those of the scalar r128_x64_emit_tex_level. The
   fraction subtracts the floor converted to int and back, as the C
   subtracts (float) u0, so a saturated u0 gives the same fraction in
   both. The coordinate and the weight go through the saturating
   conversions below, as r3d_f2i and r3d_f2u do in the C. */
static void
r128_x64_soa_tex_axis(r128_x64_emit_t *e, const r3d_stage_hdr_t *h,
                      int axis, int linear, int perlane, int xc, int rdim,
                      int xs1, int xs2, int xs3)
{
    uint32_t mode = axis ? h->clamp_t : h->clamp_s;
    int      c0o  = axis ? R128_X64_SP_SOAT_V0 : R128_X64_SP_SOAT_U0;
    int      c1o  = axis ? R128_X64_SP_SOAT_V1 : R128_X64_SP_SOAT_U1;
    int      wo   = axis ? R128_X64_SP_SOAT_WV : R128_X64_SP_SOAT_WU;
    int      vlo  = axis ? R128_X64_SP_SOAT_VLH : R128_X64_SP_SOAT_VLW;

    if (perlane) {
        r128_x64_movaps_ld(e, 0, X64_RSP, vlo);
        r128_x64_cvtdq2ps(e, 0, 0); /* (float)dim lanes  */
    } else {
        r128_x64_cvtsi2ss_r64(e, 0, rdim);
        r128_x64_pshufd(e, 0, 0, 0x00); /* (float)dim splat  */
    }
    r128_x64_mulps(e, xc, 0);
    r128_x64_sse_rip(e, 0, 0x59, xc, e->cp_tfx); /* * 4096       */
    r128_x64_roundps(e, xc, xc, 8);
    r128_x64_sse_rip(e, 0, 0x59, xc, e->cp_tfx + 16); /* * 2^-12: fx  */
    if (!linear) {
        r128_x64_roundps(e, xs1, xc, 9);
        r128_x64_call_to(e, r128_x64_soa_cvt_i32_sub(e)); /* xs2       */
        r128_x64_movaps_rr(e, xc, xs2);
        if (perlane)
            r128_x64_movaps_ld(e, xs1, X64_RSP, vlo);
        else {
            r128_x64_movd_x_r(e, xs1, rdim);
            r128_x64_pshufd(e, xs1, xs1, 0x00); /* dim int splat     */
        }
        r128_x64_soa_wrap_v(e, mode, xc, xs1, 0, xs2);
        r128_x64_movaps_st(e, xc, X64_RSP, c0o);
        return;
    }
    r128_x64_sse_rip(e, 0, 0x5C, xc, R128_X64_CP_HALFF); /* fu          */
    r128_x64_roundps(e, xs1, xc, 9);
    r128_x64_call_to(e, r128_x64_soa_cvt_i32_sub(e)); /* raw c0 in xs2    */
    r128_x64_cvtdq2ps(e, xs1, xs2);
    r128_x64_subps(e, xc, xs1);                        /* frac              */
    r128_x64_sse_rip(e, 0, 0x5B, 0, R128_X64_CP_256I); /* cvtdq2ps: 256.0f */
    r128_x64_mulps(e, xc, 0);
    r128_x64_sse_rip(e, 0, 0x58, xc, R128_X64_CP_HALFF);
    r128_x64_movaps_rr(e, xs1, xc);
    r128_x64_call_to(e, r128_x64_soa_cvt_u32_sub(e)); /* w in xs1         */
    r128_x64_movaps_st(e, xs1, X64_RSP, wo);
    if (perlane)
        r128_x64_movaps_ld(e, xs1, X64_RSP, vlo);
    else {
        r128_x64_movd_x_r(e, xs1, rdim);
        r128_x64_pshufd(e, xs1, xs1, 0x00); /* dim int splat     */
    }
    r128_x64_movaps_rr(e, xc, xs2);
    r128_x64_soa_wrap_v(e, mode, xc, xs1, 0, xs3);
    r128_x64_movaps_st(e, xc, X64_RSP, c0o);
    /* The clamp base wrap leaves each lane's n - 1 in xmm0. Capping
       the raw successor preserves the upper edge at saturation. */
    if ((mode & 3) == 2)
        r128_x64_pminsd(e, xs2, 0);
    r128_x64_pcmpeqd(e, 0, 0);
    r128_x64_psubd(e, xs2, 0); /* raw c0 + 1        */
    r128_x64_soa_wrap_v(e, mode, xs2, xs1, 0, xs3);
    r128_x64_movaps_st(e, xs2, X64_RSP, c1o);
}

/* Four texel fetches, one per lane, through one (u slot, v slot) pair
   of the frame coordinate slots; the texels are assembled in xd (movd
   for lane 0, pinsrd for lanes 1 to 3). The order of the fetches does
   not matter: a fetch only reads texture memory, and a draw whose
   texture overlaps the rows it writes keeps the scalar loop
   (soa_selftex in r128_x64_gen_setup). */
static void
r128_x64_soa_tex_fetch4(r128_x64_emit_t *e, const r3d_stage_hdr_t *h, int st,
                        int xd, int uo, int vo)
{
    for (int k = 0; k < 4; k++) {
        r128_x64_emit_texel(e, h, uo + 4 * k, vo + 4 * k, e->tex_sub[st]);
        if (k)
            r128_x64_pinsrd(e, xd, X64_R11, k);
        else
            r128_x64_movd_x_r(e, xd, X64_R11);
    }
}

/* r3d_lerp_packed on four lanes: vx = lerp(vx, vy, w), with the
   per-lane 8.8 weights loaded from w_off into xmm14. Writes vx, xmm0,
   xmm1, xmm14 and xmm15; vy is only read. */
static void
r128_x64_soa_lerp(r128_x64_emit_t *e, int vx, int vy, int w_off)
{
    r128_x64_movaps_ld(e, 14, X64_RSP, w_off);
    r128_x64_emit_lerp_w(e, vx, vy);
}

/* The texel fetches through the frame coordinate slots and, for a
   bilinear filter, the four-wide lerps; the filtered texels end in
   xmm11. Nearest: the four c00 fetches. Bilinear: c00 and c10 lerped
   by wu, c01 and c11 lerped by wu, then those two lerped by wv, the
   order of r3d_tex_level. A fetch (r128_x64_emit_texel and the decode
   subroutines) uses only general registers, so xmm11 to xmm14 hold the
   gathered lanes across fetches; the lerps use xmm14 and xmm15.

   Per-lane level classes go lane by lane: a lane's level words are set
   up once and its corner fetches follow. The four corners land in
   xmm11 to xmm14. The first lerp loads its weights into xmm14, so c11
   waits in the U0 slot, free once every fetch is done, and comes back
   in xmm12. */
static void
r128_x64_soa_tex_gather(r128_x64_emit_t *e, const r3d_stage_hdr_t *h, int st,
                        int linear, int perlane)
{
    if (perlane) {
        static const int cx[4] = { 11, 12, 13, 14 };
        static const int cu[4] = { R128_X64_SP_SOAT_U0, R128_X64_SP_SOAT_U1,
                                   R128_X64_SP_SOAT_U0, R128_X64_SP_SOAT_U1 };
        static const int cv[4] = { R128_X64_SP_SOAT_V0, R128_X64_SP_SOAT_V0,
                                   R128_X64_SP_SOAT_V1, R128_X64_SP_SOAT_V1 };

        for (int k = 0; k < 4; k++) {
            r128_x64_soa_tex_lane_desc(e, st, k);
            for (int c = 0; c < (linear ? 4 : 1); c++) {
                r128_x64_emit_texel(e, h, cu[c] + 4 * k, cv[c] + 4 * k,
                                    e->tex_sub[st]);
                if (k)
                    r128_x64_pinsrd(e, cx[c], X64_R11, k);
                else
                    r128_x64_movd_x_r(e, cx[c], X64_R11);
            }
        }
        if (linear) {
            r128_x64_movaps_st(e, 14, X64_RSP, R128_X64_SP_SOAT_U0);
            r128_x64_soa_lerp(e, 11, 12, R128_X64_SP_SOAT_WU); /* c00/c10 */
            r128_x64_movaps_ld(e, 12, X64_RSP, R128_X64_SP_SOAT_U0);
            r128_x64_soa_lerp(e, 13, 12, R128_X64_SP_SOAT_WU); /* c01/c11 */
            r128_x64_soa_lerp(e, 11, 13, R128_X64_SP_SOAT_WV);
        }
        return;
    }
    r128_x64_soa_tex_fetch4(e, h, st, 11, R128_X64_SP_SOAT_U0, R128_X64_SP_SOAT_V0);
    if (!linear)
        return;
    r128_x64_soa_tex_fetch4(e, h, st, 12, R128_X64_SP_SOAT_U1, R128_X64_SP_SOAT_V0);
    r128_x64_soa_lerp(e, 11, 12, R128_X64_SP_SOAT_WU); /* c00/c10    */
    r128_x64_soa_tex_fetch4(e, h, st, 12, R128_X64_SP_SOAT_U0, R128_X64_SP_SOAT_V1);
    r128_x64_soa_tex_fetch4(e, h, st, 13, R128_X64_SP_SOAT_U1, R128_X64_SP_SOAT_V1);
    r128_x64_soa_lerp(e, 12, 13, R128_X64_SP_SOAT_WU); /* c01/c11    */
    r128_x64_soa_lerp(e, 11, 12, R128_X64_SP_SOAT_WV);
}

/* Coordinate and gather subroutine of a trilinear stage: the per-lane
   level sizes for the slots in SL, both axes per lane, and the
   per-lane gather with the stage's minification texel filter. Called
   with s in xmm12 and t in xmm14; returns the texels in xmm11. It is
   emitted once per stage ahead of the vector loop, and only the two
   level passes call it. Returns its entry offset.

   On entry it pops its return address into the SLR frame slot
   (8F /0, pop m64), and before ret it pushes it back (FF /6, push
   m64). With rsp as the base register, pop computes the address after
   incrementing rsp and push before decrementing it, so both reach the
   slot at the same offset the block body uses. The body therefore runs
   at the block's rsp: every frame offset is valid, and the decode
   subroutines it calls find one return address on the stack, as their
   R128_X64_SUBF bias assumes. It clobbers the same registers as the
   inline pipeline. */
static int
r128_x64_emit_soa_coord_sub(r128_x64_emit_t *e, const rage128_draw_state_t *ds,
                            int st)
{
    const r3d_stage_hdr_t *h       = &ds->sh[st];
    int                    has_lod = st ? ds->need_lod2 : ds->need_lod;
    int                    linear  = r128_jit_minb_lin_min(h->minb, h->mipdis, has_lod);
    int                    entry   = r128_x64_here(e);

    r128_x64_e8(e, 0x8F); /* pop m64  */
    r128_x64_modrm_mem(e, 0, X64_RSP, R128_X64_SP_SOAT_SLR);
    r128_x64_soa_tex_dims(e, st);
    r128_x64_soa_tex_axis(e, h, 0, linear, 1, 12, X64_R10, 11, 15, 13);
    r128_x64_soa_tex_axis(e, h, 1, linear, 1, 14, X64_RDX, 11, 15, 13);
    r128_x64_soa_tex_gather(e, h, st, linear, 1);
    r128_x64_e8(e, 0xFF); /* push m64 */
    r128_x64_modrm_mem(e, 6, X64_RSP, R128_X64_SP_SOAT_SLR);
    r128_x64_ret(e);
    return entry;
}

/* Per lane w0*a + w1*b + w2*c, for three consecutive floats a, b, c at
   [rax + off] (a coordinate or rhw triple of r3d_texctx_t), added left
   to right like the C. movups loads four floats; the fourth is unused.
   Result in xd; scratch xmm0, xmm1, xt. */
static void
r128_x64_soa_tex_dot3(r128_x64_emit_t *e, int xd, int xt, int off)
{
    r128_x64_movups_ld(e, 0, X64_RAX, off);
    r128_x64_movaps_rr(e, xd, 8);
    r128_x64_pshufd(e, 1, 0, 0x00);
    r128_x64_mulps(e, xd, 1);
    r128_x64_movaps_rr(e, xt, 9);
    r128_x64_pshufd(e, 1, 0, 0x55);
    r128_x64_mulps(e, xt, 1);
    r128_x64_addps(e, xd, xt);
    r128_x64_movaps_rr(e, xt, 10);
    r128_x64_pshufd(e, 1, 0, 0xAA);
    r128_x64_mulps(e, xt, 1);
    r128_x64_addps(e, xd, xt);
}

/* xmm13 = ir per lane, as rage128_texstage_run computes it: 1 / rhw, or
   1.0 where rhw == 0. The cmpeqps mask is false on NaN, so a NaN rhw
   takes 1 / rhw, as with the C's rhw != 0.0f test. When want_rhw is
   set (a LOD block needs it) the rhw dot is also stored at
   R128_X64_SP_SOAT_RHW. Without perspective ir is 1.0. In: rax =
   texctx. Scratch xmm0, xmm11, xmm12. */
static void
r128_x64_soa_tex_ir(r128_x64_emit_t *e, const rage128_draw_state_t *ds,
                    int want_rhw)
{
    if (ds->do_persp) {
        r128_x64_soa_tex_dot3(e, 12, 11, (int) offsetof(r3d_texctx_t, arhw));
        if (want_rhw)
            r128_x64_movaps_st(e, 12, X64_RSP, R128_X64_SP_SOAT_RHW);
        r128_x64_sse_rip(e, 0, 0x28, 13, R128_X64_CP_ONEF);
        r128_x64_movaps_rr(e, 11, 13);
        r128_x64_divps(e, 11, 12); /* 1 / rhw       */
        r128_x64_xorps(e, 0, 0);
        r128_x64_cmpp_imm(e, 0, 0, 12, 0); /* rhw == 0      */
        r128_x64_pand(e, 13, 0);           /* 1.0 & m       */
        r128_x64_pandn(e, 0, 11);          /* 1/rhw & ~m    */
        r128_x64_por(e, 13, 0);
    } else
        r128_x64_sse_rip(e, 0, 0x28, 13, R128_X64_CP_ONEF);
}

/* One texture stage of the group body. The previous-color operands
   read the slots at prev_base: the vertex color at C0 for stage 0, the
   stage-0 float output at P0 for stage 1. The interpolated-color
   operands always read C0. With out_float the stage stores its combine
   floats, alpha included, to P0 and stops there, leaving the pack, the
   SRC slots and the alpha test to stage 1; this is the interpreter's
   col[], which goes from stage 0 to stage 1 without conversion. With
   dual set, ir comes from the IR slot written by the dual head. Only
   stage 0 runs the chroma key. xmm8 and xmm10 survive the stage; xmm9
   survives when the stage leaves its channels in the SRC slots and
   otherwise returns the packed lanes. */
static void
r128_x64_emit_soa_texstage_one(r128_x64_emit_t *e, const rage128_draw_state_t *ds,
                               int st, int prev_base, int out_float, int dual)
{
    const r3d_stage_hdr_t *h       = &ds->sh[st];
    const r3d_comb_desc_t *cd      = &ds->comb[st];
    int                    dith_on = ds->dither && ds->dst_dt == 4;
    int                    at_on   = r128_x64_soa_atest_on(ds);
    int                    has_lod = st ? ds->need_lod2 : ds->need_lod;
    int                    mip_on  = r128_jit_minb_mip_on(h->minb, h->mipdis, has_lod);
    int                    split   = r128_jit_minb_split(h->minb, h->mipdis, h->mag, has_lod);
    int                    tri     = r128_jit_minb_tri(h->minb, h->mipdis, has_lod);
    int                    linear  = r128_x64_soa_tex_linear(h, has_lod);
    int                    ck      = ds->need_ck && !st;
    /* The chroma key compares the nearest texel at the lane's level,
       fetched without the bilinear -0.5 offset (r3d_tex_level's
       nearest). A nearest stage without a split takes it from the
       gathered c00, and a nearest trilinear stage from pass A's c00
       (pass A runs at SL, slot top on magnified lanes). Every other
       stage with the key (bilinear minification filter, or a split
       outside trilinear) runs a separate nearest pass first. */
    int ck_pass = ck && (linear || (split && !tri));
    int sel     = st && ds->sec_sel;
    int sa      = (int) (sel ? offsetof(r3d_texctx_t, s2a)
                             : offsetof(r3d_texctx_t, sta));
    int ta      = (int) (sel ? offsetof(r3d_texctx_t, t2a)
                             : offsetof(r3d_texctx_t, tta));
    /* The blend, specular and fog stages read float channels from the
       SRC slots; without them this stage packs the color itself. */
    int              park    = ds->alpha_en || ds->spec_en || ds->fog_en;
    static const int tslo[3] = { R128_X64_SP_SOAT_U0, R128_X64_SP_SOAT_V0,
                                 R128_X64_SP_SOAT_U1 };
    static const int bslo[4] = { R128_X64_SP_SOA_SRCR, R128_X64_SP_SOA_SRCG,
                                 R128_X64_SP_SOA_SRCB, R128_X64_SP_SOA_SRCA };
    /* RGB 565 pack, per channel: right shift of the 8-bit value, then
       left shift into place */
    static const int p565[3][2] = {
        { 3, 11 },
        { 2, 5  },
        { 3, 0  }
    };
    int ch;

    r128_x64_ld(e, 1, X64_RAX, X64_RBX, (int) offsetof(r128_jit_tri_t, texctx));
    if (!dual)
        r128_x64_soa_tex_ir(e, ds, has_lod); /* ir -> xmm13   */

    /* sp in xmm12 and tp in xmm14 from the weight dots. The LOD, mip
       slots and level sizes use sp and tp before the multiply by ir,
       as in the C; then s = sp * ir and t = tp * ir. */
    r128_x64_soa_tex_dot3(e, 12, 11, sa);
    r128_x64_soa_tex_dot3(e, 14, 11, ta);
    if (has_lod) {
        r128_x64_soa_tex_lod(e, ds, st); /* lod -> xmm11  */
        if (split) {
            /* mask of the lanes with lod > 0, which keep the
               minification result */
            r128_x64_xorps(e, 15, 15);
            r128_x64_cmpltps(e, 15, 11);
            r128_x64_movaps_st(e, 15, X64_RSP, R128_X64_SP_SOAT_MSK);
        }
        if (tri)
            r128_x64_soa_tex_slots_tri(e, st); /* -> SL/SLB/WM  */
        else if (mip_on) {
            r128_x64_soa_tex_slots(e, st); /* -> SL         */
            r128_x64_soa_tex_dims(e, st);  /* -> VLW/VLH    */
        }
    }
    if (dual)
        r128_x64_movaps_ld(e, 13, X64_RSP, R128_X64_SP_SOAT_IR);
    r128_x64_mulps(e, 12, 13); /* s             */
    r128_x64_mulps(e, 14, 13); /* t             */
    if (tri || split) {
        /* keep s and t for pass B and the magnification pass */
        r128_x64_movaps_st(e, 12, X64_RSP, R128_X64_SP_SOAT_TS);
        r128_x64_movaps_st(e, 14, X64_RSP, R128_X64_SP_SOAT_TT);
    }

    /* Level words: a base-level class stores the top level's words
       (r10d = lw, edx = lh); a per-lane class stores only the palette
       pointer, and each lane's words are written before its fetches.
       Then quantize and wrap per axis and fetch. ir in xmm13 is dead
       once s and t are formed, so the axes may use xmm13. */
    if (mip_on)
        r128_x64_soa_tex_pal(e, ds, st);
    else
        r128_x64_soa_tex_desc(e, ds, st); /* r10d lw, edx lh */
    if (ck_pass) {
        /* Separate nearest pass for the key, ahead of the filtered
           pass. s and t wait in the WU and WV slots, which the filtered
           pass writes only later. A trilinear stage first builds the
           level sizes for pass A's slots. A base-level stage reloads
           lw and lh into r10d and edx afterwards, since the fetches
           clobber both. */
        r128_x64_movaps_st(e, 12, X64_RSP, R128_X64_SP_SOAT_WU);
        r128_x64_movaps_st(e, 14, X64_RSP, R128_X64_SP_SOAT_WV);
        if (tri)
            r128_x64_soa_tex_dims(e, st); /* pass-A dims   */
        r128_x64_soa_tex_axis(e, h, 0, 0, mip_on, 12, X64_R10, 11, 15, 13);
        r128_x64_soa_tex_axis(e, h, 1, 0, mip_on, 14, X64_RDX, 11, 15, 13);
        r128_x64_soa_tex_gather(e, h, st, 0, mip_on);
        r128_x64_soa_tex_ck(e, ds);
        r128_x64_movaps_ld(e, 12, X64_RSP, R128_X64_SP_SOAT_WU);
        r128_x64_movaps_ld(e, 14, X64_RSP, R128_X64_SP_SOAT_WV);
        if (!mip_on) {
            r128_x64_ld(e, 0, X64_R10, X64_RSP, R128_X64_TS_LW);
            r128_x64_ld(e, 0, X64_RDX, X64_RSP, R128_X64_TS_LH);
        }
    }
    if (tri) {
        /* Trilinear: the coordinate and gather pipeline runs once per
           level through the stage's subroutine, because each level has
           its own sizes, quantization, wrap and 8.8 fractions, as in
           the C's two r3d_tex_level calls. Pass A uses the slots in SL;
           pass B copies its SLB slots into SL, which the subroutine
           reads. The two results are then blended per lane by the
           level weight, as r3d_lerp_argb(ca, cb, f) does. */
        r128_x64_call_to(e, e->soa_csub[st]);
        if (ck && !ck_pass)
            r128_x64_soa_tex_ck(e, ds);
        r128_x64_movaps_st(e, 11, X64_RSP, R128_X64_SP_SOAT_CA);
        r128_x64_movaps_ld(e, 0, X64_RSP, R128_X64_SP_SOAT_SLB);
        r128_x64_movaps_st(e, 0, X64_RSP, R128_X64_SP_SOAT_SL);
        r128_x64_movaps_ld(e, 12, X64_RSP, R128_X64_SP_SOAT_TS);
        r128_x64_movaps_ld(e, 14, X64_RSP, R128_X64_SP_SOAT_TT);
        r128_x64_call_to(e, e->soa_csub[st]);
        r128_x64_movaps_ld(e, 12, X64_RSP, R128_X64_SP_SOAT_CA);
        r128_x64_soa_lerp(e, 12, 11, R128_X64_SP_SOAT_WM);
        r128_x64_movaps_rr(e, 11, 12);
    } else {
        r128_x64_soa_tex_axis(e, h, 0, linear, mip_on, 12, X64_R10, 11, 15, 13);
        r128_x64_soa_tex_axis(e, h, 1, linear, mip_on, 14, X64_RDX, 11, 15, 13);
        r128_x64_soa_tex_gather(e, h, st, linear, mip_on); /* texel lanes */
        if (ck && !ck_pass)
            /* nearest stage: the gathered c00 lanes are the key's
               texel (the same unbiased floor, wrap and fetch at the
               same level, border included) */
            r128_x64_soa_tex_ck(e, ds);
    }
    if (split) {
        /* Magnification pass (the C's lod <= 0 case): the
           minification result waits in CA, the largest level is
           sampled again with the magnification filter from the saved
           s and t, and lanes with lod > 0 keep the minification texel
           (a bitwise select). A base-level minification pass leaves
           the top level's words in place, since the fetches only read
           them; a per-lane pass overwrote them, so they are rebuilt. */
        r128_x64_movaps_st(e, 11, X64_RSP, R128_X64_SP_SOAT_CA);
        r128_x64_movaps_ld(e, 12, X64_RSP, R128_X64_SP_SOAT_TS);
        r128_x64_movaps_ld(e, 14, X64_RSP, R128_X64_SP_SOAT_TT);
        if (mip_on)
            r128_x64_soa_tex_desc(e, ds, st); /* r10d lw, edx lh */
        else {
            r128_x64_ld(e, 0, X64_R10, X64_RSP, R128_X64_TS_LW);
            r128_x64_ld(e, 0, X64_RDX, X64_RSP, R128_X64_TS_LH);
        }
        r128_x64_soa_tex_axis(e, h, 0, h->mag == 1, 0, 12, X64_R10, 11, 15, 13);
        r128_x64_soa_tex_axis(e, h, 1, h->mag == 1, 0, 14, X64_RDX, 11, 15, 13);
        r128_x64_soa_tex_gather(e, h, st, h->mag == 1, 0);
        r128_x64_movaps_ld(e, 12, X64_RSP, R128_X64_SP_SOAT_CA);
        r128_x64_movaps_ld(e, 13, X64_RSP, R128_X64_SP_SOAT_MSK);
        r128_x64_pand(e, 12, 13);
        r128_x64_pandn(e, 13, 11);
        r128_x64_por(e, 12, 13);
        r128_x64_movaps_rr(e, 11, 12);
    }

    /* Texel channels to float, ((tx >> shift) & 0xff) / 255.0f as in the
       C. ta stays in xmm15; r, g and b go to the U0, V0 and U1 slots,
       free once every fetch is done, so the combine keeps its
       scratch registers. */
    r128_x64_movaps_rr(e, 15, 11);
    r128_x64_psrld(e, 15, 24);
    r128_x64_cvtdq2ps(e, 15, 15);
    r128_x64_sse_rip(e, 0, 0x5E, 15, R128_X64_CP_255F);
    for (ch = 0; ch < 3; ch++) {
        static const int shr[3] = { 16, 8, 0 };

        r128_x64_movaps_rr(e, 12, 11);
        if (shr[ch])
            r128_x64_psrld(e, 12, shr[ch]);
        r128_x64_sse_rip(e, 0x66, 0xDB, 12, R128_X64_CP_255I); /* pand  */
        r128_x64_cvtdq2ps(e, 12, 12);
        r128_x64_sse_rip(e, 0, 0x5E, 12, R128_X64_CP_255F); /* divps */
        r128_x64_movaps_st(e, 12, X64_RSP, tslo[ch]);
    }

    /* Color combine, one channel at a time, as r3d_tex_combine computes
       it for each lane. The code numbers are those of PRIMARY_COMB_FCN,
       COLOR_FACTOR and INPUT_FACTOR (SDK: Texture Mapping, pp. 6-42-6-43
       / PDF 154-155, Tables 6-7 to 6-9); the formulas, and the cd->fmsb
       variants of codes 0, 4, 5 and 6, are the interpreter's. Registers:
       texel channel xmm12, color factor fc xmm13, input factor ci
       xmm14, texel alpha xmm15, result xmm11, scratch xmm0 and xmm1.
       Previous-color operands read prev_base; interpolated-color
       operands always read C0. Each formula uses the same operations in
       the same order as the scalar stage. */
    for (ch = 0; ch < 3; ch++) {
        int ic_off = R128_X64_SP_SOAT_C0 + 16 * ch;
        int pv_off = prev_base + 16 * ch;

        r128_x64_movaps_ld(e, 12, X64_RSP, tslo[ch]);
        switch (cd->cfac) {
            case 0:
            case 1:
                r128_x64_splat_fimm(e, 13, cd->cfac ? 1.0f - ds->cc[ch] : ds->cc[ch], X64_RAX);
                break;
            case 5: /* 1 - t */
                r128_x64_sse_rip(e, 0, 0x28, 13, R128_X64_CP_ONEF);
                r128_x64_subps(e, 13, 12);
                break;
            case 6: /* ta */
                r128_x64_movaps_rr(e, 13, 15);
                break;
            case 7: /* 1 - ta */
                r128_x64_sse_rip(e, 0, 0x28, 13, R128_X64_CP_ONEF);
                r128_x64_subps(e, 13, 15);
                break;
            case 8: /* previous color */
                r128_x64_movaps_ld(e, 13, X64_RSP, pv_off);
                break;
            default: /* 4, texel color, and undefined codes */
                r128_x64_movaps_rr(e, 13, 12);
                break;
        }
        switch (cd->ifac) {
            case 2:
                r128_x64_splat_fimm(e, 14, ds->cc[ch], X64_RAX);
                break;
            case 3:
                r128_x64_splat_fimm(e, 14, ds->cc[3], X64_RAX);
                break;
            case 5: /* interpolated alpha */
                r128_x64_movaps_ld(e, 14, X64_RSP, R128_X64_SP_SOAT_C0 + 48);
                break;
            case 9: /* previous alpha */
                r128_x64_movaps_ld(e, 14, X64_RSP, prev_base + 48);
                break;
            case 8: /* previous color */
                r128_x64_movaps_ld(e, 14, X64_RSP, pv_off);
                break;
            default: /* 4, interpolated color, and undefined codes */
                r128_x64_movaps_ld(e, 14, X64_RSP, ic_off);
                break;
        }
        switch (cd->comb) {
            case 2:
                r128_x64_movaps_rr(e, 11, 14);
                break;
            case 0:
                if (cd->fmsb) { /* max(fc - ci, 0) */
                    r128_x64_movaps_rr(e, 11, 13);
                    r128_x64_subps(e, 11, 14);
                    r128_x64_sse_rip(e, 0, 0x5F, 11, R128_X64_CP_ZERO);
                } else /* disable: the texel channel itself */
                    r128_x64_movaps_rr(e, 11, 12);
                break;
            case 1: /* copy: fc */
                r128_x64_movaps_rr(e, 11, 13);
                break;
            case 4:
                if (cd->fmsb) { /* ci*(1-t) + fc*t */
                    r128_x64_sse_rip(e, 0, 0x28, 0, R128_X64_CP_ONEF);
                    r128_x64_subps(e, 0, 12);
                    r128_x64_mulps(e, 0, 14);
                    r128_x64_movaps_rr(e, 1, 13);
                    r128_x64_mulps(e, 1, 12);
                    r128_x64_movaps_rr(e, 11, 0);
                    r128_x64_addps(e, 11, 1);
                } else { /* min(2*ci*fc, 1) */
                    r128_x64_movaps_rr(e, 11, 14);
                    r128_x64_mulps(e, 11, 13);
                    r128_x64_addps(e, 11, 11);
                    r128_x64_sse_rip(e, 0, 0x5D, 11, R128_X64_CP_ONEF);
                }
                break;
            case 5:
                if (cd->fmsb) { /* min(fc + ci*(1-t), 1) */
                    r128_x64_sse_rip(e, 0, 0x28, 0, R128_X64_CP_ONEF);
                    r128_x64_subps(e, 0, 12);
                    r128_x64_mulps(e, 0, 14);
                    r128_x64_movaps_rr(e, 11, 13);
                    r128_x64_addps(e, 11, 0);
                    r128_x64_sse_rip(e, 0, 0x5D, 11, R128_X64_CP_ONEF);
                } else { /* min(4*ci*fc, 1) */
                    r128_x64_movaps_rr(e, 11, 14);
                    r128_x64_mulps(e, 11, 13);
                    r128_x64_addps(e, 11, 11);
                    r128_x64_addps(e, 11, 11);
                    r128_x64_sse_rip(e, 0, 0x5D, 11, R128_X64_CP_ONEF);
                }
                break;
            case 6:
                if (cd->fmsb) { /* min(fc + ci*t, 1) */
                    r128_x64_movaps_rr(e, 0, 14);
                    r128_x64_mulps(e, 0, 12);
                    r128_x64_movaps_rr(e, 11, 13);
                    r128_x64_addps(e, 11, 0);
                } else { /* min(ci + fc, 1) */
                    r128_x64_movaps_rr(e, 11, 14);
                    r128_x64_addps(e, 11, 13);
                }
                r128_x64_sse_rip(e, 0, 0x5D, 11, R128_X64_CP_ONEF);
                break;
            case 7:  /* clamp(ci + fc - 0.5, 0, 1) */
            case 14: /* clamp(2*(ci + fc - 0.5), 0, 1) */
                r128_x64_movaps_rr(e, 11, 14);
                r128_x64_addps(e, 11, 13);
                r128_x64_sse_rip(e, 0, 0x5C, 11, R128_X64_CP_HALFF);
                if (cd->comb == 14)
                    r128_x64_addps(e, 11, 11);
                r128_x64_sse_rip(e, 0, 0x5F, 11, R128_X64_CP_ZERO);
                r128_x64_sse_rip(e, 0, 0x5D, 11, R128_X64_CP_ONEF);
                break;
            case 8:
            case 9:
            case 10:
            case 12:
            case 15:
                /* lerp family: weight -> xmm0, C = ci*(1-w) + fc*w */
                switch (cd->comb) {
                    case 8: /* interpolated alpha */
                        r128_x64_movaps_ld(e, 0, X64_RSP, R128_X64_SP_SOAT_C0 + 48);
                        break;
                    case 12: /* previous alpha */
                        r128_x64_movaps_ld(e, 0, X64_RSP, prev_base + 48);
                        break;
                    case 9: /* ta */
                        r128_x64_movaps_rr(e, 0, 15);
                        break;
                    case 10:
                        r128_x64_splat_fimm(e, 0, ds->cc[3], X64_RAX);
                        break;
                    default: /* 15: per-channel cc */
                        r128_x64_splat_fimm(e, 0, ds->cc[ch], X64_RAX);
                        break;
                }
                r128_x64_sse_rip(e, 0, 0x28, 1, R128_X64_CP_ONEF);
                r128_x64_subps(e, 1, 0);  /* 1 - w         */
                r128_x64_mulps(e, 1, 14); /* ci*(1-w)      */
                r128_x64_mulps(e, 0, 13); /* fc*w          */
                r128_x64_movaps_rr(e, 11, 1);
                r128_x64_addps(e, 11, 0);
                break;
            case 11: /* min(fc + ci*(1-ta), 1) */
                r128_x64_sse_rip(e, 0, 0x28, 0, R128_X64_CP_ONEF);
                r128_x64_subps(e, 0, 15);
                r128_x64_mulps(e, 0, 14);
                r128_x64_movaps_rr(e, 11, 13);
                r128_x64_addps(e, 11, 0);
                r128_x64_sse_rip(e, 0, 0x5D, 11, R128_X64_CP_ONEF);
                break;
            case 13: /* min(fc + ci*ta, 1) */
                r128_x64_movaps_rr(e, 0, 14);
                r128_x64_mulps(e, 0, 15);
                r128_x64_movaps_rr(e, 11, 13);
                r128_x64_addps(e, 11, 0);
                r128_x64_sse_rip(e, 0, 0x5D, 11, R128_X64_CP_ONEF);
                break;
            case 3:
            default: /* MODULATE: ci * fc */
                r128_x64_movaps_rr(e, 11, 14);
                r128_x64_mulps(e, 11, 13);
                break;
        }
        if (out_float)
            r128_x64_movaps_st(e, 11, X64_RSP, R128_X64_SP_SOAT_P0 + 16 * ch);
        else if (park)
            r128_x64_movaps_st(e, 11, X64_RSP, bslo[ch]);
        else if (ds->dst_dt == 6)
            r128_x64_soa_quant_pack(e, 11, 9, ch == 0, 0, 16 - 8 * ch, 0, 0);
        else
            r128_x64_soa_quant_pack(e, 11, 9, ch == 0, p565[ch][0],
                                    p565[ch][1], dith_on,
                                    (ch == 1) ? e->soa_bay2 : e->soa_bay1);
    }

    if (out_float || park || at_on || ds->dst_dt == 6) {
        /* Alpha combine, as r3d_tex_combine computes it. The codes are
           those of COMB_FCN_ALPHA (SDK: Texture Mapping, p. 6-44 /
           PDF 156, Table 6-10), ALPHA_FACTOR and INPUT_FACTOR_ALPHA;
           as in the interpreter, any other alpha function code
           modulates. The 565 pack does not use the alpha. A stage-0
           float output always computes it, because stage 1's
           previous-alpha operands read it, as they read the
           interpreter's col[3]; the blend factors, the alpha test and
           the 8888 pack also need it. Registers: fa xmm13, ia xmm14,
           the texel alpha xmm15, result xmm11. */
        if (cd->afac == 7) {
            r128_x64_sse_rip(e, 0, 0x28, 13, R128_X64_CP_ONEF);
            r128_x64_subps(e, 13, 15);
        } else
            r128_x64_movaps_rr(e, 13, 15);
        if (cd->ifaca == 1)
            r128_x64_splat_fimm(e, 14, ds->cc[3], X64_RAX);
        else if (cd->ifaca == 2) /* interpolated alpha */
            r128_x64_movaps_ld(e, 14, X64_RSP, R128_X64_SP_SOAT_C0 + 48);
        else /* 4, previous alpha, and undefined codes */
            r128_x64_movaps_ld(e, 14, X64_RSP, prev_base + 48);
        switch (cd->comba) {
            case 0: /* disable: the texel alpha itself on the first
                       stage, the incoming alpha after it */
                if (cd == &ds->comb[0])
                    r128_x64_movaps_rr(e, 11, 15);
                else
                    r128_x64_movaps_ld(e, 11, X64_RSP, prev_base + 48);
                break;
            case 1: /* copy: fa */
                r128_x64_movaps_rr(e, 11, 13);
                break;
            case 2:
                r128_x64_movaps_rr(e, 11, 14);
                break;
            case 4: /* min(2*ia*fa, 1) */
            case 5: /* min(4*ia*fa, 1) */
                r128_x64_movaps_rr(e, 11, 14);
                r128_x64_mulps(e, 11, 13);
                r128_x64_addps(e, 11, 11);
                if (cd->comba == 5)
                    r128_x64_addps(e, 11, 11);
                r128_x64_sse_rip(e, 0, 0x5D, 11, R128_X64_CP_ONEF);
                break;
            case 6: /* min(ia + fa, 1) */
                r128_x64_movaps_rr(e, 11, 14);
                r128_x64_addps(e, 11, 13);
                r128_x64_sse_rip(e, 0, 0x5D, 11, R128_X64_CP_ONEF);
                break;
            case 7:  /* clamp(ia + fa - 0.5, 0, 1) */
            case 14: /* clamp(2*(ia + fa - 0.5), 0, 1) */
                r128_x64_movaps_rr(e, 11, 14);
                r128_x64_addps(e, 11, 13);
                r128_x64_sse_rip(e, 0, 0x5C, 11, R128_X64_CP_HALFF);
                if (cd->comba == 14)
                    r128_x64_addps(e, 11, 11);
                r128_x64_sse_rip(e, 0, 0x5F, 11, R128_X64_CP_ZERO);
                r128_x64_sse_rip(e, 0, 0x5D, 11, R128_X64_CP_ONEF);
                break;
            case 3:
            default:
                r128_x64_movaps_rr(e, 11, 14);
                r128_x64_mulps(e, 11, 13);
                break;
        }
        if (out_float) {
            /* stage 1 packs and tests; the float channels sit at P0 */
            r128_x64_movaps_st(e, 11, X64_RSP, R128_X64_SP_SOAT_P0 + 48);
            return;
        }
        if (park)
            r128_x64_movaps_st(e, 11, X64_RSP, R128_X64_SP_SOA_SRCA);
        if (at_on) {
            int inv, mreg;

            /* Alpha test on the combined alpha, quantized as the
               interpreter does, (u32) (a * 255 + 0.5), and compared with
               the reference by ALPHA_TEST_OP. The interpreter tests
               before it reads the destination, so the test runs here,
               ahead of the blend. movmskps collects the four lane
               results; when r128_x64_soa_ucmp returns the inverse mask,
               xor ecx, 0xf flips them. The pass bits are ANDed into the
               lane cover mask, so failed lanes neither store nor widen
               rx0/rx1. */
            r128_x64_movaps_rr(e, 12, 11);
            r128_x64_sse_rip(e, 0, 0x59, 12, R128_X64_CP_255F);
            r128_x64_sse_rip(e, 0, 0x58, 12, R128_X64_CP_HALFF);
            r128_x64_cvttps2dq(e, 12, 12);
            r128_x64_splat_imm(e, 13, ds->atest_ref & 0xff, X64_RCX);
            mreg = r128_x64_soa_ucmp(e, ds->atest_fn, 12, 13, 0, &inv);
            r128_x64_movmskps(e, X64_RCX, mreg);
            if (inv)
                r128_x64_alu_r_imm(e, 6, 0, X64_RCX, 0xf);
            r128_x64_alu_mem_r(e, 0x21, 0, X64_RCX, X64_RSP, R128_X64_SP_KMASK);
        }
        if (!park && ds->dst_dt == 6)
            r128_x64_soa_quant_pack(e, 11, 9, 0, 0, 24, 0, 0);
    }

    /* The store stage takes the packed lanes in xmm9. For RGB 565,
       packusdw narrows the dword lanes to words; every value fits in 16
       bits, so nothing saturates. */
    if (!park && ds->dst_dt != 6)
        r128_x64_packusdw(e, 9, 9);
}

/* The textured group body. The interpolated vertex color is computed
   once into C0, where both stages' interpolated-color operands read
   it. With one stage, that stage finishes the color. With two
   (sec_en), rhw and ir are computed once, as rage128_texstage_run
   computes them once for both stages; stage 0 then writes float
   output, and stage 1 reads it as its previous color, as the
   interpreter's col[] carries it, and finishes the color. */
static void
r128_x64_emit_soa_texstage(r128_x64_emit_t *e, const rage128_draw_state_t *ds)
{
    int ch;

    /* Channel ch of w0*vca + w1*vcb + w2*vcc per lane, added left to
       right like the C, stored as floats at C0. vca, vcb and vcc are
       the RGBA vertex colors in xmm5 to xmm7; pshufd with ch * 0x55
       copies element ch to all four lanes. */
    for (ch = 0; ch < 4; ch++) {
        uint8_t sel = (uint8_t) (ch * 0x55);

        r128_x64_movaps_rr(e, 12, 8);
        r128_x64_pshufd(e, 0, 5, sel);
        r128_x64_mulps(e, 12, 0);
        r128_x64_movaps_rr(e, 1, 9);
        r128_x64_pshufd(e, 0, 6, sel);
        r128_x64_mulps(e, 1, 0);
        r128_x64_addps(e, 12, 1);
        r128_x64_movaps_rr(e, 1, 10);
        r128_x64_pshufd(e, 0, 7, sel);
        r128_x64_mulps(e, 1, 0);
        r128_x64_addps(e, 12, 1);
        r128_x64_movaps_st(e, 12, X64_RSP, R128_X64_SP_SOAT_C0 + 16 * ch);
    }

    if (!ds->sec_en) {
        r128_x64_emit_soa_texstage_one(e, ds, 0, R128_X64_SP_SOAT_C0, 0, 0);
        return;
    }

    /* Dual head: rhw (when either stage computes a LOD) and ir, once.
       Each stage reloads ir from the IR slot for its s and t
       multiply. */
    r128_x64_ld(e, 1, X64_RAX, X64_RBX, (int) offsetof(r128_jit_tri_t, texctx));
    r128_x64_soa_tex_ir(e, ds, ds->need_lod || ds->need_lod2);
    r128_x64_movaps_st(e, 13, X64_RSP, R128_X64_SP_SOAT_IR);
    r128_x64_emit_soa_texstage_one(e, ds, 0, R128_X64_SP_SOAT_C0, 1, 1);
    r128_x64_emit_soa_texstage_one(e, ds, 1, R128_X64_SP_SOAT_P0, 0, 1);
}

#endif /* VIDEO_ATI_RAGE128_CODEGEN_X86_64_SOA_TEX_H */

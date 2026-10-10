/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- ARM64 span JIT, SoA multi-pixel row loop.
 *
 *          For the states the sub-gates below admit (untextured states,
 *          and the textured states r128_a64_soa_tex_can accepts), the
 *          span function starts with a vector loop that shades four
 *          pixels per iteration in SoA form: each per-pixel scalar
 *          becomes one lane of a NEON vector. The ordinary scalar loop
 *          follows and finishes the rest of the row. The per-pixel
 *          skips (coverage, aux scissors, Z test, chroma key, alpha
 *          test) become per-lane masks, and the store writes back only
 *          the lanes whose mask bit is set.
 *
 *          Every lane must match the interpreter bit for bit. Each lane
 *          runs the same float operations in the same order as the C
 *          code: no fused multiply-add and no reassociation. The int64
 *          edge lanes are exact. (float)e converts int64 to double,
 *          which is exact because |e| stays below 2^40 for the snapped
 *          coordinate range, and then double to float, which rounds
 *          once; the scalar loop's single SCVTF from int64 to float
 *          gives the same result. zline advances by the same serial
 *          chain of double adds as the C loop.
 *
 *          A group whose lane addresses would leave a staged arena or
 *          straddle the VRAM wrap mask bails to the scalar loop before
 *          any accumulator is stepped, so the scalar loop applies its
 *          own per-pixel bounds skips to those pixels.
 *
 *          Included only by vid_ati_rage128_codegen_arm64.h.
 *
 *          Target: baseline ARMv8.0-A with Advanced SIMD (NEON) only.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEO_ATI_RAGE128_CODEGEN_ARM64_SOA_H
#define VIDEO_ATI_RAGE128_CODEGEN_ARM64_SOA_H

/* ------------------------------------------------------------------------
 * Vector-loop register map (four lanes). The general registers keep the
 * roles they have in the scalar loop (see vid_ati_rage128_codegen_arm64.h):
 * the vector loop keeps x1/x2/x3 (e0..e2 at the group base), d0 (zline),
 * w12 (px) and w23/w24 (rx0/rx1) at exactly the values the scalar loop
 * would hold after the same pixels. Any group boundary is therefore a
 * valid entry point into the scalar loop, both for the row tail and for
 * a bail.
 *
 * NEON registers live across the whole vector loop:
 *   v1        invs splat 4S (lane 0 keeps invs for the scalar tail)
 *   v4/v5/v6  vca/vcb/vcc (RGBA; read with FMUL by element)
 *   v8/v9     e0 lane offsets 2D: {0, dx0} / {2*dx0, 3*dx0}
 *   v10/v11   e1 lane offsets
 *   v12/v13   e2 lane offsets      (d8-d15 saved in the prologue)
 *   v16       (z_on) (double)zmax splat 2D
 *   v17       (z_on) 0.5 splat 2D
 *   v18       (z_on) 1.0 splat 2D
 *   v19..v22  not written (lane 0 holds the scalar d19..d22 constants)
 *   v23/v24   255.0f / 0.5f splats 4S (shared with the scalar loop)
 *   v28       (dither) 255 integer splat 4S (shared with the scalar loop)
 *   v29/v30   (dither) the row's Bayer adds 4S: 565 uses bay>>1 / bay>>2,
 *             1555 RGB uses bay>>1 in v29, and 4444 uses bay in v29;
 *             (px+k)&3 stays fixed as px advances by 4, so adds stay live
 *   d0/d2     zline accumulator / dZdx (shared with the scalar loop)
 * NEON scratch inside one group iteration:
 *   v3, v7, v14, v15, v25, v26, v27, v31   (8 registers, all used at peak)
 * General scratch: x16, x17, x25, x28. x26 is the group's Z cell base and
 * x27 the group's color cell base, the same roles as the scalar loop's
 * lane-0 addresses.
 * ---------------------------------------------------------------------- */

/* ---- additional instruction emitters (SoA only) ----
   Each emitter writes one A64 instruction. The function name gives the
   mnemonic and the vector arrangement (16b = 16 bytes, 8h = 8 halfwords,
   4h = 4 halfwords, 4s = 4 words, 2d = 2 doublewords; _x/_w = 64-/32-bit
   general register), and the hex constant is that instruction's encoding
   with every register field zero. */

static void
r128_a64_add_x_lsl(r128_a64_emit_t *e, int rd, int rn, int rm, int sh)
{
    r128_a64_e32(e, 0x8B000000 | (rm << 16) | (sh << 10) | (rn << 5) | rd);
}

static void
r128_a64_tst_w_reg(r128_a64_emit_t *e, int rn, int rm)
{
    r128_a64_e32(e, 0x6A00001F | (rm << 16) | (rn << 5));
}

static void
r128_a64_rbit_x(r128_a64_emit_t *e, int rd, int rn)
{
    r128_a64_e32(e, 0xDAC00000 | (rn << 5) | rd);
}

static void
r128_a64_clz_x(r128_a64_emit_t *e, int rd, int rn)
{
    r128_a64_e32(e, 0xDAC01000 | (rn << 5) | rd);
}

/* TBZ Xt, #bit, label for any bit 0..63: bit 5 of the index goes in the
   b5 field (bit 31). The base emitter covers only bits 0..31. Returns
   the instruction's position for patching. */
static int
r128_a64_tbz_any(r128_a64_emit_t *e, int rt, int bit)
{
    int at = e->pos;

    r128_a64_e32(e, 0x36000000 | ((bit >= 32) ? 0x80000000u : 0) | ((uint32_t) (bit & 31) << 19) | rt);
    return at;
}

static void
r128_a64_cmn_x_imm(r128_a64_emit_t *e, int rn, int imm)
{
    r128_a64_e32(e, 0xB100001F | ((uint32_t) imm << 10) | (rn << 5));
}

/* SUB Xd, Xn, #imm12. Register 31 is SP in this form; the prologue uses
   it to open frames larger than the STP pre-index offset can reach. */
static void
r128_a64_sub_x_imm(r128_a64_emit_t *e, int rd, int rn, int imm)
{
    r128_a64_e32(e, 0xD1000000 | ((uint32_t) imm << 10) | (rn << 5) | rd);
}

static void
r128_a64_dup_2d_x(r128_a64_emit_t *e, int vd, int xn)
{
    r128_a64_e32(e, 0x4E080C00 | (xn << 5) | vd);
}

static void
r128_a64_dup_2d_lane(r128_a64_emit_t *e, int vd, int vn, int lane)
{
    r128_a64_e32(e, 0x4E000400 | ((uint32_t) ((lane << 4) | 8) << 16) | (vn << 5) | vd);
}

static void
r128_a64_ins_elem_d(r128_a64_emit_t *e, int vd, int dlane, int vn, int slane)
{
    r128_a64_e32(e, 0x6E000400 | ((uint32_t) ((dlane << 4) | 8) << 16) | ((uint32_t) (slane << 3) << 11) | (vn << 5) | vd);
}

static void
r128_a64_not_16b(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x6E205800 | (vn << 5) | vd);
}

static void
r128_a64_bsl_16b(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6E601C00 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_add_2d(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4EE08400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_cmge0_2d(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x6EE08800 | (vn << 5) | vd);
}

static void
r128_a64_scvtf_2d(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x4E61D800 | (vn << 5) | vd);
}

static void
r128_a64_fcvtn(r128_a64_emit_t *e, int vd, int vn) /* Vd.2S <- Vn.2D, clears top */
{
    r128_a64_e32(e, 0x0E616800 | (vn << 5) | vd);
}

static void
r128_a64_fcvtn2(r128_a64_emit_t *e, int vd, int vn) /* Vd.4S upper <- Vn.2D */
{
    r128_a64_e32(e, 0x4E616800 | (vn << 5) | vd);
}

static void
r128_a64_fmul_2d(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6E60DC00 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_fadd_2d(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4E60D400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_fcmgt0_2d(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x4EE0C800 | (vn << 5) | vd);
}

static void
r128_a64_fcmgt_2d(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6EE0E400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_fcvtzu_2d(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x6EE1B800 | (vn << 5) | vd);
}

static void
r128_a64_uzp1_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4E801800 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_uzp2_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4E805800 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_xtn_4h(r128_a64_emit_t *e, int vd, int vn) /* Vd.4H <- Vn.4S */
{
    r128_a64_e32(e, 0x0E612800 | (vn << 5) | vd);
}

static void
r128_a64_uxtl_4s(r128_a64_emit_t *e, int vd, int vn) /* Vd.4S <- Vn.4H */
{
    r128_a64_e32(e, 0x2F10A400 | (vn << 5) | vd);
}

static void
r128_a64_sxtl_4s(r128_a64_emit_t *e, int vd, int vn) /* sign-extend 4H */
{
    r128_a64_e32(e, 0x0F10A400 | (vn << 5) | vd);
}

static void
r128_a64_cmhi_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6EA03400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_cmhs_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6EA03C00 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_cmeq_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6EA08C00 | (vm << 16) | (vn << 5) | vd);
}

/* FMUL Vd.4S, Vn.4S, Vm.S[lane]: a separate multiply, never a fused
   FMLA, so each product rounds as it does in the C code. */
static void
r128_a64_fmul_4s_elem(r128_a64_emit_t *e, int vd, int vn, int vm, int lane)
{
    r128_a64_e32(e, 0x4F809000 | ((uint32_t) (lane & 1) << 21) | (vm << 16) | ((uint32_t) (lane >> 1) << 11) | (vn << 5) | vd);
}

/* ---- emitters for textured groups ---- */

static void
r128_a64_frintn_4s(r128_a64_emit_t *e, int vd, int vn) /* nearbyintf lanes */
{
    r128_a64_e32(e, 0x4E218800 | (vn << 5) | vd);
}

static void
r128_a64_fcvtms_4s(r128_a64_emit_t *e, int vd, int vn) /* (int)floorf lanes */
{
    r128_a64_e32(e, 0x4E21B800 | (vn << 5) | vd);
}

static void
r128_a64_scvtf_4s(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x4E21D800 | (vn << 5) | vd);
}

static void
r128_a64_smax_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4EA06400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_smin_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x4EA06C00 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_cmgt_4s(r128_a64_emit_t *e, int vd, int vn, int vm) /* signed */
{
    r128_a64_e32(e, 0x4EA03400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_cmge_4s(r128_a64_emit_t *e, int vd, int vn, int vm) /* signed >= */
{
    r128_a64_e32(e, 0x4EA03C00 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_fcmeq0_4s(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x4EA0D800 | (vn << 5) | vd);
}

static void
r128_a64_eor_16b(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6E201C00 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_movi_8h(r128_a64_emit_t *e, int vd, uint32_t imm8) /* per-16-bit */
{
    r128_a64_e32(e, 0x4F008400 | (((imm8 >> 5) & 7) << 16) | ((imm8 & 31) << 5) | vd);
}

/* ---- emitters for per-pixel LOD ---- */

static void
r128_a64_fcvtzs_4s(r128_a64_emit_t *e, int vd, int vn) /* (int) cast lanes */
{
    r128_a64_e32(e, 0x4EA1B800 | (vn << 5) | vd);
}

static void
r128_a64_fcmgt_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6EA0E400 | (vm << 16) | (vn << 5) | vd);
}

static void
r128_a64_fcmgt0_4s(r128_a64_emit_t *e, int vd, int vn)
{
    r128_a64_e32(e, 0x4EA0C800 | (vn << 5) | vd);
}

/* ---- emitters for stencil ---- */

static void
r128_a64_and_x_reg(r128_a64_emit_t *e, int rd, int rn, int rm)
{
    r128_a64_e32(e, 0x8A000000 | (rm << 16) | (rn << 5) | rd);
}

static void
r128_a64_umax_4s(r128_a64_emit_t *e, int vd, int vn, int vm)
{
    r128_a64_e32(e, 0x6EA06400 | (vm << 16) | (vn << 5) | vd);
}

/* ------------------------------------------------------------------------
 * SoA sub-gate for untextured states (r128_jit_soa_can): which states
 * that _can() approved get the vector loop. Textured states go through
 * r128_a64_soa_tex_can below instead. Table fog with z_en stores the raw
 * zline double lanes before the Z block consumes them, and the fog stage
 * derives zc from them again per lane. Vertex fog, specular, aux scissors
 * and stencil all run in the vector loop; stencil stores the Z block's
 * results packed per lane and does a masked read-modify-write after the
 * color pack.
 * ---------------------------------------------------------------------- */
static int
r128_a64_soa_can(const rage128_draw_state_t *ds)
{
    return r128_jit_soa_can(ds);
}

/* What the textured vector loop covers (gate: r128_a64_soa_tex_can).
   Every texel family the inline stage decodes runs in every filter
   class, with one stage or two (sec_en). Besides requiring stage 0 to
   be enabled, the gate refuses only a chroma key on a CI texture,
   texture lighting, textured table fog and dual pairs over the size
   budgets. The S3TC and YUV families decode in a per-stage BL
   subroutine that is emitted once ahead of both loops and shared with
   the scalar tail.

   With per-pixel LOD the mip slot is chosen per lane. minb 2/3 sample
   one nearest mip level. minb 4/5 (trilinear) run the coordinate and
   gather pipeline once per level and blend the two results. When the
   MIN and MAG texel filters differ, the minify result is stored, the
   magnify lanes are sampled again at the base level with the MAG
   filter, and the LOD sign picks one result per lane.

   Chroma key runs in every class. The key compares the nearest texel
   before filtering (an unbiased floor fetch at the primary level). On
   nearest classes without trilinear or split, that texel is the
   gathered c00; every other class runs a separate per-lane nearest
   pass ahead of the filtered pipeline.

   Alpha test ANDs its pass mask into the deferred cover stash. Alpha
   blend runs a per-channel dst read, factor and blend stage between
   the texture stage and the pack. Specular and vertex fog run per
   channel on the float channels the texture stage leaves in the frame;
   they reload the weights from the W0..W2 stash, so textured spec/fog
   blocks take the 656-byte R128_A64_FRAME_ST2 frame where those slots
   exist. Aux scissors AND each rect's per-lane x-window mask into the
   cover mask right after coverage, using the row's y-active mask at
   R128_A64_SP_AUX. Textured table fog is refused: it forces the
   rage128_texstage_run call path, which the vector loop cannot take.
   Untextured table fog with z_en runs in the loop from the stored raw
   zline lanes. */
/* Dual-stage (sec_en) blocks run every filter class per stage: no LOD,
   mip-nearest, trilinear (minb 4/5) and the MIN/MAG split. The
   trilinear and split slots (SLB, WM, TS, TT, CA, MSK; see the frame
   slot section below, e.g. R128_A64_SP_SOA_SLB) live only within one
   stage and sit below the dual stashes at P0 and W0..IR, so the 656-byte
   frame holds them unchanged. The two stages together must still fit
   the 16 KB block cap, so r128_a64_soa_tex_can weighs the stage pair
   with the functions below. */

/* Emitted size of one stage's SoA pipeline, in bytes above a no-LOD
   nearest stage: the worst case measured by the sizes mode of
   tests/video/rage128/jit-harness over all texel families, all 16 wrap-mode
   pairs and the largest optional stages. A dual block adds the two
   stage weights to a shared base (prologue, dual head, blend, alpha
   test, store and scalar tail) that measures at most 7656 bytes, so the
   stage-pair budget is 16384 - 7656 less a margin. Over the 12x12
   matrix of stage classes measured, base plus the two weights matched
   the emitted length to within 4 bytes. */
static int
r128_a64_soa_stage_weight(const rage128_draw_state_t *ds, int st)
{
    const r3d_stage_hdr_t *h       = &ds->sh[st];
    int                    has_lod = st ? ds->need_lod2 : ds->need_lod;
    int                    mip_on  = r128_jit_minb_mip_on(h->minb, h->mipdis, has_lod);
    int                    tri     = r128_jit_minb_tri(h->minb, h->mipdis, has_lod);
    int                    lmin    = r128_jit_minb_lin_min(h->minb, h->mipdis, has_lod);
    int                    split   = r128_jit_minb_split(h->minb, h->mipdis, h->mag, has_lod);
    int                    linear  = has_lod ? lmin
                                             : !(h->minb == 0 && h->mag == 0);
    int                    w;

    if (tri && split) /* 2 tri passes + mag rerun */
        w = linear ? 8048 : 5524;
    else if (tri)
        w = linear ? 7992 : 2528;
    else if (split) /* min pipeline + mag rerun */
        w = mip_on ? 3940 : 3516;
    else if (mip_on)
        w = linear ? 3868 : 1296;
    else
        w = linear ? 1840 : 0;
    /* Stage-0 chroma key. On nearest classes without trilinear or
       split, the pipeline's c00 is already the nearest texel and only
       the masked compare is added; every other class also adds the
       separate per-lane nearest pass. Worst case per class from the
       harness sizes mode with the chroma key on (largest at dt 3 with
       border wrap on s and mirror on t). */
    if (!st && ds->need_ck) {
        if (!(linear || tri || split))
            w += 160;
        else if (tri && split)
            w += 1300;
        else if (tri)
            w += linear ? 1468 : 1132;
        else if (split)
            w += mip_on ? 1160 : 964;
        else
            w += mip_on ? 1328 : 964;
    }
    return w;
}

/* Per-stage size in sub mode. A heavy dual pair (inline sum over the
   budget) emits each trilinear stage's filtered coordinate and gather
   pipeline once, as a BL subroutine that pass A and pass B both call;
   on nearest texel-filter classes the stage-0 chroma-key nearest pass
   calls it too. That saves about one pipeline copy per trilinear stage.
   Classes without trilinear emit inline either way and keep their
   inline weights: their magnify rerun uses a different axis and gather
   setup, so there is nothing to share. The constants are the per-cell
   maximums from the harness sizes mode swept over the wrap modes. In
   sub mode the block length was exactly 7656 + w0 + w1 in every cell
   measured. */
static int
r128_a64_soa_stage_weight_sub(const rage128_draw_state_t *ds, int st)
{
    const r3d_stage_hdr_t *h       = &ds->sh[st];
    int                    has_lod = st ? ds->need_lod2 : ds->need_lod;
    int                    lmin    = r128_jit_minb_lin_min(h->minb, h->mipdis, has_lod);
    int                    tri     = r128_jit_minb_tri(h->minb, h->mipdis, has_lod);
    int                    split   = r128_jit_minb_split(h->minb, h->mipdis, h->mag, has_lod);
    int                    linear  = lmin;
    int                    w;

    if (!tri)
        return r128_a64_soa_stage_weight(ds, st);
    if (split)
        w = linear ? 5256 : 4596;
    else
        w = linear ? 5200 : 1888;
    if (!st && ds->need_ck) {
        if (linear) /* the nearest pass cannot reuse a linear sub, so
                       it stays inline at its inline weight */
            w += split ? 1300 : 1468;
        else /* the nearest pass is the sub itself: one BL plus
                the compare */
            w += split ? 348 : 180;
    }
    return w;
}

/* Stencil weight in the dual budgets. Turning stencil on adds at most
   500 bytes to the same state (harness sizes mode, alpha test on and
   off, worst stencil ops; the negative deltas seen there come from a
   pair switching from inline to sub mode, not from the stencil code).
   900 leaves 400 bytes of slack: with it, the full stencil sweep has no
   overflow and its largest block is 15752 bytes, 632 under the cap,
   more than the 256-byte margin the budgets keep. */
#define R128_A64_SOA_STEN_W 900

/* True when a dual pair is over both weight budgets (inline 8600, sub
   8500); such a pair keeps its scalar-loop block. Used by
   r128_a64_soa_tex_can. */
static int
r128_a64_soa_pair_weight_gated(const rage128_draw_state_t *ds)
{
    int sf = (ds->spec_en || ds->fog_en) ? 516 : 0;
    /* specular/fog add to the shared base, not to a stage:
       at most 516 bytes (harness sizes mode, same-state
       deltas) */
    int aux = ds->aux_on ? 360 : 0;
    /* at most 360 bytes; counted in the sub sum only, see
       r128_a64_soa_tex_can */
    int stw = ds->sten_on ? R128_A64_SOA_STEN_W : 0;
    /* stencil counts in both budgets */

    return r128_a64_soa_stage_weight(ds, 0)
            + r128_a64_soa_stage_weight(ds, 1) + sf + stw
        > 8600
        && r128_a64_soa_stage_weight_sub(ds, 0)
            + r128_a64_soa_stage_weight_sub(ds, 1)
            + sf + aux + stw
        > 8500;
}

static int
r128_a64_soa_tex_can(const rage128_draw_state_t *ds)
{
    if (ds->stip_en || !ds->tex_en)
        return 0;
    /* The textured vector tails pack only 565 or 8888; alpha-bearing
       16-bit targets keep scalar sampling, blending and packing. */
    if (ds->dst_dt == 3 || ds->dst_dt == 15)
        return 0;
    /* CI chroma key compares the raw palette index; the vector gather
       only has the converted lanes, so those draws keep the scalar loop. */
    if (ds->need_ck && (ds->sh[0].dt == 1 || ds->sh[0].dt == 2))
        return 0;
    if (ds->sec_en) {
        /* Dual stage: both stages must be in the inline texel families.
           The stage-0 chroma key is allowed in every filter class: its
           nearest pass counts in the stage-0 weight, and the dual cover
           stash sits above the 656-byte frame (r128_a64_soa_cover_off),
           clear of the per-stage overlay at 96..191. */
        if (!r128_jit_dt_inline_family(ds->sh[0].dt)
            || !r128_jit_dt_inline_family(ds->sh[1].dt))
            return 0;
        /* 16 KB block cap. A heavy pair would overflow the code block,
           so it keeps its scalar-loop block instead;
           r128_jit_arm64_generate also re-emits any overflow the
           measurements missed as a scalar-only block. The inline
           budget 8600 sits between the largest fitting pair sum
           measured (8052, a 15708-byte block, 676 free) and the
           smallest overflowing one (9040). A pair over the inline
           budget is checked again with the sub-mode weights (BL
           coordinate/gather subroutine, trilinear stages only); a pair
           over both stays scalar. In sub mode the length is exactly
           7656 + sum, so the sub budget 8500 keeps every admitted pair
           at least 256 bytes under the cap (largest admitted sum 8464,
           16120 bytes, 264 free; smallest refused 8536, 16192 bytes,
           only 192 free). Aux scissors add to the base like specular
           and fog, at most 360 bytes over the six shapes measured
           (same-state deltas); without that weight the tightest
           sub-mode cells overflow, so aux counts in the sub sum. The
           inline budget leaves aux out: the largest inline pair (15708)
           plus 360 still fits, and counting aux there would only move
           states that fit onto the scalar loop. */
        if (r128_a64_soa_pair_weight_gated(ds))
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
        return 0; /* textured table fog: only the rage128_texstage_run
                     call path handles it, and the inline gate refuses
                     it too. Untextured table fog with z_en runs in the
                     vector loop. */
    /* Texture lighting: the vector combine has no third combine pass,
       so lit draws keep the scalar loop, which runs it inline. */
    if (ds->light_on)
        return 0;
    /* Single stage: every inline texel family in every filter class.
       This includes the S3TC/YUV subroutine families: in trilinear and
       split blocks their decode sub keeps its third scratch word in
       v31.s[2] instead of frame word 184, which is the WM slot. */
    return r128_jit_dt_inline_family(ds->sh[0].dt);
}

/* Frame slots. Offsets are from SP. The comments in this file call a
   slot by the last part of its macro name: SL for R128_A64_SP_SOA_SL,
   WM for R128_A64_SP_SOA_WM, and so on. In the definitions, "q" marks
   a 16-byte vector slot, "x" an 8-byte general-register slot and "d"
   an 8-byte vector slot; "4S"/"4H" give the lane layout.

   The masked-store bounce reuses the helper-call spill area, which
   untextured blocks never use: packed color at R128_A64_SP_COL, zi at
   R128_A64_SP_E01. Untextured blocks save d8-d15 over
   R128_A64_SP_TRI..R128_A64_SP_D21 (96..159); textured SoA blocks keep
   the texture prologue's save at R128_A64_SP_D8. */
#define R128_A64_SP_SOAZ R128_A64_SP_E01

/* Textured-group frame extension, above the inline texture stage's
   frame (R128_A64_FRAME_TI, 336). The helper-call spill slots 192..255
   hold the four int-color channels (C0); the store bounce writes there
   only after the combine has read them. During the vector loop the
   R128_A64_TS_LW, _LH, _BASE, _MASK and _PAL words hold the level
   descriptor hoisted once per row; the scalar tail rebuilds them per
   pixel afterwards. */
/* clang-format off */
#define R128_A64_SP_SOA_C0  R128_A64_SP_COL /* int color r,g,b,a: 4 q  */
#define R128_A64_SP_SOA_U0  336             /* wrapped lane coords: q  */
#define R128_A64_SP_SOA_U1  352
#define R128_A64_SP_SOA_V0  368
#define R128_A64_SP_SOA_V1  384
#define R128_A64_SP_SOA_WU  400             /* 8.8 lerp weights: q     */
#define R128_A64_SP_SOA_WV  416
#define R128_A64_SP_SOA_TB  432             /* level texbase: x        */
#define R128_A64_SP_SOA_FLW 440             /* (float)lw / (float)lh   */
#define R128_A64_SP_SOA_FLH 444
#define R128_A64_SP_SOA_ZI  448             /* zi vector spill: q      */
#define R128_A64_SP_SOA_DC  464             /* dst group cell spill: x
                                               (emit_texel returns its
                                               texel in w27 = dcell)   */
#define R128_A64_FRAME_ST   480
/* clang-format on */

/* Mip-mapped (need_lod) groups choose the mip slot per lane, so the
   level descriptor cannot be hoisted per row. These vector slots
   overlay the scalar texture stage's stash words 96..191, none of which
   the vector loop uses otherwise. The per-lane descriptor reload at
   fetch time still writes R128_A64_TS_BASE and _MASK (112..119) and
   R128_A64_TS_LW (152..155), and R128_A64_TS_LH (120..123) belongs to
   the same descriptor; the q slots below avoid all of these words. */
/* clang-format off */
#define R128_A64_SP_SOA_VLW 96              /* per-lane level lw 4S: q
                                               (also the cover-mask
                                               stash of chroma-key
                                               blocks that leave it
                                               free, see
                                               r128_a64_soa_cover_off)  */
#define R128_A64_SP_SOA_VLH 128             /* per-lane level lh 4S: q */
#define R128_A64_SP_SOA_SL  160             /* per-lane mip slot 4H: d */
/* clang-format on */

/* Trilinear (minb 4/5) slots. The level-B slot vector and the 8.8
   mip-blend weight take the last free d slots of the 96..191 overlay.
   176..183 is R128_A64_TS_PAL. The S3TC/YUV decode sub also uses words
   176 and 180 as scratch, which is safe because those families have no
   palette, and in these blocks it keeps its third scratch word in
   v31.s[2], so WM survives the sub calls of pass A and pass B. s and t
   are kept across pass A in TS, which overlays the TB/FLW/FLH words
   that mip-mapped groups do not use. TT (the t stash) and CA (pass A's
   ARGB lanes) extend the frame to 512, the largest frame the STP
   pre-index form can open. Trilinear and split blocks take the 512-byte
   frame; other single-stage textured blocks keep 480. */
#define R128_A64_SP_SOA_SLB 168 /* level-B mip slot 4H: d  */
#define R128_A64_SP_SOA_WM  184 /* 8.8 mip weight 4H: d    */
#define R128_A64_SP_SOA_TS  432 /* s stash across pass A: q */
#define R128_A64_SP_SOA_TT  480 /* t stash across pass A: q */
#define R128_A64_SP_SOA_CA  496 /* pass-A ARGB lanes: q    */
#define R128_A64_FRAME_ST3  512

/* MIN/MAG texel-filter split. The lod > 0 lane mask is stored as 4H in
   the unused gap after the dst cell spill (DC). The minify result
   reuses the CA slot, which is free once the mip blend has read it.
   The magnify rerun reloads s and t from TS/TT in mip-mapped states, or
   from the per-lane dimension slots VLW/VLH in base-level states, which
   use the row-hoisted descriptor and leave those slots free. Split
   blocks always take the 512-byte frame so the 480..511 slots exist. */
#define R128_A64_SP_SOA_MSK 472 /* lod>0 mask 4H: d        */

/* Dual-stage (sec_en) frame extension. Stage 0 leaves its combine
   output at P0 as float channels, because the interpreter passes col[]
   to stage 1 without quantizing it. The block-entry weights, rhw and ir
   are stored once so each stage can rebuild its coordinate dot
   products, and stage 1 does the pack. All of these slots sit at 512
   and above, past every per-stage slot, so they survive both stages'
   per-lane fetches. The 656-byte frame is larger than STP pre-index can
   open, so the prologue opens it with an explicit SUB SP. */
#define R128_A64_SP_SOA_P0  512 /* stage-0 out r,g,b,a: 4 q */
#define R128_A64_SP_SOA_W0  576 /* weight lanes w0/w1/w2: q */
#define R128_A64_SP_SOA_W1  592
#define R128_A64_SP_SOA_W2  608
#define R128_A64_SP_SOA_RHW 624 /* rhw lanes (persp LOD): q */
#define R128_A64_SP_SOA_IR  640 /* 1/rhw lanes: q           */
#define R128_A64_FRAME_ST2  656

/* Blend source channels. The final texture stage, or the untextured
   channel dot products, leave col[] here as float lanes for the blend
   stage, which reads the dst group and packs. The slots alias the
   coordinate words, which are free once the last combine has read its
   texel channel. Untextured blend blocks take the 480-byte frame so
   these slots exist. */
#define R128_A64_SP_SOA_SRCR R128_A64_SP_SOA_U0
#define R128_A64_SP_SOA_SRCG R128_A64_SP_SOA_U1
#define R128_A64_SP_SOA_SRCB R128_A64_SP_SOA_V0
#define R128_A64_SP_SOA_SRCA R128_A64_SP_SOA_V1

/* Untextured table fog. The Z block consumes v7/v15, so the raw zline
   double lanes are stored in the 8.8 lerp-weight slots, which
   untextured blocks never use since they run no texel pipeline. The
   fog stage derives zc again with its own clamp to [0,1], and passes the
   table indices through TFA and TFB once it has reloaded the zline
   pair. These blocks also use the SRC channel slots, so they always
   take the 480-byte frame. */
#define R128_A64_SP_SOA_TFA R128_A64_SP_SOA_WU
#define R128_A64_SP_SOA_TFB R128_A64_SP_SOA_WV

/* Alpha test present and live (r128_jit_soa_atest_on). */
static int
r128_a64_soa_atest_on(const rage128_draw_state_t *ds)
{
    return r128_jit_soa_atest_on(ds);
}

/* Cover-mask stash for textured groups that decide rx late (chroma
   key, alpha test, stencil). The loop head stores the coverage and Z
   mask here, the texture stage ANDs its reject masks into it, and
   rx0/rx1 come from the lanes that survive. A block with only a chroma
   key uses VLW when stage 0 leaves it free (single-stage base-level
   classes without split). Mip-mapped groups keep per-lane dimensions in
   VLW and base-level split groups keep s/t there, so those groups, and
   every group with alpha test or stencil, use a dedicated slot above
   the frame; r128_a64_gen_setup adds 16 bytes to the frame to match
   the offset returned here. */
static int
r128_a64_soa_cover_off(const rage128_draw_state_t *ds)
{
    int mip_on0 = r128_jit_minb_mip_on(ds->sh[0].minb, ds->sh[0].mipdis,
                                       ds->need_lod);
    int split0  = r128_jit_minb_split(ds->sh[0].minb, ds->sh[0].mipdis,
                                      ds->sh[0].mag, ds->need_lod);

    if (ds->sec_en || ds->spec_en || ds->fog_en)
        /* Dual stage: VLW (96) is per-stage working space, so it cannot
           also hold the cover mask as it does in single-stage chroma-key
           blocks. Chroma key, alpha test and stencil share the dedicated
           slot above the 656-byte frame; each ANDs into it, and
           r128_a64_gen_setup adds 16 bytes to the frame. Textured
           spec/fog blocks take the same 656-byte frame for the W0..W2
           weight stash, so their cover slot moves up with it. */
        return R128_A64_FRAME_ST2;
    if (!r128_a64_soa_atest_on(ds) && !ds->sten_on
        && !(ds->need_ck && (mip_on0 || split0)))
        return R128_A64_SP_SOA_VLW;
    /* Stencil always takes the dedicated slot: its op mask must survive
       from the loop head to the read-modify-write stage after the pack,
       whatever the stage-0 class. */
    if (ds->need_lod
        && (r128_jit_minb_tri(ds->sh[0].minb, ds->sh[0].mipdis, ds->need_lod)
            || split0))
        return R128_A64_FRAME_ST3;
    return R128_A64_FRAME_ST;
}

/* True for a dual pair over the inline budget; if such a pair reaches
   the vector loop, it was admitted with the sub-mode weights. Its block
   emits each trilinear stage's filtered pipeline as a BL subroutine and
   adds 16 bytes to the frame for the saved return address. Pairs within
   the inline budget are emitted inline, unchanged. */
static int
r128_a64_soa_dual_sub(const rage128_draw_state_t *ds)
{
    int sf  = (ds->spec_en || ds->fog_en) ? 516 : 0;
    int stw = ds->sten_on ? R128_A64_SOA_STEN_W : 0;

    return ds->sec_en
        && r128_a64_soa_stage_weight(ds, 0)
            + r128_a64_soa_stage_weight(ds, 1) + sf + stw
        > 8600;
}

/* Frame slot for the sub-mode return address, above the cover stash.
   The coordinate/gather sub cannot keep LR in v31 the way the decode
   subs do: its body calls those decode subs (emit_texel on the S3TC/YUV
   families), and the inner call's own FMOV D31, X30 would overwrite the
   outer return address. */
static int
r128_a64_soa_slr_off(const rage128_draw_state_t *ds)
{
    return R128_A64_FRAME_ST2
        + ((r128_a64_soa_atest_on(ds) || ds->need_ck || ds->sten_on)
               ? 16
               : 0);
}

/* Stencil q stash: one packed word {sbuf | sres<<8 | zres<<9} per lane,
   written by the stencil Z block and read by the op/read-modify-write
   stage after the pack. Untextured blocks use the TB/FLW/FLH words,
   which they never use otherwise; textured blocks take a dedicated slot
   above the cover stash and the sub-mode return-address slot, and
   r128_a64_gen_setup sizes the frame to match. The scalar tail's own
   stencil word (sten_off, at the base frame's top, 272 or 336) is live
   only within one pixel and never at the same time as this stash. */
static int
r128_a64_soa_sten_q_off(const rage128_draw_state_t *ds)
{
    if (!ds->tex_en)
        return R128_A64_SP_SOA_TB;
    return r128_a64_soa_cover_off(ds) + 16
        + (r128_a64_soa_dual_sub(ds) ? 16 : 0);
}

/* r3d_cmp(atest_fn, a, ref) on 4 lanes. va holds the quantized alpha
   lanes, (uint32)(col[3]*255+0.5), and is preserved; the pass mask
   (all ones per passing lane) lands in vd. vs is vector scratch, w17
   general scratch. The compares are unsigned, like the scalar
   r128_a64_cmp_inv table. Functions 0 and 7 never reach here
   (r128_jit_soa_atest_on). */
static void
r128_a64_soa_atest_mask(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                        int vd, int va, int vs)
{
    r128_a64_movz_w(e, 17, ds->atest_ref & 0xff, 0);
    r128_a64_dup_4s_w(e, vs, 17);
    /* clang-format off */
    switch (ds->atest_fn & 7) {
        case 1: r128_a64_cmhi_4s(e, vd, vs, va); break; /* a <  ref */
        case 2: r128_a64_cmhs_4s(e, vd, vs, va); break; /* a <= ref */
        case 3: r128_a64_cmeq_4s(e, vd, va, vs); break;
        case 4: r128_a64_cmhs_4s(e, vd, va, vs); break; /* a >= ref */
        case 5: r128_a64_cmhi_4s(e, vd, va, vs); break; /* a >  ref */
            /* clang-format on */
        default: /* 6 */
            r128_a64_cmeq_4s(e, vd, va, vs);
            r128_a64_not_16b(e, vd, vd);
            break;
    }
}

/* Stage-0 chroma-key compare on the nearest-texel lanes in v3 (tnear,
   taken before filtering). The mask of lanes to keep is ANDed into the
   loop's cover-mask stash, so rejected lanes drop out of the store mask
   and of the rx0/rx1 span, as the scalar loop's pixel skip does.
   Scratch v7/v14/v15, w17. */
static void
r128_a64_soa_ck_mask(r128_a64_emit_t *e, const rage128_draw_state_t *ds)
{
    if (ds->ck3d_on) {
        r128_a64_mov_w_imm32(e, 17, ds->ck3d_msk);
        r128_a64_dup_4s_w(e, 15, 17);
        r128_a64_and_16b(e, 15, 3, 15);
        r128_a64_mov_w_imm32(e, 17, ds->ck3d_clr & ds->ck3d_msk);
        r128_a64_dup_4s_w(e, 7, 17);
        r128_a64_cmeq_4s(e, 14, 15, 7);
        if (ds->ckfn == 3) /* reject on eq         */
            r128_a64_not_16b(e, 14, 14);
    }
    if (ds->ckc_on) {
        r128_a64_mov_w_imm32(e, 17, ds->ckc_msk);
        r128_a64_dup_4s_w(e, 15, 17);
        r128_a64_and_16b(e, 15, 3, 15);
        r128_a64_mov_w_imm32(e, 17, ds->ckc_clr & ds->ckc_msk);
        r128_a64_dup_4s_w(e, 7, 17);
        r128_a64_cmeq_4s(e, 15, 15, 7);
        r128_a64_not_16b(e, 15, 15); /* eq-reject only       */
        if (ds->ck3d_on)
            r128_a64_and_16b(e, 14, 14, 15);
        else
            r128_a64_orr_16b(e, 14, 15, 15);
    }
    r128_a64_ldr_q(e, 15, 31, r128_a64_soa_cover_off(ds));
    r128_a64_and_16b(e, 15, 15, 14);
    r128_a64_str_q(e, 15, 31, r128_a64_soa_cover_off(ds));
}

/* ------------------------------------------------------------------------
 * Row prologue additions: d8-d15 save, lane offsets, invs splat, 2D
 * splats for the Z quantize, and the dither Bayer add vectors. Emitted
 * after the scalar prologue, when all per-triangle registers and
 * d19..d22/v23/v24/v28 are already set. Scratch is w16/w17 as in the
 * scalar prologue, plus x25 and v3 for the lane offsets.
 * ---------------------------------------------------------------------- */
/* Row hoist for textured groups whose mip slot is fixed at compile
   time: the level descriptor is loaded once per span. The
   texture-stage descriptor words (R128_A64_TS_LW and the others) are
   free during the vector loop; the scalar tail rebuilds its own.
   Prologue-time scratch: w16/w17/w25/w28 and s26 (x26-x28 hold
   nothing until the loop starts). */
static void
r128_a64_emit_soa_tex_hoist(r128_a64_emit_t *e, const rage128_draw_state_t *ds)
{
    const r3d_stage_hdr_t *h      = &ds->sh[0];
    int                    sd_off = (int) offsetof(r3d_texctx_t, sd0);
    int                    sl_off = sd_off + (int) offsetof(r3d_stage_desc_t, slot);

    r128_a64_ldr_x(e, 16, 0, (int) offsetof(r128_jit_tri_t, texctx));
    if (ds->need_lod && !h->mipdis && h->minb >= 2) {
        /* per-lane slots: only the per-stage palette can hoist */
        if (h->dt == 1 || h->dt == 2) {
            r128_a64_ldr_x(e, 25, 16, sd_off + (int) offsetof(r3d_stage_desc_t, pal));
            r128_a64_str_x(e, 25, 31, R128_A64_TS_PAL);
        }
        return;
    }
    /* fixed level (top): base-level blocks, including need_lod states
       where only the filter split uses the LOD */
    r128_a64_add_x_imm(e, 17, 16,
                       sl_off + h->top * (int) sizeof(struct r3d_slot_desc_t));
    r128_a64_ldp_w(e, 25, 28, 17, 0); /* lw, lh          */
    r128_a64_str_w(e, 25, 31, R128_A64_TS_LW);
    r128_a64_str_w(e, 28, 31, R128_A64_TS_LH);
    r128_a64_ucvtf_s_w(e, 26, 25);
    r128_a64_str_s(e, 26, 31, R128_A64_SP_SOA_FLW);
    r128_a64_ucvtf_s_w(e, 26, 28);
    r128_a64_str_s(e, 26, 31, R128_A64_SP_SOA_FLH);
    r128_a64_ldr_w(e, 25, 17, 16); /* arena base      */
    r128_a64_str_w(e, 25, 31, R128_A64_TS_BASE);
    r128_a64_ldr_w(e, 25, 17, 20); /* arena mask      */
    r128_a64_str_w(e, 25, 31, R128_A64_TS_MASK);
    r128_a64_ldr_x(e, 25, 17, 8); /* level texbase   */
    r128_a64_str_x(e, 25, 31, R128_A64_SP_SOA_TB);
    if (h->dt == 1 || h->dt == 2) {
        r128_a64_ldr_x(e, 25, 16, sd_off + (int) offsetof(r3d_stage_desc_t, pal));
        r128_a64_str_x(e, 25, 31, R128_A64_TS_PAL);
    }
}

static void
r128_a64_emit_soa_prologue(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                           int tex)
{
    int dith_on = ds->dither && ds->dst_dt != 6;

    if (tex) {
        /* The texture prologue has already saved d8-d15 at
           R128_A64_SP_D8. The lane offsets below overwrite its constant
           registers v8-v13, which the scalar loop head loads again.
           Dual-stage blocks skip the row hoist: the two stages share
           the texture-stage descriptor words, so each stage writes its
           own descriptor per group instead. */
        if (!ds->sec_en)
            r128_a64_emit_soa_tex_hoist(e, ds);
    } else {
        r128_a64_stp_d_sp(e, 8, 9, R128_A64_SP_TRI);
        r128_a64_stp_d_sp(e, 10, 11, R128_A64_SP_TRI + 16);
        r128_a64_stp_d_sp(e, 12, 13, R128_A64_SP_ROW + 16);
        r128_a64_stp_d_sp(e, 14, 15, R128_A64_SP_D19 + 16);
    }

    /* invs splat (lane 0 keeps the scalar value) */
    r128_a64_dup_4s_lane0(e, 1, 1);

    /* per-edge lane offsets {0,dx} / {2dx,3dx} from x7/x8/x9 */
    for (int j = 0; j < 3; j++) {
        int lo = 8 + j * 2, hi = lo + 1;

        r128_a64_dup_2d_x(e, 25, 7 + j); /* {dx,dx}        */
        r128_a64_movi_4s_zero(e, 3);
        r128_a64_ins_elem_d(e, 3, 1, 25, 0); /* {0,dx}         */
        r128_a64_add_2d(e, 25, 25, 25);      /* {2dx,2dx}      */
        r128_a64_add_2d(e, hi, 3, 25);       /* {2dx,3dx}      */
        r128_a64_orr_16b(e, lo, 3, 3);
    }

    if (ds->z_en) {
        /* 2D splats from the scalar double constants (FP-domain dup) */
        r128_a64_dup_2d_lane(e, 16, 21, 0); /* (double)zmax */
        r128_a64_dup_2d_lane(e, 17, 22, 0); /* 0.5          */
        r128_a64_dup_2d_lane(e, 18, 20, 0); /* 1.0          */
    }

    if (dith_on) {
        /* Row bayer adds per lane: lane k threshold is byte (px0+k)&3 of
           the packed row word (w6); px += 4 keeps the phase invariant. */
        for (int k = 0; k < 4; k++) {
            r128_a64_add_w_imm(e, 16, 12, k);
            r128_a64_and_w_bitmask(e, 16, 16, 3);
            r128_a64_lsl_w(e, 16, 16, 3);
            r128_a64_lsrv_w(e, 17, 6, 16);
            r128_a64_and_w_bitmask(e, 17, 17, 0xff);
            r128_a64_ins_s_w(e, 29, k, 17);
        }
        /* Quantization discards two green bits in 565 and three RGB
           bits in 1555, so the Bayer adds use the corresponding shifts.
           Four-bit 4444 channels consume the unshifted thresholds. */
        if (ds->dst_dt == 4) {
            r128_a64_ushr_4s(e, 30, 29, 2);
            r128_a64_ushr_4s(e, 29, 29, 1);
        } else if (ds->dst_dt == 3)
            r128_a64_ushr_4s(e, 29, 29, 1);
    }
}

static void
r128_a64_emit_soa_epilogue(r128_a64_emit_t *e)
{
    r128_a64_ldp_d_sp(e, 8, 9, R128_A64_SP_TRI);
    r128_a64_ldp_d_sp(e, 10, 11, R128_A64_SP_TRI + 16);
    r128_a64_ldp_d_sp(e, 12, 13, R128_A64_SP_ROW + 16);
    r128_a64_ldp_d_sp(e, 14, 15, R128_A64_SP_D19 + 16);
}

/* Group address resolve + bounds: computes the lane-0 cell into xcell
   and bails (to the scalar loop) unless all four lanes are contiguous
   and in bounds. ptr_x/base_w/lim_w describe the staged arena
   (cptr/c_base/c_lim or zptr/z_base/z_lim); row_w is drow/zrow. */
static void
r128_a64_soa_group_addr(r128_a64_emit_t *e, int ptr_x, int base_w, int lim_w,
                        int row_w, int bppsh, int bpp, int xcell,
                        int *bails, int *nbail)
{
    int b_vram, b_res;

    r128_a64_add_w_lsl(e, 25, row_w, 12, bppsh); /* addr0            */
    b_vram = r128_a64_cbz_x(e, ptr_x);
    r128_a64_sub_w_reg(e, 28, 25, base_w);  /* off0             */
    r128_a64_add_w_imm(e, 17, 28, 4 * bpp); /* end              */
    r128_a64_cmp_w_reg(e, 17, lim_w);
    bails[(*nbail)++] = r128_a64_bcond(e, A64_HI); /* out of arena     */
    r128_a64_cmp_w_reg(e, 17, 28);
    bails[(*nbail)++] = r128_a64_bcond(e, A64_LO); /* 32-bit wrap      */
    r128_a64_add_x_uxtw(e, xcell, ptr_x, 28);
    b_res = r128_a64_b(e);
    r128_a64_patch19(e, b_vram, r128_a64_here(e));
    /* vram: bail if the group straddles the wrap window (per-lane
       masked addresses would not be contiguous) */
    r128_a64_add_w_imm(e, 17, 25, 4 * bpp - 1);
    r128_a64_eor_w_reg(e, 17, 17, 25);
    r128_a64_mvn_w(e, 28, 11);
    r128_a64_tst_w_reg(e, 17, 28);
    bails[(*nbail)++] = r128_a64_bcond(e, A64_NE);
    r128_a64_and_w_reg(e, 28, 25, 11);
    r128_a64_add_x_uxtw(e, xcell, 10, 28);
    r128_a64_patch26(e, b_res, r128_a64_here(e));
}

/* One SoA color channel: col = w0*vXa[ch] + w1*vXb[ch] + w2*vXc[ch]
   (left-associated like the C), scaled/quantized, and left in vd as
   uint32 lanes. Dither adds the format's Bayer vector and saturates at
   255 to match the modeled byte quantization. w0/w1/w2 live in
   vw0/vw1/vw2; vt is a scratch distinct from all of them. */
static void
r128_a64_soa_channel(r128_a64_emit_t *e, int vd, int vt, int vw0, int vw1,
                     int vw2, int ch, int dith_on, int bayv)
{
    r128_a64_fmul_4s_elem(e, vd, vw0, 4, ch);
    r128_a64_fmul_4s_elem(e, vt, vw1, 5, ch);
    r128_a64_fadd_4s(e, vd, vd, vt);
    r128_a64_fmul_4s_elem(e, vt, vw2, 6, ch);
    r128_a64_fadd_4s(e, vd, vd, vt);
    r128_a64_fmul_4s(e, vd, vd, 23);
    r128_a64_fadd_4s(e, vd, vd, 24);
    r128_a64_fcvtzu_4s(e, vd, vd);
    if (dith_on) {
        r128_a64_add_4s(e, vd, vd, bayv);
        r128_a64_umin_4s(e, vd, vd, 28);
    }
}

/* r3d_tex_wrap on 4 lanes, mode fixed at compile time. The coordinate
   vector in vc (signed integer lanes) is replaced; the dimension is
   loaded from the hoisted integer slot at lw_off. vs1/vs2 are vector
   scratch, w17 general scratch. Each case gives the same value as the
   scalar r128_a64_emit_wrap. CLAMP: SMAX/SMIN equal the CSEL chain,
   c == 0 included. BORDER: ORing in the CMHS mask gives the same -1
   (all ones). MIRROR: m ^ (2n-1) equals the C's 2n-1-m because n is a
   power of two and m < 2n. */
static void
r128_a64_soa_wrap(r128_a64_emit_t *e, uint32_t mode, int vc, int lw_off,
                  int vs1, int vs2)
{
    r128_a64_ldr_w(e, 17, 31, lw_off);
    switch (mode & 3) {
        case 0: /* WRAP: c & (n-1) */
            r128_a64_sub_w_imm(e, 17, 17, 1);
            r128_a64_dup_4s_w(e, vs1, 17);
            r128_a64_and_16b(e, vc, vc, vs1);
            break;
        case 1: /* MIRROR: m = c & (2n-1); m < n ? m : m ^ (2n-1) */
            r128_a64_dup_4s_w(e, vs2, 17);
            r128_a64_lsl_w(e, 17, 17, 1);
            r128_a64_sub_w_imm(e, 17, 17, 1);
            r128_a64_dup_4s_w(e, vs1, 17);
            r128_a64_and_16b(e, vc, vc, vs1);
            r128_a64_eor_16b(e, vs1, vc, vs1);
            r128_a64_cmgt_4s(e, vs2, vs2, vc);
            r128_a64_bsl_16b(e, vs2, vc, vs1);
            r128_a64_orr_16b(e, vc, vs2, vs2);
            break;
        case 3: /* BORDER: (unsigned) c >= n -> -1 */
            r128_a64_dup_4s_w(e, vs1, 17);
            r128_a64_cmhs_4s(e, vs1, vc, vs1);
            r128_a64_orr_16b(e, vc, vc, vs1);
            break;
        default: /* CLAMP: c<0 -> 0; c>=n -> n-1 */
            r128_a64_movi_4s_zero(e, vs1);
            r128_a64_smax_4s(e, vc, vc, vs1);
            r128_a64_sub_w_imm(e, 17, 17, 1);
            r128_a64_dup_4s_w(e, vs1, 17);
            r128_a64_smin_4s(e, vc, vc, vs1);
            break;
    }
}

/* r3d_tex_wrap on 4 lanes with a per-lane dimension vector in vn, for
   mip-mapped states where each lane samples its own level. Gives the
   same values as r128_a64_soa_wrap. vn is preserved because the caller
   wraps c0 and c1 against the same dimensions. vs1/vs2 scratch. */
static void
r128_a64_soa_wrap_v(r128_a64_emit_t *e, uint32_t mode, int vc, int vn,
                    int vs1, int vs2)
{
    switch (mode & 3) {
        case 0: /* WRAP: c & (n-1) */
            r128_a64_movi_4s_imm8(e, vs1, 1);
            r128_a64_sub_4s(e, vs1, vn, vs1);
            r128_a64_and_16b(e, vc, vc, vs1);
            break;
        case 1: /* MIRROR: m = c & (2n-1); m < n ? m : m ^ (2n-1) */
            r128_a64_shl_4s(e, vs1, vn, 1);
            r128_a64_movi_4s_imm8(e, vs2, 1);
            r128_a64_sub_4s(e, vs1, vs1, vs2);
            r128_a64_and_16b(e, vc, vc, vs1);
            r128_a64_eor_16b(e, vs1, vc, vs1);
            r128_a64_cmgt_4s(e, vs2, vn, vc);
            r128_a64_bsl_16b(e, vs2, vc, vs1);
            r128_a64_orr_16b(e, vc, vs2, vs2);
            break;
        case 3: /* BORDER: (unsigned) c >= n -> -1 */
            r128_a64_cmhs_4s(e, vs1, vc, vn);
            r128_a64_orr_16b(e, vc, vc, vs1);
            break;
        default: /* CLAMP: c<0 -> 0; c>=n -> n-1 */
            r128_a64_movi_4s_zero(e, vs1);
            r128_a64_smax_4s(e, vc, vc, vs1);
            r128_a64_movi_4s_imm8(e, vs1, 1);
            r128_a64_sub_4s(e, vs1, vn, vs1);
            r128_a64_smin_4s(e, vc, vc, vs1);
            break;
    }
}

/* Per-pixel LOD on 4 lanes (the need_lod block of rage128_texstage_run).
   Each lane runs the scalar float sequence operation for operation,
   including the bit manipulation in r3d_log2f_fast, which is plain
   integer lane math. st selects the texel dimensions and the gradient
   set: on stage 1 with ds->sec_sel set (R128_SEC_SELECT_SEC_ST), the
   second coordinate set's gradients. rhw_off is where wp is stored
   (U0 single-stage, RHW dual). In: sp in v3, tp in v26 (both
   preserved), x25 = texctx. Out: lod lanes in v14. Scratch
   v7/v15/v25/v27, w17. */
static void
r128_a64_emit_soa_lod(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                      int st, int rhw_off)
{
    int   sel  = st && ds->sec_sel;
    int   gs_x = (int) (sel ? offsetof(r3d_texctx_t, dS2dx) : offsetof(r3d_texctx_t, dSdx));
    int   gs_y = (int) (sel ? offsetof(r3d_texctx_t, dS2dy) : offsetof(r3d_texctx_t, dSdy));
    int   gt_x = (int) (sel ? offsetof(r3d_texctx_t, dT2dx) : offsetof(r3d_texctx_t, dTdx));
    int   gt_y = (int) (sel ? offsetof(r3d_texctx_t, dT2dy) : offsetof(r3d_texctx_t, dTdy));
    int   gw_x = (int) offsetof(r3d_texctx_t, dWdx);
    int   gw_y = (int) offsetof(r3d_texctx_t, dWdy);
    float texw = st ? ds->texw1 : ds->texw0;
    float texh = st ? ds->texh1 : ds->texh0;

    if (ds->do_persp) {
        /* iw2 = (wp != 0) ? 1 / (wp * wp) : 0 -> v15; wp -> v25 */
        r128_a64_ldr_q(e, 25, 31, rhw_off);
        r128_a64_fmul_4s(e, 15, 25, 25);
        r128_a64_mov_w_fbits(e, 17, 1.0f);
        r128_a64_dup_4s_w(e, 7, 17);
        r128_a64_fdiv_4s(e, 15, 7, 15);
        r128_a64_fcmeq0_4s(e, 14, 25);
        r128_a64_movi_4s_zero(e, 7);
        r128_a64_bsl_16b(e, 14, 7, 15);
        r128_a64_orr_16b(e, 15, 14, 14); /* iw2                  */
        /* dsx = (dSdx*wp - sp*dWdx) * iw2 -> v27 */
        r128_a64_ldr_s(e, 27, 25, gs_x);
        r128_a64_dup_4s_lane0(e, 27, 27);
        r128_a64_fmul_4s(e, 27, 27, 25);
        r128_a64_ldr_s(e, 14, 25, gw_x);
        r128_a64_dup_4s_lane0(e, 14, 14);
        r128_a64_fmul_4s(e, 14, 3, 14);
        r128_a64_fsub_4s(e, 27, 27, 14);
        r128_a64_fmul_4s(e, 27, 27, 15);
        /* dtx -> v14 */
        r128_a64_ldr_s(e, 14, 25, gt_x);
        r128_a64_dup_4s_lane0(e, 14, 14);
        r128_a64_fmul_4s(e, 14, 14, 25);
        r128_a64_ldr_s(e, 7, 25, gw_x);
        r128_a64_dup_4s_lane0(e, 7, 7);
        r128_a64_fmul_4s(e, 7, 26, 7);
        r128_a64_fsub_4s(e, 14, 14, 7);
        r128_a64_fmul_4s(e, 14, 14, 15);
    } else {
        r128_a64_ldr_s(e, 27, 25, gs_x);
        r128_a64_dup_4s_lane0(e, 27, 27); /* dsx                  */
        r128_a64_ldr_s(e, 14, 25, gt_x);
        r128_a64_dup_4s_lane0(e, 14, 14); /* dtx                  */
    }
    /* ax2 = (dsx*texw)^2 + (dtx*texh)^2 -> v27 */
    r128_a64_mov_w_fbits(e, 17, texw);
    r128_a64_dup_4s_w(e, 7, 17);
    r128_a64_fmul_4s(e, 27, 27, 7);
    r128_a64_mov_w_fbits(e, 17, texh);
    r128_a64_dup_4s_w(e, 7, 17);
    r128_a64_fmul_4s(e, 14, 14, 7);
    r128_a64_fmul_4s(e, 27, 27, 27);
    r128_a64_fmul_4s(e, 14, 14, 14);
    r128_a64_fadd_4s(e, 27, 27, 14);
    if (ds->do_persp) {
        /* dsy -> v14 */
        r128_a64_ldr_s(e, 14, 25, gs_y);
        r128_a64_dup_4s_lane0(e, 14, 14);
        r128_a64_fmul_4s(e, 14, 14, 25);
        r128_a64_ldr_s(e, 7, 25, gw_y);
        r128_a64_dup_4s_lane0(e, 7, 7);
        r128_a64_fmul_4s(e, 7, 3, 7);
        r128_a64_fsub_4s(e, 14, 14, 7);
        r128_a64_fmul_4s(e, 14, 14, 15);
        /* dty -> v7 (wp and iw2 die here) */
        r128_a64_ldr_s(e, 7, 25, gt_y);
        r128_a64_dup_4s_lane0(e, 7, 7);
        r128_a64_fmul_4s(e, 7, 7, 25);
        r128_a64_ldr_s(e, 25, 25, gw_y);
        r128_a64_dup_4s_lane0(e, 25, 25);
        r128_a64_fmul_4s(e, 25, 26, 25);
        r128_a64_fsub_4s(e, 7, 7, 25);
        r128_a64_fmul_4s(e, 7, 7, 15);
    } else {
        r128_a64_ldr_s(e, 14, 25, gs_y);
        r128_a64_dup_4s_lane0(e, 14, 14); /* dsy                  */
        r128_a64_ldr_s(e, 7, 25, gt_y);
        r128_a64_dup_4s_lane0(e, 7, 7); /* dty                  */
    }
    /* ay2 -> v14 */
    r128_a64_mov_w_fbits(e, 17, texw);
    r128_a64_dup_4s_w(e, 25, 17);
    r128_a64_fmul_4s(e, 14, 14, 25);
    r128_a64_mov_w_fbits(e, 17, texh);
    r128_a64_dup_4s_w(e, 25, 17);
    r128_a64_fmul_4s(e, 7, 7, 25);
    r128_a64_fmul_4s(e, 14, 14, 14);
    r128_a64_fmul_4s(e, 7, 7, 7);
    r128_a64_fadd_4s(e, 14, 14, 7);
    /* rho2 = ax2 > ay2 ? ax2 : ay2 -> v15 */
    r128_a64_fcmgt_4s(e, 15, 27, 14);
    r128_a64_bsl_16b(e, 15, 27, 14);
    /* log2fast on every lane (bit ops are total; rho2 <= 0 lanes are
       discarded by the select below, like the scalar b.le skip) */
    r128_a64_ushr_4s(e, 7, 15, 23);
    r128_a64_movi_4s_imm8(e, 27, 255);
    r128_a64_and_16b(e, 7, 7, 27);
    r128_a64_scvtf_4s(e, 7, 7);
    r128_a64_mov_w_fbits(e, 17, 127.0f);
    r128_a64_dup_4s_w(e, 27, 17);
    r128_a64_fsub_4s(e, 7, 7, 27); /* e                    */
    r128_a64_mov_w_imm32(e, 17, 0x007fffffu);
    r128_a64_dup_4s_w(e, 25, 17);
    r128_a64_and_16b(e, 25, 15, 25);
    r128_a64_mov_w_fbits(e, 17, 1.0f); /* 0x3f800000           */
    r128_a64_dup_4s_w(e, 27, 17);
    r128_a64_orr_16b(e, 25, 25, 27); /* m                    */
    r128_a64_mov_w_fbits(e, 17, 0.153918478f);
    r128_a64_dup_4s_w(e, 27, 17);
    r128_a64_fmul_4s(e, 27, 25, 27); /* m*c3                 */
    r128_a64_mov_w_fbits(e, 17, -1.029521946f);
    r128_a64_dup_4s_w(e, 14, 17);
    r128_a64_fadd_4s(e, 27, 14, 27);
    r128_a64_fmul_4s(e, 27, 25, 27);
    r128_a64_mov_w_fbits(e, 17, 3.010783972f);
    r128_a64_dup_4s_w(e, 14, 17);
    r128_a64_fadd_4s(e, 27, 14, 27);
    r128_a64_fmul_4s(e, 27, 25, 27);
    r128_a64_mov_w_fbits(e, 17, -2.133847707f);
    r128_a64_dup_4s_w(e, 14, 17);
    r128_a64_fadd_4s(e, 27, 14, 27);
    r128_a64_fadd_4s(e, 7, 7, 27); /* log2                 */
    r128_a64_mov_w_fbits(e, 17, 0.5f);
    r128_a64_dup_4s_w(e, 27, 17);
    r128_a64_fmul_4s(e, 7, 27, 7);
    r128_a64_mov_w_fbits(e, 17, ds->lod_bias);
    r128_a64_dup_4s_w(e, 27, 17);
    r128_a64_fadd_4s(e, 7, 7, 27);
    r128_a64_mov_w_fbits(e, 17, -1000.0f);
    r128_a64_dup_4s_w(e, 27, 17);
    r128_a64_fcmgt0_4s(e, 14, 15);
    r128_a64_bsl_16b(e, 14, 7, 27); /* lod                  */
}

/* Per-lane mip slot select (mip-nearest, minb 2/3), the nearest-mip
   branch of r3d_tex_sample lane by lane: lod <= 0 samples the base slot
   (top); lod > 0 samples top - (int)(lvl + 0.5) with lvl = min(lod, top),
   which is always in [0, top]. In: lod in v14, x25 = texctx. Out: slot
   indices packed 4H at SL. Scratch v7/v15/v25, w17. */
static void
r128_a64_emit_soa_slots(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                        int st)
{
    const r3d_stage_hdr_t *h = &ds->sh[st];

    r128_a64_mov_w_fbits(e, 17, (float) h->top);
    r128_a64_dup_4s_w(e, 25, 17);
    r128_a64_fcmgt_4s(e, 15, 14, 25);
    r128_a64_bsl_16b(e, 15, 25, 14); /* lvl = min(lod, top)  */
    r128_a64_mov_w_fbits(e, 17, 0.5f);
    r128_a64_dup_4s_w(e, 25, 17);
    r128_a64_fadd_4s(e, 15, 15, 25);
    r128_a64_fcvtzs_4s(e, 15, 15); /* (int)(lvl + 0.5)     */
    r128_a64_movz_w(e, 17, (uint32_t) h->top, 0);
    r128_a64_dup_4s_w(e, 25, 17);
    r128_a64_sub_4s(e, 15, 25, 15); /* top - l              */
    r128_a64_fcmgt0_4s(e, 7, 14);   /* minify lanes         */
    r128_a64_bsl_16b(e, 7, 15, 25); /* else the base slot   */
    r128_a64_xtn_4h(e, 7, 7);
    r128_a64_str_d(e, 7, 31, R128_A64_SP_SOA_SL);
}

/* Trilinear per-lane slot pair and 8.8 blend weight (minb 4/5), the
   mip_linear branch of r3d_tex_sample: lvl = min(lod, top);
   l0 = floor(lvl); f = lvl - l0; slotA = top - l0;
   slotB = max(slotA - 1, 0); w = (u32)(f * 256 + 0.5). Magnify lanes
   (lod <= 0) set slotA = slotB = top. The packed lerp of two equal
   inputs returns that input exactly ((v*256 + 0x80) >> 8 == v), so the
   weight in those lanes does not matter. In: lod in v14, x25 = texctx.
   Out: 4H slot vectors at SL (A) and SLB (B), weight 4H at WM. Scratch
   v7/v14/v15/v25/v27, w17. */
static void
r128_a64_emit_soa_slots_tri(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                            int st)
{
    const r3d_stage_hdr_t *h = &ds->sh[st];

    r128_a64_mov_w_fbits(e, 17, (float) h->top);
    r128_a64_dup_4s_w(e, 25, 17);
    r128_a64_fcmgt_4s(e, 15, 14, 25);
    r128_a64_bsl_16b(e, 15, 25, 14); /* lvl = min(lod, top)  */
    r128_a64_fcvtms_4s(e, 7, 15);    /* l0 = (int)floorf     */
    r128_a64_scvtf_4s(e, 27, 7);
    r128_a64_fsub_4s(e, 15, 15, 27); /* f = lvl - (float)l0  */
    r128_a64_mov_w_fbits(e, 17, 256.0f);
    r128_a64_dup_4s_w(e, 27, 17);
    r128_a64_fmul_4s(e, 15, 15, 27);
    r128_a64_mov_w_fbits(e, 17, 0.5f);
    r128_a64_dup_4s_w(e, 27, 17);
    r128_a64_fadd_4s(e, 15, 15, 27);
    r128_a64_fcvtzu_4s(e, 15, 15); /* (u32)(f*256 + 0.5)   */
    r128_a64_xtn_4h(e, 15, 15);
    r128_a64_str_d(e, 15, 31, R128_A64_SP_SOA_WM);
    r128_a64_movz_w(e, 17, (uint32_t) h->top, 0);
    r128_a64_dup_4s_w(e, 25, 17);
    r128_a64_sub_4s(e, 27, 25, 7);   /* slotA = top - l0     */
    r128_a64_fcmgt0_4s(e, 15, 14);   /* minify lanes         */
    r128_a64_orr_16b(e, 14, 15, 15); /* mask copy (lod dead) */
    r128_a64_bsl_16b(e, 14, 27, 25); /* A: else base slot    */
    r128_a64_xtn_4h(e, 14, 14);
    r128_a64_str_d(e, 14, 31, R128_A64_SP_SOA_SL);
    r128_a64_movi_4s_imm8(e, 7, 1);
    r128_a64_sub_4s(e, 27, 27, 7); /* slotA - 1            */
    r128_a64_movi_4s_zero(e, 7);
    r128_a64_smax_4s(e, 27, 27, 7);  /* clamp at 0           */
    r128_a64_bsl_16b(e, 15, 27, 25); /* B: else base slot    */
    r128_a64_xtn_4h(e, 15, 15);
    r128_a64_str_d(e, 15, 31, R128_A64_SP_SOA_SLB);
}

/* Per-lane level dimensions from the slot vector at frame offset slo:
   lw and lh integer vectors to VLW and VLH. In: x25 = texctx. Scratch
   v25/v27, w17/w28/w30, x17. */
static void
r128_a64_emit_soa_dims(r128_a64_emit_t *e, int slo, int st)
{
    int sd_off = (int) (st ? offsetof(r3d_texctx_t, sd1)
                           : offsetof(r3d_texctx_t, sd0));
    int sl_off = sd_off + (int) offsetof(r3d_stage_desc_t, slot);
    int k;

    for (k = 0; k < 4; k++) {
        r128_a64_add_x_imm(e, 17, 25, sl_off);
        r128_a64_ldrh(e, 28, 31, slo + 2 * k);
        r128_a64_movz_w(e, 30, (uint32_t) sizeof(struct r3d_slot_desc_t), 0);
        r128_a64_madd_w(e, 28, 28, 30, 31);
        r128_a64_add_x_uxtw(e, 17, 17, 28);
        r128_a64_ldp_w(e, 28, 30, 17, 0); /* lw, lh               */
        r128_a64_ins_s_w(e, 25, k, 28);
        r128_a64_ins_s_w(e, 27, k, 30);
    }
    r128_a64_str_q(e, 25, 31, R128_A64_SP_SOA_VLW);
    r128_a64_str_q(e, 27, 31, R128_A64_SP_SOA_VLH);
}

/* Per-lane level descriptor at fetch time: the R128_A64_TS_LW, _BASE
   and _MASK words and x28 = texbase, from the stage's slot[] entry
   named by lane k of the slot vector at slo. Emitted once per lane; the
   lane's corner fetches share it. Scratch w29/w30/x17, which emit_texel
   overwrites anyway. */
static void
r128_a64_soa_lane_desc(r128_a64_emit_t *e, int slo, int k, int st)
{
    int sd_off = (int) (st ? offsetof(r3d_texctx_t, sd1)
                           : offsetof(r3d_texctx_t, sd0));
    int sl_off = sd_off + (int) offsetof(r3d_stage_desc_t, slot);

    r128_a64_ldr_x(e, 17, 0, (int) offsetof(r128_jit_tri_t, texctx));
    r128_a64_add_x_imm(e, 17, 17, sl_off);
    r128_a64_ldrh(e, 29, 31, slo + 2 * k);
    r128_a64_movz_w(e, 30, (uint32_t) sizeof(struct r3d_slot_desc_t), 0);
    r128_a64_madd_w(e, 29, 29, 30, 31);
    r128_a64_add_x_uxtw(e, 17, 17, 29);
    r128_a64_ldr_w(e, 29, 17, 0);
    r128_a64_str_w(e, 29, 31, R128_A64_TS_LW);
    r128_a64_ldr_w(e, 29, 17, 16);
    r128_a64_str_w(e, 29, 31, R128_A64_TS_BASE);
    r128_a64_ldr_w(e, 29, 17, 20);
    r128_a64_str_w(e, 29, 31, R128_A64_TS_MASK);
    r128_a64_ldr_x(e, 28, 17, 8);
}

/* r3d_lerp_packed on 4 lanes with the per-lane 8.8 weight already in
   vw (4S): vx = lerp(vx, vy, w). Integer math, exact. Uses five
   scratch vectors. The masks M (0x00ff00ff) and K (0x00800080) are each
   rebuilt with a single MOVI Vd.8H, #imm8 (#0xff and #0x80). */
static void
r128_a64_soa_lerp_vw(r128_a64_emit_t *e, int vx, int vy,
                     int vw, int viw, int vm, int t1, int t2)
{
    r128_a64_movi_4s_lsl8(e, viw, 1); /* 256                       */
    r128_a64_sub_4s(e, viw, viw, vw); /* iw = 256 - w              */
    r128_a64_movi_8h(e, vm, 0xff);    /* M                         */
    r128_a64_and_16b(e, t1, vx, vm);  /* rb: (x & M) * iw          */
    r128_a64_mul_4s(e, t1, t1, viw);
    r128_a64_and_16b(e, t2, vy, vm);
    r128_a64_mla_4s(e, t1, t2, vw); /*   + (y & M) * w           */
    r128_a64_movi_8h(e, t2, 0x80);  /* K                         */
    r128_a64_add_4s(e, t1, t1, t2);
    r128_a64_ushr_4s(e, t1, t1, 8);
    r128_a64_and_16b(e, t1, t1, vm); /* rb result                 */
    r128_a64_ushr_4s(e, t2, vx, 8);  /* ag plane                  */
    r128_a64_and_16b(e, t2, t2, vm);
    r128_a64_mul_4s(e, t2, t2, viw);
    r128_a64_ushr_4s(e, vx, vy, 8); /* vy's ag (vx now free)     */
    r128_a64_and_16b(e, vx, vx, vm);
    r128_a64_mla_4s(e, t2, vx, vw);
    r128_a64_movi_8h(e, vx, 0x80);
    r128_a64_add_4s(e, t2, t2, vx);
    r128_a64_ushr_4s(e, t2, t2, 8);
    r128_a64_and_16b(e, t2, t2, vm);
    r128_a64_shl_4s(e, t2, t2, 8);
    r128_a64_orr_16b(e, vx, t1, t2);
}

/* Same with the weight vector loaded from the frame (bilinear WU/WV). */
static void
r128_a64_soa_lerp(r128_a64_emit_t *e, int vx, int vy, int w_off,
                  int vw, int viw, int vm, int t1, int t2)
{
    r128_a64_ldr_q(e, vw, 31, w_off);
    r128_a64_soa_lerp_vw(e, vx, vy, vw, viw, vm, t1, t2);
}

/* One quantized output channel from a combined float vector: *255 +
   0.5, unsigned convert, optional 565 dither, field shifts, then OR into
   the pack accumulator (or start it). The quantize and dither steps are
   the same as in the untextured r128_a64_soa_channel. */
static void
r128_a64_soa_quant_pack(r128_a64_emit_t *e, int vd, int acc, int first,
                        int pre_shr, int shl, int dith_on, int bayv)
{
    r128_a64_fmul_4s(e, vd, vd, 23);
    r128_a64_fadd_4s(e, vd, vd, 24);
    r128_a64_fcvtzu_4s(e, vd, vd);
    if (dith_on) {
        r128_a64_add_4s(e, vd, vd, bayv);
        r128_a64_umin_4s(e, vd, vd, 28);
    }
    if (pre_shr)
        r128_a64_ushr_4s(e, vd, vd, pre_shr);
    if (shl)
        r128_a64_shl_4s(e, vd, vd, shl);
    if (first)
        r128_a64_orr_16b(e, acc, vd, vd);
    else
        r128_a64_orr_16b(e, acc, acc, vd);
}

/* One texture coordinate axis: quantize s (v3) or t (v26) against the
   level dimensions (per-lane vectors when perlane, else the hoisted
   values) into the frame coordinate slots: c0 alone for nearest, or
   c0, c1 and the 8.8 weight for bilinear. Per lane,
   fx = r3d_texcoord_fx(coord * (float)dim). */
static void
r128_a64_soa_tex_axis(r128_a64_emit_t *e, const r3d_stage_hdr_t *h,
                      int axis, int perlane, int linear)
{
    int      vc   = axis ? 26 : 3;
    int      flo  = axis ? R128_A64_SP_SOA_FLH : R128_A64_SP_SOA_FLW;
    int      dlo  = axis ? R128_A64_TS_LH : R128_A64_TS_LW;
    int      vlo  = axis ? R128_A64_SP_SOA_VLH : R128_A64_SP_SOA_VLW;
    int      c0o  = axis ? R128_A64_SP_SOA_V0 : R128_A64_SP_SOA_U0;
    int      c1o  = axis ? R128_A64_SP_SOA_V1 : R128_A64_SP_SOA_U1;
    int      wo   = axis ? R128_A64_SP_SOA_WV : R128_A64_SP_SOA_WU;
    uint32_t mode = axis ? h->clamp_t : h->clamp_s;

    if (perlane) {
        /* per-lane (float)dim from the gathered level dims */
        r128_a64_ldr_q(e, 15, 31, vlo);
        r128_a64_ucvtf_4s(e, 15, 15);
    } else {
        r128_a64_ldr_s(e, 15, 31, flo);
        r128_a64_dup_4s_lane0(e, 15, 15);
    }
    r128_a64_fmul_4s(e, vc, vc, 15);
    r128_a64_mov_w_fbits(e, 17, 4096.0f);
    r128_a64_dup_4s_w(e, 15, 17);
    r128_a64_fmul_4s(e, vc, vc, 15);
    r128_a64_frintn_4s(e, vc, vc);
    r128_a64_mov_w_fbits(e, 17, 1.0f / 4096.0f);
    r128_a64_dup_4s_w(e, 15, 17);
    r128_a64_fmul_4s(e, vc, vc, 15); /* fx                   */
    if (!linear) {
        r128_a64_fcvtms_4s(e, vc, vc);
        if (perlane) {
            r128_a64_ldr_q(e, 14, 31, vlo);
            r128_a64_soa_wrap_v(e, mode, vc, 14, 15, 25);
        } else
            r128_a64_soa_wrap(e, mode, vc, dlo, 15, 25);
        r128_a64_str_q(e, vc, 31, c0o);
    } else {
        /* fu = fx - 0.5; c0 = floor(fu); w = (u32)((fu-c0)*256+.5) */
        r128_a64_mov_w_fbits(e, 17, 0.5f);
        r128_a64_dup_4s_w(e, 15, 17);
        r128_a64_fsub_4s(e, vc, vc, 15); /* fu                   */
        r128_a64_fcvtms_4s(e, 7, vc);    /* raw c0               */
        r128_a64_scvtf_4s(e, 15, 7);
        r128_a64_fsub_4s(e, vc, vc, 15); /* frac                 */
        r128_a64_mov_w_fbits(e, 17, 256.0f);
        r128_a64_dup_4s_w(e, 15, 17);
        r128_a64_fmul_4s(e, vc, vc, 15);
        r128_a64_mov_w_fbits(e, 17, 0.5f);
        r128_a64_dup_4s_w(e, 15, 17);
        r128_a64_fadd_4s(e, vc, vc, 15);
        r128_a64_fcvtzu_4s(e, vc, vc);
        r128_a64_str_q(e, vc, 31, wo);
        if (perlane)
            r128_a64_ldr_q(e, 14, 31, vlo); /* dims (both wraps)    */
        r128_a64_orr_16b(e, 15, 7, 7);      /* raw c0 copy          */
        if (perlane)
            r128_a64_soa_wrap_v(e, mode, 15, 14, 25, 27);
        else
            r128_a64_soa_wrap(e, mode, 15, dlo, 25, 27);
        r128_a64_str_q(e, 15, 31, c0o);
        /* Both clamp base wraps leave each lane's n - 1 in v25.
           Capping the raw successor preserves the saturated edge. */
        if ((mode & 3) == 2)
            r128_a64_smin_4s(e, 7, 7, 25);
        r128_a64_movi_4s_imm8(e, 15, 1);
        r128_a64_add_4s(e, 7, 7, 15); /* raw c0 + 1           */
        if (perlane)
            r128_a64_soa_wrap_v(e, mode, 7, 14, 25, 27);
        else
            r128_a64_soa_wrap(e, mode, 7, dlo, 25, 27);
        r128_a64_str_q(e, 7, 31, c1o);
    }
}

/* Per-lane scalar texel fetches (NEON has no gather load) and the
   4-wide bilinear lerps; the filtered ARGB lanes end in v3. emit_texel
   reads the descriptor words and the frame coordinate slots directly.
   perlane states set up each lane's level with r128_a64_soa_lane_desc
   from the slot vector at slo; fixed-level states use the hoisted
   words and the texbase at TB. */
static void
r128_a64_soa_tex_gather(r128_a64_emit_t *e, const r3d_stage_hdr_t *h,
                        int perlane, int slo, int linear, int st)
{
    int k;

    if (perlane) {
        /* Lane by lane: one descriptor setup per lane (its four
           corners share the level), corners inserted into
           v3/v14/v15/v7. c11 goes through the U0 slot, whose
           coordinates have all been read by then, so each lerp has five
           scratch registers. The fetch order across lanes cannot be
           observed: the fetches only read. */
        for (k = 0; k < 4; k++) {
            r128_a64_soa_lane_desc(e, slo, k, st);
            r128_a64_emit_texel(e, h, R128_A64_SP_SOA_U0 + 4 * k,
                                R128_A64_SP_SOA_V0 + 4 * k, e->tex_sub[st]);
            r128_a64_ins_s_w(e, 3, k, 27); /* c00                  */
            if (linear) {
                r128_a64_emit_texel(e, h, R128_A64_SP_SOA_U1 + 4 * k,
                                    R128_A64_SP_SOA_V0 + 4 * k, e->tex_sub[st]);
                r128_a64_ins_s_w(e, 14, k, 27); /* c10                  */
                r128_a64_emit_texel(e, h, R128_A64_SP_SOA_U0 + 4 * k,
                                    R128_A64_SP_SOA_V1 + 4 * k, e->tex_sub[st]);
                r128_a64_ins_s_w(e, 15, k, 27); /* c01                  */
                r128_a64_emit_texel(e, h, R128_A64_SP_SOA_U1 + 4 * k,
                                    R128_A64_SP_SOA_V1 + 4 * k, e->tex_sub[st]);
                r128_a64_ins_s_w(e, 7, k, 27); /* c11                  */
            }
        }
        if (linear) {
            r128_a64_str_q(e, 7, 31, R128_A64_SP_SOA_U0); /* coords dead */
            r128_a64_soa_lerp(e, 3, 14, R128_A64_SP_SOA_WU, 25, 26, 27, 31, 7);
            r128_a64_ldr_q(e, 14, 31, R128_A64_SP_SOA_U0);
            r128_a64_soa_lerp(e, 15, 14, R128_A64_SP_SOA_WU, 25, 26, 27, 31, 7);
            r128_a64_soa_lerp(e, 3, 15, R128_A64_SP_SOA_WV, 25, 26, 27, 31, 14);
        }
    } else {
        r128_a64_ldr_x(e, 28, 31, R128_A64_SP_SOA_TB);
        for (k = 0; k < 4; k++) {
            r128_a64_emit_texel(e, h, R128_A64_SP_SOA_U0 + 4 * k,
                                R128_A64_SP_SOA_V0 + 4 * k, e->tex_sub[st]);
            r128_a64_ins_s_w(e, 3, k, 27); /* c00 lanes            */
        }
        if (linear) {
            for (k = 0; k < 4; k++) {
                r128_a64_emit_texel(e, h, R128_A64_SP_SOA_U1 + 4 * k,
                                    R128_A64_SP_SOA_V0 + 4 * k, e->tex_sub[st]);
                r128_a64_ins_s_w(e, 14, k, 27); /* c10                  */
            }
            r128_a64_soa_lerp(e, 3, 14, R128_A64_SP_SOA_WU, 25, 26, 27, 31, 15);
            for (k = 0; k < 4; k++) {
                r128_a64_emit_texel(e, h, R128_A64_SP_SOA_U0 + 4 * k,
                                    R128_A64_SP_SOA_V1 + 4 * k, e->tex_sub[st]);
                r128_a64_ins_s_w(e, 14, k, 27); /* c01                  */
            }
            for (k = 0; k < 4; k++) {
                r128_a64_emit_texel(e, h, R128_A64_SP_SOA_U1 + 4 * k,
                                    R128_A64_SP_SOA_V1 + 4 * k, e->tex_sub[st]);
                r128_a64_ins_s_w(e, 15, k, 27); /* c11                  */
            }
            r128_a64_soa_lerp(e, 14, 15, R128_A64_SP_SOA_WU, 25, 26, 27, 31, 7);
            r128_a64_soa_lerp(e, 3, 14, R128_A64_SP_SOA_WV, 25, 26, 27, 31, 15);
        }
    }
}

/* One filtered pipeline (coordinate quantize, wrap and gather) for a
   dual trilinear stage (minb 4/5), emitted as a BL subroutine inside
   the block, ahead of the loops. Pass A, pass B and, on nearest
   texel-filter classes, the stage-0 chroma-key nearest pass all call
   this one copy, which keeps heavy dual pairs under the 16 KB cap. The
   contract is the inline pipeline's: in, s/t in v3/v26 (consumed) and
   the slot vector at SL; out, the filtered ARGB lanes in v3. The return
   address is kept in a dedicated frame slot, not in v31: the body's
   emit_texel calls the S3TC/YUV decode sub, which keeps its own return
   address in v31.d[0] (and uses s[2] as scratch under vecwm). w30 is
   scratch inside the body, so only the frame slot holds the return
   address. */
static int
r128_a64_emit_soa_coord_sub(r128_a64_emit_t            *e,
                            const rage128_draw_state_t *ds, int st)
{
    const r3d_stage_hdr_t *h      = &ds->sh[st];
    int                    linear = (int) (h->minb & 1);
    int                    slr    = r128_a64_soa_slr_off(ds);
    int                    entry  = r128_a64_here(e);
    int                    axis;

    r128_a64_str_x(e, 30, 31, slr);
    r128_a64_ldr_x(e, 25, 0, (int) offsetof(r128_jit_tri_t, texctx));
    r128_a64_emit_soa_dims(e, R128_A64_SP_SOA_SL, st);
    for (axis = 0; axis < 2; axis++)
        r128_a64_soa_tex_axis(e, h, axis, 1, linear);
    r128_a64_soa_tex_gather(e, h, 1, R128_A64_SP_SOA_SL, linear, st);
    r128_a64_ldr_x(e, 30, 31, slr);
    r128_a64_ret(e);
    return entry;
}

/* Fixed-level descriptor for one stage of a dual-stage block, written
   per group. The two stages share the R128_A64_TS_LW, _LH, _BASE, _MASK
   and _PAL words and the TB/FLW/FLH slots, so there is no row hoist;
   each stage writes its own top-level descriptor inside the group body
   (the same words the split rerun rebuilds). The literal 24 is the
   slot[] stride, sizeof(struct r3d_slot_desc_t), which
   vid_ati_rage128_codegen_arm64_tex.h checks at compile time. General
   scratch x17/w25/w28; s15. */
static void
r128_a64_emit_soa_stage_desc(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                             int st)
{
    const r3d_stage_hdr_t *h      = &ds->sh[st];
    int                    sd_off = (int) (st ? offsetof(r3d_texctx_t, sd1)
                                              : offsetof(r3d_texctx_t, sd0));
    int                    sl_off = sd_off + (int) offsetof(r3d_stage_desc_t, slot);

    r128_a64_ldr_x(e, 25, 0, (int) offsetof(r128_jit_tri_t, texctx));
    r128_a64_add_x_imm(e, 17, 25, sl_off + h->top * 24);
    r128_a64_ldp_w(e, 25, 28, 17, 0); /* lw, lh          */
    r128_a64_str_w(e, 25, 31, R128_A64_TS_LW);
    r128_a64_str_w(e, 28, 31, R128_A64_TS_LH);
    r128_a64_ucvtf_s_w(e, 15, 25);
    r128_a64_str_s(e, 15, 31, R128_A64_SP_SOA_FLW);
    r128_a64_ucvtf_s_w(e, 15, 28);
    r128_a64_str_s(e, 15, 31, R128_A64_SP_SOA_FLH);
    r128_a64_ldr_w(e, 25, 17, 16); /* arena base      */
    r128_a64_str_w(e, 25, 31, R128_A64_TS_BASE);
    r128_a64_ldr_w(e, 25, 17, 20); /* arena mask      */
    r128_a64_str_w(e, 25, 31, R128_A64_TS_MASK);
    r128_a64_ldr_x(e, 25, 17, 8); /* level texbase   */
    r128_a64_str_x(e, 25, 31, R128_A64_SP_SOA_TB);
    if (h->dt == 1 || h->dt == 2) {
        r128_a64_ldr_x(e, 17, 0, (int) offsetof(r128_jit_tri_t, texctx));
        r128_a64_ldr_x(e, 25, 17, sd_off + (int) offsetof(r3d_stage_desc_t, pal));
        r128_a64_str_x(e, 25, 31, R128_A64_TS_PAL);
    }
}

/* ------------------------------------------------------------------------
 * Textured group body, one or two stages. The dual-stage behavior
 * follows the interpreter's rage128_texstage_run: int_color stays the
 * block-entry color for both stages, prev is the running color, and
 * alpha passes from stage 0 to stage 1 even for a 565 destination.
 * Steps: int color channels to the frame, vector s/t (and the
 * perspective 1/rhw), vector coordinate quantize and wrap, per-lane
 * scalar texel fetches through the frame coordinate slots, 4-wide
 * bilinear lerps, per-channel combine (the full code book) and the
 * quantized pack. The weights v25/v27/v14 are consumed; the caller
 * keeps zi (v7) in the frame around this section. On exit the packed
 * lanes are in v25 (565 already narrowed to 4H, as in the untextured
 * pack), or, when blend, specular or fog follow, the float channels
 * are in the SRC slots. One static emitter per step below, sharing the
 * stage state through r128_a64_soa_ts_t; r128_a64_emit_soa_texstage_one
 * runs them in order.
 * ---------------------------------------------------------------------- */
typedef struct {
    r128_a64_emit_t            *e;
    const rage128_draw_state_t *ds;
    const r3d_stage_hdr_t      *h;
    const r3d_comb_desc_t      *cd;
    int                         st, prev_base, out_float, dual;
    int                         has_lod, mip_on, tri, split, linear;
    int                         dith_on, sa, ta, sd_off, csub;
} r128_a64_soa_ts_t;

static void
r128_a64_soa_ts_setup(r128_a64_soa_ts_t *t, r128_a64_emit_t *e,
                      const rage128_draw_state_t *ds, int st, int prev_base,
                      int out_float, int dual)
{
    const r3d_stage_hdr_t *h       = &ds->sh[st];
    int                    has_lod = st ? ds->need_lod2 : ds->need_lod;
    int                    lmin    = r128_jit_minb_lin_min(h->minb, h->mipdis, has_lod);
    int                    sel     = st && ds->sec_sel;

    t->e         = e;
    t->ds        = ds;
    t->h         = h;
    t->cd        = &ds->comb[st];
    t->st        = st;
    t->prev_base = prev_base;
    t->out_float = out_float;
    t->dual      = dual;
    t->has_lod   = has_lod;
    t->mip_on    = r128_jit_minb_mip_on(h->minb, h->mipdis, has_lod);
    t->tri       = r128_jit_minb_tri(h->minb, h->mipdis, has_lod);
    /* split: the LOD sign picks the texel filter per lane. The gather
       pipeline (r128_a64_soa_ts_gather) runs with the MIN filter, and
       the magnify rerun (r128_a64_soa_ts_split) samples the base level
       again with the MAG filter. Without a split the filter is fixed at
       compile time even when the slot is chosen per lane. In dual
       blocks the trilinear/split slots (SLB, WM, TS, TT, CA, MSK) are
       all dead before the stage hands its result on through P0. */
    t->split   = r128_jit_minb_split(h->minb, h->mipdis, h->mag, has_lod);
    t->linear  = has_lod ? lmin : !(h->minb == 0 && h->mag == 0);
    t->dith_on = ds->dither && ds->dst_dt == 4;
    t->sa      = (int) (sel ? offsetof(r3d_texctx_t, s2a)
                            : offsetof(r3d_texctx_t, sta));
    t->ta      = (int) (sel ? offsetof(r3d_texctx_t, t2a)
                            : offsetof(r3d_texctx_t, tta));
    t->sd_off  = (int) (st ? offsetof(r3d_texctx_t, sd1)
                           : offsetof(r3d_texctx_t, sd0));
    t->csub    = e->soa_csub[st]; /* >= 0: shared coord/gather sub
                                     (heavy dual tri) */
}

/* ir (the perspective reciprocal, or the dual stash), s/t from the
   weights, per-lane LOD to mip slots, the s/t stash for trilinear and
   split, and the per-stage descriptor in dual blocks */
static void
r128_a64_soa_ts_coords(const r128_a64_soa_ts_t *t)
{
    r128_a64_emit_t            *e  = t->e;
    const rage128_draw_state_t *ds = t->ds;
    const r3d_stage_hdr_t      *h  = t->h;
    int                         st = t->st, dual = t->dual;
    int                         has_lod = t->has_lod, mip_on = t->mip_on;
    int                         tri = t->tri, split = t->split;
    int                         sa = t->sa, ta = t->ta, sd_off = t->sd_off;

    r128_a64_ldr_x(e, 25, 0, (int) offsetof(r128_jit_tri_t, texctx));
    if (dual) {
        /* The weights come back from the dual head's stash. ir is
           reloaded from its slot further down, after the LOD block,
           which does not touch v31; loading it late keeps the register
           use the same as in the single-stage path. */
        r128_a64_ldr_q(e, 25, 31, R128_A64_SP_SOA_W0);
        r128_a64_ldr_q(e, 27, 31, R128_A64_SP_SOA_W1);
        r128_a64_ldr_q(e, 14, 31, R128_A64_SP_SOA_W2);
    } else if (ds->do_persp) {
        /* ir = perspective reciprocal: v31 */
        r128_a64_ldur_q(e, 15, 25, (int) offsetof(r3d_texctx_t, arhw));
        r128_a64_fmul_4s_elem(e, 3, 25, 15, 0);
        r128_a64_fmul_4s_elem(e, 26, 27, 15, 1);
        r128_a64_fadd_4s(e, 3, 3, 26);
        r128_a64_fmul_4s_elem(e, 26, 14, 15, 2);
        r128_a64_fadd_4s(e, 3, 3, 26); /* rhw                  */
        if (has_lod)                   /* the LOD block wants wp; U0 is dead until quantize */
            r128_a64_str_q(e, 3, 31, R128_A64_SP_SOA_U0);
        r128_a64_mov_w_fbits(e, 17, 1.0f);
        r128_a64_dup_4s_w(e, 15, 17);
        r128_a64_fdiv_4s(e, 26, 15, 3);
        r128_a64_fcmeq0_4s(e, 31, 3);
        r128_a64_bsl_16b(e, 31, 15, 26); /* rhw==0 -> 1.0        */
    } else {
        r128_a64_mov_w_fbits(e, 17, 1.0f);
        r128_a64_dup_4s_w(e, 31, 17);
    }

    /* tp -> v26, sp -> v3 (weights die here), then s/t = *ir */
    r128_a64_ldur_q(e, 15, 25, ta);
    r128_a64_fmul_4s_elem(e, 26, 25, 15, 0);
    r128_a64_fmul_4s_elem(e, 7, 27, 15, 1);
    r128_a64_fadd_4s(e, 26, 26, 7);
    r128_a64_fmul_4s_elem(e, 7, 14, 15, 2);
    r128_a64_fadd_4s(e, 26, 26, 7);
    r128_a64_ldur_q(e, 15, 25, sa);
    r128_a64_fmul_4s_elem(e, 3, 25, 15, 0);
    r128_a64_fmul_4s_elem(e, 7, 27, 15, 1);
    r128_a64_fadd_4s(e, 3, 3, 7);
    r128_a64_fmul_4s_elem(e, 7, 14, 15, 2);
    r128_a64_fadd_4s(e, 3, 3, 7);
    if (has_lod) {
        /* per-lane lod -> mip slot(s) (weights are dead; sp/tp/ir
           survive in v3/v26/v31) */
        r128_a64_emit_soa_lod(e, ds, st,
                              dual ? R128_A64_SP_SOA_RHW : R128_A64_SP_SOA_U0);
        if (split) {
            /* minify mask (lod > 0) for the filter select, packed 4H */
            r128_a64_fcmgt0_4s(e, 15, 14);
            r128_a64_xtn_4h(e, 15, 15);
            r128_a64_str_d(e, 15, 31, R128_A64_SP_SOA_MSK);
        }
        if (tri)
            r128_a64_emit_soa_slots_tri(e, ds, st);
        else if (mip_on) {
            r128_a64_emit_soa_slots(e, ds, st);
            r128_a64_emit_soa_dims(e, R128_A64_SP_SOA_SL, st);
        }
    }
    if (dual)
        r128_a64_ldr_q(e, 31, 31, R128_A64_SP_SOA_IR);
    r128_a64_fmul_4s(e, 3, 3, 31);   /* s                    */
    r128_a64_fmul_4s(e, 26, 26, 31); /* t                    */
    if (tri || split) {
        /* s/t are kept for pass B and the magnify rerun: in TS/TT for
           mip-mapped states (TS overlays the TB/FLW/FLH words they do
           not use), or in the unused VLW/VLH slots for base-level split
           states, which keep their fixed-level descriptor in
           TB/FLW/FLH */
        r128_a64_str_q(e, 3, 31,
                       mip_on ? R128_A64_SP_SOA_TS : R128_A64_SP_SOA_VLW);
        r128_a64_str_q(e, 26, 31,
                       mip_on ? R128_A64_SP_SOA_TT : R128_A64_SP_SOA_VLH);
    }
    if (dual) {
        /* per-group per-stage descriptor replaces the row hoist */
        if (!mip_on)
            r128_a64_emit_soa_stage_desc(e, ds, st);
        else if (h->dt == 1 || h->dt == 2) {
            /* per-lane levels: only the per-stage palette hoists */
            r128_a64_ldr_x(e, 17, 0, (int) offsetof(r128_jit_tri_t, texctx));
            r128_a64_ldr_x(e, 25, 17,
                           sd_off + (int) offsetof(r3d_stage_desc_t, pal));
            r128_a64_str_x(e, 25, 31, R128_A64_TS_PAL);
        }
    }
}

/* Stage-0 chroma key for the classes where c00 is not the nearest
   texel (linear texel filter, trilinear, or MIN/MAG split). tnear is
   r3d_tex_level's separate unbiased floor fetch at the primary level,
   not the bilinear c00, which is biased by -0.5. The primary level is
   the per-lane SL slot when mip-mapped (the slot emitters set magnify
   lanes to top, as the scalar lod <= 0 branch does), else the fixed top
   descriptor. So a separate nearest coordinate/gather pass runs ahead
   of the filtered pipeline; s/t wait in the WU/WV slots, which the
   filtered axis pass writes afterwards. On nearest classes without
   trilinear or split, the pipeline's own c00 is tnear and the compare
   runs after the pipeline instead. */
static void
r128_a64_soa_ts_ck_near(const r128_a64_soa_ts_t *t)
{
    r128_a64_emit_t            *e  = t->e;
    const rage128_draw_state_t *ds = t->ds;
    const r3d_stage_hdr_t      *h  = t->h;
    int                         st = t->st, mip_on = t->mip_on;
    int                         tri = t->tri, linear = t->linear;
    int                         csub = t->csub;
    int                         axis;

    r128_a64_str_q(e, 3, 31, R128_A64_SP_SOA_WU);
    r128_a64_str_q(e, 26, 31, R128_A64_SP_SOA_WV);
    if (csub >= 0 && !linear)
        /* nearest-filter trilinear class: the nearest pass is the sub
           itself (same dimensions, axis and gather setup), one BL */
        r128_a64_bl_to(e, csub);
    else {
        if (tri) {
            /* pass A's dimensions for the nearest fetch (the pass loop
               emits its own for each pass) */
            r128_a64_ldr_x(e, 25, 0,
                           (int) offsetof(r128_jit_tri_t, texctx));
            r128_a64_emit_soa_dims(e, R128_A64_SP_SOA_SL, st);
        }
        for (axis = 0; axis < 2; axis++)
            r128_a64_soa_tex_axis(e, h, axis, mip_on, 0);
        r128_a64_soa_tex_gather(e, h, mip_on, R128_A64_SP_SOA_SL, 0, st);
    }
    r128_a64_soa_ck_mask(e, ds);
    r128_a64_ldr_q(e, 3, 31, R128_A64_SP_SOA_WU);
    r128_a64_ldr_q(e, 26, 31, R128_A64_SP_SOA_WV);
}

/* Coordinate/gather pipeline. Trilinear runs it once per mip level:
   each level has its own dimensions, so the quantize, the wrap and the
   bilinear 8.8 fractions are all per level, as in the scalar
   r3d_tex_level call per slot. The two level results are then
   lerped. */
static void
r128_a64_soa_ts_gather(const r128_a64_soa_ts_t *t)
{
    r128_a64_emit_t       *e  = t->e;
    const r3d_stage_hdr_t *h  = t->h;
    int                    st = t->st, mip_on = t->mip_on;
    int                    tri = t->tri, linear = t->linear;
    int                    csub = t->csub;
    int                    axis, pass;

    for (pass = 0; pass < (tri ? 2 : 1); pass++) {
        int slo = pass ? R128_A64_SP_SOA_SLB : R128_A64_SP_SOA_SL;

        if (tri) {
            if (pass) {
                r128_a64_ldr_q(e, 3, 31, R128_A64_SP_SOA_TS);
                r128_a64_ldr_q(e, 26, 31, R128_A64_SP_SOA_TT);
            }
            if (csub >= 0) {
                /* the sub reads the slot vector at SL, so pass B copies its
                   level-B slots there (pass A has finished with SL) */
                if (pass) {
                    r128_a64_ldr_d(e, 15, 31, R128_A64_SP_SOA_SLB);
                    r128_a64_str_d(e, 15, 31, R128_A64_SP_SOA_SL);
                }
            } else {
                r128_a64_ldr_x(e, 25, 0, (int) offsetof(r128_jit_tri_t, texctx));
                r128_a64_emit_soa_dims(e, slo, st);
            }
        }

        if (csub >= 0)
            r128_a64_bl_to(e, csub);
        else {
            /* coordinate quantize and wrap per axis:
               fx = r3d_texcoord_fx(s * (float)dim), then u0 for nearest or
               u0/u1 and the 8.8 fraction for bilinear */
            for (axis = 0; axis < 2; axis++)
                r128_a64_soa_tex_axis(e, h, axis, mip_on, linear);

            /* per-lane scalar fetches and lerps: emit_texel reads the
               hoisted (or per-lane) texture-stage descriptor words and the
               frame coordinate words */
            r128_a64_soa_tex_gather(e, h, mip_on, slo, linear, st);
        }

        if (tri && !pass)
            r128_a64_str_q(e, 3, 31, R128_A64_SP_SOA_CA);
    } /* pass */

    if (tri) {
        /* r3d_lerp_argb(ca, cb, f): ca = pass A (x operand), cb = v3 */
        r128_a64_ldr_q(e, 14, 31, R128_A64_SP_SOA_CA);
        r128_a64_ldr_d(e, 25, 31, R128_A64_SP_SOA_WM);
        r128_a64_uxtl_4s(e, 25, 25);
        r128_a64_soa_lerp_vw(e, 14, 3, 25, 26, 27, 31, 7);
        r128_a64_orr_16b(e, 3, 14, 14);
    }
}

/* Magnify rerun (the scalar lod <= 0 branch): store the MIN-filter
   lanes, sample the base level again with the MAG filter, then select
   per lane on the LOD sign. Minify lanes keep the pipeline result and
   drop their rerun result; magnify lanes do the opposite. */
static void
r128_a64_soa_ts_split(const r128_a64_soa_ts_t *t)
{
    r128_a64_emit_t       *e  = t->e;
    const r3d_stage_hdr_t *h  = t->h;
    int                    st = t->st, mip_on = t->mip_on;
    int                    sd_off = t->sd_off;
    int                    axis;

    r128_a64_str_q(e, 3, 31, R128_A64_SP_SOA_CA);
    r128_a64_ldr_q(e, 3, 31,
                   mip_on ? R128_A64_SP_SOA_TS : R128_A64_SP_SOA_VLW);
    r128_a64_ldr_q(e, 26, 31,
                   mip_on ? R128_A64_SP_SOA_TT : R128_A64_SP_SOA_VLH);
    if (mip_on) {
        /* the per-lane fetches overwrote the descriptor words, so
           rebuild the fixed top-level descriptor (base-level states
           fetch through it and still hold it); sd_off selects the
           stage's own descriptor, sd1 for dual stage 1 */
        int sl_off = sd_off + (int) offsetof(r3d_stage_desc_t, slot);

        r128_a64_ldr_x(e, 25, 0, (int) offsetof(r128_jit_tri_t, texctx));
        r128_a64_add_x_imm(e, 17, 25, sl_off + h->top * 24);
        r128_a64_ldp_w(e, 25, 28, 17, 0); /* lw, lh    */
        r128_a64_str_w(e, 25, 31, R128_A64_TS_LW);
        r128_a64_str_w(e, 28, 31, R128_A64_TS_LH);
        r128_a64_ucvtf_s_w(e, 15, 25);
        r128_a64_str_s(e, 15, 31, R128_A64_SP_SOA_FLW);
        r128_a64_ucvtf_s_w(e, 15, 28);
        r128_a64_str_s(e, 15, 31, R128_A64_SP_SOA_FLH);
        r128_a64_ldr_w(e, 25, 17, 16); /* arena base */
        r128_a64_str_w(e, 25, 31, R128_A64_TS_BASE);
        r128_a64_ldr_w(e, 25, 17, 20); /* arena mask */
        r128_a64_str_w(e, 25, 31, R128_A64_TS_MASK);
        r128_a64_ldr_x(e, 25, 17, 8); /* texbase    */
        r128_a64_str_x(e, 25, 31, R128_A64_SP_SOA_TB);
    }
    for (axis = 0; axis < 2; axis++)
        r128_a64_soa_tex_axis(e, h, axis, 0, h->mag == 1);
    r128_a64_soa_tex_gather(e, h, 0, 0, h->mag == 1, st);
    /* select: minify (lod > 0) lanes take the stored result, the
       others the rerun; an integer BSL, exact */
    r128_a64_ldr_q(e, 14, 31, R128_A64_SP_SOA_CA);
    r128_a64_ldr_d(e, 15, 31, R128_A64_SP_SOA_MSK);
    r128_a64_sxtl_4s(e, 15, 15);
    r128_a64_bsl_16b(e, 15, 14, 3);
    r128_a64_orr_16b(e, 3, 15, 15);
}

/* Texel channels to float: ta stays in v31; tr/tg/tb go to the
   coordinate slots, which are free by now, so the combine below has
   all scratch registers. */
static void
r128_a64_soa_ts_unpack(const r128_a64_soa_ts_t *t)
{
    r128_a64_emit_t *e = t->e;
    int              ch;

    r128_a64_ushr_4s(e, 31, 3, 24);
    r128_a64_ucvtf_4s(e, 31, 31);
    r128_a64_fdiv_4s(e, 31, 31, 23); /* ta                   */
    for (ch = 0; ch < 3; ch++) {
        static const int shr[3] = { 16, 8, 0 };
        static const int slo[3] = { R128_A64_SP_SOA_U0, R128_A64_SP_SOA_U1,
                                    R128_A64_SP_SOA_V0 };

        if (shr[ch])
            r128_a64_ushr_4s(e, 26, 3, shr[ch]);
        else
            r128_a64_orr_16b(e, 26, 3, 3);
        r128_a64_movi_4s_imm8(e, 15, 255);
        r128_a64_and_16b(e, 26, 26, 15);
        r128_a64_ucvtf_4s(e, 26, 26);
        r128_a64_fdiv_4s(e, 26, 26, 23);
        r128_a64_str_q(e, 26, 31, slo[ch]);
    }
}

/* Color combine, one channel at a time (r3d_tex_combine per lane).
   The previous-color operands read prev_base: C0 on stage 0, where the
   previous color is the int color, and the stage-0 float output at P0
   on stage 1. R128_INPUT_FACTOR_INT_COLOR and
   R128_INPUT_FACTOR_INT_ALPHA always read C0. v3 becomes the pack
   accumulator; with out_float the unquantized combine results are
   stored instead, as the interpreter's col[] carries them. */
static void
r128_a64_soa_ts_combine(const r128_a64_soa_ts_t *t)
{
    r128_a64_emit_t            *e         = t->e;
    const rage128_draw_state_t *ds        = t->ds;
    const r3d_comb_desc_t      *cd        = t->cd;
    int                         prev_base = t->prev_base;
    int                         out_float = t->out_float;
    int                         dith_on   = t->dith_on;
    int                         ch;

    for (ch = 0; ch < 3; ch++) {
        static const int slo[3]     = { R128_A64_SP_SOA_U0, R128_A64_SP_SOA_U1,
                                        R128_A64_SP_SOA_V0 };
        int              ic_off     = R128_A64_SP_SOA_C0 + 16 * ch;
        int              pv_off     = prev_base + 16 * ch;
        int              p565[3][2] = {
            { 3, 11 },
            { 2, 5  },
            { 3, 0  }
        };
        int bayv = (ch == 1) ? 30 : 29;

        r128_a64_ldr_q(e, 26, 31, slo[ch]); /* tex channel          */
        /* fc -> v27 */
        switch (cd->cfac) {
            case 0:
            case 1:
                r128_a64_mov_w_fbits(e, 17, cd->cfac ? 1.0f - ds->cc[ch] : ds->cc[ch]);
                r128_a64_dup_4s_w(e, 27, 17);
                break;
            case 5: /* 1 - t */
                r128_a64_mov_w_fbits(e, 17, 1.0f);
                r128_a64_dup_4s_w(e, 27, 17);
                r128_a64_fsub_4s(e, 27, 27, 26);
                break;
            case 6: /* ta */
                r128_a64_orr_16b(e, 27, 31, 31);
                break;
            case 7: /* 1 - ta */
                r128_a64_mov_w_fbits(e, 17, 1.0f);
                r128_a64_dup_4s_w(e, 27, 17);
                r128_a64_fsub_4s(e, 27, 27, 31);
                break;
            case 8: /* R128_COLOR_FACTOR_PREV_COLOR */
                r128_a64_ldr_q(e, 27, 31, pv_off);
                break;
            default: /* 4 TEX, and undefined codes */
                r128_a64_orr_16b(e, 27, 26, 26);
                break;
        }
        /* ci -> v25 */
        switch (cd->ifac) {
            case 2:
                r128_a64_mov_w_fbits(e, 17, ds->cc[ch]);
                r128_a64_dup_4s_w(e, 25, 17);
                break;
            case 3:
                r128_a64_mov_w_fbits(e, 17, ds->cc[3]);
                r128_a64_dup_4s_w(e, 25, 17);
                break;
            case 5: /* R128_INPUT_FACTOR_INT_ALPHA: always the
                       block-entry color */
                r128_a64_ldr_q(e, 25, 31, R128_A64_SP_SOA_C0 + 48);
                break;
            case 9: /* R128_INPUT_FACTOR_PREV_ALPHA */
                r128_a64_ldr_q(e, 25, 31, prev_base + 48);
                break;
            case 8: /* R128_INPUT_FACTOR_PREV_COLOR */
                r128_a64_ldr_q(e, 25, 31, pv_off);
                break;
            default: /* 4 R128_INPUT_FACTOR_INT_COLOR, and undefined
                        codes */
                r128_a64_ldr_q(e, 25, 31, ic_off);
                break;
        }
        /* COMB operation -> v14 (scratch v15, v7); each formula gives
           the same values as the scalar switch in r3d_tex_combine */
        switch (cd->comb) {
            case 2:
                r128_a64_orr_16b(e, 14, 25, 25);
                break;
            case 0:
                if (cd->fmsb) {
                    r128_a64_fsub_4s(e, 14, 27, 25);
                    r128_a64_movi_4s_zero(e, 15);
                    r128_a64_fmax_4s(e, 14, 14, 15);
                } else /* disable: the texel channel itself */
                    r128_a64_orr_16b(e, 14, 26, 26);
                break;
            case 1: /* copy: fc */
                r128_a64_orr_16b(e, 14, 27, 27);
                break;
            case 4:
                if (cd->fmsb) { /* ci*(1-t) + fc*t */
                    r128_a64_mov_w_fbits(e, 17, 1.0f);
                    r128_a64_dup_4s_w(e, 15, 17);
                    r128_a64_fsub_4s(e, 15, 15, 26);
                    r128_a64_fmul_4s(e, 15, 25, 15);
                    r128_a64_fmul_4s(e, 7, 27, 26);
                    r128_a64_fadd_4s(e, 14, 15, 7);
                } else { /* min(2*ci*fc, 1) */
                    r128_a64_fmul_4s(e, 14, 25, 27);
                    r128_a64_fadd_4s(e, 14, 14, 14);
                    r128_a64_mov_w_fbits(e, 17, 1.0f);
                    r128_a64_dup_4s_w(e, 15, 17);
                    r128_a64_fmin_4s(e, 14, 14, 15);
                }
                break;
            case 5:
                if (cd->fmsb) { /* min(fc + ci*(1-t), 1) */
                    r128_a64_mov_w_fbits(e, 17, 1.0f);
                    r128_a64_dup_4s_w(e, 15, 17);
                    r128_a64_fsub_4s(e, 7, 15, 26);
                    r128_a64_fmul_4s(e, 7, 25, 7);
                    r128_a64_fadd_4s(e, 14, 27, 7);
                    r128_a64_fmin_4s(e, 14, 14, 15);
                } else { /* min(4*ci*fc, 1) */
                    r128_a64_fmul_4s(e, 14, 25, 27);
                    r128_a64_fadd_4s(e, 14, 14, 14);
                    r128_a64_fadd_4s(e, 14, 14, 14);
                    r128_a64_mov_w_fbits(e, 17, 1.0f);
                    r128_a64_dup_4s_w(e, 15, 17);
                    r128_a64_fmin_4s(e, 14, 14, 15);
                }
                break;
            case 6:
                if (cd->fmsb) { /* min(fc + ci*t, 1) */
                    r128_a64_fmul_4s(e, 7, 25, 26);
                    r128_a64_fadd_4s(e, 14, 27, 7);
                } else /* min(ci + fc, 1) */
                    r128_a64_fadd_4s(e, 14, 25, 27);
                r128_a64_mov_w_fbits(e, 17, 1.0f);
                r128_a64_dup_4s_w(e, 15, 17);
                r128_a64_fmin_4s(e, 14, 14, 15);
                break;
            case 7:  /* clamp(ci + fc - 0.5, 0, 1) */
            case 14: /* clamp(2*(ci + fc - 0.5), 0, 1) */
                r128_a64_fadd_4s(e, 14, 25, 27);
                r128_a64_mov_w_fbits(e, 17, 0.5f);
                r128_a64_dup_4s_w(e, 15, 17);
                r128_a64_fsub_4s(e, 14, 14, 15);
                if (cd->comb == 14)
                    r128_a64_fadd_4s(e, 14, 14, 14);
                r128_a64_movi_4s_zero(e, 15);
                r128_a64_fmax_4s(e, 14, 14, 15);
                r128_a64_mov_w_fbits(e, 17, 1.0f);
                r128_a64_dup_4s_w(e, 15, 17);
                r128_a64_fmin_4s(e, 14, 14, 15);
                break;
            case 8:
            case 9:
            case 10:
            case 12:
            case 15:
                {
                    /* lerp family: weight -> v15, C = ci*(1-w) + fc*w */
                    switch (cd->comb) {
                        case 8: /* R128_COMB_BLEND_VERTEX: the iterated
                                   vertex alpha, always C0 */
                            r128_a64_ldr_q(e, 15, 31, R128_A64_SP_SOA_C0 + 48);
                            break;
                        case 12: /* prev alpha */
                            r128_a64_ldr_q(e, 15, 31, prev_base + 48);
                            break;
                        case 9: /* ta */
                            r128_a64_orr_16b(e, 15, 31, 31);
                            break;
                        case 10:
                            r128_a64_mov_w_fbits(e, 17, ds->cc[3]);
                            r128_a64_dup_4s_w(e, 15, 17);
                            break;
                        default: /* 15: per-channel cc */
                            r128_a64_mov_w_fbits(e, 17, ds->cc[ch]);
                            r128_a64_dup_4s_w(e, 15, 17);
                            break;
                    }
                    r128_a64_mov_w_fbits(e, 17, 1.0f);
                    r128_a64_dup_4s_w(e, 7, 17);
                    r128_a64_fsub_4s(e, 7, 7, 15);
                    r128_a64_fmul_4s(e, 7, 25, 7);
                    r128_a64_fmul_4s(e, 15, 27, 15);
                    r128_a64_fadd_4s(e, 14, 7, 15);
                    break;
                }
            case 11: /* min(fc + ci*(1-ta), 1) */
                r128_a64_mov_w_fbits(e, 17, 1.0f);
                r128_a64_dup_4s_w(e, 15, 17);
                r128_a64_fsub_4s(e, 7, 15, 31);
                r128_a64_fmul_4s(e, 7, 25, 7);
                r128_a64_fadd_4s(e, 14, 27, 7);
                r128_a64_fmin_4s(e, 14, 14, 15);
                break;
            case 13: /* min(fc + ci*ta, 1) */
                r128_a64_fmul_4s(e, 7, 25, 31);
                r128_a64_fadd_4s(e, 14, 27, 7);
                r128_a64_mov_w_fbits(e, 17, 1.0f);
                r128_a64_dup_4s_w(e, 15, 17);
                r128_a64_fmin_4s(e, 14, 14, 15);
                break;
            case 3:
            default:
                r128_a64_fmul_4s(e, 14, 25, 27);
                break;
        }
        if (out_float)
            r128_a64_str_q(e, 14, 31, R128_A64_SP_SOA_P0 + 16 * ch);
        else if (ds->alpha_en || ds->spec_en || ds->fog_en)
            /* blend and the spec/fog stage take float channels: store
               each in its coordinate slot, which this iteration has
               already read */
            r128_a64_str_q(e, 14, 31, slo[ch]);
        else if (ds->dst_dt == 6)
            r128_a64_soa_quant_pack(e, 14, 3, ch == 0, 0,
                                    16 - 8 * ch, 0, 0);
        else
            r128_a64_soa_quant_pack(e, 14, 3, ch == 0, p565[ch][0],
                                    p565[ch][1], dith_on, bayv);
    }
}

/* Alpha combine (the alpha code book of r3d_tex_combine). The 565
   pack drops alpha, but a stage-0 float output always computes it,
   because stage 1's previous-alpha operands read it as the
   interpreter's running col[3]; the blend factors and the alpha test
   read it too. fa in v27, ia in v25, the texel alpha in v31, result in
   v14. */
static void
r128_a64_soa_ts_alpha(const r128_a64_soa_ts_t *t)
{
    r128_a64_emit_t            *e         = t->e;
    const rage128_draw_state_t *ds        = t->ds;
    const r3d_comb_desc_t      *cd        = t->cd;
    int                         prev_base = t->prev_base;
    int                         out_float = t->out_float;

    if (cd->afac == 7) {
        r128_a64_mov_w_fbits(e, 17, 1.0f);
        r128_a64_dup_4s_w(e, 27, 17);
        r128_a64_fsub_4s(e, 27, 27, 31);
    } else
        r128_a64_orr_16b(e, 27, 31, 31);
    switch (cd->ifaca) {
        case 1:
            r128_a64_mov_w_fbits(e, 17, ds->cc[3]);
            r128_a64_dup_4s_w(e, 25, 17);
            break;
        case 2: /* R128_INP_FACTOR_A_INT_ALPHA: always the
                   block-entry color */
            r128_a64_ldr_q(e, 25, 31, R128_A64_SP_SOA_C0 + 48);
            break;
        default: /* 4 R128_INP_FACTOR_A_PREV_ALPHA, and undefined
                    codes */
            r128_a64_ldr_q(e, 25, 31, prev_base + 48);
            break;
    }
    switch (cd->comba) {
        case 0: /* disable: the texel alpha itself on the first stage,
                   the incoming alpha after it */
            if (cd == &ds->comb[0])
                r128_a64_orr_16b(e, 14, 31, 31);
            else
                r128_a64_ldr_q(e, 14, 31, prev_base + 48);
            break;
        case 1: /* copy: fa */
            r128_a64_orr_16b(e, 14, 27, 27);
            break;
        case 2:
            r128_a64_orr_16b(e, 14, 25, 25);
            break;
        case 4: /* min(2*ia*fa, 1) */
        case 5: /* min(4*ia*fa, 1) */
            r128_a64_fmul_4s(e, 14, 25, 27);
            r128_a64_fadd_4s(e, 14, 14, 14);
            if (cd->comba == 5)
                r128_a64_fadd_4s(e, 14, 14, 14);
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, 15, 17);
            r128_a64_fmin_4s(e, 14, 14, 15);
            break;
        case 6: /* min(ia + fa, 1) */
            r128_a64_fadd_4s(e, 14, 25, 27);
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, 15, 17);
            r128_a64_fmin_4s(e, 14, 14, 15);
            break;
        case 7:  /* clamp(ia + fa - 0.5, 0, 1) */
        case 14: /* clamp(2*(ia + fa - 0.5), 0, 1) */
            r128_a64_fadd_4s(e, 14, 25, 27);
            r128_a64_mov_w_fbits(e, 17, 0.5f);
            r128_a64_dup_4s_w(e, 15, 17);
            r128_a64_fsub_4s(e, 14, 14, 15);
            if (cd->comba == 14)
                r128_a64_fadd_4s(e, 14, 14, 14);
            r128_a64_movi_4s_zero(e, 15);
            r128_a64_fmax_4s(e, 14, 14, 15);
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, 15, 17);
            r128_a64_fmin_4s(e, 14, 14, 15);
            break;
        case 3:
        default:
            r128_a64_fmul_4s(e, 14, 25, 27);
            break;
    }
    if (out_float)
        r128_a64_str_q(e, 14, 31, R128_A64_SP_SOA_P0 + 48);
    else {
        if (ds->alpha_en || ds->spec_en || ds->fog_en)
            /* blend and spec/fog pack later: the 8888 alpha and the
               blend factors read alpha from this slot */
            r128_a64_str_q(e, 14, 31, R128_A64_SP_SOA_SRCA);
        if (r128_a64_soa_atest_on(ds)) {
            /* alpha test on the combined alpha (the interpreter tests
               before the dst read): quantize a copy and AND the pass
               mask into the deferred cover stash, as the chroma-key
               rejects do */
            r128_a64_fmul_4s(e, 15, 14, 23);
            r128_a64_fadd_4s(e, 15, 15, 24);
            r128_a64_fcvtzu_4s(e, 15, 15);
            r128_a64_soa_atest_mask(e, ds, 25, 15, 27);
            r128_a64_ldr_q(e, 15, 31, r128_a64_soa_cover_off(ds));
            r128_a64_and_16b(e, 15, 15, 25);
            r128_a64_str_q(e, 15, 31, r128_a64_soa_cover_off(ds));
        }
        if (!ds->alpha_en && !ds->spec_en && !ds->fog_en
            && ds->dst_dt == 6)
            r128_a64_soa_quant_pack(e, 14, 3, 0, 0, 24, 0, 0);
    }
}

static void
r128_a64_emit_soa_texstage_one(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                               int st, int prev_base, int out_float, int dual)
{
    r128_a64_soa_ts_t t;

    r128_a64_soa_ts_setup(&t, e, ds, st, prev_base, out_float, dual);

    r128_a64_soa_ts_coords(&t);
    if (ds->need_ck && !st && (t.linear || t.tri || t.split))
        r128_a64_soa_ts_ck_near(&t);
    r128_a64_soa_ts_gather(&t);
    if (t.split)
        r128_a64_soa_ts_split(&t);
    if (ds->need_ck && !st && !(t.linear || t.tri || t.split)) {
        /* Chroma key on the nearest texel before filtering, stage 0
           only (the interpreter passes a NULL nearest pointer for
           stage 1). On nearest classes without trilinear or split the
           gathered c00 lanes in v3 are tnear: the same unbiased floor
           and wrap fetch at the same level, border included. The other
           classes ran their separate nearest pass before the
           pipeline. */
        r128_a64_soa_ck_mask(e, ds);
    }
    r128_a64_soa_ts_unpack(&t);
    r128_a64_soa_ts_combine(&t);
    if (out_float || ds->dst_dt == 6 || ds->alpha_en
        || r128_a64_soa_atest_on(ds))
        r128_a64_soa_ts_alpha(&t);

    if (out_float)
        return; /* stage 1 packs; the float channels sit at P0 */
    if (ds->alpha_en || ds->spec_en || ds->fog_en)
        return; /* blend and/or the spec/fog stage read the SRC slots */

    /* hand the packed lanes to the store stage in v25, 565 narrowed */
    r128_a64_orr_16b(e, 25, 3, 3);
    if (ds->dst_dt != 6)
        r128_a64_xtn_4h(e, 25, 25);
}

/* The textured group body: the int color channels once (both stages'
   R128_INPUT_FACTOR_INT_COLOR reads use them), then one stage that
   packs. In dual (sec_en) blocks: the weight/rhw/ir stash, a stage 0
   with float output feeding stage 1's previous-color operands, as
   rage128_texstage_run's running col[] does, and a stage 1 that
   packs. */
static void
r128_a64_emit_soa_texstage(r128_a64_emit_t *e, const rage128_draw_state_t *ds)
{
    int ch;

    /* int color channels (weights live): channel ch of
       w0*vca + w1*vcb + w2*vcc, added left to right per lane exactly as
       in the scalar loop */
    for (ch = 0; ch < 4; ch++) {
        r128_a64_fmul_4s_elem(e, 3, 25, 4, ch);
        r128_a64_fmul_4s_elem(e, 15, 27, 5, ch);
        r128_a64_fadd_4s(e, 3, 3, 15);
        r128_a64_fmul_4s_elem(e, 15, 14, 6, ch);
        r128_a64_fadd_4s(e, 3, 3, 15);
        r128_a64_str_q(e, 3, 31, R128_A64_SP_SOA_C0 + 16 * ch);
    }

    if (!ds->sec_en) {
        if (ds->spec_en || ds->fog_en) {
            /* the spec/fog stage after the texture stage needs the
               weights again, and the texture stage consumes them:
               store them as the dual head does (these blocks take the
               656-byte frame, so W0..W2 exist) */
            r128_a64_str_q(e, 25, 31, R128_A64_SP_SOA_W0);
            r128_a64_str_q(e, 27, 31, R128_A64_SP_SOA_W1);
            r128_a64_str_q(e, 14, 31, R128_A64_SP_SOA_W2);
        }
        r128_a64_emit_soa_texstage_one(e, ds, 0, R128_A64_SP_SOA_C0, 0, 0);
        return;
    }

    /* Dual head: store the weights (each stage rebuilds its coordinate
       dot products from them) and compute rhw and ir once, as the
       interpreter does; both stages share the rhw dot product and its
       reciprocal. */
    r128_a64_str_q(e, 25, 31, R128_A64_SP_SOA_W0);
    r128_a64_str_q(e, 27, 31, R128_A64_SP_SOA_W1);
    r128_a64_str_q(e, 14, 31, R128_A64_SP_SOA_W2);
    r128_a64_ldr_x(e, 25, 0, (int) offsetof(r128_jit_tri_t, texctx));
    if (ds->do_persp) {
        r128_a64_ldur_q(e, 15, 25, (int) offsetof(r3d_texctx_t, arhw));
        r128_a64_fmul_4s_elem(e, 3, 25, 15, 0);
        r128_a64_fmul_4s_elem(e, 26, 27, 15, 1);
        r128_a64_fadd_4s(e, 3, 3, 26);
        r128_a64_fmul_4s_elem(e, 26, 14, 15, 2);
        r128_a64_fadd_4s(e, 3, 3, 26); /* rhw                  */
        r128_a64_str_q(e, 3, 31, R128_A64_SP_SOA_RHW);
        r128_a64_mov_w_fbits(e, 17, 1.0f);
        r128_a64_dup_4s_w(e, 15, 17);
        r128_a64_fdiv_4s(e, 26, 15, 3);
        r128_a64_fcmeq0_4s(e, 31, 3);
        r128_a64_bsl_16b(e, 31, 15, 26); /* rhw==0 -> 1.0        */
    } else {
        r128_a64_mov_w_fbits(e, 17, 1.0f);
        r128_a64_dup_4s_w(e, 31, 17);
    }
    r128_a64_str_q(e, 31, 31, R128_A64_SP_SOA_IR);

    r128_a64_emit_soa_texstage_one(e, ds, 0, R128_A64_SP_SOA_C0, 1, 1);
    r128_a64_emit_soa_texstage_one(e, ds, 1, R128_A64_SP_SOA_P0, 0, 1);
}

/* Vertex-fog factor on 4 lanes: f = w0*fga + w1*fgb + w2*fgc clamped to
   [0,1], the scalar dot product added left to right per lane. Both
   clamps select on ordered comparisons, so a NaN factor stays NaN
   to match the reference lane and the scalar block. In: weights
   v25/v27/v14. Out: f in v3. Scratch v15/v26/v31, w17. */
static void
r128_a64_soa_fogf(r128_a64_emit_t *e)
{
    r128_a64_ldur_q(e, 31, 0, (int) offsetof(r128_jit_tri_t, fog));
    r128_a64_fmul_4s_elem(e, 3, 25, 31, 0);
    r128_a64_fmul_4s_elem(e, 15, 27, 31, 1);
    r128_a64_fadd_4s(e, 3, 3, 15);
    r128_a64_fmul_4s_elem(e, 15, 14, 31, 2);
    r128_a64_fadd_4s(e, 3, 3, 15);
    r128_a64_movi_4s_imm8(e, 15, 0);
    r128_a64_fcmgt_4s(e, 26, 15, 3); /* 0 > f, false on NaN  */
    r128_a64_bsl_16b(e, 26, 15, 3);  /* f < 0 -> 0           */
    r128_a64_orr_16b(e, 3, 26, 26);
    r128_a64_mov_w_fbits(e, 17, 1.0f);
    r128_a64_dup_4s_w(e, 15, 17);
    r128_a64_fcmgt_4s(e, 26, 3, 15);
    r128_a64_bsl_16b(e, 26, 15, 3); /* f > 1 -> 1           */
    r128_a64_orr_16b(e, 3, 26, 26);
}

/* Table-fog factor on 4 lanes (untextured, z_en only), the
   interpreter's blend between adjacent table entries. Per lane,
   q = trunc(zc*255*2^32) (FCVTZU 2D on the exact product); UZP2 takes
   the entry indices and UZP1 the 32-bit fractions out of the 64-bit
   lanes. f = (T[i] + (T[i1]-T[i]) * t) / 255.0f with t = float(fr) *
   2^-32 and i1 = min(i+1, 255). zc is the raw zline double stored at
   TFA/TFB by the Z block, clamped to [0,1] in the same form as the
   scalar FCSEL clamp (compare-mask AND for <= 0 or NaN, BSL for > 1).
   Both byte lookups pass their index vectors through TFA/TFB, which are
   free after the reload. Out: f in v3. Scratch v15/v26/v31, x17/x25;
   v7 (zi) and v17 are not touched; v18 (1.0 2D splat) is read for the
   clamp. */
static void
r128_a64_soa_tfogf(r128_a64_emit_t *e)
{
    r128_a64_ldr_q(e, 26, 31, R128_A64_SP_SOA_TFA);
    r128_a64_ldr_q(e, 31, 31, R128_A64_SP_SOA_TFB);
    r128_a64_fcmgt0_2d(e, 15, 26);
    r128_a64_and_16b(e, 26, 26, 15); /* zc <= 0 / NaN -> 0   */
    r128_a64_fcmgt_2d(e, 15, 26, 18);
    r128_a64_bsl_16b(e, 15, 18, 26); /* zcA = zc > 1 ? 1     */
    r128_a64_fcmgt0_2d(e, 26, 31);
    r128_a64_and_16b(e, 31, 31, 26);
    r128_a64_fcmgt_2d(e, 26, 31, 18);
    r128_a64_bsl_16b(e, 26, 18, 31);                    /* zcB                  */
    r128_a64_mov_x_imm64(e, 17, 0x406FE00000000000ull); /* 255.0        */
    r128_a64_dup_2d_x(e, 31, 17);
    r128_a64_fmul_2d(e, 15, 15, 31);
    r128_a64_fmul_2d(e, 26, 26, 31);
    r128_a64_mov_x_imm64(e, 17, 0x41F0000000000000ull); /* 2^32         */
    r128_a64_dup_2d_x(e, 31, 17);
    r128_a64_fmul_2d(e, 15, 15, 31);
    r128_a64_fmul_2d(e, 26, 26, 31);
    r128_a64_fcvtzu_2d(e, 15, 15); /* q = i<<32 | fr       */
    r128_a64_fcvtzu_2d(e, 26, 26);
    r128_a64_uzp2_4s(e, 31, 15, 26); /* i  4S                */
    r128_a64_uzp1_4s(e, 3, 15, 26);  /* fr 4S                */
    r128_a64_str_q(e, 31, 31, R128_A64_SP_SOA_TFA);
    r128_a64_ucvtf_4s(e, 3, 3);
    r128_a64_mov_w_fbits(e, 17, 1.0f / 4294967296.0f);
    r128_a64_dup_4s_w(e, 15, 17);
    r128_a64_fmul_4s(e, 3, 3, 15); /* t                    */
    r128_a64_movi_4s_imm8(e, 15, 1);
    r128_a64_add_4s(e, 26, 31, 15);
    r128_a64_movi_4s_imm8(e, 15, 255);
    r128_a64_umin_4s(e, 26, 26, 15); /* i1                   */
    r128_a64_str_q(e, 26, 31, R128_A64_SP_SOA_TFB);
    r128_a64_ldr_x(e, 25, 0, (int) offsetof(r128_jit_tri_t, fog_table));
    for (int k = 0; k < 4; k++) {
        r128_a64_ldr_w(e, 17, 31, R128_A64_SP_SOA_TFA + 4 * k);
        r128_a64_ldrb_reg(e, 17, 25, 17);
        r128_a64_ins_s_w(e, 15, k, 17); /* T[i]                 */
        r128_a64_ldr_w(e, 17, 31, R128_A64_SP_SOA_TFB + 4 * k);
        r128_a64_ldrb_reg(e, 17, 25, 17);
        r128_a64_ins_s_w(e, 26, k, 17); /* T[i1]                */
    }
    r128_a64_sub_4s(e, 26, 26, 15);
    r128_a64_scvtf_4s(e, 26, 26);   /* fd                   */
    r128_a64_ucvtf_4s(e, 15, 15);   /* fa                   */
    r128_a64_fmul_4s(e, 26, 26, 3); /* fd*t                 */
    r128_a64_fadd_4s(e, 3, 15, 26); /* fa + fd*t            */
    r128_a64_fdiv_4s(e, 3, 3, 23);  /* f = .../255.0f       */
}

/* Specular add and fog lerp on the float channels in the SRC slots:
   col[rgb] += w0*spa + w1*spb + w2*spc, clamped above at 1 (FMIN, as in
   the scalar block), then col[rgb] = col*f + fogc*(1-f) with f in v3
   and the fog color as per-channel immediates. The R/G/B SRC slots are
   updated in place; the alpha slot is not touched, since both
   operations are RGB only. In: weights v25/v27/v14, f in v3 (fog_en).
   Scratch v15/v26/v31, w17. */
static void
r128_a64_soa_specfog(r128_a64_emit_t *e, const rage128_draw_state_t *ds)
{
    static const int slo[3]  = { R128_A64_SP_SOA_SRCR, R128_A64_SP_SOA_SRCG,
                                 R128_A64_SP_SOA_SRCB };
    const float      fogc[3] = { ds->fogr, ds->fogg, ds->fogb };
    int              ch;

    for (ch = 0; ch < 3; ch++) {
        r128_a64_ldr_q(e, 26, 31, slo[ch]);
        if (ds->spec_en) {
            r128_a64_ldur_q(e, 31, 0, (int) offsetof(r128_jit_tri_t, spa));
            r128_a64_fmul_4s_elem(e, 31, 25, 31, ch);
            r128_a64_ldur_q(e, 15, 0, (int) offsetof(r128_jit_tri_t, spb));
            r128_a64_fmul_4s_elem(e, 15, 27, 15, ch);
            r128_a64_fadd_4s(e, 31, 31, 15);
            r128_a64_ldur_q(e, 15, 0, (int) offsetof(r128_jit_tri_t, spc));
            r128_a64_fmul_4s_elem(e, 15, 14, 15, ch);
            r128_a64_fadd_4s(e, 31, 31, 15);
            r128_a64_fadd_4s(e, 26, 26, 31);
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, 15, 17);
            r128_a64_fmin_4s(e, 26, 26, 15);
        }
        if (ds->fog_en) {
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, 15, 17);
            r128_a64_fsub_4s(e, 15, 15, 3); /* 1 - f                */
            r128_a64_fmul_4s(e, 26, 26, 3); /* col * f              */
            r128_a64_mov_w_fbits(e, 17, fogc[ch]);
            r128_a64_dup_4s_w(e, 31, 17);
            r128_a64_fmul_4s(e, 31, 31, 15);
            r128_a64_fadd_4s(e, 26, 26, 31);
        }
        r128_a64_str_q(e, 26, 31, slo[ch]);
    }
}

/* Quantize and pack the float channels in the SRC slots into v25 (565
   narrowed to 4H), for spec/fog groups without blend: the same
   per-channel r128_a64_soa_quant_pack as the texture stage's pack, fed
   from the slots. Scratch v3 (accumulator) and v14, w17; the dither
   constants v28/v29/v30 stay live. */
static void
r128_a64_soa_pack_slots(r128_a64_emit_t *e, const rage128_draw_state_t *ds)
{
    int dith_on = ds->dither && ds->dst_dt == 4;

    if (ds->dst_dt == 6) {
        r128_a64_ldr_q(e, 14, 31, R128_A64_SP_SOA_SRCR);
        r128_a64_soa_quant_pack(e, 14, 3, 1, 0, 16, 0, 0);
        r128_a64_ldr_q(e, 14, 31, R128_A64_SP_SOA_SRCG);
        r128_a64_soa_quant_pack(e, 14, 3, 0, 0, 8, 0, 0);
        r128_a64_ldr_q(e, 14, 31, R128_A64_SP_SOA_SRCB);
        r128_a64_soa_quant_pack(e, 14, 3, 0, 0, 0, 0, 0);
        r128_a64_ldr_q(e, 14, 31, R128_A64_SP_SOA_SRCA);
        r128_a64_soa_quant_pack(e, 14, 3, 0, 0, 24, 0, 0);
        r128_a64_orr_16b(e, 25, 3, 3);
    } else {
        static const int p565[3][2] = {
            { 3, 11 },
            { 2, 5  },
            { 3, 0  }
        };
        static const int slo[3] = { R128_A64_SP_SOA_SRCR,
                                    R128_A64_SP_SOA_SRCG,
                                    R128_A64_SP_SOA_SRCB };

        for (int ch = 0; ch < 3; ch++) {
            r128_a64_ldr_q(e, 14, 31, slo[ch]);
            r128_a64_soa_quant_pack(e, 14, 3, ch == 0, p565[ch][0],
                                    p565[ch][1], dith_on,
                                    (ch == 1) ? 30 : 29);
        }
        r128_a64_orr_16b(e, 25, 3, 3);
        r128_a64_xtn_4h(e, 25, 25);
    }
}

/* Destination alpha as float lanes in vd. A 565 destination reads
   back with alpha 255 (r3d_dst_read ORs in 0xff000000), so da is
   exactly 1.0f; 8888 extracts it from the raw dst group in v26. w17
   scratch. */
static void
r128_a64_soa_blend_da(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                      int vd)
{
    if (ds->dst_dt == 6) {
        r128_a64_ushr_4s(e, vd, 26, 24);
        r128_a64_ucvtf_4s(e, vd, vd);
        r128_a64_fdiv_4s(e, vd, vd, 23);
    } else {
        r128_a64_mov_w_fbits(e, 17, 1.0f);
        r128_a64_dup_4s_w(e, vd, 17);
    }
}

/* r3d_blend_factor on 4 lanes for one channel (the per-channel form of
   the scalar r128_a64_emit_blend_factor). vsc/vdc are the current
   channel's src/dst float lanes; source alpha is loaded from the SRCA
   slot and destination alpha extracted from the raw dst in v26. vd is
   v27 or v31; v15/v25 scratch. Codes 0xb/0xc never reach here: the
   caller builds those forced pairs itself. */
static void
r128_a64_soa_blend_factor(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                          int vd, uint32_t code, int ch, int vsc, int vdc)
{
    switch (code & 0xf) {
        case 0x0:
            r128_a64_movi_4s_zero(e, vd);
            break;
        case 0x2:
            r128_a64_orr_16b(e, vd, vsc, vsc);
            break;
        case 0x3:
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, vd, 17);
            r128_a64_fsub_4s(e, vd, vd, vsc);
            break;
        case 0x4:
            r128_a64_ldr_q(e, vd, 31, R128_A64_SP_SOA_SRCA);
            break;
        case 0x5:
            r128_a64_ldr_q(e, vd, 31, R128_A64_SP_SOA_SRCA);
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, 15, 17);
            r128_a64_fsub_4s(e, vd, 15, vd);
            break;
        case 0x6:
            r128_a64_soa_blend_da(e, ds, vd);
            break;
        case 0x7:
            r128_a64_soa_blend_da(e, ds, vd);
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, 15, 17);
            r128_a64_fsub_4s(e, vd, 15, vd);
            break;
        case 0x8:
            r128_a64_orr_16b(e, vd, vdc, vdc);
            break;
        case 0x9:
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, vd, 17);
            r128_a64_fsub_4s(e, vd, vd, vdc);
            break;
        case 0xa:
            /* R128_ALPHA_BLEND_SAT (SRCALPHASAT), valid as source or
               destination factor */
            if (ch == 3) {
                /* the alpha factor is 1 */
                r128_a64_mov_w_fbits(e, 17, 1.0f);
                r128_a64_dup_4s_w(e, vd, 17);
                break;
            }
            /* f = sa < 1-da ? sa : 1-da. The C compare is strict, so
               the select mask is (1-da) > sa and ties take 1-da, as in
               the C */
            r128_a64_soa_blend_da(e, ds, vd);
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, 15, 17);
            r128_a64_fsub_4s(e, vd, 15, vd); /* 1-da       */
            r128_a64_ldr_q(e, 15, 31, R128_A64_SP_SOA_SRCA);
            r128_a64_fcmgt_4s(e, 25, vd, 15);
            r128_a64_bsl_16b(e, 25, 15, vd);
            r128_a64_orr_16b(e, vd, 25, 25);
            break;
        case 0x1:
        default:
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, vd, 17);
            break;
    }
}

/* Alpha blend on the group, one channel at a time. The interpreter
   blends all four channels with per-channel factor components, so each
   channel is independent once the alpha operands are fixed. The final
   float channels wait in the SRC slots. The dst group is loaded as one
   contiguous vector through x27: the loop head checked the bounds, and
   lanes the C would skip are masked at the store. The result is
   quantized and packed into v25 (565 narrowed to 4H) for the store
   stage. Registers: v26 raw dst, v3 pack accumulator, v7 src/result,
   v14 dst channel, v27/v31 factors, v15/v25 scratch; the dither
   constants v28/v29/v30 are not touched; w17 general scratch (x16
   holds the store mask). */
static void
r128_a64_emit_soa_blend(r128_a64_emit_t *e, const rage128_draw_state_t *ds)
{
    static const int slo[4]  = { R128_A64_SP_SOA_SRCR, R128_A64_SP_SOA_SRCG,
                                 R128_A64_SP_SOA_SRCB, R128_A64_SP_SOA_SRCA };
    int              dith_on = ds->dither && ds->dst_dt == 4;
    int              forced  = ds->bsrc == 0xb || ds->bsrc == 0xc;
    int              nch     = (ds->dst_dt == 6) ? 4 : 3;
    int              ch;

    /* dst group -> raw integer lanes in v26 */
    if (ds->dst_dt == 6)
        r128_a64_ldr_q(e, 26, 27, 0);
    else {
        r128_a64_ldr_d(e, 26, 27, 0);
        r128_a64_uxtl_4s(e, 26, 26);
    }

    if (forced) {
        /* forced pair (R128_ALPHA_BLEND_BLEND 0xb, R128_ALPHA_BLEND_INVBLEND
           0xc): fs = sa (0xb) or 1-sa (0xc) on every channel and
           fd = 1-fs; both stay set for the whole channel loop */
        r128_a64_ldr_q(e, 27, 31, R128_A64_SP_SOA_SRCA);
        r128_a64_mov_w_fbits(e, 17, 1.0f);
        r128_a64_dup_4s_w(e, 15, 17);
        if (ds->bsrc == 0xc)
            r128_a64_fsub_4s(e, 27, 15, 27);
        r128_a64_fsub_4s(e, 31, 15, 27);
    }

    for (ch = 0; ch < nch; ch++) {
        static const int p565[3][2] = {
            { 3, 11 },
            { 2, 5  },
            { 3, 0  }
        };
        static const int f565[3][3] = {
            /* shr, mask, shl */
            { 11, 0x1f, 3 },
            { 5,  0x3f, 2 },
            { 0,  0x1f, 3 }
        };
        int bayv = (ch == 1) ? 30 : 29;

        r128_a64_ldr_q(e, 7, 31, slo[ch]); /* src channel  */
        /* dst channel as float: unpack the raw field exactly as
           r3d_dst_read expands it, then divide by 255 as the C does */
        if (ds->dst_dt == 6) {
            if (ch == 3)
                r128_a64_ushr_4s(e, 14, 26, 24);
            else {
                if (ch < 2)
                    r128_a64_ushr_4s(e, 14, 26, 16 - 8 * ch);
                else
                    r128_a64_orr_16b(e, 14, 26, 26);
                r128_a64_movi_4s_imm8(e, 15, 255);
                r128_a64_and_16b(e, 14, 14, 15);
            }
        } else {
            if (f565[ch][0])
                r128_a64_ushr_4s(e, 14, 26, (uint32_t) f565[ch][0]);
            else
                r128_a64_orr_16b(e, 14, 26, 26);
            r128_a64_movi_4s_imm8(e, 15, (uint32_t) f565[ch][1]);
            r128_a64_and_16b(e, 14, 14, 15);
            r128_a64_shl_4s(e, 14, 14, f565[ch][2]);
        }
        r128_a64_ucvtf_4s(e, 14, 14);
        r128_a64_fdiv_4s(e, 14, 14, 23);
        if (!forced) {
            r128_a64_soa_blend_factor(e, ds, 27, ds->bsrc, ch, 7, 14);
            r128_a64_soa_blend_factor(e, ds, 31, ds->bdst, ch, 7, 14);
        }
        r128_a64_fmul_4s(e, 7, 7, 27);   /* col*fs       */
        r128_a64_fmul_4s(e, 14, 14, 31); /* dc*fd        */
        if (ds->bfcn & 2)
            r128_a64_fsub_4s(e, 7, 7, 14);
        else
            r128_a64_fadd_4s(e, 7, 7, 14);
        if (ds->bfcn & 1) {
            /* no clamp: (float)(lrintf(v*255) & 0xff) / 255, the
               interpreter's 8-bit wrap; FCVTNS rounds to nearest even
               like lrintf in the default rounding mode */
            r128_a64_fmul_4s(e, 7, 7, 23);
            r128_a64_fcvtns_4s(e, 7, 7);
            r128_a64_movi_4s_imm8(e, 15, 255);
            r128_a64_and_16b(e, 7, 7, 15);
            r128_a64_ucvtf_4s(e, 7, 7);
            r128_a64_fdiv_4s(e, 7, 7, 23);
        } else {
            /* clamp to [0,1]. FMAX and FMIN match the C ternary except
               that -0.0 comes out as +0.0, and both pack to the same
               byte; a NaN stays a NaN in both, as in the scalar
               block. */
            r128_a64_movi_4s_zero(e, 15);
            r128_a64_fmax_4s(e, 7, 7, 15);
            r128_a64_mov_w_fbits(e, 17, 1.0f);
            r128_a64_dup_4s_w(e, 15, 17);
            r128_a64_fmin_4s(e, 7, 7, 15);
        }
        /* blended channels are in [0,1], so no field can carry into
           the next byte and the shift/OR pack equals the C's build of
           out */
        if (ds->dst_dt == 6)
            r128_a64_soa_quant_pack(e, 7, 3, ch == 0, 0,
                                    (ch == 3) ? 24 : 16 - 8 * ch, 0, 0);
        else
            r128_a64_soa_quant_pack(e, 7, 3, ch == 0, p565[ch][0],
                                    p565[ch][1], dith_on, bayv);
    }

    r128_a64_orr_16b(e, 25, 3, 3);
    if (ds->dst_dt != 6)
        r128_a64_xtn_4h(e, 25, 25);
}

/* rx0/rx1 from the lane mask in x16 (16 bits per lane), with the same
   result as the scalar loop's per-pixel updates in x order. RBIT+CLZ
   finds the first set lane and sets rx0 only if it is still -1; 63-CLZ
   finds the last set lane for rx1. w17/w28 scratch. */
static void
r128_a64_soa_rx(r128_a64_emit_t *e)
{
    r128_a64_rbit_x(e, 17, 16);
    r128_a64_clz_x(e, 17, 17);
    r128_a64_lsr_x(e, 17, 17, 4);
    r128_a64_add_w_lsl(e, 17, 12, 17, 0);
    r128_a64_cmn_w_imm(e, 23, 1);
    r128_a64_csel_w(e, 23, 17, 23, A64_EQ);
    r128_a64_clz_x(e, 17, 16);
    r128_a64_movz_w(e, 28, 63, 0);
    r128_a64_sub_w_reg(e, 17, 28, 17);
    r128_a64_lsr_w(e, 17, 17, 4);
    r128_a64_add_w_lsl(e, 24, 12, 17, 0);
}

/* Stencil Z block, emitted instead of folding the Z test into the
   cover mask. Load the four 32-bit depth/stencil cell words, compute
   the depth and stencil test results as masks, and store
   {sbuf | sres<<8 | zres<<9} per lane for the stage after the pack. A
   Z fail must not drop the lane from the cover mask, because the lane
   still runs its fail-op read-modify-write. x26 = Z group base; zi is
   in v7 whenever z_en and zfn is not 0 or 7, since zquant is then set.
   Scratch v15/v26/v31, w17; v3 (cover) and v7 (zi) are not touched. */
static void
r128_a64_soa_sten_zblock(r128_a64_emit_t *e, const rage128_draw_state_t *ds)
{
    uint32_t sva = (ds->sref & ds->svmask) & 0xff;

    r128_a64_ldr_q(e, 26, 26, 0); /* cell words           */
    /* zres mask -> v15 (depth off / ALWAYS -> all-ones; NEVER -> 0) */
    if (!ds->z_en || ds->zfn == 7) {
        r128_a64_movi_4s_zero(e, 15);
        r128_a64_not_16b(e, 15, 15);
    } else if (ds->zfn == 0) {
        r128_a64_movi_4s_zero(e, 15);
    } else {
        if (ds->zshift)
            r128_a64_ushr_4s(e, 15, 26, ds->zshift);
        else
            r128_a64_orr_16b(e, 15, 26, 26);
        if (ds->zmax != 0xffffffffu) {
            r128_a64_movz_w(e, 17, ds->zmax & 0xffff, 0);
            if (ds->zmax >> 16)
                r128_a64_movk_w(e, 17, ds->zmax >> 16, 1);
            r128_a64_dup_4s_w(e, 31, 17);
            r128_a64_and_16b(e, 15, 15, 31);
        }
        /* clang-format off */
        switch (ds->zfn & 7) {  /* r3d_cmp(zfn, zi, zbuf), zi in v7 */
            case 1: r128_a64_cmhi_4s(e, 15, 15, 7); break; /* zi <  zb */
            case 2: r128_a64_cmhs_4s(e, 15, 15, 7); break; /* zi <= zb */
            case 3: r128_a64_cmeq_4s(e, 15, 15, 7); break;
            case 4: r128_a64_cmhs_4s(e, 15, 7, 15); break; /* zi >= zb */
            case 5: r128_a64_cmhi_4s(e, 15, 7, 15); break; /* zi >  zb */
                /* clang-format on */
            default: /* 6 */
                r128_a64_cmeq_4s(e, 15, 15, 7);
                r128_a64_not_16b(e, 15, 15);
                break;
        }
    }
    r128_a64_movz_w(e, 17, 0x200, 0); /* fold to bit 9        */
    r128_a64_dup_4s_w(e, 31, 17);
    r128_a64_and_16b(e, 15, 15, 31);
    /* sbuf lanes -> v26 = (word >> sshift) & 0xff, ORed in low byte */
    if (ds->sshift)
        r128_a64_ushr_4s(e, 26, 26, ds->sshift);
    r128_a64_movi_4s_imm8(e, 31, 255);
    r128_a64_and_16b(e, 26, 26, 31);
    r128_a64_orr_16b(e, 15, 15, 26);
    /* sres mask -> v31 = r3d_cmp(sfn, sref&svmask, sbuf&svmask) */
    if (ds->sfn == 7) {
        r128_a64_movi_4s_zero(e, 31);
        r128_a64_not_16b(e, 31, 31);
    } else if (ds->sfn == 0) {
        r128_a64_movi_4s_zero(e, 31);
    } else {
        /* clang-format off */
        r128_a64_mov_w_imm32(e, 17, ds->svmask); /* may not be a
                                                    bitmask immediate */
        r128_a64_dup_4s_w(e, 31, 17);
        r128_a64_and_16b(e, 26, 26, 31);         /* b = sbuf & svmask  */
        r128_a64_mov_w_imm32(e, 17, sva);
        r128_a64_dup_4s_w(e, 31, 17);            /* a splat            */
        switch (ds->sfn & 7) {
            case 1: r128_a64_cmhi_4s(e, 31, 26, 31); break; /* a <  b */
            case 2: r128_a64_cmhs_4s(e, 31, 26, 31); break; /* a <= b */
            case 3: r128_a64_cmeq_4s(e, 31, 31, 26); break;
            case 4: r128_a64_cmhs_4s(e, 31, 31, 26); break; /* a >= b */
            case 5: r128_a64_cmhi_4s(e, 31, 31, 26); break; /* a >  b */
                /* clang-format on */
            default: /* 6 */
                r128_a64_cmeq_4s(e, 31, 31, 26);
                r128_a64_not_16b(e, 31, 31);
                break;
        }
    }
    r128_a64_movz_w(e, 17, 0x100, 0); /* fold to bit 8        */
    r128_a64_dup_4s_w(e, 26, 17);
    r128_a64_and_16b(e, 31, 31, 26);
    r128_a64_orr_16b(e, 15, 15, 31);
    r128_a64_str_q(e, 15, 31, r128_a64_soa_sten_q_off(ds));
}

/* One stencil op result on the sbuf lanes (uint32, in [0,255]): vd from
   vs, vt scratch, w17. Gives the same values as the interpreter's
   switch. The saturating ops use UMIN/UMAX, which equal the C's
   compare-and-select on [0,255]. INVERT and the wrap ops leave bits
   above bit 7 set, and the swmask merge (swmask is 8 bits) clears them,
   as in the scalar r128_a64_emit_sten_op. */
static void
r128_a64_soa_sten_op_vec(r128_a64_emit_t *e, int vd, int vs, uint32_t op,
                         uint32_t sref, int vt)
{
    /* clang-format off */
    switch (op & 7) {
        case 0: r128_a64_orr_16b(e, vd, vs, vs); break;      /* KEEP    */
        case 1: r128_a64_movi_4s_zero(e, vd); break;         /* ZERO    */
        case 2:                                              /* REPLACE */
            r128_a64_mov_w_imm32(e, 17, sref & 0xff);
            r128_a64_dup_4s_w(e, vd, 17);
            break;
        case 3: /* INC sat: min(sbuf + 1, 0xff) */
            r128_a64_movi_4s_imm8(e, vt, 1);
            r128_a64_add_4s(e, vd, vs, vt);
            r128_a64_movi_4s_imm8(e, vt, 255);
            r128_a64_umin_4s(e, vd, vd, vt);
            break;
        case 4: /* DEC sat: max(sbuf, 1) - 1 */
            r128_a64_movi_4s_imm8(e, vt, 1);
            r128_a64_umax_4s(e, vd, vs, vt);
            r128_a64_sub_4s(e, vd, vd, vt);
            break;
        case 5: r128_a64_not_16b(e, vd, vs); break;          /* INVERT  */
        case 6: /* INC wrap */
            r128_a64_movi_4s_imm8(e, vt, 1);
            r128_a64_add_4s(e, vd, vs, vt);
            break;
        default: /* 7 DEC wrap */
            r128_a64_movi_4s_imm8(e, vt, 1);
            r128_a64_sub_4s(e, vd, vs, vt);
            break;
    }
    /* clang-format on */
}

/* Stencil stage after the pack, at the interpreter's position: after
   the alpha test (whose result is already in the op mask) and before
   the color store. In: x16 = op mask (at least one lane set), the
   packed stash, x26 = Z group base. sop = !sres ? sfail : (zres ?
   zpass : zfail), built with nested BSLs on the three op results;
   merged = (sbuf & ~swmask) | (snew & swmask). The byte
   read-modify-write runs per lane, skipped by TBZ on the lane's op-mask
   bit; lanes that failed Z or stencil still apply their fail op, as in
   the scalar block. Then x16 becomes the write mask (op mask AND sres
   AND zres) for the store stage and rx. Scratch
   v3/v14/v15/v26/v27/v31, w17/w25/w28; v25 (packed color), v7 (zi) and
   the cell bases are not touched. */
static void
r128_a64_emit_soa_sten(r128_a64_emit_t *e, const rage128_draw_state_t *ds)
{
    int      qoff = r128_a64_soa_sten_q_off(ds);
    uint32_t keep = ~(0xffu << ds->sshift);
    int      k;

    r128_a64_ldr_q(e, 26, 31, qoff); /* packed lanes         */
    /* write mask -> x25 (lanes with bits 8 and 9 both set), while the
       read-modify-write still runs on the full op mask in x16 */
    r128_a64_movz_w(e, 17, 0x300, 0);
    r128_a64_dup_4s_w(e, 15, 17);
    r128_a64_and_16b(e, 3, 26, 15);
    r128_a64_cmeq_4s(e, 3, 3, 15);
    r128_a64_xtn_4h(e, 3, 3);
    r128_a64_fmov_x_d(e, 17, 3);
    r128_a64_and_x_reg(e, 25, 16, 17);
    /* sbuf lanes */
    r128_a64_movi_4s_imm8(e, 15, 255);
    r128_a64_and_16b(e, 14, 26, 15);
    /* nested select: tmp = zres ? zpass : zfail; snew = sres ? tmp : sfail */
    r128_a64_soa_sten_op_vec(e, 3, 14, ds->zfail_op, ds->sref, 15);
    r128_a64_soa_sten_op_vec(e, 27, 14, ds->zpass_op, ds->sref, 15);
    r128_a64_movz_w(e, 17, 0x200, 0);
    r128_a64_dup_4s_w(e, 31, 17);
    r128_a64_and_16b(e, 15, 26, 31);
    r128_a64_cmeq_4s(e, 15, 15, 31); /* zres mask            */
    r128_a64_bsl_16b(e, 15, 27, 3);
    r128_a64_soa_sten_op_vec(e, 3, 14, ds->sfail_op, ds->sref, 27);
    r128_a64_movz_w(e, 17, 0x100, 0);
    r128_a64_dup_4s_w(e, 31, 17);
    r128_a64_and_16b(e, 27, 26, 31);
    r128_a64_cmeq_4s(e, 27, 27, 31); /* sres mask            */
    r128_a64_bsl_16b(e, 27, 15, 3);  /* snew                 */
    /* merged = (sbuf & ~swmask) | (snew & swmask). swmask and svmask
       may not be encodable as bitmask immediates, so they are built
       with mov_w_imm32 as in the scalar block */
    r128_a64_mov_w_imm32(e, 17, ds->swmask);
    r128_a64_dup_4s_w(e, 15, 17);
    r128_a64_and_16b(e, 27, 27, 15);
    r128_a64_mov_w_imm32(e, 17, (~ds->swmask) & 0xff);
    r128_a64_dup_4s_w(e, 15, 17);
    r128_a64_and_16b(e, 14, 14, 15);
    r128_a64_orr_16b(e, 27, 27, 14);
    r128_a64_str_q(e, 27, 31, qoff); /* bounce (packed dead) */
    /* per-lane byte read-modify-write under the op mask. The C skips
       the write when the byte is unchanged; writing the same value
       back leaves memory identical, as in the scalar block */
    for (k = 0; k < 4; k++) {
        int b = r128_a64_tbz_any(e, 16, 16 * k);

        r128_a64_ldr_w(e, 17, 31, qoff + 4 * k);
        r128_a64_ldr_w(e, 28, 26, 4 * k);
        r128_a64_and_w_bitmask(e, 28, 28, keep);
        r128_a64_orr_w_lsl(e, 28, 28, 17, ds->sshift);
        r128_a64_str_w(e, 28, 26, 4 * k);
        r128_a64_patch14(e, b, r128_a64_here(e));
    }
    /* deferred discard: color/Z stores and rx follow the write mask */
    r128_a64_orr_x_lsl(e, 16, 31, 25, 0);
}

/* ------------------------------------------------------------------------
 * The vector loop. Emitted between the shared prologue and the scalar
 * loop; every bail lands on the scalar loop head (patched by the
 * caller), which then finishes the row with per-pixel semantics. One
 * static emitter per phase below, sharing the loop state through
 * r128_a64_soa_lp_t; r128_a64_emit_soa_loop runs them in order.
 * ---------------------------------------------------------------------- */
typedef struct {
    r128_a64_emit_t            *e;
    const rage128_draw_state_t *ds;
    int                        *bails, *nbail;
    int                         bppsh, z_on, sten, at_on, defer, cover;
    int                         dith_on, zquant;
    int                         head, b_adv0, b_adv1, b_adv2, b_part, b_full;
} r128_a64_soa_lp_t;

static void
r128_a64_soa_lp_setup(r128_a64_soa_lp_t *l, r128_a64_emit_t *e,
                      const rage128_draw_state_t *ds, int *bails, int *nbail)
{
    int z_on  = ds->z_en;
    int sten  = ds->sten_on;
    int ck_on = ds->tex_en && ds->need_ck;
    int at_on = r128_a64_soa_atest_on(ds);

    l->e     = e;
    l->ds    = ds;
    l->bails = bails;
    l->nbail = nbail;
    l->bppsh = (ds->bpp == 4) ? 2 : 1;
    l->z_on  = z_on;
    l->sten  = sten;
    l->at_on = at_on;
    /* Textured rejects (chroma key, alpha test) are known only inside
       the texture stage, so the cover mask waits in the stash and
       rx0/rx1 wait for the surviving lanes. Stencil defers too: its op
       mask (cover after the alpha test, without the Z result) drives
       the fail-op read-modify-write, so rx and the store mask wait for
       the write mask built after it. */
    l->defer   = ck_on || (ds->tex_en && (at_on || sten));
    l->cover   = ds->tex_en ? r128_a64_soa_cover_off(ds) : 0;
    l->dith_on = ds->dither && ds->dst_dt != 6;
    l->zquant  = z_on && (ds->z_wr || ds->zfn != 7);
    l->b_adv1  = -1;
    l->b_adv2  = -1;
}

/* Color/Z row overlap guard, once per row, for stencil blocks and for
   every block that reads or writes the Z cell (zquant). The interpreter
   does the Z/stencil access and the color write pixel by pixel; the
   vector loop does them per group of four. If the color row and the Z
   row overlap, the two orders give different results: writes land in a
   different order and reads see old bytes. So the whole row bails to
   the scalar loop when the masked row starts lie within one row extent
   of each other in either direction ((x1-px)*4 plus 72 bytes of slack;
   4 bytes per pixel covers both buffers' strides). The distance is
   taken modulo the VRAM size: a row that wraps past the end of VRAM
   overlaps the other row's bytes at the start, and only the group that
   straddles the wrap bails by itself, while the groups after it lie
   below the mask and would run vectorized. A false positive only costs
   the vector loop for that row. */
static void
r128_a64_soa_lp_rowguard(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t            *e     = l->e;
    const rage128_draw_state_t *ds    = l->ds;
    int                        *bails = l->bails, *nbail = l->nbail;

    r128_a64_add_w_lsl(e, 16, 4, 12, l->bppsh);
    r128_a64_and_w_reg(e, 16, 16, 11); /* color start & mask */
    r128_a64_add_w_lsl(e, 17, 5, 12, (ds->zbpp == 4) ? 2 : 1);
    r128_a64_and_w_reg(e, 17, 17, 11); /* z start & mask     */
    r128_a64_sub_w_reg(e, 16, 16, 17);
    r128_a64_and_w_reg(e, 16, 16, 11); /* (c - z) mod size   */
    r128_a64_sub_w_reg(e, 17, 13, 12);
    r128_a64_lsl_w(e, 17, 17, 2);
    r128_a64_add_w_imm(e, 17, 17, 72); /* extent + slack     */
    r128_a64_cmp_w_reg(e, 16, 17);
    bails[(*nbail)++] = r128_a64_bcond(e, A64_LO);
    r128_a64_sub_w_reg(e, 16, 11, 16); /* other direction:   */
    r128_a64_add_w_imm(e, 16, 16, 1);  /* size - d           */
    r128_a64_cmp_w_reg(e, 16, 17);
    bails[(*nbail)++] = r128_a64_bcond(e, A64_LO);
}

/* Loop head: a full group needs px+3 <= x1. The group back-branch
   comes back here, so the per-row guard above runs once, outside the
   loop. Then the group cell bases and the edge lane vectors. */
static void
r128_a64_soa_lp_head(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t            *e     = l->e;
    const rage128_draw_state_t *ds    = l->ds;
    int                        *bails = l->bails, *nbail = l->nbail;
    int                         z_on = l->z_on, sten = l->sten;

    l->head = r128_a64_here(e);
    r128_a64_add_w_imm(e, 16, 12, 3);
    r128_a64_cmp_w_reg(e, 16, 13);
    bails[(*nbail)++] = r128_a64_bcond(e, A64_GT);

    /* group cell bases and bounds (any failure bails to the scalar
       loop with no state changed). Color first, then Z; the order
       cannot be observed. */
    r128_a64_soa_group_addr(e, 20, 21, 22, 4, l->bppsh, ds->bpp, 27, bails, nbail);
    if (z_on || sten)
        /* stencil without Z still resolves the Z group: the stencil
           byte lives in the Z cell */
        r128_a64_soa_group_addr(e, 14, 15, 19, 5, (ds->zbpp == 4) ? 2 : 1,
                                ds->zbpp, 26, bails, nbail);

    /* e lane vectors from the group-base GPRs: {e, e+dx} / {e+2dx, e+3dx} */
    r128_a64_dup_2d_x(e, 3, 1);
    r128_a64_add_2d(e, 25, 3, 8);
    r128_a64_add_2d(e, 26, 3, 9);
    r128_a64_dup_2d_x(e, 3, 2);
    r128_a64_add_2d(e, 27, 3, 10);
    r128_a64_add_2d(e, 31, 3, 11);
    r128_a64_dup_2d_x(e, 3, 3);
    r128_a64_add_2d(e, 14, 3, 12);
    r128_a64_add_2d(e, 15, 3, 13);
}

/* coverage: lane covered iff (e0|e1|e2) >= 0 (sign-bit OR, as the
   scalar tbnz63 test); then the weights. The fill-rule bias decides
   coverage only, so the mask reads the biased lane values and then each
   edge's bias flag from tri, splatted to v7 through x16 (both free at
   this point), is added to its lanes before the convert. */
static void
r128_a64_soa_lp_cover(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t *e = l->e;

    r128_a64_orr_16b(e, 3, 25, 27);
    r128_a64_orr_16b(e, 3, 3, 14);
    r128_a64_cmge0_2d(e, 3, 3);
    r128_a64_orr_16b(e, 7, 26, 31);
    r128_a64_orr_16b(e, 7, 7, 15);
    r128_a64_cmge0_2d(e, 7, 7);
    r128_a64_uzp1_4s(e, 3, 3, 7); /* v3 = cover mask 4S */

    r128_a64_ldr_x(e, 16, 0, (int) offsetof(r128_jit_tri_t, e0b));
    r128_a64_dup_2d_x(e, 7, 16);
    r128_a64_add_2d(e, 25, 25, 7);
    r128_a64_add_2d(e, 26, 26, 7);
    r128_a64_ldr_x(e, 16, 0, (int) offsetof(r128_jit_tri_t, e1b));
    r128_a64_dup_2d_x(e, 7, 16);
    r128_a64_add_2d(e, 27, 27, 7);
    r128_a64_add_2d(e, 31, 31, 7);
    r128_a64_ldr_x(e, 16, 0, (int) offsetof(r128_jit_tri_t, e2b));
    r128_a64_dup_2d_x(e, 7, 16);
    r128_a64_add_2d(e, 14, 14, 7);
    r128_a64_add_2d(e, 15, 15, 7);

    /* weights: (float)e * invs per lane. int64->double is exact at these
       magnitudes, so the double->float narrowing is the same single
       rounding as the scalar scvtf s,x. */
    r128_a64_scvtf_2d(e, 25, 25);
    r128_a64_scvtf_2d(e, 26, 26);
    r128_a64_fcvtn(e, 25, 25);
    r128_a64_fcvtn2(e, 25, 26);
    r128_a64_fmul_4s(e, 25, 25, 1); /* w0 */
    r128_a64_scvtf_2d(e, 27, 27);
    r128_a64_scvtf_2d(e, 31, 31);
    r128_a64_fcvtn(e, 27, 27);
    r128_a64_fcvtn2(e, 27, 31);
    r128_a64_fmul_4s(e, 27, 27, 1); /* w1 */
    r128_a64_scvtf_2d(e, 14, 14);
    r128_a64_scvtf_2d(e, 15, 15);
    r128_a64_fcvtn(e, 14, 14);
    r128_a64_fcvtn2(e, 14, 15);
    r128_a64_fmul_4s(e, 14, 14, 1); /* w2 */
}

/* Aux scissors, in the interpreter's order: after coverage, before Z.
   The row's y-active rect mask is at R128_A64_SP_AUX, written by the
   shared row code. Each y-active rect's x window removes lanes from
   the cover mask: a subtractive rect clears the lanes inside it, and
   when additive rects are enabled a lane survives only if it is inside
   at least one y-active additive rect. The lane x vector px+{0..3} is
   in v26; w16 holds the y mask (it is free until the kmask move); v7
   is free because zi is not built yet. The rect coordinates are
   immediates, as in the scalar loop. */
static void
r128_a64_soa_lp_aux(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t            *e       = l->e;
    const rage128_draw_state_t *ds      = l->ds;
    int                         aux_add = 0, i, k;

    for (i = 0; i < 3; i++)
        if ((ds->aux_cntl & (1u << (i * 2)))
            && !(ds->aux_cntl & (2u << (i * 2))))
            aux_add |= 1 << i;
    r128_a64_ldr_w(e, 16, 31, R128_A64_SP_AUX);
    r128_a64_movi_4s_zero(e, 15);
    for (k = 1; k < 4; k++) {
        r128_a64_movz_w(e, 17, (uint32_t) k, 0);
        r128_a64_ins_s_w(e, 15, k, 17);
    }
    r128_a64_dup_4s_w(e, 26, 12);
    r128_a64_add_4s(e, 26, 26, 15); /* x lanes              */
    for (i = 0; i < 3; i++) {
        int b1;

        if (!(ds->aux_cntl & (2u << (i * 2))))
            continue; /* enabled subtractive rects only */
        b1 = r128_a64_tbz(e, 16, i);
        r128_a64_mov_w_s14(e, 17, ds->aux_x0[i]);
        r128_a64_dup_4s_w(e, 31, 17);
        r128_a64_cmge_4s(e, 15, 26, 31); /* x >= x0              */
        r128_a64_mov_w_s14(e, 17, ds->aux_x1[i]);
        r128_a64_dup_4s_w(e, 31, 17);
        r128_a64_cmge_4s(e, 31, 31, 26); /* x <= x1              */
        r128_a64_and_16b(e, 15, 15, 31); /* inside               */
        r128_a64_not_16b(e, 15, 15);
        r128_a64_and_16b(e, 3, 3, 15); /* reject inside lanes  */
        r128_a64_patch14(e, b1, r128_a64_here(e));
    }
    if (aux_add) {
        /* additive rects: a lane must sit inside a y-active one */
        r128_a64_movi_4s_zero(e, 7);
        for (i = 0; i < 3; i++) {
            int b1;

            if (!(aux_add & (1 << i)))
                continue;
            b1 = r128_a64_tbz(e, 16, i);
            r128_a64_mov_w_s14(e, 17, ds->aux_x0[i]);
            r128_a64_dup_4s_w(e, 31, 17);
            r128_a64_cmge_4s(e, 15, 26, 31);
            r128_a64_mov_w_s14(e, 17, ds->aux_x1[i]);
            r128_a64_dup_4s_w(e, 31, 17);
            r128_a64_cmge_4s(e, 31, 31, 26);
            r128_a64_and_16b(e, 15, 15, 31);
            r128_a64_orr_16b(e, 7, 7, 15);
            r128_a64_patch14(e, b1, r128_a64_here(e));
        }
        r128_a64_and_16b(e, 3, 3, 7);
    }
}

/* zline lanes through the same serial chain of double adds as the C
   loop increment, so the sums are bit-exact; the lanes are assembled
   in FP registers. d0 advances to the next group base here, so no bail
   may follow this point. Then the quantize and the Z test. */
static void
r128_a64_soa_lp_z(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t            *e    = l->e;
    const rage128_draw_state_t *ds   = l->ds;
    int                         sten = l->sten, zquant = l->zquant;

    r128_a64_ins_elem_d(e, 7, 0, 0, 0);
    r128_a64_fadd_d(e, 26, 0, 2);
    r128_a64_ins_elem_d(e, 7, 1, 26, 0);
    r128_a64_fadd_d(e, 26, 26, 2);
    r128_a64_ins_elem_d(e, 15, 0, 26, 0);
    r128_a64_fadd_d(e, 26, 26, 2);
    r128_a64_ins_elem_d(e, 15, 1, 26, 0);
    r128_a64_fadd_d(e, 0, 26, 2);

    if (ds->fog_en && ds->fog_table_en) {
        /* table fog: store the raw zline lanes before the quantize and
           test consume v7/v15; the fog stage derives zc again with its
           own clamp */
        r128_a64_str_q(e, 7, 31, R128_A64_SP_SOA_TFA);
        r128_a64_str_q(e, 15, 31, R128_A64_SP_SOA_TFB);
    }

    if (zquant) {
        /* clamp: NaN or <= 0 -> 0 with a compare-mask AND (like the
           scalar FCSEL: FCMGT is false on unordered), > 1 -> 1 with a
           BSL select */
        r128_a64_fcmgt0_2d(e, 26, 7);
        r128_a64_and_16b(e, 7, 7, 26);
        r128_a64_fcmgt0_2d(e, 26, 15);
        r128_a64_and_16b(e, 15, 15, 26);
        r128_a64_fcmgt_2d(e, 26, 7, 18);
        r128_a64_bsl_16b(e, 26, 18, 7); /* zcA */
        r128_a64_fcmgt_2d(e, 31, 15, 18);
        r128_a64_bsl_16b(e, 31, 18, 15); /* zcB */
        /* zq = zc*zmax + 0.5, clamp > zmax -> zmax, then to uint32 */
        r128_a64_fmul_2d(e, 26, 26, 16);
        r128_a64_fadd_2d(e, 26, 26, 17);
        r128_a64_fmul_2d(e, 31, 31, 16);
        r128_a64_fadd_2d(e, 31, 31, 17);
        r128_a64_fcmgt_2d(e, 15, 26, 16);
        r128_a64_bsl_16b(e, 15, 16, 26);
        r128_a64_fcmgt_2d(e, 7, 31, 16);
        r128_a64_bsl_16b(e, 7, 16, 31);
        r128_a64_fcvtzu_2d(e, 15, 15);
        r128_a64_fcvtzu_2d(e, 7, 7);
        r128_a64_uzp1_4s(e, 7, 15, 7); /* v7 = zi 4S */
    }

    if (!sten && ds->zfn != 7) {
        /* Z buffer lanes (the group is in bounds, so the whole 8- or
           16-byte load is safe; data of masked-out lanes is
           discarded) */
        if (ds->zbpp == 2) {
            r128_a64_ldr_d(e, 26, 26, 0);
            r128_a64_uxtl_4s(e, 26, 26); /* the gate requires
                                            zmax == 0xffff at 16 bpp */
        } else {
            r128_a64_ldr_q(e, 26, 26, 0);
            if (ds->zshift)
                r128_a64_ushr_4s(e, 26, 26, ds->zshift);
            if (ds->zmax != 0xffffffffu) {
                r128_a64_movz_w(e, 17, ds->zmax & 0xffff, 0);
                if (ds->zmax >> 16)
                    r128_a64_movk_w(e, 17, ds->zmax >> 16, 1);
                r128_a64_dup_4s_w(e, 31, 17);
                r128_a64_and_16b(e, 26, 26, 31);
            }
        }
        /* pass mask per r3d_cmp (unsigned), ANDed into cover */
        /* clang-format off */
        switch (ds->zfn & 7) {
            case 1: r128_a64_cmhi_4s(e, 26, 26, 7); break; /* zi <  zb */
            case 2: r128_a64_cmhs_4s(e, 26, 26, 7); break; /* zi <= zb */
            case 3: r128_a64_cmeq_4s(e, 26, 26, 7); break;
            case 4: r128_a64_cmhs_4s(e, 26, 7, 26); break; /* zi >= zb */
            case 5: r128_a64_cmhi_4s(e, 26, 7, 26); break; /* zi >  zb */
                /* clang-format on */
            default: /* 6 */
                r128_a64_cmeq_4s(e, 26, 26, 7);
                r128_a64_not_16b(e, 26, 26);
                break;
        }
        r128_a64_and_16b(e, 3, 3, 26);
    }
}

/* Untextured alpha test. col[3] is the plain weight dot product
   (specular and fog change RGB only), so it can be computed here; the
   pass mask is ANDed into the cover mask so failed lanes never widen
   rx0/rx1. Same operation order as the channel pack, then the scalar
   alpha test's quantize and compare. */
static void
r128_a64_soa_lp_atest(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t            *e  = l->e;
    const rage128_draw_state_t *ds = l->ds;

    r128_a64_fmul_4s_elem(e, 26, 25, 4, 3);
    r128_a64_fmul_4s_elem(e, 15, 27, 5, 3);
    r128_a64_fadd_4s(e, 26, 26, 15);
    r128_a64_fmul_4s_elem(e, 15, 14, 6, 3);
    r128_a64_fadd_4s(e, 26, 26, 15);
    r128_a64_fmul_4s(e, 26, 26, 23);
    r128_a64_fadd_4s(e, 26, 26, 24);
    r128_a64_fcvtzu_4s(e, 26, 26);
    r128_a64_soa_atest_mask(e, ds, 15, 26, 31);
    r128_a64_and_16b(e, 3, 3, 15);
}

/* kmask: one move from NEON to a general register per group, 16 bits
   per lane (XTN to 4H, then FMOV). Then rx0/rx1 from the lane mask.
   Chroma-key and alpha-test states do the rx update after the texture
   stage has applied its masks; stencil states after the stencil stage
   has built the write mask. */
static void
r128_a64_soa_lp_kmask(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t *e     = l->e;
    int              defer = l->defer, sten = l->sten;

    if (defer)
        /* the texture stage's key compare and alpha test AND their
           pass lanes into this stash; rx0/rx1 wait for the mask reload
           in r128_a64_soa_lp_color_tex */
        r128_a64_str_q(e, 3, 31, l->cover);
    r128_a64_xtn_4h(e, 26, 3);
    r128_a64_fmov_x_d(e, 16, 26);
    l->b_adv0 = r128_a64_cbz_x(e, 16);

    if (!defer && !sten)
        r128_a64_soa_rx(e);
}

/* Textured group. zi is kept in the frame across the texture stage,
   which uses v7 as scratch, and so is the dst cell base: the per-lane
   texel fetches return their texel in w27, the low half of x27. The
   packed lanes come back in v25. */
static void
r128_a64_soa_lp_color_tex(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t            *e    = l->e;
    const rage128_draw_state_t *ds   = l->ds;
    int                         z_on = l->z_on, sten = l->sten;
    int                         defer = l->defer, cover = l->cover;

    if (z_on && ds->z_wr)
        r128_a64_str_q(e, 7, 31, R128_A64_SP_SOA_ZI);
    r128_a64_str_x(e, 27, 31, R128_A64_SP_SOA_DC);
    r128_a64_emit_soa_texstage(e, ds);
    r128_a64_ldr_x(e, 27, 31, R128_A64_SP_SOA_DC);
    if (z_on && ds->z_wr && !ds->alpha_en)
        r128_a64_ldr_q(e, 7, 31, R128_A64_SP_SOA_ZI);
    if (defer) {
        /* Mask after the key compare and alpha test. If every lane is
           rejected, the group advances with no store and no rx update.
           This holds for stencil groups too: a pixel rejected before
           the stencil stage runs no stencil op. A stencil group with
           any lane left goes on to the stencil stage, and its rx
           update waits for the write mask. */
        r128_a64_ldr_q(e, 26, 31, cover);
        r128_a64_xtn_4h(e, 26, 26);
        r128_a64_fmov_x_d(e, 16, 26);
        l->b_adv1 = r128_a64_cbz_x(e, 16);
        if (!sten)
            r128_a64_soa_rx(e);
    }
    if (ds->spec_en || ds->fog_en) {
        /* spec/fog stage on the float channels in the SRC slots: the
           weights come back from W0..W2 (stored by the dual head or by
           the single-stage spec/fog path); x16 and v7 are not
           touched. */
        r128_a64_ldr_q(e, 25, 31, R128_A64_SP_SOA_W0);
        r128_a64_ldr_q(e, 27, 31, R128_A64_SP_SOA_W1);
        r128_a64_ldr_q(e, 14, 31, R128_A64_SP_SOA_W2);
        if (ds->fog_en)
            /* always the vertex fog dot product: textured table fog
               forces the rage128_texstage_run call path, which never
               reaches the vector loop */
            r128_a64_soa_fogf(e);
        r128_a64_soa_specfog(e, ds);
        if (!ds->alpha_en)
            r128_a64_soa_pack_slots(e, ds);
    }
    if (ds->alpha_en) {
        /* dst read, blend and pack (the blend uses v7, so zi stays in
           the frame until it finishes) */
        r128_a64_emit_soa_blend(e, ds);
        if (z_on && ds->z_wr)
            r128_a64_ldr_q(e, 7, 31, R128_A64_SP_SOA_ZI);
    }
}

/* Untextured blend and/or spec/fog. The float channel dot products go
   to the SRC slots (these blocks take the 480-byte frame so the slots
   exist), then the spec/fog stage, then the shared dst read, blend and
   pack, or the slot pack when blend is off. zi is kept in the frame
   around the blend, which uses v7; the spec/fog helpers and the slot
   pack do not touch v7. */
static void
r128_a64_soa_lp_color_slots(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t            *e       = l->e;
    const rage128_draw_state_t *ds      = l->ds;
    int                         z_on    = l->z_on, k;
    static const int            bslo[4] = { R128_A64_SP_SOA_SRCR,
                                            R128_A64_SP_SOA_SRCG,
                                            R128_A64_SP_SOA_SRCB,
                                            R128_A64_SP_SOA_SRCA };

    if (z_on && ds->z_wr && ds->alpha_en)
        r128_a64_str_q(e, 7, 31, R128_A64_SP_SOA_ZI);
    for (k = 0; k < 4; k++) {
        r128_a64_fmul_4s_elem(e, 26, 25, 4, k);
        r128_a64_fmul_4s_elem(e, 15, 27, 5, k);
        r128_a64_fadd_4s(e, 26, 26, 15);
        r128_a64_fmul_4s_elem(e, 15, 14, 6, k);
        r128_a64_fadd_4s(e, 26, 26, 15);
        r128_a64_str_q(e, 26, 31, bslo[k]);
    }
    if (ds->fog_en) {
        if (ds->fog_table_en)
            r128_a64_soa_tfogf(e);
        else
            r128_a64_soa_fogf(e);
    }
    if (ds->spec_en || ds->fog_en)
        r128_a64_soa_specfog(e, ds);
    if (ds->alpha_en) {
        r128_a64_emit_soa_blend(e, ds);
        if (z_on && ds->z_wr)
            r128_a64_ldr_q(e, 7, 31, R128_A64_SP_SOA_ZI);
    } else
        r128_a64_soa_pack_slots(e, ds);
}

/* Plain pack: 8888 out = a<<24 | r<<16 | g<<8 | b, or 565 raw =
   (r>>3)<<11 | (g>>2)<<5 | (b>>3) with alpha unused. This path runs
   without blend, specular or fog, so every channel is at most 255 (the
   dither note in r128_a64_gen_pack_store says why) and the shift/OR
   pack equals the C's build of out, in which a channel above 255 could
   carry into the next byte. Alpha-bearing 16-bit formats use their own
   field shifts and alpha quantization below. */
static void
r128_a64_soa_lp_color_pack(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t            *e       = l->e;
    const rage128_draw_state_t *ds      = l->ds;
    int                         dith_on = l->dith_on;

    if (ds->dst_dt == 6) {
        r128_a64_soa_channel(e, 26, 31, 25, 27, 14, 3, 0, 0); /* a */
        r128_a64_shl_4s(e, 26, 26, 24);
        r128_a64_soa_channel(e, 15, 31, 25, 27, 14, 0, 0, 0); /* r */
        r128_a64_shl_4s(e, 15, 15, 16);
        r128_a64_orr_16b(e, 26, 26, 15);
        r128_a64_soa_channel(e, 15, 31, 25, 27, 14, 1, 0, 0); /* g */
        r128_a64_shl_4s(e, 15, 15, 8);
        r128_a64_orr_16b(e, 26, 26, 15);
        r128_a64_soa_channel(e, 15, 31, 25, 27, 14, 2, 0, 0); /* b */
        r128_a64_orr_16b(e, 25, 26, 15);
    } else if (ds->dst_dt == 3 || ds->dst_dt == 15) {
        /* Plain shading quantizes to bytes before the field shifts.
           The 1555 alpha threshold uses bit seven without dithering;
           4444 adds the full Bayer threshold to every channel. The
           narrowed lanes use the common 16-bit masked store paths. */
        int bits = ds->dst_dt == 3 ? 5 : 4;

        r128_a64_soa_channel(e, 26, 31, 25, 27, 14, 3,
                             dith_on && ds->dst_dt == 15, 29);
        r128_a64_ushr_4s(e, 26, 26, ds->dst_dt == 3 ? 7 : 4);
        r128_a64_shl_4s(e, 26, 26, bits * 3);
        for (int ch = 0; ch < 3; ch++) {
            r128_a64_soa_channel(e, 15, 31, 25, 27, 14, ch, dith_on, 29);
            r128_a64_ushr_4s(e, 15, 15, 8 - bits);
            if (ch != 2)
                r128_a64_shl_4s(e, 15, 15, (2 - ch) * bits);
            r128_a64_orr_16b(e, 26, 26, 15);
        }
        r128_a64_xtn_4h(e, 25, 26);
    } else {
        r128_a64_soa_channel(e, 26, 31, 25, 27, 14, 0, dith_on, 29); /* r */
        r128_a64_ushr_4s(e, 26, 26, 3);
        r128_a64_shl_4s(e, 26, 26, 11);
        r128_a64_soa_channel(e, 15, 31, 25, 27, 14, 1, dith_on, 30); /* g */
        r128_a64_ushr_4s(e, 15, 15, 2);
        r128_a64_shl_4s(e, 15, 15, 5);
        r128_a64_orr_16b(e, 26, 26, 15);
        r128_a64_soa_channel(e, 15, 31, 25, 27, 14, 2, dith_on, 29); /* b */
        r128_a64_ushr_4s(e, 15, 15, 3);
        r128_a64_orr_16b(e, 25, 26, 15);
        r128_a64_xtn_4h(e, 25, 25);
    }
}

/* Stores. A group with all four lanes set takes one contiguous vector
   store; any other group goes through the frame and stores lane by
   lane under TBZ. A partial PLANE_3D_MASK_C write mask merges against
   the packed pixels after dithering, as r3d_dst_write does: BSL takes
   the masked bits from the new pixels and the rest from the reloaded
   old ones. */
static void
r128_a64_soa_lp_store_full(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t            *e    = l->e;
    const rage128_draw_state_t *ds   = l->ds;
    int                         z_on = l->z_on;

    r128_a64_cmn_x_imm(e, 16, 1);
    l->b_part = r128_a64_bcond(e, A64_NE);
    if (ds->dst_dt == 6) {
        if (ds->wmask != 0xffffffffu) {
            r128_a64_ldr_q(e, 15, 27, 0);
            r128_a64_mov_w_imm32(e, 17, ds->wmask);
            r128_a64_dup_4s_w(e, 26, 17);
            r128_a64_bsl_16b(e, 26, 25, 15);
            r128_a64_str_q(e, 26, 27, 0);
        } else
            r128_a64_str_q(e, 25, 27, 0);
    } else {
        if ((ds->wmask & 0xffff) != 0xffff) {
            uint32_t m = ds->wmask & 0xffff;

            r128_a64_ldr_d(e, 15, 27, 0);
            r128_a64_mov_w_imm32(e, 17, m | (m << 16));
            r128_a64_dup_4s_w(e, 26, 17);
            r128_a64_bsl_16b(e, 26, 25, 15);
            r128_a64_str_d(e, 26, 27, 0);
        } else
            r128_a64_str_d(e, 25, 27, 0);
    }
    if (z_on && ds->z_wr) {
        if (ds->zbpp == 2) {
            r128_a64_xtn_4h(e, 7, 7);
            r128_a64_str_d(e, 7, 26, 0);
        } else {
            uint32_t keep = ~(ds->zmax << ds->zshift);

            if (ds->zshift)
                r128_a64_shl_4s(e, 7, 7, ds->zshift);
            if (keep != 0) {
                r128_a64_ldr_q(e, 15, 26, 0);
                r128_a64_movz_w(e, 17, keep & 0xffff, 0);
                if (keep >> 16)
                    r128_a64_movk_w(e, 17, keep >> 16, 1);
                r128_a64_dup_4s_w(e, 31, 17);
                r128_a64_and_16b(e, 15, 15, 31);
                r128_a64_orr_16b(e, 7, 7, 15);
            }
            r128_a64_str_q(e, 7, 26, 0);
        }
    }
    l->b_full = r128_a64_b(e);
}

/* partial group: packed color (and zi) through the frame, then
   per-lane stores */
static void
r128_a64_soa_lp_store_partial(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t            *e    = l->e;
    const rage128_draw_state_t *ds   = l->ds;
    int                         z_on = l->z_on, k;
    int                         lanebr[4];

    r128_a64_patch19(e, l->b_part, r128_a64_here(e));
    if (ds->dst_dt == 6)
        r128_a64_str_q(e, 25, 31, R128_A64_SP_COL);
    else
        r128_a64_str_d(e, 25, 31, R128_A64_SP_COL);
    if (z_on && ds->z_wr) {
        if (ds->zbpp == 2) {
            r128_a64_xtn_4h(e, 7, 7);
            r128_a64_str_d(e, 7, 31, R128_A64_SP_SOAZ);
        } else
            r128_a64_str_q(e, 7, 31, R128_A64_SP_SOAZ);
    }
    if ((ds->dst_dt == 6) ? ds->wmask != 0xffffffffu
                          : (ds->wmask & 0xffff) != 0xffff)
        /* write-mask constant for the lane stores, set once here (w25
           is free from the group address resolve to the loop
           back-branch) */
        r128_a64_mov_w_imm32(e, 25, (ds->dst_dt == 6) ? ds->wmask : (ds->wmask & 0xffff));
    for (k = 0; k < 4; k++) {
        lanebr[k] = r128_a64_tbz_any(e, 16, 16 * k);
        if (ds->dst_dt == 6) {
            r128_a64_ldr_w(e, 17, 31, R128_A64_SP_COL + 4 * k);
            if (ds->wmask != 0xffffffffu) {
                r128_a64_ldr_w(e, 28, 27, 4 * k);
                r128_a64_and_w_reg(e, 17, 17, 25);
                r128_a64_bic_w_reg(e, 28, 28, 25);
                r128_a64_orr_w_lsl(e, 17, 17, 28, 0);
            }
            r128_a64_str_w(e, 17, 27, 4 * k);
        } else {
            r128_a64_ldrh(e, 17, 31, R128_A64_SP_COL + 2 * k);
            if ((ds->wmask & 0xffff) != 0xffff) {
                r128_a64_ldrh(e, 28, 27, 2 * k);
                r128_a64_and_w_reg(e, 17, 17, 25);
                r128_a64_bic_w_reg(e, 28, 28, 25);
                r128_a64_orr_w_lsl(e, 17, 17, 28, 0);
            }
            r128_a64_strh(e, 17, 27, 2 * k);
        }
        if (z_on && ds->z_wr) {
            if (ds->zbpp == 2) {
                r128_a64_ldrh(e, 17, 31, R128_A64_SP_SOAZ + 2 * k);
                r128_a64_strh(e, 17, 26, 2 * k);
            } else {
                uint32_t keep = ~(ds->zmax << ds->zshift);

                r128_a64_ldr_w(e, 17, 31, R128_A64_SP_SOAZ + 4 * k);
                if (keep == 0) {
                    if (ds->zshift)
                        r128_a64_lsl_w(e, 17, 17, ds->zshift);
                    r128_a64_str_w(e, 17, 26, 4 * k);
                } else {
                    r128_a64_ldr_w(e, 28, 26, 4 * k);
                    r128_a64_and_w_bitmask(e, 28, 28, keep);
                    r128_a64_orr_w_lsl(e, 28, 28, 17, ds->zshift);
                    r128_a64_str_w(e, 28, 26, 4 * k);
                }
            }
        }
        r128_a64_patch14(e, lanebr[k], r128_a64_here(e));
    }

    r128_a64_patch26(e, l->b_full, r128_a64_here(e));
}

/* advance: exact integer group step (e += 4*dx); d0 already advanced
   in r128_a64_soa_lp_z when z_on */
static void
r128_a64_soa_lp_advance(r128_a64_soa_lp_t *l)
{
    r128_a64_emit_t *e   = l->e;
    int              adv = r128_a64_here(e);

    r128_a64_patch19(e, l->b_adv0, adv);
    if (l->b_adv1 >= 0)
        r128_a64_patch19(e, l->b_adv1, adv);
    if (l->b_adv2 >= 0)
        r128_a64_patch19(e, l->b_adv2, adv);
    r128_a64_add_x_lsl(e, 1, 1, 7, 2);
    r128_a64_add_x_lsl(e, 2, 2, 8, 2);
    r128_a64_add_x_lsl(e, 3, 3, 9, 2);
    r128_a64_add_w_imm(e, 12, 12, 4);
    r128_a64_b_to(e, l->head);
}

static void
r128_a64_emit_soa_loop(r128_a64_emit_t *e, const rage128_draw_state_t *ds,
                       int *bails, int *nbail)
{
    r128_a64_soa_lp_t l;

    r128_a64_soa_lp_setup(&l, e, ds, bails, nbail);

    if (l.sten || l.zquant)
        r128_a64_soa_lp_rowguard(&l);
    r128_a64_soa_lp_head(&l);
    r128_a64_soa_lp_cover(&l);
    if (ds->aux_on)
        r128_a64_soa_lp_aux(&l);
    if (l.z_on)
        r128_a64_soa_lp_z(&l);
    if (l.sten)
        /* stencil: the Z result must not drop lanes from the cover
           mask, because failed lanes still run the fail-op
           read-modify-write. Store {sbuf | sres<<8 | zres<<9} per lane
           for the stage after the pack; v3 (cover) and v7 (zi) are not
           touched. */
        r128_a64_soa_sten_zblock(e, ds);
    if (!ds->tex_en && l.at_on)
        r128_a64_soa_lp_atest(&l);
    r128_a64_soa_lp_kmask(&l);

    /* color channels -> packed lanes in v25 */
    if (ds->tex_en)
        r128_a64_soa_lp_color_tex(&l);
    else if (ds->alpha_en || ds->spec_en || ds->fog_en)
        r128_a64_soa_lp_color_slots(&l);
    else
        r128_a64_soa_lp_color_pack(&l);

    if (l.sten) {
        /* stencil stage at the interpreter's position (after the alpha
           test, before the color store): op read-modify-write on the
           op-mask lanes, then x16 becomes the write mask. A group with
           every lane discarded advances after the read-modify-write
           with no store and no rx update, like the scalar loop's
           continue. */
        r128_a64_emit_soa_sten(e, ds);
        l.b_adv2 = r128_a64_cbz_x(e, 16);
        r128_a64_soa_rx(e);
    }

    r128_a64_soa_lp_store_full(&l);
    r128_a64_soa_lp_store_partial(&l);
    r128_a64_soa_lp_advance(&l);
}

#endif /* VIDEO_ATI_RAGE128_CODEGEN_ARM64_SOA_H */

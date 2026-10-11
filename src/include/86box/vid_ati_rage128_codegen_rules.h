/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- draw-state rules shared by the span JIT
 *          backends.
 *
 *          The ARM64 and x86-64 span JITs, in both their scalar loops
 *          and their vector (SoA) loops, use these functions to decide
 *          which draw states they compile and which filter path each
 *          texture stage takes. The ARM64 vector code also uses them in
 *          its block-size estimate and in its stack frame layout. Each
 *          rule is written once so that the two backends compile the
 *          same set of states, and so that the size estimate and the
 *          frame layout classify a stage exactly as the emitter does.
 *          If the frame layout read a stage differently, the stack
 *          slots it reserves would not match the code emitted.
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
#ifndef VIDEO_ATI_RAGE128_CODEGEN_RULES_H
#define VIDEO_ATI_RAGE128_CODEGEN_RULES_H

/* Texture minification filter. minb is the 3-bit field
   PRIM_TEX_CNTL_C:PRIM_MIN_BLEND_FCN, whose documented codes are
   (SDK: Texture Mapping, p. 6-40 / PDF 152, Table 6-4; xf86-video-r128
   defines the same codes as R128_MIN_BLEND_NEAREST through
   R128_MIN_BLEND_LINEARMIPLINEAR):

     0  nearest texel in the largest map
     1  bilinear in the largest map
     2  nearest texel in the nearest mip level
     3  bilinear in the nearest mip level
     4  "1x1 filtering" in the SDK; Mesa r128 r128_tex.c r128SetTexFilter
        programs it for OpenGL's nearest-mipmap-linear minification
        filter, and the interpreter models it that way: nearest texels,
        blended between two levels
     5  trilinear

   Codes 6 and 7 are not documented. The SDK says codes 2 to 5 are
   valid only with mipmapping enabled (PRIM_MIP_MAP_DIS clear).

   The emitters must match the interpreter's sampler, r3d_tex_sample,
   bit for bit; it is the function they are checked against. It takes
   the mip path only when the stage has a per-pixel LOD, mipmapping is
   enabled and the code is 2 or more. On that path a minified pixel
   gets a bilinear texel filter for codes 3 and 5 only and a blend of
   two levels for codes 4 and 5 only, so codes 6 and 7 sample one level
   with nearest texels, like code 2. Off the mip path the minification
   texel filter is bit 0 of the code. Bit 0 alone would also give the
   right answer on the mip path for every code except 7, which is why
   the mip-path test names codes 3 and 5. */
static inline int
r128_jit_minb_mip_on(uint32_t minb, int mipdis, int has_lod)
{
    return has_lod && !mipdis && minb >= 2;
}

static inline int
r128_jit_minb_lin_min(uint32_t minb, int mipdis, int has_lod)
{
    return r128_jit_minb_mip_on(minb, mipdis, has_lod)
        ? (minb == 3 || minb == 5)
        : (int) (minb & 1);
}

static inline int
r128_jit_minb_tri(uint32_t minb, int mipdis, int has_lod)
{
    return r128_jit_minb_mip_on(minb, mipdis, has_lod)
        && (minb == 4 || minb == 5);
}

/* With a per-pixel LOD, the interpreter filters a minified pixel
   (LOD above 0) with the minification texel filter and a magnified one
   with the magnification filter. mag is the stage's decoded bilinear
   flag from r3d_stage_hdr_init: for the primary stage bit 0 of
   PRIM_TEX_CNTL_C:PRIM_MAG_BLEND_FCN, set for every bilinear code
   (SDK: Texture Mapping, p. 6-40 / PDF 152, Table 6-5); the secondary
   stage's field has its own code book, decoded there. This is true
   when the two filters differ: the vector loops then filter every
   lane with the minification filter and redo the magnified lanes with
   the magnification filter. */
static inline int
r128_jit_minb_split(uint32_t minb, int mipdis, uint32_t mag, int has_lod)
{
    return has_lod
        && (r128_jit_minb_lin_min(minb, mipdis, has_lod) != (mag == 1));
}

/* Texture datatypes whose texel decode both backends emit inline and
   reproduce exactly. dt is PRIM_TEX_CNTL_C:PRIMARY_DATATYPE (SDK:
   Texture Mapping, p. 6-39 / PDF 151, Table 6-3):

     1, 2         4-bpp and 8-bpp palette indices
     3, 4, 6, 15  ARGB 1555, RGB 565, ARGB 8888, ARGB 4444
     5            24-bpp RGB, 3 bytes per texel
     7, 8, 9      RGB 332, Y8 gray scale, RGB8 gray scale
     11, 12       YUV 422 packed
     14           AYUV 444
     0            the SDK names it 2-bpp VQ; the interpreter decodes it
                  as S3TC blocks for all four values of its block-class
                  field

   Datatype 10, 16-bpp pseudo color, has no decoder in the emitters or
   in the interpreter (which samples it as opaque white). It and 13,
   which the SDK does not list, keep the per-pixel call to
   rage128_texstage_run. */
static inline int
r128_jit_dt_inline_family(uint32_t dt)
{
    switch (dt) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 11:
        case 12:
        case 14:
        case 15:
            return 1;
        default:
            return 0;
    }
}

/* True when at least one texture stage is on and every stage that is
   on has an inline datatype. S3TC and YUV texels decode through a
   subroutine emitted once per stage, the other datatypes in line. The
   texture combine, including the texture-lighting pass, is always
   emitted inline, so the datatype is the only per-stage test. Callers
   refuse table fog on their own. */
static inline int
r128_jit_texinline_can(const rage128_draw_state_t *ds)
{
    if (!ds->tex_en && !ds->sec_en)
        return 0;
    if (ds->tex_en && !r128_jit_dt_inline_family(ds->sh[0].dt))
        return 0;
    if (ds->sec_en && !r128_jit_dt_inline_family(ds->sh[1].dt))
        return 0;
    return 1;
}

/* An alpha test or Z test that never passes. Function code 0 means
   "never pass" for both (SDK: Setting 3D Render States, p. 6-51 /
   PDF 163, Table 6-20 ALPHA_TEST_OP; SDK: Setting 3D Render States,
   p. 6-55 / PDF 167, Table 6-23 Z_TEST). The interpreter then rejects
   every pixel before any write, so the compiled block only returns an
   empty written range and the rest of the state does not matter. In
   the interpreter the alpha test comes before the stencil update, so a
   never-passing alpha test writes nothing even with stencil on. A
   failed Z test still applies the stencil zfail operation, so a
   never-passing Z test qualifies only with stencil off. */
static inline int
r128_jit_never_pass(const rage128_draw_state_t *ds)
{
    if (ds->atest_en && (ds->atest_fn & 7) == 0)
        return 1;
    return !ds->sten_on && ds->z_en && ds->zfn == 0;
}

/* The part of the compile gate common to both backends: the draw
   states a compiled block reproduces exactly. Anything refused here
   stays on the interpreter. draw_ok is the interpreter's own
   destination check (ARGB 1555, RGB 565, ARGB 8888 or ARGB 4444). A
   never-passing state that passes draw_ok is accepted before the other
   tests, since its block has no pixel code. Refused:

   - a destination outside the four modeled direct-color formats;
     the pixel tails pack only ARGB 1555, RGB 565, ARGB 8888 and
     ARGB 4444, matching the interpreter's destination check.
   - with Z or stencil on, a Z buffer that is neither 16-bit with the
     full 0xffff depth range nor 32-bit with a depth shift below 32.

   The ARM64 gate adds one test: the mask that keeps the stencil bits
   in a 32-bit Z write must be encodable as an AND bitmask
   immediate. */
static inline int
r128_jit_state_can(const rage128_draw_state_t *ds)
{
    if (!ds->draw_ok)
        return 0;
    if (r128_jit_never_pass(ds))
        return 1;
    if (ds->dst_dt != 3 && ds->dst_dt != 4 && ds->dst_dt != 6 && ds->dst_dt != 15)
        return 0;
    if (ds->z_en || ds->sten_on) {
        if (ds->zbpp == 2) {
            if (ds->zmax != 0xffff)
                return 0;
        } else if (ds->zbpp == 4) {
            if (ds->zshift > 31)
                return 0;
        } else
            return 0;
    }
    return 1;
}

/* Which untextured states, among those r128_jit_state_can accepts, get
   the vector loop; the others keep the scalar loop. Textured states go
   through each backend's own textured gate instead. Table fog without
   the Z test keeps the scalar loop, since only the vector Z stage
   saves the raw zline lanes that the vector fog stage clamps and
   indexes. This also applies to stencil-only draws. */
static inline int
r128_jit_soa_can(const rage128_draw_state_t *ds)
{
    if (ds->stip_en || ds->tex_en || ds->sec_en)
        return 0;
    /* The alpha-bearing 16-bit vector pack handles plain shading only;
       blend, specular and fog use channel-slot tails that pack 565 or
       8888, so these combinations keep the scalar loop. */
    if ((ds->dst_dt == 3 || ds->dst_dt == 15)
        && (ds->alpha_en || ds->spec_en || ds->fog_en))
        return 0;
    if (ds->fog_en && ds->fog_table_en && !ds->z_en)
        return 0;
    return 1;
}

/* Whether the secondary stage divides by a W of its own: a different
   perspective enable from the primary stage's, or SEC_SRC_SEL_W (the
   vertex rhw2). The interpreter then forms a second W dot product and
   reciprocal for that stage (rage128_texstage_run); the scalar inline
   emitters emit the same head before stage 1, and the vector loops,
   whose dual head shares one rhw and ir between the stages, leave such
   a state to the scalar loop. */
static inline int
r128_jit_sec_w_own(const rage128_draw_state_t *ds)
{
    return ds->sec_en && (ds->sel_w || ds->sec_persp_diff);
}

/* A stage's perspective enable: the primary's, or the secondary's own
   (do_persp with the difference bit applied). */
static inline int
r128_jit_stage_persp(const rage128_draw_state_t *ds, int st)
{
    return st ? (ds->do_persp ^ ds->sec_persp_diff) : ds->do_persp;
}

/* Whether the vector loop emits an alpha test. Function 0 never gets
   that far, because r128_jit_never_pass makes the whole block an
   immediate return, and function 7 always passes (SDK: Setting 3D
   Render States, p. 6-51 / PDF 163, Table 6-20), so neither needs a
   test. */
static inline int
r128_jit_soa_atest_on(const rage128_draw_state_t *ds)
{
    return ds->atest_en && (ds->atest_fn & 7) != 0 && (ds->atest_fn & 7) != 7;
}

#endif /* VIDEO_ATI_RAGE128_CODEGEN_RULES_H */

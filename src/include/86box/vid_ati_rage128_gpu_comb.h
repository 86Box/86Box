/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- GPU backend, seed list of texture-combine
 *          tuples.
 *
 *          The GPU backend builds one folded pipeline for each pair of
 *          kernel and combine tuple. A tuple holds the seven combine
 *          selectors of each texture stage, in the order of comb_cfg[]
 *          (vid_ati_rage128_gpu_abi.glsl):
 *            {comb, fmsb, cfac, ifac, comba, afac, ifaca}
 *          gpu_tuples_prime interns every row of this table at startup.
 *          With R128_GPU_PRECOMP=full the boot precompile then builds
 *          each row against every kernel of its stage count, 8
 *          one-texture or 8 two-texture kernels, so a row costs 8
 *          pipelines. In the default list mode the boot precompile
 *          builds the learned list instead and the rows are only
 *          interned, so a row costs almost nothing there.
 *
 *          The rows come from what the drivers' combine code can write,
 *          not from tuples seen while running programs. The sources are
 *          Mesa r128 r128_texstate.c r128UpdateTextureEnv, which maps
 *          the GL texture environment onto the combine registers, and
 *          the Direct3D HAL of the Windows 9x driver, which maps each
 *          D3DTOP onto a combine code through tables it sets up
 *          (RE: ati3draa.dll @b00ce170). The HAL writes R128_COMB_FCN_MSB
 *          only on a Pro (RE: ati3draa.dll @b00ea199). The list is not
 *          complete. A draw whose combine selectors are not in it still
 *          renders: the uber kernel serves it while its folded pipeline
 *          compiles in the background, or the unfolded pipeline runs it
 *          once GPU_TUPLE_CAP tuples are interned. gpu_tuple_intern logs
 *          each such tuple as "combine tuple outside enumeration" so it
 *          can be added here.
 *
 *          The values are the fields of PRIM_TEXTURE_COMBINE_CNTL_C for
 *          stage 0 and SEC_TEX_COMBINE_CNTL_C for stage 1, shifted down
 *          to bit 0, with the numbering of the xf86-video-r128 macros:
 *            comb   PRIMARY_COMB_FCN (SDK: Texture Mapping, p. 6-42 /
 *                   PDF 154, Table 6-7): 0 disable, 1 copy, 2 copy
 *                   input, 3 modulate, 4 modulate x2, 5 modulate x4,
 *                   6 add, 7 add signed, 8 blend vertex, 9 blend
 *                   texture, 10 blend constant, 11 blend premultiply,
 *                   12 blend previous, 13 blend premultiply inverse,
 *                   14 add signed x2.
 *            fmsb   R128_COMB_FCN_MSB, bit 8 of the register in
 *                   xf86-video-r128 and Mesa r128. The SDK lists bits
 *                   9:8 as reserved (SDK: Table F-22, p. F-34 / PDF
 *                   324). With it set, codes 0, 4, 5 and 6 take other
 *                   formulas, as r3d_tex_combine models them.
 *            cfac   COLOR_FACTOR (SDK: Texture Mapping, p. 6-43 /
 *                   PDF 155, Table 6-8): 4 texel color, 5 one minus
 *                   texel color, 6 texel alpha. 0 is
 *                   R128_COLOR_FACTOR_CONST_COLOR, which the SDK table
 *                   does not list; Mesa uses it only with fmsb set. The
 *                   HAL writes 6 for the two Direct3D operations that
 *                   add the texel alpha, with fmsb set (RE: ati3draa.dll
 *                   @b00ea1c8).
 *            ifac   INPUT_FACTOR (Table 6-9, same page): 4 interpolated
 *                   color. Stage 1 also has 8, the previous stage's
 *                   color (SDK: Texture Mapping, p. 6-45 / PDF 157,
 *                   Table 6-13).
 *            comba  COMB_FCN_ALPHA (SDK: Texture Mapping, p. 6-44 /
 *                   PDF 156, Table 6-10): codes 0 to 7 and 14 as for
 *                   comb.
 *            afac   ALPHA_FACTOR (Table 6-11, same page): 6 texel
 *                   alpha, 7 one minus texel alpha.
 *            ifaca  INPUT_FACTOR_ALPHA (SDK: Texture Mapping, p. 6-45 /
 *                   PDF 157, Tables 6-12 and 6-14): 2 interpolated
 *                   alpha. Stage 1 also has 4, the previous stage's
 *                   alpha.
 *
 *          Disable outputs the texel and copy outputs COLOR_FACTOR
 *          (Table 6-7); on the first stage the alpha codes work the same
 *          way with ALPHA_FACTOR (Table 6-10). On the second stage alpha
 *          disable is modeled as passing the incoming alpha through, so
 *          there it differs from copy. Every row with code 0 selects the
 *          texel as COLOR_FACTOR and the texel alpha as ALPHA_FACTOR, so
 *          the two color codes, and the two first-stage alpha codes, give
 *          the same result there. Mesa writes 0 to output the texel and
 *          the HAL writes 1 for the Direct3D SELECTARG1 operation.
 *          gpu_tuple_canon folds 1 to 0 for the color code when
 *          COLOR_FACTOR is 4, and for the first stage's alpha code when
 *          ALPHA_FACTOR is 6, before a tuple is interned, so the table
 *          carries only the 0 form; a copy with any other factor, and a
 *          second-stage alpha copy, is its own tuple. An alpha code of 0
 *          never reads INPUT_FACTOR_ALPHA, so gpu_tuple_canon also sets
 *          that field to 2 when the alpha code is 0.
 *
 *          Each row comment names the color op as a GL texture
 *          environment mode or a D3DTOP without its prefix, then the
 *          alpha op in the notation of Mesa's comments:
 *            At     alpha code 0: A = the texel alpha (on the
 *                   second stage, the incoming alpha)
 *            Af     alpha code 2: A = the input alpha
 *            AfAt   alpha code 3: the two multiplied
 *          Direct3D sets the color op and the alpha op of a stage
 *          separately, so a stage can pair any color op with any alpha
 *          op, and the table carries each color op in all three alpha
 *          columns.
 *
 *          Included only by vid_ati_rage128_gpu.c.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 Software Development Guide",
 *              SDK-G04000 Rev 0.01, August 1999. Cited as "SDK: ...".
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */

typedef struct r128_gpu_comb_seed_t {
    uint8_t stages;  /* 1 or 2; a 1-stage row has zero stage-1 selectors */
    uint8_t sel[14]; /* stage 0's seven selectors, then stage 1's */
} r128_gpu_comb_seed_t;

static const r128_gpu_comb_seed_t r128_gpu_comb_seed[] = {
    /* One stage: the color ops of both drivers, each in the three alpha
       columns, which stand for the D3D alpha ops DISABLE and SELECTARG1
       (At), SELECTARG2 (Af) and MODULATE (AfAt). The inputs are stage
       0's, the interpolated color (4) and alpha (2). Where no D3DTOP or
       GL mode matches a row with fmsb set, the row comment gives the
       formula r3d_tex_combine models for it. */
    /* clang-format off */
    { 1, {  0,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* SELECTARG1/REPLACE x At */
    { 1, {  0,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* SELECTARG1/REPLACE x Af */
    { 1, {  0,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* SELECTARG1/REPLACE x AfAt */
    { 1, {  2,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* SELECTARG2 x At */
    { 1, {  2,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* SELECTARG2 x Af */
    { 1, {  2,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* SELECTARG2 x AfAt */
    { 1, {  3,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* MODULATE x At */
    { 1, {  3,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* MODULATE x Af */
    { 1, {  3,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* MODULATE x AfAt */
    { 1, {  4,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* MODULATE2X x At */
    { 1, {  4,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* MODULATE2X x Af */
    { 1, {  4,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* MODULATE2X x AfAt */
    { 1, {  5,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* MODULATE4X x At */
    { 1, {  5,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* MODULATE4X x Af */
    { 1, {  5,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* MODULATE4X x AfAt */
    { 1, {  6,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADD x At */
    { 1, {  6,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADD x Af */
    { 1, {  6,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADD x AfAt */
    { 1, {  7,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADDSIGNED x At */
    { 1, {  7,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADDSIGNED x Af */
    { 1, {  7,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADDSIGNED x AfAt */
    { 1, { 14,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADDSIGNED2X x At */
    { 1, { 14,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADDSIGNED2X x Af */
    { 1, { 14,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADDSIGNED2X x AfAt */
    { 1, {  8,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDDIFFUSEALPHA x At */
    { 1, {  8,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDDIFFUSEALPHA x Af */
    { 1, {  8,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDDIFFUSEALPHA x AfAt */
    { 1, {  9,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDTEXTUREALPHA/DECAL x At */
    { 1, {  9,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDTEXTUREALPHA/DECAL x Af */
    { 1, {  9,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDTEXTUREALPHA/DECAL x AfAt */
    { 1, { 10,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDFACTORALPHA x At */
    { 1, { 10,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDFACTORALPHA x Af */
    { 1, { 10,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDFACTORALPHA x AfAt */
    { 1, { 11,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDTEXTUREALPHAPM or modulate-inverse-alpha-add-color xAt */
    { 1, { 11,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDTEXTUREALPHAPM or modulate-inverse-alpha-add-color xAf */
    { 1, { 11,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDTEXTUREALPHAPM or modulate-inverse-alpha-add-color xAfAt */
    { 1, { 12,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDCURRENTALPHA x At */
    { 1, { 12,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDCURRENTALPHA x Af */
    { 1, { 12,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* BLENDCURRENTALPHA x AfAt */
    { 1, { 13,  0,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* modulate-alpha-add-color (Direct3D) x At */
    { 1, { 13,  0,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* modulate-alpha-add-color (Direct3D) x Af */
    { 1, { 13,  0,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* modulate-alpha-add-color (Direct3D) x AfAt */
    { 1, {  0,  1,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* SUBTRACT x At */
    { 1, {  0,  1,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* SUBTRACT x Af */
    { 1, {  0,  1,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* SUBTRACT x AfAt */
    { 1, {  5,  1,  4,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADDSMOOTH xAt */
    { 1, {  5,  1,  4,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADDSMOOTH xAf */
    { 1, {  5,  1,  4,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* ADDSMOOTH xAfAt */
    /* The HAL's two Direct3D operations that add the texel alpha: add
       + fmsb with COLOR_FACTOR texel alpha gives C = At + Cf*Ct
       (modulate-color-add-alpha), and modulate x4 + fmsb with the same
       factor gives C = At + Cf*(1-Ct) (modulate-inverse-color-add-alpha),
       as r3d_tex_combine models codes 6 and 5 with the bit set. */
    { 1, {  6,  1,  6,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* modulate-color-add-alpha (Direct3D) x At */
    { 1, {  6,  1,  6,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* modulate-color-add-alpha (Direct3D) x Af */
    { 1, {  6,  1,  6,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* modulate-color-add-alpha (Direct3D) x AfAt */
    { 1, {  5,  1,  6,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* modulate-inverse-color-add-alpha (Direct3D) x At */
    { 1, {  5,  1,  6,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* modulate-inverse-color-add-alpha (Direct3D) x Af */
    { 1, {  5,  1,  6,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* modulate-inverse-color-add-alpha (Direct3D) x AfAt */
    { 1, {  4,  1,  0,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* GL_BLEND (Pro) x At */
    { 1, {  4,  1,  0,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* GL_BLEND (Pro) x Af */
    { 1, {  4,  1,  0,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* GL_BLEND (Pro) x AfAt */
    { 1, {  3,  0,  5,  4,  0,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* GL_BLEND fake x At */
    { 1, {  3,  0,  5,  4,  2,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* GL_BLEND fake x Af */
    { 1, {  3,  0,  5,  4,  3,  6,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* GL_BLEND fake x AfAt */
    /* Mesa's plain-chip GL_BLEND on a GL_INTENSITY texture with a zero
       constant color and alpha: both the color and the alpha modulate
       by one minus the texel (Mesa r128 r128_texstate.c
       r128UpdateTextureEnv, the R128_IS_PLAIN path). */
    { 1, {  3,  0,  5,  4,  3,  7,  2,  0,  0,  0,  0,  0,  0,  0 } }, /* GL_BLEND fake, intensity, A = Af(1-It) */
    /* Two stages. Stage 0 is REPLACE or MODULATE and stage 1 is MOD,
       MOD2X, ADD or DECAL (blend texture), each in the three alpha
       columns, and every pairing of the two is present. Stage 1 takes the
       previous stage's color (8) and alpha (4) as its inputs, as Mesa
       sets them for its second unit, except in the At column, where
       gpu_tuple_canon sets the unread input alpha field to 2. On stage 1
       the At column is alpha code 0, which is modeled as passing stage
       0's alpha through rather than as the texel alpha; the column keeps
       its name because it names the code. */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  3,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/At x MOD/At */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  3,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/At x MOD/Af */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  3,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/At x MOD/AfAt */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  4,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/At x MOD2X/At */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  4,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/At x MOD2X/Af */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  4,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/At x MOD2X/AfAt */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  6,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/At x ADD/At */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  6,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/At x ADD/Af */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  6,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/At x ADD/AfAt */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  9,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/At x DECAL/At */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  9,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/At x DECAL/Af */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  9,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/At x DECAL/AfAt */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  3,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/Af x MOD/At */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  3,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/Af x MOD/Af */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  3,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/Af x MOD/AfAt */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  4,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/Af x MOD2X/At */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  4,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/Af x MOD2X/Af */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  4,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/Af x MOD2X/AfAt */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  6,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/Af x ADD/At */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  6,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/Af x ADD/Af */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  6,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/Af x ADD/AfAt */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  9,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/Af x DECAL/At */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  9,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/Af x DECAL/Af */
    { 2, {  0,  0,  4,  4,  2,  6,  2,  9,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/Af x DECAL/AfAt */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  3,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/AfAt x MOD/At */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  3,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/AfAt x MOD/Af */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  3,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/AfAt x MOD/AfAt */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  4,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/AfAt x MOD2X/At */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  4,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/AfAt x MOD2X/Af */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  4,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/AfAt x MOD2X/AfAt */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  6,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/AfAt x ADD/At */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  6,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/AfAt x ADD/Af */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  6,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/AfAt x ADD/AfAt */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  9,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/AfAt x DECAL/At */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  9,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/AfAt x DECAL/Af */
    { 2, {  0,  0,  4,  4,  3,  6,  2,  9,  0,  4,  8,  3,  6,  4 } }, /* REPLACE/AfAt x DECAL/AfAt */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  3,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/At x MOD/At */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  3,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/At x MOD/Af */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  3,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/At x MOD/AfAt */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  4,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/At x MOD2X/At */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  4,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/At x MOD2X/Af */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  4,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/At x MOD2X/AfAt */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  6,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/At x ADD/At */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  6,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/At x ADD/Af */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  6,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/At x ADD/AfAt */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  9,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/At x DECAL/At */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  9,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/At x DECAL/Af */
    { 2, {  3,  0,  4,  4,  0,  6,  2,  9,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/At x DECAL/AfAt */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  3,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/Af x MOD/At */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  3,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/Af x MOD/Af */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  3,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/Af x MOD/AfAt */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  4,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/Af x MOD2X/At */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  4,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/Af x MOD2X/Af */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  4,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/Af x MOD2X/AfAt */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  6,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/Af x ADD/At */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  6,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/Af x ADD/Af */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  6,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/Af x ADD/AfAt */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  9,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/Af x DECAL/At */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  9,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/Af x DECAL/Af */
    { 2, {  3,  0,  4,  4,  2,  6,  2,  9,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/Af x DECAL/AfAt */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  3,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/AfAt x MOD/At */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  3,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/AfAt x MOD/Af */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  3,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/AfAt x MOD/AfAt */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  4,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/AfAt x MOD2X/At */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  4,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/AfAt x MOD2X/Af */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  4,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/AfAt x MOD2X/AfAt */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  6,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/AfAt x ADD/At */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  6,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/AfAt x ADD/Af */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  6,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/AfAt x ADD/AfAt */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  9,  0,  4,  8,  0,  6,  2 } }, /* MODULATE/AfAt x DECAL/At */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  9,  0,  4,  8,  2,  6,  4 } }, /* MODULATE/AfAt x DECAL/Af */
    { 2, {  3,  0,  4,  4,  3,  6,  2,  9,  0,  4,  8,  3,  6,  4 } }, /* MODULATE/AfAt x DECAL/AfAt */
    /* Mesa's GL_REPLACE on the second unit behind a REPLACE first unit,
       by texture format: RGBA passes the texel color and alpha (both
       codes disable), RGB passes the texel color and the previous alpha
       (alpha code 2), ALPHA passes the previous color (color code 2) and
       the texel alpha (Mesa r128 r128_texstate.c r128UpdateTextureEnv). */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  0,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/At x REPLACE(RGBA)/At */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  0,  0,  4,  8,  2,  6,  4 } }, /* REPLACE/At x REPLACE(RGB)/Af */
    { 2, {  0,  0,  4,  4,  0,  6,  2,  2,  0,  4,  8,  0,  6,  2 } }, /* REPLACE/At x REPLACE(ALPHA)/At */
    /* Emboss rows: stage 1 passes the previous color through (code 2,
       input 8) and adds, signed, one minus the texel alpha to the
       previous alpha (alpha code 7, factor 7, input 4). Stage 0 is
       MODULATE2X in each alpha column. */
    { 2, {  4,  0,  4,  4,  0,  6,  2,  2,  0,  4,  8,  7,  7,  4 } }, /* MODULATE2X/At x emboss */
    { 2, {  4,  0,  4,  4,  2,  6,  2,  2,  0,  4,  8,  7,  7,  4 } }, /* MODULATE2X/Af x emboss */
    { 2, {  4,  0,  4,  4,  3,  6,  2,  2,  0,  4,  8,  7,  7,  4 } }, /* MODULATE2X/AfAt x emboss */
    /* clang-format on */
};

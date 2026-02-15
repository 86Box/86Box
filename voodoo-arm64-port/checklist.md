# Voodoo ARM64 JIT Port — Master Checklist

## Pre-Implementation Research (do NOT skip these)

- [x] **Instruction mapping**: Go through x86-64 codegen line by line, build translation table of every x86-64 instruction → ARM64 equivalent(s). Note where 1 x86 instruction becomes 2-3 ARM64 instructions. Save to `instruction-mapping.md` — DONE. Covers GPR + NEON. Audit verified 100% coverage of all 80+ distinct instructions. Found upstream bug at line 1303 (0x8E should be 0x83).
- [x] **NEON mapping**: Map every SSE2 JIT emission sequence to equivalent NEON instruction sequences. Focus on bilinear filtering and color blending — these are the hardest. — DONE. Covered in `instruction-mapping.md` sections 9-11 (individual SSE2→NEON mappings, bilinear filter full sequence, alpha blend multiply+round pattern). Separate file not needed.
- [x] **Struct offset map**: Document every `offsetof()` the x86-64 codegen uses into `voodoo_state_t` and `voodoo_params_t`. Verify they match on ARM64 (watch for alignment). Save to `struct-offsets.md` — DONE. 130+ offsetof usages mapped to 30 unique state fields + 17 unique params fields. All offsets fit in ARM64 LDR/STR scaled immediate. All 64-bit fields 8-byte aligned. Sub-byte access patterns (+3, +4) documented with ARM64 alternatives. The x86 `-0x10` bilinear hack identified and will not be ported. Offset constants and TMU-indexed helper macros drafted for the codegen header.
- [x] **Encoding macros draft**: Draft the `#define` helper macros for ARM64 instruction encoding based on the instruction mapping. These become the top of `vid_voodoo_codegen_arm64.h` — DONE. 212 ARM64_* instruction macros + 24 field helpers + 16 condition codes + 46 struct offset constants in `encoding-macros.md`. All verified against assembler output and CPU backend OPCODE_* constants. ROR_IMM bug found and fixed. All ARMv8.0-A baseline.

## Agent Setup

- [x] Define custom agent configs in `.claude/agents/` — DONE: voodoo-lead (red), voodoo-texture (cyan), voodoo-color (green), voodoo-effects (magenta)
- [x] Verify agents can read reference docs and the x86-64 codegen — verified in Phase 1 implementation
- [x] Assign phases to agents per plan — lead=Phase 1+coord, texture=Phase 3, color=Phase 4, effects=Phase 5+6

## Phase 1: Scaffolding + Prologue/Epilogue

- [x] Create `src/include/86box/vid_voodoo_codegen_arm64.h` with header guard, includes, constants
- [x] Define `voodoo_arm64_data_t` struct (mirrors `voodoo_x86_data_t`)
- [x] Add all encoding helper macros — 292 macros (GPR + NEON), all verified against CPU backend + ARM ref
- [x] Implement NEON lookup tables (`alookup[]`, `bilinear_lookup[]`, etc.) — using `voodoo_neon_reg_t` union (u16[8]/u32[4]/u64[2])
- [x] Implement `voodoo_codegen_init()` — `plat_mmap` + table init
- [x] Implement `voodoo_codegen_close()` — `plat_munmap`
- [x] Implement `voodoo_get_block()` — cache lookup + W^X toggle (`pthread_jit_write_protect_np`) + I-cache invalidate (`__clear_cache`)
- [x] Implement `voodoo_generate()` — prologue only (STP x29/x30, x19-x28, d8-d13; load pinned GPR ptrs; load NEON constants v8-v11; load fb_mem/aux_mem)
- [x] Implement `voodoo_generate()` — epilogue only (LDP restore all callee-saved, RET)
- [x] Guard change: `src/include/86box/vid_voodoo_render.h` line 4 — add `|| defined __aarch64__ || defined _M_ARM64` to `NO_CODEGEN` gate
- [x] Guard change: `src/video/vid_voodoo_render.c` line 658 — add `#elif (defined __aarch64__ || defined _M_ARM64)` with `#include <86box/vid_voodoo_codegen_arm64.h>`
- [x] **BUILD TEST**: Compiles on ARM64 with no errors — verified, all 50 struct offset static assertions pass
- [x] **RUN TEST**: Emulator runs, Voodoo card initializes, falls through to interpreter — verified 2026-02-15

## Phase 2: Pixel Loop + Depth

- [ ] Pixel loop structure (x iteration with xdir)
- [ ] W-depth calculation
- [ ] Stipple test (pattern and rotating)
- [ ] Depth test — DEPTHOP_NEVER
- [ ] Depth test — DEPTHOP_LESSTHAN
- [ ] Depth test — DEPTHOP_EQUAL
- [ ] Depth test — DEPTHOP_LESSTHANEQUAL
- [ ] Depth test — DEPTHOP_GREATERTHAN
- [ ] Depth test — DEPTHOP_NOTEQUAL
- [ ] Depth test — DEPTHOP_GREATERTHANEQUAL
- [ ] Depth test — DEPTHOP_ALWAYS
- [ ] **RUN TEST**: Depth-only rendering works

## Phase 3: Texture Fetch

- [ ] `codegen_texture_fetch()` — perspective-correct W division (SDIV)
- [ ] LOD calculation (BSR equivalent via CLZ on ARM64)
- [ ] Point-sampled texture lookup
- [ ] Bilinear filtered texture lookup (NEON 4-tap blend)
- [ ] Texture mirror S / mirror T handling
- [ ] Texture clamp S / clamp T handling
- [ ] TMU0-only path
- [ ] TMU1-only (passthrough) path
- [ ] Dual-TMU fetch and combine (tc_mselect, tc_add, tc_invert)
- [ ] trexInit1 override path
- [ ] **RUN TEST**: Textured triangles render correctly

## Phase 4: Color/Alpha Combine

- [ ] Color select — CC_LOCALSELECT_ITER_RGB
- [ ] Color select — CC_LOCALSELECT_TEX
- [ ] Color select — CC_LOCALSELECT_COLOR1
- [ ] Color select — CC_LOCALSELECT_LFB
- [ ] Local color select + override (tex_a bit 7)
- [ ] Chroma key test
- [ ] Alpha select — A_SEL_ITER_A, A_SEL_TEX, A_SEL_COLOR1
- [ ] CCA local select — ITER_A, COLOR0, ITER_Z
- [ ] Alpha mask test
- [ ] Color combine: cc_zero_other
- [ ] Color combine: cc_sub_clocal
- [ ] Color combine: cc_mselect (ZERO, CLOCAL, AOTHER, ALOCAL, TEX, TEXRGB)
- [ ] Color combine: cc_reverse_blend
- [ ] Color combine: cc_add (CLOCAL, ALOCAL)
- [ ] Color combine: cc_invert_output
- [ ] Alpha combine: cca_zero_other, cca_sub_clocal, cca_mselect, cca_reverse_blend, cca_add, cca_invert_output
- [ ] Clamp all outputs
- [ ] **RUN TEST**: Colored, shaded geometry renders correctly

## Phase 5: Fog + Alpha Test + Alpha Blend

- [ ] Fog — FOG_CONSTANT
- [ ] Fog — FOG_ADD / FOG_MULT modes
- [ ] Fog — FOG source: w_depth table, Z, alpha, W
- [ ] Alpha test — all 8 AFUNC modes
- [ ] Alpha blend — dest_afunc (AZERO, ASRC_ALPHA, A_COLOR, ADST_ALPHA, AONE, AOMSRC_ALPHA, AOM_COLOR, AOMDST_ALPHA, ACOLORBEFOREFOG)
- [ ] Alpha blend — src_afunc (AZERO, ASRC_ALPHA, A_COLOR, ADST_ALPHA, AONE, AOMSRC_ALPHA, AOM_COLOR, AOMDST_ALPHA, ASATURATE)
- [ ] Dithersub (4x4 and 2x2)
- [ ] **RUN TEST**: Transparent/fogged geometry works

## Phase 6: Framebuffer Write + Dithering

- [ ] Dither 4x4 pattern
- [ ] Dither 2x2 pattern
- [ ] No-dither path (shift only)
- [ ] RGB565 pack
- [ ] Framebuffer write — linear
- [ ] Framebuffer write — tiled
- [ ] Depth write to aux buffer (linear + tiled)
- [ ] Alpha write to aux buffer (linear + tiled)
- [ ] Per-pixel state increments (dRdX, dGdX, dBdX, dAdX, dZdX, dSdX, dTdX, dWdX for both TMUs + W)
- [ ] Pixel/texel counter updates
- [ ] **RUN TEST**: Full rendering pipeline matches interpreter output

## Phase 7: Optimization + Validation

- [ ] Profile hot paths on Apple Silicon
- [ ] Optimize NEON usage (wider ops, fewer loads)
- [ ] ARM64-specific opts (CSEL, UBFX/BFI, conditional select vs branch)
- [ ] Test: Voodoo 1 card works
- [ ] Test: Voodoo 2 (SB50) card works
- [ ] Test: Voodoo Banshee card works
- [ ] Test: Voodoo 3 card works
- [ ] Test: SLI mode works (odd/even rendering)
- [ ] Test: Tiled framebuffer mode works
- [ ] Compare performance: JIT vs interpreter on same workload

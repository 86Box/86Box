# Voodoo ARM64 JIT Port -- Changelog

All changes, decisions, and progress for the ARM64 port of the Voodoo GPU pixel pipeline JIT.

---

## Phase 1: Scaffolding + Prologue/Epilogue

### 2026-02-15 -- Phase 1 started

- Created `voodoo-arm64-port/CHANGELOG.md` to track all changes
- Branch: `phase-1-scaffolding`

#### Files created:
- `src/include/86box/vid_voodoo_codegen_arm64.h` -- ARM64 JIT codegen header

#### Files modified:
- `src/include/86box/vid_voodoo_render.h` -- Added ARM64 to NO_CODEGEN gate
- `src/video/vid_voodoo_render.c` -- Added ARM64 include path

#### Key decisions:
- Register allocation: x0=state, x1=params, x2=x, x3=real_y, x19-x28=callee-saved pinned values
- NEON constants in v8-v11 (callee-saved): xmm_01_w, xmm_ff_w, xmm_ff_b, minus_254
- W^X toggle via `pthread_jit_write_protect_np()` in `voodoo_get_block()`
- I-cache flush via `__clear_cache()` (cross-platform, matches codegen_allocator.c)
- NEON lookup tables use `uint16_t[8]` arrays (no SIMD intrinsics in header)
- Upstream bug at x86-64 line 1303 (0x8E instead of 0x83) will NOT be ported

#### Build fixes:
- Added `#include <stdint.h>` to resolve uint8_t/uint16_t/uint32_t types
- Build verified on macOS ARM64: all 50 static offset assertions pass, links cleanly

#### Testing:
- **Runtime test passed** (2026-02-15): Emulator launches, Voodoo card initializes, rendering falls through to interpreter as expected (pixel pipeline not yet implemented)

---

## Phase 2: Pixel Loop + Depth

### 2026-02-15 -- Phase 2 complete

- Branch: `phase-2-pixel-loop-depth`

#### Implementation:
- **Pixel loop structure**: X-coordinate iteration with xdir (forward/backward) support
- **Stipple test**: Pattern stipple (bit lookup from real_y/x) and rotating stipple (ROR + TBZ)
- **Tiled framebuffer**: X-coordinate calculation for tiled modes
- **W-depth computation**: CLZ-based (ARM64 BSR equivalent) with clamping to 0..0xFFFF
- **Z-buffer depth**: SAR 12 with signed clamping, depth bias (zaColor addition)
- **All 8 DEPTHOP modes**: NEVER (immediate RET), LESSTHAN, EQUAL, LESSTHANEQUAL, GREATERTHAN, NOTEQUAL, GREATERTHANEQUAL, ALWAYS
- **Per-pixel state increments**: RGBA via NEON 4xS32, Z via GPR, TMU S/T via NEON 2xD64, TMU W and global W via GPR 64-bit
- **Forward branch patching**: Macros for BCOND, B, TBxZ, CBxZ
- **Bitmask immediates**: AND/ANDS/ORR/TST with ARM64 bitmask encoding

#### Key decisions:
- Depth test uses unsigned comparison (values 0..0xFFFF) with ARM64 condition codes: CS (>=), HI (>), LS (<=), CC (<)
- Texture fetch calls are placeholders for Phase 3 integration

#### Testing:
- **Runtime test passed** (2026-02-15): Quake 3 renders black screen, 3DMark 99 renders gray screen (expected - color/texture pipeline not yet implemented). No crashes, depth pipeline executes correctly.

---

## Phase 3: Texture Fetch

### 2026-02-15 -- Phase 3 complete

- Branch: `phase-3-texture-fetch`
- PR: #3

#### Implementation:
- **`codegen_texture_fetch()`**: Complete texture sampling pipeline
- **Perspective-correct W division**: SDIV instruction for 1/W calculation
- **LOD calculation**: CLZ-based (ARM64 BSR equivalent) with clamping
- **Point-sampled lookup**: Direct texel fetch from texture RAM
- **Bilinear filtering**: NEON 4-tap weighted blend (horizontal + vertical)
- **Texture coordinate wrapping**: Mirror S/T and Clamp S/T modes
- **TMU configuration**: TMU0-only, TMU1-only (passthrough), and dual-TMU combine
- **Dual-TMU combine modes**: tc_mselect (ZERO, C_LOCAL, A_OTHER, A_LOCAL, TEX, TEXRGB), tc_add, tc_invert
- **trexInit1 override**: Texture parameter override path

#### Bug fixes:
- **Upstream bug NOT ported**: x86-64 line 1303 emits invalid opcode (0x8E MOV ES,AX instead of 0x83 ADD EAX,1). ARM64 implementation uses correct ADD encoding.
- **Second upstream quirk**: x86-64 line 3053 uses ROR instead of SHR (functionally equivalent in context, both implementations valid)

#### Key decisions:
- Texture lookups use NEON vector operations for RGBA unpacking and blending
- LOD calculation matches x86-64 bit-manipulation semantics exactly
- Dual-TMU combine uses the same logical pipeline as single-TMU for consistency

---

## Phase 4: Color/Alpha Combine

### 2026-02-15 -- Phase 4 complete

- Branch: `phase-4-color-alpha`

#### Implementation:
- **Color select (CC_LOCALSELECT)**: ITER_RGB (Gouraud shading), TEX (texture color), COLOR1 (constant), LFB (framebuffer read)
- **Local color override**: tex_a bit 7 control path
- **Chroma key test**: RGB comparison against chromaKey register with reject path
- **Alpha select (A_SEL)**: ITER_A (Gouraud alpha), TEX (texture alpha), COLOR1 (constant)
- **CCA local select**: ITER_A, COLOR0, ITER_Z modes
- **Alpha mask test**: Alpha value comparison against alphaMask register
- **Color combine pipeline**:
  - cc_zero_other: Zero "other" color input
  - cc_sub_clocal: Subtract "local" from "other"
  - cc_mselect: Multiply select (ZERO, CLOCAL, AOTHER, ALOCAL, TEX, TEXRGB)
  - cc_reverse_blend: Reverse blend direction
  - cc_add: Add mode (CLOCAL or ALOCAL)
  - cc_invert_output: Invert final RGB output
- **Alpha combine pipeline**: cca_zero_other, cca_sub_clocal, cca_mselect, cca_reverse_blend, cca_add, cca_invert_output (parallel to color combine)
- **Output clamping**: All RGBA channels clamped to [0, 255]

#### Key decisions:
- Color and alpha pipelines implemented as parallel paths following x86-64 structure
- NEON vector operations used for parallel RGB channel processing
- All combine modes match x86-64 bit-exact behavior
- Clamping uses NEON SMIN/SMAX for efficient saturation

#### Testing:
- **Build verified** (2026-02-15): Compiles cleanly, all static assertions pass
- **JIT validation logging added**: Logs codegen events for debugging
- **Validation complete** (2026-02-15): All 4 phases validated against 3dfx specs by voodoo-arch agents

#### Post-Phase 4 Fixes (2026-02-15)

**Frame size optimization**: Reduced stack frame from 144 to 128 bytes (-16 bytes per call)
- Validation found prologue comment claimed d14/d15 saved at SP-144, but actual code only saves d8-d13
- Since v14/v15 are never used, removed the misleading comment and reduced frame size
- Stack remains 16-byte aligned (128 = 8 × 16)

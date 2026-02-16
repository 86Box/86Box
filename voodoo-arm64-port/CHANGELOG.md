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

# Plan: Voodoo Pixel Pipeline JIT — ARM64 Port

## What This Is

The Voodoo GPU has a configurable pixel pipeline controlled by hardware registers (`fbzColorPath`, `fbzMode`, `alphaMode`, `textureMode`, `fogMode`). On x86-64, a JIT compiler (`vid_voodoo_codegen_x86-64.h`, 3561 lines) generates native machine code for the current pipeline configuration, avoiding per-pixel branching. On ARM64, this JIT doesn't exist — the interpreter fallback in `vid_voodoo_render.c` runs instead (lines 941-1386), which is significantly slower.

The JIT function signature: `uint8_t (*voodoo_draw)(voodoo_state_t *state, voodoo_params_t *params, int x, int real_y)`

All Voodoo card families (Voodoo 1, 2/SB50, Banshee, 3) use the same codegen — the pipeline is parameterized by register state, not card type.

## Architecture Decision: Raw Instruction Emission

**Approach: Emit raw ARM64 instruction words (uint32_t) directly**, matching the x86-64 codegen's `addbyte()` pattern. Reasons:

1. The CPU dynarec's ARM64 backend (`codegen_backend_arm64_ops.c`) uses `codeblock_t` which is tightly coupled to the CPU recompiler's block management, register allocation, and IR pipeline. Reusing it would require either: (a) making the Voodoo codegen depend on the CPU dynarec infrastructure, or (b) duplicating large parts of it. Neither is clean.

2. The x86-64 Voodoo codegen uses a dead-simple model: `addbyte(val)` writes bytes to `code_block[block_pos++]`. ARM64 is simpler here — fixed 4-byte instructions — so we use `addlong(val)` to write one instruction word at a time.

3. Self-contained in a single header file, just like the x86-64 version. No new build dependencies.

We **will** define helper macros for common ARM64 instruction encodings (ADD, SUB, LDR, STR, branch, NEON ops) at the top of the file — these are just `#define` macros that encode to `uint32_t`, not function calls. This avoids raw numeric constants everywhere while keeping zero overhead.

## macOS Apple Silicon JIT Constraints

The existing infrastructure handles this:

1. **Entitlements**: `src/mac/entitlements.plist` already has `com.apple.security.cs.allow-jit = true`
2. **MAP_JIT**: `plat_mmap()` in `qt_platform.cpp` and `unix.c` already passes `MAP_JIT` on Darwin when `executable=1`
3. **W^X toggling**: `pthread_jit_write_protect_np(0/1)` must bracket code generation. The CPU dynarec already does this per-block in `386_dynarec.c:548-643`.

**For the Voodoo codegen**, we need to add W^X toggling in `voodoo_get_block()` around the call to `voodoo_generate()`. We also need to call `sys_icache_invalidate()` after writing to the code block, since ARM64 has a non-coherent I-cache. This is a ~4 line addition, guarded with `#if defined(__APPLE__) && defined(__aarch64__)`.

## NEON Strategy

The x86-64 codegen uses SSE2 for:
- Bilinear texture filtering (4-tap weighted blend)
- Color combine multiply+add
- Alpha blending

ARM64 equivalent: **NEON**, which is always available on AArch64 (no optional feature detection needed).

Static lookup table type mapping:
- `__m128i` → `int16x8_t` / `uint16x8_t` / `uint8x16_t` as appropriate
- `_mm_set_epi32()` → `vld1q_u32()` or brace-initialized vectors
- `_mm_mullo_epi16()` → `vmulq_u16()`
- `_mm_add_epi16()` → `vaddq_u16()`
- `_mm_srli_epi16()` → `vshrq_n_u16()`
- `_mm_packus_epi16()` → `vqmovun_s16()`

In the JIT-generated code, NEON instructions will be emitted as encoded `uint32_t` values (not compiler intrinsics, since we're generating machine code at runtime).

## Register Allocation Plan

ARM64 has 31 GPRs (x0-x30) + 32 SIMD/FP regs (v0-v31). Much more generous than x86-64.

**GPR assignment in generated code:**
| Register | Purpose |
|----------|---------|
| x0 | `voodoo_state_t *state` (arg0, kept here) |
| x1 | `voodoo_params_t *params` (arg1, kept here) |
| x2 | `x` pixel position (arg2) |
| x3 | `real_y` (arg3) |
| x4-x7 | Scratch / intermediate calculations |
| x8 | `fb_mem` pointer |
| x9 | `aux_mem` pointer |
| x10-x15 | Scratch registers (caller-saved, free to use) |
| x16-x17 | Intra-procedure scratch (IP0/IP1) |
| x19-x28 | Callee-saved (for values that persist across the pixel loop) |
| x29 | Frame pointer (saved/restored in prologue/epilogue) |
| x30 | Link register (saved/restored) |

**NEON assignment:**
| Register | Purpose |
|----------|---------|
| v0-v7 | Scratch (caller-saved) |
| v8-v15 | Callee-saved — used for pinned constants (bilinear weights, 0x01/0xFF masks, etc.) |
| v16-v31 | Scratch (caller-saved) |

This means we can pin SIMD constants (like the x86-64 XMM8-XMM11 for `xmm_01_w`, `xmm_ff_w`, `xmm_ff_b`, `minus_254`) in v8-v11, loaded once in the prologue.

## Files to Create/Modify

### New file: `src/include/86box/vid_voodoo_codegen_arm64.h`

~3500-4000 lines. Structure mirroring `vid_voodoo_codegen_x86-64.h`:

```
1. Header guard, includes (<arm_neon.h>)
2. Constants: BLOCK_NUM, BLOCK_SIZE, BLOCK_MASK
3. Data structure: voodoo_arm64_data_t (replaces voodoo_x86_data_t)
4. Instruction encoding macros (~150 lines)
5. Lookup tables: alookup[], bilinear_lookup[] (NEON vectors instead of __m128i)
6. codegen_texture_fetch() — texture coordinate + LOD + sampling
7. voodoo_generate() — main JIT emission function, pipeline stages:
   a. Prologue (save callee-saved regs, load constants)
   b. Pixel loop entry + x iteration
   c. Stipple test
   d. W-depth calculation
   e. Depth test
   f. Framebuffer read (dest color)
   g. Texture fetch dispatch
   h. Color select (cother/clocal)
   i. Chroma key test
   j. Alpha select (aother/alocal)
   k. Alpha mask test
   l. Color combine (zero/sub/multiply/add/clamp/invert)
   m. Fog application
   n. Alpha test
   o. Alpha blending (src/dest factor selection + blend)
   p. Dithering
   q. Framebuffer write (RGB + depth/alpha)
   r. Per-pixel state increment (dRdX, dGdX, etc.)
   s. Epilogue (restore regs, RET)
8. voodoo_get_block() — block cache lookup/generation
9. voodoo_codegen_init() — allocate executable memory + init lookup tables
10. voodoo_codegen_close() — free memory
```

### Modified file: `src/include/86box/vid_voodoo_render.h` (guarded)

```c
// Change line 4-6 from:
#if !(defined __amd64__ || defined _M_X64)
#    define NO_CODEGEN
#endif

// To:
#if !(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64)
#    define NO_CODEGEN
#endif
```

### Modified file: `src/video/vid_voodoo_render.c` (guarded)

```c
// Change line 658-662 from:
#if (defined __amd64__ || defined _M_X64)
#    include <86box/vid_voodoo_codegen_x86-64.h>
#else
int voodoo_recomp = 0;
#endif

// To:
#if (defined __amd64__ || defined _M_X64)
#    include <86box/vid_voodoo_codegen_x86-64.h>
#elif (defined __aarch64__ || defined _M_ARM64)
#    include <86box/vid_voodoo_codegen_arm64.h>
#else
int voodoo_recomp = 0;
#endif
```

### Modified file: `src/video/CMakeLists.txt`

The existing `-msse2` flag is already conditionally applied to x86 only (line 177-179). NEON is mandatory on AArch64, so no additional flags needed.

## Implementation Phases

### Phase 1: Scaffolding + Prologue/Epilogue
- Create `vid_voodoo_codegen_arm64.h` with data structures, encoding macros, `voodoo_codegen_init/close`, `voodoo_get_block`
- Implement function prologue (STP x29/x30, save callee-saved GPRs and NEON regs, load constants) and epilogue (LDP restore, RET)
- Add `pthread_jit_write_protect_np` + `sys_icache_invalidate` calls in `voodoo_get_block()`
- Modify the two guard files (`vid_voodoo_render.h`, `vid_voodoo_render.c`)
- **Test**: Builds, runs, falls through to interpreter (empty generated function returns immediately)

### Phase 2: Pixel Loop + Depth
- Emit the pixel loop structure (x iteration, xdir handling)
- W-depth calculation
- Depth test (all 8 DEPTHOP modes)
- Stipple test
- **Test**: Depth-only rendering works

### Phase 3: Texture Fetch
- `codegen_texture_fetch()` — perspective-correct coordinate computation (SDIV for W division)
- Point-sampled texture lookup
- Bilinear filtered texture lookup (NEON 4-tap blend)
- LOD calculation
- TMU0-only, TMU1-only, and dual-TMU paths
- Texture combine (for dual-TMU: tc_mselect, tc_add, tc_invert)
- **Test**: Textured triangles render correctly

### Phase 4: Color/Alpha Combine
- Color select (rgb_sel: iterated, texture, color1, LFB)
- Local color select (cc_localselect, override)
- Chroma key test
- Alpha select (a_sel, cca_localselect)
- Alpha mask test
- Color combine pipeline: zero_other → sub_clocal → mselect multiply → add → clamp → invert
- Alpha combine pipeline (parallel, same structure)
- **Test**: Colored, shaded geometry renders correctly

### Phase 5: Fog + Alpha Test + Alpha Blend
- Fog application (FOG_CONSTANT, FOG_ADD, FOG_MULT, FOG_Z/ALPHA/W table lookup)
- Alpha test (all 8 AFUNC modes)
- Alpha blending (src_afunc x dest_afunc, 10+ blend modes each)
- **Test**: Transparent/fogged geometry works

### Phase 6: Framebuffer Write + Dithering
- Dither (4x4 and 2x2 patterns, dithersub)
- RGB565 pack and framebuffer write (tiled and linear)
- Depth/alpha write to aux buffer
- Per-pixel counter updates
- Per-pixel state increments (dRdX, dGdX, etc.)
- **Test**: Full rendering pipeline matches interpreter output

### Phase 7: Optimization
- Profile hot paths
- Optimize NEON usage (wider operations where possible)
- Consider ARM64-specific optimizations (CSEL for branchless conditionals, UBFX/BFI for bit field operations)
- Verify all Voodoo card variants work (Voodoo 1, 2, Banshee, 3)

## Custom Agents

Yes, custom agents would help parallelize the translation once the scaffolding (Phase 1) is complete. Recommended setup:

1. **Lead agent**: Manages Phase 1 (scaffolding), coordinates, integrates
2. **Texture agent**: Phase 3 (texture fetch — the most complex single stage)
3. **Color pipeline agent**: Phase 4 (color/alpha combine)
4. **Effects agent**: Phase 5+6 (fog, alpha test, blending, dither, FB write)

Phases 2-6 can partially overlap once the encoding macros and data structures from Phase 1 are stable. But Phase 1 must complete first as it defines the ABI, register allocation, and encoding helpers that everything else depends on.

## Key Reference Files

| File | What to look at |
|------|----------------|
| `src/include/86box/vid_voodoo_codegen_x86-64.h` | The x86-64 JIT to port (3561 lines) |
| `src/include/86box/vid_voodoo_codegen_x86.h` | 32-bit x86 JIT for comparison (3206 lines) |
| `src/video/vid_voodoo_render.c:941-1386` | Interpreter fallback (correctness reference) |
| `src/video/vid_voodoo_render.c:41-93` | `voodoo_state_t` definition (JIT accesses via offsetof) |
| `src/include/86box/vid_voodoo_common.h:112-230` | `voodoo_params_t` definition |
| `src/include/86box/vid_voodoo_common.h:268-725` | `voodoo_t` definition |
| `src/include/86box/vid_voodoo_render.h` | Interpreter macros (DEPTH_TEST, ALPHA_TEST, ALPHA_BLEND, APPLY_FOG) |
| `src/codegen_new/codegen_backend_arm64_ops.c` | Existing ARM64 instruction encoding patterns |
| `src/codegen_new/codegen_backend_arm64_defs.h` | ARM64 register definitions |
| `src/cpu/386_dynarec.c:548-643` | W^X toggle pattern for Apple Silicon |
| `src/mac/entitlements.plist` | JIT entitlement (already present) |
| `src/qt/qt_platform.cpp:427-439` | `plat_mmap` with MAP_JIT (already present) |

# Comprehensive ARM64 Voodoo JIT Codegen Audit

Date: 2026-02-16
Auditor: voodoo-debug agent
File: `src/include/86box/vid_voodoo_codegen_arm64.h` (4118 lines)
Reference: `src/include/86box/vid_voodoo_codegen_x86-64.h` (3561 lines)

## Executive Summary

Full instruction-level audit of the ARM64 JIT codegen covering all 6 phases.
Found **3 confirmed bugs** (1 previously known, 2 new) and **2 potential issues**.

---

## CRITICAL BUGS

### Bug A: Color Combine uses SSHR (arithmetic) instead of USHR (logical) [KNOWN - Bug #6]

- **Location**: Line ~3025 in `voodoo_generate()`
- **Confidence**: HIGH
- **Code**:
  ```c
  addlong(ARM64_SMULL_4S_4H(17, 16, 3));
  addlong(ARM64_SSHR_V4S(17, 17, 8));       // <-- BUG: arithmetic shift
  addlong(ARM64_SQXTN_4H_4S(0, 17));
  ```
- **x86-64 reference** (lines 2197-2200): Uses `PSRLD XMM0, 8` which is **logical** shift right.
- **Problem**: When `cother < clocal`, the subtraction produces negative 16-bit values.
  After SMULL widening to 32-bit, the products can be negative. SSHR (arithmetic)
  preserves sign bits, producing large negative 32-bit values. USHR (logical) would
  produce zero-extended results. The subsequent SQXTN saturating narrow then produces
  different results.
- **Impact**: Visual corruption -- bright-to-dark banding when color combine subtracts
  and the result is negative before the blend multiply.
- **Fix**: Change `ARM64_SSHR_V4S` to `ARM64_USHR_V4S` at this ONE location only.
  Do NOT change the texture combine instances (lines ~2272, ~2468) which correctly
  use SSHR to match x86-64 PSRAD.

### Bug B: Alpha-of-alpha blend shift amount off by one [NEW]

- **Location**: Lines ~3527-3536 in `voodoo_generate()`
- **Confidence**: HIGH
- **Code**:
  ```c
  if (dest_aafunc == 4) {
      addlong(ARM64_LSL_IMM(6, 5, 6));   /* w6 = dst_alpha*2 << 6 = dst_alpha << 7 */
      addlong(ARM64_ADD_REG(4, 4, 6));
  }
  if (src_aafunc == 4) {
      addlong(ARM64_LSL_IMM(6, 12, 6));  /* w6 = src_alpha*2 << 6 = src_alpha << 7 */
      addlong(ARM64_ADD_REG(4, 4, 6));
  }
  ```
- **x86-64 reference** (lines 3041-3049):
  ```
  SHL EBX, 0x7    ; EBX = dst_alpha*2, so result = dst_alpha*2 << 7 = dst_alpha << 8
  ADD EAX, EBX
  SHL EDX, 0x7    ; EDX = src_alpha*2, so result = src_alpha*2 << 7 = src_alpha << 8
  ADD EAX, EDX
  SHR EAX, 8      ; divide accumulated alpha by 256
  ```
- **Problem**: The x86-64 shifts the doubled alpha by 7 (`<< 7`), producing
  `dst_alpha << 8`. But the ARM64 shifts by 6 (`<< 6`), producing `dst_alpha << 7`.
  After the final `>> 8` division:
  - x86-64: `(dst_alpha << 8) >> 8 = dst_alpha` (correct)
  - ARM64: `(dst_alpha << 7) >> 8 = dst_alpha / 2` (WRONG -- half the correct value)
- **Impact**: When alpha blending uses `dest_aafunc == 4` or `src_aafunc == 4`,
  the blended alpha channel will be exactly half of what it should be. This affects
  games that use alpha-channel blending (e.g., transparent effects, HUD overlays).
- **Fix**: Change both `LSL_IMM(..., 6)` to `LSL_IMM(..., 7)`:
  - Line ~3528: `ARM64_LSL_IMM(6, 5, 7)` (was 6)
  - Line ~3534: `ARM64_LSL_IMM(6, 12, 7)` (was 6)

### Bug C: Stack frame overflow -- d12/d13 stored beyond frame [RE-INTRODUCED]

- **Location**: Lines ~1700-1704 (prologue) and ~3913-3929 (epilogue)
- **Confidence**: HIGH
- **Code**:
  ```c
  addlong(ARM64_STP_PRE_X(29, 30, 31, -128));  // Allocate 128 bytes
  // ... stores at offsets 0, 16, 32, 48, 64, 80, 96, 112 (all within bounds)
  addlong(ARM64_STP_D(12, 13, 31, 128));        // Offset 128 = OUT OF BOUNDS
  ```
- **Problem**: The frame is 128 bytes (offsets 0-127 valid). The 9th store pair
  (d12, d13) is placed at offset 128, which is the caller's stack frame. This was
  previously Bug #1 (fixed in commit a5fe235 by using 144 bytes) but appears to have
  been re-introduced during the "frame size optimization (144->128 bytes)" noted in
  project memory.
- **Impact**: Writes 16 bytes into the caller's stack on every JIT invocation.
  The symmetric LDP in the epilogue reads it back, so d12/d13 themselves are fine,
  but the caller's local variables at that stack location are corrupted. This could
  cause intermittent wrong behavior in `vid_voodoo_render` between JIT calls.
- **Fix**: Change frame from 128 to 144 bytes:
  - Prologue: `ARM64_STP_PRE_X(29, 30, 31, -144)`
  - Epilogue: `ARM64_LDP_POST_X(29, 30, 31, 144)`
  - Note: 144 is 16-byte aligned, so no adjustment needed for other offsets.
    Simply having extra space at the top of the frame works.

---

## MEDIUM ISSUES

### Issue D: Fog alookup table indexing off-by-one vs x86-64 [POTENTIAL]

- **Location**: Lines ~3155-3158
- **Confidence**: MEDIUM (may be intentional correction of x86-64 bug)
- **Code**:
  ```c
  addlong(ARM64_ADD_REG_X_LSL(5, 20, 4, 3));  /* x5 = x20 + w4*8 */
  addlong(ARM64_LDR_D(5, 5, 0));              /* v5 = alookup[fog_a] */
  ```
- **x86-64 reference** (lines 2387-2393):
  ```
  PMULLW XMM3, [R10 + RAX*8 + 16]   ; alookup[fog_a + 1]
  ```
  R10 = &alookup, RAX = fog_a*2. Address = alookup_base + fog_a*16 + 16 = &alookup[fog_a + 1].
- **Problem**: The x86-64 fog path accesses `alookup[fog_a + 1]` (displacement +16),
  while the ARM64 accesses `alookup[fog_a]` (no offset). The alpha blend paths in
  both x86-64 and ARM64 correctly access `alookup[index]` without the +1 offset,
  suggesting the x86-64 fog path's +16 may be an upstream quirk.
- **Impact**: When fog_a = 0, x86-64 applies a tiny fog (alookup[1] = {1,1,1,1})
  while ARM64 applies zero fog (alookup[0] = {0,0,0,0}). For non-zero fog_a values,
  the difference is negligible (off by one lookup entry in a 256-entry table).
- **Assessment**: The ARM64 behavior (fog_a=0 means no fog) is arguably more correct.
  Do not change unless visual comparison shows fog differences.

### Issue E: Fog blend factor potential off-by-one [KNOWN - Bug #3]

- **Location**: Lines ~3139-3153 (fog table lookup)
- **Confidence**: MEDIUM
- **Details**: Previously identified as Bug #3 in debug-findings.md. The fog
  interpolation between fog table entries may have a subtle rounding difference
  vs x86-64 due to the dfog multiplication path. Low priority since fog accuracy
  within 1/1024 is rarely visible.

---

## VALIDATION RESULTS BY SECTION

### Section 1-7: Macro Definitions and Field Helpers -- PASS

- All ARM64 encoding opcodes verified against ISA manual
- Condition code values (0x0-0xE) are correct
- Shift/rotate field encodings match specification
- NEON shift immediate helpers use correct (element_size - shift) encoding for right shifts

### Section 8-14: GPR Operations -- PASS

- MOV, ADD, SUB, MUL, SDIV opcodes correct
- Immediate encodings (IMM12, IMM16) properly positioned
- Bitmask immediates use correct N/immr/imms encoding
- UBFX/SBFX lsb+width calculation correct
- CLZ, SXTW, UXTH all have correct opcodes

### Section 15: Conditional Select -- PASS

- CSEL, CSINC, CSET opcodes verified
- CSET correctly inverts condition (XOR 1)

### Section 16-19: Load/Store -- PASS

- Offset scaling correct: B(x1), H(x2), W(x4), X(x8), Q(x16)
- Register offset variants use correct S/option bits
- STP/LDP pair encodings verified (pre-index, post-index, signed offset)
- IMM7 encoding for pairs correctly divides by 8

### Section 20: Branch/Control Flow -- PASS

- B, BL, B.cond, CBZ, CBNZ, TBZ, TBNZ all verified
- Offset field encodings (imm14, imm19, imm26) correct
- BIT_TBxZ macro correctly handles bit positions 0-31 (and sets bit 31 for bits 32+)
- Forward-branch patching macros use correct OR to inject offset into placeholder

### Section 21-29: NEON Operations -- PASS (with Bug A exception)

- Element size specifiers correct for all arithmetic ops
- Widening multiply (SMULL) opcode verified
- Shift right (USHR/SSHR) immh:immb encoding uses (element_size - shift)
- Narrowing operations (SQXTN, SQXTUN) opcodes correct
- ZIP1/ZIP2, EXT permute opcodes verified
- DUP lane encoding uses correct (lane * element_size_bytes + element_size_bytes/2) formula
- INS/UMOV lane encodings verified

### Section 31: Struct Offsets -- PASS

- All 30+ _Static_assert checks guarantee compile-time correctness
- No manual offset values to get wrong

### Phase 1 (Prologue/Epilogue) -- PASS (with Bug C exception)

- Register save/restore pairs symmetric between prologue and epilogue
- NEON constant loading via MOVZ+MOVK+LDR Q is correct
- EMIT_MOV_IMM64 macro correctly emits 4-instruction sequence
- Frame pointer setup (MOV x29, SP) present

### Phase 2 (Pixel Loop/Depth) -- PASS

- Stipple test: pattern and rotating modes both correct
- Tiled X calculation: `(x & 63) + ((x >> 6) << 11)` matches x86-64
- W-depth calculation: CLZ-based BSR equivalent is correct
  - `exp = CLZ - 16` matches `exp = 15 - (31 - CLZ)` simplification
  - Shift, mantissa extraction, and 0xFFFF clamp all verified
- Z-buffer depth: `z >> 12` clamped to [0, 0xFFFF] matches x86-64
- Depth bias: `(depth + zaColor) & 0xFFFF` via UXTH correct
- All 6 depth operations use correct unsigned condition codes

### Phase 3 (Texture Fetch) -- PASS

- Perspective W division via SDIV correct
- LOD calculation via CLZ matches BSR-based x86-64 logic
- Bilinear filter: sub-texel fraction extraction, 4-texel fetch, weight
  multiply, pairwise add, and >>8 normalization all verified
- Point-sample: coordinate computation and single texel load correct
- Mirror S/T via TBZ+MVN correct
- Clamp/wrap via CSEL and AND correct

### Phase 3 (TMU Combine) -- PASS

- Dual-TMU path: TMU1 fetch, texture combine (tc_*), alpha combine (tca_*)
- Signed multiply sequence: SMULL+SSHR+SQXTN correct for texture combine
  (SSHR matches x86-64 PSRAD)
- Bug fix for x86-64 line 1303 (ADD w4, #1 instead of 0x8E) correctly implemented
- Trilinear LOD blend factors: XOR with xmm_00_ff_w table correct

### Phase 4 (Color/Alpha Combine) -- PASS (with Bug A exception)

- Chroma key test: XOR+AND 24-bit+CBZ correct
- Alpha select (a_other): all 4 sources correctly implemented
- Alpha local (a_local): all 4 sources with proper clamping
- CCA combine: mselect, reverse_blend, multiply, add, clamp all correct
- CC combine: mselect (6 sources), reverse_blend, multiply structure correct
- **Bug A**: SSHR at line ~3025 should be USHR (see above)
- cc_invert_output: EOR with v10 (xmm_ff_b = 0x00FFFFFF) correct

### Phase 5 (Fog) -- PASS (with Issues D, E)

- FOG_CONSTANT: UQADD saturating byte add correct
- Non-constant fog: unpack, subtract, >>1, multiply, >>7 structure correct
- fog_a sources: w_depth table, z, ia, w all correctly computed
- fog_a doubling and alookup multiply correct
- **Issue D**: alookup indexing differs from x86-64 by +1 (see above)

### Phase 5 (Alpha Test) -- PASS

- All 6 alpha test functions use correct inverted condition codes
- AFUNC_NEVER correctly emits RET (bail out)
- AFUNC_ALWAYS case handled by outer conditional (no test emitted)
- Alpha reference loaded from PARAMS_alphaMode+3 (correct byte offset)

### Phase 5 (Alpha Blend) -- PASS (with Bug B exception)

- Dest alpha loading from aux buffer correct
- RGB565 decode via rgb565[] LUT correct
- All 10 dest_afunc cases: blend factor computation and >>8 rounding correct
- All 10 src_afunc cases: same pattern, verified
- AFUNC_ASATURATE: min(src_alpha, 0xFF - dst_alpha) logic correct
- AFUNC_ACOLORBEFOREFOG: uses v13 (pre-fog color copy) correctly
- **Bug B**: alpha-of-alpha shift is 6 instead of 7 (see above)

### Phase 6 (Dither/FB Write/Depth Write) -- PASS

- Dither table pointer loaded correctly (dither_rb/dither_g for 4x4,
  dither_rb2x2/dither_g2x2 for 2x2)
- Dither index computation: value*stride + y*row_size + x correct
- RGB565 no-dither pack: UBFX bit positions verified (B[7:3], G[15:10], R[23:19])
- Framebuffer write: STRH correct for 16-bit pixel
- Depth write (alpha-buffer path): STRH w12 correct
- Depth write (non-alpha path): LDRH new_depth + STRH correct
- Skip position patching: all 5 skip types use correct patch macros

### Phase 6 (Per-Pixel Increments) -- PASS

- ib/ig/ir/ia: LD1+ADD/SUB V4S+ST1 at STATE_ib(472, non-aligned) correct
- z: LDR+ADD/SUB+STR W correct
- tmu0 s/t: LDR Q at STATE_tmu0_s(496, Q-aligned) + ADD/SUB V2D correct
- tmu0_w, w: LDR X + ADD/SUB X + STR X correct
- tmu1 s/t: ADD+LD1 (520 not Q-aligned) + ADD/SUB V2D correct
- tmu1_w: same pattern as tmu0_w, correct
- pixel_count++, texel_count += 1 or 2: correct
- X increment: ADD/SUB 1 based on xdir, CMP with x2, B.NE loop back

### Cache/W^X (voodoo_get_block) -- PASS

- Cache key comparison includes all relevant fields
- W^X bracket covers both voodoo_generate() and metadata writes
- __clear_cache() called after re-enabling execute permission
- Block cycling via next_block_to_write correct

### Lookup Table Init (voodoo_codegen_init) -- PASS

- alookup[0..255]: broadcasts c to 4 lanes, upper 4 lanes zero
- alookup[256]: special entry with value 256 (for clamped alpha)
- aminuslookup[0..255]: broadcasts (255-c) to 4 lanes
- bilinear_lookup: d[0..3] weight calculation matches x86-64
- neon_00_ff_w: [0]={0}, [1]={0xFF x4} for trilinear blend mask

---

## REGISTER ALLOCATION ANALYSIS

### GPR Conflicts: NONE FOUND

- x0 (state) and x1 (params) never clobbered -- always used as base registers
- x8 (fb_mem) and x9 (aux_mem) loaded once, never overwritten
- x19-x25 (pinned pointers) loaded in prologue, preserved by callee-save
- x24 (real_y) preserved across loop iterations
- Scratch registers (x4-x7, x10-x15) are reloaded before each use
- x16-x17 (IP0/IP1) only used for temporary pointer computation

### NEON Conflicts: NONE FOUND

- v8-v11 (constants) loaded once, never overwritten by generated code
- v13 (color-before-fog) written once, read only in ACOLORBEFOREFOG path
- v0-v7, v16-v17 used as scratch, always reloaded before use
- v2 (zero) cleared before each pixel and re-used where needed

---

## SUMMARY

| ID | Description | Severity | Status | Line |
|----|-------------|----------|--------|------|
| A  | Color combine SSHR instead of USHR | CRITICAL | Known (Bug #6) | ~3025 |
| B  | Alpha-of-alpha blend shift off by 1 | CRITICAL | **NEW** | ~3528, ~3534 |
| C  | Stack frame overflow (128 vs 144 bytes) | CRITICAL | Re-introduced | ~1700 |
| D  | Fog alookup off-by-one vs x86-64 | LOW | Potential | ~3155 |
| E  | Fog blend factor rounding | LOW | Known (Bug #3) | ~3139 |

**Recommended fix priority**:
1. Bug C (stack overflow) -- safety issue, fix first
2. Bug A (SSHR->USHR) -- causes visible corruption
3. Bug B (alpha shift) -- causes incorrect alpha blending
4. Issues D, E -- low priority, investigate after visual comparison

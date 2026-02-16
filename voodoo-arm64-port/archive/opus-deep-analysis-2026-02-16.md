# Opus Deep Analysis — Voodoo ARM64 JIT Horizontal Striping Corruption

**Date:** 2026-02-16
**Analyst:** Claude Opus 4.6 (direct, no subagents)
**Status:** ROOT CAUSE IDENTIFIED

## Root Cause: LD1/ST1 Opcode Bug — 2-Register Instead of 1-Register

### The Bug

In `vid_voodoo_codegen_arm64.h`, lines 859 and 862:

```c
#define ARM64_LD1_V4S(t, n) (0x4C40A800 | Rn(n) | Rt(t))  // WRONG
#define ARM64_ST1_V4S(t, n) (0x4C00A800 | Rn(n) | Rt(t))  // WRONG
```

The opcode field (bits [15:12]) is `1010`, which encodes **LD1/ST1 with 2 consecutive registers**:
- `LD1 {Vt.4S, V(t+1).4S}, [Xn]` — loads 32 bytes into Vt AND V(t+1)
- `ST1 {Vt.4S, V(t+1).4S}, [Xn]` — stores 32 bytes from Vt AND V(t+1)

The correct opcode for 1-register LD1/ST1 is `0111`:

```c
#define ARM64_LD1_V4S(t, n) (0x4C407800 | Rn(n) | Rt(t))  // CORRECT
#define ARM64_ST1_V4S(t, n) (0x4C007800 | Rn(n) | Rt(t))  // CORRECT
```

### ARM64 Encoding Reference

LD1 (multiple structures, no offset):
```
0 Q 001100 0 L 000000 opcode size Rn Rt
```

Opcode field (bits [15:12]):
- `0010` = 4 registers
- `0110` = 3 registers
- `1010` = 2 registers  <-- CURRENT (WRONG)
- `0111` = 1 register   <-- CORRECT

### Impact Analysis

Every use of LD1_V4S/ST1_V4S loads/stores an extra 16 bytes (one extra V register):

#### Critical Store: Per-pixel ib/ig/ir/ia increment (line 3782)

```c
addlong(ARM64_ADD_IMM_X(16, 0, STATE_ib));    // x16 = &state->ib (offset 472)
addlong(ARM64_LD1_V4S(0, 16));                 // Loads v0 AND v1
addlong(ARM64_ADD_IMM_X(17, 1, PARAMS_dBdX));
addlong(ARM64_LD1_V4S(1, 17));                 // Loads v1 AND v2
// ... ADD/SUB v0, v0, v1 ...
addlong(ARM64_ST1_V4S(0, 16));                 // Stores v0 AND v1 to &state->ib
```

The ST1 stores 32 bytes starting at `&state->ib` (offset 472):
- `[472..487]` = v0 = {ib, ig, ir, ia} -- CORRECT
- `[488..503]` = v1 = {dBdX, dGdX, dRdX, dAdX} -- **CORRUPTS state->z, state->new_depth, and state->tmu0_s!**

State memory layout:
```
Offset  Field         Stored Value (WRONG)
472     ib            v0[0] <- correct
476     ig            v0[1] <- correct
480     ir            v0[2] <- correct
484     ia            v0[3] <- correct
488     z             dBdX  <- CORRUPTED
492     new_depth     dGdX  <- CORRUPTED
496     tmu0_s (low)  dRdX  <- CORRUPTED
500     tmu0_s (high) dAdX  <- CORRUPTED
```

The subsequent z fixup (lines 3786-3794) reads the ALREADY CORRUPTED z value, adds dZdX, and stores back — so z remains wrong.

The TMU0 s/t increment (lines 3801-3814) reads the CORRUPTED tmu0_s, adds dSdX, and stores back — so texture coordinates accumulate errors every pixel.

#### Critical Store: TMU1 s/t increment (line 3851)

```c
addlong(ARM64_ST1_V4S(0, 16));  // x16 = &state->tmu1_s (offset 520)
```

Stores 32 bytes:
- `[520..535]` = v0 = {tmu1_s, tmu1_t} -- correct
- `[536..551]` = v1 = {TMU1 dSdX, TMU1 dTdX} -- **CORRUPTS state->tmu1_w and state->w!**

#### Critical Load: Color combine cother (line 2844)

```c
addlong(ARM64_LD1_V4S(0, 16));  // Loads v0 AND v1 from &state->ib
```

When cother=ITER_RGB: this clobbers v1 which holds clocal (already computed). If cc_sub_clocal is true, the subsequent `SUB v0, v0, v1` subtracts garbage instead of the correct clocal value.

### Why This Causes Horizontal Striping

1. **tmu0_s corruption**: Every pixel iteration corrupts the texture S coordinate with delta values. The texture fetch on the NEXT iteration reads the wrong coordinate, producing wrong texels.

2. **Accumulating error**: Each pixel adds dSdX to an already-wrong tmu0_s, so the error grows across the span.

3. **Per-scanline reset**: The C caller resets state->tmu0_s at the start of each span, so each scanline starts fresh but degrades from left to right.

4. **Consistent pattern**: The corruption follows a predictable pattern based on the delta values, creating regular horizontal banding.

### Why Three Audits Missed It

The audits verified the LOGIC (register allocation, data flow, branch targets) but not the raw instruction ENCODING of the LD1/ST1 macros. The opcode for 1-register vs 2-register differs by only 2 bits in a single field. The macro definition looks plausible at first glance — `0x4C40A800` vs the correct `0x4C407800`.

### All Uses

| Line | Instruction | Effect |
|------|-------------|--------|
| 2649 | LD1_V4S(16, 16) | Loads v16+v17; v17 clobber probably harmless |
| 2802 | LD1_V4S(1, 16) | Loads v1+v2; v2 clobber probably harmless |
| 2823 | LD1_V4S(1, 16) | Same as above |
| 2844 | LD1_V4S(0, 16) | Loads v0+v1; **CLOBBERS clocal in v1** |
| 3774 | LD1_V4S(0, 16) | Loads v0+v1; v1 overwritten next line |
| 3776 | LD1_V4S(1, 17) | Loads v1+v2; v2 clobber harmless |
| 3782 | **ST1_V4S(0, 16)** | **CORRUPTS z, new_depth, tmu0_s** |
| 3841 | LD1_V4S(0, 16) | Loads v0+v1; v1 clobber probably harmless |
| 3851 | **ST1_V4S(0, 16)** | **CORRUPTS tmu1_w, w** |

### Fix

Change two bytes in two macro definitions (line 859, 862):

```c
// Line 859: Change 0xA8 -> 0x78
#define ARM64_LD1_V4S(t, n) (0x4C407800 | Rn(n) | Rt(t))

// Line 862: Change 0xA8 -> 0x78
#define ARM64_ST1_V4S(t, n) (0x4C007800 | Rn(n) | Rt(t))
```

### Verification

Correct encoding for LD1 {Vt.4S}, [Xn] (1 register, 128-bit):
```
0 1 001100 0 1 000000 0111 10 nnnnn ttttt
= 0x4C407800
```

Correct encoding for ST1 {Vt.4S}, [Xn] (1 register, 128-bit):
```
0 1 001100 0 0 000000 0111 10 nnnnn ttttt
= 0x4C007800
```

---

## Independent Verification Pass (GPT-5) — 2026-02-16

### Scope covered in this pass
- ARM64 dither + RGB565 pack path (`src/include/86box/vid_voodoo_codegen_arm64.h:3577-3708`)
- x86-64 reference dither + RGB565 pack path (`src/include/86box/vid_voodoo_codegen_x86-64.h:3077-3221`)
- ARM64 per-pixel increment block (`src/include/86box/vid_voodoo_codegen_arm64.h:3771-3851`)
- x86-64 reference per-pixel increment block (`src/include/86box/vid_voodoo_codegen_x86-64.h:3260-3395`)
- State layout / offset validity (`src/video/vid_voodoo_render.c:49-101`, offset asserts in ARM64 header)

### Result summary (this pass)
1. Dither and RGB565 packing logic in ARM64 currently matches x86-64 behavior.
2. The LD1/ST1 encoding issue is real and severe when present: it turns 16-byte ops into 32-byte ops and corrupts adjacent state.
3. In this current tree, macros at `src/include/86box/vid_voodoo_codegen_arm64.h:859` and `src/include/86box/vid_voodoo_codegen_arm64.h:862` are already the corrected 1-register encodings (`0x4C407800`, `0x4C007800`).

### Line-by-line comparison notes

#### A) Dither indexing math (ARM64 vs x86-64)
- x86-64 4x4 path:
  - `EDX &= 3` (x mask) at `vid_voodoo_codegen_x86-64.h:3111-3113`
  - `ESI &= 3` (y mask) at `vid_voodoo_codegen_x86-64.h:3114-3116`
  - `LEA ESI, [RDX + RSI*4]` (sub-index) at `vid_voodoo_codegen_x86-64.h:3135-3137`
  - `ECX <<= 4` and `EAX &= 0xff0` for table value offsets at `vid_voodoo_codegen_x86-64.h:3155-3159`
- ARM64 4x4 path:
  - `w10 = x & 3` at `vid_voodoo_codegen_arm64.h:3627`
  - `w5 = y & 3` at `vid_voodoo_codegen_arm64.h:3628`
  - `w5 = x + (y << 2)` at `vid_voodoo_codegen_arm64.h:3644`
  - `R <<= 4` and `B <<= 4` at `vid_voodoo_codegen_arm64.h:3645-3646`
- Equivalence: confirmed.

#### B) Channel extraction in dither path
- x86-64 uses:
  - `AH` for G (`vid_voodoo_codegen_x86-64.h:3097-3099`)
  - `AL` for low channel (`vid_voodoo_codegen_x86-64.h:3121-3123`)
  - shifted/masked `EAX` for the other RB channel (`vid_voodoo_codegen_x86-64.h:3132-3134`, `3158-3159`)
- ARM64 uses explicit extraction from packed 32-bit color:
  - G: `UBFX(6,4,8,8)` at `vid_voodoo_codegen_arm64.h:3603`
  - R: `UBFX(13,4,16,8)` at `vid_voodoo_codegen_arm64.h:3632`
  - B: `AND_MASK(6,4,8)` at `vid_voodoo_codegen_arm64.h:3634`
- Packing to RGB565:
  - ARM64 `R<<11 | G<<5 | B` at `vid_voodoo_codegen_arm64.h:3677-3680`
  - x86-64 same bit placement at `vid_voodoo_codegen_x86-64.h:3174-3183`
- Equivalence: confirmed.

#### C) No-dither RGB565 packing
- x86-64 path at `vid_voodoo_codegen_x86-64.h:3185-3211`.
- ARM64 path at `vid_voodoo_codegen_arm64.h:3693-3703`.
- ARM64 bitfield extraction (`UBFX`) matches x86 shifts/masks:
  - blue 5 bits from byte0,
  - green 6 bits from byte1,
  - red 5 bits from byte2.
- Equivalence: confirmed.

#### D) Register-reuse safety in dither path
- Concerned registers per request:
  - `w6` (G then B): overwritten intentionally after G is preserved in `w11` (`vid_voodoo_codegen_arm64.h:3630` then `3634`).
  - `w11` preserves G offset and later loaded dithered G (`3659-3669`).
  - `w5` used for y mask then sub-index (`3628`, `3644`) and then added to base in x7 (`3651`).
- No destructive reuse bug found in this segment.

### Verified calculation checks
- 4x4 index formula in ARM64 equals scalar path in `src/video/vid_voodoo_render.c:1362-1364`:
  - `index = value*16 + (real_y & 3)*4 + (x & 3)`.
- 2x2 index formula in ARM64 equals scalar path in `src/video/vid_voodoo_render.c:1358-1360`:
  - `index = value*4 + (real_y & 1)*2 + (x & 1)`.

### Critical discrepancy validated independently: LD1/ST1 encoding class

Even though current macros are now corrected, I validated by disassembly that:
- `0x4C40A800` decodes as `ld1.4s {v0, v1}, [x0]` (2-register)
- `0x4C407800` decodes as `ld1.4s {v0}, [x0]` (1-register)
- `0x4C00A800` decodes as `st1.4s {v0, v1}, [x0]` (2-register)
- `0x4C007800` decodes as `st1.4s {v0}, [x0]` (1-register)

Reproduction command used:
- Assembled raw words with clang/otool in `/tmp`; output showed exact decodes above.

Why this matters to striping:
- At `vid_voodoo_codegen_arm64.h:3773-3782`, a 2-register `ST1` would overwrite fields after `ib/ig/ir/ia` (including `z`, `new_depth`, and beginning of `tmu0_s`).
- At `vid_voodoo_codegen_arm64.h:3840-3851`, a 2-register `ST1` would overwrite fields after `tmu1_s/t` (including `tmu1_w` and `w`).
- This exactly matches a deterministic per-pixel state drift pattern that manifests as scanline-dependent corruption.

### Additional consistency checks
- `voodoo_state_t` field order at `src/video/vid_voodoo_render.c:78-90` matches ARM64 offset constants and static asserts in `vid_voodoo_codegen_arm64.h:968-983`.
- Dither row source (`real_y`) is preserved from arg3 into `x24` at `vid_voodoo_codegen_arm64.h:1722-1723` and read in dither block at `3598-3599`; calling convention matches `voodoo_draw(state, params, x, real_y)` at `src/video/vid_voodoo_render.c:955`.

### Additional verification added (encoding sanity)

I disassembled representative dither helper encodings to rule out macro-helper mistakes:
- `ARM64_AND_MASK(10,14,2)` -> `and w10, w14, #0x3`
- `ARM64_AND_MASK(6,4,8)` -> `and w6, w4, #0xff`
- `ARM64_UBFX(6,4,8,8)` -> `ubfx w6, w4, #8, #8`
- `ARM64_LSL_IMM(13,13,4)` -> `lsl w13, w13, #4`
- `ARM64_LDRB_REG` base opcode -> `ldrb w0, [x0, x0]`

Conclusion from this micro-check: dither helper macros encode correctly and are not the striping root cause.

### Working-tree state relevant to this bug

`git diff` for `src/include/86box/vid_voodoo_codegen_arm64.h` currently shows the LD1/ST1 macro constants changed from:
- `0x4C40A800` -> `0x4C407800`
- `0x4C00A800` -> `0x4C007800`

So this tree already contains the fix in source, but it is still an uncommitted modification in the working tree.

Practical implication:
- If corruption observations came from binaries built before this local edit, the behavior would still match the striping/corruption signature.
- A rebuild using the corrected macros is required before concluding whether additional bugs remain.

## Final conclusion from this investigation pass

### Most likely root cause of the persistent striping/color corruption

The specific ARM64 bug that best explains the observed artifacts is the `LD1_V4S` / `ST1_V4S` opcode-class mismatch (2-register form instead of 1-register form) when it existed as `0x4C40A800` / `0x4C00A800`.

Evidence chain:
- Disassembly confirms these constants are 2-register forms.
- They are used in per-pixel state update stores at:
  - `src/include/86box/vid_voodoo_codegen_arm64.h:3782`
  - `src/include/86box/vid_voodoo_codegen_arm64.h:3851`
- In 2-register form, each store writes 32 bytes and overwrites adjacent state fields (`z`, `new_depth`, `tmu0_s`, `tmu1_w`, `w`), creating deterministic scanline-evolving corruption.

### Specific code changes needed

If your branch still has old constants, change exactly:
- `src/include/86box/vid_voodoo_codegen_arm64.h:859`
  - from `#define ARM64_LD1_V4S(t, n) (0x4C40A800 | Rn(n) | Rt(t))`
  - to   `#define ARM64_LD1_V4S(t, n) (0x4C407800 | Rn(n) | Rt(t))`
- `src/include/86box/vid_voodoo_codegen_arm64.h:862`
  - from `#define ARM64_ST1_V4S(t, n) (0x4C00A800 | Rn(n) | Rt(t))`
  - to   `#define ARM64_ST1_V4S(t, n) (0x4C007800 | Rn(n) | Rt(t))`

### Current tree status note

In the current working tree I inspected, those two lines are already set to the corrected values.

Therefore, if corruption appears unchanged right now, the next high-probability explanation is stale runtime/build artifacts (binary built before this macro fix). Rebuild and retest is required to validate whether this was the remaining blocker.

### What I did NOT find (requested focus areas)

No ARM64-vs-x86 mismatch found in:
- RGB565 bit packing logic (`src/include/86box/vid_voodoo_codegen_arm64.h:3676-3703` vs x86 reference `3077-3211`)
- Dither index math and table selection (`src/include/86box/vid_voodoo_codegen_arm64.h:3615-3674`)
- Register reuse (`w6`, `w11`, `w5`) in the dither path
- Shift multipliers and 2x2/4x4 index scaling

These sections are line-by-line consistent with x86 behavior in this tree.

# Voodoo ARM64 JIT Debug Findings

Date: 2026-02-15
Reviewer: voodoo-debug agent
Files analyzed:
- `src/include/86box/vid_voodoo_codegen_arm64.h` (4115 lines)
- `src/include/86box/vid_voodoo_codegen_x86-64.h` (3561 lines, reference)

## Bug #1: Stack frame overflow — STP d12/d13 writes beyond allocated frame

- **Location**: `src/include/86box/vid_voodoo_codegen_arm64.h:1703-1704`
- **Code**:
  ```c
  /* STP x29, x30, [SP, #-128]! */           // Allocates 128 bytes
  addlong(ARM64_STP_PRE_X(29, 30, 31, -128));
  ...
  /* STP d12, d13, [SP, #128] */              // Writes at byte offset 128 = OUTSIDE frame!
  addlong(ARM64_STP_D(12, 13, 31, 128));
  ```
- **Problem**: The prologue allocates a 128-byte frame (`SP -= 128`), giving valid
  offsets [0, 127]. But `STP d12, d13, [SP, #128]` stores at byte offset 128,
  which is the first byte of the **caller's** stack frame. This is stack corruption.
  The frame needs 9 register pairs x 16 bytes = 144 bytes minimum.

  Frame layout showing the overflow:
  ```
  SP+0:   x29, x30   (16 bytes)
  SP+16:  x19, x20
  SP+32:  x21, x22
  SP+48:  x23, x24
  SP+64:  x25, x26
  SP+80:  x27, x28
  SP+96:  d8, d9
  SP+112: d10, d11
  SP+128: d12, d13   <-- OUT OF BOUNDS (writes into caller's stack!)
  ```

  The symmetric LDP in the epilogue (line 3913) reads back from the same
  out-of-bounds location, so d12/d13 themselves survive. But the write
  corrupts 16 bytes of the caller's stack frame on every JIT invocation.

- **Reference**: No equivalent on x86-64 (uses PUSH/POP which auto-manage SP).
- **Impact**: Corrupts caller's stack locals. May cause intermittent wrong behavior
  in the calling C code (vid_voodoo_render). Could contribute to visual corruption
  by corrupting renderer state variables between JIT calls.
- **Severity**: CRITICAL
- **Fix**: Change frame allocation from 128 to 144 bytes (already 16-byte aligned):
  - Line 1688: Change `ARM64_STP_PRE_X(29, 30, 31, -128)` to `-144`
  - Line 3929: Change `ARM64_LDP_POST_X(29, 30, 31, 128)` to `144`
  - Adjust ALL STP/LDP offsets by +16 (shift registers up to make room):
    - Or simpler: keep d12/d13 at offset 128, increase frame to 144

## Bug #2: Wrong shift type in alpha combine — ASR instead of LSR

- **Location**: `src/include/86box/vid_voodoo_codegen_arm64.h:2905`
- **Code**:
  ```c
  addlong(ARM64_MUL(12, 12, 4));          /* w12 = (a_diff) * factor */
  addlong(ARM64_ASR_IMM(12, 12, 8));      /* w12 >>= 8 */
  ```
- **Problem**: The x86-64 reference uses `SHR EDX, 8` (unsigned/logical shift right)
  at line 2057:
  ```
  addbyte(0xc1); /*SHR EDX, 8*/
  addbyte(0xea);
  addbyte(8);
  ```
  The ARM64 uses `ASR` (arithmetic/signed shift right). For negative multiply
  results (when `a_other < a_local`), these produce different values:
  - `SHR` shifts in zeros: e.g., -256 (0xFFFFFF00) >> 8 = 0x00FFFFFF = 16777215
  - `ASR` shifts in ones:  e.g., -256 >> 8 = -1

  After subsequent clamping:
  - x86-64: 16777215 clamped to 0xFF (max alpha)
  - ARM64: -1 clamped to 0 (min alpha)

  This means for cases where `a_other < a_local`, the alpha combine produces
  OPPOSITE results (0xFF on x86-64 vs 0 on ARM64). This directly affects
  the color blend factor when `CC_MSELECT_AOTHER` is selected, causing
  incorrect color blending that would manifest as wrong brightness/contrast
  in rendered pixels.

- **Reference**: x86-64 line 2057: `SHR EDX, 8` (opcode C1 EA 08, /5 = SHR)
- **Impact**: Wrong alpha values when alpha_other < alpha_local. Affects
  transparency, brightness, and color blending. Could cause striping if
  some pixels have a_other > a_local (correct) and others have a_other < a_local
  (wrong), creating alternating bright/dark bands.
- **Severity**: CRITICAL
- **Fix**: Change `ARM64_ASR_IMM(12, 12, 8)` to `ARM64_LSR_IMM(12, 12, 8)`

## Bug #3: Fog blend factor off by one — alookup[fog_a] instead of alookup[fog_a + 1]

- **Location**: `src/include/86box/vid_voodoo_codegen_arm64.h:3182-3183`
- **Code**:
  ```c
  addlong(ARM64_ADD_REG_X_LSL(5, 20, 4, 3));  /* x5 = x20 + w4*8 */
  addlong(ARM64_LDR_D(5, 5, 0));              /* v5 = alookup[fog_a] */
  ```
- **Problem**: The x86-64 fog multiply at line 2388-2394 accesses the alookup
  table with a +16 byte displacement:
  ```
  PMULLW XMM3, [R10 + RAX*8 + 16]
  ```
  With RAX = fog_a * 2, the effective address is:
  `alookup_base + fog_a*2*8 + 16 = alookup_base + (fog_a+1)*16 = &alookup[fog_a + 1]`

  This means the x86-64 uses `alookup[fog_a + 1]` = `{fog_a+1, fog_a+1, fog_a+1, fog_a+1}`
  as the fog blend factor. The ARM64 code uses `alookup[fog_a]` = `{fog_a, fog_a, fog_a, fog_a}`.

  The +1 is intentional: the fog blend formula needs factor = fog_a + 1 so that
  fog_a = 255 produces factor = 256 (full fog color replacement). Without the +1,
  fog at maximum intensity only reaches 255/256 of full fog color.

- **Reference**: x86-64 line 2388-2394: displacement = 16 = sizeof(__m128i) = one
  alookup entry beyond the base index.
- **Impact**: Fog is slightly too weak at all intensity levels. At fog_a = 0, no fog
  is applied (correct: factor = 0). At fog_a = 255, fog is 255/256 instead of full.
  Subtle visual difference, not the cause of major striping.
- **Severity**: MEDIUM (affects fog only, which is an optional feature)
- **Fix**: Change the LDR offset or adjust the index:
  ```c
  /* Option A: add 16 to the LDR offset */
  addlong(ARM64_LDR_D(5, 5, 16));  /* v5 = alookup[fog_a + 1] */

  /* Option B: pre-increment fog_a before computing address */
  addlong(ARM64_ADD_IMM(4, 4, 2));  /* fog_a*2 += 2 (= (fog_a+1)*2) */
  addlong(ARM64_ADD_REG_X_LSL(5, 20, 4, 3));
  addlong(ARM64_LDR_D(5, 5, 0));
  ```

## Bug #4: Fog SSHR should be SSHR not MUL_V4H for multiply

- **Location**: `src/include/86box/vid_voodoo_codegen_arm64.h:3185-3186`
- **Code**:
  ```c
  addlong(ARM64_MUL_V4H(3, 3, 5));   /* v3 *= alookup[fog_a] */
  addlong(ARM64_SSHR_V4H(3, 3, 7));  /* v3 >>= 7 (arithmetic) */
  ```
- **Problem**: The x86-64 reference at line 2388-2399 uses:
  ```
  PMULLW XMM3, alookup[...]     ; signed 16x16 -> low 16 multiply
  PSRAW XMM3, 7                 ; arithmetic shift right 7
  ```
  `PMULLW` is a signed multiply that produces the low 16 bits of each
  16x16 product. The ARM64 `MUL_V4H` is unsigned multiply (`MUL v.4H`).

  In this context, v3 contains `(fogColor - color) >> 1` which can be NEGATIVE
  (signed 16-bit values). Using unsigned multiply on negative values gives
  wrong results.

  For example: v3 lane = -50 (0xFFCE as uint16), factor = 128 (0x0080).
  - Signed: -50 * 128 = -6400 -> low 16 bits = 0xE700 -> SSHR 7 = -50 (correct)
  - Unsigned: 0xFFCE * 0x0080 = 0x7FE700 -> low 16 bits = 0xE700 (same!)

  Actually, for PMULLW (low 16 of 16x16), the low 16 bits are the SAME for
  both signed and unsigned multiply. So `MUL v.4H` (which also gives low 16 bits)
  produces the same result. This is NOT a bug.

  **RETRACTED**: After analysis, this is NOT a bug. Low 16 bits of signed and
  unsigned 16x16 multiply are identical. The ARM64 MUL_V4H is correct here.

- **Severity**: NOT A BUG (retracted)

## Bug #5: FOG_W clamp uses wrong condition — CSEL with COND_HI for signed comparison

- **Location**: `src/include/86box/vid_voodoo_codegen_arm64.h:3155-3161`
- **Code**:
  ```c
  /* FOG_W case */
  addlong(ARM64_LDR_W(4, 0, STATE_w + 4));  /* high word of w */
  addlong(ARM64_CMP_IMM(4, 0));
  addlong(ARM64_CSEL(4, 31, 4, COND_LT));   /* if negative, 0 */
  addlong(ARM64_MOVZ_W(5, 0xFF));
  addlong(ARM64_CMP_REG(4, 5));
  addlong(ARM64_CSEL(4, 5, 4, COND_HI));    /* if > 0xFF, 0xFF */
  ```
- **Problem**: The x86-64 reference at line 2377 uses `CMOVAE` (unsigned above or equal):
  ```
  CMP EAX, 0xff
  CMOVAE EAX, EBX    ; if EAX >= 0xFF (unsigned), EAX = 0xFF
  ```
  The ARM64 uses `COND_HI` (unsigned strictly greater than). This means:
  - x86-64: clamp when value >= 0xFF (includes 0xFF itself)
  - ARM64: clamp when value > 0xFF (excludes 0xFF)

  When value = 0xFF: x86 clamps to 0xFF (no-op), ARM64 keeps 0xFF (also no-op).
  When value = 0x100: x86 clamps to 0xFF, ARM64 clamps to 0xFF.
  So the behavior is actually the same since CSEL destination is also 0xFF.

  **RETRACTED**: Not a bug. COND_HI and "AE" produce equivalent results here
  because the clamp target equals the threshold.

- **Severity**: NOT A BUG (retracted)

---

## Bug #6: Color combine multiply uses SSHR (arithmetic) instead of USHR (logical)

- **Location**: `src/include/86box/vid_voodoo_codegen_arm64.h:3025`
- **Code**:
  ```c
  /* Signed multiply: v0 * v3 -> 32-bit -> >>8 -> saturating narrow
   * SMULL v17.4S, v0.4H, v3.4H
   * SSHR v17.4S, v17.4S, #8       <-- BUG: should be USHR
   * SQXTN v0.4H, v17.4S
   */
  addlong(ARM64_SMULL_4S_4H(17, 16, 3));
  addlong(ARM64_SSHR_V4S(17, 17, 8));     // <-- THE BUG
  addlong(ARM64_SQXTN_4H_4S(0, 17));
  ```
- **Problem**: The x86-64 COLOR COMBINE uses `PSRLD XMM0, 8` (LOGICAL shift right)
  at line 2199:
  ```
  PMULLW XMM0, XMM3      ; low 16 of signed multiply
  PMULHW XMM4, XMM3      ; high 16 of signed multiply
  PUNPCKLWD XMM0, XMM4   ; form full 32-bit signed products
  PSRLD XMM0, 8          ; LOGICAL (unsigned) shift right by 8
  PACKSSDW XMM0, XMM0    ; signed saturating narrow 32->16
  ```

  Note the critical distinction: the TEXTURE combines (lines 1204, 1507) use
  `PSRAD` (ARITHMETIC shift right), but the COLOR combine uses `PSRLD` (LOGICAL
  shift right). The ARM64 code uses `SSHR` (arithmetic) for ALL of them, which
  is correct for texture combines but WRONG for the color combine.

  When the 32-bit product is negative (e.g., cother < clocal in some channel):
  - x86-64 PSRLD: shifts zeros in from MSB, turning negative into large positive
    (e.g., -256 -> 0x00FFFFFF = 16777215), then PACKSSDW saturates to +32767
  - ARM64 SSHR: preserves sign bit, keeping negative (e.g., -256 -> -1),
    then SQXTN preserves negative value

  After cc_add (adds clocal back) and SQXTUN/PACKUSWB (clamp to [0,255]):
  - x86-64: 32767 + clocal -> huge positive -> clamped to 255
  - ARM64: -1 + clocal -> small value -> clamped to clocal or small value

  This means whenever cother < clocal in any RGB channel, the color combine
  produces 255 (white/bright) on x86-64 but a small/dark value on ARM64.
  This difference creates visible striping/banding wherever adjacent pixels
  alternate between cother > clocal (correct) and cother < clocal (wrong).

- **Reference**: x86-64 line 2199: `PSRLD XMM0, 8` (opcode 66 0F 72 E0 08)
  vs texture combine line 1204: `PSRAD XMM0, 8` (opcode 66 0F 72 E0 08).
  Note: `PSRLD` = Group 13, /2 (logical), `PSRAD` = Group 13, /4 (arithmetic).
- **Impact**: CRITICAL. Affects the MAIN color combine path that runs on every
  pixel. Produces visibly wrong colors whenever the color difference is negative
  in any channel. This is almost certainly the root cause of the striping
  corruption, because:
  1. It affects the color combine (Phase 4), which runs on EVERY pixel
  2. It produces dramatically different results (255 vs small value) for
     negative intermediate products
  3. The effect is systematic and position-dependent (creates regular patterns)
  4. Fixes to Phase 5/6 bugs (#2 ASR->LSR, #4 shift 6->7) would NOT fix
     this because the damage is done before the blend/dither stages
- **Severity**: CRITICAL -- ROOT CAUSE of striping corruption
- **Fix**: Change line 3025 from:
  ```c
  addlong(ARM64_SSHR_V4S(17, 17, 8));
  ```
  to:
  ```c
  addlong(ARM64_USHR_V4S(17, 17, 8));
  ```
  Also update the comment on line 3020 from `SSHR` to `USHR`.

  NOTE: Do NOT change the texture combine instances (lines 2272, 2468) -- those
  correctly use SSHR to match the x86-64 PSRAD (arithmetic shift).

---

## Summary (updated 2026-02-16)

### Critical bugs (MUST FIX):
1. **Bug #1**: Stack frame overflow (STP d12/d13 at offset 128 in 128-byte frame) -- FIXED
2. **Bug #2**: Wrong shift in alpha combine (ASR instead of LSR) -- FIXED
3. **Bug #6**: Color combine SSHR instead of USHR -- **NEW, UNFIXED, ROOT CAUSE**

### Medium bugs:
4. **Bug #3**: Fog blend factor off by one (alookup[fog_a] vs alookup[fog_a+1])

### Root cause analysis:

**Bug #6 is the root cause of the striping corruption.** Here is why the previous
fixes (#1 stack frame, #2 ASR->LSR, #4 shift 6->7) did not resolve the issue:

- Bug #1 (stack frame) was fixed but fog is not active in the test scene
- Bug #2 (alpha combine ASR) was fixed and is correct, but the damage occurs
  BEFORE the alpha combine, in the color combine (Phase 4)
- Bug #4 (shift 6->7) was correct but irrelevant to the test scene
- Bug #6 is in the COLOR COMBINE multiply (Phase 4), which runs on EVERY pixel
  and produces the wrong color values that flow into all subsequent stages

The key insight: x86-64 uses PSRLD (logical shift) for the color combine but
PSRAD (arithmetic shift) for texture combines. The ARM64 code uses SSHR
(arithmetic) for both, which is correct for textures but wrong for colors.

### Recommended fix:
1. Change line 3025: `ARM64_SSHR_V4S(17, 17, 8)` -> `ARM64_USHR_V4S(17, 17, 8)`
2. Rebuild and test -- this single change should eliminate the striping

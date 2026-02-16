# Voodoo ARM64 JIT Debugging Session - 2026-02-16

## Problem Statement

Visual corruption (horizontal/vertical striping) in Voodoo ARM64 JIT rendering persists despite fixing multiple bugs.

## Session Timeline

### Initial State
- **Branch**: `phase-5-fog-alpha-blend`
- **Status**: Phase 5+6 implemented, visual striping corruption present
- **Test VM**: Windows 98 Low End copy with Voodoo card

### Bugs Found and "Fixed"

We identified and fixed 6 bugs:

1. **Bug #1 - Stack Frame Overflow** (commit a5fe23515)
   - Prologue allocated 128 bytes but stored 144 bytes (9 register pairs)
   - Fixed: Changed SP offset from -128 to -144
   - **Result**: No visual change

2. **Bug #2 - Alpha Blend ASR→LSR** (part of original Phase 5 code)
   - Line 2905: Used ASR (arithmetic shift) instead of LSR (logical)
   - Fixed: Changed `ARM64_ASR_IMM` to `ARM64_LSR_IMM`
   - **Result**: No visual change

3. **Bug #3 - Fog Table Offset** (part of original Phase 5 code)
   - Line 3177: Used offset +0 instead of +16 for fog table lookup
   - Fixed: Changed `LDR_D(5, 5, 0)` to `LDR_D(5, 5, 16)`
   - **Result**: No visual change

4. **Bug #4 - Alpha Blend Shift Amount** (commit 0488894ff)
   - Lines 3521, 3528: Used LSL #6 instead of #7
   - Fixed: Changed shift amount from 6 to 7
   - **Result**: No visual change

5. **Bug #5 - Debug Logging Added** (commit 6612c0ca7)
   - Not a bug fix, added periodic logging for JIT activity
   - Confirmed JIT is executing

6. **Bug #6 - Color Combine SSHR→USHR** (commit 83903f81b)
   - Line 3025: Used SSHR (signed shift) instead of USHR (unsigned)
   - Fixed: Changed `ARM64_SSHR_V4S` to `ARM64_USHR_V4S`
   - **Result**: No visual change

### Critical Discovery #1: JIT Executing But Writing Zero Pixels

Added pixel value tracing (commit 6612c0ca7 + uncommitted changes):
- Configured to skip first 100 triangles (clears)
- Trace 20 scanlines of actual 3D rendering
- Show RGB565 values before/after JIT execution

**Finding**: ALL 21 traced scanlines showed `changed=0/N`
- JIT code executes (confirmed by EXECUTE logs)
- Zero pixels written to framebuffer
- Framebuffer values unchanged before/after JIT call

### Pipeline Mode Analysis

From JIT logs, the active rendering features are:
```
alphaMode=0x00005119
fbzMode=0x00000b61

Active features:
- alpha_test=1 (ENABLED) - function 4 (GREATERTHAN), ref=0
- alpha_blend=1 (ENABLED)
- dither=1 (ENABLED)
- depth_en=0 (DISABLED)
- fog_en=0 (DISABLED)
```

**Key Insight**: Fog and depth are NOT active in test scene, so bugs #1 and #3 were irrelevant.

### Critical Discovery #2: Original Code Has Same Corruption

Checked out original Phase 5+6 commit (57e5c6fe1) before ANY bug fixes:
- Rebuilt from scratch
- Tested with same scene
- **Result**: IDENTICAL corruption pattern

**Conclusion**: All 6 "bug fixes" were either:
1. Fixing things that weren't bugs
2. Fixing correct bugs but not the root cause
3. Fixing bugs in code paths not being executed

## Investigation by Custom Agents

### voodoo-debug (Agent ID: ac64e75)
- Found bug #4 (alpha blend shift 6→7)
- Verified many code sections as correct

### voodoo-debug (Agent ID: abafdc6)
- Investigated alpha blend + dither path
- Did extensive tracing but no conclusive bug found

### voodoo-arch (Agent ID: ac2cdab)
- Validated implementation against 3dfx specs
- No spec violations found

### voodoo-lead (Agent ID: a0b3177)
- Added periodic debug logging
- Confirmed JIT execution

### voodoo-lead (Agent ID: aa72136)
- Added pixel value tracing infrastructure
- Revealed zero pixels being written

### voodoo-debug (Agent ID: afc540d)
- Investigated why JIT writes zero pixels
- Checked alpha test, stipple, framebuffer pointer
- Found no conclusive bug (investigation incomplete)

## Current State

**Git Status**:
- Detached HEAD at 57e5c6fe1 (original Phase 5+6)
- Changes stashed on phase-5-fog-alpha-blend branch

**Known Facts**:
1. JIT code executes but writes **zero pixels** to framebuffer
2. Alpha test enabled (GREATERTHAN with ref=0)
3. Visual corruption exists on screen (something is rendering)
4. Original Phase 5+6 has same corruption as "fixed" versions
5. All bug fixes had zero visual impact

**Unknown**:
1. WHY does JIT write zero pixels?
2. WHAT is producing the visual output on screen?
3. Is there a fundamental architectural issue?

## Theories for Zero Pixel Writes

1. **Alpha test rejecting all pixels**
   - Alpha test function 4 (GREATERTHAN) with ref=0
   - Should pass for any alpha > 0
   - Input alpha `ia=1044480` should convert to 255
   - But something may corrupt alpha before test

2. **Framebuffer pointer wrong**
   - x8 (fb_mem) verified not clobbered
   - But may be initialized to wrong address

3. **Software fallback rendering**
   - JIT may be failing silently
   - Software renderer drawing instead
   - Would explain visual output despite zero JIT writes

4. **Loop never executing**
   - Loop structure verified correct
   - But some condition may prevent entry

5. **Stipple rejecting all pixels**
   - Ruled out: FBZ_STIPPLE not set in fbzMode

## Files Modified

- `src/include/86box/vid_voodoo_codegen_arm64.h` - All bug fixes applied here
- `src/video/vid_voodoo_render.c` - Pixel tracing added (uncommitted)

## Next Steps

### Immediate Priority

**Find why JIT writes zero pixels** - This is THE root cause. Everything else is noise.

Recommended approach:
1. **Dump actual JIT-generated ARM64 code**
   - Disassemble generated blocks at runtime
   - Verify instructions match what we think we emit
   - Check for encoding bugs or wrong opcodes

2. **Add runtime assertions**
   - Check fb_mem pointer is valid
   - Check alpha values before alpha test
   - Check loop bounds (x, x2)
   - Verify pixel coordinates are in range

3. **Test with software renderer disabled**
   - Force JIT-only rendering
   - See if screen goes black (confirming JIT writes nothing)
   - Or if corruption persists (JIT is writing but wrong values)

4. **Simplify test case**
   - Disable alpha test in fbzMode (force it off)
   - Disable alpha blend
   - Disable dither
   - Test each feature in isolation

5. **Check calling convention**
   - Verify state/params pointers are correct
   - Check if x/y coordinates are passed correctly
   - Validate register usage matches AAPCS64

### Long-term Investigation

- Compare generated ARM64 code byte-by-byte against x86-64 equivalent
- Profile with Instruments to see where time is spent
- Add comprehensive logging at every pipeline stage
- Consider if there's a macOS-specific issue (W^X, entitlements, etc.)

## Key Files

- **ARM64 codegen**: `src/include/86box/vid_voodoo_codegen_arm64.h` (3879 lines)
- **x86-64 reference**: `src/include/86box/vid_voodoo_codegen_x86-64.h` (3561 lines)
- **Voodoo renderer**: `src/video/vid_voodoo_render.c`
- **Planning docs**: `voodoo-arm64-port/plan.md`, `voodoo-arm64-port/checklist.md`
- **Debug findings**: `voodoo-arm64-port/debug-findings.md`

## Custom Agents

Use these agents via Task tool for Voodoo ARM64 work:
- `voodoo-lead` - Coordination, build, test
- `voodoo-texture` - Texture fetch (Phase 3)
- `voodoo-color` - Color/alpha combine (Phase 4)
- `voodoo-effects` - Fog, alpha test/blend, dither (Phase 5+6)
- `voodoo-debug` - Debugging, validation
- `voodoo-arch` - Architecture research, spec validation

## Commit History (Phase 5 branch)

```
83903f81b Fix color combine shift bug: SSHR → USHR (Bug #6)
6612c0ca7 Add periodic debug logging to Voodoo ARM64 JIT
0488894ff Fix alpha blend shift bug: LSL #6 → LSL #7
a5fe23515 Fix stack frame overflow: 128→144 bytes
45ae11f73 Update checklist: Phase 5+6 complete
57e5c6fe1 Phase 5+6: Complete Voodoo ARM64 pixel pipeline ← CURRENTLY HERE
```

## Test Configuration

**Default VM**: `~/Library/Application Support/86Box/Virtual Machines/Windows 98 Low End copy`

**Build Command**: `./scripts/build-and-sign.sh` or manual:
```bash
rm -rf build
cmake -S . -B build --preset regular \
  --toolchain ./cmake/llvm-macos-aarch64.cmake \
  -D NEW_DYNAREC=ON -D QT=ON \
  -D Qt5_ROOT=$(brew --prefix qt@5) \
  -D Qt5LinguistTools_ROOT=$(brew --prefix qt@5) \
  -D OpenAL_ROOT=$(brew --prefix openal-soft) \
  -D LIBSERIALPORT_ROOT=$(brew --prefix libserialport)
cmake --build build
codesign --force --deep --sign - \
  --entitlements src/mac/entitlements.plist \
  --options runtime build/src/86Box.app
```

## Summary

Six bugs were found and fixed in the Voodoo ARM64 JIT pipeline, but visual corruption persists unchanged. Critical finding: **JIT executes but writes zero pixels to framebuffer**. Testing the original Phase 5+6 code (before any fixes) shows identical corruption, confirming the bug fixes addressed wrong issues or non-root-cause bugs. The actual root cause preventing pixel writes remains unknown and is the critical blocker for Phase 5+6 completion.

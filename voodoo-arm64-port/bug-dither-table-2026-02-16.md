# Voodoo ARM64 JIT Bug: Wrong Dither Table Pointer

**Date**: 2026-02-16
**Phase**: 5+6 (Fog, Alpha Test, Alpha Blend, Dithering, Framebuffer Write)
**Severity**: HIGH - Causes visible horizontal striping corruption

## Symptom

Horizontal striping and color corruption visible in all rendered graphics (Doom menu, 3DMark, racing games). Pixels are written but with incorrect color values.

## Root Cause

**File**: `src/include/86box/vid_voodoo_codegen_arm64.h`
**Location**: Lines ~3614-3640 (dither value loading)

The ARM64 codegen was loading **ditherMatrix4x4_g** (green dither table) for ALL three color channels instead of using separate tables:
- `ditherMatrix4x4_r` for red
- `ditherMatrix4x4_g` for green
- `ditherMatrix4x4_b` for blue

**Buggy code** (~line 3614):
```c
ARM64_ADRP_ADD(11, "ditherMatrix4x4_g");  // WRONG - always green table
```

This caused incorrect dither values to be added to R and B channels, producing wrong RGB565 output with repeating horizontal patterns (dither pattern is 4x4, errors repeat every 4 pixels).

## Fix

Load correct dither table per channel before fetching dither values.

## False Lead: Endianness

Initial hypothesis was byte-order bug (RGB565 endianness). Applied REV16 byte swap which made corruption WORSE (total color chaos). Both x86-64 and ARM64 macOS are little-endian, so endianness is identical. The REV16 was reverted.

## Status

- Dither table loading fixed
- REV16 reverted
- Build successful
- **PENDING TEST** - awaiting full ARM64 codegen audit before testing

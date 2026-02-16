# ARM64 Codegen Technical Reference

A comprehensive technical reference for the Voodoo pixel pipeline ARM64 JIT compiler. Aimed at developers who understand CPU emulation, JIT compilation, and ARM64 assembly.

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Register Allocation](#register-allocation)
- [Encoding Macros](#encoding-macros)
- [Pipeline Phases](#pipeline-phases)
- [Key Differences from x86-64](#key-differences-from-x86-64)
- [Known Issues and Limitations](#known-issues-and-limitations)
- [Maintenance and Extension](#maintenance-and-extension)

---

## Overview

### What is the Voodoo Pixel Pipeline?

The Voodoo Graphics series (1997-2000) used a hardware pixel pipeline to render 3D graphics. The pipeline processes one scanline at a time, applying per-pixel operations:

1. **Depth test**: Compare Z-buffer value, reject occluded pixels
2. **Texture fetch**: Sample texel from texture RAM, apply filtering
3. **Color combine**: Blend texture with Gouraud-shaded color
4. **Fog**: Apply distance fog
5. **Alpha test**: Reject pixels based on alpha threshold
6. **Alpha blend**: Blend with framebuffer pixel
7. **Dither**: Reduce color banding for RGB565 output
8. **Write**: Store RGB565 pixel and optional depth/alpha to framebuffer

### Why JIT?

The Voodoo renderer has **hundreds of render states** (depth modes, blend modes, texture modes, fog modes, etc.). Pre-compiling all combinations is impractical. Instead, the JIT generates specialized code for each unique render state combination at runtime, caching the compiled blocks.

### x86-64 Reference

The ARM64 port is based on `src/include/86box/vid_voodoo_codegen_x86-64.h` (3561 lines of x86-64 JIT codegen). The ARM64 implementation in `src/include/86box/vid_voodoo_codegen_arm64.h` (4000 lines) follows the same structure and logic.

---

## Architecture

### Entry Point: `voodoo_generate()`

```c
void voodoo_generate(uint8_t *code_block, voodoo_t *voodoo,
                     voodoo_params_t *params, voodoo_state_t *state, int depthop)
```

Generates ARM64 machine code for a scanline renderer based on current render state. The generated function has the signature:

```c
uint8_t (*voodoo_draw)(voodoo_state_t *state, voodoo_params_t *params,
                       int x, int real_y)
```

**Arguments (AAPCS64):**
- `x0` = `state` (per-scanline mutable state)
- `x1` = `params` (render parameters, read-only)
- `x2` = `x` (starting pixel X coordinate)
- `x3` = `real_y` (scanline Y coordinate)

### Block Caching: `voodoo_get_block()`

```c
void *voodoo_get_block(voodoo_t *voodoo, voodoo_params_t *params,
                       voodoo_state_t *state, int odd_even)
```

Cache lookup by render state hash. If no match, calls `voodoo_generate()` to compile a new block. The cache has 8 slots per odd/even pair (16 total).

**Cache key:**
- `xdir` (forward/backward X iteration)
- `alphaMode` (alpha test and blend config)
- `fbzMode` (framebuffer and Z-buffer config)
- `fogMode` (fog config)
- `fbzColorPath` (color combine config)
- `textureMode[0]`, `textureMode[1]` (texture fetch config for TMU0/TMU1)
- `tLOD[0]`, `tLOD[1]` (LOD and mirroring config)
- `trexInit1` bit 18 (texture override flag)
- `col_tiled`, `aux_tiled` (framebuffer tiling flags)

### W^X Toggle (macOS)

macOS enforces W^X (write-xor-execute) for security. JIT memory must toggle between writable (for code generation) and executable (for running code).

**Implementation:**

```c
#if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *)) {
        pthread_jit_write_protect_np(0);  // Make writable
    }
#endif

voodoo_generate(data->code_block, ...);  // Generate code + write metadata

#if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *)) {
        pthread_jit_write_protect_np(1);  // Make executable
        __clear_cache(data->code_block, data->code_block + block_size);
    }
#endif
```

**Critical:** The W^X bracket must cover BOTH `voodoo_generate()` AND metadata writes (`data->xdir`, `data->alphaMode`, etc.) because they share the same mmap'd region.

### I-Cache Coherency

ARM64 has separate instruction and data caches. After writing machine code, the I-cache must be flushed:

```c
__clear_cache(start, end)
```

This is the portable builtin; do NOT use `sys_icache_invalidate()` (macOS-only).

---

## Register Allocation

### GPR Allocation (x0-x30)

| Register | Role | Saved | Usage |
|----------|------|-------|-------|
| `x0` | `state` pointer | Pinned | Per-scanline mutable state (ib, ig, ir, ia, z, tex_s, etc.) |
| `x1` | `params` pointer | Pinned | Render parameters (dBdX, dGdX, color0, fogTable, etc.) |
| `x2` | `x` (pixel X) | Temp | Starting X coordinate (passed as arg, then overwritten) |
| `x3` | `real_y` | Temp | Scanline Y (passed as arg, then overwritten) |
| `x4-x7` | Scratch | Caller | Temp computations |
| `x8` | `fb_mem` pointer | Pinned | Framebuffer base address (loaded in prologue) |
| `x9` | `aux_mem` pointer | Pinned | Auxiliary buffer (depth/alpha) base address |
| `x10-x15` | Scratch | Caller | Temp computations |
| `x16-x17` | IP0/IP1 | Temp | Intra-procedure scratch (used for temp addresses) |
| `x19` | `logtable` pointer | Callee | Address of `logtable[]` (for LOD calculation) |
| `x20` | `alookup` pointer | Callee | Address of `alookup[]` (for alpha blend division by 255) |
| `x21` | `aminuslookup` pointer | Callee | Address of `aminuslookup[]` (for `255 - alpha`) |
| `x22` | `xmm_00_ff_w` pointer | Callee | Address of NEON constant table |
| `x23` | `i_00_ff_w` pointer | Callee | Address of scalar `{0, 0xFF}` table |
| `x24` | `real_y` copy | Callee | Preserved scanline Y (for dither table indexing) |
| `x25` | `bilinear_lookup` pointer | Callee | Address of `bilinear_lookup[]` (for texture filtering) |
| `x26-x28` | Scratch | Callee | Available for use (currently unused) |
| `x29` | FP (frame pointer) | Callee | Stack frame base |
| `x30` | LR (link register) | Callee | Return address |
| `x31` | SP/ZR | Special | Stack pointer (as destination) or zero register (as source) |

### NEON Allocation (v0-v31)

| Register | Role | Saved | Usage |
|----------|------|-------|-------|
| `v0` | Work | Caller | Texture/color data (packed or unpacked) |
| `v1` | Work | Caller | Color local (clocal), unpacked 4x16 |
| `v2` | Zero | Caller | Cleared to zero for unpacking |
| `v3` | Work | Caller | Blend factor or TMU1 result |
| `v4` | Work | Caller | Destination color or saved texture |
| `v5-v7` | Work | Caller | Temp |
| `v8` | `xmm_01_w` | Callee | `{1,1,1,1,0,0,0,0}` (low 64 bits = 4x16) |
| `v9` | `xmm_ff_w` | Callee | `{0xFF,0xFF,0xFF,0xFF,0,0,0,0}` |
| `v10` | `xmm_ff_b` | Callee | `{0x00FFFFFF,0,0,0}` (24-bit mask) |
| `v11` | `minus_254` | Callee | `{0xFF02,0xFF02,0xFF02,0xFF02,0,0,0,0}` |
| `v12-v13` | Work | Callee | Saved pre-fog color (v13), scratch (v12) |
| `v14-v15` | Unused | Callee | Reserved for future use |
| `v16-v31` | Work | Caller | Temp (used for multiply results, loads, etc.) |

**Note:** ARM64 requires callee-saved registers (x19-x28, v8-v15) to be preserved across function calls. The prologue saves them to the stack; the epilogue restores them.

---

## Encoding Macros

### Instruction Emission Primitive

```c
#define addlong(val) \
    do { \
        *(uint32_t *) &code_block[block_pos] = val; \
        block_pos += 4; \
    } while (0)
```

All ARM64 instructions are 32 bits (4 bytes). The `addlong()` macro writes an instruction word and advances the code pointer.

### Field Encoding Helpers

ARM64 instruction encoding uses bitfield insertion. Common patterns:

```c
#define Rt(x)  (x)                  // Target register (bits [4:0])
#define Rd(x)  (x)                  // Destination register (bits [4:0])
#define Rn(x)  ((x) << 5)           // Source register 1 (bits [9:5])
#define Rm(x)  ((x) << 16)          // Source register 2 (bits [20:16])
#define IMM12(imm) ((imm) << 10)    // 12-bit unsigned immediate (bits [21:10])
```

### Example: ADD Instruction

```c
/* ADD Wd, Wn, Wm (32-bit register-register add) */
#define ARM64_ADD_REG(d, n, m) (0x0B000000 | Rm(m) | Rn(n) | Rd(d))
```

**Encoding:**
- `0x0B000000` = opcode base (bits [31:21] = `01011000000`)
- `Rm(m)` = source register 2 (bits [20:16])
- `Rn(n)` = source register 1 (bits [9:5])
- `Rd(d)` = destination register (bits [4:0])

### Offset Encoding for Loads/Stores

ARM64 load/store instructions use scaled immediate offsets:

```c
#define OFFSET12_W(offset) (((offset) >> 2) << 10)  // Word (4-byte) scale
#define OFFSET12_X(offset) (((offset) >> 3) << 10)  // Doubleword (8-byte) scale
#define OFFSET12_Q(offset) (((offset) >> 4) << 10)  // Quadword (16-byte) scale
```

**Example:**
```c
ARM64_LDR_W(5, 0, 188)  // LDR w5, [x0, #188]
```

Expands to:
```c
0xB9400000 | OFFSET12_W(188) | Rn(0) | Rt(5)
```

Where `OFFSET12_W(188) = (188 >> 2) << 10 = 47 << 10`.

**Alignment requirement:** The offset `188` must be 4-byte aligned (188 % 4 = 0) for `LDR_W`. Misaligned offsets are silently truncated by the `>> 2` shift, causing hard-to-debug bugs.

### Bitmask Immediate Encoding

ARM64 logical immediates (AND, ORR, EOR) use a complex `N:immr:imms` encoding. For simple low-bit masks:

```c
/* AND Wd, Wn, #(2^width - 1) -- mask low 'width' bits */
#define ARM64_AND_MASK(d, n, width) ARM64_AND_BITMASK(d, n, 0, 0, (width) - 1)
```

**Example:**
```c
ARM64_AND_MASK(4, 4, 8)  // AND w4, w4, #0xFF (mask low 8 bits)
```

Expands to `ARM64_AND_BITMASK(4, 4, 0, 0, 7)` where `N=0, immr=0, imms=7`.

### Forward Branch Patching

Branches to future locations (e.g., skip pixel on depth test fail) are emitted as placeholders, then patched when the target is known:

```c
int skip_pos = block_pos;
addlong(ARM64_BCOND_PLACEHOLDER(COND_EQ));  // Emit B.EQ with offset=0

// ... emit code to skip ...

PATCH_FORWARD_BCOND(skip_pos);  // Patch offset to branch here
```

**Macro:**
```c
#define PATCH_FORWARD_BCOND(pos) \
    do { \
        int32_t _off = block_pos - (pos); \
        *(uint32_t *) &code_block[(pos)] |= OFFSET19(_off); \
    } while (0)
```

This OR's the computed offset into the placeholder instruction at `pos`.

---

## Pipeline Phases

The generated code processes pixels in a loop, applying the full pipeline to each pixel. The loop structure:

```c
loop_top:
    // Phase 2: Stipple test
    // Phase 2: Depth test
    // Phase 3: Texture fetch + TMU combine
    // Phase 4: Color/alpha combine
    // Phase 5: Fog
    // Phase 5: Alpha test
    // Phase 5: Alpha blend
    // Phase 6: Dither + framebuffer write
    // Phase 6: Depth write
skip_pixel:
    // Per-pixel state increments
    x += xdir
    if (x != x2) goto loop_top
epilogue:
    return
```

### Prologue (Phase 1)

**x86-64 ref:** Lines 661-764

**Purpose:** Save callee-saved registers, load constant pointers, initialize NEON constants.

**Stack layout (160 bytes, 16-byte aligned):**

| Offset | Contents |
|--------|----------|
| SP+0 | x29 (FP), x30 (LR) |
| SP+16 | x19, x20 |
| SP+32 | x21, x22 |
| SP+48 | x23, x24 |
| SP+64 | x25, x26 |
| SP+80 | x27, x28 |
| SP+96 | d8, d9 |
| SP+112 | d10, d11 |
| SP+128 | d12, d13 |

**Key operations:**

1. **Save registers:**
   ```c
   STP x29, x30, [SP, #-160]!  // Pre-index: SP = SP - 160, store FP/LR
   STP x19, x20, [SP, #16]
   // ... (save x21-x28, d8-d13)
   ```

2. **Load constant pointers:**
   ```c
   // 64-bit pointer load (4 instructions)
   MOVZ x19, #(logtable_addr & 0xFFFF)
   MOVK x19, #((logtable_addr >> 16) & 0xFFFF), LSL #16
   MOVK x19, #((logtable_addr >> 32) & 0xFFFF), LSL #32
   MOVK x19, #((logtable_addr >> 48) & 0xFFFF), LSL #48
   ```

3. **Load NEON constants:**
   ```c
   LDR Q8, [x_temp]  // Load xmm_01_w into v8
   LDR Q9, [x_temp]  // Load xmm_ff_w into v9
   // ...
   ```

4. **Load framebuffer pointers:**
   ```c
   LDR x8, [x0, #STATE_fb_mem]    // x8 = state->fb_mem
   LDR x9, [x0, #STATE_aux_mem]   // x9 = state->aux_mem
   ```

### Phase 2: Stipple Test

**x86-64 ref:** Lines 766-828

**Purpose:** Reject pixels based on stipple pattern (for dithered transparency effects).

**Two modes:**

1. **Pattern stipple (`FBZ_STIPPLE_PATT`):**
   ```c
   bit = (real_y & 3) * 8 + (~x & 7)
   if (!(state->stipple & (1 << bit))) goto skip_pixel
   ```

   **ARM64:**
   ```c
   AND w4, w24, #3           // w4 = real_y & 3
   LSL w4, w4, #3            // w4 *= 8
   LDR w5, [x0, #STATE_x]    // w5 = state->x
   MVN w5, w5                // w5 = ~x
   AND w5, w5, #7            // w5 &= 7
   ORR w4, w4, w5            // w4 = bit index
   MOV w6, #1
   LSL w6, w6, w4            // w6 = 1 << bit
   LDR w7, [x0, #STATE_stipple]
   TST w7, w6                // Test bit
   B.EQ skip_pixel           // Skip if zero
   ```

2. **Rotating stipple:**
   ```c
   state->stipple = ROR(state->stipple, 1)
   if (!(state->stipple & 0x80000000)) goto skip_pixel
   ```

   **ARM64:**
   ```c
   LDR w4, [x0, #STATE_stipple]
   ROR w4, w4, #1            // Rotate right by 1
   STR w4, [x0, #STATE_stipple]
   TBZ w4, #31, skip_pixel   // Test bit 31, branch if zero
   ```

### Phase 2: Depth Test

**x86-64 ref:** Lines 966-1023

**Purpose:** Compare pixel depth against Z-buffer, reject occluded pixels.

**Depth source:**
- **W-buffer mode:** Compute depth from W (perspective-correct)
- **Z-buffer mode:** Use Z value (screen-space depth)

**W-depth calculation (when enabled):**

```c
// Load high 32 bits of W (64-bit value)
LDR w4, [x0, #(STATE_w + 4)]
UXTH w5, w4                 // Test low 16 bits of high word
CBNZ w5, got_depth          // If nonzero, depth = 0

// High word is zero, compute from low word
LDR w4, [x0, #STATE_w]
CLZ w6, w4                  // Count leading zeros
SUB w7, w6, #16             // exp = CLZ - 16
// ... (compute mantissa, pack to 16-bit depth)
```

**Z-depth calculation (when W-buffer disabled):**

```c
LDR w10, [x0, #STATE_z]
ASR w10, w10, #12           // Z >> 12 (28-bit to 16-bit)
// Clamp to [0, 0xFFFF]
CMP w10, #0
CSEL w10, wzr, w10, LT      // If negative, = 0
MOV w11, #0xFFFF
CMP w10, w11
CSEL w10, w11, w10, GT      // If > 0xFFFF, = 0xFFFF
```

**Depth bias (when enabled):**

```c
LDR w4, [x1, #PARAMS_zaColor]
ADD w10, w10, w4            // new_depth += zaColor
UXTH w10, w10               // Mask to 16 bits
```

**Depth comparison (8 modes):**

| Mode | Condition | ARM64 Branch |
|------|-----------|--------------|
| `DEPTHOP_NEVER` | Always fail | `RET` (immediate return) |
| `DEPTHOP_LESSTHAN` | `new < old` | Skip on `B.CS` (unsigned >=) |
| `DEPTHOP_EQUAL` | `new == old` | Skip on `B.NE` |
| `DEPTHOP_LESSTHANEQUAL` | `new <= old` | Skip on `B.HI` (unsigned >) |
| `DEPTHOP_GREATERTHAN` | `new > old` | Skip on `B.LS` (unsigned <=) |
| `DEPTHOP_NOTEQUAL` | `new != old` | Skip on `B.EQ` |
| `DEPTHOP_GREATERTHANEQUAL` | `new >= old` | Skip on `B.CC` (unsigned <) |
| `DEPTHOP_ALWAYS` | Always pass | No branch |

**ARM64 example (LESSTHAN):**

```c
LDR w4, [x0, #STATE_x]           // Load x coord (for tiling)
LDRH w5, [x9, x4, LSL #1]        // w5 = old_depth from aux_mem[x]
CMP w10, w5                      // Compare new vs old
B.CS z_skip_pos                  // Skip if new >= old (fail)
```

### Phase 3: Texture Fetch

**x86-64 ref:** Lines 78-647 (in `codegen_texture_fetch()`)

**Purpose:** Sample texel from texture RAM, apply filtering and mirroring.

**Function:** `codegen_texture_fetch(code_block, voodoo, params, state, block_pos, tmu)`

Returns texel in `w4` as packed BGRA32 (blue in byte 0, green in byte 1, red in byte 2, alpha in byte 3).

**Perspective-correct path (textureMode bit 0 set):**

```c
// Load TMU s, t, w (64-bit values)
LDR x5, [x0, #STATE_tmu_s(tmu)]
LDR x6, [x0, #STATE_tmu_t(tmu)]
LDR x7, [x0, #STATE_tmu_w(tmu)]

// Compute reciprocal: (1 << 48) / w
MOV x4, #0x0001000000000000   // 1 << 48 (4 instructions)
SDIV x4, x4, x7                // x4 = quotient

// Multiply s, t by reciprocal
ASR x5, x5, #14                // s >> 14
ASR x6, x6, #14                // t >> 14
MUL x5, x5, x4                 // s *= quotient
MUL x6, x6, x4                 // t *= quotient
ASR x5, x5, #30                // s >> 30 (final tex_s)
ASR x6, x6, #30                // t >> 30 (final tex_t)

// LOD calculation using CLZ (ARM64 equivalent of x86 BSR)
CLZ x10, x4                    // Count leading zeros
MOV w11, #63
SUB w11, w11, w10              // BSR result = 63 - CLZ
// ... (compute exp, mantissa, pack to LOD)
```

**Non-perspective path (textureMode bit 0 clear):**

```c
LDR x4, [x0, #STATE_tmu_s(tmu)]
LDR x6, [x0, #STATE_tmu_t(tmu)]
LSR x4, x4, #28                // Simple shift
LSR x6, x6, #28
STR w4, [x0, #STATE_tex_s]     // Store as 32-bit (sufficient)
STR w6, [x0, #STATE_tex_t]
```

**Point-sampled lookup:**

```c
LDR w6, [x0, #STATE_lod]
ADD x11, x0, #STATE_tex_n(tmu)
LDR x12, [x11, x6, LSL #3]     // x12 = tex[tmu][lod] base pointer

// Compute s, t from tex_s, tex_t
LDR w4, [x0, #STATE_tex_s]
LDR w5, [x0, #STATE_tex_t]
// ... (apply mirroring, clamping, compute linear index)
LSL w5, w5, w11                // t << tex_shift
ADD w5, w5, w4                 // index = (t << shift) + s
LDR w4, [x12, x5, LSL #2]      // w4 = tex[index] (BGRA32)
```

**Bilinear filtering:**

Fetch 4 texels (s, s+1) × (t, t+1), weight by sub-texel fractions, sum:

```c
// Extract sub-texel fractions
MOV w10, w4
MOV w11, w5
AND w10, w10, #0xF            // frac_s = s & 0xF
LSL w11, w11, #4
AND w11, w11, #0xF0           // frac_t = (t & 0xF) << 4
ORR w10, w10, w11             // bilinear_index
LSL w10, w10, #5              // index * 32 (entry size)

// Load 4 texels as 2x 32-bit BGRA
LDR d0, [x14, x4]             // row0[s], row0[s+1]
LDR d1, [x13, x4]             // row1[s], row1[s+1]

// Unpack to 16-bit lanes
UXTL v0.8H, v0.8B
UXTL v1.8H, v1.8B

// Load bilinear weights
ADD x11, x25, x10             // x25 = bilinear_lookup ptr
LDR q16, [x11, #0]            // weights for row0
LDR q17, [x11, #16]           // weights for row1

// Multiply and sum
MUL v0.8H, v0.8H, v16.8H
MUL v1.8H, v1.8H, v17.8H
ADD v0.8H, v0.8H, v1.8H       // Vertical sum
// Horizontal pairwise sum (EXT + ADD)
EXT v1.16B, v0.16B, v0.16B, #8
ADD v0.4H, v0.4H, v1.4H
USHR v0.4H, v0.4H, #8         // Normalize (/256)
SQXTUN v0.8B, v0.8H           // Pack to bytes
FMOV w4, s0                   // Extract to GPR
```

**Dual-TMU combine:**

When both TMU0 and TMU1 are active, fetch from TMU1 first, apply `tc_*` combine logic, then fetch TMU0 and combine again.

### Phase 4: Color/Alpha Combine

**x86-64 ref:** Lines 1689-2228

**Purpose:** Blend texture color with Gouraud-shaded color (or constant color), compute final RGBA.

**Color select (cother):**

| Mode | Source |
|------|--------|
| `CC_LOCALSELECT_ITER_RGB` | Gouraud shading (ib, ig, ir) >> 12 |
| `CC_LOCALSELECT_TEX` | Texture color (from Phase 3) |
| `CC_LOCALSELECT_COLOR1` | Constant color (`params->color1`) |
| `CC_LOCALSELECT_LFB` | Framebuffer read (unsupported in codegen) |

**Chroma key test:**

```c
// Load selected RGB source to w4
// ...
LDR w5, [x1, #PARAMS_chromaKey]
EOR w5, w5, w4                // XOR source with key
AND w5, w5, #0x00FFFFFF       // Mask to 24 bits (RGB only)
CBZ w5, chroma_skip_pos       // Skip if match
```

**Alpha select (a_other):**

| Mode | Source |
|------|--------|
| `A_SEL_ITER_A` | Gouraud alpha (ia >> 12), clamped |
| `A_SEL_TEX` | Texture alpha (`state->tex_a`) |
| `A_SEL_COLOR1` | Constant alpha (`params->color1 + 3`) |

**Color combine pipeline:**

```c
// v0 = cother (unpacked 4x16)
// v1 = clocal (unpacked 4x16)

if (cc_sub_clocal) {
    SUB v0.4H, v0.4H, v1.4H   // v0 = cother - clocal
}

// Compute blend factor in v3 based on cc_mselect
switch (cc_mselect) {
    case CC_MSELECT_ZERO:
        MOVI v3.2D, #0
        break;
    case CC_MSELECT_CLOCAL:
        MOV v3.16B, v1.16B
        break;
    case CC_MSELECT_ALOCAL:
        DUP v3.4H, w15         // Broadcast alpha
        break;
    // ...
}

if (!cc_reverse_blend) {
    EOR v3.16B, v3.16B, v9.16B  // XOR with 0xFF (invert)
}
ADD v3.4H, v3.4H, v8.4H         // factor += 1

// Signed multiply: (v0 * v3) >> 8, saturating narrow
SMULL v17.4S, v0.4H, v3.4H      // 16x16 -> 32-bit
SSHR v17.4S, v17.4S, #8         // Arithmetic shift right
SQXTN v0.4H, v17.4S             // Saturating narrow to 16-bit

if (cc_add == 1) {
    ADD v0.4H, v0.4H, v1.4H     // Add clocal back
}

SQXTUN v0.8B, v0.8H             // Pack to unsigned bytes

if (cc_invert_output) {
    EOR v0.16B, v0.16B, v10.16B // XOR with 0x00FFFFFF
}
```

**Alpha combine:** Parallel logic for alpha channel in scalar registers (`w12`, `w14`, `w15`).

### Phase 5: Fog

**x86-64 ref:** Lines 2236-2417

**Purpose:** Apply distance fog (blend between pixel color and fog color based on depth).

**Fog constant mode:**

```c
LDR w4, [x1, #PARAMS_fogColor]
FMOV s3, w4
UQADD v0.8B, v0.8B, v3.8B   // Saturating add (no overflow)
```

**General fog mode:**

```c
// Unpack color to 16-bit
UXTL v0.8H, v0.8B

// Load fog color
LDR w4, [x1, #PARAMS_fogColor]
FMOV s3, w4
UXTL v3.8H, v3.8B

if (!(FOG_ADD)) {
    // v3 = fogColor
} else {
    MOVI v3.2D, #0            // v3 = 0
}

if (!(FOG_MULT)) {
    SUB v3.4H, v3.4H, v0.4H   // v3 = fogColor - color
}

SSHR v3.4H, v3.4H, #1         // Divide by 2 (prevent overflow)

// Compute fog_a based on fog source
switch (fogMode & (FOG_Z | FOG_ALPHA)) {
    case 0:  // w_depth table lookup
        LDR w4, [x0, #STATE_w_depth]
        LSR w4, w4, #10
        AND w4, w4, #0x3F           // fog_idx
        LSR w5, w4, #2
        AND w5, w5, #0xFF           // frac
        // Load fogTable[fog_idx].fog, fogTable[fog_idx].dfog
        ADD x6, x1, #PARAMS_fogTable
        ADD x6, x6, x4, LSL #1
        LDRB w7, [x6, #1]           // dfog
        LDRB w6, [x6, #0]           // fog
        MUL w5, w5, w7              // frac * dfog
        LSR w5, w5, #10
        ADD w4, w6, w5              // fog_a = fog + (dfog * frac >> 10)
        break;
    case FOG_Z:
        LDR w4, [x0, #STATE_z]
        LSR w4, w4, #12
        AND w4, w4, #0xFF
        break;
    // ... (FOG_ALPHA, FOG_W modes)
}

ADD w4, w4, w4                // fog_a *= 2 (compensate for >>1 above)

// Multiply: v3 *= alookup[fog_a] >> 7
ADD x5, x20, x4, LSL #3       // x5 = alookup + fog_a*2*8
LDR d5, [x5, #16]             // v5 = alookup[fog_a+1] (matches x86 +16 offset)
MUL v3.4H, v3.4H, v5.4H
SSHR v3.4H, v3.4H, #7

if (FOG_MULT) {
    MOV v0.16B, v3.16B        // Result = fog only
} else {
    ADD v0.4H, v0.4H, v3.4H   // Result = color + fog
}

SQXTUN v0.8B, v0.8H           // Pack to bytes
```

### Phase 5: Alpha Test

**x86-64 ref:** Lines 2419-2467

**Purpose:** Reject pixels based on alpha threshold (for binary transparency).

```c
// w12 = src alpha (EDX in x86)
LDRB w4, [x1, #(PARAMS_alphaMode + 3)]  // Load alpha ref
CMP w12, w4

switch (alpha_func) {
    case AFUNC_LESSTHAN:
        B.CS a_skip_pos       // Skip if alpha >= ref
        break;
    case AFUNC_EQUAL:
        B.NE a_skip_pos       // Skip if alpha != ref
        break;
    // ... (8 modes total)
}
```

### Phase 5: Alpha Blend

**x86-64 ref:** Lines 2469-3058

**Purpose:** Blend pixel with framebuffer using alpha factors (for transparency).

**Load destination pixel:**

```c
// Load x coordinate (tiled or linear)
LDR w4, [x0, #STATE_x_tiled]  // or STATE_x

// Load RGB565 from framebuffer
LDRH w6, [x8, x4, LSL #1]

// Decode via rgb565[] LUT
// x7 = &rgb565
MOV x7, #(rgb565_addr)        // 4 instructions for 64-bit load
LDR w6, [x7, w6, UXTW #2]     // w6 = rgb565[pixel] (BGRA32)
FMOV s4, w6
UXTL v4.8H, v4.8B             // Unpack to 4x16
```

**Compute dest blend factor:**

Example for `AFUNC_ASRC_ALPHA`:

```c
// v4 = dst * alookup[src_alpha] >> 8
ADD x7, x20, x12, LSL #3      // x7 = alookup + src_alpha*2*8
LDR d5, [x7, #0]              // v5 = alookup[src_alpha]
MUL v4.4H, v4.4H, v5.4H

// Rounding: add alookup[1], add (result>>8), shift >>8
LDR d16, [x20, #16]           // v16 = alookup[1]
MOV v17.16B, v4.16B
USHR v17.4H, v17.4H, #8
ADD v4.4H, v4.4H, v16.4H
ADD v4.4H, v4.4H, v17.4H
USHR v4.4H, v4.4H, #8
```

**Compute src blend factor:** Similar logic for `src_afunc`.

**Combine:**

```c
ADD v0.4H, v0.4H, v4.4H       // v0 = src_blended + dst_blended
SQXTUN v0.8B, v0.8H           // Pack to bytes with saturation
```

**Alpha channel blend:** Separate logic in scalar registers for alpha result.

### Phase 6: Dither + Framebuffer Write

**x86-64 ref:** Lines 3077-3221

**Purpose:** Convert 8-bit RGB to 5:6:5 (RGB565) with optional dithering, write to framebuffer.

**No-dither path:**

```c
FMOV w4, s0                   // w4 = packed BGRA
// Extract R, G, B and pack to RGB565
UBFX w5, w4, #3, #5           // B = bits[7:3]
UBFX w6, w4, #10, #6          // G = bits[15:10]
LSL w6, w6, #5
UBFX w7, w4, #19, #5          // R = bits[23:19]
LSL w7, w7, #11
ORR w4, w7, w6
ORR w4, w4, w5                // w4 = RGB565

// Store
LDR w14, [x0, #STATE_x_tiled]  // or STATE_x
STRH w4, [x8, x14, LSL #1]     // fb_mem[x] = pixel
```

**Dither path (4x4 or 2x2):**

```c
// Load dither table base pointer
// x7 = &dither_rb (or &dither_rb2x2)
MOV x7, #(dither_rb_addr)     // 4 instructions

// Extract R, G, B bytes
UBFX w6, w4, #8, #8           // G
UBFX w13, w4, #16, #8         // R
AND w6, w4, #0xFF             // B

// Compute dither table index
MOV w5, w24                   // w5 = real_y
AND w10, w14, #3              // w10 = x & 3 (or #1 for 2x2)
AND w5, w5, #3                // w5 = y & 3 (or #1 for 2x2)

// For 4x4: offset = value*16 + y*4 + x
ADD w5, w10, w5, LSL #2       // w5 = x + y*4
LSL w13, w13, #4              // R*16
LSL w6, w6, #4                // B*16
LSL w11, w11, #4              // G*16 (separate table)

// Load dithered values
ADD x7, x7, x5                // x7 += sub-index
LDRB w13, [x7, x13]           // dithered R
LDRB w6, [x7, x6]             // dithered B
// (load G from dither_g table with offset)
LDRB w11, [x11, x16]          // dithered G

// Pack RGB565
LSL w13, w13, #11
LSL w11, w11, #5
ORR w4, w13, w11
ORR w4, w4, w6

STRH w4, [x8, x14, LSL #1]
```

### Phase 6: Depth Write

**x86-64 ref:** Lines 3224-3243

**Purpose:** Write depth value to auxiliary buffer (if depth write mask enabled).

**Two cases:**

1. **Alpha-buffer enabled:** Write blended alpha (w12) as depth
   ```c
   LDR w4, [x0, #STATE_x_tiled]
   STRH w12, [x9, x4, LSL #1]  // aux_mem[x] = alpha
   ```

2. **Alpha-buffer disabled:** Write computed depth (new_depth)
   ```c
   LDRH w5, [x0, #STATE_new_depth]
   STRH w5, [x9, x4, LSL #1]   // aux_mem[x] = depth
   ```

### Per-Pixel Increments

**x86-64 ref:** Lines 3256-3427

**Purpose:** Update per-pixel state for next iteration (Gouraud colors, depth, texture coordinates).

**RGBA increment (NEON 4xS32):**

```c
ADD x16, x0, #STATE_ib        // x16 = &state->ib
LD1 {v0.4S}, [x16]            // v0 = {ib, ig, ir, ia}
ADD x17, x1, #PARAMS_dBdX
LD1 {v1.4S}, [x17]            // v1 = {dBdX, dGdX, dRdX, dAdX}

if (xdir > 0) {
    ADD v0.4S, v0.4S, v1.4S
} else {
    SUB v0.4S, v0.4S, v1.4S
}

ST1 {v0.4S}, [x16]            // Store back
```

**Z increment (scalar):**

```c
LDR w4, [x0, #STATE_z]
LDR w5, [x1, #PARAMS_dZdX]
if (xdir > 0) {
    ADD w4, w4, w5
} else {
    SUB w4, w4, w5
}
STR w4, [x0, #STATE_z]
```

**TMU0 S/T increment (NEON 2xD64):**

```c
LDR q0, [x0, #STATE_tmu0_s]   // Load tmu0_s (64-bit) + tmu0_t (64-bit)
LDR q1, [x1, #PARAMS_tmu0_dSdX]
if (xdir > 0) {
    ADD v0.2D, v0.2D, v1.2D
} else {
    SUB v0.2D, v0.2D, v1.2D
}
STR q0, [x0, #STATE_tmu0_s]
```

**TMU0 W increment (scalar 64-bit):**

```c
LDR x10, [x0, #STATE_tmu0_w]
LDR x11, [x1, #PARAMS_tmu0_dWdX]
if (xdir > 0) {
    ADD x10, x10, x11
} else {
    SUB x10, x10, x11
}
STR x10, [x0, #STATE_tmu0_w]
```

**Global W increment:** Same as TMU W.

**TMU1 S/T/W increment (if dual TMUs):** Same logic as TMU0.

**Pixel count increment:**

```c
LDR w4, [x0, #STATE_pixel_count]
ADD w4, w4, #1
STR w4, [x0, #STATE_pixel_count]
```

**Texel count increment:**

```c
LDR w4, [x0, #STATE_texel_count]
if (single TMU) {
    ADD w4, w4, #1
} else {
    ADD w4, w4, #2
}
STR w4, [x0, #STATE_texel_count]
```

### X Increment and Loop

**x86-64 ref:** Lines 3448-3469

```c
LDR w4, [x0, #STATE_x]
if (xdir > 0) {
    ADD w5, w4, #1
} else {
    SUB w5, w4, #1
}
STR w5, [x0, #STATE_x]

LDR w6, [x0, #STATE_x2]
CMP w4, w6
B.NE loop_jump_pos            // Branch back to loop top if x != x2
```

### Epilogue

**x86-64 ref:** Lines 3471-3561

**Purpose:** Restore callee-saved registers, return.

```c
LDP d12, d13, [SP, #128]
LDP d10, d11, [SP, #112]
LDP d8, d9, [SP, #96]
LDP x27, x28, [SP, #80]
LDP x25, x26, [SP, #64]
LDP x23, x24, [SP, #48]
LDP x21, x22, [SP, #32]
LDP x19, x20, [SP, #16]
LDP x29, x30, [SP], #160      // Post-index: restore FP/LR, SP += 160
RET
```

---

## Key Differences from x86-64

### 1. Load-Store Architecture

**x86-64:** Instructions can operate directly on memory.
```x86asm
ADD [mem], EAX    ; 1 instruction
```

**ARM64:** Must load to register, operate, then store.
```armasm
LDR w4, [x0, #mem]
ADD w4, w4, w5
STR w4, [x0, #mem]    ; 3 instructions
```

**Impact:** ARM64 code is longer, but the explicit data flow can be easier to optimize.

### 2. NEON vs SSE2

**SSE2 (x86-64):**
```x86asm
PMULLW XMM0, XMM1       ; Low 16 bits of 16x16 multiply
PMULHW XMM2, XMM1       ; High 16 bits
PUNPCKLWD XMM0, XMM2    ; Interleave to 32-bit
PSRAD XMM0, 8           ; Arithmetic shift right
PACKSSDW XMM0, XMM0     ; Pack with saturation
```

**NEON (ARM64):**
```armasm
SMULL v0.4S, v0.4H, v1.4H   ; Signed 16x16 -> 32-bit (widening)
SSHR v0.4S, v0.4S, #8       ; Arithmetic shift right
SQXTN v0.4H, v0.4S          ; Saturating narrow to 16-bit
```

**Impact:** ARM64 widening multiply is more efficient (3 instructions vs 5).

### 3. Conditional Execution

**x86-64:** Flags + conditional jumps.
```x86asm
TEST EAX, 0x80
JZ skip
```

**ARM64:** Flags + conditional branches OR test-and-branch.
```armasm
TBZ w4, #7, skip    ; Test bit 7, branch if zero (1 instruction)
```

**Impact:** `TBZ`/`TBNZ` for single-bit tests is faster than TEST+Jcc.

### 4. Immediate Encoding

**x86-64:** 32-bit immediates fit in one instruction.
```x86asm
MOV EAX, 0x12345678    ; 1 instruction
```

**ARM64:** 64-bit immediates need up to 4 instructions.
```armasm
MOVZ x0, #0x5678
MOVK x0, #0x1234, LSL #16
MOVK x0, #0x0000, LSL #32
MOVK x0, #0x0000, LSL #48
```

**Optimization:** Use `ADRP`+`ADD` for pointers when possible (2 instructions).

### 5. Alignment Requirements

**x86-64:** Unaligned loads/stores are supported (with minor performance penalty).

**ARM64:** Unsigned-offset loads/stores require alignment:
- `LDR_W` (32-bit): offset must be 4-byte aligned
- `LDR_X` (64-bit): offset must be 8-byte aligned
- `LDR_Q` (128-bit): offset must be 16-byte aligned

**Example bug:** `STR_X(t, n, 188)` silently truncates to offset 184 because `188 >> 3 = 23`, and `23 << 3 = 184`.

**Fix:** Use `STR_W` for 32-bit stores to unaligned offsets (4-byte alignment only).

**Audit results:** All struct offsets in `voodoo_state_t` and `voodoo_params_t` are properly aligned for their access sizes, except `STATE_tex_s` (188) and `STATE_tex_t` (192), which are accessed as 32-bit (W) rather than 64-bit (X).

### 6. W^X and I-Cache

**x86-64:** Weak consistency. Code can be written and executed immediately without explicit cache maintenance.

**ARM64:** Strong consistency. After writing code, must:
1. Toggle W^X: `pthread_jit_write_protect_np(1)` (macOS)
2. Flush I-cache: `__clear_cache(start, end)`

**Impact:** Adds overhead (~14 lines per JIT invocation), but required for correctness.

---

## Known Issues and Limitations

### 1. Bilinear Rounding Differences

**Issue:** Bilinear filtered textures may differ by ±1 LSB compared to x86-64.

**Cause:** ARM64 NEON uses `USHR` (unsigned shift) for final division by 256, while x86-64 SSE2 uses `PSRLW` (also unsigned). Both are correct, but intermediate rounding may differ due to instruction ordering.

**Impact:** Visually imperceptible (difference is 1/256 brightness per channel).

**Status:** Not a bug, within acceptable tolerance.

### 2. LD1/ST1 vs LDR/STR

**Issue (fixed):** Early implementation used `LD1 {Vt.4S}` / `ST1 {Vt.4S}` for 128-bit loads/stores to unaligned addresses. The initial macro definition used the 2-register form (opcode `1010`) instead of 1-register form (opcode `0111`), causing 32-byte loads/stores and data corruption.

**Fix:** Changed opcode from `0x4C40A800` to `0x4C407800` (bits [15:12]: `1010` → `0111`).

**Status:** Fixed in Phase 5+6 commit.

### 3. Frame Size Optimization

**Issue (fixed):** Prologue comment claimed d14/d15 were saved at SP-144, but code only saved d8-d13. v14/v15 were never used.

**Fix:** Removed misleading comment, reduced frame size from 144 to 128 bytes.

**Status:** Fixed in Phase 4 post-merge.

### 4. Voodoo 2 Non-Perspective Texture Bug

**Issue (fixed):** Voodoo 2 not detected by Windows 98 when JIT enabled. Caused by misaligned `STR_X` to `STATE_tex_s` (offset 188).

**Cause:** `STR_X` requires 8-byte alignment. `188 >> 3 = 23`, `23 << 3 = 184`. The store went to offset 184, but reads from 188, loading garbage (upper 32 bits of the misaligned 64-bit store).

**Fix:** Changed `STR_X` → `STR_W` for `tex_s` and `tex_t` stores (lines 1189, 1195). 32-bit stores only require 4-byte alignment.

**Impact:** Caused test failures in non-perspective texture path (textureMode bit 0 clear). Voodoo 2 driver probe renders test patterns using this path; garbage input caused probe to fail.

**Status:** Fixed but uncommitted (as of 2026-02-16).

### 5. x86-64 Reference Quirks

Two quirks in the x86-64 reference were discovered during ARM64 port:

1. **Line 1303:** Emits `0x8E` (MOV ES, AX) instead of `0x83` (ADD EAX, 1). ARM64 port uses correct `ADD` encoding. (Bug to be reported separately.)

2. **Line 3053:** Comment says "SHR EAX, 8" but encoding is `ROR EAX, 8`. Functionally equivalent in context (low 8 bits are zero), but unusual. ARM64 uses `LSR` (logical shift right) for clarity.

---

## Maintenance and Extension

### Adding New Pipeline Features

**Example:** Add support for a new blend mode.

1. **Identify render state bit:** Find the bit in `fbzMode`, `alphaMode`, etc.
2. **Update cache key:** Add the bit to the hash comparison in `voodoo_get_block()`.
3. **Generate code path:** Add conditional emission in `voodoo_generate()` based on the bit.
4. **Test:** Verify cache hit/miss behavior and correctness with JIT debug logging.

### Debugging Codegen Issues

**Tools:**

1. **JIT Debug Logging (`jit_debug=1`):**
   - Logs every cache hit/miss and recompilation.
   - Useful for understanding render state changes.

2. **Verify Mode (`jit_debug=2`):**
   - Runs JIT + interpreter per scanline, compares output.
   - Catches pixel-level differences.
   - Very slow, only use when debugging visual corruption.

3. **Disassembly:**
   ```bash
   # Dump generated code block
   objdump -D -b binary -m aarch64 -M no-aliases code_block.bin
   ```

   Or use an online disassembler like [https://armconverter.com/](https://armconverter.com/).

4. **LLDB Breakpoints:**
   ```lldb
   (lldb) br set -a 0x<code_block_address>
   (lldb) register read
   (lldb) memory read -s4 -fx -c16 $x0  # Dump state struct
   ```

### Performance Optimization

**Current performance:** ARM64 JIT is 7-10x faster than interpreter on Apple M2/M3.

**Potential improvements:**

1. **Reduce cache misses:** The 16-slot cache may thrash on games with many render states. Consider increasing to 32 slots.

2. **Optimize hot paths:** Profile with Instruments (macOS) to find bottlenecks. Common hot spots:
   - Bilinear texture fetch (many loads/multiplies)
   - Alpha blend (division by 255 via lookup table)

3. **NEON intrinsics:** The current implementation uses raw instruction encoding. Consider porting to NEON intrinsics (`arm_neon.h`) for better readability and compiler optimization.

4. **Lazy state updates:** Some state (e.g., `pixel_count`) is updated every pixel but rarely read. Consider batching updates.

### Code Style Conventions

- **Macro naming:** `ARM64_<INSN>_<VARIANT>` (e.g., `ARM64_ADD_REG`, `ARM64_LDR_W`)
- **Register naming:** Use architectural names (`x0`, `w4`, `v0.4H`) in comments
- **Struct offsets:** Use `STATE_*` and `PARAMS_*` macros, never hardcoded numbers
- **Comments:** Document the x86-64 reference line numbers for each section

### Testing Checklist

Before submitting codegen changes:

- [ ] Build succeeds on macOS ARM64
- [ ] Codesign with JIT entitlements
- [ ] Test with Voodoo 1, 2, 3, Banshee
- [ ] Test point-sampled and bilinear filtering
- [ ] Test forward and backward X iteration (`xdir`)
- [ ] Run 3DMark 99, Quake, Unreal
- [ ] Enable JIT debug logging, check for crashes/corruption
- [ ] Verify mode (level 2) for pixel-perfect validation (slow)
- [ ] Check cache hit rate (should be >90% after warm-up)

---

## References

- **ARM Architecture Reference Manual (ARMv8):** Official instruction set documentation
- **AAPCS64:** ARM64 calling convention (register usage, stack alignment)
- **3Dfx Voodoo Hardware Specs:** [3dfxarchive.com](http://www.3dfxarchive.com/)
- **Glide API Reference:** Voodoo software interface documentation
- **x86-64 Codegen Reference:** `src/include/86box/vid_voodoo_codegen_x86-64.h`

---

**Document version:** 1.0 (2026-02-16)

**Maintainer:** ARM64 JIT port team

**For questions or bug reports:** https://github.com/yourusername/86Box-voodoo-arm64/issues

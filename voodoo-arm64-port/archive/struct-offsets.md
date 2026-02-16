# Struct Offset Map — Voodoo JIT ARM64 Port

Computed on ARM64 (Apple Silicon, LP64: `int`=4, `long`=8, pointer=8).
Source structs: `voodoo_state_t` (vid_voodoo_render.c:41-93) and
`voodoo_params_t` (vid_voodoo_common.h:112-230).

Verified by compiling a standalone C utility on ARM64 (Apple Silicon)
and cross-checked by three independent audits against the x86-64 codegen.

---

## 1. voodoo_state_t (size=600, align=8)

The JIT function receives `voodoo_state_t *state` in **x0**. All offsets below
are from the start of the struct and serve as the immediate displacement in
`LDR/STR [x0, #offset]`.

### 1.1 Fields accessed by the x86-64 JIT

Only fields referenced via `offsetof()` in `vid_voodoo_codegen_x86-64.h` are
listed. Fields not accessed by the JIT are omitted.

| Offset | Size | Type | Field | JIT usage |
|-------:|-----:|------|-------|-----------|
| 56 | 4 | int | `tmu[0].lod` | TMU0 LOD level (nested in tmu array sub-struct) |
| 88 | 4 | int | `tmu[1].lod` | TMU1 LOD level (nested in tmu array sub-struct) |
| 104 | 4 | int | `lod` | Current LOD level; texture fetch LOD selection, detail combine |
| 108 | 4 | int | `lod_min[0]` | TMU0 min LOD clamp |
| 112 | 4 | int | `lod_min[1]` | TMU1 min LOD clamp |
| 116 | 4 | int | `lod_max[0]` | TMU0 max LOD clamp |
| 120 | 4 | int | `lod_max[1]` | TMU1 max LOD clamp |
| 156 | 4 | int | `tex_b[0]` | Fetched texel blue (TMU0); also accessed as `ib` block via `tex_b` base (see §5) |
| 180 | 4 | int | `tex_a[0]` | Fetched texel alpha (TMU0) |
| 184 | 4 | int | `tex_a[1]` | Fetched texel alpha (TMU1) |
| 188 | 4 | int | `tex_s` | Texture S coordinate (integer, post-divide) |
| 192 | 4 | int | `tex_t` | Texture T coordinate (integer, post-divide) |
| 240 | 8 | uint32_t* | `tex[0][0]` | TMU0 texture data pointer array (9 LOD levels) |
| 312 | 8 | uint32_t* | `tex[1][0]` | TMU1 texture data pointer array (9 LOD levels) |
| 456 | 8 | uint16_t* | `fb_mem` | Framebuffer write pointer |
| 464 | 8 | uint16_t* | `aux_mem` | Aux (depth/alpha) buffer pointer |
| 472 | 4 | int32_t | `ib` | Iterated blue; also base of {ib,ig,ir,ia} block (see §5) |
| 484 | 4 | int32_t | `ia` | Iterated alpha |
| 488 | 4 | int32_t | `z` | Iterated Z depth |
| 492 | 4 | int32_t | `new_depth` | Computed depth value for write |
| 496 | 8 | int64_t | `tmu0_s` | TMU0 S accumulator (48.16 fixed-point) |
| 504 | 8 | int64_t | `tmu0_t` | TMU0 T accumulator |
| 512 | 8 | int64_t | `tmu0_w` | TMU0 W accumulator |
| 520 | 8 | int64_t | `tmu1_s` | TMU1 S accumulator |
| 528 | 8 | int64_t | `tmu1_t` | TMU1 T accumulator |
| 536 | 8 | int64_t | `tmu1_w` | TMU1 W accumulator |
| 544 | 8 | int64_t | `w` | Global W accumulator |
| 552 | 4 | int | `pixel_count` | Pixel counter (incremented per pixel) |
| 556 | 4 | int | `texel_count` | Texel counter (incremented per pixel) |
| 560 | 4 | int | `x` | Current pixel X coordinate |
| 564 | 4 | int | `x2` | End X coordinate for loop termination |
| 568 | 4 | int | `x_tiled` | X coordinate in tiled layout |
| 572 | 4 | uint32_t | `w_depth` | W-based depth value |
| 580 | 4 | uint32_t | `ebp_store` | Scratch storage (x86 spill slot; repurpose on ARM64) |
| 588 | 4 | int | `lod_frac[0]` | TMU0 LOD fraction for trilinear |
| 592 | 4 | int | `lod_frac[1]` | TMU1 LOD fraction for trilinear |
| 596 | 4 | int | `stipple` | Rotating stipple pattern state |

### 1.2 Sub-word access patterns

The x86-64 JIT uses byte/word offsets into some fields:

| Expression | Computed offset | What it accesses | ARM64 approach |
|-----------|----------------:|------------------|----------------|
| `offsetof(state, w) + 4` | 548 | High 32 bits of `w` (int64_t) | `LDR W, [x0, #548]` or `LSR Xn, Xn, #32` after loading full 64-bit |

### 1.3 Contiguous field blocks

The x86-64 JIT accesses `tex_b` through `tex_a` as a contiguous block of
8 ints (32 bytes) starting at offset 156. It also accesses `ib` through `z`
as 5 contiguous int32_t values (20 bytes) starting at offset 472.

| Block | Start offset | Fields | Total bytes |
|-------|-------------:|--------|------------:|
| texel BGRA (TMU0+1) | 156 | tex_b[2], tex_g[2], tex_r[2], tex_a[2] | 32 |
| iterated BGRA+Z | 472 | ib, ig, ir, ia, z | 20 |
| TMU0 S/T/W | 496 | tmu0_s, tmu0_t, tmu0_w | 24 |
| TMU1 S/T/W | 520 | tmu1_s, tmu1_t, tmu1_w | 24 |

NEON opportunity: The texel BGRA block (8 × int32) can be loaded with
`LD1 {v0.4S, v1.4S}, [Xn]` for 32 bytes in one shot. The iterated BGRA
block (4 × int32) can use `LD1 {v0.4S}, [Xn]`.

---

## 2. voodoo_params_t (size=1208, align=8)

The JIT function receives `voodoo_params_t *params` in **x1**. All offsets
below are from the start of the struct.

### 2.1 Fields accessed by the x86-64 JIT

| Offset | Size | Type | Field | JIT usage |
|-------:|-----:|------|-------|-----------|
| 48 | 4 | int32_t | `dBdX` | Blue gradient per pixel (also base of dBdX..dZdX block) |
| 64 | 4 | int32_t | `dZdX` | Z gradient per pixel |
| 96 | 8 | int64_t | `dWdX` | W gradient per pixel |
| 144 | 8 | int64_t | `tmu[0].dSdX` | TMU0 S gradient per pixel |
| 160 | 8 | int64_t | `tmu[0].dWdX` | TMU0 W gradient per pixel |
| 240 | 8 | int64_t | `tmu[1].dSdX` | TMU1 S gradient per pixel |
| 256 | 8 | int64_t | `tmu[1].dWdX` | TMU1 W gradient per pixel |
| 304 | 4 | uint32_t | `color0` | Color register 0 (BGRA packed) |
| 308 | 4 | uint32_t | `color1` | Color register 1 (BGRA packed) |
| 456 | 4 | uint32_t | `alphaMode` | Alpha test/blend mode register |
| 460 | 4 | uint32_t | `zaColor` | Z/Alpha constant color |
| 476 | 4 | uint32_t | `chromaKey` | Chroma key comparison value |
| 324 | 4 | rgbvoodoo_t | `fogColor` | Fog color (BGRA, 4 bytes) |
| 328 | 2 | struct | `fogTable[0]` | Fog table entry 0 (fog + dfog, 2 bytes each entry) |
| 696 | 4 | int | `tex_w_mask[0][0]` | TMU0 texture width mask array (10 entries per LOD) |
| 736 | 4 | int | `tex_w_mask[1][0]` | TMU1 texture width mask array |
| 856 | 4 | int | `tex_h_mask[0][0]` | TMU0 texture height mask array |
| 896 | 4 | int | `tex_h_mask[1][0]` | TMU1 texture height mask array |
| 936 | 4 | int | `tex_shift[0][0]` | TMU0 texture shift array (disabled in x86; referenced in dead code) |

### 2.2 Sub-word access patterns

| Expression | Computed offset | What it accesses | ARM64 approach |
|-----------|----------------:|------------------|----------------|
| `offsetof(params, color0) + 3` | 307 | Alpha byte of color0 (byte 3 = bits 24-31) | `LDRB Wn, [x1, #307]` — **unaligned, but LDRB has no alignment requirement** |
| `offsetof(params, color1) + 3` | 311 | Alpha byte of color1 | `LDRB Wn, [x1, #311]` |
| `offsetof(params, alphaMode) + 3` | 459 | High byte of alphaMode (alpha reference value) | `LDRB Wn, [x1, #459]` |
| `offsetof(params, fogTable) + 1` | 329 | `dfog` field of fogTable[0] | `LDRB Wn, [x1, #329]` |
| `offsetof(params, fogTable[N])` | 328+2*N | `fog` field of fogTable[N] | `LDRB Wn, [x1, Xn]` with computed index |

Note: The x86-64 codegen accesses individual bytes by adding +3 to the
offsetof a uint32_t field. On little-endian ARM64, byte layout is identical
to x86-64 (byte 0 = LSB, byte 3 = MSB), so the same byte offsets work.
However, for cleaner ARM64 code, consider loading the full word and using
`UBFX` or `LSR` to extract the desired byte instead of byte loads.

### 2.3 The `-0x10` bilinear addressing pattern

The x86-64 JIT uses `offsetof(params, tex_w_mask[tmu]) - 0x10` in the
bilinear texture path (lines 597-630). This is an x86 addressing mode
optimization:

```
;; x86-64: CMP EAX, params->tex_w_mask[RSI+ECX*4]
;; where ECX = lod (0-8), RSI points to params
;; The -0x10 adjusts because the encoded offset uses ECX*4 but needs
;; to index from tex_w_mask[tmu][lod], and the tmu dimension adds 0x10
;; (= 4 * sizeof(int)) worth of stride for LOD_MAX+2 entries.
```

**ARM64 approach**: Don't replicate this hack. Instead:
1. Compute the effective address: `ADD Xtmp, x1, #offsetof(params, tex_w_mask[tmu])`
2. Load with register offset: `LDR Wn, [Xtmp, Xlod, LSL #2]`

### 2.4 Contiguous field blocks

| Block | Start offset | Fields | Total bytes |
|-------|-------------:|--------|------------:|
| dBdX..dZdX | 48 | dBdX, dGdX, dRdX, dAdX, dZdX | 20 |
| TMU0 dSdX/dTdX/dWdX | 144 | dSdX, dTdX, dWdX (with p1 pad at 136) | 24 (non-contiguous, p2 at 168) |
| TMU1 dSdX/dTdX/dWdX | 240 | dSdX, dTdX, dWdX (with p1 pad at 232) | 24 (non-contiguous, p2 at 264) |
| tex_w_mask[0] | 696 | 10 × int (LOD_MAX+2) | 40 |
| tex_w_mask[1] | 736 | 10 × int | 40 |
| tex_h_mask[0] | 856 | 10 × int | 40 |
| tex_h_mask[1] | 896 | 10 × int | 40 |

Note: The TMU sub-struct has explicit padding fields (p1, p2, p3) that break
contiguity between startW/dSdX and dWdX/dSdY. The dSdX and dWdX fields
within each TMU are at offsets +32 and +48 from the TMU sub-struct start —
not adjacent. Load them individually.

---

## 3. ARM64 Addressing Feasibility

### 3.1 LDR/STR immediate ranges

| Instruction | Max unsigned offset | Max signed (LDUR) |
|-------------|--------------------:|------------------:|
| LDRB Wn, [Xn, #imm] | 4095 | -256..+255 |
| LDRH Wn, [Xn, #imm] | 8190 (×2) | -256..+255 |
| LDR Wn, [Xn, #imm] | 16380 (×4) | -256..+255 |
| LDR Xn, [Xn, #imm] | 32760 (×8) | -256..+255 |
| LDR Qn, [Xn, #imm] | 65520 (×16) | -256..+255 |

### 3.2 Verdict

- **voodoo_state_t** (600 bytes): All 4-byte fields have offsets ≤ 596.
  Max scaled immediate for LDR Wn is 16380 → **all offsets fit in a single
  LDR/STR instruction** with the base pointer in x0.

- **voodoo_params_t** (1208 bytes): All 4-byte fields have offsets ≤ 1204.
  Max scaled immediate for LDR Wn is 16380 → **all offsets fit**.

- **Sub-byte accesses** (offsets 307, 311, 329, 459): LDRB max unsigned
  immediate is 4095 → **all fit**.

- **8-byte fields** in state (offsets 496-544): LDR Xn requires 8-byte-aligned
  offset for scaled immediate. All int64_t fields are 8-byte aligned → **OK**.

- **No fields require multi-instruction address computation.** This is a
  significant advantage over typical ARM64 scenarios with large structs.

### 3.3 Alignment verification

All 64-bit (8-byte) fields in both structs are naturally 8-byte aligned on
ARM64. The compiler inserts 4 bytes of padding between `base_z` (offset 28,
last 4-byte field before the TMU sub-struct) and `tmu[0].base_s` (offset 32),
which happens to be naturally aligned without explicit padding.

Similarly, the TMU sub-struct within `voodoo_params_t` starts at offset 112
(8-byte aligned) because of the preceding int64_t fields (startW at 88, dWdX
at 96, dWdY at 104).

**No unaligned 64-bit loads are required.** Every LDR Xn / STR Xn instruction
can use its natural scaled-immediate addressing mode.

---

## 4. Offset Constants for ARM64 Codegen Header

These `#define` constants should go at the top of `vid_voodoo_codegen_arm64.h`.
Only JIT-accessed fields are included.

```c
/* --- voodoo_state_t offsets (base register: x0) --- */
#define STATE_tmu0_lod           56   /* tmu[0].lod (nested sub-struct) */
#define STATE_tmu1_lod           88   /* tmu[1].lod (nested sub-struct) */
#define STATE_lod               104
#define STATE_lod_min           108   /* [2] array, stride 4 */
#define STATE_lod_max           116   /* [2] array, stride 4 */
#define STATE_tex_b             156   /* [2] array, stride 4 */
#define STATE_tex_a             180   /* [2] array, stride 4 */
#define STATE_tex_s             188
#define STATE_tex_t             192
#define STATE_tex               240   /* [2][9] ptr array, stride 72 per TMU */
#define STATE_fb_mem            456
#define STATE_aux_mem           464
#define STATE_ib                472
#define STATE_ia                484
#define STATE_z                 488
#define STATE_new_depth         492
#define STATE_tmu0_s            496
#define STATE_tmu0_t            504
#define STATE_tmu0_w            512
#define STATE_tmu1_s            520
#define STATE_tmu1_t            528
#define STATE_tmu1_w            536
#define STATE_w                 544
#define STATE_pixel_count       552
#define STATE_texel_count       556
#define STATE_x                 560
#define STATE_x2                564
#define STATE_x_tiled           568
#define STATE_w_depth           572
#define STATE_ebp_store         580
#define STATE_lod_frac          588   /* [2] array, stride 4 */
#define STATE_stipple           596

/* --- voodoo_params_t offsets (base register: x1) --- */
#define PARAMS_dBdX              48   /* also base of {dB,dG,dR,dA,dZ}dX block */
#define PARAMS_dZdX              64
#define PARAMS_dWdX              96
#define PARAMS_tmu0_dSdX        144
#define PARAMS_tmu0_dWdX        160
#define PARAMS_tmu1_dSdX        240
#define PARAMS_tmu1_dWdX        256
#define PARAMS_color0           304
#define PARAMS_color1           308
#define PARAMS_fogColor         324
#define PARAMS_fogTable         328   /* [64] array, stride 2 */
#define PARAMS_alphaMode        456
#define PARAMS_zaColor          460
#define PARAMS_chromaKey        476
#define PARAMS_tex_w_mask       696   /* [2][10] array, stride 40 per TMU */
#define PARAMS_tex_h_mask       856   /* [2][10] array, stride 40 per TMU */
```

### 4.1 TMU-indexed offset helper

For fields that differ by TMU index (0 or 1), use a macro:

```c
/* TMU-indexed state fields */
#define STATE_tmu_lod(tmu)    ((tmu) ? STATE_tmu1_lod : STATE_tmu0_lod)
#define STATE_tmu_s(tmu)      ((tmu) ? STATE_tmu1_s : STATE_tmu0_s)
#define STATE_tmu_t(tmu)      ((tmu) ? STATE_tmu1_t : STATE_tmu0_t)
#define STATE_tmu_w(tmu)      ((tmu) ? STATE_tmu1_w : STATE_tmu0_w)
#define STATE_lod_min_n(tmu)  (STATE_lod_min + (tmu) * 4)
#define STATE_lod_max_n(tmu)  (STATE_lod_max + (tmu) * 4)
#define STATE_tex_a_n(tmu)    (STATE_tex_a + (tmu) * 4)
#define STATE_tex_n(tmu)      (STATE_tex + (tmu) * 72)  /* 9 × 8-byte ptrs */
#define STATE_lod_frac_n(tmu) (STATE_lod_frac + (tmu) * 4)

/* TMU-indexed params fields */
#define PARAMS_tmu_dSdX(tmu)   ((tmu) ? PARAMS_tmu1_dSdX : PARAMS_tmu0_dSdX)
#define PARAMS_tmu_dWdX(tmu)   ((tmu) ? PARAMS_tmu1_dWdX : PARAMS_tmu0_dWdX)
#define PARAMS_tex_w_mask_n(tmu) (PARAMS_tex_w_mask + (tmu) * 40)
#define PARAMS_tex_h_mask_n(tmu) (PARAMS_tex_h_mask + (tmu) * 40)
```

---

## 5. x86-64 → ARM64 Access Pattern Translation

### 5.1 Simple field load

```
;; x86-64:  MOV EAX, [RDI + offsetof(state, lod)]
;;          (RDI = state pointer)

;; ARM64:   LDR w4, [x0, #STATE_lod]
;;          (x0 = state pointer, w4 = scratch)
```

### 5.2 64-bit field load

```
;; x86-64:  MOV RAX, [RDI + offsetof(state, tmu0_s)]

;; ARM64:   LDR x4, [x0, #STATE_tmu0_s]
```

### 5.3 Sub-byte access (alpha from color1)

```
;; x86-64:  MOVZX EAX, BYTE [RSI + offsetof(params, color1) + 3]
;;          (RSI = params pointer)

;; ARM64 option A (byte load):
;;          LDRB w4, [x1, #311]
;;
;; ARM64 option B (word load + extract, avoids unaligned byte address):
;;          LDR  w4, [x1, #PARAMS_color1]
;;          UBFX w4, w4, #24, #8
```

Option B is preferred — it avoids an unaligned address calculation and the
word load may already be in cache from a previous use of color1.

### 5.4 High 32 bits of int64_t

```
;; x86-64:  MOV EAX, [RDI + offsetof(state, w) + 4]

;; ARM64 option A (offset load):
;;          LDR  w4, [x0, #548]      ; STATE_w + 4
;;
;; ARM64 option B (shift):
;;          LDR  x4, [x0, #STATE_w]
;;          LSR  x4, x4, #32         ; or ASR for signed
;;
;; ARM64 option C (paired load if both halves needed):
;;          LDP  w4, w5, [x0, #STATE_w]  ; w4=lo, w5=hi
```

Option A is simplest. The offset 548 is not naturally 4-byte aligned for the
purpose of the value semantics, but the _address_ 548 is 4-byte aligned
(548 / 4 = 137), so LDR Wn scaled immediate works.

### 5.5 Array element with computed index (tex_w_mask)

```
;; x86-64:  CMP EAX, [RSI + ECX*4 + offsetof(params, tex_w_mask[tmu]) - 0x10]
;;          (ECX = lod + 4, hence the -0x10 to compensate)

;; ARM64 (clean version, no -0x10 hack):
;;          ADD  x4, x1, #PARAMS_tex_w_mask_n(tmu)   ; base of tex_w_mask[tmu]
;;          LDR  w5, [x4, w6, UXTW #2]               ; w6 = lod index (0-8)
;;          CMP  w7, w5
```

### 5.6 Fog table entry (2-byte struct with byte fields)

```
;; x86-64:  MOVZX EAX, BYTE [RSI + fogIndex*2 + offsetof(params, fogTable)]
;;          MOVZX EDX, BYTE [RSI + fogIndex*2 + offsetof(params, fogTable) + 1]
;;          (first byte = fog, second byte = dfog)

;; ARM64:
;;          ADD  x4, x1, #PARAMS_fogTable     ; base of fogTable[0]
;;          ADD  x4, x4, x5, LSL #1           ; x5 = fog index
;;          LDRB w6, [x4]                      ; fog
;;          LDRB w7, [x4, #1]                  ; dfog
```

---

## 6. Platform Consistency

The struct layouts are **identical** on x86-64 and ARM64 (both LP64 with
identical type sizes and alignment rules for these types). Verified by:
- All `int`/`int32_t`/`uint32_t` fields: 4 bytes, 4-byte aligned
- All `int64_t` fields: 8 bytes, 8-byte aligned
- All pointers: 8 bytes, 8-byte aligned
- `rgbvoodoo_t`: 4 bytes (packed {b,g,r,pad}), 1-byte aligned
- `fogTable` entry: 2 bytes ({fog,dfog}), 1-byte aligned

The `offsetof()` values computed on ARM64 can be used directly as compile-time
constants — no runtime offset calculation needed, and no conditional
compilation for different ABIs.

---

## 7. Safety Checklist

- [x] All offsets < 4096 → LDR/STR unsigned scaled immediate: all fit
- [x] All int64_t fields 8-byte aligned → no unaligned 64-bit loads
- [x] All pointer fields 8-byte aligned → no unaligned pointer loads
- [x] Sub-byte accesses use LDRB (always unaligned-safe on ARM64)
- [x] No field offset exceeds 12-bit unsigned immediate × scale factor
- [x] Struct sizes match between x86-64 and ARM64 (same LP64 ABI)
- [x] Little-endian byte order matches (byte +3 = MSB in both)
- [x] The x86 `-0x10` adjustment is identified and will NOT be ported

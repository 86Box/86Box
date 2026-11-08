# Rage Pro VQ textures

ATI3DCIF's VQ textures (C3D_ETF_VQ, Rage Pro only) are implemented in
`mach64_rp_texel_vq`. The lookup is measured on a real Rage Pro (ATI3DCIF
4.11.2692, a diagnostic program built on the SDK, tests A to N); the parts
that are not measured are said so below.

## Format (from the SDK)

* A codebook of 256 blocks, 8 bytes each: four 16-bit texels, upper left,
  upper right, lower left, lower right (`C3D_CODEBOOKENTRY`).
* An index map of (w/2) x (h/2) bytes for each mip level; the smallest
  levels all point at the last index map.

## What a real card does (measured)

* TEX_CNTL bits 21 and 22 are set and bit 30 cleared, DP_PIX_WIDTH 31:28 is
  2. The codebook words are read as RGB565 (a codebook of 5-bit R, G, B
  components stored as 565 came back with every channel intact; inferred:
  1555 would have moved the red and green bits).
* TEX_SIZE_PITCH is in texels. The index map is half the size on each axis.
* The codebook goes into the 0x800 byte texture cache palette image through
  the Y8 scaler pass that loads a CI8 palette. The driver stages it
  swizzled: for k below 128, offset 8k holds the upper two words of block k
  and the lower two words of block k + 128, and offset 0x400 + 8k holds the
  upper two words of block k + 128 and the lower two words of block k.
* The lookup undoes that. For index i the word of row r (0 upper, 1 lower) and
  column c is at image offset
  `(((i >> 7) & 1) ^ r) * 0x400 + (i & 127) * 8 + r * 4 + c * 2`,
  so a texel of the 2x2 block named by index i reads the word at that position
  of codebook block i, for all 256 indices, the lower row included. Point
  sampled 1:1 and at 2x magnification, no pixel offset. The emulator read
  block i at 8 * i with the rows in order, so every lower-row texel came
  from block i ^ 128.

## What the engine does

* The index map is at TEX_n_OFF, in rows, or tiled as an 8 bpp map when
  TEXTURE_TILING is set (not measured for VQ). Its size comes from
  TEX_SIZE_PITCH in texels.
* The texel type is RGB565 for the DP_PIX_WIDTH 31:28 the driver writes (2).
  A 3, 4 or 15 there selects 1555, 565 or 4444 words (not measured; no driver
  we have writes it).
* Not implemented: TEX_CNTL bit 22 (the secondary texture's VQ).

`RP_VQ_TILED_INDEX_MAP` in `vid_ati_mach64_3d.c` is the one switch left that a
real card has not settled.

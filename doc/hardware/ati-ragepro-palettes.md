# Rage Pro palettized textures

The Windows 98 rp98 VM uses the Rage Pro AGP device (1002:4742) and
ATID3DR3.DLL. Forsaken's Visuals / Select Texture Format / 8-bit Palettized
option exposed two independent faults: an incomplete texture-palette load
and an incorrect assumption that 8-bit tiled textures use the 16-bit layout.

## Palette upload

ATID3DR3 stages 0x800 bytes of RGB565 palette data in video memory. In each
8-byte group of the first 0x400 bytes it places two copies of entry i and
two copies of entry i+128. The second half exchanges the low and high
entries. It then runs a front-end scaler pass with destination writes
disabled. The observed register stream is:

| Register | Value |
| --- | --- |
| DP_SRC | 0x505 |
| DST_CNTL | 0x0008e023 |
| DP_PIX_WIDTH | 0x80000003 (Y8 source) |
| DP_WRITE_MASK | 0 |
| SCALE_3D_CNTL | 0x40 |
| TEX_CNTL | 0x40800000 |
| SCALE_Y_OFF | staging + 4 (observed 0x00098804) |
| SCALE_Y_PITCH | 0x3fc |
| SCALE_WIDTH, SCALE_HEIGHT | 0x400, 2 |
| SCALE_HACC, SCALE_VACC | 0, 0x8000 |
| SCALE_X_INC, SCALE_Y_INC | 0x200000, 0x10000 |
| DST_HEIGHT_WIDTH | 0x00200001 |

The source samples are 32 bytes apart. Merely retaining the individual
sampled bytes leaves most palette entries unloaded. The emulator models
32-byte cache fills during Y8 scaler reads with TEX_CNTL bit 30 set and
decodes the fetched palette image. The Rage Pro source offset retains
bit 2, which the older scaler's eight-byte alignment discarded. Both rows
then cover all 0x800 bytes. A subsequent cache flush preserves the decoded
palette, as required by the driver's ordinary texture bindings.

This models the observed driver's palette-loading use of the texture cache;
it is not a complete texture-cache replacement/eviction model or a physical
hardware measurement of cache-line size. TEX_CNTL bit 30 retains its
documented cache-size-select name. The older packed TEX_PALETTE path keeps
its explicit index in bits 31:24; writing 0x340 does not select a separate
auto-increment upload mode.

## CI8 tiling

For a 256x256 8-bit texture, byte address bits from least to most significant
are:

```
x0 x1 y0 x2 y1 y2 x3 y3 x4 y4 x5 (y5 XOR x6) x6 y6 x7 y7
```

This was established by reading Forsaken's menu texture from the VM's AGP
aperture at AGP_BASE + TEX_8_OFF (0xe2000000 + 0x0041c0c0), comparing it with
Data/Textures/M-TpageE.bmp, and finding zero mismatches across all 65,536
index bytes. The original bitmap and VM dumps are external validation
inputs and are not distributed in the repository.

Smaller/rectangular maps omit absent coordinate bits, as the existing
16/32-bit paths do. The three formats now have separate lookup tables;
the established 16-bit and 32-bit layouts are unchanged. Their bank XORs
all affect byte address bit 11.

## Regression checks

Build `mach64_3d_tests`, then run:

```
ctest --test-dir build -R '^Mach64.Palette$' --output-on-failure
```

The test executes the scaler register stream with two different palettes,
checks all 256 decoded entries and local/AGP CI8 reads, verifies that disabled
destination writes and cache flushes preserve the expected state, and
checks that ordinary reads and older cards do not load the cache image.
It also compares all 256x256 tiled addresses with the fixed bit mapping
above and checks local and AGP texels against synthetic coordinate colors.

The rebuilt `build/src/86Box.exe` was also checked in rp98 with Forsaken's
8-bit Palettized format explicitly selected. Both the menu and the Volcano
level rendered without the palette speckling or scrambled tiles. Movement
and firing were exercised; the VM was left paused in the level. This is a
smoke test, not exhaustive coverage of every game or texture dimension.

The full pre-existing Mach64.3D suite reports 84 failed checks with both
the unmodified production source and this change (FIFO-barrier and signed
scissor expectations); those failures are outside this palette correction.
Mach64.Accel also has a pre-existing build failure referring to the removed
TIMING_FIFO_DEPTH constant.

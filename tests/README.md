86Box tests and benchmarks
=========================
Last updated: 2026-09-08

## Overview

This directory, `tests/`, holds C and C++ tests and benchmarks organized to mirror `src/`. For example, for the Mitsumi emulated device:

Driver:  
`src/cdrom/cdrom_mitsumi.c`

Tests:  
`tests/cdrom/cdrom_mitsumi_test.cpp`
`tests/cdrom/cdrom_mitsumi_benchmark.cpp`

Try to match your code's filename and append the type of test it is.

## Summary of current tests

# Mitsumi

The Mitsumi tests exercise the device implementation in isolation using mocked CD-ROM, DMA, interrupt and timer dependencies. They are device-level unit tests, not full-emulator or guest-driver integration tests. The benchmark measures performance and is not a correctness test.

# Cartridges

The cartridge tests compile `src/device/cartridge.c` as C and exercise raw and headered image loading, replacement/ejection, overlap and address bounds, and hard-reset reloads. They use real temporary image files with a small external-memory mapping adapter; they do not emulate CPU execution, RAM/BIOS arbitration, or the real memory mapping cache.

Build the `cartridge_tests` target with `BUILD_TESTING=ON`, then run:

```sh
ctest --test-dir build --output-on-failure -R '^CartridgeTest\.'
```

# FDC Read ID

Build `fdc_read_id_tests` with `BUILD_TESTING=ON`, then run `ctest --test-dir build --output-on-failure -R '^FdcReadId\.'`.

The tests use the real FDC/FDD/IMG/D86F path to check immediate missing-head errors and normal bitstream Read ID completion in DMA and non-DMA modes. They adapt host timers, interrupts and file services; Read ID has no DMA payload, so this is not a DMA-transfer test. The fixture follows the C-in-C++ device-test convention and uses POSIX temporary-directory helpers.

# Raw floppy images

Build `img_track_tests` with `BUILD_TESTING=ON`, then run `ctest --test-dir build --output-on-failure -R '^ImgTrack\.'`.

The tests check consecutive 40-cylinder image placement in ordinary 3.5-inch 720KB/1.44MB drives, preserved 5.25-inch double stepping, physical-cylinder reload after formatting, and invalid-track/sector isolation. Both flux and turbo paths use real FDC/FDD/IMG/D86F code, with an in-memory DMA transport and host-service adapters. Backing-file comparisons verify formatted contents and unchanged neighboring sectors. The fixture follows the C-in-C++ device-test convention and uses POSIX temporary-directory helpers; it does not emulate a complete machine or physical floppy hardware.

The generic raw-image path does not enable machine-specific skipped-track layouts. A 360KB image alone is not an opt-in to double stepping on 3.5-inch hardware.

In the JX integration, selecting a PC JX machine explicitly enables skipped-track placement for eligible 360KB media and 3.5-inch drives: image cylinders occupy even physical tracks, with odd tracks blank. `PcjxFloppy.PlacementIsScopedToMachineAndEligiblePhysicalDrive` checks the machine-selection boundary on both 720KB and 1.44MB drive types, in flux and turbo modes.

# PC JX board and video

Build `pcjx_tests` with `BUILD_TESTING=ON`, then run `ctest --test-dir build --output-on-failure -R '^(PcjxBoard|PcjxFloppy|PcjxNativeVideo)\.'`.

The board tests exercise independently effective decoder-register writes, CPU memory access while display resources are masked, cartridge routing, VP1/VP2 register and display-page separation, and emulated raster feedback. Native-video regressions cover CPU font decoding and ROM protection, gaiji aliases and display persistence, paired full-width codes, font byte lanes, independent CPU/display pages, pre-palette mixing, and combined bitplanes. The fixture includes the board and video implementations with host-service adapters and synthetic glyph data; it does not boot a BIOS or authenticate ROM images.

JX video uses its own `pcjx_video_t` state and `src/video/vid_pcjx.c` implementation. Ordinary PCjr video remains in `src/video/vid_pcjr.c`, without JX-specific branches or state.

The machine's **Native Japanese video** setting defaults to **Automatic (BIOS profile)**: enabled for the Japanese BIOS profile, disabled for both English-market profiles. Explicit Enabled/Disabled overrides are available. Native hardware adds VP2 rendering, CG2 font access, 2 KiB writable gaiji RAM, and VP1/VP2 mixing including combined 640×200×16 graphics. This does not add the separate optional VP3 extension card.

The native loader currently accepts the existing `machines/ibmpcjx/5601_JBA_JFC_KANJI.BIN` resource as a 224 KiB CPU-aperture image. This is not the 128 KiB physical ROM layout described by IBM, and its provenance is unverified. The image ends at virtual address `B7FFFh`; the unavailable tail reads `FFh`. No host-font substitution, checksum patching, or invented ROM contents are used.

Native macOS smoke runs with the local image reached I-BASIC 1.02, displayed `日本あ`, completed a BIOS gaiji write/read comparison and displayed the resulting glyph, and rendered all sixteen color bars using BASIC `SCREEN 6`. Both English BIOS profiles reached BASIC with the font device absent; explicitly enabling native hardware exposed the font resource without changing the English BIOS. The local font image fails three documented module checksums (`A6h`, `A8h`, `A6h`, expected zero), and cold POST reports `ERROR K`; a delivered Enter continues to BASIC. These observations establish exercised emulator behavior, not authentic ROM provenance or a diagnostic-clean hardware qualification.

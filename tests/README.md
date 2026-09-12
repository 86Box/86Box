86Box tests and benchmarks
=========================
Last updated: 2026-09-11

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

# FDC Read ID

Build `fdc_read_id_tests` with `BUILD_TESTING=ON`, then run `ctest --test-dir build --output-on-failure -R '^FdcReadId\.'`.

The tests use the real FDC/FDD/IMG/D86F path to check immediate missing-head errors and normal bitstream Read ID completion in DMA and non-DMA modes. They adapt host timers, interrupts and file services; Read ID has no DMA payload, so this is not a DMA-transfer test. The fixture includes FDC/FDD as C-in-C++ for private device access, compiles IMG/D86F/FIFO/CRC as C, and uses portable filesystem temporary directories.

# Cartridges

The cartridge tests compile `src/device/cartridge.c` as C and exercise raw and headered image loading, replacement/ejection, overlap and address bounds, and hard-reset reloads. They use real temporary image files with a small external-memory mapping adapter; they do not emulate CPU execution, RAM/BIOS arbitration, or the real memory mapping cache.

Build the `cartridge_tests` target with `BUILD_TESTING=ON`, then run:

```sh
ctest --test-dir build --output-on-failure -R '^CartridgeTest\.'
```

# Raw floppy images

Build `img_track_tests` with `BUILD_TESTING=ON`, then run `ctest --test-dir build --output-on-failure -R '^ImgTrack\.'`.

The tests check consecutive 40-cylinder image placement in ordinary 3.5-inch 720KB/1.44MB drives, preserved 5.25-inch double stepping, physical-cylinder reload after formatting, and invalid-track/sector isolation. Both flux and turbo paths use real FDC/FDD/IMG/D86F code, with an in-memory DMA transport and host-service adapters. Backing-file comparisons verify formatted contents and unchanged neighboring sectors. The fixture includes FDC/FDD as C-in-C++ for private device access, compiles IMG/D86F/FIFO/CRC as C, and uses portable filesystem temporary directories; it does not emulate a complete machine or physical floppy hardware.

The generic raw-image path does not enable machine-specific skipped-track layouts. A 360KB image alone is not an opt-in to double stepping on 3.5-inch hardware.

In the JX integration, selecting a PC JX machine explicitly enables skipped-track placement for eligible 360KB media and 3.5-inch drives: image cylinders occupy even physical tracks, with odd tracks blank. `PcjxFloppy.PlacementIsScopedToMachineAndEligiblePhysicalDrive` checks the machine-selection boundary on both 720KB and 1.44MB drive types, in flux and turbo modes.

# PC JX board and video

Build `pcjx_tests` with `BUILD_TESTING=ON`, then run `ctest --test-dir build --output-on-failure -R '^(PcjxBoard|PcjxFloppy|PcjxNativeVideo)\.'`.

The board tests exercise independently effective decoder-register writes, CPU memory access while display resources are masked, cartridge routing, VP1/VP2 register and display-page separation, and emulated raster feedback. Native-video regressions cover CPU font decoding and ROM protection, gaiji aliases and display persistence, paired full-width codes, font byte lanes, independent CPU/display pages, pre-palette mixing, and combined bitplanes. The fixture includes the board and video implementations with host-service adapters and synthetic glyph data; it does not boot a BIOS or authenticate ROM images.

JX video uses its own `pcjx_video_t` state and `src/video/vid_pcjx.c` implementation. Ordinary PCjr video remains in `src/video/vid_pcjr.c`, without JX-specific branches or state.

The single PC JX machine has three BIOS profiles with fixed video hardware: both English profiles (1985 and 1986) always omit native Japanese video and its font resource; the Japanese profile always includes them and requires the Kanji image for availability. There is no native-hardware override, and stale configuration keys cannot change the BIOS profile's hardware. The standard Display panel shows a disabled **Internal device** selector and a disabled Configure button, following the existing board-owned fixed-video convention. Native hardware adds VP2 rendering, CG2 font access, 2 KiB writable gaiji RAM, and VP1/VP2 mixing including combined 640×200×16 graphics. This does not add the separate optional VP3 extension card.

The native loader requires `machines/ibmpcjx/5601_JBA_JFC_KANJI_PATCHED.BIN`, a checksum-corrected 224 KiB CPU-aperture image. BIOS availability checks require the same filename; there is no old-name fallback or migration. This is not the 128 KiB physical ROM layout described by IBM; the format alone does not establish the physical ROM revision or acquisition method. The image ends at virtual address `B7FFFh`; the unavailable tail reads `FFh`. The loader neither substitutes a host font nor patches bytes at runtime.

The patched resource changes three bytes from the preserved original: file offsets `10FFFh` (`00h` → `5Ah`), `1101Fh` (`00h` → `58h`), and `11FFFh` (`00h` → `5Ah`). These are the last bytes of unassigned Shift-JIS slots `887Fh`, `8880h`, and `88FFh`, not defined glyphs. All four module checksums become zero, and the patched image's SHA-256 is `c175adb334ac3eecca8350861ace1f126d2277ed091452a8b9ba8fc39ea24756`. All defined Shift-JIS/CP932 glyphs and the single-byte bank remain byte-for-byte unchanged. Arbitrary software can still address the unassigned slots; this is a deliberate checksum workaround, not an authenticated replacement ROM.

Earlier native macOS smoke runs with the unpatched image reached I-BASIC 1.02, displayed `日本あ`, completed a BIOS gaiji write/read comparison and displayed the resulting glyph, and rendered all sixteen color bars using BASIC `SCREEN 6`. Both English BIOS profiles reached BASIC with the font device absent. The unpatched font image fails three documented module checksums (`A6h`, `A8h`, `A6h`, expected zero), and cold POST reports `ERROR K`; a delivered Enter continues to BASIC. With the checksum-corrected image, a cold boot reaches I-BASIC 1.02 without `ERROR K` or keyboard input, and BASIC displays `星日本あ`. These observations establish exercised emulator behavior, not authentic ROM provenance or a complete hardware qualification.

For full-emulator profile verification, boot both English profiles with a stale `native_video = 1` entry and the Japanese profile with a stale `native_video = 0` entry in the machine's configuration section. English profiles must still omit the native font device, and Japanese must still expose it. With the Kanji image unavailable, both English profiles must remain available while Japanese must not. Check that machine configuration offers BIOS Version and Clock option but no native-video selector, and that Display shows the disabled Internal device selector and Configure button. The isolated board/video fixtures do not substitute for these profile-loading and UI checks.

Delivery verification on Linux ARM64 (Debian trixie Docker, GCC 14.2) and native macOS ARM64 passed all 42 board/video, floppy, and cartridge tests. Both runs had six failures in the separate 15-test Mitsumi suite; the same six failures reproduced on untouched upstream `c9b1d449ce`. The installed Release application booted all three JX profiles with opposing stale native-video settings, and its disabled Internal device controls were checked through native accessibility and a screenshot.

A live Japanese POST trace with the unpatched image passed the first 32 KiB checksum, then failed the next module with `AL=A6h`, producing diagnostic `2701` (`ERROR K`). All 224 KiB read through the CPU aperture matched the loaded file byte-for-byte at the checksum breakpoint. This rules out byte corruption in that exercised mapping, not a font-revision or image-representation mismatch. The Technical Reference's printed page C-13 (PDF page 493), “Kanji ROM address calculation,” illustrates code `90AFh` (星) with a different raster: its row-six left byte at `A15ECh` is `1Fh`, while the image has `00h`. That comparison is not proof that one glyph causes the module checksum failure; the illustration may be schematic rather than a byte-exact font specimen.
The tests use the real FDC/FDD/IMG/D86F path to check immediate missing-head errors and normal bitstream Read ID completion in DMA and non-DMA modes. They adapt host timers, interrupts and file services; Read ID has no DMA payload, so this is not a DMA-transfer test. The fixture follows the C-in-C++ device-test convention and uses POSIX temporary-directory helpers.

# Cartridges

The cartridge tests compile `src/device/cartridge.c` as C and exercise raw and headered image loading, replacement/ejection, overlap and address bounds, and hard-reset reloads. They use real temporary image files with a small external-memory mapping adapter; they do not emulate CPU execution, RAM/BIOS arbitration, or the real memory mapping cache.

Build the `cartridge_tests` target with `BUILD_TESTING=ON`, then run:

```sh
ctest --test-dir build --output-on-failure -R '^CartridgeTest\.'
```

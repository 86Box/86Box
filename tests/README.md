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

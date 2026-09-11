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

# Raw floppy images

Build `img_track_tests` with `BUILD_TESTING=ON`, then run `ctest --test-dir build --output-on-failure -R '^ImgTrack\.'`.

The tests check consecutive 40-cylinder image placement in ordinary 3.5-inch 720KB/1.44MB drives, preserved 5.25-inch double stepping, physical-cylinder reload after formatting, and invalid-track/sector isolation. Both flux and turbo paths use real FDC/FDD/IMG/D86F code, with an in-memory DMA transport and host-service adapters. Backing-file comparisons verify formatted contents and unchanged neighboring sectors. The fixture follows the C-in-C++ device-test convention and uses POSIX temporary-directory helpers; it does not emulate a complete machine or physical floppy hardware.

The generic raw-image path does not enable machine-specific skipped-track layouts. A 360KB image alone is not an opt-in to double stepping on 3.5-inch hardware.

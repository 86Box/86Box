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

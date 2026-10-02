# PC-9801VX CPU limits and release logging

The VX permits 286 CPUs at 8 or 10 MHz. Partial and full CPU overrides cannot
bypass its CPU type, clock or package limits, including when loading old INI
files. Ordinary PC override behavior remains available. The VX memory range
is 640–8832 KiB; PC-98 configurations are clamped to that range.

The banked firmware loader accepts the existing VX layouts and checks exact
file sizes. Its ROM windows cover the conventional and 24-bit reset addresses.
Regression fixtures use synthetic firmware bytes.

## Local firmware

The `roms/`, `converted/` and `tmp/` directories contain local firmware, disk
images and investigation outputs and are excluded from Git. Install VX firmware
under `machines/pc9801/` in a configured ROM root. The accepted layouts are:

| Layout | Required files |
| --- | --- |
| Normalized IPL | `ipl.rom`, exactly `0x30000` bytes |
| Separate banks | `itf.rom` (`0x8000`) and `bios.rom` (`0x18000`) |
| Physical CPU-board dumps | `nec_d27c256d-15_cpu_extboard_yll01.bin` through `yll04.bin`, each `0x8000` bytes |

Every layout also requires `font.rom` (`0x46800`) and `24256c-x01.bin` through
`24256c-x04.bin` (each `0x8000`) in that directory. The normalized IPL contains
erased bytes at `00000–0FFFF`, ITF at `10000–17FFF`, and BIOS at `18000–2FFFF`.

## Release logging

| Audit finding | Change |
| --- | --- |
| A CMake Release configuration did not necessarily set `RELEASE_BUILD` | Release configurations now set it even when `RELEASE=OFF` |
| Packaging switches could disable diagnostics in Debug | Explicit Debug configurations retain logging even with `RELEASE=ON` or `BUILD_TYPE=release` |
| No-op logging functions still evaluated arguments | Release callers omit `pclog` and `pclog_ex` arguments; callable no-op symbols remain available |
| PC-9801-55 initialization and availability used `always_log` | Device diagnostics now use the release-gated logger |
| Tape tracing could write its own debug file | Release undefines tape trace/file flags, including explicitly enabled flags |
| Qt printed a routine startup banner directly to stderr | The banner is limited to non-release builds |
| Native PC-98 traces scanned RAM or formatted data without visible output | Release omits the reset memory probes, descriptor scans, transfer dumps, command/result formatting, display-state traces and reset-vector dumps |
| `log_set_suppr_seen` was declared but not implemented in release | Its implementation is available in both builds |

Command decoding, DMA routing, interrupt queues, reset control and media transfers remain active. In particular, the floppy command buffers also carry functional parameters, so disabling diagnostics does not remove them. Error dialogs, fatal/warning paths and explicit CLI output remain available.

This audit covers the CPU/profile changes and emulator diagnostic paths involved here. It is not a claim that every device in the repository has received a functional or security audit.

## Validation

After removing the experimental model additions, 665 regression checks passed:
109 machine/firmware, 23 video mapping, 159 floppy/RTC, 358 graphics and 16 logging
checks. The machine and video mapping checks also passed under the Debug policy.

Standalone regression projects cover the VX machine registration and CPU
eligibility, firmware loading, video ROM windows and bank/reset handling,
native floppy transfers and reset ordering, graphics, and both logging policies.
They can be built without Qt or GoogleTest:

```sh
cmake -S tests/machine/pc98 -B build/pc98-machine-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/pc98-machine-tests
ctest --test-dir build/pc98-machine-tests --output-on-failure

cmake -S tests/logging -B build/logging-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/logging-tests
ctest --test-dir build/logging-tests --output-on-failure
```

Use a separate Debug build directory for the machine tests to exercise the
diagnostic branches. The logging project builds both logging policies in one run.

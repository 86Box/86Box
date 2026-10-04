# PC-9801VX CPU limits and release logging

By default, VX offers its standard 286 CPUs at 8 or 10 MHz and limited compatibility
choices: Intel i386SX/DX and AMD Am386SX/DX from 16 MHz through the catalogue's
nominal 33 MHz entry (33,333,333 Hz). These appear directly in the VX CPU
selector. The profile continues to use VX firmware and motherboard devices.

The 386 choices are restricted to the four named CPU families, their matching
SX/DX packages, and a 1× multiplier. With the default settings, faster CPUs,
embedded variants, special packages and 486 upgrades are excluded. Partial and
full CPU overrides do not expand this list, including when loading old INI files.
The VX's Configure dialog now offers **Skip BIOS timer checks (CPU upgrades)**,
disabled by default. It patches the recognized ITF revision in memory and enables
plain Intel/AMD 386 choices through 40 MHz and the selected Intel/AMD 486
SX/DX/DX2/DX4 families through 100 MHz. It retains explicit family, core,
package and multiplier checks. Disabling the option restores the default CPU
list and original firmware on the next initialization. Details and runtime
limits are in `pc98-vx-timer-post-bypass.md`.

Ordinary PC override behavior remains available. The VX memory range is
640–8832 KiB; configurations
are clamped to the selected machine's limits. The experimental BX4 has its own
CPU, firmware and memory policy, described in `pc98-bx4-experimental.md`.

The banked firmware loader accepts the existing VX layouts and checks exact
file sizes. Its ROM windows cover the conventional and 24-bit reset addresses.
Regression fixtures use synthetic firmware bytes.

## Native 386SX firmware prerequisite

The PC-9801RS is a suitable next native model: NEC's 1993 hardware manual lists
the RS51 with a 16 MHz 386SX and an 8 MHz V30. The local firmware inventory has
its `itf_rs.rom`, but lacks the identified RS BIOS and font. The staged
`bios_rx.rom` matches the RX `BAD_DUMP` in
[MAME's NEC driver](https://github.com/mamedev/mame/blob/master/src/mame/nec/pc9801.cpp),
whose source explicitly questions that BIOS's model provenance. Combining these
files cannot establish a native RS firmware set. A native RS registration is
therefore deferred rather than identifying the VX compatibility CPUs as an RS.

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

The machine and CPU policies pass 6398 machine/firmware checks, including every CPU
catalogue entry under override modes 0, 1 and 2, clock boundaries, family/type
mismatches and forbidden packages/multipliers, with the optional upgrade setting
both enabled and disabled. These checks and 134 video mapping
and actual-PIC interrupt checks pass under both Release and Debug policies.

The updated floppy/motherboard suite passes 2316 checks, including reset during
transfers, DMA route changes, page masking and the BIOS equipment scan for zero,
one and two installed mechanisms. The separate production DMA-core suite passes
113 checks in Release and Debug. The image/FDC/production-8237 suite passes 58
checks and the media lifecycle suite passes 170; both also pass in Release and
Debug. The DOS 6.20 hard-reset failure and its physical recalibration fix are
documented in `pc98-floppy-lifecycle-audit.md` and
`pc98-floppy-wrapper-audit.md`. The preceding repository cleanup also passed
358 graphics and 16 logging checks.

Skirmish Disk A exposed a separate CRT interrupt arming defect during GE10.COM
initialization. The video wrapper now requests one interrupt after power-on or
reset, and one after each port 64h reset write. EOI alone does not rearm it.
See `pc98-vrtc-interrupt-audit.md` for the hardware protocol and
`pc98-skirmish-boot-fix.md` for guest instruction evidence and runtime results.
The D88 regression also passes 125 checks against the user's Disk A, read-only,
including cold READ ID and head/track changes with turbo both enabled and disabled.

The equipment scan does not invent absent drives. FreeDOS lists each configured
NEC mechanism through both its 2HD and 2DD interfaces: FD0/FD2 are physical drive
zero and FD1/FD3 are physical drive one. Selecting **None** for the second
mechanism removes its equipment bits; an empty drive with no inserted image is
still an installed mechanism.

Additional device validation and guest-runtime limits are recorded in
`pc98-scsi-bios-fix.md`, `pc98-additional-sound.md` and
`pc98-common-peripherals.md`. Local firmware and test executables are not a
published patron release.

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

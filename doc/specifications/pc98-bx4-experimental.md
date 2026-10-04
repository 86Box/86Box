# PC-9801BX4 experimental firmware profile

The `pc9801bx4` profile uses the user's supplied BX4 ROM banks. It is an
experimental development target, not complete stock BX4 emulation. The VX remains
a separate model with its existing firmware paths, CPU upgrades and RAM limits.
The requested true 386SX model still needs its own identified firmware and
hardware profile; this BX4 entry does not stand in for that model.

## Hardware and implemented scope

[NEC's BX4/U2 specification](https://support.nec-lavie.jp/support/product/data/spec/cpu/95070039-1.html)
identifies the standard CPU as an Intel i486DX2 at 66 MHz, with a DX4 100 MHz
OverDrive option. The machine selector restricts CPUs to those choices. A stock
486SX is not offered. The profile exposes the specified 2 MiB standard RAM and
64 MiB maximum, but high memory and the later machine's memory control hardware
are not yet validated with the firmware.

The current development target reuses the legacy PC-98 dual-GDC graphics,
keyboard, floppy controller, C-BUS peripherals, printer and mouse. It adds
model-specific firmware loading, a 32-bit reset ROM alias, logical flash-bank
selection at 043Fh, a native SDIP NVR backend and the later F6h A20 gate. F6h is decoded only for BX4;
02h enables A20 and 03h masks it. F2h retains the common PC-98 behavior.

SDIP is 24 bytes in two banks selected through 8F1Fh values 80h/C0h; its twelve
byte ports are 841Eh–8F1Eh at intervals of 100h. Guest writes are persisted to
`pc9801bx4.sdip` through the normal per-VM NVR directory. Missing or malformed
NVR starts erased (FFh); firmware owns configuration initialization and parity.
Reset selects bank zero and retains saved bytes. The legacy DIP inputs reflect
the stored SDIP settings. These register and translation semantics are supported
by [MAME's SDIP device](https://github.com/mamedev/mame/blob/master/src/mame/nec/pc98_sdip.h).

The 09A8h display-frequency selector stores bits 0–1, with bit 7 low. Its
reset value is zero. The legacy renderer still uses its existing 24 kHz timing;
31 kHz output timing is not implemented.

The stock GD5430 local-bus graphics and 1 MiB VRAM, full firmware setup behavior, PMC,
onboard IDE and later memory-controller behavior are missing. This profile does
not claim those facilities or successful operating-system boot. Its title and
startup log explicitly identify the experimental scope.

## ROM input

Supply all eight files `pc98BANK0.BIN` through `pc98BANK7.BIN`, each exactly
32,768 bytes, in `roms/machines/pc9801bx4/` under a configured ROM root. Files
with missing, truncated or oversized banks are rejected. VX firmware is never
used as a fallback. The common character ROMs currently remain under
`roms/machines/pc9801/`; the bank archive contains no character ROMs.

These are logical `getitf98` banks, not raw physical flash-chip order. The
[dump author's ROM-map documentation](https://www7b.biglobe.ne.jp/~marimo9821/knowhow/rommap.html)
describes bank 4 as ITF and banks 5–7 as BASIC and system BIOS. The loader builds
the reset layout from bank 4 and the normal E8000h–FFFFFh layout from banks 5–7.
All eight banks remain available for logical F8000h selection through
043Fh values E0h, E2h, …, EEh. Writing the normal IPL selection at 043Dh restores
the normal mapping. The supplied bank 4 reset vector targets F800:4000, while
bank 7's reset vector targets FD80:0000.

[MAME's NEC driver](https://github.com/mamedev/mame/blob/master/src/mame/nec/pc9801.cpp)
provides an independent reference for the normalized logical-bank ordering and
later PC-98 A20 port behavior. It is not evidence of complete BX4 emulation.
No firmware bytes are committed to this repository.

## Verification

The standalone machine tests use the production machine registration, CPU
catalogue and ROM loader. They verify the allowed DX2/DX4 models, isolated ROM
paths, all exact bank lengths, ITF/BIOS normalization, the 32-bit reset alias and
preservation of VX's 16-bit alias. The video harness exercises actual memory
handlers for reset/normal bank selection and every logical flash bank at low,
16-bit and 32-bit aliases. The shared motherboard harness verifies that F6h
affects BX4 only, ignores unsupported writes and reports active-low A20/NMI
status correctly. SDIP tests cover both banks, persistence across reset/reload,
erased defaults, invalid NVR lengths, selector writes and legacy DIP translation.
These tests pass; real firmware POST and guest boot remain separate runtime checks.

A disposable VM with the supplied ROMs and DX2-66 reached BX4 ITF and wrote
`SET THE SOFTWARE DIP-SWITCH` to native text VRAM. This proves execution from
the supplied BX4 reset bank and display output. The initial 25-second run had
not initialized the disk BIOS vector or BIOS work area; complete POST/boot is
not established by this evidence.

With SDIP and 09A8h implemented, the firmware generates and persists its own
24-byte configuration. A second cold start reads that saved configuration, but
still repeats the configuration-validation/initialization path before normal
BIOS setup. Runtime probes at the SDIP parity tests observe the expected odd
parity for both banks. The later character-generator configuration shadow is
not established: code-mode character 5610h contains FFh in the two bytes that
ITF reads as a checksum length at F800:4741–474Ah, yielding FFFFh. That path scans
further CG storage, including 58xx, while the current VX-derived write decoder
supports 56xx/57xx only. This is a concrete unresolved BX4 configuration and CG
mapping requirement, not evidence that the ROMs are missing. No checksum or
configuration bytes are patched to bypass it.

```powershell
cmake -S tests/machine/pc98 -B build/tests-pc98-machine -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/tests-pc98-machine
ctest --test-dir build/tests-pc98-machine --output-on-failure

cmake -S tests/floppy/pc98 -B build/pc98-floppy-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/pc98-floppy-tests
ctest --test-dir build/pc98-floppy-tests --output-on-failure
```

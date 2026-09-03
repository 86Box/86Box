# ELO Serial Touchscreen Emulation — Plan

## Goal

Add a new 86Box device emulating an Elo TouchSystems **SmartSet** serial
touchscreen controller (the "10-byte packet" protocol — the modern/native Elo
serial protocol, as opposed to the legacy 3/4/6-byte emulation formats also
supported by the same silicon). This lets guest OSes running period-accurate
Elo drivers (DOS, Windows 3.1x, Windows 9x) talk to a virtual touchscreen the
same way they would to a real E271-2200/2210/2201/2202 SmartSet controller,
using 86Box's existing absolute-pointer ("tablet") input plumbing.

Model: 86Box already ships `mouse_microtouch_touchscreen.c`, a 3M MicroTouch
serial touch device. It is the closest existing analog — same category
(`tablet_devices[]`), same serial-port attach pattern, same NVRAM-backed
calibration persistence, same `mouse_get_abs_coords()` input source. The ELO
device follows the same file shape and lifecycle, with an Elo-specific command
parser instead of MicroTouch's SOH/CR ASCII command language.

## Why SmartSet ("10-byte") and not the legacy formats

The Linux kernel's `drivers/input/touchscreen/elo.c` supports four wire
formats keyed by a `proto` field: a 10-byte packet (its default/primary path),
plus 6-byte, 4-byte and 3-byte legacy formats. Cross-referencing that with
Elo's own 1993 *SmartSet Touchscreen Controller Family Technical Reference
Manual* (`Info/SmartSet.PDF`) shows the kernel's "10-byte" framing is
byte-for-byte the SmartSet serial packet: lead byte `0x55` (`'U'`), 8 command/
response bytes, trailing checksum = `0xAA + lead + Σ(8 bytes)` mod 256. The
kernel's 6/4/3-byte formats correspond to SmartSet's *emulation modes* for
older discontinued controllers (IntelliTouch E281A-4002, AccuTouch E271-140,
DuraTouch E261-280 respectively) — legacy formats a SmartSet-family
controller can still speak, but not what it speaks natively.

The DOS/Win3.1/Win9x driver packages under `Drivers/` (`elodos.zip`,
`EloDos_W31.zip`, `elo98.exe`, `win9x sw500085.exe`) and the utilities under
`Utilities/eloutil/` (COMDUMP, COMTEST, BUSSTAT, INFO, SAWDUMP, TSTIMER) are
all-generations-later than 1993 and are expected to speak SmartSet natively
by default. **Boring-explanation check (guardrail 6):** rather than
reverse-engineer the DOS driver binaries to find the wire protocol, the cheap
experiment is to implement the documented SmartSet protocol first and use
`COMDUMP.EXE`/`COMTEST.EXE` (raw serial dump/echo tools, no driver required)
against the emulated device to confirm byte-for-byte what the real driver
sends before touching a disassembler. RE tooling (radare2/Ghidra in WSL) is
the fallback if the documented protocol doesn't match observed bytes.

## Protocol summary (implementation reference)

Full command reference lives in `Info/SmartSet.PDF` chapters 5–6; condensed
notes below are what the device needs.

**Framing:** `0x55` lead byte, 8 data bytes (byte 0 = command char, 7 data
bytes), 1 checksum byte. Checksum is transmitted by the controller but by
factory default *not* validated on receive (a `Parameter` bit turns on
enforcement — low priority, most drivers won't set it).

**Command byte case convention:** uppercase = *set* (host → controller,
controller acks), lowercase = *query* (host → controller, controller replies
with the same packet uppercase, then an Ack). Every command (except Hard
Reset and Quiet-all) is followed by an `Acknowledge` (`'A'`) packet:
`'A' err1 err2 err3 err4 0 0 0`, `'0'` = no error.

**Commands to implement (priority order):**
1. `Acknowledge` ('A'/'a') — every command's trailer.
2. `Touch`/auto-report ('T'/'t') — the actual pointer data, sent unsolicited
   in Stream mode (factory default): `'T' status x_lo x_hi y_lo y_hi z_lo
   z_hi`, x/y/z little-endian signed 16-bit. Status bits: 0=Initial,
   1=Stream, 2=Untouch, 4=WarningPending, 6=OutOfRange.
3. `Mode` ('M'/'m') — binary form (byte1=0) sets Initial/Stream/Untouch
   report bits, Range-Checking, Calibration, Scaling, Trim, Tracking.
   Factory default: Initial+Stream+Untouch on, Calibration+Scaling off
   (raw 0–4095 range).
4. `ID` ('i') — touchscreen type / interface / firmware rev / feature bits.
5. `Reset` ('R') — soft (warm) vs hard (cold, jumper/NVRAM reload); hard
   reset gets no Ack, host waits for CTS instead (mirror the MicroTouch
   device's `reset_timer` pattern).
6. `Calibration` ('C'/'c') — set/query per-axis Offset/Numerator/Denominator
   or Low/High raw points; axis swap flag.
7. `Scaling` ('S'/'s') — same shape as Calibration but signed, drives the
   output range (default 0–4095).
8. `Parameter` ('P'/'p') — baud rate + framing (serial-only fields); must
   actually reconfigure the emulated UART-facing timer like MicroTouch's
   `host_to_serial_timer` baud recompute.
9. `Quiet` ('Q'/'q') — mask Touch/Timer auto-reports and quiet-all.
10. `Jumpers` ('j'), `Configuration`/dump ('g'), `Owner` ('o'), `Diagnostics`
    ('D'/'d'), `NVRAM` ('N'), `Report` ('B'/'b'), `Filter` ('F'/'f'),
    `Timer` ('H'/'h'), `Key` ('K'/'k'), `Low Power` ('L'/'l') — stub with
    sane fixed/defaulted responses; only worth fleshing out if a real
    driver is observed depending on one (COMDUMP capture will show this).

**Not implementing:** PC-Bus/Micro Channel I/O-port variants (E271-2201/2202)
— out of scope, no ISA/MCA touch controller card in 86Box's model; bus vs.
serial is selected in real hardware by which controller SKU you buy, and
86Box's `tablet_devices[]` slot is serial-port-attached only, matching
MicroTouch's existing device.

## Integration points (confirmed by reading the MicroTouch device + build)

- New file `src/device/mouse_elo_touchscreen.c`.
- `src/device/CMakeLists.txt` — add to the `mouse` target's source list
  (alongside `mouse_microtouch_touchscreen.c`).
- `src/include/86box/mouse.h` — `extern const device_t mouse_elo_device;`
- `src/device/mouse.c` — add `{ &mouse_elo_device }` to `tablet_devices[]`
  (Qt/other frontends enumerate this array generically; no separate UI code
  needed, confirmed by grep — MicroTouch has no other reference sites).

## Phases

| Phase | Deliverable | Status |
|---|---|---|
| 0 — bootstrap: read MicroTouch device, fetch/cross-reference Linux elo.c + SmartSet PDF, confirm integration points, stand up this repo | this file | done |
| 1 — implement `mouse_elo_touchscreen.c` (packet parser, Touch/Mode/Calibration/Scaling/Parameter/Reset/ID/Ack), wire into build | `src/device/mouse_elo_touchscreen.c` | done |
| 2 — build via MSYS2 (ucrt64, not clang64 — see toolchain note below), smoke-test device attaches/detaches cleanly in 86Box UI | `build/regular/src/86Box.exe` | done |
| 3 — DOS test: `Utilities/eloutil/COMDUMP.EXE` + `COMTEST.EXE` against the emulated COM port with no driver loaded, confirm raw bytes match protocol notes above | — | done — packets correctly framed/checksummed, tracked live during a drag |
| 4 — DOS test: install `Drivers/elodos.zip` driver + calibrate + verify touch tracks mouse/tablet input | — | done — `ELODEV.EXE` 1.7d detects+self-tests, `ELOCALIB.EXE` 1.6c calibrates and tracks correctly (found and fixed two real bugs along the way: the Offset/Numerator/Denominator calibration/scaling set-form was a silent no-op, and Trim mode wasn't clamping) |
| 5 — Windows 9x test: install `Drivers/elo98.exe` or `win9x sw500085.exe`, verify touch works in Win98SE test VM | — | done — MonitorMouse 03.00.00 installs without PnP involvement (expected, real hardware isn't PnP either) and reports "installed and working properly"; the Diagnostics tab's "Controller model" field shows "Not available" — confirmed via strings/RE on the driver binary that this is a generic placeholder baked into `monmouse.cpl` with no wire-protocol field backing it, not a gap on our end (real hardware would show the same) |
| 6 — write up findings, README, protocol doc polish | — | in progress — upstream PR opened: https://github.com/86Box/86Box/pull/7849 |

New surfaces (protocol mismatches, driver quirks, undocumented commands a
real driver turns out to need) are expected to appear during phases 3–5, not
a process failure — append them here when found.

**Toolchain correction (Phase 2):** the workspace has MSYS2 `ucrt64`, not
`clang64` as originally assumed. Build recipe:
```
export PATH="/c/msys64/ucrt64/bin:$PATH"
cmake -B build/regular -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DDEV_BRANCH=OFF -DNEW_DYNAREC=OFF -DQT=ON \
  -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64/qt5-static \
  -DQt5_DIR=C:/msys64/ucrt64/qt5-static/lib/cmake/Qt5 \
  -DQt5LinguistTools_DIR=C:/msys64/ucrt64/qt5-static/lib/cmake/Qt5LinguistTools
ninja -C build/regular src/86Box.exe
```
The resulting `86Box.exe` is fully statically linked (`objdump -p` shows only
standard Windows/UCRT DLLs, no MinGW runtime or Qt DLLs) — a build folder is
a portable, drop-in-and-run VM rig with no other files needed beyond the
usual `roms/`, `nvr/`, and a `.cfg`.

## What the user supplies / has already supplied

- `Win98SE/` — working 86Box VM (Windows 98 SE + DOS-bootable) for phases 3–5.
- `Drivers/`, `Utilities/`, `Info/` — vendor driver/utility/documentation
  archives, already extracted into this workspace.
- MSYS2 clang64 build toolchain (confirmed working: `Git/86Box-Builds` has a
  successful prior build of `Xeon3D/86Box-noVMM` via the same toolchain).

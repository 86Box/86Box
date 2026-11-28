# SID to PMD86

Converts one C64 SID chip into a PMD `.M` score for a NEC PC-98 with a
PC-9801-86 sound board. Three FM parts approximate pitched SID voices and
three SSG parts approximate noise. Fast writes to the D418 volume register
become signed PCM in a `.P86` bank for P86DRV. This is an approximation of SID
sound; it does not require a SID board or an MML compiler.

## Quick test

`converted/sid2pmd/EVOLVER_BOOT.hdi` contains a complete 222-second capture of
the provided `Evolver_6581.sid`, its original SID file, PMD86/PMP/P86DRV and
FreeDOS. Attach as SASI unit 0 on a PC-9801-27 controller, using its automatic
drive profile. Use 4 MiB RAM and a PC-9801-86 board at 188h, IRQ 12.
Geometry is 615 cylinders, 8 heads, 33 sectors, 256 bytes per sector.

The image autoplays. DOS commands `REPLAY`, `STOP` and `DIGITEST` replay Evolver,
stop playback and run a two-second original PCM diagnostic respectively.
Evolver uses normal slow master-volume changes rather than detected D418
digis, so it needs no PCM bank. The diagnostic exercises the PCM path.
Evolver's FM/SSG controls update about every 120 ms to fit PMD86's 40 KiB
resident buffer. The capture starts without discarding its introduction.

## Convert another tune

Python 3.10+, CMake 3.20+ and a C++17 compiler are required. Setup downloads
the hash-pinned official libsidplayfp 3.1.1 source and builds the capture helper.
On this Windows checkout, the installed MinGW toolchain is detected automatically.

```powershell
python tools/sid2pmd.py --setup
python tools/sid2pmd.py song.sid SONG.M --seconds 180 --warmup 0
```

The output basename must follow DOS 8.3 naming. Copy the `.M`, generated
`.BAT`, and optional `.P86` alongside `PMD86.COM`, `PMP.COM` and `P86DRV.COM`
on the PC-98. Run the batch file there. Existing files require `--force`.
Conversion also writes a JSON report with the chosen subtune, duration,
effect approximations, PCM regions and control interval.

Conversion now tracks SID rate-counter ADSR, including sustain changes, release,
gate retriggers and the rate-change delay. Pitch retains a stable note anchor
and follows vibrato and slides with detune commands. PWM and waveform combinations
change a four-partial spectrum. Routed filter cutoff, resonance and LP/BP/HP
combinations change FM operator levels live during held notes, including notch
sweeps. Noise also follows filter gain and approximate bandwidth. Ring and sync
use the source oscillator's frequency in the harmonic approximation.

Controls are sampled about every 20 ms. Adaptive reduction retains note edges
and treats pitch and each operator level independently. `--resolution` specifies
level tolerance in 0.75 dB steps, rather than discarding whole blocks of controls.
`--fit auto` (default) relaxes precision until the score fits PMD86's 40 KiB
resident buffer; `--fit strict` keeps the requested precision and fails if it
cannot fit. The JSON report records the actual pitch and level tolerances.
D418 sample rendering stays at 16.5375 kHz. Generated launchers allocate the
score buffer with `/M40 /V0 /E0` and enough EMS pages for the PCM bank.
The full Evolver example uses:

```powershell
python tools/sid2pmd.py "C:/Users/Clara/Downloads/Evolver_6581.sid" converted/sid2pmd/EVOLVER.M --seconds 222 --warmup 0 --resolution 6
```

`--song` selects a 1-based subtune. PAL/NTSC comes from the SID header unless
overridden with `--clock`. `--digi auto` keeps ordinary volume fades in the
FM/SSG master volume and extracts rapid DAC changes; `force` treats all
D418 changes as PCM, and `off` disables extraction. `--digi-gain` adjusts
PCM amplitude. `--warmup` defaults to zero to retain the introduction and
start software envelope tracking from reset.

The capture uses libsidplayfp's full C64 CPU, VIC and CIA scheduler, including
illegal opcodes. RSID tunes need a user-supplied Kernal ROM via `--kernal`;
BASIC tunes also need `--basic`. Multi-SID tunes are rejected.

## Limits and verification

Four FM carriers approximate the waveform spectrum and the filter's complex
frequency response. FM does not reproduce SID analog distortion, phase,
combined-waveform bit coupling or arbitrary non-harmonic ring/sync sidebands.
The 6581/8580 cutoff curves are approximate calibrations, not chip measurements.
SSG noise pitch is shared
across its three channels. Oscillator/PWM digis are not decoded. Captures
have an explicit duration; the converter does not infer a tune's ending.
The binary format has 16-bit pointers, but this PMD86 resident driver allows
a maximum 40 KiB score buffer; the converter enforces that smaller limit.
PCM supports up to 256 regions.

```powershell
$env:SID2PMD_TRACE = (Resolve-Path build/sid2pmd/sidtrace.exe).Path
python -m unittest discover -s tools -p test_sid2pmd.py -v
python tools/sid2pmd_hdi.py
```

Tests exercise C64 execution, subtune selection, D418 extraction, PCM offsets,
gate retriggers, release, relocated PMD loop pointers and output safety.
The HDI packager preserves the template's boot sectors and checks file
hashes, both FAT copies, cluster ownership and free space.
The earlier converter was rendered with PMDWin and the Evolver disk was booted.
The subsequent synthesis improvements and Turbo Outrun disk were built without
tests or playback verification, as requested.

Package another score without post-build verification:

```powershell
python tools/sid2pmd_hdi.py --sid "C:/Users/Clara/Downloads/Turbo_Outrun.sid" --score converted/sid2pmd/TURBO.M --output converted/sid2pmd/TURBO_BOOT.hdi --no-verify
```

The packager uses the existing SASI-compatible FreeDOS image and locally
available PMD drivers. FreeDOS license files and kernel source are included
on the disk. Keep original tune attribution when sharing music.

Sources: [libsidplayfp](https://github.com/libsidplayfp/libsidplayfp),
[original PMD driver](https://github.com/d2lmirrors/pmd),
[PMD sequence format](https://github.com/ValleyBell/MidiConverters/blob/master/pmd_SeqFormat.txt),
[independent PMDWin player](https://github.com/mistydemeo/pmdmini).

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

# TEAC CD-55A

The `mke_cdrom_tests` target includes Panasonic regressions and TEAC protocol
tests. Run `ctest --test-dir build -R '^(Mke|Teac)Test\.' --output-on-failure`.
The optional `cdrom/teac_cda_smoke.py` boots external TEAC DOS drivers against
Creative, standard Panasonic, TEAC 8-bit and TEAC 16-bit interfaces. It checks
exact ISO/BIN file copies and optional CD-DA/MSCDEX operations and PCM output.
See [TEAC hardware and validation notes](../doc/hardware/teac-cdrom.md) for
setup, required external images and validation limits.

# Creative CD-200 / FUNAI CD200F

The `mke_cdrom_tests` target also exercises the seven-byte CMD2 protocol on
both MKE adapter layouts, including the packets used by CRCCD2.ADD in
CRCCD2.ZIP. Run `ctest --test-dir build -R '^(Mke|Mke2|Teac)Test\.'
--output-on-failure`. These are isolated device tests with mocked media and
timers. The optional `cdrom/cd200_smoke.py` boots external Creative DOS
drivers and checks exact CD file copies on both adapter layouts for either
drive model. See [CD-200 protocol and validation notes](../doc/hardware/cd200-cdrom.md)
for the archive evidence and the limits of guest-driver validation.

# WD90C31 graphics

Build `wd90c31_tests` with `BUILD_TESTING=ON`, then run `ctest --test-dir build --output-on-failure -R '^Wd90c31\.'` for register, BitBLT, host-transfer and hardware-cursor checks. The optional `video/wd90c31_bios_smoke.py` runs the supplied option ROMs in a complete SDL emulator using a synthetic AT BIOS. See [the WD90C31 hardware notes](../doc/hardware/wd90c31.md) for ROM paths, commands, tested modes and validation limits.

# ATI Mach64

Build `mach64_accel_tests` and `mach64_3d_tests` with `BUILD_TESTING=ON`, then run `ctest --test-dir build --output-on-failure -R '^Mach64\.'`. Both include the source they test, for its private functions.

`video/vid_ati_mach64_accel_test.c` runs the draw engine's FIFO thread on the test's own thread. It checks the color compare of transparent blits as the 3D Rage II+ DirectDraw driver writes them, `DP_SET_GUI_ENGINE` and the scissors it opens, the overlay's ECP clock division, source edges and YUV blends on the VT2 and the GT-B, and the GT-B's engine timing: its 48-entry FIFO, a CPU write waiting on a full FIFO, the clocks from the PLL, the display's share of the memory and the memory costs of ATI's method (RAGE PRO Programmer's Guide 7.9.7).

`video/vid_ati_mach64_3d_test.c` stubs the draw engine FIFO and the timing model. It checks the 3D registers' fixed-point fields, the texture palette and keys, mip level selection, YUV and the scaler, then 1,077 draw cases through the registers against the whole frame and Z buffers: triangles of two trapezoids in ARGB1555, RGB565 and ARGB8888, shaded, textured and additive, in both directions and clipped; all eight Z tests; texel keys, alpha masks and the destination compare; the `DST_BRES_LNTH` commands of RRG-G02700 4-46; signed scissors; color and Z fields that wrap; textured lines; bilinear texel centers; and eight register streams of ATI3DCIF 4.03.2510's triangle setup against the color and depth planes of their vertices. The expected images are closed forms, not the engine's own helpers. The tests establish the documented register behavior and the engine's consistency, not the chip's undocumented rounding or dither.

# Mitsumi

The Mitsumi tests exercise the device implementation in isolation using mocked CD-ROM, DMA, interrupt and timer dependencies. They are device-level unit tests, not full-emulator or guest-driver integration tests. The benchmark measures performance and is not a correctness test.

# FDC Read ID

Build `fdc_read_id_tests` with `BUILD_TESTING=ON`, then run `ctest --test-dir build --output-on-failure -R '^FdcReadId\.'`.

The tests use the real FDC/FDD/IMG/D86F path to check immediate missing-head errors and normal bitstream Read ID completion in DMA and non-DMA modes. They adapt host timers, interrupts and file services; Read ID has no DMA payload, so this is not a DMA-transfer test. The fixture includes FDC/FDD as C-in-C++ for private device access, compiles IMG/D86F/FIFO/CRC as C, and uses portable filesystem temporary directories.

The same fixture checks zero-step SEEK completion with `FDC_FLAG_IRQ_ON_NOOP_SEEK` enabled and disabled, including the DOR interrupt gate and successful Sense Interrupt Status without head movement. The default retains the polled compatibility behavior introduced by commit `e140db7d1` for 386BSD and 1B/V3. The Convertible opts into interrupts because its BIOS power-on recovery waits for IRQ6 even when restoring cylinder zero.

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

# IBM PC Convertible (5140)

Native full-emulator acceptance uses the unmodified IBM BIOS, the 4.77 MHz 80C88 (`cpu_family = 80c88`), 512 KiB RAM, and the two fixed 720 KiB drives. Keep the Marty CPU and new BIU disabled. These guest checks are separate from the isolated PC JX/FDC fixtures and do not qualify other RAM sizes or CPU engines.

The 80C88 uses the shared 8-bit 808x interpreter. Its IRET interrupt-shadow and taken-branch timing changes are compatibility isolation, not established CMOS-only silicon differences; other CPU models retain their previous behavior. Five-clock I/O and stop-clock handling additionally require the Convertible board. Select `80c88` directly; no Convertible CPU migration or compatibility shim is provided. Ordinary XT machines reject the new package.

The model cutover passed original-ROM POST, DOS boot, and `DIR B:`. A BIOS-valid soft-power save preserved a DOS environment variable across separate emulator processes. An ordinary 8088 XT also booted DOS and completed `DIR B:`; the CPU-only contribution independently built and booted an XT after rejecting an ineligible `80c88` configuration. These checks do not qualify the dormant Marty engine or establish broad legacy CPU timing compatibility.

The ARM64 Qt 5 runtime reached the DOS 3.20 `A>` prompt, resident BASIC C1.10 without media, and the StartUp 1.01 menu. Unmodified POST passed the native PIC, all three PIT counters, DMA channels 1–3, keyboard controller, RAM, both floppy drives, and the onboard printer transmitter's register, buffered 8N2 loopback, and IRQ7 checks. DMA channel 0/PIT channel 1 do not generate motherboard refresh.

Advanced Diagnostics 1.11 exercised the system board/clock, 512 KiB memory, a keyboard subset, LCD attributes/fonts/graphics, and drive B sequential seek, random seek, and nine-sector verification on a writable scratch image. This is not a complete keyboard matrix or optional printer/modem/serial qualification; no such peripherals were installed.

Known failure, reproduced on the final build: drive B **Speed Test** reports `0611 TIME OUT ERROR DRIVE B`. Reproduction: Run Diagnostic Routines, confirm the installed-device list, Run Tests One Time, select `6`, enter `4,b`, and confirm a writable scratch disk. The power-qualified drive-select correction did not resolve this diagnostic; ordinary boot/read and the separate seek/verify checks are not evidence that rotational-speed qualification passes.

For SRAM continuation, run a program with a changing in-memory counter, wait until disk activity has stopped, and use **Action → Power off (soft)** or the toolbar power button. The approximately two-second supply cutoff now invokes normal host VM shutdown, saves NVR, and closes 86Box without an exit-confirmation prompt. Relaunch the same VM and verify that the program continues. Repeat the shutdown/relaunch cycle: the exercised counter advanced `1 → 2 → 3` across separate emulator processes, with BIOS-owned `5678h` save signatures. Resume enters RESET/POST and lets the BIOS validate and restore its own context; no CPU snapshot or synthesized BIOS data is used. Pending IRQs may hold the CPU clock awake without invalidating standby SRAM. A genuinely active disk can make the BIOS reject suspend with `0172`; do not bypass that check.

There is one power action, not a separate soft and hard pair. It uses ACPI when the machine has it, otherwise a registered non-ACPI device callback. A machine with neither has no soft-power method, so the action simply exits 86Box: a hard power off is equivalent to quitting the emulator, and the ordinary exit confirmation is the only prompt. Requesting the button before the guest has settled produces a plain supply cutoff with the `5678h` signature cleared, not a resumable save; the firmware, not the host, decides that.

`MOV AL,2` / `OUT 7Fh,AL` requests power-off but clears the suspend-NMI enable bit (`04h`). It must still close the VM after the supply delay; retained physical RAM alone does not imply a BIOS-valid saved context. For a BIOS-managed forced-resume shutdown, use `MOV AX,4201h` / `INT 15h`, which also quiesces disk activity and enables the suspend NMI. The raw request, BIOS call, and UI button all closed the tested VM in approximately 2.5–2.8 seconds including host input and teardown.

Convertible persistence uses one `<machine>.nvr` file: the normal 64-byte RTC prefix, a 16-byte `IBM5140R` version/installed-RAM-size header, all installed main SRAM, then the video SRAM/font payload containing the BIOS's shadow. The existing vendor-save hook appends the payload after every RTC rewrite; shutdown's repeated NVR saves must not truncate it away. With 512 KiB and the internal LCD, the completed file is 548945 bytes. The old `.5140-sram` sidecar is no longer written or read; existing sidecars are left untouched, and the new build should be cold-started before creating a new-format save.

On startup, restore SRAM before POST and consume the appended image while preserving the RTC prefix, so a later hard exit cannot resurrect an old suspend. A one-byte-truncated copy of a proven valid save was rejected, left a 64-byte NVR with its profile intact, and cold-started the probe at `STEP 0`; a subsequent BIOS shutdown saved a valid new image. These are real guest/runtime smoke checks, not isolated CPU or lifecycle unit tests.

Power cutoff is terminal for that emulator instance: late button/alarm activity must not reset the board and invalidate its saved NVR during frontend teardown. Ordinary RTC alarms still run while the VM is running, but automatically starting a closed VM on an RTC alarm would require host/manager scheduling; no such scheduler is provided here.

All three LCD profiles present the same 640×200 binary controller output but with distinct panel colours. Off/background and on/foreground sRGB endpoints, mixed in linear light across 64 optical levels:

| Display selection | Off/background | On/foreground |
|---|---|---|
| `LCD` (original reflective) | `#929C91` | `#515560` |
| `Improved-contrast LCD` | `#A3AD98` | `#494B65` |
| `Backlit LCD` | `#5357A6` | `#B5CECB` |

The backlit panel is effectively the reverse of the reflective panels; that follows from its own endpoints, not from a polarity flag. These are hand-selected healthy-panel approximations from `IBM5140_REF/HANDBOOK_5140/LCD_APPEARANCE.md`, not instrument readings, and they do not model power-off, ambient light, a contrast/brightness control, or panel ageing. 320-mode `01`/`10` remain opposite pixel pairs, not two calibrated intermediate levels.

Panel persistence is modelled as an exponential response in both directions, stepped once per 20 ms frame because the controller is driven at 50 Hz. The time constants start at 100 ms each way: the LGR frame measurement supports roughly 106 ms for darkening on one reflective panel, while lightening and both later panels remain unmeasured tuning parameters rather than IBM specifications. A 100 ms constant gives a 220 ms 10–90% transition. Each pel keeps its own quantized level, so interrupted transitions and software temporal dithering keep their history instead of restarting from the endpoint. Quantizing the level would otherwise stall one level short of an endpoint, so the tables force progress while a pel is short; that costs a bounded one-level-per-frame deviation in the tail. The panel has no separate power-off, backlight-switch or contrast control in the emulator.

A 50×50 native square occupied 100×100 host pixels at 2× scale, with a 1280×400 active rectangle (16:5) by default. The panel is presented as an ordinary 640×200 display: the standard **Force 4:3 display ratio** option applies to it exactly as it does to other 640×200 machines, and no machine-specific aspect handling is imposed. All five external selections cold-booted visibly into BASIC and rendered graphics: 5144 monochrome, 5145 RGB, and old/new/PCjr composite. External selection disconnects the detachable panel's sense input rather than leaving the BIOS drawing to a hidden LCD. Old/new composite colors differed measurably; the PCjr-specific border distinction and physical optical calibration were not established.

# Cartridges

The cartridge tests compile `src/device/cartridge.c` as C and exercise raw and headered image loading, replacement/ejection, overlap and address bounds, and hard-reset reloads. They use real temporary image files with a small external-memory mapping adapter; they do not emulate CPU execution, RAM/BIOS arbitration, or the real memory mapping cache.

Build the `cartridge_tests` target with `BUILD_TESTING=ON`, then run:

```sh
ctest --test-dir build --output-on-failure -R '^CartridgeTest\.'
```

# MicroTouch

The MicroTouch tests exercise `src/device/mouse_microtouch_touchscreen.c` in isolation against the MicroTouch Touch Controllers Reference Guide: serial port, timers and host mouse are mocked, commands go in byte by byte and each byte slot of the controller's transmit timer is stepped by hand. They cover the data formats (Tablet, Decimal, Hexadecimal, Binary, Zone, Raw), the operating modes (Stream, Point, Down/Up, Polled, Inactive, Status), calibration acknowledgements, NOVRAM settings across a power cycle, Restore Defaults and the status commands.

Build `mouse_microtouch_tests` with `BUILD_TESTING=ON`, then run:

```sh
ctest --test-dir build --output-on-failure -R '^MicroTouch\.'
```

# Sony CDU31A / CDU33A

Build `sony_cdrom_tests` with `BUILD_TESTING=ON`, then run:

```sh
ctest --test-dir build --output-on-failure -R '^SonyTest\.'
```

The nineteen tests exercise the Sony and Sony/Creative interfaces through public
I/O handlers, with mocked image access, timers, PIC and DMA. The separate
`tests/cdrom/sony_slcd_smoke.py` boots external DOS drivers in an isolated VM
and compares copied ISO files byte for byte. See the
[Sony hardware guide](../doc/hardware/sony-cdrom.md) for prerequisites, resource
settings, the five-version driver matrix, and validation limits.

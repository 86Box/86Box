# ATI 3D Rage IIC

## Device model

The existing `mach64_rage2c` is the PCI PQFP part, device ID `4756h` (GV).
`mach64_rage2c_agp` is the AGP-connector PQFP part, `475Ah` (GZ). Both retain
the II+ trapezoid/line renderer and front-end scaler, rather than the Pro's
triangle setup engine. The IIC has a fixed 128-entry command FIFO; the II+
retains 48 entries and the Pro retains its selectable capacities.

The IIC ASIC revision is `3Ah`, not the II+'s `9Ah`. ATI's programmer guide
Appendix B lists BIOS ASIC codes `473Ah` for IIC and `479Ah` for II+; these
are distinct from PCI device IDs. The Windows driver recognizes both GV
and GZ without changing its INF.

The AGP IIC uses PCI-66/FRAME transactions on the AGP connector. It reports
66 MHz capability, but does not advertise the Pro's AGP capability list,
sideband addressing, or AGP texture engine. AMD explicitly calls this
configuration "AGP 0X". Adding a Pro capability merely because the card
occupies an AGP slot would misrepresent its hardware.

## Corrections

- IIC initialization selects the memory strap from BIOS metadata: EDO = 2,
  SDRAM = 4. The pre-existing 38816 BIOS entry retains its SGRAM strap = 5.
  `MEM_CNTL` initially matches allocated VRAM: 2/4/8 MiB encode as 3/7/F.
- The IIC driver waits for 128 free entries in `GUI_STAT`. Before the FIFO
  correction, the real driver stalled with EAX = 48 and ECX = 128 during
  the first Direct3D probe. It now proceeds to rendering. `FIFO_STAT` also
  accounts for the larger FIFO.
- Descriptor DMA and end-of-list interrupts are connected for IIC. It uses
  the existing system/GUI transfer engine, including packets split across
  descriptors, without exposing Pro setup, AGP texture, or programmable FIFO
  registers.
- `BUS_CNTL` no longer goes into the generic GT-B register latch. That
  shadow intercepted writes before the actual register's DMA-disable,
  extended-register-window, ROM-mapping, and interrupt behavior could run.
- The old test harness used a removed FIFO constant, obsolete CPU-side
  2D/3D barriers, and 13-bit encodings for the implementation's 14-bit
  horizontal scissors. The tests now use FIFO-ordered dispatch and cover
  II+, IIC and Pro FIFO capacities.

## Supplied BIOS choices

The files remain verbatim dumps in `roms/video/mach64/`; 64 KiB EPROM dumps
retain their upper-half FF padding. The emulated BIOS window maps their
lower 32 KiB. Their declared option-ROM checksums all pass.

| Bus | Selection | Dump size | Memory strap |
| --- | --- | --- | --- |
| PCI | 113-61801-101 (default) | 64 KiB | SDRAM |
| PCI | 113-40605-102 | 64 KiB | EDO |
| PCI | 113-40606-102 | 32 KiB | EDO |
| AGP | 113-52805-101 (default) | 64 KiB | SDRAM |
| AGP | 113-48301-100 | 64 KiB | EDO |
| AGP | 113-48301-100 alternate | 64 KiB | EDO |
| AGP | 113-48303-100 | 64 KiB | EDO |
| AGP | 113-49302-100 | 32 KiB | SDRAM |
| AGP | 113-EV200-H00 evaluation (Gainward dump) | 32 KiB | EDO |
| AGP | 113-EV205-S00 evaluation | 64 KiB | SDRAM |
| AGP | EDO single-cycle 3.096 | 32 KiB | EDO |

The two 48301 dumps differ by 19 bytes and are separate choices. The source
filename `4mbsgram.vbi` is misleading: its own BIOS string identifies EDO
single-cycle memory. The pre-existing 113-38816-100 entry remains available,
but that ROM was not supplied for this validation.

## Validation, 2026-11-04

The branch was first rebased onto upstream master `675bf6644`. Tests used
a separate copy of the Windows 98 VM, not the original disk/configuration.
The machine was an Acorp 6VIA90AP with Celeron 400, 128 MiB RAM, and 8 MiB
video memory for Windows graphics checks.

The supplied 5.30-C9W-WEB package has all files named in its main INF. The
guest's `ATID3D.DLL` matches the supplied file, SHA-256
`02c3568a1ecd7c6570cec8bb5c4de9ea457a1b4fd8d7a9081c9ca5f5ad8fea00`.
The INF matches PCI `4756h` and AGP `475Ah`/`4757h`; the latter BGA ID has
no supplied matching ROM and is not added as a selectable board.

Both PCI and AGP IIC reach the 800x600 Windows desktop. A guest probe changes
to 640x480x16, creates a Direct3D HAL device, uploads a local RGB565 texture,
renders it, then repeats with a managed texture. All logged operations
succeed. RGB555 destination readback is green `03E0h` and red `7C00h`, as
expected. Both probes were repeated with the inherited `ATIDDRAW.INI`
override removed, and still passed. Nonlocal texture memory reports zero
for the AGP IIC, consistent with its lack of AGP texture functionality.

3DMark 99 Max identifies the AGP board as RAGE IIC AGP and renders the race
with road, building, sky, vehicle and HUD textures. This was a visual smoke
check, stopped before a complete benchmark score.

The same local/managed-texture probe also passes on the existing Rage Pro
AGP board with BIOS 113-40212-103 and its existing driver configuration,
including the documented Pro `DDHALAGP=0` setting. This checks the shared
bus-control and memory-initialization changes against the Pro path.

A diskless POST matrix passes all 15 cases: the PCI 61801 and AGP 52805
BIOSes at 2/4/8 MiB, both additional PCI EDO BIOSes at 4 MiB, and all seven
other supplied AGP choices (4 MiB, except EV205 at 8 MiB). Each case checks
reset PCI identity/revision/capabilities, memory type/size, and `BUS_CNTL`
read/write behavior, then reaches the system BIOS's disk-boot-failure
message. These POST checks do not imply Windows coverage for every ROM.

Build and run the focused tests with:

```text
cmake --build build --target 86Box mach64_accel_tests mach64_3d_tests mach64_busmaster_tests
ctest --test-dir build -R "^Mach64\." --output-on-failure
```

All four tests pass: Accel, 3D, Palette, and BusMaster. The 3D suite includes
1,077 draw cases; FIFO status additionally covers II+/IIC/Pro capacities.
The bus-master tests exercise DMA enable gates, partial writes, both system
transfer directions, chains, dirty tracking, end-of-list status, and GUI
register packets split across descriptors, for both IIC and Pro.

## References

- [ATI RAGE PRO and Derivatives Programmer's Guide, PRG-215R3-00-10](https://old.vgamuseum.info/images/stories/doc/ati/prg-215r3-00-10_rage_pro_and_derivatives_programmers_guide_mar00.pdf),
  section 3.3.1, section 8.10, and Appendix B.
- [ATI RAGE 128 controller specifications](https://bitsavers.trailing-edge.com/components/ati/Rage_128/Rage_128_GC_Spec.pdf),
  Appendix B's legacy chip-ID table (GV/GZ/GW/GY package/bus distinctions).
- [AMD article 18872, ATI AGP Graphics Cards: AGP Compliance](https://synthmind.com/737-18872ATIAGPGraphicsCards-AGPComplianceDetails.pdf),
  preserved AMD article, footnote 1 describing IIC FRAME-only operation.

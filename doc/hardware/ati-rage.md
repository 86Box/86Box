# ATI 3D Rage and 3D Rage II

These PCI devices extend the Mach64 implementation. They share its 2D engine,
register-driven 3D engine and front-end scaler with the existing Rage II+ DVD.

| Device | Configuration name | PCI ID | ASIC revision | Memory |
| --- | --- | --- | --- | --- |
| 3D Rage (GT-A2) | `mach64_rage` | `1002:4754` | `40` | 2 or 4 MiB EDO |
| 3D Rage II (GT-B) | `mach64_rage2` | `1002:4754` | `9a` | 2, 4 or 8 MiB EDO |
| Onboard 3D Rage | `mach64_rage_onboard` | `1002:4754` | `40` | 2 or 4 MiB EDO |
| Onboard 3D Rage II | `mach64_rage2_onboard` | `1002:4754` | `9a` | 2 or 4 MiB EDO |

The original GT uses the VT-style video register block and buffer registers.
GT-B uses separate scaler buffer registers and exposes the auxiliary register
BAR. The existing II+ DVD retains its `1002:4755` identity and SGRAM strap.
The Sony VAIO PCV-90 now supplies the original onboard Rage at PCI device 8; its
video BIOS comes from the system firmware.

## Standalone ROMs

ROMs are not included in the source tree. Put these in `roms/video/mach64/`:

| Filename | BIOS | Bytes | SHA-256 |
| --- | --- | --- | --- |
| `3d_rage.bin` | ATI-264GT EDO 113-34003-101 | 32768 | `9eb265c65711889ccb492f8d4c7366d225c4a7f7d1251c8b0f9a090db695137e` |
| `3d_rage_ii.bin` | ATI MACH64 GTB 113-38500-100 | 32768 | `17842db8fa23cf619f8c6197f1600e5205c4c3a91ef541b8558d8bbcc7b8430c` |

The latter may be distributed as a 65536-byte dump with padding. Its ROM
header and PCI data structure specify a 32768-byte image. Both images above
have a zero byte-sum checksum over that length.

## PCV-90 recovery reproduction

Use the Sony Recovery CD (1996), volume label `SONY_96`, with a **new** test
hard disk. Create an active primary FAT16 partition and format it before
launching the recovery utility. The FAT BPB and partition CHS must use the
BIOS-translated geometry (64 heads for this disk), rather than the native
16-head ATA geometry. Mismatched geometry misdirects writes and eventually
produces SMARTDRV's serious disk error. A blank, unpartitioned disk causes the
utility to return to DOS instead of completing the restore.

The reproduction configuration uses a Pentium 200, 32 MiB RAM, onboard sound
and video, a 2 GiB FAT16 disk (4092 native cylinders, 16 heads, 63 sectors), and the
Sony CDU5225 (`sony_5225`) at 52x on secondary IDE master. Boot the CD's
1.44 MiB El Torito image, then choose Complete Restore, Format and Restore
Original Software. Confirm the DOS FORMAT prompt with `Y`.

For headless Windows automation, static Qt builds with `AUTOMATION=ON`
explicitly link and import the offscreen platform plugin. Both the library
dependency and the import in the main translation unit are necessary; an
unreferenced import object inside a static archive can be discarded.

### VAIO Space playback

The recovery CD's June 1996 ATI driver programs the original GT's legacy
video buffer registers. Using a Rage II with this driver leaves its separate
scaler buffer registers unprogrammed, producing green stripes. The PCV-90
uses the original Rage so the factory software can use its expected layout.
Rage II remains available as a standalone card; playback with a later Rage II
driver has not been validated here.

The apparently frozen or blank desktop had a separate cause. The factory
driver sets `CRTC_DISPLAY_DIS` before a display-mode transition and enables
the display again through VGA `ATTRX` bit 5. The emulator now handles this
VGA display-enable write, including when the palette-enable value itself has
not changed. Attribute data writes do not change this blanking state.

With the factory driver and original Rage, the fullscreen startup movie now
plays and returns to the visible registration dialog. The subsequent movie
plays inside the VAIO interface with the surrounding controls visible, and
the Exit controls return to the Windows desktop.

## Validation

The Mach64 accelerator tests cover the original GT overlay as well as VT2
and GT-B. The 3D suite runs against both GT and GT-B (2154 draw cases).
Standalone cards have also reached the PCV-90 BIOS screen, with PCI revision
40/9a and the auxiliary BAR absent/present respectively. Headless startup
has been verified with the Qt 5 static Windows build. Live MMIO checks
verified the original GT's byte/word/dword 3D register readback, the 2 MiB
memory-size encodings on GT and GT-B, and distinct memory above 4 MiB on
the 8 MiB Rage II.

Hardware reference: [ATI-264VT and 3D RAGE Register Reference Guide, 1996](https://www.bitsavers.org/components/ati/RRG-G02700_mach64_Register_Reference_Guide_ATI-264VT_and_3D_RAGE_1996.pdf).

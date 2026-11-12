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
| `3d_rage.bin` | ATI-264GT EDO 113-34003-101 | 32768 | `72cd2246c85f9a8c0df49413024367619e4c1255ea863c81b0f950fe90970f4a` |
| `3d_rage_ii.bin` | ATI MACH64 GTB 113-38500-100 | 32768 | `17842db8fa23cf619f8c6197f1600e5205c4c3a91ef541b8558d8bbcc7b8430c` |

The latter may be distributed as a 65536-byte dump with padding. Its ROM
header and PCI data structure specify a 32768-byte image. Both images above
have a zero byte-sum checksum over that length.

## Rage II ATI3DCIF drivers

Keep the Rage II PCI ID `1002:4754` and ASIC revision `9a`. This combination
is reported by real hardware, including the PCI configuration dumps in
[NetBSD PR 50429](https://gnats.netbsd.org/50429). The Windows driver also
reports the identity as `479a` through ATI's display-driver escape interface.
Changing the revision to `40` makes the card claim to be an original GT;
it does not change its GT-B memory-size encoding or register layout.

The PCV-90 recovery driver belongs to the original Rage configuration above.
Use a Rage II-capable display driver and its matching ATI3DCIF components
for the standalone Rage II. Replacing an ASIC ID check is not a driver update.

A Windows 98 test installation has been verified with the `w82420en.exe`
package: display driver build `4.10.2420`, package `5.21_OEMS_ENU`,
`MACXDD32.DLL` version `4.10.2359`, and `ATI3DCIF.DLL` version `4.10.2690`.
The installed DirectDraw HAL and CIF DLL were checked against the package's
compressed files. The package is available on
[AMD's Rage II driver page](https://www.amd.com/en/support/downloads/drivers.html/graphics/legacy-graphics/rage-series/ati-rage-ii-series.html).
Its INF advertises II+, LT and VT3 models, while the display-driver binary
also recognizes GT/GT-B revision `9a`. The test used the package's `SETUP.EXE`
to update an existing MACXW4 installation; this does not validate automatic
Plug and Play selection of that package on a fresh `4754` installation.
ATI's earlier [4.03.2162 release notes](https://files.mpoli.fi/unpacked/hardware/display/ati/w95-2162.zip/w95-2162.txt)
explicitly list both Rage II and II+ as supported products.

With `9a` unchanged, the tested driver returns interface version `0205` from
escape `73f0`, and `ATI3DCIF_Init` returns zero. MechWarrior 2 ATI 3D Rage
Special Edition installs, reaches Clan Wolf's Nav Computer training mission,
and renders the terrain, sky, other mechs, cockpit and pause menu at 512x384.
This validates this title and driver combination, not every ATI3DCIF game.

The trapezoid renderer also decodes both Bresenham edges' error, increment
and decrement as signed 18-bit fields, as specified by the VT/RAGE register
guide. Treating the full dword as signed made zero-extended negative fields
or reserved high bits change triangle coverage. Regression cases verify
color, depth and continuation in all four directions, including split draws.
This field-decoding fault is reproduced independently of the game; the
captured MechWarrior 2 stream used sign-extended edge values already.

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
not changed. Attribute data writes do not change this blanking state, and
an index write with bit 5 clear does not set `CRTC_DISPLAY_DIS`: the Windows
XP Rage II driver leaves bit 5 clear after restoring the attribute registers
around its BIOS calls and writes `CRTC_GEN_CNTL` back as it reads it, which
blanked its desktop.

With the factory driver and original Rage, the fullscreen startup movie now
plays and returns to the visible registration dialog. The subsequent movie
plays inside the VAIO interface with the surrounding controls visible, and
the Exit controls return to the Windows desktop.

## Validation

The Mach64 accelerator tests cover the original GT overlay as well as VT2
and GT-B. The 3D suite runs against both GT and GT-B (2202 draw cases).
Standalone cards have also reached the PCV-90 BIOS screen, with PCI revision
40/9a and the auxiliary BAR absent/present respectively. Headless startup
has been verified with the Qt 5 static Windows build. Live MMIO checks
verified the original GT's byte/word/dword 3D register readback, the 2 MiB
memory-size encodings on GT and GT-B, and distinct memory above 4 MiB on
the 8 MiB Rage II.

Hardware reference: [ATI-264VT and 3D RAGE Register Reference Guide, 1996](https://www.bitsavers.org/components/ati/RRG-G02700_mach64_Register_Reference_Guide_ATI-264VT_and_3D_RAGE_1996.pdf).

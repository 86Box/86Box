# Creative CD-200 and FUNAI CD200F

Select **MKE** as the CD-ROM bus and either **CREATIVE CD-200 1.01**
(`creative_cd200`) or **FUNAI CD200F 2.10** (`funai_cd200f`). Both are 2x
profiles and work with the existing **MKE/Panasonic interface (Creative)**
and **MKE/Panasonic interface**. Drive IDs 0–3 use the existing MKE channel
setting. The command family is selected by drive model, independently of
the adapter layout. No firmware image is required.

The family names used by the original Linux driver are:

| Drives | Command family | Packet length |
|---|---|---|
| Panasonic CR-52x / CR-521B | CMD0 | 7 bytes, with a separate one-byte status command |
| Panasonic CR-562 / CR-563 | CMD1 | 7 bytes, with a one-byte abort |
| Creative CD-200 / FUNAI CD200F | CMD2 | 7 bytes |
| TEAC CD-55A | CMDT | 10 bytes |

## Evidence

The supplied `CRCCD2.ZIP` contains an OS/2 driver and its README, not drive
firmware. The README identifies CRCCD2.ADD as version 2.00 and explicitly
lists Panasonic CR-52x, Panasonic CR-563 and Creative CD-200 support. The
binary contains both 2.00 and 2.01 version strings.

Eberhard Moenkeberg's original Linux
[sbpcd.c](https://github.com/torvalds/linux/blob/v2.6.12/drivers/cdrom/sbpcd.c)
and [sbpcd.h](https://github.com/torvalds/linux/blob/v2.6.12/drivers/cdrom/sbpcd.h)
identify CMD2 as the CD200 family. They recognize `CD200` and `CD200F`
inquiry strings, with known revisions including 1.01 and 2.10 respectively.
The source also names E2550UA, MK4015 and 2800F as labels found on compatible
FUNAI drives; it explicitly excludes other FUNAI families. These aliases
are not separate emulated firmware profiles.

CRCCD2.ADD's NE code segment starts at file offset `0x0fb0`. Relevant
segment-relative routine offsets, checked against the Linux CMD2 paths:

| Routine | Offset | Wire behavior |
|---|---|---|
| Send command | `0702h` | Writes exactly seven bytes |
| Set speed | `32f4h` | `DAh`, little-endian 150 or 300 in bytes 2–3 |
| Read | `372ah` | `28h`, binary MSF in bytes 1–3, count in 4–5, `02h` in byte 6 |
| Identify CD-200 | `38d4h` | `12h`, 12-byte identification |
| Disc/session information | `3d9eh` | `43 02 AB FF 00 00 00`, 8-byte result |
| TOC entry | `3e1ah` | `43 02 <track> 00 00 00 00`, 5-byte result |
| Last session | `3edeh` | `43 00 AB 00 00 00 00`, same session payload |

The OS/2 driver drains information responses until the result FIFO is
empty, even when its nominal TOC allocation is eight bytes. Information
payloads end with a CMD2 status byte. SBCDNT.SYS 1.14's routine at `13147h`
(preferred image base `10000h`) explicitly treats the last FIFO byte as
status; omitting it discards the last payload byte and prevents mounting.
Linux's fixed-length reads can leave this byte unread. Action commands
return only status: ready `80h`, locked `40h`, spinning `20h`, busy `18h`, door
closed `04h`, disc present `02h`, check condition `01h`.

Linux sends big-endian 150 or `FFFFh` for speed selection; the emulation
accepts both drivers' encodings. Sector transfers stream one sector at a
time, with completion after the last data byte. The implementation also
handles reset, six-byte error results, tray/lock, seek, audio play,
pause/resume, ten-byte sub-Q, audio volume, and the raw-read forms used by
these drivers. Unsupported commands return check condition. Read Capacity
(`25h`) has an eight-byte payload: the last LBA and 2048-byte block length,
both big endian, followed by status.
Creative SBCDNT.SYS 1.14 requires this command and passes its payload
through to the NT CD-ROM class driver. Linux skips it because some firmware
lacks the command and obtains capacity through the TOC instead.
The five-byte TOC-entry response contains ADR/control, track number (or
`AAh` for lead-out), and binary MSF. SBCDNT uses the returned track number;
Linux ignores that byte.

## DOS drivers and configuration

The original Creative [CRCCD.EXE package](https://files.mpoli.fi/hardware/SOUND/CLABS/CRCCD.EXE)
contains `CCD.SYS` 1.07 and `CRCCD.SYS` 1.02. With MS-DOS 6.20 and MSCDEX
2.23, both emulated models passed byte-for-byte copies of 131072-byte and
100003-byte files using `CCD.SYS` on both adapter layouts:

| Interface | CONFIG.SYS line |
|---|---|
| Creative, CD port `230h` | `DEVICE=C:\CCD.SYS /D:CD200 /P:230 /T:2` |
| Standard Panasonic, port `340h` | `DEVICE=C:\CCD.SYS /D:CD200 /P:340 /T:3` |

Add `C:\DOS\MSCDEX.EXE /D:CD200 /L:R` to AUTOEXEC.BAT. These DOS `/P`
values identify the CD port, unlike the OS/2 Creative example below.
`CRCCD.SYS` 1.02 also passed with both models on the Creative adapter.
It reads sector data from the base port and does not support the standard
adapter's separate data port; use `CCD.SYS` 1.07 there.

To reproduce with your own DOS image and ROMs:

```sh
python tests/cdrom/cd200_smoke.py \
  --emulator build/src/86Box --dos-image /path/to/dos.img \
  --nvr /path/to/pb430.nvr --roms /path/to/roms \
  --output /tmp/cd200-creative --driver 1.07=/path/to/CCD.SYS \
  --drive-model creative_cd200 --workers 2
```

Repeat with `--drive-model funai_cd200f` and a different output directory.
The default disk geometry is 63 sectors, 16 heads, 854 cylinders, with a
partition offset of 32256 bytes. Override the geometry and offset for other
images. The script uses disposable copies and requires the same host tools
as `teac_cda_smoke.py`. Vendor drivers and operating-system images are external.

## Windows NT drivers

The original Creative [SB_NT.EXE package](https://ftpmirror1.infania.net/sites/ct_treiber_service/treiber/creative/cdrom/sb_nt.exe)
contains `SBCDNT.SYS` 1.14 (11 August 1995) and `TXTSETUP.OEM`. This driver
recognizes CD-200; the older 1.06 driver in
[SB_NT.ZIP](https://apricot.retropc.se/files/area88/sb_nt.zip) does not.
The [Metropoli SB_NT.EXE mirror](https://files.mpoli.fi/hardware/SOUND/CLABS/SB_NT.EXE)
has a different archive size but contains the same 1.14 driver bytes.

NT 3.5 Workstation build 807 completed both text and graphical installation
from the supplied CD image through SBCDNT.SYS 1.14 and the Creative CD-200.
The test machine was a PB430, 486DX2/66, 16 MiB RAM, with the Creative MKE
adapter at `230h` and drive ID 0. The installed NT kernel, CDFS.SYS and
SBCDNT.SYS were checked byte for byte against their sources. These NT tests
use the Creative adapter layout; the standard layout is covered by DOS
and protocol tests.

Separate copies of the completed installation booted to the NT desktop
with each drive profile. Both copied `I386\NTOSKRNL.EX_` (403792 bytes)
and `I386\CMD.EX_` (69531 bytes) from the CD with exact byte matches to
the ISO. This verifies normal NT filesystem reads as well as installation.

For a fresh NT installation, copy the original NT driver package to a
scratch floppy, select Custom Setup, skip automatic mass-storage detection,
add Other, and select `CD-ROM driver for Sound Blaster Cards`. Use the
package's original `TXTSETUP.OEM`. The supplied NT 3.5 Setup Disk 2 had
`MsDosInitiated=1` in WINNT.SIF; that file was removed from a scratch copy
for the floppy-initiated installation. Original OS images were unchanged.

The supplied NT 3.1 Setup did not detect a drive with unmodified 1.14. Executing its
original DriverEntry together with that NT 3.1 ScsiPortInitialize
confirmed `STATUS_REVISION_MISMATCH` (`C0000059h`): 1.14 supplies a 76-byte
HW_INITIALIZATION_DATA structure, while NT 3.1 requires 64 bytes. The older
1.06 driver uses the older interface but lacks CD-200 support. Thus neither
downloaded driver provides working CD-200 support on NT 3.1. The package's
generic NT 3.1 installation instructions are not evidence that 1.14 works
there; no vendor binaries were patched for these tests.

Driver SHA-256 values:

| File/version | SHA-256 |
|---|---|
| CCD.SYS 1.07 | `6322fd0ee38243057d0426fabb59f4b16e5845acbbb26db9b4cf8267984c63bc` |
| CRCCD.SYS 1.02 | `7f31799e5e9d6574e8cc40ea32ae478aacb93056605032ee542526a06bf79a04` |
| SBCDNT.SYS 1.14 | `57f187036b736327a02a05709194a026185148ff29e6f72fd73dbcd6049393a2` |
| SBCDNT.SYS 1.06 | `26dfb245ab510527ac40978b73936ed9c35579caa4bd9c9e18cca046d0a272a0` |

## OS/2 configuration

For the Creative interface at `230h`, the archive documents
`BASEDEV=CRCCD2.ADD /P:220`. For the standard MKE interface at `340h`, use
the documented MKE-base and standalone switches:
`BASEDEV=CRCCD2.ADD /M:340 /T:2`. The README also describes `/D:x`, `/N:x`
and `/NS`; consult it for the accompanying OS/2 CD-ROM filesystem drivers.

## Validation and limits

Build `86Box` and `mke_cdrom_tests`, then run:

```sh
ctest --test-dir build --output-on-failure -R '^(Mke|Mke2|Teac)Test\.'
```

All 31 tests pass, including eight CMD2 tests covering both profiles and
adapter layouts, mixed CMD1/CMD2 drive selection, exact response framing,
multisession TOC, NT Read Capacity, speed encodings, long and raw transfers, errors, media
change, reset, and audio/tray controls. Media backends and timers are mocked.
Guest checks cover data CDs; CD-DA playback has only protocol-level coverage.

An additional local Unicorn harness executed the archive's original
identification, speed, disc-information, TOC-entry and read routines against
the compiled device implementation for all four model/adapter combinations.
Only OS/2 timing and physical-memory mapping services were substituted;
the driver's actual command construction, port I/O, FIFO polling and status
decoding ran. All four cases returned the expected identity, TOC and two
2048-byte sectors from the mock backend. This is routine-level driver
validation, not an OS/2 installation or filesystem boot test. Physical
hardware timing and undocumented firmware behavior have not been measured.

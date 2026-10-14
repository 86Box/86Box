# Sony CDU31A / CDU33A proprietary CD-ROM (experimental)

Select **SONY CDU31A interface** or **SONY/Creative interface** in Storage
controllers. In Floppy & CD-ROM drives, select the **Sony** bus and either
**SONY CDU31A** (1x) or **SONY CDU33A** (up to 2x). Attach one drive and mount
an image. These are two selectable drive models; a single interface supports
one attached drive. No drive firmware ROM is required.

## Resources and DOS setup

| Interface | I/O base | IRQ | DMA |
| --- | --- | --- | --- |
| SONY CDU31A | 320h, 330h, 340h, 360h | 3, 4, 5, 6, or disabled | 1, 2, 3, or disabled |
| SONY/Creative | 230h, 250h, 270h, 290h | **None** | **None** |

The Sony adapter resource choices follow the supplied **21722.pdf**,
“SONY CORPORATION CDU-31A ADAPTER BOARD,” interrupt/DMA tables on PDF page 1
and base-address table on PDF page 3. The adapter is 8-bit ISA. Disabled IRQ
and DMA are emulator options for polled operation; they are not claimed as
documented jumper settings. The default emulator configuration is 340h,
IRQ disabled, DMA disabled.

The supplied **Sound_Blaster_Pro_Getting_Started.pdf**, printed page 2-1
(PDF page 10), lists the CD-ROM interface as **230h–237h with no IRQ or DMA**.
Those CD-ROM resources are independent of the sound card's audio resources.
The emulated Creative variant offers an address selector for 230h (default),
250h, 270h, or 290h, with IRQ and DMA always disabled. The cited manual
documents the 230h setting.
Select the Sound Blaster separately if desired; choosing the CD interface
does not add a sound card.

Example CONFIG.SYS for the Sony adapter in polled mode:

```dos
DEVICE=C:\SLCD.SYS /D:SONYCD /B:340 /V
LASTDRIVE=Z
```

For Sony/Creative use `/B:230`, `/B:250`, `/B:270`, or `/B:290` to match the
selected address. For DMA on the Sony adapter, configure DMA 3
in 86Box and use `/M:D /T:3` **in that order** after the other options:

```dos
DEVICE=C:\SLCD.SYS /D:SONYCD /B:340 /V /M:D /T:3
```

`/T:` selects the DMA channel; `/C:` does not. Choose resources that do not
overlap other devices. Run MSCDEX in AUTOEXEC.BAT:

```dos
C:\DOS\MSCDEX.EXE /D:SONYCD /L:R
```

Configuration files use `cdrom_interface = sony_cdu31a` or `sony_creative`,
`cdrom_01_parameters = 1, sony`, and `cdrom_01_type = sony_cdu31a` or
`sony_cdu33a`. The Sony interface section is `[SONY CDU31A interface]` with
`base`, `irq`, and `dma`. The Creative section is `[SONY/Creative interface]`
with only `base`, defaulting to `0230`. The formerly unused Sony bus number 2 conflicted
with XTA in shared storage settings; the implemented Sony bus uses 15.
Configuration files persist the bus name rather than that number.

## Windows NT 3.1 Setup

Use **SONY CDU31A interface**, IRQ **5**, and DMA **disabled** with Sony's
`SLCD32.SYS` from [s31x86.exe](https://apricot.retropc.se/files/area88/s31x86.exe).
The tested address is **320h**, also specified by the supplied `TXTSETUP.OEM`.
The README's example registry configuration uses 340h; match the adapter
address to the configuration actually used. The default IRQ-disabled
configuration is suitable for DOS but does not match this NT driver's
requirements. The Creative variant has no CD-ROM IRQ and is not the
configuration tested here.

Copy the driver archive's files to a floppy, boot the NT 3.1 CD-ROM Setup
disk, and choose **Custom Setup**. After the adapter scan, press **S**, select
**Other**, insert the Sony driver floppy, and select **Textsetup for Sony
CDU-31A**. Keep the NT Setup floppy available for subsequent prompts.

NT reads attention codes before acknowledging them; the DOS drivers use
the opposite order. Both orders are supported, including queued attentions.
Previously, NT read FFh instead of the reset attention and then saw a
nonempty FIFO, causing its adapter probe to fail.
The status register also reports the interrupt enables while no event is
pending. Setup polls the miniport ISR before data arrives; the ISR saves
those enables, disables interrupts, then restores them. Reporting only
pending interrupts caused the enables to be lost and the first read to stall.

The October 1993 driver in this archive has an additional automatic-search
bug: its second-drive probe tests the first drive again, then registers
base+4 as another drive. The unchanged binary consequently polls an absent
drive during I/O. This is separate from the two emulator fixes above.
Do not infer complete compatibility from successful adapter detection.

For a single-drive diagnostic copy of this exact binary, changing file
offset 082Bh from `0F BF` to `EB 39` skips the second-drive probe and retains
a drive count of one. Set the four bytes at checksum offset 00D8h to
`BA AA 01 00` (PE checksum `0001AABA`) after the edit and preserve the
original file. This workaround is specific to this driver
and single-drive testing. The binary is not included in the repository.

```text
Original SLCD32.SYS SHA-256:
2cc65c0bf06a3d36df5877189a6359878d208c79cac17d7184c0f0d956e08501
Single-drive copy SHA-256 (including updated PE checksum):
92f55ceeed876f1dc545c1c674e69d5d2ad7b76737b5585f2d9971e4dfef5259
```

With both emulator fixes and the single-drive copy, NT 3.1 Workstation
3.10.511.1 completed text-mode Setup, including all CD-to-disk file copies,
on a PB430 with 486DX2/66 and 16 MiB RAM. The installed miniport matched the
single-drive hash above. The first NT kernel boot initialized the miniport
and reached graphical Setup; the remaining graphical installation was not
run. The original binary passed adapter detection
but failed during I/O because of its second-drive probe. This is a driver
workaround, not a claim that the unchanged vendor binary installs successfully.

## Windows NT 3.5 disc changes

The NT 3.5 `SLCD32.SYS` enables automatic spin-up through mechanical
parameter 05h. Loading a disc now honors that setting: it spins up, reads
the TOC, and queues loading-mechanism (80h), spin-up-complete (24h), and
TOC-read-complete (62h) attentions. With automatic spin-up disabled, loading
still requires explicit spin-up/TOC commands.

Previously, insertion left the disc stopped even with automatic spin-up
enabled. NT received the media-change notification, but subsequent
`TEST UNIT READY` requests continued to report no medium (sense 02/3A/00).

The unchanged x86 driver from NT 3.5 Workstation 3.50.807 was executed in
a Unicorn harness against the Sony emulation with mocked image, timer,
PIC, and SCSI port services. Eject/reinsert, direct replacement, and queued
eject/load passed for both drive models, including repeated image-loader
callbacks, media-change sense 06/28/00, readiness, refreshed capacity, and
sector reads. The same test fails after reinsertion with the previous
emulator code. This is driver-level validation, not a full NT 3.5 guest run.
The driver SHA-256 is
`ff18d453b728f980cc07eba58f0bb58c17070819faa68117f98c4502f2275a07`.

## Implemented behavior

- Command/status, parameter/result FIFO, data, and control registers at base
  +0 through +3; unused addresses in the eight-port window read FFh.
- Ten-byte parameter FIFO and ten-byte result batches, attention queue,
  interrupt enables/acknowledgements, reset, and media-change cancellation.
- Drive identification, parameters, mechanical status, single-session TOC,
  seek, cooked reads, raw reads, abort, spin up/down, and eject.
- Data streaming at 75 or 150 sectors/second according to the mechanical
  parameter and drive model. DMA retains unread bytes while the 8237 is
  masked or reprogrammed, including a terminal count partway through a sector.
- Cooked 2048-byte data, raw 2340-byte data (sync omitted), and 2352-byte
  raw CD-DA transfers through the shared image backend.
- CD-DA play by start/end MSF, pause, sub-Q position, completion attention,
  volume, and channel routing through the shared CD audio mixer. Resume uses
  another play command from the saved position.
- Qt model filtering, bus tracking, media labels, and saved configuration.

The identification response advertises a tray, eject button, LED, 64 KiB
buffer, audio, and separate volume controls. The CDU31A service manual's
block diagram shows two 32 KiB SRAMs. The firmware string `1.0` is an
emulated identification value, not a claim to reproduce a particular ROM.

## Validation

Full-emulator tests use a Packard Bell PB430, 486DX2/66, 8 MiB RAM,
MS-DOS 6.20 and MSCDEX 2.23. Every combination below mounted the generated
ISO, listed its directory, and copied both a 131,072-byte file and a
100,003-byte file with exact byte comparisons against the source data.
The larger copies exercise multiblock reads and DMA memory boundaries.

| Sony driver | CDU31A: Sony PIO / Creative PIO / Sony DMA 3 | CDU33A: Sony PIO / Creative PIO / Sony DMA 3 |
| --- | --- | --- |
| 1.27a | Pass / Pass / Pass | Pass / Pass / Pass |
| 1.61a | Pass / Pass / Pass | Pass / Pass / Pass |
| 1.71a (`SLCDE.SYS`) | Pass / Pass / Pass | Pass / Pass / Pass |
| 1.73a | Pass / Pass / Pass | Pass / Pass / Pass |
| 1.74d | Pass / Pass / Pass | Pass / Pass / Pass |

PIO tests use the driver's default software transfer mode. The main matrix
uses 340h for Sony and 230h for Creative, with IRQ disabled. Successful use
of an older driver with CDU33A does not establish that it selects 2x mode.
IRQ delivery and the other selectable resources are covered by unit tests,
not by this DOS matrix. Additional SLCD.SYS 1.74d runs passed at Creative
addresses 250h, 270h, and 290h with both drive models (six cases), including
both exact file copies and verification of the selected address in the guest.

Driver SHA-256 values (binaries remain external to the repository):

```text
1.27a 34a30f9f7e4cd757042e799affd1efbcf2f53d9602cb161b1c96581108c152ac
1.61a 4f12ba5bebe2559e7bb7aec154997dedd104e0d558c077b820609aa0618a2dc0
1.71a a38ccb922233b5492579863b3e5ac421bb0fac4a1da88771e5b21518d273ea13
1.73a 9887df69c14cab309cd1c4517ce4854147bf4ba17933bc5d21a5b26ada7c71ad
1.74d 61f7372f96111397f3cba10495594630bddd10d782a9c6faf06ccd6d95a2d859
```

### BIN/CUE and audio CD playback

The same five driver versions were tested with both drive models and both
interfaces using two generated BIN/CUE discs (40 combinations):

- A mixed-mode disc containing a MODE1/2352 data track and two CD-DA tracks.
  Both test files copied exactly from the raw-sector image.
- An audio-only disc containing two CD-DA tracks, each ten seconds long.

Every case passed TOC and track queries, play, advancing sub-Q position,
pause with a stationary position, resume, and playback completion at the
requested frame. Four additional cases passed with SLCD.SYS 1.74d and Sony
DMA 3: both disc types on both drive models. These tests use Sony at 340h
and Creative at 230h, with IRQ disabled.

The test captures the CD mixer's buffers at the OpenAL submission boundary
with no emulated sound card or audio filter. Every non-silent captured
buffer matched the generated stereo PCM source exactly. This verifies
digital audio output through the emulator's mixer; physical speaker output
and sound-card-specific filtering are not tested.

These tests exposed and fixed two Sony protocol errors: the audio-playing
flag belongs in bit 4 of the second mechanical-status byte, and the play
command's final-frame address is inclusive. The incorrect status flag made
SLCD.SYS discard its resume state during status polling. The incorrect end
address shortened playback by one frame. The DOS probe acknowledges the
initial disc change through IOCTL 9 before querying the TOC; older SLCD.SYS
versions otherwise keep returning the media-changed error 800Fh.

The twenty-one Sony unit tests exercise public I/O with mocked timers, image
access, PIC, and DMA. They cover both models, FIFO probing/batching, TOC
replies longer than 255 bytes, PIO/DMA continuation, terminal counts,
all selectable addresses/IRQs/DMA channels, errors, short reads, raw-sector
lengths, audio state/routing and inclusive end frames, reset, eject, media
changes, an absent drive, NT's read-before-acknowledge attention handling, and interrupt-enable
readback when Setup polls before a sector is ready, and automatic versus
manual spin-up after insertion with both attention acknowledgment orders.
They pass with AddressSanitizer and UndefinedBehaviorSanitizer; LeakSanitizer
must be disabled in the ptraced test environment. These are protocol and
software integration checks, not physical-hardware validation.

```sh
cmake --build build --target 86Box sony_cdrom_tests
ctest --test-dir build -R '^SonyTest\.' --output-on-failure
```

The related Philips/Hitachi/MKE/CM100/CM153 regression run has one existing
failure: `PhilipsTest.MediaChangeCancelsPartialCommandAndClearsLatchedStatus`.
The same IRQ assertion fails with the unchanged Philips source and fixture
from parent `ce36443e7` (plus the missing UI stub needed to link it).
The Philips fixture now supplies that stub. The tests root also no longer
references the absent `tests/sound` directory, allowing CMake to configure.

### Repeating the DOS matrix

`tests/cdrom/sony_slcd_smoke.py` requires mtools, genisoimage, GNU as/ld, the
PB430 ROMs, a DOS raw disk containing `C:\DOS\MSCDEX.EXE`, and PB430 CMOS
already configured for that disk. It copies the disk and CMOS into a new
output directory; only the copy's CONFIG.SYS, AUTOEXEC.BAT and test files are
changed. Default geometry is 63 sectors, 16 heads, 854 cylinders, with the
FAT partition at byte 32256. Override both geometry and partition offset for
other prepared disks. Paths and drivers are supplied explicitly:

```sh
python3 tests/cdrom/sony_slcd_smoke.py \
  --emulator /path/to/build/src/86Box \
  --dos-image /path/to/pb430-dos.img --nvr /path/to/pb430.nvr \
  --roms /path/to/roms --output /tmp/sony-matrix \
  --driver 1.27a=/path/to/1.27a/SLCD.SYS \
  --driver 1.61a=/path/to/1.61a/SLCD.SYS \
  --driver 1.71a=/path/to/1.71a/SLCDE.SYS \
  --driver 1.73a=/path/to/1.73a/SLCD.SYS \
  --driver 1.74d=/path/to/1.74d/SLCD.SYS
```

Use `--modes creative --creative-base 250` (or `270` / `290`) to exercise
another Creative address.

It saves guest logs, driver hashes, copied data and `results.json`. A small
assembled DOS helper flushes disk writes and creates a completion marker;
the harness then terminates the isolated emulator. This checks guest disk
results, not Qt shutdown. Earlier runs using the unit-tester exit port
occasionally crashed during frontend teardown after correct file copies.
No DOS, Sony driver, ROM, or manual is bundled with the test.

### Repeating the BIN/CUE and audio matrix

`tests/cdrom/sony_audio_smoke.py` accepts the same external emulator, DOS
disk, CMOS, ROM and driver arguments shown above. It additionally requires
a C compiler and OpenAL development headers on a little-endian Linux host.
Use a new output directory, such as `--output /tmp/sony-audio-matrix`.
The default matrix covers mixed-mode and audio-only BIN/CUE images, both
drive models, and Sony/Creative PIO. `--modes dma` selects Sony DMA 3;
`--media audio` or `--media mixed` narrows the disc types. `--workers 4`
runs four isolated guests concurrently. Each worker needs enough space
for a copy of the DOS disk; successful guests' disk copies are removed.

The harness generates valid MODE1/2352 sectors with EDC/ECC and stereo test
tones, assembles `sony_audio_probe.s`, and builds `sony_audio_capture.c` as
an OpenAL capture shim. It saves guest logs, request/IOCTL records, PCM
captures, file copies, driver hashes and `results.json`. A nonzero exit
status identifies a failed assertion. The generated discs use one BIN per
CUE, ordinary INDEX 01 tracks, and no extra pregaps. This matrix does not
establish compatibility with every CUE layout or audio-player application.

## Limits and references

This is a host-protocol implementation. It does not execute Sony firmware
or reproduce mechanical timings, read-ahead RAM, or all error conditions.
Multisession Photo CD, XA ADPCM playback, diagnostic/buffer commands,
UPC/ISRC, and audio-scan modes are not implemented. NT 3.1 Setup testing and
NT 3.5 driver-level disc-change testing are described above; other Windows
versions and Linux guest drivers have not been tested.

- [CDU31A service manual, 9-974-500-11 (1993)](https://theretroweb.com/storage/documentation/9-974-500-11-cdu31a-service-1993-677d4f912e12a059646658.pdf):
  command exercises (including spin-up 51h and read-TOC 30h on printed
  page 3-12), drive hardware and block diagram.
- [CDU33A manual](https://theretroweb.com/storage/documentation/sony-cdu33a-manual-684c741ee97d7007353914.pdf):
  printed pages 20–21 specify 150/300 kbytes/s and supported disc formats.
- Supplied `21722.pdf` and `Sound_Blaster_Pro_Getting_Started.pdf`:
  adapter resources as identified above. The drive service manual alone
  does not establish the standalone ISA adapter's jumper choices.
- [Linux v2.6.12 cdu31a driver](https://github.com/torvalds/linux/blob/v2.6.12/drivers/cdrom/cdu31a.c)
  and [protocol definitions](https://github.com/torvalds/linux/blob/v2.6.12/include/linux/cdu31a.h),
  Corey Minyard, GPL-2.0-or-later: register bits, commands, FIFO batching,
  TOC/sub-Q formats, and raw-sector conventions. Unmodified Sony DOS driver
  execution supplied independent checks of probe and transfer behavior.
- [Microsoft MSCDEX device-driver specification (archived transcription)](https://gist.github.com/abrasive/7a615e6dde0c1da962f9930cc63ee43d):
  request packets and audio IOCTL layouts used by the DOS playback probe.

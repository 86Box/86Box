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

The fifteen Sony unit tests exercise public I/O with mocked timers, image
access, PIC, and DMA. They cover both models, FIFO probing/batching, TOC
replies longer than 255 bytes, PIO/DMA continuation, terminal counts,
all selectable addresses/IRQs/DMA channels, errors, short reads, raw-sector
lengths, audio state/routing, reset, eject, media changes, and an absent drive.
They pass with AddressSanitizer and UndefinedBehaviorSanitizer; LeakSanitizer
must be disabled in the ptraced test environment. Raw-sector and audio checks
are unit tests, not original-driver playback or physical-hardware validation.

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

## Limits and references

This is a host-protocol implementation. It does not execute Sony firmware
or reproduce mechanical timings, read-ahead RAM, or all error conditions.
Multisession Photo CD, XA ADPCM playback, diagnostic/buffer commands,
UPC/ISRC, and audio-scan modes are not implemented. Windows and Linux guest
drivers, original-driver audio playback, and IRQ-driven guest software have
not been tested. A successful DOS data matrix does not qualify those paths.

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

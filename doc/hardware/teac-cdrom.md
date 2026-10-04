# TEAC CD-55A proprietary CD-ROM (experimental)

The CD-55A is a **4x drive with a Panasonic-compatible electrical interface
and a different command protocol**. It is not an IDE/ATAPI drive. The TEAC
DOS driver cannot operate an emulated CR-562/CR-563 merely by selecting a
Creative/MKE adapter: the drive model must also be a CD-55A.

Open **Settings > Storage controllers > CD-ROM controller** and select
**TEAC CD-55A interface (8-bit)** or **TEAC CD-55A interface (16-bit)**.
Use the adjacent **Configure** button to select the card's **Address**.
The 16-bit card requires a machine with a 16-bit ISA slot.
In Floppy & CD-ROM drives select **MKE**, **TEAC
CD-55A**, speed **4x**, channel **0**, and mount a disc image. The existing
**MKE/Panasonic interface (Creative)** and standard **MKE/Panasonic interface**
also accept this drive.

The old ATAPI `teac_55a` profile, labelled `CD 55A`, is a legacy identification
profile, not this hardware implementation. The new model is `teac_cd55a`
on MKE. Existing ATAPI configurations are not migrated. No drive firmware
ROM is required. Revision `1.00` is an emulated identification value, not
an authenticated TEAC firmware dump.

## DOS setup

Use the external `TEAC_CDA.SYS` driver. The archive folder `DOS/1.18`
contains **1.18k**, as identified by its boot banner.

| Interface | CD-ROM I/O base | Driver options |
| --- | --- | --- |
| MKE/Panasonic (Creative) | 230h | `/P:220 /T:0` |
| MKE/Panasonic | 340h | `/P:340 /T:2` |
| TEAC 8-bit | 2C0h (default) | `/P:2C0 /T:1` |
| TEAC 16-bit | 2C0h (default) | `/P:2C0 /T:1` |

For Creative, `/P:` is the **sound-card base**: the driver adds 10h to reach
the CD-ROM registers. A CD interface at 250h therefore needs `/P:240 /T:0`.
For `/T:1` and `/T:2`, `/P:` is the CD interface address itself. These CD
interfaces use polled I/O without CD-ROM IRQ or DMA settings and do not
instantiate a sound card.

CONFIG.SYS for either TEAC card at its default address:

```dos
DEVICE=C:\TEAC\TEAC_CDA.SYS /D:TEACCD /P:2C0 /T:1
LASTDRIVE=Z
```

AUTOEXEC.BAT:

```dos
C:\DOS\MSCDEX.EXE /D:TEACCD /L:R
```

Match `/P:` to the selected emulator address when using a different setting.

Both cards' **Address** dropdowns match the supplied manual's installation
screens: **2C0h, 2E0h, 300h, 320h, 340h, 360h, 380h, 3A0h**, default **2C0h**.
PDF page 5 is headed "TEAC's 8-bit I/F card with Installation disk ver. 2.10
(or higher)"; page 6 covers the 16-bit card.

The **8-bit card's hardware** has a wider range than that installer list.
The note beneath the page 5 table explicitly gives 000h through 3FCh and
instructs users to edit CONFIG.SYS and match the physical switches for other
addresses. Page 4 illustrates 230h. SW1 switches 1 through 8 select address
bits 9 through 2; ON means 1. Existing explicit emulator configurations such
as `base = 0230` still work, but the dropdown follows the eight installer
choices. Choose a free four-port range and match `/P:` to it.

The **16-bit card's** Address selection represents these JP1 positions
(PDF page 6):

| Address | JP1-1 | JP1-2 | JP1-3 |
| --- | --- | --- | --- |
| 2C0h (default) | OFF | OFF | OFF |
| 2E0h | OFF | OFF | ON |
| 300h | OFF | ON | OFF |
| 320h | OFF | ON | ON |
| 340h | ON | OFF | OFF |
| 360h | ON | OFF | ON |
| 380h | ON | ON | OFF |
| 3A0h | ON | ON | ON |

OFF means open; ON means shorted. The emulator applies these settings by
address; no separate switch or jumper entry is needed.

Configuration names are `cdrom_interface = teac_8bit` or `teac_16bit`,
`cdrom_01_parameters = 1, mke`, `cdrom_01_type = teac_cd55a`, and
`cdrom_01_mke_channel = 0`. Resource sections are
`[TEAC CD-55A interface (8-bit)]` and `[TEAC CD-55A interface (16-bit)]`,
with `base = 02c0` by default for both cards.

## Protocol differences

| Operation | Panasonic CR-562/563 | TEAC CD-55A |
| --- | --- | --- |
| Command length | 7 bytes | 10 bytes |
| Reset | 0Ah | C0h; returns 55h signature |
| Identification | 83h | 12h |
| Ready/status | 05h | 00h |
| Read data | 10h, MSF address | 28h, 32-bit LBA, 16-bit count |
| Mode select / sense | 09h / 84h | 55h / 5Ah |
| TOC | 8Bh / 8Ch | 43h, data FIFO plus completion |
| Audio play / pause | 0Eh / 0Dh | 47h / 4Bh |

The TEAC card's command/information port is base+0, status and
information/data selection is base+1, reset is base+2, and drive selection
is base+3. TEAC and Creative adapters multiplex data at base+0; the standard
Panasonic adapter reads data at base+2. The TEAC 16-bit card additionally
reports active-low status bit 7 and supplies two successive data bytes for
a word read at the same port. Renaming an 8-bit Panasonic adapter would
not implement it.

The common MKE transport selects the TEAC command handler by **drive model**,
independently of the adapter. Panasonic families 0 and 1 keep their own
command formats. Four drive IDs are supported, with Creative/TEAC selection
bit ordering and shared TEAC hardware reset.

## Implemented behavior and limits

- Reset signatures, ten-byte command parsing, adapter selection, byte/word
  PIO, and absent-drive polling.
- Identification, readiness, sense errors, modes, 1x/2x/4x reads, TOC/session
  responses, seek, abort, tray control, and software door locking.
- Sector-at-a-time streaming with 16-bit counts larger than the FIFO.
  Cooked 2048-byte and raw 2340/2352-byte reads use the shared backend.
  Reset, abort and media changes cancel pending transfers.
- CD-DA playback with an inclusive final frame, pause, resume by a new play
  command, compact sub-Q, audio status in sense, and channel mute.
  Sub-Q describes the last sector read, including after playback completes.

The linked document contains installation information and specifications,
not a complete programming manual. Command behavior is reconstructed from
the supplied DOS binaries and historical Linux driver. This is driver
compatibility testing, not physical timing qualification. Power-saving
mode bytes are stored without modelling motor timeouts. Arbitrary analog
volume levels, undocumented mode bits, and diagnostic commands are not
claimed. Unknown commands return sense key 5 / ASC 20h.

## Validation

Before implementation, MS-DOS 6.20 / MSCDEX 2.23 with a CR-563 on Creative
230h or standard Panasonic 340h failed with all four supplied driver
versions: 1.10g, 1.11h, 1.15i, 1.18k. Version 1.18k printed
`[CD-ROM not found]`; MSCDEX could not find its device. Version 1.15i timed
out. TEAC-card `/T:1` probes against Creative 230h also failed.

The test machine is a Packard Bell PB430, 486DX2/66, 8 MiB RAM. The reusable
`tests/cdrom/teac_cda_smoke.py` boots unmodified external drivers and checks
131,072-byte and 100,003-byte file copies against generated contents. Its
mixed-mode/audio-only fixtures exercise MSCDEX TOC/track requests, play,
advancing sub-Q, pause, stationary sub-Q, resume, and completion. OpenAL
submission buffers are compared with generated stereo PCM. Physical
speakers and sound-card-specific filters are not tested.

All **48 DOS runs passed** on the master-based TEAC branch: four driver
versions, four interfaces, and three media fixtures (ISO data, mixed-mode,
and audio-only). The smoke harness includes its MSCDEX probe and fixture
helpers and does not require another feature branch.

| TEAC_CDA.SYS | Creative 230h | Panasonic 340h | TEAC 8-bit 2C0h | TEAC 16-bit 2C0h |
| --- | --- | --- | --- | --- |
| 1.10g | 3/3 pass | 3/3 pass | 3/3 pass | 3/3 pass |
| 1.11h | 3/3 pass | 3/3 pass | 3/3 pass | 3/3 pass |
| 1.15i | 3/3 pass | 3/3 pass | 3/3 pass | 3/3 pass |
| 1.18k | 3/3 pass | 3/3 pass | 3/3 pass | 3/3 pass |

Earlier runs also passed all three media fixtures with all four driver
versions on the 8-bit card at 230h. Both configuration dialogs were checked
for exactly eight choices, the 2C0h default, and saving/reopening each address.

This includes exact file comparisons in all 32 data-bearing runs and
audio control/PCM checks in all 32 audio-bearing runs. Guest tests used one
drive at ID 0 and the addresses above. Multiple drive IDs and raw-sector
lengths were checked in isolated device tests. Apart from the additional
8-bit 230h runs, other address choices and
Windows, OS/2, and Linux guest drivers have not been boot-tested.

The 23 isolated device tests (13 Panasonic, 10 TEAC) pass under
AddressSanitizer and UndefinedBehaviorSanitizer, with leak detection disabled
in the sandbox. They cover command framing, both byte-port layouts, word
byte order, read counts exceeding FIFO capacity, raw-sector lengths, speed
selection, TOC allocation, sense errors, media changes, reset, abort,
drive selection, all hardware switch/jumper addresses, audio endpoints and
door locking. These mocks do not substitute for the full DOS runs.

Driver SHA-256 values:

```text
1.10g 7f51ab5b736862bc5619d39e903337bed16cb01c181fea15e17b41c49105274e
1.11h 5313082da621f47cb38650182fed1f528b9b6841c17521a47ce9f2b3153a9570
1.15i 25f4620a058357f919459edf9b2f61b3830ccea995c9a8d43d84376d11c3f863
1.18k bd9ad6954c1d0ae55fb15452ba64acf1f43e1303275503713ef324fe7fcca673
```

Reproduction (supply the ROMs, prepared CMOS, DOS disk and driver):

```sh
cmake --build build --target 86Box mke_cdrom_tests
ctest --test-dir build -R '^(Mke|Teac)Test\.' --output-on-failure
python3 tests/cdrom/teac_cda_smoke.py \
  --emulator build/src/86Box --roms /path/to/roms \
  --dos-image /path/to/dos.img --nvr /path/to/pb430.nvr \
  --driver 1.18k=/path/to/TEAC_CDA.SYS \
  --media iso mixed audio --workers 2 --output /tmp/teac-results
```

Default raw-disk geometry: 63 sectors, 16 heads, 854 cylinders; partition
offset 32256. Override `--geometry` and `--partition-offset` as needed.
`--drive-model cr563 --modes creative panasonic` reproduces the incompatible
Panasonic-drive comparison. Scratch disks use sparse copies. Successful
disks are removed after outputs are extracted; failed disks remain for
diagnosis. Vendor drivers, DOS, ROMs, and PDFs are not redistributed.

## Sources

- [TEAC CD-55A/CF506 information supplied by the user](https://theretroweb.com/storage/documentation/5cd0020a-65c8f79643e26133386949.pdf):
  PDF page 4, 8-bit address diagram; pages 5-6, installation choices and
  16-bit jumper matrix; page 14, specifications (600 KB/s sustained,
  2.3 MB/s burst, 64 KB buffer). Version 2.10 on page 6 identifies the
  **installation software**, not a verified drive firmware revision.
- TEAC `DOS/cd55adoc.txt` and `DOS/1.10g/README.NOW` from the supplied
  `cd55a-driver-archive-65c928ead335e737493978.zip`: Panasonic hardware
  compatibility, card types, `/P:` and `/T:`, and the required TEAC driver.
- [Linux v2.6.12 sbpcd.c](https://github.com/torvalds/linux/blob/v2.6.12/drivers/cdrom/sbpcd.c)
  and [sbpcd.h](https://github.com/torvalds/linux/blob/v2.6.12/drivers/cdrom/sbpcd.h),
  principally Eberhard Moenkeberg's CMDT paths: commands, reply layouts,
  adapter offsets, `Teac16bit`, and word-transfer detection.
- Supplied DOS binaries, especially 1.18k file offsets 0C54h (ports),
  0CEEh (transport), 0F21h (55h detection), 0FD1h (reset), 1251h (mode),
  132Ah (read), 1419h (TOC), and 1AF5h (inclusive audio endpoint).

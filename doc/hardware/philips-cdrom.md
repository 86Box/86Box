# Philips/LMS CM250 with CM205 or CM205MS (experimental)

The 8-bit ISA CM250 adapter supports two separate emulated 1x drive models:
**original CM205** and **CM205MS**. Both implement CD-DA playback and raw
Mode 1 / Mode 2 XA sector reads. Multisession behavior belongs to CM205MS;
the original CM205 exposes only the first session. No firmware ROM is required.
CM206 and the 16-bit CM260 adapter are not implemented by this device.

## Configuration

In **Storage controllers**, select **Philips/LMS CM250**, normally **340h /
IRQ 5**. In **Floppy & CD-ROM drives**, select **Philips/LMS** and either
**PHILIPS CM205** or **PHILIPS CM205MS**. Enable CD audio and mount a BIN/CUE
containing audio tracks to play CD-DA. Use one drive; speed is fixed at 1x.

Match the DOS driver to the selected drive model:

| Drive | Tested driver | CONFIG.SYS device line |
| --- | --- | --- |
| CM205 | CM250.MSC 1.00, October 3, 1991 | `DEVICE=C:\CDROM\CM250.MSC /D:MSCD001 /P:340 /I:5` |
| CM205MS | DD250MS.SYS 3.30, August 9, 1993 | `DEVICE=C:\CDROM\DD250MS.SYS /D:MSCD001 /P:340 /I:5` |

Also set `LASTDRIVE=Z` in CONFIG.SYS and run this in AUTOEXEC.BAT:

```dos
C:\DOS\MSCDEX /D:MSCD001 /M:4 /L:D
```

The original and MS drivers use different command families and are not
interchangeable. Use an AT/286 or newer with these drivers; the executables
contain 80186-family instructions. The validated guest is DOS 5.00 with
MSCDEX 2.22. Windows multimedia applications and Linux drivers have not been
validated. CM205MS is a later model, unsuitable for a strictly early-1992 setup.

The adapter occupies eight I/O addresses. Selectable bases are 300h, 310h,
330h and 340h, with IRQ 3, 4, 5 or 6. Keep those resources free of other devices.
The earlier Mode 1 tests also passed at 330h / IRQ 3. This adapter is separate
from a Sound Blaster's proprietary CD-ROM interface.

## Implemented behavior

- Byte-I/O command echo, response retrieval, readiness, IRQ acknowledgement,
  adapter reset, drive-reset handshake and selected-channel status.
- Original CM205 identification, status, seek, reads and continuation, FIFO TOC,
  CD-DA play (`B1h`), pause (`EAh`), routing (`C5h`) and current audio position.
  Its DOS driver resumes by issuing another play command from the saved position.
- CM205MS binary-address command family: seek/read, play/pause, audio routing,
  current Q and streamed lead-in Q, disc/audio/drive status and reset completion.
  Responses use `F8h`; commands can produce a separate completion byte.
- Raw 2352-byte Mode 1 and Mode 2 Form 1/Form 2 transfers at 75 sectors/second.
  A pending sector remains available until the guest consumes it. Audio tracks
  are excluded from the data-read path. XA ADPCM decoding is outside this change.
- Original CM205 first-session TOC/capacity/read limits. CM205MS reports the
  last session and preserves the links between sessions. For CUE images that
  omit the final B0 link, an end-of-disc link terminates the session chain.
- Media changes cancel partial packets, pending completions, reads and audio.
  Failed/short reads do not expose old data. Audio uses the shared CD backend
  and mixer callbacks.
- Separate model selection and configuration persistence in Qt.

The Philips storage-settings bus ID is 13; its previously unused ID 1 collided
with MFM. Configuration files store the name `philips`.

## Validation

Tests use separate temporary boot floppies in an isolated IBM AT, 286 at
6 MHz, 512 KB. Existing VM configurations and disks were not modified.

| Test | Result |
| --- | --- |
| CM205 and CM205MS, ISO9660 | Directory and exact 100,003-byte file copy |
| CM205, mixed-mode MODE1/2352 | Directory and exact 83,774-byte file copy (earlier test) |
| Both models, MSCDEX CD-DA requests | Play, advancing Q position, pause with stable position, resume |
| Both models, captured CD audio buffer | Nonzero 17,640-byte PCM block exactly matches the mounted BIN |
| Both models, direct DOS adapter-I/O probe | Exact 2352-byte XA Form 1 and Form 2 sectors |
| CM205MS, MSCDEX cooked XA read | Exact 2048-byte primary volume descriptor |
| Original CM205, two-session Mode 1 fixture | Only first-session directory visible |
| CM205MS, two-session raw XA fixture | Second-session directory and exact added-file copy |

The mixed-mode fixture is the local Tomb Raider BIN/CUE, used for protocol
validation rather than as a period-correct software recommendation. XA tests
use a small copy of the local Theme Hospital image, with one generated Form 2
sector. No original media files were edited. Audio capture checks the emulator's
PCM buffer, not physical speaker output or analog volume accuracy.

`philips_cdrom_tests` contains 21 tests covering both protocols, reset,
media removal, invalid packets/ranges, sector pacing and continuation, TOC,
session links, XA transfer selection, CD-DA status and mixer routing. All
45 Philips/Hitachi/MKE tests pass. Philips AddressSanitizer and
UndefinedBehaviorSanitizer runs also pass; LeakSanitizer is disabled because
of interference from the execution environment.

```sh
cmake --build build --target 86Box philips_cdrom_tests hitachi_cdrom_tests mke_cdrom_tests
ctest --test-dir build -R '^(PhilipsTest|HitachiTest|MkeTest)\.' --output-on-failure
```

Six pre-existing Mitsumi test failures reproduce on the unchanged parent.
Mitsumi code is not modified.

## Limits

This is a behavioral implementation, not firmware or cycle-accurate emulation.
The real adapter's full read-ahead RAM, interrupt-mask details, test registers,
error fidelity and mechanical timings remain incomplete. Guest eject/load and
UPC are unsupported; CM205MS lock/unlock currently maintains reported state.
Volume scaling and routing lack comparison with physical Philips hardware.
XA sector support does not imply that every original DOS driver exposes cooked
XA reads, or that XA ADPCM audio is decoded.

Multisession image formats must preserve session boundaries; a flat ISO cannot
do so. Testing uses generated two-session media, not a physical Photo CD dump.
DD250MS probes later-session volume descriptors at the XA data offset; with
Mode 1 sessions it can scan the TOC but still mount the first-session directory.

## References

- [Philips DOS archive](https://files.mpoli.fi/hardware/CDROM/PHILIPS/PHILIPS.ZIP):
  original CM250.MSC 1.00 and configuration documentation.
- [LZ250MS README](https://files.mpoli.fi/unpacked/hardware/cdrom/philips/lz250ms.zip/_lz250ms.exe/read.me):
  later CM205 generation and the multisession driver.
- [Linux CD-ROM driver archive](https://ibiblio.org/pub/Linux/kernel/patches/cdrom/):
  `lmscd0.4.tar.gz`, `cm205cd.0.5a.tar.gz` (Kai Petzke / Martin Seine), and
  `linux-2.0.30-cm205ms-0.10.tgz` (Sudarshan Bhat's CM250 support in David
  van Leeuwen's CM206 driver), GPL-2.0-or-later.

Unmodified DOS driver register traces and MSCDEX probes independently validate
the implemented command formats. Precise hardware timing and untested commands
are not established by these sources.

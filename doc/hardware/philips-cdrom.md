# Philips/LMS CM250 + original CM205 (experimental)

This implements the host interface of the 8-bit ISA CM250 adapter and the
original, non-multisession CM205 at 1x. It is an initial data-reading
implementation. CM205MS, CM206 and CM260 use a different command family and
are not represented by this device. No firmware ROM is required or executed.

## Configuration

In **Storage controllers**, select **Philips/LMS CM250** as the CD-ROM
controller. Configure **340h / IRQ 5**. In **Floppy & CD-ROM drives**, select
the **Philips/LMS** bus and **PHILIPS CM205**, then mount a Mode 1 image.
Use one Philips drive; there is no selectable drive channel or speed.

Use the original **CM250.MSC version 1.00, October 3, 1991**, from PHILIPS.ZIP:

```dos
REM CONFIG.SYS
LASTDRIVE=Z
DEVICE=A:\CM250.MSC /D:MSCD001 /P:340 /I:5
```

```dos
REM AUTOEXEC.BAT
MSCDEX /D:MSCD001 /M:4 /L:D
```

Match `/P:` and `/I:` to the controller settings. The adapter occupies eight
I/O addresses. Available bases are 300h, 310h, 330h and 340h; IRQs are 3, 4, 5
and 6. Avoid a resource already used by the sound card, serial port or floppy
controller. Testing also passed at 330h / IRQ 3. These settings do not select
a Sound Blaster CD-ROM interface.

Use an AT-class guest for this driver. Its executable contains 80186-family
instructions, including PUSH immediate, LEAVE and REP INSB; the card being
8-bit ISA does not establish compatibility with an 8088 and this DOS driver.
The validated combination is DOS 5.00 and MSCDEX 2.22. Older DOS/MSCDEX
versions and Windows multimedia applications have not been validated.

## Implemented behavior

- Byte-I/O command echo and response retrieval, receive/transmit readiness,
  interrupt acknowledgement, adapter reset and drive-reset error handshake.
- Identification (`2Dh`), disc/position status (`3Ah`), error clear (`4Eh`),
  seek/stop probe (`59h`), Mode 1 reads (`A6h`) and read continuation (`A7h`).
- TOC (`E5h`) through the adapter FIFO and track-ready interrupt. Command
  addresses use BCD frame/second/minute; FIFO TOC positions use binary values.
- Raw 2352-byte sector transfer to the driver, paced at 75 sectors/second,
  retaining an unread sector until the guest consumes it.
- Media-change notification cancels pending transfers and partial commands;
  errors, reset and failed/short backend reads do not expose stale sectors.
- Original-CM205 session restriction: track range, TOC requests, reported
  capacity and read limits use the first session known to the image backend.
- Qt settings, configuration persistence and media-menu integration.

The Philips bus receives storage-settings ID 13. Its previous unused ID 1
collided with the shared MFM hard-disk bus ID. Configuration files persist
the name `philips`, rather than this numeric ID.

## Limits

CD audio playback, pause/resume, audio routing, XA/Mode 2, subchannel/UPC
queries, guest eject/load/lock commands and diagnostic fidelity are not
implemented. **This build is not yet a complete MPC multimedia CD drive.**
Recognizing audio tracks in a mixed-mode TOC does not establish audio playback.

Timing, error details and identity bytes are partly inferred from published
driver code and the original DOS driver. There is one retained sector rather
than a cycle-accurate model of the real adapter's FIFO/read-ahead behavior.
Test registers are minimally handled. Physical-hardware comparison, Linux
driver validation and a complete interrupt-mask model remain future work.
The first-session restriction has a synthetic two-session backend unit test;
no physical multisession disc dump has been validated in a guest.

## Validation

An isolated IBM AT, 286 at 6 MHz, 512 KB, ran the unmodified CM250.MSC and
MSCDEX. Guest writes went to separate temporary DOS boot floppies. Existing
VMs and their disks were not modified.

| Image / adapter setting | Verified guest result |
| --- | --- |
| ISO9660, 340h / IRQ 5 | Directory listing and exact 100,003-byte PATTERN.BIN copy |
| Mixed-mode MODE1/2352 BIN/CUE, 330h / IRQ 3 | Directory listing and exact 83,774-byte HMIDET.386 copy |

SHA-256 of the copies matched the host files:

```text
PATTERN.BIN  8616347c14b23deba6f62767f748baf7a8eae2407b4069bdc823b5b91b2c9daa
HMIDET.386   6a5f9b019586607995609040857eeafc143ce90e83a2d0b3d1ec23bad81ee932
```

The BIN/CUE is the local Tomb Raider image, used as a protocol fixture, not
as a historically appropriate title for the 1991 drive. A DOS device-IOCTL
probe checks volume size, track range, lead-out and data/audio track positions.
The expected disc has 165,951 logical sectors, tracks 1–10, lead-out 36:54:51,
track 1 at 00:02:00 (data) and track 2 at 19:26:55 (audio).

`philips_cdrom_tests` exercises the public device/I/O callbacks. Tests cover
the original driver's reset/identification sequence, pacing and retained data,
read continuation, TOC framing and binary positions, first-session bounds,
media changes, invalid addresses, short/failed reads, reset and teardown.
AddressSanitizer/UndefinedBehaviorSanitizer are also used; leak detection is
disabled because the execution environment interferes with LeakSanitizer.

```sh
cmake --build build --target 86Box philips_cdrom_tests
ctest --test-dir build -R '^PhilipsTest\.' --output-on-failure
```

Hitachi and MKE controller regression tests pass. Six existing Mitsumi tests
fail identically on the unchanged parent revision and on this branch; the
Mitsumi controller was not modified by this patch.

## References

- [Philips DOS driver archive](https://files.mpoli.fi/hardware/CDROM/PHILIPS/PHILIPS.ZIP):
  CM250.MSC 1.00 and CM250.CNF. DOS register traces and device IOCTL results
  provide independent checks on the protocol implementation.
- [Original CM205 Linux drivers](https://ibiblio.org/pub/Linux/kernel/patches/cdrom/):
  `lmscd0.4.tar.gz` and `cm205cd.0.5a.tar.gz`, Kai Petzke / Martin Seine,
  GPL-2.0-or-later. The original Linux driver does not implement CD audio.
- [Philips archive listing](https://files.mpoli.fi/unpacked/hardware/cdrom/philips/):
  OS/2 driver documentation gives CM250 resources; LZ250MS documents the
  later CM205 multisession generation. Those later drivers are not the test driver.

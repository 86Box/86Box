# Hitachi CD-IFI4-A / CDR-1503S (experimental)

This is a high-level, PIO-only implementation of the proprietary Hitachi CD-ROM
interface. It reconstructs the host-visible protocol used by HITACHI.SYS 1.02
and HITACHIA.SYS 2.10. It does not execute a drive firmware ROM. HITACHIB.SYS
is the Micro Channel driver and is not supported by this ISA controller.

## Configuration

In **Storage controllers**, select **Hitachi CD-IFI4-A** as the CD-ROM controller.
Configure its I/O address (default **300h**). In **Floppy & CD-ROM drives**, select
the **Hitachi** bus, **HITACHI CDR-1503S**, and channel **0:0** for the first drive.
Mount a Mode 1 data CD image. The controller supports four drive addresses.

For one drive, use the original DOS driver:

```dos
DEVICE=A:\HITACHIA.SYS /D:MSCD001 /N:1 /P:300
```

For version 1.02, replace `HITACHIA.SYS` with `HITACHI.SYS`. Match `/P:` to the
controller's address. Configure sufficient `LASTDRIVE` letters, then run:

```dos
MSCDEX /D:MSCD001 /M:4 /L:D
```

MSCDEX 2.10 from the supplied Hitachi v2.10 disk works with both drivers under
PC DOS 3.10. The MSCDEX 1.01 on the v1.02 disk recognizes High Sierra discs;
it reports CDR103 for the ISO9660 fixture used in validation.

## Implemented path

- 8255 mode-0 command/reply strobes, port-C drive selection and PIO data port.
- BCD MSF seek and streaming Mode 1 reads, stop/reset, ready/error state.
- Single-speed sector pacing (75 sectors/second) and consumed-sector acknowledgement.
  Cooked reads discard the remainder of the 2340-byte sector after its four-byte
  header and 2048 user bytes. Unread sectors are retained until acknowledged.
- Media-change callback cancels pending reads and latches a change indication.
- Basic disc length and lead-in Q replies reconstructed from the DOS driver.
- Config persistence and Qt bus/channel/model selection.

The interface occupies 16 I/O addresses at one of 200h, 220h, 240h, 260h, 300h,
320h, 340h or 360h. These choices and the four-drive limit come from Hitachi's
v1.02 README. No IRQ or DMA is used by this implementation.

## Limits

This is an experimental data-reading implementation, not full firmware or
electrical emulation. CD audio playback, audio routing, XA/Mode 2, DMA transfers,
eject/load commands and diagnostic/identification packet contents remain
unsupported. Lock state and power-save commands have only minimal handling;
there is no power-save timer. Error flags and latency are partly inferred from
driver behavior, not physical hardware traces. General 8255 modes are not
emulated. Actual CD-IFI4-A / CDR-1503S hardware comparison is still needed.

No firmware image is embedded or required for this device. The host card photo
shows a NEC D8255AC-5, discrete logic and no visible option ROM on its component
side. This does not establish the absence of firmware inside the drive.

## Validation

Both original SYS files booted in IBM XT (1986), 8088 at 4.77 MHz, and IBM AT,
286 at 6 MHz, using PC DOS 3.10 and MSCDEX 2.10. Each listed an ISO9660 directory
and copied a deterministic 100,003-byte file whose SHA-256 matched the host:
`8616347c14b23deba6f62767f748baf7a8eae2407b4069bdc823b5b91b2c9daa`.
The drivers were neither modified nor bypassed. Original disk images were
accessed read-only; guest writes went to separate temporary boot images.

A Tomb Raider MODE1/2352 BIN/CUE was also tested with driver 2.10 on the XT
at 300h and driver 1.02 on the AT at 340h. Both copied the 83,774-byte HMIDET.386
file with matching SHA-256
`6a5f9b019586607995609040857eeafc143ce90e83a2d0b3d1ec23bad81ee932`.
A small DOS program queried device IOCTL 8 (volume size) and 10 (audio disc
information). These return 165,951 logical sectors, tracks 1–10 and lead-out
36:54:51 on that image. These queries do not test audio playback.

`hitachi_cdrom_tests` exercises the public device/I/O callbacks, including
handshakes, read timing, cooked/raw sector acknowledgement, stop/reset, invalid
MSF, end-of-disc and backend errors, media changes, drive isolation, absent
drives and teardown. It also runs under AddressSanitizer/UndefinedBehaviorSanitizer.

```sh
cmake --build build --target 86Box hitachi_cdrom_tests
ctest --test-dir build -R '^HitachiTest\.' --output-on-failure
```

## Research sources

- Supplied Hitachi v1.02/v2.10 IMA files: README and unmodified DOS SYS drivers.
- [CD-IFI4-A component-side photos](https://electronic.acca3.it/cd-rom-controller-hitachi-ifi4-a-isa-8-bit-1988/).
- [Archived Hitachi HELP-CD](https://driverzone.com/drivers/hitachi/cdrom/hitachi.htm).

# PC-9801VX system speaker and storage audit

Reviewed and updated on 2026-10-01, after the upstream floppy merge recorded
in [pc98-storage-upstream-merge.md](pc98-storage-upstream-merge.md).

## System speaker

The shared audio core already created an IBM PC speaker, but the PC-98
motherboard supplied neither its waveform nor its enable signal. The VX
speaker therefore had no working audio path.

The PC-98 architecture now selects a dedicated speaker device using the
shared mixer, speaker mute, volume and filtering controls. Its digital input
is the actual PIT channel 1 OUT signal, gated by active-low system PPI port C
bit 3 (BUZ). BSR writes 06h/07h at port 37h enable/disable it; writes to port
35h also update BUZ. The existing shutdown flip-flops retain their behavior.

Both the 73h/77h PIT decoder and the 3FDBh/3FDFh mirrors reach the same PIT.
The existing VX clock is 1.9968 MHz; a mode-3 count of 998 produces about
2 kHz. Pin changes flush the old audio state before storing the new levels.
This avoids applying IBM channel-2 count and PWM shortcuts to the PC-98.
Full motherboard reset silences the speaker. PIT teardown now cancels its
callback timer in both compiled implementations before freeing the device.

Hardware basis:

- NEC *PC-9800 Series Technical Data Book: Hardware* (1993), printed p. 124
  (PDF p. 137), describes speaker frequency programming on counter 1,
  including the 998/1229 counts for the two clock families:
  https://vtda.org/docs/computing/NEC/PC-9800TechnicalDataBookHARDWARE%2BOCR_1993.pdf
- *PC-9801 Programmer's Bible* documents system PPI BUZ and the 06h/07h BSR
  controls: https://pc98.ne.jp/devdocs/pc-9801programmersbible.pdf

The implementation models the VX programmable speaker. Earlier machines
whose counter 1 supplies memory refresh need their separate fixed buzzer
wiring if those machines are added. No acoustic response measurements were
made; this is digital wiring plus the existing host audio filtering.

## SASI and native disk containers

- Invalid or unreadable HDI/NHD containers now fail attachment explicitly.
  They no longer fall back to the generic raw loader, which could expose
  the container header as disk sectors. Other file extensions retain the
  generic image path. `pc98_hdi_open()` returns 1 for success, 0 for another
  file type, and -1 for an invalid/unreadable native container.
- Header validation rejects payload offsets inside the header and geometries
  whose native block count exceeds the 32-bit API. Native accesses use the
  same 64-bit seek/tell interfaces as the generic image layer.
- Native writes propagate `fflush()` failures. The generic image layer now
  rejects short reads, incomplete VHD transfers and failed write flushes,
  allowing both SASI and common SCSI targets to receive I/O failures instead
  of consuming old staging-buffer data or reporting an incomplete write as
  successful.
- Generic bounded read/write/zero helpers now convert the inclusive last LBA
  to the correct capacity and reject requests starting past it before
  subtracting. They previously excluded valid final sectors and could wrap
  the available-sector calculation. Zero-fill operations also propagate
  short-write, VHD and flush failures.
- SASI callbacks check unit, count and physical backing bounds before LBA
  arithmetic or staging-buffer access. Physical backing capacity is stored
  separately from the capacity exposed by the selected BIOS drive profile.
  Existing odd-half-sector preservation and 33-to-17 track translation remain.
- Native geometries that cannot supply the required logical track, and
  generic images exceeding the block-address range, are closed and detached.
- While SASI reset is held, target REQ/BSY/ACK and completion IRQ are cleared,
  unfinished transfers are discarded and data-port writes are ignored.
  Releasing reset resets the target while preserving the adapter control
  value written by the guest.
- Routine data, DMA and port traces are opt-in with `ENABLE_PC98_SASI_LOG`.
  Attachment diagnostics remain enabled.

## SCSI: PC-9801-55 / WD33C93

- Every target command starts with `buffer_length = -1`, as required by the
  common target API. Previously a short INQUIRY or REQUEST SENSE could leave
  a length limit that truncated subsequent disk transfers.
- OWN ID bits 7:6 are clock selection bits. They no longer cap the CDB length,
  which previously rejected standard ten/twelve-byte commands with some
  clock settings. The common target API still limits accepted CDBs to twelve
  bytes.
- TRANSFER INFO now honors SBT and zero-count single-byte operation in data,
  command and message phases. A successful single-byte transfer clears the
  transfer count. DMA handles this without subtracting from a zero count.
- PIO command bytes accumulate across successive TRANSFER INFO commands,
  including single-byte commands. A partial CDB is retained; a new selection
  clears it. Execution starts when the expected command bytes have arrived.
- Completed target data phases clear remaining transfer state before the
  pending status phase. The existing status helper already cleared DBR.
- Routine register/byte tracing is opt-in with `ENABLE_PC98_SCSI_LOG`.
  Initialization, ROM and configuration errors retain their diagnostics.

The single-byte and clock-bit fixes follow Western Digital's *WD33C93A SCSI
Bus Controller* datasheet, sections 6.2.3, 6.2.16 and 6.2.20:
https://www.bitsavers.org/components/westernDigital/WD33C93A_SCSI_Bus_Controller_1991.pdf

## Floppy

Reviewed the instanced drive ownership, PC-98 bank routing, D88 parser,
sector access, writeback and close paths alongside the preceding merge fixes.
D88 writeback now clears dirty state only after both the write and flush
succeed; failure is logged and the dirty state is retained while mounted.

The D86F writeback callback returns void, so it cannot report a host flush
failure through the guest FDC result. Closing/ejecting still destroys the
in-memory image after the final save attempt. Structural D88 formatting
remains deliberately unsupported.

## Build and limits

The Windows sources compile with these changes and the pre-existing local
video/keyboard edits. The final link uses `build/src/86Box-speaker-storage.exe`
because Windows locks `86Box.exe` while the current emulator is running.
No tests or guest runtime checks
were run. Audible output, guest boot compatibility and host I/O failure
behavior have not been demonstrated in a running emulator.

The SASI and WD33C93 models still service DMA synchronously. This audit does
not establish hardware-exact bus timing, asynchronous DMA resume, advanced
WD33C93 modes, disconnected target scheduling, or analog speaker fidelity.

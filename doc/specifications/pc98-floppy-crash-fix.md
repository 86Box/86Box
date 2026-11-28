# Floppy media cleanup access violation

## Recorded failure

Windows Application Error events on 2026-10-01 at 20:09:55, 22:32:43 and
23:00:15 (Europe/Amsterdam) report exception C0000005 in the emulator at
module offset 00434DBAh. The 22:32 event is from
`86Box-speaker-storage.exe`; the other two are from `86Box.exe`.

The existing executable's PE image base is 140000000h. Disassembling
140434DBAh places the fault at the first pointer dereference in
`fdi2raw_header_free`, ten bytes after its entry. That matches the invalid
FDI-header pointer produced by the following source-level ownership bug.

## Cause

Two unrelated formats share the `.FDI` extension. The FDI loader checks
for the flux format's `Formatted Disk Image file` signature. If it is
absent, it delegates to the IMG loader for the Japanese sector format.

After the upstream drive-instantiation change, both loaders store their
different private structures in the same `drv->local` field. The generic
drive close routine still chose the destructor from the original
extension-loader index. Consequently, Japanese FDI images were loaded as
`img_t`, then passed to `fdi_close` and interpreted as `fdi_t`.

The supposed `fdi_t::h` is actually bytes from `img_t::track_data`. The
cleanup can therefore pass disk bytes as a pointer to
`fdi2raw_header_free`. Whether that pointer is zero or invalid depends on
the loaded track contents, explaining the intermittent behavior.

This is a direct explanation for the recorded cleanup faults. It does
not establish that every possible reset crash has this cause.

## Repair

- Each drive records the close callback of its actual mounted backend.
  The Japanese FDI delegation changes this callback to `img_close`.
  Eject/replacement/shutdown now close the allocation with its owner.
- Drive polling, deferred seek and READY callbacks are stopped before
  backend cleanup. A previously running motor poll can resume afterwards
  against the empty drive. Shutdown does not restart timers after the
  timer subsystem has been closed.
- Failed native 86F loads now use one cleanup path that closes the file,
  releases all allocated surface buffers/sector lists, clears the drive's
  decoder pointer and removes its callbacks. Previously several rejection
  paths freed the decoder while leaving `drv->d86f_priv` pointing to it.
- Native 86F flag handlers are installed before querying surface data
  and track-table size, rather than querying empty proxy flags.
- FDI file-open/header failures now leave an empty drive instead of
  dereferencing a missing file or header object.
- Physical floppy images retain IMG ownership, which releases their
  ioctl resources through the same backend close path.

## Build and limits

The changed sources and dependent objects compiled. The complete updated
executable was initially linked as `build/src/86Box-floppyfix.exe` while
Windows locked the running executable. After that instance closed, the
normal `build/src/86Box.exe` also linked successfully with the same fixes.

The evidence consists of Windows crash records, executable disassembly
and source inspection. No guest runtime checks or new tests were run.
The preceding speaker, storage and sound-card changes remain included.

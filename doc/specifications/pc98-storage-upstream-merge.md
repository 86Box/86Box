# PC-98 storage integration with upstream 86Box

## Imported revisions

- Original source snapshot: `571ab627fdb86d42dc9b985aef239bef8957737a` (2026-09-21).
  The initial local commit has identical source/build files except the added
  uPD7220 files. CI files and QDarkStyle resources were omitted from the import.
- Updated upstream master: `fa1c79bb8b18f4815deb530c86500453cd0e4ff0`
  (fetched 2026-10-01 from https://github.com/86Box/86Box).
- Floppy instantiation refactor: `1d12375ece6afbd37eec41f8244b2568d391c0a3`.
  Includes subsequent upstream IMG, PCjr, FDD and FDC fixes through this master.

The local repository originally had independent history. A merge using the
`ours` strategy records the matching import baseline without changing its tree
or rewriting existing commits. The following merge imports upstream changes
using that baseline. `codex/pre-upstream-20261001` retains the original HEAD;
`refs/codex/pre-upstream-local-edits` retains the previously uncommitted edits.

## Floppy

Upstream now supports eight drive objects in two groups of four. Each FDC has
its own `fdd[]` pointers; image backends and D86F callbacks accept a drive object.

- Ported the PC-98 motherboard and D88 loader to these object APIs. D88 state
  lives in `drv->local`; D86F handlers, write protection and media paths live
  on the same drive object.
- Kept the existing native PC-98 FDC, 1 MB / 640 KB bank routing, DMA 2 / DMA 3,
  IRQ 11 / IRQ 10, drive-select-in-command behavior and three-mode density
  control. These remain one controller with its existing bank emulation.
- Preserved READY spin-up timing and media-swap cancellation using per-drive
  timers and pending state. The missing-ID countdown is also per drive, so
  another running drive cannot advance or finish its error response.
- Reset discards old backend command state and restarts motor state. Media
  replacement remains serialized with emulation via the existing pause path.
- Failed image loads retain an empty drive instead of overriding the loader's
  failure with a mounted state.

## SASI (PC-9801-27)

- Retained native 256-byte HDI/NHD handling and the existing 33-block-per-track
  translation for 512-byte containers; it does not use the generic SCSI bus.
- Moved the shared-settings SASI bus ID from 12 to 15: upstream now uses 12 for
  Hitachi CD-ROM, 13 for Philips and 14 for CM100. Configuration files use the
  string `sasi`, so their on-disk bus selection remains compatible.
- DMA callbacks now assert and release DRQ and reset clears it. The NEC ROM's
  board-supplied memory-write strobe for its VERIFY-mode read remains intact.
- Sector staging storage is owned by the controller. Close deasserts IRQ/DRQ,
  disables the ROM mapping and releases the ROM payload. The mapping node
  remains allocated, matching the memory layer's linked-node lifetime.
- Native image range checks use subtraction to avoid an overflowing LBA sum.

## SCSI (PC-9801-55)

The WD33C93 adapter still allocates one bus with `scsi_get_bus()` and uses the
common target command/data APIs. Upstream's new `scsi_plan()` assigns one bus
to an expansion SCSI card by default, so the adapter is included in settings
bus ownership without an additional callback.

Reset now stops an outstanding target command, releases DRQ/IRQ and clears
transfer, CDB and message state. A device-reset callback restores the board
register defaults, DMA gate and ROM bank, retaining the configured host ID.
Controller close performs the same command cleanup before removing its mapping.

## Validation limits

The normal Windows build is used to compile and link the merged implementation.
No new tests or guest runtime checks were requested or run. This integration
does not establish hardware-exact timing for the existing synchronous SASI and
WD33C93 transfer models. D88 structural formatting remains unsupported.

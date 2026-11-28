# PC-98 absent-drive detection and floppy reset

The reported configuration is one installed drive, with B set to None. The
reported symptom is successful floppy access on initial emulator startup,
followed by a DMA-related stall after a reset.

## Absent drive identified as a mechanism

The 04BEh read handler tested only whether the selected internal drive was
5.25-inch. None has no drive flags, so `fdd_is_525(None)` is false. It therefore
returned the same 3-mode ID as an installed 3.5-inch drive: FEh in the initial
2HD mode. Writes also accepted the nonexistent drive's 1.44 MB mode latch.

Both directions now require a configured physical mechanism. An installed
empty drive still identifies, but an absent drive returns zero and ignores
mode programming. Motor requests are also restricted to installed drives.

The TYP0 bit at 94h/CCh describes the internal pair's interface type; it does
not count individual mechanisms. Likewise, the existing four post-reset
SENSE INTERRUPT notifications are controller unit notifications, not proof
that four drives are installed. Those behaviours are retained.

## Reset left asynchronous work alive

The generic DOR reset path stopped only the DOR-selected drive, without
cancelling the command timer, releasing DRQ, or clearing the old result and
terminal-count state. PC-98 selects the command's drive through command bytes,
so that drive can differ from the internal DOR's unit zero.

The board also dropped its route lock before resetting the core. A DMA3
command reset through the 1 MB interface consequently switched the core to
DMA2 while leaving DMA3's request asserted. The reverse direction had the
same problem.

PC-98 reset now cancels the controller timer, media decoder, deferred seeks
and pending operations, clears old execution/result state, and lowers IRQ/DRQ
on the original route. The board then clears its queues and establishes the
new route before releasing RESET and scheduling the reset interrupt. This
matches NEC's documented reset behaviour: RESET returns the FDC to idle and
lowers INT and DRQ. [NEC uPD765A/uPD765B datasheet, pin functions, page 2](https://hxc2001.com/download/datasheet/floppy/thirdparty/FDC/NEC/UPD765_Datasheet_OCRed.pdf).

Port F0h is a CPU-only reset, also used to return from protected-mode firmware
operations. It now preserves both the physical floppy command and its board
queues/routing; previously it discarded only the latter and could orphan an
active transfer. An explicit 94h/CCh controller reset performs the abort.

## Startup and later reset differed

Initial creation initializes the FDC before the board applies native wiring.
`device_reset_all()` instead visits the board before its nested FDC. The
generic core's later reset overwrote the board's 500 kbit/s rate and density
polarity, and installed IBM 3F0h decoding again.

Native PC-98 core reset now preserves the externally supplied rate and density
polarity and never installs IBM ports. Board reset explicitly aborts old work
before dropping the old route. It keeps motors off until firmware starts them,
because the subsequent `fdd_reset()` rebuilds media timers.

Motor reconciliation no longer returns early solely because the board's
cached request is unchanged. This lets the next control write or command
restart polling when a drive reset has stopped the physical motor and rebuilt
its timer.

## Verification

`tests/floppy/pc98_floppy_test.c` includes the production motherboard, FDC and
FDD code and links the actual IMG/86F decoder. Adapters provide only host
clock scheduling, IRQ observation, DMA memory transport, unrelated host
services and UI/audio. They do not synthesize media data or completions.

The suite has 155 checks. It covers absent versus installed-empty B, both
IRQ/DMA reset directions, pending-operation cancellation, board-before-core
reset order, motor timer reconciliation and CPU-only reset preservation.
It also verifies 1024-byte sector contents from a generated 77-cylinder,
two-sided, eight-sector-per-track image on startup and after repeated
controller resets, machine resets, media timer rebuilds, and FDC close/recreate
with the mounted image retained.

The fixed code passes all 155 checks. The same suite against the saved
pre-change board/FDC/FDD sources fails 35 checks. This reproduces the faulty
state transitions; it is not a captured live guest boot or a claim that every
old-code test read stalls.

Standalone build, without Qt or GoogleTest:

```powershell
cmake -S tests/floppy/pc98 -B build/pc98-floppy-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/pc98-floppy-tests
ctest --test-dir build/pc98-floppy-tests --output-on-failure
```

The full emulator objects compiled successfully. The usual executable was
locked by two running instances, so the same complete application link was
saved as `build/src/86Box-floppy-reset.exe`. Live guest boot/reset validation
remains outstanding.

# PC-9801VX device timing and compatibility rollback

Reference target for device timing: the motherboard with an 8 or 10 MHz
80286. CPU upgrades require a separate bus/cache relationship. This code
does not establish whole-machine cycle accuracy or hardware equivalence.

## Compatibility rollback

The added CPU/VRAM wait model was removed after a slowdown report. Its
`is286` gate also selected 386/486 upgrades, so every CPU text/graphics
access acquired extra waits measured in GDC clocks. Those absolute host
interface costs were assumptions, not calibrated VX or upgrade timings.
Even EGC CPU-source readback and shifter priming received the extra charge.

Removed from the release source:

- CPU VRAM reservations, two/four-GDC-clock access costs, separate charger
  read/write stalls and page-switch settling deadlines.
- Extra external-ROM, I/O and interrupt-acknowledge WAIT callbacks.
- The stock-VX-specific prefetch cost and six-byte queue override.
- Timing-only charger capture APIs and the unused CPU wait module.

VRAM callbacks still synchronize GDC command execution to elapsed CPU time,
then apply their existing functional read/write behavior. Ordinary opcode
and memory accounting remains in effect. Page-selection ports update their
latches immediately. This rollback restores compatibility rather than
claiming those original timings are exact.

## Retained behavior

- Native PC-98 uses the existing precise single-8237 core, including masks,
  requests, terminal count, EOP, auto-init, demand/block ownership and
  native page carry. The global XT rewrite is not enabled.
- DMA RESET leaves all four native channels usable. Channel zero remains
  available to the PC-9801-27 SASI interface; IBM 5140-specific disabling
  and refresh behavior are kept separate.
- Stock 286 DMA occupancy retains conditional S1, S2/S3/S4, board SW and
  compressed/verify handling. Upgraded CPUs retain their previous occupancy.
  Physical HOLD/HLDA arbitration is still outside the synchronous API.
- GDC command/FIFO execution, refresh/display drawing grants, row preparation
  and DMA request windows retain their device-clock models.
- Text and graphics clocks use separate accumulators and SW2-8 selection.
  Integer remainders preserve elapsed time across polling and speed changes.
- Programmed raster intervals and scanline sampling remain, including the
  text partition/scroll corrections. VRTC interrupt arming is unchanged.
- Bus stalls used by DMA advance TSC once. Interpreter retirement avoids
  charging them twice. Expired timers are processed before calculating the
  next interpreter period, retaining the reset-path hang fix.
- Extended memory relocation and the VX memory-size changes remain.

## Manufacturer references and limits

- NEC, [uPD7220/uPD7220A User's Manual, December 1985](https://www.bitsavers.org/components/nec/uPD7220/uPD7220-uPD7220A_User_Manual_Dec85.pdf):
  clocking, command processing, memory grants, refresh, character-row
  preparation and DMA request windows.
- NEC/ASCII, [PC-9800 Series Technical Data Book, Hardware, 1993](https://vtda.org/docs/computing/NEC/PC-9800TechnicalDataBookHARDWARE%2BOCR_1993.pdf):
  printed p.39 gives stock 286 waits; p.71 describes SW2-8; pp.163,
  175-176, 188 and 191 describe video access, exclusion and charger WAIT;
  p.330 gives DMA timing; pp.349/351 describe video clocks and raster.
- Intel, [80286 Hardware Reference Manual, 1987](https://www.bitsavers.org/components/intel/80286/210760-002_80286_Hardware_Reference_Manual_1987.pdf):
  independent bus/execution units and the six-byte prefetch queue.
- Intel, [8237A data sheet, order 231466](https://read.seas.harvard.edu/~kohler/class/04f-aos/ref/hardware/8237A.pdf):
  service modes, requests, priority, terminal count and conditional S1.

Datasheet cycle totals cannot simply be appended to the interpreter's
existing instruction accounting. CPU/display fetch ownership, RAM boundary
rules, actual EGC/GRCG READY edges, CPU locks and upgrade bus/cache timing
still require measurements and an appropriate bus interface. The published
normal-mode video clock/dividers remain a model, not crystal measurements.

## Verification

The graphics suite covers functional EGC/GRCG behavior, all ROP truth tables,
FIFO/work timing and GDC arbitration. The video wrapper regression checks
normal byte/word/odd-word access, bulk GRCG writes, EGC readback, page switches
and refresh positions at 8, 10, 20 and 32 MHz without extra CPU bus stalls.
DMA and CPU suites retain SASI channel-zero reset/transfer checks, native
occupancy and production timer-queue reset regression coverage.

Standalone projects are `tests/video/pc98`, `tests/machine/pc98`,
`tests/dma/pc98`, `tests/floppy/pc98` and `tests/cpu/pc98`. Generated builds,
firmware, disks and diagnostic captures remain outside the source push.

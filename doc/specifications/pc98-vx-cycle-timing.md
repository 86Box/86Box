# PC-9801VX device timing and compatibility rollback

Reference target for device timing: the motherboard with an 8 or 10 MHz
80286. CPU upgrades require a separate bus/cache relationship. This code
does not establish whole-machine cycle accuracy or hardware equivalence.

## Stock CPU wait-state restoration

The earlier wait-state implementation was rolled back after it slowed CPU
upgrades. In particular, the video wrapper's `is286` flag also covers later
386/486 cores. The previous CPU/VRAM model is now restored only for the native
`pc9801vx` machine with the Intel `286` family, `CPU_286`, an 8 or 10 MHz
catalogue clock, a 1x multiplier and the matching running clock. One production
selector, `cpu_pc98_vx_bus_active`, gates all restored paths. Faster 286s,
386/486 upgrades, other families, BX4 and ordinary PC machines retain their
previous behavior, even if their legacy `is286` flag is set.

Restored for those stock configurations:

- External-ROM WAIT extensions for C0000h-DFFFFh: four clocks at 8 MHz or
  five at 10 MHz per physical 16-bit bus transfer, splitting odd words.
- I/O WAIT extensions: three clocks at 8 MHz or four at 10 MHz per bus
  transfer. The generic unhandled-port delay is suppressed on this path.
- Two interrupt-acknowledge cycles with five WAIT clocks each.
- Stock prefetch bus costs and a six-byte prefetch queue limit.
- CPU text/graphics VRAM ownership through the existing GDC arbiter,
  two-GDC-clock reads/writes and split four-clock charger RMW operations.
  The write uses values captured at the end of its read phase.
- The previous graphics page-switch settling interval, cleared by reset
  or a switch away from the stock CPU configuration.

The VRAM costs, charger READY behavior and external CPU/display arbitration
remain model assumptions rather than calibrated VX hardware timings. No new
onboard DRAM boundary or expansion-board timing rules are inferred. The
restoration does not establish whole-machine cycle accuracy.

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
and refresh positions. Stock 8/10 MHz 286 cases must incur waits, while
faster 286, 386/486, BX4 and ordinary PC cases must incur none. The CPU
callback tests also reject wrong-family, multiplier and running-clock
configurations even when legacy CPU flags suggest a 286.
DMA and CPU suites retain SASI channel-zero reset/transfer checks, native
occupancy and production timer-queue reset regression coverage.

Standalone projects are `tests/video/pc98`, `tests/machine/pc98`,
`tests/dma/pc98`, `tests/floppy/pc98` and `tests/cpu/pc98`. Generated builds,
firmware, disks and diagnostic captures remain outside the source push.

The earlier rollback build reached the FreeCOM date prompt in a disposable
stock-VX SASI VM. That result predates this restoration. Automated checks
of the restored model exercise the production wait callbacks and VRAM
handlers; they do not establish hardware timing accuracy.

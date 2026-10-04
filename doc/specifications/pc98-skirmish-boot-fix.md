# Skirmish floppy boot: premature CRTV interrupt

Skirmish Disk A booted its DOS system files, then continuously sounded BEL while
installing its graphics driver. The failure was caused by repeated unsolicited
CRTV interrupts on IRQ2. It was not reproduced as an FDC command or D88 sector
decoding failure.

The native video wrapper now produces one initial CRTV request and waits for a
write to port 64h before requesting another. The hardware source, implementation
comparison, and production PIC regression are documented in
[the CRTV interrupt audit](pc98-vrtc-interrupt-audit.md).

## Guest instruction evidence

Disk A has ordinary 77-cylinder, two-sided, eight-sector, 1,024-byte-sector D88
geometry. Its sector headers contain no error, deleted-data, or density flags.
The boot loader uses INT 1Bh BIOS reads and recalibration. It successfully loads
IO.SYS, MSDOS.SYS, and DSHELL.COM. DSHELL starts GE10.COM before TITLE.EXE,
MANDK.COM, and PMD.COM.

The failing resident driver is identifiable at 1177:0106 by its
`BB_CLUB(N.Matsumoto)GE10` signature. Its initializer calls these routines in order:

1. 0FF2 calls 045D, installing the vertical interrupt handler.
2. 0FF5 calls 0373, installing its interval timer handler.
3. 0FF8 calls 072C, installing its INT 41h API.

Routine 045D executes CLI, replaces IVT 0Ah with CS:04D4, saves the previous
FD80:0937 handler in CS:0867/0869, and unmasks master IRQ2 at 0490 (`OUT 02h,AL`,
after `AND AL,FBh`). Only afterward does it call routine 0755 to preserve and
install the INT 42h chain, using DOS INT 21h AH=35h at 075D and AH=25h at 0777.
The explicit CRTV reset write is later still, at 049E.

DOS's INT 21h entry is 05B8:1181 in the captured guest. Its common dispatch switches
to the DOS stack and executes STI at 11F3, before dispatching AH=35h. Thus GE10's
CLI does not protect the interval between unmasking IRQ2 and installing INT 42h.
A request already pending from the emulator's unconditional per-frame IRQ can
enter GE10 before its interrupt chain exists.

Both pre-fix 45-second and 60-second RAM snapshots show:

- IVT 0Ah = 1177:04D4, GE10's vertical interrupt handler.
- IVT 41h and IVT 42h = 0060:5151, DOS's unused-interrupt handler.
- GE10 CS:0865 = 1, vertical interrupt installation started.
- GE10 CS:086D = 0, interval timer installation not started.
- GE10 CS:0CC2/0CC4 = 0, the INT 42h previous-vector save not reached.
- GE10 CS:0CC6/0CC8 = 0, INT 41h installation not reached.

GE10's ISR sends master PIC EOI at 0530 and executes INT 42h at 0538, pushing
1177:053A as its return address. The captured stack contains that exact frame
below the fatal handler's console stack. DOS handler 0060:5151 prints a message
beginning with BEL: "This interrupt cannot be used." Its BEL handler at 3131
enables the speaker through port 37h. Console cursor updates execute an
unconditional STI at 37D1, allowing further unsolicited CRTV interrupts to reenter
GE10 and overwrite its private stack. This explains the sustained beep.

## Validation and observed progress

A temporary probe using the production FDC/FDD/D88/8237 code compares every one
of the 61,440 boot-loaded IO.SYS/MSDOS.SYS bytes with Disk A. Both turbo modes
pass **115 checks**, including multi-sector head transitions, normal result
completion, and release of DMA requests. A separate production D88 READ ID probe
passes **125 checks**, including cold access, head changes, cylinder 76, and an
absent-track fixture. The video wrapper regression uses the actual PIC request,
acknowledge, and EOI code and passes **130 checks** for VX and BX4.

With the CRTV change, fresh floppy-only guest captures at 45 and 60 seconds no
longer enter the DOS fatal handler or enable the beeper. They instead execute
DSHELL.COM at 10C0:05FB/05E5. This is its first interactive menu loop at 056C,
called from 0310 with return address 0313. The loop reads the MANDK mouse API
through INT 33h and the flags maintained by DSHELL's keyboard interrupt handler.
The floppy controller is idle after loading later files through cylinder 8.
INT 33h points to MANDK at 1177:0608; IVT 0Ah points to PMD at 122C:0126, whose
resident signature confirms the music driver has loaded. GE10, TITLE, GE10's
uninstall, MANDK, and PMD initialization have all completed.

Post-fix IVT 41h/42h contain words `0060,5151`, meaning **5151:0060**, not the
original fatal vector 0060:5151. GE10's uninstall code swaps the saved offset and
segment when restoring these vectors. They are no longer used in the observed
DSHELL menu loop; their bytes must not be mistaken for renewed entry into the
fatal handler.

The blank text buffer is expected after DSHELL switches to graphics. Its menu
uses GRCG tile writes and direct graphics VRAM stores. Capturing the emulator's
actual composed 640-by-400 framebuffer confirms the first menu, with choices
「はじめからする」 and 「続きからする」, at 45 and 60 seconds.

A production hard-reset request was then sent only to this disposable VM while
it was at the menu. The next capture shows memory POST, followed by the same
rendered menu 48 seconds after reset. The beeper remains disabled and the FDC
returns to idle. This verifies cold boot and menu-to-hard-reset-to-menu behavior
on the copied VX configuration (286/8 MHz, 4 MiB, two NEC mechanisms, internal
mouse, PC-9801-26K, floppy turbo enabled, hard disk controller None).
Frame captures are under `tmp/skirmish-runtime/vrtc-floppy-long`.

A separate fresh clone preserving the explicit PC-9801-27 SASI interface and
copied hard disk also reaches the visibly rendered Skirmish menu by 90 seconds.
The captured frame shows both menu choices, the selected row, and a mouse pointer.
Its earlier 25/45/60-second option-ROM READ ID observations were a transient
density-discovery/retry stage, not a permanent boot or lost-IRQ stall. No additional
SASI or floppy source change was needed for this outcome. The screenshot is
retained at `tmp/skirmish-runtime/vrtc-sasi/frame-90s.png`.

Full gameplay and later disk changes have not been verified.

All runtime probes used copied disposable media and VM configurations. The
original user images and configuration were unchanged by this investigation.
Temporary instruction dumps, byte comparisons, and RAM snapshots are under
`tmp/skirmish-loader`, `tmp/skirmish-d88-audit`, and `tmp/skirmish-runtime`.

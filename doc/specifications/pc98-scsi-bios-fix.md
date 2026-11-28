# PC-9801-55 BIOS initialization fixes

Reviewed 2026-10-02 using the installed PC-9801-55L ROM pair and the real
adapter's registered I/O and memory handlers.

## ROM bank switch never completed

The board control register is selected with 30h at port 0CC0h and read or
written at 0CC2h. Its ROM bank selector is **bit 6 (40h)**. The emulator
incorrectly used bit 0 (01h).

In the installed 55L first bank, CS:0D81 builds a small trampoline in RAM
at 0000:04F8. CS:0D9E sets bit 6, then the trampoline writes the control
register and jumps to CS:0000. The second bank starts with a CALL into its
initialization routine. With the incorrect selector the jump fetches the
first bank again, restarting initialization instead of completing the
banked firmware routine. The second bank clears bit 6 at offset 008F and
uses the RAM trampoline to return to the first bank.

The controller now uses 40h for both memory reads and the executable ROM
mapping. Machine reset restores bank zero.

## PIO polling could never observe data readiness

The 55L firmware's TRANSFER INFO loops at first-bank CS:0CAB and CS:0CE6
compare the auxiliary status against **21h (BSY | DBR)** before transferring
a byte. The emulator previously supplied 01h (DBR only), so the firmware
could not transfer command or result bytes even after the bank switch was
fixed. Active INFO transfers now expose both BSY and DBR. Both bits clear
when a transfer completes; INT remains set until the controller status is
acknowledged.

## Selection and transfer completion interrupts

A subsequent full-emulator smoke run with a present disk found another
initialization wait: after SELECT returns 11h, the 55L BIOS waits for the
target's first phase-service interrupt. The adapter now queues 88h OR the
command/message phase after selection completion is acknowledged. The test
checks both interrupts separately, including IRQ reassertion and acknowledgment.

The WD33C93 status table specifies that a non-Message-In INFO completion
reports the **new phase requested by the target** in the status low bits.
The previous controller reported the old data phase, then synchronously
raised another completion during the first status read. In the running
55L BIOS that second interrupt overwrote its DMA completion state before
the DMA gate was disabled. Data completion now reports STATUS (1Bh) once;
status-byte completion reports Message In (1Fh) once. Message In completion
pauses with ACK asserted (20h) until NEGATE ACK; basic-command disconnect
reports 85h, while Select-And-Transfer retains its level-2 completion (16h).

These semantics follow the [Western Digital WD33C93A data sheet, SCSI Status
register table, page 18](https://www.bitsavers.org/components/westernDigital/WD33C93A_Data_Sheet_and_Application_Notes_Nov1990.pdf).
The firmware clears Own-ID's enhanced-mode bit before RESET, so ordinary
reset completion 00h is retained.

## Verification

`tests/scsi/pc98_55_test.c` initializes the actual adapter and exercises its
registered public I/O and ROM handlers using synthetic ROM banks and one
synthetic SCSI target. It checks the firmware's bank-control sequence,
byte/word/dword reads, executable bank mapping, machine reset, controller
reset interrupt/acknowledgment, board IRQ/ID readback, six-byte PIO INQUIRY,
36-byte PIO response, all six IRQ-jumper encodings with host ID zero,
self-selection timeout, DMA0 transfer into synthetic host memory, terminal
count and DRQ behavior, status/message progression, IRQ acknowledgment
without a duplicate completion, and missing-ROM fallback.

- Debug: 287 checks, zero failures.
- Release: 287 checks, zero failures.
- The initial bank-switch/PIO test harness against the preceding HEAD controller:
  121 checks, 47 failures, reproducing both defects.

Standalone reproduction:

```powershell
cmake -S tests/scsi/pc98 -B build/pc98-scsi-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/pc98-scsi-debug
ctest --test-dir build/pc98-scsi-debug --output-on-failure
```

ROM contents are neither changed nor distributed by this patch. A full
Release emulator running an isolated PC-9801VX with the installed 55L ROMs
and an empty SCSI bus completed card initialization and returned to the
motherboard firmware. A present-disk smoke test exercised actual PIO INQUIRY
command bytes and DMA0 response bytes and exposed the selection and
duplicate-completion defects described above.

With all fixes in the production Release binary, a fresh 1 MiB blank disk
using the generic 1997 preset and `hdd_01_vendor = NEC` completed detection.
By 12 seconds the BIOS had set disk-present mask 0000:0482 to 01h and
returned to motherboard ROM FD80:255E, and by 25 seconds execution was at
F320:054E. The controller completed later commands, including SEEK (2Bh),
and ended disconnected with status 85h and no pending IRQ. The blank disk
does not establish operating-system boot.

The installed 55L firmware itself requires direct-access disks to identify
as NEC. It checks INQUIRY bytes 8-10 at CS:0632/0638 and intentionally loops
at CS:0645 for another manufacturer. A separate default-vendor run reached
that exact loop after successful INQUIRY and disconnect. Use an existing
generic disk preset with custom vendor `NEC`; this requirement is retained
without modifying firmware or spoofing the target's INQUIRY response.

## IRQ 13 conflicts with the native mouse clock

A subsequent real boot test reproduced a POST hang with the SCSI card
configured for IRQ 13, host ID 0, ROM D0000h and DMA 2, alongside a SASI
boot disk on an Am386SX-33 PC-9801VX. The card's RESET completion was pending,
but its BIOS waited indefinitely at CS:0EB1-0EB6.

The IRQ selector itself is correct. The six encodings map to IRQs
3, 5, 6, 9, 12 and 13. The 55L firmware masks board register 33h with 38h
and indexes its table at CS:0DF8. The IRQ 13 entry installs vector 15h,
uses slave PIC mask port 000Ah and unmask byte DFh; the IRQ 5 entry installs
vector 0Dh and uses master mask port 0002h with the same DFh byte.

An instrumented, disposable full-emulator run observed this sequence:

1. The SCSI BIOS unmasked IRQ 13 by writing DFh to port 000Ah at CS:0FB2.
2. A pending native mouse-clock IRQ entered its new SCSI interrupt handler.
   That handler masked IRQ 13 again by writing FFh at CS:0FD8.
3. The handler saw auxiliary status 00h because the SCSI card had no pending
   interrupt. It returned without unmasking the shared line.
4. The subsequent controller RESET completed and asserted IRQ 13, but the
   slave PIC remained masked. The BIOS waited forever for its handler.

At the stall, CPU IF was set, vector 15h pointed to the correct SCSI handler,
slave PIC IRR contained IRQ 13, and the controller's pending interrupt and
asserted line were both present. This is a shared-resource conflict, rather
than a missing SCSI completion or incorrect PIC port decode. The native
mouse clock can interrupt without host mouse movement.

The configuration now labels IRQ 13 with its mouse-clock use. Use a free
IRQ such as the default IRQ 5. With only the copied configuration's SCSI
IRQ changed from 13 to 5, the same full emulator completed the empty SCSI
scan, booted FreeDOS from the copied SASI disk and ran the LemonSpin demo;
the demo's ShinkoNet/spaztron64 text and animated glyphs were present at
45 seconds. Host ID zero and DMA 2 did not prevent that boot. No firmware
bytes, original disk contents, guest interrupt logic or mouse timer rate
were altered for the successful boot.

## Explicit SASI controller selection

The motherboard previously installed PC-9801-27 automatically whenever a SASI
disk image was configured, even if the hard disk controller selector displayed
None. That hidden installation has been removed. Select PC-9801-27 explicitly
under Hard disk controller to access SASI disks (`hdc_1 = pc9801_27` in the
configuration). Selecting None leaves the controller absent regardless of
attached disk images; existing configurations are not silently migrated.

Full-emulator tests with independent copies of the same SASI disk confirm that
None creates no SASI device and reaches built-in BASIC. Explicit PC-9801-27
creates the controller and boots FreeDOS/LemonSpin. These cases used no SCSI
card, isolating controller selection from the separate PC-9801-55 IRQ issue.

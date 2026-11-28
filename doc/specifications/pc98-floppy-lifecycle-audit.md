# PC-98 floppy controller lifecycle audit

The native floppy path had four reproducible lifecycle failures in the actual
765 controller and IMG/86F media engine:

| Trigger | Previous behavior | Corrected behavior |
| --- | --- | --- |
| Eject during READ, WRITE or FORMAT | Decoder disappears while the 765 remains busy; READ/WRITE keep DRQ asserted | End execution with seven result bytes, release DRQ, report READY changed and Not Ready |
| Eject while a READ waits for an implied SEEK | Deferred callback is discarded but controller execution never finishes | Cancel the callback and return the same READY-change result with the requested CHRN |
| Eject during a mechanical SEEK | Completion timer is cancelled and SEEK never produces its interrupt | Keep the mechanical completion after releasing the image backend |
| Close the controller during DMA | Freed controller leaves IRQ/DRQ outputs asserted | Release the native controller's outputs before detaching its drives |

A further absence case was reproduced: an absent drive has no running media
poll timer, so its fallback missing-ID countdown never expires. READ, WRITE,
FORMAT and READ ID could remain busy indefinitely. Native data commands now
check the selected mechanism's READY input before execution and return a
Not Ready result when it is low. This check includes the existing motor spinup
delay; guest software must wait for READY before issuing media access.

The [NEC APC System Reference Guide, Table 3-10 and the uPD765 command
descriptions](https://bitsavers.computerhistory.org/pdf/nec/APC/819-000100-1003_APC_System_Reference_Guide_Apr83.pdf)
defines ST0 interrupt code `11` for READY changing during execution, and NR in
bit 3 for a not-ready drive. Accordingly, media removal returns `C8h` plus the
command's head/unit bits. A drive already not ready when a command begins
returns the ordinary abnormal-termination code `48h` plus head/unit bits.
The READY checks apply to the PC-98 adapter; IBM PC adapters tie that input
active and retain their existing missing-ID behavior.

Media removal does not overwrite a partly received command or an existing
result phase. It clears execution FIFO/data state before exposing an error.
A mechanical SEEK can continue without media because its completion callback
checks backend pointers before using them. A deferred media operation cannot
continue after eject, so its callback is cancelled instead.

## Native interface and reset fixes

The motherboard wrapper had six further independently reproduced faults:

| Trigger | Correction |
| --- | --- |
| Apply the native DMA permission gate after SPECIFY ND=1 | Preserve the controller's programmed non-DMA mode |
| Read PIO data or probe the FIFO during execution | Keep the originating IRQ/DMA route until the actual result phase drains |
| Write PIO payload containing command byte values | Forward execution data without interpreting it as SEEK, READ ID or command parameters |
| Write a command while the controller is busy | Reject it before changing wrapper queues or routes |
| Assert reset through one identity and release through the other | Compare the shared physical RESET pin, rather than independent control-register histories |
| Reset while the head is away from track zero, then RECALIBRATE | Move to physical TRACK0 independently of the reset controller's cylinder counter |

The last fault reproduces a real DOS 6.20 reboot failure. The installer leaves
the head on track 16. Hard reset clears PCN to zero without physically moving
the head, and the old wrapper calculated RECALIBRATE's movement from PCN.
It therefore moved zero tracks, leaving firmware in a READ-ID retry loop.
The corrected wrapper uses the mechanism's position for RECALIBRATE; ordinary
SEEK retains its target-minus-PCN step semantics.

The native interface suite passes **2316 checks** in Debug and Release. The
separate image/FDC/production-8237 suite passes **58 checks**, including aborting
a transfer after actual RAM writes, cross-interface reset release, page masking,
and three track-16 reset/recalibration/sector-read cycles. The cross-interface
reset case failed six checks before its fix; the physical recalibration cycles
failed 21 before their fix.

Disposable full-emulator tests used copies of local NEC DOS boot media. DOS
2.11 boots before and after hard reset on a 286/640KB and a 386SX/4MB profile.
DOS 6.20 with two configured NEC mechanisms cold-boots its installer, but before
the physical recalibration fix remained in BIOS READ-ID at 60 seconds after
reset. With the fix, hard reset at 25 seconds returns the head to track zero,
loads DOS at 35 seconds, and returns to the installer at 40 seconds. Native
Ctrl/Alt/Delete delivery at the older DOS date prompt did not reboot, so it
does not establish a guest-initiated warm-reboot result.

External mechanisms 2/3 are still incomplete: the VX wrapper's motor and
density handling covers the internal pair. BX4 external floppies also require
the separate PC-9801-87 adapter. This audit does not claim those adapters work.

## Validation

`tests/floppy/pc98_lifecycle_test.c` reuses the production-core host adapter and
links the actual IMG and 86F backends. It creates its own exclusive temporary
1.23 MB image and removes it after the run. It exercises:

- DMA READ/WRITE/FORMAT eject, reinsertion, and successful sector reads.
- PIO READ eject and FIFO cleanup.
- Eject during an implied SEEK with a pending READ, preserving error CHRN.
- Mechanical SEEK completing after eject.
- Controller close during an active DMA command, including timer detachment.
- Reset during READ/WRITE/FORMAT, SEEK and RECALIBRATE, followed by fresh reads.
- Eject at command-parameter and result boundaries.
- Empty installed and absent mechanisms for READ/WRITE/FORMAT/READ ID.

Debug and Release each pass **170 checks**. Running the identical final suite
against saved sources from immediately before these core fixes fails **44
checks**. The failures include unavailable results, stale DMA/IRQ state, lost
SEEK completion, and failure to read successfully after reinsertion.

This suite uses adapted clock, interrupt, DMA-memory and UI host services.
The separate PC-98 floppy DMA suite exercises the production 8237. BIOS/OS
boot checks are separate runtime evidence and are not implied by this host
suite. No user VM configuration or media was changed by this audit.

Standalone invocation:

```powershell
cmake -S tests/floppy/lifecycle -B build-tests/floppy-lifecycle-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests/floppy-lifecycle-release
ctest --test-dir build-tests/floppy-lifecycle-release --output-on-failure
```

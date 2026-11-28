# PC-98 mouse, printer and MIDI interfaces

The native mouse and printer now use the motherboard's existing 8255 ports.
The standalone MIDI option selects a C-BUS MPU-401 on PC-98 machines. None
of these interfaces installs an IBM PC peripheral decoder on a PC-98.

| Interface | Guest programming | Host connection |
| --- | --- | --- |
| Mouse | 7FD9h data, 7FDBh machine inputs, 7FDDh control, 7FDFh 8255 mode/BSR, BFDBh interrupt divider | Select **NEC PC-9872 mouse**; relative capture and fullscreen input feed two buttons and eight-bit movement counters |
| Printer | 40h data, 42h system/active-low BUSY inputs, 44h control, 46h 8255 mode/BSR | Enable LPT1 and select an existing printer or character backend in Ports |
| Roland MPU-PC98 | E0D0h data and E0D2h command/status by default | Enable **Standalone MPU-401** and select MIDI input/output in Sound |

Mouse control multiplexes the X/Y low/high nibbles. A rising latch edge
captures both counters and clears the live counters; subsequent movement
continues separately while the guest reads its snapshot. The periodic
IRQ13 clock runs at 120, 60, 30 or 15 Hz, including without movement, and
the inhibit bit gates its contribution. The existing DIP/CPU-mode inputs
remain available on the other PPI pins. The wiring is cross-checked against
[MAME's native PC-98 implementation](https://github.com/mamedev/mame/blob/master/src/mame/nec/pc9801.cpp).

The printer backend receives data and the active-low PC7 strobe. System
PC6 masks that strobe. Host BUSY feeds the PPI input without replacing its
clock-family and DIP-switch bits. The backend is initialized without AT
378h/278h/3BCh decoding; the native printer has one guest-visible port.
The backend's existing output-file, pipe and emulated-printer connections
are used rather than a separate print implementation.

The MIDI interface uses the existing intelligent MPU-401 protocol engine,
including UART mode and MIDI input. Its two registers are on the even byte
lane, and the odd ports do not decode. Jumper choices cover C0D0h through
FCD0h in 400h increments and IRQ3/5/6/12 (INT0/1/2/5). Defaults are E0D0h
and IRQ6. Reset clears queued input, pending commands and timers; close
removes handlers and MIDI input registration. Port and IRQ choices follow
[NP2kai's MPU-98 implementation](https://github.com/AZO234/NP2kai/blob/master/cbus/mpu98ii.c).

The native RS-232C 8251 register/interrupt implementation remains present,
but its host transmit/receive bridge is not implemented. The generic AT
serial setup is suppressed, and PC-98 serial connection controls are
disabled until that bridge exists. Additional parallel connection controls
are also disabled because only the native printer port is decoded.

## Verification

The standalone mouse/printer suite includes the production motherboard,
8255, mouse counters, LPT transport and FIFO. It checks counter wrap,
snapshot reads and motion while latched, BSR, button polarity, DIP inputs,
interrupt masking/dividers, capture gating, backend output on strobe and
host BUSY. The 64-check mouse/printer suite uses the production PIC to verify
that mouse inhibition and periodic ticks preserve pending C-BUS interrupts.
Inhibition prevents future mouse pulses; it does not withdraw a request already
latched by the edge-triggered PIC. In particular, PPI writes no longer erase a
pending SCSI request on shared IRQ13.

The installed 55L BIOS masks IRQ13 when it receives a mouse-clock interrupt
through its SCSI handler. Use a separate SCSI IRQ, such as the default IRQ5,
with the native mouse clock. Details and a copied-VM boot test are recorded in
`pc98-scsi-bios-fix.md`.

The MIDI suite exercises the production protocol engine through
registered native ports at all sixteen addresses and all four IRQ choices:
UART acknowledgment, output bytes, input interrupts and lifecycle reset.
The MIDI suite passes 400 checks in Release and Debug, including reset and
close with all three callbacks queued, an interrupt asserted and a command
pending during RESET busy. A separate production MIDI callback-list suite
passes 55 checks under both policies. It caught existing broken append links
and removal traversal that could lose callbacks or hang device cleanup;
the shared callback-list implementation now preserves every surviving handler.

```powershell
cmake -S tests/device/pc98 -B build/pc98-mouse-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/pc98-mouse-tests
ctest --test-dir build/pc98-mouse-tests --output-on-failure

cmake -S tests/sound/mpu_pc98 -B build/pc98-mpu-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/pc98-mpu-tests
ctest --test-dir build/pc98-mpu-tests --output-on-failure
```

These tests use host service adapters. They do not establish successful
guest mouse-driver use, physical printer passthrough, MIDI listening or
cycle-exact peripheral timing.

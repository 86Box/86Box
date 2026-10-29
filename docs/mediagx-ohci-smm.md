# MediaGX / CS5530 USB (OHCI) and SMM: findings

Notes for people and agents working on the Cyrix MediaGX MMX + CS5530
emulation (machine `p5gxm`, ECS P5GX-M, Award BIOS 09/25/1998, VSA
`GXM55303`). Gathered on 2026-10-29 with the Windows 98 SE test VM
(`build/src/p5gxm-98se`) and a USB keyboard and mouse
(`keyboard_type = usb_keyboard`, `mouse_type = usb_mouse`).

## Setup

- BIOS Setup, Chipset Features: `USB Controller` defaults to Disabled;
  enabling it shows `USB Legacy Support`. With a USB keyboard and both
  options off, the keyboard does nothing until an OS driver loads, so
  Setup has to be entered with a PS/2 keyboard first.
- In Award Setup, F10 (save) only works from the main menu, not inside a
  submenu: press Esc first.
- The controller is the CS5530's OpenHCI (0E11:A0F8), bus 0 device 19,
  BAR E0007000h, INTA routed to IRQ 15 by this BIOS.

## How the BIOS does legacy USB keyboard support

- It does **not** use the OpenHCI legacy registers (HceControl at +100h,
  OpenHCI 1.0a Appendix B). Nothing ever writes them.
- POST sets HcControl.InterruptRouting (HcControl = 180h, later 184h with
  the periodic list), enables MIE | OC | RHSC, and from then on the root
  hub and transfer completions are SMIs. Enumeration happens in SMM
  (port resets through HcRhPortStatus, control transfers with
  ControlListFilled).
- The structures live in adapter ROM space: HCCA at C9000h, EDs/TDs around
  C8A80h-C8DB0h. The keyboard's interrupt ED is in HCCA slots 0 and 16,
  so it is polled every 16 ms.
- Keys reach the OS through the real 8042: in SMM the handler writes D2h
  (write keyboard output buffer) to 64h and a **set 1** scan code to 60h;
  the KBC (command byte 67h) raises IRQ1 and INT 9 reads it as usual.
- Because of the 16 ms poll, a key held for less than about 16-25 ms can be
  missed entirely (box86mcp `type_text` at its default 50 ms per key
  drops about one key in three in DOS; 160 ms per key drops none). Real
  hardware behaves the same, since the HID device only reports its current
  state when polled.
- Windows 98 SE loads its own OpenHCI driver early and takes the
  controller over: HcControl becomes 4BCh (InterruptRouting clear), with a
  new HCCA in high memory (0FC90000h in the test). From that point legacy
  input stops, as it should. On a first boot with the controller newly
  enabled, Windows asks for `USB Human Interface Device` drivers
  (HIDDEV.INF, needs `hidclass.sys` from the Win98 SE CD). Until then
  there is no keyboard. A PS/2 mouse is the easy way through that wizard.
  After that, USB keyboard and mouse both work.
- Windows sizes the OHCI BAR (writes FFFFFFFFh, then restores it) with
  Memory Access still on. It does that after taking the controller over,
  so it doesn't race the BIOS handler.

## CS5530 USB SMI semantics (implemented in cs5530.c)

- F0 Index 42h bit 7: "USB-generated SMI pulls SMI# pin active (low)":
  a **level**. Bit 6 routes it to the top-level status (F1BAR+00h/02h bit
  14). The handler (VSA) dispatches on the top-level status.
- The model: `usb_smi_level` follows the OHCI SMI output; SMI# is low while
  the top-level status has a bit set or while `usb_smi_level && 42h[7]`.
  TOP_USB latches when the USB SMI asserts (42h bits 7 or 6, as before).
- Do **not** re-latch TOP_USB when the top-level status is read while the
  USB SMI is still asserted. VSA reads F1BAR+02h/03h in a loop until it
  reads zero, so re-latching on read keeps it spinning inside one SMI and
  hangs POST at the memory test (seen while developing this).
- Before this change the USB SMI was edge-only. That works for this BIOS
  too, because its handler always clears the condition. The level follows
  the data book: an RSM before the condition is cleared is the next SMI.

## OHCI legacy emulation (HceControl): not implemented

`src/usb.c` models the HceControl/HceInput/HceOutput/HceStatus registers
only; ports 60h/64h aren't decoded. This BIOS doesn't need more. A draft
of the full Appendix B behaviour was written and then left out because
nothing here exercises it. Its main points, for whoever needs it (another
OHCI board's BIOS):

- With EmulationEnable set, the HC positively decodes 60h/64h (a chipset
  claim, `io_set_claim`, would need a way to return read data). Reading 60h
  returns HceOutput and clears OutputFull (and AuxOutputFull). Reading 64h
  returns HceStatus. Writing either fills HceInput, sets InputFull, and
  sets CmdData (0 for 60h, 1 for 64h).
- Gate A20 sequence (OpenHCI 1.0a p. 139): D1h to 64h sets GateA20Sequence
  and is captured **without** InputFull. The next byte to 60h sets
  InputFull only if its bit 1 differs from A20State. FFh to 64h while the
  sequence is active clears it without InputFull. Any other 64h write
  clears it and sets InputFull.
- IRQEn: IRQ1 while OutputFull (IRQ12 when AuxOutputFull too).
- Emulation interrupts follow HcControl.InterruptRouting (SMI or the normal
  HC interrupt) and are held off without PCI Memory Access. The
  asynchronous condition is (EE && InputFull) || (ExternalIRQEn &&
  (IRQ1Active || IRQ12Active)). The synchronous one is EE &&
  CharacterPending && !OutputFull, latched at the next SOF (p. 146).
- A20State should drive the board's Gate A20 logic; ExternalIRQEn blocks
  the 8042's IRQ1/IRQ12 into IRQ1Active/IRQ12Active.
- Spec: OpenHCI 1.0a, http://www.o3one.org/hwdocs/usb/hcir1_0a.pdf
  (Appendix B, p. 136-146).

## Related: VSA MPU-401 / MIDI (implemented in cs5530.c)

- VSA's MPU-401 UART mode drives a 16550 at the **Sound Blaster base**
  (its UART base table at DS:2E0h is {220h, 240h, 260h, 280h}, static in
  GXM55303.bin at D340h; status 331h = FFh & ~80h if LSR.DR, & ~40h if
  LSR.TEMT; data through RBR/THR; it never sets a baud rate).
- With nothing there the bus reads FFh: 331h reads 3Fh ("data ready")
  forever and mixer 82h bit 2 (MPU IRQ) sticks. The NT 4.0 XpressAudio
  driver's ISR then only drains the MPU and never counts the DSP test IRQ
  ("cannot be found with this Interrupt").
- The model is an SMM-only 16550 subset at the SB base (outside SMM the
  CS5530 claims those trapped cycles), wired to 86Box MIDI out and in. In
  SMM the CS5530 fast-path OPL status reads at 2x0h-2x3h stand aside so
  they don't AND into the UART's registers.
- BIOS "Onboard MIDI Port" is hidden unless Serial Port 2 is Disabled.
  With it Enabled, the BIOS turns UART2 off and leaves PC97317 GPIO12
  (port 1 bit 2, ports 800h/801h) an input. Otherwise it makes GPIO12 an
  output driving low (801h |= 04h, 800h &= ~04h). PC87317VUL datasheet,
  9.2.2 and Table 9-2: an input reads the pin, the data latches reset to
  FFh, the direction to 00h, and the internal pull-ups are on. So GPIO12
  is high exactly when the MIDI port is enabled.
- The model: the P5GX-M machine has a GPIO handler where undriven pins
  read high, and it tells the CS5530 that the UART is enabled while
  GPIO12 is high (`pc87307_gpio_pins()`,
  `cs5530_set_midi_uart_enabled()`).
- The GPIO latches only reset on a hard reset, and the BIOS's MIDI path
  never sets GPIO12 back to an input. After turning the MIDI port on in
  Setup, power-cycle: Ctrl+Alt+Del keeps it disabled.
- Consequence: the NT 4.0 driver works with MPU-401 at 330h only with
  "Onboard MIDI Port" enabled (Serial Port 2 Disabled). Otherwise set
  MPU401 to Disable in the driver dialog. The readme asks for the driver
  to match the BIOS settings.

## Debugging tips

- 86Box's pclog collapses identical consecutive lines ("*** N repeats
  ***"), so counting log lines undercounts. Static caps on debug logs get
  used up by the BIOS's SMM handler long before Windows starts: sample
  (every Nth event) instead of capping.
- Log SMM-side I/O separately (`in_smm`). The BIOS's USB handler and VSA
  both run there.
- `read_memory` on HCCA+80h (the frame number) shows at a glance whether
  the HC is still running frames on the HCCA you expect. If it's frozen,
  check whether an OS driver moved HcHCCA.

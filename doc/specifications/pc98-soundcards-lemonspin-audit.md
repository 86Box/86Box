# PC-98 sound cards and LemonSpin PCM audit

Reviewed and patched on 2026-10-01. Scope: the PC-9801-26K, PC-9801-86,
and their shared YM2203/YM2608 bridge. The build succeeded. A subsequent
read-only inspection of the user's running silent demo identified missing
guest EMS/sample drivers; audible playback has not been confirmed.

## Why LemonSpin's PCM was broken

The 86 card consumed the FIFO on DAC timer ticks, but retained only the
most recent left/right DAC values. Its mixer callback repeated those two
values over the entire output buffer. The sound core delivers a buffer
every 20 ms: at the highest PCM rate about 882 DAC frames occur in that
interval, and all their intermediate values were discarded.

This is a demonstrated error in the audio path used by the supplied demo.
It turns PCM into a roughly 50 Hz sequence of held values, rather than
the original waveform. A zero held value also makes that whole buffer
silent. This explains a failure to reproduce the demo's PCM, but does not
prove that it was the only cause of silence in the user's running setup.

The card now records the held DAC output into a stereo buffer up to
`sound_pos_global` before each sample change. The mixer finishes the tail
and adds the recorded waveform. Volume and mute changes similarly flush
the old state first. The DAC still holds its value between ticks; its
transitions are quantized to the host sample grid.

## Supplied binaries and playback chain

Original directory: the supplied `2023_sessions_lemonspin/lemnspin` folder.
The originals were read only. Copies and disassembly are in
`tmp/pc98-sound-audit/`.

`START.BAT` loads PMD86, then P86DRV with `/76`, loads `LEEKSPIN.M2` with
PMP, stops playback with `PMP /S`, and runs `98FLIPPY.EXE`. Embedded version
strings identify PMD86 4.8o (86PCM) and P86DRV 1.1c. The M2 data references
`EUROTHN8.P86`; its short score directs the sample playback. The P86 file
has the `PCM86 DATA` signature and is 1,040,140 bytes.

SHA-256 identifiers:

| File | SHA-256 |
| --- | --- |
| 98FLIPPY.EXE | 8051e4cd4f1e9788c088eb7ff632103fc3e026c0cf078f7d86593aa95bbdd6e4 |
| PMD86.COM | 34b7381d66400b89fca833e08fb30f315b9092eb3883f137538dcf18477fd77f |
| P86DRV.COM | f8562a1edb9bf80106cb5b9d4e12f9d8818dd513e165e44086676dc349e2ac8b |
| LEEKSPIN.M2 | 6d4745c468f455e4147771fb95d616b28fa0475e7febfba22aa138517daf9999 |

### Demo control calls

The MZ load module begins at file offset 0800h. Offsets below are relative
to that module, before DOS relocation; COM addresses below include the
100h PSP bias. Focused disassembly was aligned to the routines rather
than treating embedded strings and tables as instructions.

At module 6EF4h, the demo prepares a C `REGS` structure with AH=00h and
DX=0000h, then calls the runtime's `int86` helper at 26EEh with interrupt
60h. At 6F16h it sets AH=01h and makes the same call. These are PMD start
and stop requests. A second pair occurs at 73AFh/73D1h. The executable's
debug string table includes `pmd_play_loaded_song` and `pmd_stop_song`.

The demo therefore delegates playback to the resident driver. It does
not implement a separate host-independent PCM output routine.

### PMD86 initialization and refill

| PMD86 CS offset | Observed operation |
| --- | --- |
| 5439h-5448h | Reads A460h and clears its low two mode/mute bits before probing. |
| 545Bh-5486h | Selects OPNA register FFh, expects ID 01h, and expects FFh from both disabled upper ports. |
| 5491h-54A5h | Sets A460h bit 0 to enable OPNA; clears A66Eh bit 0 to unmute PCM. |
| 0940h-0963h | Stops/unpauses transfers, pulses FIFO reset, disables interrupts, and clears the request. |
| 0964h-0969h | Writes F2h to A46Ah with interrupts disabled: signed 8-bit stereo. |
| 097Fh-0982h | Calls initial-fill routine 0776h; CX=0100h means 256 stereo frames, 512 bytes. |
| 0988h-099Bh | Enables FIFO interrupts and sets A46Ah to 01h: a 256-byte refill threshold. |
| 099Ch-09A4h | Sets A468h RUN. |
| 39F8h-3A85h | Shared hardware IRQ handler repeatedly checks A468h bit 4 and FM timer status until both sources are serviced. |
| 073Fh-0775h | Refills PCM, then clears/re-arms bit 4 with two A468h writes. |
| 077Bh | Refill sets CX=0080h: 128 stereo frames, 256 bytes. |
| 077Eh-07DCh | Locates P86DRV through INT 65h, checks its signature/version, and obtains the sample memory. |
| 07DDh-07EDh | Centered playback writes the same sample byte twice to A46Ch, once per channel. |
| 09B1h-09DAh | Stop routine stops RUN, resets the FIFO and disables its IRQ. |

These routines use the 86 board's separate PCM FIFO. Adding OPNA
ADPCM-B sample RAM cannot repair this playback path.

## Other fixes from the audit

### PC-9801-86

- FIFO requests stay latched until acknowledged, instead of lowering and
  raising the shared interrupt line on every DAC tick below the threshold.
  The PCM interrupt-enable bit gates its contribution to the shared line;
  acknowledging PCM preserves an asserted FM interrupt.
- Empty running FIFOs can request a refill. Previously an early return
  prevented that condition from reaching the interrupt check.
- A468h bit 4 acknowledgement clears the request even if the FIFO is
  still below the threshold. A subsequent eligible DAC tick may request
  another refill.
- FIFO reset acts on the rising edge of bit 3. Rewriting a control byte
  with that bit already high does not repeatedly erase the queued bytes.
- Full-FIFO writes no longer advance the DAC outside its oscillator.
  That old compatibility shortcut also drained stopped/paused FIFOs.
- Recurring DAC ticks advance from their scheduled timestamp, rather
  than accumulating CPU callback latency. Reset restores the rate-zero
  clock as well as the visible rate bits.
- A460h bit 0 gates upper-bank reads and writes for all accesses and
  selects three/six FM channels. This replaces the sequence-specific PMD
  detector workaround with board behavior observed in the driver and
  corroborated by the reference implementation.
- A460h bit 1 mutes OPNA output while preserving the PCM FIFO output.
  Buffered audio preceding the register write retains its previous level.
- A stock 86 board no longer receives fictitious ADPCM-B RAM. The new
  **Chibi-oto ADPCM memory (256 KiB)** configuration option enables that
  memory modification explicitly. It defaults off. Software requiring
  the modification must enable it; LemonSpin's FIFO PCM does not need it.

### PC-9801-26K

The selected IRQ was stored in `int_level`, but Port A jumper readback
used a separate, never-initialized `irq_sel`. Consequently the default
INT5/IRQ12 card advertised INT0/IRQ3. Music drivers that install their
handler based on that readback would choose the wrong vector. Both paths
now use the same setting. Reset also resets the board's address shadow.

### Shared bridge and lifetime

FM timer callbacks now render audio up to the current cursor before
expiring the timer, so timer-driven synthesis changes do not affect an
already elapsed interval. Reset initializes the actual chip address
latch along with its software shadow.

Both sound cards embedded their registered ROM mapping node in an
allocation that they freed on close. They now allocate it separately,
disable the mapping, remove handlers/direct execution and release the
ROM payload before freeing the device. The small inert mapping node
remains valid until the memory subsystem discards its list, matching the
storage-controller cleanup convention in this codebase.

## Sources and remaining hardware gaps

- The supplied executables and PMD86/P86DRV binaries are the primary
  evidence for this demo's behavior.
- The demo author's [Lemonspin write-up](https://lainnet.arcesia.net/blog/2023_05_11_n02.html)
  describes its PMD86 PCM playback and use on real PC-98 hardware.
- [NP2kai board86.c](https://github.com/AZO234/NP2kai/blob/5939e0c6d5985c4c08fc70f289a83290e5d3e6f7/cbus/board86.c)
  gates the extended ports and switches FM channels with board mode.
  [pcm86io.c](https://github.com/AZO234/NP2kai/blob/5939e0c6d5985c4c08fc70f289a83290e5d3e6f7/cbus/pcm86io.c)
  corroborates attenuation, threshold programming and FIFO reset edges.
  These are emulator implementations, not a hardware timing specification.
- A hardware builder's [86 board ADPCM memory modification](https://ameblo.jp/framgate/entry-12805172195.html)
  documents adding the missing sample RAM and recognizing the modified
  board in software.
- [MAME's 86 implementation](https://github.com/mamedev/mame/blob/master/src/devices/bus/pc98_cbus/pc9801_86.cpp)
  supplied a useful comparison, but its FIFO IRQ and overflow behavior
  contains explicit research TODOs and compatibility hacks. Those are
  not evidence of exact hardware behavior.

This pass does not establish cycle-exact hardware fidelity. Outstanding
items include CPU WAIT/backpressure on FIFO overflow (excess bytes are
currently dropped), precise request reassertion timing at the threshold,
partial-frame underrun behavior, capture/recording, analog attenuation
curves and FM/SSG balance, rhythm behavior during OPN compatibility mode,
and full 26K joystick support. The existing 86 joystick alias at 188h
while configured for 288h is a compatibility extension, not a claim
about the physical card's decode.

## Build

`cmake --build build --parallel 4` compiled the three changed sound
objects and linked `build/src/86Box.exe` successfully. The resulting
executable includes the preceding speaker/storage changes in the same
working tree. No guest execution or audio listening test was performed.

## Follow-up: PCM still silent

The user reports that LemonSpin still does not work with this build. The
mixer fix therefore must not be treated as a demonstrated complete remedy.

Further aligned P86DRV disassembly establishes a separate prerequisite:
`/76` parses the decimal number 76 and stores it as the requested number
of 16 KiB pages (082Dh). At 086Eh-0896h it checks for EMS, requests those
pages with INT 67h/AH=43h, and obtains the EMS page frame. This is
1,216 KiB of EMS backing for a sample file of approximately 1 MiB.

If EMS is absent or allocation fails, the conventional-memory fallback
at 089Ch-08C6h rejects requests of 41 or more pages. `/76` therefore
cannot use that fallback: it prints `メモリが確保出来ません。` (cannot
allocate memory) and exits with DOS errorlevel 1. `/M` disables EMS and
does not remedy this request.

`START.BAT` does not check errorlevel after P86DRV or PMP, so animation
alone does not prove that the sample driver initialized or loaded the
sample file.

### Live guest inspection

After the user left the demo running, read-only Windows process-memory
snapshots of `86Box-floppyfix.exe` established:

- Active VM at the time of inspection: `86box.cfg`; 386SX, 2,048 KiB
  configured RAM; BIOS reports 1,408 KiB extended RAM at physical 0401h.
- The -86 is at 188h, IRQ12, unmuted, PCM volume 7/15, DAC timer enabled.
- PMD's INT60h vector is `19C1:0103`, with its `PMD` resident signature.
  IRQ12's INT14h vector is `19C1:39F8`, the expected shared PMD handler.
- INT65h (P86DRV) and INT67h (EMS) both still point to `0060:3673`, a
  default DOS handler; neither resident signature is present.
- PMD's PCM running/sample-available flags and sample position/length
  are zero. The hardware FIFO, retained sample bytes and mixer output
  are zero; RUN is clear. CR3 is zero and no VEM486/VEMEMM text marker is
  present in conventional RAM.

Thus the observed run never supplies sample audio to the card: P86DRV
has not installed, and the `/76` request cannot succeed without EMS.
This isolates the current silence to guest memory/driver initialization;
it does not establish why the EMS drivers failed or prove the corrected
sound-card implementation's audible behavior.

The configured HDD is the supplied `lemonspin_freedos_sasi.hdi`. Its
`FDCONFIG.SYS` loads `VEM486.EXE /U` followed by `VEMEMM.SYS`; those files
are present. Its root AUTOEXEC calls `DEMO/START.BAT`. The bundled README
specifies a 386-class CPU and approximately 1.2 MiB of EMS for sound,
and explicitly labels the separate floppy image as having no sound.

Aligned VEM486 disassembly shows that its initial check at module
0C50h-0C8Dh tests writable FLAGS.NT, requires BIOS 0401h >= 8 (1 MiB
extended RAM), and requires real mode. The configured CPU and reported
memory satisfy those numerical requirements. A later snapshot showed
the VM paused (`dopause=1`) at the firmware memory check after a reset;
boot-driver messages are still needed to trace the actual rejection.

`tmp/pc98-sound-audit/read_live_pcm.py` reads only the selected 86Box
process. It does not execute guest code, inject calls, pause the VM or
write emulator memory. Its structure layout and TVRAM offset apply to
the current Windows build and must be updated if those layouts change.

### FreeDOS boot compatibility and DMA destination

The later boot screenshot exposed a disk initialization failure before
COMMAND.COM: FreeDOS reported missing drive parameters and assigned only
floppy drive letters. The user chose to retain FreeDOS with a guest-side
compatibility fix.

The supplied January 2022 kernel was rebuilt from fdkernel commit
`679eda7eeeac6cc68bad4d3ff9b055e672529731`. A fallback for the identified
PC-9801-27 ROM obtains AH=84h geometry from the initialized BIOS drive table
when the ROM reports success but leaves DX zero. That ROM dispatches AH=84h
to ordinary SENSE status and does not return the extended geometry FreeDOS
expects. The fallback preserves BIOS errors and other successful responses.
Source, license, installer and a separate HDI are provided; details are in
`tools/pc98_freedos_sasi_fix.md`.

The user's next screenshot confirms detection of the SASI partition and
installation of VEM486. COMMAND.COM still fails to load. A read-only process
snapshot at that prompt shows a successful READ of LBA 300, four 256-byte
blocks, containing valid root-directory entries in the SASI buffer. The
controller status and sense bytes are zero, but DMA channel 0 has page 70h
and base address 704374h, outside the installed 2 MiB of memory.

Disassembly of the -27 ROM's DMA setup at offsets 098Dh-09A7h explains the
address: ROL AX,4 on the buffer segment puts unrelated segment bits into
the upper nibble of the byte written to the DMA page register at 27h. The
intended low-memory destination is 04374h. NEC's hardware manual specifies
that A23-A20 are normally masked by 0439h bit 2. The previous motherboard
reset selected 24-bit DMA instead. Generic `dma_reset_legacy()` also runs
after motherboard initialization and could override its mask.

Both reset paths now select the normal 20-bit PC-98 DMA mask. Explicit
0439h writes still control access above 1 MiB, and page-latch readback and
restoration retain the full byte. The primary reference is the NEC
[PC-9800 Technical Data Book, Hardware, 1993](https://vtda.org/docs/computing/NEC/PC-9800TechnicalDataBookHARDWARE%2BOCR_1993.pdf),
printed pages 109-110 (PDF pages 122-123).

The updated motherboard and DMA objects compiled and `build/src/86Box.exe`
linked successfully. The running `86Box-floppyfix.exe` still has the older
DMA default until restarted with the new executable. The observed VEM486
boot reports 992 KiB available, below P86DRV's 1,216 KiB request; 4 MiB guest
RAM is therefore requested for the next boot. Neither successful shell
loading after this correction nor audible PCM has yet been demonstrated.

The user's next boot with the corrected executable and 4 MiB RAM still
fails to load COMMAND.COM. Its process snapshot confirms 3,040 KiB available
after VEM486, but DMA is again 24-bit (`dma_mask=FFFFFFh`), with the same
704374h directory destination. VEM486's startup code explicitly clears
0439h MSK at module offsets 2326h-2337h. Thus the corrected hardware reset
does not address this ROM's behavior after the memory manager initializes.

The revised guest kernel canonicalizes the BIOS transfer buffer for the
identified old -27 ROM immediately before both CHS and LBA transfer calls.
It preserves the physical address while expressing ES as a multiple of
1000h and BP as the within-bank offset: 0437:0004 becomes 0000:4374. The
ROM then emits a clean DMA page byte even with 24-bit DMA enabled. Other
controllers and all DMA control-port settings are preserved. FreeDOS's
existing low-memory bounce-buffer path handles high-memory disk buffers.

The driver library and kernel compiled. The result is packaged separately
as `converted/lemonspin_freedos_sasi_compat_dma.hdi`, with the matching full
GPL source and license in `converted/lemonspin-freedos-compat-dma`. This
replaces the geometry-only image for the next user boot; the older image
is retained for comparison.

### Working run after the guest correction

The user confirmed successful shell/demo startup and audio with the revised
HDI, `build/src/86Box.exe` and 4 MiB RAM. A read-only snapshot of this run
also establishes:

- DMA remains 24-bit, as requested by VEM486, but channel 0's page is 00h
  and its disk-transfer destination is now 04374h rather than 704374h.
- P86DRV is resident at INT65h `1382:0103`, with 76 EMS pages, page frame
  C000h and EMS handle 2.
- PMD is resident at INT60h `07A0:0103`. Its running and sample-available
  flags are both 1; sample position is 10,512 with 1,029,628 bytes remaining.
- The -86 is at 188h/IRQ12. PCM control is A3h (RUN set), mode F2h, threshold
  256, FIFO occupancy 270 bytes, and PCM IRQ asserted. Its DAC outputs and
  retained mixer samples are nonzero (observed buffer peak 6,331).

The snapshot is saved as
`tmp/pc98-sound-audit/boot/live-working-compat-dma.json`. The user's audible
result plus the resident-driver and FIFO state demonstrate working PCM for
this configuration. They do not establish the remaining hardware timing
details listed earlier. The current configuration mounts
`converted/lemonspin_freedos_sasi_compat_dma.hdi`.

# PC-9801VX EGC / GRCG audit

## Scope and result

The video core models a VX with its original NEC EGC, four planes and two
128 KiB graphics pages. This audit repairs functional mismatches in the CPU,
I/O and GDC routes. It does **not** establish cycle accuracy or confirm that
Rusty's scrolling problem is fixed. The initial audit used documentation.
The October 2 follow-up below incorporates the supplied DOS capture and
standalone regressions; a fresh guest run and physical VX comparison remain
unverified.

## Evidence

* NEC / ASCII, *PC-9800 Series Technical Data Book, supplement* (1987),
  printed pp.42–47, PDF pages 55–60:
  [archive listing](https://hackipedia.org/browse.cgi/Computer/Platform/PC%2C%20NEC%20PC-98/Collections).
  This is the original VX register and shifted-MOVS specification.
* NEC / ASCII, *Technical Data Book, Hardware* (1993), printed pp.190–194:
  [manual](https://vtda.org/docs/computing/NEC/PC-9800TechnicalDataBookHARDWARE%2BOCR_1993.pdf).
  Confirms GRCG modes, the EGC-equipped GDC route and mode-change unlocking.
* Terakawa, *EGC Manual*, TSG bulletin 181, printed pp.18–30:
  [original hardware experiments](http://www.yl.is.s.u-tokyo.ac.jp/~oiwa/tsg/buho/Old-buho_Project/181.pdf).
  Includes VX observations, compare values and shared-latch behavior.
* Kodaka / Kono, *UNDOCUMENTED 9801/9821 Vol.2*:
  [authors' original publication](https://www.webtech.co.jp/company/doc/undocumented_mem/index.html),
  `io_egc.txt` and `io_disp.txt`. Establishes register decoding and
  write-only VX ports. Its compare polarity and mask description conflict
  with the original NEC supplement and hardware experiments; those two
  descriptions were not adopted.
* *PC-9801 Programmer's Bible*, printed p.464:
  [manual](https://pc98.ne.jp/devdocs/pc-9801programmersbible.pdf).
  Describes the zero read seen during GDC drawing with the charger active.

MAME, NP2kai and DOSBox-X source were also inspected. They disagree on several
of these points and are comparison material, not proof of hardware behavior.

## Functional contracts and corrections

| Path | Contract / correction |
| --- | --- |
| Mode switching | `6Ah=07h` unlocks EGC switching; `04h/05h` select compatibility/extended mode; `06h` locks switching. |
| Register writes | Require extended mode and `7Ch` bit 7. Native aligned word OUT now reaches the core once instead of two partial OUTs. |
| Register reads | Original VX ports do not expose their stored values; open bus is returned. |
| Color/mask ports | Mask writes require pattern selection; color writes require FG/BG selection. Existing mask/color contents survive selection changes. |
| Foreground bank | Four physical 16-bit latches are shared with GRCG tiles. Tile writes repeat the byte into both lanes; color writes expand four color bits. |
| Compatibility RMW | CPU write data becomes the shared EGC mask, including zero in the inactive byte lane. |
| Compare read | Operation bit 13 = 1 compares active planes against foreground latches, independently of the selected ROP pattern input. |
| Lead-plane read | CPU receives unshifted selected-plane data; shifted four-plane source latches are separate. |
| CPU-source read | NEC returns the previous CPU VRAM write. A read must not clock VRAM into the CPU-source shifter. |
| Pattern loading | PLOAD_R loads the selected plane, or active planes in compare mode. PLOAD_W captures all active destination planes before the store. Both preserve unaddressed byte lanes. CPU-source loading occurs on writes, including priming writes. |
| ROP changes | Changing `4A4h` preserves a source already loaded. Shift/length writes rearm the stream; write-inhibited planes still clock CPU source data. |
| GDC route | Charger writes reach all enabled planes on the GDC-addressed page. Preliminary GDC reads see zero. GDC writes bypass CPU bit-address clipping and do not advance the CPU stream. |
| Odd CPU words | Two byte bus cycles at their actual physical addresses replace the old silently rounded-down word access. |

The existing scalar ROP truth table, GRCG TDW/TCR/RMW equations and reversal
within each byte agree with the documented pixel order. The word shifter's
priming write, first/last masks, direction and retained length were checked
against the supplement's transfer diagrams. Retained carry at a raster
boundary remains in the implementation; rewriting the ROP no longer clears it.

## Remaining accuracy limits

* Hardware documentation requires even-port word I/O and aligned 16-bit EGC
  VRAM transfers. Byte I/O remains a compatibility fallback; byte/mixed-width
  shifter behavior and invalid-access bus hangs are not hardware-exact.
* CPU WAIT cycles, DMA contention and concurrent mode writes are not modeled
  at the electrical bus level. Reset values and reserved modes need a
  physical VX measurement rather than an inference from emulator defaults.
* GRCG RMW reads are explicitly prohibited by NEC. Returning zero is a
  deterministic emulation policy, not a specified CPU read value.
* The TSG notes report an early VX quirk involving CPU-source ROPs with
  compare bit 13 clear. They do not fully specify the failed write result;
  no speculative no-op/data substitution was added.
* Nontrivial pattern-output shifts, carry across unusual read/write
  sequences, and the precise treatment of interleaved GDC/CPU accesses
  still need hardware traces. Do not label these paths exact on the basis
  of agreement with another emulator.

## Capture driven graphics corrections on October 2 2026

The supplied `converted/gfx-results/gfx-results.zip` contains four strict
partial-FIGS failures and three aggregate EGC failures. The accompanying
analysis identified useful leads; implementation decisions were checked
against the CSV rows, the test executable's register writes and NEC's manual.
The capture's machine description is blank, so it does not independently
identify the machine or executable that produced it.

| Evidence | Correction |
| --- | --- |
| `GDCSTATE.CSV`: `FIGS_DIFFP1_EQ_0`, `FIGS_DIFFP1_EQ_1`, `FIGS_SAMEP1_EQ_0`, `FIGS_SAMEP1_EQ_1` | A new FIGS establishes geometry defaults before its parameters arrive. The defaults helper preserves the pattern latch; the post-operation helper separately restores the PRAM pattern. |
| `EGCALL.CSV`: `B06_PLOAD_WRITE`, `W10_PLOADW_CAPTURE`, `W11_PAT_AFTER` | Separate write capture from read-plane selection. Capture the old destination across active planes before applying the ROP/store, preserving the other byte lane. |
| `GDCTIME.CSV`: similar completion times despite changing figure size | Replace immediate whole-figure execution with memory cycles proportional to generated pixels/words, including rectangles and zoomed graphics characters. |
| `GDCFIFO.CSV`: the safe burst never observes FULL | Consume command/parameter bytes over time. Stop draining input during memory execution; retain sixteen queued bytes and expose their status. |
| Core regression review | RESET aborts queued work, reads and DMA, including deasserting DRQ. DMA word reads step by a word without rotating the pixel mask. Read-transfer abortion clears its remaining count before CURD or another command. Overlong parameter streams cannot wrap into earlier fields. |

`EGCWRITE.EXE` programs ACCESS=`FFF0`, mode=`00FF`, OPE=`0200`, then
switches to OPE=`1000` to inspect the captured pattern. Thus the failing
write cases exercise all active planes with compare read disabled. The v1.3
read-suite note explicitly expects selected-plane PLOAD_R. These two paths
must have distinct selection rules. This correction follows the supplied
V166-oriented suite; matching every original VX revision still requires
physical measurements, particularly where the older TSG description differs.

### Command and memory timing

[NEC's December 1985 uPD7220 and uPD7220A User Manual](https://bitsavers.trailing-edge.com/components/nec/uPD7220/uPD7220-uPD7220A_User_Manual_Dec85.pdf)
provides command/parameter processing periods in table 3-1, printed p.3-6
(PDF p.30). Sections 2 and 4 describe memory-cycle work, and printed pp.5-1
and 5-2 describe FIFO turnaround, overflow and the RESET decoder ahead of
the FIFO. Those rules inform the new scheduler.

The core now runs one memory/DDA cycle at a time and releases drawing status
at completion. Commands such as ZOOM or PITCH no longer claim to be drawing.
RDAT fills over time and stalls when output has insufficient room; receiving
an opcode can abort it even when FULL is set. FIFO overflow replaces the
oldest queued byte, as NEC describes. RESET takes effect independently of
FIFO capacity.

The 86Box wrapper advances both cores from elapsed CPU time, preserving
fractional GDC clocks. It synchronizes before I/O and CPU VRAM accesses and
schedules an independent execution timer at the selected 2.5/5 MHz rate.
Raster callbacks continue supplying HBLANK/VSYNC but no longer advance
execution in fixed raster-sized batches. Idle cores do not need a periodic
execution tick.

This remains an execution-work model. Display/refresh arbitration,
flashless drawing windows, host bus WAIT cycles, clocked DMA acknowledgments
and the variable uPD7220 CURS P3 wait are not fully modeled. The current
CURS P3 delay uses the table's minimum. Reserved RDAT type `01` retains the
prior deterministic low-byte policy; it is not a verified hardware rule.
The captured UNDEF stage stopping at that probe is not claimed fixed.

### Validation and rerunning

`tests/video/pc98_graphics_test.c` exercises the seven functional regression
scenarios, selected-plane reads, byte-lane/plane inhibition, all 256 ROP
truth tables, FIFO pressure and overflow, intermediate VRAM updates,
work-dependent drawing lengths, RDAT refill/abort, DMA addressing/reset,
and long parameter streams. **358 checks pass** with the patched cores.
Compiling the same tests against saved pre-change sources produces
**15 failing scenarios**, including all seven functional reproductions.

An additional matrix compares five figure types, eight directions, four
RMW modes and three character zooms: **480 cases preserve the previous
VRAM, final EAD/mask/direction and write counts**. Its aggregate snapshots
are retained in the regression test. These characterize geometry already
present before this scheduler change; they are not physical-hardware oracles.

The standalone test build needs neither Qt nor GoogleTest:

```powershell
cmake -S tests/video/pc98 -B build/pc98-tests -G Ninja
cmake --build build/pc98-tests
ctest --test-dir build/pc98-tests --output-on-failure
```

The full MSYS2 MINGW64 Release build links successfully to
`build/src/86Box.exe`. The unattended offscreen/minimal Qt launch produced
no guest log or fresh results; the minimal attempt exited with Windows
status `0xC0000602` before observable guest startup. Boot the existing
`converted/pc98_gfx_tests.hdi` using the rebuilt executable to obtain the
full post-change DOS comparison. Keep the earlier results intact, shut
down the VM before extracting, and use a new output directory:

```powershell
python tools/pc98_gfx_test_hdi.py extract converted/pc98_gfx_tests.hdi converted/gfx-results-after
```

The image allocates a new `RESULTS\RUNnnn` folder on each run. A full-suite
pass, hardware timing equivalence and the undefined-opcode stage remain
unverified until that comparison is available.

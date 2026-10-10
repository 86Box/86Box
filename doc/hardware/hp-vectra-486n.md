# HP Vectra 486N (D26xxA)

The `vect486n` machine models the original Socket 1 D26xxA board with a VLSI
VL82C486 chipset, VL82C113 keyboard/RTC I/O, PC87311 Super I/O at 026Eh,
primary ISA IDE and the existing S3 86C924 ISA video device. It supports
2–48 MB RAM and 512 KB or 1 MB video memory.

Normal CPU selection is restricted to these Socket 1, 5 V Intel processors:

- 486SX-25 and 486SX-33
- 486DX-33 and 486DX-50
- 486DX2-50 and 486DX2-66
- i486DX OverDrive 25 (50 MHz core) and 33 (66 MHz core)

The original clock-doubled OverDrive entries use the existing DX2 emulation
with a 25/33 MHz bus and 2x multiplier. Intel's
[OverDrive datasheet, section 2](https://datasheets.chipdb.org/Intel/x86/486/applnots/29043606.PDF)
describes these 50/66 MHz upgrades. DX4 upgrades are not offered.
The machine name is **HP Vectra 486N (D26xxA)**; its configuration identifier
remains `vect486n` and its device settings remain in `[HP Vectra 486N]` so
existing BIOS and cache selections are preserved.

The global CPU override retains its usual meaning. Both fast and accurate
(SoftFloat) FPU emulation are supported. HP tests an unmasked empty-stack
FISTP m64 exception and its IRQ13 delivery; the fast FPU now handles this
without writing the destination or popping the empty stack. Both recompilers
call the instruction handler for FISTP m64 to preserve this behavior. SoftFloat
remains available for software requiring more complete x87 accuracy.

The complete T.04.05 flash image is the default. It permits F2 Setup,
error-message display, saved disk geometry and normal boot. Board I/O includes
the configuration EEPROM at 009Bh and auxiliary PIT at 009Ch–009Fh. Reading
009Bh bit 2 samples the active-low configuration-clear switch; writing that
bit drives the EEPROM clock. Treating it as a simple readback register made
POST erase the saved configuration every time. KBC input bit 5 reports the
CPU fan connected.

The board uses an HP-specific keyboard-controller command set. Its `DEh`
extended-command prefix and following payload are consumed by the controller,
including the one-byte `94h` and eight-byte `93h` payloads used by the BIOS.
Forwarding these bytes to the keyboard produced resend replies which DOS-era
BIOS calls could mistake for the controller command byte, disabling IRQ1 and
locking out input after boot. The extended commands' board/security side
effects are not yet modeled. Other VL82C113 machines retain their existing
controller selection.

Hardware reference: supplied motherboard photograph and HP's
[PC Service Handbook Volume 2, 9th edition, chapter 12, pp. 127–134](https://manuals.plus/m/29dac22b29f9684e28eacf47d75918be4e263d8e039219d220f46d13e455b255.pdf).
This is not the later D27xxA N/NI or N2.

## Firmware and configuration

Install these files under `roms/machines/vect486n/`:

| File | Size | Revision |
|---|---:|---|
| `t0405-flash.rom` | 262144 | T.04.05, 10/11/94 (default) |
| `f000-64k.rom` | 65536 | T.04.02, 08/27/92 (incomplete dump) |
| `c0202.rom` | 32768 | HP Ultra VGA C.02.02, 01/18/93 |

Only one system BIOS revision is required. Select it in the machine's
Configure dialog. T.04.02 is retained for comparison, but its missing flash
regions contain Setup and error-message modules. It cannot provide the same
Setup functionality as the complete T.04.05 image. T.04.03 and T.04.04 were
not recovered and are not offered as fabricated selections.

For the complete HP configuration:

```ini
[HP Vectra 486N]
bios = t0405
cpu_cache = 0

[Video]
gfxcard = internal

[S3 86c924 ISA]
bios = hp_vectra_486n_c0202
memory = 0
```

Video `memory = 0` selects 512 KB; `1` selects 1 MB. The reused S3 device
retains its existing AMI BIOS and 1 MB defaults; select the HP BIOS explicitly.
The HP variant uses the board's standard VGA palette interface (INMOS IMS
G176), while the AMI variant retains its SC11483 RAMDAC.

The 64 KB system block maps at F0000h. With the complete image, the standard
BIOS mappings expose its upper half at E0000h and all 256 KB at FFFC0000h.
HP's module loader accesses the lower 128 KB through F7FE0000h–F7FFFFFFh;
this alias is necessary for F2 Setup. VGA has its separate C0000h mapping.
Flash programming and every low-memory ROM bank-switch mode are not modeled.

### Authentic HP firmware extraction

HP's official [T.04.05 updater](https://ftp.hp.com/pub/softlib/software1/vc2007/vc2007en/t0405us.exe)
contains the ZIP member `RB0405US.BIN` (262160 bytes). Extract it without
running the updater and remove its 16-byte header:

```python
from pathlib import Path

member = Path("RB0405US.BIN").read_bytes()
assert len(member) == 262160
flash = member[16:]
system = flash[0x30000:]
assert system[0x93:0x9a] == b"T.04.05"
assert system[0xfff5:0xfffd] == b"10/11/94"
assert sum(system) % 256 == 0
Path("t0405-flash.rom").write_bytes(flash)
Path("c0202.rom").write_bytes(flash[:0x8000])
```

SHA-256:

```
105c1e45affea415ca19e54f8b8ca12e502d00cbb7002dddb781590f409d7f03  f000-64k.rom
72e659746507daf8e9d6c19f4a94b20a0dd13e45b398cd3f84f04ee84e3bccab  t0405-flash.rom
d8c16cc8611943a468a7484a628c5650f264495a957d158b8290f7e212ffbd01  c0202.rom
```

No reconstructed bytes or cache bypasses are stored in the ROM files.
The supplied `c000c5ff.rom` is a 24 KB C.01.00 shadow dump: its initialization
calls C000:6D49 and C000:7961 outside the dump, causing an invalid-opcode loop
at POST 07h. Appending the AMI S3 924 tail also fails because HP's references
and routine addresses differ. C.02.02 is a complete authentic replacement,
not proof of the missing C.01.00 bytes. The unrecovered C.01.00 region is
6000h–7FFFh, including entry points 680Ah, 6D28h, 6D49h, 6E88h, 733Ch,
745Fh, 74BAh, 7544h, 76EDh, 77DCh, 78EFh, 7929h, 793Ch and 7961h.

## Cache mode and BIOS patch

In the machine's configuration dialog, **Emulate CPU cache (Intel 486,
experimental, slow)** is unchecked by default. The equivalent configuration is:

```ini
[HP Vectra 486N]
cpu_cache = 0
```

- `0` (default): use the existing timing model and allow normal recompiler
  execution. Patch the loaded BIOS copy to skip POST 06h's cache diagnostic.
- `1`: enable the functional cache for the standard Intel 486SX/DX/DX2 CPU
  types, retain the original diagnostic, and use interpreter execution.
  Selecting an unsupported CPU logs a fallback to the patched mode.

Both revisions jump to the cache test at F000:048C. The bypass replaces that
jump with a short jump over one checksum-preserving byte to POST 07h:

| System BIOS | Patch address | Original bytes | Bypass bytes | POST 07h |
|---|---|---|---|---|
| T.04.02, 08/27/92 | F000:84FF | `E9 8A 7F` | `EB 01 06` | F000:8502 |
| T.04.05, 10/11/94 | F000:859D | `E9 EC 7E` | `EB 01 67` | F000:85A0 |

The skipped byte preserves the ROM's byte sum modulo 256, as checked by
POST 02h. No other firmware bytes change. The helper checks the 64 KB size,
version, date and surrounding POST/jump instructions before patching. An
unknown image is rejected in bypass mode; it is not patched speculatively.
Both original ROM files on disk remain unchanged in either cache mode.

## Functional CPU cache

The optional functional model supports the standard Intel 486SX,
486DX and DX2 CPU types: 8 KB unified instruction/data storage, four ways,
128 sets and 16-byte lines. Other machines and CPU types retain the existing
timing-only model. This is not a cycle-accurate implementation or an L2 cache.

- Read misses allocate whole lines, invalid ways are used first, and full
  sets use the Intel pseudo-LRU replacement tree. Write misses do not allocate.
- Writes normally update both cache hits and the independently mapped backing
  memory. CD inhibits fills; NW suppresses write-through and snoop invalidation
  on hits. Changing CD or chipset cacheability does not discard resident lines.
- INVD, WBINVD and CPU reset invalidate the cache. TR3-TR5 diagnostics share
  its tags, data, valid bits and replacement state. Cache-as-RAM writes remain
  in the cache when this model is active.
- VLSI MISCSET.CEN (07h), segment cacheability/write protection (13h-18h),
  and programmed-region cacheability (20h-23h) control fills and protected-line
  invalidation. Overlapping noncacheable regions take precedence. PMR bus
  remapping itself remains outside this implementation.
- CPU byte/word/dword/qword accesses, instruction fetches and normal 486
  page-table walks use the cache; PCD is honored for line allocation. DMA
  writes through the physical-memory API snoop it without reading cached data.

Cache-aware page-table walks use a separate implementation selected only when
the functional cache is enabled. With it disabled, the normal page-table walker
retains its original direct RAM accesses, without cache callbacks or PCD
bookkeeping. PAE translation is unchanged.

When the functional model is enabled, direct host RAM lookups and compiled
blocks are disabled because they bypass cache accesses. Selecting the dynamic
recompiler in this mode still uses its interpreter path. Expect lower host
performance; the default patched mode retains normal recompiler execution.
The existing address translator and diagnostic TLB remain separate; this work
does not add a hardware TLB. The OS installation tests below use the default
cache bypass, not this experimental functional cache.

Register decoding follows the original
[VLSI VL82C486 data manual, June 1992, pp. 20 and 27-28](https://ftpmirror.your.org/pub/misc/bitsavers/components/vti/pc/VTI_VL82C486_Data_Manual_199206.pdf).
CPU behavior follows the
[Intel i486 datasheet 240440-002, sections 5 and 8.2](https://www.ardent-tool.com/CPU/docs/Intel/486/datasheets/240440-002.pdf).

## Validation and limitations

The Qt 6 build passes with the restricted CPU list. Runtime enumeration reports
exactly the eight supported CPU selections above; loading a saved configuration
for each preserves its CPU family, core frequency and multiplier.

Windows NT 3.1 Workstation (3.10.511.1) completes both text and graphical
Setup from the 3.5-inch floppy distribution, boots from its FAT partition,
and logs in to Program Manager after a full emulator exit/relaunch. The test
uses DX2-50, 16 MB RAM, 512 KB HP VGA, a 252 MiB IDE disk (512/16/63),
T.04.05 and cache bypass. Set a period-appropriate date (tested with 1994)
and disable host time synchronization; NT 3.1 otherwise reports an invalid
system time with the host's 2026 date and this BIOS's 19xx century byte.

Windows NT 3.5 Workstation (3.50.807) completes CD installation and boots from
NTFS with the same board configuration, plus an ATAPI Toshiba XM-5302B slave.
Its built-in S3 driver passes the display test at 640×480 in 256 colors with
512 KB video memory, and Setup creates an emergency repair floppy. A full
emulator exit/relaunch with the normal build also logs in to Program Manager.
Both test installations omit networking. The supplied NT 3.5 boot disk 2 had a WINNT.SIF
with `MsDosInitiated=1`; a working copy with that file removed allows direct CD
installation without an MS-DOS staging partition. The original media is unchanged.

After the fast-FPU fix, the installed NT 3.1 system also passes POST and logs
in with SoftFloat disabled using the old recompiler. NT 3.5 passes the same
check with the new recompiler. These boot checks use DX2-50, T.04.05 and cache
bypass; the original full installation tests used SoftFloat.

The Qt 6 build succeeds. The 28 cache, diagnostic-TLB and BIOS-patch tests and
28 FISTP tests pass with AddressSanitizer and UndefinedBehaviorSanitizer.
LeakSanitizer is disabled because process inspection is unavailable in the test
sandbox.
The page-walker tests cover switching the cache on and off, accessed/dirty bits
for 4 KB and 4 MB pages, and page-fault addresses and error codes in both paths.
The FISTP tests cover both address sizes and both recompiler stack-tag formats:
masked and unmasked invalid operations, IRQ13/native exception selection,
memory faults, and exact 64-bit integer stores. Fast-FPU arithmetic and other
instructions retain their existing accuracy limitations.

MS-DOS 6.22 boots from a minimal floppy and accepts commands with both AT and
PS/2 keyboards on a DX4-100 with 8 MB RAM, T.04.05, SoftFloat and the CPU
interpreter (tested before restricting the CPU list). This reproduces and verifies the fix for issue #8254 without
requiring the reporter's hard-disk image or startup drivers.

The NT 3.5 CD setup test exposed stale ATAPI packet-transfer flags: HP's BIOS
writes FFh to the shared write-precompensation/Features register while reading
the hard disk, and NT 3.5 issues Device Reset followed by INQUIRY without
rewriting Features. Clearing Features on ATAPI Device Reset prevents the stale
DMA bit from requesting a bus-master transfer on ISA IDE. The trace changes
from failed DMA INQUIRY requests to successful PIO transfers and CD detection.

T.04.05 passes POST after configuring a 512-cylinder, 16-head, 63-sector IDE
disk and date/time through F2. Configuration persists through saving and
restarting. Both 512 KB and 1 MB HP VGA variants previously passed standalone
initialization, mode 03h scrolling and mode 13h memory checks.

The BIOS's CPU identification/speed measurement remains approximate: a
configured DX2-50 can be displayed as an OverDrive at 66 MHz. This does not
change the configured emulated CPU speed. Auxiliary timer calibration,
OverDrive identification, extended video clocks, all RAM sizes and flash
programming remain unverified. The functional CPU cache is experimental and
OS installation tests use its default disabled setting.

The existing Vectra 486VL uses different AMI AA.05.00 firmware and does not
run the 486N's observed cache-residency diagnostic; it does not require this
cache-test bypass.

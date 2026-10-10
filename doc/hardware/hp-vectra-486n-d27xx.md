# HP Vectra 486N (D27xxA)

The `vect486n_d27xx` machine is a separate Socket 2 model for the later
D27xxA generation. The existing Socket 1 D26xxA `vect486n` machine and its
settings are unchanged. The display name follows the existing chipset-prefix
convention: **[VLSI 82C486] HP Vectra 486N (D27xxA)**.

The board uses the existing VL82C486, VL82C113 and PC87311 emulation, with
primary ISA IDE, PS/2 keyboard/mouse and 2–48 MB RAM. Its Super I/O registers
are at 398h/399h, the default switch setting documented by HP. Normal CPU
selection offers the factory SX-S 25/33, DX 33 and DX2-S 50/66 configurations,
the 25/33 MHz bus 486 OverDrive upgrades, and Pentium OverDrive (P24T) 63/83
upgrades. Pentium OverDrive is available only on D27xxA, not D26xxA. The global
CPU override retains its usual meaning. Socket 2 machines use the project's
`CPU_PKG_SOCKET3` package mask; the machine is listed under Socket 2 in the UI.

Hardware reference: HP's [PC Service Handbook, Volume 2, chapter 13](https://manuals.plus/m/29dac22b29f9684e28eacf47d75918be4e263d8e039219d220f46d13e455b255.pdf).

## Firmware

Install both files under `roms/machines/vect486n_d27xx/`:

| File | Bytes | Contents |
|---|---:|---|
| `v0409-flash.rom` | 262144 | PhoenixBIOS (HP), V.04.09, 04/11/95 |
| `vga10100.rom` | 32768 | HP Vectra 486 N & M VESA VGA BIOS, 1.01.00 |

Both are extracted losslessly from the supplied `VB0409US.BIN`; no missing
VGA bytes need to be reconstructed. The system image includes Setup D.04.03
and the error-message modules. The BIOS selector uses the same revision/date
naming convention as the D26xxA. Only the recovered V.04.09 image is offered.
HP's handbook documents V.04.04 and V.04.08, and its service note mentions
V.04.07, but usable images of those revisions have not been located.

The [archived CAD Studio BBS listing](https://www.cadstudio.cz/bbs-hw.htm)
identifies `VBB1Z1US.EXE` as the V.04.09 updater for the M/MI/N/NI machines;
it is a historical listing, not an additional recovered ROM.

Extraction (without executing the updater):

```python
from pathlib import Path
import hashlib

image = Path("VB0409US.BIN").read_bytes()
assert len(image) == 262160
assert hashlib.sha256(image).hexdigest() == "af9d4f33ba116ec23387caf8dfe8b73b9917a506b16740f2324ec4dceda2cc6f"
flash = image[16:]
system = flash[0x30000:]
vga = flash[:0x8000]
assert system[0x30:0x38] == b"HPD2751A"
assert system[0x93:0x9a] == b"V.04.09"
assert system[0xfff5:0xfffd] == b"04/11/95"
assert sum(system) % 256 == 0
assert vga[:3] == bytes.fromhex("55 aa 40")
assert sum(vga) % 256 == 0
Path("v0409-flash.rom").write_bytes(flash)
Path("vga10100.rom").write_bytes(vga)
```

SHA-256:

```text
70e759b6a6a69c680f6be67e3dc8cc94eebb90b843de66c451f3adf5792c6167  v0409-flash.rom
b52dcbf4823ea3414fafb698836fdb98eb88dc4f7e83913ac3f1b764d5fa6806  vga10100.rom
```

## Board and video support

The system BIOS resides at F0000h. Unlike the D26xxA alias, the D27xxA
module loader selects four 64 KB flash banks at FFFF0000h through port 98h
bits 1:0. Values 3, 2, 1 and 0 select flash offsets 00000h, 10000h, 20000h
and 30000h respectively. The board owns this mapping; generic ROM and
chipset mappings are not changed. This supplies the original Setup and
error-message modules while the system BIOS executes from its low shadow.
The flash also contains a LAN boot ROM for the related NI configuration;
this N model does not add an integrated Ethernet controller.

The separate board device also implements the auxiliary PIT at 9Ch–9Fh and
the configuration EEPROM interface at 9Bh. Reading bit 2 samples the inactive
configuration-clear switch; writing it clocks the EEPROM. The EEPROM is saved
as `vect486n_d27xx_93c66.nvr`, separately from the D26xxA configuration.
The existing HP keyboard-controller command handling is selected only for
this machine entry.

Internal video is **HP Ultra VGA+ 805**, a new board variant of the existing
S3 86C805 implementation, with the original HP VGA ROM and 512 KB default
memory, expandable to 1 MB. The variant uses a Bt481 RAMDAC and ICS2494 clock
model; other S3 cards keep their existing defaults and paths.

```ini
[Machine]
machine = vect486n_d27xx

[HP Vectra 486N (D27xxA)]
bios = v0409

[Video]
gfxcard = internal

[HP Ultra VGA+ 805]
memory = 0
```

Use `memory = 1` for the 1 MB video upgrade.

V.04.09 boots without a cache-test ROM patch. The normal timing-based cache
model is used; no functional-cache option is exposed for this machine.
Both recompilers use the existing fast-FPU FISTP m64 exception handler for
D27xxA as well as D26xxA, because HP tests an unmasked empty-stack exception
and IRQ13 delivery. Other machines retain their compiled FISTP m64 path.

On first boot, enter F2 Setup, set the date and disk/interface configuration,
and save with F3. Uninitialized configuration may report errors, including a
parallel-port mismatch; saving valid settings clears these on the next POST.

## Limits

This models the firmware paths needed for POST, Setup and boot, not every
board feature. Flash programming, the integrated NI Ethernet hardware and
HP security/keyboard-controller side effects are not implemented. The BIOS's
CPU label and timing-derived MHz display can differ from the configured CPU;
the emulator continues to use the selected CPU and speed. Additional BIOS
revisions and all accelerated/high-color video modes have not been validated.

## Validation

Both old and new recompiler builds complete. The focused ASan/UBSan suite
passes 59 existing cache, TLB, ROM-guard, HP keyboard-controller and fast-FPU
tests. Runtime checks cover:

- V.04.09 POST, original F2 Setup, configuration save, reboot and EEPROM reload.
- MS-DOS 6.22 boot and keyboard input with the fast FPU on both recompilers,
  plus a 486SX-S configuration without an FPU.
- Original HP VBE firmware reporting 512 KB and 1 MB correctly, setting
  640×480×256 (101h) and 1024×768×256 (105h), and switching all video banks.
- The existing D26xxA T.04.05 configuration still passing its video, keyboard
  and FPU POST tests and booting MS-DOS with the new recompiler.
- Pentium OverDrive 63 and 83 passing V.04.09 POST and booting MS-DOS 6.22
  with 8 MB RAM, the new recompiler and fast FPU. Guest CPUID reports 1531h
  and 1532h respectively; the BIOS speed display reports 63 MHz for both.
  Normal CPU selection offers nine D27xxA choices. D26xxA offers six
  SX/DX/DX2 choices, excluding the separate 486 and Pentium OverDrive families.
- Clean installation of Windows NT 3.1 Workstation (3.10.511.1) from its
  22 floppy disks to FAT, using standard VGA.
- Clean installation of Windows NT 3.5 Workstation (3.50.807) from CD,
  including ATAPI detection, conversion to NTFS, and automatic S3 detection.
  Its built-in S3 driver passes the 640×480×256 display test with 512 KB VRAM.
- Both NT installations reaching Program Manager, shutting down normally,
  and retaining a guest-written file through a full emulator exit, cold boot
  and Administrator login. The NT 3.1 FAT volume checks clean offline;
  NT 3.5's NTFS volume passes CHKDSK. NT 3.1 also passes floppy write/readback.
- NT 3.5's Repair Disk Utility successfully creating an emergency repair
  floppy, with its setup log and compressed registry files verified on disk.

DOS validation used 8 MB main RAM. NT validation used the new recompiler,
fast FPU, an Intel 486DX2-S at 50 MHz, 16 MB RAM, and a 252 MiB primary IDE
disk (512 cylinders, 16 heads, 63 sectors). F2 Setup used matching custom
disk geometry, the Integrated disk interface, and a 1994 date with host time
synchronization disabled. The full memory/CPU range and NT on the old
recompiler have not been tested.

The supplied NT 3.5 boot disk 2 contains `WINNT.SIF` with
`MsDosInitiated=1`, which requires files staged by DOS. Direct CD installation
used a copy of that floppy with `WINNT.SIF` removed. No guest OS binaries,
firmware bytes or emulator code were changed for the NT tests.

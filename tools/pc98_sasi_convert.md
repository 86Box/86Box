# PC-98 MS-DOS HDI to SASI HDI

## Automatic import in the emulator

The PC-9801-27 now imports supported PC-98 MS-DOS FAT12/FAT16 HDI and NHD
images when their physical layout differs from its native 33-sector,
256-byte-sector layout. Select the original image as a SASI disk. The loader
creates `<original filename>.sasi.hdi`, opens that copy, and updates the disk
settings to point to it. A message identifies the copy; subsequent guest
writes and saves go there. The original is never written or replaced.

The importer keeps the source IPL/menu program and relocates each complete
FAT volume. FATs, directories, clusters, deleted data inside the volume, and
file contents remain intact. It changes partition CHS fields and the DOS
boot loader's physical-sector metadata. It supports the older NEC DOS boot
layout and the extended layout used by NEC DOS 6.x. No BIOS calls or machine
ROMs are patched, and the source DOS installation is retained.

Container access accepts physical sectors of 256, 512, 1024, 2048, or 4096
bytes. An NHD signature is recognized even under an `.hdi` filename. Native
SASI images open directly, and the legacy 17x512 padded-SASI view remains
available for images whose partition table is already at the SASI offset.
An ordinary 17x512 DOS image is imported instead of treating the upper half
of every seventeenth sector as padding.

Automatic import requires `IPL1`, supported MS-DOS partition IDs, exactly one
active DOS partition, and that partition's `IO.SYS`, `MSDOS.SYS`, and
`COMMAND.COM`. Up to eight FAT12/FAT16 partitions are preserved, provided
they fit a supported 5, 10, 20, or 40 MB drive. Overlapping/truncated volumes,
unknown boot-loader layouts, separate partition IPL locations, and unsupported
partition types produce an attachment error. These are hardware/boot-format
limits; the container extension alone does not establish SASI compatibility.

Existing imports are never overwritten. A fingerprint of the source header
and all imported data detects a changed source before reusing a copy. The
loader also checks for source changes during conversion. It publishes the
reusable-import marker only after copying and flushing the complete image.
If a previous import is incomplete or belongs to a changed source, move that
copy aside before importing again. A running or edited imported disk is
independent of its original after the disk setting has selected the copy.

Validation used the production C importer against local TH01, TH03 and Rusty
images: all 90, 27 and 449 file hashes matched, respectively. Complete FAT
volume bytes after the boot sector also matched. Synthetic Release and Debug
checks cover physical sector sizes, both DOS boot layouts, multiple partitions,
renamed NHD containers, malformed metadata, bounds, writes to imported copies,
and stale-import refusal. Guest boot/gameplay has not been verified.

Standalone regression build:

```powershell
cmake -S tests/disk/pc98 -B build/pc98-disk-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/pc98-disk-tests
ctest --test-dir build/pc98-disk-tests --output-on-failure
```

## Command-line FAT file migration

`pc98_sasi_convert.py` moves a PC-98 17-sector, 512-byte HDI's own MS-DOS
installation and files onto a new 33-sector, 256-byte SASI HDI. The source must
contain root-level `IO.SYS`, `MSDOS.SYS`, and `COMMAND.COM` files. Those files,
the source FAT boot code, and the rest of the source FAT files are copied to the
output.

The required `--template` image supplies only the target disk layout: its IPL,
partition table, geometry, and FAT BPB. Its existing DOS installation and files
are erased from the output. The converter retains the source boot loader and
copies the template's FAT parameters. It locates the source MS-DOS boot
metadata, recalculates the partition LBA, data-area offset, and physical sector
size for SASI, and updates standard BPB geometry fields when that DOS boot
sector uses them. This handles the distinct boot-field layouts found in older
PC-98 MS-DOS and NEC MS-DOS 6.x images. It requires matching logical FAT sector
sizes. Neither input image is modified.

The source and layout image both need an active PC-98 `IPL1` partition with a
FAT12 or FAT16 volume. The converter keeps 8.3 directory names as raw PC-98
bytes, volume labels, and file contents exactly, except for the optional
`AUTOEXEC.BAT` launch addition. `IO.SYS` and `MSDOS.SYS` are written first and
contiguously for the MS-DOS boot loader. Empty directories and long filename
entries are not migrated.

Example for Touhou 3:

```powershell
py -3 tools\pc98_sasi_convert.py `
  input/th03e.hdi `
  converted/th03e_sasi.hdi `
  --template input/lemonspin_freedos_sasi.hdi `
  --launch GAME.BAT
```

The example uses a FreeDOS SASI image only as a layout seed; the output boots
the MS-DOS installation from `th03e.hdi` and does not retain the seed's
FreeDOS files. `--launch` adds the requested command after the source
`AUTOEXEC.BAT`, keeping its PATH, drivers, and other startup setup. If the last
active source command already names that launch target, it is left alone.
Without `--launch`, the source startup script is retained unchanged.
The output uses the layout image's size (40 MB in this example). The converter
creates a new output path and refuses to overwrite an existing file.

This is a FAT file migration, not a raw-sector clone. It does not carry over
non-FAT partitions, deleted files, or physical sectors from the source image.
Booting and running the game must still be checked in the guest.

# PC-98 MS-DOS HDI to SASI HDI

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

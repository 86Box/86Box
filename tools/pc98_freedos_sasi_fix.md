# LemonSpin FreeDOS(98) compatibility image

The supplied LemonSpin SASI image uses FreeDOS(98) build 2043. Its disk
initialization expects INT 1Bh/AH=84h to return sector size and geometry.
The loaded PC-9801-27 ROM returns successful SENSE status for that function
without supplying geometry. FreeDOS rejects the disk because DX remains zero.

The compatibility kernel keeps FreeDOS and obtains geometry from the -27's
initialized per-unit ROM table when this specific old ROM response occurs.
It preserves BIOS errors and successful extended geometry responses. It
validates the ROM signature, dispatch entry, connected drive type and table
pointer before using the fallback; it does not substitute arbitrary geometry.

## Prepared files

- `converted/lemonspin_freedos_sasi_compat_dma.hdi`: a new copy of the supplied
  HDI with only its root KERNEL.SYS replaced and the associated FAT allocation
  updated. The original downloaded image is unchanged.
- `converted/lemonspin-freedos-compat-dma/KERNEL.SYS`: rebuilt, uncompressed
  FreeDOS(98) kernel; SHA-256
  `23fb1fc308e987583f014bcd522e4695837f60008ac0bc8df506890a5e0e60cf`.
- `converted/lemonspin-freedos-compat-dma/fdkernel-pc9801-27-source.zip`: full
  corresponding kernel source, including the change and original GPL license.
- `converted/lemonspin-freedos-compat-dma/COPYING`: license accompanying the kernel.

The earlier `lemonspin_freedos_sasi_compat.hdi` contains only the geometry
fallback. It still fails to load COMMAND.COM after VEM486 enables 24-bit DMA.

Boot this image with the updated `build/src/86Box.exe`. The emulator also
needs the DMA reset correction described below. LemonSpin's `P86DRV /76`
requests 76 pages of 16 KiB EMS (1,216 KiB). Use at least 4 MiB guest RAM;
the examined 2 MiB configuration leaves only 992 KiB available after VEM486.

## Reproducing the kernel and image

The source base is [lpproj/fdkernel, test-20220120-cherrypick](https://github.com/lpproj/fdkernel/tree/679eda7eeeac6cc68bad4d3ff9b055e672529731),
commit `679eda7eeeac6cc68bad4d3ff9b055e672529731`, matching the supplied
kernel's January 2022 release. `freedos98-pc9801-27.patch` contains the two
initialization changes, `pc9801_27_geometry.h`, the two disk-transfer call-site
changes and `pc9801_27_dma.inc`. The standalone copies of the helper files
are `freedos98_pc9801_27.h` and `freedos98_pc9801_27_dma.inc` in this directory.

The prepared source archive already includes that patch. It was built with
Open Watcom 2.0, NASM, the source's NEC98/DBCS/JAPAN configuration, `XCPU=86`,
`XFAT=16`, `XNASM=nasm`, `COMPILER=owwin` and `XLINK=wlink`. Set WATCOM,
INCLUDE and PATH for the local toolchain. Build the production utility and
driver libraries, create the source tree's empty `lib/libm.lib` with `wtouch`
as its top-level makefile does, then invoke:

```powershell
# Working directory: the extracted source's nec98/kernel directory.
wmake -ms -h -e -f makefile.wc production
```

The kernel output is `bin/KERNEL.SYS` at the source root. No UPX compression
is required. Avoid the top-level SYS.COM packing target with XUPXSYS unset:
that makefile can attempt to execute the DOS utility on the host.

To create a fresh image, from this repository root:

```powershell
python tools/pc98_freedos_sasi_fix.py `
  input/lemonspin_freedos_sasi.hdi `
  converted/lemonspin-freedos-compat-dma/KERNEL.SYS `
  converted/lemonspin_freedos_sasi_compat_dma.hdi
```

The installer requires the supplied original kernel's SHA-256 and refuses
an existing output. It stages a new image, writes the kernel and FAT copies,
flushes the image and renames it into place. A failed operation removes only
the staging file it created.

## DMA reset correction

The -27 ROM forms its DMA page byte by rotating the buffer segment four
bits. Its unused upper nibble can contain segment bits unrelated to the
physical address. NEC normally masks DMA A23-A20 through port 0439h/MSK=1.
The previous emulator reset enabled those bits, redirecting a directory
transfer intended for 04374h to 704374h in the observed boot.

The motherboard and generic DMA reset now both select the normal 20-bit
mask. Explicit guest writes to 0439h can still enable DMA above 1 MiB, and
the full page register byte remains readable and recoverable. See the NEC
[PC-9800 Technical Data Book, Hardware, 1993](https://vtda.org/docs/computing/NEC/PC-9800TechnicalDataBookHARDWARE%2BOCR_1993.pdf),
printed pages 109-110 (PDF pages 122-123).

VEM486 explicitly clears MSK during startup (module offsets 2326h-2337h)
and leaves 24-bit DMA active. A subsequent live snapshot with the corrected
emulator still shows the invalid 704374h destination. A reset correction
alone cannot resolve this ROM/memory-manager combination.

The revised guest disk-transfer helpers detect the same -27 ROM and
canonicalize ES:BP immediately before both CHS and LBA BIOS transfers.
ES becomes a multiple of 1000h and BP the offset within that physical
64 KiB bank, preserving the buffer's physical address. For example,
0437:0004 becomes 0000:4374. Rotating this ES produces a valid page byte
with no unused upper-nibble bits. Other controllers retain their original
buffer pointer, and DMA control ports are not modified. Conversion is
skipped if the pointer would wrap above physical 1 MiB; the existing
FreeDOS driver already uses its low-memory bounce buffer for high buffers.

The geometry fallback has been observed to detect the SASI partition and
allow VEM486 to install. The rebuilt emulator linked successfully. The user
confirmed that the revised DMA image loads the shell and LemonSpin has audio.
A read-only snapshot of that run shows DMA page 00h, destination 04374h,
P86DRV installed with 76 EMS pages, PMD playback active, a nonempty -86 PCM
FIFO and nonzero DAC/mixer samples. VEM486's 24-bit DMA setting remains active.

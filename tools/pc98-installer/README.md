# 86Box PC-98 source installer

This package adds the local PC-98 implementation to an upstream **86Box source
checkout**. It includes the committed work and current uncommitted emulator
fixes from the 2026-10-03 snapshot, plus regression suites, hardware audit
notes, and PC-98 disk tools. This is a source installer; building produces
the Windows executable. Python 3.10+ and Git are required.

The default upstream revision is
`e6256889a663f0482b4a68df750611989e20bd31`, checked against upstream master
on 2026-10-03. The previous integration baseline,
`fa1c79bb8b18f4815deb530c86500453cd0e4ff0`, has its own patch. Later revisions
are attempted through a three-way merge and refused if they conflict.

## Quick start on Windows

Extract the ZIP, open PowerShell in this folder, and run one of these:

```powershell
# Create a separate upstream checkout with PC-98 support.
.\Install-PC98.ps1 -CloneTo C:\projects\86Box-PC98

# Update an existing clean upstream source checkout.
.\Install-PC98.ps1 -SourcePath C:\projects\86Box

# Preview compatibility without changing source files.
.\Install-PC98.ps1 -SourcePath C:\projects\86Box -CheckOnly

# Install and compile using an existing MSYS2 MinGW64 toolchain.
.\Install-PC98.ps1 -CloneTo C:\projects\86Box-PC98 -Build
```

`INSTALL.cmd` also accepts these switches. Double-clicking it clones the
verified upstream revision into a new `86Box-PC98` folder next to the
installer and applies the patch. If PowerShell execution policy blocks the
`.ps1`, use the `.cmd` launcher; its policy override applies only to that
process. Nothing changes the machine-wide execution policy.

New clones use branch `codex/pc98`. Existing checkouts retain their branch
and HEAD. Source changes are left unstaged for review and committing. The
installer requires a clean tracked working tree, checks new-file collisions,
verifies the patch checksum, and merges in a temporary index. A conflicting
merge changes neither source files nor the real index. Upstream CI files,
Qt resources, and unrelated upstream changes are retained.

For a newer upstream master, explicitly choose `-Ref master` with `-CloneTo`.
That revision is unverified until it passes the merge and your build. The
installer does not fetch or switch branches in an existing source checkout.

## Build dependencies

The Windows build helper targets MSYS2 **MinGW64**, 64-bit GCC, Ninja, and
static Qt5, matching the local build. Install the normal upstream 86Box
dependencies first. In the MSYS2 MinGW64 terminal, the upstream dependency
list maps to:

```sh
pacman -S --needed git mingw-w64-x86_64-{ninja,cmake,gcc,pkgconf,openal,freetype,sdl3,zlib,libpng,rtmidi,libslirp,fluidsynth,qt5-static,qt5-translations,vulkan-headers,libsndfile,libserialport,make,rust,rust-src,cargo-c,corrosion,zstd}
```

Then run:

```powershell
.\Build-PC98.ps1 -SourcePath C:\projects\86Box-PC98 -Msys2Root C:\msys64
```

The result is `build-pc98\src\86Box.exe` inside the checkout. A build failure
leaves the installed source available for fixing dependencies and retrying.
The helper does not install packages, replace a running emulator, or launch
a VM. Other platforms can use their normal upstream CMake toolchain after
applying the source package with Python:

```sh
python3 install.py --source /path/to/86Box
cmake -S /path/to/86Box -B /path/to/86Box/build-pc98 -DCMAKE_BUILD_TYPE=Release -DRELEASE=ON
cmake --build /path/to/86Box/build-pc98 --parallel
```

## Undo

```powershell
.\Install-PC98.ps1 -SourcePath C:\projects\86Box -Uninstall
```

Original file bytes are backed up under `.pc98-install` before installation.
Undo verifies checksums, refuses edited installed files or a changed HEAD,
restores original files, removes files introduced by this package, and
retains the backup in a uniquely named `.pc98-backup-*` folder. `-CheckOnly
-Uninstall` checks undo first. Undo also recovers an interrupted installation
recorded by `pending.json`. Keep the backup until you have committed or
discarded the source changes. Backups remain untracked.

## PC-98 setup and scope

Select **NEC PC-9801VX** after building. Supply your existing firmware using
the layout documented in `doc/specifications/pc98-release-audit.md` in the
patched source. The BX4 profile is experimental; its limits are documented
in `pc98-bx4-experimental.md`. The source includes the text partition fix,
PC-98 storage and sound devices, input support, firmware handling, CPU
upgrade settings, and related shared-subsystem fixes present locally.

ROMs, disk images, VM settings, executables, debug captures, demo build
folders, the separate `98bridge-software` project, and SID conversion work
are excluded. Demo-specific source fixes and HDIs remain separate artifacts.
This installer makes no claim of complete physical-hardware accuracy.

`manifest.json` lists every source path, patch checksum, baseline, and local
snapshot revision. The two patch files can also be reviewed directly. The
source retains its existing copyright and license notices; `COPYING` is
included. Hardware audits document outside influences and validation limits.

The reusable exporter is `tools/build_pc98_installer.py` in the local
repository. Its `--upstream-checkout` must contain the verified upstream
revision, and `--output` must name a new package folder.

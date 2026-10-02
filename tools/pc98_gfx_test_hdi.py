#!/usr/bin/env python3
"""Build a PC-98 GDC/EGC test HDI, or extract its guest RESULTS directory."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import zipfile

from pc98_sasi_convert import ConversionError, Fat, FileEntry, Hdi, u16

ROOT = Path(__file__).resolve().parents[1]
DOWNLOADS = Path.home() / 'Downloads'


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dos_name(name: str) -> bytes:
    stem, dot, extension = name.upper().partition('.')
    if not re.fullmatch(r'[A-Z0-9_-]{1,8}', stem) or (dot and not
            re.fullmatch(r'[A-Z0-9_-]{1,3}', extension)):
        raise ConversionError(f'Unsupported DOS filename: {name}')
    return (stem.ljust(8) + extension.ljust(3)).encode('ascii')


def display_name(name: bytes) -> str:
    stem = name[:8].decode('ascii').rstrip()
    extension = name[8:].decode('ascii').rstrip()
    result = stem + ('.' + extension if extension else '')
    dos_name(result)  # Reject filesystem metacharacters before host extraction.
    return result


def crlf(text: str) -> bytes:
    return text.replace('\r\n', '\n').replace('\r', '\n').replace('\n', '\r\n').encode('ascii')


def normalize_batch(data: bytes) -> bytes:
    # These supplied archives actually contain CR CR LF, despite their README.
    return re.sub(rb'\r+\n|\r|\n', b'\r\n', data)


def payload(image: Hdi, item: FileEntry) -> bytes:
    return b''.join(image.read(offset, size) for offset, size in item.extents)


def package(path: Path, folder: str) -> dict[str, bytes]:
    files = {}
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise ConversionError(f'Archive CRC check failed: {path}')
        for item in archive.infolist():
            parts = item.filename.replace('\\', '/').split('/')
            if folder not in parts or item.is_dir():
                continue
            index = parts.index(folder)
            if len(parts) != index + 2:
                raise ConversionError(f'Unexpected nested package entry: {item.filename}')
            name = parts[-1].upper()
            dos_name(name)
            if name in files:
                raise ConversionError(f'Duplicate package filename: {name}')
            files[name] = archive.read(item)
        sums = next((name for name in archive.namelist() if name.endswith('/SHA256SUMS')), None)
        if sums:
            prefix = sums.rsplit('/', 1)[0] + '/'
            for line in archive.read(sums).decode('ascii').splitlines():
                digest, name = line.split(maxsplit=1)
                key = prefix + name.lstrip('*').removeprefix('./')
                if sha(archive.read(key)) != digest:
                    raise ConversionError(f'Bundled checksum mismatch: {key}')
    if not files:
        raise ConversionError(f'{path}: no {folder} package')
    return files


def extra_runner(executable: str, rcfile: str) -> bytes:
    return crlf(f'''@ECHO OFF
IF NOT EXIST GDCRES\\NUL MD GDCRES
IF EXIST MACHINE.TXT COPY MACHINE.TXT GDCRES\\MACHINE.TXT > NUL
{executable}
IF ERRORLEVEL 2 GOTO RC2
IF ERRORLEVEL 1 GOTO RC1
ECHO 0 > GDCRES\\{rcfile}.RC
GOTO DONE
:RC2
ECHO 2 > GDCRES\\{rcfile}.RC
GOTO DONE
:RC1
ECHO 1 > GDCRES\\{rcfile}.RC
:DONE
ECHO RETURNED > GDCRES\\DONE.TXT
''')


def launcher() -> bytes:
    # Keep command lines short enough for a DOS command processor.
    selectors = ''.join('FOR %%N IN (' + ' '.join(f'{n:03}' for n in range(start, min(start+10, 100))) +
                        ') DO CALL \\PICKRUN.BAT %%N\n' for start in range(1, 100, 10))
    text = f'''@ECHO OFF
CD \\
IF NOT EXIST \\RESULTS\\NUL MD \\RESULTS
SET TESTRUN=
{selectors.rstrip()}
IF "%TESTRUN%"=="" GOTO FULL
MD %TESTRUN%
IF NOT EXIST %TESTRUN%\\NUL GOTO MKFAIL
ECHO Started %TESTRUN% > \\RESULTS\\STARTED.TXT
ECHO %TESTRUN% > \\RESULTS\\LASTRUN.TXT
ECHO PC-98 graphics test capture > %TESTRUN%\\RUNINFO.TXT
ECHO Run uses the currently selected hardware and GDC clock. >> %TESTRUN%\\RUNINFO.TXT
ECHO Do not merge captures from different clocks or machines. >> %TESTRUN%\\RUNINFO.TXT
ECHO Capture directory: %TESTRUN%
'''
    stages = [
        ('SMOKE', 'GDCTEST', ['SMOKE.EXE', 'MACHINE.TXT', 'RUNSMOK.BAT'], 'RUNSMOK.BAT'),
        ('EGCFOCUS', 'EGCTEST', ['EGCREAD.EXE', 'EGCWRITE.EXE', 'RUNREAD.BAT',
                               'RUNWRITE.BAT', 'MACHINE.TXT'], 'RUNFOCUS.BAT'),
        ('GDC', 'GDCTEST', ['*.*'], 'RUNFULL.BAT'),
        ('PITCH', 'GDCTEST', ['GDCPITCH.EXE', 'MACHINE.TXT', 'RUNMONO.BAT'], 'RUNMONO.BAT'),
        ('EGC', 'EGCTEST', ['*.*'], 'RUNALL.BAT'),
        ('UNDEF', 'GDCTEST', ['GDCUNDEF.EXE', 'MACHINE.TXT', 'RUNUNDEF.BAT'], 'RUNUNDEF.BAT'),
    ]
    for name, suite, files, runner in stages:
        text += f'MD %TESTRUN%\\{name}\n'
        for file in files:
            text += f'COPY \\{suite}\\{file} %TESTRUN%\\{name}\\ > NUL\n'
        if name == 'EGCFOCUS':
            text += f'COPY \\RUNFOCUS.BAT %TESTRUN%\\{name}\\ > NUL\n'
        text += f'CD %TESTRUN%\\{name}\n'
        text += f'ECHO {name} RUNNING > %TESTRUN%\\STAGE.TXT\n'
        text += f'ECHO {name} STARTED >> %TESTRUN%\\PROGRESS.TXT\n'
        text += f'ECHO Running {name} tests...\n'
        text += f'CALL {runner} > CONSOLE.TXT\n'
        text += f'ECHO {name} RETURNED >> %TESTRUN%\\PROGRESS.TXT\n'
    text += '''CD \\
ECHO FINISHED > %TESTRUN%\\STAGE.TXT
ECHO All stage launchers returned. This is not an all-tests-PASS claim. > %TESTRUN%\\DONE.TXT
ECHO GDC safe profile: > %TESTRUN%\\SUMMARY.TXT
IF EXIST %TESTRUN%\\GDC\\GDCRES\\FULLSUM.TXT TYPE %TESTRUN%\\GDC\\GDCRES\\FULLSUM.TXT >> %TESTRUN%\\SUMMARY.TXT
ECHO EGC complete profile: >> %TESTRUN%\\SUMMARY.TXT
IF EXIST %TESTRUN%\\EGC\\EGCRES\\SUMMARY.TXT TYPE %TESTRUN%\\EGC\\EGCRES\\SUMMARY.TXT >> %TESTRUN%\\SUMMARY.TXT
ECHO GDC undefined profile: >> %TESTRUN%\\SUMMARY.TXT
IF EXIST %TESTRUN%\\UNDEF\\GDCRES\\UNDEFSUM.TXT TYPE %TESTRUN%\\UNDEF\\GDCRES\\UNDEFSUM.TXT >> %TESTRUN%\\SUMMARY.TXT
ECHO Finished. Preserve the complete %TESTRUN% folder, including failures.
GOTO END
:FULL
ECHO RUN001 through RUN099 already exist. Export results to make space.
GOTO END
:MKFAIL
ECHO Could not create the capture directory. Tests were not started.
:END
SET TESTRUN=
'''
    return crlf(text)


def readme(image_name: str) -> str:
    return f'''PC-98 GDC + EGC test HDI

Attach {image_name} as SASI disk 0 on the PC-9801-27 controller.
Physical geometry: 615 cylinders, 8 heads, 33 sectors/track, 256-byte
sectors (about 40 MiB). Use an EGC-equipped PC-98, e.g. the PC-9801VX.
The FreeDOS98 startup loads no EMS/XMS drivers or other TSRs.

The first boot automatically runs the tests. Later boots leave a prompt
so a reset after a hang does not automatically repeat the same experiment.
Run RUNTESTS from the root to start another capture. Every invocation
chooses a new RESULTS\\RUN001 ... RUN099 directory and never cleans old runs.

Result tree, for example:
  RESULTS\\RUN001\\SMOKE\\GDCRES       smoke test
  RESULTS\\RUN001\\EGCFOCUS\\EGCRES    focused read/write profiles first
  RESULTS\\RUN001\\GDC\\GDCRES         complete normal GDC profile
  RESULTS\\RUN001\\PITCH\\GDCRES       standalone monolithic GDCPITCH
  RESULTS\\RUN001\\EGC\\EGCRES         all six EGC executables
  RESULTS\\RUN001\\UNDEF\\GDCRES       reserved/undefined GDC probes last
Each stage also saves DOS stdout in CONSOLE.TXT. Native CSV/VRD/RC files
are the primary results. SUMMARY.TXT copies the suites' own summaries;
DONE.TXT only says the launchers returned, not that all checks passed.
PROGRESS.TXT and STAGE.TXT identify the last stage if execution hangs.

Both uploaded GDCTEST ZIPs are identical and included once as programs.
RUNFULL includes the four GDPIT chunks; the separate PITCH stage also
runs GDCPITCH so every supplied executable is scheduled. GDC undefined
probes can hang on some machines. Earlier captures are already on disk
in separate stage folders. After a hang, reset and export the partial run.

Before collecting labeled hardware/emulator evidence, edit MACHINE.TXT
in GDCTEST and EGCTEST, then run RUNTESTS. The supplied metadata starts
blank: the image does not guess your CPU/model or assert real hardware.
GDC tests record BIOS clock flags. Select 2.5 MHz or 5 MHz in the machine
configuration, reboot as needed, and start a new run for each clock.
Do not merge results from different machines, clocks or emulator builds.
A FAIL/INFO row is evidence; keep the complete capture.

Copy results out only after shutting down the VM to flush disk writes.
From this repository, extract the guest folder to a NEW host directory:
  python tools/pc98_gfx_test_hdi.py extract converted/{image_name} converted/gfx-results
This extracts the entire RESULTS tree, including completed and partial runs.
The HDI is writable; the companion JSON records initial free space.
Archive old runs or start with a fresh image when storage becomes low.

Original ZIPs are preserved in ARCHIVES. Test executables/documents are
unchanged; installed BAT files normalize CR CR LF into DOS CR LF.
FreeDOS98 and the patched PC-9801-27 kernel come from the existing local
lemonspin_freedos_sasi_compat_dma.hdi. COPYING is the bundled GPL license,
and FDKSRC.ZIP contains the corresponding patched kernel source.

Build checks: archive CRCs and EGC published checksums; original program
hashes; DOS boot-sector preservation; both FAT copies; file hashes;
directory links; no cross-linked or orphaned allocated clusters.
These checks do not establish that the guest tests pass or finish.
'''


def verify(image: Hdi, expected: dict[tuple[bytes, ...], str]) -> dict:
    volume = Fat(image)
    if image.read(volume.fat_start, len(volume.fat)) != image.read(
            volume.fat_start + len(volume.fat), len(volume.fat)):
        raise ConversionError('FAT copies differ')
    actual = {item.path: sha(payload(image, item)) for item in volume.source_files()}
    if actual != expected:
        raise ConversionError('Image file manifest differs from inputs')
    owners = set()

    def claim(chain):
        if owners.intersection(chain):
            raise ConversionError('Cross-linked FAT chain')
        owners.update(chain)

    def visit(first=None, parent=0):
        directory, chain = volume.directory(first)
        claim(chain)
        if first is not None and (directory[:11] != b'.          ' or
                u16(directory, 26) != first or directory[32:43] != b'..         ' or
                u16(directory, 58) != parent):
            raise ConversionError('Invalid dot/dotdot directory links')
        for pos in range(0, len(directory), 32):
            entry = directory[pos:pos+32]
            if entry[0] == 0:
                break
            if entry[0] == 0xE5 or entry[11] & 8 or entry[:11] in (b'.          ', b'..         '):
                continue
            cluster = u16(entry, 26)
            if entry[11] & 0x10:
                visit(cluster, first or 0)
            elif cluster:
                claim(volume.chain(cluster))

    visit()
    allocated = {c for c in range(2, volume.clusters + 2) if volume.get(c)}
    if owners != allocated:
        raise ConversionError('Orphaned allocated clusters')
    return dict(files=len(actual), free_bytes=(volume.clusters-len(owners))*volume.cluster_size,
                fat_copies_match=True, no_crosslinks_or_orphans=True)


def build(args) -> None:
    output = args.output.resolve()
    staging = output.with_suffix(output.suffix + '.partial')
    report_path = output.with_suffix('.json')
    readme_path = output.with_suffix('.txt')
    if any(p.exists() for p in (output, staging, report_path, readme_path)):
        raise FileExistsError('Output image, partial image, README or manifest already exists')
    gdc = package(args.gdc, 'GDCTEST')
    egc = package(args.egc, 'EGCTEST')
    same_gdc = args.gdc.read_bytes() == args.gdc_duplicate.read_bytes()
    if not same_gdc:
        raise ConversionError('GDC archives differ; review and package each version separately')
    gdc_exes = {name for name in gdc if name.endswith('.EXE')}
    egc_exes = {name for name in egc if name.endswith('.EXE')}
    invoked = {'SMOKE.EXE', 'GDCPITCH.EXE'}
    for files in (gdc, egc):
        for name, data in files.items():
            if name.endswith('.BAT') and name != 'CLEAN.BAT':
                invoked.update(match.decode() for match in re.findall(
                    rb'(?im)^([A-Z0-9]+\.EXE)[ \t]*$',
                    normalize_batch(data).replace(b'\r\n', b'\n')))
    if not (gdc_exes | egc_exes) <= invoked:
        raise ConversionError('A supplied executable is not covered by the launchers')
    for files in (gdc, egc):
        for name, data in files.items():
            if name.endswith('.EXE') and data[:2] != b'MZ':
                raise ConversionError(f'{name} is not a DOS MZ executable')
    gdc['RUNSMOK.BAT'] = extra_runner('SMOKE.EXE', 'SMOKE')
    gdc['RUNMONO.BAT'] = extra_runner('GDCPITCH.EXE', 'GDCPITCH')
    additions = {
        ('AUTOEXEC.BAT',): crlf('''@ECHO OFF
SET TZ=JST-9
CD \\
IF EXIST \\RESULTS\\STARTED.TXT GOTO EXISTING
CALL \\RUNTESTS.BAT
GOTO DONE
:EXISTING
ECHO Existing graphics-test captures are in \\RESULTS.
ECHO Type RUNTESTS to collect a new run. TYPE \\RESULTS\\LASTRUN.TXT shows the last one.
:DONE
'''),
        ('FDCONFIG.SYS',): crlf('DOS=LOW\nFILES=32\nBUFFERS=6\nLASTDRIVE=Z\n'),
        ('RUNTESTS.BAT',): launcher(),
        ('PICKRUN.BAT',): crlf('''@ECHO OFF
IF NOT "%TESTRUN%"=="" GOTO DONE
IF EXIST \\RESULTS\\RUN%1\\NUL GOTO DONE
SET TESTRUN=\\RESULTS\\RUN%1
:DONE
'''),
        ('RUNFOCUS.BAT',): crlf('@ECHO OFF\nCALL RUNREAD.BAT\nCALL RUNWRITE.BAT\n'),
        ('README.TXT',): crlf(readme(output.name)),
        ('FDKSRC.ZIP',): (ROOT/'converted/lemonspin-freedos-compat-dma/fdkernel-pc9801-27-source.zip').read_bytes(),
        ('ARCHIVES', 'GDC1.ZIP'): args.gdc.read_bytes(),
        ('ARCHIVES', 'GDC2.ZIP'): args.gdc_duplicate.read_bytes(),
        ('ARCHIVES', 'EGC13.ZIP'): args.egc.read_bytes(),
    }
    for folder, files in [('GDCTEST', gdc), ('EGCTEST', egc)]:
        for name, data in files.items():
            additions[(folder, name)] = normalize_batch(data) if name.endswith('.BAT') else data
    expected = {}
    source = Hdi(args.template)
    destination = None
    created = False
    source_sha = sha(args.template.read_bytes())
    try:
        src_volume = Fat(source)
        boot = source.read(0, src_volume.start + src_volume.bps)
        retained = [item for item in src_volume.source_files() if len(item.path) == 1 and
                    item.path[0] not in {dos_name(name) for name in
                        ('AUTOEXEC.BAT', 'AUTOEXEC.BAK', 'FDCONFIG.SYS', 'FDCONFIG.BAK')}]
        retained.sort(key=lambda item: (item.path != (dos_name('KERNEL.SYS'),), item.path))
        output.parent.mkdir(parents=True, exist_ok=True)
        with staging.open('xb') as target, args.template.open('rb') as origin:
            created = True
            shutil.copyfileobj(origin, target)
        destination = Hdi(staging, writable=True)
        volume = Fat(destination)
        volume.clear_for_install()
        volume.add_entry(None, Fat.make_entry(b'GFXTEST HDI', 8, 0))
        for item in retained:
            volume.write_file(source, item)
            expected[item.path] = sha(payload(source, item))
        volume.find_directory((dos_name('RESULTS'),))
        for names, data in additions.items():
            path = tuple(dos_name(name) for name in names)
            volume.write_file(None, FileEntry(path, Fat.make_entry(path[-1], 0x20, 0), []), data)
            expected[path] = sha(data)
        volume.sync()
        destination.file.flush()
        os.fsync(destination.file.fileno())
        if destination.read(0, len(boot)) != boot:
            raise ConversionError('Boot sectors changed')
        destination.close()
        destination = Hdi(staging)
        checks = verify(destination, expected)
        geometry = dict(cylinders=destination.cylinders, heads=destination.heads,
                        sectors_per_track=destination.spt, bytes_per_sector=destination.bps)
        destination.close()
        destination = None
        if sha(args.template.read_bytes()) != source_sha:
            raise ConversionError('Template image changed')
        staging.rename(output)
        created = False
        report = dict(image=str(output), sha256=sha(output.read_bytes()), geometry=geometry,
                      **checks, boot_sectors_unchanged=True, gdc_archives_identical=same_gdc,
                      scheduled_executables=sorted(gdc_exes | egc_exes), runtime_tested=False,
                      archives={p.name: sha(p.read_bytes()) for p in
                                (args.gdc, args.gdc_duplicate, args.egc)},
                      files_sha256={'/'.join(display_name(n) for n in path): digest
                                    for path, digest in expected.items()})
        with readme_path.open('x', encoding='ascii', newline='\n') as file:
            file.write(readme(output.name))
        with report_path.open('x', encoding='ascii', newline='\n') as file:
            json.dump(report, file, indent=2)
            file.write('\n')
        print(json.dumps({k: v for k, v in report.items() if k != 'files_sha256'}, indent=2))
    finally:
        if destination:
            destination.close()
        source.close()
        if created:
            staging.unlink(missing_ok=True)


def extract(args) -> None:
    output = args.directory.resolve()
    if output.exists():
        raise FileExistsError(f'Use a new output directory: {output}')
    image = Hdi(args.image)
    try:
        files = [item for item in Fat(image).source_files() if item.path[0] == dos_name('RESULTS')]
        if not files:
            raise ConversionError('No result files: boot the HDI and run RUNTESTS first')
        output.mkdir(parents=True, exist_ok=False)
        for item in files:
            relative = Path(*(display_name(name) for name in item.path[1:]))
            target = output / relative
            if not target.resolve().is_relative_to(output):
                raise ConversionError('Result path leaves output directory')
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as file:
                file.write(payload(image, item))
        print(f'Extracted {len(files)} result-tree files to {output}')
    finally:
        image.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode', required=True)
    builder = modes.add_parser('build', help='Create a new bootable test disk')
    builder.add_argument('--gdc', type=Path, default=DOWNLOADS/'GDCTEST.zip')
    builder.add_argument('--gdc-duplicate', type=Path, default=DOWNLOADS/'GDCTEST (2).zip')
    builder.add_argument('--egc', type=Path, default=DOWNLOADS/'pc98-egc-tests-v1.3-real-hardware (2).zip')
    builder.add_argument('--template', type=Path, default=ROOT/'converted/lemonspin_freedos_sasi_compat_dma.hdi')
    builder.add_argument('--output', type=Path, default=ROOT/'converted/pc98_gfx_tests.hdi')
    reader = modes.add_parser('extract', help='Export the complete RESULTS tree after VM shutdown')
    reader.add_argument('image', type=Path)
    reader.add_argument('directory', type=Path, help='New host directory; existing paths are refused')
    args = parser.parse_args()
    try:
        (build if args.mode == 'build' else extract)(args)
    except (OSError, ValueError, ConversionError, zipfile.BadZipFile) as error:
        print(f'Error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

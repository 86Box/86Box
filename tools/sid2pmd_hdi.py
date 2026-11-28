#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Package the Evolver quick test on the existing FreeDOS PC-98 SASI image.

Run after converting EVOLVER.M and TEST.M into converted/sid2pmd.
The template is read only; the new image is checked before being published.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import math

from pc98_sasi_convert import Fat, FileEntry, Hdi, ConversionError
from pc98_gfx_test_hdi import crlf, dos_name, display_name, payload, sha, verify

ROOT = Path(__file__).resolve().parents[1]
README = """SID2PMD - Evolver 6581 quick test

PC-98 with PC-9801-86 sound board at 188h, IRQ 12, and 4 MiB RAM.
Attach this HDI as SASI unit 0 on the PC-9801-27 controller.
Geometry: 615 cylinders, 8 heads, 33 sectors, 256 bytes/sector.
Use the controller's automatic drive profile. FreeDOS boots and autoplays.

REPLAY    Play Evolver again (complete 222-second capture).
STOP      Stop music.
DIGITEST  Play a two-second original SID fixture with D418 PCM and FM/SSG.

EVOLVER.M approximates SID voices with FM/SSG, envelopes, detune and noise.
Effects update about every 120 ms to fit PMD86's 40 KiB resident buffer.
This Evolver tune has no detected fast D418 digis; TEST.P86 tests that path.
The original full SID is included as MUSIC/EVOLVER.SID for reference.
Playback uses PMD86 4.8o, P86DRV 1.1c and PMP.
PMD86 targets NEC PC-98; it does not target the unrelated Tesla PMD 85.

FreeDOS license files are retained. FDKSRC.ZIP contains the source for the
SASI-compatible kernel in this image. Host converter: tools/sid2pmd.py.
"""


def build(template, source_sid, output, score=None, verify_image=True):
    staging = output.with_suffix('.hdi.partial')
    manifest = output.with_suffix('.json')
    readme = output.with_suffix('.txt')
    if any(p.exists() for p in (output, staging, manifest, readme)):
        raise FileExistsError('Choose a new output name; image or sidecar already exists')
    score = score or ROOT / 'converted/sid2pmd/EVOLVER.M'
    music = score.parent
    song = score.stem.upper()
    score_name = song+'.M'
    report_path = score.with_suffix('.json')
    conversion = json.loads(report_path.read_text(encoding='utf-8')) if report_path.exists() else {}
    parts = conversion.get('parts', [dict(conversion, file=score.name)])
    title = conversion.get('title', source_sid.stem).encode('ascii', 'replace').decode()
    duration = conversion.get('duration_seconds', 222)
    adaptive = any(part.get('reduction') is not None for part in parts)
    conversion['operator_tolerance_db'] = max(part.get('operator_tolerance_db',0) for part in parts)
    conversion['pitch_tolerance_cents'] = max(part.get('pitch_tolerance_cents',0) for part in parts)
    description = README.replace('Evolver 6581', title).replace('Evolver', title)
    description = description.replace('222-second', f'{duration:g}-second').replace('EVOLVER.', song+'.')
    if adaptive:
        description = description.replace("Effects update about every 120 ms to fit PMD86's 40 KiB resident buffer.",
            'Controls track SID rate-counter envelopes, pitch, PWM and resonant filters.\n'
            'FM operator levels change during notes for routed LP/BP/HP filter sweeps.\n'
            f'Adaptive precision: {conversion.get("operator_tolerance_db", 0):g} dB, '
            f'{conversion.get("pitch_tolerance_cents", 0):g} cents. Gate edges are retained.')
    pcm = any(part.get('pcm_bytes',0) > 0 for part in parts)
    pages = max(76, max(part.get('pcm_ems_pages',76) for part in parts))
    if pages > 100:
        description = description.replace('4 MiB RAM', '8 MiB RAM')
    if pcm:
        description = description.replace(f'This {title} tune has no detected fast D418 digis; TEST.P86 tests that path.',
                                           'Volume-register digis are included in the song PCM bank.')
    else:
        description = description.replace(f'This {title} tune has no detected fast D418 digis; TEST.P86 tests that path.',
                                           'No fast D418 digis were detected; TEST.P86 exercises PCM separately.')
    drivers = ROOT / 'tmp/pc98-sound-audit/lemonspin'
    additions = {
        ('AUTOEXEC.BAT',): crlf(f'@ECHO OFF\nSET PATH=\\\nCD \\\nPMD86 /M40 /V0 /E0\nP86DRV /{pages}\nECHO SID2PMD: {title}. Commands: REPLAY, STOP, DIGITEST.\nCALL REPLAY.BAT\n'),
        ('REPLAY.BAT',): crlf(f'@ECHO OFF\nCD \\MUSIC\n\\PMP {score_name}\n'),
        ('STOP.BAT',): crlf('@ECHO OFF\n\\PMP /S\n'),
        ('DIGITEST.BAT',): crlf('@ECHO OFF\nCD \\MUSIC\n\\PMP TEST.M\n'),
        ('README.TXT',): crlf(description),
        ('FDKSRC.ZIP',): (ROOT / 'converted/lemonspin-freedos-compat-dma/fdkernel-pc9801-27-source.zip').read_bytes(),
        ('MUSIC', song+'.SID'): source_sid.read_bytes(),
    }
    for part in parts:
        name = part['file']
        additions[('MUSIC', name)] = (music/name).read_bytes()
        if part.get('pcm_bytes',0):
            additions[('MUSIC', Path(name).with_suffix('.P86').name)] = (music/name).with_suffix('.P86').read_bytes()
    for name in ('TEST.M', 'TEST.P86'):
        additions[('MUSIC', name)] = (ROOT / 'converted/sid2pmd' / name).read_bytes()
    if conversion.get('segmented'):
        wait_binary = ROOT/'build/sid2pmd/WAIT.COM'
        nasm = shutil.which('nasm') or 'C:/msys64/mingw64/bin/nasm.exe'
        subprocess.run([nasm,'-f','bin',str(ROOT/'tools/sid2pmd/wait.asm'),'-o',str(wait_binary)],check=True)
        additions[('WAIT.COM',)] = wait_binary.read_bytes()
        playlist = ['@ECHO OFF', 'CD \\MUSIC']
        for part in parts:
            playlist += [f'\\PMP {part["file"]}', f'\\WAIT {math.ceil(part["pmd_duration_seconds"]*100)}', 'IF ERRORLEVEL 1 GOTO END']
        playlist += [':END', '\\PMP /S']
        additions[('REPLAY.BAT',)] = crlf('\n'.join(playlist)+'\n')
        description += '\nPlayback is split into '+str(len(parts))+' automatically sequenced scores.\n'
        description += 'Short disk-loading gaps between sections are possible. ESC ends the playlist.\n'
        additions[('README.TXT',)] = crlf(description)
    for name in ('PMD86.COM', 'P86DRV.COM', 'PMP.COM'):
        additions[(name,)] = (drivers / name).read_bytes()
    expected = {}
    original_hash = sha(template.read_bytes())
    source, destination = Hdi(template), None
    created = False
    try:
        src = Fat(source)
        boot = source.read(0, src.start + src.bps)
        retained = [item for item in src.source_files() if len(item.path) == 1 and
                    item.path[0] not in {dos_name(n) for n in ('AUTOEXEC.BAT', 'AUTOEXEC.BAK', 'FDCONFIG.BAK', 'README.TXT')}]
        retained.sort(key=lambda item: (item.path != (dos_name('KERNEL.SYS'),), item.path))
        output.parent.mkdir(parents=True, exist_ok=True)
        with staging.open('xb') as target, template.open('rb') as origin:
            created = True
            shutil.copyfileobj(origin, target)
        destination = Hdi(staging, writable=True)
        volume = Fat(destination)
        volume.clear_for_install()
        volume.add_entry(None, Fat.make_entry(b'SID2PMD    ', 8, 0))
        for item in retained:
            volume.write_file(source, item)
            expected[item.path] = sha(payload(source, item))
        for names, data in additions.items():
            path = tuple(dos_name(n) for n in names)
            volume.write_file(None, FileEntry(path, Fat.make_entry(path[-1], 0x20, 0), []), data)
            expected[path] = sha(data)
        volume.sync()
        destination.file.flush()
        os.fsync(destination.file.fileno())
        if verify_image and destination.read(0, len(boot)) != boot:
            raise ConversionError('Boot sectors changed')
        checks = verify(destination, expected) if verify_image else dict(verification_skipped=True, files=len(expected))
        geometry = dict(cylinders=destination.cylinders, heads=destination.heads,
                        sectors_per_track=destination.spt, bytes_per_sector=destination.bps)
        destination.close(); destination = None
        if verify_image and sha(template.read_bytes()) != original_hash:
            raise ConversionError('Template image changed')
        staging.rename(output); created = False
        report = dict(image=str(output), sha256=sha(output.read_bytes()), geometry=geometry,
                      boot_sectors_unchanged=True if verify_image else None, conversion=conversion, **checks,
                      files_sha256={'/'.join(display_name(n) for n in path): digest for path, digest in expected.items()})
        manifest.write_text(json.dumps(report, indent=2)+'\n', encoding='ascii')
        readme.write_text(description, encoding='ascii')
        print(f'Created {output}: {checks}')
    finally:
        source.close()
        if destination: destination.close()
        if created: staging.unlink(missing_ok=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--template', type=Path, default=ROOT/'converted/lemonspin_freedos_sasi_compat_dma.hdi')
    parser.add_argument('--sid', type=Path, default=Path.home()/'Downloads/Evolver_6581.sid')
    parser.add_argument('--output', type=Path, default=ROOT/'converted/sid2pmd/EVOLVER_BOOT.hdi')
    parser.add_argument('--score', type=Path, default=ROOT/'converted/sid2pmd/EVOLVER.M')
    parser.add_argument('--no-verify', action='store_true', help='package without post-build verification')
    args = parser.parse_args()
    build(args.template.resolve(), args.sid.resolve(), args.output.resolve(), args.score.resolve(), not args.no_verify)

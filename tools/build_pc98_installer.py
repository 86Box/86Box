#!/usr/bin/env python3
"""Export local PC-98 work as reviewed patches against two upstream revisions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from zipfile import ZipFile, ZIP_DEFLATED

BASE = 'fa1c79bb8b18f4815deb530c86500453cd0e4ff0'
CURRENT = 'e6256889a663f0482b4a68df750611989e20bd31'
ROOT = Path(__file__).resolve().parents[1]


def command(root, *args, env=None, input=None, check=True):
    p = subprocess.run(['git', '-c', 'core.safecrlf=false', '-C', str(root), *args],
                       env=env, input=input, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and p.returncode:
        raise RuntimeError(p.stderr.decode('utf-8', 'replace'))
    return p.stdout if check else p


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(reference, destination):
    if destination.exists():
        raise FileExistsError('Choose a fresh output folder: ' + str(destination))
    candidates = command(ROOT, 'diff', '--name-only', '-z', BASE).split(b'\0')
    candidates += command(ROOT, 'ls-files', '--others', '--exclude-standard', '-z',
                          'src', 'tests', 'tools', 'doc/specifications').split(b'\0')
    names = []
    for raw in candidates:
        if not raw:
            continue
        name = raw.decode('utf-8')
        relevant = (name == 'CMakeLists.txt' or name.startswith(('src/', 'tests/',
                    'doc/specifications/pc98')) or
                    name.startswith(('tools/pc98_', 'tools/freedos98_pc9801_27',
                                     'tools/freedos98-pc9801-27')))
        if relevant and (ROOT / name).is_file():
            names.append(name)
    destination.mkdir(parents=True)
    patches = []
    with tempfile.TemporaryDirectory(prefix='pc98-export-') as temporary:
        repo = Path(temporary)
        command(repo, 'init', '--quiet')
        object_dirs = []
        for source in (ROOT, reference):
            common = command(source, 'rev-parse', '--git-common-dir').decode().strip()
            object_dirs.append((source / common / 'objects').resolve().as_posix())
        (repo / '.git/objects/info/alternates').write_bytes(('\n'.join(object_dirs)+'\n').encode())
        command(repo, 'read-tree', BASE)
        selected = []
        for name in sorted(set(names)):
            data = (ROOT / name).read_bytes()
            if b'\0' not in data:
                data = data.replace(b'\r\n', b'\n')
            before = command(repo, 'show', BASE + ':' + name, check=False)
            if before.returncode == 0 and before.stdout == data:
                continue
            blob = command(repo, 'hash-object', '-w', '--stdin', input=data).decode().strip()
            command(repo, 'update-index', '--add', '--cacheinfo', '100644', blob, name)
            selected.append(name)
        original = command(repo, 'diff', '--cached', '--binary', '--full-index', BASE)
        (destination / 'pc98-baseline.patch').write_bytes(original)
        patches.append(dict(file='pc98-baseline.patch', base_revision=BASE, sha256=sha(original)))
        command(repo, 'read-tree', CURRENT)
        p = command(repo, 'apply', '--cached', '--3way', str(destination / 'pc98-baseline.patch'), check=False)
        if p.returncode:
            conflicts = command(repo, 'diff', '--cached', '--name-only', '--diff-filter=U').decode().splitlines()
            # Upstream added sound tests where the local PC-98 suites were added.
            # Resolve only this reviewed insertion, retaining both test groups.
            if conflicts != ['tests/CMakeLists.txt']:
                raise RuntimeError('Unexpected upstream conflicts: ' + repr(conflicts))
            theirs = command(repo, 'show', ':2:tests/CMakeLists.txt')
            local = command(repo, 'show', ':3:tests/CMakeLists.txt')
            ancestor = command(repo, 'show', ':1:tests/CMakeLists.txt')
            marker = b'add_subdirectory(mem)\n'
            if theirs != ancestor.replace(marker, marker + b'add_subdirectory(sound)\n', 1):
                raise RuntimeError('Upstream test registration differs from the reviewed insertion.')
            resolved = local.replace(marker, marker + b'add_subdirectory(sound)\n', 1)
            blob = command(repo, 'hash-object', '-w', '--stdin', input=resolved).decode().strip()
            command(repo, 'update-index', '--cacheinfo', '100644', blob, 'tests/CMakeLists.txt')
        current_patch = command(repo, 'diff', '--cached', '--binary', '--full-index', CURRENT)
        (destination / 'pc98-current-master.patch').write_bytes(current_patch)
        patches.insert(0, dict(file='pc98-current-master.patch', base_revision=CURRENT, sha256=sha(current_patch)))
        # Every upstream file outside this package must retain its Git blob and mode.
        changed = command(repo, 'diff', '--cached', '--name-only', CURRENT).decode().splitlines()
        if set(changed) - set(selected):
            raise RuntimeError('Export modified unexpected upstream files.')
    for name in ('install.py', 'Install-PC98.ps1', 'Build-PC98.ps1', 'INSTALL.cmd', 'README.md'):
        shutil.copy2(ROOT / 'tools/pc98-installer' / name, destination / name)
    shutil.copy2(ROOT / 'COPYING', destination / 'COPYING')
    manifest = dict(format_version=1, package_date='2026-10-04',
                    upstream_url='https://github.com/86Box/86Box.git',
                    default_upstream_revision=CURRENT, verified_upstream_revisions=[CURRENT, BASE],
                    local_head=command(ROOT, 'rev-parse', 'HEAD').decode().strip(),
                    includes_uncommitted_source=True, source_paths=selected, patches=patches,
                    excluded=['ROMs', 'disk images', 'VM configs', 'build outputs', 'debug captures',
                              '98bridge-software', 'SID conversion project'],
                    upstream_resolution='Retain upstream sound test registration alongside local PC-98 suites.')
    (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    archive = destination.with_suffix('.zip')
    with ZipFile(archive, 'x', ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(destination.iterdir()):
            z.write(path, destination.name + '/' + path.name)
    archive.with_suffix('.zip.sha256').write_text(sha(archive.read_bytes())+'  '+archive.name+'\n')
    print(json.dumps(dict(folder=str(destination), archive=str(archive), source_files=len(selected),
                          upstream=CURRENT, zip_sha256=sha(archive.read_bytes())), indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--upstream-checkout', type=Path, required=True,
                   help='Git checkout containing current upstream master objects')
    p.add_argument('--output', type=Path, default=ROOT/'converted/86Box-PC98-Installer-20261004')
    a = p.parse_args()
    build(a.upstream_checkout.resolve(), a.output.resolve())

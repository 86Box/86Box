#!/usr/bin/env python3
"""Install a packaged PC-98 source snapshot into an 86Box Git checkout."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tempfile


class InstallError(Exception):
    pass


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(args, cwd=None, env=None, check=True):
    p = subprocess.run(args, cwd=cwd, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if check and p.returncode:
        raise InstallError(p.stderr.decode('utf-8', 'replace').strip() or
                           p.stdout.decode('utf-8', 'replace').strip())
    return p


def git(root, *args, env=None, check=True):
    if env is None:
        env = os.environ.copy()
        for name in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE'):
            env.pop(name, None)
    return run(['git', '-c', 'core.safecrlf=false', '-C', str(root), *args],
               env=env, check=check)


def safe_path(root, name):
    parts = PurePosixPath(name).parts
    if (not parts or name != '/'.join(parts) or '\\' in name or ':' in name or
            any(p in ('', '.', '..') or p.lower() == '.git' for p in parts) or
            PurePosixPath(name).is_absolute()):
        raise InstallError('Invalid package path: ' + name)
    path = root.joinpath(*parts)
    if not path.resolve().is_relative_to(root.resolve()):
        raise InstallError('Path escapes source folder: ' + name)
    for parent in (path, *path.parents):
        if parent == root:
            break
        if parent.is_symlink():
            raise InstallError('Symlink in installation path: ' + name)
    if path.exists() and not path.is_file():
        raise InstallError('Expected a regular file: ' + name)
    return path


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.pc98-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
        os.chmod(temporary, (path.stat().st_mode & 0o777) if path.exists() else 0o644)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def source_hash(path):
    return digest(path.read_bytes()) if path.exists() else None


def check_installed(root, state):
    for item in state['files']:
        path = safe_path(root, item['path'])
        if source_hash(path) != item['installed_sha256']:
            raise InstallError('Installed file was edited; preserve your changes before undo: ' +
                               item['path'])


def restore(root, state_dir, records):
    for item in records:
        path = safe_path(root, item['path'])
        if item['before_sha256'] is None:
            path.unlink(missing_ok=True)
        else:
            saved = safe_path(state_dir / 'backup', item['path'])
            data = saved.read_bytes()
            if digest(data) != item['before_sha256']:
                raise InstallError('Backup checksum failed: ' + item['path'])
            atomic_write(path, data)


def uninstall(root, state_dir, check_only=False):
    state_file = state_dir / 'state.json'
    recovering = False
    if not state_file.is_file() and (state_dir / 'pending.json').is_file():
        state_file = state_dir / 'pending.json'
        recovering = True
    if not state_file.is_file():
        raise InstallError('No PC-98 installation record in this checkout.')
    state = json.loads(state_file.read_text(encoding='utf-8'))
    head = git(root, 'rev-parse', 'HEAD').stdout.decode().strip()
    if head != state['upstream_head']:
        raise InstallError('HEAD changed since installation; undo on the original checkout revision.')
    if git(root, 'diff', '--cached', '--quiet', check=False).returncode:
        raise InstallError('Index has staged changes; preserve or unstage them before undo.')
    if recovering:
        for item in state['files']:
            current = source_hash(safe_path(root, item['path']))
            if current not in (item['before_sha256'], item['installed_sha256']):
                raise InstallError('File was edited after interrupted installation: ' + item['path'])
    else:
        check_installed(root, state)
    # Check every backup before restoring any source file.
    for item in state['files']:
        if item['before_sha256'] is not None:
            data = safe_path(state_dir / 'backup', item['path']).read_bytes()
            if digest(data) != item['before_sha256']:
                raise InstallError('Backup checksum failed: ' + item['path'])
    if check_only:
        print('Undo preflight passed; no source files changed.')
        return
    restore(root, state_dir, state['files'])
    state_file.rename(state_dir / 'uninstalled.json')
    import uuid
    archive = root / ('.pc98-backup-' + uuid.uuid4().hex[:12])
    if not state_dir.resolve().is_relative_to(root) or not archive.resolve().is_relative_to(root):
        raise InstallError('Backup archive path escapes checkout.')
    state_dir.rename(archive)
    print('PC-98 source changes removed. Original bytes restored; backup retained at', archive)


def install(root, package, check_only=False):
    manifest_bytes = (package / 'manifest.json').read_bytes()
    manifest = json.loads(manifest_bytes)
    if manifest['format_version'] != 1:
        raise InstallError('Unsupported package format.')
    allowed = set(manifest['source_paths'])
    for name in allowed:
        safe_path(root, name)
    state_dir = root / '.pc98-install'
    if state_dir.is_symlink():
        raise InstallError('Installation record folder must not be a symlink.')
    state_file = state_dir / 'state.json'
    if state_file.exists():
        state = json.loads(state_file.read_text(encoding='utf-8'))
        if state['manifest_sha256'] != digest(manifest_bytes):
            raise InstallError('A different package is already installed; undo it first.')
        check_installed(root, state)
        print('This PC-98 package is already installed; no changes made.')
        return
    if state_dir.exists():
        raise InstallError('An older backup exists at .pc98-install; keep it and use a fresh checkout.')
    top = Path(git(root, 'rev-parse', '--show-toplevel').stdout.decode().strip()).resolve()
    if top != root:
        raise InstallError('--source must name the root of an 86Box Git checkout.')
    if not (root / 'src/86box.c').is_file() or not (root / 'CMakeLists.txt').is_file():
        raise InstallError('This is not an 86Box source checkout.')
    if git(root, 'status', '--porcelain', '--untracked-files=no').stdout.strip():
        raise InstallError('Checkout has tracked changes. Commit them or use a fresh clone.')
    head = git(root, 'rev-parse', 'HEAD').stdout.decode().strip()
    variant = next((v for v in manifest['patches'] if v['base_revision'] == head),
                   manifest['patches'][0])
    patch = safe_path(package, variant['file'])
    if digest(patch.read_bytes()) != variant['sha256']:
        raise InstallError('Package patch checksum failed.')
    if head not in manifest['verified_upstream_revisions']:
        print('This revision is unverified; checking a three-way merge before installation.')
    for name in allowed:
        exists_in_head = git(root, 'cat-file', '-e', 'HEAD:' + name, check=False).returncode == 0
        if not exists_in_head and safe_path(root, name).exists():
            raise InstallError('Untracked file would be replaced: ' + name)
    with tempfile.TemporaryDirectory(prefix='86box-pc98-') as temporary:
        scratch = Path(temporary)
        env = os.environ.copy()
        for name in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE'):
            env.pop(name, None)
        env['GIT_INDEX_FILE'] = str(scratch / 'index')
        git(root, 'read-tree', 'HEAD', env=env)
        merged = git(root, 'apply', '--cached', '--3way', str(patch), env=env, check=False)
        if merged.returncode:
            conflicts = git(root, 'diff', '--cached', '--name-only', '--diff-filter=U', env=env).stdout
            details = conflicts.decode().strip() or merged.stderr.decode('utf-8', 'replace').strip()
            raise InstallError('Patch is incompatible; source and real index were not changed.\n' + details)
        paths = git(root, 'diff', '--cached', '--name-only', '-z', 'HEAD', env=env).stdout
        changed = [p.decode('utf-8') for p in paths.split(b'\0') if p]
        if not changed:
            print('Source already contains these changes; no installation needed.')
            return
        if set(changed) - allowed:
            raise InstallError('Merge modified files outside the package manifest.')
        if check_only:
            print('Preflight passed:', len(changed), 'source files; no source files changed.')
            return
        export = scratch / 'export'
        export.mkdir()
        git(root, 'checkout-index', '--prefix=' + export.as_posix() + '/', '--', *changed, env=env)
        records = []
        for name in changed:
            path = safe_path(root, name)
            result = safe_path(export, name)
            records.append(dict(path=name, before_sha256=source_hash(path),
                                installed_sha256=digest(result.read_bytes())))
        if git(root, 'rev-parse', 'HEAD').stdout.decode().strip() != head:
            raise InstallError('HEAD changed during preflight.')
        if git(root, 'status', '--porcelain', '--untracked-files=no').stdout.strip():
            raise InstallError('Checkout changed during preflight.')
        state_dir.mkdir()
        state = dict(format_version=1, manifest_sha256=digest(manifest_bytes),
                     upstream_head=head, files=records)
        for item in records:
            original = safe_path(root, item['path'])
            if source_hash(original) != item['before_sha256']:
                raise InstallError('Source changed during preflight: ' + item['path'])
            if original.exists():
                saved = safe_path(state_dir / 'backup', item['path'])
                saved.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(original, saved)
        atomic_write(state_dir / 'pending.json', json.dumps(state, indent=2).encode())
        try:
            for item in records:
                atomic_write(safe_path(root, item['path']), safe_path(export, item['path']).read_bytes())
            check_installed(root, state)
            (state_dir / 'pending.json').rename(state_file)
        except BaseException:
            restore(root, state_dir, records)
            raise
        print('Installed PC-98 support in', root)
        print('Changed', len(records), 'source files. Original bytes saved at', state_dir)
        print('Changes are unstaged for review. No commits, branch changes, ROMs, or VM configuration updates.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--source', type=Path, help='Existing clean 86Box Git checkout')
    target.add_argument('--clone', type=Path, help='Create a new upstream checkout here')
    parser.add_argument('--ref', help='Clone this upstream ref instead of the verified revision')
    parser.add_argument('--check', action='store_true', help='Preflight without changing source')
    parser.add_argument('--uninstall', action='store_true', help='Restore backed-up source')
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    if not shutil.which('git'):
        raise InstallError('Git is required and must be on PATH.')
    package = args.package.resolve()
    if args.clone:
        if args.check or args.uninstall:
            raise InstallError('--check and --uninstall require --source.')
        root = args.clone.resolve()
        if root.exists():
            raise InstallError('Clone destination already exists; choose a new folder.')
        manifest = json.loads((package / 'manifest.json').read_text(encoding='utf-8'))
        run(['git', 'clone', manifest['upstream_url'], str(root)])
        git(root, 'checkout', '-b', 'codex/pc98', args.ref or manifest['default_upstream_revision'])
    else:
        root = args.source.resolve()
        if args.ref:
            raise InstallError('--ref is only available with --clone.')
    if args.uninstall:
        uninstall(root, root / '.pc98-install', args.check)
    else:
        install(root, package, args.check)


if __name__ == '__main__':
    try:
        main()
    except (InstallError, OSError, ValueError, KeyError) as exc:
        print('PC-98 installer:', exc, file=sys.stderr)
        sys.exit(1)

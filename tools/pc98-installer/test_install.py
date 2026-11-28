"""Installer transaction checks using tiny disposable Git repositories."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('pc98_install', Path(__file__).with_name('install.py'))
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pc98-installer-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'checkout'
        self.package = Path(self.temp.name) / 'package'
        self.root.mkdir()
        self.package.mkdir()
        self.git('init', '--quiet')
        self.git('config', 'user.name', 'Installer Test')
        self.git('config', 'user.email', 'test@example.invalid')
        self.git('config', 'core.autocrlf', 'false')
        (self.root / 'src').mkdir()
        (self.root / 'CMakeLists.txt').write_bytes(b'project(86Box)\n')
        self.original = (b'/* upstream */\nint enabled = 0;\n' +
                         b'/* stable separation */\n' * 6)
        (self.root / 'src/86box.c').write_bytes(self.original)
        self.git('add', '.')
        self.git('commit', '--quiet', '-m', 'upstream fixture')
        self.head = self.git('rev-parse', 'HEAD').decode().strip()
        env = os.environ.copy()
        env['GIT_INDEX_FILE'] = str(Path(self.temp.name) / 'export.index')
        installer.git(self.root, 'read-tree', 'HEAD', env=env)
        for name, data in (('src/86box.c', self.original.replace(b'= 0', b'= 1')),
                           ('src/pc98.c', b'int pc98 = 1;\n')):
            blob = subprocess.check_output(['git', '-C', str(self.root), 'hash-object', '-w', '--stdin'], input=data).decode().strip()
            installer.git(self.root, 'update-index', '--add', '--cacheinfo', '100644', blob, name, env=env)
        patch = installer.git(self.root, 'diff', '--cached', '--binary', '--full-index', 'HEAD', env=env).stdout
        (self.package / 'fixture.patch').write_bytes(patch)
        self.manifest = dict(format_version=1, source_paths=['src/86box.c', 'src/pc98.c'],
            verified_upstream_revisions=[self.head],
            patches=[dict(file='fixture.patch', base_revision=self.head, sha256=installer.digest(patch))])
        self.write_manifest()

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args], stderr=subprocess.PIPE)

    def write_manifest(self):
        (self.package / 'manifest.json').write_text(json.dumps(self.manifest))

    def test_preflight_does_not_change_source_or_real_index(self):
        index = self.git('ls-files', '--stage')
        installer.install(self.root, self.package, True)
        self.assertEqual((self.root / 'src/86box.c').read_bytes(), self.original)
        self.assertFalse((self.root / 'src/pc98.c').exists())
        self.assertEqual(self.git('ls-files', '--stage'), index)
        self.assertFalse((self.root / '.pc98-install').exists())

    def test_install_idempotence_and_byte_exact_undo(self):
        installer.install(self.root, self.package)
        self.assertTrue((self.root / 'src/pc98.c').exists())
        installer.install(self.root, self.package)
        installer.uninstall(self.root, self.root / '.pc98-install')
        self.assertEqual((self.root / 'src/86box.c').read_bytes(), self.original)
        self.assertFalse((self.root / 'src/pc98.c').exists())
        self.assertFalse(self.git('diff', '--name-only').strip())
        installer.install(self.root, self.package)

    def test_refuse_dirty_checkout(self):
        (self.root / 'src/86box.c').write_bytes(b'user work\n')
        with self.assertRaises(installer.InstallError):
            installer.install(self.root, self.package)
        self.assertEqual((self.root / 'src/86box.c').read_bytes(), b'user work\n')

    def test_refuse_untracked_collision(self):
        (self.root / 'src/pc98.c').write_bytes(b'user file\n')
        with self.assertRaises(installer.InstallError):
            installer.install(self.root, self.package)
        self.assertEqual((self.root / 'src/pc98.c').read_bytes(), b'user file\n')

    def test_refuse_corrupt_patch(self):
        with (self.package / 'fixture.patch').open('ab') as f:
            f.write(b'corruption')
        with self.assertRaises(installer.InstallError):
            installer.install(self.root, self.package)

    def test_refuse_path_escape(self):
        self.manifest['source_paths'].append('../outside')
        self.write_manifest()
        with self.assertRaises(installer.InstallError):
            installer.install(self.root, self.package)

    def test_conflict_leaves_checkout_and_index_clean(self):
        (self.root / 'src/86box.c').write_bytes(self.original.replace(b'= 0', b'= 99'))
        self.git('add', '.')
        self.git('commit', '--quiet', '-m', 'conflicting future upstream')
        index = self.git('ls-files', '--stage')
        with self.assertRaises(installer.InstallError):
            installer.install(self.root, self.package)
        self.assertFalse((self.root / 'src/pc98.c').exists())
        self.assertEqual(self.git('ls-files', '--stage'), index)
        self.assertFalse(self.git('status', '--porcelain').strip())

    def test_keep_unrelated_upstream_changes(self):
        (self.root / 'src/86box.c').write_bytes(self.original + b'int future_feature = 1;\n')
        self.git('add', '.')
        self.git('commit', '--quiet', '-m', 'future upstream feature')
        installer.install(self.root, self.package)
        installed = (self.root / 'src/86box.c').read_bytes()
        self.assertIn(b'future_feature = 1', installed)
        self.assertIn(b'enabled = 1', installed)

    def test_refuse_undo_after_user_edit(self):
        installer.install(self.root, self.package)
        (self.root / 'src/86box.c').write_bytes(b'user edit\n')
        with self.assertRaises(installer.InstallError):
            installer.uninstall(self.root, self.root / '.pc98-install')
        self.assertEqual((self.root / 'src/86box.c').read_bytes(), b'user edit\n')

    def test_crlf_checkout_undo_restores_original_bytes(self):
        self.git('config', 'core.autocrlf', 'true')
        crlf = self.original.replace(b'\n', b'\r\n')
        (self.root / 'src/86box.c').write_bytes(crlf)
        self.git('add', '--renormalize', '.')
        self.git('commit', '--quiet', '--allow-empty', '-m', 'CRLF checkout fixture')
        installer.install(self.root, self.package)
        installer.uninstall(self.root, self.root / '.pc98-install')
        self.assertEqual((self.root / 'src/86box.c').read_bytes(), crlf)

    def test_write_failure_rolls_back_and_can_recover_pending_record(self):
        real_write = installer.atomic_write
        failed = False
        def fail_once(path, data):
            nonlocal failed
            if path == self.root / 'src/pc98.c' and not failed:
                failed = True
                raise OSError('Simulated disk write failure')
            return real_write(path, data)
        with patch.object(installer, 'atomic_write', fail_once):
            with self.assertRaises(OSError):
                installer.install(self.root, self.package)
        self.assertEqual((self.root / 'src/86box.c').read_bytes(), self.original)
        self.assertFalse((self.root / 'src/pc98.c').exists())
        installer.uninstall(self.root, self.root / '.pc98-install')

    def test_clone_uses_verified_revision_and_creates_branch(self):
        self.manifest['upstream_url'] = str(self.root)
        self.manifest['default_upstream_revision'] = self.head
        self.write_manifest()
        destination = Path(self.temp.name) / 'new clone'
        subprocess.run([sys.executable, str(Path(__file__).with_name('install.py')),
                        '--clone', str(destination), '--package', str(self.package)],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        self.assertEqual(installer.git(destination, 'rev-parse', 'HEAD').stdout.decode().strip(), self.head)
        self.assertEqual(installer.git(destination, 'branch', '--show-current').stdout.decode().strip(), 'codex/pc98')
        self.assertTrue((destination / 'src/pc98.c').is_file())


if __name__ == '__main__':
    unittest.main()

"""Release provenance, integrity and reproducibility checks."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('package_release', ROOT / 'tools/package-release.py')
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dist = Path(self.temp.name)
        (self.dist / 'firmware').mkdir()
        hashes = {}
        for name in release.IMAGES:
            data = name.encode()
            (self.dist / 'firmware' / name).write_bytes(data)
            hashes[name] = hashlib.sha256(data).hexdigest()
        self.manifest = dict(git_revision='abc123', dirty=False, country='US',
                             heap_diagnostics=False, artifacts=hashes)
        self.save_manifest()
        for name in ('size.txt', 'size.json', 'firmware.elf', 'firmware.map', 'build.log'):
            (self.dist / name).write_text('test fixture\n')

    def save_manifest(self):
        (self.dist / 'build-manifest.json').write_text(json.dumps(self.manifest))

    def package(self):
        return release.payload(ROOT, self.dist, 'v0.1.0', 'abc123')

    def test_notes_exact_entry(self):
        changelog = '# Changelog\n\n## [0.2.0] - 2026-10-01\n\nNext\n\n## [0.1.0] - 2026-09-27\n\nInitial release\n'
        self.assertEqual(release.release_notes(changelog, 'v0.1.0'), 'Initial release\n')
        for version in ('v0.3.0', '../../bad', 'v01.0.0'):
            with self.assertRaises(ValueError):
                release.release_notes(changelog, version)
        with self.assertRaises(ValueError):
            release.release_notes(changelog + '\n## [0.1.0] - 2026-09-27\nDuplicate\n', 'v0.1.0')

    def test_reject_wrong_revision_dirty_or_diagnostics(self):
        for key, value in (('git_revision', 'stale'), ('dirty', True),
                           ('heap_diagnostics', True), ('country', 'GB')):
            old = self.manifest[key]
            self.manifest[key] = value
            self.save_manifest()
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.package()
            self.manifest[key] = old

    def test_reject_corrupt_or_extra_image(self):
        path = self.dist / 'firmware' / 'extra.bin'
        path.write_bytes(b'extra')
        with self.assertRaises(ValueError):
            self.package()
        path.unlink()
        (self.dist / 'firmware' / 'km0_boot_all.bin').write_bytes(b'corrupt')
        with self.assertRaises(ValueError):
            self.package()

    def test_archive_reproducible_and_all_payload_checksummed(self):
        files, notes = self.package()
        self.assertEqual(notes, 'Initial release\n')
        self.assertIn('examples/https.py', files)
        self.assertIn('SUPPORTED_COMMANDS.md', files)
        sums = files['SHA256SUMS'].decode().splitlines()
        self.assertEqual(len(sums), len(files) - 1)
        for line in sums:
            digest, name = line.split('  ', 1)
            self.assertEqual(digest, hashlib.sha256(files[name]).hexdigest())
        a, b = self.dist / 'a.tar.gz', self.dist / 'b.tar.gz'
        release.write_archive(a, files, 'release', 1234567890)
        release.write_archive(b, files, 'release', 1234567890)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        with tarfile.open(a) as archive:
            self.assertEqual(len(archive.getmembers()), len(files))
            for item in archive:
                self.assertEqual(archive.extractfile(item).read(), files[item.name.removeprefix('release/')])


if __name__ == '__main__':
    unittest.main()

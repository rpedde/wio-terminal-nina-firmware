"""Firmware workflow checks; protocol and backend tests live alongside these."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copyfile(ROOT / 'fw', self.root / 'fw')
        (self.root / 'fw').chmod(0o755)

    def fw(self, *args):
        return subprocess.run([str(self.root / 'fw'), *args], capture_output=True, text=True)

    def test_argument_errors(self):
        for args in [('unknown',), ('build', 'extra'), ('flash-rtl',),
                     ('erase-rtl',), ('install-circuitpython',), ('image', '--bad'),
                     ('flash-rtl', '--port', '/dev/ttyACM0; touch /tmp/injected')]:
            with self.subTest(args=args):
                self.assertNotEqual(self.fw(*args).returncode, 0)

    def test_uf2_validation_and_copy(self):
        mount = self.root / 'WIO TERMINAL'
        mount.mkdir()
        uf2 = self.root / 'circuit python.uf2'
        uf2.write_bytes(struct.pack('<II', 0x0A324655, 0x9E5D5157) + bytes(504))
        args = ('install-circuitpython', '--mount', str(mount), '--uf2', str(uf2))
        self.assertNotEqual(self.fw(*args).returncode, 0)
        (mount / 'INFO_UF2.TXT').write_text('Board-ID: Wio_Terminal\nModel: SAMD51\n')
        result = self.fw(*args)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((mount / uf2.name).read_bytes(), uf2.read_bytes())
        uf2.write_bytes(b'invalid')
        self.assertNotEqual(self.fw(*args).returncode, 0)

    def test_clean_preserves_artifacts(self):
        (self.root / 'dist').mkdir()
        (self.root / 'dist' / 'test.bin').write_bytes(b'firmware')
        self.assertEqual(self.fw('clean').returncode, 0)
        self.assertFalse((self.root / 'dist').exists())
        backups = list(self.root.glob('.dist-backup.*/dist/test.bin'))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), b'firmware')

    def test_lock_has_immutable_sources(self):
        lock = json.loads((ROOT / 'tools/toolchain.lock.json').read_text())
        self.assertEqual(lock['board_package_version'], '3.0.5')
        self.assertIn('@sha256:', lock['ubuntu'])
        self.assertRegex(lock['ubuntu_snapshot'], r'^[0-9]{8}T[0-9]{6}Z$')
        self.assertIn(lock['ubuntu_snapshot'], (ROOT / 'tools/Dockerfile').read_text())
        for entry in lock['archives'].values():
            self.assertRegex(entry['sha256'], r'^[a-f0-9]{64}$')
            self.assertTrue(entry['url'].startswith('https://'))
            self.assertNotIn('/latest/', entry['url'])


if __name__ == '__main__':
    unittest.main()

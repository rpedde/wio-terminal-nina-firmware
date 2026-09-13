"""Sanitized failure injection around the production mbedTLS adapter."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class TLSTests(unittest.TestCase):
    def test_backend_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / 'tls')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                '-I' + str(ROOT / 'tests/tls_mock'), str(ROOT / 'tests/tls_test.c'),
                str(ROOT / 'src/nina/nina_time.c'), '-o', binary], check=True)
            subprocess.run([binary], check=True, env=dict(os.environ,
                ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
    def test_bundle_consistency(self):
        subprocess.run(['python3', str(ROOT / 'tools/check-certificates.py')], check=True)

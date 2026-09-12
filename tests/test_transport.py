"""Compile the production transport engine with a fault-injecting host backend."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TransportTests(unittest.TestCase):
    def test_sanitized_transport(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / 'transport-test')
            subprocess.run([
                'cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                '-I' + str(ROOT / 'src/nina'),
                str(ROOT / 'tests/transport_test.c'),
                str(ROOT / 'src/nina/nina_transport.c'),
                str(ROOT / 'src/nina/nina_spi_proof.c'), '-o', binary,
            ], check=True)
            subprocess.run([binary], check=True, env=dict(
                os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))

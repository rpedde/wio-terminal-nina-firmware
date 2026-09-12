"""Compile the production socket backend with test-only POSIX OS adapters."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SocketTests(unittest.TestCase):
    def test_live_loopback(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / 'sockets')
            subprocess.run([
                'cc', '-std=c11', '-D_DEFAULT_SOURCE', '-O1', '-g', '-pthread',
                '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-no-pie',
                '-I' + str(ROOT / 'tests/socket_posix'), '-I' + str(ROOT / 'src/nina'),
                str(ROOT / 'tests/socket_loopback.c'),
                str(ROOT / 'src/nina/nina_sockets.c'),
                str(ROOT / 'src/nina/nina_sockets_rtl8720.c'), '-o', binary,
            ], check=True)
            subprocess.run([binary], check=True, timeout=15, env=dict(
                os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))

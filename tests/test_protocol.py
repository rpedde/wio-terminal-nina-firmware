"""Sanitized portable protocol, dispatcher and scan tests with mock Wi-Fi."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ProtocolTests(unittest.TestCase):
    def test_sanitized_protocol(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / 'protocol-test')
            subprocess.run([
                'cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                '-I' + str(ROOT / 'src/nina'), str(ROOT / 'tests/protocol_test.c'),
                *[str(ROOT / 'src/nina' / name) for name in (
                    'nina_protocol.c', 'nina_server.c', 'nina_wifi.c', 'nina_dhcp.c')],
                '-o', binary,
            ], check=True)
            subprocess.run([binary], check=True, env=dict(
                os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))

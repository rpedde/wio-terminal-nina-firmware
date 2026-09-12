"""Restrict upstream autodetection to the one explicitly passed device.

Upstream's --port branch leaves _isbootloader uninitialized. Use its detection
branch, filtered to our device, and propagate tool failures as nonzero exits.
"""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys

command, port = sys.argv[1:]
spec = importlib.util.spec_from_file_location('ambd', Path.cwd() / 'ambd_flash_tool.py')
ambd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ambd)
original_ports = ambd.getAllPortInfo
ambd.getAllPortInfo = lambda: [info for info in original_ports() if info.device == port]


def checked_system(command):
    subprocess.run(command, shell=True, check=True)
    return 0


class CheckedOutput:
    def __init__(self, command):
        self.command = command

    def read(self):
        result = subprocess.run(self.command, shell=True, check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if 'successfully' not in result.stdout:
            raise RuntimeError('Seeed tool did not confirm success:\n' + result.stdout)
        return result.stdout


os.system = checked_system
os.popen = CheckedOutput
args = ['flash', '-d', '/dist/firmware'] if command == 'flash-rtl' else ['erase']
ambd.cli(args=args)

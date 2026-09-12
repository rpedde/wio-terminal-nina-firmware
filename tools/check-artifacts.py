"""Validate a completed baseline build, including checksums and ownership."""
import hashlib
import json
import os
from pathlib import Path
import sys

dist = Path(sys.argv[1] if len(sys.argv) > 1 else 'dist')
expected = {'km0_boot_all.bin', 'km4_boot_all.bin', 'km0_km4_image2.bin'}
assert {p.name for p in (dist / 'firmware').iterdir()} == expected
manifest = json.loads((dist / 'build-manifest.json').read_text())
for name in expected:
    path = dist / 'firmware' / name
    assert path.stat().st_size > 0, name
    assert hashlib.sha256(path.read_bytes()).hexdigest() == manifest['artifacts'][name], name
for name in ('firmware.elf', 'firmware.map', 'size.txt', 'size.json', 'build.log', 'SHA256SUMS'):
    assert (dist / name).stat().st_size > 0, name
assert (dist / 'firmware.elf').read_bytes()[:4] == b'\x7fELF'
for path in dist.rglob('*'):
    assert path.stat().st_uid == os.getuid(), f'Wrong owner: {path}'
print('Artifact names, sizes, checksums, ELF and ownership verified.')

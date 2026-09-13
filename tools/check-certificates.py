"""Offline consistency guard; ordinary builds never fetch certificates."""
import hashlib
import importlib.util
import json
from pathlib import Path
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('update', root / 'tools/update-certificates.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
pem = (root / 'certificates/roots.pem').read_bytes()
metadata = json.loads((root / 'certificates/metadata.json').read_text())
assert hashlib.sha256(pem).hexdigest() == metadata['pem_sha256'], 'Bundle hash mismatch'
assert (root / 'src/nina/nina_roots.h').read_text() == module.header(pem), 'Embedded bundle mismatch'
assert pem.count(b'-----BEGIN CERTIFICATE-----') == len(metadata['roots'])

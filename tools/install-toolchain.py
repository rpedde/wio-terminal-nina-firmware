"""Install only checksum-locked archives; no board-manager index refresh."""
import difflib
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

LOCK = json.loads(Path('/opt/fw-tools/toolchain.lock.json').read_text())
DESTINATIONS = {
    'ctags': ('/opt/arduino/packages/builtin/tools/ctags/5.8-arduino11', 1),
    'serial-discovery': ('/opt/arduino/packages/builtin/tools/serial-discovery/1.4.3', 1),
    'mdns-discovery': ('/opt/arduino/packages/builtin/tools/mdns-discovery/1.0.9', 1),
    'cli': ('/usr/local/bin', 0),
    'core': ('/opt/arduino/packages/realtek/hardware/AmebaD/3.0.5', 1),
    'compiler': ('/opt/arduino/packages/realtek/tools/ameba_d_asdk_toolchain/1.0.1', 1),
    'tools': ('/opt/arduino/packages/realtek/tools/ameba_d_tools/1.0.4', 1),
    'flash': ('/opt/ambd-flash', 1),
}

installed = subprocess.check_output(['dpkg-query', '-W', '-f=${Package}=${Version}\n'], text=True)
expected = Path('/opt/fw-tools/system-packages.lock').read_text()
if installed.splitlines() != expected.splitlines():
    print(''.join(difflib.unified_diff(expected.splitlines(True), installed.splitlines(True),
                                     fromfile='system-packages.lock', tofile='installed')))
    raise SystemExit('System packages changed; explicitly review and update system-packages.lock')

for name, entry in LOCK['archives'].items():
    with tempfile.TemporaryDirectory() as tmp:
        archive = Path(tmp) / 'archive'
        subprocess.run(['curl', '-fL', '--retry', '3', entry['url'], '-o', str(archive)], check=True)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != entry['sha256']:
            raise SystemExit(f'Checksum mismatch: {name}')
        destination, strip = DESTINATIONS[name]
        Path(destination).mkdir(parents=True, exist_ok=True)
        subprocess.run(['tar', '-xf', str(archive), '-C', destination,
                        f'--strip-components={strip}'], check=True)

# The vendor archives contain executables without executable mode bits.
for root in ('/opt/ambd-flash/tool/linux',
             '/opt/arduino/packages/realtek/tools/ameba_d_tools/1.0.4'):
    for path in Path(root).rglob('*'):
        if path.is_file():
            path.chmod(path.stat().st_mode | 0o111)

# Empty offline indexes prevent CLI from downloading floating package indexes.
Path('/opt/arduino/package_index.json').write_text('{"packages": []}\n')
Path('/opt/arduino/library_index.json').write_text('{"libraries": []}\n')

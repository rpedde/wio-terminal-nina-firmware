"""Package only a clean, production build of the requested release tag."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile

PROJECT = 'wio-terminal-nina-firmware'
IMAGES = {'km0_boot_all.bin', 'km4_boot_all.bin', 'km0_km4_image2.bin'}


def release_notes(changelog, version):
    if not re.fullmatch(r'v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)', version):
        raise ValueError('Release tag must be vMAJOR.MINOR.PATCH')
    sections = re.split(r'^## ', changelog, flags=re.MULTILINE)[1:]
    matches = [part.split('\n', 1)[1].strip() for part in sections
               if re.fullmatch(r'\[' + re.escape(version[1:]) + r'\] - \d{4}-\d{2}-\d{2}',
                               part.split('\n', 1)[0])]
    if len(matches) != 1 or not matches[0]:
        raise ValueError(f'Expected exactly one nonempty changelog entry for {version}')
    return matches[0] + '\n'


def payload(source, dist, version, revision):
    notes = release_notes((source / 'CHANGELOG.md').read_text(), version)
    manifest = json.loads((dist / 'build-manifest.json').read_text())
    if manifest['git_revision'] != revision or manifest['dirty'] is not False:
        raise ValueError('Build must come from the clean release revision')
    if manifest['heap_diagnostics'] is not False or manifest['country'] != 'US':
        raise ValueError('Release requires production diagnostics and the default US country')
    if set(manifest['artifacts']) != IMAGES or {p.name for p in (dist / 'firmware').iterdir()} != IMAGES:
        raise ValueError('Expected exactly three RTL images')
    files = {}
    for name in sorted(IMAGES):
        data = (dist / 'firmware' / name).read_bytes()
        if not data or hashlib.sha256(data).hexdigest() != manifest['artifacts'][name]:
            raise ValueError(f'Invalid firmware checksum: {name}')
        files['firmware/' + name] = data
    for name in ('build-manifest.json', 'size.txt', 'size.json', 'firmware.elf', 'firmware.map', 'build.log'):
        files[name] = (dist / name).read_bytes()
    for name in ('README.md', 'SUPPORTED_COMMANDS.md', 'LICENSE', 'THIRD_PARTY.md',
                 'certificates/README.md', 'certificates/metadata.json',
                 'examples/https.py', 'examples/settings.toml.example',
                 'licenses/Apache-2.0.txt', 'licenses/MPL-2.0.txt',
                 'licenses/LGPL-2.1.txt', 'licenses/SDK-NOTICES.txt'):
        files[name] = (source / name).read_bytes()
    files['RELEASE_NOTES.md'] = notes.encode()
    files['INSTALL.md'] = (f'''# {PROJECT} {version}

These are RTL8720DN images, not a SAMD51 UF2. Country: US.

1. Verify the downloaded archive against its .sha256 file, then extract it.
2. In the extracted directory run `sha256sum -c SHA256SUMS`.
3. Clone https://github.com/rpedde/{PROJECT}.git and check out `{version}`.
4. In that checkout run `./fw image`, then create `dist/` and copy this
   archive's contents into it. See README.md for the full build alternative.
5. Run `./fw flash-rtl --port /dev/ttyACM0` with your board's explicit port.
6. Restore your stock Wio Terminal CircuitPython UF2 on the SAMD51.
7. Install Adafruit ESP32SPI 11.1.4 and copy `examples/https.py` to
   CIRCUITPY/code.py. Copy examples/settings.toml.example to
   CIRCUITPY/settings.toml and fill in your Wi-Fi credentials.

See README.md for restoration instructions and SUPPORTED_COMMANDS.md for
compatibility, limits and outstanding hardware coverage deferrals.
''').encode()
    files['SHA256SUMS'] = ''.join(f'{hashlib.sha256(data).hexdigest()}  {name}\n'
                                  for name, data in sorted(files.items())).encode()
    return files, notes


def write_archive(path, files, prefix, epoch):
    # Stable ordering, metadata and gzip header make identical inputs reproducible.
    with path.open('wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as gz:
        with tarfile.open(fileobj=gz, mode='w', format=tarfile.USTAR_FORMAT) as archive:
            for name, data in sorted(files.items()):
                info = tarfile.TarInfo(f'{prefix}/{name}')
                info.size, info.mtime, info.mode = len(data), epoch, 0o644
                archive.addfile(info, io.BytesIO(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version')
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--dist', type=Path, default=Path('dist'))
    args = parser.parse_args()
    def git(*argv):
        return subprocess.check_output(['git', '-C', str(args.source), *argv], text=True).strip()
    release_notes((args.source / 'CHANGELOG.md').read_text(), args.version)
    revision = git('rev-parse', 'HEAD')
    if git('status', '--porcelain', '--untracked-files=normal'):
        raise ValueError('Commit all source changes before packaging')
    if git('rev-parse', f'refs/tags/{args.version}^{{commit}}') != revision:
        raise ValueError('Release tag must point to HEAD')
    files, notes = payload(args.source, args.dist, args.version, revision)
    output = args.dist / 'release'
    output.mkdir(exist_ok=True)
    prefix = f'{PROJECT}-{args.version}'
    archive = output / f'{prefix}.tar.gz'
    write_archive(archive, files, prefix, int(git('show', '-s', '--format=%ct', 'HEAD')))
    archive.with_suffix('.gz.sha256').write_text(f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n')
    (output / 'RELEASE_NOTES.md').write_text(notes)
    print(f'Created {archive}')


if __name__ == '__main__':
    main()

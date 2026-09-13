"""Disposable build workspace; mounted source is always read-only."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

IMAGES = ('km0_boot_all.bin', 'km4_boot_all.bin', 'km0_km4_image2.bin')
LOCK = json.loads(Path('/opt/fw-tools/toolchain.lock.json').read_text())


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def build(work):
    country = os.environ.get('WIFI_COUNTRY', 'US')
    if country not in ('US', 'CA', 'GB', 'DE', 'FR', 'AU', 'JP'):
        raise RuntimeError('WIFI_COUNTRY must be US, CA, GB, DE, FR, AU, or JP')
    manifest_path = Path('/dist/build-manifest.json')
    if manifest_path.exists():
        manifest_path.replace('/dist/build-manifest.previous.json')
    sketch = work / 'seeed-ambd-firmware'
    shutil.copytree('/source', sketch, ignore=shutil.ignore_patterns(
        '.git', '.agents', '.codex', 'dist', '.dist-backup.*', '__pycache__'))
    run('python3', str(sketch / 'tools/check-certificates.py'))
    # Vendor postbuild tools write into their own installation directory.
    data = work / 'arduino'
    shutil.copytree('/opt/arduino', data)
    core = data / 'packages/realtek/hardware/AmebaD/3.0.5'
    # Preserve every vendor linker flag; add only the two DHCP send hooks.
    platform = core / 'platform.txt'
    platform_text = platform.read_text()
    marker = 'compiler.c.elf.extra_flags='
    if platform_text.count(marker) != 1:
        raise RuntimeError('Pinned platform linker flags changed')
    platform.write_text(platform_text.replace(marker, marker +
        '-Wl,--wrap=udp_sendto_if -Wl,--wrap=udp_sendto_if_src '))
    os.environ['ARDUINO_DIRECTORIES_DATA'] = str(data)
    os.environ['ARDUINO_DIRECTORIES_DOWNLOADS'] = str(work / 'downloads')
    os.environ['ARDUINO_DIRECTORIES_USER'] = str(work / 'user')
    output = work / 'build'
    includes = ' '.join(f'-I{sketch}/src/{name}' for name in (
        'easylogger', 'easylogger/inc', 'ble', 'wifi', 'esp_lib', 'erpc', 'erpc_shim', 'mDNS'))
    includes += f' -I{core}/system/libameba/sdk/component/common/network/sntp'
    includes += f' -DWIFI_COUNTRY=RTW_COUNTRY_{country}'
    with Path('/dist/build.log').open('w') as log:
        result = subprocess.run(['arduino-cli', 'compile', '--fqbn', LOCK['board'],
            '--build-path', str(output), '--build-property', f'build.extra_flags={includes}',
            str(sketch)], stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print(Path('/dist/build.log').read_text()[-16000:], file=sys.stderr)
        raise SystemExit(result.returncode)
    vendor = data / 'packages/realtek/tools/ameba_d_tools/1.0.4'
    firmware = Path('/dist/firmware')
    firmware.mkdir(exist_ok=True)
    for name in IMAGES:
        source = vendor / name
        if not source.is_file():
            raise RuntimeError(f'Missing generated image: {source}')
        shutil.copyfile(source, firmware / name)
    shutil.copyfile(output / 'application.axf', Path('/dist/firmware.elf'))
    shutil.copyfile(output / 'application.map', Path('/dist/firmware.map'))
    compiler = data / 'packages/realtek/tools/ameba_d_asdk_toolchain/1.0.1/bin'
    with Path('/dist/size.txt').open('w') as report:
        run(str(compiler / 'arm-none-eabi-size'), '-A', '/dist/firmware.elf', stdout=report)
    sections = {}
    for line in Path('/dist/size.txt').read_text().splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0].startswith('.'):
            sections[fields[0]] = int(fields[1])
    sizes = {
        'flash_image_bytes': sum((firmware / name).stat().st_size for name in IMAGES),
        'sram_image_bytes': sum(size for name, size in sections.items() if name.startswith('.ram_image2.')),
        'sram_reserved_heap_bytes': sections.get('.ram_heap.data', 0),
        'sections': sections,
    }
    Path('/dist/size.json').write_text(json.dumps(sizes, indent=2) + '\n')
    hashes = {name: hashlib.sha256((firmware / name).read_bytes()).hexdigest() for name in IMAGES}
    manifest = dict(LOCK, git_revision=os.environ['FW_REVISION'],
        dirty=os.environ['FW_DIRTY'] == 'true', firmware_version='3.3.0+rtl8720.1',
        firmware_protocol='NINA SPI phase 5 (verified TLS)', country=country,
        certificate_bundle_sha256=hashlib.sha256((sketch / 'certificates/roots.pem').read_bytes()).hexdigest(), artifacts=hashes,
        sizes=sizes,
        system_packages_sha256=hashlib.sha256(Path('/opt/fw-tools/system-packages.lock').read_bytes()).hexdigest(),
        python_packages=subprocess.check_output(['pip3', 'freeze', '--local'], text=True).splitlines(),
        compiler_version=subprocess.check_output([str(compiler / 'arm-none-eabi-gcc'), '--version'], text=True).splitlines()[0],
        arduino_cli=subprocess.check_output(['arduino-cli', 'version'], text=True).strip())
    Path('/dist/build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    Path('/dist/SHA256SUMS').write_text(''.join(f'{digest}  firmware/{name}\n' for name, digest in hashes.items()))
    print('Built three RTL images, ELF, map, size report and manifest in dist/.')


def main():
    command = sys.argv[1]
    # Every invocation has its own --rm container. Fixed paths also keep
    # __FILE__ strings and debug information independent of temporary names.
    work = Path('/tmp/fw-work')
    work.mkdir()
    os.chdir(work)
    if command == 'build':
        build(work)
    elif command == 'shell':
        run('/bin/bash')
    elif command == 'test':
        run('python3', '-B', '-m', 'unittest', 'discover', '-s', '/source/tests', '-v')
        if Path('/dist/build-manifest.json').exists():
            run('python3', '/opt/fw-tools/check-artifacts.py', '/dist')
    elif command in ('flash-rtl', 'erase-rtl'):
        if command == 'flash-rtl':
            run('python3', '/opt/fw-tools/check-artifacts.py', '/dist')
        shutil.copytree('/opt/ambd-flash', work / 'flash')
        os.chdir(work / 'flash')
        run('python3', '/opt/fw-tools/flash.py', command, os.environ['FW_PORT'])
    else:
        raise SystemExit(f'Unknown container command: {command}')


if __name__ == '__main__':
    main()

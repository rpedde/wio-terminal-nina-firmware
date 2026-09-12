# Seeed RTL872X RPC firmware  [![Build Status](https://travis-ci.com/Seeed-Studio/seeed-ambd-firmware.svg?branch=master)](https://travis-ci.com/Seeed-Studio/seeed-ambd-firmware)

## Docker workflow and phase 2 SPI proof

The NINA port in [TODO.md](plans/TODO.md) now contains the phase 2 SPI proof.
It returns fixed firmware version and idle status replies only; networking
is not implemented. Hardware acceptance is tracked in [PHASE2.md](plans/PHASE2.md).
The phase 1 baseline record remains in [PHASE1.md](plans/PHASE1.md).

On Linux with Docker, Bash, Git and standard coreutils:

```sh
./fw image
./fw build
./fw test
```

The image targets linux/amd64 because Seeed's compiler and flashing binaries
are x86-64. Image creation downloads checksum-pinned dependencies. Firmware
builds run offline, as your UID/GID, with a read-only source mount and a
disposable copy of both source and SDK. Only `dist/` is exported. No host
Arduino, ARM compiler or Python installation is needed. There are no persistent
compiler/download volumes; Docker layers provide the image cache. Use
`./fw image --no-cache` to recreate the image without that cache. Every
`./fw build` compiles from a fresh workspace.

Artifacts include `dist/firmware/{km0_boot_all,km4_boot_all,km0_km4_image2}.bin`,
`firmware.elf`, `firmware.map`, `size.txt`, `size.json`, `build.log`, `SHA256SUMS`, and
`build-manifest.json`. Paths after the firmware directory are relative to
`dist/`. The manifest records the source revision/dirty state, locked toolchain,
compiler version and artifact hashes. Country and certificate metadata are
explicitly marked as not applicable until their NINA phases.

`./fw test` runs workflow checks, native SPI transport fault tests under
ASan/UBSan, and validates completed build artifacts. Hardware tests require
the separate procedure in PHASE2.md. CI also builds the image and firmware
and checks that tracked sources remain unchanged.

To flash a connected Wio Terminal, explicitly choose its serial device:

```sh
./fw flash-rtl --port /dev/ttyACM0
# Only when intentionally erasing the RTL:
./fw erase-rtl --port /dev/ttyACM0 --confirm-erase
```

Only the selected device is passed into Docker. The pinned Seeed tool uses its
checked-in SAMD bridge firmware and the locally built RTL images; it never
downloads firmware. The wrapper fixes upstream port detection/error reporting
without broadening device access. USB re-enumeration may invalidate Docker's
device mapping or change the port. If so, stop, identify the board's current
port, and rerun with that explicit device; do not use `--privileged` or expose
all of `/dev`. The baseline flash workflow passed on physical hardware (see PHASE1.md).

The SAMD51 may now contain Seeed's temporary bridge. Enter its UF2 bootloader
and restore your explicitly supplied CircuitPython UF2:

```sh
./fw install-circuitpython --mount /path/to/WIO_TERMINAL --uf2 file.uf2
```

This is a host-side copy; the command checks the bootloader's `INFO_UF2.TXT`
and UF2 header/length. It does not download or select a CircuitPython build.
Use the stock ESP32SPI 11.1.4 driver with the phase 2 hardware test script.
This proof supports only status/version; Wi-Fi and sockets require later phases.

`./fw shell` opens a disposable toolchain shell. `./fw clean` moves `dist/`
into a recoverable, ignored `.dist-backup.*` directory and prints its location.

Dependency pins and source URLs are in [tools/toolchain.lock.json](tools/toolchain.lock.json).
System-package drift causes image creation to fail against
`tools/system-packages.lock`; Python wheels are version- and hash-locked in
`tools/requirements.lock`. Update these only as an explicit maintenance change
and repeat the baseline build/flash tests. Archive checksums for Seeed compiler,
postbuild tools, and Arduino builtins originate from their package indexes;
CLI/core/flasher archive checksums were measured from the pinned downloads.
The unmodified vendor code retains its original licensing and attribution.

## Introduction

This RTL87XX [RPC](https://github.com/EmbeddedRPC/eRPC) firmware export a RPC server interface through hardware SPI/UART port to MCU.  

## How to compile 
### Tools 
The arduino-cli tool is used to build and upload the RTL8720DN firmware to the Seeed Wio terminal board. Use following link for download and installation procedure:
* [Arduino CLI](https://arduino.github.io/arduino-cli/installation/).

Sample script is below:
```sh
wget https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh
chmod a+x install.sh
sudo BINDIR=/usr/local/bin ./install.sh
sudo rm -rf ~/.arduino15
arduino-cli config init
```

### ArduinoCore
Before compiling the firmware, you need to install the Arduino core of rtl872x [ArduinoCore-ambd](https://github.com/Seeed-Studio/ArduinoCore-ambd/)
- board index
```
https://files.seeedstudio.com/arduino/package_realtek.com_amebad_index.json
```

Sample script is below:
```sh
arduino-cli config add board_manager.additional_urls https://files.seeedstudio.com/arduino/package_realtek.com_amebad_index.json
arduino-cli core update-index
arduino-cli core install realtek:AmebaD
rm -rf ~/.arduino15/packages/realtek/hardware/AmebaD/3.0.5
git clone https://github.com/Seeed-Studio/ArduinoCore-ambd ~/.arduino15/packages/realtek/hardware/AmebaD/3.0.5
```

### build
```sh
./arduino-build.sh --build
```

### flash

```sh
chmod +x build.sh
./build.sh --flash /dev/tty***
````

-----
This software RPC server section is written by Seeed Studio
and is licensed under The MIT License. Check License.txt for more information.

Contributing to this software is warmly welcomed. You can do this basically by
forking, committing modifications and then pulling requests (follow the links above
for operating guide). Adding change log and your contact into file header is encouraged.
Thanks for your contribution.


# wio-terminal-nina-firmware

This firmware turns the Wio Terminal's RTL8720DN into a NINA-compatible Wi-Fi
coprocessor for stock CircuitPython. It replaces the original Seeed eRPC
service with 8 MHz SPI, station Wi-Fi, TCP, UDP and certificate-verified HTTPS.

The compatibility target is stock CircuitPython for `seeeduino_wio_terminal`
and unmodified Adafruit ESP32SPI **11.1.4**, using Adafruit NINA firmware 3.3.0
as the protocol reference. Hardware acceptance used CircuitPython **10.3.0**.
The firmware reports `3.3.0+rtl8720.1`; this is its protocol version string,
not a claim that the ESP32 firmware runs on Realtek hardware.

Phase 6 source cleanup and regression status are tracked in
[PHASE6.md](plans/PHASE6.md). Earlier hardware evidence is in
[Phase 3](plans/PHASE3.md), [Phase 4](plans/PHASE4.md) and
[Phase 5](plans/PHASE5.md). Release packaging is documented in [RELEASING.md](RELEASING.md).

## Build and flash

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
compiler version, country, artifact hashes and the certificate bundle hash.
Root selection and explicit updates are documented in
[certificates/README.md](certificates/README.md). Country defaults to US; select a different compile-time
plan with `WIFI_COUNTRY=GB ./fw build`. Supported values are US, CA, GB, DE,
FR, AU, and JP. Changing the country requires rebuilding and reflashing.

`./fw test` runs workflow checks, native protocol/dispatcher/transport tests
and fuzz cases under ASan/UBSan, and validates completed build artifacts.
Hardware tests require a connected Wio and the procedures below. CI also builds the image and firmware
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
Use the stock ESP32SPI 11.1.4 driver with `tests/hardware/phase3_wifi.py`.
Copy `tests/hardware/settings.toml.example` to `CIRCUITPY/settings.toml` and
fill in the local credentials. `DNS_TEST_HOST` is a hostname to resolve;
DNS server addresses normally come from DHCP. Run
`tests/hardware/phase4_sockets.py` for TCP/UDP and
`tests/hardware/phase5_tls.py` for verified HTTPS. TLS requires a hostname,
SNTP time and a chain to an included root; only one TLS socket is supported.

Optional runtime RTL heap measurements use
`NINA_HEAP_DIAGNOSTICS=1 ./fw build` and
`tests/hardware/phase5_heap.py`; see [PHASE5.md](plans/PHASE5.md).
Ordinary builds default to diagnostics disabled.

`./fw shell` opens a disposable toolchain shell. `./fw clean` moves `dist/`
into a recoverable, ignored `.dist-backup.*` directory and prints its location.

Dependency pins and source URLs are in [tools/toolchain.lock.json](tools/toolchain.lock.json).
System-package drift causes image creation to fail against
`tools/system-packages.lock`; Python wheels are version- and hash-locked in
`tools/requirements.lock`. Update these only as an explicit maintenance change
and repeat the baseline build/flash tests. Archive checksums for Seeed compiler,
postbuild tools, and Arduino builtins originate from their package indexes;
CLI/core/flasher archive checksums were measured from the pinned downloads.
See [THIRD_PARTY.md](THIRD_PARTY.md) for licensing and attribution.


## CircuitPython HTTPS example

After restoring CircuitPython, copy the CircuitPython-compatible ESP32SPI
11.1.4 package to `CIRCUITPY/lib/adafruit_esp32spi/`. Copy
[settings.toml.example](examples/settings.toml.example) to
`CIRCUITPY/settings.toml`, set your Wi-Fi credentials, and copy
[https.py](examples/https.py) to `CIRCUITPY/code.py`.
The example uses only the stock driver and CircuitPython modules, waits for
SNTP, and prints a verified HTTPS response. Its initialization is:

```python
import board
import busio
import digitalio
from adafruit_esp32spi import adafruit_esp32spi

spi = busio.SPI(board.RTL_CLK, MOSI=board.RTL_MOSI, MISO=board.RTL_MISO)
cs = digitalio.DigitalInOut(board.RTL_CS)
ready = digitalio.DigitalInOut(board.RTL_READY)
reset = digitalio.DigitalInOut(board.RTL_PWR)
esp = adafruit_esp32spi.ESP_SPIcontrol(spi, cs, ready, reset)
```

`RTL_DIR` is unused and must remain an input. Do not initialize an eRPC UART
on the SPI pins. The stock driver handles the active-low READY handshake.

## Supported commands and limits

See [SUPPORTED_COMMANDS.md](SUPPORTED_COMMANDS.md) for the command matrix,
resource limits, unsupported features and hardware coverage deferrals.

## Validation and troubleshooting

Run `./fw test` after a clean build for native ASan/UBSan tests and artifact
checks. On a Wio with the stock library and the hardware settings example,
run these scripts via REPL, releasing pins or soft-reloading between scripts:

- `tests/hardware/phase2_transport.py`: 100 version reads, 20 resets, 10,000 requests.
- `tests/hardware/phase3_wifi.py` and `phase3_recovery.py`: Wi-Fi and recovery.
- `tests/hardware/phase4_sockets.py`: HTTP, UDP NTP, capacity and 100 TCP cycles.
- `tests/hardware/phase5_tls.py`: verified HTTPS, certificate rejection and 100 TLS cycles.

For a READY timeout, confirm CircuitPython was restored after flashing, check
the six RTL pin names, and reset the board. Stop other code using the same SPI
pins. For intermittent corruption or clock-dependent failures, revisit the
deferred logic-analyzer capture before changing timing.

For HTTPS failure, check plain TCP/DNS connectivity, SNTP access (UDP 123),
the hostname, included root and certificate validity. `esp.get_time()` on
11.1.4 returns a one-element tuple and raises while time is unavailable.
TLS fails securely if time cannot synchronize; do not disable verification.
Close sockets in `finally` blocks to avoid exhausting the four slots.

For Docker access errors, ensure your user can access the Docker daemon.
For flash errors after USB re-enumeration, identify the new explicit serial
port and retry. Build details are in `dist/build.log`; sizes and hashes are
in `dist/size.json` and `dist/build-manifest.json`.

## License

Project code is MIT unless a file identifies another license. Original Seeed
copyrights are preserved. The refactored TLS adapter has Apache-2.0 provenance;
the Mozilla-derived root data is MPL-2.0. See [LICENSE](LICENSE),
[THIRD_PARTY.md](THIRD_PARTY.md) and [licenses/](licenses/).

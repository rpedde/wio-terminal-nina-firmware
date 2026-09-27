# Licensing and attribution

The project began as Seeed Studio's `seeed-ambd-firmware`, copyright 2020
Seeed Studio, distributed under the [MIT license](LICENSE). The NINA port
and project tooling use the same license except for the components below.
The original RPC, BLE, EasyLogger and mDNS sources have been removed from
the active tree; historical revisions retain their original notices.

| Component | Attribution and license |
| --- | --- |
| `src/wifi/wifi_ssl_client.c` | Refactored TLS adapter; original copyright 2006–2015 ARM Limited and 2017 Evandro Luis Copercini. Apache-2.0; retained file notice and [license text](licenses/Apache-2.0.txt). The NINA refactor replaces RPC ownership, verification, deadlines and cleanup. |
| `certificates/roots.pem`, `src/nina/nina_roots.h` | Mozilla-derived certificate data, MPL-2.0; [license text](licenses/MPL-2.0.txt). Source, selection, hashes and reproduction command are in [metadata](certificates/metadata.json) and [certificate documentation](certificates/README.md). |

Adafruit ESP32SPI 11.1.4 and Adafruit NINA firmware 3.3.0 (reference commit
`586ef2a`) are the interoperability references used for the wire fixtures.
They are not bundled into this source tree or required for a firmware build.
CircuitPython and ESP32SPI are installed separately on the SAMD51.

The Docker toolchain downloads the pinned Seeed ArduinoCore-ambd, Realtek
SDK/compiler tools, Arduino CLI and Seeed ambd_flash_tool. Their own licenses
and notices remain in the installed archives. The linked firmware includes
vendor SDK code, lwIP and mbedTLS; the project's MIT license does not replace
those components' terms. Exact source URLs, revisions and hashes are recorded
in [toolchain.lock.json](tools/toolchain.lock.json) and the build manifest.
Before redistributing a toolchain image or release archive, preserve the
applicable notices from those pinned dependencies alongside the artifacts.

Release archives preserve pinned SDK notices in [SDK-NOTICES.txt](licenses/SDK-NOTICES.txt)
and the Arduino LGPL-2.1 text in [LGPL-2.1.txt](licenses/LGPL-2.1.txt).
The exact SDK sources are identified by the manifest and lock file. The build
recipe in tools/container.py records the linker changes applied to the core.

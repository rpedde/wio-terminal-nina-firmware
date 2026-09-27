# Port the Adafruit NINA SPI Protocol to the Wio Terminal RTL8720DN

## 1. Objective and Compatibility Contract

Replace this fork's eRPC-over-UART service with firmware that makes the Wio Terminal's RTL8720DN appear to CircuitPython as a standard Adafruit NINA/Airlift Wi-Fi coprocessor.

The first release must work with:

- The stock CircuitPython build for `seeeduino_wio_terminal`.
- The unmodified `adafruit-circuitpython-esp32spi` 11.1.4 library.
- Protocol behavior matching Adafruit `nina-fw` 3.3.0, using the adjacent `../nina-fw` checkout at commit `586ef2a` as the conformance reference.
- Station-mode Wi-Fi, DHCP, DNS, network scanning, ping, TCP clients, UDP clients, and certificate-verified HTTPS clients.
- Existing Wio Terminal board names:
  - `board.RTL_CLK`
  - `board.RTL_MOSI`
  - `board.RTL_MISO`
  - `board.RTL_CS`
  - `board.RTL_READY`
  - `board.RTL_PWR`

No CircuitPython fork or Wio-specific Python transport shim is part of the result. A normal application must be able to construct `ESP_SPIcontrol` directly:

```python
import board
import busio
import digitalio
from adafruit_esp32spi import adafruit_esp32spi

spi = busio.SPI(
    board.RTL_CLK,
    MOSI=board.RTL_MOSI,
    MISO=board.RTL_MISO,
)
cs = digitalio.DigitalInOut(board.RTL_CS)
ready = digitalio.DigitalInOut(board.RTL_READY)
reset = digitalio.DigitalInOut(board.RTL_PWR)

esp = adafruit_esp32spi.ESP_SPIcontrol(spi, cs, ready, reset)
```

AP mode, TCP server use, enterprise Wi-Fi, BLE/HCI, GPIO proxy commands, filesystem commands, OTA, and Adafruit's low-level BSD-style commands `0x70-0x7f` are explicitly deferred.

## 2. Established Hardware and Repository Facts

- The current firmware initializes the Realtek Wi-Fi stack and FreeRTOS correctly but starts eRPC on `Serial2` at 614400 baud.
- The UART is multiplexed onto pins that are also part of the RTL high-speed SPI interface, so the new firmware must remultiplex those pins to USI0 in SPI slave mode.
- The Wio Terminal routes a complete SPI bus between the SAMD51 and RTL8720DN:
  - RTL PA30: SPI clock
  - RTL PA25: host MOSI / RTL receive
  - RTL PA26: host MISO / RTL transmit
  - RTL PA28: chip select
  - RTL PA12: `IRQ0`, used as NINA READY
  - RTL `CHIP_PU`: driven by `RTL_PWR`, used as hardware reset
  - RTL PA13: `SYNC`/`RTL_DIR` is not required by the NINA protocol and must remain a high-impedance input
- Pin mapping corrected during phase 2 against schematic sheet 6; the original PB13/14/16/17 mapping described unconnected module pins. See `PHASE2.md`.
- The RTL8720DN USI0 peripheral supports SPI slave mode, making the design electrically possible. See the [Wio Terminal schematic](https://files.seeedstudio.com/wiki/Wio-Terminal/res/Wio-Terminal-SCH-v1.2.pdf) and [RTL872xD datasheet](https://cdn.sparkfun.com/assets/5/0/5/6/0/UM0401-RTL872xD-Datasheet-v2.9.pdf).
- Wio RTL flashing requires Seeed's SAMD-to-RTL bridge tooling and a directory containing `km0_boot_all.bin`, `km4_boot_all.bin`, and `km0_km4_image2.bin`. See the [Seeed flashing procedure](https://wiki.seeedstudio.com/Wio-Terminal-Network-Overview/).

## 3. Target Firmware Architecture

Create five separable subsystems beneath `src/nina/`:

1. `nina_transport`
   - Owns USI0 in SPI slave mode, READY, CS transaction completion, DMA/FIFO buffers, timeouts, and peripheral recovery.
   - Does not understand NINA commands.
2. `nina_protocol`
   - Parses command envelopes and serializes replies.
   - Contains no Realtek or FreeRTOS dependencies so it can be compiled and fuzzed on the host.
3. `nina_wifi`
   - Maps NINA connection, status, scan, addressing, DNS, and ping commands onto the existing Realtek APIs.
4. `nina_sockets`
   - Owns the socket table and maps NINA TCP/UDP/TLS operations onto lwIP and mbedTLS.
5. `nina_server`
   - Runs the protocol task, dispatches parsed commands, manages state transitions, and returns replies to the transport.

Keep `src/wifi/` initially, but separate reusable Wi-Fi/lwIP/TLS functions from RPC-specific wrappers. New NINA code must call typed internal functions rather than `rpc_*` functions that expose `binary_t`, retained pointers, or eRPC allocation rules.

The final startup sequence in `wio-terminal-nina-firmware.ino` must be:

1. Initialize logging in production-safe mode.
2. Initialize the Realtek Wi-Fi and TCP/IP stacks.
3. Initialize the socket table and synchronization objects.
4. Initialize USI0 in SPI slave mode and READY.
5. Start the NINA server task.
6. Leave Arduino `loop()` idle except for watchdog-friendly delay/yield behavior.

Do not initialize BLE or any eRPC server/client objects.

## 4. SPI Transport and Handshake

### Electrical configuration

Configure USI0 in SPI slave mode as:

- Slave mode
- SPI mode 0: CPOL 0, CPHA 0
- MSB first
- Eight-bit words
- Chip-select boundaries observed on GPIO with USI reset between frames (phase 2 hardware-validated backend; see `PHASE2.md`)
- DMA when supported
- Required operating rate: 8 MHz

The Arduino `SPIClass` is expected to expose only master-oriented functionality. Use the Realtek SDK's slave API from the pinned ArduinoCore-ambd package. If that public HAL does not expose transaction length or reliable DMA completion, use the underlying RTL8721D USI SSI HAL directly, isolated entirely inside `nina_transport`.

### Buffers

Define:

```text
NINA_SPI_BUFFER_SIZE = 4092
NINA_MAX_RESPONSE_DATA = 4084
```

Allocate one aligned receive buffer and one aligned transmit buffer statically or once during startup. Do not allocate transport buffers per request.

The limit matches the practical ESP32 NINA DMA transaction size. A response containing one 16-bit-length parameter can carry at most 4084 data bytes after envelope and alignment overhead.

### READY behavior

READY is active low from the CircuitPython driver's perspective. Implement this exact state machine:

```text
BOOT
  READY = high
  initialize SPI

ARM_COMMAND_RX
  clear receive buffer
  queue/arm receive transaction
  READY = low

HOST_WRITES_COMMAND
  CS falling edge causes READY = high
  receive until CS rises
  record the actual byte count
  signal protocol task

PROCESS_COMMAND
  READY remains high
  validate and dispatch request
  build aligned response

ARM_RESPONSE_TX
  queue/arm response transaction
  READY = low

HOST_READS_RESPONSE
  CS falling edge causes READY = high
  transmit until CS rises
  verify that the transaction completed
  return to ARM_COMMAND_RX
```

This matches the current driver, which waits for READY low, asserts CS, waits for READY high, and then performs the SPI transfer. See the [Adafruit ESP32SPI transport behavior](https://raw.githubusercontent.com/adafruit/Adafruit_CircuitPython_ESP32SPI/main/adafruit_esp32spi/adafruit_esp32spi.py).

### Recovery

- An unexpected CS edge, zero-length transfer, overrun, underrun, or transfer timeout must abort the current transaction, return READY high, reset/reinitialize the SPI peripheral, and re-enter `ARM_COMMAND_RX`.
- Never leave READY low unless an RX or TX transaction is actually armed.
- Processing errors return a NINA error frame; transport errors discard the incomplete frame and rearm.
- Add counters for RX frames, TX frames, parse failures, unknown commands, overruns, underruns, and transport resets.
- Debug builds may expose counters through the logging UART. Production builds must not write debug data on any SPI signal pin.

## 5. NINA Wire Format

### Request

```text
byte 0        0xE0 START_CMD
byte 1        command ID, reply bit clear
byte 2        parameter count
parameters    length followed by parameter data
trailer       0xEE END_CMD
padding       bytes to a four-byte transaction boundary (ignored on receive)
```

Most commands use an unsigned eight-bit parameter length. Commands `0x44`, `0x45`, and `0x46` use unsigned 16-bit big-endian parameter lengths in requests. The data response from `0x45` also uses a 16-bit big-endian parameter length.

ESP32SPI 11.1.4 reuses its send buffer without clearing padding. Accept the
one-to-three alignment bytes after `END_CMD` regardless of their value;
reject extra full words. Replies always have zero padding. Commands `0x21`
through `0x26` carry one dummy one-byte request parameter in the stock driver.

### Successful response

```text
byte 0        0xE0
byte 1        request command ID | 0x80
byte 2        response parameter count
parameters    length followed by parameter data
trailer       0xEE
padding       zero bytes to a four-byte boundary
```

### Error response

```text
0xEF 0x00 0xEE 0x00
```

### Parser requirements

The parser must reject:

- Buffers shorter than the minimum frame.
- Missing `0xE0` or `0xEE`.
- Request commands with the reply bit already set.
- Unsupported command IDs.
- Parameter counts that do not match the command definition.
- Parameter lengths extending beyond the received transaction.
- Embedded strings exceeding their command-specific limits.
- Socket indexes outside the configured table.
- Frames whose decoded or encoded result exceeds 4092 bytes.

The parser must not read unaligned integers by pointer casting. Decode all integers byte-by-byte or with `memcpy` into correctly aligned storage, then apply explicit byte order conversion.

## 6. NINA Command Surface

Implement these commands and response shapes exactly as exercised by ESP32SPI 11.1.4.

| ID | Command | Required behavior |
|---:|---|---|
| `0x10` | Set network | Start an asynchronous connection to an open network; return accepted status immediately. |
| `0x11` | Set passphrase | Start an asynchronous WPA/WPA2 connection; enforce SSID <=32 bytes and passphrase <=63 bytes. |
| `0x14` | Set IP configuration | Apply static IPv4 address, gateway, and mask through the Realtek TCP/IP adapter. |
| `0x15` | Set DNS configuration | Apply primary and secondary IPv4 DNS servers. |
| `0x16` | Set hostname | Apply the DHCP hostname before the next connection. |
| `0x20` | Connection status | Return the maintained NINA `WL_*` state. |
| `0x21` | IP information | Return IPv4 address, subnet mask, and gateway as three four-byte parameters. |
| `0x22` | MAC address | Return the six-byte address in the byte order expected by ESP32SPI's `MAC_address` property. |
| `0x23` | Current SSID | Return the connected SSID without a required trailing NUL. |
| `0x24` | Current BSSID | Return six bytes. |
| `0x25` | Current RSSI | Return signed little-endian 32-bit dBm. |
| `0x26` | Current encryption | Map Realtek security to NINA values: open `7`, WEP `5`, WPA `2`, WPA2/mixed `4`, unknown `255`. |
| `0x27` | Scan results | Return one SSID parameter per cached network. |
| `0x28` | Start server/socket | Support the UDP bind path used by ESP32SPI's UDP socket implementation; return failure for deferred TCP-server modes. |
| `0x2A` | Data sent | Return success after synchronous socket writes complete. |
| `0x2B` | Available data | Return available byte count as little-endian 16-bit. |
| `0x2C` | Read one byte | Preserve protocol compatibility even though normal SocketPool traffic uses `0x45`. |
| `0x2D` | Start client | Open TCP, prepare UDP destination, or establish TLS according to the requested mode. |
| `0x2E` | Stop client | Close and fully release the slot; repeated close must be harmless. |
| `0x2F` | Client state | Return `4` for established TCP/TLS and `0` when closed. |
| `0x30` | Disconnect | Disconnect Wi-Fi, stop SNTP, close every socket, and return success. |
| `0x32` | Indexed RSSI | Return scan result RSSI as signed little-endian 32-bit. |
| `0x33` | Indexed encryption | Return mapped NINA encryption type. |
| `0x34` | Request DNS lookup | Resolve and cache one hostname result. |
| `0x35` | Get DNS result | Return the cached four-byte IPv4 address. |
| `0x36` | Start scan | Start an asynchronous Realtek scan and clear the previous snapshot. |
| `0x37` | Firmware version | Return `3.3.0+rtl8720.1`, including a trailing NUL only if required by golden-driver behavior. |
| `0x39` | Send UDP data | Finalize and send the accumulated UDP datagram. |
| `0x3A` | Remote endpoint | Return remote IPv4 address and two-byte port in NINA's expected byte order. |
| `0x3B` | Get time | Return a signed little-endian 64-bit Unix timestamp after SNTP synchronization. |
| `0x3C` | Indexed BSSID | Return six bytes from cached scan data. |
| `0x3D` | Indexed channel | Return one channel byte. |
| `0x3E` | Ping | Return elapsed milliseconds as an unsigned little-endian 16-bit value; use `0xffff` for failure. |
| `0x3F` | Allocate socket | Return a free slot or `255` when exhausted. |
| `0x44` | Send TCP/TLS data | Send the supplied 16-bit-length payload and return bytes sent. |
| `0x45` | Read data buffer | Return up to the requested size, capped at 4084 bytes, using a 16-bit response length. |
| `0x46` | Insert UDP data | Append the supplied bytes to the current UDP datagram and return success. |

Return the standard NINA error frame for all other command IDs.

## 7. Wi-Fi State and Scanning

Maintain an explicit NINA connection state rather than deriving every result from one Realtek boolean:

```text
startup                  WL_IDLE_STATUS (0)
SSID unavailable         WL_NO_SSID_AVAIL (1)
scan complete            WL_SCAN_COMPLETED (2), only for scan state where applicable
connected                WL_CONNECTED (3)
authentication/failure   WL_CONNECT_FAILED (4)
unexpected link loss     WL_CONNECTION_LOST (5)
requested disconnect     WL_DISCONNECTED (6)
```

Connection commands must enqueue work to a Wi-Fi worker and return their acknowledgement before the ESP32SPI ten-second response timeout. The worker updates status when Realtek callbacks report success or failure.

Scanning must:

- Use the existing asynchronous Realtek scan callback.
- Store at most `WL_NETWORKS_LIST_MAXNUM` results.
- Clamp SSIDs to 32 bytes and guarantee internal NUL termination.
- Deduplicate identical BSSIDs.
- Preserve SSID, BSSID, RSSI, channel, and security type in one immutable snapshot.
- Protect snapshot replacement with a mutex so indexed commands always see a consistent generation.
- Return zero results while the first scan is incomplete rather than exposing partially written records.

Default to the US country/channel plan. Add a compile-time `WIFI_COUNTRY` setting with documented values; changing it requires a rebuild.

## 8. Socket Model

Configure four NINA-visible socket slots:

```text
NINA_MAX_SOCKETS = 4
NINA_MAX_TLS_SOCKETS = 1
```

Each slot tracks:

- Free, TCP, UDP, or TLS type.
- Allocated, connecting, established, or closed state.
- lwIP descriptor.
- TLS context when applicable.
- UDP destination and partially accumulated datagram.
- Whether it has ever connected, so dead sockets can be reclaimed.
- Last error.
- Per-slot mutex.

Rules:

- `0x3F` reserves a slot before returning it.
- A reserved slot must be released by close or automatically after a failed connection.
- TCP and TLS writes must handle partial `send()` results until the complete requested chunk is written or an error occurs.
- Reads must never block waiting for the full requested size. Return currently available data, EOF, or an error.
- Configure finite socket and TLS timeouts below the host's ten-second command-response timeout.
- UDP insertion must reject a datagram that would exceed 4084 bytes.
- Disconnect/reset closes all descriptors and frees all mbedTLS objects.
- One failed socket must not reset Wi-Fi or other sockets.
- Protect lwIP calls if the Realtek port requires single-threaded TCP/IP execution; use the SDK's TCP/IP callback mechanism rather than calling non-thread-safe APIs from SPI interrupt context.
- No Wi-Fi, socket, DNS, scan, or TLS work may run in an SPI ISR.

## 9. Verified TLS and Time

Retain and refactor `wifi_ssl_client.c` as the Realtek mbedTLS backend.

TLS requirements:

- TLS is available only when the destination was supplied as a hostname.
- Set the hostname with `mbedtls_ssl_set_hostname()` before the handshake to enable SNI and hostname verification.
- Set `MBEDTLS_SSL_VERIFY_REQUIRED`.
- Load a built-in Mozilla-derived server-auth root bundle.
- Reject an IP-only TLS destination because the stock ESP32SPI driver already documents hostname-only TLS mode.
- Allow only one active TLS context, matching the host driver's existing restriction.
- Cap connect plus handshake time so the NINA response is produced within ten seconds.
- On certificate, hostname, allocation, timeout, or handshake failure, destroy the TLS context and release the socket slot.
- Never use `MBEDTLS_SSL_VERIFY_NONE` in production.
- Do not implement client-certificate commands in the first release.

Add `certificates/roots.pem`, its source metadata, and a deterministic update tool. The checked-in metadata must contain:

- Mozilla source URL or source repository.
- Source revision/date.
- SHA-256 of the input.
- Generation command.
- SHA-256 of the produced PEM.

Normal Docker builds must use the checked-in bundle and must not download current certificates. Certificate updates are an explicit maintenance operation followed by size and HTTPS regression tests.

Start SNTP after a successful DHCP connection. TLS connection requests must wait for valid wall-clock time for at most five seconds; if time remains earlier than 2000-01-01, fail securely rather than bypassing certificate date checks.

## 10. Docker Build and Flash Workflow

Add a single host entry point named `fw` with these commands:

```text
./fw image
./fw build
./fw test
./fw shell
./fw flash-rtl --port /dev/ttyACM0
./fw erase-rtl --port /dev/ttyACM0
./fw install-circuitpython --mount /path/to/WIO_TERMINAL --uf2 file.uf2
./fw clean
```

### Image contents

The Docker image must pin:

- Ubuntu base image by digest.
- Arduino CLI version and checksum.
- Realtek AmebaD board package version `3.0.5`.
- The exact Seeed `ArduinoCore-ambd` commit proven by the baseline build.
- ARM/asdk compiler archive URL and checksum.
- Python dependencies used by Seeed flashing tools.
- A specific `ambd_flash_tool` commit rather than downloading its latest firmware or executable dynamically.

The initial toolchain task is to reproduce the current unmodified `v2.1.3` firmware build inside the image. Once successful, record every resolved revision and checksum; subsequent builds must fail rather than silently upgrading dependencies.

### Build behavior

- Copy source into a container build directory rather than compiling directly in the mounted source tree.
- Export only artifacts to the host `dist/` directory.
- Produce:
  - `dist/firmware/km0_boot_all.bin`
  - `dist/firmware/km4_boot_all.bin`
  - `dist/firmware/km0_km4_image2.bin`
  - ELF and map files
  - flash/RAM size report
  - build manifest containing Git revision, dirty-state flag, tool versions, core revision, country setting, firmware version, and certificate bundle hash
- Run as the invoking UID/GID so `dist/` is not root-owned.
- Use named Docker volumes only for disposable compiler and Arduino download caches.
- A no-cache build must remain supported.

### Flash behavior

`flash-rtl` must:

1. Require an explicit serial device.
2. Verify that all three required images exist.
3. Pass only that device into the container.
4. Invoke the pinned Seeed tool with `flash -d dist/firmware`.
5. Never ask the tool to download "latest" firmware.
6. Report that the SAMD51 may now contain Seeed's temporary bridge firmware and that CircuitPython must be restored.
7. Refuse to run `erase-rtl` without a second explicit confirmation flag because erase is destructive and normally unnecessary.

`install-circuitpython` remains a host-side UF2 copy because containerized mass-storage discovery is brittle. It must verify the target mount resembles a SAMD bootloader volume and copy the explicitly supplied UF2. Do not automatically download or guess a CircuitPython build.

## 11. Testing

### Native protocol tests

Compile `nina_protocol` on the host and test:

- Every supported request and expected response envelope.
- Eight-bit and 16-bit parameter lengths.
- Four-byte padding for every possible remainder.
- Zero-length parameters.
- Maximum legal response.
- Truncated and oversized frames.
- Incorrect start/end markers.
- Incorrect parameter counts.
- Unsupported commands.
- Invalid socket IDs.
- Signed RSSI and all multibyte endian conversions.
- Fuzz input with sanitizers; no crash, out-of-bounds access, leak, or unbounded allocation is acceptable.

Generate golden fixtures from the behavior expected by ESP32SPI 11.1.4 and cross-check them against `../nina-fw` 3.3.0. The adjacent checkout is a development oracle, not a runtime or build dependency.

### Backend tests

Provide mock backends for:

- Successful and failed Wi-Fi connection.
- Link loss and explicit disconnect.
- Scan completion and snapshot replacement.
- DNS success/failure.
- Socket exhaustion and reuse.
- Partial writes.
- EOF and nonblocking reads.
- UDP accumulation/send.
- TLS success, verification failure, timeout, and allocation failure.

### Container tests

CI must run:

1. Native unit tests with AddressSanitizer and UndefinedBehaviorSanitizer.
2. Docker image build.
3. Clean firmware build.
4. Artifact-name and artifact-count checks.
5. Flash/RAM size extraction.
6. `git diff --check`.
7. A guard proving generated build files did not modify tracked source.

### Hardware tests

Use a physical Wio Terminal running stock CircuitPython and ESP32SPI 11.1.4.

Transport gate:

- Read firmware version 100 consecutive times at 8 MHz.
- Reset the RTL 20 times and successfully read status after every reset.
- Run at least 10,000 mixed status/version requests without a timeout or malformed response.
- Logic-analyzer traces confirming READY/CS ordering and clock rate are
  deferred and nonblocking by user decision on 2026-09-12. Revisit for
  intermittent corruption, unexplained timeouts, reset-dependent failures,
  or SPI-speed/pause-dependent behavior; see `PHASE2.md`.

Network acceptance:

- Scan and inspect SSID, BSSID, RSSI, channel, and encryption.
- Connect to an open test AP.
- Connect to a WPA2 test AP.
- Verify DHCP address, mask, gateway, MAC, SSID, BSSID, and RSSI.
- Resolve a hostname and ping it.
- Perform a plain HTTP GET over TCP.
- Perform a UDP NTP request and receive its response.
- Perform a verified HTTPS GET to a hostname chaining to an included root.
- Confirm TLS rejection for an untrusted certificate.
- Confirm TLS rejection for a hostname mismatch.
- Transfer payloads larger than one 64-byte ESP32SPI write chunk.
- Request a receive larger than one SPI response chunk.
- Exhaust all four sockets, confirm allocation returns `255`, close them, and confirm reuse.
- Repeat connect, transfer, and close for 100 cycles without losing socket capacity or increasing reported free-heap loss beyond a small stable initialization allowance.
- Disconnect and reconnect Wi-Fi while sockets are allocated.
- Power-cycle and repeat the basic HTTPS test.

## 12. Implementation Sequence and Gates

### Phase 1: Reproducible baseline

- Containerize the current build without changing firmware behavior.
- Export and flash the existing three-image eRPC firmware.
- Record the working core/toolchain revisions and checksums.

Gate: clean Docker build and successful RTL flash.

### Phase 2: SPI proof

- Implement only `nina_transport`.
- Return a fixed valid response to `GET_FW_VERSION` and `GET_CONN_STATUS`.
- Validate READY behavior and 8 MHz operation with CircuitPython.

Gate: functional transport tests passed on real hardware; phase 3 may proceed.
The user waived the blocking logic-analyzer gate on 2026-09-12. Electrical
timing capture remains deferred, not passed; see `PHASE2.md` for evidence,
limitations and criteria for revisiting capture.

### Phase 3: Protocol and Wi-Fi control

- [x] Add the portable parser/serializer.
- [x] Implement status, version, connection, addressing, scanning, DNS, and ping.
- [x] Add native golden-vector tests.
- [x] Validate the stock-driver network gate on hardware; see `PHASE3.md`.

Gate: stock ESP32SPI can scan, connect, report addressing, resolve DNS, and ping.
Passed on 2026-09-12 using stock CircuitPython 10.3.0 and ESP32SPI 11.1.4
with the configured WPA2 AP. Static IPv4/reconnect, explicit DNS, connection
failure/cancellation and the phase 2 transport regression also pass.
Separate open-AP testing and direct DHCP hostname-option inspection remain
unrun; see `PHASE3.md` for the exact coverage and evidence.

### Phase 4: TCP and UDP

- [x] Add socket allocation/state management.
- [x] Implement TCP connect/send/receive/close and UDP destination/bind/accumulate/send/receive.
- [x] Add stock-driver fixtures, mock lifecycle tests and live loopback tests.
- [x] Validate HTTP/NTP and socket exhaustion/reuse on the physical Wio.

Gate: HTTP and NTP tests pass, including socket exhaustion and reuse.
Passed on 2026-09-12 with stock CircuitPython 10.3.0 and ESP32SPI 11.1.4,
including 100 TCP transfer/close cycles and the transport regression.
See `PHASE4.md` for results and remaining broader acceptance coverage.

### Phase 5: Verified TLS

- [x] Refactor the existing mbedTLS integration.
- [x] Add root bundle, SNTP synchronization, hostname verification, cleanup, and timeout behavior.
- [x] Validate trusted HTTPS and certificate rejection on the physical Wio; see `PHASE5.md`.

Gate: trusted HTTPS succeeds and both untrusted and mismatched certificates fail.
Passed on 2026-09-13 with stock CircuitPython 10.3.0 and ESP32SPI 11.1.4;
expired certificates are also rejected. Follow-up physical power-cycle HTTPS
and RTL heap checks pass: 100 HTTPS cycles and 30 certificate failures leave
zero post-warm-up heap loss. See `PHASE5.md` for exact coverage.

### Phase 6: Cleanup

- [x] Remove eRPC, generated RPC shims, BLE RPC, duplicate generated trees, PowerShell generators, and obsolete build scripts from the active project.
- [x] Remove RPC types from reusable Wi-Fi code.
- [x] Update licensing and attribution.
- [x] Rewrite the README around NINA compatibility, Docker workflow, flashing, CircuitPython restoration, example code, supported commands, limitations, and troubleshooting.

Gate: all native, container, and hardware acceptance tests still pass after removal.
Passed on 2026-09-17: native/container, Wi-Fi, TCP/UDP, verified TLS, transport,
zero post-warm-up heap loss and physical cold-start HTTPS regressions.
Production firmware and CircuitPython are restored. Existing coverage
deferrals and diagnostic interruptions are recorded in `PHASE6.md`.

### Phase 7: Release artifact

- [x] Produce a versioned archive containing the three RTL images, build manifest, checksums, release notes, supported-command matrix, and the matching CircuitPython example.
- [x] Tag the first working release as `v0.1.0`.

Release packaging and GitHub automation are documented in [PHASE7.md](PHASE7.md)
and [RELEASING.md](../RELEASING.md).

## 13. Completion Criteria

The project is complete when a user with only Docker, a Wio Terminal, a USB cable, a stock Wio Terminal CircuitPython UF2, and the stock ESP32SPI 11.1.4 library can:

1. Build the RTL firmware without installing Arduino, Realtek, ARM, or Python toolchains on the host.
2. Flash the three generated RTL images using the documented container workflow.
3. Restore CircuitPython on the SAMD51.
4. Run the standard `ESP_SPIcontrol` initialization without a custom driver.
5. Connect to Wi-Fi and successfully make a certificate-verified HTTPS request.
6. Repeat resets and socket operations without transport deadlocks, socket leakage, or silent TLS verification bypass.

# Phase 4: TCP and UDP

**Phase 4 hardware gate passed on 2026-09-12**, using stock Wio
CircuitPython 10.3.0 and ESP32SPI 11.1.4. The Wio was connected; initial
sandbox device discovery hid its serial ports. Host-level inspection found
`/dev/ttyACM0`, and flashing plus hardware acceptance then succeeded.

## Implementation

`nina_sockets.c` owns four reserved slots, per-slot synchronization, descriptor
cleanup, TCP partial writes and EOF detection, and fixed UDP transmit/receive
buffers. Allocation reserves immediately, failed connection releases the
reservation, and close is idempotent. Dead TCP slots are reclaimed only after
pending stream data has drained. Disconnect closes all slots synchronously
before requesting the Wi-Fi disconnect. Hardware reset naturally rebuilds the
table; SPI framing recovery does not terminate unrelated network connections.

The typed Realtek backend uses nonblocking lwIP BSD sockets, whose operations
marshal through the SDK TCP/IP thread. Connect and complete-chunk TCP writes
each have a four-second budget; hostname resolution adds at most four seconds.
Reads never wait for the requested length. No socket operation runs in an ISR.
TLS modes and TCP server modes return failure and release the reserved slot.
Verified TLS remains Phase 5.

UDP supports both bind-before-destination and destination-before-bind (the
latter is used by ESP32SPI's `socket_connect`). Insertion accepts zero-length
chunks, including the extra chunk the driver emits for exact multiples of
64 bytes. Accumulation is capped at 4084 bytes; overflow invalidates the
pending datagram until a new destination command, preventing partial sends.
Partial receive reads and peek preserve the remaining datagram. Datagrams
larger than the fixed receive buffer are truncated to 4084 bytes.

New dispatch: `0x28`, `0x2a`–`0x2f`, `0x39`, `0x3a`, `0x3f`, `0x44`–`0x46`.
Both hostname/IP client forms and both bind envelope forms are validated.
A failed client start returns a one-byte zero status so the pinned driver's
`socket_open` raises its intended ConnectionError (the reference's empty
parameter list would cause an IndexError). Remote ports are little endian,
matching ESP32SPI 11.1.4's `get_remote_data`; the adjacent NINA reference emits
big endian here. Request ports remain big endian. TCP byte counts and read
sizes are little endian; wide parameter lengths are big endian.

## Verification

- Docker firmware build produces the required three images, ELF, map, sizes,
  checksums and manifest. No new NINA compiler warnings.
- Sanitized native suite includes 39 fixtures generated using the actual
  ESP32SPI 11.1.4 `_send_command`, parser mutation/fuzz tests, mock lifecycle
  tests (exhaustion, reservation reuse, failure cleanup, partial writes,
  timeout, EOF, UDP bounds, partial reads, and 100 reuse cycles).
- Live loopback tests compile the production backend with test-only POSIX
  OS adapters. They exercise TCP connect/write, a 5084-byte stream across
  bounded reads, EOF, UDP bind/destination/send/partial receive/remote port,
  and refused-connection cleanup. This validates socket behavior on Linux,
  not the Realtek lwIP implementation or physical SPI bus.
- Existing transport and workflow regressions and artifact checks pass.

Final verification logs: `dist/phase4-build.log` and `dist/phase4-tests.log`.
All seven tests pass. Images total 598,304 bytes; linked SRAM sections total
245,728 bytes; reserved heap is 208,896 bytes. These are link-time sizes,
not runtime heap measurements. `git diff --check` passes.

Run `./fw build` and `./fw test` to reproduce software checks.

## Hardware gate

Attach the Wio, flash with `./fw flash-rtl --port /dev/ttyACM0`, and restore
the explicitly supplied stock CircuitPython UF2 following Phase 3's procedure.
Preserve the board's existing files and credentials. Run
`tests/hardware/phase4_sockets.py` via raw REPL with ESP32SPI 11.1.4 and local
settings from `tests/hardware/settings.toml.example`.

The script checks four-slot exhaustion/reuse, HTTP with a multi-chunk write
and oversized read request, UDP NTP with partial datagram reads and remote
endpoint verification, 100 TCP transfer/close cycles, and disconnect/reconnect
with allocated slots. It does not measure runtime free heap. Public HTTP/NTP
availability depends on the test network; local targets can be configured.
Repeat the Phase 2 transport regression on the flashed image. Record console
logs and the installed manifest before marking the hardware gate passed.

## Hardware results (2026-09-12)

- `dist/phase4-flash.log`: all three images successfully flashed on the
  first attempt. Stock CircuitPython 10.3.0 was restored from the same
  supplied UF2 and SHA-256 recorded in Phase 2.
- `dist/phase4-sockets-console.txt`: four-slot reservation/exhaustion and
  repeated close, HTTP GET (829 response bytes) with a multi-chunk write and
  a 65535-byte receive request, UDP NTP with partial reads and correct remote
  port, 100 TCP connect/transfer/close cycles, repeated capacity checks, and
  Wi-Fi disconnect/reconnect with all four slots allocated. All passed.
- `dist/phase4-transport-console.txt`: 100/100 version reads, 20/20 RTL resets,
  10,000/10,000 mixed status/version requests, 38.415 seconds, no retries.
- `dist/phase4-file-preservation.json`: all 57 original files compared equal
  to `dist/phase4-circuitpython-backup`; only generated `boot_out.txt` was
  excluded. Credentials were neither printed nor changed. Existing `code.py`
  was not replaced; tests ran through raw REPL.
- `dist/phase4-edges-console.txt`: 50 ms pauses inside both request and
  response transactions pass at the configured 8 MHz.
- Installed firmware manifest: `dist/phase4-acceptance-build.json`.

The HTTP response on hardware was smaller than one SPI response chunk;
actual multi-response stream data is covered by the 5084-byte native loopback
case. Runtime free-heap measurement remains unrun. Logic-analyzer capture
remains deferred by the existing user decision. Verified TLS is Phase 5.

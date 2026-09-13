# Phase 5: verified TLS

**The required TLS hardware gate passed on 2026-09-13**, with stock
CircuitPython 10.3.0 and ESP32SPI 11.1.4 on the physical Wio Terminal.

## Implementation

The NINA socket table now supports hostname-only TLS mode 2, with one TLS
context across its four slots. TLS uses the same allocation, close, disconnect,
partial-write and buffered-read lifecycle as TCP. A failed handshake releases
the reservation and descriptor; rejection of a second TLS connection leaves
the first connection intact. IP-only TLS requests fail securely.

`src/wifi/wifi_ssl_client.c` is now a typed NINA mbedTLS adapter. The former
RPC-owned TLS implementation and its insecure no-CA mode were removed. Old RPC
headers/wrappers remain inactive and are discarded by the linker, pending
Phase 6 cleanup. No NINA code calls those wrappers.

The adapter requires TLS 1.2, SNI/hostname verification, mandatory trust-chain
verification, and the checked-in Mozilla-derived root selection described in
`certificates/README.md`. It checks entropy seeding, allocation, certificate
parsing, configuration, hostname setup, handshake and verification results.
All mbedTLS objects, including the entropy state and root chain, are freed on
failure and close. The root chain remains alive throughout the connection.

A single 8.5-second budget covers the time wait, DNS, TCP connect and TLS
handshake. Time waits at most five seconds; DNS at most four seconds or the
remaining budget; TCP at most three seconds or the remaining budget. TLS BIOs
use nonblocking sockets and check the overall deadline. Stream writes retain
the four-second complete-chunk budget. Reads consume only available plaintext;
a fixed buffer supports peek and preserves bytes after peer EOF. The protocol
task stack was increased from 4 KiB to 16 KiB for mbedTLS call depth.

SNTP starts when station addressing succeeds and stops after disconnect/link
loss. Calls execute in the TCP/IP thread. `0x3b` returns an eight-byte little
endian Unix timestamp, or zero before synchronization. The stock driver's
`get_time()` returns a one-element tuple and raises on zero. TLS refuses to
proceed with time before 2000-01-01.

## Pinned SDK caveats

The vendor library also ships placeholder threading callbacks. The adapter
registers FreeRTOS mutex initialization/free/lock/unlock before creating TLS
objects; mutex allocation/locking failure propagates securely. Without these
hooks the entropy seeding call fails with `-52` before any handshake. Seeding
uses the SDK's hardware entropy source through mbedTLS, with its return status
checked, and no application-generated pseudo-random fallback.

The pinned Realtek mbedTLS 2.4.0 library disables `MBEDTLS_HAVE_TIME_DATE`.
A mandatory verification callback therefore adds future/expired flags for
every certificate in the verified chain. It never clears trust, signature,
or hostname failure flags. Certificate dates use portable UTC conversion.

The vendor `sntp_gen_system_time()` adds 1900 to `tm_year` and one to `tm_mon`
itself. The production adapter instead reads `sntp_get_lasttime()` and advances
the raw Unix seconds with elapsed FreeRTOS ticks. This was checked against
`sntp.o` from the pinned `lib_arduino.a`, and native tests cover unsynchronized
samples and microsecond/second carry.

## Reproduction and evidence

- `./fw image`, `./fw build`, `./fw test`.
- `tests/hardware/phase5_tls.py` uses stock CircuitPython and ESP32SPI 11.1.4.
  It checks SNTP, trusted HTTPS, reachable untrusted/mismatched/expired endpoints,
  recovery after each rejection, multi-chunk requests, oversized read requests,
  100 TLS cycles with capacity checks, and disconnect/reconnect cleanup.
- Native tests include stock-driver wire fixtures, TLS slot lifecycle, and
  sanitized failure injection around the production TLS adapter. The TLS API
  mock tests policy, cleanup, timeouts, dates and buffering; it does not prove
  cryptographic verification. That requires the hardware gate.
- Certificate updates are explicit, offline generation from a hash-verified
  dated input. Normal Docker builds check the PEM/header hashes and embed the
  bundle hash in the build manifest without downloading certificates.

The production image (without TLS diagnostics) successfully fetched 765 bytes
from `sha256.badssl.com`, rejected `self-signed.badssl.com`,
`wrong.host.badssl.com` and `expired.badssl.com`, and fetched trusted HTTPS again
after each rejection. Each negative endpoint was first checked with a plain
TCP connection so network unreachability could not count as rejection.
Reservation/capacity checks run before explicitly closing failed connections.
See `dist/phase5-tls-console.txt`.

The native suite passes all nine tests, including 43 stock-driver fixtures,
ASan/UBSan TLS failure injection, TCP/UDP loopback and transport fuzz/stress.
Logs: `dist/phase5-tests.log` and `dist/phase5-build.log`. The three images total
635,168 bytes; linked SRAM sections total 245,984 bytes, with 208,896 bytes of
reserved heap. These are link-time sizes, not runtime free-heap measurements.
Root regeneration from the recorded input reproduces the checked-in PEM and
header exactly; artifact checks and `git diff --check` pass.

The user's original board files were backed up to
`dist/phase5-circuitpython-backup` before flashing. The same supplied
CircuitPython 10.3.0 UF2 recorded in Phase 2 was restored. Tests run through raw
REPL and do not replace `code.py` or credentials. The final manifest is
`dist/phase5-acceptance-build.json`; preservation evidence
is `dist/phase5-file-preservation.json` (57 original files unchanged, excluding
generated `boot_out.txt`).

`dist/phase5-tls-console.txt` also records 100 successful TLS connect/transfer/
close cycles with full capacity checks after every cycle, and successful
verified HTTPS after disconnecting Wi-Fi with an active TLS socket and then
reconnecting. The test runs on the production image, with `NINA_TLS_DEBUG`
disabled.

The final production image also passes:

- `dist/phase5-sockets-console.txt`: HTTP, UDP NTP, exhaustion/reuse, 100 TCP
  cycles, and Wi-Fi disconnect/reconnect cleanup.
- `dist/phase5-transport-console.txt`: 100 version reads, 20 RTL hardware
  resets, and 10,000 mixed requests in 38.397 seconds, without retries or errors.
- `dist/phase5-release-pins-console.txt`: test pins released after acceptance.

Runtime free-heap trends and a physical power-cycle test remain separate from
the required trusted/untrusted/hostname gate. Logic-analyzer capture remains
deferred under the existing Phase 2 decision.

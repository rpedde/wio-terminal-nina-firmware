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

The follow-up below completes runtime heap measurement and the physical
power-cycle check. Logic-analyzer capture remains deferred under the existing
Phase 2 decision.

## Follow-up power-cycle and heap validation

The user confirmed removing all power for at least ten seconds and reconnecting
on 2026-09-13. The previously accepted production firmware then synchronized
SNTP and completed a verified HTTPS request to `sha256.badssl.com` (765 bytes),
with socket capacity checks before and after. See
`dist/phase5-power-cycle-console.txt` and the preserved production manifest in
`dist/phase5-production-before-heap/build-manifest.json`.

Heap instrumentation is explicitly opt-in:

```sh
./fw image
NINA_HEAP_DIAGNOSTICS=1 ./fw build
./fw flash-rtl --port /dev/ttyACM0
# Restore the supplied CircuitPython UF2, then run phase5_heap.py via raw REPL.
python3 tools/analyze-heap-log.py dist/phase5-heap-console.txt \
    --output dist/phase5-heap-results.json
```

`NINA_HEAP_DIAGNOSTICS` accepts only 0 or 1, defaults to 0, and is recorded as
`heap_diagnostics` in the manifest. When enabled, existing status requests
emit current and minimum-ever FreeRTOS heap on the dedicated RTL LOG UART.
The NINA wire response stays unchanged; no additional command is introduced.
The test reads that UART using `board.RTL_RXD`/`board.RTL_TXD`, and prints only
heap records, excluding other vendor log content. The SDK's tiny formatter
requires `%d`; `%u` does not print numeric values.

The test measures before/while/after the first TLS connection, warms up ten
connections, then measures after each of 100 verified HTTPS cycles and after
ten batches of three certificate failures. It checks HTTPS recovery and waits
130 seconds for asynchronous network cleanup. The predeclared allowance is at
most 1 KiB of final loss against the warm baseline. Minimum-ever heap describes
the entire boot/run and is not a TLS-only peak. These are RTL heap readings,
not CircuitPython heap measurements; they do not include separate static pools.

### Measured results (2026-09-13)

Both follow-up gates passed. `dist/phase5-heap-console.txt` contains all 138
samples; `dist/phase5-heap-results.json` contains the checked summary and samples.
The instrumented images and their manifest are preserved under
`dist/phase5-heap-artifacts/`.

| Measurement | RTL free heap (bytes) |
| --- | ---: |
| Before first TLS connection | 82,464 |
| First TLS connection open | 36,128 |
| First TLS connection closed | 82,048 |
| Warm baseline after ten connections | 82,048 |
| After each of 100 measured HTTPS cycles | 82,048 |
| After each batch of three certificate rejections (30 total) | 82,048 |
| After HTTPS recovery and 130 seconds settling | 82,048 |
| Minimum-ever free heap observed | 29,984 |

The initial retained difference was 416 bytes. Post-warm-up loss was **zero
bytes**, including after certificate failures; the first-ten and last-ten
post-close medians were both 82,048 bytes. No progressive loss was observed
within this run. All socket-capacity and HTTPS recovery checks passed.

The cold-start production test fetched 765 HTTPS bytes after the user-confirmed
power removal. This was distinct from the earlier automated RTL reset tests.
The heap measurements used instrumentation, and do not claim to measure every
possible certificate chain, workload, static buffer or allocator.

All 57 original CircuitPython files match the pre-test backup, excluding only
generated `boot_out.txt`; see `dist/phase5-heap-file-preservation.json`.
The normal image was rebuilt and restored with `heap_diagnostics: false`;
its ELF contains no heap-log format string. Stock CircuitPython was restored
and verified HTTPS again fetched 765 bytes with full socket capacity.
Production restoration and its final HTTPS result are recorded in
`dist/phase5-post-heap-production-flash.log` and
`dist/phase5-post-heap-production-console.txt`; the installed manifest is
`dist/phase5-post-heap-production-manifest.json`. The nine native tests and
artifact checks pass; `git diff --check` is clean.

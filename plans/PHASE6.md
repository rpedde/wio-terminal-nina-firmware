# Phase 6: cleanup

**The Phase 6 regression gate passed on 2026-09-17**, including physical
power-cycle HTTPS after the user confirmed reconnection. Existing deferred
coverage and the interrupted diagnostic attempts are documented below.

## Changes

Removed 131 obsolete tracked files: the eRPC runtime/setup, RPC IDL and both
generated shim trees, BLE RPC, EasyLogger, mDNS RPC support, the unused Wi-Fi
RPC wrappers and types, and the shell/PowerShell generators and old Arduino
build scripts. Historical baseline records remain in `plans/` and Git.

The only retained `src/wifi/` file is the typed, verified TLS adapter
`wifi_ssl_client.c`. Its API is declared in `src/nina/nina_sockets.h`; the
obsolete `wifi_sslclient_context` header is gone. NINA calls the SDK directly
through its typed backends. The Docker build no longer adds include paths
for removed code. The linked map contains no eRPC/RPC, BLE-wrapper, EasyLogger
or mDNS source references.

The README now describes NINA compatibility, build/flash/CircuitPython
restoration, command coverage, limitations and troubleshooting. A standalone
stock-driver HTTPS example and credential template live in `examples/`.
Contribution and CI descriptions now reflect this project. Original Seeed
copyright is retained; `THIRD_PARTY.md` documents the TLS adapter, root data
and external toolchain. Apache-2.0 and MPL-2.0 license texts are included.

## Regression evidence (2026-09-17)

- Docker image builds with the pinned dependencies (`dist/phase6-image.log`).
- Clean offline firmware build passes (`dist/phase6-build.log`).
- All nine native tests pass, including ASan/UBSan protocol, transport,
  socket/TLS lifecycle and failure injection, loopback and workflow checks
  (`dist/phase6-tests.log`). Artifact names, sizes, hashes, ELF and ownership
  validation pass.
- The three images total 606,496 bytes, down 28,672 bytes from Phase 5.
  Linked SRAM totals 245,456 bytes, down 528 bytes; reserved heap remains
  208,896 bytes. These are link-time sizes, not runtime heap measurements.
- Flashed the production build with heap diagnostics disabled
  (`dist/phase6-flash.log`) and restored the same supplied stock CircuitPython
  10.3.0 UF2 used in prior phases. Tests use unmodified ESP32SPI 11.1.4 via
  raw REPL and do not replace `code.py` or credentials.
- Backed up 58 CircuitPython files to `dist/phase6-circuitpython-backup`;
  pre-test hashes are in `dist/phase6-files-before.json`.
- Wi-Fi scan, WPA2, addressing, metadata, DNS, ping, DNS failure and reconnect
  pass (`dist/phase6-network-console.txt`).

- Wi-Fi authentication failure, cancellation, static IPv4/reconnect and explicit
  DNS recovery pass (`dist/phase6-recovery-console.txt`).
- HTTP, UDP NTP, exhaustion/reuse and 100 TCP cycles pass, including disconnect
  with allocated slots (`dist/phase6-sockets-console.txt`).
- Trusted HTTPS, untrusted/mismatched/expired rejection, HTTPS recovery,
  100 TLS cycles and disconnect/reconnect pass (`dist/phase6-tls-console.txt`).
- Production transport regression passes: 100 version reads, 20 RTL resets
  and 10,000 mixed status/version requests in 38.376 seconds, without retries
  or errors (`dist/phase6-transport-console.txt`).

Runtime heap regression passed on a complete fresh run (details below).
Production restoration, the documented HTTPS example and the physical
cold-start check pass.
Separate open-AP testing and direct DHCP hostname-option inspection remain
unrun. Logic-analyzer capture remains deferred under the Phase 2 user decision.

## Diagnostic interruptions

The first diagnostic heap run completed 76 measured HTTPS cycles at a stable
82,816 free bytes, then `socket_open` failed on cycle 77. This incomplete run
is not counted as a pass (`dist/phase6-heap-console.txt`). A fresh initialization
subsequently failed to synchronize SNTP within 30 seconds
(`dist/phase6-heap-recovery-probe.txt`). HTTP worked but UDP to `pool.ntp.org`
timed out on the Wio (`dist/phase6-network-diagnostic.txt`). A host-side probe
also timed out against `pool.ntp.org`, while Google and Cloudflare NTP replied.
The Wio then passed the complete TCP/UDP gate against `time.cloudflare.com`,
including all 100 TCP cycles (`dist/phase6-udp-alternate.txt`). This supports
an endpoint/network explanation for the UDP failure; it does not establish
the cause of the earlier TLS connection failure. No firmware changes, retry
logic or weakened acceptance assertions were introduced in response.

## Completed heap regression

The unchanged test completed a fresh run on the same diagnostic firmware:
`dist/phase6-heap-retry-console.txt`, analyzed into
`dist/phase6-heap-results.json`. All 138 samples were recorded. Free heap was
83,232 bytes before TLS, 36,896 while the first TLS connection was open, and
82,816 after closing it. Warm-up, all 100 measured HTTPS cycles, all ten
batches of three certificate failures, HTTPS recovery and 130 seconds of
settling each returned to 82,816 bytes. Post-warm-up loss was **zero bytes**;
minimum-ever free heap was 30,752 bytes. The warm baseline and minimum-ever
reading are both 768 bytes above Phase 5. These readings cover this workload
and do not measure all possible chains, workloads or separate static pools.

The interrupted first attempt remains documented above; the successful run
contains no retry logic or relaxed assertions. Diagnostic artifacts are saved
in `dist/phase6-heap-artifacts/`. Production artifacts from the original tested
build are in `dist/phase6-production-artifacts/` and restored to `dist/` with
all image hashes verified against the saved manifest.

## Production restoration and file preservation

The saved, already-tested production images were reflashed successfully
(`dist/phase6-production-restore-flash.log`) and stock CircuitPython 10.3.0
restored. The new `examples/https.py` ran unchanged through REPL and returned
HTTP 200 over verified TLS (`dist/phase6-example-console.txt`). It was not
copied over the user's `code.py`. The final manifest has
`heap_diagnostics: false`; `dist/` contains the production artifacts.

All 57 original board files match the backup, excluding generated
`boot_out.txt` (`dist/phase6-file-preservation.json`). No credentials or
application files were changed. `git diff --check` and local documentation
link checks pass. The software and hardware regression gate is complete
within the previously accepted coverage and deferrals.

## Physical cold-start acceptance

On 2026-09-17 the user confirmed reconnection after the requested removal of
all Wio power for at least ten seconds. The restored production firmware
synchronized SNTP and fetched 765 HTTPS bytes from `sha256.badssl.com` with
certificate verification. Four-slot capacity/exhaustion/reuse checks passed
before and after the request. See `dist/phase6-power-cycle-console.txt`.
Test pins were subsequently checked and released via REPL
(`dist/phase6-release-pins-console.txt`). This completes the Phase 6 gate;
Phase 7 release packaging and tagging remain separate work.

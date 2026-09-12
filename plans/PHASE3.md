# Phase 3: Protocol and Wi-Fi control

Status: **phase 3's stock-driver scan/connect/addressing/DNS/ping gate passed
on 2026-09-12 using the configured WPA2 AP**. Native/container checks and
the transport regression pass. Separate open-AP testing and direct DHCP
hostname-option inspection remain unrun; they are not claimed as passed.

## Implementation

- Portable bounded parser and serializer, no Realtek/FreeRTOS dependencies.
  Requests must fit 4092 bytes and the exact aligned command envelope.
  Responses zero-pad to four bytes, retaining the unpadded length required
  by the transport. Data response `0x45` supports the full 4084-byte payload.
- Dispatch for `0x10`, `0x11`, `0x14`–`0x16`, `0x20`–`0x27`, `0x30`,
  `0x32`–`0x37`, `0x3c`–`0x3e`. Other commands return the standard error frame.
  The parser also validates the 16-bit envelopes of `0x44`–`0x46`; their
  socket backends remain deferred and dispatch returns an error.
- Typed Realtek station backend with an asynchronous connection/disconnect
  worker. No RPC wrappers, Serial2, BLE, or eRPC objects are started.
- Maintained connection state, DHCP/static IPv4, configurable country,
  hostname, DNS servers, MAC/current AP information, scan, DNS, and ICMP.
- Scan callbacks accumulate at most 60 deduplicated BSSIDs with bounded,
  NUL-terminated SSIDs. A mutex protects completed snapshot replacement.
  The server freezes the generation returned by `0x27` for indexed reads.
- Direct lwIP configuration and DNS operations run on the TCP/IP thread.
  DNS response waits are bounded to four seconds, with persistent callback
  storage surviving timeouts; failures clear the cached result. ICMP uses
  a private raw socket, validates source/identifier/sequence/payload, applies
  TTL, and closes the descriptor on every exit.
- Protocol dispatch now runs outside the transport critical section. CS
  events during processing invalidate the response without reusing its
  buffers. Response timeout starts when TX is armed, after processing.

## Compatibility findings

The development oracles are the ESP32SPI 11.1.4 source and adjacent
`../nina-fw` 3.3.0 (`586ef2a`), `main/CommandHandler.cpp`.

ESP32SPI's `_send_command` leaves reused-buffer bytes in request padding.
Only the alignment tail is ignored; the parser still validates the trailer
position and total frame length. IP/MAC and current AP getters carry one
dummy byte. Version replies include their NUL. RSSI is signed little-endian
32-bit; ping is little-endian 16-bit. The legacy MAC property is reversed
so the driver's `MAC_address_actual`/`mac_address` properties give the actual
hardware address.

The pinned SDK's precompiled DHCP client has `LWIP_NETIF_HOSTNAME=0`.
`nina_dhcp_rtl8720.c` wraps its two outgoing UDP functions at link time and
adds DHCP option 12 to client packets. It preserves the vendor netif ABI and
DHCP implementation. The packet transformation is separately host-tested.
The build modifies only the disposable SDK copy to add the linker flags.

## Software verification

- `./fw image`, `./fw build`, and `./fw test` passed.
- Six tests include 23 stock-driver golden vectors, all response-padding
  remainders, zero-length and maximum-length parameters, invalid envelopes,
  socket IDs, endian conversions, mock backend failures, scan generations,
  DHCP options, 30,000 random fuzz inputs, and exhaustive one-byte mutations
  of golden requests. ASan/UBSan/leak checks enabled.
- Transport coverage includes 10,000 status requests, 20,000 malformed
  frames, fault recovery, a 4.5-second responder, and CS during processing.
- Three-image artifact checks, checksums, ELF/map/size extraction and file
  ownership checks passed. New NINA files build without compiler warnings.
- `git diff --check` passed. Existing legacy-code warnings remain outside
  the phase 3 implementation; legacy removal is phase 6.

Golden fixtures are checked in at `tests/protocol_vectors.h`. Regenerate
with `tools/generate-protocol-vectors.py /path/to/adafruit_esp32spi.py`.
It executes the actual parsed `_send_command` method from the pinned driver
with a capture bus. No driver checkout is needed for normal tests/builds.

## Hardware procedure and evidence

Use the supplied stock Wio CircuitPython 10.3.0 UF2 and ESP32SPI 11.1.4.
The Wio's original files, including the user's new settings, are backed up
in ignored `dist/phase3-circuitpython-backup/`; never publish credentials.

1. Build, flash the RTL, and restore the supplied CircuitPython UF2.
2. Put local values in `CIRCUITPY/settings.toml` using
   `tests/hardware/settings.toml.example`. Do not print its password.
   `DNS_TEST_HOST` is a hostname to resolve, not a resolver address.
3. Run `tests/hardware/phase3_wifi.py` through the raw REPL or as `code.py`.
   It checks scan metadata, connect/reconnect, addressing, current AP data,
   DNS success/failure/cache clearing, and gateway ping. An optional
   `OPEN_WIFI_SSID` enables the separate open-network check.
4. Run `tests/hardware/phase3_recovery.py` for wrong-password/no-SSID status,
   asynchronous acknowledgements, cancellation, static IPv4/reconnect, and
   explicit DNS. It reuses the client's DHCP-assigned IP for the static
   test, uses 1.1.1.1/1.0.0.1 for the explicit resolver test, and resets to
   the default DHCP configuration at the end.
5. Repeat the phase 2 transport test on the networking image.

Initial flash attempts (`dist/phase3-flash.log` and
`dist/phase3-flash-retry.log`) installed the SAMD bridge but failed to enter
RTL UART download mode. This reproduces the phase 2 flashing issue, not a
network runtime result. Immediate reset-hold recovery also failed
(`dist/phase3-flash-reset-held.log`). The user's physical power cycle then
allowed all three images to flash (`dist/phase3-flash-powercycle.log`).
The supplied stock CircuitPython UF2 was restored afterward.

### Results on the flashed image

- `dist/phase3-network-console.txt`: 100 version reads; scan of eight unique
  APs with indexed metadata; WPA2 connection in 3.614 seconds and reconnect
  in 2.135 seconds; DHCP address/mask/gateway; MAC, SSID, BSSID, RSSI and
  security; DNS resolution; gateway ICMP responses (7 and 21 ms); DNS
  failure deadline and cleared result cache. No password was logged.
- `dist/phase3-recovery-console.txt`: sub-second connection acknowledgement,
  missing-SSID status, wrong-password status, cancellation with protection
  against late completion, static IPv4 and static reconnect, explicit DNS
  configuration and lookup. Ends with RTL reset and idle/default DHCP.
- `dist/phase3-transport-console.txt`: 100/100 version reads, 20/20 resets
  followed by idle status, 10,000/10,000 mixed status/version requests,
  no retries or malformed responses, 38.390 seconds total. The old test's
  printed analyzer-gate wording was stale and has been corrected; the
  existing user waiver remains authoritative.
- `dist/phase3-edges-console.txt`: 50 ms pauses inside both request and
  response transactions pass at the driver's configured 8 MHz.
- Installed artifact manifest: `dist/phase3-acceptance-build.json`.
  Images total 594,208 bytes; linked SRAM sections total 208,800 bytes;
  reserved heap is 208,896 bytes. These are link-time sizes, not measured
  runtime heap usage.

The SAMD is restored to stock CircuitPython. Its existing `code.py` was not
replaced; the phase 3 scripts ran through raw REPL. User settings remain
on `CIRCUITPY` and a private backup is in the ignored directory above.

## Coverage checklist

- [x] Flash phase 3 firmware and restore CircuitPython.
- [x] Stock-driver scan, WPA2, DHCP/addressing, DNS and ping.
- [ ] Open-network connection.
- [x] Transport regression on the networking image.
- [x] Hardware static IP/reconnect and explicit DNS.
- [x] Hardware no-SSID, wrong-password, cancellation and recovery.
- [ ] Direct inspection of DHCP hostname option on the network/server.

The configured AP was WPA2. `OPEN_WIFI_SSID` was unset, so the hardware
script explicitly reported the open-network check as not run. WPA-only
AP interoperability and externally induced link loss have not been
hardware-tested either. Preserve these gaps for the broader network
acceptance suite; passing WPA2 tests does not establish those behaviors.

TCP/UDP socket commands and SNTP/TLS are future phases. Logic-analyzer
capture remains deferred per `PHASE2.md`; no electrical trace is claimed.

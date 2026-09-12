# Phase 2: SPI proof

Status: **phase 2 functional acceptance passed; electrical timing capture
deferred by user decision on 2026-09-12**. Implementation is complete and
flashed; all automated hardware tests pass on stock CircuitPython configured
for 8 MHz. Phase 3 may proceed.
No networking commands are included in this phase.

## Acceptance decision (2026-09-12)

The user accepted the passing real-device CircuitPython tests as sufficient
to proceed and waived logic-analyzer capture as a blocking phase 2 gate.
The internal SAMD-to-RTL SPI signals are not exposed on the rear 40-pin
header; probing would require opening the case and potentially soldering
temporary wires. The rear header's external SPI bus is a separate connection.

The tests establish successful stock-driver communication, response integrity,
reset recovery and tolerance of 50 ms pauses within transactions. They do not
directly measure actual clock frequency, precise READY/CS/clock edge ordering,
glitches or timing margins. No analyzer trace exists, and electrical capture
must not be reported as passed.

Revisit logic-analyzer capture if we see intermittent corruption, shifted or
malformed replies, unexplained READY/response timeouts, reset-dependent
failures, or behavior that changes with SPI speed or transaction pauses.
Also reconsider capture if stronger reliability requirements warrant direct
timing measurements. Preserve the preparation below for that investigation;
capture is not the next required development step.

## Deferred logic-analyzer procedure and saved checkpoint

Checkpoint saved on 2026-09-12 after the user paused work to clear context.
**Do not rebuild or reflash merely to resume capture:** the tested firmware is
already installed. No capture or test process remains running. The proposed
capture command was interrupted before it created its script or output;
there is **no saved analyzer trace** from this session.

Device state at the checkpoint (recheck before a future capture):

- RTL: logging-disabled USI SPI proof, version `3.3.0+rtl8720.1`.
- SAMD: stock CircuitPython 10.3.0, ESP32SPI 11.1.4 and BusDevice 5.2.17.
- CircuitPython console was `/dev/ttyACM0` (data CDC `/dev/ttyACM1`). The last
  raw REPL test finished and deinitialized its pins. Check enumeration again.
- `CIRCUITPY/code.py` contains the stock-driver transport test. Original files
  are backed up in `dist/phase2-circuitpython-backup/`.
- Installed image/source hashes: `dist/phase2-acceptance-build.json`.
  Its application image `km0_km4_image2.bin` SHA-256 is
  `fd0c06de044f8fbd3b2b17219adf0e58faa5b0579983aab844407650874cd826`.
- Existing phase 1 and phase 2 changes remain uncommitted; preserve them.

Analyzer preparation already completed:

- USB `0925:3881` was identified as Saleae Logic, eight channels D0–D7.
- Built isolated Docker image `seeed-phase2-sigrok:local` with sigrok-cli
  0.7.2 and `sigrok-firmware-fx2lafw`. No host package installation was needed.
  Build log: `dist/phase2-sigrok-build.log`. Docker image ID:
  `sha256:448efa5f6693a459bb9c8ab6ff4cfe9bdf62ef33145ef8faf50b1aa5c2f9d174`.
- Sigrok successfully initialized the analyzer with temporary fx2lafw runtime
  firmware. USB connection changed from `3.12` to **`3.90`**, last device path
  `/dev/bus/usb/003/090`. These numbers are transient; rediscover after unplug.
- `--show` reported rates through 48 MHz. The planned initial capture was
  24 MHz, all eight channels, four seconds, binary output. At 24 MHz each
  sample is 41.7 ns; report timing precision and inspect for capture loss.
- **Probe wiring/channel mapping and common ground have not been confirmed.**
  Ask for the actual connections; USB detection alone does not establish them.

Session helpers were preserved beyond `/tmp` in
`dist/phase2-session-tools/` (ignored local evidence, not committed sources):

- `phase2-gate-runner.py`: runs a supplied CircuitPython script through raw
  REPL, streams/saves output, detects tracebacks, and allows up to 600 seconds.
- `phase2-repl.py`, `phase2-spi-debug.py`: shorter dedicated-UART diagnostic.
- `phase2-hold-reset.py`: holds RTL reset low before flashing if needed.
- `sigrok/Dockerfile`: reproduces the separate capture-tool container.

Procedure if electrical capture is revisited:

1. Read this checkpoint and confirm analyzer wiring to GND, CLK, CS, READY,
   and preferably MOSI/MISO. RTL pin mapping is below; do not assume these
   internal bus signals are on the external header.
2. Inspect `lsusb` and rediscover the analyzer with sigrok `--show`. USB and
   Docker access require execution outside the filesystem sandbox. Once its
   runtime firmware is loaded, expose only its current device node:

   ```sh
   docker run --rm --device /dev/bus/usb/003/090 seeed-phase2-sigrok:local --driver fx2lafw:conn=3.90 --show
   ```

   Substitute the actual bus/device. After a cold analyzer reconnect, sigrok
   may need USB bus access through re-enumeration to load runtime firmware.
3. Start capture and the serial test together so the capture includes reset
   startup and active requests. The interrupted orchestration script was
   never created; prepare it again. Capture stdout as binary and stderr in a
   separate log. Example sigrok arguments after device discovery:

   ```sh
   --driver fx2lafw:conn=3.90 --config samplerate=24m --time 4000 --output-format binary
   ```

   Trigger the stock-driver test using the preserved helper:

   ```sh
   python3 dist/phase2-session-tools/phase2-gate-runner.py tests/hardware/phase2_transport.py dist/phase2-capture-console.txt
   ```

   The separate pause test is `tests/hardware/phase2_edges.py`. Keep the
   original passing console files intact when collecting new evidence.
4. Decode mode 0, MSB first, eight bits. Verify CLK rate, READY low before CS
   falls, READY high after CS falls and before the first clock, READY high
   throughout the transaction/processing, and rearm before READY falls.
   Verify reset startup and distinguish request/response transactions.
5. Save the raw trace, channel mapping, sample rate, tool versions, firmware
   hashes and measured timing results. If the trace passes, check the deferred
   checklist item and record electrical validation here. If it fails, fix the
   transport issue and repeat the affected validation.

## Findings (2026-09-11)

- Phase 1's recorded build and flash gate passed. Existing uncommitted phase 1
  workflow files are being preserved.
- The [Wio v1.2 schematic](https://files.seeedstudio.com/wiki/Wio-Terminal/res/Wio-Terminal-SCH-v1.2.pdf),
  sheet 6, contradicts TODO.md's original RTL pin mapping. The fitted 0-ohm
  links connect CLK to PA30, MOSI to PA25, MISO to PA26, CS to PA28.
  IRQ0/READY is PA12 and SYNC is PA13. PB13/14/16/17 are unconnected.
  PA30 also requires disabling the SWD pinmux.
- Pinned Seeed core `f81bca75e433e35b2d27cadd178596888940fb5a` documents
  SPI slave mode, 64-entry FIFOs, RX/TX DMA and both-edge GPIO interrupts.
  Subsequent testing identified USI0 as the connected peripheral; idle-gap termination is
  unsuitable because the host pauses between reads while CS remains asserted.
- [ESP32SPI 11.1.4 source](https://github.com/adafruit/Adafruit_CircuitPython_ESP32SPI/blob/11.1.4/adafruit_esp32spi/adafruit_esp32spi.py)
  sends four-byte zero-parameter requests for status/version, selects 8 MHz,
  waits for READY high after CS assertion, and reads replies only through
  END_CMD. It does **not** clock response padding. Error responses may be
  deselected after the first `0xEF` byte.
- Adjacent NINA reference `main/CommandHandler.cpp` returns the version's
  trailing NUL (`sizeof(FIRMWARE_VERSION)`) and one status byte.

## Work checklist

- [x] Transport state machine, buffers, USI/GDMA backend and recovery.
- [x] Fixed status/version proof responder and SPI-only sketch startup.
- [x] Native fault/recovery tests under ASan/UBSan.
- [x] Clean Docker build, artifacts and source cleanliness checks.
- [x] Flash the phase 2 proof images onto the RTL.
- [x] Stock CircuitPython hardware test script and instructions.
- [x] 100 consecutive version reads at 8 MHz.
- [x] 20 RTL resets followed by successful status reads.
- [x] 10,000 mixed version/status requests without errors.
- [x] 50 ms pauses inside both request and response while CS stays low.
- [ ] Deferred, nonblocking: logic-analyzer capture proving READY/CS ordering
  and clock rate (user waiver, 2026-09-12; not performed).

Phase 2 functional acceptance is complete based on the real-device tests above.
The user waiver supersedes the original requirement for a trace before phase 3;
electrical timing remains unverified by direct measurement.

## Implementation and software validation

`src/nina/nina_transport.c` is the portable transport state engine. The RTL
backend uses **USI0 in SPI slave mode**, two reserved DMA channels and two
32-byte-aligned buffers with 4092 usable bytes in 4096-byte allocations.
Whole-cache-line storage prevents cache maintenance from touching unrelated
state. RX is cleaned/invalidated before DMA and invalidated afterward; TX is
cleaned before DMA reads it.

Both CS edges are observed with PA28 kept in GPIO mode. Hardware tests show
USI receives/transmits on the connected bus in this configuration. CLK has a
pull-down matching mode 0; changing the previous pull-up removed the observed one-bit shift. A spurious
clock was the working explanation and still needs electrical confirmation. The USI peripheral is reset between frames. TX preloads its
64-entry FIFO before lowering READY; DMA supplies the remainder.

At CS release, RX DMA is suspended and its FIFO drained with a bounded poll.
The destination address is saved before the vendor disable helper clears it,
then remaining USI FIFO bytes are drained. Byte-wide transfers avoid packed
word tails. RX accounts for response clocks too: TX DMA completion only means
data entered the transmit FIFO. The task handles replies and rearming; the
ISR performs no parsing or networking. A two-second timeout covers active CS
and abandoned responses. Idle RX waits indefinitely. Recovery waits for CS
release before advertising readiness again.

`nina_spi_proof.c` accepts only the four-byte zero-parameter commands `0x20`
and `0x37`. The status is fixed `WL_IDLE_STATUS` (0), not live Wi-Fi state.
The version response has 16 parameter bytes including its NUL, 21 bytes
through END_CMD, and 24 bytes including padding. Status is six bytes through
END_CMD, eight including padding. Other commands or malformed requests return
`EF 00 EE 00`. This is not the phase 3 general parser.

The sketch starts only the transport proof task and leaves `loop()` sleeping.
It does not initialize Serial2, eRPC, BLE, Wi-Fi workers, or EasyLogger.
Legacy files remain for later phases. Vendor ROM/core boot output uses the
dedicated LOG UART. Counters can be read with `nina_transport_get_counters()`;
the proof deliberately adds no debug wire command or UART output.

`./fw test` passed all five tests in Docker, including the production state
engine under ASan/UBSan with leak detection. Coverage includes literal replies,
unaligned response reads, error-byte early deselection, malformed and unknown
commands, zero/oversized transfers, FIFO faults, unexpected CS, stuck CS,
abandoned TX, short/long reads, arm failure, timestamp wraparound, 10,000 valid
transactions and 20,000 deterministic random malformed transactions.
Host-sandbox LeakSanitizer cannot inspect processes under ptrace; the complete
sanitizer run succeeded in Docker without disabling leak detection.

## Hardware procedure

1. Run `./fw image`, `./fw test`, `./fw build`, then `./fw test` to validate
   the exported images as well. Confirm the manifest identifies the NINA SPI
   proof; baseline images are not suitable.
2. Flash using `./fw flash-rtl --port /dev/ttyACM0` (use the actual explicit
   device). This replaces the RTL application and may install Seeed's SAMD
   bridge. No erase is needed.
3. Enter the Wio UF2 bootloader and restore an explicitly chosen stock Wio
   CircuitPython UF2 using `./fw install-circuitpython --mount PATH --uf2 FILE`.
4. Install released ESP32SPI **11.1.4** and its dependencies into `CIRCUITPY/lib`.
   Copy `tests/hardware/phase2_transport.py` to `CIRCUITPY/code.py`. Preserve
   any existing application first. Record console output with the build
   manifest and firmware hashes. Do not modify the driver or retry failures.
5. Optional deferred electrical validation: connect the analyzer to GND, CLK,
   CS and READY (and preferably MOSI/MISO).
   Use 3.3 V inputs, SPI mode 0, MSB first, eight bits; sample fast enough to
   resolve the 8 MHz clock. Verify each exchange: READY low before CS falls,
   READY high after CS falls and **before the first clock**, READY stays high
   during transfer/processing, and goes low only after the next DMA arm.
   Preserve the capture and note channel mapping/sample rate. Capture reset
   startup too. The script's PASS messages do not replace trace inspection.

Host inspection outside the sandbox found Wio `2886:802d` at `/dev/ttyACM0`
and Saleae Logic `0925:3881`. This proves USB presence, not probe wiring or
SPI behavior. CircuitPython UF2 selection was supplied on 2026-09-12; analyzer wiring
remains to be confirmed before electrical capture can be recorded as passed.

## Build and flash results (2026-09-11)

- Rebuilt the pinned Docker image with phase 2 manifest metadata. Core,
  compiler, OS and download pins were not changed.
- Clean offline firmware build passed. No warnings were reported for the new
  NINA source; existing legacy-source warnings remain during compilation.
- All three RTL images, ELF, map, size reports, manifest, checksum and ownership
  checks passed. `./fw test` passed again against the completed build.
- Total flash-image bytes: **188,704**. SRAM image sections: **39,664** bytes.
  Separately reserved heap: **208,896** bytes. These are linker measurements,
  not runtime heap or stack measurements.
- `git diff --check` passed. Source hashes remained unchanged during final
  artifact testing/flashing. Builds use the existing read-only source mount
  and a disposable container workspace.
- Executed `./fw flash-rtl --port /dev/ttyACM0`. Tool exit code was 0, with
  `Verify successful` for the SAMD bridge and `All images are sent successfully!`,
  `Image tool closed!`, and `Success!` for RTL flashing.
- Evidence and the exact firmware hashes are recorded in
  `dist/phase2-flash-result.json` and `dist/build-manifest.json`.

At the end of the 2026-09-11 work, the SAMD contained Seeed's temporary
bridge and no CircuitPython UF2 had been supplied. See the 2026-09-12 update
below for the subsequent installation, debugging and acceptance results. At
that initial checkpoint, none of the runtime transport gates had passed. Compilation, simulation and successful flashing do not establish
8 MHz electrical behavior, simultaneous GPIO/CS observation, or DMA tail
accounting on real silicon. These were the next required measurements at that
initial checkpoint; the current remaining work is listed at the top.

## Completion record

Functional acceptance passed; see the recorded results below. On 2026-09-12,
the user waived the blocking analyzer gate and authorized proceeding to phase 3.
The analyzer trace, channel mapping and measured clock/READY ordering remain
deferred, not passed. See “Acceptance decision” for limitations and symptoms
that should prompt electrical investigation.

## Earlier hardware diagnosis (2026-09-12; resolved below)

Installed the user-selected stock CircuitPython 10.3.0 UF2 from
`/home/rpedde/Downloads/adafruit-circuitpython-seeeduino_wio_terminal-en_US-10.3.0.uf2`.
SHA-256: `09ceed220dba71e142e74b028224cc4b7684e04f33e3ae235916c86d3c464c95`.
The board reported CircuitPython 10.3.0, build `seeeduino_wio_terminal`.
The previous CIRCUITPY contents were preserved in
`dist/phase2-circuitpython-backup/` before installing the test application.
Unmodified release libraries: ESP32SPI 11.1.4 and BusDevice 5.2.17 (Python
release ZIPs). The test confirms the ESP32SPI version at runtime.

The first version request failed waiting for READY; the failure is retained
in `dist/phase2-console-initial-failure.txt`. Dedicated RTL boot logging is
available with `busio.UART(board.RTL_RXD, board.RTL_TXD, baudrate=115200)`;
the aliases must be reversed relative to the previous application.

Diagnostic images reached the transport task. GPIO reads of PA28 report zero
when the pad is muxed to SPIS, even with host CS high. An intermediate backend was
changed to observe falling CS in GPIO mode, mux to hardware CS before raising
READY, and use the SSI select-rise interrupt for completion. This corrected idle
CS and READY, but did not yet produce a valid command/response exchange.
The next diagnostic found the DMA destination register remained zero; explicit
GDMA0 clock enable was added for further hardware validation.

Temporary diagnostic images log only to the dedicated RTL LOG UART. They are
not production acceptance builds. Current diagnostic captures are in
`dist/phase2-diagnostic-console.txt`. At that intermediate stage no runtime
gate had passed. The later acceptance results supersede those failures.

### Continued hardware diagnosis

The user's physical power cycle allowed the logging-disabled GDMA-clock fix
to flash successfully. Restored CircuitPython still timed out waiting for a
version response and received zero bytes instead of the response marker.
A subsequent diagnostic flash again failed to enter UART download mode.
Holding `board.RTL_PWR` low from CircuitPython before installing the Seeed
bridge then allowed a diagnostic flash without another physical power cycle.
This is a tested recovery procedure, not yet an automated workflow guarantee.

The diagnostic with explicit GDMA0 clock enable reports `CTRLR0=7`, pinmux
values `5d03,5e03,5e03,5d03` (CLK, MOSI, MISO, CS), `SR=1`, `TXFLR=0x40`,
raw SSI interrupt status zero and a zero DMA destination address at stop.
Thus the clock enable alone did not solve the transfer failure. Further
instrumentation compares the DMA address before and after channel shutdown.
That intermediate development image enabled dedicated UART diagnostics.
The acceptance build described below disables them.

CircuitPython installation metadata and release archive hashes are in
`dist/phase2-circuitpython-install.json`. Flash transcripts and diagnostics
are under `dist/`; `phase2-flash-result.json` records the original 2026-09-11
images, not later diagnostic builds. The build manifest always describes the
latest exported build, which must not be assumed to be the installed image.

### Peripheral selection correction

Seeed's [Intercom.c](https://github.com/Seeed-Studio/seeed-ambd-sdk/blob/d953d6473f1d7612d88b0aaeefadb48ee2e72e5c/component/common/example/spi_atcmd/Intercom.c)
and its USI SPI slave DMA example identify the connected PA25/26/30/28 pins
as **USI0 SPI**, not SPI0. TODO.md's peripheral name is corrected. The initial
SPI0 backend could not receive clocks on these pins. The backend now uses
USI SSI, its separate RX/TX FIFO registers and USI0 DMA handshake IDs.
Hardware diagnostics confirm four bytes reach RX DMA with this correction.

The zero destination address was a second, independent issue: it was valid
immediately before `GDMA_Cmd(..., DISABLE)` and zero immediately afterward.
Receive accounting must preserve the address before the HAL clears it; it
must also quiesce outstanding DMA before relying on that value.

USI activity flags did not prove a reliable substitute for CS edges. The
working backend keeps PA28 in GPIO mode and resets USI between transactions.
Hardware tests below validate this approach, including long clock pauses.
This revises the original hardware-CS pinmux plan; GPIO edges delimit frames.

### First valid stock-driver response

With USI0, GPIO CS edges, cache maintenance and CLK pull-down, the unmodified
ESP32SPI 11.1.4 driver read `3.3.0+rtl8720.1` at 8 MHz. The request was exactly
`E0 37 00 EE`. Bounded DMA suspend/drain corrected a final in-flight byte race:
request count is four and version response count is 21, including END_CMD.

Diagnostic logging is now disabled by default. The acceptance build and all
five Docker tests (including sanitizers and artifact verification) passed.
The subsequent complete stock-driver run and 50 ms intra-transaction pause
test both passed, as recorded below.

## Acceptance results (2026-09-12)

Logging-disabled firmware, stock CircuitPython 10.3.0, unmodified released
ESP32SPI 11.1.4, default 8 MHz:

- **PASS:** 100 consecutive exact firmware version reads.
- **PASS:** 20 RTL hardware resets, each followed by status `WL_IDLE_STATUS`.
- **PASS:** 10,000 alternating version/status requests without retries.
- Full stock-driver run: **38.393 seconds**.
- **PASS:** independent raw SPI test holds CS low during a 50 ms request pause
  and a 50 ms response pause. READY stays high and the exact reply survives.
- **PASS:** all five Docker tests, including ASan/UBSan/leak checking and
  artifact size/checksum/ELF/ownership checks. `git diff --check` passed.

Evidence: `dist/phase2-acceptance-console.txt`,
`dist/phase2-edges-console.txt`, `dist/phase2-acceptance-flash.log`, and
`dist/phase2-acceptance-build.json` (installed image and source hashes).
The SAMD is restored to the supplied CircuitPython UF2; its original files
remain backed up. The RTL contains the logging-disabled USI acceptance build.

**Deferred electrical validation:** a logic-analyzer trace showing reset
startup, READY/CS ordering and measured CLK frequency has not been collected.
Automated SPI success and the pause test support functional acceptance but do
not replace direct timing measurements. The user waived this as a blocking
gate on 2026-09-12; phase 3 may proceed. Revisit capture for the timing symptoms
listed in “Acceptance decision.”

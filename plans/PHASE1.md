# Phase 1 baseline record

Scope: reproduce the existing v2.1.3 eRPC firmware in Docker. Stop before
Phase 2; no NINA transport, networking, cleanup, or release work is included.

## Verified on 2026-09-11

- Source baseline: `7334817a6fcf311fbe3c9a8c5644b805ae12bdab` (`v2.1.3`).
- Sketch and `src/` match that baseline without modifications.
- Ubuntu amd64 image digest, Arduino CLI, Realtek compiler/tools, Arduino
  builtin tools, Seeed core and flasher archives are locked in
  `tools/toolchain.lock.json`.
- Core proven by compilation: `f81bca75e433e35b2d27cadd178596888940fb5a`,
  installed under the AmebaD `3.0.5` package path.
- Compiler: Realtek ASDK-6.5.0 Build 3292, GCC 6.5.0.
- Docker image build succeeded, including a build with `--no-cache`.
- Clean, offline firmware build succeeded as the invoking UID/GID.
- All three RTL images exported; ELF, linker map, size reports, manifest and
  image checksums validated. Generated artifacts are ignored under `dist/`.
- Four workflow tests passed both on the host and in the container.
- `git diff --check` passed. Firmware source comparison against v2.1.3 passed.

Measured image size: **885,024 bytes**, 42% of the 2,097,152-byte flash limit.
SRAM image sections occupy **212,128 bytes**, with **208,896 bytes** separately
reserved for heap. These are linker section sizes, not runtime free-heap
measurements. Existing compiler warnings remain in the unmodified baseline.

## Hardware flash gate passed on 2026-09-11

Host USB inspection outside the sandbox detected the Wio bootloader as
`2886:002d` at `/dev/ttyACM0`. Earlier sandbox-only checks did not expose the
device and were not evidence that it was physically absent.

Executed:

```sh
./fw flash-rtl --port /dev/ttyACM0
```

The Seeed tool wrote and verified the 63,104-byte SAMD bridge (`Verify
successful`), then reported `All images are sent successfully!`, `Image tool
closed!`, and `Success!`, exiting with code 0. The board re-enumerated as
`2886:802d` on the same port. Only `/dev/ttyACM0` was exposed to the container;
no privileged container or erase command was used. Artifact hashes and the
result are recorded in `dist/phase1-flash-result.json`.

The specified Phase 1 gate (clean Docker build and successful RTL flash) has
passed. An eRPC runtime response test has not been performed; successful
flashing alone does not prove application behavior. CircuitPython has not
been restored: the SAMD51 currently contains the temporary bridge. Restore
it using the README procedure and an explicitly supplied UF2 when ready.
Do not claim NINA/ESP32SPI compatibility for these baseline images.

Do not start Phase 2 as part of this change.

# Phase 7: Release artifact

The project is now named `wio-terminal-nina-firmware`, including its Arduino
sketch, disposable build directory and Docker toolchain image. Empty RPC,
BLE, EasyLogger and mDNS directories and inherited unused community templates
were removed. Required Seeed/Realtek toolchain dependencies and attribution
remain; earlier phase reports retain their historical names and evidence.

`./fw package v0.1.0` creates a deterministic versioned archive with the three
RTL images, build manifest, SHA-256 checksums, release notes, supported-command
matrix, matching CircuitPython HTTPS/settings examples, size reports,
ELF/map/build log, installation instructions and license notices. It requires
a clean source tree and build manifest matching the release tag, default US
country and disabled heap diagnostics. The changelog entry is `Initial release`.

GitHub branch/PR CI builds the pinned Docker image, runs native ASan/UBSan
tests, builds firmware, validates artifacts and checks source isolation. Tag
CI reuses the same build, verifies provenance, and publishes the archive and
checksum using the corresponding changelog section. Existing releases are
never overwritten. See [RELEASING.md](../RELEASING.md) for the workflow.

Validation includes a clean disposable firmware compile, native suite and
release tests for wrong revisions, dirty/diagnostic builds, damaged or extra
images, changelog extraction and reproducible archives. Firmware behavior is
unchanged; hardware was not reflashed for this packaging phase. Phase 6
hardware evidence and existing coverage deferrals continue to apply.

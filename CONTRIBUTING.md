# Contributing

Keep changes within the compatibility contract in [SUPPORTED_COMMANDS.md](SUPPORTED_COMMANDS.md).
Use the pinned Docker toolchain; do not silently update the SDK or roots.
Describe the concrete behavior change and validation in each contribution.

Run `./fw image`, `./fw build`, `./fw test` and `git diff --check`.
Builds use a read-only source mount; generated artifacts belong in `dist/`.
Protocol and backend changes need relevant native coverage and hardware
regression using stock CircuitPython and unmodified ESP32SPI 11.1.4.
Record actual results and unrun gates in the phase documents. Never commit
Wi-Fi credentials or board settings. Preserve component license notices.

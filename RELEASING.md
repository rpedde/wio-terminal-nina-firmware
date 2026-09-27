# GitHub releases

Project versions start at **v0.1.0**. The NINA wire compatibility string
`3.3.0+rtl8720.1` is independent of the project release version. Historical
Seeed tags describe the predecessor firmware, not releases of this project.

1. Add exactly one `## [X.Y.Z] - YYYY-MM-DD` section to `CHANGELOG.md`.
   Its body becomes the GitHub release notes without generated commit lists.
2. Commit the changes, then run `./fw image`, `./fw test`, and `./fw build`.
   Release builds must be clean, use country US and disable heap diagnostics.
3. Create an annotated tag: `git tag -a vX.Y.Z -m 'Release vX.Y.Z'`.
4. Run `./fw package vX.Y.Z` and inspect `dist/release/`. Packaging checks
   the tag, clean tree, manifest revision, production settings and image hashes.
5. Push the branch and tag to GitHub. The Release workflow runs the reusable
   Firmware workflow, packages its checked artifacts and publishes the archive
   and its SHA-256 sidecar with the exact changelog entry.

The archive includes three RTL images, the original build manifest, flash/RAM
size reports, checksums for every payload file, release notes, installation
instructions, command matrix, matching CircuitPython example and licenses.
ELF/map/build logs are included and also available in the workflow's
`firmware-build` artifact.
Archives are deterministic for identical inputs and source commit timestamps.
No credentials or local hardware logs are packaged.

Branch pushes and pull requests build firmware and run the native ASan/UBSan
suite, artifact checks and source-isolation guard. GitHub-hosted runners do
not run hardware tests; coverage deferrals are in SUPPORTED_COMMANDS.md.

A failed release can be rerun in Actions, or the Release workflow can be
manually dispatched against the existing tag. An existing published release
is never overwritten; publishing fails if the tag already has a release.

# Built-in server authentication roots

`roots.pem` is a deliberately small Mozilla-derived server-auth bundle for the
Wio's constrained heap. It contains ISRG Root X1, DigiCert Global Root G2,
GlobalSign Root CA R3 and Amazon Root CA 1. Hosts chaining only to other roots
are rejected. It is not the complete Firefox trust store, and does not reproduce
Firefox's additional root constraints or revocation services.

Source: [curl's Mozilla CA extract](https://curl.se/docs/caextract.html), dated
2026-08-13. The source and its independent published SHA-256 were checked when
creating this bundle. The converted Mozilla certificate data is licensed under
[MPL 2.0](https://www.mozilla.org/MPL/2.0/).

`metadata.json` records the dated input URL, input hash, output hash, root names,
and exact generation command. Download that dated input explicitly, verify its
published hash, then run the recorded command with the local input filename in
place of `INPUT.pem`. The update tool validates the input hash, selects roots in
a fixed order, and writes both PEM and the embedded C header. To change the
selection, edit `NAMES` in the tool and review the resulting roots and metadata.

Normal builds only check local hashes and header consistency; they never
fetch certificates. After any update run `./fw build`, review the size report,
and run `./fw test` plus `tests/hardware/phase5_tls.py` on hardware.

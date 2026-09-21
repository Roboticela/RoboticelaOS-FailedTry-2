# RoboFont Test Corpus

Structured fixtures for Goal **5.21** conformance.

## Policy

* Prefer **synthetic SFNT** builders inside C suites (existing house style).
* On-disk blobs are tiny, purpose-built regressions — **not** third-party font dumps.
* Every fixture must document provenance, license, purpose, and expected behavior.
* Chromium / Skia / OTS corpora are **reference-only**; do not vendor their binaries.

## Categories

| Directory | Purpose |
|---|---|
| `sfnt/` | Signature / directory samples |
| `truetype/` | Reserved for TT-specific blobs |
| `opentype/` | Reserved for OT structural blobs |
| `cff/` / `cff2/` | CFF INDEX / CFF2 probes |
| `collections/` | TTC/OTC regressions |
| `unicode/` | cmap / UVS edge cases |
| `metrics/` | Metrics golden inputs |
| `names/` | Name-table encodings |
| `variation/` | fvar/avar edge cases |
| `color/` | COLR/CPAL/sbix/SVG inert data |
| `emoji/` | Emoji capability fixtures |
| `malformed/` | Truncated / invalid containers |
| `security/` | Pathological counts / limits |

## License

All blobs in this tree are **original RoboticelaOS test data** (public domain /
CC0 for the purpose of this repository). They are not derived from commercial
fonts or Chromium test resources.

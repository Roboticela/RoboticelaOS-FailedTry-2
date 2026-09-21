# Font Conformance (Goal 5.21)

RoboFont treats Goals **5.1–5.20** as a frozen functional surface. Goal **5.21**
is the authoritative **regression / conformance boundary** before API freeze
(Goal 5.22).

## Trust & independence

* Every fixture is **synthetic** or a tiny documented corpus blob.
* No Chromium, OTS, Skia, FreeType, HarfBuzz, or system fonts are linked or required.
* Chromium may be consulted as a reference only during development.

## Architecture helpers

Public header: `RoboFontConformance.h`

| Type / API | Role |
|---|---|
| `FontConformCategory` | Maps to filter suffixes (`sfnt`, `cmap`, …) |
| `FontConformCase` | Case metadata (id, fixture, purpose) |
| `FontConformResult` | Structured mismatch diagnostics |
| `FontGoldenData` | Deterministic `key=value\n` golden payloads |
| `FontConformFuzzSmoke` | Bounded fuzz entry over validate + cmap |

## Runner / filters

Use the existing host test runner:

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "font.conform"
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "font.conform.security"
.\runtime\Tools\Build.ps1 -Configuration Debug -Sanitize -Verify -Test -Filter "font."
```

Category filters (substring match):

| Filter | Coverage |
|---|---|
| `font.conform` | Full Goal 5.21 suite |
| `font.conform.sfnt` | SFNT container |
| `font.conform.truetype` | TrueType tables / metrics |
| `font.conform.opentype` | Outline-tech detection |
| `font.conform.cff` | CFF INDEX probe |
| `font.conform.cmap` | Unicode → GID golden |
| `font.conform.metrics` | Layout + glyph metrics golden |
| `font.conform.names` | Metadata strings |
| `font.conform.style` | Weight/stretch/style |
| `font.conform.selection` | Deterministic select |
| `font.conform.discovery` | Classify / extensions |
| `font.conform.loading` | Validate-for-load gate |
| `font.conform.coverage` | Coverage contains |
| `font.conform.fallback` | Fallback chain |
| `font.conform.generic` | Generic family names |
| `font.conform.emoji` | Color emoji face parse |
| `font.conform.variation` | Normalize / clamp golden |
| `font.conform.color` | COLR/CPAL capabilities |
| `font.conform.security` | Malformed corpus |
| `font.conform.limits` | Below / at / above limits |
| `font.conform.cross_table` | maxp ↔ hmtx ↔ cmap |
| `font.conform.golden` | Metadata golden dump |
| `font.conform.property` | Property-based checks |
| `font.conform.fuzz` | Fuzz smoke |
| `font.conform.bench` | Parse/lookup baseline |

Per-goal suites (`font.validate.*`, `font.cmap.*`, …) remain; conformance is
cross-cutting and must not regress them.

## Corpus

```text
runtime/font/tests/fonts/
├── sfnt/  truetype/  opentype/  cff/  cff2/
├── collections/  unicode/  metrics/  names/
├── variation/  color/  emoji/
├── malformed/  security/
└── README.md
```

Each category directory documents provenance/license. Binary blobs under
`malformed/` and `security/` are mirrored as permanent regressions inside
`SuiteRoboFontConformance.c`.

## Golden format

Deterministic text only — no pointers, timestamps, or hash-order maps:

```text
family=GoldenFace
ps=GoldenFacePS
upem=1000
```

## Diagnostics

On mismatch, conformance helpers record fixture, table tag, glyph ID,
code point, axis tag, expected, actual, and message — never raw addresses.

## Performance

`font.conform.bench` establishes a baseline of repeated load → cmap lookup.
Thresholds are soft (hit counts); do not micro-optimize prematurely.

## Adding a regression

1. Prefer a tiny synthetic SFNT in the suite (house style).
2. If the bug is a byte pattern, add a `.bin` under `tests/fonts/malformed/` or
   `security/` with a README note, and embed the same bytes in the suite.
3. Register `font.conform.<area>` (or extend an existing case).
4. Run `-Filter font.conform` and full `-Filter font.` under Debug + Sanitize.

# Unicode Data Pipeline Audit (Goal 3.14)

Snapshot of Roboticela Runtime Unicode data before/as of the Goal 3.14
centralization work. Authoritative pins live in
`Tools/ucd/robo_unicode_version.py` and `Include/RoboUnicode/RoboUnicodeVersion.h`.

## Versions

| Domain | Version |
|---|---|
| Unicode Standard / UCD | **16.0.0** |
| Emoji / UTS #51 | **16.0** (paired with Unicode 16.0.0) |
| Generator tooling | **1.0.0** |

## Layout

| Path | Role |
|---|---|
| `runtime/unicode/` | Documentation only (`README.md`, this audit, upgrade guide) |
| `runtime/Tools/ucd/` | Fetch + generate + verify tooling |
| `runtime/Tools/ucd/source/` | Official downloads (**gitignored**; not in normal builds) |
| `runtime/Include/RoboUnicode/*Data.h` | Generated table declarations |
| `runtime/Source/RoboUnicode/data/robo_*_data.c` | Generated compact tables (committed) |
| `runtime/Tests/Data/` | Conformance fixtures (`.inc` / `.bin`) |

## Generated vs hand-maintained

| Kind | Status |
|---|---|
| Property / segmentation / bidi / case / norm / emoji tables | **Generated** by `generate_*.py` |
| Hangul algorithmic constants in `RoboUnicodeNorm.c` | Hand-maintained Unicode algorithm (not a UCD dump) |
| Segmentation / UBA rule engines | Hand-maintained logic; tables generated |
| Famous-emoji lists | **None** — RGI catalog from official files |

## Modules and generators

| Module | Generator | Representation |
|---|---|---|
| UCD props + Script/SCX | `generate_ucd.py` | Compressed ranges + SCX pools |
| Normalization | `generate_norm.py` | Decomp maps + composition pairs |
| Grapheme | `generate_grapheme.py` | GB + InCB ranges |
| Case | `generate_case.py` | Simple/full/fold tables |
| Word | `generate_word.py` | Word_Break ranges |
| Sentence | `generate_sentence.py` | Sentence_Break ranges |
| Line | `generate_linebreak.py` | Line_Break ranges |
| Bidi | `generate_bidi.py` | Bidi_Class ranges + brackets |
| Emoji sequences | `generate_emoji.py` | Code-point trie |

Emoji **boolean** properties share `generate_ucd.py` / `robo_ucd_data.c`
(not duplicated in the sequence trie).

## Pre–3.14 risk (addressed)

Version strings were duplicated across ~9 generators, `Fetch-Ucd.ps1`,
`RoboUnicode.h`, tests, and docs. Emoji URLs were hard-coded separately from
`$Version`. Goal 3.14 introduces:

- single Python pin + synced C header
- `manifest.json` (URLs, SHA-256, roles)
- `generate_all.py` / `verify_pipeline.py` / `golden_hashes.json`

## Offline / normal builds

`Build.ps1` **compiles committed generated tables**. It does **not** download
Unicode data or run generators. Regeneration is explicit (see `UPGRADE.md`).

## Chromium

Not used for tables or generation. Reference-only if inspected for behavior.

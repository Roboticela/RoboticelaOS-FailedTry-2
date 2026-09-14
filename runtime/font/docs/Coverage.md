# Font Coverage Analysis (Goal 5.14)

Determines which Unicode scalars a loaded font can represent via its `cmap`.

**Coverage does not mean shaping support, language support, fallback
suitability, or rendering correctness.**

```text
Unicode scalar
      ↓
cmap lookup
      ↓
glyph ID
      ↓
usable? (GID ≠ 0, in range)
      ↓
coverage ranges / scripts / stats
```

## Components

| Type | Role |
|---|---|
| `RoboFontCoverage` | Immutable coverage object (retains cmap) |
| `RoboFontCoverageRange` | Contiguous **usable** code-point span |
| `RoboFontCoverageQuery` | Per-scalar status + glyph ID |
| `RoboFontCoverageStats` | Aggregates (counts, scripts, emoji, …) |

## Query statuses

| Status | Meaning |
|---|---|
| `UNSUPPORTED` | No cmap mapping |
| `MAPPED_NOTDEF` | Explicit map to glyph 0 (`.notdef`) |
| `MAPPED_USABLE` | Map to GID &gt; 0 within `numGlyphs` |
| `INVALID_MAPPING` | Malformed / out-of-range GID |
| `INVALID_CODE_POINT` | Not a Unicode scalar (surrogate / &gt; U+10FFFF) |
| `NO_CMAP` | No coverage / no primary cmap |

**Mapped ≠ usable.** `contains()` is true only for `MAPPED_USABLE`.

**Empty outline ≠ unsupported.** An intentionally blank glyph remains covered.
Probe with `robo_font_coverage_probe_outline` only when outlines are available;
default coverage never loads outlines.

## Ranges

Ranges merge contiguous **usable** scalars only. A single Latin letter does
**not** claim the Latin block. Format-0 tables that map every byte to `.notdef`
do not produce usable ranges for those bytes.

Membership uses binary search on sorted ranges (`contains`) or authoritative
`cmap` lookup (`query` / `glyph`).

## Scripts & Script_Extensions

During construction, each usable scalar contributes its Script_Extensions
(falling back to Script). Content scripts exclude Common/Inherited (tracked as
flags). **Script coverage is not language support.**

## Variation selectors

`robo_font_coverage_query_uvs` wraps Goal 5.6 format-14 UVS. Distinguishes
base support, default UVS, non-default UVS, and unsupported selector pairs.
No emoji presentation selection or shaping.

## Combining marks

Coverage reports whether a mark has a glyph mapping. Actual mark attachment /
positioning is Phase 6.

## Cache integration

Coverage attaches lazily to `RoboFontFace` via `robo_font_face_parse_coverage`.
It lives with the face in the Goal 5.13 loader cache; `approx_face_bytes`
includes coverage heap size when present. Immutable after construction;
concurrent reads are safe. Concurrent construction on one face returns
`ROBO_BUSY` while building.

## Construction

```text
cmap map
   ↓
foreach mapped scalar (ascending)
   ↓
usable → merge ranges + scripts/categories/emoji
```

Outlines are not required. Visit/range limits may set `stats.truncated`.

## Performance

| Operation | Expected |
|---|---|
| `query` / `glyph` | cmap lookup (log segments/groups) |
| `contains` | binary search on ranges |
| construction | O(mapped scalars), capped |
| range iteration | O(range_count) |

## Chromium independence

No Chromium / Skia headers, libraries, or coverage classes. Uses RoboFont cmap,
RoboUnicode Script/SCX/emoji properties, RoboMem, RoboError only.

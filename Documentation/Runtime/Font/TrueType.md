# TrueType Table Parsing (Goal 5.3)

Semantic parsers for core TrueType tables on top of the SFNT container (Goal 5.2).

```text
SFNT container          TrueType semantics
─────────────────       ──────────────────
header + directory  →   head / hhea / maxp / hmtx / loca / glyf
table byte views    →   name / OS/2 / post
```

**Not** in this goal: `cmap` (5.6), CFF (5.5), GSUB/GPOS, outlines API (5.8), rasterization.

## Tables

| Table | Role |
|---|---|
| `head` | unitsPerEm, bbox, indexToLocFormat, magic |
| `hhea` | ascender/descender/lineGap, numberOfHMetrics |
| `maxp` | numGlyphs (+ TT v1.0 limits) |
| `hmtx` | advanceWidth + lsb (last AW reused) |
| `loca` | short (×2) or long glyph offsets; monotonic |
| `glyf` | empty / simple / composite structural parse |
| `name` | name records; UTF-16BE → UTF-8 via RoboUnicode |
| `OS/2` | version-aware weight/width/fsSelection/metrics |
| `post` | italicAngle, underline, isFixedPitch |

## Glyph ID model

TrueType glyph indices are integers in `[0, numGlyphs)`. Index **0** is a real glyph
(typically `.notdef`). `RoboGlyphIdNone` (also 0) means “unmapped” only at the
**cmap** layer (Goal 5.6) — do not conflate with TT index 0 when calling
`robo_font_tt_*` metric/glyph APIs.

## Cross-table rules

- `numberOfHMetrics ∈ [1, numGlyphs]` when `numGlyphs > 0`
- `hmtx` length ≥ `4*numberOfHMetrics + 2*(numGlyphs − numberOfHMetrics)`
- `loca` has `numGlyphs + 1` offsets; non-decreasing; last ≤ `glyf` size
- Composite glyph indices `< numGlyphs`; depth/cycle capped

## Security

Bounds-checked BE reader only. Caps on glyphs, name records, composite depth,
components per glyph. Malformed input → `RoboParseError` / `RoboNotSupported`
(never UB/crash). Overlapping SFNT tables already rejected in 5.2.

## Ownership

```text
Source → SFNT → FontTt → table views / loca array
Face may retain SFNT+TT after FontFaceParseTruetype
```

`FontGlyfGlyph` owns decoded arrays; clear with `FontGlyfGlyphClear`.

## API entry

`FontTtParse(sfnt, flags, &tt)` · `FontTtFuzzParse` · face helper
`FontFaceParseTruetype`.

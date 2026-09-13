# Character-to-Glyph Mapping (`cmap`) — Goal 5.6

Authoritative Unicode scalar → glyph ID for TrueType (`glyf`), CFF, and CFF2
fonts. Structural encoding discovery remains in Goal 5.4
(`robo_font_cmap_parse`); this layer adds format bodies, selection, and lookup.

## Distinctions

| Concept | Meaning |
|---|---|
| Unicode code point / scalar | Valid scalar `U+0000..U+10FFFF` excluding surrogates |
| Glyph ID | Face-local index `< maxp.numGlyphs`; `.notdef` is `0` |
| Grapheme cluster | Text segmentation — **not** this goal |
| Glyph position | Shaping / GPOS — **not** this goal |

Inputs are **Unicode scalar values**, never raw UTF-8 bytes or UTF-16 code units.

## Architecture

```text
SFNT `cmap` table
    → encoding records (platform / encoding / offset)
    → select primary Unicode subtable (+ optional format 14 UVS)
    → immutable format body (segments / groups / arrays)
    → lookup(code_point) → RoboCmapLookupResult
```

Public surface: `RoboFontCmap.h`. Face helpers:
`robo_font_face_parse_cmap`, `robo_font_face_lookup_glyph`.

## Supported formats

| Format | Coverage | Lookup |
|---|---|---|
| 0 | `U+0000..U+00FF` | direct array |
| 2 | 8/16-bit high/low byte | subHeaderKeys + idDelta / idRangeOffset |
| 4 | BMP | binary search over segments |
| 6 | trimmed BMP array | indexed array |
| 10 | 32-bit trimmed array | indexed array |
| 12 | sequential groups (incl. SMP) | binary search |
| 13 | many→one glyph groups | binary search |
| 14 | UVS foundation | default / non-default UVS only |

## Subtable selection

Among encoding records with a recognized primary format (not 8/14):

1. Platform/encoding preference (highest first):
   - Unicode full: `(0,4)`, `(0,6)`, `(3,10)`
   - Unicode BMP: `(0,3)`, `(3,1)`, then older Unicode `(0,0|1|2)`
   - Symbol `(3,0)`, Mac Roman `(1,0)`, then other
2. Format bonus: 12/13 > 10 > 4/6 > 0 > 2

Format **14** is never primary; it is attached for UVS when present.
The first matching record wins only as a tie-breaker after scoring.

## Lookup results

| Status | Meaning |
|---|---|
| `MAPPED` | Valid GID (including explicit `.notdef` = 0) |
| `UNMAPPED` | No mapping for this scalar |
| `INVALID_CODE_POINT` | Surrogate or `> U+10FFFF` |
| `INVALID_FONT` | Mapped GID ≥ `numGlyphs` or corrupt body at lookup |

Do not treat `glyph_id == 0` alone as failure.

## Variation selectors (format 14)

`robo_font_cmap_lookup_uvs(base, vs)` distinguishes:

- `NONE` — no format 14 / no matching selector record
- `DEFAULT` — base is in a default UVS range (use primary cmap for base)
- `NON_DEFAULT` — explicit GID for `(base, vs)`
- `UNSUPPORTED` — selector known but base not covered
- `INVALID_INPUT` — bad scalars

No OpenType shaping or emoji presentation policy here.

## Performance & thread safety

After `map_create`, bodies are immutable. Concurrent readers are safe for the
font lifetime model. Format 4/12/13 use binary search; arrays are pre-copied.

## Malformed fonts & fuzzing

Offsets, lengths, group order/overlap, Unicode scalar ranges, and GID bounds
are validated. Caps limit segments/groups/arrays/UVS. Entry point:
`robo_font_cmap_fuzz_parse`.

## Chromium independence

No Chromium / Blink / Skia includes or links. `chromium/` is reference-only.

## Not in scope

Fallback, discovery, matching, shaping (GSUB/GPOS), measurement, rasterization.
Those belong to later goals / Phase 6+.

# OpenType Table Parsing (Goal 5.4)

Parses and **represents** OpenType structures for later shaping, color fonts, and
font selection. **Does not** execute GSUB/GPOS, shape text, or parse CFF/CFF2.

```text
SFNT container (5.2)
    ↓
TrueType core tables (5.3)     OpenType structures (5.4)
glyf / loca / hmtx …           Coverage, ClassDef, ScriptList,
                               FeatureList, LookupList,
                               cmap (structural), GDEF/GSUB/GPOS,
                               BASE, JSTF, MATH, kern, COLR, CPAL, STAT
```

## Outline technology

| Detection | Meaning |
|---|---|
| `glyf` + `loca` | TrueType outlines |
| `CFF ` | CFF outlines (body → Goal 5.5 / `docs/Cff.md`) |
| `CFF2` | CFF2 outlines (body → Goal 5.5 / `docs/Cff.md`) |

## Table families

`robo_font_ot_table_family(tag)` classifies tags into core / metric / cmap /
layout / variation / color / math / legacy / outline.

SFNT directory lookup remains the source of truth (`has_table` / `table_bytes`).

## Shared structures

Reusable parsers in `RoboFontOtCommon.h`:

* Coverage formats 1–2 (+ glyph → coverage index)
* ClassDef formats 1–2 (+ glyph → class, default 0)
* Device / VariationIndex (structural)
* ScriptList / FeatureList / LookupList (full offset trees)

Offsets are always **table-relative**, resolved with checked helpers.

## Layout tables

GSUB/GPOS: version 1.0/1.1 headers → ScriptList → FeatureList → LookupList.
Lookup types validated (GSUB 1–8, GPOS 1–9). Subtable bytes are located but
**not executed**.

GDEF: glyph/mark class defs + attach/ligCaret/markGlyphSets/itemVarStore offsets.

## cmap (structural + mapping)

**Goal 5.4** — Encoding records + subtable format/length validation for formats
0, 2, 4, 6, 8, 10, 12, 13, 14 (`robo_font_cmap_parse`).

**Goal 5.6** — Full character→glyph mapping, selection policy, UVS foundation:
see **`docs/Cmap.md`** and `RoboFontCmap.h`.

## Security

Resource caps on scripts/features/lookups/coverage. Malformed nested offsets →
`ROBO_PARSE_ERROR`. Fuzz entry: `robo_font_ot_fuzz_parse`.

## Relationship to Phase 6

Phase 6 (RoboText) will **consume** these structures to perform shaping.
Goal 5.4 stops at safe representation.

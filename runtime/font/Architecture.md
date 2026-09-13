# RoboFont Architecture (Goals 5.1–5.7)

## Layering

```text
Font Source     immutable bytes (memory copy/borrow; file later)
    ↓
SFNT container  header + table directory + raw table spans (Goal 5.2)
    ↓
TrueType tables head/hhea/maxp/hmtx/loca/glyf/name/OS/2/post (Goal 5.3)
    ↓
OpenType model  outline tech, table families, shared OT structures,
                cmap (structural), GDEF/GSUB/GPOS, BASE/JSTF/MATH,
                kern, COLR/CPAL, STAT (Goal 5.4 — parse only)
    ↓
CFF / CFF2      INDEX, DICT, CharStrings IR, charset/encoding,
                CID FDArray/FDSelect, CFF2 VStore (Goal 5.5)
    ↓
cmap map        Unicode scalar → glyph ID; UVS foundation (Goal 5.6)
    ↓
Metrics info    global + glyph advances/LSB/bbox; vertical foundation (5.7)
    ↓
Font Face       resource identity + collection index + optional TT/cmap/metrics
    ↓
Font Instance   face + pixel size + style request (+ future variation axes)
    ↓
Metrics         typographic measures (font units → CSS px)
Glyph ID        face-local index (≠ Unicode code point)
```

## Outline technologies

| Technology | Tables | Outline source | Horizontal metrics |
|---|---|---|---|
| TrueType | `glyf` + `loca` | TT glyph programs | `hmtx` |
| CFF | `CFF ` | Type 2 CharStrings | `hmtx` |
| CFF2 | `CFF2` | Type 2 CharStrings (+ VStore) | `hmtx` |

Do not fabricate a `glyf` view for CFF fonts. Metrics stay in shared SFNT tables.

## Distinctions

| Concept | Meaning |
|---|---|
| Unicode code point | Scalar value (validated; not UTF-8/UTF-16 units) |
| Glyph ID | Face-local drawing ID (`< maxp.numGlyphs`) |
| Layout metrics | Policy-selected ascent/descent/line_gap |
| Windows clip | `usWin*` — not default line spacing |
| Grapheme cluster | Segmentation — later text layer |
| Glyph position | Shaping — Phase 6 |

## Ownership & lifetime

- Source / SFNT / TT / OT / CFF / cmap / metrics info / face / instance use
  explicit retain/release.
- Metrics info retains TT; after create, global snapshot is immutable and
  reader-safe.

## Dependencies

Allowed: RoboBase, RoboMem, RoboUnicode (name UTF-16BE decode).

Forbidden: Chromium, Skia, OS font APIs, RoboHTML/CSS/Layout/Paint.

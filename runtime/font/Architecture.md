# RoboFont Architecture (Goals 5.1–5.11)

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
Glyph outlines  unified MOVE/LINE/QUAD/CUBIC/CLOSE paths (Goal 5.8)
    ↓
Naming/metadata name decode, locale lookup, RoboFontMetadata (Goal 5.9)
    ↓
Classification  style/weight/stretch raw + normalized (Goal 5.10)
    ↓
Selection       request ↔ loaded faces (Goal 5.11 — no discovery)
    ↓
Font Face       TT/cmap/metrics/outlines/name_info/classification
    ↓
Font Instance   face + pixel size + style request (+ future variation axes)
    ↓
Metrics         typographic measures (font units → CSS px)
Glyph ID        face-local index (≠ Unicode code point)
```

## Outline technologies

| Technology | Tables | Outline source | Horizontal metrics |
|---|---|---|---|
| TrueType | `glyf` + `loca` | TT → QUAD paths | `hmtx` |
| CFF | `CFF ` | Type 2 → CUBIC paths | `hmtx` |
| CFF2 | `CFF2` | Type 2 → CUBIC (+ VStore later) | `hmtx` |

```text
TrueType glyf ─────┐
                   ├──> RoboGlyphOutline
CFF CharStrings ───┤
CFF2 CharStrings ──┘
```

Do not fabricate a `glyf` view for CFF fonts. Metrics stay in shared SFNT tables.

## Distinctions

| Concept | Meaning |
|---|---|
| Unicode code point | Scalar value (validated; not UTF-8/UTF-16 units) |
| Glyph ID | Face-local drawing ID (`< maxp.numGlyphs`) |
| Layout metrics | Policy-selected ascent/descent/line_gap |
| Windows clip | `usWin*` — not default line spacing |
| Outline path | Geometry in font units (not pixels) |
| Font-authored name | Decoded `name` string as stored in the font |
| Normalized metadata | Snapshot fields with avail / source name ID |
| Preferred family | Policy 16 → 21 → 1 (not weight inference) |
| Normalized weight/stretch/style | Classification for later matching (not selection yet) |
| Font selection | Match request to loaded faces only (no discovery) |
| Raw OS/2 class fields | Font-authored; preserved beside normalized values |
| Grapheme cluster | Segmentation — later text layer |
| Glyph position | Shaping — Phase 6 |

## Ownership & lifetime

- Source / SFNT / TT / OT / CFF / cmap / metrics info / outline / name_info /
  classification / face / instance use explicit retain/release.
- Face-local outline cache (FIFO ≤ 512); outlines immutable after build.
- Name info + metadata + classification immutable after create; face-local only.
- Metrics info retains TT; after create, global snapshot is immutable and
  reader-safe.

## Dependencies

Allowed: RoboBase, RoboMem, RoboUnicode (UTF-16BE), RoboI18N locale (BCP-47).

Forbidden: Chromium, Skia, OS font APIs, RoboHTML/CSS/Layout/Paint.

# RoboFont Architecture (Goals 5.1–5.4)

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
Font Face       resource identity + collection index + optional TT parse
    ↓
Font Instance   face + pixel size + style request (+ future variation axes)
    ↓
Metrics         typographic measures (font units → CSS px)
Glyph ID        face-local index (≠ Unicode code point)
```

## Distinctions

| Concept | Meaning |
|---|---|
| Unicode code point | Scalar value |
| Glyph ID | Face-local drawing ID (`maxp.numGlyphs` range) |
| Glyph cluster / position | Shaping output (Phase 6; consumes 5.4 structures) |

**Never assume** 1 code point = 1 glyph. Full `cmap` mapping is Goal 5.6.

## Ownership & lifetime

- Source / SFNT / TT / OT / face / instance use explicit retain/release (non-atomic).
- TT retains SFNT; face may retain SFNT+TT after `parse_truetype`.
- OT bundle retains SFNT; table byte views valid while owning SFNT/source live.
- No global font cache (→ Goal 5.13).

## Dependencies

Allowed: RoboBase, RoboMem, RoboUnicode (name UTF-16BE decode).

Forbidden: Chromium, Skia, OS font APIs, RoboHTML/CSS/Layout/Paint.

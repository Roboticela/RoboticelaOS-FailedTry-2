# RoboFont Architecture (Goals 5.1–5.17)

## Layering

```text
Discovery       explicit sources → filter → register (Goal 5.12)
    ↓
Registry        stable registrations (identity + metadata snapshot)
    ↓
Loader / Cache  on-demand parse + bounded LRU (Goal 5.13)
    ↓
Font Source     immutable bytes (memory copy/borrow; file via IO)
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
Coverage        usable ranges, scripts/SCX, stats (Goal 5.14)
    ↓
Fallback        coverage-first alternate face resolution (Goal 5.15)
    ↓
Generic family  semantic → configured concrete families (Goal 5.16)
    ↓
Emoji fonts     classification, color/mono capabilities, select (Goal 5.17)
    ↓
Metrics info    global + glyph advances/LSB/bbox; vertical foundation (5.7)
    ↓
Glyph outlines  unified MOVE/LINE/QUAD/CUBIC/CLOSE paths (Goal 5.8)
    ↓
Naming/metadata name decode, locale lookup, RoboFontMetadata (Goal 5.9)
    ↓
Classification  style/weight/stretch raw + normalized (Goal 5.10)
    ↓
Selection       request ↔ loaded faces (Goal 5.11)
    ↓
Font Face       TT/cmap/coverage/metrics/outlines/name_info/classification
    ↓
Font Instance   face + pixel size + style request (+ future variation axes)
```

## Distinctions

| Concept | Meaning |
|---|---|
| Font discovery | Explicit sources → registry candidates (no OS-wide scan) |
| Font loading | On-demand parse + cache of registered faces |
| Font coverage | Character→glyph availability (not shaping/fallback/language) |
| Font selection | Match request to loaded faces (no discovery/load inside select) |
| Font fallback | Alternate covering face when primary lacks usable glyphs |
| Generic family | Semantic category mapped by explicit config (not a face) |
| Font-authored name | Decoded `name` string as stored in the font |
| Preferred family | Policy 16 → 21 → 1 (not weight inference) |
| Glyph ID | Face-local drawing ID (`< maxp.numGlyphs`) |

## Ownership & lifetime

- Source / SFNT / TT / OT / CFF / cmap / coverage / metrics / outline / name_info /
  classification / face / instance use explicit retain/release.
- Registry retains faces for registered entries until remove/destroy.
- Loader cache retains loaded faces; eviction releases only the cache retain.
- Fallback resolver retains result faces; `result_clear` releases them.
- Generic config uses retain/release; resolver retains layer snapshots.
- Callers who `load` must `face_release` / `load_result_clear`.

## Dependencies

Allowed: RoboBase, RoboMem, RoboUnicode, RoboI18N locale (BCP-47).

Forbidden: Chromium, Skia, OS font APIs, RoboHTML/CSS/Layout/Paint.

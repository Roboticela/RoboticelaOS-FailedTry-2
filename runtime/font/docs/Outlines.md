# Glyph Outlines (Goal 5.8)

Unified immutable geometric outlines from TrueType `glyf`, CFF Type 2
CharStrings, and CFF2 CharStrings — in **font design units**.

```text
TrueType glyf ─────┐
                   ├──> RoboGlyphOutline
CFF CharStrings ───┤
CFF2 CharStrings ──┘
```

No rasterization, hinting execution, shaping, or device-pixel conversion.

## Model

| Concept | Role |
|---|---|
| `robo_font_point_t` | Font-unit `(x,y)` |
| `robo_font_path_seg_t` | `MOVE` / `LINE` / `QUAD` / `CUBIC` / `CLOSE` |
| `robo_font_contour_t` | Ordered segments + closed flag + bounds |
| `robo_font_outline_t` | Glyph ID, status, tech, contours, global bounds |

Public geometry does **not** expose format-specific instructions.

## TrueType conversion

- Simple glyphs: expand consecutive off-curve midpoints; emit `LINE` / `QUAD`.
- Do not flatten quads to lines.
- Composite glyphs: affine compose (`F2DOT14` + translation), nested flatten.
- Security: max depth, component budget, cycle/`visiting` set, overflow checks.
- Empty (`numberOfContours == 0`) → `ROBO_FONT_OUTLINE_STATUS_EMPTY`.
- Contour winding is preserved (no auto-reverse).

## CFF / CFF2 conversion

- Consume Goal 5.5 CharString IR; hints/masks skipped for geometry.
- Relative moves/lines; all curve encodings → `CUBIC`.
- Width remains separate from path coordinates.
- Subroutine depth / instruction limits inherited from CharString parse.
- CFF2: static outlines; variation *coordinates* are Goal 5.18; blend/interp later.

## Bounds

Computed from actual geometry:

- line endpoints
- quadratic extrema
- cubic extrema
- post-transform composite points

Not control-point-only AABBs.

## Ownership & cache

```text
RoboFontFace
  └── outline_cache (FIFO, max 512)
        └── retained RoboGlyphOutline
```

After construction the outline is immutable. Face-local cache only (no global
unbounded store). Concurrent readers are safe once backends are prepared.

## Status

`ROBO_FONT_OUTLINE_STATUS_*`: OK, EMPTY, INVALID_ID, UNAVAILABLE, MALFORMED,
UNSUPPORTED, LIMIT, OVERFLOW.

## Limits

Contours, segments, points, composite depth/components, cache size — see
`RoboFontOutline.h`.

## API

| Call | Role |
|---|---|
| `robo_font_face_parse_outlines` / `face_glyph_outline` | Face path |
| `robo_font_outline_from_tt` / `_from_cff` / `_from_sfnt` | Direct |
| `robo_font_outline_dump` | Golden ASCII serialization |
| `robo_font_outline_fuzz_parse` | Malformed SFNT entry |
| `robo_font_affine_*` | Composite transforms |

## Chromium independence

No Chromium / Blink / Skia includes or link deps.

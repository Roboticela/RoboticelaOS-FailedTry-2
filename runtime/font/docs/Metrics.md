# Font Metrics (Goal 5.7)

Unified, validated font and glyph measurements for TrueType, CFF, and CFF2
SFNT fonts. Metrics are **not** shaping, kerning, layout, or CSS line-height
resolution.

## Distinctions

| Concept | Meaning |
|---|---|
| Font units (FUnits) | Design-space integers; scale by `units_per_em` |
| Glyph ID | Face-local index (`< maxp.numGlyphs`) |
| Advance width | Unshaped `hmtx` advance (no GPOS/kern) |
| Layout line metrics | Canonical ascent/descent/line_gap for text layout |
| Windows clip metrics | `usWinAscent` / `usWinDescent` — clipping, not default line spacing |
| Glyph position | Shaping / layout — later phases |

## Architecture

```text
SFNT head/hhea/hmtx/maxp/(OS/2)/(post)/(vhea/vmtx)
    → immutable robo_font_metrics_info_t
    → layout snapshot + glyph queries
    → face / instance consumers
```

Outline technology does **not** change horizontal metrics:

```text
TrueType → hmtx
CFF      → hmtx
CFF2     → hmtx
```

TrueType glyph bboxes come from `glyf` headers when present. CFF/CFF2 outline
bboxes remain unavailable until Goal 5.8 (status: no fabricated box).

## Canonical line-metric policy

1. If `OS/2` is present and `fsSelection & USE_TYPO_METRICS (0x80)`:
   use `sTypoAscender` / `|sTypoDescender|` / `sTypoLineGap` (negative gap → 0).
2. Else: use `hhea.ascender` / `|hhea.descender|` / `hhea.lineGap` (negative → 0).
3. Descent is always stored as a **non-negative magnitude** (design y = −descent).
4. `usWinAscent` / `usWinDescent` are exposed separately as clip metrics — never
   mixed into the default layout line metrics.

Categories remain distinct in `robo_font_metrics_global_t` (`hhea_*`, `typo_*`,
`win_*`, `layout`).

## Glyph metrics

`robo_font_metrics_info_glyph` / `robo_font_face_glyph_metrics`:

- Validates `glyph_id < numGlyphs`
- Advance + LSB from `hmtx` (compact: reuse last advance; per-glyph LSB)
- Right side bearing when outline bbox known:
  `RSB = advance − LSB − (xMax − xMin)`
- Empty glyf: RSB = `advance − LSB`, zero bbox

Status enum distinguishes OK / invalid ID / unavailable / malformed — never
magic sentinel numbers alone.

## Scaling & rounding

`robo_font_scale_font_units(value, upem, font_size, round_mode, out)`:

- Multiplies before dividing (double intermediate) to avoid overflow
- Rejects invalid `upem` (must be 1…16384) and non-positive sizes
- Rounding: nearest (ties away from 0), floor, ceil, toward zero
- Does **not** convert to device pixels (DPI is a later rendering concern)

## Underline / strikeout / caret

| Metric | Source | Notes |
|---|---|---|
| Underline | `post` | Else soft FALLBACK defaults (clearly marked) |
| Strikeout | `OS/2` | Absent → UNAVAILABLE |
| Caret | `hhea` | `(0,0)` → FALLBACK `(1,0)` per OT |

## Vertical foundation

Optional `vhea` + `vmtx` (same compact layout as `hmtx`). Provides
`advance_height` / `top_side_bearing`. Full vertical writing is later.

## Caching & threads

After `metrics_info` creation, the global snapshot is immutable. Glyph queries
are O(1) via `hmtx`/`vmtx` (no lazy mutable caches → no reader races). Concurrent
readers are safe under the existing face/info retain lifetime.

## Variable fonts

API shape allows a future `RoboFontInstanceMetrics` path. This goal does **not**
evaluate `HVAR`/`VVAR`/`MVAR`/`gvar`.

## Fuzzing

`robo_font_metrics_fuzz_parse` — arbitrary bytes must not crash.

## Chromium independence

No Chromium / Blink / Skia dependency. Reference only for policy comparison.

## Not in scope

Discovery, matching, fallback, variation interpolation, GSUB/GPOS, shaping,
text measurement, CSS line-height, rasterization (Goals 5.8+ / Phase 6+).

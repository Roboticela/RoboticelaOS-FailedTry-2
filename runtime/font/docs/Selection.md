# Font Selection (Goal 5.11)

Deterministic matching of a structured request against **already-loaded**
`RoboFontFace` candidates. No discovery, fallback, CSS parsing, or glyph
geometry changes. Goal 5.12 registries and Goal 5.13 loaders supply
candidates (`collect_faces` / `loader_load`); selection itself stays
discovery- and load-free.

```text
RoboFontSelectRequest
        ↓
score each RoboFontFace  (metadata + classification)
        ↓
family gate → stretch → style → weight → tie-break
        ↓
RoboFontSelection  (face + match score)
```

## Request

`robo_font_select_request_t`:

| Field | Role |
|---|---|
| `family` / aliases | UTF-8 family identity (casefold compare) |
| `weight` / `stretch` / `style` | Normalized request (Goal 5.10 types) |
| `has_oblique_angle` | Optional millidegree slant for oblique |
| `flags` | WWS preference; allow full/PostScript name |
| `reserved_axes` | Goal 5.18 — must be empty |

Not a CSS type. Reusable by RoboText / future CSS / apps.

## Family gate

A face is **eligible** only if the request family (or an alias) casefold-equals
one of:

| Priority (default) | Name ID |
|---|---|
| 1 Typographic | 16 |
| 2 WWS | 21 |
| 3 Legacy | 1 |
| 4 Full (optional flag) | 4 |
| 5 PostScript (optional flag) | 6 |

With `ROBO_FONT_SELECT_FLAG_PREFER_WWS`, WWS ranks above typographic.

Comparison uses `robo_unicode_casefold_compare_utf8` (not locale collation).
Subfamily / manufacturer / etc. are **not** family identity.

No family match → face never selected (no silent unrelated pick).

## Distance keys (lower better)

### Stretch

CSS Fonts–inspired:

- Prefer the preferred side of normal (condensed request → narrower first)
- Then absolute percent distance

### Style

| Request | Preference order |
|---|---|
| normal | normal → oblique → italic |
| italic | italic → oblique → normal |
| oblique | oblique (angle) → italic → normal |

Angle penalty uses millidegrees when both sides provide an angle.

### Weight

CSS Fonts–inspired side preference:

- request &lt; 400 → prefer lighter first
- request &gt; 500 → prefer heavier first
- 400–500 → prefer lighter first
- then absolute distance

## Combined order

Among eligible faces:

1. better `family_rank`
2. lower `stretch_dist`
3. lower `style_dist`
4. lower `weight_dist`
5. more `exact_bits` (stretch/style/weight exact)
6. higher `wws_flag`
7. lower `face_id` (stable)

Same request + same candidate multiset (by `face_id`) → same winner regardless
of input array order.

## Result

`robo_font_selection_t`:

- `found` — 0 if no eligible face (`ROBO_OK`)
- retained `face` + `face_id`
- `score` components for diagnostics
- `requested` / `selected` style_desc

Call `robo_font_selection_clear` to release the retained face.

`robo_font_select` accepts a zeroed or previously initialized
`robo_font_selection_t` (cookie `ROBO_FONT_SELECTION_COOKIE`). It will not
attempt to free a garbage `face` pointer from an uninitialized stack object.

## Limits

| Limit | Value |
|---|---|
| max candidates | 4096 |
| max aliases | 32 |
| max family bytes | 1024 |

Malformed candidates are skipped; they do not crash selection.

## Not in this goal

Discovery, fallback, generic families, CSS `@font-face`, variable axes
(`fvar` / interpolation), rasterization, shaping.

## API

| Call | Role |
|---|---|
| `robo_font_select` | Best face among candidates |
| `robo_font_select_score_face` | Score one face |
| `robo_font_match_score_compare` | Deterministic compare |
| `robo_font_*_dist` | Stretch/style/weight keys |
| `robo_font_selection_fuzz_parse` | Fuzz entry |

## Example

```c
robo_font_select_request_t req;
robo_font_selection_t sel;
robo_font_face_t *faces[2] = { face_a, face_b };

robo_font_select_request_init(&req);
robo_font_selection_init(&sel);
req.family = "Probe";
req.weight = 700;
req.stretch = 100;
req.style = ROBO_FONT_STYLE_NORMAL;

robo_font_select(&req, faces, 2, &sel);
if (sel.found) {
    /* use sel.face; inspect sel.score */
}
robo_font_selection_clear(&sel);
```

## Chromium independence

No Chromium / Blink / Skia includes or link deps.

# Variable Fonts (Goal 5.18)

OpenType variation metadata, coordinate normalization, and instance foundations.

> This goal does **not** implement gvar/HVAR/MVAR/CFF2 blend execution,
> variable glyph or metric interpolation, CSS `font-variation-settings`,
> or CSS `font-optical-sizing`.

```text
fvar (+ optional avar / STAT / name)
        ↓
RoboFontVariationModel (immutable)
        ↓
User coords → normalize → avar adjust
        ↓
RoboFontVariationCoords
        ↓
RoboFontInstance (shared face) + Selection
```

## Model

| Type | Role |
|---|---|
| `RoboFontVariationAxis` | tag, min/default/max, flags, name, registered vs custom |
| `RoboFontVariationInstance` | named instance + user coordinates |
| `RoboFontVariationCoordinates` | user + normalized + avar-adjusted snapshots |
| `RoboFontVariationModel` | parsed fvar/avar (+ STAT tags, capability flags) |
| `RoboFontVariationRequest` | style axes, named instance, opsz, explicit tags |

Registered tags recognized: `wght`, `wdth`, `ital`, `slnt`, `opsz`.
Arbitrary four-character tags remain fully supported as **custom**.

## Normalization

1. Clamp user value into `[min, max]` (deterministic; NaN/Inf rejected).
2. Default → `0`; below default → negative; above → positive (OpenType default normalization).
3. Optional `avar` piecewise-linear remapping; absent `avar` leaves values unchanged.

Malformed `avar` is ignored; valid `fvar` is retained.

## STAT

Structural STAT axis tags enrich capability metadata. They never override
validated `fvar` ranges. Font-authored OS/2 classification remains authoritative
for static style numerics; variation selection can satisfy weight/stretch/style
via axes when present.

## Instance sharing

Multiple `RoboFontInstance` objects share one immutable `RoboFontFace` and model.
Each instance owns its coordinate snapshot. No duplicate font load per instance.

Metrics variation state:

| State | Meaning |
|---|---|
| `STATIC` | no variation / no coords |
| `PENDING` | coords present; HVAR/MVAR not applied yet |
| `APPLIED` | reserved for later goals |

## Selection

When a face has `wght`/`wdth`/`ital`/`slnt` covering the request, selection
treats those distances as exact so a variable face is preferred over loading
another physical face. Explicit tagged axes, named instances, and optical-size
flags are carried on the select request.

## Outlines

`robo_font_face_glyph_outline_at` accepts variation coordinates but returns the
**static default** outline in Goal 5.18 (`variation_applied == 0`).

## Capabilities

Flags report presence of `fvar`, `avar`, `STAT`, `gvar`, `HVAR`, `MVAR`, `CFF2`,
and named instances — for later interpolation work.

## Limits

| Limit | Value |
|---|---|
| Axes | 64 |
| Named instances | 512 |
| avar segments / axis | 256 |

Malformed tables produce controlled `ROBO_PARSE_ERROR` / `ROBO_NOT_FOUND`.

## API entry points

* `robo_font_face_parse_variation` / `robo_font_face_variation`
* `robo_font_variation_model_create*`
* `robo_font_variation_coords_create*`
* `robo_font_instance_create` / `robo_font_instance_create_variation`
* `robo_font_variation_fuzz_parse`

See also: `Selection.md`, `Classification.md`, `Outlines.md`, `Metrics.md`.

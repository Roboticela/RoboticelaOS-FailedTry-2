# Color Fonts (Goal 5.19)

Unified OpenType color-font parsing and representation for RoboFont.

> This goal does **not** implement color glyph rasterization, COLR paint
> execution, gradient/bitmap/SVG rendering, CSS `font-palette`, emoji/ZWJ
> shaping, or compositor integration. Color data stays inert until a later
> graphics/text phase consumes it.

```text
SFNT color tables
  COLR / CPAL / CBDT+CBLC / sbix / SVG
        ↓
RoboFontColorInfo (immutable)
        ↓
Capabilities + palettes + glyph queries
        ↓
Future rendering (not Goal 5.19)
```

## Model

| Type | Role |
|---|---|
| `RoboColorFontInfo` (`robo_font_color_info_t`) | Parsed capabilities + table views |
| `RoboColorGlyph` | Per-glyph format + layer/paint/image/SVG flags |
| `RoboColorGlyphLayer` | COLR v0 layer glyph + palette index |
| `RoboColorGlyphPaint` | Tagged COLR v1 paint node |
| `RoboColorPalette` / entry | CPAL palette metadata + RGBA |
| `RoboColorGlyphImage` | Borrowed sbix/CBDT image span |
| `RoboColorGlyphFormat` | none / mono / layers / paint / bitmap / SVG / unsupported |

## Capabilities

| Flag | Meaning |
|---|---|
| `COLR` / `COLR_V0` / `COLR_V1` | Vector color layers or paint DAG |
| `CPAL` | Palettes present |
| `CBDT` / `CBLC` | Color bitmap location + data |
| `SBIX` | Strike-based embedded images |
| `SVG` | Inert SVG document ranges |
| `VARIATION` | COLR ItemVariationStore present (deltas not applied) |

A color-capable font is **not** automatically an emoji font. Emoji policy
(Goal 5.17) remains separate and may query color capabilities.

## COLR / CPAL

* **v0:** base glyph → layer records (glyph ID + palette index).
* **v1:** base glyph → paint DAG (formats 1–32 represented; unknown formats
  marked unsupported without unsafe reads).
* Paint nesting and cycles are bounded (`MAX_PAINT_DEPTH`, `MAX_PAINT_NODES`).
* Version `> 1` is rejected for COLR; other color tables may still parse.
* CPAL: multiple palettes, BGRA records, optional v1 type/label metadata.
* Palette selection + override entries are represented for later rendering;
  CSS palette manipulation is out of scope.

## Bitmaps / sbix / SVG

* **CBLC/CBDT:** strike metadata + safe bitmap span access (no decode).
* **sbix:** strikes, ppem, origin offsets, graphic type, borrowed data.
* **SVG:** glyph ID ranges + bounded document payloads. Never executed.
  Heuristic `has_external_ref_hint` flags possible external references;
  loaders must still treat SVG as untrusted inert bytes.

## Glyph query priority

1. COLR v1 paint  
2. COLR v0 layers  
3. sbix / CBDT bitmap  
4. SVG  
5. monochrome (no color data for that GID)

Underlying glyph IDs remain the same as outline/cmap models. Color does not
replace monochrome outlines.

## Variation

Optional `RoboFontVariationCoords` may be attached at create time and queried
later. Variation-dependent paint formats set `has_variation` / capability
flags; full paint interpolation is not executed in 5.19.

## Emoji integration

```text
Emoji policy → Selection/Fallback → Color capability → Color glyph data → Future render
```

Use `robo_font_face_parse_color` / `robo_font_face_color` alongside
`robo_font_face_emoji`. Do not move emoji selection into the color parser.

## Security / lifetime

* All color tables are untrusted: offset, overflow, nesting, and size limits.
* Bitmap/SVG/paint bytes are **views** into the font source — never outlive it.
* Parsed metadata is immutable after construction; lazy paint trees are cached
  per GID with a busy flag for concurrent init.
* No unbounded decoded-image cache.

## Limits

| Limit | Value |
|---|---|
| Palettes | 256 |
| Palette entries (total colors) | 1024 |
| Layers / paint nodes | 1024 / 4096 |
| Paint depth | 32 |
| Strikes | 64 |
| SVG documents | 4096 |
| SVG payload | 1 MiB |

## API entry points

* `robo_font_color_info_create` / `create_from_face`
* `robo_font_face_parse_color` / `robo_font_face_color`
* `robo_font_color_info_glyph*` / palette / image / svg queries
* `robo_font_color_resolve_palette_color`
* `robo_font_color_fuzz_parse`

See also: `Emoji.md`, `Variation.md`, `OpenType.md`, `Outlines.md`.

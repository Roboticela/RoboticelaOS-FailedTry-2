# Emoji Fonts (Goal 5.17)

Emoji font awareness, capability detection, and selection foundations.

> This goal does **not** implement emoji shaping, ZWJ sequence shaping,
> emoji presentation resolution, COLR paint execution, bitmap/SVG
> rasterization, or platform emoji font discovery.

```text
Coverage / OpenType tables / Unicode emoji props
        ↓
RoboFontEmojiInfo (kind + capabilities)
        ↓
Generic family `emoji` (5.16) + Selector (5.11)
        ↓
Fallback (5.15) when primary lacks emoji coverage
```

## Classification

| Kind | Meaning |
|---|---|
| `none` | Not emoji-related |
| `incidental` | A few emoji scalars in an otherwise normal font |
| `monochrome` | Meaningful emoji coverage, no color glyph tables |
| `color` | Color tables (COLR/CPAL, sbix, CBDT/CBLC, SVG) **and** emoji coverage |
| `configured` | Explicitly marked, **without** structural mono/color classification |

**Name hints** (family/full name containing `"emoji"`) are a weak signal only.
A name alone never classifies a font as monochrome, color, or configured.

When a face is listed under generic `emoji` (or `configured_as_emoji`):

* `CAP_CONFIGURED` is always set
* structural `color` / `monochrome` kind is **preserved** when tables/coverage warrant it
* `configured` kind is used only when the face has no structural emoji classification
  (e.g. an ASCII-only family explicitly mapped to `emoji`)

Distinction:

* **Emoji-capable** — usable emoji coverage and/or configured/color/mono kinds
* **Classified emoji font** — monochrome, color, or configured (not incidental)

## Capabilities

Bit flags on `robo_font_emoji_info_t`:

* coverage / monochrome / color
* vector color (`COLR`+`CPAL`)
* bitmap (`sbix`, `CBDT`/`CBLC`) — presence only; no decode
* SVG — presence only
* outline (glyf/CFF)
* UVS / VS text (`U+FE0E`) / VS emoji (`U+FE0F`)
* name hint / configured

Capability objects are immutable after construction and safe for concurrent reads.
Lazy face analysis: `robo_font_face_parse_emoji`.

## Coverage & variation selectors

Uses `RoboFontCoverage` + Unicode emoji properties (Phase 3).

* `robo_font_emoji_covers_codepoint`
* `robo_font_emoji_query_uvs`
* `robo_font_emoji_probe_presentation` — reports base coverage and FE0E/FE0F
  UVS mapping status without synthesizing presentation

## Sequence foundation

`robo_font_emoji_sequence_t` can represent:

* simple emoji
* emoji + VS15/VS16
* skin-tone modifiers
* ZWJ sequences
* regional indicators
* keycaps

`supports_sequence` is **conservative**:

* components covered → `COMPONENTS` / `COMPONENTS_AND_VS`
* ZWJ / flag / keycap / tag RGI kinds → `UNKNOWN` (needs shaping)
* never claims a single ligature glyph without shaping

## Color preference

Selection request preference:

* `any` / `prefer_color` / `prefer_mono` / `require_color` / `require_mono`

Prefer does not invent color when unavailable. Require filters candidates.

## Selection & fallback

`robo_font_emoji_select` ranks registry faces:

1. configured / classified
2. required coverage
3. variation-selector support
4. color preference
5. configured family priority (generic `emoji` + explicit lists)
6. deterministic `face_id` tie-break

`robo_font_emoji_resolve_fallback` uses emoji-related Unicode detection, then
Goal 5.15 fallback with emoji families as preferred — no second fallback algorithm.

Configuration of preferred emoji fonts is via Goal 5.16 generic `emoji` mappings
plus optional color/mono family lists on the select request. No hard-coded global
emoji font list.

## Locale

Emoji is not language-specific in this goal. Locale is not used to invent fonts.

## Caching

Face-attached emoji info is built once and retained until the face is released.
Invalidate by releasing/reloading the face when the source/registration changes.

## Chromium

Reference-only. No Chromium headers, libraries, emoji maps, or Skia paths.

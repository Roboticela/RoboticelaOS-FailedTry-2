# RoboFont API Inventory — API v1 (0.22.0)

Frozen Phase 5 public surface. Internal `Source/RoboFont/*` is not listed.

Stability: **frozen** unless noted. Thread classes: **TS** = thread-safe readers on
fully parsed immutable state; **TC** = thread-compatible (caller serializes);
**ST** = single-thread-only for mutations.

| Header | Primary symbols / types | Purpose | Ownership | Thread | Stability |
|---|---|---|---|---|---|
| `RoboFont.h` | `FontInit/shutdown`, `FontVersionString` | Umbrella + process hooks | n/a | TC | frozen |
| `RoboFontVersion.h` | `ROBO_FONT_VERSION_*`, `RoboFontApiFrozen` | Version / freeze pin | n/a | n/a | frozen |
| `RoboFontTypes.h` | IDs, units, bbox | Shared value types | value | TS | frozen |
| `RoboFontBinary.h` | `robo_font_span_*`, readers | Bounds-checked binary IO | view | TS | frozen |
| `RoboFontSource.h` | `robo_font_source_*` | Immutable font bytes | retain/release | TC | frozen |
| `RoboFontSfnt.h` | `robo_font_sfnt_*`, tags | SFNT / collection container | face-owned / view | TS after parse | frozen |
| `RoboFontTt.h` | `robo_font_tt_*` | TrueType tables | face-owned | TS after parse | frozen |
| `RoboFontOt.h` / `OtCommon.h` | OT structures | OpenType structural parse | face-owned | TS after parse | frozen |
| `RoboFontCff.h` | CFF/CFF2 | CFF INDEX/DICT/CharString | face-owned | TS after parse | frozen |
| `RoboFontCmap.h` | `robo_font_cmap_*` | Unicode → GID | face-owned | TS after parse | frozen |
| `RoboFontMetrics.h` | layout / glyph metrics | Advances, bearings, scale | value / face | TS | frozen |
| `RoboFontOutline.h` | `robo_font_outline_*` | Unified glyph outlines | caller release | TC | frozen |
| `RoboFontName.h` | name / metadata | Localized names | face-owned | TS after parse | frozen |
| `RoboFontStyle.h` | weight/stretch/style | Style value types | value | TS | frozen |
| `RoboFontClassification.h` | face classification | OS/2 / name derived style | face-owned | TS after parse | frozen |
| `RoboFontFace.h` | `robo_font_face_*` | Central face object | retain/release | parse=TC; query=TS | frozen |
| `RoboFontInstance.h` | `robo_font_instance_*` | Variation instance | retain/release | TC | frozen |
| `RoboFontDiscovery.h` | discovery sources / scan | Font discovery | registry / caller | ST mutate | frozen |
| `RoboFontRegistry.h` | registrations / queries | Font registry | registry owns entries | ST mutate | frozen |
| `RoboFontLoader.h` | loader / cache | Lazy load + eviction | loader owns cache | ST mutate | frozen |
| `RoboFontCoverage.h` | coverage queries | Unicode coverage | face-owned | TS after parse | frozen |
| `RoboFontSelection.h` | `FontSelect*` | Family/style matching | caller result | TC | frozen |
| `RoboFontFallback.h` | fallback resolver | Coverage-first fallback | resolver owns | TC | frozen |
| `RoboFontGenericFamily.h` | generic family map | CSS-like generics | config owned | TC | frozen |
| `RoboFontEmoji.h` | emoji capabilities | Emoji classification | face-owned | TS after parse | frozen |
| `RoboFontVariation.h` | fvar/avar/coords | Variable fonts | face / instance | TS / TC | frozen |
| `RoboFontColor.h` | COLR/CPAL/CBDT/sbix/SVG | Color font data | face-owned | TS after parse | frozen |
| `RoboFontValidate.h` | validate / limits | Security validation | caller release result | TC | frozen |
| `RoboFontConformance.h` | conform / golden / fuzz | Test harness APIs | harness | TC | **public harness** |

## Naming conventions (frozen)

| Concept | Type / ID |
|---|---|
| Font source | `FontSource` |
| Font face | `FontFace` / `FontFaceId` |
| Font instance | `FontInstance` |
| Glyph | `robo_font_glyph_id_t` (+ face or instance) |
| Registration | `FontRegistration` / `FontRegistrationId` |
| Metrics | `FontMetrics`, `FontGlyphMetrics` |
| Outline | `FontOutline` |
| Variation | `robo_font_variation_*` |
| Color | `robo_font_color_*` |
| Selection | `FontSelectRequest` / `FontSelection` |
| Fallback | `robo_font_fallback_*` |
| Validation | `robo_font_validation_*` / `robo_font_validate_*` |

## Explicit non-duplicates

* **Typeface / FontHandle / LoadedFont** — not public types; use face + loader state.
* **RoboFontOt cmap** vs **RoboFontCmap** — structural vs Unicode map (both kept).
* **Style** vs **Classification** — request values vs face-derived classification.

# RoboFont Public API (Goals 5.1–5.19)

Umbrella: `#include "RoboFont/RoboFont.h"`

| Header | Contents |
|---|---|
| `RoboFontTypes.h` | face/instance/glyph IDs, font units, sizes, bbox |
| `RoboFontStyle.h` | weight, stretch, style, generic-family IDs, axis settings |
| `RoboFontClassification.h` | raw + normalized style/weight/stretch classification |
| `RoboFontSelection.h` | select request, match score, face selection |
| `RoboFontDiscovery.h` | discovery sources, IO, scan, virtual FS, fuzz |
| `RoboFontRegistry.h` | registrations, queries, invalidate hook |
| `RoboFontLoader.h` | load request/result, cache, eviction, fuzz |
| `RoboFontCoverage.h` | Unicode coverage query, ranges, scripts, UVS, stats |
| `RoboFontFallback.h` | coverage-first fallback resolver, policy, score, cache |
| `RoboFontGenericFamily.h` | generic-family config, resolver, select/fallback glue |
| `RoboFontEmoji.h` | emoji kind/capabilities, sequence foundation, select |
| `RoboFontVariation.h` | fvar/avar model, coords, named instances, opsz |
| `RoboFontColor.h` | COLR/CPAL, CBDT/CBLC, sbix, SVG, palettes, paints |
| `RoboFontMetrics.h` | layout metrics, metrics info, scaling |
| `RoboFontOutline.h` | unified glyph outlines, affine, cache, dump, fuzz |
| `RoboFontName.h` | name table decode, locale lookup, metadata snapshot |
| `RoboFontSource.h` | memory copy/borrow sources |
| `RoboFontFace.h` | face + TT / cmap / coverage / emoji / variation / color / … |
| `RoboFontInstance.h` | instance create, variation coords, metrics sync/scale |
| `RoboFontBinary.h` | big-endian bounds-checked reader / spans |
| `RoboFontSfnt.h` | SFNT container parse, table lookup, checksums |
| `RoboFontTt.h` | TrueType table parse, hmtx/loca/glyf, name/OS/2/post |
| `RoboFontOtCommon.h` | Coverage, ClassDef, Device, Script/Feature/Lookup lists |
| `RoboFontOt.h` | Outline tech, table families, cmap/GDEF/GSUB/GPOS/BASE/… |
| `RoboFontCff.h` | CFF/CFF2 INDEX, DICT, CharStrings, CID, VStore |
| `RoboFontCmap.h` | Unicode cmap map, lookup, UVS, foreach, fuzz entry |
| `RoboFontVersion.h` | `0.19.0` |

## Color fonts (5.19)

| API | Role |
|---|---|
| `robo_font_color_info_*` | Unified color capabilities + glyph queries |
| `robo_font_face_parse_color` | Lazy face-attached color info |
| `robo_font_color_resolve_palette_color` | Palette selection + overrides |
| `robo_font_color_fuzz_parse` | Malformed-table robustness |

## Variable fonts (5.18)

| API | Role |
|---|---|
| `robo_font_variation_model_*` | fvar/avar parse, axes, named instances |
| `robo_font_variation_coords_*` | user → normalize → avar-adjusted |
| `robo_font_variation_request_*` | style/opsz/named-instance request |
| `robo_font_face_parse_variation` | Lazy face-attached model |
| `robo_font_instance_create_variation` | Instance with explicit coords |
| `robo_font_face_glyph_outline_at` | Outline request accepting coords (static) |

## Emoji fonts (5.17)

| API | Role |
|---|---|
| `robo_font_emoji_info_*` | Kind + capability analysis |
| `robo_font_emoji_sequence_*` | Multi-CP request foundation (no shaping) |
| `robo_font_emoji_select` | Prefer configured/classified emoji faces |
| `robo_font_emoji_resolve_fallback` | Emoji-aware glue to Goal 5.15 |
| `robo_font_face_parse_emoji` | Lazy face-attached cache |

## Generic families (5.16)

| API | Role |
|---|---|
| `robo_font_generic_config_*` | Explicit mappings + generation |
| `robo_font_generic_resolver_*` | Layered resolve + cache |
| `robo_font_generic_resolve` | Generic → concrete family list |
| `robo_font_generic_resolve_select` | + Goal 5.11 selection |
| `robo_font_generic_resolve_fallback` | + Goal 5.15 fallback |
| `robo_font_generic_fuzz_parse` | Malformed-input entry |

## Fallback (5.15)

| API | Role |
|---|---|
| `robo_font_fallback_resolver_create` / `_destroy` | Resolver + policy + cache |
| `robo_font_fallback_resolver_set_policy` | Replace owned policy |
| `robo_font_fallback_resolve` / `_resolve_codepoint` | Resolve request |
| `robo_font_fallback_face_covers` | Coverage probe helper |
| `robo_font_fallback_score_compare` | Deterministic score order |
| `robo_font_fallback_resolver_cache_clear` / `_stats` | Decision cache |
| `robo_font_fallback_fuzz_resolve` | Malformed / pathological entry |

## Coverage (5.14)

| API | Role |
|---|---|
| `robo_font_coverage_create_from_cmap` | Build from cmap (no outlines) |
| `robo_font_face_parse_coverage` | Lazy attach to face |
| `robo_font_coverage_query` / `_contains` / `_glyph` | Exact membership |
| `robo_font_coverage_query_uvs` | Format-14 variation selectors |
| `robo_font_coverage_range*` / `_script*` / `_stats` | Ranges & aggregates |
| `robo_font_coverage_fuzz_parse` | Malformed-input entry |

## Loader & cache (5.13)

| API | Role |
|---|---|
| `robo_font_loader_create` / `_destroy` | Loader + cache lifetime |
| `robo_font_loader_load` / `_load_registration` | On-demand load |
| `robo_font_loader_state` | Query load state |
| `robo_font_cache_*` | Invalidate, clear, evict, stats |
| `robo_font_loader_fuzz_parse` | Malformed-input entry |

## Discovery & registry (5.12)

| API | Role |
|---|---|
| `robo_font_registry_create` / `_destroy` | Registry lifetime |
| `robo_font_registry_set_io` | Injectable filesystem IO |
| `robo_font_registry_add_*_source` | Memory / file / directory |
| `robo_font_registry_scan_source` / `_scan_all` / `_refresh_source` | Discover |
| `robo_font_registry_collect_faces` / `_for_family` | Selection candidates |
| `robo_font_registry_set_invalidate_hook` | Loader integration |

## Selection (5.11)

| API | Role |
|---|---|
| `robo_font_select` | Best face among loaded candidates |
| `robo_font_select_score_face` | Score one face |
| `robo_font_match_score_compare` | Deterministic score compare |

Details: `docs/Emoji.md`, `docs/GenericFamilies.md`, `docs/Fallback.md`, `docs/Coverage.md`,
`docs/Loading.md`, `docs/Discovery.md`, `docs/Selection.md`,
`docs/Classification.md`, `docs/Naming.md`, `docs/Outlines.md`,
`docs/Metrics.md`, `docs/Cmap.md`, `docs/Cff.md`, `docs/OpenType.md`,
`docs/TrueType.md`, `docs/Sfnt.md`.

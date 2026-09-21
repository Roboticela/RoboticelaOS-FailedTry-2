# RoboFont Public API (Goals 5.1–5.22 — API v1 frozen)

Umbrella: `#include "RoboFont/RoboFont.h"`

**Freeze:** `RoboFontApiFrozen == 1` at **0.22.0**. See [`ApiFreeze.md`](ApiFreeze.md)
and [`ApiInventory.md`](ApiInventory.md).

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
| `RoboFontValidate.h` | staged validation, limits, diagnostics, fuzz |
| `RoboFontConformance.h` | conformance categories, goldens, diagnostics, fuzz smoke |
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
| `RoboFontVersion.h` | `0.22.0` + `RoboFontApiFrozen` |

## Font conformance (5.21)

| API | Role |
|---|---|
| `robo_font_conform_category_*` | Category / kind strings for filters |
| `robo_font_golden_*` | Deterministic golden serialization |
| `robo_font_conform_result_*` | Structured mismatch diagnostics |
| `FontConformFuzzSmoke` | Bounded fuzz over validate + cmap |

See `docs/Conformance.md` and `tests/README.md`.

## Font validation (5.20)

| API | Role |
|---|---|
| `robo_font_validation_policy_*` | Levels, checksum policy, limits |
| `FontValidateSource/sfnt/face` | Staged validation entry points |
| `FontValidateForLoad` | Loader integration |
| `FontValidateFuzzParse` | Malformed-input robustness |

## Color fonts (5.19)

| API | Role |
|---|---|
| `robo_font_color_info_*` | Unified color capabilities + glyph queries |
| `FontFaceParseColor` | Lazy face-attached color info |
| `FontColorResolvePaletteColor` | Palette selection + overrides |
| `FontColorFuzzParse` | Malformed-table robustness |

## Variable fonts (5.18)

| API | Role |
|---|---|
| `robo_font_variation_model_*` | fvar/avar parse, axes, named instances |
| `robo_font_variation_coords_*` | user → normalize → avar-adjusted |
| `robo_font_variation_request_*` | style/opsz/named-instance request |
| `FontFaceParseVariation` | Lazy face-attached model |
| `FontInstanceCreateVariation` | Instance with explicit coords |
| `FontFaceGlyphOutlineAt` | Outline request accepting coords (static) |

## Emoji fonts (5.17)

| API | Role |
|---|---|
| `robo_font_emoji_info_*` | Kind + capability analysis |
| `robo_font_emoji_sequence_*` | Multi-CP request foundation (no shaping) |
| `FontEmojiSelect` | Prefer configured/classified emoji faces |
| `FontEmojiResolveFallback` | Emoji-aware glue to Goal 5.15 |
| `FontFaceParseEmoji` | Lazy face-attached cache |

## Generic families (5.16)

| API | Role |
|---|---|
| `robo_font_generic_config_*` | Explicit mappings + generation |
| `robo_font_generic_resolver_*` | Layered resolve + cache |
| `FontGenericResolve` | Generic → concrete family list |
| `FontGenericResolveSelect` | + Goal 5.11 selection |
| `FontGenericResolveFallback` | + Goal 5.15 fallback |
| `FontGenericFuzzParse` | Malformed-input entry |

## Fallback (5.15)

| API | Role |
|---|---|
| `FontFallbackResolverCreate` / `_destroy` | Resolver + policy + cache |
| `FontFallbackResolverSetPolicy` | Replace owned policy |
| `FontFallbackResolve` / `_resolve_codepoint` | Resolve request |
| `FontFallbackFaceCovers` | Coverage probe helper |
| `FontFallbackScoreCompare` | Deterministic score order |
| `FontFallbackResolverCacheClear` / `_stats` | Decision cache |
| `robo_font_fallback_fuzz_resolve` | Malformed / pathological entry |

## Coverage (5.14)

| API | Role |
|---|---|
| `FontCoverageCreateFromCmap` | Build from cmap (no outlines) |
| `FontFaceParseCoverage` | Lazy attach to face |
| `FontCoverageQuery` / `_contains` / `_glyph` | Exact membership |
| `FontCoverageQueryUvs` | Format-14 variation selectors |
| `FontCoverageRange*` / `_script*` / `_stats` | Ranges & aggregates |
| `FontCoverageFuzzParse` | Malformed-input entry |

## Loader & cache (5.13)

| API | Role |
|---|---|
| `FontLoaderCreate` / `_destroy` | Loader + cache lifetime |
| `FontLoaderLoad` / `_load_registration` | On-demand load |
| `FontLoaderState` | Query load state |
| `robo_font_cache_*` | Invalidate, clear, evict, stats |
| `FontLoaderFuzzParse` | Malformed-input entry |

## Discovery & registry (5.12)

| API | Role |
|---|---|
| `FontRegistryCreate` / `_destroy` | Registry lifetime |
| `FontRegistrySetIo` | Injectable filesystem IO |
| `robo_font_registry_add_*_source` | Memory / file / directory |
| `FontRegistryScanSource` / `_scan_all` / `_refresh_source` | Discover |
| `FontRegistryCollectFaces` / `_for_family` | Selection candidates |
| `FontRegistrySetInvalidateHook` | Loader integration |

## Selection (5.11)

| API | Role |
|---|---|
| `FontSelect` | Best face among loaded candidates |
| `FontSelectScoreFace` | Score one face |
| `FontMatchScoreCompare` | Deterministic score compare |

Details: `docs/Emoji.md`, `docs/GenericFamilies.md`, `docs/Fallback.md`, `docs/Coverage.md`,
`docs/Loading.md`, `docs/Discovery.md`, `docs/Selection.md`,
`docs/Classification.md`, `docs/Naming.md`, `docs/Outlines.md`,
`docs/Metrics.md`, `docs/Cmap.md`, `docs/Cff.md`, `docs/OpenType.md`,
`docs/TrueType.md`, `docs/Sfnt.md`.

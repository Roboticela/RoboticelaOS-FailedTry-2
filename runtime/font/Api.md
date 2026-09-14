# RoboFont Public API (Goals 5.1–5.10)

Umbrella: `#include "RoboFont/RoboFont.h"`

| Header | Contents |
|---|---|
| `RoboFontTypes.h` | face/instance/glyph IDs, font units, sizes, bbox |
| `RoboFontStyle.h` | weight, stretch, style, categories, style_desc |
| `RoboFontClassification.h` | raw + normalized style/weight/stretch classification |
| `RoboFontMetrics.h` | layout metrics, metrics info, scaling, vertical foundation |
| `RoboFontOutline.h` | unified glyph outlines, affine, cache, dump, fuzz |
| `RoboFontName.h` | name table decode, locale lookup, metadata snapshot |
| `RoboFontSource.h` | memory copy/borrow sources |
| `RoboFontFace.h` | face + TT / cmap / metrics / outlines / metadata / class |
| `RoboFontInstance.h` | instance create, metrics sync/scale |
| `RoboFontBinary.h` | big-endian bounds-checked reader / spans |
| `RoboFontSfnt.h` | SFNT container parse, table lookup, checksums |
| `RoboFontTt.h` | TrueType table parse, hmtx/loca/glyf, name/OS/2/post |
| `RoboFontOtCommon.h` | Coverage, ClassDef, Device, Script/Feature/Lookup lists |
| `RoboFontOt.h` | Outline tech, table families, cmap/GDEF/GSUB/GPOS/BASE/… |
| `RoboFontCff.h` | CFF/CFF2 INDEX, DICT, CharStrings, CID, VStore |
| `RoboFontCmap.h` | Unicode cmap map, lookup, UVS, fuzz entry |
| `RoboFontVersion.h` | `0.10.0` |

## Classification (5.10)

| API | Role |
|---|---|
| `robo_font_classify` | Raw bag → normalized classification |
| `robo_font_classification_info_create` / `_from_sfnt` | Build snapshot |
| `robo_font_face_parse_classification` / `face_classification` | Face |
| `robo_font_weight_category` / `stretch_from_width_class` | Helpers |
| `robo_font_classification_fuzz_parse` | Malformed-input entry |

## Naming & metadata (5.9)

| API | Role |
|---|---|
| `robo_font_name_info_create` / `_from_span` | Decode + metadata |
| `robo_font_name_info_lookup` | Localized name-ID query |
| `robo_font_name_info_metadata` | Normalized snapshot |
| `robo_font_metadata_preferred_family` / `_subfamily` | 16→21→1 / 17→22→2 |
| `robo_font_face_parse_metadata` / `face_name_info` / `face_metadata` | Face |
| `robo_font_name_fuzz_parse` | Malformed-input entry |

## Outlines (5.8)

| API | Role |
|---|---|
| `robo_font_outline_from_tt` / `_from_cff` / `_from_sfnt` | Build outline |
| `robo_font_face_parse_outlines` / `face_glyph_outline` | Face + cache |
| `robo_font_outline_dump` | Golden ASCII dump |
| `robo_font_outline_fuzz_parse` | Malformed-input entry |
| `robo_font_affine_identity` / `_multiply` / `_apply` | Composite math |

## Metrics (5.7)

| API | Role |
|---|---|
| `robo_font_metrics_info_create` / `_from_tt` | Immutable metrics info |
| `robo_font_metrics_info_global` / `_layout` / `_glyph` | Queries |
| `robo_font_metrics_info_glyph_vertical` | Optional vhea/vmtx |
| `robo_font_scale_font_units` | Safe FUnit → size scaling + rounding |
| `robo_font_face_parse_metrics` / `face_glyph_metrics` | Face path |
| `robo_font_instance_sync_metrics` / `scale_advance` | Sized instance |
| `robo_font_metrics_fuzz_parse` | Malformed-input entry |

Details: `docs/Classification.md`, `docs/Naming.md`, `docs/Outlines.md`,
`docs/Metrics.md`, `docs/Cmap.md`, `docs/Cff.md`, `docs/OpenType.md`,
`docs/TrueType.md`, `docs/Sfnt.md`.

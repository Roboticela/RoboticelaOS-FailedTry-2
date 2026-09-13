# RoboFont Public API (Goals 5.1–5.7)

Umbrella: `#include "RoboFont/RoboFont.h"`

| Header | Contents |
|---|---|
| `RoboFontTypes.h` | face/instance/glyph IDs, font units, sizes, bbox |
| `RoboFontStyle.h` | weight, stretch, style, style_desc |
| `RoboFontMetrics.h` | layout metrics, metrics info, scaling, vertical foundation |
| `RoboFontSource.h` | memory copy/borrow sources |
| `RoboFontFace.h` | face + TT / cmap / metrics parse |
| `RoboFontInstance.h` | instance create, metrics sync/scale |
| `RoboFontBinary.h` | big-endian bounds-checked reader / spans |
| `RoboFontSfnt.h` | SFNT container parse, table lookup, checksums |
| `RoboFontTt.h` | TrueType table parse, hmtx/loca/glyf, name/OS/2/post |
| `RoboFontOtCommon.h` | Coverage, ClassDef, Device, Script/Feature/Lookup lists |
| `RoboFontOt.h` | Outline tech, table families, cmap/GDEF/GSUB/GPOS/BASE/… |
| `RoboFontCff.h` | CFF/CFF2 INDEX, DICT, CharStrings, CID, VStore |
| `RoboFontCmap.h` | Unicode cmap map, lookup, UVS, fuzz entry |
| `RoboFontVersion.h` | `0.7.0` |

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

Details: `docs/Metrics.md`, `docs/Cmap.md`, `docs/Cff.md`, `docs/OpenType.md`,
`docs/TrueType.md`, `docs/Sfnt.md`.

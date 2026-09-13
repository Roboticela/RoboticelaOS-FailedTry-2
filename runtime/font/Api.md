# RoboFont Public API (Goals 5.1–5.4)

Umbrella: `#include "RoboFont/RoboFont.h"`

| Header | Contents |
|---|---|
| `RoboFontTypes.h` | face/instance/glyph IDs, font units, sizes, bbox |
| `RoboFontStyle.h` | weight, stretch, style, style_desc |
| `RoboFontMetrics.h` | metrics value type + unit↔pixel helpers |
| `RoboFontSource.h` | memory copy/borrow sources |
| `RoboFontFace.h` | face + optional `parse_truetype` |
| `RoboFontInstance.h` | instance create, metrics attach/query |
| `RoboFontBinary.h` | big-endian bounds-checked reader / spans |
| `RoboFontSfnt.h` | SFNT container parse, table lookup, checksums |
| `RoboFontTt.h` | TrueType table parse, hmtx/loca/glyf, name/OS/2/post |
| `RoboFontOtCommon.h` | Coverage, ClassDef, Device, Script/Feature/Lookup lists |
| `RoboFontOt.h` | Outline tech, table families, cmap/GDEF/GSUB/GPOS/BASE/… |
| `RoboFontVersion.h` | `0.4.0` |

## TrueType (5.3)

| API | Role |
|---|---|
| `robo_font_tt_parse` | Bundle parse + cross-validation |
| `robo_font_tt_advance_width` / `left_side_bearing` | hmtx |
| `robo_font_tt_glyph_offset` / `glyph_length` | loca |
| `robo_font_tt_parse_glyph` | simple/composite structure |
| `robo_font_name_get_utf8` | decoded names |
| `robo_font_tt_metrics` / `style_desc` | model bridge |
| `robo_font_face_parse_truetype` | attach TT to face |

## OpenType (5.4)

| API | Role |
|---|---|
| `robo_font_ot_detect_outline` | TT / CFF / CFF2 / unknown |
| `robo_font_ot_table_family` | core / metric / layout / color / … |
| `robo_font_ot_parse` | Optional bundle of present OT tables |
| `robo_font_ot_coverage_*` / `class_def_*` | Shared structure parsers + queries |
| `robo_font_cmap_parse_structural` | Encoding records + format metadata |
| `robo_font_gsub_parse` / `gpos_parse` / `gdef_parse` | Layout structure (no execution) |
| `robo_font_*_parse` (BASE/JSTF/MATH/kern/COLR/CPAL/STAT) | Structural parsers |
| `robo_font_ot_fuzz_parse` | Malformed-input entry |

Details: `docs/OpenType.md`, `docs/TrueType.md`, `docs/Sfnt.md`.

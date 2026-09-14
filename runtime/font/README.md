# RoboFont

Independent font subsystem for Roboticela Runtime (**Goals 5.1–5.10**).

RoboFont is an independent RoboticelaOS runtime subsystem.

Chromium is only a temporary technical reference.

RoboFont must never depend on Chromium headers, Chromium libraries,
Chromium source files, Chromium build targets, Chromium runtime components,
Skia, or operating-system font managers.

## Layout (project convention)

| Path | Role |
|---|---|
| `runtime/Include/RoboFont/` | Public API |
| `runtime/Source/RoboFont/` | Implementation |
| `runtime/Tests/Suites/SuiteRoboFont.c` | Goal 5.1 tests |
| `runtime/font/tests/SuiteRoboFontSfnt.c` | Goal 5.2 SFNT tests |
| `runtime/font/tests/SuiteRoboFontTrueType.c` | Goal 5.3 TrueType tests |
| `runtime/font/tests/SuiteRoboFontOpenType.c` | Goal 5.4 OpenType tests |
| `runtime/font/tests/SuiteRoboFontCff.c` | Goal 5.5 CFF/CFF2 tests |
| `runtime/font/tests/SuiteRoboFontCmap.c` | Goal 5.6 cmap mapping tests |
| `runtime/font/tests/SuiteRoboFontMetrics.c` | Goal 5.7 metrics tests |
| `runtime/font/tests/SuiteRoboFontOutline.c` | Goal 5.8 outline tests |
| `runtime/font/tests/SuiteRoboFontName.c` | Goal 5.9 naming/metadata tests |
| `runtime/font/tests/SuiteRoboFontClassification.c` | Goal 5.10 classification tests |
| `runtime/font/` | Documentation |

## Architecture

```text
Font Source → SFNT (5.2) → TrueType (5.3) / OpenType (5.4) / CFF·CFF2 (5.5)
         → cmap (5.6) → metrics (5.7) → outlines (5.8) → naming (5.9)
         → classification (5.10) → Face → Instance
```

See **Architecture.md**, **docs/Sfnt.md**, **docs/TrueType.md**, **docs/OpenType.md**,
**docs/Cff.md**, **docs/Cmap.md**, **docs/Metrics.md**, **docs/Outlines.md**,
**docs/Naming.md**, **docs/Classification.md**.

## Goal status

| Goal | Scope |
|---|---|
| **5.1** | Types, memory sources, face/instance, style, metrics helpers |
| **5.2** | SFNT header + table directory, BE reader, checksums, bounded views |
| **5.3** | head/hhea/maxp/hmtx/loca/glyf/name/OS/2/post + cross-validation |
| **5.4** | OpenType structures, layout foundations, color/STAT (no shaping) |
| **5.5** | CFF/CFF2 INDEX/DICT/CharStrings/CID/VStore |
| **5.6** | Unicode `cmap` formats 0/2/4/6/10/12/13 + format 14 UVS foundation |
| **5.7** | Unified font/glyph metrics, scaling, vertical foundation |
| **5.8** | Unified glyph outlines (TT quads, CFF/CFF2 cubics, composites) |
| **5.9** | `name` table decode, locale lookup, RoboFontMetadata |
| **5.10** | Style / weight / stretch classification (raw + normalized) |

**Not** in scope yet: font selection (5.11), discovery, shaping (Phase 6),
rasterization, hinting execution, variable-font tables (5.18).

## Version

RoboFont **0.10.0**

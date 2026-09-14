# RoboFont

Independent font subsystem for Roboticela Runtime (**Goals 5.1–5.17**).

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
| `runtime/font/tests/SuiteRoboFontSelection.c` | Goal 5.11 selection tests |
| `runtime/font/tests/SuiteRoboFontDiscovery.c` | Goal 5.12 discovery tests |
| `runtime/font/tests/SuiteRoboFontLoader.c` | Goal 5.13 loader/cache tests |
| `runtime/font/tests/SuiteRoboFontCoverage.c` | Goal 5.14 coverage tests |
| `runtime/font/tests/SuiteRoboFontFallback.c` | Goal 5.15 fallback tests |
| `runtime/font/tests/SuiteRoboFontGenericFamily.c` | Goal 5.16 generic-family tests |
| `runtime/font/tests/SuiteRoboFontEmoji.c` | Goal 5.17 emoji font tests |
| `runtime/font/` | Documentation |

## Architecture

```text
Discovery (5.12) → Registry → Loader/Cache (5.13) → Selection (5.11)
         ↕
Font Source → SFNT (5.2) → TrueType (5.3) / OpenType (5.4) / CFF·CFF2 (5.5)
         → cmap (5.6) → Coverage (5.14) → Fallback (5.15)
         → Generic Families (5.16) → Emoji Fonts (5.17)
         → metrics (5.7) → outlines (5.8)
         → naming (5.9) → classification (5.10) → Face → Instance
```

See **Architecture.md**, **docs/Sfnt.md**, **docs/TrueType.md**, **docs/OpenType.md**,
**docs/Cff.md**, **docs/Cmap.md**, **docs/Metrics.md**, **docs/Outlines.md**,
**docs/Naming.md**, **docs/Classification.md**, **docs/Selection.md**,
**docs/Discovery.md**, **docs/Loading.md**, **docs/Coverage.md**, **docs/Fallback.md**,
**docs/GenericFamilies.md**, **docs/Emoji.md**.

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
| **5.11** | Font selection among loaded faces (family/style/weight/stretch) |
| **5.12** | Explicit discovery, registry, TTC/OTC faces, refresh |
| **5.13** | Lazy load, bounded LRU cache, invalidation, load policies |
| **5.14** | Unicode coverage ranges, scripts/SCX, UVS, stats (no fallback) |
| **5.15** | Coverage-first fallback, chains, scoring, sequence foundation |
| **5.16** | Generic families (serif/sans/…/system-ui/emoji/math/ui-*) |
| **5.17** | Emoji font classification, capabilities, select/fallback glue |

**Not** in scope yet: CSS matching, variable-font tables (5.18), color-glyph
rendering (5.19), shaping (Phase 6), rasterization.

## Version

RoboFont **0.17.0**

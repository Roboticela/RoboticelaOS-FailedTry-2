# RoboFont

Independent font subsystem for Roboticela Runtime (**Goals 5.1–5.4**).

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
| `runtime/font/` | Documentation |

## Architecture

```text
Font Source → SFNT (5.2) → TrueType tables (5.3) → OpenType structures (5.4)
         → Face → Instance → Metrics
```

See **Architecture.md**, **docs/Sfnt.md**, **docs/TrueType.md**, **docs/OpenType.md**.

## Goal status

| Goal | Scope |
|---|---|
| **5.1** | Types, memory sources, face/instance, style, metrics helpers |
| **5.2** | SFNT header + table directory, BE reader, checksums, bounded views |
| **5.3** | head/hhea/maxp/hmtx/loca/glyf/name/OS/2/post + cross-validation |
| **5.4** | OpenType outline detect, table families, Coverage/ClassDef/Device, cmap structural, GDEF/GSUB/GPOS, BASE/JSTF/MATH/kern/COLR/CPAL/STAT |

**Not** in scope yet: CFF/CFF2 bodies (5.5), full `cmap` mapping (5.6), shaping (Phase 6), discovery, rasterization.

Goal 5.4 parses and represents OpenType structures. It does **not** perform text shaping.

## Version

RoboFont **0.4.0**

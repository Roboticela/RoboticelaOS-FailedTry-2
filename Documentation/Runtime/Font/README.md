# RoboFont

Independent font subsystem for Roboticela Runtime (**Goals 5.1–5.22**).

**RoboFont API v1** is frozen at **0.22.0** (`RoboFontApiFrozen == 1`).
See [`ApiFreeze.md`](ApiFreeze.md) and [`Phase5.md`](Phase5.md).

RoboFont is an independent RoboticelaOS runtime subsystem.

Chromium is only a temporary technical reference.

RoboFont must never depend on Chromium headers, Chromium libraries,
Chromium source files, Chromium build targets, Chromium runtime components,
Skia, or operating-system font managers.

## Layout (project convention)

| Path | Role |
|---|---|
| `runtime/Include/RoboFont/` | Public API (frozen) |
| `runtime/Source/RoboFont/` | Implementation |
| `runtime/Tests/Suites/SuiteRoboFont.c` | Goal 5.1 tests |
| `runtime/font/tests/SuiteRoboFont*.c` | Goals 5.2–5.22 suites |
| `runtime/font/tests/SuiteRoboFontPhase5.c` | Goal 5.22 freeze / E2E |
| `runtime/font/tests/fonts/` | Documented fixture corpus |
| `runtime/font/` | Documentation |

## Architecture

```text
Font Source → Validation → SFNT → Face → Metadata / Coverage / Variation
         → Instance → Metrics / Glyphs / Color
         → Discovery → Registry → Loader/Cache
         → Selection → Fallback → (RoboText later)
```

See **Architecture.md**, **ApiFreeze.md**, feature docs under **docs/**,
and **docs/Integration.md** for consumer contracts.

## Goal status

| Goal | Scope |
|---|---|
| **5.1–5.21** | Model through conformance |
| **5.22** | Final integration, API freeze, E2E acceptance |
| **12.2** | System typeface pack: `/System/Fonts/`, its manifest, semantic roles, discovery/selection binding. See [`../../Development/SystemTypefacePack.md`](../../Development/SystemTypefacePack.md) |

**Not** in scope: shaping (Phase 6), rasterization, CSS/web-font policy.

## Version

RoboFont **0.22.0** — **API v1 frozen**

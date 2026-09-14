# RoboGraphics Test Strategy (Goal 6.15)

## Hierarchy

Tests live under `runtime/graphics/tests/` and register as `graphics.*`
names in the unified RoboTest host binary.

```text
RoboGraphics
├── graphics.*              SuiteRoboGraphics.c          (6.1 smoke)
├── graphics.pf.*           SuiteRoboGraphicsPixelFormat.c
├── graphics.fb.*           SuiteRoboGraphicsFramebuffer.c
├── graphics.surf.*         SuiteRoboGraphicsSurface.c
├── graphics.bmp.*          SuiteRoboGraphicsBitmap.c
├── graphics.geom.*         SuiteRoboGraphicsGeometry.c  (pixels/rects)
├── graphics.line.*         SuiteRoboGraphicsLine.c
├── graphics.path.*         SuiteRoboGraphicsPath.c
├── graphics.xform.*        SuiteRoboGraphicsTransform.c
├── graphics.comp.*         SuiteRoboGraphicsComposite.c
├── graphics.raster.*       SuiteRoboGraphicsRaster.c
├── graphics.text.*         SuiteRoboGraphicsText.c
├── graphics.encode.*       SuiteRoboGraphicsEncode.c
├── graphics.shot.*         SuiteRoboGraphicsScreenshot.c
└── graphics.p6.*           SuiteRoboGraphicsPhase6.c    (6.15 integration)
```

Filter examples:

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "graphics."
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "graphics.p6"
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Sanitize -Filter "graphics."
```

## Principles

* Exact expected pixels where possible (integer RGBA8).
* Float transforms use small tolerances.
* Deterministic: no wall-clock IDs, no random fixtures.
* Synthetic RoboFont bytes only — no system fonts, no Chromium.
* Phase 6.15 adds integration / freeze / smoke; it does not replace
  per-goal suites.

## Visual fixtures

`graphics.p6.visual_fixture` embeds a 4×4 RGBA reference grid for:

* clear + fill_rect + line + translucent pixel

Integration then captures and encodes that scene.

## Safety

Host tests run under AddressSanitizer / UBSan when `-Sanitize` is available.
Build.ps1 also scans RoboGraphics sources for Chromium/Skia/GPU headers.

## Performance

`graphics.p6.perf_smoke` is a coarse CPU-time ceiling only — not a benchmark.

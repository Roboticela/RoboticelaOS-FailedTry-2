# RoboGraphics API Freeze (Goal 6.15)

**RoboGraphics API v1** is frozen at **0.15.0** (`ROBO_GRAPHICS_API_FROZEN == 1`).

Breaking changes to the public headers under `runtime/Include/RoboGraphics/`
require a new phase milestone and an explicit freeze revisit.

## Freeze pin

* `RoboGraphics/RoboGraphicsVersion.h` — version macros + `ROBO_GRAPHICS_API_FROZEN`
* `robo_graphics_version_string()` returns `"0.15.0"`

## Rules

* Additive APIs for later phases may appear behind new headers/goals without
  breaking v1 callers.
* Do not silently change: half-open rects, Y-down device space, straight
  RGBA8 storage, source-over semantics, or encode determinism contracts.
* Chromium remains reference-only — never a link/include dependency.

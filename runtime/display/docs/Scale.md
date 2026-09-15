# RoboDisplay Scale & DPI (Goal 7.8)

Platform-independent density model for Roboticela Runtime.

Chromium `display::Display` / `device_scale_factor` are **technical reference
only**. This module does not depend on Chromium.

## Units

| Term | Meaning |
|---|---|
| **Logical (DIP)** | Density-independent pixels used for window geometry, bounds, work area |
| **Physical** | Device pixels (framebuffer / mode resolution) |
| **Scale** | `physical ≈ logical × scale` |
| **DPI** | Effective dots-per-inch reported by a backend (`dpi_x` / `dpi_y`) |

```text
logical coordinates
        × scale
physical pixels
```

## Canonical scale

`float` scale — not an integer-only representation.

Accepted known range: **`[0.25, 8.0]`** inclusive.

Examples: `1.0`, `1.25`, `1.5`, `1.75`, `2.0`, `2.5`, `3.0`.

| Value | Meaning |
|---|---|
| `0` | **Unavailable** (not “zero scale”) |
| in range | Known effective scale |
| NaN / Inf / out of range | Invalid → `ROBO_INVALID_ARGUMENT` |

## DPI

| Value | Meaning |
|---|---|
| `0` | **Unavailable** — distinct from invalid |
| `[1, 1000]` | Known effective DPI |
| negative / NaN / Inf / extreme | Invalid |

`ROBO_DISPLAY_DPI_REFERENCE` (`96`) is the CSS/Windows **reference** used only
to derive effective scale from effective DPI when scale itself is unavailable:

```text
scale ≈ dpi / 96
```

Do **not** treat 96 as the physical PPI of a monitor.

## Rounding

Single policy for all logical↔physical helpers:

* **Nearest integer**
* **Ties away from zero** (e.g. `1 × 1.5 → 2`, `-1 × 1.5 → -2`)

Conversions use `double` intermediates. Overflow of `int32` →
`ROBO_INVALID_ARGUMENT` (no silent saturate).

Rect conversion transforms **origin and size independently** (documented;
not corner-based). Negative origins are supported for multi-monitor layouts.

## Multi-display

Each `robo_display_info_t` carries its own `scale` / `dpi_*`. Displays may
differ:

```text
Display A → 1.0
Display B → 1.5
Display C → 2.0
```

There is **no** global runtime scale. Windows resolve scale from their
associated display (or an explicit `scale_preference`).

Dynamic DPI-change events and automatic cross-display relocation are
**deferred**.

## RoboWindow

| API | Role |
|---|---|
| `robo_window_get_scale` | Effective scale (state, else associated display) |
| `robo_window_get_dpi` | DPI from associated display |
| `robo_window_get_size_physical` | Logical client size × scale |
| `scale_preference` | Optional create-time lock (validated) |

Changing association updates `state.scale` unless locked by preference.

## RoboGraphics boundary

RoboGraphics surfaces/framebuffers remain **physical pixel buffers**.

| Layer | Coordinates |
|---|---|
| Window / UI geometry | Logical DIP |
| RoboGraphics raster | Physical pixels |
| Display scale | Converts between them |

Goal 7.8 does **not** insert hidden scaling inside RoboGraphics. Callers that
need a physical framebuffer size must convert explicitly.

## Text boundary

Future RoboText / font rasterization should query
`robo_window_get_scale` / display scale. No font scaling in 7.8.

## Native backend

Win32 may expose `GetDpiForWindow` when available (effective DPI → scale).
If unavailable, scale/DPI remain unset or placeholder — **never fabricated**.

Host display enumeration is still placeholder-based.

## Limitations (Goal 7.8)

* No dynamic DPI-change events
* No automatic framebuffer scaling
* No DPI-aware text/layout pipeline
* No full host multi-monitor DPI map
* Full event loop remains Goal 7.9

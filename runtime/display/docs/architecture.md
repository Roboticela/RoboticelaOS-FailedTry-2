# RoboDisplay architecture (Goal 7.3)

Chromium `ui/display` / `display::Display` are **temporary technical reference
only**. RoboDisplay must never depend on Chromium headers, libraries, or types.

## Role

```text
Platform backends (future)
        ↓
 RoboDisplay backend vtable
        ↓
 RoboDisplay manager (cache)
        ↓
 RoboWindow association / queries
```

RoboDisplay is independent of RoboWindow. Windows consume display info through
this module; they do not own the display backend.

## Identity

`robo_display_id_t` is a `uint64_t` runtime token:

- `ROBO_DISPLAY_ID_NONE` (0) is invalid / unset
- Comparable via `robo_display_id_equal` (NONE never equals)
- Not an OS handle, not a window ID, not a permanent enumeration index
- Backends assign stable IDs for the lifetime of their session

## Geometry

Uses `robo_gfx_rect_t` (top-left origin, half-open, signed):

- `bounds` — full display in **logical** DIP
- `work_area` — usable logical area
- Origins may be negative (multi-monitor)

Physical mode size is separate: `resolution_width_px` / `resolution_height_px`.

## Logical vs physical

| Field | Meaning |
|-------|---------|
| `bounds` / `work_area` | Logical DIP |
| `resolution_*_px` | Physical pixels of current mode |
| `scale` | Approximate `physical ≈ logical * scale` |

Helpers: `robo_display_logical_to_physical`, `robo_display_physical_to_logical`.

Goal 7.3 does **not** query host DPI; backends supply values when known.
Zero means unknown — do not treat a hard-coded DPI as “real” host data.

## Refresh rate

`refresh_hz` is `float` (0 = unknown). Fractional rates (e.g. 59.94) are valid.
`robo_display_refresh_hz_validate` rejects NaN and negatives. No VSync yet.

## Orientation

`robo_display_orientation_t`: unknown, landscape, portrait, and flipped forms.
No automatic detection in 7.3.

## Connection state

Minimal model: unknown, connected, disconnected, unavailable.
No hotplug event loop in this goal.

## Primary display

`is_primary` is a property of a display record — **not** “ID zero”.
`robo_display_primary` returns `ROBO_NOT_FOUND` when none exist.

## Manager lifecycle

1. `robo_display_init`
2. Register backend (`use_null`, `use_placeholder`, or custom vtable)
3. `robo_display_refresh` → fill cache
4. Query via `count` / `at` / `get` / `primary`
5. `robo_display_shutdown` (clears backend)

## Backend interface

```text
initialize / shutdown
enumerate
get_primary
get_info
get_modes (optional)
```

### Stock backends

| Backend | Behavior |
|---------|----------|
| **null** | Zero displays |
| **placeholder** | One 1280×800 primary @ scale 1 — architecture only |

### Capabilities

Flags: enumerate, primary, get_info, work_area, refresh_rate, scale_dpi,
orientation, modes. Unsupported ops return `ROBO_NOT_SUPPORTED`.

## Mock backend

Tests register a configurable mock in `display/tests/SuiteRoboDisplay.c`.
It is not a production platform backend.

## RoboWindow boundary

- Window `state.display_id` / config `display_id`
- `robo_window_get_associated_display` / `robo_window_set_associated_display`
- No native window→monitor hit-testing yet

## Platform limitations (Goal 7.3)

- No real host display enumeration
- No QEMU display output
- No framebuffer presentation
- No mode switching, brightness, color profiles, or hotplug

Next: Goal 7.4 framebuffer presentation (when requested).

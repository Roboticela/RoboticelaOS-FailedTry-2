# RoboWindow Sizing (Goal 7.6)

## Logical size model

| Field | Meaning |
|---|---|
| `requested_width/height` | Last size successfully accepted by `set_size` / create |
| `width/height` (actual) | Current logical **client** size |
| `min_*` / `max_*` | Constraints (`0` = none) |

Uses `robo_gfx_size_t` for get APIs — no duplicate size type.

Logical size is **not**:

* display resolution
* framebuffer / surface dimensions
* DPI or scale factor — see [../display/docs/Scale.md](../display/docs/Scale.md)
  (Goal 7.8); convert with `robo_window_get_size_physical` / display helpers

## Constraint semantics (rejection)

Public API **rejects** invalid sizes; it does not silently clamp on `set_size`.

| Case | Behavior |
|---|---|
| No min / no max (`0`) | Only `> 0` and `≤ ROBO_WINDOW_MAX_DIM` |
| Min only | Reject below min |
| Max only | Reject above max |
| Both | Require `min ≤ max`; reject outside range |
| `min == max` (> 0) | Fixed size allowed |
| `min > max` (both > 0) | `ROBO_INVALID_ARGUMENT` |

When **setting** new constraints, if the current actual size is outside the new
range, RoboWindow applies a **deterministic corrective resize** to the nearest
valid size (clamp). That is the only clamp path.

Backend/OS may still report a different **actual** size after a successful
request (`reports_actual_state`). Requested and actual remain separate.

## APIs

```text
robo_window_set_size / request_size
robo_window_get_size                 → actual (may sync from backend)
robo_window_get_requested_size
robo_window_get/set_size_constraints
robo_window_observe_size             → future 7.9 OS resize path
```

Failed `set_size` restores previous requested values.

## Backend

* `set_size` — runtime resize
* `set_size_constraints` — optional; Win32 / null / mocks implement it
* `query_state` — actual client size when `reports_actual_state`

Caps: `resize`, `size_constraints`, `reports_actual_state`.

## Framebuffer relationship

Window size and RoboGraphics framebuffer size are **independent**.

* Presentation (Goal 7.4) validates the surface against backend present limits.
* Callers own framebuffer allocation/resize.
* Future compositor work may reconcile them; Goal 7.6 does not.

## Resize events

`set_size` / corrective constraint resize / `observe_size` may push
`ROBO_WINDOW_EVENT_RESIZED`. Full native resize event routing is **Goal 7.9**.

## Tests

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "window.size."
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "window.host.sizing"
```

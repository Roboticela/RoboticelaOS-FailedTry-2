# RoboWindow + RoboGraphics Integration (Goal 7.14)

## Pipeline

```text
Application
    ↓ begin_frame
Owned RoboGraphics surface (physical pixels)
    ↓ clear / draw (RoboGraphics APIs)
end_frame  →  mark dirty
    ↓ present_frame (if dirty)
WindowPresent → backend present_surface
    ↓
host Win32  |  QEMU SYS_DISPLAY  |  mock
```

## Surface ownership

| Object | Owner | Lifetime |
|---|---|---|
| Owned render surface | RoboWindow (retain) | create via `ensure`/`begin_frame`; release on destroy / `release_render_surface` / recreate |
| Borrowed `present_surface` (7.4) | Caller | unchanged; still supported |
| Explicit `WindowPresent(id, surf)` | Caller | read only for the call |

Physical size = logical client size × effective scale (Phase 7.8).
Default format: **BGRA8** (override with `WindowSetRenderFormat`).

Resize / scale / format change marks the surface **stale**. The next
`ensure_render_surface` / `begin_frame` releases the old surface and allocates
a new zero-filled buffer. Do not keep pointers across that boundary.

## Frame lifecycle

1. `WindowBeginFrame` — ensure surface, open frame, return draw target  
2. Draw with RoboGraphics (raster / surface ops)  
3. `WindowEndFrame` — close frame, mark dirty  
4. `WindowPresentFrame` — present owned surface if dirty; clear dirty  

Nested begin → `RoboBusy` (`*out_surface` unchanged). Present while frame open → `RoboBusy`.  
Not dirty → `present_frame` is a no-op success (skips backend).

`WindowPresent(id, NULL)` resolves: explicit surface → borrowed
association → **owned render surface**.

## Invalidation

- `WindowInvalidate` / `invalidate_rect` — mark dirty  
- `WindowIsDirty` / `clear_dirty`  
- Region dirty uses a **full-surface** present fallback (no tile damage)

## Buffering

**Single-buffered** CPU surface. No swapchain / compositor thread.

## Backends

Same present contract for host (7.13) and QEMU (7.12). No platform types in
the public graphics integration API.

## Tests / smoke

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "window.graphics."
scripts\robo test-display
scripts\robo test-qemu-display
$env:ROBO_WINDOW_SMOKE="1"
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "window.host.smoke"
```

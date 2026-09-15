# RoboWindow presentation (Goal 7.4)

## Architecture

```text
RoboGraphics surface / framebuffer
        ↓  validate (dims, format, stride, pixels)
robo_window_present / present_framebuffer
        ↓  owner-thread check
backend.present_surface (synchronous)
        ↓
mock / future host / QEMU (not yet)
```

RoboGraphics never includes OS window headers. Presentation is always through
RoboWindow’s backend vtable.

## Public API

| Call | Role |
|------|------|
| `robo_window_set_present_surface` | Borrow default surface (no retain) |
| `robo_window_present(id, surface)` | Present `surface` or associated |
| `robo_window_present_framebuffer` | Wrap FB → temporary borrowed surface → present |
| `robo_window_present_validate_*` | Explicit validation helpers |
| `robo_window_present_format_supported` | Query active backend mask |

## Formats

Default mock / early backends advertise:

* `RGBA8`, `BGRA8`, `RGB8` (`ROBO_WINDOW_PRESENT_FMT_CPU_DEFAULT`)

Channel order and depth follow RoboGraphics metadata. Alpha is **straight** as
stored — no premul conversion in 7.4. Unsupported formats return
`ROBO_NOT_SUPPORTED` (no silent reinterpret).

## Ownership / lifetime

* Present reads pixels **only for the duration of the call** (synchronous).
* Association is borrow-only; callers must keep the surface alive until present
  returns / association is cleared.
* Production backends must **not** retain the surface pointer after return.
* The test mock may **copy** bytes for integrity checks — that is test-only.

## Threading

Same as Goal 7.2: when an owner thread is bound, present must run on that
thread (`ROBO_PERMISSION_DENIED` otherwise).

## Errors (RoboError)

| Situation | Result |
|-----------|--------|
| Presented successfully (backend accepted) | `ROBO_OK` |
| Invalid / destroyed window | `ROBO_NOT_FOUND` |
| NULL / no surface / no pixels | `ROBO_INVALID_ARGUMENT` |
| Zero dims / bad stride | `ROBO_INVALID_ARGUMENT` |
| Unimplemented / masked-out format / oversize | `ROBO_NOT_SUPPORTED` |
| No present capability / null backend | `ROBO_NOT_SUPPORTED` |
| Wrong owner thread | `ROBO_PERMISSION_DENIED` |
| Backend I/O failure | backend code (e.g. `ROBO_IO_ERROR`) |

`ROBO_OK` means the backend accepted the frame — **not** that a host OS window
painted pixels. Goal 7.4 has no real host/QEMU display output.

## Capabilities

When `present != 0`:

* `present_format_mask` — `ROBO_WINDOW_PRESENT_FMT_*` bits
* `present_max_width` / `present_max_height` — 0 = unlimited (within gfx limits)

## Non-goals

No Win32/X11/Wayland/Cocoa, no QEMU output, no VSync, no GPU, no frame queues,
no compositor scheduling.

Next: Goal 7.14 Window + RoboGraphics integration (when requested).

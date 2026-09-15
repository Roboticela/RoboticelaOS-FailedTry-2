# RoboWindow Event Loop (Goal 7.9)

Platform-independent native event-loop architecture for RoboWindow.

Chromium `MessagePump` / task runners are **reference only** — not linked,
included, or copied.

## Architecture

```text
Native OS events
       ↓
Native backend (process_events / wait_events / wakeup)
       ↓
observe_* translation boundary (RoboWindow IDs only)
       ↓
In-process event queue (FIFO)
       ↓
Application poll (robo_window_event_poll)
```

OS-specific message types stay inside `platform/*`. Public events never carry
native handles or OS structs.

## Ownership

| Concern | Owner |
|---|---|
| Event loop | RoboWindow core (one authoritative loop) |
| Native backend | Single registered backend |
| Event queue | RoboWindow core |
| Window slots | RoboWindow core |
| Dispatch / observe | RoboWindow core on the owner thread |

Do not run competing native pumps. `robo_window_process_events` is an alias of
`robo_window_event_loop_poll`.

## Event representation

See `RoboWindowEvent.h`. Goal 7.9 uses lifecycle/state events:

* `CLOSE_REQUESTED` / `CLOSED`
* `RESIZED` / `MOVED`
* `SHOWN` / `HIDDEN`
* `FOCUS_GAINED` / `FOCUS_LOST`
* `DISPLAY_CHANGED` / `SCALE_CHANGED` (types reserved; emit when backend has
  a real signal)

**Deferred to later goals:** mouse, pointer, touch, gestures, clipboard,
drag/drop, timers, compositor scheduling, Unicode text/IME.

Keyboard foundation: see [Keyboard.md](Keyboard.md) (Goal 7.10).

## Identity

Events carry `robo_window_id_t` only. Destroyed / unknown IDs make
`observe_*` return `ROBO_NOT_FOUND` (no use-after-free). Queued events that
outlive a window remain pollable; callers must check `robo_window_id_is_live`
before mutating.

## Queue semantics

Capacity: `ROBO_WINDOW_EVENT_QUEUE_CAPACITY` (128).

| Op | Behavior |
|---|---|
| push | FIFO enqueue; full → `ROBO_BUSY` |
| poll | dequeue head; empty → `ROBO_NOT_FOUND` |
| peek | copy head; empty → `ROBO_NOT_FOUND` |
| clear / is_empty / size | housekeeping |

Ordering: preserve backend injection order. Same-source native order is kept.
Close request does not reorder ahead of earlier resize/move from the same
drain batch.

Destroyed-window targets: observe drops with `NOT_FOUND`; already-queued
events are not rewritten.

## Backend contract

Required: `process_events` (non-blocking batch drain, capped).

Optional:

* `wait_events(timeout_ms)` — block until native work, wakeup, or timeout;
  `ROBO_WINDOW_EVENT_WAIT_FOREVER` waits indefinitely. Does not drain.
* `wakeup` — interrupt a blocked wait (e.g. Win32 `PostThreadMessage(WM_NULL)`).

Caps: `wait_events`, `wakeup`.

## Loop control

| API | Role |
|---|---|
| `event_loop_start` | mark running |
| `event_loop_request_stop` | stop + wakeup |
| `event_loop_poll` | non-blocking drain |
| `event_loop_run_once(timeout)` | wait (if timeout≠0) then drain |
| `event_loop_run` | loop until stop |
| `event_loop_wakeup` | wake blocked wait |

Nested poll / run_once while pumping → `ROBO_BUSY`.

## Lifecycle integration

Native close → `observe_close_request` → `CLOSE_REQUESTED` / CLOSING.
**Does not destroy.** Application calls `destroy` separately.

## Resize / move / visibility / focus

| Native signal | Core API | Notes |
|---|---|---|
| size | `observe_size` | updates actual only; no `set_size` |
| position | `observe_position` | negatives OK |
| show/hide | `observe_visibility` | only real platform signals |
| focus | `observe_focus` | not keyboard routing |

While applying programmatic `set_size` / `set_position` / `show` / `hide` /
`request_focus`, observes are suppressed to avoid recursive loops and
duplicate events.

## Thread affinity

Owner-thread tokens (`RoboWindowThread`): mutating loop APIs require
`current == owner` when bound. Wrong thread → `ROBO_PERMISSION_DENIED`.

## Reentrancy

No application callback stack inside the core. Nested `poll` during pump →
`BUSY`. Closing / destroying / resizing from the application after polling an
event is sequential and safe. Observes after destroy → `NOT_FOUND`.

## Mock backend

Tests register a `loop_mock` that:

* injects pending events drained by `process_events`
* supports direct `observe_*` injection
* implements wait/wakeup with a wake flag

No graphical environment required.

## Win32 behavior

| Message | Translation |
|---|---|
| `WM_CLOSE` | `observe_close_request` (no DestroyWindow) |
| `WM_SIZE` | `observe_size` (non-minimized) |
| `WM_MOVE` | `observe_position` |
| `WM_SHOWWINDOW` | `observe_visibility` |
| `WM_SETFOCUS` / `WM_KILLFOCUS` | `observe_focus` |
| wait | `MsgWaitForMultipleObjects` + `QS_ALLINPUT` |
| wakeup | `PostThreadMessage(WM_NULL)` |

## Error handling

Backend init / wait / process / wakeup failures propagate as
`ROBO_INIT_ERROR` / `ROBO_IO_ERROR` (etc.). Invalid observe dimensions →
`ROBO_INVALID_ARGUMENT`. Dropped invalid targets are logged at debug level.

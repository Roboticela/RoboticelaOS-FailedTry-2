# RoboWindow Lifecycle (Goal 7.7)

Close request and destruction are distinct. Visibility is not a lifecycle
enum — use `state.visible` for shown/hidden.

## States

| Enum | Meaning |
|---|---|
| `UNINITIALIZED` | Slot unused / before create |
| `CREATED` | Live window; mutations allowed |
| `CLOSING` | Close requested; native may still exist |
| `CLOSED` | Destroyed; id is not live |

```text
uninitialized
      ↓  create
   created  ←→  show / hide  (visibility flags only)
      ↓  request_close / observe_close_request
   closing
      ↓  destroy
   closed
```

`destroy` from `created` is also valid (skip explicit close).

## Valid transitions

| From | To | How |
|---|---|---|
| UNINITIALIZED | CREATED | `WindowCreate` |
| CREATED | CLOSING | `request_close` / `observe_close_request` |
| CREATED | CLOSED | `destroy` |
| CLOSING | CLOSED | `destroy` |
| CLOSED | CLOSED | allowed as no-op in the state machine helper only |

Invalid transitions → `RoboInvalidArgument`.
Destroyed windows cannot become active again.

## Close vs destroy

| Operation | Effect |
|---|---|
| `WindowRequestClose` | CREATED → CLOSING; emit `CLOSE_REQUESTED`; **keeps** native handle |
| `WindowObserveCloseRequest` | Same as request when CREATED; if already CLOSING → `RoboOk` (idempotent for native spam) |
| `WindowDestroy` | Native destroy once → CLOSED; invalidate handle; emit `CLOSED` |

Close does **not** destroy the OS window. That separation supports future close
confirmation and unsaved-state handling without inventing dialogs in 7.7.

## Close cancellation

**Not supported** at this lifecycle layer. Close requests are irreversible.
No cancel API.

## Idempotency

| Call | Result |
|---|---|
| `request_close` while CLOSING | `RoboAlreadyExists` |
| `observe_close_request` while CLOSING | `RoboOk` |
| `destroy` twice | second → `RoboNotFound` |
| close / mutate after destroy | `RoboNotFound` |
| show / resize / present / hide while CLOSING | `RoboInvalidArgument` |

Reads (`get_state`, `get_size`, `get_lifecycle`, `get_native_handle`) remain
valid while CLOSING.

## Events (existing queue)

Uses the Goal 7.1 event queue — no second callback system:

* `CREATED`
* `SHOWN` / `HIDDEN`
* `CLOSE_REQUESTED`
* `CLOSED` / destroyed

No events after the window is destroyed.

## Native close boundary (Goal 7.9)

Win32 `WM_CLOSE` → `WindowObserveCloseRequest` → CLOSING + queue event.
The OS window is **not** destroyed until `WindowDestroy`.
See [EventLoop.md](EventLoop.md).

```text
native OS close (e.g. WM_CLOSE)
        ↓
WindowObserveCloseRequest(id)
        ↓
CREATED → CLOSING + CLOSE_REQUESTED event
        ↓
application later calls destroy
```

Win32 `WM_CLOSE` already calls `observe_close_request` and does **not**
`DestroyWindow` — destroy owns native teardown.

Full native event-loop dispatch is **deferred to Goal 7.9**.

## Backend shutdown

`WindowShutdown` destroys all live windows (CREATED or CLOSING),
invalidates handles, clears present surfaces, then clears the backend.
Logs a warning if any live windows existed. Subsequent ops → `RoboInitError`.

## Ownership

```text
RoboWindow slot
  ├── lifecycle / state / title
  ├── present_surface (borrowed; cleared on destroy)
  └── native handle (backend-owned until destroy)
```

Destroyed windows retain no usable native resources.

## Thread affinity

Lifecycle ops use the owner-thread contract (`RoboPermissionDenied` on the
wrong thread). No cross-thread dispatcher.

## Errors

| Condition | Code |
|---|---|
| Missing / destroyed window | `RoboNotFound` |
| Invalid transition / mutate while CLOSING | `RoboInvalidArgument` |
| Close already requested | `RoboAlreadyExists` |
| Native destroy failure | backend error; lifecycle unchanged |
| Wrong owner thread | `RoboPermissionDenied` |
| Not initialized | `RoboInitError` |

## Limitations (Goal 7.7)

* No full event loop / input events
* No close-confirmation UI
* No DPI / QEMU backend
* Close is irreversible at this layer

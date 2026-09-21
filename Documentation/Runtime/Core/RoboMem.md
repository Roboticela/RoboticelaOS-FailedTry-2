# RoboMem

Memory subsystem for Roboticela Runtime (**Goals 2.1–2.5**).

**Canonical guide:** [`memory/README.md`](../memory/README.md)  
**Umbrella header:** `Include/RoboMem/RoboMemAll.h`

## Purpose

Provide portable heap, arena, and pool allocators plus an optional debug layer
used by higher subsystems (containers, DOM, JS, …).

## Public API (heap)

Header: `Include/RoboMem/RoboMem.h`

| Function | Role |
|---|---|
| `Malloc(size)` | Uninitialized block |
| `RoboCalloc(count, size)` | Zero-filled block |
| `Realloc(ptr, size)` | Resize / malloc / free |
| `RoboFree(ptr)` | Release |
| `RoboMemCopy` / `move` / `set` / `compare` | Safe-ish byte helpers |

OOM injection for tests: `MemDebugFailNext` in `RoboMemDebug.h`.

## Ownership

- Every **non-NULL** pointer from `Malloc`, `RoboCalloc`, or a successful
  `Realloc` is **owned by the caller**.
- Ownership is released by `RoboFree`, or transferred into a later
  `Realloc` that returns a (possibly new) pointer.
- On **failed** `Realloc`, the original pointer remains owned by the caller
  and must still be freed.

## Failure behavior

- Allocation failure returns **NULL** (maps conceptually to `RoboOutOfMemory`).
- `RoboCalloc` also returns NULL if `count * size` would overflow `size_t`.
- No abort on OOM in this layer.

## Zero-size behavior

| Call | Result |
|---|---|
| `Malloc(0)` | `NULL` |
| `RoboCalloc(0, n)` / `RoboCalloc(n, 0)` | `NULL` |
| `Realloc(ptr, 0)` | frees `ptr` (if any), returns `NULL` |
| `RoboFree(NULL)` | no-op |

## NULL behavior (utilities)

- `n == 0`: copy/move/set return `dst`; compare returns 0.
- `n > 0` with NULL pointer(s): copy/move/set return NULL; compare returns non-zero if exactly one side is NULL.

## Alignment

Returned blocks have the same alignment guarantees as the platform `malloc`
(suitable for any object type the C library allocator supports). Tests check
at least pointer alignment.

## Thread-safety

As thread-safe as the underlying C library allocator. RoboMem does **not** add
its own locks. The fail-injection counter is **not** thread-safe.

## Arena allocator (Goal 2.2)

Header: `Include/RoboMem/RoboArena.h`

Bump allocator for temporary / parse-scoped objects (HTML/CSS/DOM later).

| Function | Role |
|---|---|
| `ArenaCreate(capacity)` | Fixed backing region (one `Malloc`) |
| `ArenaAlloc` | Default-aligned bump alloc (`sizeof(void *)`) |
| `ArenaAllocAligned` | Power-of-two alignment |
| `ArenaReset` | Invalidate all arena pointers; reuse buffer |
| `ArenaDestroy` | Free arena |
| `ArenaBytesRemaining` | Bytes left before capacity |

### Arena ownership and lifetime

- Creator owns the `Arena` until `ArenaDestroy`.
- Allocated pointers are valid until **reset** or **destroy** — no per-block free.
- After reset, old pointers must not be used (may be overwritten).
- After destroy, the arena pointer must not be used.

### Arena semantics

| Case | Behavior |
|---|---|
| `create(0)` / OOM | `NULL` |
| `alloc(NULL, …)` / `size == 0` | `NULL` |
| Exhausted capacity | `NULL` |
| Alignment not power-of-two or 0 | `NULL` |
| Alignment 1 | Allowed |
| `reset` / `destroy` on NULL | No-op |

Use an **arena** for many short-lived related allocations freed together.
Use **`Malloc`** for long-lived or individually freed objects.

Arenas are **not** thread-safe.

## Pool allocator (Goal 2.3)

Header: `Include/RoboMem/RoboPool.h`

Fixed-capacity pool of equal-sized objects with individual free/reuse.

| Function | Role |
|---|---|
| `PoolCreate(object_size, capacity)` | Build pool |
| `PoolAlloc` | Take one free slot |
| `PoolFree` | Return slot (`Result`) |
| `PoolReset` | All slots free again |
| `PoolDestroy` | Release pool |
| `PoolAvailable` / `capacity` / `object_size` | Queries |

### Pool ownership and lifetime

- Creator owns the pool until destroy.
- Allocated objects are owned by the caller until free/reset/destroy.
- Only pointers returned by `PoolAlloc` on **that** pool may be freed on it.

### Free / invalid behavior

| Case | Result |
|---|---|
| Valid free | `RoboOk` |
| NULL pool/ptr, foreign ptr, misaligned/interior ptr | `RoboInvalidArgument` |
| Double-free | `RoboAlreadyExists` |

### Alignment

- Stride ≥ `max(object_size, sizeof(void *))`, rounded up to alignment.
- If `object_size` is a power of two larger than `sizeof(void *)`, that size is the alignment (e.g. 16).
- Slot addresses meet that absolute alignment.

### Capacity

Exactly `capacity` live allocations at once; further `alloc` returns NULL until free/reset.

### Use cases

Pools: many same-sized objects with individual free (DOM nodes, events, commands).  
Arenas: many temporary allocations freed together.  
`Malloc`: irregular sizes / long-lived unique objects.

Pools are **not** thread-safe.

## Debug allocator (Goal 2.4)

Optional layer around `Malloc` / `calloc` / `realloc` / `free` for development.

Header: `Include/RoboMem/RoboMemDebug.h`

### Architecture

When `RoboMemDebug=1` and debugging is enabled at runtime:

```text
[header metadata][front guard][user memory][back guard]
```

- Header stores magic, state, size, allocation id, source file/line.
- Guard bytes use a fixed pattern (`0xAB`); checked on free and via `MemDebugCheck`.
- Live allocations sit on a doubly linked list for leak reports.
- Recently freed blocks are quarantined (with magic retained) so double-free can be distinguished from invalid-free.
- Free/realloc resolve pointers by scanning live/quarantine lists (never probe arbitrary addresses), which stays ASan-safe.

Internal system allocation (`malloc`/`free`) is used under the hood so the debug layer does not recurse into itself.

### Enabling / disabling

| Mechanism | Behavior |
|---|---|
| Compile `-DRoboMemDebug=1` | Debug bookkeeping compiled in (Debug builds via `Build.ps1`) |
| `-DRoboMemDebug=0` | Thin stubs; macros inactive; no guards/stats cost |
| `MemDebugEnable()` | Track new allocations (default on when compiled in) |
| `MemDebugDisable()` | New allocs go straight to the system allocator |

Source location: when `RoboMemDebug=1`, macros remap `Malloc` etc. to `*_at(__FILE__, __LINE__)`. `RoboMem.c` defines `RoboMemImplementation` so the implementation is not macro-wrapped.

### Leak reporting

`MemDebugReport()` prints to stderr: live count, live bytes, peaks, totals, and each live allocation id/size/file:line.

### Statistics

`MemStats` via `MemDebugGetStats` / `MemDebugResetStats`:

- live / peak live allocation counts
- live / peak live bytes
- total allocs / total frees

### Detected invalid operations

| Bug | Detection |
|---|---|
| Leak | Live list + `MemDebugReport` |
| Double free | Quarantined block with `state == FREED` |
| Invalid free | Pointer lacks RoboMem magic / not a debug block |
| Buffer underflow / overflow | Front/back guard pattern mismatch |
| Corrupted metadata | Bad magic or unexpected state on free |

Default fault behavior: log + `abort()`. Tests may install `MemDebugSetFaultHandler`.

Controlled corruption without UB: `MemDebugCorruptGuardForTest` (test helper only).

### Debug vs Release

| | Debug (`RoboMemDebug=1`) | Release (`=0`) |
|---|---|---|
| Overhead | Header + 32 guard bytes + list + stats | Direct system malloc path |
| Faults | Detected as above | Not detected by RoboMem |
| Macros | Capture file/line | Plain function calls |

### Performance

Keep debugging **off** in production/Release. When enabled, each alloc/free does list + guard work — fine for development, not for hot paths.

### RoboMem vs sanitizers

| Bug | RoboMem debug | ASan (when available) |
|---|---|---|
| Leaks | Report API | LeakSanitizer (if linked) |
| Double / invalid free | Yes | Yes |
| Guard overflow/underflow | Yes (at free/check) | Yes (immediate on access) |
| Use-after-free | Poison fill only; not a full UAF detector | Primary tool |
| Uninitialized reads | No | MSan / similar |

Do not duplicate sanitizer work; use RoboMem debug for portable checks and ASan when the host toolchain supports it.

### Not provided yet (later goals)

- GC / refcount / smart pointers
- Custom page allocators
- Thread-safe / lock-free pools
- Full use-after-free tracking

## Dependencies

Heap layer uses the C library. Arena and pool use `Malloc` / `RoboFree` (inherit debug when enabled).
Must not depend on Thread, Event, DOM, JS, graphics, or networking.


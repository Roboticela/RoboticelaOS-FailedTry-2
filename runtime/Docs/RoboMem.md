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
| `robo_malloc(size)` | Uninitialized block |
| `robo_calloc(count, size)` | Zero-filled block |
| `robo_realloc(ptr, size)` | Resize / malloc / free |
| `robo_free(ptr)` | Release |
| `robo_mem_copy` / `move` / `set` / `compare` | Safe-ish byte helpers |

OOM injection for tests: `robo_mem_debug_fail_next` in `RoboMemDebug.h`.

## Ownership

- Every **non-NULL** pointer from `robo_malloc`, `robo_calloc`, or a successful
  `robo_realloc` is **owned by the caller**.
- Ownership is released by `robo_free`, or transferred into a later
  `robo_realloc` that returns a (possibly new) pointer.
- On **failed** `robo_realloc`, the original pointer remains owned by the caller
  and must still be freed.

## Failure behavior

- Allocation failure returns **NULL** (maps conceptually to `ROBO_OUT_OF_MEMORY`).
- `robo_calloc` also returns NULL if `count * size` would overflow `size_t`.
- No abort on OOM in this layer.

## Zero-size behavior

| Call | Result |
|---|---|
| `robo_malloc(0)` | `NULL` |
| `robo_calloc(0, n)` / `robo_calloc(n, 0)` | `NULL` |
| `robo_realloc(ptr, 0)` | frees `ptr` (if any), returns `NULL` |
| `robo_free(NULL)` | no-op |

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
| `robo_arena_create(capacity)` | Fixed backing region (one `robo_malloc`) |
| `robo_arena_alloc` | Default-aligned bump alloc (`sizeof(void *)`) |
| `robo_arena_alloc_aligned` | Power-of-two alignment |
| `robo_arena_reset` | Invalidate all arena pointers; reuse buffer |
| `robo_arena_destroy` | Free arena |
| `robo_arena_bytes_remaining` | Bytes left before capacity |

### Arena ownership and lifetime

- Creator owns the `robo_arena_t` until `robo_arena_destroy`.
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
Use **`robo_malloc`** for long-lived or individually freed objects.

Arenas are **not** thread-safe.

## Pool allocator (Goal 2.3)

Header: `Include/RoboMem/RoboPool.h`

Fixed-capacity pool of equal-sized objects with individual free/reuse.

| Function | Role |
|---|---|
| `robo_pool_create(object_size, capacity)` | Build pool |
| `robo_pool_alloc` | Take one free slot |
| `robo_pool_free` | Return slot (`robo_result_t`) |
| `robo_pool_reset` | All slots free again |
| `robo_pool_destroy` | Release pool |
| `robo_pool_available` / `capacity` / `object_size` | Queries |

### Pool ownership and lifetime

- Creator owns the pool until destroy.
- Allocated objects are owned by the caller until free/reset/destroy.
- Only pointers returned by `robo_pool_alloc` on **that** pool may be freed on it.

### Free / invalid behavior

| Case | Result |
|---|---|
| Valid free | `ROBO_OK` |
| NULL pool/ptr, foreign ptr, misaligned/interior ptr | `ROBO_INVALID_ARGUMENT` |
| Double-free | `ROBO_ALREADY_EXISTS` |

### Alignment

- Stride ≥ `max(object_size, sizeof(void *))`, rounded up to alignment.
- If `object_size` is a power of two larger than `sizeof(void *)`, that size is the alignment (e.g. 16).
- Slot addresses meet that absolute alignment.

### Capacity

Exactly `capacity` live allocations at once; further `alloc` returns NULL until free/reset.

### Use cases

Pools: many same-sized objects with individual free (DOM nodes, events, commands).  
Arenas: many temporary allocations freed together.  
`robo_malloc`: irregular sizes / long-lived unique objects.

Pools are **not** thread-safe.

## Debug allocator (Goal 2.4)

Optional layer around `robo_malloc` / `calloc` / `realloc` / `free` for development.

Header: `Include/RoboMem/RoboMemDebug.h`

### Architecture

When `ROBO_MEM_DEBUG=1` and debugging is enabled at runtime:

```text
[header metadata][front guard][user memory][back guard]
```

- Header stores magic, state, size, allocation id, source file/line.
- Guard bytes use a fixed pattern (`0xAB`); checked on free and via `robo_mem_debug_check`.
- Live allocations sit on a doubly linked list for leak reports.
- Recently freed blocks are quarantined (with magic retained) so double-free can be distinguished from invalid-free.
- Free/realloc resolve pointers by scanning live/quarantine lists (never probe arbitrary addresses), which stays ASan-safe.

Internal system allocation (`malloc`/`free`) is used under the hood so the debug layer does not recurse into itself.

### Enabling / disabling

| Mechanism | Behavior |
|---|---|
| Compile `-DROBO_MEM_DEBUG=1` | Debug bookkeeping compiled in (Debug builds via `Build.ps1`) |
| `-DROBO_MEM_DEBUG=0` | Thin stubs; macros inactive; no guards/stats cost |
| `robo_mem_debug_enable()` | Track new allocations (default on when compiled in) |
| `robo_mem_debug_disable()` | New allocs go straight to the system allocator |

Source location: when `ROBO_MEM_DEBUG=1`, macros remap `robo_malloc` etc. to `*_at(__FILE__, __LINE__)`. `RoboMem.c` defines `ROBO_MEM_IMPLEMENTATION` so the implementation is not macro-wrapped.

### Leak reporting

`robo_mem_debug_report()` prints to stderr: live count, live bytes, peaks, totals, and each live allocation id/size/file:line.

### Statistics

`robo_mem_stats_t` via `robo_mem_debug_get_stats` / `robo_mem_debug_reset_stats`:

- live / peak live allocation counts
- live / peak live bytes
- total allocs / total frees

### Detected invalid operations

| Bug | Detection |
|---|---|
| Leak | Live list + `robo_mem_debug_report` |
| Double free | Quarantined block with `state == FREED` |
| Invalid free | Pointer lacks RoboMem magic / not a debug block |
| Buffer underflow / overflow | Front/back guard pattern mismatch |
| Corrupted metadata | Bad magic or unexpected state on free |

Default fault behavior: log + `abort()`. Tests may install `robo_mem_debug_set_fault_handler`.

Controlled corruption without UB: `robo_mem_debug_corrupt_guard_for_test` (test helper only).

### Debug vs Release

| | Debug (`ROBO_MEM_DEBUG=1`) | Release (`=0`) |
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

Heap layer uses the C library. Arena and pool use `robo_malloc` / `robo_free` (inherit debug when enabled).
Must not depend on Thread, Event, DOM, JS, graphics, or networking.


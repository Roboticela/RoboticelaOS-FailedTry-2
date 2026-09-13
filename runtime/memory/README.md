# RoboMem — memory subsystem

Phase 2 of the Roboticela Runtime (Goals 2.1–2.5).

Source lives under `Include/RoboMem/` and `Source/RoboMem/`. This directory
holds the subsystem guide; API detail also appears in `Docs/RoboMem.md`.

## Architecture

```text
                    ┌──────────────────────┐
                    │   RoboMemDebug       │  optional (ROBO_MEM_DEBUG)
                    │  guards / leaks / stats │
                    └──────────┬───────────┘
                               │ wraps
                    ┌──────────▼───────────┐
                    │  robo_malloc / free  │  general heap
                    └──────────┬───────────┘
               ┌───────────────┼───────────────┐
               ▼               ▼               ▼
         RoboArena        RoboPool      direct callers
       (bump / temp)   (fixed-size)
```

Umbrella header: `Include/RoboMem/RoboMemAll.h`

| Component | Headers | Role |
|---|---|---|
| Heap | `RoboMem.h` | General-purpose allocate / free / realloc / byte utils |
| Arena | `RoboArena.h` | Fixed-capacity bump allocator |
| Pool | `RoboPool.h` | Fixed-size object pool with free-list |
| Debug | `RoboMemDebug.h` | Tracking, guards, reports, OOM injection |

Arena and pool obtain backing storage through `robo_malloc` / `robo_free`, so
they inherit debug tracking when `ROBO_MEM_DEBUG=1` and debugging is enabled.

## Allocator selection

| Need | Use |
|---|---|
| Irregular sizes, long-lived, individual free | `robo_malloc` / `robo_calloc` / `robo_realloc` / `robo_free` |
| Many temporary objects, free all at once | `RoboArena` (`reset` / `destroy`) |
| Many equal-sized objects, individual free/reuse | `RoboPool` |
| Development diagnostics (leaks, double-free, overflow) | `RoboMemDebug` (`ROBO_MEM_DEBUG=1`) |

Do **not** free arena pointers with `robo_free`. Do **not** free pool slots
with `robo_free` — use `robo_pool_free`.

## Public APIs (summary)

### Heap (`RoboMem.h`)

- `robo_malloc` / `robo_calloc` / `robo_realloc` / `robo_free`
- `robo_mem_copy` / `move` / `set` / `compare`
- `*_at(file, line)` variants (used by debug macros)

### Arena (`RoboArena.h`)

- `robo_arena_create` / `alloc` / `alloc_aligned` / `reset` / `destroy`
- `robo_arena_bytes_remaining`

### Pool (`RoboPool.h`)

- `robo_pool_create` / `alloc` / `free` / `reset` / `destroy`
- Queries: `object_size` / `capacity` / `available`
- `robo_pool_free` returns `robo_result_t` (`ROBO_OK`, `ROBO_INVALID_ARGUMENT`, `ROBO_ALREADY_EXISTS`)

### Debug (`RoboMemDebug.h`)

- `robo_mem_debug_enable` / `disable` / `is_enabled`
- `robo_mem_debug_report` / `get_stats` / `reset_stats` / `live_count` / `check`
- `robo_mem_debug_set_fault_handler`
- `robo_mem_debug_fail_next` (OOM injection for tests)
- `robo_mem_debug_corrupt_guard_for_test` (test helper only)

## Ownership

| Allocator | Who owns what |
|---|---|
| Heap | Caller owns non-NULL results until `robo_free` or successful consuming `realloc` |
| Arena | Creator owns arena until destroy; alloc pointers valid until reset/destroy |
| Pool | Creator owns pool; slots owned by caller until free/reset/destroy |

Failed `robo_realloc` leaves the original pointer owned by the caller.

## Alignment

- Heap: same as platform `malloc`; with debug enabled, user pointers are
  aligned to at least `max_align_t` (C11) / `sizeof(void *)`.
- Arena default: `sizeof(void *)`; `alloc_aligned` requires power-of-two.
- Pool: stride ≥ `max(object_size, sizeof(void *))`, rounded up; power-of-two
  `object_size` larger than a pointer becomes the slot alignment.

## Lifetime

- After `robo_free` / arena `reset`|`destroy` / pool `free`|`reset`|`destroy`,
  prior pointers must not be used.
- Debug builds may quarantine recently freed heap blocks briefly for
  double-free detection; still treat them as invalid for the application.

## Thread-safety

**None** beyond the underlying C library allocator for the raw heap path.
Fail-injection counters, debug lists, arena, and pool are single-threaded.
Future `RoboThread` may add locks; do not assume them today.

## Debugging

| Switch | Meaning |
|---|---|
| `-DROBO_MEM_DEBUG=1` | Compile-time bookkeeping (Debug `Build.ps1`) |
| `-DROBO_MEM_DEBUG=0` | Release: stubs, no guard/list cost |
| `robo_mem_debug_enable/disable` | Runtime gate when compiled in |

Layout when tracking:

```text
[pad][header][front guard 0xAB×16][user][back guard]
```

Default fault: log + `abort`. Tests may install a handler.

**RoboMem vs sanitizers:** RoboMem detects leaks (report), double/invalid free,
and guard corruption at free/check. AddressSanitizer catches immediate OOB/UAF.
Prefer both in development; do not rely on RoboMem as a full UAF detector.

## Performance

- Release heap path is thin (direct `malloc`/`free`).
- Debug adds header, guards, list/stats — use only while developing.
- Arenas: O(1) bump; excellent for parsers / short scopes.
- Pools: O(1) alloc/free; fixed capacity — size pools for peak live objects.

## Known limitations

- Not thread-safe.
- No garbage collector / refcounting / smart pointers.
- Debug does not catch use-after-free on access (only poison on free).
- Arena `bytes_remaining` ignores upcoming alignment padding.
- Quarantine of freed debug blocks is bounded (oldest reclaimed).
- No custom page/mmap allocator yet.

## Tests

Suites: `SuiteRoboMem`, `SuiteRoboMemDebug`, `SuiteRoboMemSystem`,
`SuiteRoboArena`, `SuiteRoboPool`, plus RoboBase suites in the same runner.

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test
.\runtime\Tools\Build.ps1 -Configuration Release -Verify -Test
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Sanitize
```

`-Sanitize` enables AddressSanitizer and/or UndefinedBehaviorSanitizer when
the host clang supports them.

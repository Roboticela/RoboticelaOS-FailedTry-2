# RoboDebug / RoboAssert

Part of RoboBase (Goal 1.4). Assertions, panic, and debug break.

## Build configurations

| Configuration | Compiler define | `ROBO_DEBUG_ASSERT` | Typical flags |
|---|---|---|---|
| **Debug** | `-DROBO_DEBUG=1` | Enabled (same as `ROBO_ASSERT`) | `-O0 -g` |
| **Release** | `-DROBO_DEBUG=0` (default) | Compiled out; condition **not** evaluated | `-O2` |

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Test
.\runtime\Tools\Build.ps1 -Configuration Release -Test
```

## API

| Macro / function | Debug | Release |
|---|---|---|
| `ROBO_ASSERT(cond)` | Always checked; failure → log + panic | Same |
| `ROBO_DEBUG_ASSERT(cond)` | Checked | No-op (no side effects in `cond`) |
| `ROBO_UNREACHABLE()` | Panic | Panic |
| `robo_panic(...)` | Log + panic handler | Same |
| `robo_debug_break()` | Logs a warning by default; hardware trap only if `ROBO_DEBUG_TRAP=1` | Same |
| `robo_debug_enabled()` | Returns 1 | Returns 0 |

Default panic handler logs with RoboLog (`ERROR`) and calls `abort()`.
Tests may install `robo_set_panic_handler` to avoid aborting the process.

## How to use

- Use **`ROBO_ASSERT`** for invariants that must hold in production (corrupt state is worse than crash).
- Use **`ROBO_DEBUG_ASSERT`** for expensive or development-only checks. Never put required side effects in the condition.
- Use **`robo_panic`** when the process cannot continue (after logging context).
- Use **`ROBO_UNREACHABLE`** for switch defaults / impossible branches.
- Prefer returning `robo_result_t` (`RoboError`) to callers for recoverable failures; assertions are for programmer errors.

## Behavior notes

- Assertion failure logs: `ASSERT failed: <expr> (Error)` then panics.
- No undefined behavior in `robo_debug_break` when a trap instruction is unavailable.
- Not a substitute for RoboError in public APIs.

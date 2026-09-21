# RoboDebug / RoboAssert

Part of RoboBase (Goal 1.4). Assertions, panic, and debug break.

## Build configurations

| Configuration | Compiler define | `RoboDebugAssert` | Typical flags |
|---|---|---|---|
| **Debug** | `-DRoboDebug=1` | Enabled (same as `RoboAssert`) | `-O0 -g` |
| **Release** | `-DRoboDebug=0` (default) | Compiled out; condition **not** evaluated | `-O2` |

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Test
.\runtime\Tools\Build.ps1 -Configuration Release -Test
```

## API

| Macro / function | Debug | Release |
|---|---|---|
| `RoboAssert(cond)` | Always checked; failure → log + panic | Same |
| `RoboDebugAssert(cond)` | Checked | No-op (no side effects in `cond`) |
| `RoboUnreachable()` | Panic | Panic |
| `Panic(...)` | Log + panic handler | Same |
| `RoboDebugBreak()` | Logs a warning by default; hardware trap only if `RoboDebugTrap=1` | Same |
| `RoboDebugEnabled()` | Returns 1 | Returns 0 |

Default panic handler logs with RoboLog (`ERROR`) and calls `abort()`.
Tests may install `SetPanicHandler` to avoid aborting the process.

## How to use

- Use **`RoboAssert`** for invariants that must hold in production (corrupt state is worse than crash).
- Use **`RoboDebugAssert`** for expensive or development-only checks. Never put required side effects in the condition.
- Use **`Panic`** when the process cannot continue (after logging context).
- Use **`RoboUnreachable`** for switch defaults / impossible branches.
- Prefer returning `Result` (`RoboError`) to callers for recoverable failures; assertions are for programmer errors.

## Behavior notes

- Assertion failure logs: `ASSERT failed: <expr> (Error)` then panics.
- No undefined behavior in `RoboDebugBreak` when a trap instruction is unavailable.
- Not a substitute for RoboError in public APIs.

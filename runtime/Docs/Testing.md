# Testing Roboticela Runtime

Goal 1.5 introduces **RoboTest**, a small C test framework under `Tests/`.
Production code under `Include/` and `Source/` must never include RoboTest.

## Layout

```text
Tests/
├── Framework/
│   ├── RoboTest.h      # registration, runner, ROBO_CHECK*
│   └── RoboTest.c
├── Suites/
│   ├── SuiteRoboBase.c
│   ├── SuiteRoboLog.c
│   ├── SuiteRoboError.c
│   ├── SuiteRoboDebug.c
│   ├── SuiteRoboMem.c
│   ├── SuiteRoboMemDebug.c
│   ├── SuiteRoboMemSystem.c
│   ├── SuiteRoboArena.c
│   ├── SuiteRoboPool.c
│   ├── SuiteRoboArray.c
│   ├── SuiteRoboString.c
│   ├── SuiteRoboUnicode.c
│   └── SuiteRoboUnicodeProps.c
└── Main.c              # registers suites + robo_test_main
```

Build products:

| Artifact | Role |
|---|---|
| `Out/lib/*.o` + `librobobase.a` | Production library (RoboBase + RoboMem) |
| `Out/RoboticelaRuntime.elf` | Production guest entry |
| `Out/RoboTest.host.exe` | Host test executable only |

## Commands

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test
.\runtime\Tools\Build.ps1 -Configuration Release -Verify -Test
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter base -VerboseTests
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Sanitize   # if ASan available
```

Direct runner options:

```text
RoboTest.host.exe
RoboTest.host.exe --filter=log
RoboTest.host.exe --verbose
RoboTest.host.exe --list
RoboTest.host.exe --child-panic              # expect non-zero (abort)
RoboTest.host.exe --child-assert             # expect non-zero (abort)
RoboTest.host.exe --child-mem-double-free    # Debug / ROBO_MEM_DEBUG=1
RoboTest.host.exe --child-mem-invalid-free
RoboTest.host.exe --child-mem-underflow
RoboTest.host.exe --child-mem-overflow
```

In-process memdbg tests install a non-aborting fault handler. Child modes verify default abort behavior without UB in the main suite process.

## Adding a new test (future modules)

1. Create `Tests/Suites/SuiteRoboMem.c` (example).
2. Implement `void robo_test_register_mem(void)` that calls `robo_test_register("mem.arena", test_fn)`.
3. Use `ROBO_CHECK` / `ROBO_CHECK_EQ_INT` / `ROBO_CHECK_STR_EQ`.
4. Call `robo_test_register_mem()` from `Tests/Main.c`.
5. Add the suite `.c` to `$SuiteSrcs` in `Tools/Build.ps1`.
6. Rebuild and run.

Do not put test helpers into `Include/`. Keep production APIs free of test-only symbols.

## Output shape

```text
Roboticela Runtime Tests

[PASS] base.version
[PASS] base.init_shutdown
...

Tests: N
Passed: N
Failed: 0

ALL TESTS PASSED
```

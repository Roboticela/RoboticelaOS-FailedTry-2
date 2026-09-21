# RoboBase

Foundational C library for Roboticela Runtime (Goals 1.1–1.5).

## Modules

| Module | Header | Role |
|---|---|---|
| Lifecycle / version | `Include/RoboBase/RoboBase.h` | `BaseInit` / `shutdown` / `version` |
| Logging | `Include/RoboBase/RoboLog.h` | INFO / WARN / ERROR / DEBUG to stderr |
| Errors | `Include/RoboBase/RoboError.h` | `Result` + `ErrorString` |
| Assert / debug | `Include/RoboBase/RoboDebug.h` | `RoboAssert`, panic, debug asserts |

Production sources live under `Source/RoboBase/`. Guest objects archive to `Out/lib/librobobase.a`.

## Build configurations

| Config | Flag | Notes |
|---|---|---|
| Debug | `-DRoboDebug=1` | `RoboDebugAssert` enabled; `-O0 -g` |
| Release | `-DRoboDebug=0` | `RoboDebugAssert` compiled out; `-O2` |

See also `Docs/RoboDebug.md` and `Docs/Testing.md`.

## Usage sketch

```c
#include "RoboBase/RoboBase.h"
#include "RoboBase/RoboLog.h"
#include "RoboBase/RoboError.h"
#include "RoboBase/RoboDebug.h"

Result st = BaseInit();
RoboAssert(st == RoboOk);
LogInfo("ready %s", BaseVersion());
BaseShutdown();
```

## Status (Goal 1.5)

RoboBase foundation is complete and covered by the unified `RoboTest` suite.
Next phase is RoboMem (not started).

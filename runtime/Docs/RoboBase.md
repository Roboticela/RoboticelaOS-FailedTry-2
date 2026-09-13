# RoboBase

Foundational C library for Roboticela Runtime (Goals 1.1–1.5).

## Modules

| Module | Header | Role |
|---|---|---|
| Lifecycle / version | `Include/RoboBase/RoboBase.h` | `robo_base_init` / `shutdown` / `version` |
| Logging | `Include/RoboBase/RoboLog.h` | INFO / WARN / ERROR / DEBUG to stderr |
| Errors | `Include/RoboBase/RoboError.h` | `robo_result_t` + `robo_error_string` |
| Assert / debug | `Include/RoboBase/RoboDebug.h` | `ROBO_ASSERT`, panic, debug asserts |

Production sources live under `Source/RoboBase/`. Guest objects archive to `Out/lib/librobobase.a`.

## Build configurations

| Config | Flag | Notes |
|---|---|---|
| Debug | `-DROBO_DEBUG=1` | `ROBO_DEBUG_ASSERT` enabled; `-O0 -g` |
| Release | `-DROBO_DEBUG=0` | `ROBO_DEBUG_ASSERT` compiled out; `-O2` |

See also `Docs/RoboDebug.md` and `Docs/Testing.md`.

## Usage sketch

```c
#include "RoboBase/RoboBase.h"
#include "RoboBase/RoboLog.h"
#include "RoboBase/RoboError.h"
#include "RoboBase/RoboDebug.h"

robo_result_t st = robo_base_init();
ROBO_ASSERT(st == ROBO_OK);
robo_log_info("ready %s", robo_base_version());
robo_base_shutdown();
```

## Status (Goal 1.5)

RoboBase foundation is complete and covered by the unified `RoboTest` suite.
Next phase is RoboMem (not started).

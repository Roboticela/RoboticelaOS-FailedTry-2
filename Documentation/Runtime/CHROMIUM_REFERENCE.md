# Chromium Reference Policy (Goal 0.2)

## Status

Chromium is **frozen as temporary technical reference material only**.

Roboticela Runtime is **not** a Chromium fork, overlay, or rename.
The final Roboticela Runtime must build and run as part of Roboticela OS with
`Chromium/` absent.

## Frozen pin

| Field | Value |
|---|---|
| Checkout path | `Chromium/` (depot_tools layout) |
| Source tree | `Chromium/src` |
| Chrome version | `155.0.8055.0` (`MAJOR.MINOR.BUILD.PATCH`) |
| Git commit | `a2bee684f3c4224a8836957b917c167d9bb9a349` |
| Commit subject | Roll Chrome Mac Arm PGO Profile |
| Commit date | 2026-09-12 |
| Upstream | `https://chromium.googlesource.com/chromium/src.git` |
| Recorded for Goals | 0.1 / 0.2 on 2026-09-13 |

Do not modify Chromium source for Roboticela Runtime work.
If the pin changes later, update this file in the same commit that records the new checkout.

## What Chromium is allowed for

Use Chromium only to investigate:

- required behavior
- algorithms and data structures at a conceptual level
- subsystem responsibilities
- standards implementation patterns
- architectural ideas worth adopting or rejecting
- dependency relationships between browser subsystems
- test ideas and specifications where legally appropriate

## What is forbidden

The Roboticela Runtime must **not**:

- depend on Chromium source files
- include Chromium headers
- link Chromium libraries or object files
- consume Chromium build files (GN/Ninja targets, `.gclient` solutions) as part of the runtime build
- ship or require Chromium binaries
- copy Chromium trees wholesale into `runtime/`
- mechanically rename Chromium identifiers into “Robo*” names
- mechanically convert Chromium C++ into C

## Reuse and licensing

Before any third-party or Chromium-derived source is actually placed under `runtime/`:

1. Identify the exact files and license(s).
2. Confirm redistribution and modification terms allow Roboticela’s use.
3. Record attribution requirements.
4. Prefer independent C implementations and properly licensed standalone libraries over Chromium code.
5. Do not silently copy source.

Until that review is complete, treat Chromium as read-only reference.

## Intended engineering workflow

```text
Chromium behavior / architecture (reference only)
        ↓
Understand the real requirement for Roboticela
        ↓
Design a Roboticela-specific C API
        ↓
Implement independently in C under runtime/
        ↓
Test independently on Roboticela OS (guest ELF / initrd)
```

## Runtime product shape

Roboticela Runtime is a **guest OS component**, not a Windows host program.

- Build product: freestanding **ELF64** for Roboticela userland (`x86_64-unknown-none`)
- Links against Roboticela `libc` / SDK when needed
- Runs on Roboticela OS (boot → initrd / system paths), not as `*.exe` on Windows
- Chromium paths must never appear on the compile or link command line

## Build independence check (Goal 0.2)

```powershell
.\runtime\Tools\Build.ps1 -Verify
```

Expected:

- compiles `runtime/Source/Main.c` only from `runtime/` + Roboticela `libc` / `sdk` / `user` linker script
- writes `runtime/Out/RoboticelaRuntime.elf`
- reports ELF64 / no host libc leakage
- reports **no Chromium include or link paths**

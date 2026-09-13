# Roboticela Runtime

Independent C runtime for **Roboticela OS**. See `ARCHITECTURE.md`, `DEPENDENCIES.md`, `DESIGN_RULES.md`, `CHROMIUM_REFERENCE.md`.

## RoboBase foundation (Goals 1.1–1.5) — complete

## RoboMem (Goals 2.1–2.5) — complete

Heap, arena, pool, and optional debug allocator. Guide: [`memory/README.md`](memory/README.md).

## RoboContainers (Goal 3.1–3.2) + RoboUnicode (Goal 3.3–3.4)

`RoboArray`, `RoboString`, Unicode encoding + **UCD properties**.  
Guides: [`containers/README.md`](containers/README.md), [`unicode/README.md`](unicode/README.md).  
Regenerate UCD tables: [`Tools/ucd/README.md`](Tools/ucd/README.md).

## Build

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test
.\runtime\Tools\Build.ps1 -Configuration Release -Verify -Test
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Sanitize
```

| Output | Role |
|---|---|
| `Out/lib/librobobase.a` | Production archive (Base + Mem + Containers) |
| `Out/RoboticelaRuntime.elf` | Guest OS entry |
| `Out/RoboTest.host.exe` | Unified host test suite |

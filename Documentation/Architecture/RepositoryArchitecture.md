# Repository architecture

RoboticelaOS is an independently implemented Chromium-class OS and runtime. This document describes the tree after the Phase A restructure.

## Top level

```text
RoboticelaOS/
├── Bootloader/          UEFI bootloader
├── Kernel/              kernel (include paths mm/, fs/, … are internal)
├── Runtime/             web/UI runtime (one runtime, not a host fork)
│   ├── Include/         public C headers (the ABI)
│   ├── Source/          RoboBase, RoboMem, … RoboDesktop, RoboInput
│   └── Tools/           Runtime/Tools/Build.ps1
├── Libc/                guest libc
├── User/                guest programs + linker.ld
├── Tests/               tests by subsystem
├── Documentation/       all project docs
├── Scripts/
│   ├── Build/
│   ├── Test/
│   ├── Qemu/
│   ├── Utilities/
│   └── pipeline/        robo.ps1 engine
├── Build/               generated output only
├── Tools/
├── Sdk/
└── AGENTS.md
```

## Source ownership

| Subsystem | Public | Private |
|---|---|---|
| Runtime modules | `Runtime/Include/Robo*/` | `Runtime/Source/Robo*/` |
| Bootloader | `Bootloader/Include/` | `Bootloader/Source/` |
| Kernel | `Kernel/include/` plus subsystem folders | same tree; do not include from Runtime |

## Tests

`Tests/Html`, `Tests/Css`, `Tests/Layout`, `Tests/Paint`, `Tests/Rasterizer`, `Tests/Desktop`, `Tests/Graphics`, `Tests/Window`, `Tests/Display`, `Tests/Input`, `Tests/Font`, `Tests/Runtime` (framework + core suites), `Tests/Bootloader`, `Tests/Kernel`, `Tests/Libc`.

## Documentation

`Documentation/Architecture`, `Documentation/Build`, `Documentation/Testing`, `Documentation/Runtime/<Module>`, `Documentation/Kernel`, `Documentation/Development`.

Subsystem `README.md` files under `Runtime/` are pointers, not a second manual.

## Generated artifacts

Everything compiled or packed lives under `Build/`:

```text
Build/
├── Host/            (reserved)
├── Qemu/            (reserved)
├── Bootloader/      EFI, host preview, ESP image
├── Kernel/          kernel.elf, kernel host tests
├── Libc/            libc.a, crt objects
├── Runtime/         guest ELF, RoboTest.host.exe, obj/
├── artifacts/
├── images/
└── logs/
```

## Dependency graph

See `AGENTS.md`. Runtime pipeline:

```text
Html + Css → Layout → Paint → Display list → Rasterizer → Graphics → Window → Desktop
Input → Window / Desktop
Kernel → native OS → guest programs that host Runtime
```

No circular dependencies. No Chromium in the compile or link line.

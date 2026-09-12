# RoboRuntime / RoboJS approach

## Do not mass-rename Chromium

Renaming `v8::` → `robojs::` or Blink classes across the tree breaks every
upstream sync. Keep:

```text
runtime/vendor/v8|skia|chromium   pristine upstream
runtime/robojs|robohtml|...       your names, APIs, and optimizations
```

Optimizations belong in **build flags**, **feature disables** (no Chrome UI,
no sync, no PDF, …), and **your embedder** — not a global find-replace.

## What to clone (best order)

| Product name | Upstream | Command | Disk |
|--------------|----------|---------|------|
| Tooling | depot_tools | `.\scripts\clone-engines.ps1 depot-tools` | small |
| **RoboJS** | V8 | `.\scripts\clone-engines.ps1 v8 -NoHistory` | ~few GB |
| **RoboSkia** | Skia | `.\scripts\clone-engines.ps1 skia -NoHistory` | ~1–2 GB |
| **RoboHTML + RoboCSS** | Blink inside Chromium | `.\scripts\clone-engines.ps1 chromium -NoHistory` | **~100 GB** |

There is no supported “HTML+CSS only” Chromium tarball. Blink needs the Chromium
`src` tree; you simply **never build** `chrome` / Chrome UI — target
`content_shell` or a Roboticela embedder later.

## Guest C / C++ (inside RoboticelaOS)

Host build tools stay on your PC (system clang/python for building the OS).
Inside the OS, C and C++ are managed by **`robo`**:

```text
robo install llvm@18.1.8
robo list
robo use llvm 18.1.8
clang --version
clang++ --version
```

The interactive program is **`terminal`** (`/bin/terminal`), not a shell binary.

## Clone engines

```powershell
cd D:\Muhammad\RoboticelaOS
.\scripts\clone-engines.ps1 all-small -NoHistory
.\scripts\clone-engines.ps1 chromium -NoHistory   # when ready (~100GB)
```

# Guest packages and terminal

## `robo` package manager

Guest CLI at `/bin/robo`. **C and C++ only** (package id `llvm`).

```text
robo install llvm@18.1.8
robo install c@18.1.8
robo install cxx@18.1.8
robo list
robo use llvm 18.1.8
robo uninstall llvm@18.1.8
robo driver status
```

Layout:

```text
/System/robo/catalog/llvm/<ver>/bin/clang
/System/robo/catalog/llvm/<ver>/bin/clang++
/System/toolchains/llvm/<ver>/bin/clang
/System/toolchains/llvm/<ver>/bin/clang++
/System/toolchains/defaults          # llvm=<ver>
/bin/clang   /bin/clang++            # version shims
```

`clang` / `clang++` today are **RoboClang stubs** (`--version` works; full LLVM
codegen is a later port). Multi-version install/use paths are real.

## `terminal`

`/bin/terminal` and `/System/services/terminal` — interactive line editor on
the PS/2 keyboard (via display INPUT). Started by init on normal boot.

Type `help`, `robo list`, `clang --version`, `exit`.

On normal boot the graphical program is **the desktop** (`/System/runtime/robo-runtime`,
see [desktop-runtime.md](desktop-runtime.md)): it claims the display and draws the shell
with one document window. `terminal` is a program you can run from that shell's stack of
tools rather than a service init starts. Compositor / window-server run in selftest mode
only, so they do not take the display from the desktop.

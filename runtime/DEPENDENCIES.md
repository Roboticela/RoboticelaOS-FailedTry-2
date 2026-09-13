# Roboticela Runtime — Allowed Dependencies

This file is the normative dependency graph for `runtime/`.
If code and this document disagree, fix the code or update this document in the same change.

## Direction (high → low)

Arrows mean **“may depend on”** (importer → imported).

```text
Applications
    → RoboShell
        → RoboAPI
            → RoboJS
            → RoboNet / RoboStorage / RoboSecurity
                → RoboCompositor
                    → RoboRender
                        → RoboPaint
                            → RoboLayout
                                → RoboHTML / RoboCSS
                                    → RoboDOM
                                        → RoboWindow
                                            → RoboGraphics
                                                → RoboPlatform
                                                    → RoboIPC
                                                        → RoboEvent
                                                            → RoboThread
                                                                → RoboContainers
                                                                    → RoboMem
                                                                        → RoboBase
                                                                            → Roboticela libc / SDK
```

Same hierarchy as a stack (bottom is foundational):

```text
RoboBase
  ↑
RoboMem
  ↑
RoboContainers
  ↑
RoboThread
  ↑
RoboEvent
  ↑
RoboIPC
  ↑
RoboPlatform
  ↑
RoboGraphics
  ↑
RoboWindow
  ↑
RoboDOM
  ↑
RoboHTML / RoboCSS
  ↑
RoboLayout
  ↑
RoboPaint
  ↑
RoboRender
  ↑
RoboCompositor
  ↑
RoboJS
  ↑
RoboAPI
  ↑
RoboNet / RoboStorage / RoboSecurity
  ↑
RoboShell
  ↑
Applications
```

## Peer rules

| Peers | Rule |
|---|---|
| RoboHTML and RoboCSS | May both depend on RoboDOM. Must not depend on each other. |
| RoboNet, RoboStorage, RoboSecurity | May depend on RoboPlatform / RoboIPC / RoboEvent / RoboAPI facades as documented. Must not depend on layout/paint/JS engines. |
| RoboGraphics and RoboWindow | Window may use Graphics. Graphics must not depend on Window. |
| RoboI18N | May depend on RoboBase, RoboMem, RoboContainers, RoboUnicode. Must not depend on HTML/CSS/DOM/Layout/Graphics/Render/Window/Shell. Unicode and higher UI must not depend on RoboI18N until a documented exception. |

## Forbidden edges

- Any lower module → any higher module (no upward includes/links).
- Any `runtime/` translation unit → Chromium headers, sources, libraries, or build outputs.
- RoboDOM / RoboLayout / RoboPaint → RoboJS (JS binds downward; engines do not call JS).
- RoboBase / RoboMem / RoboContainers → RoboPlatform (keep core portable; platform stays behind RoboPlatform).
- Circular dependencies without an explicit, reviewed exception recorded here.

## Chromium

Chromium is **not** a node in this graph.
See `CHROMIUM_REFERENCE.md`.

## Milestone independence

Each subsystem milestone should produce a library or test binary that links only its allowed dependencies.
The skeleton `RoboticelaRuntime.elf` today depends only on Roboticela libc/SDK and `Include/RoboticelaRuntime.h`.

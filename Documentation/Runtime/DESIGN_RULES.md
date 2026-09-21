# Roboticela Runtime — Design Rules

Permanent engineering rules for `runtime/`. These apply to every milestone.

## Language and independence

1. **Core implementation is C.** Do not introduce C++ implementations in the runtime core.
2. **No Chromium dependency in the final runtime.** The tree must eventually build with `Chromium/` deleted.
3. **No Chromium headers** included by runtime code.
4. **No Chromium libraries** linked by runtime code.
5. **No mechanical C++ → C translation** of Chromium sources.
6. **No copying Chromium architecture blindly.** Study behavior; design Roboticela’s own structure.
7. **Chromium may only be used** to understand behavior, standards, algorithms, and subsystem responsibilities.
8. **Roboticela APIs must be designed independently** — clear ownership, names, and contracts — not renamed Chromium APIs.

## Dependency discipline

9. **Lower-level modules must never depend on higher-level modules.** Follow `DEPENDENCIES.md`.
10. **Platform-specific code must stay behind RoboPlatform abstractions.** No scattered OS `#ifdef` in DOM/layout/JS core.
11. **Avoid global state** unless explicitly justified in the subsystem design notes (and preferred injectable/context-based).

## Quality bar

12. **Every subsystem must have tests.**
13. **Every milestone must compile independently whenever practical.**
14. **Public APIs should be documented** (header comments + architecture notes as the surface grows).
15. **Document memory ownership** at every public allocate/free boundary.
16. Prefer simple, maintainable C over unnecessary abstraction.

## Product scope

17. This runtime is an **application/desktop platform** for Roboticela OS, not a Chrome-like browser UI.
18. Build artifacts are **Roboticela guest ELF** components, not Windows host executables.
19. Before reusing any third-party or Chromium-derived source: license and attribution review (`CHROMIUM_REFERENCE.md`).

## Workflow for each feature

```text
Understand requirement (optionally via Chromium reference)
    → Design Roboticela C API
    → Implement under runtime/
    → Test independently on Roboticela OS
    → Only then proceed to the next milestone
```

## Change control

- Updating these rules is allowed when the architecture requires it; record the reason in `PROGRESS.md`.
- Do not weaken Chromium-independence rules.

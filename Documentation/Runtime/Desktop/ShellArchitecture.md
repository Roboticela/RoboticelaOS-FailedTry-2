# Desktop shell architecture (Phase 14.1)

**C = behavior. HTML/CSS = design. Roboticela React package = authoring + JS→C bridge.**

This is the product law for the Beautiful Desktop Shell and every app that rides it.
`runtime-plan.mdc` (Application & UI doctrine) is the standing rule; this document is the
Desktop module’s concrete shape.

## 1. Layers

```text
@roboticela/react  (host)     author components, tokens, data-command helpers
        │ SSR / export
        ▼
HTML + CSS                    window *content* only (no nested dock / menu / WM)
        │ RoboHTML → CSS → Layout → Paint
        ▼
RoboDesktop chrome (C)        wallpaper plane, panel/menu bar, title bars, dock hit targets
RoboInput + Compositor (C)    pointer motion, focus, raise, present
        ▲
RoboJS (thin) ──data-last-command──► DesktopBridge (C allowlist)
```

| Surface | Owner | Notes |
|---|---|---|
| Menu bar / panel | `RoboDesktopChrome` (C) | Not an HTML fake desk |
| Dock / launcher tiles | C hit targets; launch = bridge verb | Icons may be bitmaps; hit-testing stays C |
| Window frames / drag / resize | C (Phase 13 foundations) | Never driven by RoboJS per mousemove |
| App bodies (Settings, Overview, Lab) | HTML/CSS from React export | Content-only trees |
| Click → OS action | `data-command` → page script → `data-last-command` → `DesktopBridge` | Allowlist only |

## 2. DesktopBridge

`RoboDesktopBridge` is the Phase 14.5 door: a **named verb table** in C. Page script never
calls WM APIs. After `DesktopScriptPump`, the scene calls
`DesktopBridgeConsumeDocument` on the focused app’s document; an allowlisted verb updates
status / raises windows / opens content; an unknown verb is **refused** (counted), not
improvised.

Security (14.6): no host filesystem, network, or process spawn from a page verb. Grow the
table deliberately; Phase 22 extends the same pattern into RoboOS Native API.

## 3. Roboticela React package

`@roboticela/react` lives under `Desktop/packages/roboticela-react/`. It:

- exports `commandProps(verb)` / `CommandButton` so authors stamp `data-command` correctly;
- mirrors the allowlist names used by C (keep both lists in sync when adding a verb);
- is **host-only** — Vite lab and SSR. The guest never loads React.

## 4. What not to build

- A guest React reconciler / VDOM
- A second JavaScript engine
- Nested wallpaper + dock + traffic lights inside exported markup
- Unrestricted FFI from RoboJS into C

## 5. Chromium lesson (reference only)

Aura/Views-class chrome and the compositor stay native; content is a document pipeline.
Never add Blink, V8, or Skia.

## 6. Related

- Inventory: `Documentation/Runtime/Desktop/Architecture.md`
- UI export: `Documentation/Desktop/Ui.md`
- Input join: `Documentation/Runtime/Desktop/InputIntegration.md`
- RoboJS shell gate: `Documentation/RoboJS/Integration.md` §4

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
| RoboHTML and RoboCSS | May both depend on RoboDOM. Must not depend on each other. Whoever needs both sits above both — RoboLayout is that module (rows below). |
| RoboNet, RoboStorage, RoboSecurity | May depend on RoboPlatform / RoboIPC / RoboEvent / RoboAPI facades as documented. Must not depend on layout/paint/JS engines. |
| RoboGraphics and RoboWindow | Window may use Graphics. Graphics must not depend on Window. |
| RoboLayout, and RoboHTML / RoboCSS | Layout is the **first module that may depend on both peers at once**, because a layout pass needs the element tree and each element's computed style and the peers must not know each other (see below). It may use RoboGraphics for geometry **only**. It must not depend on RoboWindow or RoboDisplay — both sit *below* it, so no layering argument catches a violation, and the rule is there because a layout pass that consulted a window could not be tested without one: the viewport is an argument to a pass, never a query. |
| RoboPaint, and RoboLayout / RoboFont / RoboRender | Paint sits **directly above RoboLayout and directly below RoboRender**: it consumes layout's completed geometry (the result, the two inset records, the overflow record, a text run with its baseline and its resolved font inputs) through `const` read doors, plus a **text layout style** it carries by value, and produces an ordered operation list and, above it, a render-ready display list that resolves the box model into one rectangle per command (11.3), and it derives nothing layout already concluded. It may use RoboGraphics for geometry **only** — there are no new coordinate types — and, since 11.4–11.6, the **CPU raster, the surface and the face it is handed** when it fills backgrounds and border bands and strokes a run's glyphs into one (`PaintDecorationPaint`); drawing into a surface and a font its caller owns is not a dependency on the module that presents it. The one font door it calls beyond RoboGraphics' own bridge is `FontFaceParseCmap`, and it calls it for exactly the reason the layout measurer does: a face is created without a character map, so the layer that resolves a glyph must ask for one. Since 11.8 the boundary a drawing is bounded by is read from the **scope the display list states** — a clip stack the pass owns — and applied once through `GfxRasterSetClip`; that is still RoboGraphics' own rectangular clip, not a second clipping system, and no clip it applies needs anything but the raster and the surface it was already handed. Since 11.9 a drawing also has a **strength**: an element's `opacity` is accumulated down the tree by the traversal and applied once per drawing through `GfxRasterSetOpacity`, which is RoboGraphics' own *global* opacity — a multiplier the graphics module keeps beside its blend, so a `rgba(…, 0.5)` fill at `opacity: 0.5` composites at a quarter without either module doing the other's arithmetic. That is the whole of the dependency: one more raster setter and one more computed value read, no new type, no offscreen layer and no compositor. Since 11.10 the module also answers the **inverse** question — a point in paint coordinates, the node drawn there out — and that adds **no dependency at all**: it walks the same tree through the same `const` doors, reads the boundary and the strength from the same two records it already owns, allocates nothing, and touches no surface. It is worth stating because the reflex for a new facility is a new module, and a module beside RoboPaint would have had to either duplicate `PaintClip` and `PaintOpacity` or depend on paint for two types and reach past it for the geometry — two boundaries where there is one. What a hit test does **not** bring with it is event dispatch: it names one node, and the layer that walks up to the nearest element that could listen is Phase 12's, above RoboPaint rather than inside it. Since 11.11 the module also answers **which regions a change made stale**, and that adds **no dependency either** — it compares two artifacts it already produces (two record lists, two display lists) or reads a pass record, its only allocation is the set it owns, and the rectangle in a region is geometry re-exported by layout while the boundary and the strength in it are the two records the module already carries. It executes nothing, so it needs no surface for that either. It must not depend on RoboWindow, RoboDisplay or RoboRender: a paint pass that consulted a surface could not be tested without one, so a surface is always the **caller's argument, never a query**. It must not write layout state, which is why the pass takes the tree by `const` pointer and allocates nothing but its own list. It must not depend on RoboJS (engines do not call JS). |
| RoboDesktop, and RoboRender / RoboWindow / RoboPaint / RoboDesktopDocument | Desktop sits **directly above RoboRender** (a target and the presenter) and **below RoboCompositor**, and it is the first module that owns a *screen* rather than a page — which is why its dependency rule is stated as three halves rather than one. It **reads** RoboWindow (the native window it presents into), RoboFont (a face it is handed), RoboGraphics (fills, lines, the composite and the glyph bridge) and RoboPaint (the display list a shell window borrows, read through `const`). It **must not** name a document layer *except in one file*: `Source/RoboDesktop/RoboDesktopDocument.c` and `Include/RoboDesktop/RoboDesktopDocument.h` are the markup bridge, and `Assert-RoboDesktopBoundaries` in `Tools/Build.ps1` exempts them **by name** so that the exemption is visible in the build's own output — the shell must stay drawable when a document is broken, empty or not compiled in, which is exactly what the shell's capability record says when it reports `documents == 0` beside `RoboDesktopDocumentsImplemented = 1`. It must not depend on RoboCompositor (a layer tree is the next module up), on RoboJS/RoboShell/the API layer, or on Chromium, and it must not reach into RoboDisplay: a display framebuffer arrives as a RoboGraphics framebuffer through `RenderPresentFramebuffer`, one door over, which is the same rule RoboRender states for itself. Its ten absences live in its **capability table** (`compositor`, `tiles`, `damage_tracking`, `frame_scheduling`, `vsync`, `gpu`, `window_manager`, `input_delivery`, `desktop_environment`, and `documents` for the shell), each naming what it would need. |
| RoboRender, and RoboPaint / RoboGraphics / RoboFont / RoboDesktop | Render sits **directly above RoboPaint and directly below RoboDesktop and RoboCompositor**: its input contract is a **display list** (`PaintDisplayList`) and the geometry that list carries, so it reads RoboPaint through `const` doors and **names no document layer at all** — no RoboHTML, no RoboCSS, no layout tree, no computed style. `Assert-RoboRenderBoundaries` enforces that for the module's own headers and sources, and holds the suites and documents to the upward half of the rule instead (a render fixture *is* a paint fixture plus a target, so it builds a document on purpose). It needs RoboGraphics for the surface it owns (`GfxSurfaceCreate`/`_clear_rgba8`/`_get_pixel`/`_encode`), the byte view and the CPU raster the walk draws through, and RoboFont for a face it is **handed** and never resolves: a target, a surface and a face are always the *caller's* arguments, which is what makes the whole module testable without a window, a display or a font database. Since 11.12 it also reads a **paint invalidation set** (11.11) to bound a run, and it is the module that drove the one door 11.12 added below itself (`PaintDecorationPaintBounded`), because a second walk in this module would be a second answer to "what does this command do". Since **11.13** it also owns a **presentation** door (`RoboRenderPresenter.h`), which composites one frame into one destination **surface** at one offset with a record whose two pixel counts sum to the frame's own — and that door is why the two rules below still hold. It must not depend on RoboWindow, RoboDisplay or RoboCompositor: taking a *window id* or a QEMU mapping would make it reach a display module and be untestable without one, so the destination is always a RoboGraphics surface the caller owns, and which surface a frame goes to — and when — is the caller's (`RoboDesktop` is the caller in this build). It must not depend on RoboJS, RoboShell or the API layer, and it must not name Chromium. Its **capability table** is where the five absences live (`compositor`, `tiles`, `damage_tracking`, `frame_scheduling`, `gpu`), each naming the phase that would make it a 1; `presentation` was the sixth and is now **1**. |
| RoboI18N | May depend on RoboBase, RoboMem, RoboContainers, RoboUnicode. Must not depend on HTML/CSS/DOM/Layout/Graphics/Render/Window/Shell. Unicode and higher UI must not depend on RoboI18N until a documented exception. Date/time formatting, intervals, relative time, measurement unit formatting/conversion, calendar arithmetic, collation, display names, timezone display/GMT formatting, application message catalogs, RoboMsgFmt formatting, message-format validation, and IANA tzdb timezone conversion use integer/UCA/CLDR/tzdb tables from Unicode UCA 16.0.0, CLDR 46, and IANA tzdb 2026c plus RoboMsg catalogs; duration (fixed ms) is distinct from period (calendar units); collation is distinct from normalization/casefold/binary compare; display names are distinct from locale identifiers; timezone *names* are CLDR presentation data distinct from tzdb *rules*; unit conversion is distinct from unit localization; application RoboMsg catalogs are distinct from CLDR; RoboMsgFmt interprets templates and delegates number/date/plural to existing RoboI18N APIs (not a second formatter stack); validation analyzes contracts without formatting; timezone conversion lives in RoboI18NTimeZone and display in RoboI18NDisplayNames (not formatters) and must not pull platform clocks into RoboBase. Goal 4.16 locale service (`RoboI18NLocaleService`) centralizes Unicode extensions and calendar/numbering/hour-cycle/collation/timezone selection without conflating locale with timezone. Goal 4.17 adds Accept-Language / language-range negotiation (`RoboI18NLocaleNegotiate`) separate from resource fallback. Goal 4.19 completes relative AUTO ±2 and adds `RoboI18NUnit`. Goal 4.21 adds currency metadata/formatting (`RoboI18NCurrency`); Goal 4.22 adds number/currency parsing (`RoboI18NParse`); Goal 4.23 adds date/time parsing (`RoboI18NDateTimeParse`) as a separate API from formatting; Goal 4.24 adds locale-aware search/matching (`RoboI18NSearch`) distinct from collation sorting and from transliteration; Goal 4.25 adds CLDR transliteration (`RoboI18NTranslit`) distinct from translation/search/collation. See `i18n/TZDB.md`, `i18n/TZDisplay.md`, `i18n/LocaleService.md`, `i18n/LocaleNegotiate.md`, `i18n/Units.md`, `i18n/Currency.md`, `i18n/Parse.md`, `i18n/DateTimeParse.md`, `i18n/Search.md`, and `i18n/Transliteration.md`. |

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

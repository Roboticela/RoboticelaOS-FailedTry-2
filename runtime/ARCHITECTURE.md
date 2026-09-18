# Roboticela Runtime Architecture

Independent C runtime for Roboticela OS applications and the desktop shell.

Chromium is temporary reference material only (`CHROMIUM_REFERENCE.md`).
Engineering rules: `DESIGN_RULES.md`. Allowed edges: `DEPENDENCIES.md`.

## Product shape

| Aspect | Decision |
|---|---|
| Language | C for core implementation |
| Host | Roboticela OS guest userland |
| Artifact | Freestanding ELF64 (`x86_64-unknown-none`) |
| Not | Windows `.exe`, Chromium fork, browser chrome UI |

Browser tabs, address bar, bookmarks, and Chrome-style browser windows are out of scope.
RoboHTML / RoboCSS / RoboJS / RoboDOM / RoboRender form a general application platform.

## Planned hierarchy

```text
RoboBase
  → RoboMem
  → RoboContainers
  → RoboThread
  → RoboEvent
  → RoboIPC
  → RoboPlatform
  → RoboGraphics
  → RoboWindow
  → RoboDOM
  → RoboHTML / RoboCSS
  → RoboLayout
  → RoboPaint
  → RoboRender
  → RoboCompositor
  → RoboJS
  → RoboAPI
  → RoboNet / RoboStorage / RoboSecurity
  → RoboShell
  → Applications
```

Lower modules never depend on higher modules.
Related services (RoboFont, RoboTLS, RoboMedia) attach beside the layer that needs them; they are not Stage 0 deliverables.

## Subsystems

For each subsystem: responsibility, allowed dependencies, modules that must not depend on it
(until a later documented exception), and the planned public C API boundary.

API names below are **planned boundaries**, not implemented headers yet (except the skeleton
`RoboticelaRuntime.h`).

---

### RoboBase

| | |
|---|---|
| **Responsibility** | Logging, errors, assertions, configuration, debugging helpers, small shared utilities. Foundation every other module can rely on. |
| **May depend on** | Roboticela `libc` / SDK only (no other Robo* subsystems). |
| **Must NOT depend on it** | Nothing in the runtime is forbidden from using RoboBase; it is the root. External apps may use it via higher APIs later. |
| **Public C API boundary** | Planned: `Include/RoboBase/RoboBase.h` — status codes, log macros/API, assert macros, config getters. |

---

### RoboMem

| | |
|---|---|
| **Responsibility** | Heap, arenas, pools, optional debug tracking (Phase 2 complete). |
| **May depend on** | RoboBase. |
| **Must NOT depend on it** | RoboBase. |
| **Public C API boundary** | `Include/RoboMem/RoboMemAll.h` (or `RoboMem.h` / `RoboArena.h` / `RoboPool.h` / `RoboMemDebug.h`). Guide: `memory/README.md`. |

---

### RoboContainers

| | |
|---|---|
| **Responsibility** | Dynamic arrays (RoboArray), UTF-8 strings (RoboString), and later maps/sets/queues/stacks/builders. |
| **May depend on** | RoboBase, RoboMem, RoboUnicode (string Unicode helpers). |
| **Must NOT depend on it** | RoboBase, RoboMem. |
| **Public C API boundary** | `Include/RoboContainers/RoboArray.h`, `RoboString.h`. Guide: `containers/README.md`. |

---

### RoboUnicode

| | |
|---|---|
| **Responsibility** | Unicode encoding (UTF-8/16/32), UCD properties, normalization, segmentation (grapheme/word/sentence/line), bidi, Script/SCX, emoji properties and RGI sequences; build-time data pipeline (Goal 3.14). |
| **May depend on** | RoboBase. |
| **Must NOT depend on it** | RoboBase, RoboMem (encode/decode are buffer-based; string helpers may use Mem via RoboString). |
| **Public C API boundary** | `Include/RoboUnicode/RoboUnicode.h` (+ Props/Norm/Grapheme/Case/Word/Sentence/Line/Bidi/Emoji). Version pin: `RoboUnicodeVersion.h` (**16.0.0** / Emoji **16.0**). Guide: `unicode/README.md`. Data pipeline: `Tools/ucd/` (generators are build-time only; not a runtime dependency). |

---

### RoboI18N

| | |
|---|---|
| **Responsibility** | Locale / language-tag foundation (BCP 47), CLDR-derived locale metadata, locale-aware decimal number formatting, CLDR plural categories, date/time/calendar/timezone foundations, localized date/time formatting (styles/patterns/skeletons), date/time intervals, relative time, measurement unit formatting/conversion, measurement systems / unit preferences, calendar arithmetic / date-time utilities, locale-aware collation (UCA + CLDR tailorings), locale-aware search/string matching, CLDR transliteration, locale display names, application message catalogs (RoboMsg), RoboMsgFmt formatting, and message-format validation/compatibility. Not a full ICU MessageFormat clone. |
| **May depend on** | RoboBase, RoboMem, RoboContainers, RoboUnicode. |
| **Must NOT depend on it** | RoboBase … RoboUnicode (until a reviewed exception). Must not be depended on by HTML/CSS/DOM/Layout/Graphics yet. |
| **Public C API boundary** | `Include/RoboI18N/RoboI18N.h`, Locale/LocaleData/LocaleService/LocaleNegotiate/Number/Plural/DateTime/TimeZone/Calendar/DateTimeFormat/DateTimeInterval/RelativeTime/Unit/Measure/Currency/Parse/DateTimeParse/Search/Translit/DateTimeArith/Collation/DisplayNames/Message/MessageFormat/MessageValidate. Guide: `i18n/README.md`, `i18n/LocaleService.md`, `i18n/LocaleNegotiate.md`, `i18n/OrdinalPlurals.md`, `i18n/Units.md`, `i18n/MeasurePrefs.md`, `i18n/Currency.md`, `i18n/Parse.md`, `i18n/DateTimeParse.md`, `i18n/Search.md`, `i18n/Transliteration.md`, `i18n/RoboMsg.md`, `i18n/RoboMsgFormat.md`, `i18n/TZDB.md`, `i18n/TZDisplay.md`. IANA File-Date **2026-05-05**; CLDR **46** / Unicode+UCA **16.0.0**; IANA tzdb **2026c**; API **0.25.0**. |

---

### RoboThread

| | |
|---|---|
| **Responsibility** | Threads, mutexes, condition variables, semaphores, atomics, TLS — OS-portable sync primitives. |
| **May depend on** | RoboBase, RoboMem; OS details only through future RoboPlatform where needed (bootstrap may use libc/syscalls until RoboPlatform exists). |
| **Must NOT depend on it** | RoboBase, RoboMem, RoboContainers (containers stay usable single-threaded without threading). |
| **Public C API boundary** | Planned: `Include/RoboThread/RoboThread.h` — thread spawn/join, mutex, cond, atomic ops. |

---

### RoboEvent

| | |
|---|---|
| **Responsibility** | Event queue, task queue, timers, callbacks, microtasks, event loop. Coordinates async work for JS, net, and render later. |
| **May depend on** | RoboBase, RoboMem, RoboContainers, RoboThread. |
| **Must NOT depend on it** | RoboBase … RoboThread. |
| **Public C API boundary** | Planned: `Include/RoboEvent/RoboEvent.h` — loop create/run/quit, post task, timer, microtask drain. |

---

### RoboIPC

| | |
|---|---|
| **Responsibility** | Messages, channels, endpoints, serialization, shared memory between runtime processes/services. |
| **May depend on** | RoboBase, RoboMem, RoboContainers, RoboThread, RoboEvent (as needed for async delivery). |
| **Must NOT depend on it** | RoboBase … RoboEvent (except documented shared message header types living in RoboBase/RoboMem if required). |
| **Public C API boundary** | Planned: `Include/RoboIPC/RoboIPC.h` — channel open/close, send/recv, shared-memory map. |

---

### RoboPlatform

| | |
|---|---|
| **Responsibility** | Clock, filesystem, process, display, input, audio, and other OS-facing abstractions. All platform-specific code stops here. |
| **May depend on** | RoboBase, RoboMem, RoboContainers, RoboThread, RoboEvent, RoboIPC; Roboticela OS syscalls/SDK. |
| **Must NOT depend on it** | RoboBase … RoboIPC (core libraries stay platform-agnostic). |
| **Public C API boundary** | Planned: `Include/RoboPlatform/RoboPlatform.h` (+ focused headers) — clock, fs, process, display, input. |

---

### RoboGraphics

| | |
|---|---|
| **Responsibility** | Display surfaces, framebuffers, bitmaps, basic drawing (rect, image, clip), low-level raster primitives. |
| **May depend on** | RoboBase … RoboPlatform. |
| **Must NOT depend on it** | RoboBase … RoboPlatform; also must not depend on DOM/HTML/CSS/JS. |
| **Public C API boundary** | Planned: `Include/RoboGraphics/RoboGraphics.h` — surface create, draw rect/image/text stub, present. |

---

### RoboWindow

| | |
|---|---|
| **Responsibility** | Window create/destroy, resize, move, focus, z-order, minimize/maximize — connection to the OS window server. |
| **May depend on** | RoboBase … RoboPlatform, RoboGraphics. |
| **Must NOT depend on it** | RoboBase … RoboGraphics; must not depend on DOM/HTML/JS. |
| **Public C API boundary** | Planned: `Include/RoboWindow/RoboWindow.h` — window lifecycle and geometry. |

---

### RoboDOM

| | |
|---|---|
| **Responsibility** | Document, node, element, text, attributes, tree structure, DOM events (model only). |
| **May depend on** | RoboBase, RoboMem, RoboContainers (and Thread/Event only if event targeting needs them). |
| **Must NOT depend on it** | RoboBase … RoboWindow; must not depend on HTML/CSS parsers, layout, paint, JS. |
| **Public C API boundary** | Planned: `Include/RoboDOM/RoboDOM.h` — document/node create, tree ops, attribute get/set. |

---

### RoboHTML

| | |
|---|---|
| **Responsibility** | HTML tokenize/parse → RoboDOM tree construction (application markup subset first). |
| **May depend on** | RoboBase, RoboMem, RoboContainers, RoboDOM. |
| **Must NOT depend on it** | RoboBase … RoboDOM; RoboCSS, layout, paint, JS. |
| **Public C API boundary** | Planned: `Include/RoboHTML/RoboHTML.h` — parse HTML bytes/string into a document. |

---

### RoboCSS

| | |
|---|---|
| **Responsibility** | CSS tokenize/parse, selectors, style rules, computed style, inheritance applied onto DOM. |
| **May depend on** | RoboBase, RoboMem, RoboContainers, RoboDOM. |
| **Must NOT depend on it** | RoboBase … RoboDOM; RoboHTML (CSS must work on any DOM tree); layout, paint, JS. |
| **Public C API boundary** | `Include/RoboCSS/RoboCSS.h` — the 9.1 model (source, token, value, property, declaration, selector, rule, stylesheet, parser cursor), the 9.2 tokenizer (`RoboCSSTokenizer.h`), the 9.3 grammar behind `robo_css_stylesheet_parse` and `robo_css_declaration_list_parse_text`, the 9.4 selector surface: the selector grammar (`RoboCSSSelectorParser.h`) and the matcher (`RoboCSSMatcher.h`), and the 9.5 cascade (`RoboCSSCascade.h`) behind `robo_css_style_context_match_declarations`, the 9.6 specificity (`RoboCSSSpecificity.h`) that cascade consumes, the 9.7 property metadata (`RoboCSSPropertyRegistry.h`) and inheritance (`RoboCSSInherit.h`) behind `robo_css_inherit_resolve` and `robo_css_style_context_resolve_inherited_style`, the 9.8 typed computed value (`RoboCSSComputedValue.h`) and computed style (`RoboCSSComputed.h`) behind `robo_css_computed_value_compute` and `robo_css_style_context_compute_style`, the 9.9 colour (`RoboCSSColor.h`) that the value layer consumes rather than duplicating and that the computed style resolves `currentColor` against, and the 9.10 unit vocabulary and typed numeric value (`RoboCSSUnit.h`) that the value layer likewise consumes — with a resolution context and a resolver that answers "not enough context" rather than inventing a font size or a viewport, and the 9.11 box-model vocabulary (`RoboCSSBox.h`) — four sides, the three shorthands and their expansion, which is where a shorthand declaration becomes longhand declarations the cascade can order like any other, the 9.12 display vocabulary (`RoboCSSDisplay.h`) — four values, each a row carrying its keyword and the outer/inner pair a later phase extends, with the classifications a layout pass dispatches on, the 9.13 positioning vocabulary (`RoboCSSPosition.h`) — five `position` values, each a row carrying whether its box stays in flow and whether it is a positioned box, with the four insets on the 9.11 `EDGE_OFFSET` shape and `z-index` as `auto` or a whole number, the 9.14 background family (`RoboCSSBackground.h`) — five models rather than one because the six longhands do not share a grammar: two closed vocabularies (`repeat`, `attachment`) with rows read back by name, a colour reused from 9.9, an image reference kept as the tokens the grammar produced, and the phase's first two *pair* values (`position`, `size`) where a single component folds into both halves and equality is kind-aware so `left` and `0px` stay different values, and the 9.15 border family (`RoboCSSBorder.h`) — not a family but a *grid*: three kinds over four sides is twelve longhands, and four different declarations can set any one of them, so the phase's central act is a *move* of the distribution of the three border rows out of the box model's pass and into one that can see `border-width`, a side shorthand and `border` at once, with the box table keeping the rows because a row is where the relation between a shorthand, its four sides and the twelve names is written down once; a fifth closed vocabulary for the styles that carries whether a style draws anything at all, and a classifier whose question *order* — a style keyword before a width keyword before a colour name — is the whole of its logic. The parser *builds and interprets nothing*; the matcher *answers and applies nothing* — it asks a host (the four callbacks in `RoboCSSStyle.h`) about one element at a time, holds no document and mutates nothing; the cascade *collects and orders* what applies by level, then specificity, then document order, and holds no specificity arithmetic of its own; inheritance *resolves one element* from its own cascade and an explicitly supplied parent, and never competes with the cascade; computation *normalises a value* from its declared shape and the value, and resolves nothing against a containing block, a font or the element; a colour owns nothing, is four bytes and a tag by value, and keeps `currentColor` contextual until the one layer that has an element and a parent settles it; a unit is an enumerator in a closed vocabulary whose resolution is a *question* with four answers, taken by whoever has the font metrics or the viewport — an argument, never a global; and the box model is *vocabulary and expansion only* — it knows the four sides, the three shorthands, how many components each accepts, and how a declaration distributes, and it computes no geometry, collapses no margins and resolves no containing block; display is *a closed vocabulary with no consequence* — it answers what an element generates rather than doing anything about it, so `display: none` is the absence of a box as a fact and not a removal from a tree, and no element's tag is consulted for its display; and positioning is *the same shape of answer about where* — the five values and their two facts are typed and classified, while no containing block is found, no offset is applied, no rectangle exists and no stacking order is computed, so `z-index` is a number nothing compares and `position: sticky` is a value rather than a behaviour; and the backgrounds are *a description with no picture* — six typed coordinates that a later paint pass could use, with `none` a value rather than a missing image, `url(…)` never fetched, `50%` never resolved against a box, `background-attachment: fixed` a keyword with no scrollport to fix against; and the borders are *edges with no geometry* — twelve typed values whose distribution has one owner because the cascade, and not a pass order, is what decides which of four declarers a longhand is, with `medium` a keyword rather than a used length, `border-width: 50%` computed because 9.8's contract is a value's shape and not a property's range, `generates_border` the one fact a paint phase must ask of a style, and `border-radius` not a value this build has. `robo_css_get_capabilities` reports what this build actually does. |
| **Status** | Goals 9.1–9.15 complete (`css/README.md`, `css/docs/`): the model (9.1), the real CSS Syntax tokenizer (9.2), the real CSS Syntax parser with the inline-style boundary filled by the same grammar (9.3), the selector grammar and matcher tested against the real DOM as well as a callback host (9.4), the cascade (9.5), specificity (9.6), inheritance and the property registry (9.7), typed computed values (9.8), colours (9.9), units and numbers (9.10), the box model (9.11), the `display` vocabulary (9.12), positioning (9.13), the background family (9.14) and the border family (9.15). Chromium- *and* higher-layer independence asserted by `-Verify`. |

---

### RoboLayout

| | |
|---|---|
| **Responsibility** | DOM + computed style → box geometry (block, inline, flex, absolute, later grid). |
| **May depend on** | RoboBase … RoboDOM, RoboCSS (computed style), optionally RoboFont when text metrics exist. |
| **Must NOT depend on it** | RoboHTML parser (layout consumes DOM+style, not source text); paint, render, JS. |
| **Public C API boundary** | Planned: `Include/RoboLayout/RoboLayout.h` — layout tree build, box queries. |

---

### RoboPaint

| | |
|---|---|
| **Responsibility** | Layout boxes → paint command lists (background, border, text, image, clip). Does not present to screen. |
| **May depend on** | RoboBase … RoboLayout, RoboGraphics types as needed for paint records. |
| **Must NOT depend on it** | RoboHTML/CSS parsers; RoboRender presentation; RoboJS. |
| **Public C API boundary** | Planned: `Include/RoboPaint/RoboPaint.h` — paint record list build/iterate. |

---

### RoboRender

| | |
|---|---|
| **Responsibility** | Execute paint records onto graphics surfaces / prepare frames for composition. |
| **May depend on** | RoboBase … RoboPaint, RoboGraphics, RoboWindow as needed. |
| **Must NOT depend on it** | RoboCompositor policy layers above; RoboJS; RoboShell. |
| **Public C API boundary** | Planned: `Include/RoboRender/RoboRender.h` — render frame from paint list to surface. |

---

### RoboCompositor

| | |
|---|---|
| **Responsibility** | Layers, surfaces, transforms, clipping, z-order, damage tracking, frame scheduling for desktop/window stacks. |
| **May depend on** | RoboBase … RoboRender, RoboGraphics, RoboWindow, RoboPlatform. |
| **Must NOT depend on it** | RoboJS, RoboAPI, RoboShell (shell uses compositor; compositor does not know shell). |
| **Public C API boundary** | Planned: `Include/RoboCompositor/RoboCompositor.h` — layer tree, schedule frame, commit. |

---

### RoboJS

| | |
|---|---|
| **Responsibility** | JavaScript language implementation (lexer → VM) and binding surface to DOM/events — independent of Chromium/V8. |
| **May depend on** | RoboBase … RoboEvent, RoboDOM, and later controlled hooks into layout/render invalidation; not on Chromium/V8. |
| **Must NOT depend on it** | RoboBase … RoboCompositor core libraries (except explicit binding facades). |
| **Public C API boundary** | Planned: `Include/RoboJS/RoboJS.h` — isolate/context create, eval script, expose host objects. |

---

### RoboAPI

| | |
|---|---|
| **Responsibility** | Roboticela platform APIs for applications (`window`, `filesystem`, `process`, `display`, `input`, `network`, `notifications`, …) exposed to JS and native callers. |
| **May depend on** | RoboBase … RoboPlatform, RoboWindow, RoboJS bindings layer, and service facades. |
| **Must NOT depend on it** | Core DOM/layout/paint pipelines (they must not call RoboAPI). |
| **Public C API boundary** | Planned: `Include/RoboAPI/RoboAPI.h` — register modules, native entry points mirroring JS `roboticela.*`. |

---

### RoboNet

| | |
|---|---|
| **Responsibility** | URL, sockets, DNS, HTTP(S), WebSocket. TLS via audited libraries behind RoboTLS (later), not home-grown crypto. |
| **May depend on** | RoboBase … RoboPlatform, RoboEvent, RoboIPC as needed. |
| **Must NOT depend on it** | DOM/HTML/CSS/layout/paint/JS core (net is a service; fetch bindings call into RoboNet). |
| **Public C API boundary** | Planned: `Include/RoboNet/RoboNet.h` — request/response, socket, DNS. |

---

### RoboStorage

| | |
|---|---|
| **Responsibility** | Application files, key/value storage, cache, database façade, per-app isolation. |
| **May depend on** | RoboBase … RoboPlatform, RoboSecurity policy hooks when available. |
| **Must NOT depend on it** | Rendering pipeline; RoboJS engine core (storage is a service). |
| **Public C API boundary** | Planned: `Include/RoboStorage/RoboStorage.h` — open store, get/set, file paths for app data. |

---

### RoboSecurity

| | |
|---|---|
| **Responsibility** | App identity, permissions, sandbox boundaries, FS/network/IPC restriction enforcement. |
| **May depend on** | RoboBase … RoboPlatform, RoboIPC. |
| **Must NOT depend on it** | HTML/CSS/layout (security policy is consulted by services, not owned by render). |
| **Public C API boundary** | Planned: `Include/RoboSecurity/RoboSecurity.h` — permission check, capability tokens. |

---

### RoboShell

| | |
|---|---|
| **Responsibility** | Desktop, launcher, taskbar, notifications, window manager integration, settings — preferably built with the runtime stack itself. |
| **May depend on** | Full runtime stack as a client (HTML/CSS/JS/API/compositor/window). |
| **Must NOT depend on it** | Any core library below Applications (nothing in Base…Compositor may include Shell). |
| **Public C API boundary** | Planned: `Include/RoboShell/RoboShell.h` for native shell services; UI may live as RoboHTML apps under OS paths. |

---

### Applications

| | |
|---|---|
| **Responsibility** | System apps (file manager, settings, terminal, calculator, text editor, system monitor, …) using the runtime. |
| **May depend on** | Public RoboAPI / Shell contracts and documented libraries. |
| **Must NOT depend on it** | Entire runtime core (apps are leaves). |
| **Public C API boundary** | App-local; no requirement that every app export a shared C API. |

---

## Current tree (Goal 1.1)

```text
runtime/
├── ARCHITECTURE.md
├── CHROMIUM_REFERENCE.md
├── DEPENDENCIES.md
├── DESIGN_RULES.md
├── PROGRESS.md
├── README.md
├── Include/
│   ├── RoboticelaRuntime.h
│   └── RoboBase/
│       └── RoboBase.h
├── Source/
│   ├── Main.c
│   └── RoboBase/
│       └── RoboBase.c
├── Tests/
│   └── TestRoboBase.c
├── Tools/
│   └── Build.ps1
└── Out/
```

Implemented: RoboBase (Goals 1.x), RoboMem Phase 2, RoboArray/RoboString, RoboUnicode, RoboI18N and CLDR data, RoboFont, RoboText, RoboGraphics, RoboWindow and the QEMU display backend, RoboHTML/DOM (Goals 8.1–8.15), and RoboCSS through borders (Goals 9.1–9.15) — the box model as *values and expansion* with no geometry computed, `display` as a typed vocabulary with no layout behaviour, positioning as typed values and classifications with no coordinates, backgrounds as six typed longhands with no picture painted, and borders as twelve typed values with no edge measured, no width combined with the style's `generates_border` fact, and no corner rounded.
Still not implemented: CSS conformance tests (Goal 9.16), threads, events, JS, layout, paint and networking.

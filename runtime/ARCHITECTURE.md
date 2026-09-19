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
| **Public C API boundary** | `Include/RoboCSS/RoboCSS.h` — the 9.1 model (source, token, value, property, declaration, selector, rule, stylesheet, parser cursor), the 9.2 tokenizer (`RoboCSSTokenizer.h`), the 9.3 grammar behind `robo_css_stylesheet_parse` and `robo_css_declaration_list_parse_text`, the 9.4 selector surface: the selector grammar (`RoboCSSSelectorParser.h`) and the matcher (`RoboCSSMatcher.h`), and the 9.5 cascade (`RoboCSSCascade.h`) behind `robo_css_style_context_match_declarations`, the 9.6 specificity (`RoboCSSSpecificity.h`) that cascade consumes, the 9.7 property metadata (`RoboCSSPropertyRegistry.h`) and inheritance (`RoboCSSInherit.h`) behind `robo_css_inherit_resolve` and `robo_css_style_context_resolve_inherited_style`, the 9.8 typed computed value (`RoboCSSComputedValue.h`) and computed style (`RoboCSSComputed.h`) behind `robo_css_computed_value_compute` and `robo_css_style_context_compute_style`, the 9.9 colour (`RoboCSSColor.h`) that the value layer consumes rather than duplicating and that the computed style resolves `currentColor` against, and the 9.10 unit vocabulary and typed numeric value (`RoboCSSUnit.h`) that the value layer likewise consumes — with a resolution context and a resolver that answers "not enough context" rather than inventing a font size or a viewport, and the 9.11 box-model vocabulary (`RoboCSSBox.h`) — four sides, the three shorthands and their expansion, which is where a shorthand declaration becomes longhand declarations the cascade can order like any other, the 9.12 display vocabulary (`RoboCSSDisplay.h`) — four values, each a row carrying its keyword and the outer/inner pair a later phase extends, with the classifications a layout pass dispatches on, the 9.13 positioning vocabulary (`RoboCSSPosition.h`) — five `position` values, each a row carrying whether its box stays in flow and whether it is a positioned box, with the four insets on the 9.11 `EDGE_OFFSET` shape and `z-index` as `auto` or a whole number, the 9.14 background family (`RoboCSSBackground.h`) — five models rather than one because the six longhands do not share a grammar: two closed vocabularies (`repeat`, `attachment`) with rows read back by name, a colour reused from 9.9, an image reference kept as the tokens the grammar produced, and the phase's first two *pair* values (`position`, `size`) where a single component folds into both halves and equality is kind-aware so `left` and `0px` stay different values, and the 9.15 border family (`RoboCSSBorder.h`) — not a family but a *grid*: three kinds over four sides is twelve longhands, and four different declarations can set any one of them, so the phase's central act is a *move* of the distribution of the three border rows out of the box model's pass and into one that can see `border-width`, a side shorthand and `border` at once, with the box table keeping the rows because a row is where the relation between a shorthand, its four sides and the twelve names is written down once; a fifth closed vocabulary for the styles that carries whether a style draws anything at all, and a classifier whose question *order* — a style keyword before a width keyword before a colour name — is the whole of its logic. The parser *builds and interprets nothing*; the matcher *answers and applies nothing* — it asks a host (the four callbacks in `RoboCSSStyle.h`) about one element at a time, holds no document and mutates nothing; the cascade *collects and orders* what applies by level, then specificity, then document order, and holds no specificity arithmetic of its own; inheritance *resolves one element* from its own cascade and an explicitly supplied parent, and never competes with the cascade; computation *normalises a value* from its declared shape and the value, and resolves nothing against a containing block, a font or the element; a colour owns nothing, is four bytes and a tag by value, and keeps `currentColor` contextual until the one layer that has an element and a parent settles it; a unit is an enumerator in a closed vocabulary whose resolution is a *question* with four answers, taken by whoever has the font metrics or the viewport — an argument, never a global; and the box model is *vocabulary and expansion only* — it knows the four sides, the three shorthands, how many components each accepts, and how a declaration distributes, and it computes no geometry, collapses no margins and resolves no containing block; display is *a closed vocabulary with no consequence* — it answers what an element generates rather than doing anything about it, so `display: none` is the absence of a box as a fact and not a removal from a tree, and no element's tag is consulted for its display; and positioning is *the same shape of answer about where* — the five values and their two facts are typed and classified, while no containing block is found, no offset is applied, no rectangle exists and no stacking order is computed, so `z-index` is a number nothing compares and `position: sticky` is a value rather than a behaviour; and the backgrounds are *a description with no picture* — six typed coordinates that a later paint pass could use, with `none` a value rather than a missing image, `url(…)` never fetched, `50%` never resolved against a box, `background-attachment: fixed` a keyword with no scrollport to fix against; and the borders are *edges with no geometry* — twelve typed values whose distribution has one owner because the cascade, and not a pass order, is what decides which of four declarers a longhand is, with `medium` a keyword rather than a used length, `border-width: 50%` computed because 9.8's contract is a value's shape and not a property's range, `generates_border` the one fact a paint phase must ask of a style, and `border-radius` not a value this build has. 9.16 (**conformance**) adds no surface at all — it is a suite that asks this whole API the questions no per-family suite can, over all 63 registry rows, one declaration per property, one realistic document, a malformed corpus and the capability struct field by field. `robo_css_get_capabilities` reports what this build actually does. |
| **Status** | Goals 9.1–9.16 complete (`css/README.md`, `css/docs/`): the model (9.1), the real CSS Syntax tokenizer (9.2), the real CSS Syntax parser with the inline-style boundary filled by the same grammar (9.3), the selector grammar and matcher tested against the real DOM as well as a callback host (9.4), the cascade (9.5), specificity (9.6), inheritance and the property registry (9.7), typed computed values (9.8), colours (9.9), units and numbers (9.10), the box model (9.11), the `display` vocabulary (9.12), positioning (9.13), the background family (9.14), the border family (9.15) and conformance (9.16 — no production code, the whole-product questions). Chromium- *and* higher-layer independence asserted by `-Verify`. |

---

### RoboLayout

| | |
|---|---|
| **Responsibility** | DOM + computed style → box geometry (block, inline, flex, absolute, later grid). |
| **May depend on** | RoboBase, RoboMem, RoboGraphics (geometry only), RoboHTML, RoboCSS. It is the first module allowed to see **both** peers — DEPENDENCIES.md: the two must not know each other, so whoever needs both sits above both. |
| **Must NOT depend on it** | RoboWindow / RoboDisplay (below it in the graph, but a pass must be testable without one — the viewport is an argument); paint, render, compositor, JS, shell; Chromium. Asserted by `Tools/Build.ps1 -Verify`. |
| **Public C API boundary** | `Include/RoboLayout/RoboLayout.h` — module lifecycle, version, capabilities; the layout tree and its objects (`RoboLayoutTree.h`, `RoboLayoutObject.h`, `RoboLayoutTypes.h`), which reference DOM elements and computed styles rather than copying either; the constraints record and its bounds arithmetic (`RoboLayoutConstraints.h`); the result record with its explicit state (`RoboLayoutResult.h`); the pass context (`RoboLayoutContext.h`); the formatting-context vocabulary, record and classification (`RoboLayoutFormattingContext.h`); the **block-layout pass** and its record (`RoboLayoutBlock.h`); the **inline direction**, its record and its atomic-versus-transparent doctrine (`RoboLayoutInline.h`); the style-source seam (`RoboLayoutStyle.h`) that keeps the builder testable without CSS; the DOM→tree builder (`RoboLayoutBuilder.h`); the RoboHTML + RoboCSS bridge (`RoboLayoutDom.h`) that provides the computed styles; borrowed RoboGraphics geometry aliases (`RoboLayoutGeometry.h`); the **text-layout foundation** (`RoboLayoutText.h`) — code-point ranges, direction, the resolved font inputs, the run record and the **measurement seam** — with its RoboFont-backed implementation beside it (`RoboLayoutTextFont.h`); the **line-box record** (`RoboLayoutLineBox.h`) with the tree doors that read a pass's lines back (`robo_layout_tree_line_count` / `_line_at` / `_line` / `_line_member`, `robo_layout_object_line`); and the **baseline vocabulary** (`RoboLayoutBaseline.h`) — three kinds, the metrics record, its arithmetic, and the two rules that produce one — with the line carrying its own ascent, descent and kind (`robo_layout_line_box_baseline`, `_baseline_offset`, `_baseline_y`) and the object doorway back to the metrics it was *placed* with (`robo_layout_object_baseline`); and the **sizing layer** (`RoboLayoutSizing.h`) — one dimension in three kinds (`auto`, a definite `px` length, and a declaration this build cannot resolve), the record that carries the available extent per direction with whether it is *known at all*, the declared dimensions, the bounds, the contents' extent and the resolved size with a **source** per dimension, the two resolvers and the pair, and the one seam a computed style becomes a size through; the **margin layer** (`RoboLayoutMargin.h`) — the four declarations in four kinds (`absent`, signed `resolved`, `auto`, `unresolved`) read out of a computed style, the four **used values** a result is placed by, and the two policies (a block box's, which distributes a horizontal `auto` beside a declared width, and an atomic inline's, which turns every `auto` into zero); and the **padding layer** (`RoboLayoutPadding.h`) — the same four declarations in *three* kinds (`absent`, **non-negative** `resolved`, `unresolved`) read out of a computed style, the four used values, the **one** policy (a padding has no `auto`, so every kind of box reads the same four declarations the same way), and the two derived numbers (the width left for a box's contents, floored at zero, and the **padding box** a content box asks for, summed in 64 bits and saturated — renamed from `border_box` in 10.11, its real meaning now that a border exists); and the **border layer** (`RoboLayoutBorder.h`) — the **in-box layer between the padding and the margin**, twelve computed values over four sides, read through **three** seams because the three properties have three genuinely different computed shapes (a width is a length *or* one of `thin`/`medium`/`thick`, which the shared classification folds onto `UNUSABLE` because no other property's keyword names a length, so this reader goes to the computed style's `px`-or-keyword door rather than the generic one; a style is a closed vocabulary whose rows carry the one fact a paint phase must ask — `generates_border`, false only for `none`, because the vocabulary has ten rows and this build draws one), with the four sides each a width, a style and a colour **reused from 9.9** rather than re-invented, the whole discharged as three derived numbers — the border box (the padding box plus the border widths, 64 bits and saturated), the content width (borders and padding both subtracted, floored at zero), and the two edge policies that stay opposite because one floors and one ceilings. **10.3 computes the block direction and 10.4 the inline one**, as one pass with two branches: an inline formatting context's contents are placed along a line, wrapping when one does not fit, with an atomic inline's contents laid out inside it by the block pass and a transparent inline box written as the **union** of what its contents reached. **10.5** widens the object vocabulary for the first time — a text node is not an element and has no `display` to derive from, so `ROBO_LAYOUT_OBJECT_TEXT` is the one justification 10.1 wrote down that a second kind satisfies — and makes a run an ordinary participant whose width comes from a callback, a pass with none measuring every run as zero and **counting** it. **10.6** turns the line into a record a caller reads back: identity, an ordinal within its context, a finalized rectangle in the owner's content-box space, the width it had to fit, and the objects it placed — where a transparent inline box with contents is a participant of the context and a member of **no** line, because its rectangle is a union. **10.7** gives every line a **floor** — an ascent, a descent and the kind they came from, folded as the members' maxima while they are placed — and places each member *on* it, writing each member's `y` in one walk at finalization because the maximum is not final until the last placement; the same phase moves the union to a post-order walk over the finished context, since a box's rectangle bounds positions that are no longer final when it closes. **10.8** is where a box's `size` stops having one rule: the width is resolved first — the declared one, or the containing block's for `auto` — the box's children are laid out **into that width**, and the height is resolved afterwards as a declared `px` length or the contents' extent; an atomic inline cannot use that pair, because its `auto` height is the extent of contents that could only have been laid out *at* a width, so it resolves width, walks its contents, and asks for height last. A percentage, a relative unit or an intrinsic keyword is read, recorded as **unresolved**, and filled from the available space with a source saying so — never a zero, and never a guess. Every percentage and relative unit *as a length*, shrink-to-fit and the intrinsic keywords, `min-width`/`max-width`/`min-height`/`max-height` as *properties* (the sizing layer has bounds and clamps through them, but RoboCSS registers none of the four yet), `box-sizing` (a `border-box` model is one branch a later goal adds), a percentage padding (read, counted unresolved and used as zero), padding on a **transparent inline box**, a **border**, positioning, overflow, `line-height` as a resolved used value, `vertical-align`, shaping, bidi, Unicode line breaking, fragmentation and the anonymous box are absent by design, and `robo_layout_result_state` is the per-box authority on whether geometry exists. **10.9** adds the first number in the module that is *not* a size: a box's position becomes its **margin origin** — its containing block's content origin plus its own `margin-top` and `margin-left` — while its `size` is untouched by any margin, and what a parent stacks its next child below becomes the **margin box**, `top + height + bottom`. The four margins resolve *before* the width, because an `auto` width fills what they left; a horizontal `auto` takes its share of the leftover beside a declared **definite** width, an odd pixel going to `right`, while a vertical `auto` is zero (it means something only for an absolutely positioned box, 10.12) and every `auto` on an atomic inline is zero; a negative marginal is legal and kept **signed**, which is the one respect the margin vocabulary differs from the sizing one. The four used values are kept **on the object** rather than in the result, because adding them back is the arithmetic the phase exists to stop a caller re-deriving. **Margin collapsing is deliberately absent** — adjacent vertical margins *sum*, asserted as a value so a later goal changes rather than deletes it — and the shape left for it is that collapsing becomes a rule over those four used numbers rather than over the declarations. **10.10** is the phase that retires the identity a box's content box used to have with its border box: a box's `size` becomes its **border** box — the content box the sizing layer resolved plus the padding on each axis — its contents begin `padding-left`/`padding-top` inside it, and its `position` is untouched by any padding, while an `auto` width fills what the margins **and** the padding left, because CSS's constraint equation has a term for each. A padding is three kinds rather than the margin vocabulary's four (the grammar has no `padding: auto`) and **one** resolution door rather than two (with no `auto` there is nothing for a block box and an atomic inline to disagree about), and it is **non-negative** — the single parameter by which the two readers differ over a shared classification. The border box is derived by the padding layer from the sizing layer's answer rather than inside it, in 64 bits and saturated, because two numbers each bounded by the seam's maximum have a sum that is not. **10.11** completes the box model by describing the layer the padding sits inside and the margin sits outside of: a border is an in-box layer, not a spacing, so it grows the **border box** and moves the **content origin** and never moves the box — the mirror of the margin, which moves the box and never the size. The phase is mostly a *move*: the padding layer's `border_box` door stops being named for a box it does not compute and becomes `padding_box`, its true meaning, and the pass grows a fourth term — `border → padding → content` — so content begins `border-left + padding-left` inside the border box and an `auto` width fills what the **margins, the borders and the padding** left, because CSS's constraint equation has a term for each. It reads through three seams rather than one because the three properties compute three different ways: a width is a length or a `thin`/`medium`/`thick` keyword, which no other property's grammar shares, so the border reader asks the computed style's `px`-or-keyword door while everything else asks the generic one; a style is a closed vocabulary of ten rows whose one fact — `generates_border`, false only for `none` — is what a later paint pass must ask of a style rather than of a width, and this build draws exactly one; and a colour is 9.9's, `currentColor` and all, rather than a second colour system. A zero width and a `none` style both mean "no visible border" and both leave the geometry alone, because a border that draws nothing must not reserve space. What is deliberately absent is `box-sizing: border-box`, a percentage border width, a percentage padding, padding or a border on a **transparent inline box** (whose rectangle is the union of its contents), `border-radius` as a value this build has, and every style but `solid` actually drawn — the vocabulary is ten rows wide because the *value model* is, not because a painter exists. |
| **Status** | Goals **10.1** through **10.11** complete (`layout/README.md`, `layout/docs/`): the tree, the object, the constraints, the result, the context, the formatting contexts, the block pass, the inline direction, the text-layout foundation, the line-box record, the baseline vocabulary and its placement rule, the sizing layer, the margin layer, the padding layer, the border layer, the builder and the DOM+CSS bridge, with seventeen suites over 261 cases. Every object participates in exactly one formatting context once a pass has run, and a box that establishes one owns it; `ROOT`, `BLOCK` and `INLINE` exist and `flex`/`grid`/`table` deliberately have no enumerator yet. `NONE`, `BOX` and `TEXT` are the three object kinds. Every block-level box in a block-direction context — and every participant of an inline formatting context — holds a position and a size in its containing block's coordinate space, and every object that could not be placed is counted rather than dropped: `inline_level_skipped`, `unreached`, `block_level_declined`, and the four add up to the object count **including text objects**. A text run's width comes from a font through a callback seam, and a pass given no seam measures every run as zero and counts it rather than failing. A line is a record on the tree — never mutated, never created by a caller — whose members are the objects it *placed*, which is a smaller set than the context's participants whenever a transparent inline box enclosed something. Every stored line carries a **baseline** — an ascent, a descent and the kind they came from — its height **is** their sum, and every member holds a `y` derived from it, so two runs of different fonts on one line share a floor and an object that was not placed reports a miss rather than a zero-extent floor. **Version 0.11.0**, `ROBO_LAYOUT_API_FROZEN=0` (the surface grows for the whole of Phase 10), `ROBO_LAYOUT_GEOMETRY_IMPLEMENTED=1`, `ROBO_LAYOUT_FORMATTING_CONTEXTS_IMPLEMENTED=1`, `ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED=1`, `ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED=1`, `ROBO_LAYOUT_TEXT_LAYOUT_IMPLEMENTED=1`, `ROBO_LAYOUT_LINE_BOXES_IMPLEMENTED=1`, `ROBO_LAYOUT_BASELINES_IMPLEMENTED=1`, `ROBO_LAYOUT_SIZING_IMPLEMENTED=1`, `ROBO_LAYOUT_MARGINS_IMPLEMENTED=1`, `ROBO_LAYOUT_PADDING_IMPLEMENTED=1`, `ROBO_LAYOUT_BORDER_IMPLEMENTED=1`. A declared `width` and `height` now reach the geometry, and a declared width is the width a box's *children* are resolved against. A box's margins and padding now reach it too: it is placed at its **margin origin**, its `size` is its **border** box, its contents begin inside it, an `auto` width fills what the margins, the borders and the padding left, and its parent stacks by its margin box — and the box model is now **complete**, four layers inset by inset: a border sits between the padding and the margin, growing the border box and moving the content origin while leaving the box where flow put it. Next: 10.12 basic positioning. |

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

Implemented: RoboBase (Goals 1.x), RoboMem Phase 2, RoboArray/RoboString, RoboUnicode, RoboI18N and CLDR data, RoboFont, RoboText, RoboGraphics, RoboWindow and the QEMU display backend, RoboHTML/DOM (Goals 8.1–8.15), RoboCSS through conformance (Goals 9.1–9.16) — the box model as *values and expansion* with no geometry computed, `display` as a typed vocabulary with no layout behaviour, positioning as typed values and classifications with no coordinates, backgrounds as six typed longhands with no picture painted, borders as twelve typed values with no edge measured, no width combined with the style's `generates_border` fact, and no corner rounded, and a conformance suite that adds no production code but asserts the whole registry, one declaration per property, one document, a malformed corpus and every capability field — and RoboLayout (Goals 10.1–10.11), where both directions are laid out for real: positions and sizes in the containing block's coordinate space, a cursor that advances and wraps along a line, a transparent inline box written as the union of its contents, an atomic inline's contents stacked inside it, a text node becoming an object whose width comes from a font through a measurement seam, each line a finalized record a caller reads back with its members, every line carrying a **baseline** its members are placed on, a **sizing layer** that turns a declared `px` width and height into geometry — with the width a box resolves being the width its children are laid out into — where a percentage, a relative unit or an intrinsic keyword is recorded as unresolved and filled rather than guessed, and a **margin layer** that places a box at its margin origin, distributes a horizontal `auto` beside a declared width, and stacks a parent's children by their margin boxes — with the four kinds keeping "nobody declared this side" apart from a declared zero, and adjacent vertical margins summing rather than collapsing — a **padding layer** and a **border layer** that complete the box model from the inside (a box's `size` is its border box, its contents begin inside the borders and the padding, an `auto` width fills what the margins, the borders and the padding left, and the model stays `box-sizing: content-box`, so a declared `width: 200px` with `padding: 5px` and `border: 5px` is 200 inside and 220 outside) — with `line-height` as a resolved used value, `vertical-align`, shaping, bidi, Unicode line breaking, fragmentation, the anonymous box, percentage and intrinsic sizing, margin collapsing, `box-sizing: border-box`, percentage padding and percentage border widths, padding or a border on a transparent inline box, `border-radius`, and every border style but `solid` actually drawn, positioning, overflow and paint all deferred to the goals that own them.
Still not implemented: threads, events, JS, layout, paint and networking.

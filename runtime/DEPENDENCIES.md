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
| RoboPaint, and RoboLayout / RoboFont / RoboRender | Paint sits **directly above RoboLayout and directly below RoboRender**: it consumes layout's completed geometry (the result, the two inset records, the overflow record, a text run with its baseline and its resolved font inputs) through `const` read doors, plus a **text layout style** it carries by value, and produces an ordered operation list and, above it, a render-ready display list that resolves the box model into one rectangle per command (11.3), and it derives nothing layout already concluded. It may use RoboGraphics for geometry **only** — there are no new coordinate types — and, since 11.4–11.6, the **CPU raster, the surface and the face it is handed** when it fills backgrounds and border bands and strokes a run's glyphs into one (`robo_paint_decoration_paint`); drawing into a surface and a font its caller owns is not a dependency on the module that presents it. The one font door it calls beyond RoboGraphics' own bridge is `robo_font_face_parse_cmap`, and it calls it for exactly the reason the layout measurer does: a face is created without a character map, so the layer that resolves a glyph must ask for one. It must not depend on RoboWindow, RoboDisplay or RoboRender: a paint pass that consulted a surface could not be tested without one, so a surface is always the **caller's argument, never a query**. It must not write layout state, which is why the pass takes the tree by `const` pointer and allocates nothing but its own list. It must not depend on RoboJS (engines do not call JS). |
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

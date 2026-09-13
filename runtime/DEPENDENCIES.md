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
| RoboI18N | May depend on RoboBase, RoboMem, RoboContainers, RoboUnicode. Must not depend on HTML/CSS/DOM/Layout/Graphics/Render/Window/Shell. Unicode and higher UI must not depend on RoboI18N until a documented exception. Date/time formatting, intervals, relative time, measurement unit formatting/conversion, calendar arithmetic, collation, display names, timezone display/GMT formatting, application message catalogs, RoboMsgFmt formatting, message-format validation, and IANA tzdb timezone conversion use integer/UCA/CLDR/tzdb tables from Unicode UCA 16.0.0, CLDR 46, and IANA tzdb 2026c plus RoboMsg catalogs; duration (fixed ms) is distinct from period (calendar units); collation is distinct from normalization/casefold/binary compare; display names are distinct from locale identifiers; timezone *names* are CLDR presentation data distinct from tzdb *rules*; unit conversion is distinct from unit localization; application RoboMsg catalogs are distinct from CLDR; RoboMsgFmt interprets templates and delegates number/date/plural to existing RoboI18N APIs (not a second formatter stack); validation analyzes contracts without formatting; timezone conversion lives in RoboI18NTimeZone and display in RoboI18NDisplayNames (not formatters) and must not pull platform clocks into RoboBase. Goal 4.16 locale service (`RoboI18NLocaleService`) centralizes Unicode extensions and calendar/numbering/hour-cycle/collation/timezone selection without conflating locale with timezone. Goal 4.17 adds Accept-Language / language-range negotiation (`RoboI18NLocaleNegotiate`) separate from resource fallback. Goal 4.19 completes relative AUTO ±2 and adds `RoboI18NUnit`. See `i18n/TZDB.md`, `i18n/TZDisplay.md`, `i18n/LocaleService.md`, `i18n/LocaleNegotiate.md`, and `i18n/Units.md`. |

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

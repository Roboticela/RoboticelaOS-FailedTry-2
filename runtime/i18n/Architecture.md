# Architecture & dependencies (Goal 4.27)

## Dependency direction (allowed)

```text
RoboBase / RoboError
    ↑
RoboMem / RoboContainers
    ↑
RoboUnicode (UCD, norm, case, segmentation, bidi, emoji)
    ↑
RoboI18N (locale → data → formatters/parsers → messages)
```

Lower layers must not include RoboI18N. RoboI18N must not depend on fonts, layout,
DOM, CSS, networking, or platform UI.

## Locale resolution path (single)

```text
locale input (UTF-8 BCP 47)
    → parse / canonicalize (IANA Preferred-Value)
    → RoboLocale service: Unicode extensions + resolve
    → fallback (CLDR parentLocales / truncation / root)
    → subsystem tables (numbers, plurals, calendars, …)
```

No subsystem may invent a private fallback chain.

## Shared state

| State | Model |
|---|---|
| Generated CLDR/tzdb/UCA/translit tables | Immutable, read-only, shareable |
| `robo_i18n_locale_t` / settings | Value types |
| Formatters / parsers / collators / searchers | Owned; not thread-safe for concurrent mutation |
| Process default locale / application locale | Process-wide; **caller synchronizes** until RoboThread |
| Demo message catalog singleton | Immutable after first open |

## Intentional exceptions

* Display-name / timezone display may call into calendar/timezone helpers (peer I18N modules), not upward into UI.
* Message format embeds number/date formatters by value (no circular link).

## Chromium

`chromium/` is reference-only. Build VERIFY and `verify_i18n_pipeline.py` reject
Chromium include/link paths in runtime sources.

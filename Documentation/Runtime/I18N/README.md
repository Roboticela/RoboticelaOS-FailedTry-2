# RoboI18N

Internationalization for Roboticela Runtime (**Goals 4.1–4.27 — Phase 4 complete**).

**API freeze:** see [`ApiFreeze.md`](ApiFreeze.md) (`RoboI18nApiFrozen=1`, API **0.27.0**).
Architecture / UTF-8 / performance / licenses / Phase 4 summary:
[`Architecture.md`](Architecture.md), [`Utf8Contract.md`](Utf8Contract.md),
[`Performance.md`](Performance.md), [`Licenses.md`](Licenses.md), [`Phase4.md`](Phase4.md).
Completeness: [`Completeness.md`](Completeness.md), [`DataManifest.md`](DataManifest.md).

## Scope

| In scope | Out of scope (later phases) |
|---|---|
| BCP 47 / RFC 5646 language tags | Percent / compact / scientific |
| Parse, format, canonicalize (IANA) | Full ICU MessageFormat parity |
| Equality / matching / truncation parent | Full-text indexing |
| Default locale + preference lists | Fonts / shaping / layout |
| **Runtime locale service** (extensions, resolve, fallback) | Holiday databases |
| **Locale negotiation / Accept-Language** | Recurrence / scheduling engines |
| CLDR likely subtags maximize/minimize | Full LDML rule builder for every locale |
| Numbering-system metadata + digits | Translator UI / TMS |
| Locale-aware decimal number formatting | TAI/UTC leap-second modeling |
| CLDR parentLocales + root inheritance | |
| CLDR cardinal / ordinal plural categories | |
| Date/time/instant/duration primitives | |
| Calendar abstraction + CLDR calendar metadata | |
| **IANA tzdb** (offsets, DST, aliases, fixed) | |
| **Localized timezone names / GMT / exemplars** | |
| Localized date/time formatting | |
| Date/time interval formatting | |
| Relative-time formatting (+ special ±2 forms) | |
| **Measurement unit formatting + conversion** | |
| **Measurement systems / unit preferences** | |
| **Currency metadata + formatting** | |
| **Number / currency parsing** | |
| **Date / time parsing** | |
| **Locale-aware search / string matching** | |
| **Transliteration (CLDR transforms)** | |
| Calendar arithmetic / date-time utilities | |
| Locale-aware collation (UCA + CLDR) | |
| Locale display names (CLDR) | |
| Application message catalogs (RoboMsg) | |
| RoboMsgFmt message formatting engine | |
| RoboMsgFmt validation / compatibility | |
| Completeness / conformance / Phase 4 freeze | |

## Versions

| Dataset | Pin |
|---|---|
| RoboI18N API | `0.27.0` (frozen) |
| Generator | `1.25.0` |
| RoboMsgFmt syntax | **1** |
| IANA Language Subtag Registry | File-Date **2026-05-05** |
| CLDR | **46** (JSON **46.0.0**) |
| Unicode / UCA (DUCET) | **16.0.0** |
| Emoji | **16.0** |
| IANA tzdb | **2026c** |

## Verification

```text
python runtime/Tools/i18n/verify_i18n_pipeline.py
.\runtime\Tools\Build.ps1 -Configuration Debug -Test
.\runtime\Tools\Build.ps1 -Configuration Release -Test
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Sanitize
```

## Feature docs

See `LocaleService.md`, `LocaleNegotiate.md`, `OrdinalPlurals.md`, `Units.md`,
`MeasurePrefs.md`, `Currency.md`, `Parse.md`, `DateTimeParse.md`, `Search.md`,
`Transliteration.md`, `TZDB.md`, `TZDisplay.md`, `RoboMsg.md`, `RoboMsgFormat.md`.

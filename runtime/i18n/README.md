# RoboI18N

Internationalization for Roboticela Runtime (**Goals 4.1–4.25**).

## Scope

| In scope | Out of scope (later goals) |
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
| CLDR parentLocales + root inheritance | General unit / currency *parsing* beyond 4.22 |
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

## Versions

| Dataset | Pin |
|---|---|
| RoboI18N API | `0.25.0` |
| Generator | `1.22.0` |
| RoboMsgFmt syntax | **1** |
| IANA Language Subtag Registry | File-Date **2026-05-05** |
| CLDR | **46** (JSON **46.0.0**) |
| Unicode / UCA (DUCET) | **16.0.0** |
| IANA tzdb | **2026c** |

## Locale service (Goal 4.16)

See **`LocaleService.md`**. Central resolve / fallback / Unicode extensions /
capability queries. API: `RoboI18NLocaleService.h`.

## Locale negotiation (Goal 4.17)

See **`LocaleNegotiate.md`**. Accept-Language / language-range negotiation.
API: `RoboI18NLocaleNegotiate.h`. Not language detection.

## Ordinal plurals (Goal 4.18)

See **`OrdinalPlurals.md`**. CLDR ordinal categories distinct from cardinal;
`selectordinal` in RoboMsgFmt. API: `RoboI18NPlural.h`.

## Units & relative time (Goal 4.19)

See **`Units.md`**. CLDR unit formatting (long/short/narrow), compound `per`,
conversion foundation; relative AUTO ±2 keywords on the Goal 4.7 foundation.
API: `RoboI18NUnit.h`, `RoboI18NRelativeTime.h`.

## Measurement preferences (Goal 4.20)

See **`MeasurePrefs.md`**. Measurement systems (metric/US/UK) and preferred
units by category/region. Distinct from formatting. API: `RoboI18NMeasure.h`.

## Currency (Goal 4.21)

See **`Currency.md`**. ISO 4217 + CLDR currency metadata, patterns, accounting,
and opt-in cash rounding. API: `RoboI18NCurrency.h`. No FX conversion / parsing.

## Number & currency parsing (Goal 4.22)

See **`Parse.md`**. Strict/permissive locale-aware number and currency parsers
over `robo_i18n_decimal_t`. API: `RoboI18NParse.h`.

## Date & time parsing (Goal 4.23)

See **`DateTimeParse.md`**. Strict/permissive locale-aware date/time parsers
over calendar fields, local date-time, and optional instant resolution.
API: `RoboI18NDateTimeParse.h`.

## Search & matching (Goal 4.24)

See **`Search.md`**. Explicit modes for exact / canonical / casefold / collation
substring search with UTF-8 byte ranges. Not transliteration or FTS indexing.
API: `RoboI18NSearch.h`.

## Transliteration (Goal 4.25)

See **`Transliteration.md`**. CLDR transform rule-sets, forward/reverse IDs,
reusable transliterators, streaming hold buffer. Not search/collation/translation.
API: `RoboI18NTranslit.h`.

## Time zones (Goals 4.14–4.15)

- **Rules / conversions:** `TZDB.md` — `RoboI18NTimeZone.h`, `RoboTzData`
- **Display / formatting:** `TZDisplay.md` — `robo_i18n_timezone_display_name_at`,
  CLDR metazone/exemplar/GMT via `generate_display.py`

## Message validation (Goal 4.13)

See **`RoboMsgFormat.md`** (Validation section). API: `RoboI18NMessageValidate.h`.

## Message formatting (Goal 4.12)

See **`RoboMsgFormat.md`**. Parse once / format many; not full ICU MessageFormat.

API: `RoboI18NMessageFormat.h` — `mf_parse` / `mf_format` / `mf_format_template` /
`message_format_mf`. Delegates number, date/time, and plural to existing RoboI18N.

## Message catalogs (Goal 4.11)

See **`RoboMsg.md`**. Application translations are not CLDR.

API: `RoboI18NMessage.h` — `catalog_open_demo` / `catalog_load` / `message_get` /
`message_format`. Fallback locale is **configured**, never assumed English.

## Display names (Goal 4.10)

API: `RoboI18NDisplayNames.h`. Distinguishes **identifier** vs **display locale**.

| Category | Identifier example | Display example (en) |
|---|---|---|
| Language | `ur` | Urdu |
| Script | `Arab` | Arabic |
| Region | `PK` | Pakistan |
| Currency | `PKR` | Pakistani Rupee / Rs |
| Calendar | `gregorian` | Gregorian Calendar |
| Numbering | `arabext` | Extended Arabic-Indic Digits |
| Time zone | `Asia/Karachi` | Pakistan Standard Time / Karachi |
| Locale | `ur-PK` | Urdu (Pakistan) via `localePattern` |

**Fallback:** display locale → CLDR `parentLocales` / truncation parents → `en`
tables → stable identifier (`ROBO_I18N_DISP_FALLBACK_CODE`, default) or
`ROBO_NOT_FOUND`. English is the ultimate **table** fallback because CLDR
localenames `root` is empty in cldr-json — not silent invention of names.

**Missing data:** never invent translations. Context: standard only;
`dialect` reserved.

**Unicode:** output is logical UTF-8; no visual bidi reordering in this layer.

Data: `RoboDispData` from `Tools/i18n/generate_display.py`.

## Collation (Goal 4.9)

Do **not** confuse:

| Mode | API |
|---|---|
| Binary / code-unit | `robo_i18n_compare_utf8_binary` |
| Code-point order | `robo_i18n_compare_utf8_codepoints` |
| Canonical equivalence | `RoboUnicodeNorm` (NFC/NFD) |
| Locale-aware collation | `robo_i18n_collator_*` (UTS #10) |
| Search / substring matching | `robo_i18n_searcher_*` (Goal 4.24) |

Pipeline: STRICT UTF-8 decode → NFD → collation elements → level weights / sort key.

- **Strength:** primary … identical (independent of locale).
- **Alternate:** non-ignorable vs shifted (variable punctuation).
- **Numeric:** digit runs compared as integers (ASCII + `Nd`); not floating-point.
- **Invalid UTF-8:** `ROBO_PARSE_ERROR` (no replacement, no UB).
- **Sort keys:** opaque bytes; `memcmp`-compatible for the same options + data version; **not** portable across UCA/CLDR bumps.
- **Tailoring:** CLDR 46 metadata for 15 locales; compiled remaps for selected types (e.g. `sv`/`es` standard, `de` phonebook). CJK/Arabic full rule application remains architecture + root UCA until a full LDML builder ships.
- **Ownership:** caller destroys collators; sort-key buffers are caller-owned; DUCET/CLDR tables are immutable process data.
- **Threads:** one writer thread per mutable collator options, or external locking.

Regenerate UCA/collation:

```powershell
.\runtime\Tools\i18n\Fetch-Uca.ps1
python .\runtime\Tools\i18n\generate_uca.py --write-golden
python .\runtime\Tools\i18n\generate_collation.py --write-golden
```

## Duration vs period (Goal 4.8)

| Concept | Meaning |
|---|---|
| **Duration** | Fixed elapsed time in milliseconds (`robo_i18n_duration_t`). `from_days(1)` = 86400000 ms — **not** a calendar day across DST. |
| **Period** | Calendar-relative years/months/weeks/days (`robo_i18n_period_t`). Never auto-converted to seconds. |
| **Date difference** | Day count via Rata Die, or civil period (Y→M→D) compatible with CLAMP month add. |
| **Instant + duration** | Absolute timeline arithmetic (checked overflow). |
| **Instant + period in zone** | Local civil fields → add period → keep wall clock → UTC (IANA via tzdb; disambiguate folds/gaps). |

### End-of-month policy

**CLAMP** to the last valid day of the target month (ISO-8601 / Java `ChronoUnit.MONTHS` style):
Jan 31 + 1 month → Feb 28/29. Documented in `RoboI18NDateTimeArith.h`.

## Ownership / threads

Locale objects and formatters: caller-owned. Generated CLDR/UCA tables: immutable
process data. Display-name APIs are pure lookups (no global mutable cache).

## Chromium

Reference only. No Chromium headers, libraries, or `chromium/` build dependency.

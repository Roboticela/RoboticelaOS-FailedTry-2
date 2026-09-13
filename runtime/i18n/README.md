# RoboI18N

Internationalization for Roboticela Runtime (**Goals 4.1–4.15**).

## Scope

| In scope | Out of scope (later goals) |
|---|---|
| BCP 47 / RFC 5646 language tags | Percent / compact / scientific |
| Parse, format, canonicalize (IANA) | Full ICU MessageFormat parity |
| Equality / matching / truncation parent | Full-text search / transliteration |
| Default locale + preference lists | Fonts / shaping / layout |
| CLDR likely subtags maximize/minimize | Holiday databases |
| Numbering-system metadata + digits | Recurrence / scheduling engines |
| Locale-aware decimal number formatting | Full LDML rule builder for every locale |
| CLDR parentLocales + root inheritance | Translator UI / TMS |
| CLDR cardinal / ordinal plural categories | TAI/UTC leap-second modeling |
| Date/time/instant/duration primitives | |
| Calendar abstraction + CLDR calendar metadata | |
| **IANA tzdb** (offsets, DST, aliases, fixed) | |
| **Localized timezone names / GMT / exemplars** | |
| Localized date/time formatting | |
| Date/time interval formatting | |
| Relative-time formatting | |
| Calendar arithmetic / date-time utilities | |
| Locale-aware collation (UCA + CLDR) | |
| Locale display names (CLDR) | |
| Application message catalogs (RoboMsg) | |
| RoboMsgFmt message formatting engine | |
| RoboMsgFmt validation / compatibility | |

## Versions

| Dataset | Pin |
|---|---|
| RoboI18N API | `0.15.0` |
| Generator | `1.14.0` |
| RoboMsgFmt syntax | **1** |
| IANA Language Subtag Registry | File-Date **2026-05-05** |
| CLDR | **46** (JSON **46.0.0**) |
| Unicode / UCA (DUCET) | **16.0.0** |
| IANA tzdb | **2026c** |

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

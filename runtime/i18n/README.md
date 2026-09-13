# RoboI18N

Internationalization for Roboticela Runtime (**Goals 4.1–4.3**).

## Scope

| In scope | Out of scope (later goals) |
|---|---|
| BCP 47 / RFC 5646 language tags | Currency / percent / compact / scientific |
| Parse, format, canonicalize (IANA) | Date / time formatting |
| Equality / matching / truncation parent | Plural rules |
| Default locale + preference lists | Collation |
| CLDR likely subtags maximize/minimize | Message translation / catalogs |
| Numbering-system metadata + digits | Fonts / shaping / layout |
| **Locale-aware decimal number formatting** | Calendar arithmetic |
| Calendar preference identifiers | Locale display names |
| Measurement system preference | |
| CLDR parentLocales + root inheritance | |

## Standards relationship

```text
BCP 47 (RFC 5646)     — language tag grammar + IANA registry
Unicode (RoboUnicode) — scripts, text algorithms (separate pin)
CLDR 46               — locale data (this module; versioned separately)
```

**Unicode Script ≠ locale.** Example:

| Concept | Example |
|---|---|
| Language | Urdu (`ur`) |
| Unicode Script | `Arab` (code-point property; not assumed from language name) |
| Region | Pakistan (`PK`) |
| Locale | `ur-PK` (script filled only via CLDR likely subtags when maximized) |

## Versions

| Dataset | Pin |
|---|---|
| RoboI18N API | `0.3.0` |
| IANA Language Subtag Registry | File-Date **2026-05-05** |
| CLDR | **46** (JSON **46.0.0**) |
| Unicode version in CLDR metadata | **16.0.0** |
| Unicode / UCD / Emoji | independent — see `Tools/ucd/` |

`Unicode UCD != CLDR != IANA File-Date` — track each explicitly.

## Locale model (Goal 4.1)

Value type `robo_i18n_locale_t` (no heap for parse/format).

### Truncation parent vs CLDR data parent

| API | Behavior |
|---|---|
| `robo_i18n_locale_parent` | Truncation (private → extensions → variants → region → script) |
| `robo_i18n_locale_data_parent` | CLDR `parentLocales` map, else truncation, else `root` |

## Number formatting (Goal 4.3)

```text
locale + options
       ↓
robo_i18n_number_formatter_t   (stack, no heap, reusable)
       ↓
int64 / uint64 / decimal / double
       ↓
UTF-8 string (CLDR symbols + digits + grouping)
```

### Numeric representations

| Type | Precision | Notes |
|---|---|---|
| `int64_t` / `uint64_t` | Exact | Preferred for integers |
| `robo_i18n_decimal_t` | Exact | ASCII digit payload + scale + sign |
| `double` | Approximate | IEEE-754; NaN / ±∞ / −0 handled; \|x\| ≥ 1e18 → `ROBO_NOT_SUPPORTED` |

Do not expect exact decimal results from binary floating-point without an explicit decimal input.

### Options (defaults)

| Option | Default |
|---|---|
| `min_integer_digits` | 1 |
| `min_fraction_digits` | 0 |
| `max_fraction_digits` | −1 → CLDR pattern max (often 3) |
| `use_grouping` | 1 |
| `show_plus` | 0 |
| `rounding` | `ROBO_I18N_ROUND_HALF_EVEN` |
| `numbering_system` | empty → locale default |

Symbols (decimal / group / ± / NaN / ∞), grouping sizes, and `minimumGroupingDigits` come from CLDR — not English assumptions.

### Examples

| Locale | Value | Typical output |
|---|---|---|
| `en-US` | 1234567 | `1,234,567` |
| `de-DE` | 1234.56 | `1.234,56` |
| `fr-FR` | 1234.56 | `1 234,56` (U+202F group) |
| `hi-IN` (latn) | 1234567 | `12,34,567` |
| `ar-SA` | 1234 | Arabic-Indic digits + `٬` |

### Thread safety

Formatters are not synchronized. Own one per thread, or protect externally. Configuration is fixed after `init`. Symbol pointers refer to immortal generated tables.

### Public API

```c
#include "RoboI18N/RoboI18N.h"   /* includes Number.h */

robo_i18n_number_options_init(&opts);
robo_i18n_number_formatter_init(&fmt, &locale, &opts);
robo_i18n_number_format_i64(&fmt, value, buf, cap, &len);
robo_i18n_number_format_to_string(&fmt, &dec, &str);
robo_i18n_number_formatter_destroy(&fmt);
```

## CLDR data pipeline

```text
IANA registry  → Fetch-Iana.ps1 → generate_iana.py → RoboIanaData.*
CLDR 46 JSON   → Fetch-Cldr.ps1 → generate_cldr.py → RoboCldrData.*
```

Goal 4.3 adds per-locale × numbering-system rows: symbols, pattern, primary/secondary grouping, `minimumGroupingDigits` (511 rows).

Sources under `Tools/i18n/source/` are gitignored. Generated C is committed.
`python generate_cldr.py --repro` checks determinism.

## Layout

```text
Include/RoboI18N/   (Locale, LocaleData, Number, …)
Source/RoboI18N/
Tools/i18n/
i18n/README.md
Tests/Suites/SuiteRoboI18N*.c
```

## Attribution

- IANA: `Tools/i18n/IANA_ATTRIBUTION.md`
- CLDR: `Tools/i18n/CLDR_ATTRIBUTION.md` (Unicode License)

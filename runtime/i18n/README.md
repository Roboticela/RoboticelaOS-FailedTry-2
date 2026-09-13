# RoboI18N

Internationalization for Roboticela Runtime (**Goals 4.1–4.4**).

## Scope

| In scope | Out of scope (later goals) |
|---|---|
| BCP 47 / RFC 5646 language tags | Currency / percent / compact / scientific |
| Parse, format, canonicalize (IANA) | Date / time formatting |
| Equality / matching / truncation parent | Message formatting / catalogs |
| Default locale + preference lists | Collation |
| CLDR likely subtags maximize/minimize | Fonts / shaping / layout |
| Numbering-system metadata + digits | Calendar arithmetic |
| Locale-aware decimal number formatting | Locale display names |
| Calendar preference identifiers | |
| Measurement system preference | |
| CLDR parentLocales + root inheritance | |
| **CLDR cardinal / ordinal plural categories** | |

## Standards relationship

```text
BCP 47 (RFC 5646)     — language tag grammar + IANA registry
Unicode (RoboUnicode) — scripts, text algorithms (separate pin)
CLDR 46               — locale data (this module; versioned separately)
UTS #35               — plural operands and rule syntax
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
| RoboI18N API | `0.4.0` |
| Generator | `1.3.0` |
| IANA Language Subtag Registry | File-Date **2026-05-05** |
| CLDR | **46** (JSON **46.0.0**) |
| Unicode version in CLDR metadata | **16.0.0** |
| Unicode / UCD / Emoji | independent — see `Tools/ucd/` |

`Unicode UCD != CLDR != IANA File-Date` — track each explicitly. Plural tables use the same CLDR pin as number formatting.

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

Trailing fractional zeros matter for plurals: `1`, `1.0`, and `1.00` are distinct when `v` differs.

## Plural rules (Goal 4.4)

```text
locale + number
       ↓
CLDR operands (n, i, v, w, f, t, e/c)
       ↓
compiled bytecode evaluation
       ↓
category: zero | one | two | few | many | other
```

### Cardinal vs ordinal

| Kind | Use |
|---|---|
| Cardinal | quantity (“1 item”, “2 items”) |
| Ordinal | ranking (“1st”, “2nd”) |

These are separate CLDR rule sets. Do not mix them. Message formatting (selecting a branch from a category) is a **later** goal — this API only returns the category.

### Categories

Canonical CLDR names: `zero`, `one`, `two`, `few`, `many`, `other`.

`other` is always the safe fallback. Not every locale defines every category. `one` does **not** mean “exactly one” in every language.

### Operands (UTS #35)

| Operand | Meaning |
|---|---|
| `n` | Absolute value of the source number |
| `i` | Integer digits of `n` |
| `v` | Number of visible fraction digits |
| `w` | Visible fraction digits without trailing zeros (count) |
| `f` | Visible fraction digits as an integer |
| `t` | Visible fraction digits without trailing zeros as an integer |
| `e` / `c` | Compact decimal exponent (`0` for ordinary decimal inputs) |

Negative inputs keep a sign flag; **operands use the absolute value** (CLDR).

### Decimal representation for plurals

Prefer `robo_i18n_decimal_t` or an ASCII decimal string so scale (visible fraction length) is preserved. Do not round-trip through `double` when trailing zeros matter.

Precision limit: `ROBO_I18N_DECIMAL_DIGITS_MAX` (40) significant ASCII digits.

### Rule representation

Build-time parser (`Tools/i18n/plural_compile.py`) compiles CLDR rule expressions to compact bytecode (`PL_TEST` / `AND` / `OR` / `TRUE`). Runtime evaluates immutable generated tables — never reparses rule strings.

### Locale fallback

Lookup order for the current locale: `language-region` → `language-script` → `language`, then CLDR data parents. Example: `pt-PT` must not silently use Brazilian `pt` rules.

Unknown languages with no plural data yield `other`.

### Public API

```c
#include "RoboI18N/RoboI18N.h"   /* includes Plural.h */

robo_i18n_plural_cardinal(&locale, &dec, &cat);
robo_i18n_plural_ordinal(&locale, &dec, &cat);
robo_i18n_plural_cardinal_i64(&locale, value, &cat);
robo_i18n_plural_category_name(cat);   /* "one", … */
robo_i18n_plural_categories(&locale, kind, out, cap, &n);
```

### Thread safety

Generated rule tables are immutable. Evaluation is safe for concurrent callers. No mutable global plural cache.

### Limitations

- Compact-decimal samples (`1c6`) are not evaluated as `e ≠ 0` inputs in this goal (`e` is always 0 for public APIs).
- Message / ICU MessageFormat selection is intentionally absent.

## CLDR data pipeline

```text
IANA registry  → Fetch-Iana.ps1 → generate_iana.py → RoboIanaData.*
CLDR 46 JSON   → Fetch-Cldr.ps1 → generate_cldr.py → RoboCldrData.*
                                    (+ generate_plurals.py) → RoboPluralData.*
```

`generate_cldr.py` also emits plural bytecode and `Tests/Data/robo_plural_samples.txt` from CLDR `@integer` / `@decimal` samples.

Sources under `Tools/i18n/source/` are gitignored. Generated C is committed.
`python generate_cldr.py --repro` checks determinism (including plurals).

## Layout

```text
Include/RoboI18N/   (Locale, LocaleData, Number, Plural, …)
Source/RoboI18N/
Tools/i18n/
i18n/README.md
Tests/Suites/SuiteRoboI18N*.c
Tests/Data/robo_plural_samples.txt
```

## Attribution

- IANA: `Tools/i18n/IANA_ATTRIBUTION.md`
- CLDR: `Tools/i18n/CLDR_ATTRIBUTION.md` (Unicode License)

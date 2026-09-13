# Currency Data & Formatting (Goal 4.21)

Goal **4.21** formats money amounts with locale-aware currency patterns.
Parsing is Goal **4.22** — see `Parse.md`.

```text
Currency metadata          (ISO 4217 + CLDR fractions / region map)
      ↓
Currency formatting rules  (CLDR currencyFormats + spacing)
      ↓
Number formatting          (Goal 4.3 — robo_i18n_decimal_t)
      ↓
Localized currency output
```

Keep these concepts separate:

| Concept | Role |
|---|---|
| Currency identity | ISO 4217 alphabetic code (`USD`) |
| Currency metadata | numeric code, tender/historical flags |
| Currency precision | standard fraction digits + rounding increment |
| Cash rounding | optional cashDigits / cashRounding (explicit opt-in) |
| Currency formatting | patterns, placement, symbols/names |
| Currency parsing | Goal 4.22 (`RoboI18NParse.h`) |
| Currency conversion | **out of scope** |

## CLDR / ISO sources

| Data | Package |
|---|---|
| `currencyData.json` | fractions + region→currency |
| `codeMappings.json` | ISO 4217 numeric codes |
| `currencyFormats-numberSystem-*` | standard / accounting / spacing / unitPattern |
| `currencies.json` | symbols (via Goal 4.10) + plural names for NAME style |
| CLDR | **46** / Unicode **16.0.0** |
| Generator | `Tools/i18n/generate_currency.py` |
| Tables | `RoboCurrencyData.*` |

**Discrepancy note:** Fraction digits come from CLDR `currencyData` fractions
(`DEFAULT` inherited when a code has no override). Numeric codes come from
CLDR `codeMappings` (ISO 4217), not from a separate ISO download.

## API

`RoboI18NCurrency.h`

- `robo_i18n_currency_normalize` / `lookup` / `for_region` / `for_locale`
- `robo_i18n_currency_formatter_init` + `format` / `format_minor` / `format_i64`
- Styles: symbol, narrow symbol, ISO code, localized name, accounting
- Usage: `STANDARD` (default) vs `CASH` (explicit cash rounding only)

### Currency code resolution

1. Explicit `options.currency`
2. Unicode locale extension `cu`
3. CLDR region tender map (after maximize)

### Accounting

`ROBO_I18N_CURRENCY_STYLE_ACCOUNTING` or locale `cf=account` selects CLDR
accounting patterns (parentheses are locale data — not hard-coded).

### Precision

Prefer `robo_i18n_decimal_t` or `format_minor` (integer minor units).
`format_double` is approximate convenience only.

Cash rounding is never applied to standard/electronic amounts unless
`usage == CASH`.

## Ownership / thread safety

Formatters are stack value types (no heap of their own). Not synchronized —
one owner thread, or lock externally. Generated tables are immortal/read-only.

## Testing

`SuiteRoboI18NCurrency.c` — metadata, region/locale matrix, styles,
accounting, cash rounding, minor units, fuzz/robustness.

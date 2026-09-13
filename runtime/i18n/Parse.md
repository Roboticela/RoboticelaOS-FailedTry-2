# Number & Currency Parsing (Goal 4.22)

Formatting (Goals 4.3 / 4.21) and parsing are **separate APIs**.

```text
localized text
      ↓
number / currency parser   (this goal)
      ↓
robo_i18n_decimal_t (+ optional ISO currency)
```

| Concept | Role |
|---|---|
| Formatting | decimal / currency → localized UTF-8 |
| Parsing | localized UTF-8 → decimal / currency |
| Validation | strict vs permissive grammar checks |
| Conversion | FX / unit conversion — out of scope |

## Modes

| Mode | Behavior |
|---|---|
| **STRICT** | Reject malformed grouping, unexpected characters, trailing junk, and ambiguous currency symbols unless constrained (`require_currency` / accepted set). |
| **PERMISSIVE** | Allow missing or irregular grouping; if a symbol matches multiple currencies and the locale default is among them, select that default. Still deterministic. |

## Numeric representation

Results use `robo_i18n_decimal_t` (ASCII digits + `scale`), so these remain distinct:

```text
1      → digits "1", scale 0
1.0    → digits "10", scale 1
1.00   → digits "100", scale 2
```

Do not force money through `double`.

## Numbering systems

Digit glyphs come from CLDR numbering systems (same tables as Goal 4.3).
ASCII digits are also accepted. Tested: latn, arab, arabext, deva, beng, thai.

## Currency identity

- ISO codes are unambiguous.
- Symbols / narrow symbols / names are matched via display data (longest match).
- Ambiguous symbols never silently pick an arbitrary currency in **strict** mode;
  the result reports `ROBO_I18N_PARSE_ERR_AMBIGUOUS_CURRENCY` and candidate codes.
- Callers may `require_currency` or supply an accepted set.

## Errors

`robo_i18n_parse_result_t` carries category + UTF-8 **byte** `error_offset`.
API returns `ROBO_PARSE_ERROR` for grammar/content failures.

## API

`RoboI18NParse.h` — `robo_i18n_number_parser_*`, `robo_i18n_parse_number` /
`parse_integer`, `robo_i18n_currency_parser_*`, `robo_i18n_parse_currency`.

Input limit: `ROBO_I18N_PARSE_INPUT_MAX` (4096 bytes).

## Ownership / thread safety

Parsers are stack value types. Not synchronized. No global mutable parse state.

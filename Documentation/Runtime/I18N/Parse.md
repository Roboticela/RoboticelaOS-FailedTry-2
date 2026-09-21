# Number & Currency Parsing (Goal 4.22)

Formatting (Goals 4.3 / 4.21) and parsing are **separate APIs**.

```text
localized text
      ↓
number / currency parser   (this goal)
      ↓
I18nDecimal (+ optional ISO currency)
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

Results use `I18nDecimal` (ASCII digits + `scale`), so these remain distinct:

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
  the result reports `RoboI18nParseErrAmbiguousCurrency` and candidate codes.
- Callers may `require_currency` or supply an accepted set.

## Errors

`I18nParseResult` carries category + UTF-8 **byte** `error_offset`.
API returns `RoboParseError` for grammar/content failures.

## API

`RoboI18NParse.h` — `robo_i18n_number_parser_*`, `I18nParseNumber` /
`parse_integer`, `robo_i18n_currency_parser_*`, `I18nParseCurrency`.

Input limit: `RoboI18nParseInputMax` (4096 bytes).

## Ownership / thread safety

Parsers are stack value types. Not synchronized. No global mutable parse state.

# Ordinal Plural Rules (Goal 4.18)

Language: **C**. Builds on Goal 4.4 plural bytecode and Goals 4.11–4.13 message formatting.

## Cardinal vs ordinal

| Kind | CLDR source | MessageFormat type |
|---|---|---|
| Cardinal | `plurals.json` → `plurals-type-cardinal` | `plural` |
| Ordinal | `ordinals.json` → `plurals-type-ordinal` | `selectordinal` |

They are **separate** rule tables in the same generated blob
(`robo_plural_bytecode` + `robo_plural_ordinal_index`). Never assume ordinal
categories match cardinal ones for a locale (e.g. English `2` is cardinal
`other` and ordinal `two`).

## Architecture

1. Build time: `generate_plurals.py` / `plural_compile.py` compile CLDR rule
   expressions to compact bytecode (no runtime rule-text parsing).
2. Runtime: `robo_i18n_plural_select(..., ROBO_I18N_PLURAL_ORDINAL, ...)` evaluates
   bytecode using exact decimal operands (`n,i,v,w,f,t`; `e=0`).
3. Locale resolve: language-region → language-script → language → CLDR
   `data_parent` chain (same as cardinal; no second resolver).

CLDR **46** / Unicode **16.0.0**. Attribution: `Tools/i18n/CLDR_ATTRIBUTION.md`.

## Public API

```c
robo_i18n_plural_ordinal(locale, decimal, &cat);
robo_i18n_plural_ordinal_i64(locale, value, &cat);
robo_i18n_plural_select(locale, ROBO_I18N_PLURAL_ORDINAL, &ops, &cat);
robo_i18n_plural_has_rules(locale, ROBO_I18N_PLURAL_ORDINAL);
robo_i18n_plural_categories(locale, ROBO_I18N_PLURAL_ORDINAL, cats, cap, &n);
robo_i18n_plural_category_name(cat);
robo_i18n_plural_category_from_name("few", &cat);
```

- Thread-safe: read-only tables; locale/operands are caller-owned values.
- Unknown locale without rules → category `other` (not an error).
- Invalid decimal (NaN/∞) → `ROBO_INVALID_ARGUMENT`.

## Operands

Same exact-decimal model as Goal 4.4. Trailing zeros are preserved in operands
(`1` vs `1.0` vs `1.00`). Whether a locale’s ordinal rules *use* `v`/`w` depends
on CLDR (English ordinals key primarily on `i`, so `1.0` still selects `one`).

## MessageFormat

```text
{n, selectordinal,
  =1 {exact}
  one {#st}
  two {#nd}
  few {#rd}
  other {#th}}
```

Exact selectors (`=N`) are evaluated **before** ordinal categories. `#` expands
via number formatting. Parser/AST is shared with `plural` (kind `MF_ORDINAL`).

## Testing

- Official CLDR `@integer`/`@decimal` ordinal rows in
  `Tests/Data/robo_plural_samples.txt` (via `SuiteRoboI18NPlural`)
- Goal 4.18 suite `SuiteRoboI18NOrdinal.c`: locale matrix, cardinal≠ordinal,
  MF exact/`selectordinal`, fuzz
- Docs: this file; overview in `i18n/README.md`

## Performance

Selection is bytecode walk over immutable tables — no heap, no rule parsing,
no cache (locale resolve is a short parent walk). Suitable for hot message
formatting paths.

# RoboI18N tooling (Goals 4.1–4.3)

## Offline builds

`Build.ps1` compiles committed IANA + CLDR tables. It does **not** download
or regenerate data.

## IANA (BCP 47 Preferred-Value)

```powershell
.\runtime\Tools\i18n\Fetch-Iana.ps1
python .\runtime\Tools\i18n\generate_iana.py
```

## CLDR (locale-data + number symbols)

```powershell
.\runtime\Tools\i18n\Fetch-Cldr.ps1
python .\runtime\Tools\i18n\generate_cldr.py --write-golden
python .\runtime\Tools\i18n\generate_cldr.py --repro
```

Pins: `robo_i18n_version.py` / `manifest.json`.

| Pin | Value |
|---|---|
| CLDR | **46** (JSON package 46.0.0) |
| Unicode (from CLDR metadata) | **16.0.0** |
| Generator | `generate_cldr.py` **1.2.0** |

Goal 4.3 extracts from `numbers/*.json`:

- `symbols-numberSystem-*` (decimal, group, ±, nan, infinity)
- `decimalFormats-numberSystem-*.standard`
- `minimumGroupingDigits`
- primary/secondary grouping sizes parsed from patterns

Attribution: `CLDR_ATTRIBUTION.md`.

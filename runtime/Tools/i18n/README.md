# RoboI18N tooling (Goal 4.1)

## Offline builds

`Build.ps1` uses committed IANA-derived tables. It does **not** download
IANA or CLDR data.

## Fetch + generate

```powershell
.\runtime\Tools\i18n\Fetch-Iana.ps1
python .\runtime\Tools\i18n\generate_iana.py
```

Pins: `robo_i18n_version.py` / `manifest.json`.

## CLDR

Not imported yet. Architecture recorded in `manifest.json` → `cldr`.
When adding CLDR tables, follow the same pattern as `Tools/ucd/`
(version pin, checksums, explicit generate, committed outputs).

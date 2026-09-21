# Third-party data & licenses (Goal 4.27)

RoboI18N does **not** copy Chromium/ICU/Blink implementation code. Upstream data
is fetched offline and compiled into `robo_*_data.c` tables.

| Source | Version / pin | License / attribution | Generation |
|---|---|---|---|
| Unicode UCD / Emoji / UCA | 16.0.0 / Emoji 16.0 | Unicode License — `Tools/ucd/README.md` | `Tools/ucd/generate_*.py` |
| CLDR | 46 (JSON 46.0.0) | Unicode CLDR License — `Tools/i18n/CLDR_ATTRIBUTION.md` | `generate_cldr.py` et al. |
| IANA Language Subtag Registry | File-Date 2026-05-05 | IANA — `IANA_ATTRIBUTION.md` | `generate_iana.py` |
| IANA tzdb | 2026c | Public domain / ICANN — `TZDB_ATTRIBUTION.md` | `generate_tzdb.py` |
| ISO 4217 codes | Via CLDR `codeMappings` | As CLDR | currency generator |
| CLDR transforms | curated subset | As CLDR | `generate_translit.py` |

Pins: `Tools/i18n/robo_i18n_version.py`, `Tools/ucd/robo_unicode_version.py`,
`i18n/DataManifest.md`, `Tools/i18n/manifest.json`.

Upgrade: fetch → regenerate → `verify_i18n_pipeline.py --update-golden --update-manifest --report`
→ update suite version expectations → Debug/Release/Sanitize.

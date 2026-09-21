# RoboI18N Data / Version Manifest (Goals 4.26–4.27)

Authoritative machine-readable source: `Tools/i18n/manifest.json` +
`Tools/i18n/robo_i18n_version.py` + `Tools/ucd/robo_unicode_version.py`.

**API freeze:** `RoboI18nApiFrozen=1` (Phase 4).

| Domain | Version / source |
|---|---|
| RoboI18N API | **0.27.0** |
| I18N generator | **1.25.0** |
| Unicode UCD | **16.0.0** |
| Emoji | **16.0** |
| UCA / DUCET | **16.0.0** |
| CLDR | **46** (package 46.0.0) |
| CLDR Unicode association | **16.0.0** |
| IANA language-subtag File-Date | **2026-05-05** |
| IANA tzdb | **2026c** |
| ISO 4217 | **CLDR-46-codeMappings** |

## Compatibility rule

UCD version, UCA version, and CLDR Unicode association must be identical
(currently **16.0.0**). Mixing CLDR releases or UCD releases
in one generation is forbidden.

## Offline policy

Build.ps1 compiles committed tables only; Fetch-*.ps1 + generate_*.py and verify_i18n_pipeline.py are explicit developer steps.

## Regression locales

`en`, `fr`, `de`, `es`, `tr`, `ru`, `pl`, `uk`, `ar`, `fa`, `ur`, `he`, `hi`, `bn`, `th`, `zh`, `ja`, `ko`

## Generated artifacts

- `Include/RoboI18N/RoboI18NVersion.h`
- `Include/RoboI18N/RoboIanaData.h`
- `Source/RoboI18N/data/robo_iana_data.c`
- `Include/RoboI18N/RoboCldrData.h`
- `Source/RoboI18N/data/robo_cldr_data.c`
- `Include/RoboI18N/RoboPluralData.h`
- `Source/RoboI18N/data/robo_plural_data.c`
- `Tests/Data/robo_plural_samples.txt`
- `Include/RoboI18N/RoboCalData.h`
- `Source/RoboI18N/data/robo_cal_data.c`
- `Include/RoboI18N/RoboRelData.h`
- `Source/RoboI18N/data/robo_rel_data.c`
- `Include/RoboI18N/RoboHcData.h`
- `Source/RoboI18N/data/robo_hc_data.c`
- `Include/RoboI18N/RoboDispData.h`
- `Source/RoboI18N/data/robo_disp_data.c`
- `Include/RoboI18N/RoboUnitData.h`
- `Source/RoboI18N/data/robo_unit_data.c`
- `Include/RoboI18N/RoboMeasurePrefData.h`
- `Source/RoboI18N/data/robo_measure_pref_data.c`
- `Include/RoboI18N/RoboCurrencyData.h`
- `Source/RoboI18N/data/robo_currency_data.c`
- `Include/RoboI18N/RoboMsgData.h`
- `Source/RoboI18N/data/robo_msg_data.c`
- `Include/RoboI18N/RoboTzData.h`
- `Source/RoboI18N/data/robo_tz_data.c`
- `Include/RoboI18N/RoboUcaData.h`
- `Source/RoboI18N/data/robo_uca_data.c`
- `Include/RoboI18N/RoboCollData.h`
- `Source/RoboI18N/data/robo_coll_data.c`
- `Include/RoboI18N/RoboTranslitData.h`
- `Source/RoboI18N/data/robo_translit_data.c`

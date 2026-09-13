# RoboI18N tooling (Goals 4.1–4.27)

## Offline builds

`Build.ps1` compiles committed IANA + CLDR + RoboMsg + tzdb + UCA + translit tables.
It does **not** download or regenerate data.

## Completeness / freeze (Goals 4.26–4.27)

```powershell
python .\runtime\Tools\i18n\verify_i18n_pipeline.py
python .\runtime\Tools\i18n\verify_i18n_pipeline.py --update-golden
python .\runtime\Tools\i18n\verify_i18n_pipeline.py --update-manifest --report
```

Pins: `robo_i18n_version.py` (`ROBO_I18N_API_FROZEN`). Docs: `runtime/i18n/Phase4.md`.

## IANA (BCP 47 Preferred-Value)

```powershell
.\runtime\Tools\i18n\Fetch-Iana.ps1
python .\runtime\Tools\i18n\generate_iana.py
```

## CLDR (locale-data + numbers + plurals + calendars + relative + display)

```powershell
.\runtime\Tools\i18n\Fetch-Cldr.ps1
python .\runtime\Tools\i18n\generate_cldr.py --write-golden
python .\runtime\Tools\i18n\generate_cldr.py --repro
```

Or individually:

```powershell
python .\runtime\Tools\i18n\generate_calendar.py --write-golden
python .\runtime\Tools\i18n\generate_relative.py --write-golden
python .\runtime\Tools\i18n\generate_display.py --write-golden
python .\runtime\Tools\i18n\generate_hour_cycle.py
python .\runtime\Tools\i18n\validate_locale_consistency.py
```

## IANA tzdb (Goal 4.14)

```powershell
.\runtime\Tools\i18n\Fetch-Tzdb.ps1
python .\runtime\Tools\i18n\generate_tzdb.py --write-golden
python .\runtime\Tools\i18n\generate_tzdb.py --repro --check
```

Pin: `TZDB_VERSION` = **2026c**. Outputs: `RoboTzData.h` / `robo_tz_data.c`.
Docs: `runtime/i18n/TZDB.md`, attribution `TZDB_ATTRIBUTION.md`.

## Application messages (Goal 4.11)

```powershell
python .\runtime\Tools\i18n\write_demo_messages.py   # optional: refresh demo JSON
python .\runtime\Tools\i18n\generate_messages.py --write-golden
```

Source: `Tools/i18n/messages/demo/*.robomsg.json` → `RoboMsgData` + `RMSG` blob.
Spec: `runtime/i18n/RoboMsg.md`.

Pins: `robo_i18n_version.py` / `manifest.json`.

| Pin | Value |
|---|---|
| CLDR | **46** (JSON package 46.0.0) |
| Unicode (from CLDR metadata) | **16.0.0** |
| IANA tzdb | **2026c** |
| Generator | **1.14.0** |
| RoboI18N API | **0.15.0** |

### Calendar / date-time / interval data

`weekData`, `calendarData`, `dates/*/ca-*.json` → `RoboCalData.*` via
`generate_calendar.py` (names, styles, `availableFormats`, **`intervalFormats`**).

### Relative time data (Goals 4.7 / 4.19)

`dates/*/dateFields.json` → `RoboRelData.*` via `generate_relative.py`.

Fields: year/quarter/month/week/day/hour/minute/second × long/short/narrow,
with `relative-type-{−2,−1,0,1,2}` keywords and past/future plural patterns.

### Unit formatting data (Goal 4.19)

`units/*/units.json` → `RoboUnitData.*` via `generate_units.py`.

Curated CLDR type-subtype keys × long/short/narrow, plural `unitPattern`s,
`perUnitPattern`, and compound `per`/`times` patterns. Runtime: `RoboI18NUnit.h`.

### Measurement preferences (Goal 4.20)

`supplemental/unitPreferenceData.json` + temperature rows from
`measurementData.json` → `RoboMeasurePrefData.*` via `generate_measure_prefs.py`.
Runtime: `RoboI18NMeasure.h`.

### Currency (Goal 4.21)

`currencyData.json` + `codeMappings.json` + `currencyFormats-*` + plural names →
`RoboCurrencyData.*` via `generate_currency.py`. Runtime: `RoboI18NCurrency.h`.

### Display names (Goal 4.10)

`localenames/*/{languages,scripts,territories,localeDisplayNames}.json`,
`numbers/*/currencies.json`, `dates/*/timeZoneNames.json`,
`supplemental/metaZones.json` → `RoboDispData.*` via `generate_display.py`.

Runtime API: `RoboI18NDisplayNames.h`. Fallback: display locale → parents →
`en` → identifier or `ROBO_NOT_FOUND`. Does not invent translations.
`dialect` context is reserved (standard names only in 4.10).

Attribution: `CLDR_ATTRIBUTION.md`, `TZDB_ATTRIBUTION.md`.

## Interval formatting (Goal 4.7)

API: `RoboI18NDateTimeInterval.h`.

1. Compute greatest differing field (`y` > `M` > `d` > `a` > `H`/`h` > `m` > `s`).
2. Look up `intervalFormats[skeleton][letter]` with locale inheritance.
3. Split pattern at the first **repeated** pattern letter (second run); format
   start with the left part and end with the right part.
4. If no row / no repeat → `intervalFormatFallback` (`{0}` / `{1}`).
5. Equal endpoints → single skeleton format (no range).

Reversed intervals: rejected unless `normalize_reversed` is set.

Instant intervals convert both endpoints with the **same** zone (IANA via tzdb).
Ambiguous local results are refused.

## Relative-time formatting (Goals 4.7 / 4.19)

API: `RoboI18NRelativeTime.h`.

```text
value + unit
  → AUTO: relative-type keywords (−2…+2) when CLDR provides them
  → else past/future pattern by cardinal plural + {0} number
```

Goal 4.7 foundation; Goal 4.19 adds −2/+2 special forms (locale-dependent).

Modes: `ROBO_I18N_REL_NUMERIC_AUTO` | `_ALWAYS`.

## Unit formatting (Goal 4.19)

API: `RoboI18NUnit.h`. See `i18n/Units.md`.

## Known limitations

* Flexible day periods / full interval skeleton matching heuristics beyond
  exact + fallback are minimal.
* Localized zone *display names* come from CLDR display-name tables, not tzdb.
* islamic/hebrew/persian/chinese interval formatting → `ROBO_NOT_SUPPORTED`.
* Leap seconds not modeled (POSIX 86400-second days).
* Collation: root UCA full; selected CLDR remaps; full LDML for all locales later.
* Units: curated allowlist (not full CLDR unit catalog); no unit parsing.
* Logical UTF-8 only (no bidi visual reordering).
* Formatters are single-owner (no global mutable caches).

# CLDR attribution (Goal 4.2)

Roboticela Runtime embeds a **selected subset** of Unicode CLDR data,
generated offline into committed C tables (`RoboCldrData.h` /
`robo_cldr_data.c`).

## Source

| Field | Value |
|---|---|
| Project | Unicode Common Locale Data Repository (CLDR) |
| Release | **46** (JSON package **46.0.0**) |
| Unicode version in CLDR metadata | **16.0.0** |
| License | Unicode License v3 (see https://www.unicode.org/license.txt) |
| Distribution | https://github.com/unicode-org/cldr-json |

## Categories imported

### Goal 4.2
- likely subtags
- numbering systems (+ per-locale default/native ids)
- calendar preference data (identifiers only)
- measurement system preferences
- parent locale overrides

### Goal 4.3
- number symbols (decimal, group, plus, minus, nan, infinity)
- standard decimal patterns
- minimum grouping digits
- primary/secondary grouping sizes derived from patterns

### Goal 4.4
- cardinal plural rules (`plurals.json`)
- ordinal plural rules (`ordinals.json`)

### Goal 4.5
- weekData (first day / weekend)
- calendarData (Japanese era start dates)
- calendar names + date/time/dateTime patterns (`dates/*/ca-*.json`)

### Goal 4.6
- narrow month/weekday names
- `availableFormats` skeletons (same `ca-*.json` sources)
- consumed at runtime by `RoboI18NDateTimeFormat` (no extra CLDR packages)

### Goal 4.7
- `intervalFormats` / `intervalFormatFallback` from `ca-*.json`
- `dateFields.json` relative + relativeTime patterns (fetched per date locale)
- consumed by `RoboI18NDateTimeInterval` and `RoboI18NRelativeTime`

### Goal 4.9
- CLDR `common/collation/*.xml` metadata + selected remaps (`RoboCollData`)
- Unicode UCA DUCET `allkeys.txt` **16.0.0** (`RoboUcaData`) — separate UCA pin
- consumed by `RoboI18NCollation`

### Goal 4.10
- locale display names: languages, scripts, territories, keys/types
  (calendars, numbering systems), localeDisplayPattern
- currency displayName / symbol / symbol-alt-narrow
- timeZoneNames (exemplarCity, metazone long names, gmtFormat) + metaZones
- generated into `RoboDispData` / consumed by `RoboI18NDisplayNames`

### Goal 4.15
- timeZoneNames short generic/standard/daylight; zone-level name overrides
- gmtZeroFormat, regionFormat-type-*, fallbackFormat
- historical metaZones `usesMetazone` `_from`/`_to` ranges
- recursive exemplar cities; timezone display API + DateTimeFormat `z`/`v`/`V`/`O`

## Not imported here

Percent/compact number patterns, units measure formatting, full message
catalogs, and complete LDML collation rule compilation for every locale
(esp. CJK) remain for later goals. IANA tzdb is a separate pin
(see `i18n/TZDB.md`).

## Policy

- Do not invent locale data.
- Do not mix CLDR releases in one generation.
- Normal `Build.ps1` does not download CLDR/UCA; use `Fetch-Cldr.ps1` /
  `Fetch-Uca.ps1` then generators explicitly.
- Unicode UCD / Emoji versions are independent pins under `Tools/ucd/`.
- UCA **16.0.0** must match RoboUnicode **16.0.0** (do not mix with ICU 17 data).

# Time-zone display names (Goal 4.15)

Localized **presentation** of time zones from CLDR — not tzdb transition rules
(Goal 4.14). Language: **C**.

## Separation

| Layer | Responsibility |
|---|---|
| `RoboI18NTimeZone` | IANA identity, offsets, DST, transitions |
| `RoboDispData` + display API | Metazone/zone names, exemplar cities, GMT patterns |
| `RoboI18NDateTimeFormat` | Pattern letters `z`/`v`/`V`/`O` → display API |

## Styles

`I18nTimezoneNameStyle`:

| Style | Meaning |
|---|---|
| `IDENTIFIER` | Requested id (aliases keep requested spelling) |
| `EXEMPLAR_CITY` | CLDR exemplar city |
| `LONG_*` / `SHORT_*` GENERIC/STANDARD/DAYLIGHT | Explicit CLDR forms |
| `SPECIFIC_LONG` / `SPECIFIC_SHORT` | Std vs DST from instant (`is_dst_at`) |
| `GMT` | Locale `gmtFormat` + `hourFormat` (+ `gmtZeroFormat`) |

Preferred API: `I18nTimezoneDisplayNameAt(tz, instant, style, …)`.
Wrapper without instant: GMT on IANA → `RoboInvalidArgument`.

## Lookups

1. Resolve **canonical** IANA id for data (aliases share tables).
2. Pick metazone via historical `usesMetazone` ranges (`from` ≤ t < `to`).
3. Prefer **zone-specific** CLDR names, then **metazone** names.
4. Apply style fallbacks (never invent translations).

### Fallback (by style family)

- **SPECIFIC_SHORT:** short specific → short generic → long generic → exemplar → GMT → id  
- **SPECIFIC_LONG / LONG_GENERIC:** long forms → exemplar → GMT → id  
- **EXEMPLAR:** CLDR city → last path segment with `_`→` ` (structural id form, not a translation) → id  
- **GMT:** always formats offset (numbering-system digits applied)  
- **IDENTIFIER:** requested id  

Missing CLDR → `FALLBACK_CODE` (default) or `RoboNotFound`.

## GMT / numbering

Uses CLDR `gmtFormat`, `gmtZeroFormat`, `hourFormat`. ASCII digits in the
offset payload are remapped through the display locale’s default numbering
system (`I18nLocaleDefaultNumberingSystem`).

## Formatter mapping

| Pattern | Style |
|---|---|
| `z`–`zzz` | `SPECIFIC_SHORT` |
| `zzzz` | `SPECIFIC_LONG` |
| `v` / `vvvv` | short / long generic |
| `V` | identifier |
| `VV`–`VVVV` | exemplar city |
| `O` / `OOOO` | GMT |

ISO `Z` / `X` / `x` remain numeric offset forms.

## Data generation

```powershell
python .\runtime\Tools\i18n\generate_display.py --write-golden
```

Offline `Build.ps1` compiles committed `RoboDispData` only.
Pins: CLDR **46**, Unicode **16.0.0**, generator **1.14.0**, API **0.15.0**,
tzdb pin recorded for correlation (**2026c**).

## Ownership / threads

Immutable generated tables; pure functions; no global mutable cache.

## Related

- Rules / conversions: `i18n/TZDB.md`
- Attribution: `Tools/i18n/CLDR_ATTRIBUTION.md`

# Units & Relative Time (Goal 4.19)

Goal **4.7** shipped the relative-time *foundation* (model, CLDR `dateFields`
tables, AUTO vs ALWAYS, plural + number expansion). Goal **4.19** completes
the localized formatting layer (special −2/+2 keywords) and adds CLDR
**measurement unit** formatting with a conversion foundation.

## Architecture

```text
measurement value
      ↓
unit conversion   (optional; SI / affine; not locale data)
      ↓
localized number formatting   (RoboI18NNumber)
      ↓
localized unit formatting     (CLDR unitPattern / compound)
```

Relative time stays orthogonal: caller supplies `(value, unit)`; formatter
does not “pick” a calendar distance.

## CLDR data

| Item | Value |
|---|---|
| CLDR | **46** (JSON **46.0.0**) |
| Package | `cldr-units-full` → `units/*/units.json` |
| Relative | existing `dates/*/dateFields.json` (extended −2/+2) |
| Generator | `Tools/i18n/generate_units.py`, `generate_relative.py` |
| Tables | `RoboUnitData.h` / `robo_unit_data.c`, `RoboRelData.*` |
| Fetch | `Fetch-Cldr.ps1` (units locales) |

Generated tables record CLDR/Unicode versions, are deterministic (golden
hashes), and are looked up at runtime — **no** XML/JSON parsing in the
runtime. Locale fallback: tag → language → CLDR parents → `en`.

Curated coverage: **60** common units × **long/short/narrow** × **19**
locales (not the full ~188-unit CLDR catalog).

## Unit API

`RoboI18NUnit.h`

- `robo_i18n_unit_formatter_init` / `destroy`
- `robo_i18n_unit_format` / `_i64` / `_double`
- `robo_i18n_unit_format_per` — compound “num per den” via `perUnitPattern`
  or compound `{0} per {1}`
- Precomposed compounds (e.g. `speed-kilometer-per-hour`,
  `consumption-liter-per-100-kilometer`) format as ordinary unit ids
- `robo_i18n_unit_convert` / `_i64` — quantity conversion (exact vs approximate)
- `robo_i18n_unit_id_from_cldr_key`, `robo_i18n_unit_has_patterns`

**Ownership / lifetime:** stack-allocatable formatter; no heap of its own;
pattern pointers are immortal. **Thread safety:** one owner thread or external
lock. **Invalid unit / locale:** `ROBO_NOT_SUPPORTED` / fallback to English
patterns. **Parsing:** not in this goal.

### Conversion exactness

| Exact (rational SI) | Approximate |
|---|---|
| Metric length/mass/area/volume prefixes | Customary (mile, pound, gallon, …) |
| Duration week/day/hour/…/ms | Mean month/year |
| Digital decimal (1000) bit→TB | Temperature affine (°C/°F/K) |
| Frequency / power (W, kW) / kWh | Horsepower, psi, calorie, radian↔degree |

Cross-quantity conversion is rejected.

## Relative time (4.19 completion)

AUTO mode now consults CLDR `relative-type--2` … `relative-type-2` when
present (e.g. Spanish `anteayer` / `pasado mañana`). Locales without those
keywords (English) keep numeric past/future patterns. ALWAYS mode is
unchanged.

## Cardinal vs unit plurals

Unit patterns use **cardinal** plural categories (same engine as Goal 4.4).
Ordinal plurals (Goal 4.18) are not used for units.

## Testing

`SuiteRoboI18NUnit.c` — styles, locales (incl. RTL/CJK), compound per,
conversion, fallback, fuzz; plus relative ±2 keyword checks.

## Non-goals

- General unit *parsing* (later)
- Full MeasureUnit algebra / mixed units (ft+in)
- Currency / percent as units
- Replacing the Goal 4.7 relative foundation

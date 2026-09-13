# Measurement Systems & Unit Preferences (Goal 4.20)

Goal **4.19** formats a unit you already chose. Goal **4.20** decides
**which unit** a locale / preference context should use.

```text
Locale
  ↓
Measurement preferences   (this goal)
  ↓
Preferred unit
  ↓
Optional conversion       (RoboI18NUnit)
  ↓
Localized formatting      (RoboI18NUnit)
```

Keep these concepts separate:

| Concept | Role |
|---|---|
| Measurement system | metric / US / UK (CLDR `measurementSystem`) |
| Preferred unit | Category+usage unit pick (`unitPreferenceData`) |
| Unit conversion | Dimensional / temperature math |
| Unit formatting | Locale patterns + plurals + numbers |

## CLDR sources

| Data | Package |
|---|---|
| `measurementData.json` | systems + temperature-category systems |
| `unitPreferenceData.json` | category → usage → region → unit list |
| CLDR | **46** / Unicode **16.0.0** |
| Generator | `Tools/i18n/generate_measure_prefs.py` |
| Tables | `RoboMeasurePrefData.*` |

Region matching uses the locale region (maximized when absent), then `001`.
Unsupported CLDR unit names in a preference list are skipped until a curated
Goal 4.19 unit is found.

## API

`RoboI18NMeasure.h`

- `robo_i18n_measure_prefs_init` — locale-derived context (no OS reads)
- `robo_i18n_measure_prefs_set_system` / `set_unit` — overrides
- `robo_i18n_measure_resolve_unit` / `preferred_unit`
- `robo_i18n_measure_format_converted` — prefer → convert → format

### Precedence

1. Explicit unit argument  
2. Per-category override on the context  
3. Unicode locale extension `ms` (affects system; preference lists still region-based)  
4. CLDR `unitPreferenceData` (region → `001`)  
5. Measurement-system heuristic defaults  

Explicit requests are never replaced by locale defaults.

### Volume note

Category `VOLUME` with usage `DEFAULT` resolves through CLDR **`fluid`**
preferences (liter / gallon), because CLDR’s volume/`default` list is
cubic-length oriented and mostly outside the curated unit set.

### Temperature

Uses `measurementSystem-category-temperature` exceptions when present;
otherwise Fahrenheit only for US-style systems, else Celsius. Conversion
stays in `robo_i18n_unit_convert`.

## Ownership / thread safety

Preference contexts are stack value types (no heap). Share only when
immutable after init, or synchronize externally. Generated tables are
immortal/read-only.

## Testing

`SuiteRoboI18NMeasure.c` — system matrix, language≠region, overrides,
`ms` extension, convert+format, intl smoke, fuzz.

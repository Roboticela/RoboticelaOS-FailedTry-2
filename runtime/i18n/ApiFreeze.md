# ApiFreeze (Goal 4.27)

**Status:** Phase 4 public API is **frozen** at RoboI18N **0.27.0**
(`ROBO_I18N_API_FROZEN == 1`).

## Frozen public surface

Applications may depend on:

* `RoboI18N/RoboI18N.h` (umbrella)
* All `RoboI18N/RoboI18N*.h` headers (locale, number, plural, datetime, …)
* `RoboI18N/RoboI18NVersion.h` pin macros

## Not public (internal / generated)

Do **not** treat these as application API (may change without notice):

* `Robo*Data.h` / `robo_*_data.c` generated tables (`RoboCldrData`, `RoboTzData`, …)
* Generator scripts under `Tools/i18n/`
* Test-only fixtures under `Tests/Data/`

## Stability rules

| Concern | Contract |
|---|---|
| Naming | `robo_i18n_*` / `ROBO_I18N_*` |
| Errors | `robo_result_t` + subsystem error enums (parse/dtparse) |
| Ownership | Documented per type: value types vs heap (`*_destroy` / catalog close) |
| Lifetime | Formatters/parsers valid until destroy; generated tables immortal |
| Thread safety | Formatters/parsers **not** synchronized; generated data immutable; process default locale caller-synchronized |
| UTF-8 | See `Utf8Contract.md` — lengths are **bytes** unless named otherwise |
| Enums | Numeric values stable within 0.27.x; new members append only |
| Versioning | Breaking changes require bumping `ROBO_I18N_VERSION` and clearing freeze only via a new Phase milestone |

## Intentional non-goals (frozen as unsupported)

Percent/compact/scientific number formats, full ICU MessageFormat parity, FTS indexing,
holiday calendars, leap-second TAI, 100% CLDR locale×feature coverage, full transform catalog.

# RoboI18N Locale Service (Goal 4.16)

Central runtime locale context for Roboticela. Language: **C**.

Locale, language, timezone, calendar, and numbering system remain **independent**
concepts. The service resolves effective settings without conflating them.

## RoboLocale model

`robo_i18n_locale_t` (Goal 4.1) holds identity:

| Field | Meaning |
|---|---|
| language / script / region / variants | BCP 47 subtags |
| extensions | Opaque Unicode + other extensions (e.g. `u-nu-arab`) |
| privateuse | Private-use subtags |

`robo_i18n_locale_settings_t` is the resolved **runtime context**:

| Setting | Source ranking |
|---|---|
| calendar | override → `u-ca` → CLDR locale default → `gregorian` |
| numbering system | override → `u-nu` → CLDR default → `latn` |
| hour cycle | override → `u-hc` → CLDR `timeData` by region → `h23` |
| collation type | override → `u-co` → `standard` |
| timezone | override → `u-tz` → **unset** (never inferred from language) |

Empty timezone after resolve means “no zone attached.” Convenience
`robo_i18n_locale_resolve_timezone` falls back to **UTC** for formatters that
require a concrete zone.

## Canonicalization

Use `robo_i18n_locale_parse_canonical` (Goal 4.1). Aliases, casing, and
deprecated subtags follow IANA + CLDR mappings already in the parser.
Unicode `u` keys are parsed by `robo_i18n_locale_unicode_keywords` without
discarding unknown keys (kept in `other` when re-serialized).

## Unicode extensions

Known keys: `ca`, `nu`, `hc`, `co`, `tz`. Unknown keys are **not** remapped to
known ones. Get/set via `robo_i18n_locale_get_keyword` /
`robo_i18n_locale_set_keyword`.

## Matching & negotiation

`robo_i18n_locale_matches` ranks EXACT > REGION > SCRIPT > LANGUAGE > NONE.
`robo_i18n_locale_negotiate` walks the preference list and picks the best
available match for the first preference that finds at least LANGUAGE.

Empty preference list / empty available → error. Invalid entries are skipped
during list parse (Goal 4.1). Duplicates keep preference order.

## Fallback chains

`robo_i18n_locale_fallback_chain` uses **CLDR data parents**
(`robo_i18n_locale_data_parent`), not naive string truncation. Caps at
`ROBO_I18N_FALLBACK_MAX` (16). Message catalogs use the same parent walk.

Typical conceptual path (actual edges come from CLDR `parentLocales`):

```text
language-script-region → language-script → language-region → language → root
```

## Likely subtags

`robo_i18n_locale_maximize` / `robo_i18n_locale_minimize` (Goal 4.2) remain the
authority. The locale service uses maximize when resolving region-based hour
cycles for language-only tags.

## Default / application / effective locale

| API | Role |
|---|---|
| `robo_i18n_get_default_locale` | Process default (`und` after `robo_i18n_init`) |
| `robo_i18n_set_application_locale` | App preference (process-wide; caller syncs) |
| `robo_i18n_get_effective_locale` | Application if set, else default |

No desktop OS API is called inside RoboI18N. Platform locale plugs in later
behind this abstraction.

## Capability queries

Stable booleans/ids only — no raw table exposure:

- `robo_i18n_locale_supports_calendar`
- `robo_i18n_locale_supports_numbering`
- `robo_i18n_locale_supports_collation_type`
- `robo_i18n_locale_has_display_names`
- `robo_i18n_locale_has_timezone_display`
- `robo_i18n_timezone_id_is_known`

## Subsystem wiring

| Subsystem | Uses |
|---|---|
| Number | `resolve_numbering` (honors `u-nu`) |
| Date/time format | resolve calendar / timezone / hour cycle; numbers via Number |
| Collation | `resolve_collation` when type argument is NULL |
| Messages | `locale_data_parent` fallback |
| Date/time format | `u-ca` / `u-nu` / hour cycle; NULL calendar → Gregorian unless `u-ca` |

Style formatters keep Gregorian when the calendar argument is NULL and no `u-ca`
is set, because shipped pattern tables are Gregorian-keyed. `resolve_calendar`
still returns the CLDR locale-default calendar for callers that need it.

## Override precedence (all formatters)

```text
explicit formatter / API override
  > Unicode u-extension on the locale
  > CLDR locale / region default
  > runtime fallback
```

Timezone: locale identity alone never becomes a zone.

## Thread safety

- Locale structs and settings are value types (immutable once copied).
- Generated tables are read-only.
- Application locale is process-wide mutable — synchronize if multi-threaded.
- No C-library `setlocale` global is used.

## Caching

No unbounded global locale object cache. Parsing identical tags repeatedly is
cheap relative to formatting; measure before adding a cache.

## Data versions

`robo_i18n_data_versions` exposes API, generator, CLDR, Unicode, tzdb, and IANA
registry file date. Hour-cycle table: `robo_hc_data.c` from CLDR `timeData`.

## Examples

```c
robo_i18n_locale_t loc;
robo_i18n_locale_settings_t st;
robo_i18n_locale_parse_canonical("fa-IR-u-nu-arabext", 17, &loc);
robo_i18n_locale_resolve(&loc, NULL, &st);
/* st.numbering_system == "arabext", src == EXTENSION */

robo_i18n_locale_overrides_t ov;
robo_i18n_locale_overrides_init(&ov);
ov.numbering_system = "latn";
robo_i18n_locale_resolve(&loc, &ov, &st);
/* st.numbering_system == "latn", src == OVERRIDE */
```

See `SuiteRoboI18NLocaleService.c` for matching, fallback, maximize matrix,
cross-subsystem numbering consistency, and robustness cases.

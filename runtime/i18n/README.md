# RoboI18N

Internationalization foundation for Roboticela Runtime (**Goal 4.1**).

## Scope (this goal)

| In scope | Out of scope |
|---|---|
| BCP 47 / RFC 5646 language tags | Number / date / time formatting |
| Parse, format, canonicalize | Plural rules |
| Equality / matching / truncation fallback | Collation |
| Default locale abstraction | Message translation / catalogs |
| Locale preference lists | Automatic language detection |
| IANA Preferred-Value tables | Full CLDR dataset |
| CLDR integration **architecture** | Fonts / shaping / layout |

## Standards relationship

```text
BCP 47 (RFC 5646)     — language tag grammar + IANA registry
Unicode (RoboUnicode) — scripts, text algorithms (separate)
CLDR                  — locale data for formatting (future; versioned separately)
```

**Unicode Script ≠ locale.** Example:

| Concept | Example |
|---|---|
| Language | Arabic (`ar`) |
| Unicode Script | `Arab` (property on code points) |
| Region | Saudi Arabia (`SA`) |
| Locale | `ar-SA` |

Locale parsing is **not** language identification/detection from arbitrary text.

## Versions

| Dataset | Pin |
|---|---|
| RoboI18N API | `0.1.0` (`RoboI18NVersion.h`) |
| IANA Language Subtag Registry | File-Date **2026-05-05** |
| CLDR | `none` (architecture only; not imported) |
| Unicode / UCD | independent — see `Tools/ucd/` |

`Unicode version != CLDR version != IANA File-Date`.

## Locale model

Value type `robo_i18n_locale_t` (no heap for parse/format):

- `language` — lowercase primary language
- `script` — Title case (e.g. `Hans`) or empty
- `region` — UPPER alpha or digit region, or empty
- `variants[]` — lowercase
- `extensions` — e.g. `u-ca-gregory`
- `privateuse` — e.g. `x-private`

### Distinctions

| Concept | Meaning |
|---|---|
| Syntactic validity | Tag matches BCP 47 structure |
| Canonical form | Correct casing + IANA Preferred-Value |
| Preferred locale | Future (CLDR likely subtags / matching) |

Unknown-but-structurally-valid subtags are accepted (not catastrophic failure).

## Public API

```c
#include "RoboI18N/RoboI18N.h"

robo_i18n_locale_parse(tag, len, &loc);
robo_i18n_locale_parse_canonical(tag, len, &loc);
robo_i18n_locale_canonicalize(&loc);
robo_i18n_locale_format(&loc, buf, cap, &n);
robo_i18n_locale_equal(&a, &b);
robo_i18n_locale_matches(&range, &candidate);
robo_i18n_locale_parent(&loc, &parent);

robo_i18n_set_default_locale(&loc);
robo_i18n_get_default_locale(&out);   /* default und until set */

robo_i18n_locale_list_parse(text, len, &list);
robo_i18n_locale_list_destroy(list);
```

## Fallback algorithm

Truncation parent (resource-lookup style), **not** CLDR likely-subtags:

1. Remove private-use  
2. Else remove extensions  
3. Else remove last variant  
4. Else remove region  
5. Else remove script  
6. Else `ROBO_NOT_FOUND` (language-only has no parent)

Example: `zh-Hant-TW` → `zh-Hant` → `zh`.

## Default locale

Process-wide copy. Initial / unset → `und` (**not** hard-coded `en-US`).

Goal 4.1 does not read OS settings. Future sources: OS, app config, env, user prefs, API override.

**Thread safety:** locale values are immutable copies; default locale get/set must be synchronized by the caller until RoboThread provides primitives. Prefer passing locales explicitly.

## Memory ownership

| Object | Ownership |
|---|---|
| `robo_i18n_locale_t` | Caller stack/value; no destroy |
| `robo_i18n_locale_list_t` | Heap via RoboMem; `*_destroy` |

## Data pipeline

```text
IANA language-subtag-registry
        ↓ Fetch-Iana.ps1 (explicit)
Tools/i18n/source/          (gitignored)
        ↓ generate_iana.py
RoboIanaData.h + robo_iana_data.c   (committed Preferred-Value maps)
```

Normal `Build.ps1` does **not** download IANA/CLDR data.

CLDR future layout: `Tools/i18n/cldr_manifest.json` (see file).

## Layout

```text
Include/RoboI18N/
Source/RoboI18N/
Tools/i18n/
i18n/README.md          ← this file
Tests/Suites/SuiteRoboI18N.c
```

## Attribution

IANA Language Subtag Registry is published by IANA. See `Tools/i18n/IANA_ATTRIBUTION.md`.
CLDR (future) has its own Unicode/CLDR license — documented when imported.

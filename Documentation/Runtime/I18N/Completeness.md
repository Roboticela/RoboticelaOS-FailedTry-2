# Completeness & Conformance (Goals 4.26–4.27)

Goal 4.26 delivered the completeness pipeline; Goal 4.27 freezes Phase 4.

## Authoritative pins

See **`DataManifest.md`** and `Tools/i18n/manifest.json`.

| Pin | Source of truth |
|---|---|
| RoboI18N API / generator / CLDR / tzdb / IANA / UCA / ISO4217-via-CLDR | `Tools/i18n/robo_i18n_version.py` → `Include/RoboI18N/RoboI18NVersion.h` |
| Unicode UCD / Emoji | `Tools/ucd/robo_unicode_version.py` → `Include/RoboUnicode/RoboUnicodeVersion.h` |
| API freeze | `RoboI18nApiFrozen` |

**Compatibility rule:** UCD version, UCA version, and CLDR Unicode association
must match (currently **16.0.0**). Do not mix CLDR releases.

## Verification pipeline

```text
python runtime/Tools/i18n/verify_i18n_pipeline.py
python runtime/Tools/i18n/verify_i18n_pipeline.py --update-golden
python runtime/Tools/i18n/verify_i18n_pipeline.py --update-manifest --report
python runtime/Tools/ucd/generate_all.py --verify-only
```

## Conformance / Phase 4 suites

* `SuiteRoboI18NConformance.c` — Goal 4.26 corpus
* `SuiteRoboI18NPhase4.c` — Goal 4.27 regional matrix, round trips, threads, Unicode-heavy

CLDR plural samples: `Tests/Data/robo_plural_samples.txt`.

## Limitation classification (4.27 review)

| Limitation | Class |
|---|---|
| Not every CLDR locale × feature | intentionally unsupported / data unavailable |
| Full LDML collation for every locale (esp. CJK) | intentionally unsupported (later) |
| Curated transliteration ID allowlist | intentionally unsupported |
| Search does not auto-transliterate | intentional boundary |
| ISO 4217 via CLDR only | intentional data-source choice |
| Leap seconds omitted | intentionally unsupported |
| Process default locale not lock-free | intentional until RoboThread |

No open implementation gaps were left as bugs for Phase 4 freeze.

## Upgrade procedure

1. Bump pins in `robo_i18n_version.py` / `robo_unicode_version.py`.
2. Fetch + regenerate tables.
3. `verify_i18n_pipeline.py --update-golden --update-manifest --report`
4. Update suite version expectations; Debug / Release / Sanitize.
5. If public API changes: bump version and revisit `RoboI18nApiFrozen`.

## Licensing

See **`Licenses.md`**.

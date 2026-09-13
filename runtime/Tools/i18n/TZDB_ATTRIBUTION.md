# IANA Time Zone Database — Attribution

Roboticela Runtime embeds compact tables derived from the
**IANA Time Zone Database (tzdb)**.

| Field | Value |
|---|---|
| tzdb release | **2026c** |
| Pin | `Tools/i18n/robo_i18n_version.py` → `TZDB_VERSION` |
| Generator | `generate_tzdb.py` (RoboI18N generator stream **1.13.0**) |
| Primary source | https://www.iana.org/time-zones |
| Distribution | `tzdata2026c.tar.gz` / `tzcode2026c.tar.gz` (tools offline tree) |
| Runtime outputs | `RoboTzData.h`, `robo_tz_data.c` (generated; do not hand-edit) |

## License

The tz database is in the public domain (see IANA tz-link and LICENSE in the
tz distribution). Roboticela does not claim copyright on the underlying
zone rules; generated tables are mechanical transforms for offline runtime use.

## Pipeline notes

- Authoritative zone **rules** come from IANA text + TZif.
- When `source/tzdb-2026c/zoneinfo/` is absent, `generate_tzdb.py` may copy
  TZif files from the Python **`tzdata`** package (IANA-derived) for offline
  generation. Normal `Build.ps1` never downloads or regenerates.
- Leap seconds are **not** applied to RoboI18N instants (POSIX 86400-day model).

## Upgrade checklist

1. Bump `TZDB_VERSION` / `ROBO_TZDB_VERSION_STRING_PIN`
2. `Fetch-Tzdb.ps1` (verify checksums)
3. `python generate_tzdb.py --write-golden`
4. `python generate_tzdb.py --repro --check`
5. Full RoboTest + update this file / DEPENDENCIES if needed

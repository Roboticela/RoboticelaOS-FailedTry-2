# IANA Time Zone Database (tzdb) — Roboticela (Goal 4.14)

**Status:** integrated. Runtime embeds compact tables generated from IANA
tzdb **2026c** (TZif + POSIX TZ footer expansion).

## Model

| Concept | Role |
|---|---|
| **Instant** | Absolute timeline (`unix_ms`, POSIX 86400-second days) |
| **Local date/time** | Civil fields in a zone (not an instant) |
| **UTC offset** | Seconds east of UTC at a specific instant |
| **Time zone** | Named IANA rules / fixed offset / UTC — **not** a locale |
| **Calendar** | Civil field arithmetic / naming (Gregorian, …) |
| **Locale** | Language/region preferences for formatting |

A time zone is **not** a fixed UTC offset. Offsets change with history and DST.

### Leap seconds

**Not modeled.** Instants use continuous Unix time with 86400-second days
(POSIX-style). APIs are internally consistent with that policy. Do not claim
TAI/UTC leap-second modeling.

### Timestamp range

Generated transitions cover civil **1800-01-01 … 2100-01-01 UTC**.
Before the first stored transition, the zone’s initial offset applies.
After the last explicit TZif transition, the POSIX TZ footer (when present)
is expanded through 2100. Beyond that range, behavior is last-offset /
initial-offset only — not unlimited historical correctness.

## Public API

Header: `Include/RoboI18N/RoboI18NTimeZone.h`

| Operation | Notes |
|---|---|
| `timezone_from_id` / `utc` / `fixed_seconds` | Lookup; unknown IANA → `ROBO_NOT_FOUND` |
| `canonical_id` | Alias → canonical zone id; requested id preserved on handle |
| `offset_at` / `is_dst_at` / `abbreviation_at` | Instant queries |
| `to_local` | Instant → local civil |
| `to_utc` / `to_utc_ex` | Local → instant with disambiguation |
| `next_transition` / `prev_transition` | Strictly after / before instant |
| `robo_i18n_tzdb_version_string` | e.g. `"2026c"` |

**Ownership:** value types; tables are immutable process data.  
**Threads:** re-entrant pure functions over const tables.  
**SYSTEM:** unresolved (`local` / `system`) — convert → `ROBO_NOT_SUPPORTED`.

### Local → instant resolution

| Result | Meaning |
|---|---|
| `UNIQUE` | One instant |
| `AMBIGUOUS` | Fold (repeated local interval) |
| `NONEXISTENT` | Gap (skipped local interval) |

Policies (`robo_i18n_tz_disambiguation_t`):

| Policy | Ambiguous | Nonexistent |
|---|---|---|
| `EARLIER` | earlier instant (default `to_utc`) | fail `ROBO_INVALID_ARGUMENT` |
| `LATER` | later instant | fail |
| `REJECT` | fail | fail |

Never silently guess.

### Fixed-offset zones

`+05:00`, `UTC-04:30`, `timezone_fixed_seconds` — range **±18h**, second
granularity. Distinct from named IANA zones. Canonical id is the numeric label.

### Aliases

tzdb `Link` lines (e.g. `US/Eastern` → `America/New_York`) share one transition
table. `timezone_id` returns the **requested** id; `canonical_id` returns the
**canonical** zone id.

## Data pipeline

```text
Fetch-Tzdb.ps1
    ↓
Tools/i18n/source/tzdb-2026c/   (IANA text + optional zoneinfo/)
    ↓
generate_tzdb.py
    ↓
Include/RoboI18N/RoboTzData.h
Source/RoboI18N/data/robo_tz_data.c
Tools/i18n/tzdb_golden_hashes.json
```

Normal `Build.ps1` compiles committed tables only — **no network**, no generator.

### TZif + POSIX footer

Modern zoneinfo stores a finite transition list and a POSIX TZ string for
later years (e.g. `EST5EDT,M3.2.0,M11.1.0`). The generator:

1. Parses Links / `zone1970.tab` from IANA text
2. Reads TZif (prefer `source/tzdb-*/zoneinfo/`; else copy from Python `tzdata`)
3. Expands the footer into transitions through 2100
4. Emits compact C tables + validates ordering / offsets / uniqueness

`zoneinfo/` under source is regenerable and gitignored.

### Compact representation

- String pool (ids + abbreviations)
- Sorted zone table: initial offset/DST/abbr + transition slice
- Flat transition array: `(unix_sec, offset, is_dst, abbr)` — binary searched
- Alias table: alias → canonical id (no duplicated rules)

**Complexity:** zone/alias lookup O(log N); transition lookup O(log T) per zone.

### Regeneration

```powershell
.\runtime\Tools\i18n\Fetch-Tzdb.ps1
python .\runtime\Tools\i18n\generate_tzdb.py --write-golden
python .\runtime\Tools\i18n\generate_tzdb.py --repro --check
```

## Integration

```text
Instant → TimeZone → UTC offset / local date-time → Calendar → Locale → Formatter
```

Timezone conversion lives in `RoboI18NTimeZone`, not in formatters.
Interval / relative-time / message format receive timezone-aware values via
existing public datetime/timezone APIs — no duplicated conversion stacks.

### Instant vs local vs calendar arithmetic

| Operation | Meaning |
|---|---|
| Instant + duration | Absolute ms (86400000 ≠ “one local day” across DST) |
| Local + period in zone | Civil fields in zone, then convert with disambiguation |
| Calendar add | Field arithmetic independent of zone until converted |

## Performance

Binary search on sorted transitions; avoid full linear scans for offset queries.
Local→UTC probes a small set of candidate offsets near the civil probe.

## Validation

`generate_tzdb.py` checks unique canonical ids, ordered transitions, offset
bounds (±18h), and golden SHA-256 reproducibility (`--repro --check`).

## Licensing

See `Tools/i18n/TZDB_ATTRIBUTION.md`. IANA tzdb is public-domain style;
record exact release and source.

## Chromium

Reference only for architectural ideas. No Chromium timezone code, data, or
build dependency.

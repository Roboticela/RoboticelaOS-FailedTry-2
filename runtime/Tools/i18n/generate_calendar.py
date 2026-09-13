#!/usr/bin/env python3
"""
Emit RoboI18N calendar metadata tables from CLDR 46 (Goals 4.5–4.7).

Compact tables for names, style patterns, availableFormats, and intervalFormats.
Formatting algorithms live in RoboI18NDateTimeFormat / DateTimeInterval.

Inputs under Tools/i18n/source/cldr-46.0.0/:
  supplemental/weekData.json
  supplemental/calendarData.json  (Japanese era _start)
  dates/<locale>/ca-*.json

Outputs:
  Include/RoboI18N/RoboCalData.h
  Source/RoboI18N/data/robo_cal_data.c
  Tools/i18n/calendar_golden_hashes.json

Weekday policy:
  CLDR day names sun..sat are stored in that order in day-name arrays.
  API helpers expose ISO weekday Mon=1 .. Sun=7 (not ICU UCAL_SUNDAY=1).
  Mapping: ISO dow -> storage index = iso % 7  (Mon=1..Sat=6, Sun=7->0).
"""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

from robo_i18n_version import (
    CLDR_JSON_PACKAGE,
    CLDR_UNICODE_VERSION,
    CLDR_VERSION,
    GENERATOR_VERSION,
)

HERE = Path(__file__).resolve().parent
RUNTIME = HERE.parent.parent
SRC = HERE / "source" / f"cldr-{CLDR_JSON_PACKAGE}"
OUT_H = RUNTIME / "Include" / "RoboI18N" / "RoboCalData.h"
OUT_C = RUNTIME / "Source" / "RoboI18N" / "data" / "robo_cal_data.c"
GOLDEN = HERE / "calendar_golden_hashes.json"

WEEKDATA_SHA256 = "7667d479f415418288ed1ab0390bb440892626f4549612d875becc4334708c77"

# CLDR day name -> ISO weekday (Mon=1 .. Sun=7)
CLDR_DAY_TO_ISO = {
    "mon": 1,
    "tue": 2,
    "wed": 3,
    "thu": 4,
    "fri": 5,
    "sat": 6,
    "sun": 7,
}
# Storage order for day-name arrays (CLDR keys).
DAY_STORAGE_ORDER = ("sun", "mon", "tue", "wed", "thu", "fri", "sat")
FORMAT_KEYS = ("full", "long", "medium", "short")
ERA_INDEX_RE = re.compile(r"^\d+$")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def load_json(path: Path) -> dict:
    if not path.is_file():
        raise SystemExit(f"missing CLDR source: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def utf8_c_bytes(s: str) -> str:
    """Emit a C string literal with UTF-8 escapes; avoid \\xNN eating following hex digits."""
    raw = s.encode("utf-8")
    parts: list[str] = []
    pending_hex = False
    for b in raw:
        if 32 <= b < 127 and b not in (0x22, 0x5C):
            ch = chr(b)
            if pending_hex and ch in "0123456789abcdefABCDEF":
                parts.append('""')
            parts.append(ch)
            pending_hex = False
        else:
            parts.append(f"\\x{b:02x}")
            pending_hex = True
    return "".join(parts)


def verify_versions(supp: dict) -> None:
    ver = supp.get("supplemental", {}).get("version", {})
    cv = str(ver.get("_cldrVersion", ""))
    uv = str(ver.get("_unicodeVersion", ""))
    if not cv.startswith(CLDR_VERSION):
        raise SystemExit(f"CLDR version mismatch: file={cv!r} pin={CLDR_VERSION!r}")
    if uv != CLDR_UNICODE_VERSION:
        raise SystemExit(
            f"Unicode version in CLDR data {uv!r} != pin {CLDR_UNICODE_VERSION!r}"
        )


def cldr_day_to_iso(name: str) -> int:
    key = name.strip().lower()
    if key not in CLDR_DAY_TO_ISO:
        raise SystemExit(f"unknown CLDR day name {name!r}")
    return CLDR_DAY_TO_ISO[key]


def parse_era_start(s: str) -> tuple[int, int, int]:
    """Parse CLDR era _start 'Y-M-D' or '-Y-M-D' into (year, month, day)."""
    if not isinstance(s, str) or not s:
        raise SystemExit(f"bad era _start {s!r}")
    if s.startswith("-"):
        rest = s[1:]
        parts = rest.split("-")
        if len(parts) != 3:
            raise SystemExit(f"bad negative era _start {s!r}")
        return -int(parts[0]), int(parts[1]), int(parts[2])
    parts = s.split("-")
    if len(parts) != 3:
        raise SystemExit(f"bad era _start {s!r}")
    return int(parts[0]), int(parts[1]), int(parts[2])


def format_value(val: object) -> str | None:
    """CLDR patterns may be a string or {_value, _numbers, ...} object."""
    if isinstance(val, str) and val:
        return val
    if isinstance(val, dict):
        inner = val.get("_value")
        if isinstance(inner, str) and inner:
            return inner
    return None


def pick_format(mapping: dict, key: str) -> str:
    """Prefer non-alt-ascii keys; fall back to *-alt-ascii."""
    got = format_value(mapping.get(key))
    if got is not None:
        return got
    got = format_value(mapping.get(f"{key}-alt-ascii"))
    if got is not None:
        return got
    raise SystemExit(f"missing format key {key!r}")


def build_calendar_outputs() -> tuple[str, str, dict[str, str], dict[str, int]]:
    week_path = SRC / "supplemental" / "weekData.json"
    caldata_path = SRC / "supplemental" / "calendarData.json"
    week_j = load_json(week_path)
    caldata_j = load_json(caldata_path)
    verify_versions(week_j)
    verify_versions(caldata_j)

    week_sha = sha256_file(week_path)
    if week_sha != WEEKDATA_SHA256:
        raise SystemExit(
            f"weekData.json sha256 mismatch:\n  expected {WEEKDATA_SHA256}\n  actual   {week_sha}"
        )

    week = week_j["supplemental"]["weekData"]
    first_map = week["firstDay"]
    wstart_map = week["weekendStart"]
    wend_map = week["weekendEnd"]
    if "001" not in first_map or "001" not in wstart_map or "001" not in wend_map:
        raise SystemExit("weekData missing required region 001")

    jap = caldata_j["supplemental"]["calendarData"].get("japanese")
    if not isinstance(jap, dict) or not isinstance(jap.get("eras"), dict):
        raise SystemExit("calendarData.json missing japanese.eras")

    pool: list[str] = []
    pool_index: dict[str, int] = {}

    def intern(s: str) -> int:
        if s not in pool_index:
            pool_index[s] = len(pool)
            pool.append(s)
        return pool_index[s]

    # --- week tables (ISO Mon=1..Sun=7), one row per firstDay region ---
    def weekend_for(region: str, mapping: dict) -> int:
        name = mapping.get(region, mapping["001"])
        return cldr_day_to_iso(str(name))

    week_rows: list[tuple[int, int, int, int]] = []
    for region in sorted(first_map.keys()):
        week_rows.append(
            (
                intern(region),
                cldr_day_to_iso(str(first_map[region])),
                weekend_for(region, wstart_map),
                weekend_for(region, wend_map),
            )
        )
    week_rows.sort(key=lambda r: pool[r[0]])

    # --- Japanese era starts ---
    era_start_rows: list[tuple[int, int, int, int]] = []
    for era_key, info in jap["eras"].items():
        if not ERA_INDEX_RE.match(str(era_key)):
            continue
        if not isinstance(info, dict) or "_start" not in info:
            raise SystemExit(f"japanese era {era_key} missing _start")
        y, m, d = parse_era_start(str(info["_start"]))
        if m < 1 or m > 12 or d < 1 or d > 31:
            raise SystemExit(f"japanese era {era_key} bad date {info['_start']!r}")
        era_start_rows.append((int(era_key), y, m, d))
    era_start_rows.sort(key=lambda r: (r[1], r[2], r[3], r[0]))

    # --- locale calendar symbols ---
    dates_dir = SRC / "dates"
    if not dates_dir.is_dir():
        raise SystemExit(f"missing {dates_dir}")

    symbol_rows: list[dict] = []
    era_name_rows: list[tuple[int, int, int, int]] = []
    avail_rows: list[tuple[int, int, int, int]] = []
    interval_fallback_rows: list[tuple[int, int, int]] = []
    interval_rows: list[tuple[int, int, int, int, int]] = []  # loc,cal,skel,diff,pat

    for path in sorted(dates_dir.rglob("ca-*.json")):
        data = load_json(path)
        main = data.get("main") or {}
        if len(main) != 1:
            raise SystemExit(f"unexpected dates shape: {path}")
        loc = next(iter(main))
        calendars = (main[loc].get("dates") or {}).get("calendars") or {}
        if not calendars:
            raise SystemExit(f"no calendars in {path}")

        for cal_id, cal in sorted(calendars.items()):
            if not isinstance(cal, dict):
                raise SystemExit(f"bad calendar {cal_id} in {path}")

            months_fmt = (cal.get("months") or {}).get("format") or {}
            months_wide = months_fmt.get("wide") or {}
            months_abbr = months_fmt.get("abbreviated") or {}
            months_narrow = months_fmt.get("narrow") or months_abbr
            days_fmt = (cal.get("days") or {}).get("format") or {}
            days_wide = days_fmt.get("wide") or {}
            days_abbr = days_fmt.get("abbreviated") or {}
            days_narrow = days_fmt.get("narrow") or days_abbr
            day_periods = (
                (cal.get("dayPeriods") or {}).get("format") or {}
            ).get("abbreviated") or {}
            date_formats = cal.get("dateFormats") or {}
            time_formats = cal.get("timeFormats") or {}
            dt_formats = cal.get("dateTimeFormats") or {}
            available = dt_formats.get("availableFormats") or {}
            eras = cal.get("eras") or {}
            era_abbr = eras.get("eraAbbr") or {}

            loc_i = intern(loc)
            cal_i = intern(cal_id)

            mw = []
            ma = []
            mn = []
            for mi in range(1, 13):
                key = str(mi)
                w = months_wide.get(key)
                a = months_abbr.get(key)
                n = months_narrow.get(key) or a
                if not isinstance(w, str) or not w:
                    raise SystemExit(f"missing month wide {key} in {path}/{cal_id}")
                if not isinstance(a, str) or not a:
                    raise SystemExit(f"missing month abbr {key} in {path}/{cal_id}")
                if not isinstance(n, str) or not n:
                    raise SystemExit(f"missing month narrow {key} in {path}/{cal_id}")
                mw.append(intern(w))
                ma.append(intern(a))
                mn.append(intern(n))

            dw = []
            da = []
            dn = []
            for day in DAY_STORAGE_ORDER:
                w = days_wide.get(day)
                a = days_abbr.get(day)
                n = days_narrow.get(day) or a
                if not isinstance(w, str) or not w:
                    raise SystemExit(f"missing day wide {day} in {path}/{cal_id}")
                if not isinstance(a, str) or not a:
                    raise SystemExit(f"missing day abbr {day} in {path}/{cal_id}")
                if not isinstance(n, str) or not n:
                    raise SystemExit(f"missing day narrow {day} in {path}/{cal_id}")
                dw.append(intern(w))
                da.append(intern(a))
                dn.append(intern(n))

            am = day_periods.get("am")
            pm = day_periods.get("pm")
            if not isinstance(am, str) or not am:
                raise SystemExit(f"missing dayPeriods am in {path}/{cal_id}")
            if not isinstance(pm, str) or not pm:
                raise SystemExit(f"missing dayPeriods pm in {path}/{cal_id}")

            date_idx = [intern(pick_format(date_formats, k)) for k in FORMAT_KEYS]
            time_idx = [intern(pick_format(time_formats, k)) for k in FORMAT_KEYS]
            dt_idx = [intern(pick_format(dt_formats, k)) for k in FORMAT_KEYS]

            symbol_rows.append(
                {
                    "locale": loc_i,
                    "calendar": cal_i,
                    "months_wide": mw,
                    "months_abbr": ma,
                    "months_narrow": mn,
                    "days_wide": dw,
                    "days_abbr": da,
                    "days_narrow": dn,
                    "am": intern(am),
                    "pm": intern(pm),
                    "date": date_idx,
                    "time": time_idx,
                    "dt": dt_idx,
                }
            )

            for era_key, name in era_abbr.items():
                if not ERA_INDEX_RE.match(str(era_key)):
                    continue
                if not isinstance(name, str) or not name:
                    raise SystemExit(f"bad eraAbbr {era_key} in {path}/{cal_id}")
                era_name_rows.append((loc_i, cal_i, int(era_key), intern(name)))

            if isinstance(available, dict):
                for skel, pat in available.items():
                    if not isinstance(skel, str) or "-alt-" in skel:
                        continue
                    got = format_value(pat)
                    if got is None:
                        continue
                    avail_rows.append((loc_i, cal_i, intern(skel), intern(got)))

            intervals = dt_formats.get("intervalFormats") or {}
            if isinstance(intervals, dict):
                fb = intervals.get("intervalFormatFallback")
                fb_s = format_value(fb) if fb is not None else None
                if isinstance(fb_s, str) and fb_s:
                    interval_fallback_rows.append((loc_i, cal_i, intern(fb_s)))
                for skel, diffs in intervals.items():
                    if skel == "intervalFormatFallback" or not isinstance(skel, str):
                        continue
                    if "-alt-" in skel or not isinstance(diffs, dict):
                        continue
                    for diff_letter, pat in diffs.items():
                        if not isinstance(diff_letter, str) or len(diff_letter) != 1:
                            continue
                        if "-alt-" in diff_letter:
                            continue
                        got = format_value(pat)
                        if got is None:
                            continue
                        interval_rows.append(
                            (loc_i, cal_i, intern(skel), intern(diff_letter), intern(got))
                        )

    symbol_rows.sort(key=lambda r: (pool[r["locale"]], pool[r["calendar"]]))
    era_name_rows.sort(key=lambda r: (pool[r[0]], pool[r[1]], r[2]))
    avail_rows.sort(key=lambda r: (pool[r[0]], pool[r[1]], pool[r[2]]))
    interval_fallback_rows.sort(key=lambda r: (pool[r[0]], pool[r[1]]))
    interval_rows.sort(
        key=lambda r: (pool[r[0]], pool[r[1]], pool[r[2]], pool[r[3]])
    )

    # Detect duplicate symbol keys
    seen_sym = set()
    for row in symbol_rows:
        key = (row["locale"], row["calendar"])
        if key in seen_sym:
            raise SystemExit(
                f"duplicate symbols for {pool[row['locale']]}/{pool[row['calendar']]}"
            )
        seen_sym.add(key)

    checksums = {
        "weekData.json": week_sha,
        "calendarData.json": sha256_file(caldata_path),
    }
    for path in sorted(dates_dir.rglob("ca-*.json")):
        rel = path.relative_to(SRC).as_posix()
        checksums[rel] = sha256_file(path)

    h_text = "\n".join(
        [
            "/* Auto-generated by Tools/i18n/generate_calendar.py — DO NOT EDIT. */",
            f"/* Roboticela generator {GENERATOR_VERSION} | CLDR {CLDR_VERSION} | Unicode {CLDR_UNICODE_VERSION} */",
            "#ifndef ROBO_CAL_DATA_H",
            "#define ROBO_CAL_DATA_H",
            "",
            "#include <stddef.h>",
            "#include <stdint.h>",
            "",
            "#ifdef __cplusplus",
            'extern "C" {',
            "#endif",
            "",
            f'#define ROBO_CAL_CLDR_VERSION "{CLDR_VERSION}"',
            f'#define ROBO_CAL_UNICODE_VERSION "{CLDR_UNICODE_VERSION}"',
            "",
            "/* ISO weekday: Mon=1 .. Sun=7. Day-name arrays are stored sun..sat. */",
            "/* Storage index from ISO: (iso % 7) — Sun(7)->0, Mon(1)->1, … Sat(6)->6. */",
            "",
            "typedef struct robo_cal_week {",
            "    int32_t region;          /* string pool index */",
            "    uint8_t first_day;       /* ISO 1..7 */",
            "    uint8_t weekend_start;   /* ISO 1..7 */",
            "    uint8_t weekend_end;     /* ISO 1..7 */",
            "} robo_cal_week_t;",
            "",
            "typedef struct robo_cal_era_start {",
            "    int32_t era_index;",
            "    int16_t start_year;",
            "    uint8_t start_month; /* 1..12 */",
            "    uint8_t start_day;   /* 1..31 */",
            "} robo_cal_era_start_t;",
            "",
            "typedef struct robo_cal_symbols {",
            "    int32_t locale;",
            "    int32_t calendar;",
            "    int32_t months_wide[12];",
            "    int32_t months_abbr[12];",
            "    int32_t months_narrow[12];",
            "    int32_t days_wide[7];  /* sun..sat */",
            "    int32_t days_abbr[7];  /* sun..sat */",
            "    int32_t days_narrow[7]; /* sun..sat */",
            "    int32_t am;",
            "    int32_t pm;",
            "    int32_t date_full;",
            "    int32_t date_long;",
            "    int32_t date_medium;",
            "    int32_t date_short;",
            "    int32_t time_full;",
            "    int32_t time_long;",
            "    int32_t time_medium;",
            "    int32_t time_short;",
            "    int32_t datetime_full;",
            "    int32_t datetime_long;",
            "    int32_t datetime_medium;",
            "    int32_t datetime_short;",
            "} robo_cal_symbols_t;",
            "",
            "typedef struct robo_cal_era_name {",
            "    int32_t locale;",
            "    int32_t calendar;",
            "    int32_t era_index;",
            "    int32_t name;",
            "} robo_cal_era_name_t;",
            "",
            "typedef struct robo_cal_available_format {",
            "    int32_t locale;",
            "    int32_t calendar;",
            "    int32_t skeleton;",
            "    int32_t pattern;",
            "} robo_cal_available_format_t;",
            "",
            "typedef struct robo_cal_interval_fallback {",
            "    int32_t locale;",
            "    int32_t calendar;",
            "    int32_t pattern; /* {0} earlier, {1} later */",
            "} robo_cal_interval_fallback_t;",
            "",
            "typedef struct robo_cal_interval_format {",
            "    int32_t locale;",
            "    int32_t calendar;",
            "    int32_t skeleton;",
            "    int32_t greatest_diff; /* single letter string pool index */",
            "    int32_t pattern;",
            "} robo_cal_interval_format_t;",
            "",
            "extern const char *const robo_cal_strings[];",
            "extern const size_t robo_cal_string_count;",
            "",
            "extern const robo_cal_week_t robo_cal_week[];",
            "extern const size_t robo_cal_week_count;",
            "",
            "extern const robo_cal_era_start_t robo_cal_japanese_era_start[];",
            "extern const size_t robo_cal_japanese_era_start_count;",
            "",
            "extern const robo_cal_symbols_t robo_cal_symbols[];",
            "extern const size_t robo_cal_symbols_count;",
            "",
            "extern const robo_cal_era_name_t robo_cal_era_names[];",
            "extern const size_t robo_cal_era_names_count;",
            "",
            "extern const robo_cal_available_format_t robo_cal_available_formats[];",
            "extern const size_t robo_cal_available_formats_count;",
            "",
            "extern const robo_cal_interval_fallback_t robo_cal_interval_fallbacks[];",
            "extern const size_t robo_cal_interval_fallbacks_count;",
            "",
            "extern const robo_cal_interval_format_t robo_cal_interval_formats[];",
            "extern const size_t robo_cal_interval_formats_count;",
            "",
            "const char *robo_cal_str(int32_t idx);",
            "uint8_t robo_cal_week_first_day(const char *region);",
            "uint8_t robo_cal_week_weekend_start(const char *region);",
            "uint8_t robo_cal_week_weekend_end(const char *region);",
            "const robo_cal_week_t *robo_cal_week_find(const char *region);",
            "const robo_cal_symbols_t *robo_cal_symbols_find(const char *locale, const char *calendar);",
            "const robo_cal_era_start_t *robo_cal_era_start_find(int32_t era_index);",
            "size_t robo_cal_era_start_count(void);",
            "const robo_cal_era_name_t *robo_cal_era_name_find(const char *locale, const char *calendar, int32_t era_index);",
            "const robo_cal_available_format_t *robo_cal_available_format_find(const char *locale, const char *calendar, const char *skeleton);",
            "const robo_cal_available_format_t *robo_cal_available_formats_begin(const char *locale, const char *calendar, size_t *out_count);",
            "const robo_cal_interval_fallback_t *robo_cal_interval_fallback_find(const char *locale, const char *calendar);",
            "const robo_cal_interval_format_t *robo_cal_interval_format_find(const char *locale, const char *calendar, const char *skeleton, const char *greatest_diff);",
            "const char *robo_cal_version_string(void);",
            "const char *robo_cal_unicode_version_string(void);",
            "",
            "#ifdef __cplusplus",
            "}",
            "#endif",
            "",
            "#endif /* ROBO_CAL_DATA_H */",
            "",
        ]
    )

    c_lines: list[str] = [
        "/* Auto-generated by Tools/i18n/generate_calendar.py — DO NOT EDIT. */",
        f"/* CLDR {CLDR_VERSION} / Unicode {CLDR_UNICODE_VERSION} | generator {GENERATOR_VERSION} */",
        '#include "RoboI18N/RoboCalData.h"',
        "",
        "#include <string.h>",
        "",
        "const char *const robo_cal_strings[] = {",
    ]
    for s in pool:
        c_lines.append(f'    "{utf8_c_bytes(s)}",')
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cal_string_count = {len(pool)};")
    c_lines.append("")

    c_lines.append("const robo_cal_week_t robo_cal_week[] = {")
    for region_i, fd, ws, we in week_rows:
        c_lines.append(f"    {{{region_i}, {fd}u, {ws}u, {we}u}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cal_week_count = {len(week_rows)};")
    c_lines.append("")

    c_lines.append("const robo_cal_era_start_t robo_cal_japanese_era_start[] = {")
    for era_i, y, m, d in era_start_rows:
        c_lines.append(f"    {{{era_i}, {y}, {m}u, {d}u}},")
    c_lines.append("};")
    c_lines.append(
        f"const size_t robo_cal_japanese_era_start_count = {len(era_start_rows)};"
    )
    c_lines.append("")

    c_lines.append("const robo_cal_symbols_t robo_cal_symbols[] = {")
    for row in symbol_rows:
        mw = ", ".join(str(x) for x in row["months_wide"])
        ma = ", ".join(str(x) for x in row["months_abbr"])
        mn = ", ".join(str(x) for x in row["months_narrow"])
        dw = ", ".join(str(x) for x in row["days_wide"])
        da = ", ".join(str(x) for x in row["days_abbr"])
        dn = ", ".join(str(x) for x in row["days_narrow"])
        c_lines.append(
            "    {"
            f"{row['locale']}, {row['calendar']}, "
            f"{{{mw}}}, {{{ma}}}, {{{mn}}}, {{{dw}}}, {{{da}}}, {{{dn}}}, "
            f"{row['am']}, {row['pm']}, "
            f"{row['date'][0]}, {row['date'][1]}, {row['date'][2]}, {row['date'][3]}, "
            f"{row['time'][0]}, {row['time'][1]}, {row['time'][2]}, {row['time'][3]}, "
            f"{row['dt'][0]}, {row['dt'][1]}, {row['dt'][2]}, {row['dt'][3]}"
            "},"
        )
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cal_symbols_count = {len(symbol_rows)};")
    c_lines.append("")

    c_lines.append("const robo_cal_era_name_t robo_cal_era_names[] = {")
    for loc_i, cal_i, era_i, name_i in era_name_rows:
        c_lines.append(f"    {{{loc_i}, {cal_i}, {era_i}, {name_i}}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cal_era_names_count = {len(era_name_rows)};")
    c_lines.append("")

    c_lines.append("const robo_cal_available_format_t robo_cal_available_formats[] = {")
    for loc_i, cal_i, sk_i, pat_i in avail_rows:
        c_lines.append(f"    {{{loc_i}, {cal_i}, {sk_i}, {pat_i}}},")
    c_lines.append("};")
    c_lines.append(
        f"const size_t robo_cal_available_formats_count = {len(avail_rows)};"
    )
    c_lines.append("")

    c_lines.append("const robo_cal_interval_fallback_t robo_cal_interval_fallbacks[] = {")
    for loc_i, cal_i, pat_i in interval_fallback_rows:
        c_lines.append(f"    {{{loc_i}, {cal_i}, {pat_i}}},")
    c_lines.append("};")
    c_lines.append(
        f"const size_t robo_cal_interval_fallbacks_count = {len(interval_fallback_rows)};"
    )
    c_lines.append("")

    c_lines.append("const robo_cal_interval_format_t robo_cal_interval_formats[] = {")
    for loc_i, cal_i, sk_i, diff_i, pat_i in interval_rows:
        c_lines.append(f"    {{{loc_i}, {cal_i}, {sk_i}, {diff_i}, {pat_i}}},")
    c_lines.append("};")
    c_lines.append(
        f"const size_t robo_cal_interval_formats_count = {len(interval_rows)};"
    )
    c_lines.append("")

    c_lines.append(
        r"""
const char *robo_cal_str(int32_t idx)
{
    if (idx < 0 || (size_t)idx >= robo_cal_string_count) {
        return "";
    }
    return robo_cal_strings[idx];
}

static int robo_cal_cmp_region(const robo_cal_week_t *row, const char *region)
{
    return strcmp(robo_cal_strings[row->region], region);
}

const robo_cal_week_t *robo_cal_week_find(const char *region)
{
    size_t lo = 0;
    size_t hi = robo_cal_week_count;
    if (!region) {
        return NULL;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        int cmp = robo_cal_cmp_region(&robo_cal_week[mid], region);
        if (cmp == 0) {
            return &robo_cal_week[mid];
        }
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

static const robo_cal_week_t *robo_cal_week_resolve(const char *region)
{
    const robo_cal_week_t *row = robo_cal_week_find(region);
    if (!row) {
        row = robo_cal_week_find("001");
    }
    return row;
}

uint8_t robo_cal_week_first_day(const char *region)
{
    const robo_cal_week_t *row = robo_cal_week_resolve(region);
    return row ? row->first_day : 0u;
}

uint8_t robo_cal_week_weekend_start(const char *region)
{
    const robo_cal_week_t *row = robo_cal_week_resolve(region);
    return row ? row->weekend_start : 0u;
}

uint8_t robo_cal_week_weekend_end(const char *region)
{
    const robo_cal_week_t *row = robo_cal_week_resolve(region);
    return row ? row->weekend_end : 0u;
}

const robo_cal_symbols_t *robo_cal_symbols_find(const char *locale, const char *calendar)
{
    size_t lo = 0;
    size_t hi = robo_cal_symbols_count;
    if (!locale || !calendar) {
        return NULL;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const robo_cal_symbols_t *row = &robo_cal_symbols[mid];
        int cmp = strcmp(robo_cal_strings[row->locale], locale);
        if (cmp == 0) {
            cmp = strcmp(robo_cal_strings[row->calendar], calendar);
        }
        if (cmp == 0) {
            return row;
        }
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const robo_cal_era_start_t *robo_cal_era_start_find(int32_t era_index)
{
    size_t lo = 0;
    size_t hi = robo_cal_japanese_era_start_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        int32_t cur = robo_cal_japanese_era_start[mid].era_index;
        if (cur == era_index) {
            return &robo_cal_japanese_era_start[mid];
        }
        if (cur < era_index) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

size_t robo_cal_era_start_count(void)
{
    return robo_cal_japanese_era_start_count;
}

const robo_cal_era_name_t *robo_cal_era_name_find(const char *locale, const char *calendar, int32_t era_index)
{
    size_t lo = 0;
    size_t hi = robo_cal_era_names_count;
    if (!locale || !calendar) {
        return NULL;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const robo_cal_era_name_t *row = &robo_cal_era_names[mid];
        int cmp = strcmp(robo_cal_strings[row->locale], locale);
        if (cmp == 0) {
            cmp = strcmp(robo_cal_strings[row->calendar], calendar);
        }
        if (cmp == 0) {
            if (row->era_index == era_index) {
                return row;
            }
            cmp = row->era_index < era_index ? -1 : 1;
        }
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const robo_cal_available_format_t *robo_cal_available_format_find(
    const char *locale,
    const char *calendar,
    const char *skeleton)
{
    size_t lo = 0;
    size_t hi = robo_cal_available_formats_count;
    if (!locale || !calendar || !skeleton) {
        return NULL;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const robo_cal_available_format_t *row = &robo_cal_available_formats[mid];
        int cmp = strcmp(robo_cal_strings[row->locale], locale);
        if (cmp == 0) {
            cmp = strcmp(robo_cal_strings[row->calendar], calendar);
        }
        if (cmp == 0) {
            cmp = strcmp(robo_cal_strings[row->skeleton], skeleton);
        }
        if (cmp == 0) {
            return row;
        }
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const robo_cal_available_format_t *robo_cal_available_formats_begin(
    const char *locale,
    const char *calendar,
    size_t *out_count)
{
    size_t i;
    size_t start = (size_t)-1;
    size_t n = 0;
    if (out_count) {
        *out_count = 0;
    }
    if (!locale || !calendar) {
        return NULL;
    }
    for (i = 0; i < robo_cal_available_formats_count; i++) {
        const robo_cal_available_format_t *row = &robo_cal_available_formats[i];
        if (strcmp(robo_cal_strings[row->locale], locale) == 0 &&
            strcmp(robo_cal_strings[row->calendar], calendar) == 0) {
            if (start == (size_t)-1) {
                start = i;
            }
            n++;
        } else if (start != (size_t)-1) {
            break;
        }
    }
    if (n == 0) {
        return NULL;
    }
    if (out_count) {
        *out_count = n;
    }
    return &robo_cal_available_formats[start];
}

const robo_cal_interval_fallback_t *robo_cal_interval_fallback_find(
    const char *locale,
    const char *calendar)
{
    size_t lo = 0;
    size_t hi = robo_cal_interval_fallbacks_count;
    if (!locale || !calendar) {
        return NULL;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const robo_cal_interval_fallback_t *row = &robo_cal_interval_fallbacks[mid];
        int cmp = strcmp(robo_cal_strings[row->locale], locale);
        if (cmp == 0) {
            cmp = strcmp(robo_cal_strings[row->calendar], calendar);
        }
        if (cmp == 0) {
            return row;
        }
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const robo_cal_interval_format_t *robo_cal_interval_format_find(
    const char *locale,
    const char *calendar,
    const char *skeleton,
    const char *greatest_diff)
{
    size_t lo = 0;
    size_t hi = robo_cal_interval_formats_count;
    if (!locale || !calendar || !skeleton || !greatest_diff) {
        return NULL;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const robo_cal_interval_format_t *row = &robo_cal_interval_formats[mid];
        int cmp = strcmp(robo_cal_strings[row->locale], locale);
        if (cmp == 0) {
            cmp = strcmp(robo_cal_strings[row->calendar], calendar);
        }
        if (cmp == 0) {
            cmp = strcmp(robo_cal_strings[row->skeleton], skeleton);
        }
        if (cmp == 0) {
            cmp = strcmp(robo_cal_strings[row->greatest_diff], greatest_diff);
        }
        if (cmp == 0) {
            return row;
        }
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const char *robo_cal_version_string(void)
{
    return ROBO_CAL_CLDR_VERSION;
}

const char *robo_cal_unicode_version_string(void)
{
    return ROBO_CAL_UNICODE_VERSION;
}
""".strip(
            "\n"
        )
    )
    c_lines.append("")

    # era_start_find binary-searches by era_index, so keep a secondary index sorted by era_index
    # while the emitted table stays chronological by start date. Emit a note: find walks by index.
    # Fix: either sort by era_index for find, or use linear/secondary table.
    # Requirement: "sorted by start" for the table. Find by era_index needs unsorted-safe search.
    # Replace binary search on era_index with linear scan OR sort by era_index.
    # Chromium keeps chronological era starts; lookup by index is O(n) or via separate map.
    # Prefer: table sorted by start (as required); find uses linear scan on small N (~237).

    c_text = "\n".join(c_lines) + "\n"

    # Patch era_start_find to linear scan (table is sorted by start date, not era_index).
    c_text = c_text.replace(
        """const robo_cal_era_start_t *robo_cal_era_start_find(int32_t era_index)
{
    size_t lo = 0;
    size_t hi = robo_cal_japanese_era_start_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        int32_t cur = robo_cal_japanese_era_start[mid].era_index;
        if (cur == era_index) {
            return &robo_cal_japanese_era_start[mid];
        }
        if (cur < era_index) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}""",
        """const robo_cal_era_start_t *robo_cal_era_start_find(int32_t era_index)
{
    size_t i;
    /* Table is chronological by start date; indices are dense but not sorted here. */
    for (i = 0; i < robo_cal_japanese_era_start_count; i++) {
        if (robo_cal_japanese_era_start[i].era_index == era_index) {
            return &robo_cal_japanese_era_start[i];
        }
    }
    return NULL;
}""",
    )

    locales = {pool[r["locale"]] for r in symbol_rows}
    calendars = {pool[r["calendar"]] for r in symbol_rows}
    stats = {
        "locales": len(locales),
        "calendars": len(calendars),
        "symbol_rows": len(symbol_rows),
        "week_regions": len(week_rows),
        "japanese_eras": len(era_start_rows),
        "era_names": len(era_name_rows),
        "available_formats": len(avail_rows),
        "interval_fallbacks": len(interval_fallback_rows),
        "interval_formats": len(interval_rows),
        "string_pool": len(pool),
        "bytes_h": len(h_text.encode("utf-8")),
        "bytes_c": len(c_text.encode("utf-8")),
    }
    return h_text, c_text, checksums, stats


def write_calendar(*, repro: bool, write_golden: bool) -> int:
    h_text, c_text, checksums, stats = build_calendar_outputs()

    if repro:
        if not OUT_H.is_file() or not OUT_C.is_file():
            raise SystemExit("calendar repro failed: outputs missing")
        if OUT_H.read_text(encoding="utf-8") != h_text:
            raise SystemExit("calendar repro failed: header differs")
        if OUT_C.read_text(encoding="utf-8") != c_text:
            raise SystemExit("calendar repro failed: c differs")
        if GOLDEN.is_file():
            golden = json.loads(GOLDEN.read_text(encoding="utf-8"))
            got = {
                "RoboCalData.h": hashlib.sha256(h_text.encode("utf-8")).hexdigest(),
                "robo_cal_data.c": hashlib.sha256(c_text.encode("utf-8")).hexdigest(),
            }
            for name, digest in got.items():
                if golden.get(name) != digest:
                    raise SystemExit(f"calendar repro failed: golden mismatch {name}")
        print("Calendar generation reproducible OK")
        print("calendar stats:", json.dumps(stats))
        return 0

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(h_text, encoding="utf-8", newline="\n")
    OUT_C.write_text(c_text, encoding="utf-8", newline="\n")

    if write_golden:
        GOLDEN.write_text(
            json.dumps(
                {
                    "cldr_version": CLDR_VERSION,
                    "unicode_version": CLDR_UNICODE_VERSION,
                    "generator_version": GENERATOR_VERSION,
                    "RoboCalData.h": hashlib.sha256(h_text.encode("utf-8")).hexdigest(),
                    "robo_cal_data.c": hashlib.sha256(c_text.encode("utf-8")).hexdigest(),
                    "sources": checksums,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
            newline="\n",
        )
        print(f"Wrote {GOLDEN.name}")

    print(
        f"Wrote calendar tables: locales={stats['locales']} "
        f"symbol_rows={stats['symbol_rows']} week_regions={stats['week_regions']} "
        f"japanese_eras={stats['japanese_eras']} era_names={stats['era_names']} "
        f"available_formats={stats['available_formats']} "
        f"interval_formats={stats['interval_formats']} "
        f"pool={stats['string_pool']} bytes_h={stats['bytes_h']} bytes_c={stats['bytes_c']}"
    )
    return 0


if __name__ == "__main__":
    import sys

    args = sys.argv[1:]
    raise SystemExit(
        write_calendar(repro="--repro" in args, write_golden="--write-golden" in args)
    )

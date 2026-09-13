#!/usr/bin/env python3
"""
Generate RoboI18N locale display-name tables from CLDR (Goals 4.10 + 4.15).

Inputs under Tools/i18n/source/cldr-46.0.0/:
  localenames/*/languages.json, scripts.json, territories.json, localeDisplayNames.json
  numbers/*/currencies.json
  dates/*/timeZoneNames.json
  supplemental/metaZones.json

Outputs:
  Include/RoboI18N/RoboDispData.h
  Source/RoboI18N/data/robo_disp_data.c
  Tools/i18n/display_golden_hashes.json
"""

from __future__ import annotations

import hashlib
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from robo_i18n_version import (
    CLDR_JSON_PACKAGE,
    CLDR_UNICODE_VERSION,
    CLDR_VERSION,
    GENERATOR_VERSION,
    ROBO_I18N_VERSION,
    TZDB_VERSION,
)

HERE = Path(__file__).resolve().parent
RUNTIME = HERE.parent.parent
SRC = HERE / "source" / f"cldr-{CLDR_JSON_PACKAGE}"
OUT_H = RUNTIME / "Include" / "RoboI18N" / "RoboDispData.h"
OUT_C = RUNTIME / "Source" / "RoboI18N" / "data" / "robo_disp_data.c"
GOLDEN = HERE / "display_golden_hashes.json"

# Skip alt-* / count variants for compact v1 tables.
ALT_RE = re.compile(r"-alt-|-count-")

MZ_FROM_OPEN = -(2**63)
MZ_TO_OPEN = 2**63 - 1


def utf8_c_bytes(s: str) -> str:
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


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def ldn_section(data: dict, locale: str, section: str) -> dict:
    try:
        return data["main"][locale]["localeDisplayNames"][section]
    except KeyError:
        return {}


def filter_map(d: dict) -> dict[str, str]:
    out: dict[str, str] = {}
    for k, v in d.items():
        if not isinstance(v, str) or ALT_RE.search(k):
            continue
        out[k] = v
    return out


def parse_mz_boundary(s: str | None) -> int | None:
    """CLDR usesMetazone _from/_to: 'YYYY-MM-DD HH:MM' UTC."""
    if not s or not isinstance(s, str):
        return None
    try:
        dt = datetime.strptime(s, "%Y-%m-%d %H:%M").replace(tzinfo=timezone.utc)
        return int(dt.timestamp())
    except ValueError:
        return None


def walk_zone_tree(node: dict, parts: list[str], on_leaf):
    """Recurse CLDR zone tree; on_leaf(iana, info_dict)."""
    if not isinstance(node, dict):
        return
    # Leaf-ish: has exemplarCity or long/short without nested continents.
    has_name = any(k in node for k in ("exemplarCity", "long", "short"))
    nested = [
        (k, v)
        for k, v in node.items()
        if isinstance(v, dict) and not k.startswith("_") and k not in ("long", "short")
    ]
    if has_name and parts:
        on_leaf("/".join(parts), node)
    for k, v in nested:
        walk_zone_tree(v, parts + [k], on_leaf)


def main() -> int:
    write_golden = "--write-golden" in sys.argv
    loc_dir = SRC / "localenames"
    if not loc_dir.is_dir():
        print(f"missing {loc_dir}", file=sys.stderr)
        return 1

    pool: list[str] = []
    index: dict[str, int] = {}

    def intern(s: str) -> int:
        if s not in index:
            index[s] = len(pool)
            pool.append(s)
        return index[s]

    locales = sorted(p.name for p in loc_dir.iterdir() if p.is_dir())
    # Ensure "en" is present as ultimate name fallback table (CLDR root often empty).
    if "en" not in locales:
        print("warning: en missing from localenames", file=sys.stderr)

    # Tables of (display_loc_idx, code_idx, name_idx)
    languages: list[tuple[int, int, int]] = []
    scripts: list[tuple[int, int, int]] = []
    territories: list[tuple[int, int, int]] = []
    calendars: list[tuple[int, int, int]] = []
    numbering: list[tuple[int, int, int]] = []
    keys: list[tuple[int, int, int]] = []

    # Currency: (disp_loc, code, name, symbol, narrow)
    currencies: list[tuple[int, int, int, int, int]] = []

    # Patterns per display locale
    patterns: list[tuple[int, int, int, int]] = []  # loc, localePattern, sep, keyType

    # TZ: patterns + exemplar cities + metazone long/short + per-zone overrides
    # patterns: loc, gmt, gmtZero, hour, region, regionStd, regionDst, fallback
    tz_patterns: list[tuple[int, int, int, int, int, int, int, int]] = []
    tz_exemplars: list[tuple[int, int, int]] = []  # loc, iana_id, city
    # loc, mz, lg, ls, ld, sg, ss, sd
    tz_metazones: list[tuple[int, int, int, int, int, int, int, int]] = []
    # loc, iana, lg, ls, ld, sg, ss, sd
    tz_zones: list[tuple[int, int, int, int, int, int, int, int]] = []
    # iana, mz, from, to
    iana_mz: list[tuple[int, int, int, int]] = []

    for loc in locales:
        base = loc_dir / loc
        if not (base / "languages.json").is_file():
            print(f"skip incomplete locale {loc}", file=sys.stderr)
            continue
        loc_idx = intern(loc)
        lang = filter_map(ldn_section(load_json(base / "languages.json"), loc, "languages"))
        for code, name in sorted(lang.items()):
            languages.append((loc_idx, intern(code), intern(name)))

        scr = filter_map(ldn_section(load_json(base / "scripts.json"), loc, "scripts"))
        for code, name in sorted(scr.items()):
            scripts.append((loc_idx, intern(code), intern(name)))

        ter = filter_map(ldn_section(load_json(base / "territories.json"), loc, "territories"))
        for code, name in sorted(ter.items()):
            territories.append((loc_idx, intern(code), intern(name)))

        ldn_path = base / "localeDisplayNames.json"
        if ldn_path.is_file():
            ldn = load_json(ldn_path)["main"][loc]["localeDisplayNames"]
            pat = ldn.get("localeDisplayPattern", {})
            patterns.append(
                (
                    loc_idx,
                    intern(pat.get("localePattern", "{0} ({1})")),
                    intern(pat.get("localeSeparator", "{0}, {1}")),
                    intern(pat.get("localeKeyTypePattern", "{0}: {1}")),
                )
            )
            for code, name in sorted(filter_map(ldn.get("keys", {})).items()):
                keys.append((loc_idx, intern(code), intern(name)))
            types = ldn.get("types", {})
            for code, name in sorted(filter_map(types.get("calendar", {})).items()):
                calendars.append((loc_idx, intern(code), intern(name)))
            for code, name in sorted(filter_map(types.get("numbers", {})).items()):
                numbering.append((loc_idx, intern(code), intern(name)))

        cur_path = SRC / "numbers" / loc / "currencies.json"
        if cur_path.is_file():
            cur_root = load_json(cur_path)["main"][loc]["numbers"]["currencies"]
            for code, info in sorted(cur_root.items()):
                if not isinstance(info, dict):
                    continue
                name = info.get("displayName")
                if not isinstance(name, str):
                    continue
                sym = info.get("symbol")
                if not isinstance(sym, str):
                    sym = code
                narrow = info.get("symbol-alt-narrow")
                if not isinstance(narrow, str):
                    narrow = sym
                currencies.append(
                    (loc_idx, intern(code), intern(name), intern(sym), intern(narrow))
                )

        tz_path = SRC / "dates" / loc / "timeZoneNames.json"
        if tz_path.is_file():
            tzn = load_json(tz_path)["main"][loc]["dates"]["timeZoneNames"]
            tz_patterns.append(
                (
                    loc_idx,
                    intern(tzn.get("gmtFormat", "GMT{0}")),
                    intern(tzn.get("gmtZeroFormat", tzn.get("gmtFormat", "GMT").replace("{0}", "").strip() or "GMT")),
                    intern(tzn.get("hourFormat", "+HH:mm;-HH:mm")),
                    intern(tzn.get("regionFormat", "{0}")),
                    intern(tzn.get("regionFormat-type-standard", "{0}")),
                    intern(tzn.get("regionFormat-type-daylight", "{0}")),
                    intern(tzn.get("fallbackFormat", "{1} ({0})")),
                )
            )

            def on_zone(iana: str, info: dict) -> None:
                ex = info.get("exemplarCity")
                if isinstance(ex, str):
                    tz_exemplars.append((loc_idx, intern(iana), intern(ex)))
                long = info.get("long") if isinstance(info.get("long"), dict) else {}
                short = info.get("short") if isinstance(info.get("short"), dict) else {}
                vals = (
                    long.get("generic"),
                    long.get("standard"),
                    long.get("daylight"),
                    short.get("generic"),
                    short.get("standard"),
                    short.get("daylight"),
                )
                if any(isinstance(x, str) for x in vals):
                    tz_zones.append(
                        (
                            loc_idx,
                            intern(iana),
                            intern(vals[0]) if isinstance(vals[0], str) else -1,
                            intern(vals[1]) if isinstance(vals[1], str) else -1,
                            intern(vals[2]) if isinstance(vals[2], str) else -1,
                            intern(vals[3]) if isinstance(vals[3], str) else -1,
                            intern(vals[4]) if isinstance(vals[4], str) else -1,
                            intern(vals[5]) if isinstance(vals[5], str) else -1,
                        )
                    )

            walk_zone_tree(tzn.get("zone", {}), [], on_zone)

            for mz, info in sorted(tzn.get("metazone", {}).items()):
                if not isinstance(info, dict):
                    continue
                long = info.get("long", {}) if isinstance(info.get("long"), dict) else {}
                short = info.get("short", {}) if isinstance(info.get("short"), dict) else {}
                g = long.get("generic")
                s = long.get("standard")
                d = long.get("daylight")
                sg = short.get("generic")
                ss = short.get("standard")
                sd = short.get("daylight")
                tz_metazones.append(
                    (
                        loc_idx,
                        intern(mz),
                        intern(g) if isinstance(g, str) else -1,
                        intern(s) if isinstance(s, str) else -1,
                        intern(d) if isinstance(d, str) else -1,
                        intern(sg) if isinstance(sg, str) else -1,
                        intern(ss) if isinstance(ss, str) else -1,
                        intern(sd) if isinstance(sd, str) else -1,
                    )
                )

    # metaZones: historical IANA → metazone ranges
    mz_path = SRC / "supplemental" / "metaZones.json"
    if mz_path.is_file():
        mz = load_json(mz_path)["supplemental"]["metaZones"]["metazoneInfo"]["timezone"]

        def walk_tz(node, parts: list[str]):
            if not isinstance(node, dict):
                return
            for k, v in node.items():
                if k.startswith("_"):
                    continue
                if isinstance(v, list):
                    iana = "/".join(parts + [k])
                    for ent in v:
                        um = ent.get("usesMetazone", {})
                        mzone = um.get("_mzone")
                        if not mzone:
                            continue
                        fr = parse_mz_boundary(um.get("_from"))
                        to = parse_mz_boundary(um.get("_to"))
                        iana_mz.append(
                            (
                                intern(iana),
                                intern(mzone),
                                fr if fr is not None else MZ_FROM_OPEN,
                                to if to is not None else MZ_TO_OPEN,
                            )
                        )
                elif isinstance(v, dict):
                    walk_tz(v, parts + [k])

        walk_tz(mz, [])

    def sort_trip(rows: list[tuple[int, int, int]]):
        rows.sort(key=lambda r: (r[0], pool[r[1]] if r[1] < len(pool) else r[1]))

    sort_trip(languages)
    sort_trip(scripts)
    sort_trip(territories)
    sort_trip(calendars)
    sort_trip(numbering)
    sort_trip(keys)
    currencies.sort(key=lambda r: (r[0], pool[r[1]]))
    patterns.sort(key=lambda r: r[0])
    tz_patterns.sort(key=lambda r: r[0])
    tz_exemplars.sort(key=lambda r: (r[0], pool[r[1]]))
    tz_metazones.sort(key=lambda r: (r[0], pool[r[1]]))
    tz_zones.sort(key=lambda r: (r[0], pool[r[1]]))
    iana_mz.sort(key=lambda r: (pool[r[0]], r[2], r[3]))

    # Validation
    seen_mz_keys: set[tuple[int, int]] = set()
    for row in tz_metazones:
        key = (row[0], row[1])
        if key in seen_mz_keys:
            raise SystemExit(f"duplicate metazone row locale={pool[row[0]]} mz={pool[row[1]]}")
        seen_mz_keys.add(key)
    for a, _m, fr, to in iana_mz:
        if fr > to:
            raise SystemExit(f"bad metazone range for {pool[a]}: {fr} > {to}")
    if not tz_patterns:
        raise SystemExit("no timezone patterns extracted")
    if not iana_mz:
        raise SystemExit("no IANA→metazone mappings extracted")

    # Emit string pool as concatenated NUL-terminated strings? Better: offset table.
    # Use offsets into a single byte blob.
    blob = bytearray()
    offsets: list[int] = []
    for s in pool:
        offsets.append(len(blob))
        blob.extend(s.encode("utf-8"))
        blob.append(0)

    def c_escape_blob(data: bytes) -> str:
        lines = []
        for i in range(0, len(data), 32):
            chunk = data[i : i + 32]
            esc = "".join(f"\\x{b:02x}" for b in chunk)
            lines.append(f'    "{esc}"')
        return "\n".join(lines) if lines else '    ""'

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)

    h = f"""/* Auto-generated by generate_display.py — DO NOT EDIT.
 * CLDR {CLDR_VERSION} display names. Generator {GENERATOR_VERSION}. API {ROBO_I18N_VERSION}.
 * Unicode association: {CLDR_UNICODE_VERSION}. tzdb pin (display only): {TZDB_VERSION}.
 */
#ifndef ROBO_DISP_DATA_H
#define ROBO_DISP_DATA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define ROBO_DISP_CLDR_VERSION "{CLDR_VERSION}"
#define ROBO_DISP_MZ_FROM_OPEN ({MZ_FROM_OPEN}LL)
#define ROBO_DISP_MZ_TO_OPEN ({MZ_TO_OPEN}LL)

typedef struct robo_disp_name_row {{
    int32_t display_locale; /* string pool index */
    int32_t code;           /* string pool index */
    int32_t name;           /* string pool index */
}} robo_disp_name_row_t;

typedef struct robo_disp_currency_row {{
    int32_t display_locale;
    int32_t code;
    int32_t name;
    int32_t symbol;
    int32_t narrow_symbol;
}} robo_disp_currency_row_t;

typedef struct robo_disp_pattern_row {{
    int32_t display_locale;
    int32_t locale_pattern;
    int32_t locale_separator;
    int32_t key_type_pattern;
}} robo_disp_pattern_row_t;

typedef struct robo_disp_tz_pattern_row {{
    int32_t display_locale;
    int32_t gmt_format;
    int32_t gmt_zero_format;
    int32_t hour_format;
    int32_t region_format;
    int32_t region_format_standard;
    int32_t region_format_daylight;
    int32_t fallback_format;
}} robo_disp_tz_pattern_row_t;

typedef struct robo_disp_tz_metazone_row {{
    int32_t display_locale;
    int32_t metazone;
    int32_t long_generic;   /* -1 if absent */
    int32_t long_standard;
    int32_t long_daylight;
    int32_t short_generic;
    int32_t short_standard;
    int32_t short_daylight;
}} robo_disp_tz_metazone_row_t;

typedef struct robo_disp_tz_zone_row {{
    int32_t display_locale;
    int32_t iana_id;
    int32_t long_generic;
    int32_t long_standard;
    int32_t long_daylight;
    int32_t short_generic;
    int32_t short_standard;
    int32_t short_daylight;
}} robo_disp_tz_zone_row_t;

typedef struct robo_disp_iana_mz_range {{
    int32_t iana_id;
    int32_t metazone;
    int64_t from_unix; /* inclusive; ROBO_DISP_MZ_FROM_OPEN = unbounded */
    int64_t to_unix;   /* exclusive; ROBO_DISP_MZ_TO_OPEN = unbounded */
}} robo_disp_iana_mz_range_t;

extern const char robo_disp_string_pool[];
extern const uint32_t robo_disp_string_pool_size;
extern const int32_t robo_disp_string_offsets[];
extern const uint32_t robo_disp_string_count;

extern const robo_disp_name_row_t robo_disp_languages[];
extern const uint32_t robo_disp_language_count;
extern const robo_disp_name_row_t robo_disp_scripts[];
extern const uint32_t robo_disp_script_count;
extern const robo_disp_name_row_t robo_disp_territories[];
extern const uint32_t robo_disp_territory_count;
extern const robo_disp_name_row_t robo_disp_calendars[];
extern const uint32_t robo_disp_calendar_count;
extern const robo_disp_name_row_t robo_disp_numbering[];
extern const uint32_t robo_disp_numbering_count;
extern const robo_disp_name_row_t robo_disp_keys[];
extern const uint32_t robo_disp_key_count;

extern const robo_disp_currency_row_t robo_disp_currencies[];
extern const uint32_t robo_disp_currency_count;

extern const robo_disp_pattern_row_t robo_disp_patterns[];
extern const uint32_t robo_disp_pattern_count;

extern const robo_disp_tz_pattern_row_t robo_disp_tz_patterns[];
extern const uint32_t robo_disp_tz_pattern_count;
extern const robo_disp_name_row_t robo_disp_tz_exemplars[];
extern const uint32_t robo_disp_tz_exemplar_count;
extern const robo_disp_tz_metazone_row_t robo_disp_tz_metazones[];
extern const uint32_t robo_disp_tz_metazone_count;
extern const robo_disp_tz_zone_row_t robo_disp_tz_zones[];
extern const uint32_t robo_disp_tz_zone_count;
extern const robo_disp_iana_mz_range_t robo_disp_iana_mz_ranges[];
extern const uint32_t robo_disp_iana_mz_range_count;

const char *robo_disp_cldr_version_string(void);
const char *robo_disp_string(int32_t idx); /* NULL if idx < 0 or OOB */

#ifdef __cplusplus
}}
#endif

#endif /* ROBO_DISP_DATA_H */
"""

    def emit_name_rows(name: str, rows: list[tuple[int, int, int]]) -> str:
        body = ",\n".join(f"    {{{a}, {b}, {c}}}" for a, b, c in rows) or "    {0, 0, 0}"
        return (
            f"const robo_disp_name_row_t {name}[{max(len(rows), 1)}] = {{\n{body}\n}};\n"
            f"const uint32_t {name.replace('robo_disp_', 'robo_disp_').rsplit('[',1)[0]}_count = {len(rows)}u;\n"
        )

    # fix count names properly
    def emit_names(var: str, count_var: str, rows: list[tuple[int, int, int]]) -> str:
        body = ",\n".join(f"    {{{a}, {b}, {c}}}" for a, b, c in rows) or "    {0, 0, 0}"
        return (
            f"const robo_disp_name_row_t {var}[{max(len(rows), 1)}] = {{\n{body}\n}};\n"
            f"const uint32_t {count_var} = {len(rows)}u;\n"
        )

    off_body = ",\n".join(
        ", ".join(str(offsets[i + j]) for j in range(min(8, len(offsets) - i)))
        for i in range(0, len(offsets), 8)
    )

    cur_body = ",\n".join(
        f"    {{{a}, {b}, {c}, {d}, {e}}}" for a, b, c, d, e in currencies
    ) or "    {0, 0, 0, 0, 0}"
    pat_body = ",\n".join(
        f"    {{{a}, {b}, {c}, {d}}}" for a, b, c, d in patterns
    ) or "    {0, 0, 0, 0}"
    tzp_body = ",\n".join(
        f"    {{{a}, {b}, {c}, {d}, {e}, {f}, {g}, {hh}}}"
        for a, b, c, d, e, f, g, hh in tz_patterns
    ) or "    {0, 0, 0, 0, 0, 0, 0, 0}"
    tzm_body = ",\n".join(
        f"    {{{a}, {b}, {c}, {d}, {e}, {f}, {g}, {hh}}}"
        for a, b, c, d, e, f, g, hh in tz_metazones
    ) or "    {0, 0, -1, -1, -1, -1, -1, -1}"
    tzv_body = ",\n".join(
        f"    {{{a}, {b}, {c}, {d}, {e}, {f}, {g}, {hh}}}"
        for a, b, c, d, e, f, g, hh in tz_zones
    ) or "    {0, 0, -1, -1, -1, -1, -1, -1}"
    iana_body = ",\n".join(
        f"    {{{a}, {b}, {fr}LL, {to}LL}}" for a, b, fr, to in iana_mz
    ) or f"    {{0, 0, {MZ_FROM_OPEN}LL, {MZ_TO_OPEN}LL}}"

    c = f"""/* Auto-generated by generate_display.py — DO NOT EDIT. */
#include "RoboI18N/RoboDispData.h"

const char *robo_disp_cldr_version_string(void)
{{
    return ROBO_DISP_CLDR_VERSION;
}}

const char robo_disp_string_pool[] = {{
{c_escape_blob(bytes(blob))}
}};
const uint32_t robo_disp_string_pool_size = {len(blob)}u;

const int32_t robo_disp_string_offsets[{max(len(offsets), 1)}] = {{
{off_body if off_body else "    0"}
}};
const uint32_t robo_disp_string_count = {len(offsets)}u;

const char *robo_disp_string(int32_t idx)
{{
    if (idx < 0 || (uint32_t)idx >= robo_disp_string_count) {{
        return 0;
    }}
    return robo_disp_string_pool + robo_disp_string_offsets[idx];
}}

{emit_names("robo_disp_languages", "robo_disp_language_count", languages)}
{emit_names("robo_disp_scripts", "robo_disp_script_count", scripts)}
{emit_names("robo_disp_territories", "robo_disp_territory_count", territories)}
{emit_names("robo_disp_calendars", "robo_disp_calendar_count", calendars)}
{emit_names("robo_disp_numbering", "robo_disp_numbering_count", numbering)}
{emit_names("robo_disp_keys", "robo_disp_key_count", keys)}
{emit_names("robo_disp_tz_exemplars", "robo_disp_tz_exemplar_count", tz_exemplars)}

const robo_disp_currency_row_t robo_disp_currencies[{max(len(currencies), 1)}] = {{
{cur_body}
}};
const uint32_t robo_disp_currency_count = {len(currencies)}u;

const robo_disp_pattern_row_t robo_disp_patterns[{max(len(patterns), 1)}] = {{
{pat_body}
}};
const uint32_t robo_disp_pattern_count = {len(patterns)}u;

const robo_disp_tz_pattern_row_t robo_disp_tz_patterns[{max(len(tz_patterns), 1)}] = {{
{tzp_body}
}};
const uint32_t robo_disp_tz_pattern_count = {len(tz_patterns)}u;

const robo_disp_tz_metazone_row_t robo_disp_tz_metazones[{max(len(tz_metazones), 1)}] = {{
{tzm_body}
}};
const uint32_t robo_disp_tz_metazone_count = {len(tz_metazones)}u;

const robo_disp_tz_zone_row_t robo_disp_tz_zones[{max(len(tz_zones), 1)}] = {{
{tzv_body}
}};
const uint32_t robo_disp_tz_zone_count = {len(tz_zones)}u;

const robo_disp_iana_mz_range_t robo_disp_iana_mz_ranges[{max(len(iana_mz), 1)}] = {{
{iana_body}
}};
const uint32_t robo_disp_iana_mz_range_count = {len(iana_mz)}u;
"""

    OUT_H.write_text(h, encoding="utf-8", newline="\n")
    OUT_C.write_text(c, encoding="utf-8", newline="\n")

    hashes = {
        "generator_version": GENERATOR_VERSION,
        "cldr_version": CLDR_VERSION,
        "tzdb_version_pin": TZDB_VERSION,
        "api_version": ROBO_I18N_VERSION,
        "outputs": {
            "RoboDispData.h": hashlib.sha256(OUT_H.read_bytes()).hexdigest(),
            "robo_disp_data.c": hashlib.sha256(OUT_C.read_bytes()).hexdigest(),
        },
        "stats": {
            "locales": len(locales),
            "strings": len(pool),
            "languages": len(languages),
            "scripts": len(scripts),
            "territories": len(territories),
            "currencies": len(currencies),
            "calendars": len(calendars),
            "numbering": len(numbering),
            "tz_metazones": len(tz_metazones),
            "tz_zones": len(tz_zones),
            "tz_exemplars": len(tz_exemplars),
            "iana_mz_ranges": len(iana_mz),
        },
    }
    if write_golden:
        GOLDEN.write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {GOLDEN}")
    print(
        f"display CLDR {CLDR_VERSION}: locales={len(locales)} "
        f"lang={len(languages)} script={len(scripts)} terr={len(territories)} "
        f"cur={len(currencies)} cal={len(calendars)} nu={len(numbering)} "
        f"mz={len(tz_metazones)} zone={len(tz_zones)} ex={len(tz_exemplars)} "
        f"mz_ranges={len(iana_mz)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

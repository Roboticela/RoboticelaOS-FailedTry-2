#!/usr/bin/env python3
"""
Generate compact CLDR 46 runtime tables for RoboI18N (Goals 4.2–4.3).

Inputs under Tools/i18n/source/cldr-46.0.0/:
  supplemental/likelySubtags.json
  supplemental/numberingSystems.json
  supplemental/calendarPreferenceData.json
  supplemental/measurementData.json
  supplemental/parentLocales.json
  numbers/*.json  (defaults + symbols + decimal patterns)

Outputs:
  Include/RoboI18N/RoboCldrData.h
  Source/RoboI18N/data/robo_cldr_data.c
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

from robo_i18n_version import (
    CLDR_JSON_PACKAGE,
    CLDR_UNICODE_VERSION,
    CLDR_VERSION,
    GENERATOR_VERSION,
)
from generate_plurals import write_plurals
from generate_calendar import write_calendar
from generate_relative import write_relative


HERE = Path(__file__).resolve().parent
RUNTIME = HERE.parent.parent
SRC = HERE / "source" / f"cldr-{CLDR_JSON_PACKAGE}"
OUT_H = RUNTIME / "Include" / "RoboI18N" / "RoboCldrData.h"
OUT_C = RUNTIME / "Source" / "RoboI18N" / "data" / "robo_cldr_data.c"
GOLDEN = HERE / "cldr_golden_hashes.json"

SYM_KEYS = ("decimal", "group", "plusSign", "minusSign", "nan", "infinity")


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


def strip_pattern_quotes(pat: str) -> str:
    # CLDR JSON sometimes wraps patterns in single quotes as a whole string value;
    # the value itself is the pattern without outer JSON quotes.
    return pat


def parse_grouping(pattern: str) -> tuple[int, int, int]:
    """Return (primary, secondary, max_fraction_from_pattern)."""
    pos = strip_pattern_quotes(pattern).split(";")[0]
    frac = 0
    if "." in pos:
        intpart, fracpart = pos.split(".", 1)
        frac = sum(1 for ch in fracpart if ch in "#0")
    else:
        intpart = pos

    def digit_len(seg: str) -> int:
        return sum(1 for ch in seg if ch in "#0")

    parts = intpart.split(",")
    if not parts:
        return 0, 0, frac
    primary = digit_len(parts[-1])
    if primary == 0:
        return 0, 0, frac
    # Secondary differs only when the pattern has multiple grouping commas.
    if len(parts) >= 3:
        secondary = digit_len(parts[-2]) or primary
    else:
        secondary = primary
    return primary, secondary, frac


def build_tables() -> tuple[str, str, dict[str, str], dict[str, int]]:
    likely_j = load_json(SRC / "supplemental" / "likelySubtags.json")
    nums_j = load_json(SRC / "supplemental" / "numberingSystems.json")
    cal_j = load_json(SRC / "supplemental" / "calendarPreferenceData.json")
    meas_j = load_json(SRC / "supplemental" / "measurementData.json")
    parent_j = load_json(SRC / "supplemental" / "parentLocales.json")

    for j in (likely_j, nums_j, cal_j, meas_j, parent_j):
        verify_versions(j)

    likely = likely_j["supplemental"]["likelySubtags"]
    nsys = nums_j["supplemental"]["numberingSystems"]
    calpref = cal_j["supplemental"]["calendarPreferenceData"]
    meas = meas_j["supplemental"]["measurementData"]["measurementSystem"]
    parents = parent_j["supplemental"]["parentLocales"]["parentLocale"]
    if not isinstance(parents, dict) or not parents:
        raise SystemExit("parentLocales.parentLocale missing or empty")
    if "und" not in likely:
        raise SystemExit("likelySubtags missing required key und")
    if "latn" not in nsys:
        raise SystemExit("numberingSystems missing latn")
    if "001" not in calpref:
        raise SystemExit("calendarPreferenceData missing 001")
    if "001" not in meas:
        raise SystemExit("measurementData missing 001")

    locale_nums: dict[str, tuple[str, str]] = {}
    # (locale, nsys) -> symbols dict + pattern + min_group + primary + secondary + max_frac_pat
    numfmt_raw: dict[tuple[str, str], dict] = {}

    numbers_dir = SRC / "numbers"
    if not numbers_dir.is_dir():
        raise SystemExit(f"missing {numbers_dir}")
    for path in sorted(numbers_dir.glob("*.json")):
        data = load_json(path)
        main = data.get("main") or {}
        if len(main) != 1:
            raise SystemExit(f"unexpected numbers shape: {path}")
        loc = next(iter(main))
        nums = main[loc].get("numbers") or {}
        default = nums.get("defaultNumberingSystem")
        if not default:
            raise SystemExit(f"missing defaultNumberingSystem in {path}")
        native = (nums.get("otherNumberingSystems") or {}).get("native") or ""
        locale_nums[loc] = (default, native)

        min_g_s = nums.get("minimumGroupingDigits", "1")
        try:
            min_g = int(min_g_s)
        except (TypeError, ValueError) as exc:
            raise SystemExit(f"bad minimumGroupingDigits in {path}: {min_g_s!r}") from exc
        if min_g < 1 or min_g > 9:
            raise SystemExit(f"minimumGroupingDigits out of range in {path}: {min_g}")

        for key, syms in nums.items():
            if not key.startswith("symbols-numberSystem-"):
                continue
            if not isinstance(syms, dict):
                raise SystemExit(f"malformed symbols in {path}: {key}")
            nsys_id = key[len("symbols-numberSystem-") :]
            if nsys_id not in nsys:
                raise SystemExit(f"unknown numbering system {nsys_id!r} in {path}")
            for sk in SYM_KEYS:
                if sk not in syms or not isinstance(syms[sk], str) or syms[sk] == "":
                    raise SystemExit(f"missing symbol {sk} for {loc}/{nsys_id}")
            fmt_key = f"decimalFormats-numberSystem-{nsys_id}"
            fmt = nums.get(fmt_key) or {}
            pattern = fmt.get("standard")
            if not isinstance(pattern, str) or not pattern:
                raise SystemExit(f"missing decimal standard pattern for {loc}/{nsys_id}")
            primary, secondary, max_frac = parse_grouping(pattern)
            if primary == 0:
                raise SystemExit(f"could not parse grouping from {pattern!r} ({loc})")
            numfmt_raw[(loc, nsys_id)] = {
                "syms": {sk: syms[sk] for sk in SYM_KEYS},
                "pattern": pattern,
                "min_g": min_g,
                "primary": primary,
                "secondary": secondary,
                "max_frac": max_frac,
            }

    if "en" not in locale_nums:
        raise SystemExit("missing required locale numbers: en")
    if ("und", "latn") not in numfmt_raw and ("en", "latn") not in numfmt_raw:
        raise SystemExit("missing required number format data for und/en latn")
    for loc, (d, n) in locale_nums.items():
        if d not in nsys:
            raise SystemExit(f"locale {loc} defaultNumberingSystem {d!r} unknown")
        if n and n not in nsys:
            raise SystemExit(f"locale {loc} native numbering {n!r} unknown")

    pool: list[str] = []
    pool_index: dict[str, int] = {}

    def intern(s: str) -> int:
        if s not in pool_index:
            pool_index[s] = len(pool)
            pool.append(s)
        return pool_index[s]

    likely_pairs = sorted((str(k), str(v)) for k, v in likely.items())
    for a, b in likely_pairs:
        intern(a)
        intern(b)

    nsys_ids = sorted(nsys.keys())
    nsys_rows = []
    for nid in nsys_ids:
        info = nsys[nid]
        typ = info.get("_type", "numeric")
        digits = info.get("_digits", "") if typ == "numeric" else ""
        nsys_rows.append((intern(nid), intern(typ), intern(digits)))
    nsys_rows.sort(key=lambda r: r[0])

    cal_regions = sorted(calpref.keys())
    cal_rows = []
    max_cals = 0
    for region in cal_regions:
        cals = list(calpref[region])
        max_cals = max(max_cals, len(cals))
        cal_rows.append((intern(region), [intern(c) for c in cals]))
    cal_rows.sort(key=lambda r: r[0])

    meas_map = {"metric": 0, "US": 1, "UK": 2}
    meas_rows = []
    for region in sorted(meas.keys()):
        sysname = meas[region]
        if sysname not in meas_map:
            raise SystemExit(f"unknown measurement system {sysname!r}")
        meas_rows.append((intern(region), meas_map[sysname]))
    meas_rows.sort(key=lambda r: r[0])

    parent_pairs = sorted((str(k), str(v)) for k, v in parents.items())
    for a, b in parent_pairs:
        intern(a)
        intern(b)
    parent_kvs = sorted(
        ((pool_index[a], pool_index[b]) for a, b in parent_pairs), key=lambda r: r[0]
    )
    likely_kvs = sorted(
        ((pool_index[a], pool_index[b]) for a, b in likely_pairs), key=lambda r: r[0]
    )

    loc_num_rows = []
    for loc in sorted(locale_nums.keys()):
        d, n = locale_nums[loc]
        loc_num_rows.append((intern(loc), intern(d), intern(n) if n else -1))
    loc_num_rows.sort(key=lambda r: r[0])

    numfmt_rows = []
    for (loc, nsys_id) in sorted(numfmt_raw.keys()):
        info = numfmt_raw[(loc, nsys_id)]
        syms = info["syms"]
        numfmt_rows.append(
            (
                intern(loc),
                intern(nsys_id),
                intern(syms["decimal"]),
                intern(syms["group"]),
                intern(syms["plusSign"]),
                intern(syms["minusSign"]),
                intern(syms["nan"]),
                intern(syms["infinity"]),
                intern(info["pattern"]),
                info["min_g"],
                info["primary"],
                info["secondary"],
                info["max_frac"],
            )
        )
    numfmt_rows.sort(key=lambda r: (r[0], r[1]))

    # Detect duplicate (locale, nsys) after intern (should be unique)
    seen_pair = set()
    for row in numfmt_rows:
        key = (row[0], row[1])
        if key in seen_pair:
            raise SystemExit("duplicate numfmt locale/nsys row")
        seen_pair.add(key)

    str_order = sorted(range(len(pool)), key=lambda i: pool[i])

    checksums = {
        "likelySubtags.json": sha256_file(SRC / "supplemental" / "likelySubtags.json"),
        "numberingSystems.json": sha256_file(SRC / "supplemental" / "numberingSystems.json"),
        "calendarPreferenceData.json": sha256_file(
            SRC / "supplemental" / "calendarPreferenceData.json"
        ),
        "measurementData.json": sha256_file(SRC / "supplemental" / "measurementData.json"),
        "parentLocales.json": sha256_file(SRC / "supplemental" / "parentLocales.json"),
        "plurals.json": sha256_file(SRC / "supplemental" / "plurals.json")
        if (SRC / "supplemental" / "plurals.json").is_file()
        else "",
        "ordinals.json": sha256_file(SRC / "supplemental" / "ordinals.json")
        if (SRC / "supplemental" / "ordinals.json").is_file()
        else "",
    }
    checksums = {k: v for k, v in checksums.items() if v}

    h_text = "\n".join(
        [
            "/* Auto-generated by Tools/i18n/generate_cldr.py — DO NOT EDIT. */",
            f"/* Roboticela generator {GENERATOR_VERSION} | CLDR {CLDR_VERSION} | Unicode {CLDR_UNICODE_VERSION} */",
            "#ifndef ROBO_CLDR_DATA_H",
            "#define ROBO_CLDR_DATA_H",
            "",
            "#include <stddef.h>",
            "#include <stdint.h>",
            "",
            "#ifdef __cplusplus",
            'extern "C" {',
            "#endif",
            "",
            f'#define ROBO_CLDR_VERSION "{CLDR_VERSION}"',
            f'#define ROBO_CLDR_UNICODE_VERSION "{CLDR_UNICODE_VERSION}"',
            f"#define ROBO_CLDR_MAX_CALENDARS {max(1, max_cals)}u",
            "",
            "typedef struct robo_cldr_kv {",
            "    int32_t key;",
            "    int32_t value;",
            "} robo_cldr_kv_t;",
            "",
            "typedef struct robo_cldr_nsys {",
            "    int32_t id;",
            "    int32_t type;",
            "    int32_t digits;",
            "} robo_cldr_nsys_t;",
            "",
            "typedef struct robo_cldr_calpref {",
            "    int32_t region;",
            "    uint8_t count;",
            "    int32_t calendars[ROBO_CLDR_MAX_CALENDARS];",
            "} robo_cldr_calpref_t;",
            "",
            "typedef struct robo_cldr_loc_num {",
            "    int32_t locale;",
            "    int32_t default_ns;",
            "    int32_t native_ns; /* -1 if absent */",
            "} robo_cldr_loc_num_t;",
            "",
            "typedef struct robo_cldr_numfmt {",
            "    int32_t locale;",
            "    int32_t nsys;",
            "    int32_t decimal;",
            "    int32_t group;",
            "    int32_t plus_sign;",
            "    int32_t minus_sign;",
            "    int32_t nan;",
            "    int32_t infinity;",
            "    int32_t pattern;",
            "    uint8_t min_grouping;",
            "    uint8_t primary_group;",
            "    uint8_t secondary_group;",
            "    uint8_t pattern_max_fraction;",
            "} robo_cldr_numfmt_t;",
            "",
            "extern const char *const robo_cldr_strings[];",
            "extern const size_t robo_cldr_string_count;",
            "",
            "extern const int32_t robo_cldr_str_order[];",
            "extern const size_t robo_cldr_str_order_count;",
            "",
            "extern const robo_cldr_kv_t robo_cldr_likely[];",
            "extern const size_t robo_cldr_likely_count;",
            "",
            "extern const robo_cldr_nsys_t robo_cldr_nsys[];",
            "extern const size_t robo_cldr_nsys_count;",
            "",
            "extern const robo_cldr_calpref_t robo_cldr_calpref[];",
            "extern const size_t robo_cldr_calpref_count;",
            "",
            "extern const robo_cldr_kv_t robo_cldr_measure[];",
            "extern const size_t robo_cldr_measure_count;",
            "",
            "extern const robo_cldr_kv_t robo_cldr_parent[];",
            "extern const size_t robo_cldr_parent_count;",
            "",
            "extern const robo_cldr_loc_num_t robo_cldr_loc_num[];",
            "extern const size_t robo_cldr_loc_num_count;",
            "",
            "extern const robo_cldr_numfmt_t robo_cldr_numfmt[];",
            "extern const size_t robo_cldr_numfmt_count;",
            "",
            "const char *robo_cldr_str(int32_t idx);",
            "int32_t robo_cldr_find_str(const char *s);",
            "int32_t robo_cldr_kv_lookup(const robo_cldr_kv_t *table, size_t count, int32_t key);",
            "const robo_cldr_nsys_t *robo_cldr_nsys_find(int32_t id);",
            "const robo_cldr_calpref_t *robo_cldr_calpref_find(int32_t region);",
            "const robo_cldr_loc_num_t *robo_cldr_loc_num_find(int32_t locale);",
            "const robo_cldr_numfmt_t *robo_cldr_numfmt_find(int32_t locale, int32_t nsys);",
            "const char *robo_cldr_version_string(void);",
            "const char *robo_cldr_unicode_version_string(void);",
            "",
            "#ifdef __cplusplus",
            "}",
            "#endif",
            "",
            "#endif /* ROBO_CLDR_DATA_H */",
            "",
        ]
    )

    c_lines = [
        "/* Auto-generated by Tools/i18n/generate_cldr.py — DO NOT EDIT. */",
        f"/* CLDR {CLDR_VERSION} / Unicode {CLDR_UNICODE_VERSION} | generator {GENERATOR_VERSION} */",
        '#include "RoboI18N/RoboCldrData.h"',
        "",
        "#include <string.h>",
        "",
        "const char *const robo_cldr_strings[] = {",
    ]
    for s in pool:
        c_lines.append(f'    "{utf8_c_bytes(s)}",')
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_string_count = {len(pool)};")
    c_lines.append("")
    c_lines.append("const int32_t robo_cldr_str_order[] = {")
    c_lines.append("    " + ", ".join(str(i) for i in str_order) + ",")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_str_order_count = {len(str_order)};")
    c_lines.append("")

    c_lines.append("const robo_cldr_kv_t robo_cldr_likely[] = {")
    for a, b in likely_kvs:
        c_lines.append(f"    {{{a}, {b}}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_likely_count = {len(likely_kvs)};")
    c_lines.append("")

    c_lines.append("const robo_cldr_nsys_t robo_cldr_nsys[] = {")
    for row in nsys_rows:
        c_lines.append(f"    {{{row[0]}, {row[1]}, {row[2]}}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_nsys_count = {len(nsys_rows)};")
    c_lines.append("")

    c_lines.append("const robo_cldr_calpref_t robo_cldr_calpref[] = {")
    for region_i, cal_is in cal_rows:
        padded = cal_is + [-1] * (max_cals - len(cal_is))
        arr = ", ".join(str(x) for x in padded)
        c_lines.append(f"    {{{region_i}, {len(cal_is)}, {{{arr}}}}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_calpref_count = {len(cal_rows)};")
    c_lines.append("")

    c_lines.append("const robo_cldr_kv_t robo_cldr_measure[] = {")
    for region_i, mid in meas_rows:
        c_lines.append(f"    {{{region_i}, {mid}}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_measure_count = {len(meas_rows)};")
    c_lines.append("")

    c_lines.append("const robo_cldr_kv_t robo_cldr_parent[] = {")
    for a, b in parent_kvs:
        c_lines.append(f"    {{{a}, {b}}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_parent_count = {len(parent_kvs)};")
    c_lines.append("")

    c_lines.append("const robo_cldr_loc_num_t robo_cldr_loc_num[] = {")
    for loc_i, d_i, n_i in loc_num_rows:
        c_lines.append(f"    {{{loc_i}, {d_i}, {n_i}}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_loc_num_count = {len(loc_num_rows)};")
    c_lines.append("")

    c_lines.append("const robo_cldr_numfmt_t robo_cldr_numfmt[] = {")
    for row in numfmt_rows:
        c_lines.append(
            "    {"
            + ", ".join(str(x) for x in row)
            + "},"
        )
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_numfmt_count = {len(numfmt_rows)};")
    c_lines.append("")

    c_lines.append(
        """
const char *robo_cldr_str(int32_t idx)
{
    if (idx < 0 || (size_t)idx >= robo_cldr_string_count) {
        return "";
    }
    return robo_cldr_strings[idx];
}

int32_t robo_cldr_find_str(const char *s)
{
    size_t lo = 0;
    size_t hi = robo_cldr_str_order_count;
    if (!s) {
        return -1;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        int32_t idx = robo_cldr_str_order[mid];
        int cmp = strcmp(s, robo_cldr_strings[idx]);
        if (cmp == 0) {
            return idx;
        }
        if (cmp < 0) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    return -1;
}

int32_t robo_cldr_kv_lookup(const robo_cldr_kv_t *table, size_t count, int32_t key)
{
    size_t lo = 0;
    size_t hi = count;
    if (!table || key < 0) {
        return -1;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (table[mid].key == key) {
            return table[mid].value;
        }
        if (table[mid].key < key) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return -1;
}

const robo_cldr_nsys_t *robo_cldr_nsys_find(int32_t id)
{
    size_t lo = 0;
    size_t hi = robo_cldr_nsys_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (robo_cldr_nsys[mid].id == id) {
            return &robo_cldr_nsys[mid];
        }
        if (robo_cldr_nsys[mid].id < id) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const robo_cldr_calpref_t *robo_cldr_calpref_find(int32_t region)
{
    size_t lo = 0;
    size_t hi = robo_cldr_calpref_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (robo_cldr_calpref[mid].region == region) {
            return &robo_cldr_calpref[mid];
        }
        if (robo_cldr_calpref[mid].region < region) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const robo_cldr_loc_num_t *robo_cldr_loc_num_find(int32_t locale)
{
    size_t lo = 0;
    size_t hi = robo_cldr_loc_num_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (robo_cldr_loc_num[mid].locale == locale) {
            return &robo_cldr_loc_num[mid];
        }
        if (robo_cldr_loc_num[mid].locale < locale) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const robo_cldr_numfmt_t *robo_cldr_numfmt_find(int32_t locale, int32_t nsys)
{
    size_t lo = 0;
    size_t hi = robo_cldr_numfmt_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const robo_cldr_numfmt_t *row = &robo_cldr_numfmt[mid];
        if (row->locale == locale && row->nsys == nsys) {
            return row;
        }
        if (row->locale < locale || (row->locale == locale && row->nsys < nsys)) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const char *robo_cldr_version_string(void)
{
    return ROBO_CLDR_VERSION;
}

const char *robo_cldr_unicode_version_string(void)
{
    return ROBO_CLDR_UNICODE_VERSION;
}
"""
    )

    c_text = "\n".join(c_lines)
    stats = {
        "strings": len(pool),
        "likely": len(likely_kvs),
        "nsys": len(nsys_rows),
        "cal": len(cal_rows),
        "meas": len(meas_rows),
        "parent": len(parent_kvs),
        "loc_num": len(loc_num_rows),
        "numfmt": len(numfmt_rows),
        "bytes_h": len(h_text.encode("utf-8")),
        "bytes_c": len(c_text.encode("utf-8")),
    }
    return h_text, c_text, checksums, stats


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    repro = "--repro" in args
    write_golden = "--write-golden" in args

    h_text, c_text, checksums, stats = build_tables()

    if repro:
        if not OUT_H.is_file() or not OUT_C.is_file():
            raise SystemExit("repro failed: committed outputs missing")
        if OUT_H.read_text(encoding="utf-8") != h_text:
            raise SystemExit(f"repro failed: {OUT_H.name} differs")
        if OUT_C.read_text(encoding="utf-8") != c_text:
            raise SystemExit(f"repro failed: {OUT_C.name} differs")
        if GOLDEN.is_file():
            golden = json.loads(GOLDEN.read_text(encoding="utf-8"))
            got = {
                "RoboCldrData.h": hashlib.sha256(h_text.encode("utf-8")).hexdigest(),
                "robo_cldr_data.c": hashlib.sha256(c_text.encode("utf-8")).hexdigest(),
            }
            for name, digest in got.items():
                if golden.get(name) != digest:
                    raise SystemExit(f"repro failed: golden mismatch for {name}")
        print("CLDR generation reproducible OK")
        print("stats:", json.dumps(stats))
        rc = write_plurals(repro=True, write_golden=False)
        if rc != 0:
            return rc
        rc = write_calendar(repro=True, write_golden=False)
        if rc != 0:
            return rc
        return write_relative(repro=True, write_golden=False)

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
                    "RoboCldrData.h": hashlib.sha256(h_text.encode("utf-8")).hexdigest(),
                    "robo_cldr_data.c": hashlib.sha256(c_text.encode("utf-8")).hexdigest(),
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
        f"Wrote {OUT_H.name}/{OUT_C.name}: "
        f"strings={stats['strings']} likely={stats['likely']} nsys={stats['nsys']} "
        f"cal={stats['cal']} meas={stats['meas']} parent={stats['parent']} "
        f"loc_num={stats['loc_num']} numfmt={stats['numfmt']} bytes_c={stats['bytes_c']}"
    )
    print("checksums:", json.dumps(checksums, indent=2, sort_keys=True))
    rc = write_plurals(repro=False, write_golden=write_golden)
    if rc != 0:
        return rc
    rc = write_calendar(repro=False, write_golden=write_golden)
    if rc != 0:
        return rc
    return write_relative(repro=False, write_golden=write_golden)

if __name__ == "__main__":
    raise SystemExit(main())

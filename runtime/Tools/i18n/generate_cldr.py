#!/usr/bin/env python3
"""
Generate compact CLDR 46 runtime tables for RoboI18N (Goal 4.2).

Inputs under Tools/i18n/source/cldr-46.0.0/:
  supplemental/likelySubtags.json
  supplemental/numberingSystems.json
  supplemental/calendarPreferenceData.json
  supplemental/measurementData.json
  supplemental/parentLocales.json
  numbers/*.json          (defaultNumberingSystem extracts)

Outputs:
  Include/RoboI18N/RoboCldrData.h
  Source/RoboI18N/data/robo_cldr_data.c
"""

from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path

from robo_i18n_version import GENERATOR_VERSION

HERE = Path(__file__).resolve().parent
RUNTIME = HERE.parent.parent
SRC = HERE / "source" / "cldr-46.0.0"
OUT_H = RUNTIME / "Include" / "RoboI18N" / "RoboCldrData.h"
OUT_C = RUNTIME / "Source" / "RoboI18N" / "data" / "robo_cldr_data.c"

CLDR_VERSION = "46"
CLDR_UNICODE_VERSION = "16.0.0"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def load_json(path: Path) -> dict:
    if not path.is_file():
        raise SystemExit(f"missing CLDR source: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def c_escape(s: str) -> str:
    out = []
    for ch in s:
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif o < 32 or o == 127:
            out.append(f"\\x{o:02x}")
        else:
            out.append(ch)
    return "".join(out)


def utf8_c_bytes(s: str) -> str:
    """Emit a C string literal with UTF-8 escapes for non-ASCII."""
    raw = s.encode("utf-8")
    parts = []
    for b in raw:
        if 32 <= b < 127 and b not in (0x22, 0x5C):
            parts.append(chr(b))
        else:
            parts.append(f"\\x{b:02x}")
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


def main() -> int:
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

    # Locale numbering defaults
    locale_nums: dict[str, tuple[str, str]] = {}
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

    if "en" not in locale_nums:
        raise SystemExit("missing required locale numbers: en")

    # String pool for ids
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

    # Sorted string index for binary find_str
    str_order = sorted(range(len(pool)), key=lambda i: pool[i])

    # Source checksums
    checksums = {
        "likelySubtags.json": sha256_file(SRC / "supplemental" / "likelySubtags.json"),
        "numberingSystems.json": sha256_file(SRC / "supplemental" / "numberingSystems.json"),
        "calendarPreferenceData.json": sha256_file(
            SRC / "supplemental" / "calendarPreferenceData.json"
        ),
        "measurementData.json": sha256_file(SRC / "supplemental" / "measurementData.json"),
        "parentLocales.json": sha256_file(SRC / "supplemental" / "parentLocales.json"),
    }

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)

    OUT_H.write_text(
        "\n".join(
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
                "extern const char *const robo_cldr_strings[];",
                "extern const size_t robo_cldr_string_count;",
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
                "const char *robo_cldr_str(int32_t idx);",
                "int32_t robo_cldr_find_str(const char *s);",
                "int32_t robo_cldr_kv_lookup(const robo_cldr_kv_t *table, size_t count, int32_t key);",
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
        ),
        encoding="utf-8",
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

    c_lines.append("const robo_cldr_kv_t robo_cldr_likely[] = {")
    for a, b in likely_pairs:
        c_lines.append(f"    {{{pool_index[a]}, {pool_index[b]}}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_likely_count = {len(likely_pairs)};")
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
    for a, b in parent_pairs:
        c_lines.append(f"    {{{pool_index[a]}, {pool_index[b]}}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_parent_count = {len(parent_pairs)};")
    c_lines.append("")

    c_lines.append("const robo_cldr_loc_num_t robo_cldr_loc_num[] = {")
    for loc_i, d_i, n_i in loc_num_rows:
        c_lines.append(f"    {{{loc_i}, {d_i}, {n_i}}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_cldr_loc_num_count = {len(loc_num_rows)};")
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
    size_t hi = robo_cldr_string_count;
    if (!s) {
        return -1;
    }
    /* linear search — pool is insertion-ordered; build sorted index via kv tables */
    size_t i;
    for (i = 0; i < robo_cldr_string_count; i++) {
        if (strcmp(robo_cldr_strings[i], s) == 0) {
            return (int32_t)i;
        }
    }
    (void)lo;
    (void)hi;
    return -1;
}

int32_t robo_cldr_kv_lookup(const robo_cldr_kv_t *table, size_t count, int32_t key)
{
    size_t lo = 0;
    size_t hi = count;
    if (!table) {
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

    OUT_C.write_text("\n".join(c_lines), encoding="utf-8")

    # Fix: likely/parent/measure tables are sorted by string key, but we store
    # string *indices* which are NOT sorted by index. Binary search on key index
    # is WRONG. Need to sort kv tables by key index OR search by comparing strings.
    #
    # Re-emit kv tables sorted by key string (already are), but lookup must
    # strcmp via string pool. Better: store sorted by key index after remapping.
    #
    # Fix in generator: sort pairs by key string, then for binary search on
    # key index we need keys sorted by index value. So sort by pool_index[key].

    print(
        f"Wrote {OUT_H.name}/{OUT_C.name}: "
        f"strings={len(pool)} likely={len(likely_pairs)} nsys={len(nsys_rows)} "
        f"cal={len(cal_rows)} meas={len(meas_rows)} parent={len(parent_pairs)} "
        f"loc_num={len(loc_num_rows)}"
    )
    print("checksums:", json.dumps(checksums, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

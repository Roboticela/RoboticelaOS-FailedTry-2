#!/usr/bin/env python3
"""
Emit RoboI18N measurement preference tables (Goal 4.20).

Inputs:
  supplemental/measurementData.json  (systems + temperature category)
  supplemental/unitPreferenceData.json (category/usage/region unit lists)

Outputs:
  Include/RoboI18N/RoboMeasurePrefData.h
  Source/RoboI18N/data/robo_measure_pref_data.c
  Tools/i18n/measure_pref_golden_hashes.json
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from robo_i18n_version import (
    CLDR_JSON_PACKAGE,
    CLDR_UNICODE_VERSION,
    CLDR_VERSION,
    GENERATOR_VERSION,
)

HERE = Path(__file__).resolve().parent
RUNTIME = HERE.parent.parent
SRC = HERE / "source" / f"cldr-{CLDR_JSON_PACKAGE}" / "supplemental"
OUT_H = RUNTIME / "Include" / "RoboI18N" / "RoboMeasurePrefData.h"
OUT_C = RUNTIME / "Source" / "RoboI18N" / "data" / "robo_measure_pref_data.c"
GOLDEN = HERE / "measure_pref_golden_hashes.json"

# Public category order (robo_i18n_measure_category_t)
CATEGORIES = (
    "temperature",
    "length",
    "area",
    "mass",
    "volume",
    "speed",
    "pressure",
    "energy",
    "power",
)

# Usage ids we emit (0 = default). Extendable without breaking ABI if appended.
USAGES = (
    "default",
    "weather",
    "road",
    "fluid",
    "food",
    "engine",
    "person",
    "land",
)

# Map CLDR preference unit short names → Robo curated unit id (Goal 4.19 enum order).
UNIT_MAP: dict[str, int] = {
    "kilometer": 0,
    "meter": 1,
    "centimeter": 2,
    "millimeter": 3,
    "mile": 4,
    "yard": 5,
    "foot": 6,
    "inch": 7,
    "kilogram": 8,
    "gram": 9,
    "pound": 10,
    "ounce": 11,
    "square-kilometer": 12,
    "square-meter": 13,
    "hectare": 14,
    "acre": 15,
    "liter": 16,
    "milliliter": 17,
    "gallon": 18,
    "cubic-meter": 19,
    "kilometer-per-hour": 28,
    "meter-per-second": 29,
    "mile-per-hour": 30,
    "celsius": 31,
    "fahrenheit": 32,
    "kelvin": 33,
    "joule": 40,
    "kilowatt-hour": 41,
    "calorie": 42,
    "foodcalorie": 43,
    "kilocalorie": 43,  # map to foodcalorie curated id
    "watt": 44,
    "kilowatt": 45,
    "horsepower": 46,
    "pascal": 47,
    "hectopascal": 48,
    "millibar": 49,
    "pound-force-per-square-inch": 50,
}

# Category → which CLDR usages to emit (must exist or be skipped gracefully)
CATEGORY_USAGES: dict[str, tuple[str, ...]] = {
    "temperature": ("default", "weather"),
    "length": ("default", "road", "person"),
    "area": ("default", "land"),
    "mass": ("default", "person"),
    "volume": ("default", "fluid"),
    "speed": ("default",),
    "pressure": ("default",),
    "energy": ("default", "food"),
    "power": ("default", "engine"),
}

MEAS_MAP = {"metric": 0, "US": 1, "UK": 2}
MAX_UNITS = 8


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


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


def write_tables(*, repro: bool, write_golden: bool) -> int:
    meas_path = SRC / "measurementData.json"
    pref_path = SRC / "unitPreferenceData.json"
    if not meas_path.is_file():
        raise SystemExit(f"missing {meas_path}")
    if not pref_path.is_file():
        raise SystemExit(f"missing {pref_path}")

    meas_j = json.loads(meas_path.read_text(encoding="utf-8"))
    pref_j = json.loads(pref_path.read_text(encoding="utf-8"))
    md = meas_j["supplemental"]["measurementData"]
    systems = md["measurementSystem"]
    temp_sys = md.get("measurementSystem-category-temperature") or {}
    upd = pref_j["supplemental"]["unitPreferenceData"]

    pool: list[str] = []
    index: dict[str, int] = {}

    def intern(s: str) -> int:
        if s not in index:
            index[s] = len(pool)
            pool.append(s)
        return index[s]

    system_rows: list[tuple[int, int]] = []
    for region, name in sorted(systems.items()):
        if name not in MEAS_MAP:
            raise SystemExit(f"unknown measurement system {name!r}")
        system_rows.append((intern(region), MEAS_MAP[name]))

    temp_rows: list[tuple[int, int]] = []
    for region, name in sorted(temp_sys.items()):
        if name not in MEAS_MAP:
            raise SystemExit(f"unknown temp measurement system {name!r}")
        temp_rows.append((intern(region), MEAS_MAP[name]))

    pref_rows: list[dict] = []
    for cat_i, cat in enumerate(CATEGORIES):
        if cat not in upd:
            raise SystemExit(f"unitPreferenceData missing category {cat}")
        usages_wanted = CATEGORY_USAGES[cat]
        for usage_name in usages_wanted:
            if usage_name not in USAGES:
                raise SystemExit(f"usage {usage_name} not in USAGES")
            usage_i = USAGES.index(usage_name)
            regions = upd[cat].get(usage_name)
            if not isinstance(regions, dict):
                continue
            for region, entries in sorted(regions.items()):
                units: list[int] = []
                if not isinstance(entries, list):
                    continue
                for ent in entries:
                    if not isinstance(ent, dict):
                        continue
                    uname = ent.get("unit")
                    if not isinstance(uname, str):
                        continue
                    uid = UNIT_MAP.get(uname)
                    if uid is None:
                        continue
                    if uid not in units:
                        units.append(uid)
                    if len(units) >= MAX_UNITS:
                        break
                if not units:
                    continue
                while len(units) < MAX_UNITS:
                    units.append(255)
                pref_rows.append(
                    {
                        "cat": cat_i,
                        "usage": usage_i,
                        "region": intern(region),
                        "units": units,
                        "count": sum(1 for u in units if u != 255),
                    }
                )

    pref_rows.sort(key=lambda r: (r["cat"], r["usage"], pool[r["region"]]))
    system_rows.sort(key=lambda r: pool[r[0]])
    temp_rows.sort(key=lambda r: pool[r[0]])

    if not any(pool[r[0]] == "001" for r in system_rows):
        raise SystemExit("measurementSystem missing 001")
    if not any(r["cat"] == 0 and pool[r["region"]] == "001" for r in pref_rows):
        raise SystemExit("temperature/default/001 preference missing after filter")

    h_text = "\n".join(
        [
            "/* Auto-generated by Tools/i18n/generate_measure_prefs.py — DO NOT EDIT. */",
            f"/* Roboticela generator {GENERATOR_VERSION} | CLDR {CLDR_VERSION} | Unicode {CLDR_UNICODE_VERSION} */",
            "#ifndef ROBO_MEASURE_PREF_DATA_H",
            "#define ROBO_MEASURE_PREF_DATA_H",
            "",
            "#include <stddef.h>",
            "#include <stdint.h>",
            "",
            "#ifdef __cplusplus",
            'extern "C" {',
            "#endif",
            "",
            f'#define ROBO_MPREF_CLDR_VERSION "{CLDR_VERSION}"',
            f'#define ROBO_MPREF_UNICODE_VERSION "{CLDR_UNICODE_VERSION}"',
            f"#define ROBO_MPREF_CATEGORY_COUNT {len(CATEGORIES)}u",
            f"#define ROBO_MPREF_USAGE_COUNT {len(USAGES)}u",
            f"#define ROBO_MPREF_UNITS_MAX {MAX_UNITS}u",
            "",
            "/* system: 0=metric 1=US 2=UK */",
            "typedef struct robo_mpref_system {",
            "    int32_t region;",
            "    uint8_t system;",
            "} robo_mpref_system_t;",
            "",
            "typedef struct robo_mpref_units {",
            "    uint8_t category;",
            "    uint8_t usage;",
            "    int32_t region;",
            "    uint8_t count;",
            "    uint8_t units[ROBO_MPREF_UNITS_MAX]; /* 255 = pad */",
            "} robo_mpref_units_t;",
            "",
            "extern const char *const robo_mpref_strings[];",
            "extern const size_t robo_mpref_string_count;",
            "extern const robo_mpref_system_t robo_mpref_systems[];",
            "extern const size_t robo_mpref_systems_count;",
            "extern const robo_mpref_system_t robo_mpref_temp_systems[];",
            "extern const size_t robo_mpref_temp_systems_count;",
            "extern const robo_mpref_units_t robo_mpref_units[];",
            "extern const size_t robo_mpref_units_count;",
            "extern const char *const robo_mpref_category_names[];",
            "extern const char *const robo_mpref_usage_names[];",
            "",
            "const char *robo_mpref_str(int32_t idx);",
            "const robo_mpref_system_t *robo_mpref_system_find(const char *region);",
            "const robo_mpref_system_t *robo_mpref_temp_system_find(const char *region);",
            "const robo_mpref_units_t *robo_mpref_units_find(uint8_t category, uint8_t usage,",
            "                                                 const char *region);",
            "const char *robo_mpref_version_string(void);",
            "",
            "#ifdef __cplusplus",
            "}",
            "#endif",
            "",
            "#endif /* ROBO_MEASURE_PREF_DATA_H */",
            "",
        ]
    )

    c_lines: list[str] = [
        "/* Auto-generated by Tools/i18n/generate_measure_prefs.py — DO NOT EDIT. */",
        f"/* CLDR {CLDR_VERSION} / Unicode {CLDR_UNICODE_VERSION} | generator {GENERATOR_VERSION} */",
        '#include "RoboI18N/RoboMeasurePrefData.h"',
        "",
        "#include <string.h>",
        "",
        "const char *const robo_mpref_strings[] = {",
    ]
    for s in pool:
        c_lines.append(f'    "{utf8_c_bytes(s)}",')
    c_lines.append("};")
    c_lines.append(f"const size_t robo_mpref_string_count = {len(pool)};")
    c_lines.append("")
    c_lines.append("const char *const robo_mpref_category_names[] = {")
    for c in CATEGORIES:
        c_lines.append(f'    "{c}",')
    c_lines.append("};")
    c_lines.append("const char *const robo_mpref_usage_names[] = {")
    for u in USAGES:
        c_lines.append(f'    "{u}",')
    c_lines.append("};")
    c_lines.append("")
    c_lines.append("const robo_mpref_system_t robo_mpref_systems[] = {")
    for r, s in system_rows:
        c_lines.append(f"    {{{r}, {s}u}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_mpref_systems_count = {len(system_rows)};")
    c_lines.append("")
    c_lines.append("const robo_mpref_system_t robo_mpref_temp_systems[] = {")
    for r, s in temp_rows:
        c_lines.append(f"    {{{r}, {s}u}},")
    c_lines.append("};")
    c_lines.append(f"const size_t robo_mpref_temp_systems_count = {len(temp_rows)};")
    c_lines.append("")
    c_lines.append("const robo_mpref_units_t robo_mpref_units[] = {")
    for r in pref_rows:
        units = ", ".join(f"{u}u" for u in r["units"])
        c_lines.append(
            f"    {{{r['cat']}u, {r['usage']}u, {r['region']}, {r['count']}u, {{{units}}}}},"
        )
    c_lines.append("};")
    c_lines.append(f"const size_t robo_mpref_units_count = {len(pref_rows)};")
    c_lines.append("")
    c_lines.append(
        r"""
const char *robo_mpref_str(int32_t idx)
{
    if (idx < 0 || (size_t)idx >= robo_mpref_string_count) {
        return "";
    }
    return robo_mpref_strings[idx];
}

static const robo_mpref_system_t *system_bsearch(const robo_mpref_system_t *rows,
                                                 size_t count,
                                                 const char *region)
{
    size_t lo = 0;
    size_t hi = count;
    if (!region) {
        return NULL;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        int cmp = strcmp(robo_mpref_strings[rows[mid].region], region);
        if (cmp == 0) {
            return &rows[mid];
        }
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const robo_mpref_system_t *robo_mpref_system_find(const char *region)
{
    return system_bsearch(robo_mpref_systems, robo_mpref_systems_count, region);
}

const robo_mpref_system_t *robo_mpref_temp_system_find(const char *region)
{
    return system_bsearch(robo_mpref_temp_systems, robo_mpref_temp_systems_count, region);
}

const robo_mpref_units_t *robo_mpref_units_find(uint8_t category, uint8_t usage,
                                                const char *region)
{
    size_t lo = 0;
    size_t hi = robo_mpref_units_count;
    if (!region) {
        return NULL;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const robo_mpref_units_t *row = &robo_mpref_units[mid];
        int cmp;
        if (row->category != category) {
            cmp = row->category < category ? -1 : 1;
        } else if (row->usage != usage) {
            cmp = row->usage < usage ? -1 : 1;
        } else {
            cmp = strcmp(robo_mpref_strings[row->region], region);
            if (cmp == 0) {
                return row;
            }
        }
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const char *robo_mpref_version_string(void)
{
    return ROBO_MPREF_CLDR_VERSION;
}
""".strip(
            "\n"
        )
    )

    c_text = "\n".join(c_lines) + "\n"

    if repro:
        if not OUT_H.is_file() or not OUT_C.is_file() or not GOLDEN.is_file():
            raise SystemExit("measure_pref repro: missing outputs/golden")
        golden = json.loads(GOLDEN.read_text(encoding="utf-8"))
        if sha256_file(OUT_H) != golden["RoboMeasurePrefData.h"]:
            raise SystemExit("measure_pref repro: header hash mismatch")
        if sha256_file(OUT_C) != golden["robo_measure_pref_data.c"]:
            raise SystemExit("measure_pref repro: data hash mismatch")
        print("measure_pref repro OK")
        return 0

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(h_text, encoding="utf-8", newline="\n")
    OUT_C.write_text(c_text, encoding="utf-8", newline="\n")

    if write_golden:
        hashes = {
            "generator": GENERATOR_VERSION,
            "cldr": CLDR_VERSION,
            "unicode": CLDR_UNICODE_VERSION,
            "RoboMeasurePrefData.h": sha256_file(OUT_H),
            "robo_measure_pref_data.c": sha256_file(OUT_C),
            "pref_rows": len(pref_rows),
            "system_rows": len(system_rows),
            "temp_rows": len(temp_rows),
            "measurementData.json": sha256_file(meas_path),
            "unitPreferenceData.json": sha256_file(pref_path),
        }
        GOLDEN.write_text(json.dumps(hashes, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"Wrote {GOLDEN.name}")

    print(
        f"Wrote measure prefs: systems={len(system_rows)} temp={len(temp_rows)} "
        f"prefs={len(pref_rows)} pool={len(pool)} bytes_c={len(c_text.encode())}"
    )
    return 0


def main() -> int:
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument("--write-golden", action="store_true")
    ap.add_argument("--repro", action="store_true")
    args = ap.parse_args()
    return write_tables(repro=args.repro, write_golden=args.write_golden)


if __name__ == "__main__":
    raise SystemExit(main())

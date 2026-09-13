#!/usr/bin/env python3
"""
Emit RoboI18N unit-formatting tables from CLDR units.json (Goal 4.19).

Inputs: Tools/i18n/source/cldr-46.0.0/units/*/units.json
Outputs:
  Include/RoboI18N/RoboUnitData.h
  Source/RoboI18N/data/robo_unit_data.c
  Tools/i18n/units_golden_hashes.json
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
SRC = HERE / "source" / f"cldr-{CLDR_JSON_PACKAGE}" / "units"
OUT_H = RUNTIME / "Include" / "RoboI18N" / "RoboUnitData.h"
OUT_C = RUNTIME / "Source" / "RoboI18N" / "data" / "robo_unit_data.c"
GOLDEN = HERE / "units_golden_hashes.json"

# Curated CLDR type-subtype keys — order defines robo_i18n_unit_id_t.
UNIT_KEYS: list[str] = [
    # length
    "length-kilometer",
    "length-meter",
    "length-centimeter",
    "length-millimeter",
    "length-mile",
    "length-yard",
    "length-foot",
    "length-inch",
    # mass
    "mass-kilogram",
    "mass-gram",
    "mass-pound",
    "mass-ounce",
    # area
    "area-square-kilometer",
    "area-square-meter",
    "area-hectare",
    "area-acre",
    # volume
    "volume-liter",
    "volume-milliliter",
    "volume-gallon",
    "volume-cubic-meter",
    # duration
    "duration-year",
    "duration-month",
    "duration-week",
    "duration-day",
    "duration-hour",
    "duration-minute",
    "duration-second",
    "duration-millisecond",
    # speed (precomposed compounds)
    "speed-kilometer-per-hour",
    "speed-meter-per-second",
    "speed-mile-per-hour",
    # temperature
    "temperature-celsius",
    "temperature-fahrenheit",
    "temperature-kelvin",
    # digital
    "digital-bit",
    "digital-byte",
    "digital-kilobyte",
    "digital-megabyte",
    "digital-gigabyte",
    "digital-terabyte",
    # energy
    "energy-joule",
    "energy-kilowatt-hour",
    "energy-calorie",
    "energy-foodcalorie",
    # power
    "power-watt",
    "power-kilowatt",
    "power-horsepower",
    # pressure
    "pressure-pascal",
    "pressure-hectopascal",
    "pressure-millibar",
    "pressure-pound-force-per-square-inch",
    # frequency
    "frequency-hertz",
    "frequency-kilohertz",
    "frequency-megahertz",
    "frequency-gigahertz",
    # angle
    "angle-degree",
    "angle-radian",
    "angle-arc-minute",
    "angle-arc-second",
    # consumption (precomposed compound)
    "consumption-liter-per-100-kilometer",
]

WIDTHS = ("long", "short", "narrow")
PLURAL_CATS = ("zero", "one", "two", "few", "many", "other")


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


def write_units(*, repro: bool, write_golden: bool) -> int:
    if not SRC.is_dir():
        raise SystemExit(f"missing units source dir: {SRC}")

    pool: list[str] = []
    index: dict[str, int] = {}

    def intern(s: str) -> int:
        if s not in index:
            index[s] = len(pool)
            pool.append(s)
        return index[s]

    unit_rows: list[dict] = []
    compound_rows: list[dict] = []
    paths = sorted(SRC.rglob("units.json"))
    if not paths:
        raise SystemExit(f"no units.json under {SRC}")

    for path in paths:
        data = json.loads(path.read_text(encoding="utf-8"))
        main = data.get("main") or {}
        if len(main) != 1:
            raise SystemExit(f"unexpected units shape: {path}")
        loc = next(iter(main))
        units_root = (main[loc].get("units") or {})
        loc_i = intern(loc)

        for width_i, width in enumerate(WIDTHS):
            block = units_root.get(width)
            if not isinstance(block, dict):
                continue

            per = block.get("per") or {}
            times = block.get("times") or {}
            per_pat = per.get("compoundUnitPattern") if isinstance(per, dict) else None
            times_pat = times.get("compoundUnitPattern") if isinstance(times, dict) else None
            if isinstance(per_pat, str) and per_pat:
                compound_rows.append(
                    {
                        "locale": loc_i,
                        "width": width_i,
                        "per": intern(per_pat),
                        "times": intern(times_pat)
                        if isinstance(times_pat, str) and times_pat
                        else -1,
                    }
                )

            for unit_i, key in enumerate(UNIT_KEYS):
                node = block.get(key)
                if not isinstance(node, dict):
                    continue
                display = node.get("displayName")
                display_i = intern(display) if isinstance(display, str) and display else -1
                pats = [-1] * 6
                for ci, cat in enumerate(PLURAL_CATS):
                    v = node.get(f"unitPattern-count-{cat}")
                    if isinstance(v, str) and v:
                        pats[ci] = intern(v)
                if pats[5] < 0:
                    # Some locales only ship "other"; require it.
                    continue
                per_unit = node.get("perUnitPattern")
                per_i = intern(per_unit) if isinstance(per_unit, str) and per_unit else -1
                unit_rows.append(
                    {
                        "locale": loc_i,
                        "unit": unit_i,
                        "width": width_i,
                        "display": display_i,
                        "pats": pats,
                        "per_unit": per_i,
                    }
                )

    unit_rows.sort(key=lambda r: (pool[r["locale"]], r["unit"], r["width"]))
    compound_rows.sort(key=lambda r: (pool[r["locale"]], r["width"]))

    # Validate: every unit key appears for en/long
    en_long = {
        (pool[r["locale"]], r["unit"], r["width"]) for r in unit_rows if pool[r["locale"]] == "en"
    }
    for ui, key in enumerate(UNIT_KEYS):
        if ("en", ui, 0) not in en_long:
            raise SystemExit(f"missing en/long pattern for {key}")

    enum_lines = [
        f"    ROBO_I18N_UNIT_{k.replace('-', '_').upper()} = {i},"
        for i, k in enumerate(UNIT_KEYS)
    ]
    enum_lines.append(f"    ROBO_I18N_UNIT_COUNT = {len(UNIT_KEYS)}")

    h_text = "\n".join(
        [
            "/* Auto-generated by Tools/i18n/generate_units.py — DO NOT EDIT. */",
            f"/* Roboticela generator {GENERATOR_VERSION} | CLDR {CLDR_VERSION} | Unicode {CLDR_UNICODE_VERSION} */",
            "#ifndef ROBO_UNIT_DATA_H",
            "#define ROBO_UNIT_DATA_H",
            "",
            "#include <stddef.h>",
            "#include <stdint.h>",
            "",
            "#ifdef __cplusplus",
            'extern "C" {',
            "#endif",
            "",
            f'#define ROBO_UNIT_CLDR_VERSION "{CLDR_VERSION}"',
            f'#define ROBO_UNIT_UNICODE_VERSION "{CLDR_UNICODE_VERSION}"',
            f"#define ROBO_UNIT_ID_COUNT {len(UNIT_KEYS)}u",
            "",
            "/* patterns[6]: zero,one,two,few,many,other — -1 = missing */",
            "typedef struct robo_unit_row {",
            "    int32_t locale;",
            "    uint16_t unit;",
            "    uint8_t width;",
            "    int32_t display_name;",
            "    int32_t patterns[6];",
            "    int32_t per_unit;",
            "} robo_unit_row_t;",
            "",
            "typedef struct robo_unit_compound {",
            "    int32_t locale;",
            "    uint8_t width;",
            "    int32_t per_pattern;",
            "    int32_t times_pattern;",
            "} robo_unit_compound_t;",
            "",
            "extern const char *const robo_unit_strings[];",
            "extern const size_t robo_unit_string_count;",
            "extern const char *const robo_unit_cldr_keys[];",
            "extern const robo_unit_row_t robo_unit_rows[];",
            "extern const size_t robo_unit_rows_count;",
            "extern const robo_unit_compound_t robo_unit_compounds[];",
            "extern const size_t robo_unit_compounds_count;",
            "",
            "const char *robo_unit_str(int32_t idx);",
            "const char *robo_unit_cldr_key(uint16_t unit);",
            "const robo_unit_row_t *robo_unit_row_find(const char *locale, uint16_t unit, uint8_t width);",
            "const robo_unit_compound_t *robo_unit_compound_find(const char *locale, uint8_t width);",
            "const char *robo_unit_version_string(void);",
            "",
            "#ifdef __cplusplus",
            "}",
            "#endif",
            "",
            "#endif /* ROBO_UNIT_DATA_H */",
            "",
        ]
    )

    c_lines: list[str] = [
        "/* Auto-generated by Tools/i18n/generate_units.py — DO NOT EDIT. */",
        f"/* CLDR {CLDR_VERSION} / Unicode {CLDR_UNICODE_VERSION} | generator {GENERATOR_VERSION} */",
        '#include "RoboI18N/RoboUnitData.h"',
        "",
        "#include <string.h>",
        "",
        "const char *const robo_unit_strings[] = {",
    ]
    for s in pool:
        c_lines.append(f'    "{utf8_c_bytes(s)}",')
    c_lines.append("};")
    c_lines.append(f"const size_t robo_unit_string_count = {len(pool)};")
    c_lines.append("")
    c_lines.append("const char *const robo_unit_cldr_keys[] = {")
    for k in UNIT_KEYS:
        c_lines.append(f'    "{k}",')
    c_lines.append("};")
    c_lines.append("")
    c_lines.append("const robo_unit_row_t robo_unit_rows[] = {")
    for r in unit_rows:
        pats = ", ".join(str(x) for x in r["pats"])
        c_lines.append(
            "    {"
            f"{r['locale']}, {r['unit']}u, {r['width']}u, {r['display']}, "
            f"{{{pats}}}, {r['per_unit']}"
            "},"
        )
    c_lines.append("};")
    c_lines.append(f"const size_t robo_unit_rows_count = {len(unit_rows)};")
    c_lines.append("")
    c_lines.append("const robo_unit_compound_t robo_unit_compounds[] = {")
    for r in compound_rows:
        c_lines.append(
            f"    {{{r['locale']}, {r['width']}u, {r['per']}, {r['times']}}},"
        )
    c_lines.append("};")
    c_lines.append(f"const size_t robo_unit_compounds_count = {len(compound_rows)};")
    c_lines.append("")
    c_lines.append(
        r"""
const char *robo_unit_str(int32_t idx)
{
    if (idx < 0 || (size_t)idx >= robo_unit_string_count) {
        return "";
    }
    return robo_unit_strings[idx];
}

const char *robo_unit_cldr_key(uint16_t unit)
{
    if ((size_t)unit >= ROBO_UNIT_ID_COUNT) {
        return NULL;
    }
    return robo_unit_cldr_keys[unit];
}

const robo_unit_row_t *robo_unit_row_find(const char *locale, uint16_t unit, uint8_t width)
{
    size_t lo = 0;
    size_t hi = robo_unit_rows_count;
    if (!locale) {
        return NULL;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const robo_unit_row_t *row = &robo_unit_rows[mid];
        int cmp = strcmp(robo_unit_strings[row->locale], locale);
        if (cmp == 0) {
            if (row->unit == unit) {
                if (row->width == width) {
                    return row;
                }
                cmp = row->width < width ? -1 : 1;
            } else {
                cmp = row->unit < unit ? -1 : 1;
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

const robo_unit_compound_t *robo_unit_compound_find(const char *locale, uint8_t width)
{
    size_t lo = 0;
    size_t hi = robo_unit_compounds_count;
    if (!locale) {
        return NULL;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const robo_unit_compound_t *row = &robo_unit_compounds[mid];
        int cmp = strcmp(robo_unit_strings[row->locale], locale);
        if (cmp == 0) {
            if (row->width == width) {
                return row;
            }
            cmp = row->width < width ? -1 : 1;
        }
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const char *robo_unit_version_string(void)
{
    return ROBO_UNIT_CLDR_VERSION;
}
""".strip(
            "\n"
        )
    )

    c_text = "\n".join(c_lines) + "\n"

    if repro:
        if not OUT_H.is_file() or not OUT_C.is_file() or not GOLDEN.is_file():
            raise SystemExit("units repro: missing outputs/golden")
        golden = json.loads(GOLDEN.read_text(encoding="utf-8"))
        if sha256_file(OUT_H) != golden["RoboUnitData.h"]:
            raise SystemExit("units repro: RoboUnitData.h hash mismatch")
        if sha256_file(OUT_C) != golden["robo_unit_data.c"]:
            raise SystemExit("units repro: robo_unit_data.c hash mismatch")
        print("units repro OK")
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
            "RoboUnitData.h": sha256_file(OUT_H),
            "robo_unit_data.c": sha256_file(OUT_C),
            "unit_count": len(UNIT_KEYS),
            "row_count": len(unit_rows),
            "compound_count": len(compound_rows),
        }
        for path in paths:
            rel = str(path.relative_to(HERE / "source" / f"cldr-{CLDR_JSON_PACKAGE}")).replace(
                "\\", "/"
            )
            hashes[rel] = sha256_file(path)
        GOLDEN.write_text(json.dumps(hashes, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"Wrote {GOLDEN.name}")

    print(
        f"Wrote unit tables: locales={len({pool[r['locale']] for r in unit_rows})} "
        f"units={len(UNIT_KEYS)} rows={len(unit_rows)} compounds={len(compound_rows)} "
        f"pool={len(pool)} bytes_c={len(c_text.encode())}"
    )
    # Emit enum hint for public header (not compiled from here).
    print("enum preview:")
    for line in enum_lines[:5]:
        print(line)
    print("...")
    return 0


def main() -> int:
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument("--write-golden", action="store_true")
    ap.add_argument("--repro", action="store_true")
    args = ap.parse_args()
    return write_units(repro=args.repro, write_golden=args.write_golden)


if __name__ == "__main__":
    raise SystemExit(main())

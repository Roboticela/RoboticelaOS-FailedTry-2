#!/usr/bin/env python3
"""
Generate compact RoboUnicode UCD property tables from Unicode 16.0.0 sources.

Usage:
  python runtime/Tools/ucd/generate_ucd.py

Inputs (official Unicode Consortium files):
  runtime/Tools/ucd/source/*.txt

Outputs:
  runtime/Include/RoboUnicode/RoboUcdData.h
  runtime/Source/RoboUnicode/data/robo_ucd_data.c

License: Unicode Data Files are under the Unicode License Agreement.
See runtime/Tools/ucd/UNICODE_DATA_LICENSE.txt
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]  # runtime/
SRC = Path(__file__).resolve().parent / "source"
OUT_H = ROOT / "Include" / "RoboUnicode" / "RoboUcdData.h"
OUT_C = ROOT / "Source" / "RoboUnicode" / "data" / "robo_ucd_data.c"

UCD_VERSION = "16.0.0"
MAX_CP = 0x110000

GC_ORDER = [
    "Lu", "Ll", "Lt", "Lm", "Lo",
    "Mn", "Mc", "Me",
    "Nd", "Nl", "No",
    "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po",
    "Sm", "Sc", "Sk", "So",
    "Zs", "Zl", "Zp",
    "Cc", "Cf", "Cs", "Co", "Cn",
]

EAW_ORDER = ["N", "A", "H", "W", "F", "Na"]

BOOL_PROPS = [
    "Alphabetic",
    "Uppercase",
    "Lowercase",
    "White_Space",
    "Default_Ignorable_Code_Point",
    "Emoji",
    "Emoji_Presentation",
    "Extended_Pictographic",
]


def parse_codepoint(tok: str) -> int:
    return int(tok.strip(), 16)


def parse_range(field: str) -> tuple[int, int]:
    field = field.strip()
    if ".." in field:
        a, b = field.split("..")
        return parse_codepoint(a), parse_codepoint(b)
    cp = parse_codepoint(field)
    return cp, cp


def load_lines(path: Path):
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                yield line


def apply_ranges(arr, ranges, value):
    for start, end in ranges:
        for cp in range(start, end + 1):
            if 0 <= cp < MAX_CP:
                arr[cp] = value


def compress_ranges(arr, default):
    """Convert dense array to (start, end, value) ranges, skipping default."""
    ranges = []
    i = 0
    while i < MAX_CP:
        v = arr[i]
        j = i + 1
        while j < MAX_CP and arr[j] == v:
            j += 1
        if v != default:
            ranges.append((i, j - 1, v))
        i = j
    return ranges


def compress_bool(arr):
    """Ranges where value is True."""
    ranges = []
    i = 0
    while i < MAX_CP:
        if not arr[i]:
            i += 1
            continue
        j = i + 1
        while j < MAX_CP and arr[j]:
            j += 1
        ranges.append((i, j - 1))
        i = j
    return ranges


def parse_unicode_data(path: Path):
    gc = ["Cn"] * MAX_CP
    ccc = [0] * MAX_CP
    lines = list(load_lines(path))
    i = 0
    while i < len(lines):
        parts = lines[i].split(";")
        cp = parse_codepoint(parts[0])
        name = parts[1]
        cat = parts[2]
        cc = int(parts[3])
        if name.endswith(", First>"):
            parts2 = lines[i + 1].split(";")
            cp2 = parse_codepoint(parts2[0])
            cat2 = parts2[2]
            cc2 = int(parts2[3])
            assert cat == cat2 and cc == cc2
            for c in range(cp, cp2 + 1):
                gc[c] = cat
                ccc[c] = cc
            i += 2
            continue
        gc[cp] = cat
        ccc[cp] = cc
        i += 1
    return gc, ccc


def parse_property_file(path: Path, wanted: set[str] | None = None):
    """Parse Scripts.txt / PropList / DerivedCore / EAW / emoji-data style files."""
    props: dict[str, list[tuple[int, int]]] = {}
    for line in load_lines(path):
        # code ; property
        bits = [b.strip() for b in line.split(";")]
        if len(bits) < 2:
            continue
        start, end = parse_range(bits[0])
        prop = bits[1].split()[0] if " " in bits[1] else bits[1]
        # EastAsianWidth: code ; W
        if wanted is not None and prop not in wanted:
            continue
        props.setdefault(prop, []).append((start, end))
    return props


def c_ident(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9]", "_", name).upper()


def emit_u16_ranges(name: str, ranges: list[tuple[int, int, int]]) -> str:
    lines = [f"static const robo_ucd_range16_t {name}[] = {{"]
    for a, b, v in ranges:
        lines.append(f"    {{ 0x{a:04X}u, 0x{b:04X}u, {v} }},")
    lines.append("};")
    lines.append(f"static const size_t {name}_count = {len(ranges)};")
    return "\n".join(lines)


def emit_bool_ranges(name: str, ranges: list[tuple[int, int]]) -> str:
    lines = [f"static const robo_ucd_range_t {name}[] = {{"]
    for a, b in ranges:
        lines.append(f"    {{ 0x{a:04X}u, 0x{b:04X}u }},")
    if not ranges:
        lines.append("    { 0, 0 }, /* placeholder — count 0 */")
    lines.append("};")
    lines.append(f"static const size_t {name}_count = {len(ranges)};")
    return "\n".join(lines)


def main() -> int:
    for req in (
        "UnicodeData.txt",
        "Scripts.txt",
        "EastAsianWidth.txt",
        "PropList.txt",
        "DerivedCoreProperties.txt",
        "emoji-data.txt",
    ):
        if not (SRC / req).exists():
            print(f"Missing {SRC / req}", file=sys.stderr)
            return 1

    print("Parsing UnicodeData.txt ...")
    gc_arr, ccc_arr = parse_unicode_data(SRC / "UnicodeData.txt")

    print("Parsing Scripts.txt ...")
    script_props = parse_property_file(SRC / "Scripts.txt")
    # Collect all scripts, stable order: Common, Inherited, then alpha
    script_names = sorted(script_props.keys())
    prefer = ["Common", "Inherited", "Unknown"]
    scripts = [s for s in prefer if s in script_names] + [
        s for s in script_names if s not in prefer
    ]
    if "Unknown" not in scripts:
        scripts.append("Unknown")
    script_index = {n: i for i, n in enumerate(scripts)}
    script_arr = [script_index["Unknown"]] * MAX_CP
    for name, ranges in script_props.items():
        idx = script_index[name]
        for a, b in ranges:
            for cp in range(a, b + 1):
                if 0 <= cp < MAX_CP:
                    script_arr[cp] = idx

    print("Parsing EastAsianWidth.txt ...")
    eaw_props = parse_property_file(SRC / "EastAsianWidth.txt")
    eaw_index = {n: i for i, n in enumerate(EAW_ORDER)}
    eaw_arr = [eaw_index["N"]] * MAX_CP
    for name, ranges in eaw_props.items():
        if name not in eaw_index:
            continue
        idx = eaw_index[name]
        for a, b in ranges:
            for cp in range(a, b + 1):
                if 0 <= cp < MAX_CP:
                    eaw_arr[cp] = idx

    print("Parsing boolean properties ...")
    bool_maps = {p: [False] * MAX_CP for p in BOOL_PROPS}
    for path, names in (
        (SRC / "DerivedCoreProperties.txt", {"Alphabetic", "Uppercase", "Lowercase", "Default_Ignorable_Code_Point"}),
        (SRC / "PropList.txt", {"White_Space"}),
        (SRC / "emoji-data.txt", {"Emoji", "Emoji_Presentation", "Extended_Pictographic"}),
    ):
        props = parse_property_file(path, names)
        for name, ranges in props.items():
            if name not in bool_maps:
                continue
            for a, b in ranges:
                for cp in range(a, b + 1):
                    if 0 <= cp < MAX_CP:
                        bool_maps[name][cp] = True

    gc_index = {n: i for i, n in enumerate(GC_ORDER)}
    gc_num = [gc_index.get(g, gc_index["Cn"]) for g in gc_arr]

    print("Compressing ranges ...")
    gc_ranges = compress_ranges(gc_num, gc_index["Cn"])
    ccc_ranges = compress_ranges(ccc_arr, 0)
    script_ranges = compress_ranges(script_arr, script_index["Unknown"])
    eaw_ranges = compress_ranges(eaw_arr, eaw_index["N"])
    bool_ranges = {p: compress_bool(bool_maps[p]) for p in BOOL_PROPS}

    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.parent.mkdir(parents=True, exist_ok=True)

    # --- Header ---
    h = []
    h.append("/* Auto-generated by Tools/ucd/generate_ucd.py — DO NOT EDIT. */")
    h.append(f"/* Unicode UCD version: {UCD_VERSION} */")
    h.append("#ifndef ROBO_UCD_DATA_H")
    h.append("#define ROBO_UCD_DATA_H")
    h.append("")
    h.append('#include "RoboUnicode/RoboUnicode.h"')
    h.append("#include <stddef.h>")
    h.append("#include <stdint.h>")
    h.append("")
    h.append("#ifdef __cplusplus")
    h.append('extern "C" {')
    h.append("#endif")
    h.append("")
    h.append(f'#define ROBO_UCD_VERSION_STRING "{UCD_VERSION}"')
    h.append("")
    h.append("typedef enum robo_unicode_general_category {")
    for i, g in enumerate(GC_ORDER):
        h.append(f"    ROBO_U_GC_{g.upper()} = {i},")
    h.append("    ROBO_U_GC_COUNT")
    h.append("} robo_unicode_general_category_t;")
    h.append("")
    h.append("typedef enum robo_unicode_script {")
    for i, s in enumerate(scripts):
        h.append(f"    ROBO_U_SCRIPT_{c_ident(s)} = {i},")
    h.append("    ROBO_U_SCRIPT_COUNT")
    h.append("} robo_unicode_script_t;")
    h.append("")
    h.append("typedef enum robo_unicode_east_asian_width {")
    eaw_enum = {
        "N": "NEUTRAL",
        "A": "AMBIGUOUS",
        "H": "HALFWIDTH",
        "W": "WIDE",
        "F": "FULLWIDTH",
        "Na": "NARROW",
    }
    for i, e in enumerate(EAW_ORDER):
        h.append(f"    ROBO_U_EAW_{eaw_enum[e]} = {i},")
    h.append("    ROBO_U_EAW_COUNT")
    h.append("} robo_unicode_east_asian_width_t;")
    h.append("")
    h.append("typedef struct robo_ucd_range {")
    h.append("    uint32_t start;")
    h.append("    uint32_t end;")
    h.append("} robo_ucd_range_t;")
    h.append("")
    h.append("typedef struct robo_ucd_range16 {")
    h.append("    uint32_t start;")
    h.append("    uint32_t end;")
    h.append("    uint16_t value;")
    h.append("} robo_ucd_range16_t;")
    h.append("")
    h.append("robo_unicode_general_category_t robo_ucd_general_category(robo_uchar32_t cp);")
    h.append("robo_unicode_script_t robo_ucd_script(robo_uchar32_t cp);")
    h.append("unsigned robo_ucd_combining_class(robo_uchar32_t cp);")
    h.append("robo_unicode_east_asian_width_t robo_ucd_east_asian_width(robo_uchar32_t cp);")
    for p in BOOL_PROPS:
        h.append(f"int robo_ucd_is_{p.lower()}(robo_uchar32_t cp);")
    h.append("const char *robo_ucd_version_string(void);")
    h.append("const char *robo_ucd_general_category_name(robo_unicode_general_category_t gc);")
    h.append("const char *robo_ucd_script_name(robo_unicode_script_t sc);")
    h.append("")
    h.append("#ifdef __cplusplus")
    h.append("}")
    h.append("#endif")
    h.append("")
    h.append("#endif /* ROBO_UCD_DATA_H */")
    OUT_H.write_text("\n".join(h) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_H}")

    # --- C file ---
    c = []
    c.append("/* Auto-generated by Tools/ucd/generate_ucd.py — DO NOT EDIT. */")
    c.append(f"/* Unicode UCD version: {UCD_VERSION} */")
    c.append("/* Source: Unicode Consortium UCD files under the Unicode License. */")
    c.append("")
    c.append('#include "RoboUnicode/RoboUcdData.h"')
    c.append("")
    c.append(emit_u16_ranges("robo_ucd_gc_ranges", gc_ranges))
    c.append("")
    c.append(emit_u16_ranges("robo_ucd_ccc_ranges", ccc_ranges))
    c.append("")
    c.append(emit_u16_ranges("robo_ucd_script_ranges", script_ranges))
    c.append("")
    c.append(emit_u16_ranges("robo_ucd_eaw_ranges", eaw_ranges))
    c.append("")
    for p in BOOL_PROPS:
        c.append(emit_bool_ranges(f"robo_ucd_{p.lower()}_ranges", bool_ranges[p]))
        c.append("")

    c.append(
        """
static int robo_ucd_bsearch_u16(const robo_ucd_range16_t *ranges, size_t count,
                                uint32_t cp, uint16_t default_value)
{
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (cp < ranges[mid].start) {
            hi = mid;
        } else if (cp > ranges[mid].end) {
            lo = mid + 1u;
        } else {
            return (int)ranges[mid].value;
        }
    }
    return (int)default_value;
}

static int robo_ucd_bsearch_bool(const robo_ucd_range_t *ranges, size_t count,
                                 uint32_t cp)
{
    size_t lo = 0, hi = count;
    if (count == 0) {
        return 0;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (cp < ranges[mid].start) {
            hi = mid;
        } else if (cp > ranges[mid].end) {
            lo = mid + 1u;
        } else {
            return 1;
        }
    }
    return 0;
}
"""
    )

    c.append(
        f"""
const char *robo_ucd_version_string(void)
{{
    return ROBO_UCD_VERSION_STRING;
}}

robo_unicode_general_category_t robo_ucd_general_category(robo_uchar32_t cp)
{{
    if (cp > ROBO_UCHAR32_MAX) {{
        return ROBO_U_GC_CN;
    }}
    return (robo_unicode_general_category_t)robo_ucd_bsearch_u16(
        robo_ucd_gc_ranges, robo_ucd_gc_ranges_count, cp, (uint16_t)ROBO_U_GC_CN);
}}

unsigned robo_ucd_combining_class(robo_uchar32_t cp)
{{
    if (cp > ROBO_UCHAR32_MAX) {{
        return 0;
    }}
    return (unsigned)robo_ucd_bsearch_u16(
        robo_ucd_ccc_ranges, robo_ucd_ccc_ranges_count, cp, 0);
}}

robo_unicode_script_t robo_ucd_script(robo_uchar32_t cp)
{{
    if (cp > ROBO_UCHAR32_MAX) {{
        return ROBO_U_SCRIPT_UNKNOWN;
    }}
    return (robo_unicode_script_t)robo_ucd_bsearch_u16(
        robo_ucd_script_ranges, robo_ucd_script_ranges_count, cp,
        (uint16_t)ROBO_U_SCRIPT_UNKNOWN);
}}

robo_unicode_east_asian_width_t robo_ucd_east_asian_width(robo_uchar32_t cp)
{{
    if (cp > ROBO_UCHAR32_MAX) {{
        return ROBO_U_EAW_NEUTRAL;
    }}
    return (robo_unicode_east_asian_width_t)robo_ucd_bsearch_u16(
        robo_ucd_eaw_ranges, robo_ucd_eaw_ranges_count, cp,
        (uint16_t)ROBO_U_EAW_NEUTRAL);
}}
"""
    )

    for p in BOOL_PROPS:
        fn = p.lower()
        c.append(
            f"""
int robo_ucd_is_{fn}(robo_uchar32_t cp)
{{
    if (cp > ROBO_UCHAR32_MAX) {{
        return 0;
    }}
    return robo_ucd_bsearch_bool(robo_ucd_{fn}_ranges, robo_ucd_{fn}_ranges_count, cp);
}}
"""
        )

    # Name tables
    c.append("static const char *const robo_ucd_gc_names[] = {")
    for g in GC_ORDER:
        c.append(f'    "{g}",')
    c.append("};")
    c.append("")
    c.append("static const char *const robo_ucd_script_names[] = {")
    for s in scripts:
        c.append(f'    "{s}",')
    c.append("};")
    c.append(
        """
const char *robo_ucd_general_category_name(robo_unicode_general_category_t gc)
{
    if ((unsigned)gc >= (unsigned)ROBO_U_GC_COUNT) {
        return "?";
    }
    return robo_ucd_gc_names[gc];
}

const char *robo_ucd_script_name(robo_unicode_script_t sc)
{
    if ((unsigned)sc >= (unsigned)ROBO_U_SCRIPT_COUNT) {
        return "?";
    }
    return robo_ucd_script_names[sc];
}
"""
    )

    OUT_C.write_text("\n".join(c) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_C}")
    print(
        f"Ranges: gc={len(gc_ranges)} ccc={len(ccc_ranges)} "
        f"script={len(script_ranges)} eaw={len(eaw_ranges)}"
    )
    for p in BOOL_PROPS:
        print(f"  {p}: {len(bool_ranges[p])} ranges")
    return 0


if __name__ == "__main__":
    sys.exit(main())

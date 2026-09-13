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
  runtime/Tests/Data/robo_script_conformance.bin
  runtime/Tests/Data/robo_script_conformance.h

License: Unicode Data Files are under the Unicode License Agreement.
See runtime/Tools/ucd/UNICODE_DATA_LICENSE.txt
"""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]  # runtime/
SRC = Path(__file__).resolve().parent / "source"
OUT_H = ROOT / "Include" / "RoboUnicode" / "RoboUcdData.h"
OUT_C = ROOT / "Source" / "RoboUnicode" / "data" / "robo_ucd_data.c"
OUT_TEST_BIN = ROOT / "Tests" / "Data" / "robo_script_conformance.bin"
OUT_TEST_META = ROOT / "Tests" / "Data" / "robo_script_conformance.h"

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
        bits = [b.strip() for b in line.split(";")]
        if len(bits) < 2:
            continue
        start, end = parse_range(bits[0])
        prop = bits[1].split()[0] if " " in bits[1] else bits[1]
        if wanted is not None and prop not in wanted:
            continue
        props.setdefault(prop, []).append((start, end))
    return props


def parse_script_aliases(path: Path) -> tuple[dict[str, str], dict[str, str], dict[str, list[str]]]:
    """
    Parse PropertyValueAliases `sc` lines.

    Returns:
      short_to_long: Arab -> Arabic
      long_to_short: Arabic -> Arab (primary short)
      name_aliases: any alias/short/long -> list of all names that map to same long
    """
    short_to_long: dict[str, str] = {}
    long_to_short: dict[str, str] = {}
    extras: dict[str, str] = {}  # extra alias -> long
    for line in load_lines(path):
        if not line.startswith("sc"):
            continue
        # sc ; Short ; Long [; Alias...]
        parts = [p.strip() for p in line.split(";")]
        if len(parts) < 3 or parts[0] != "sc":
            continue
        short = parts[1]
        long = parts[2]
        short_to_long[short] = long
        long_to_short[long] = short
        for extra in parts[3:]:
            if extra:
                extras[extra] = long
    return short_to_long, long_to_short, extras


def parse_script_extensions(path: Path) -> list[tuple[int, int, list[str]]]:
    """Parse ScriptExtensions.txt → list of (start, end, [short codes])."""
    out: list[tuple[int, int, list[str]]] = []
    for line in load_lines(path):
        bits = [b.strip() for b in line.split(";")]
        if len(bits) < 2:
            continue
        start, end = parse_range(bits[0])
        shorts = bits[1].split()
        if not shorts:
            continue
        out.append((start, end, shorts))
    return out


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


def build_scx_tables(
    scx_entries: list[tuple[int, int, list[str]]],
    short_to_long: dict[str, str],
    script_index: dict[str, int],
) -> tuple[list[int], list[tuple[int, ...]], list[int], list[tuple[int, int, list[int]]]]:
    """
    Build:
      scx_arr[cp] = set index (0 = no explicit SCX)
      unique_sets: index -> sorted script id tuple (index 0 unused / empty)
      pool flat uint16 list
      conf_ranges: (start, end, sorted script ids) for conformance binary
    """
    # Deduplicate sets: map frozenset/tuple -> index starting at 1
    set_to_index: dict[tuple[int, ...], int] = {}
    unique_sets: list[tuple[int, ...]] = [()]  # index 0 sentinel
    scx_arr = [0] * MAX_CP
    conf_ranges: list[tuple[int, int, list[int]]] = []

    for start, end, shorts in scx_entries:
        ids: list[int] = []
        for sh in shorts:
            long = short_to_long.get(sh)
            if long is None:
                raise SystemExit(f"Unknown Script_Extensions short code: {sh}")
            if long not in script_index:
                raise SystemExit(f"SCX script {long} ({sh}) not in Scripts enum")
            ids.append(script_index[long])
        ids_sorted = tuple(sorted(set(ids)))
        if ids_sorted not in set_to_index:
            set_to_index[ids_sorted] = len(unique_sets)
            unique_sets.append(ids_sorted)
        idx = set_to_index[ids_sorted]
        for cp in range(start, end + 1):
            if 0 <= cp < MAX_CP:
                scx_arr[cp] = idx
        conf_ranges.append((start, end, list(ids_sorted)))

    return scx_arr, unique_sets, conf_ranges


def write_script_conformance(
    conf_ranges: list[tuple[int, int, list[int]]],
    script_props: dict[str, list[tuple[int, int]]],
    script_index: dict[str, int],
) -> None:
    """
    Binary format (little-endian):
      magic 'SCRP' (4)
      ver_major u16, ver_minor u16, ver_patch u16
      scx_count u32
      sample_count u32
      for each SCX range:
        start u32, end u32, count u16, scripts[count] u16
      for each Scripts.txt range start sample:
        cp u32, script u16
    """
    major, minor, patch = (int(x) for x in UCD_VERSION.split("."))
    samples: list[tuple[int, int]] = []
    for name, ranges in sorted(script_props.items()):
        idx = script_index[name]
        for a, _b in ranges:
            samples.append((a, idx))
    samples.sort(key=lambda t: t[0])

    OUT_TEST_BIN.parent.mkdir(parents=True, exist_ok=True)
    buf = bytearray()
    buf += b"SCRP"
    buf += struct.pack("<HHH", major, minor, patch)
    buf += struct.pack("<II", len(conf_ranges), len(samples))
    for start, end, scripts in conf_ranges:
        buf += struct.pack("<IIH", start, end, len(scripts))
        for s in scripts:
            buf += struct.pack("<H", s)
    for cp, sc in samples:
        buf += struct.pack("<IH", cp, sc)

    OUT_TEST_BIN.write_bytes(buf)
    OUT_TEST_META.write_text(
        "\n".join(
            [
                "/* Auto-generated by Tools/ucd/generate_ucd.py — DO NOT EDIT. */",
                "#ifndef ROBO_SCRIPT_CONFORMANCE_H",
                "#define ROBO_SCRIPT_CONFORMANCE_H",
                "",
                f'#define ROBO_SCRIPT_CONF_MAGIC "SCRP"',
                f"#define ROBO_SCRIPT_CONF_VERSION_MAJOR {major}",
                f"#define ROBO_SCRIPT_CONF_VERSION_MINOR {minor}",
                f"#define ROBO_SCRIPT_CONF_VERSION_PATCH {patch}",
                f"#define ROBO_SCRIPT_CONF_SCX_RANGES {len(conf_ranges)}",
                f"#define ROBO_SCRIPT_CONF_SCRIPT_SAMPLES {len(samples)}",
                f"#define ROBO_SCRIPT_CONF_BYTES {len(buf)}",
                "",
                "#endif /* ROBO_SCRIPT_CONFORMANCE_H */",
                "",
            ]
        ),
        encoding="utf-8",
    )
    print(f"Wrote {OUT_TEST_BIN} ({len(buf)} bytes)")
    print(f"Wrote {OUT_TEST_META}")


def main() -> int:
    for req in (
        "UnicodeData.txt",
        "Scripts.txt",
        "ScriptExtensions.txt",
        "PropertyValueAliases.txt",
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

    print("Parsing PropertyValueAliases.txt (sc) ...")
    short_to_long, long_to_short, extra_aliases = parse_script_aliases(
        SRC / "PropertyValueAliases.txt"
    )
    # Ensure every script enum has a short name (Unknown may only appear via aliases)
    for s in scripts:
        if s not in long_to_short:
            if s == "Unknown":
                long_to_short["Unknown"] = "Zzzz"
                short_to_long.setdefault("Zzzz", "Unknown")
            else:
                raise SystemExit(f"No ISO 15924 short name for script {s}")

    short_names = [long_to_short[s] for s in scripts]

    print("Parsing ScriptExtensions.txt ...")
    scx_entries = parse_script_extensions(SRC / "ScriptExtensions.txt")
    scx_arr, unique_sets, conf_ranges = build_scx_tables(
        scx_entries, short_to_long, script_index
    )

    # Flatten pool + set descriptors
    pool: list[int] = []
    set_descs: list[tuple[int, int]] = []  # (offset, count)
    for s in unique_sets:
        off = len(pool)
        pool.extend(s)
        set_descs.append((off, len(s)))

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
    scx_ranges = compress_ranges(scx_arr, 0)  # 0 = no explicit SCX
    eaw_ranges = compress_ranges(eaw_arr, eaw_index["N"])
    bool_ranges = {p: compress_bool(bool_maps[p]) for p in BOOL_PROPS}

    write_script_conformance(conf_ranges, script_props, script_index)

    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.parent.mkdir(parents=True, exist_ok=True)

    # Name lookup entries for from_name (long, short, extras) — case-sensitive
    name_map: list[tuple[str, int]] = []
    seen_names: set[str] = set()
    for s in scripts:
        for alias in (s, long_to_short[s]):
            if alias not in seen_names:
                name_map.append((alias, script_index[s]))
                seen_names.add(alias)
    for alias, long in extra_aliases.items():
        if long in script_index and alias not in seen_names:
            name_map.append((alias, script_index[long]))
            seen_names.add(alias)
    # Stable: longer names first is irrelevant; keep insertion then alpha for extras already handled
    name_map.sort(key=lambda t: t[0])

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
    h.append("typedef struct robo_ucd_scx_set {")
    h.append("    uint16_t offset; /* into robo_ucd_scx_pool */")
    h.append("    uint16_t count;")
    h.append("} robo_ucd_scx_set_t;")
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
    h.append("/**")
    h.append(" * Script_Extensions for @p cp.")
    h.append(" * Returns the number of scripts (always >= 1 for valid cps).")
    h.append(" * Writes min(count, capacity) entries into @p out when out != NULL.")
    h.append(" * If the code point has no explicit SCX, writes the single Script value.")
    h.append(" */")
    h.append("size_t robo_ucd_script_extensions(robo_uchar32_t cp,")
    h.append("                                  robo_unicode_script_t *out,")
    h.append("                                  size_t capacity);")
    h.append("")
    h.append("/** 1 if Script_Extensions (or Script if no SCX) includes @p sc. */")
    h.append("int robo_ucd_has_script(robo_uchar32_t cp, robo_unicode_script_t sc);")
    h.append("")
    h.append("/** ISO 15924 short name (Latn, Arab, Zyyy, …); \"?\" if out of range. */")
    h.append("const char *robo_ucd_script_short_name(robo_unicode_script_t sc);")
    h.append("")
    h.append("/**")
    h.append(" * Resolve long or short script name (case-sensitive Unicode aliases).")
    h.append(" * Also accepts Qaai → Inherited. Returns ROBO_U_SCRIPT_UNKNOWN on failure.")
    h.append(" */")
    h.append("robo_unicode_script_t robo_ucd_script_from_name(const char *name);")
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
    c.append("#include <string.h>")
    c.append("")
    c.append(emit_u16_ranges("robo_ucd_gc_ranges", gc_ranges))
    c.append("")
    c.append(emit_u16_ranges("robo_ucd_ccc_ranges", ccc_ranges))
    c.append("")
    c.append(emit_u16_ranges("robo_ucd_script_ranges", script_ranges))
    c.append("")
    c.append(emit_u16_ranges("robo_ucd_scx_index_ranges", scx_ranges))
    c.append("")
    c.append(emit_u16_ranges("robo_ucd_eaw_ranges", eaw_ranges))
    c.append("")
    for p in BOOL_PROPS:
        c.append(emit_bool_ranges(f"robo_ucd_{p.lower()}_ranges", bool_ranges[p]))
        c.append("")

    # SCX pool + sets
    c.append("static const uint16_t robo_ucd_scx_pool[] = {")
    if pool:
        row = []
        for i, v in enumerate(pool):
            row.append(f"{v}u")
            if len(row) == 16:
                c.append("    " + ", ".join(row) + ",")
                row = []
        if row:
            c.append("    " + ", ".join(row) + ",")
    else:
        c.append("    0u, /* unused */")
    c.append("};")
    c.append("")
    c.append("static const robo_ucd_scx_set_t robo_ucd_scx_set[] = {")
    for off, cnt in set_descs:
        c.append(f"    {{ {off}u, {cnt}u }},")
    c.append("};")
    c.append(f"static const size_t robo_ucd_scx_set_count = {len(set_descs)};")
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
    c.append("")
    c.append("static const char *const robo_ucd_script_short_names[] = {")
    for sh in short_names:
        c.append(f'    "{sh}",')
    c.append("};")
    c.append("")
    c.append("typedef struct robo_ucd_script_name_entry {")
    c.append("    const char *name;")
    c.append("    uint16_t script;")
    c.append("} robo_ucd_script_name_entry_t;")
    c.append("")
    c.append("static const robo_ucd_script_name_entry_t robo_ucd_script_name_map[] = {")
    for name, idx in name_map:
        c.append(f'    {{ "{name}", {idx}u }},')
    c.append("};")
    c.append(
        f"static const size_t robo_ucd_script_name_map_count = {len(name_map)};"
    )
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

const char *robo_ucd_script_short_name(robo_unicode_script_t sc)
{
    if ((unsigned)sc >= (unsigned)ROBO_U_SCRIPT_COUNT) {
        return "?";
    }
    return robo_ucd_script_short_names[sc];
}

robo_unicode_script_t robo_ucd_script_from_name(const char *name)
{
    size_t lo, hi;
    if (!name) {
        return ROBO_U_SCRIPT_UNKNOWN;
    }
    lo = 0;
    hi = robo_ucd_script_name_map_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        int cmp = strcmp(name, robo_ucd_script_name_map[mid].name);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp > 0) {
            lo = mid + 1u;
        } else {
            return (robo_unicode_script_t)robo_ucd_script_name_map[mid].script;
        }
    }
    return ROBO_U_SCRIPT_UNKNOWN;
}

size_t robo_ucd_script_extensions(robo_uchar32_t cp,
                                  robo_unicode_script_t *out,
                                  size_t capacity)
{
    uint16_t set_idx;
    size_t count;
    size_t i;
    size_t nwrite;
    const robo_ucd_scx_set_t *set;

    if (cp > ROBO_UCHAR32_MAX) {
        if (out && capacity > 0) {
            out[0] = ROBO_U_SCRIPT_UNKNOWN;
        }
        return 1u;
    }

    set_idx = (uint16_t)robo_ucd_bsearch_u16(
        robo_ucd_scx_index_ranges, robo_ucd_scx_index_ranges_count, cp, 0);
    if (set_idx == 0 || set_idx >= (uint16_t)robo_ucd_scx_set_count) {
        if (out && capacity > 0) {
            out[0] = robo_ucd_script(cp);
        }
        return 1u;
    }

    set = &robo_ucd_scx_set[set_idx];
    count = (size_t)set->count;
    nwrite = count;
    if (nwrite > capacity) {
        nwrite = capacity;
    }
    if (out) {
        for (i = 0; i < nwrite; i++) {
            out[i] = (robo_unicode_script_t)robo_ucd_scx_pool[set->offset + i];
        }
    }
    return count;
}

int robo_ucd_has_script(robo_uchar32_t cp, robo_unicode_script_t sc)
{
    uint16_t set_idx;
    size_t i;
    const robo_ucd_scx_set_t *set;

    if (cp > ROBO_UCHAR32_MAX) {
        return sc == ROBO_U_SCRIPT_UNKNOWN;
    }
    if ((unsigned)sc >= (unsigned)ROBO_U_SCRIPT_COUNT) {
        return 0;
    }

    set_idx = (uint16_t)robo_ucd_bsearch_u16(
        robo_ucd_scx_index_ranges, robo_ucd_scx_index_ranges_count, cp, 0);
    if (set_idx == 0 || set_idx >= (uint16_t)robo_ucd_scx_set_count) {
        return robo_ucd_script(cp) == sc;
    }

    set = &robo_ucd_scx_set[set_idx];
    for (i = 0; i < (size_t)set->count; i++) {
        if ((robo_unicode_script_t)robo_ucd_scx_pool[set->offset + i] == sc) {
            return 1;
        }
    }
    return 0;
}
"""
    )

    OUT_C.write_text("\n".join(c) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_C}")
    print(
        f"Ranges: gc={len(gc_ranges)} ccc={len(ccc_ranges)} "
        f"script={len(script_ranges)} scx={len(scx_ranges)} "
        f"eaw={len(eaw_ranges)} scx_sets={len(unique_sets)} "
        f"scx_pool={len(pool)}"
    )
    for p in BOOL_PROPS:
        print(f"  {p}: {len(bool_ranges[p])} ranges")
    return 0


if __name__ == "__main__":
    sys.exit(main())

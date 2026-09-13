#!/usr/bin/env python3
"""
Generate Unicode case mapping / case folding tables for UCD 16.0.0.

Inputs:
  Tools/ucd/source/UnicodeData.txt
  Tools/ucd/source/SpecialCasing.txt
  Tools/ucd/source/CaseFolding.txt
  Tools/ucd/source/DerivedCoreProperties.txt  (Cased, Case_Ignorable for Final_Sigma)

Outputs:
  Include/RoboUnicode/RoboCaseData.h
  Source/RoboUnicode/data/robo_case_data.c
  Tests/Data/robo_case_conformance.inc
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(__file__).resolve().parent / "source"
OUT_H = ROOT / "Include" / "RoboUnicode" / "RoboCaseData.h"
OUT_C = ROOT / "Source" / "RoboUnicode" / "data" / "robo_case_data.c"
OUT_TEST = ROOT / "Tests" / "Data" / "robo_case_conformance.inc"

UCD_VERSION = "16.0.0"
MAX_CP = 0x110000
MAX_MAP = 4  # SpecialCasing max observed is 3 (FFI etc.)


def parse_cp(tok: str) -> int:
    return int(tok.strip(), 16)


def load_lines(path: Path):
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                yield line


def parse_mapping(field: str) -> list[int]:
    field = field.strip()
    if not field:
        return []
    return [parse_cp(x) for x in field.split()]


def parse_bool_prop(path: Path, prop_name: str) -> list[bool]:
    arr = [False] * MAX_CP
    for line in load_lines(path):
        if ";" not in line:
            continue
        left, rest = line.split(";", 1)
        name = rest.split("#", 1)[0].strip()
        if name != prop_name:
            continue
        left = left.strip()
        if ".." in left:
            a, b = left.split("..")
            start, end = parse_cp(a), parse_cp(b)
        else:
            start = end = parse_cp(left)
        for cp in range(start, end + 1):
            if 0 <= cp < MAX_CP:
                arr[cp] = True
    return arr


def compress_bool(arr: list[bool]):
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


def pack_map_table(entries: dict[int, list[int]]):
    """entries: cp -> list of mapped cps. Returns (index_rows, data)."""
    index_rows = []
    data: list[int] = []
    for cp in sorted(entries.keys()):
        mapping = entries[cp]
        if not mapping or mapping == [cp]:
            continue
        off = len(data)
        data.extend(mapping)
        index_rows.append((cp, off, len(mapping)))
    return index_rows, data


def main() -> int:
    ud = SRC / "UnicodeData.txt"
    sc = SRC / "SpecialCasing.txt"
    cf = SRC / "CaseFolding.txt"
    dcp = SRC / "DerivedCoreProperties.txt"
    for p in (ud, sc, cf):
        if not p.exists():
            print(f"Missing {p}", file=sys.stderr)
            return 1

    # Simple 1:1 from UnicodeData fields 12/13/14
    simple_upper: dict[int, int] = {}
    simple_lower: dict[int, int] = {}
    simple_title: dict[int, int] = {}
    lines = list(load_lines(ud))
    i = 0
    while i < len(lines):
        parts = lines[i].split(";")
        cp = parse_cp(parts[0])
        name = parts[1]
        if name.endswith(", First>"):
            i += 2
            continue
        if parts[12].strip():
            simple_upper[cp] = parse_cp(parts[12])
        if parts[13].strip():
            simple_lower[cp] = parse_cp(parts[13])
        if parts[14].strip():
            simple_title[cp] = parse_cp(parts[14])
        elif parts[12].strip():
            # Title defaults to Upper when absent (Unicode rule)
            simple_title[cp] = parse_cp(parts[12])
        i += 1

    # Full mappings = simple, overridden by unconditional SpecialCasing
    full_upper: dict[int, list[int]] = {k: [v] for k, v in simple_upper.items()}
    full_lower: dict[int, list[int]] = {k: [v] for k, v in simple_lower.items()}
    full_title: dict[int, list[int]] = {k: [v] for k, v in simple_title.items()}
    # Also store special-only (multi) even when simple maps to self
    final_sigma_lower: list[int] | None = None  # mapping for U+03A3 under Final_Sigma

    for line in load_lines(sc):
        parts = [x.strip() for x in line.split(";")]
        if len(parts) < 4:
            continue
        cp = parse_cp(parts[0])
        lower = parse_mapping(parts[1])
        title = parse_mapping(parts[2])
        upper = parse_mapping(parts[3])
        cond = parts[4].strip() if len(parts) > 4 else ""
        if cond:
            # Conditional: keep Final_Sigma for lowercase of U+03A3
            if "Final_Sigma" in cond and cp == 0x03A3:
                final_sigma_lower = lower
            continue  # skip other locale/context conditionals as default maps
        if lower:
            full_lower[cp] = lower
        if title:
            full_title[cp] = title
        if upper:
            full_upper[cp] = upper

    # Case folding
    fold_simple: dict[int, int] = {}  # C + S (1:1)
    fold_full: dict[int, list[int]] = {}  # C + F
    for line in load_lines(cf):
        parts = [x.strip() for x in line.split(";")]
        if len(parts) < 3:
            continue
        cp = parse_cp(parts[0])
        status = parts[1]
        mapping = parse_mapping(parts[2])
        if status in ("C", "S") and len(mapping) == 1:
            fold_simple[cp] = mapping[0]
        if status in ("C", "F"):
            fold_full[cp] = mapping
        # status T (Turkish) intentionally omitted for locale-independent folding

    # Cased / Case_Ignorable for Final_Sigma
    cased = parse_bool_prop(dcp, "Cased") if dcp.exists() else [False] * MAX_CP
    case_ign = (
        parse_bool_prop(dcp, "Case_Ignorable") if dcp.exists() else [False] * MAX_CP
    )
    # Also Lt from UnicodeData for cased if missing
    for line in lines:
        parts = line.split(";")
        if parts[1].endswith(", First>"):
            continue
        gc = parts[2]
        cp = parse_cp(parts[0])
        if gc == "Lt":
            cased[cp] = True

    # Pack tables
    su_idx = sorted(simple_upper.items())
    sl_idx = sorted(simple_lower.items())
    st_idx = sorted(simple_title.items())
    fs_idx = sorted(fold_simple.items())

    fu_rows, fu_data = pack_map_table(full_upper)
    fl_rows, fl_data = pack_map_table(full_lower)
    ft_rows, ft_data = pack_map_table(full_title)
    ff_rows, ff_data = pack_map_table(fold_full)

    cased_ranges = compress_bool(cased)
    ign_ranges = compress_bool(case_ign)

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_TEST.parent.mkdir(parents=True, exist_ok=True)

    h = [
        "/* Auto-generated by Tools/ucd/generate_case.py — DO NOT EDIT. */",
        f"/* Unicode UCD version: {UCD_VERSION} */",
        "#ifndef ROBO_CASE_DATA_H",
        "#define ROBO_CASE_DATA_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        f'#define ROBO_CASE_UCD_VERSION "{UCD_VERSION}"',
        f"#define ROBO_CASE_MAX_MAP {MAX_MAP}",
        "",
        "typedef struct robo_case_simple_entry {",
        "    uint32_t cp;",
        "    uint32_t mapped;",
        "} robo_case_simple_entry_t;",
        "",
        "typedef struct robo_case_full_index {",
        "    uint32_t cp;",
        "    uint16_t offset;",
        "    uint16_t length;",
        "} robo_case_full_index_t;",
        "",
        "typedef struct robo_case_bool_range {",
        "    uint32_t start;",
        "    uint32_t end;",
        "} robo_case_bool_range_t;",
        "",
        "/* Simple 1:1 mappings (UnicodeData). Identity if not found. */",
        "uint32_t robo_case_simple_upper(uint32_t cp);",
        "uint32_t robo_case_simple_lower(uint32_t cp);",
        "uint32_t robo_case_simple_title(uint32_t cp);",
        "uint32_t robo_case_simple_fold(uint32_t cp);",
        "",
        "/* Full mappings (SpecialCasing unconditional + simple). Writes 1..ROBO_CASE_MAX_MAP. */",
        "size_t robo_case_full_upper(uint32_t cp, uint32_t *out, size_t out_cap);",
        "size_t robo_case_full_lower(uint32_t cp, uint32_t *out, size_t out_cap);",
        "size_t robo_case_full_title(uint32_t cp, uint32_t *out, size_t out_cap);",
        "size_t robo_case_full_fold(uint32_t cp, uint32_t *out, size_t out_cap);",
        "",
        "/* Final_Sigma special lowercase for U+03A3 (context-sensitive, locale-independent). */",
        "size_t robo_case_final_sigma_lower(uint32_t *out, size_t out_cap);",
        "",
        "int robo_case_is_cased(uint32_t cp);",
        "int robo_case_is_case_ignorable(uint32_t cp);",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif",
    ]
    OUT_H.write_text("\n".join(h) + "\n", encoding="utf-8")

    def emit_simple(name: str, items: list[tuple[int, int]], c: list[str]):
        c.append(f"static const robo_case_simple_entry_t {name}[] = {{")
        for cp, m in items:
            c.append(f"    {{ 0x{cp:04X}u, 0x{m:04X}u }},")
        if not items:
            c.append("    { 0, 0 },")
        c.append("};")
        c.append(f"static const size_t {name}_count = {len(items)};")
        c.append("")

    def emit_full(idx_name: str, data_name: str, rows, data, c: list[str]):
        c.append(f"static const robo_case_full_index_t {idx_name}[] = {{")
        for cp, off, length in rows:
            c.append(f"    {{ 0x{cp:04X}u, {off}u, {length}u }},")
        if not rows:
            c.append("    { 0, 0, 0 },")
        c.append("};")
        c.append(f"static const size_t {idx_name}_count = {len(rows)};")
        c.append("")
        c.append(f"static const uint32_t {data_name}[] = {{")
        for i in range(0, max(len(data), 1), 8):
            chunk = data[i : i + 8] if data else [0]
            if not data and i > 0:
                break
            if not data:
                c.append("    0,")
                break
            c.append("    " + ", ".join(f"0x{x:04X}u" for x in chunk) + ",")
        c.append("};")
        c.append("")

    c: list[str] = [
        "/* Auto-generated by Tools/ucd/generate_case.py — DO NOT EDIT. */",
        f"/* Unicode UCD {UCD_VERSION} */",
        '#include "RoboUnicode/RoboCaseData.h"',
        "",
    ]
    emit_simple("robo_case_su", su_idx, c)
    emit_simple("robo_case_sl", sl_idx, c)
    emit_simple("robo_case_st", st_idx, c)
    emit_simple("robo_case_sf", fs_idx, c)
    emit_full("robo_case_fu_idx", "robo_case_fu_data", fu_rows, fu_data, c)
    emit_full("robo_case_fl_idx", "robo_case_fl_data", fl_rows, fl_data, c)
    emit_full("robo_case_ft_idx", "robo_case_ft_data", ft_rows, ft_data, c)
    emit_full("robo_case_ff_idx", "robo_case_ff_data", ff_rows, ff_data, c)

    c.append("static const robo_case_bool_range_t robo_case_cased_ranges[] = {")
    for a, b in cased_ranges:
        c.append(f"    {{ 0x{a:04X}u, 0x{b:04X}u }},")
    if not cased_ranges:
        c.append("    { 0, 0 },")
    c.append("};")
    c.append(f"static const size_t robo_case_cased_ranges_count = {len(cased_ranges)};")
    c.append("")
    c.append("static const robo_case_bool_range_t robo_case_ign_ranges[] = {")
    for a, b in ign_ranges:
        c.append(f"    {{ 0x{a:04X}u, 0x{b:04X}u }},")
    if not ign_ranges:
        c.append("    { 0, 0 },")
    c.append("};")
    c.append(f"static const size_t robo_case_ign_ranges_count = {len(ign_ranges)};")
    c.append("")

    sigma_map = final_sigma_lower or [0x03C2]
    c.append(
        f"static const uint32_t robo_case_final_sigma_map[] = {{ {', '.join(f'0x{x:04X}u' for x in sigma_map)} }};"
    )
    c.append(
        f"static const size_t robo_case_final_sigma_map_len = {len(sigma_map)};"
    )
    c.append(
        r"""
static uint32_t robo_case_bsearch_simple(const robo_case_simple_entry_t *t,
                                        size_t n, uint32_t cp)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (cp < t[mid].cp) {
            hi = mid;
        } else if (cp > t[mid].cp) {
            lo = mid + 1u;
        } else {
            return t[mid].mapped;
        }
    }
    return cp;
}

static size_t robo_case_bsearch_full(const robo_case_full_index_t *idx, size_t n,
                                    const uint32_t *data, uint32_t cp,
                                    uint32_t *out, size_t out_cap)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (cp < idx[mid].cp) {
            hi = mid;
        } else if (cp > idx[mid].cp) {
            lo = mid + 1u;
        } else {
            size_t len = idx[mid].length;
            size_t i;
            if (out_cap < len) {
                return 0;
            }
            for (i = 0; i < len; i++) {
                out[i] = data[idx[mid].offset + i];
            }
            return len;
        }
    }
    if (out_cap < 1u) {
        return 0;
    }
    out[0] = cp;
    return 1;
}

static int robo_case_bsearch_bool(const robo_case_bool_range_t *r, size_t n,
                                 uint32_t cp)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (cp < r[mid].start) {
            hi = mid;
        } else if (cp > r[mid].end) {
            lo = mid + 1u;
        } else {
            return 1;
        }
    }
    return 0;
}

uint32_t robo_case_simple_upper(uint32_t cp)
{
    return robo_case_bsearch_simple(robo_case_su, robo_case_su_count, cp);
}
uint32_t robo_case_simple_lower(uint32_t cp)
{
    return robo_case_bsearch_simple(robo_case_sl, robo_case_sl_count, cp);
}
uint32_t robo_case_simple_title(uint32_t cp)
{
    return robo_case_bsearch_simple(robo_case_st, robo_case_st_count, cp);
}
uint32_t robo_case_simple_fold(uint32_t cp)
{
    return robo_case_bsearch_simple(robo_case_sf, robo_case_sf_count, cp);
}

size_t robo_case_full_upper(uint32_t cp, uint32_t *out, size_t out_cap)
{
    return robo_case_bsearch_full(robo_case_fu_idx, robo_case_fu_idx_count,
                                  robo_case_fu_data, cp, out, out_cap);
}
size_t robo_case_full_lower(uint32_t cp, uint32_t *out, size_t out_cap)
{
    return robo_case_bsearch_full(robo_case_fl_idx, robo_case_fl_idx_count,
                                  robo_case_fl_data, cp, out, out_cap);
}
size_t robo_case_full_title(uint32_t cp, uint32_t *out, size_t out_cap)
{
    return robo_case_bsearch_full(robo_case_ft_idx, robo_case_ft_idx_count,
                                  robo_case_ft_data, cp, out, out_cap);
}
size_t robo_case_full_fold(uint32_t cp, uint32_t *out, size_t out_cap)
{
    return robo_case_bsearch_full(robo_case_ff_idx, robo_case_ff_idx_count,
                                  robo_case_ff_data, cp, out, out_cap);
}

size_t robo_case_final_sigma_lower(uint32_t *out, size_t out_cap)
{
    size_t i;
    if (out_cap < robo_case_final_sigma_map_len) {
        return 0;
    }
    for (i = 0; i < robo_case_final_sigma_map_len; i++) {
        out[i] = robo_case_final_sigma_map[i];
    }
    return robo_case_final_sigma_map_len;
}

int robo_case_is_cased(uint32_t cp)
{
    return robo_case_bsearch_bool(robo_case_cased_ranges, robo_case_cased_ranges_count, cp);
}
int robo_case_is_case_ignorable(uint32_t cp)
{
    return robo_case_bsearch_bool(robo_case_ign_ranges, robo_case_ign_ranges_count, cp);
}
"""
    )
    OUT_C.write_text("\n".join(c) + "\n", encoding="utf-8")
    print(
        f"Wrote {OUT_H} / {OUT_C}: "
        f"su={len(su_idx)} sl={len(sl_idx)} fu={len(fu_rows)} fl={len(fl_rows)} "
        f"ff={len(ff_rows)} fold_s={len(fs_idx)}"
    )

    # Conformance fixtures: CaseFolding C+F pairs + SpecialCasing unconditional samples
    def cps_utf8(cps: list[int]) -> bytes:
        return "".join(chr(c) for c in cps).encode("utf-8")

    def c_bytes(b: bytes) -> str:
        return '"' + "".join(f"\\x{x:02x}" for x in b) + '"'

    cases = []
    # Full fold: every C and F entry
    for cp, mapping in sorted(fold_full.items()):
        cases.append(("fold", [cp], mapping))
    # Unconditional special upper/lower samples (all of them — small)
    for line in load_lines(sc):
        parts = [x.strip() for x in line.split(";")]
        if len(parts) < 4:
            continue
        cond = parts[4].strip() if len(parts) > 4 else ""
        if cond:
            continue
        cp = parse_cp(parts[0])
        lower = parse_mapping(parts[1])
        title = parse_mapping(parts[2])
        upper = parse_mapping(parts[3])
        cases.append(("upper", [cp], upper))
        cases.append(("lower", [cp], lower))
        cases.append(("title", [cp], title))

    # Simple identity checks for a few ASCII
    for cp in range(ord("A"), ord("Z") + 1):
        cases.append(("simple_lower", [cp], [cp + 32]))
        cases.append(("simple_upper", [cp + 32], [cp]))

    lines_out = [
        "/* Auto-generated case conformance fixtures — DO NOT EDIT. */",
        f"/* Unicode {UCD_VERSION}, {len(cases)} cases */",
        "typedef enum robo_case_conf_kind {",
        "    ROBO_CASE_CONF_UPPER = 0,",
        "    ROBO_CASE_CONF_LOWER = 1,",
        "    ROBO_CASE_CONF_TITLE = 2,",
        "    ROBO_CASE_CONF_FOLD = 3,",
        "    ROBO_CASE_CONF_SIMPLE_UPPER = 4,",
        "    ROBO_CASE_CONF_SIMPLE_LOWER = 5",
        "} robo_case_conf_kind_t;",
        "",
        "typedef struct robo_case_conf_case {",
        "    robo_case_conf_kind_t kind;",
        "    const char *src;",
        "    const char *want;",
        "    unsigned src_len;",
        "    unsigned want_len;",
        "} robo_case_conf_case_t;",
        "",
        "static const robo_case_conf_case_t robo_case_conf_cases[] = {",
    ]
    kind_map = {
        "upper": "ROBO_CASE_CONF_UPPER",
        "lower": "ROBO_CASE_CONF_LOWER",
        "title": "ROBO_CASE_CONF_TITLE",
        "fold": "ROBO_CASE_CONF_FOLD",
        "simple_upper": "ROBO_CASE_CONF_SIMPLE_UPPER",
        "simple_lower": "ROBO_CASE_CONF_SIMPLE_LOWER",
    }
    for kind, src, want in cases:
        sb = cps_utf8(src)
        wb = cps_utf8(want)
        lines_out.append(
            f"    {{ {kind_map[kind]}, {c_bytes(sb)}, {c_bytes(wb)}, {len(sb)}u, {len(wb)}u }},"
        )
    lines_out.append("};")
    lines_out.append(f"static const size_t robo_case_conf_cases_count = {len(cases)};")
    OUT_TEST.write_text("\n".join(lines_out) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_TEST} ({len(cases)} cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

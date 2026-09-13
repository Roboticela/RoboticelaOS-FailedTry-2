#!/usr/bin/env python3
"""
Generate Unicode normalization tables (decompositions + composition) for UCD 16.0.0.

Inputs:
  Tools/ucd/source/UnicodeData.txt
  Tools/ucd/source/CompositionExclusions.txt
  Tools/ucd/source/NormalizationTest.txt (optional → conformance C data)

Outputs:
  Include/RoboUnicode/RoboNormData.h
  Source/RoboUnicode/data/robo_norm_data.c
  Tests/Data/robo_norm_conformance.inc (from NormalizationTest.txt)
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(__file__).resolve().parent / "source"
OUT_H = ROOT / "Include" / "RoboUnicode" / "RoboNormData.h"
OUT_C = ROOT / "Source" / "RoboUnicode" / "data" / "robo_norm_data.c"
OUT_TEST = ROOT / "Tests" / "Data" / "robo_norm_conformance.inc"

UCD_VERSION = "16.0.0"
MAX_CP = 0x110000

# Hangul
SBASE, LBASE, VBASE, TBASE = 0xAC00, 0x1100, 0x1161, 0x11A7
LCOUNT, VCOUNT, TCOUNT = 19, 21, 28
NCOUNT = VCOUNT * TCOUNT
SCOUNT = LCOUNT * NCOUNT


def parse_cp(tok: str) -> int:
    return int(tok.strip(), 16)


def load_lines(path: Path):
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                yield line


def parse_unicode_data_decomp(path: Path):
    """Return dict cp -> (compat: bool, mapping: list[int]) one-step from UnicodeData."""
    decomp: dict[int, tuple[bool, list[int]]] = {}
    lines = list(load_lines(path))
    i = 0
    while i < len(lines):
        parts = lines[i].split(";")
        cp = parse_cp(parts[0])
        name = parts[1]
        raw = parts[5].strip()
        if name.endswith(", First>"):
            i += 2
            continue
        if raw:
            compat = False
            if raw.startswith("<"):
                compat = True
                raw = raw.split(">", 1)[1].strip()
            mapping = [parse_cp(x) for x in raw.split()]
            decomp[cp] = (compat, mapping)
        i += 1
    return decomp


def parse_exclusions(path: Path) -> set[int]:
    excl: set[int] = set()
    for line in load_lines(path):
        # single code point or range
        field = line.split(";")[0].strip()
        if ".." in field:
            a, b = field.split("..")
            for cp in range(parse_cp(a), parse_cp(b) + 1):
                excl.add(cp)
        else:
            excl.add(parse_cp(field))
    return excl


def hangul_decompose(cp: int) -> list[int] | None:
    if cp < SBASE or cp >= SBASE + SCOUNT:
        return None
    sindex = cp - SBASE
    l = LBASE + sindex // NCOUNT
    v = VBASE + (sindex % NCOUNT) // TCOUNT
    t = TBASE + sindex % TCOUNT
    if t == TBASE:
        return [l, v]
    return [l, v, t]


def full_decompose(cp: int, decomp: dict, compatibility: bool, seen=None) -> list[int]:
    """Recursive decomposition (canonical or compatibility)."""
    h = hangul_decompose(cp)
    if h is not None:
        out: list[int] = []
        for x in h:
            out.extend(full_decompose(x, decomp, compatibility))
        return out

    if cp not in decomp:
        return [cp]
    compat, mapping = decomp[cp]
    if compat and not compatibility:
        return [cp]
    out = []
    for x in mapping:
        out.extend(full_decompose(x, decomp, compatibility))
    return out


def build_composition(decomp: dict, exclusions: set[int]) -> dict[tuple[int, int], int]:
    """Primary composition pairs (c1, c2) -> composite."""
    comp: dict[tuple[int, int], int] = {}
    for cp, (compat, mapping) in decomp.items():
        if compat:
            continue
        if cp in exclusions:
            continue
        # Canonical decomposition must be length 2 after full NFD of mapping?
        # Unicode: the Decomposition_Mapping must be a pair of characters.
        if len(mapping) != 2:
            continue
        c1, c2 = mapping[0], mapping[1]
        # First must be a starter (CCC=0) — checked at generation using full decomp of c1?
        # Standard: c1 has combining class 0.
        # We need CCC — parse from UnicodeData or assume we check at runtime.
        # For table building, Unicode requires CCC(c1)==0.
        # We'll store and filter using a ccc map from UnicodeData field 3.
        comp[(c1, c2)] = cp

    # Hangul algorithmic compositions are done at runtime, not in table.
    return comp


def parse_ccc(path: Path) -> list[int]:
    ccc = [0] * MAX_CP
    lines = list(load_lines(path))
    i = 0
    while i < len(lines):
        parts = lines[i].split(";")
        cp = parse_cp(parts[0])
        name = parts[1]
        cc = int(parts[3])
        if name.endswith(", First>"):
            parts2 = lines[i + 1].split(";")
            cp2 = parse_cp(parts2[0])
            for c in range(cp, cp2 + 1):
                ccc[c] = cc
            i += 2
            continue
        ccc[cp] = cc
        i += 1
    return ccc


def cps_to_utf8(cps: list[int]) -> bytes:
    return "".join(chr(c) for c in cps).encode("utf-8")


def c_byte_string(b: bytes) -> str:
    return '"' + "".join(f"\\x{x:02x}" for x in b) + '"'


def main() -> int:
    ud = SRC / "UnicodeData.txt"
    ex = SRC / "CompositionExclusions.txt"
    if not ud.exists() or not ex.exists():
        print("Missing UnicodeData.txt or CompositionExclusions.txt", file=sys.stderr)
        return 1

    print("Parsing decompositions ...")
    decomp = parse_unicode_data_decomp(ud)
    ccc = parse_ccc(ud)
    exclusions = parse_exclusions(ex)

    # Filter composition pairs where CCC(c1)==0
    raw_comp = build_composition(decomp, exclusions)
    composition: dict[tuple[int, int], int] = {}
    for (c1, c2), cp in raw_comp.items():
        if ccc[c1] == 0:
            composition[(c1, c2)] = cp

    # Build sparse one-step tables for runtime recursive expand
    # Store all codepoints that have a mapping (canonical and/or compat)
    entries_can: list[tuple[int, list[int]]] = []
    entries_komp: list[tuple[int, list[int]]] = []  # compatibility (includes tags)
    for cp, (compat, mapping) in sorted(decomp.items()):
        if not compat:
            entries_can.append((cp, mapping))
        # For NFKD, need compatibility mappings; also characters with only canonical
        # are handled by canonical path. Compat mappings are the tagged ones.
        if compat:
            entries_komp.append((cp, mapping))

    # Flat data arrays
    def pack_entries(entries: list[tuple[int, list[int]]]):
        index_rows = []  # (cp, offset, length)
        data: list[int] = []
        for cp, mapping in entries:
            off = len(data)
            data.extend(mapping)
            index_rows.append((cp, off, len(mapping)))
        return index_rows, data

    can_idx, can_data = pack_entries(entries_can)
    komp_idx, komp_data = pack_entries(entries_komp)

    # Composition as sorted triples
    comp_list = sorted((c1, c2, r) for (c1, c2), r in composition.items())

    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_TEST.parent.mkdir(parents=True, exist_ok=True)

    # Header
    h = []
    h.append("/* Auto-generated by Tools/ucd/generate_norm.py — DO NOT EDIT. */")
    h.append(f"/* Unicode UCD version: {UCD_VERSION} */")
    h.append("#ifndef ROBO_NORM_DATA_H")
    h.append("#define ROBO_NORM_DATA_H")
    h.append("")
    h.append("#include <stddef.h>")
    h.append("#include <stdint.h>")
    h.append("")
    h.append("#ifdef __cplusplus")
    h.append('extern "C" {')
    h.append("#endif")
    h.append("")
    h.append(f'#define ROBO_NORM_UCD_VERSION "{UCD_VERSION}"')
    h.append("")
    h.append("typedef struct robo_norm_decomp_index {")
    h.append("    uint32_t cp;")
    h.append("    uint16_t offset;")
    h.append("    uint16_t length;")
    h.append("} robo_norm_decomp_index_t;")
    h.append("")
    h.append("typedef struct robo_norm_comp_pair {")
    h.append("    uint32_t a;")
    h.append("    uint32_t b;")
    h.append("    uint32_t composite;")
    h.append("} robo_norm_comp_pair_t;")
    h.append("")
    h.append("/* Canonical one-step decomposition mappings (no Hangul — algorithmic). */")
    h.append("extern const robo_norm_decomp_index_t robo_norm_canon_index[];")
    h.append("extern const size_t robo_norm_canon_index_count;")
    h.append("extern const uint32_t robo_norm_canon_data[];")
    h.append("")
    h.append("/* Compatibility one-step mappings (tagged decompositions only). */")
    h.append("extern const robo_norm_decomp_index_t robo_norm_compat_index[];")
    h.append("extern const size_t robo_norm_compat_index_count;")
    h.append("extern const uint32_t robo_norm_compat_data[];")
    h.append("")
    h.append("/* Primary composition pairs (excludes Hangul; CCC(a)==0). */")
    h.append("extern const robo_norm_comp_pair_t robo_norm_comp_pairs[];")
    h.append("extern const size_t robo_norm_comp_pairs_count;")
    h.append("")
    h.append("const uint32_t *robo_norm_lookup_canon(uint32_t cp, size_t *len);")
    h.append("const uint32_t *robo_norm_lookup_compat(uint32_t cp, size_t *len);")
    h.append("uint32_t robo_norm_compose_pair(uint32_t a, uint32_t b); /* 0 if none */")
    h.append("")
    h.append("#ifdef __cplusplus")
    h.append("}")
    h.append("#endif")
    h.append("")
    h.append("#endif")
    OUT_H.write_text("\n".join(h) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_H}")

    # C data
    c = []
    c.append("/* Auto-generated by Tools/ucd/generate_norm.py — DO NOT EDIT. */")
    c.append(f"/* Unicode UCD {UCD_VERSION} — Unicode License. */")
    c.append('#include "RoboUnicode/RoboNormData.h"')
    c.append("")

    def emit_index(name: str, rows):
        c.append(f"const robo_norm_decomp_index_t {name}[] = {{")
        for cp, off, length in rows:
            c.append(f"    {{ 0x{cp:04X}u, {off}u, {length}u }},")
        if not rows:
            c.append("    { 0, 0, 0 },")
        c.append("};")
        c.append(f"const size_t {name}_count = {len(rows)};")
        c.append("")

    def emit_data(name: str, data: list[int]):
        c.append(f"const uint32_t {name}[] = {{")
        for i in range(0, len(data), 8):
            chunk = data[i : i + 8]
            c.append("    " + ", ".join(f"0x{x:04X}u" for x in chunk) + ",")
        if not data:
            c.append("    0,")
        c.append("};")
        c.append("")

    emit_index("robo_norm_canon_index", can_idx)
    emit_data("robo_norm_canon_data", can_data)
    emit_index("robo_norm_compat_index", komp_idx)
    emit_data("robo_norm_compat_data", komp_data)

    c.append("const robo_norm_comp_pair_t robo_norm_comp_pairs[] = {")
    for a, b, r in comp_list:
        c.append(f"    {{ 0x{a:04X}u, 0x{b:04X}u, 0x{r:04X}u }},")
    if not comp_list:
        c.append("    { 0, 0, 0 },")
    c.append("};")
    c.append(f"const size_t robo_norm_comp_pairs_count = {len(comp_list)};")
    c.append("")

    c.append(
        r"""
static const robo_norm_decomp_index_t *robo_norm_bsearch_index(
    const robo_norm_decomp_index_t *idx, size_t count, uint32_t cp)
{
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (cp < idx[mid].cp) {
            hi = mid;
        } else if (cp > idx[mid].cp) {
            lo = mid + 1u;
        } else {
            return &idx[mid];
        }
    }
    return NULL;
}

const uint32_t *robo_norm_lookup_canon(uint32_t cp, size_t *len)
{
    const robo_norm_decomp_index_t *e =
        robo_norm_bsearch_index(robo_norm_canon_index, robo_norm_canon_index_count, cp);
    if (!e) {
        if (len) {
            *len = 0;
        }
        return NULL;
    }
    if (len) {
        *len = e->length;
    }
    return robo_norm_canon_data + e->offset;
}

const uint32_t *robo_norm_lookup_compat(uint32_t cp, size_t *len)
{
    const robo_norm_decomp_index_t *e =
        robo_norm_bsearch_index(robo_norm_compat_index, robo_norm_compat_index_count, cp);
    if (!e) {
        if (len) {
            *len = 0;
        }
        return NULL;
    }
    if (len) {
        *len = e->length;
    }
    return robo_norm_compat_data + e->offset;
}

uint32_t robo_norm_compose_pair(uint32_t a, uint32_t b)
{
    size_t lo = 0, hi = robo_norm_comp_pairs_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const robo_norm_comp_pair_t *p = &robo_norm_comp_pairs[mid];
        if (a < p->a || (a == p->a && b < p->b)) {
            hi = mid;
        } else if (a > p->a || (a == p->a && b > p->b)) {
            lo = mid + 1u;
        } else {
            return p->composite;
        }
    }
    return 0;
}
"""
    )

    OUT_C.write_text("\n".join(c) + "\n", encoding="utf-8")
    print(
        f"Wrote {OUT_C}: canon={len(can_idx)} compat={len(komp_idx)} "
        f"compose={len(comp_list)}"
    )

    # Conformance test data
    ntest = SRC / "NormalizationTest.txt"
    if ntest.exists():
        print("Generating conformance fixtures ...")
        cases = []
        part = -1
        for raw in ntest.read_text(encoding="utf-8").splitlines():
            if raw.startswith("@Part"):
                part = int(raw[5:].split()[0])
                continue
            line = raw.split("#", 1)[0].strip()
            if not line or line.startswith("@"):
                continue
            cols = [c.strip() for c in line.split(";")]
            if len(cols) < 5:
                continue
            try:
                src = [parse_cp(x) for x in cols[0].split()]
                nfc = [parse_cp(x) for x in cols[1].split()]
                nfd = [parse_cp(x) for x in cols[2].split()]
                nfkc = [parse_cp(x) for x in cols[3].split()]
                nfkd = [parse_cp(x) for x in cols[4].split()]
            except ValueError:
                continue
            cases.append((part, src, nfc, nfd, nfkc, nfkd))

        lines = [
            "/* Auto-generated from Unicode NormalizationTest.txt — DO NOT EDIT. */",
            f"/* Unicode {UCD_VERSION}, {len(cases)} cases */",
            "typedef struct robo_norm_conf_case {",
            "    int part;",
            "    const char *src;",
            "    const char *nfc;",
            "    const char *nfd;",
            "    const char *nfkc;",
            "    const char *nfkd;",
            "    unsigned src_len;",
            "    unsigned nfc_len;",
            "    unsigned nfd_len;",
            "    unsigned nfkc_len;",
            "    unsigned nfkd_len;",
            "} robo_norm_conf_case_t;",
            "",
            "static const robo_norm_conf_case_t robo_norm_conf_cases[] = {",
        ]
        for part, src, nfc, nfd, nfkc, nfkd in cases:
            sb, nfcb, nfdb, nfkcb, nfkdb = (
                cps_to_utf8(src),
                cps_to_utf8(nfc),
                cps_to_utf8(nfd),
                cps_to_utf8(nfkc),
                cps_to_utf8(nfkd),
            )
            lines.append(
                "    { %d, %s, %s, %s, %s, %s, %u, %u, %u, %u, %u },"
                % (
                    part,
                    c_byte_string(sb),
                    c_byte_string(nfcb),
                    c_byte_string(nfdb),
                    c_byte_string(nfkcb),
                    c_byte_string(nfkdb),
                    len(sb),
                    len(nfcb),
                    len(nfdb),
                    len(nfkcb),
                    len(nfkdb),
                )
            )
        lines.append("};")
        lines.append(
            f"static const size_t robo_norm_conf_cases_count = {len(cases)};"
        )
        OUT_TEST.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"Wrote {OUT_TEST} ({len(cases)} cases)")
    else:
        print("NormalizationTest.txt not found; skipping conformance fixtures")

    return 0


if __name__ == "__main__":
    sys.exit(main())

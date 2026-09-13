#!/usr/bin/env python3
"""
Generate Unicode Collation Algorithm (DUCET) tables from UCA allkeys.txt.

Input:  Tools/i18n/source/uca-16.0.0/allkeys.txt  (UCA 16.0.0)
Outputs:
  Include/RoboI18N/RoboUcaData.h
  Source/RoboI18N/data/robo_uca_data.c

Pins: Unicode/UCA 16.0.0 — same train as RoboUnicode 16.0.0 / CLDR 46.
Generator version: Tools/i18n/robo_i18n_version.py GENERATOR_VERSION.

Algorithm reference: UTS #10 Unicode Collation Algorithm.
"""

from __future__ import annotations

import hashlib
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from robo_i18n_version import GENERATOR_VERSION, ROBO_I18N_VERSION, CLDR_UNICODE_VERSION

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(__file__).resolve().parent / "source" / "uca-16.0.0" / "allkeys.txt"
OUT_H = ROOT / "Include" / "RoboI18N" / "RoboUcaData.h"
OUT_C = ROOT / "Source" / "RoboI18N" / "data" / "robo_uca_data.c"
GOLDEN = Path(__file__).resolve().parent / "uca_golden_hashes.json"

UCA_VERSION = "16.0.0"
UCA_URL = "https://www.unicode.org/Public/UCA/16.0.0/allkeys.txt"

CE_RE = re.compile(
    r"\[([.*])([0-9A-Fa-f]{4})\.([0-9A-Fa-f]{4})\.([0-9A-Fa-f]{4})\]"
)


def pack_ce(variable: bool, primary: int, secondary: int, tertiary: int) -> int:
    """Pack CE into uint64: V:1 | P:16 | S:16 | T:16 in low 49 bits."""
    v = 1 if variable else 0
    return (v << 48) | ((primary & 0xFFFF) << 32) | ((secondary & 0xFFFF) << 16) | (
        tertiary & 0xFFFF
    )


def parse_allkeys(path: Path):
    version = None
    implicit = []  # (start, end, base)
    singles: dict[int, list[int]] = {}
    contractions: dict[tuple[int, ...], list[int]] = {}

    with path.open("r", encoding="utf-8") as f:
        for line in f:
            raw = line.strip()
            if not raw or raw.startswith("#"):
                continue
            if raw.startswith("@version"):
                version = raw.split()[1]
                continue
            if raw.startswith("@implicitweights"):
                # @implicitweights 17000..18AFF; FB00
                body = raw[len("@implicitweights") :].strip()
                rng, base = body.split(";")
                a, b = rng.strip().split("..")
                base_tok = base.strip().split()[0]
                implicit.append((int(a, 16), int(b, 16), int(base_tok, 16)))
                continue
            if ";" not in raw:
                continue
            left, right = raw.split(";", 1)
            cps = tuple(int(x, 16) for x in left.strip().split())
            ces = []
            for m in CE_RE.finditer(right):
                variable = m.group(1) == "*"
                p = int(m.group(2), 16)
                s = int(m.group(3), 16)
                t = int(m.group(4), 16)
                ces.append(pack_ce(variable, p, s, t))
            if not cps or not ces:
                continue
            if len(cps) == 1:
                singles[cps[0]] = ces
            else:
                contractions[cps] = ces

    return version, implicit, singles, contractions


def emit_c_array(name: str, values: list[int], width: int = 8) -> str:
    lines = [f"const uint64_t {name}[{len(values)}] = {{"]
    row = []
    for i, v in enumerate(values):
        row.append(f"0x{v:016X}ULL")
        if len(row) == width:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    write_golden = "--write-golden" in sys.argv
    if not SRC.is_file():
        print(f"missing {SRC}; run Fetch-Uca.ps1", file=sys.stderr)
        return 1

    version, implicit, singles, contractions = parse_allkeys(SRC)
    if version != UCA_VERSION:
        print(f"warning: allkeys @version {version} != pin {UCA_VERSION}", file=sys.stderr)

    # CE pool + single map
    ce_pool: list[int] = []
    map_cps: list[int] = []
    map_off: list[int] = []
    map_cnt: list[int] = []

    for cp in sorted(singles.keys()):
        ces = singles[cp]
        off = len(ce_pool)
        ce_pool.extend(ces)
        map_cps.append(cp)
        map_off.append(off)
        map_cnt.append(len(ces))

    # Contractions: sort by first CP then length desc then sequence
    contr_items = sorted(
        contractions.items(), key=lambda kv: (kv[0][0], -len(kv[0]), kv[0])
    )
    contr_first: list[int] = []
    contr_len: list[int] = []
    contr_seq_off: list[int] = []
    contr_ce_off: list[int] = []
    contr_ce_cnt: list[int] = []
    contr_seq_pool: list[int] = []

    for seq, ces in contr_items:
        soff = len(contr_seq_pool)
        contr_seq_pool.extend(seq)
        coff = len(ce_pool)
        ce_pool.extend(ces)
        contr_first.append(seq[0])
        contr_len.append(len(seq))
        contr_seq_off.append(soff)
        contr_ce_off.append(coff)
        contr_ce_cnt.append(len(ces))

    src_bytes = SRC.read_bytes()
    src_sha = hashlib.sha256(src_bytes).hexdigest()

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)

    h = f"""/* Auto-generated by generate_uca.py — DO NOT EDIT.
 * UCA / DUCET {UCA_VERSION}
 * Source: {UCA_URL}
 * SHA-256: {src_sha}
 * Generator: {GENERATOR_VERSION}  RoboI18N API: {ROBO_I18N_VERSION}
 * Unicode association: {CLDR_UNICODE_VERSION}
 */
#ifndef ROBO_UCA_DATA_H
#define ROBO_UCA_DATA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define ROBO_UCA_VERSION_STRING "{UCA_VERSION}"
#define ROBO_UCA_SOURCE_SHA256 "{src_sha}"
#define ROBO_UCA_CE_VARIABLE_SHIFT 48

#define ROBO_UCA_CE_PRIMARY(ce)   ((uint16_t)(((ce) >> 32) & 0xFFFFu))
#define ROBO_UCA_CE_SECONDARY(ce) ((uint16_t)(((ce) >> 16) & 0xFFFFu))
#define ROBO_UCA_CE_TERTIARY(ce)  ((uint16_t)((ce) & 0xFFFFu))
#define ROBO_UCA_CE_IS_VARIABLE(ce) ((((ce) >> ROBO_UCA_CE_VARIABLE_SHIFT) & 1u) != 0)

typedef struct robo_uca_implicit_range {{
    uint32_t start;
    uint32_t end;
    uint16_t base; /* high primary nibble base from @implicitweights */
}} robo_uca_implicit_range_t;

typedef struct robo_uca_map_row {{
    uint32_t cp;
    uint32_t ce_offset;
    uint16_t ce_count;
}} robo_uca_map_row_t;

typedef struct robo_uca_contraction {{
    uint32_t first_cp;
    uint16_t seq_len;
    uint32_t seq_offset; /* into robo_uca_contraction_seq */
    uint32_t ce_offset;
    uint16_t ce_count;
}} robo_uca_contraction_t;

extern const uint64_t robo_uca_ce_pool[];
extern const uint32_t robo_uca_ce_pool_count;

extern const robo_uca_map_row_t robo_uca_map[];
extern const uint32_t robo_uca_map_count;

extern const uint32_t robo_uca_contraction_seq[];
extern const uint32_t robo_uca_contraction_seq_count;

extern const robo_uca_contraction_t robo_uca_contractions[];
extern const uint32_t robo_uca_contraction_count;

extern const robo_uca_implicit_range_t robo_uca_implicit_ranges[];
extern const uint32_t robo_uca_implicit_range_count;

const char *robo_uca_version_string(void);
const char *robo_uca_source_sha256(void);

#ifdef __cplusplus
}}
#endif

#endif /* ROBO_UCA_DATA_H */
"""

    impl_rows = ",\n".join(
        f"    {{{a:#x}, {b:#x}, {base:#x}}}" for a, b, base in implicit
    )

    map_rows = ",\n".join(
        f"    {{{cp:#x}, {off}u, {cnt}u}}"
        for cp, off, cnt in zip(map_cps, map_off, map_cnt)
    )

    contr_rows = ",\n".join(
        f"    {{{first:#x}, {ln}u, {soff}u, {coff}u, {ccnt}u}}"
        for first, ln, soff, coff, ccnt in zip(
            contr_first, contr_len, contr_seq_off, contr_ce_off, contr_ce_cnt
        )
    )

    seq_lines = []
    row = []
    for i, v in enumerate(contr_seq_pool):
        row.append(f"{v:#x}")
        if len(row) == 8:
            seq_lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        seq_lines.append("    " + ", ".join(row) + ",")

    c = f"""/* Auto-generated by generate_uca.py — DO NOT EDIT.
 * UCA / DUCET {UCA_VERSION}  SHA-256 {src_sha}
 */
#include "RoboI18N/RoboUcaData.h"

const char *robo_uca_version_string(void)
{{
    return ROBO_UCA_VERSION_STRING;
}}

const char *robo_uca_source_sha256(void)
{{
    return ROBO_UCA_SOURCE_SHA256;
}}

{emit_c_array("robo_uca_ce_pool", ce_pool)}
const uint32_t robo_uca_ce_pool_count = {len(ce_pool)}u;

const robo_uca_map_row_t robo_uca_map[{len(map_cps)}] = {{
{map_rows}
}};
const uint32_t robo_uca_map_count = {len(map_cps)}u;

const uint32_t robo_uca_contraction_seq[{max(len(contr_seq_pool), 1)}] = {{
{chr(10).join(seq_lines) if seq_lines else "    0"}
}};
const uint32_t robo_uca_contraction_seq_count = {len(contr_seq_pool)}u;

const robo_uca_contraction_t robo_uca_contractions[{max(len(contr_items), 1)}] = {{
{contr_rows if contr_rows else "    {0, 0, 0, 0, 0}"}
}};
const uint32_t robo_uca_contraction_count = {len(contr_items)}u;

const robo_uca_implicit_range_t robo_uca_implicit_ranges[{max(len(implicit), 1)}] = {{
{impl_rows if impl_rows else "    {0, 0, 0}"}
}};
const uint32_t robo_uca_implicit_range_count = {len(implicit)}u;
"""

    OUT_H.write_text(h, encoding="utf-8", newline="\n")
    OUT_C.write_text(c, encoding="utf-8", newline="\n")

    import json

    hashes = {
        "generator_version": GENERATOR_VERSION,
        "uca_version": UCA_VERSION,
        "source_sha256": src_sha,
        "outputs": {
            "RoboUcaData.h": hashlib.sha256(OUT_H.read_bytes()).hexdigest(),
            "robo_uca_data.c": hashlib.sha256(OUT_C.read_bytes()).hexdigest(),
        },
        "stats": {
            "singles": len(singles),
            "contractions": len(contractions),
            "ce_pool": len(ce_pool),
            "implicit_ranges": len(implicit),
        },
    }
    if write_golden:
        GOLDEN.write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
        print(f"wrote golden {GOLDEN}")
    elif GOLDEN.is_file():
        golden = json.loads(GOLDEN.read_text(encoding="utf-8"))
        if golden.get("outputs") != hashes["outputs"]:
            print("GOLDEN MISMATCH — re-run with --write-golden after review", file=sys.stderr)
            return 2

    print(
        f"UCA {UCA_VERSION}: singles={len(singles)} contractions={len(contractions)} "
        f"ce_pool={len(ce_pool)} → {OUT_C}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

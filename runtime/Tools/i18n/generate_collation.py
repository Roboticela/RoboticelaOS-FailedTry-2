#!/usr/bin/env python3
"""
Generate CLDR collation tailoring metadata + compiled CE remaps (Goal 4.9).

Inputs: Tools/i18n/source/cldr-46.0.0/collation/*.xml
        Tools/i18n/source/uca-16.0.0/allkeys.txt (for primary weights)

Outputs:
  Include/RoboI18N/RoboCollData.h
  Source/RoboI18N/data/robo_coll_data.c

Architecture:
  base UCA (RoboUcaData) + optional locale/type remap table.
  Remaps are build-time compiled from a subset of LDML collation rules
  (single-character resets and <</<<</</= relations). Complex imports
  and large CJK tailorings remain metadata-only (use root UCA).

CLDR 46 root/standard is empty (aligned with DUCET for regular letters).
"""

from __future__ import annotations

import hashlib
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from robo_i18n_version import GENERATOR_VERSION, ROBO_I18N_VERSION, CLDR_VERSION

ROOT = Path(__file__).resolve().parents[2]
COLL_DIR = Path(__file__).resolve().parent / "source" / "cldr-46.0.0" / "collation"
ALLKEYS = Path(__file__).resolve().parent / "source" / "uca-16.0.0" / "allkeys.txt"
OUT_H = ROOT / "Include" / "RoboI18N" / "RoboCollData.h"
OUT_C = ROOT / "Source" / "RoboI18N" / "data" / "robo_coll_data.c"
GOLDEN = Path(__file__).resolve().parent / "collation_golden_hashes.json"

# Locales we ship metadata for; remap compilation for small standard/phonebook rules.
LOCALES = [
    "root",
    "en",
    "de",
    "sv",
    "es",
    "tr",
    "ar",
    "fa",
    "ur",
    "he",
    "hi",
    "bn",
    "zh",
    "ja",
    "ko",
]

# Only compile remaps for these (locale, type) pairs — keep tables small & correct.
COMPILE_TARGETS = {
    ("de", "phonebook"),
    ("es", "standard"),
    ("es", "traditional"),
    ("sv", "standard"),
    ("tr", "standard"),
}


def load_primary_map(path: Path) -> dict[int, int]:
    """cp -> first primary weight from DUCET (NFD expansions use first CE)."""
    prim: dict[int, int] = {}
    ce_re = re.compile(r"\[([.*])([0-9A-Fa-f]{4})\.")
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            if ";" not in line or line.startswith("#") or line.startswith("@"):
                continue
            left, right = line.split(";", 1)
            cps = left.strip().split()
            if len(cps) != 1:
                continue
            m = ce_re.search(right)
            if not m:
                continue
            prim[int(cps[0], 16)] = int(m.group(2), 16)
    return prim


def extract_rules(xml_path: Path) -> tuple[str, list[tuple[str, str]]]:
    """Return (default_type, [(type, rules_text), ...])."""
    tree = ET.parse(xml_path)
    root = tree.getroot()
    # Strip namespaces if any
    default = "standard"
    types: list[tuple[str, str]] = []
    for el in root.iter():
        tag = el.tag.split("}")[-1]
        if tag == "defaultCollation" and el.text:
            default = el.text.strip()
        if tag == "collation":
            ctype = el.attrib.get("type", "standard")
            # skip draft-only duplicates preferring non-draft
            rules = ""
            for child in el:
                ctag = child.tag.split("}")[-1]
                if ctag == "cr" and child.text:
                    rules = child.text
            types.append((ctype, rules))
    # Dedupe by type keeping last non-empty preferential without draft alt
    by_type: dict[str, str] = {}
    for ctype, rules in types:
        if ctype not in by_type or rules.strip():
            by_type[ctype] = rules
    return default, list(by_type.items())


def nfd_approx(s: str) -> list[int]:
    """Normalize common Latin composites to base+mark for rule matching."""
    # Use Python unicodedata NFD for rule strings.
    import unicodedata

    return [ord(c) for c in unicodedata.normalize("NFD", s)]


def tokenize_rules(text: str) -> list[str]:
    text = re.sub(r"#.*", "", text)
    # Drop [import ...] and other bracket settings we don't apply
    text = re.sub(r"\[[^\]]*\]", " ", text)
    # Insert spaces around operators
    text = re.sub(r"(<<<|<<|<|=|&)", r" \1 ", text)
    return [t for t in text.split() if t]


def compile_simple_remap(
    rules: str, prim: dict[int, int]
) -> list[tuple[int, int, int, int]]:
    """
    Compile a subset of LDML rules into (cp, primary, secondary, tertiary) remaps.
    Supports: &X < Y << Z <<< W and multi-char Y for contractions as primary-after.
    Returns list of remaps for single code points only (contraction remaps skipped).
    """
    tokens = tokenize_rules(rules)
    remaps: dict[int, tuple[int, int, int]] = {}
    i = 0
    cur_p = 0
    cur_s = 0x20
    cur_t = 2
    step_p = 1

    def set_reset(seq: str) -> None:
        nonlocal cur_p, cur_s, cur_t
        cps = nfd_approx(seq)
        if not cps:
            return
        # Use primary of first character
        cur_p = prim.get(cps[0], 0x2000)
        cur_s = 0x20
        cur_t = 2

    while i < len(tokens):
        tok = tokens[i]
        if tok == "&":
            if i + 1 >= len(tokens):
                break
            set_reset(tokens[i + 1])
            i += 2
            continue
        if tok in ("<", "<<", "<<<", "="):
            if i + 1 >= len(tokens):
                break
            target = tokens[i + 1]
            # skip / extensions in rules like þ/h
            if "/" in target:
                target = target.split("/", 1)[0]
            cps = nfd_approx(target)
            if tok == "<":
                cur_p += step_p
                cur_s = 0x20
                cur_t = 2
            elif tok == "<<":
                cur_s += 1
                cur_t = 2
            elif tok == "<<<":
                cur_t += 1
            # '=' keeps weights
            if len(cps) == 1:
                remaps[cps[0]] = (cur_p, cur_s, cur_t)
            elif len(cps) >= 1:
                # contraction: remap first char primary for simple cases (ñ = n+̃ already NFD)
                # For ñ NFD is n + combining tilde — Spanish wants ñ after n as primary unit.
                # When target NFC length is 1 but NFD > 1, use NFC code point if available.
                import unicodedata

                nfc = unicodedata.normalize("NFC", target)
                if len(nfc) == 1:
                    remaps[ord(nfc)] = (cur_p, cur_s, cur_t)
            i += 2
            continue
        i += 1

    return [(cp, p, s, t) for cp, (p, s, t) in sorted(remaps.items())]


def main() -> int:
    write_golden = "--write-golden" in sys.argv
    if not COLL_DIR.is_dir():
        print(f"missing {COLL_DIR}", file=sys.stderr)
        return 1
    if not ALLKEYS.is_file():
        print(f"missing {ALLKEYS}", file=sys.stderr)
        return 1

    prim = load_primary_map(ALLKEYS)

    locales_meta = []
    type_rows = []  # locale_id, type_id, rules_offset, rules_len, remap_offset, remap_count
    rules_blob = bytearray()
    remap_cps: list[int] = []
    remap_p: list[int] = []
    remap_s: list[int] = []
    remap_t: list[int] = []

    type_id_of: dict[str, int] = {"standard": 0, "phonebook": 1, "traditional": 2, "search": 3}
    next_tid = 4

    for loc in LOCALES:
        path = COLL_DIR / f"{loc}.xml"
        if not path.is_file():
            print(f"warn skip missing {path}", file=sys.stderr)
            continue
        default, types = extract_rules(path)
        if default not in type_id_of:
            type_id_of[default] = next_tid
            next_tid += 1
        default_id = type_id_of[default]
        type_begin = len(type_rows)
        for ctype, rules in types:
            if ctype not in type_id_of:
                type_id_of[ctype] = next_tid
                next_tid += 1
            tid = type_id_of[ctype]
            rules_clean = rules.strip()
            # Keep rule text only when we compile remaps (or small locales).
            # Large CJK/Arabic rule CDATA is metadata-only via type name.
            store_rules = (loc, ctype) in COMPILE_TARGETS
            if store_rules and rules_clean:
                roff = len(rules_blob)
                rb = rules_clean.encode("utf-8")
                rules_blob.extend(rb)
                rlen = len(rb)
            else:
                roff = 0
                rlen = 0

            remap_off = 0
            remap_cnt = 0
            if store_rules and rules_clean:
                rem = compile_simple_remap(rules_clean, prim)
                if rem:
                    remap_off = len(remap_cps)
                    for cp, p, s, t in rem:
                        remap_cps.append(cp)
                        remap_p.append(p)
                        remap_s.append(s)
                        remap_t.append(t)
                    remap_cnt = len(rem)

            type_rows.append((loc, tid, ctype, roff, rlen, remap_off, remap_cnt))

        locales_meta.append((loc, default_id, type_begin, len(types)))

    # String pool for locale ids
    loc_str = bytearray()
    loc_entries = []
    for loc, default_id, tbegin, tcount in locales_meta:
        off = len(loc_str)
        loc_str.extend(loc.encode("ascii") + b"\0")
        loc_entries.append((off, default_id, tbegin, tcount))

    type_name_blob = bytearray()
    type_name_index = []
    # Rebuild type rows with type name offsets
    final_types = []
    for loc, tid, ctype, roff, rlen, remap_off, remap_cnt in type_rows:
        noff = len(type_name_blob)
        type_name_blob.extend(ctype.encode("ascii") + b"\0")
        final_types.append((tid, noff, roff, rlen, remap_off, remap_cnt))

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)

    h = f"""/* Auto-generated by generate_collation.py — DO NOT EDIT.
 * CLDR {CLDR_VERSION} collation metadata + selected remaps.
 * Generator: {GENERATOR_VERSION}  API: {ROBO_I18N_VERSION}
 */
#ifndef ROBO_COLL_DATA_H
#define ROBO_COLL_DATA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define ROBO_COLL_CLDR_VERSION "{CLDR_VERSION}"

typedef struct robo_coll_locale {{
    uint32_t id_offset; /* into robo_coll_string_pool */
    uint16_t default_type_id;
    uint16_t type_begin;
    uint16_t type_count;
}} robo_coll_locale_t;

typedef struct robo_coll_type {{
    uint16_t type_id;
    uint32_t name_offset;
    uint32_t rules_offset;
    uint32_t rules_len;
    uint32_t remap_offset;
    uint16_t remap_count; /* 0 ⇒ use root UCA only for this type */
}} robo_coll_type_t;

typedef struct robo_coll_remap {{
    uint32_t cp;
    uint16_t primary;
    uint16_t secondary;
    uint16_t tertiary;
}} robo_coll_remap_t;

extern const char robo_coll_string_pool[];
extern const uint32_t robo_coll_string_pool_size;

extern const char robo_coll_rules_pool[];
extern const uint32_t robo_coll_rules_pool_size;

extern const robo_coll_locale_t robo_coll_locales[];
extern const uint32_t robo_coll_locale_count;

extern const robo_coll_type_t robo_coll_types[];
extern const uint32_t robo_coll_type_count;

extern const robo_coll_remap_t robo_coll_remaps[];
extern const uint32_t robo_coll_remap_count;

const char *robo_coll_cldr_version_string(void);

#ifdef __cplusplus
}}
#endif

#endif /* ROBO_COLL_DATA_H */
"""

    def c_string_literal(data: bytes) -> str:
        # emit as escaped string chunks
        parts = []
        for i in range(0, len(data), 64):
            chunk = data[i : i + 64]
            esc = "".join(f"\\x{b:02x}" for b in chunk)
            parts.append(f'    "{esc}"')
        if not parts:
            parts = ['    ""']
        return "\n".join(parts)

    loc_rows = ",\n".join(
        f"    {{{off}u, {did}u, {tb}u, {tc}u}}" for off, did, tb, tc in loc_entries
    )
    type_c_rows = ",\n".join(
        f"    {{{tid}u, {noff}u, {roff}u, {rlen}u, {moff}u, {mcnt}u}}"
        for tid, noff, roff, rlen, moff, mcnt in final_types
    )
    remap_rows = ",\n".join(
        f"    {{{cp:#x}, {p:#x}, {s:#x}, {t:#x}}}"
        for cp, p, s, t in zip(remap_cps, remap_p, remap_s, remap_t)
    )

    # Combine locale ids + type names in one pool: locales first, then type names
    # Adjust type name offsets by len(loc_str)
    type_name_base = len(loc_str)
    combined_pool = bytes(loc_str) + bytes(type_name_blob)
    final_types_adj = [
        (tid, noff + type_name_base, roff, rlen, moff, mcnt)
        for tid, noff, roff, rlen, moff, mcnt in final_types
    ]
    type_c_rows = ",\n".join(
        f"    {{{tid}u, {noff}u, {roff}u, {rlen}u, {moff}u, {mcnt}u}}"
        for tid, noff, roff, rlen, moff, mcnt in final_types_adj
    )

    c = f"""/* Auto-generated by generate_collation.py — DO NOT EDIT. */
#include "RoboI18N/RoboCollData.h"

const char *robo_coll_cldr_version_string(void)
{{
    return ROBO_COLL_CLDR_VERSION;
}}

const char robo_coll_string_pool[] = {{
{c_string_literal(combined_pool)}
}};
const uint32_t robo_coll_string_pool_size = {len(combined_pool)}u;

const char robo_coll_rules_pool[] = {{
{c_string_literal(bytes(rules_blob))}
}};
const uint32_t robo_coll_rules_pool_size = {len(rules_blob)}u;

const robo_coll_locale_t robo_coll_locales[{len(loc_entries)}] = {{
{loc_rows}
}};
const uint32_t robo_coll_locale_count = {len(loc_entries)}u;

const robo_coll_type_t robo_coll_types[{max(len(final_types_adj), 1)}] = {{
{type_c_rows if type_c_rows else "    {0, 0, 0, 0, 0, 0}"}
}};
const uint32_t robo_coll_type_count = {len(final_types_adj)}u;

const robo_coll_remap_t robo_coll_remaps[{max(len(remap_cps), 1)}] = {{
{remap_rows if remap_rows else "    {0, 0, 0, 0}"}
}};
const uint32_t robo_coll_remap_count = {len(remap_cps)}u;
"""

    OUT_H.write_text(h, encoding="utf-8", newline="\n")
    OUT_C.write_text(c, encoding="utf-8", newline="\n")

    hashes = {
        "generator_version": GENERATOR_VERSION,
        "cldr_version": CLDR_VERSION,
        "outputs": {
            "RoboCollData.h": hashlib.sha256(OUT_H.read_bytes()).hexdigest(),
            "robo_coll_data.c": hashlib.sha256(OUT_C.read_bytes()).hexdigest(),
        },
        "stats": {
            "locales": len(loc_entries),
            "types": len(final_types_adj),
            "remaps": len(remap_cps),
            "rules_bytes": len(rules_blob),
        },
    }
    if write_golden:
        GOLDEN.write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {GOLDEN}")
    print(
        f"collation CLDR {CLDR_VERSION}: locales={len(loc_entries)} "
        f"types={len(final_types_adj)} remaps={len(remap_cps)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

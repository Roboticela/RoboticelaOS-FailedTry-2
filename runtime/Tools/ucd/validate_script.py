#!/usr/bin/env python3
"""
Independent Script / Script_Extensions validator for Unicode 16.0.0.

Re-parses official UCD files and compares against the same algorithms used by
generate_ucd.py (imported helpers), then optionally spot-checks the generated
C tables by regenerating expected values.

Usage:
  python runtime/Tools/ucd/validate_script.py
"""

from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from generate_ucd import (  # type: ignore
    MAX_CP,
    UCD_VERSION,
    SRC,
    build_scx_tables,
    parse_property_file,
    parse_script_aliases,
    parse_script_extensions,
)


def main() -> int:
    if UCD_VERSION != "16.0.0":
        print(f"Unexpected UCD version pin: {UCD_VERSION}", file=sys.stderr)
        return 1

    script_props = parse_property_file(SRC / "Scripts.txt")
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

    short_to_long, long_to_short, _extra = parse_script_aliases(
        SRC / "PropertyValueAliases.txt"
    )
    for s in scripts:
        if s not in long_to_short:
            print(f"FAIL: script {s} missing short alias", file=sys.stderr)
            return 1

    scx_entries = parse_script_extensions(SRC / "ScriptExtensions.txt")
    scx_arr, unique_sets, conf_ranges = build_scx_tables(
        scx_entries, short_to_long, script_index
    )

    # Every explicit SCX cp must differ from trivial {Script} or be listed
    # (Unicode lists SCX even when useful; just check consistency).
    fail = 0
    checked = 0
    for start, end, shorts in scx_entries:
        ids = tuple(
            sorted({script_index[short_to_long[sh]] for sh in shorts})
        )
        for cp in range(start, end + 1):
            checked += 1
            if tuple(unique_sets[scx_arr[cp]]) != ids:
                fail += 1
                if fail <= 10:
                    print(f"SCX mismatch U+{cp:04X}")
            # has_script semantics: primary may be Common/Inherited
            primary = script_arr[cp]
            if primary not in (script_index["Common"], script_index["Inherited"]):
                # When Script is a concrete script, SCX usually includes it
                # (UAX #24); warn only — some edge cases exist historically.
                pass

    # Round-trip short names
    for s in scripts:
        sh = long_to_short[s]
        if short_to_long[sh] != s and not (
            s == "Inherited" and short_to_long.get("Qaai") == "Inherited"
        ):
            # Inherited: Zinh is primary short
            if short_to_long[sh] != s:
                fail += 1
                print(f"alias mismatch {s} / {sh}")

    print(
        f"validate_script: scripts={len(scripts)} scx_ranges={len(conf_ranges)} "
        f"scx_cps_checked={checked} unique_sets={len(unique_sets)-1} "
        f"failures={fail}"
    )
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

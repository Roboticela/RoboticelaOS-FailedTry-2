#!/usr/bin/env python3
"""
Generate the Bidi_Mirroring_Glyph table (UAX #9 L4) for UCD 16.0.0.

Input:
  Tools/ucd/source/BidiMirroring.txt

Output:
  Source/RoboUnicode/RoboUnicodeBidiMirror.c

This script is deliberately *not* in `manifest.json`'s generator list yet: that
pipeline's `--check-golden` currently reports missing artifacts of its own, and a
new artifact registered there before that is settled would bless drift rather than
catch it. Running this script is therefore an explicit developer step, exactly as
`generate_all.py` says of itself:

  python3 Runtime/Tools/ucd/generate_mirror.py
"""

from __future__ import annotations

import sys
from pathlib import Path

from robo_unicode_version import UCD_VERSION

HERE = Path(__file__).resolve().parent
SRC = HERE / "source" / "BidiMirroring.txt"
OUT_C = HERE.parents[1] / "Source" / "RoboUnicode" / "RoboUnicodeBidiMirror.c"


def parse_cp(tok: str) -> int:
    return int(tok.strip(), 16)


def main() -> int:
    if not SRC.exists():
        print(f"Missing {SRC}", file=sys.stderr)
        return 1

    pairs: list[tuple[int, int]] = []
    for raw in SRC.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or ";" not in line:
            continue
        left, right = line.split(";", 1)
        pairs.append((parse_cp(left), parse_cp(right)))

    # Ascending by code point, which is what makes the binary search below sound —
    # and what the file's own order would not guarantee.
    pairs.sort(key=lambda p: p[0])
    for i in range(1, len(pairs)):
        if pairs[i][0] == pairs[i - 1][0]:
            print(f"Duplicate code point {pairs[i][0]:04X}", file=sys.stderr)
            return 1

    lines = [
        "/*",
        " * Bidi_Mirroring_Glyph (UAX #9 L4) — Unicode %s." % UCD_VERSION,
        " *",
        " * Auto-generated from Tools/ucd/source/BidiMirroring.txt by",
        " * Tools/ucd/generate_mirror.py — DO NOT EDIT.",
        " *",
        " * The table is the file's own pairs, sorted ascending by code point so that the",
        " * lookup below is a binary search. It is here rather than in RoboText because",
        " * mirroring is a Unicode property (the UCD owns it) and because a text layer that",
        " * carried its own copy would be a second table to keep in step.",
        " */",
        "",
        '#include "RoboUnicode/RoboBidiMirror.h"',
        "",
        "#include <stddef.h>",
        "",
        "static const BidiMirrorPair RoboBidiMirrorPairs[] = {",
    ]
    for cp, mirror in pairs:
        lines.append("    { 0x%04Xu, 0x%04Xu }," % (cp, mirror))
    lines.append("};")
    lines.append("")
    lines.append("static const size_t RoboBidiMirrorPairCount = %du;" % len(pairs))
    lines.append(
        r"""
int BidiMirrorLookup(Uchar32 codePoint, Uchar32 *outMirror)
{
    size_t low = 0u;
    size_t high = RoboBidiMirrorPairCount;

    while (low < high) {
        size_t mid = low + (high - low) / 2u;

        if (codePoint < RoboBidiMirrorPairs[mid].codePoint) {
            high = mid;
        } else if (codePoint > RoboBidiMirrorPairs[mid].codePoint) {
            low = mid + 1u;
        } else {
            if (outMirror) {
                *outMirror = RoboBidiMirrorPairs[mid].mirror;
            }
            return 1;
        }
    }
    if (outMirror) {
        *outMirror = codePoint;
    }
    return 0;
}

size_t BidiMirrorPairCountValue(void)
{
    return RoboBidiMirrorPairCount;
}

int BidiMirrorPairAt(size_t index, Uchar32 *outCodePoint, Uchar32 *outMirror)
{
    if (index >= RoboBidiMirrorPairCount) {
        return 0;
    }
    if (outCodePoint) {
        *outCodePoint = RoboBidiMirrorPairs[index].codePoint;
    }
    if (outMirror) {
        *outMirror = RoboBidiMirrorPairs[index].mirror;
    }
    return 1;
}
"""
    )

    OUT_C.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_C} ({len(pairs)} pairs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

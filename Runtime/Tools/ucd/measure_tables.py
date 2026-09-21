#!/usr/bin/env python3
"""
Report Unicode generated-table sizes and simple microbench notes (Goal 3.14).

Does not require Chromium. Runtime microbench lives in SuiteRoboUnicodeInfra.c.
"""

from __future__ import annotations

from pathlib import Path

from robo_unicode_version import EMOJI_VERSION, UCD_VERSION
from ucd_common import RUNTIME, load_manifest


def main() -> int:
    manifest = load_manifest()
    print(f"Unicode {UCD_VERSION} / Emoji {EMOJI_VERSION}")
    print("representation notes:")
    for k, v in (manifest.get("representations") or {}).items():
        print(f"  - {k}: {v}".encode("ascii", "replace").decode("ascii"))
    print()
    total = 0
    data_total = 0
    for rel in manifest.get("generated_outputs") or []:
        p = RUNTIME / rel
        if not p.is_file():
            print(f"MISSING {rel}")
            continue
        n = p.stat().st_size
        total += n
        if "/data/" in rel.replace("\\", "/") or rel.startswith("Source/RoboUnicode/data/"):
            data_total += n
        print(f"{n:10d}  {rel}")
    print(f"{data_total:10d}  data/*.c subtotal")
    print(f"{total:10d}  all generated artifacts")
    print()
    print("Performance: property/segmentation microbench is SuiteRoboUnicodeInfra")
    print("(host Debug/Release). No allocation on ordinary property lookups.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

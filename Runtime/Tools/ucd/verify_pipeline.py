#!/usr/bin/env python3
"""
Verify Unicode pipeline integrity without regenerating (Goal 3.14).

Checks:
  - manifest vs version pin
  - source file checksums + version headers
  - C RoboUnicodeVersion.h sync
  - golden hashes of committed generated artifacts
  - optional: report table sizes

Usage:
  python verify_pipeline.py
  python verify_pipeline.py --sizes
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from generate_all import check_golden
from robo_unicode_version import EMOJI_VERSION, GENERATOR_VERSION, UCD_VERSION, assert_versions_coherent
from ucd_common import (
    RUNTIME,
    load_manifest,
    sync_c_version_header,
    verify_sources,
)


def report_sizes(manifest: dict) -> None:
    print("--- generated artifact sizes ---")
    total = 0
    rows: list[tuple[int, str]] = []
    for rel in manifest.get("generated_outputs") or []:
        p = RUNTIME / rel
        if not p.is_file():
            print(f"MISSING {rel}")
            continue
        n = p.stat().st_size
        total += n
        rows.append((n, rel))
    for n, rel in sorted(rows, reverse=True):
        print(f"{n:10d}  {rel}")
    print(f"{total:10d}  TOTAL")


def main() -> int:
    assert_versions_coherent()
    ap = argparse.ArgumentParser()
    ap.add_argument("--sizes", action="store_true")
    args = ap.parse_args()

    print(f"Unicode {UCD_VERSION} | Emoji {EMOJI_VERSION} | generator {GENERATOR_VERSION}")
    manifest = load_manifest()
    for line in verify_sources(manifest):
        print(line)

    hdr = sync_c_version_header()
    text = hdr.read_text(encoding="utf-8")
    if UCD_VERSION not in text or EMOJI_VERSION not in text:
        print("FAIL RoboUnicodeVersion.h content mismatch", file=sys.stderr)
        return 1
    print(f"OK {hdr.relative_to(RUNTIME).as_posix()}")

    check_golden()

    if args.sizes:
        report_sizes(manifest)

    print("OK pipeline verification")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

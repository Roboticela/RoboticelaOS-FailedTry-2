#!/usr/bin/env python3
"""Validate locale-id consistency across generated RoboI18N datasets (Goal 4.16)."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "Source" / "RoboI18N" / "data"
INC = ROOT / "Include" / "RoboI18N"


def extract_string_literals(text: str) -> set[str]:
    return set(re.findall(r'"([a-z]{2,3}(?:-[A-Za-z0-9]+)*)"', text))


def main() -> int:
    errors: list[str] = []
    files = {
        "cldr": DATA / "robo_cldr_data.c",
        "plural": DATA / "robo_plural_data.c",
        "cal": DATA / "robo_cal_data.c",
        "coll": DATA / "robo_coll_data.c",
        "disp": DATA / "robo_disp_data.c",
        "msg": DATA / "robo_msg_data.c",
        "hc": DATA / "robo_hc_data.c",
    }
    for name, path in files.items():
        if not path.is_file():
            errors.append(f"missing {path}")
    if errors:
        print("\n".join(errors))
        return 1

    # Version pins must agree on CLDR 46 in headers.
    version_h = (INC / "RoboI18NVersion.h").read_text(encoding="utf-8")
    cldr_h = (INC / "RoboCldrData.h").read_text(encoding="utf-8")
    if 'ROBO_CLDR_VERSION_STRING "46"' not in version_h:
        errors.append("RoboI18NVersion.h CLDR pin mismatch")
    if 'ROBO_CLDR_VERSION "46"' not in cldr_h:
        errors.append("RoboCldrData.h CLDR pin mismatch")

    # Core languages used across i18n must appear in CLDR string pool.
    cldr_text = files["cldr"].read_text(encoding="utf-8", errors="replace")
    core = {"en", "ur", "ar", "fa", "he", "hi", "bn", "th", "zh", "ja", "ko", "de", "fr", "es"}
    for lang in core:
        if f'"{lang}"' not in cldr_text:
            errors.append(f"CLDR data missing language token {lang}")

    # Hour-cycle table must include world default region 001.
    hc_text = files["hc"].read_text(encoding="utf-8", errors="replace")
    if '"001"' not in hc_text:
        errors.append("hour-cycle data missing region 001")

    if errors:
        print("LOCALE CONSISTENCY FAIL:")
        print("\n".join(errors))
        return 1
    print("locale consistency OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

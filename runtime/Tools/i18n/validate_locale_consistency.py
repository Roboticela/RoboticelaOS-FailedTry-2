#!/usr/bin/env python3
"""Validate locale-id consistency across generated RoboI18N datasets (Goals 4.16 / 4.26)."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "Source" / "RoboI18N" / "data"
INC = ROOT / "Include" / "RoboI18N"

# Goal 4.26 international regression corpus — must appear as language tokens in CLDR.
CORE_LANGS = {
    "en",
    "fr",
    "de",
    "es",
    "tr",
    "ru",
    "pl",
    "uk",
    "ar",
    "fa",
    "ur",
    "he",
    "hi",
    "bn",
    "th",
    "zh",
    "ja",
    "ko",
}


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
        "unit": DATA / "robo_unit_data.c",
        "currency": DATA / "robo_currency_data.c",
        "rel": DATA / "robo_rel_data.c",
        "translit": DATA / "robo_translit_data.c",
        "tz": DATA / "robo_tz_data.c",
    }
    for name, path in files.items():
        if not path.is_file():
            errors.append(f"missing {path}")
    if errors:
        print("\n".join(errors))
        return 1

    version_h = (INC / "RoboI18NVersion.h").read_text(encoding="utf-8")
    cldr_h = (INC / "RoboCldrData.h").read_text(encoding="utf-8")
    if 'ROBO_CLDR_VERSION_STRING "46"' not in version_h:
        errors.append("RoboI18NVersion.h CLDR pin mismatch")
    if 'ROBO_CLDR_VERSION "46"' not in cldr_h and 'ROBO_CLDR_VERSION_STRING "46"' not in cldr_h:
        # Accept either macro spelling used by generate_cldr.py
        if '46' not in cldr_h:
            errors.append("RoboCldrData.h CLDR pin mismatch")

    if 'ROBO_CLDR_UNICODE_VERSION_STRING "16.0.0"' not in version_h:
        errors.append("RoboI18NVersion.h Unicode pin mismatch")
    if 'ROBO_UCA_VERSION_STRING_PIN "16.0.0"' not in version_h:
        errors.append("RoboI18NVersion.h UCA pin missing")
    if 'ROBO_TZDB_VERSION_STRING_PIN "2026c"' not in version_h:
        errors.append("RoboI18NVersion.h tzdb pin mismatch")

    cldr_text = files["cldr"].read_text(encoding="utf-8", errors="replace")
    for lang in sorted(CORE_LANGS):
        if f'"{lang}"' not in cldr_text:
            errors.append(f"CLDR data missing language token {lang}")

    plural_text = files["plural"].read_text(encoding="utf-8", errors="replace")
    for lang in ("en", "ar", "ru", "pl", "zh", "ja"):
        if f'"{lang}"' not in plural_text:
            errors.append(f"plural data missing language token {lang}")

    hc_text = files["hc"].read_text(encoding="utf-8", errors="replace")
    if '"001"' not in hc_text:
        errors.append("hour-cycle data missing region 001")

    # Translit must expose curated Cyrillic-Latin (sanity).
    tr = files["translit"].read_text(encoding="utf-8", errors="replace")
    if "Cyrillic-Latin" not in tr:
        errors.append("translit data missing Cyrillic-Latin id")

    if errors:
        print("LOCALE CONSISTENCY FAIL:")
        print("\n".join(errors))
        return 1
    print("locale consistency OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

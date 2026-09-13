#!/usr/bin/env python3
"""Shared RoboI18N data-pipeline helpers (Goal 4.26)."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Any

from robo_i18n_version import (
    CLDR_JSON_PACKAGE,
    CLDR_UNICODE_VERSION,
    CLDR_VERSION,
    GENERATOR_VERSION,
    IANA_REGISTRY_FILE_DATE,
    ISO4217_SOURCE,
    ROBO_I18N_API_FROZEN,
    ROBO_I18N_VERSION,
    TZDB_VERSION,
    UCA_VERSION,
    assert_ucd_cldr_coherent,
)

HERE = Path(__file__).resolve().parent
RUNTIME = HERE.parent.parent
MANIFEST_PATH = HERE / "manifest.json"
GOLDEN_PATH = HERE / "i18n_golden_hashes.json"
DATA_MANIFEST_MD = RUNTIME / "i18n" / "DataManifest.md"

GENERATED_ARTIFACTS: list[str] = [
    "Include/RoboI18N/RoboI18NVersion.h",
    "Include/RoboI18N/RoboIanaData.h",
    "Source/RoboI18N/data/robo_iana_data.c",
    "Include/RoboI18N/RoboCldrData.h",
    "Source/RoboI18N/data/robo_cldr_data.c",
    "Include/RoboI18N/RoboPluralData.h",
    "Source/RoboI18N/data/robo_plural_data.c",
    "Tests/Data/robo_plural_samples.txt",
    "Include/RoboI18N/RoboCalData.h",
    "Source/RoboI18N/data/robo_cal_data.c",
    "Include/RoboI18N/RoboRelData.h",
    "Source/RoboI18N/data/robo_rel_data.c",
    "Include/RoboI18N/RoboHcData.h",
    "Source/RoboI18N/data/robo_hc_data.c",
    "Include/RoboI18N/RoboDispData.h",
    "Source/RoboI18N/data/robo_disp_data.c",
    "Include/RoboI18N/RoboUnitData.h",
    "Source/RoboI18N/data/robo_unit_data.c",
    "Include/RoboI18N/RoboMeasurePrefData.h",
    "Source/RoboI18N/data/robo_measure_pref_data.c",
    "Include/RoboI18N/RoboCurrencyData.h",
    "Source/RoboI18N/data/robo_currency_data.c",
    "Include/RoboI18N/RoboMsgData.h",
    "Source/RoboI18N/data/robo_msg_data.c",
    "Include/RoboI18N/RoboTzData.h",
    "Source/RoboI18N/data/robo_tz_data.c",
    "Include/RoboI18N/RoboUcaData.h",
    "Source/RoboI18N/data/robo_uca_data.c",
    "Include/RoboI18N/RoboCollData.h",
    "Source/RoboI18N/data/robo_coll_data.c",
    "Include/RoboI18N/RoboTranslitData.h",
    "Source/RoboI18N/data/robo_translit_data.c",
]

# Core regression language tokens (Goal 4.26 consistency / CLDR presence).
REGRESSION_LOCALES = [
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
]

# Phase 4 final regional matrix (Goal 4.27 host suite).
PHASE4_REGRESSION_LOCALES = [
    "en-US",
    "en-GB",
    "fr-FR",
    "de-DE",
    "es-ES",
    "tr-TR",
    "ru-RU",
    "uk-UA",
    "pl-PL",
    "ar-SA",
    "fa-IR",
    "ur-PK",
    "he-IL",
    "hi-IN",
    "bn-BD",
    "th-TH",
    "zh-CN",
    "zh-TW",
    "ja-JP",
    "ko-KR",
]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def write_json(path: Path, obj: Any) -> None:
    path.write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_ucd_pins() -> tuple[str, str]:
    ucd_ver = HERE.parent / "ucd" / "robo_unicode_version.py"
    ns: dict = {}
    exec(ucd_ver.read_text(encoding="utf-8"), ns)
    return str(ns["UCD_VERSION"]), str(ns["EMOJI_VERSION"])


def hash_generated(artifacts: list[str] | None = None) -> dict[str, str]:
    arts = artifacts or GENERATED_ARTIFACTS
    out: dict[str, str] = {}
    for rel in arts:
        path = RUNTIME / rel
        if path.is_file():
            out[rel.replace("\\", "/")] = sha256_file(path)
    return out


def extract_define(text: str, name: str) -> str | None:
    m = re.search(rf'#define\s+{re.escape(name)}\s+"([^"]*)"', text)
    return m.group(1) if m else None


def sync_c_version_header() -> None:
    """Rewrite RoboI18NVersion.h from Python pins (deterministic)."""
    path = RUNTIME / "Include" / "RoboI18N" / "RoboI18NVersion.h"
    body = f"""/* RoboI18N version pin (Goals 4.1–4.27). Synced with Tools/i18n/robo_i18n_version.py.
 * Phase 4 public API is frozen when ROBO_I18N_API_FROZEN is 1.
 */
#ifndef ROBO_I18N_VERSION_H
#define ROBO_I18N_VERSION_H

#define ROBO_I18N_VERSION_MAJOR {ROBO_I18N_VERSION.split(".")[0]}
#define ROBO_I18N_VERSION_MINOR {ROBO_I18N_VERSION.split(".")[1]}
#define ROBO_I18N_VERSION_PATCH {ROBO_I18N_VERSION.split(".")[2]}
#define ROBO_I18N_VERSION_STRING "{ROBO_I18N_VERSION}"

/** 1 = Phase 4 public API frozen (Goal 4.27). Breaking changes require a new major/minor. */
#define ROBO_I18N_API_FROZEN {int(ROBO_I18N_API_FROZEN)}

#define ROBO_IANA_REGISTRY_FILE_DATE "{IANA_REGISTRY_FILE_DATE}"
#define ROBO_CLDR_VERSION_STRING "{CLDR_VERSION}"
#define ROBO_CLDR_UNICODE_VERSION_STRING "{CLDR_UNICODE_VERSION}"

/** RoboMsg catalog binary format version (Goal 4.11). */
#define ROBO_MSG_FORMAT_VERSION 1u

/** IANA tzdb release pin (Goal 4.14). */
#define ROBO_TZDB_VERSION_STRING_PIN "{TZDB_VERSION}"

/** UCA / DUCET pin (must match Unicode {CLDR_UNICODE_VERSION}). */
#define ROBO_UCA_VERSION_STRING_PIN "{UCA_VERSION}"

/**
 * ISO 4217 currency identifiers arrive via CLDR codeMappings (Goal 4.21/4.26).
 * Not a separate ISO dump version.
 */
#define ROBO_ISO4217_SOURCE_STRING "{ISO4217_SOURCE}"

/** Data generator stream version (Tools/i18n). */
#define ROBO_I18N_GENERATOR_VERSION_STRING "{GENERATOR_VERSION}"

#endif /* ROBO_I18N_VERSION_H */
"""
    path.write_text(body, encoding="utf-8", newline="\n")


def build_authoritative_manifest() -> dict[str, Any]:
    assert_ucd_cldr_coherent()
    ucd, emoji = load_ucd_pins()
    return {
        "schema_version": 1,
        "robo_i18n_version": ROBO_I18N_VERSION,
        "api_frozen": bool(ROBO_I18N_API_FROZEN),
        "generator_version": GENERATOR_VERSION,
        "phase4_regression_locales": PHASE4_REGRESSION_LOCALES,
        "unicode": {
            "ucd_version": ucd,
            "emoji_version": emoji,
            "uca_version": UCA_VERSION,
            "cldr_unicode_association": CLDR_UNICODE_VERSION,
            "note": "UCD/Emoji from Tools/ucd; UCA and CLDR Unicode association must match UCD.",
        },
        "cldr": {
            "version": CLDR_VERSION,
            "package": CLDR_JSON_PACKAGE,
            "unicode_version": CLDR_UNICODE_VERSION,
        },
        "iana_language_subtag_registry": {
            "file_date": IANA_REGISTRY_FILE_DATE,
        },
        "tzdb": {
            "version": TZDB_VERSION,
        },
        "iso4217": {
            "source": ISO4217_SOURCE,
            "note": "Via CLDR supplemental codeMappings; no separate ISO dump.",
        },
        "generated_artifacts": GENERATED_ARTIFACTS,
        "regression_locales": REGRESSION_LOCALES,
        "offline_policy": (
            "Build.ps1 compiles committed tables only; Fetch-*.ps1 + generate_*.py "
            "and verify_i18n_pipeline.py are explicit developer steps."
        ),
    }

#!/usr/bin/env python3
"""Authoritative version pins for RoboI18N tooling (Goals 4.1–4.4)."""

from __future__ import annotations

GENERATOR_VERSION = "1.3.0"

# IANA Language Subtag Registry (BCP 47) — independent of Unicode/CLDR versions.
IANA_REGISTRY_FILE_DATE = "2026-05-05"
IANA_REGISTRY_URL = (
    "https://www.iana.org/assignments/language-subtag-registry/language-subtag-registry"
)

# CLDR release pin (Unicode CLDR project). Separate from UCD / Emoji versions.
CLDR_VERSION = "46"
CLDR_UNICODE_VERSION = "16.0.0"  # from CLDR supplemental version._unicodeVersion
CLDR_JSON_PACKAGE = "46.0.0"
CLDR_VERSION_NOTE = (
    "CLDR 46 JSON (modern + supplemental). Unicode 16.0.0 association comes from "
    "CLDR supplemental version fields — not mixed with other CLDR releases."
)

# Runtime module API version.
ROBO_I18N_VERSION_MAJOR = 0
ROBO_I18N_VERSION_MINOR = 4
ROBO_I18N_VERSION_PATCH = 0
ROBO_I18N_VERSION = (
    f"{ROBO_I18N_VERSION_MAJOR}.{ROBO_I18N_VERSION_MINOR}.{ROBO_I18N_VERSION_PATCH}"
)

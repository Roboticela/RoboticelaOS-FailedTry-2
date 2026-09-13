#!/usr/bin/env python3
"""Authoritative version pins for RoboI18N tooling (Goal 4.1)."""

from __future__ import annotations

GENERATOR_VERSION = "1.0.0"

# IANA Language Subtag Registry (BCP 47) — independent of Unicode/CLDR versions.
IANA_REGISTRY_FILE_DATE = "2026-05-05"
IANA_REGISTRY_URL = (
    "https://www.iana.org/assignments/language-subtag-registry/language-subtag-registry"
)

# CLDR is a separate dataset. Not imported for Goal 4.1 — architecture only.
CLDR_VERSION = "none"  # set when first CLDR-derived tables are generated
CLDR_VERSION_NOTE = (
    "CLDR data is not yet imported. Unicode/UCD version is independent "
    "(see Tools/ucd/robo_unicode_version.py)."
)

# Runtime module API version (semantic for RoboI18N foundation).
ROBO_I18N_VERSION_MAJOR = 0
ROBO_I18N_VERSION_MINOR = 1
ROBO_I18N_VERSION_PATCH = 0
ROBO_I18N_VERSION = (
    f"{ROBO_I18N_VERSION_MAJOR}.{ROBO_I18N_VERSION_MINOR}.{ROBO_I18N_VERSION_PATCH}"
)

#!/usr/bin/env python3
"""
Authoritative Unicode / Emoji version pin for Roboticela UCD tooling (Goal 3.14).

All generate_*.py scripts MUST import UCD_VERSION / EMOJI_VERSION from here.
Do not scatter version literals across generators.
"""

from __future__ import annotations

# --- Roboticela generator tooling version (bump when pipeline semantics change) ---
GENERATOR_VERSION = "1.0.0"

# --- Unicode Standard / UCD ---
UCD_VERSION_MAJOR = 16
UCD_VERSION_MINOR = 0
UCD_VERSION_PATCH = 0
UCD_VERSION = f"{UCD_VERSION_MAJOR}.{UCD_VERSION_MINOR}.{UCD_VERSION_PATCH}"

# --- UTS #51 / Emoji (may advance separately from UCD; document relationship) ---
EMOJI_VERSION_MAJOR = 16
EMOJI_VERSION_MINOR = 0
EMOJI_VERSION = f"{EMOJI_VERSION_MAJOR}.{EMOJI_VERSION_MINOR}"

# Relationship: Emoji 16.0 is the emoji data train paired with Unicode 16.0.0.
EMOJI_UCD_RELATION = (
    f"Emoji {EMOJI_VERSION} data is used with Unicode {UCD_VERSION} "
    "(official Unicode.org emoji/16.0 distribution)."
)


def assert_versions_coherent() -> None:
    if UCD_VERSION_MAJOR < 1 or UCD_VERSION_MINOR < 0 or UCD_VERSION_PATCH < 0:
        raise SystemExit(f"invalid UCD version components: {UCD_VERSION}")
    if EMOJI_VERSION_MAJOR != UCD_VERSION_MAJOR or EMOJI_VERSION_MINOR != UCD_VERSION_MINOR:
        # Allowed in principle, but Roboticela currently requires matching majors/minors.
        raise SystemExit(
            f"Emoji {EMOJI_VERSION} does not match Unicode {UCD_VERSION} major.minor — "
            "update robo_unicode_version.py and document the relationship explicitly"
        )

#!/usr/bin/env python3
"""
RoboI18N completeness / conformance verification pipeline (Goal 4.26).

Usage:
  python verify_i18n_pipeline.py                  # full verify (pins + golden + locale + chromium)
  python verify_i18n_pipeline.py --update-golden  # refresh i18n_golden_hashes.json from committed tables
  python verify_i18n_pipeline.py --report         # write CompletenessReport.md
  python verify_i18n_pipeline.py --check-pins     # version coherence only

Normal runtime builds do NOT run this. Regeneration remains an explicit step.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from i18n_common import (
    DATA_MANIFEST_MD,
    GENERATED_ARTIFACTS,
    GOLDEN_PATH,
    HERE,
    MANIFEST_PATH,
    REGRESSION_LOCALES,
    RUNTIME,
    build_authoritative_manifest,
    extract_define,
    hash_generated,
    load_ucd_pins,
    sha256_file,
    sync_c_version_header,
    write_json,
)
from robo_i18n_version import (
    CLDR_UNICODE_VERSION,
    CLDR_VERSION,
    GENERATOR_VERSION,
    IANA_REGISTRY_FILE_DATE,
    ISO4217_SOURCE,
    ROBO_I18N_VERSION,
    TZDB_VERSION,
    UCA_VERSION,
    assert_ucd_cldr_coherent,
)
from validate_locale_consistency import main as locale_main

REPO = RUNTIME.parent


def check_pins() -> list[str]:
    errors: list[str] = []
    try:
        assert_ucd_cldr_coherent()
    except SystemExit as e:
        errors.append(str(e))

    ucd, emoji = load_ucd_pins()
    if ucd != CLDR_UNICODE_VERSION:
        errors.append(f"UCD {ucd} != CLDR unicode {CLDR_UNICODE_VERSION}")

    vh = (RUNTIME / "Include" / "RoboI18N" / "RoboI18NVersion.h").read_text(encoding="utf-8")
    checks = {
        "ROBO_I18N_VERSION_STRING": ROBO_I18N_VERSION,
        "ROBO_I18N_GENERATOR_VERSION_STRING": GENERATOR_VERSION,
        "ROBO_CLDR_VERSION_STRING": CLDR_VERSION,
        "ROBO_CLDR_UNICODE_VERSION_STRING": CLDR_UNICODE_VERSION,
        "ROBO_TZDB_VERSION_STRING_PIN": TZDB_VERSION,
        "ROBO_IANA_REGISTRY_FILE_DATE": IANA_REGISTRY_FILE_DATE,
        "ROBO_UCA_VERSION_STRING_PIN": UCA_VERSION,
        "ROBO_ISO4217_SOURCE_STRING": ISO4217_SOURCE,
    }
    for name, want in checks.items():
        got = extract_define(vh, name)
        if got != want:
            errors.append(f"{name}: header={got!r} pin={want!r}")

    # Freeze marker (integer define).
    m = re.search(r"#define\s+ROBO_I18N_API_FROZEN\s+(\d+)", vh)
    if not m or int(m.group(1)) != 1:
        errors.append("ROBO_I18N_API_FROZEN must be 1 for Phase 4 freeze")

    # Subsystem headers must not declare a conflicting CLDR major.
    for rel in (
        "Include/RoboI18N/RoboCldrData.h",
        "Include/RoboI18N/RoboPluralData.h",
        "Include/RoboI18N/RoboCalData.h",
        "Include/RoboI18N/RoboCurrencyData.h",
        "Include/RoboI18N/RoboTranslitData.h",
        "Include/RoboI18N/RoboCollData.h",
        "Include/RoboI18N/RoboDispData.h",
    ):
        path = RUNTIME / rel
        if not path.is_file():
            errors.append(f"missing {rel}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        # Look for "46" near CLDR version macros / comments.
        if "46" not in text and "CLDR" in text:
            errors.append(f"{rel}: CLDR 46 marker not found")

    # UCA header pin
    uca_h = RUNTIME / "Include" / "RoboI18N" / "RoboUcaData.h"
    if uca_h.is_file():
        ut = uca_h.read_text(encoding="utf-8", errors="replace")
        uv = extract_define(ut, "ROBO_UCA_VERSION_STRING")
        if uv and uv != UCA_VERSION:
            errors.append(f"RoboUcaData.h UCA {uv} != pin {UCA_VERSION}")

    # Unicode emoji pin readable
    emoji_h = RUNTIME / "Include" / "RoboUnicode" / "RoboUnicodeVersion.h"
    if emoji_h.is_file():
        et = emoji_h.read_text(encoding="utf-8", errors="replace")
        ev = extract_define(et, "ROBO_EMOJI_VERSION_STRING")
        if ev and ev != emoji:
            errors.append(f"emoji header {ev} disagrees with pin {emoji}")

    return errors


def check_golden() -> list[str]:
    errors: list[str] = []
    if not GOLDEN_PATH.is_file():
        return [f"missing {GOLDEN_PATH} — run with --update-golden"]
    golden = json_load(GOLDEN_PATH)
    if golden.get("robo_i18n_version") != ROBO_I18N_VERSION:
        errors.append(
            f"golden API {golden.get('robo_i18n_version')!r} != {ROBO_I18N_VERSION!r}"
        )
    if golden.get("cldr_version") != CLDR_VERSION:
        errors.append(f"golden CLDR {golden.get('cldr_version')!r} != {CLDR_VERSION!r}")
    if golden.get("unicode_version") != CLDR_UNICODE_VERSION:
        errors.append(
            f"golden Unicode {golden.get('unicode_version')!r} != {CLDR_UNICODE_VERSION!r}"
        )
    actual = hash_generated()
    expected = golden.get("artifacts") or {}
    for rel, digest in sorted(expected.items()):
        got = actual.get(rel)
        if got is None:
            errors.append(f"missing artifact {rel}")
        elif got != digest:
            errors.append(f"hash drift {rel}\n  expected {digest}\n  actual   {got}")
    for rel in actual:
        if rel not in expected:
            errors.append(f"unexpected artifact not in golden: {rel}")
    return errors


def json_load(path: Path) -> dict:
    import json

    return json.loads(path.read_text(encoding="utf-8"))


def update_golden() -> None:
    sync_c_version_header()
    hashes = hash_generated()
    payload = {
        "schema_version": 1,
        "robo_i18n_version": ROBO_I18N_VERSION,
        "generator_version": GENERATOR_VERSION,
        "cldr_version": CLDR_VERSION,
        "unicode_version": CLDR_UNICODE_VERSION,
        "tzdb_version": TZDB_VERSION,
        "uca_version": UCA_VERSION,
        "iana_file_date": IANA_REGISTRY_FILE_DATE,
        "iso4217_source": ISO4217_SOURCE,
        "artifacts": hashes,
    }
    write_json(GOLDEN_PATH, payload)
    print(f"Wrote {GOLDEN_PATH} ({len(hashes)} artifacts)")


def update_manifest() -> None:
    sync_c_version_header()
    man = build_authoritative_manifest()
    # Preserve supplemental URL/sha entries from existing manifest when present.
    if MANIFEST_PATH.is_file():
        old = json_load(MANIFEST_PATH)
        if "cldr" in old and "supplemental_files" in old["cldr"]:
            man["cldr"]["supplemental_files"] = old["cldr"]["supplemental_files"]
            for k in ("numbers_base_url", "dates_base_url", "supplemental_base_url", "note", "attribution", "categories"):
                if k in old["cldr"]:
                    man["cldr"][k] = old["cldr"][k]
        if "iana" in old:
            man["iana"] = old["iana"]
            man["iana"]["file_date"] = IANA_REGISTRY_FILE_DATE
    write_json(MANIFEST_PATH, man)
    print(f"Wrote {MANIFEST_PATH}")


def write_data_manifest_md(man: dict) -> None:
    uc = man["unicode"]
    lines = [
        "# RoboI18N Data / Version Manifest (Goal 4.26)",
        "",
        "Authoritative machine-readable source: `Tools/i18n/manifest.json` +",
        "`Tools/i18n/robo_i18n_version.py` + `Tools/ucd/robo_unicode_version.py`.",
        "",
        "| Domain | Version / source |",
        "|---|---|",
        f"| RoboI18N API | **{man['robo_i18n_version']}** |",
        f"| I18N generator | **{man['generator_version']}** |",
        f"| Unicode UCD | **{uc['ucd_version']}** |",
        f"| Emoji | **{uc['emoji_version']}** |",
        f"| UCA / DUCET | **{uc['uca_version']}** |",
        f"| CLDR | **{man['cldr']['version']}** (package {man['cldr']['package']}) |",
        f"| CLDR Unicode association | **{man['cldr']['unicode_version']}** |",
        f"| IANA language-subtag File-Date | **{man['iana_language_subtag_registry']['file_date']}** |",
        f"| IANA tzdb | **{man['tzdb']['version']}** |",
        f"| ISO 4217 | **{man['iso4217']['source']}** |",
        "",
        "## Compatibility rule",
        "",
        "UCD version, UCA version, and CLDR Unicode association must be identical",
        f"(currently **{CLDR_UNICODE_VERSION}**). Mixing CLDR releases or UCD releases",
        "in one generation is forbidden.",
        "",
        "## Offline policy",
        "",
        man["offline_policy"],
        "",
        "## Regression locales",
        "",
        ", ".join(f"`{x}`" for x in man["regression_locales"]),
        "",
        "## Generated artifacts",
        "",
    ]
    for a in man["generated_artifacts"]:
        lines.append(f"- `{a}`")
    lines.append("")
    DATA_MANIFEST_MD.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"Wrote {DATA_MANIFEST_MD}")


def chromium_audit() -> list[str]:
    """Fail if production runtime sources include/link Chromium paths."""
    errors: list[str] = []
    roots = [
        RUNTIME / "Include",
        RUNTIME / "Source",
        RUNTIME / "Tests" / "Suites",
        RUNTIME / "Tools" / "i18n",
    ]
    # Real dependency smells — not prose that says "not from Chromium".
    banned = re.compile(
        r"(#\s*include\s*[<\"][^>\"]*chromium[/\\]|#\s*include\s*[<\"]blink[/\\]|"
        r"#\s*include\s*[<\"]v8[/\\]|-I[^ \n]*chromium|/chromium/|/blink/|/v8/)",
        re.IGNORECASE,
    )
    skip_dirs = {".git", "__pycache__", "source", "Out"}
    for root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if any(p in skip_dirs for p in path.parts):
                continue
            if path.suffix.lower() not in {".c", ".h", ".py", ".ps1"}:
                continue
            if path.name in {"verify_i18n_pipeline.py", "i18n_common.py"}:
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            if banned.search(text):
                errors.append(f"chromium include/link path in {path.relative_to(REPO)}")
    return errors


def write_completeness_report() -> None:
    man = build_authoritative_manifest()
    report = RUNTIME / "i18n" / "CompletenessReport.md"
    # Probe which regression locales appear in CLDR pool.
    cldr_c = RUNTIME / "Source" / "RoboI18N" / "data" / "robo_cldr_data.c"
    cldr_text = cldr_c.read_text(encoding="utf-8", errors="replace") if cldr_c.is_file() else ""
    locale_rows = []
    for loc in REGRESSION_LOCALES:
        present = f'"{loc}"' in cldr_text
        locale_rows.append(
            f"| `{loc}` | {'present' if present else 'missing token'} | curated regression |"
        )

    intentional = [
        "Not every CLDR locale implements every feature — only curated modern coverage.",
        "Full LDML collation rule compilation for every locale (esp. CJK) is intentional later work.",
        "Full CLDR transform catalog is curated (Goal 4.25 allowlist), not 100% of transforms.",
        "Search does not auto-enable transliteration (Goal 4.24/4.25 boundary).",
        "ISO 4217 is CLDR-derived; discrepancies vs a live ISO list are reported as data-source notes.",
        "Leap seconds are not modeled in tzdb tables.",
    ]
    lines = [
        "# RoboI18N Completeness / Conformance Report (Goal 4.26)",
        "",
        f"Generated by `Tools/i18n/verify_i18n_pipeline.py --report`. API **{ROBO_I18N_VERSION}**,",
        f"generator **{GENERATOR_VERSION}**, CLDR **{CLDR_VERSION}**, Unicode **{CLDR_UNICODE_VERSION}**,",
        f"tzdb **{TZDB_VERSION}**, UCA **{UCA_VERSION}**.",
        "",
        "## Status summary",
        "",
        "| Area | Status |",
        "|---|---|",
        "| Version pins coherent | required by verify |",
        "| Generated-data golden hashes | `i18n_golden_hashes.json` |",
        "| Locale consistency | `validate_locale_consistency.py` |",
        "| Plural samples (CLDR) | `Tests/Data/robo_plural_samples.txt` |",
        "| International regression corpus | `SuiteRoboI18NConformance.c` |",
        "| Chromium independence | verify + Build.ps1 Assert-NoChromiumPath |",
        "",
        "## Regression locales",
        "",
        "| Locale | CLDR token | Role |",
        "|---|---|---|",
        *locale_rows,
        "",
        "## Error taxonomy (do not conflate)",
        "",
        "| Code meaning | Use when |",
        "|---|---|",
        "| data unavailable | CLDR/tzdb/UCA has no record for this locale/key |",
        "| feature unsupported | API exists but this build omits the feature deliberately |",
        "| locale unsupported | identifier rejected / not in IANA+CLDR tables |",
        "| invalid locale | syntactically illegal BCP 47 / parse error |",
        "| fallback used | resolved via parent / likely subtags / root |",
        "",
        "## Intentional limitations (not accidental omissions)",
        "",
    ]
    for item in intentional:
        lines.append(f"- {item}")
    lines.extend(
        [
            "",
            "## Subsystems audited",
            "",
            "Locale parse/canonicalize/fallback/negotiate, CLDR locale data, numbering,",
            "number format/parse, cardinal+ordinal plurals, calendars, date/time format/interval/",
            "relative/parse/arith, units, measure prefs, currency, collation, search,",
            "transliteration, tzdb + display names, RoboMsg + format + validate.",
            "",
            "## How to re-run",
            "",
            "```text",
            "python runtime/Tools/i18n/verify_i18n_pipeline.py",
            "python runtime/Tools/i18n/verify_i18n_pipeline.py --report",
            "python runtime/Tools/ucd/generate_all.py --verify-only",
            ".\\runtime\\Tools\\Build.ps1 -Configuration Debug -Test -Sanitize",
            "```",
            "",
        ]
    )
    report.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"Wrote {report}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--update-golden", action="store_true")
    ap.add_argument("--update-manifest", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--check-pins", action="store_true")
    args = ap.parse_args()

    if args.update_manifest or args.update_golden or args.report:
        sync_c_version_header()

    if args.update_manifest:
        update_manifest()
        write_data_manifest_md(build_authoritative_manifest())

    if args.update_golden:
        update_golden()

    if args.report:
        write_completeness_report()
        write_data_manifest_md(build_authoritative_manifest())

    errors: list[str] = []
    errors.extend(check_pins())
    if args.check_pins:
        if errors:
            print("PIN CHECK FAIL:")
            print("\n".join(errors))
            return 1
        print("pin check OK")
        return 0

    # Always refresh manifest metadata on full verify if pins changed (non-destructive
    # to supplemental hashes when --update-manifest not passed: still validate pins).
    if not GOLDEN_PATH.is_file():
        print("NOTE: no i18n_golden_hashes.json yet — creating from committed tables")
        update_golden()

    errors.extend(check_golden())

    # Locale consistency (reuse module)
    loc_rc = locale_main()
    if loc_rc != 0:
        errors.append("validate_locale_consistency failed")

    errors.extend(chromium_audit())

    if errors:
        print("I18N VERIFY FAIL:")
        print("\n".join(errors))
        return 1

    print(
        f"I18N VERIFY PASS: API {ROBO_I18N_VERSION} generator {GENERATOR_VERSION} "
        f"CLDR {CLDR_VERSION} Unicode {CLDR_UNICODE_VERSION} tzdb {TZDB_VERSION}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

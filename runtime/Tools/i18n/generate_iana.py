#!/usr/bin/env python3
"""
Generate compact IANA BCP 47 Preferred-Value tables for RoboI18N (Goal 4.1).

Input:  Tools/i18n/source/language-subtag-registry
Output: Include/RoboI18N/RoboIanaData.h
        Source/RoboI18N/data/robo_iana_data.c
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

from robo_i18n_version import (
    GENERATOR_VERSION,
    IANA_REGISTRY_FILE_DATE,
    IANA_REGISTRY_URL,
    ROBO_I18N_VERSION,
)

HERE = Path(__file__).resolve().parent
RUNTIME = HERE.parent.parent
SRC = HERE / "source" / "language-subtag-registry"
OUT_H = RUNTIME / "Include" / "RoboI18N" / "RoboIanaData.h"
OUT_C = RUNTIME / "Source" / "RoboI18N" / "data" / "robo_iana_data.c"


def parse_registry(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    file_date = None
    records: list[dict[str, str]] = []
    cur: dict[str, str] = {}
    pending_key = None

    def flush() -> None:
        nonlocal cur
        if cur:
            records.append(cur)
            cur = {}

    for raw in text.splitlines():
        line = raw.rstrip("\n")
        if line.startswith("File-Date:"):
            file_date = line.split(":", 1)[1].strip()
            continue
        if line.strip() == "%%":
            flush()
            pending_key = None
            continue
        if not line:
            continue
        if line.startswith("  ") and pending_key:
            # folded continuation
            cur[pending_key] = cur.get(pending_key, "") + " " + line.strip()
            continue
        if ":" not in line:
            continue
        key, val = line.split(":", 1)
        key = key.strip()
        val = val.strip()
        pending_key = key
        if key in cur and key == "Description":
            cur[key] = cur[key] + "; " + val
        else:
            cur[key] = val
    flush()
    return {"file_date": file_date, "records": records}


def c_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def emit_map(name: str, pairs: list[tuple[str, str]]) -> tuple[str, str]:
    pairs = sorted(pairs, key=lambda p: p[0].lower())
    h = (
        f"extern const robo_iana_pref_entry_t {name}[];\n"
        f"extern const size_t {name}_count;\n"
    )
    lines = [f"const robo_iana_pref_entry_t {name}[] = {{"]
    for a, b in pairs:
        lines.append(f'    {{"{c_escape(a)}", "{c_escape(b)}"}},')
    if not pairs:
        lines.append("    {NULL, NULL},")
    lines.append("};")
    lines.append(f"const size_t {name}_count = {len(pairs)};")
    return h, "\n".join(lines)


def main() -> int:
    if not SRC.is_file():
        print(f"Missing {SRC} — run Fetch-Iana.ps1", file=sys.stderr)
        return 1

    digest = hashlib.sha256(SRC.read_bytes()).hexdigest()
    data = parse_registry(SRC)
    if data["file_date"] != IANA_REGISTRY_FILE_DATE:
        print(
            f"File-Date mismatch: registry={data['file_date']!r} "
            f"pin={IANA_REGISTRY_FILE_DATE!r}",
            file=sys.stderr,
        )
        return 1

    lang: list[tuple[str, str]] = []
    script: list[tuple[str, str]] = []
    region: list[tuple[str, str]] = []
    variant: list[tuple[str, str]] = []
    grandfathered: list[tuple[str, str]] = []
    redundant: list[tuple[str, str]] = []

    for rec in data["records"]:
        typ = rec.get("Type", "").lower()
        pref = rec.get("Preferred-Value")
        if not pref:
            continue
        if typ in ("language", "extlang", "script", "region", "variant"):
            sub = rec.get("Subtag")
            if not sub:
                continue
            bucket = {
                "language": lang,
                "extlang": lang,  # map extlang subtag → preferred language range primary handled at call site
                "script": script,
                "region": region,
                "variant": variant,
            }[typ]
            bucket.append((sub.lower() if typ != "region" else sub.upper() if sub.isalpha() else sub, pref))
        elif typ == "grandfathered":
            tag = rec.get("Tag")
            if tag:
                grandfathered.append((tag.lower(), pref.lower()))
        elif typ == "redundant":
            tag = rec.get("Tag")
            if tag:
                redundant.append((tag.lower(), pref.lower()))

    # Normalize map keys: languages/scripts/variants lower; regions as stored
    lang = [(a.lower(), b.lower()) for a, b in lang]
    script = [(a.lower(), b[:1].upper() + b[1:].lower() if len(b) == 4 else b) for a, b in script]
    region_norm = []
    for a, b in region:
        key = a.upper() if a.isalpha() else a
        val = b.upper() if b.isalpha() else b
        region_norm.append((key, val))
    region = region_norm
    variant = [(a.lower(), b.lower()) for a, b in variant]

    h_parts = []
    c_parts = []
    for name, pairs in [
        ("robo_iana_lang_pref", lang),
        ("robo_iana_script_pref", script),
        ("robo_iana_region_pref", region),
        ("robo_iana_variant_pref", variant),
        ("robo_iana_grandfathered", grandfathered),
        ("robo_iana_redundant", redundant),
    ]:
        hh, cc = emit_map(name, pairs)
        h_parts.append(hh)
        c_parts.append(cc)

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)

    OUT_H.write_text(
        "\n".join(
            [
                "/* Auto-generated by Tools/i18n/generate_iana.py — DO NOT EDIT. */",
                f"/* Roboticela generator {GENERATOR_VERSION} | IANA File-Date {IANA_REGISTRY_FILE_DATE} */",
                f"/* Source: {IANA_REGISTRY_URL} */",
                f"/* sha256: {digest} */",
                "#ifndef ROBO_IANA_DATA_H",
                "#define ROBO_IANA_DATA_H",
                "",
                "#include <stddef.h>",
                "",
                "#ifdef __cplusplus",
                'extern "C" {',
                "#endif",
                "",
                f'#define ROBO_IANA_FILE_DATE "{IANA_REGISTRY_FILE_DATE}"',
                f'#define ROBO_I18N_DATA_VERSION "{ROBO_I18N_VERSION}"',
                "",
                "typedef struct robo_iana_pref_entry {",
                "    const char *from;",
                "    const char *to;",
                "} robo_iana_pref_entry_t;",
                "",
                *h_parts,
                "",
                "const char *robo_iana_lookup(const robo_iana_pref_entry_t *table,",
                "                            size_t count,",
                "                            const char *key);",
                "",
                "const char *robo_iana_file_date(void);",
                "",
                "#ifdef __cplusplus",
                "}",
                "#endif",
                "",
                "#endif /* ROBO_IANA_DATA_H */",
                "",
            ]
        ),
        encoding="utf-8",
    )

    OUT_C.write_text(
        "\n".join(
            [
                "/* Auto-generated by Tools/i18n/generate_iana.py — DO NOT EDIT. */",
                f"/* IANA File-Date {IANA_REGISTRY_FILE_DATE} | sha256 {digest} */",
                '#include "RoboI18N/RoboIanaData.h"',
                "",
                "#include <string.h>",
                "",
                *c_parts,
                "",
                "const char *robo_iana_lookup(const robo_iana_pref_entry_t *table,",
                "                            size_t count,",
                "                            const char *key)",
                "{",
                "    size_t lo = 0;",
                "    size_t hi = count;",
                "    if (!table || !key || count == 0) {",
                "        return NULL;",
                "    }",
                "    while (lo < hi) {",
                "        size_t mid = lo + (hi - lo) / 2u;",
                "        int cmp = strcmp(key, table[mid].from);",
                "        if (cmp == 0) {",
                "            return table[mid].to;",
                "        }",
                "        if (cmp < 0) {",
                "            hi = mid;",
                "        } else {",
                "            lo = mid + 1u;",
                "        }",
                "    }",
                "    return NULL;",
                "}",
                "",
                "const char *robo_iana_file_date(void)",
                "{",
                "    return ROBO_IANA_FILE_DATE;",
                "}",
                "",
            ]
        ),
        encoding="utf-8",
    )

    print(
        f"Wrote {OUT_H.name} / {OUT_C.name}: "
        f"lang={len(lang)} script={len(script)} region={len(region)} "
        f"variant={len(variant)} gf={len(grandfathered)} red={len(redundant)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

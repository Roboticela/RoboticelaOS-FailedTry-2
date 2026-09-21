#!/usr/bin/env python3
"""
Emit RoboI18N currency metadata + format pattern tables (Goal 4.21).

Inputs:
  supplemental/currencyData.json   (fractions, region→currency)
  supplemental/codeMappings.json   (ISO 4217 numeric codes)
  numbers/{locale}.json            (currencyFormats-numberSystem-*)
  numbers/{locale}/currencies.json (plural display names for NAME style)

Outputs:
  Include/RoboI18N/RoboCurrencyData.h
  Source/RoboI18N/data/robo_currency_data.c
  Tools/i18n/currency_golden_hashes.json

Currency symbols / base display names remain in RoboDispData (Goal 4.10).
"""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

from robo_i18n_version import (
    CLDR_JSON_PACKAGE,
    CLDR_UNICODE_VERSION,
    CLDR_VERSION,
    GENERATOR_VERSION,
    ROBO_I18N_VERSION,
)

HERE = Path(__file__).resolve().parent
RUNTIME = HERE.parent.parent
SRC = HERE / "source" / f"cldr-{CLDR_JSON_PACKAGE}"
SUPP = SRC / "supplemental"
NUM = SRC / "numbers"
OUT_H = RUNTIME / "Include" / "RoboI18N" / "RoboCurrencyData.h"
OUT_C = RUNTIME / "Source" / "RoboI18N" / "data" / "robo_currency_data.c"
GOLDEN = HERE / "currency_golden_hashes.json"

# Locales whose currencyFormats we ship (locale fallback covers regions).
CURATED_LOCALES = (
    "und",
    "root",
    "en",
    "en-GB",
    "de",
    "fr",
    "es",
    "ja",
    "zh",
    "zh-Hans",
    "zh-Hant",
    "hi",
    "ur",
    "ar",
    "ar-SA",
    "fa",
    "ko",
    "bn",
    "he",
    "th",
    "ru",
    "pl",
)

# Plural categories in enum order (robo_i18n_plural_category_t).
PLURAL_CATS = ("zero", "one", "two", "few", "many", "other")

# Compact plural-name coverage: currencies exercised by tests + common ISO.
PLURAL_CODES = (
    "USD",
    "EUR",
    "GBP",
    "JPY",
    "CNY",
    "INR",
    "PKR",
    "SAR",
    "AED",
    "IRR",
    "KRW",
    "BDT",
    "CHF",
    "KWD",
    "BHD",
    "OMR",
)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


class Pool:
    def __init__(self) -> None:
        self._s2i: dict[str, int] = {}
        self._strs: list[str] = []

    def add(self, s: str | None) -> int:
        if s is None:
            return -1
        if s in self._s2i:
            return self._s2i[s]
        i = len(self._strs)
        self._s2i[s] = i
        self._strs.append(s)
        return i

    def c_blob(self) -> tuple[str, list[int]]:
        blob = bytearray()
        offsets: list[int] = []
        for s in self._strs:
            offsets.append(len(blob))
            enc = s.encode("utf-8")
            blob.extend(enc)
            blob.append(0)
        # Emit as escaped C string literal chunks.
        parts: list[str] = []
        line: list[str] = []
        col = 0
        for b in blob:
            if 32 <= b < 127 and b not in (ord("\\"), ord('"')):
                ch = chr(b)
            else:
                ch = f"\\{b:03o}"
            line.append(ch)
            col += len(ch)
            if col >= 72:
                parts.append('"' + "".join(line) + '"')
                line = []
                col = 0
        if line:
            parts.append('"' + "".join(line) + '"')
        if not parts:
            parts = ['""']
        return "\n".join(parts), offsets


def parse_int(s: str | None, default: int) -> int:
    if s is None or s == "":
        return default
    return int(s)


def is_currency_code(code: str) -> bool:
    return bool(re.fullmatch(r"[A-Z]{3}", code))


def current_region_currency(entries: list) -> tuple[str | None, int]:
    """Return (code, flags) for the active tender currency of a region list."""
    # flags: bit0 historical-seen, bit1 non-tender-only
    best: str | None = None
    best_from = ""
    for item in entries:
        if not isinstance(item, dict) or len(item) != 1:
            continue
        code, meta = next(iter(item.items()))
        if not is_currency_code(code):
            continue
        if not isinstance(meta, dict):
            meta = {}
        if meta.get("_tender") == "false":
            continue
        if "_to" in meta:
            continue
        fr = meta.get("_from", "")
        if best is None or fr >= best_from:
            best = code
            best_from = fr
    return best, 0


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def main() -> None:
    curr_path = SUPP / "currencyData.json"
    map_path = SUPP / "codeMappings.json"
    if not curr_path.is_file():
        raise SystemExit(f"missing {curr_path}")
    if not map_path.is_file():
        raise SystemExit(f"missing {map_path}")

    cd = load_json(curr_path)["supplemental"]["currencyData"]
    fractions = cd["fractions"]
    regions = cd["region"]
    mappings = load_json(map_path)["supplemental"]["codeMappings"]

    default_frac = fractions.get("DEFAULT", {"_digits": "2", "_rounding": "0"})
    def_digits = parse_int(default_frac.get("_digits"), 2)
    def_rounding = parse_int(default_frac.get("_rounding"), 0)

    # Collect all currency codes from fractions + regions + numeric mappings.
    codes: set[str] = set()
    for code in fractions:
        if code != "DEFAULT" and is_currency_code(code):
            codes.add(code)
    for reg, entries in regions.items():
        for item in entries:
            if isinstance(item, dict):
                for code in item:
                    if is_currency_code(code):
                        codes.add(code)
    for code, meta in mappings.items():
        if (
            is_currency_code(code)
            and isinstance(meta, dict)
            and "_numeric" in meta
            and "_alpha3" not in meta
        ):
            codes.add(code)

    # Status: current if any region lists it without _to and tender!=false
    current: set[str] = set()
    historical: set[str] = set()
    nontender: set[str] = set()
    for reg, entries in regions.items():
        for item in entries:
            if not isinstance(item, dict) or len(item) != 1:
                continue
            code, meta = next(iter(item.items()))
            if not is_currency_code(code):
                continue
            if not isinstance(meta, dict):
                meta = {}
            if meta.get("_tender") == "false":
                nontender.add(code)
            elif "_to" in meta:
                historical.add(code)
            else:
                current.add(code)

    pool = Pool()

    # --- currency meta rows ---
    meta_rows: list[tuple] = []
    for code in sorted(codes):
        frac = fractions.get(code, {})
        digits = parse_int(frac.get("_digits"), def_digits) if code in fractions else def_digits
        rounding = (
            parse_int(frac.get("_rounding"), def_rounding) if code in fractions else def_rounding
        )
        cash_digits = -1
        cash_rounding = -1
        if "_cashDigits" in frac:
            cash_digits = parse_int(frac["_cashDigits"], digits)
        if "_cashRounding" in frac:
            cash_rounding = parse_int(frac["_cashRounding"], 0)

        numeric = -1
        m = mappings.get(code)
        if isinstance(m, dict) and "_numeric" in m and "_alpha3" not in m:
            numeric = parse_int(m["_numeric"], -1)

        flags = 0
        if code in current:
            flags |= 1  # ROBO_CURR_FLAG_CURRENT
        if code in historical and code not in current:
            flags |= 2  # ROBO_CURR_FLAG_HISTORICAL
        if code in nontender and code not in current:
            flags |= 4  # ROBO_CURR_FLAG_NON_TENDER
        if code in fractions or numeric >= 0:
            flags |= 8  # ROBO_CURR_FLAG_KNOWN

        # Document: codes only in region lists without fraction override inherit DEFAULT.
        meta_rows.append(
            (
                pool.add(code),
                numeric,
                digits,
                rounding,
                cash_digits,
                cash_rounding,
                flags,
            )
        )

    # Validation: unique codes, digits range
    seen_codes: set[int] = set()
    for row in meta_rows:
        if row[0] in seen_codes:
            raise SystemExit("duplicate currency code in meta")
        seen_codes.add(row[0])
        if not (0 <= row[2] <= 20):
            raise SystemExit(f"digits out of range for pool idx {row[0]}")

    # --- region default currency ---
    region_rows: list[tuple[int, int]] = []
    for reg in sorted(regions.keys()):
        if not re.fullmatch(r"[A-Z]{2}|[0-9]{3}", reg):
            continue
        cur, _ = current_region_currency(regions[reg])
        if not cur:
            continue
        region_rows.append((pool.add(reg), pool.add(cur)))

    # --- format patterns ---
    fmt_rows: list[tuple] = []
    for loc in CURATED_LOCALES:
        path = NUM / f"{loc}.json"
        if not path.is_file():
            # root may be absent; und often present
            continue
        data = load_json(path)
        main = data.get("main", {}).get(loc, {}).get("numbers", {})
        if not main:
            continue
        for key, block in main.items():
            if not key.startswith("currencyFormats-numberSystem-"):
                continue
            if not isinstance(block, dict):
                continue
            nsys = key[len("currencyFormats-numberSystem-") :]
            standard = block.get("standard")
            if not isinstance(standard, str) or not standard:
                continue
            accounting = block.get("accounting")
            if not isinstance(accounting, str) or not accounting:
                accounting = standard
            std_alpha = block.get("standard-alphaNextToNumber")
            acc_alpha = block.get("accounting-alphaNextToNumber")
            if not isinstance(std_alpha, str):
                std_alpha = ""
            if not isinstance(acc_alpha, str):
                acc_alpha = ""

            spacing = block.get("currencySpacing", {})
            before = ""
            after = ""
            if isinstance(spacing, dict):
                bc = spacing.get("beforeCurrency", {})
                ac = spacing.get("afterCurrency", {})
                if isinstance(bc, dict) and isinstance(bc.get("insertBetween"), str):
                    before = bc["insertBetween"]
                if isinstance(ac, dict) and isinstance(ac.get("insertBetween"), str):
                    after = ac["insertBetween"]

            unit_pats = [-1] * 6
            for i, cat in enumerate(PLURAL_CATS):
                up = block.get(f"unitPattern-count-{cat}")
                if isinstance(up, str) and up:
                    unit_pats[i] = pool.add(up)
            # Ensure OTHER exists: default "{0} {1}"
            if unit_pats[5] < 0:
                unit_pats[5] = pool.add("{0} {1}")

            fmt_rows.append(
                (
                    pool.add(loc),
                    pool.add(nsys),
                    pool.add(standard),
                    pool.add(accounting),
                    pool.add(std_alpha) if std_alpha else -1,
                    pool.add(acc_alpha) if acc_alpha else -1,
                    pool.add(before) if before else -1,
                    pool.add(after) if after else -1,
                    tuple(unit_pats),
                )
            )

    fmt_rows.sort(key=lambda r: (r[0], r[1]))
    seen_fmt: set[tuple[int, int]] = set()
    for row in fmt_rows:
        key = (row[0], row[1])
        if key in seen_fmt:
            raise SystemExit("duplicate currency format locale/nsys")
        seen_fmt.add(key)

    # --- plural currency names (NAME style) ---
    plural_rows: list[tuple] = []
    for loc in CURATED_LOCALES:
        path = NUM / loc / "currencies.json"
        if not path.is_file():
            continue
        data = load_json(path)
        currencies = (
            data.get("main", {}).get(loc, {}).get("numbers", {}).get("currencies", {})
        )
        if not isinstance(currencies, dict):
            continue
        for code in PLURAL_CODES:
            entry = currencies.get(code)
            if not isinstance(entry, dict):
                continue
            names = [-1] * 6
            base = entry.get("displayName")
            if isinstance(base, str) and base:
                names[5] = pool.add(base)
            for i, cat in enumerate(PLURAL_CATS):
                key = f"displayName-count-{cat}"
                val = entry.get(key)
                if isinstance(val, str) and val:
                    names[i] = pool.add(val)
            if all(n < 0 for n in names):
                continue
            # Fill missing from OTHER or base
            fill = names[5]
            for i in range(6):
                if names[i] < 0:
                    names[i] = fill
            plural_rows.append((pool.add(loc), pool.add(code), tuple(names)))

    plural_rows.sort(key=lambda r: (r[0], r[1]))

    blob, offsets = pool.c_blob()

    # --- emit header ---
    h_lines = [
        f"/* Auto-generated by generate_currency.py — DO NOT EDIT.",
        f" * CLDR {CLDR_VERSION} currency data. Generator {GENERATOR_VERSION}.",
        f" * API {ROBO_I18N_VERSION}. Unicode association: {CLDR_UNICODE_VERSION}.",
        f" * Sources: currencyData.json, codeMappings.json, currencyFormats, currencies.json.",
        f" * Discrepancy note: ISO numeric codes from CLDR codeMappings; fraction digits",
        f" * from currencyData fractions (DEFAULT inherited when a code has no override).",
        f" */",
        "#ifndef ROBO_CURRENCY_DATA_H",
        "#define ROBO_CURRENCY_DATA_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        f'#define ROBO_CURRENCY_CLDR_VERSION "{CLDR_VERSION}"',
        f'#define ROBO_CURRENCY_GENERATOR_VERSION "{GENERATOR_VERSION}"',
        "",
        "/* meta.flags */",
        "#define ROBO_CURR_FLAG_CURRENT 1u",
        "#define ROBO_CURR_FLAG_HISTORICAL 2u",
        "#define ROBO_CURR_FLAG_NON_TENDER 4u",
        "#define ROBO_CURR_FLAG_KNOWN 8u",
        "",
        "typedef struct robo_curr_meta {",
        "    int32_t code;          /* string pool: ISO 4217 alpha */",
        "    int16_t numeric;       /* ISO numeric, or -1 */",
        "    uint8_t digits;        /* standard fraction digits */",
        "    uint16_t rounding;     /* standard rounding increment (0 = unit) */",
        "    int8_t cash_digits;    /* -1 = same as digits */",
        "    int16_t cash_rounding; /* -1 = same as rounding */",
        "    uint8_t flags;",
        "} robo_curr_meta_t;",
        "",
        "typedef struct robo_curr_region {",
        "    int32_t region;   /* string pool */",
        "    int32_t currency; /* string pool ISO code */",
        "} robo_curr_region_t;",
        "",
        "typedef struct robo_curr_fmt {",
        "    int32_t locale;",
        "    int32_t nsys;",
        "    int32_t standard;",
        "    int32_t accounting;",
        "    int32_t standard_alpha;    /* -1 if absent */",
        "    int32_t accounting_alpha;  /* -1 if absent */",
        "    int32_t space_before;      /* insertBetween before currency */",
        "    int32_t space_after;",
        "    int32_t unit_pattern[6];   /* plural categories zero..other */",
        "} robo_curr_fmt_t;",
        "",
        "typedef struct robo_curr_plural_name {",
        "    int32_t locale;",
        "    int32_t code;",
        "    int32_t names[6]; /* plural categories; -1 absent */",
        "} robo_curr_plural_name_t;",
        "",
        "extern const char robo_curr_string_pool[];",
        "extern const uint32_t robo_curr_string_pool_size;",
        "extern const int32_t robo_curr_string_offsets[];",
        "extern const uint32_t robo_curr_string_count;",
        "",
        "extern const robo_curr_meta_t robo_curr_meta[];",
        "extern const uint32_t robo_curr_meta_count;",
        "extern const robo_curr_region_t robo_curr_regions[];",
        "extern const uint32_t robo_curr_region_count;",
        "extern const robo_curr_fmt_t robo_curr_fmts[];",
        "extern const uint32_t robo_curr_fmt_count;",
        "extern const robo_curr_plural_name_t robo_curr_plural_names[];",
        "extern const uint32_t robo_curr_plural_name_count;",
        "",
        "const char *robo_curr_cldr_version_string(void);",
        "const char *robo_curr_string(int32_t idx);",
        "const robo_curr_meta_t *robo_curr_meta_find(const char *code, size_t len);",
        "const robo_curr_region_t *robo_curr_region_find(const char *region, size_t len);",
        "const robo_curr_fmt_t *robo_curr_fmt_find(const char *locale, size_t loc_len,",
        "                                         const char *nsys, size_t nsys_len);",
        "const robo_curr_plural_name_t *robo_curr_plural_name_find(",
        "    const char *locale, size_t loc_len, const char *code, size_t code_len);",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif /* ROBO_CURRENCY_DATA_H */",
        "",
    ]
    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text("\n".join(h_lines), encoding="utf-8", newline="\n")

    def emit_meta_row(r: tuple) -> str:
        return (
            f"    {{{r[0]}, {r[1]}, {r[2]}u, {r[3]}u, {r[4]}, {r[5]}, {r[6]}u}}"
        )

    def emit_fmt_row(r: tuple) -> str:
        up = ", ".join(str(x) for x in r[8])
        return (
            f"    {{{r[0]}, {r[1]}, {r[2]}, {r[3]}, {r[4]}, {r[5]}, "
            f"{r[6]}, {r[7]}, {{{up}}}}}"
        )

    def emit_plural_row(r: tuple) -> str:
        names = ", ".join(str(x) for x in r[2])
        return f"    {{{r[0]}, {r[1]}, {{{names}}}}}"

    c_lines = [
        f"/* Auto-generated by generate_currency.py — DO NOT EDIT.",
        f" * CLDR {CLDR_VERSION}. Generator {GENERATOR_VERSION}.",
        f" */",
        '#include "RoboI18N/RoboCurrencyData.h"',
        "",
        "#include <string.h>",
        "",
        "const char robo_curr_string_pool[] =",
        blob + ";",
        f"const uint32_t robo_curr_string_pool_size = {sum(len(s.encode('utf-8')) + 1 for s in pool._strs)}u;",
        "",
        "const int32_t robo_curr_string_offsets[] = {",
        ",\n".join(f"    {o}" for o in offsets) + ("," if offsets else ""),
        "};",
        f"const uint32_t robo_curr_string_count = {len(offsets)}u;",
        "",
        f"const robo_curr_meta_t robo_curr_meta[{max(len(meta_rows), 1)}] = {{",
        ",\n".join(emit_meta_row(r) for r in meta_rows) + ("," if meta_rows else ""),
        "};",
        f"const uint32_t robo_curr_meta_count = {len(meta_rows)}u;",
        "",
        f"const robo_curr_region_t robo_curr_regions[{max(len(region_rows), 1)}] = {{",
        ",\n".join(f"    {{{a}, {b}}}" for a, b in region_rows)
        + ("," if region_rows else ""),
        "};",
        f"const uint32_t robo_curr_region_count = {len(region_rows)}u;",
        "",
        f"const robo_curr_fmt_t robo_curr_fmts[{max(len(fmt_rows), 1)}] = {{",
        ",\n".join(emit_fmt_row(r) for r in fmt_rows) + ("," if fmt_rows else ""),
        "};",
        f"const uint32_t robo_curr_fmt_count = {len(fmt_rows)}u;",
        "",
        f"const robo_curr_plural_name_t robo_curr_plural_names[{max(len(plural_rows), 1)}] = {{",
        ",\n".join(emit_plural_row(r) for r in plural_rows)
        + ("," if plural_rows else ""),
        "};",
        f"const uint32_t robo_curr_plural_name_count = {len(plural_rows)}u;",
        "",
        "const char *robo_curr_cldr_version_string(void)",
        "{",
        f'    return "{CLDR_VERSION}";',
        "}",
        "",
        "const char *robo_curr_string(int32_t idx)",
        "{",
        "    if (idx < 0 || (uint32_t)idx >= robo_curr_string_count) {",
        "        return NULL;",
        "    }",
        "    return robo_curr_string_pool + robo_curr_string_offsets[idx];",
        "}",
        "",
        "static int pool_eq(int32_t idx, const char *s, size_t len)",
        "{",
        "    const char *p;",
        "    size_t n;",
        "    p = robo_curr_string(idx);",
        "    if (!p || !s) {",
        "        return 0;",
        "    }",
        "    n = strlen(p);",
        "    return n == len && memcmp(p, s, len) == 0;",
        "}",
        "",
        "const robo_curr_meta_t *robo_curr_meta_find(const char *code, size_t len)",
        "{",
        "    uint32_t lo = 0u, hi = robo_curr_meta_count;",
        "    if (!code || len == 0u) {",
        "        return NULL;",
        "    }",
        "    while (lo < hi) {",
        "        uint32_t mid = lo + (hi - lo) / 2u;",
        "        const char *p = robo_curr_string(robo_curr_meta[mid].code);",
        "        size_t n = p ? strlen(p) : 0u;",
        "        int cmp;",
        "        if (n == len) {",
        "            cmp = memcmp(p, code, len);",
        "        } else if (n < len) {",
        "            cmp = memcmp(p, code, n);",
        "            if (cmp == 0) {",
        "                cmp = -1;",
        "            }",
        "        } else {",
        "            cmp = memcmp(p, code, len);",
        "            if (cmp == 0) {",
        "                cmp = 1;",
        "            }",
        "        }",
        "        if (cmp == 0) {",
        "            return &robo_curr_meta[mid];",
        "        }",
        "        if (cmp < 0) {",
        "            lo = mid + 1u;",
        "        } else {",
        "            hi = mid;",
        "        }",
        "    }",
        "    return NULL;",
        "}",
        "",
        "const robo_curr_region_t *robo_curr_region_find(const char *region, size_t len)",
        "{",
        "    uint32_t lo = 0u, hi = robo_curr_region_count;",
        "    if (!region || len == 0u) {",
        "        return NULL;",
        "    }",
        "    while (lo < hi) {",
        "        uint32_t mid = lo + (hi - lo) / 2u;",
        "        const char *p = robo_curr_string(robo_curr_regions[mid].region);",
        "        size_t n = p ? strlen(p) : 0u;",
        "        int cmp;",
        "        if (n == len) {",
        "            cmp = memcmp(p, region, len);",
        "        } else if (n < len) {",
        "            cmp = memcmp(p, region, n);",
        "            if (cmp == 0) {",
        "                cmp = -1;",
        "            }",
        "        } else {",
        "            cmp = memcmp(p, region, len);",
        "            if (cmp == 0) {",
        "                cmp = 1;",
        "            }",
        "        }",
        "        if (cmp == 0) {",
        "            return &robo_curr_regions[mid];",
        "        }",
        "        if (cmp < 0) {",
        "            lo = mid + 1u;",
        "        } else {",
        "            hi = mid;",
        "        }",
        "    }",
        "    return NULL;",
        "}",
        "",
        "const robo_curr_fmt_t *robo_curr_fmt_find(const char *locale, size_t loc_len,",
        "                                         const char *nsys, size_t nsys_len)",
        "{",
        "    uint32_t i;",
        "    if (!locale || loc_len == 0u || !nsys || nsys_len == 0u) {",
        "        return NULL;",
        "    }",
        "    for (i = 0u; i < robo_curr_fmt_count; i++) {",
        "        if (pool_eq(robo_curr_fmts[i].locale, locale, loc_len) &&",
        "            pool_eq(robo_curr_fmts[i].nsys, nsys, nsys_len)) {",
        "            return &robo_curr_fmts[i];",
        "        }",
        "    }",
        "    return NULL;",
        "}",
        "",
        "const robo_curr_plural_name_t *robo_curr_plural_name_find(",
        "    const char *locale, size_t loc_len, const char *code, size_t code_len)",
        "{",
        "    uint32_t i;",
        "    if (!locale || loc_len == 0u || !code || code_len == 0u) {",
        "        return NULL;",
        "    }",
        "    for (i = 0u; i < robo_curr_plural_name_count; i++) {",
        "        if (pool_eq(robo_curr_plural_names[i].locale, locale, loc_len) &&",
        "            pool_eq(robo_curr_plural_names[i].code, code, code_len)) {",
        "            return &robo_curr_plural_names[i];",
        "        }",
        "    }",
        "    return NULL;",
        "}",
        "",
    ]

    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.write_text("\n".join(c_lines), encoding="utf-8", newline="\n")

    golden = {
        "cldr": CLDR_VERSION,
        "generator": GENERATOR_VERSION,
        "api": ROBO_I18N_VERSION,
        "currencyData_sha256": sha256_file(curr_path),
        "codeMappings_sha256": sha256_file(map_path),
        "meta_count": len(meta_rows),
        "region_count": len(region_rows),
        "fmt_count": len(fmt_rows),
        "plural_name_count": len(plural_rows),
        "string_count": len(offsets),
        "header_sha256": sha256_file(OUT_H),
        "source_sha256": sha256_file(OUT_C),
    }
    GOLDEN.write_text(json.dumps(golden, indent=2) + "\n", encoding="utf-8")

    print(
        f"OK currency: meta={len(meta_rows)} regions={len(region_rows)} "
        f"fmts={len(fmt_rows)} plural_names={len(plural_rows)} strings={len(offsets)}"
    )


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Compile RoboMsg application catalogs (Goal 4.11).

Source: Tools/i18n/messages/<catalog>/*.robomsg.json
Outputs:
  Include/RoboI18N/RoboMsgData.h
  Source/RoboI18N/data/robo_msg_data.c
  Tools/i18n/messages_golden_hashes.json
"""

from __future__ import annotations

import hashlib
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from robo_i18n_version import GENERATOR_VERSION, ROBO_I18N_VERSION

HERE = Path(__file__).resolve().parent
RUNTIME = HERE.parent.parent
SRC_ROOT = HERE / "messages"
OUT_H = RUNTIME / "Include" / "RoboI18N" / "RoboMsgData.h"
OUT_C = RUNTIME / "Source" / "RoboI18N" / "data" / "robo_msg_data.c"
GOLDEN = HERE / "messages_golden_hashes.json"

FORMAT_VERSION = 1
MAGIC = 0x47534D52  # 'RMSG' LE
ID_RE = re.compile(r"^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)*$")
PH_RE = re.compile(r"\{([A-Za-z_][A-Za-z0-9_]*)\}|(\{\{)|(\}\})")
PLURAL_KEYS = ("zero", "one", "two", "few", "many", "other")
CATALOG_ID = "robo.demo"


class Err(Exception):
    pass


def check_utf8(s: str, where: str) -> None:
    try:
        s.encode("utf-8")
    except UnicodeEncodeError as e:
        raise Err(f"{where}: invalid unicode: {e}") from e


def extract_placeholders(text: str, where: str) -> set[str]:
    names: set[str] = set()
    i = 0
    while i < len(text):
        if text[i] == "{":
            if i + 1 < len(text) and text[i + 1] == "{":
                i += 2
                continue
            j = text.find("}", i + 1)
            if j < 0:
                raise Err(f"{where}: unclosed '{{'")
            name = text[i + 1 : j]
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
                raise Err(f"{where}: invalid placeholder '{{{name}}}'")
            names.add(name)
            i = j + 1
        elif text[i] == "}":
            if i + 1 < len(text) and text[i + 1] == "}":
                i += 2
                continue
            raise Err(f"{where}: stray '}}'")
        else:
            i += 1
    return names


def validate_id(mid: str, where: str) -> None:
    if not mid or len(mid) > 128:
        raise Err(f"{where}: message id length")
    if not ID_RE.fullmatch(mid):
        raise Err(f"{where}: invalid message id '{mid}'")


def load_locale_file(path: Path) -> dict:
    raw = path.read_bytes()
    try:
        raw.decode("utf-8")
    except UnicodeDecodeError as e:
        raise Err(f"{path}: malformed UTF-8: {e}") from e
    data = json.loads(raw.decode("utf-8"))
    if not isinstance(data, dict):
        raise Err(f"{path}: root must be object")
    for key in ("format_version", "catalog_id", "catalog_version", "locale", "messages"):
        if key not in data:
            raise Err(f"{path}: missing '{key}'")
    if data["format_version"] != FORMAT_VERSION:
        raise Err(f"{path}: unsupported format_version {data['format_version']}")
    if data["catalog_id"] != CATALOG_ID:
        raise Err(f"{path}: expected catalog_id {CATALOG_ID}")
    if not isinstance(data["messages"], list):
        raise Err(f"{path}: messages must be array")
    return data


def main() -> int:
    write_golden = "--write-golden" in sys.argv
    demo = SRC_ROOT / "demo"
    if not demo.is_dir():
        print(f"missing {demo}", file=sys.stderr)
        return 1

    files = sorted(demo.glob("*.robomsg.json"))
    if not files:
        print("no .robomsg.json files", file=sys.stderr)
        return 1

    catalog_version = None
    # key: (id, context) -> {locale: entry}
    by_key: dict[tuple[str, str], dict[str, dict]] = {}
    locales: list[str] = []

    for path in files:
        data = load_locale_file(path)
        loc = data["locale"]
        check_utf8(loc, f"{path}:locale")
        if catalog_version is None:
            catalog_version = data["catalog_version"]
        elif data["catalog_version"] != catalog_version:
            raise Err(f"{path}: catalog_version mismatch")
        locales.append(loc)
        seen_local: set[tuple[str, str]] = set()
        for idx, msg in enumerate(data["messages"]):
            where = f"{path}:messages[{idx}]"
            if not isinstance(msg, dict):
                raise Err(f"{where}: not an object")
            mid = msg.get("id")
            if not isinstance(mid, str):
                raise Err(f"{where}: id required")
            validate_id(mid, where)
            ctx = msg.get("context") or ""
            if not isinstance(ctx, str):
                raise Err(f"{where}: context must be string")
            check_utf8(ctx, where)
            key = (mid, ctx)
            if key in seen_local:
                raise Err(f"{where}: duplicate id+context '{mid}'/'{ctx}'")
            seen_local.add(key)
            has_text = "text" in msg and msg["text"] is not None
            has_plural = "plural" in msg and msg["plural"] is not None
            if has_text == has_plural:
                raise Err(f"{where}: need exactly one of text|plural")
            ph: set[str] = set()
            entry: dict = {"id": mid, "context": ctx}
            if has_text:
                text = msg["text"]
                if not isinstance(text, str):
                    raise Err(f"{where}: text must be string")
                check_utf8(text, where)
                ph = extract_placeholders(text, where)
                entry["text"] = text
                entry["plural"] = None
            else:
                pl = msg["plural"]
                if not isinstance(pl, dict):
                    raise Err(f"{where}: plural must be object")
                if "other" not in pl or not isinstance(pl["other"], str):
                    raise Err(f"{where}: plural.other required")
                entry["text"] = None
                entry["plural"] = {}
                for k, v in pl.items():
                    if k not in PLURAL_KEYS:
                        raise Err(f"{where}: unsupported plural category '{k}'")
                    if not isinstance(v, str):
                        raise Err(f"{where}: plural.{k} must be string")
                    check_utf8(v, where)
                    entry["plural"][k] = v
                    ph |= extract_placeholders(v, f"{where}.plural.{k}")
            entry["placeholders"] = sorted(ph)
            by_key.setdefault(key, {})[loc] = entry

    # Cross-locale placeholder consistency (vs first locale that defines the key)
    for key, locmap in by_key.items():
        ref_ph = None
        ref_loc = None
        for loc in sorted(locmap.keys()):
            ph = set(locmap[loc]["placeholders"])
            if ref_ph is None:
                ref_ph = ph
                ref_loc = loc
            elif ph != ref_ph:
                raise Err(
                    f"placeholder mismatch for {key[0]!r}/{key[1]!r}: "
                    f"{ref_loc}={sorted(ref_ph)} vs {loc}={sorted(ph)}"
                )
            # plural vs text kind consistency
            is_pl = locmap[loc]["plural"] is not None
            ref_pl = next(iter(locmap.values()))["plural"] is not None
            if is_pl != ref_pl:
                raise Err(f"text/plural kind mismatch for {key}")

    locales = sorted(set(locales))

    # String pool
    pool: list[str] = []
    index: dict[str, int] = {}

    def intern(s: str) -> int:
        if s not in index:
            index[s] = len(pool)
            pool.append(s)
        return index[s]

    catalog_id_i = intern(CATALOG_ID)
    catalog_ver_i = intern(catalog_version or "0")
    gen_i = intern(GENERATOR_VERSION)

    rows: list[tuple] = []
    # (locale_i, id_i, ctx_i, text_i_or_-1, plural[6])
    for (mid, ctx), locmap in sorted(by_key.items(), key=lambda x: (x[0][0], x[0][1])):
        id_i = intern(mid)
        ctx_i = intern(ctx) if ctx else -1
        for loc in sorted(locmap.keys()):
            e = locmap[loc]
            loc_i = intern(loc)
            if e["text"] is not None:
                text_i = intern(e["text"])
                pl = [-1] * 6
            else:
                text_i = -1
                pl = [-1] * 6
                for ci, name in enumerate(PLURAL_KEYS):
                    if name in e["plural"]:
                        pl[ci] = intern(e["plural"][name])
            rows.append((loc_i, id_i, ctx_i, text_i, pl))

    rows.sort(
        key=lambda r: (
            pool[r[0]],
            pool[r[1]],
            "" if r[2] < 0 else pool[r[2]],
        )
    )

    # Binary blob
    # Header: magic u32, format u32, catalog_id_off u32, catalog_ver_off u32,
    #         gen_off u32, locale_count u32, entry_count u32, pool_size u32
    # Then locale tags as (off,len) — skip, use entries only
    # Entries: loc_off, id_off, ctx_off (-1), text_off (-1), pl[6] offs as i32
    # Pool: concatenated NUL-terminated UTF-8

    blob_pool = bytearray()
    offs: list[int] = []
    for s in pool:
        offs.append(len(blob_pool))
        blob_pool.extend(s.encode("utf-8"))
        blob_pool.append(0)

    def pool_off(idx: int) -> int:
        if idx < 0:
            return 0xFFFFFFFF
        return offs[idx]

    entries_bin = bytearray()
    for loc_i, id_i, ctx_i, text_i, pl in rows:
        entries_bin.extend(struct.pack("<iiii", pool_off(loc_i), pool_off(id_i),
                                       pool_off(ctx_i) if ctx_i >= 0 else -1,
                                       pool_off(text_i) if text_i >= 0 else -1))
        for p in pl:
            entries_bin.extend(struct.pack("<i", pool_off(p) if p >= 0 else -1))

    header = struct.pack(
        "<IIIIIIII",
        MAGIC,
        FORMAT_VERSION,
        pool_off(catalog_id_i),
        pool_off(catalog_ver_i),
        pool_off(gen_i),
        len(locales),
        len(rows),
        len(blob_pool),
    )
    # layout: header (32) + entries + pool
    blob = header + entries_bin + blob_pool
    entry_bytes = len(rows) * (4 * 4 + 6 * 4)  # 40 bytes per entry
    assert len(entries_bin) == entry_bytes

    # Emit C
    def c_escape_blob(data: bytes) -> str:
        lines = []
        for i in range(0, len(data), 16):
            chunk = ", ".join(f"0x{b:02x}" for b in data[i : i + 16])
            lines.append(f"    {chunk},")
        return "\n".join(lines)

    pool_c_parts = []
    for s in pool:
        raw = s.encode("utf-8")
        esc = "".join(f"\\x{b:02x}" for b in raw)
        pool_c_parts.append(f'    "{esc}\\0"')

    h = f"""/* Auto-generated by generate_messages.py — DO NOT EDIT.
 * RoboMsg format {FORMAT_VERSION}. Generator {GENERATOR_VERSION}. API {ROBO_I18N_VERSION}.
 */
#ifndef ROBO_MSG_DATA_H
#define ROBO_MSG_DATA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define ROBO_MSG_DEMO_CATALOG_ID "{CATALOG_ID}"
#define ROBO_MSG_DEMO_FORMAT_VERSION {FORMAT_VERSION}u

typedef struct robo_msg_entry {{
    int32_t locale;   /* string pool index */
    int32_t id;       /* string pool index */
    int32_t context;  /* -1 if empty */
    int32_t text;     /* -1 if plural message */
    int32_t plural[6]; /* zero..other; -1 absent */
}} robo_msg_entry_t;

extern const char robo_msg_string_pool[];
extern const uint32_t robo_msg_string_pool_size;
extern const int32_t robo_msg_string_offsets[];
extern const uint32_t robo_msg_string_count;

extern const robo_msg_entry_t robo_msg_entries[];
extern const uint32_t robo_msg_entry_count;

extern const char *robo_msg_catalog_id(void);
extern const char *robo_msg_catalog_version(void);
extern const char *robo_msg_generator_version(void);

extern const uint8_t robo_msg_demo_blob[];
extern const uint32_t robo_msg_demo_blob_size;

const char *robo_msg_string(int32_t idx);

#ifdef __cplusplus
}}
#endif

#endif /* ROBO_MSG_DATA_H */
"""

    c = f"""/* Auto-generated by generate_messages.py — DO NOT EDIT. */
#include "RoboI18N/RoboMsgData.h"

const char robo_msg_string_pool[] =
{chr(10).join(pool_c_parts)};

const uint32_t robo_msg_string_pool_size = {len(blob_pool)}u;

const int32_t robo_msg_string_offsets[{len(offs)}] = {{
{', '.join(str(o) for o in offs)}
}};

const uint32_t robo_msg_string_count = {len(pool)}u;

const robo_msg_entry_t robo_msg_entries[{len(rows)}] = {{
"""
    for loc_i, id_i, ctx_i, text_i, pl in rows:
        pl_s = ", ".join(str(p) for p in pl)
        c += f"    {{{loc_i}, {id_i}, {ctx_i}, {text_i}, {{{pl_s}}}}},\n"
    c += f"""}};

const uint32_t robo_msg_entry_count = {len(rows)}u;

const char *robo_msg_catalog_id(void) {{ return "{CATALOG_ID}"; }}
const char *robo_msg_catalog_version(void) {{ return "{catalog_version}"; }}
const char *robo_msg_generator_version(void) {{ return "{GENERATOR_VERSION}"; }}

const uint8_t robo_msg_demo_blob[{len(blob)}] = {{
{c_escape_blob(blob)}
}};
const uint32_t robo_msg_demo_blob_size = {len(blob)}u;

const char *robo_msg_string(int32_t idx)
{{
    if (idx < 0 || (uint32_t)idx >= robo_msg_string_count) {{
        return NULL;
    }}
    return robo_msg_string_pool + robo_msg_string_offsets[idx];
}}
"""

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(h, encoding="utf-8", newline="\n")
    OUT_C.write_text(c, encoding="utf-8", newline="\n")

    digest = {
        "generator_version": GENERATOR_VERSION,
        "robo_i18n_version": ROBO_I18N_VERSION,
        "format_version": FORMAT_VERSION,
        "catalog_id": CATALOG_ID,
        "catalog_version": catalog_version,
        "locales": locales,
        "entry_count": len(rows),
        "sha256_h": hashlib.sha256(h.encode("utf-8")).hexdigest(),
        "sha256_c": hashlib.sha256(c.encode("utf-8")).hexdigest(),
        "sha256_blob": hashlib.sha256(blob).hexdigest(),
    }
    if write_golden:
        GOLDEN.write_text(json.dumps(digest, indent=2) + "\n", encoding="utf-8")
        print(f"wrote golden {GOLDEN}")
    elif GOLDEN.is_file():
        prev = json.loads(GOLDEN.read_text(encoding="utf-8"))
        for k in ("sha256_h", "sha256_c", "sha256_blob", "entry_count"):
            if prev.get(k) != digest.get(k):
                print(f"GOLDEN MISMATCH {k}: {prev.get(k)} vs {digest.get(k)}", file=sys.stderr)
                return 2
        print("golden OK")
    print(f"locales={len(locales)} entries={len(rows)} blob={len(blob)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Err as e:
        print(f"error: {e}", file=sys.stderr)
        raise SystemExit(1)

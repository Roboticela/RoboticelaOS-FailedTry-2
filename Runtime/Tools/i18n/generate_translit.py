#!/usr/bin/env python3
"""
Compile curated CLDR transform XML into compact RoboTranslitData tables (Goal 4.25).

Supported LDML transform subset (validated at generate time):
  - ::NFD|NFC|NFKD|NFKC (optional reverse form in parentheses)
  - ::Null
  - ::Lower / ::(Lower)
  - ::Filter UnicodeSet (recorded; rules still apply by key match)
  - ::OtherID;  (inlined when available in the transform catalog)
  - $var = UnicodeSet-or-string ;
  - conversion rules:  before{key}after →/←/↔ replacement ;
  - UnicodeSet: literals, ranges, [:Ll:], [:Lu:], [:L:], [:M:], [:Mn:], [:Me:],
    [:Nd:], [:N:], [:Uppercase:], [:Lowercase:], [:script=Name:], [:Script:],
    [:ccc=N:], [:^ccc=N:], [:block=NAME:], unions, &, -, ^, nested []

Unsupported (rules skipped with warning — do not crash generation):
  - cursor '|' and segment captures ($1..)
  - function transforms other than Lower (e.g. fullwidth-halfwidth)
  - arbitrary regex beyond UnicodeSet classes

Raw CLDR XML is never embedded in the runtime binary.
"""

from __future__ import annotations

import hashlib
import re
import sys
import unicodedata
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from robo_i18n_version import CLDR_VERSION, GENERATOR_VERSION

HERE = Path(__file__).resolve().parent
SRC_DIR = HERE / "source" / "cldr-46.0.0" / "transforms"
UCD_SCRIPTS = HERE.parent / "ucd" / "source" / "ucd-16.0.0" / "Scripts.txt"
UCD_BLOCKS = HERE.parent / "ucd" / "source" / "ucd-16.0.0" / "Blocks.txt"
OUT_H = HERE.parent.parent / "Include" / "RoboI18N" / "RoboTranslitData.h"
OUT_C = HERE.parent.parent / "Source" / "RoboI18N" / "data" / "robo_translit_data.c"
GOLDEN = HERE / "translit_golden_hashes.json"

ARROW_FWD = "\u2192"  # →
ARROW_REV = "\u2190"  # ←
ARROW_BOTH = "\u2194"  # ↔

# Public rule-set IDs to emit (canonical). Internal IDs may be pulled via chaining.
PUBLIC_TARGETS: list[dict] = [
    {"id": "Cyrillic-Latin", "file": "Cyrillic-Latin.xml", "dir": "forward"},
    {"id": "Latin-Cyrillic", "file": "Cyrillic-Latin.xml", "dir": "reverse"},
    {"id": "Greek-Latin", "file": "Greek-Latin.xml", "dir": "forward"},
    {"id": "Latin-Greek", "file": "Greek-Latin.xml", "dir": "reverse"},
    {"id": "Arabic-Latin", "file": "Arabic-Latin.xml", "dir": "forward"},
    {"id": "Latin-Arabic", "file": "Arabic-Latin.xml", "dir": "reverse"},
    {"id": "Hebrew-Latin", "file": "Hebrew-Latin.xml", "dir": "forward"},
    {"id": "Latin-Hebrew", "file": "Hebrew-Latin.xml", "dir": "reverse"},
    {"id": "Russian-Latin/BGN", "file": "Russian-Latin-BGN.xml", "dir": "forward"},
    {"id": "Ukrainian-Latin/BGN", "file": "Ukrainian-Latin-BGN.xml", "dir": "forward"},
    {"id": "Latin-ASCII", "file": "Latin-ASCII.xml", "dir": "forward"},
    {"id": "Devanagari-Latin", "file": "Devanagari-Latin.xml", "dir": "forward"},
    {"id": "Bengali-Latin", "file": "Bengali-Latin.xml", "dir": "forward"},
    {"id": "Katakana-Latin", "file": "Latin-Katakana.xml", "dir": "reverse"},
    {"id": "Hiragana-Latin", "file": "Hiragana-Latin.xml", "dir": "forward"},
    {"id": "Hangul-Latin", "file": "Hangul-Latin.xml", "dir": "forward"},
]

SCRIPT_NAME_TO_ROBO = {
    "common": 0,
    "inherited": 1,
    "arabic": 5,
    "armenian": 6,
    "bengali": 12,
    "cyrillic": 30,
    "devanagari": 32,
    "greek": 47,  # will fix from header if needed
    "han": 49,
    "hangul": 50,
    "hebrew": 53,
    "hiragana": 54,
    "katakana": 66,
    "latin": 74,
}


def load_script_map() -> dict[str, list[tuple[int, int]]]:
    """script lowercase name -> list of (start,end) inclusive."""
    out: dict[str, list[tuple[int, int]]] = {}
    if not UCD_SCRIPTS.is_file():
        print(f"WARN missing {UCD_SCRIPTS}; script sets limited", file=sys.stderr)
        return out
    for line in UCD_SCRIPTS.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line or ";" not in line:
            continue
        rng, name = [x.strip() for x in line.split(";", 1)]
        name = name.lower()
        if ".." in rng:
            a, b = rng.split("..")
            start, end = int(a, 16), int(b, 16)
        else:
            start = end = int(rng, 16)
        out.setdefault(name, []).append((start, end))
    return out


def load_block_map() -> dict[str, tuple[int, int]]:
    out: dict[str, tuple[int, int]] = {}
    if not UCD_BLOCKS.is_file():
        return out
    for line in UCD_BLOCKS.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line or ";" not in line:
            continue
        rng, name = [x.strip() for x in line.split(";", 1)]
        a, b = rng.split("..")
        key = name.upper().replace(" ", "_")
        out[key] = (int(a, 16), int(b, 16))
        out[name.upper()] = (int(a, 16), int(b, 16))
    return out


SCRIPTS = load_script_map()
BLOCKS = load_block_map()

# Fix Greek / Hangul / etc enum values from RoboUcdData by reading the header.
def fix_script_enums() -> None:
    h = HERE.parent.parent / "Include" / "RoboUnicode" / "RoboUcdData.h"
    if not h.is_file():
        return
    text = h.read_text(encoding="utf-8")
    for m in re.finditer(r"ROBO_U_SCRIPT_([A-Z0-9_]+)\s*=\s*(\d+)", text):
        name = m.group(1).lower().replace("_", "")
        # map COMMON=common, etc.
        pretty = m.group(1).lower()
        SCRIPT_NAME_TO_ROBO[pretty] = int(m.group(2))
        SCRIPT_NAME_TO_ROBO[pretty.replace("_", "")] = int(m.group(2))


fix_script_enums()


@dataclass
class CpSet:
    """Sorted disjoint inclusive ranges."""

    ranges: list[tuple[int, int]] = field(default_factory=list)

    def normalize(self) -> "CpSet":
        if not self.ranges:
            return CpSet()
        rs = sorted(self.ranges)
        out: list[tuple[int, int]] = []
        cs, ce = rs[0]
        for s, e in rs[1:]:
            if s <= ce + 1:
                ce = max(ce, e)
            else:
                out.append((cs, ce))
                cs, ce = s, e
        out.append((cs, ce))
        return CpSet(out)

    def union(self, other: "CpSet") -> "CpSet":
        return CpSet(self.ranges + other.ranges).normalize()

    def intersect(self, other: "CpSet") -> "CpSet":
        out: list[tuple[int, int]] = []
        for a0, a1 in self.ranges:
            for b0, b1 in other.ranges:
                s, e = max(a0, b0), min(a1, b1)
                if s <= e:
                    out.append((s, e))
        return CpSet(out).normalize()

    def difference(self, other: "CpSet") -> "CpSet":
        result = list(self.ranges)
        for b0, b1 in other.ranges:
            nxt: list[tuple[int, int]] = []
            for a0, a1 in result:
                if b1 < a0 or b0 > a1:
                    nxt.append((a0, a1))
                    continue
                if a0 < b0:
                    nxt.append((a0, b0 - 1))
                if a1 > b1:
                    nxt.append((b1 + 1, a1))
            result = nxt
        return CpSet(result).normalize()

    def complement(self) -> "CpSet":
        # Complement within 0..0x10FFFF is huge; only used for small property sets.
        # Represent as "all minus self" but we materialize only when needed for small ops.
        full = CpSet([(0, 0x10FFFF)])
        return full.difference(self)

    def contains(self, cp: int) -> bool:
        for s, e in self.ranges:
            if s <= cp <= e:
                return True
        return False

    def __bool__(self) -> bool:
        return bool(self.ranges)


_CAT_CACHE: dict[str, CpSet] = {}
_GC_BUILT = False


def _build_gc_caches() -> None:
    global _GC_BUILT
    if _GC_BUILT:
        return
    buckets: dict[str, list[tuple[int, int]]] = {}
    open_runs: dict[str, int] = {}
    ccc0: list[tuple[int, int]] = []
    c0_s = None
    c0_p = None

    def close(key: str, at: int) -> None:
        if key in open_runs:
            buckets.setdefault(key, []).append((open_runs[key], at))
            del open_runs[key]

    for cp in range(0x110000):
        cat = unicodedata.category(chr(cp))
        wanted = {cat, cat[0]}
        for key in list(open_runs.keys()):
            if key not in wanted:
                close(key, cp - 1)
        for key in wanted:
            if key not in open_runs:
                open_runs[key] = cp
        if unicodedata.combining(chr(cp)) == 0:
            if c0_s is None:
                c0_s = cp
            c0_p = cp
        else:
            if c0_s is not None:
                ccc0.append((c0_s, c0_p))
                c0_s = None
    for key in list(open_runs.keys()):
        close(key, 0x10FFFF)
    if c0_s is not None:
        ccc0.append((c0_s, c0_p))

    for k, rs in buckets.items():
        _CAT_CACHE[k.lower()] = CpSet(rs).normalize()
    _CAT_CACHE["ccc=0"] = CpSet(ccc0).normalize()
    _CAT_CACHE["^ccc=0"] = (
        CpSet([(0, 0x10FFFF)]).difference(_CAT_CACHE["ccc=0"]).normalize()
    )
    _CAT_CACHE["uppercase"] = _CAT_CACHE.get("lu", CpSet()).union(
        _CAT_CACHE.get("lt", CpSet())
    )
    _CAT_CACHE["lowercase"] = _CAT_CACHE.get("ll", CpSet())
    _CAT_CACHE["nonspacing mark"] = _CAT_CACHE.get("mn", CpSet())
    _GC_BUILT = True


def prop_set(name: str) -> CpSet:
    key = name.lower().strip()
    if key in _CAT_CACHE:
        return _CAT_CACHE[key]
    if key.startswith("script=") or key.startswith("sc="):
        sname = key.split("=", 1)[1].strip()
        s = CpSet(SCRIPTS.get(sname, [])).normalize()
        _CAT_CACHE[key] = s
        return s
    if key in SCRIPTS:
        s = CpSet(SCRIPTS[key]).normalize()
        _CAT_CACHE[key] = s
        return s
    if key.startswith("block="):
        bname = key.split("=", 1)[1].strip().upper().replace(" ", "_")
        if bname in BLOCKS:
            s = CpSet([BLOCKS[bname]])
            _CAT_CACHE[key] = s
            return s
        for k, v in BLOCKS.items():
            if k.replace("_", "") == bname.replace("_", ""):
                s = CpSet([v])
                _CAT_CACHE[key] = s
                return s
        _CAT_CACHE[key] = CpSet()
        return CpSet()
    if key.startswith("ccc=") or key.startswith("^ccc="):
        _build_gc_caches()
        if key in _CAT_CACHE:
            return _CAT_CACHE[key]
        # generic ccc
        n = int(key.split("=", 1)[1])
        want_eq = not key.startswith("^")
        rs: list[tuple[int, int]] = []
        run = None
        for cp in range(0x110000):
            ok = (unicodedata.combining(chr(cp)) == n) if want_eq else (
                unicodedata.combining(chr(cp)) != n
            )
            if ok:
                if run is None:
                    run = [cp, cp]
                else:
                    run[1] = cp
            elif run is not None:
                rs.append((run[0], run[1]))
                run = None
        if run is not None:
            rs.append((run[0], run[1]))
        s = CpSet(rs).normalize()
        _CAT_CACHE[key] = s
        return s

    _build_gc_caches()
    if key in _CAT_CACHE:
        return _CAT_CACHE[key]
    for alias, target in (
        ("greek", "script=greek"),
        ("latin", "script=latin"),
        ("cyrillic", "script=cyrillic"),
        ("arabic", "script=arabic"),
        ("hebrew", "script=hebrew"),
        ("common", "script=common"),
        ("inherited", "script=inherited"),
        ("katakana", "script=katakana"),
        ("hiragana", "script=hiragana"),
        ("hangul", "script=hangul"),
        ("devanagari", "script=devanagari"),
        ("bengali", "script=bengali"),
    ):
        if key == alias:
            return prop_set(target)
    print(f"WARN unknown UnicodeSet property [:{name}:]", file=sys.stderr)
    _CAT_CACHE[key] = CpSet()
    return CpSet()

class SetParser:
    def __init__(self, text: str, vars_map: dict[str, object]):
        self.s = text
        self.i = 0
        self.vars = vars_map

    def peek(self) -> str:
        return self.s[self.i] if self.i < len(self.s) else ""

    def get(self) -> str:
        ch = self.peek()
        self.i += 1
        return ch

    def skip_ws(self) -> None:
        while self.peek() and self.peek() in " \t\r\n":
            self.i += 1

    def parse(self) -> CpSet:
        self.skip_ws()
        if self.peek() != "[":
            raise ValueError(f"expected '[' at {self.i}: {self.s[self.i:self.i+20]!r}")
        return self.parse_set()

    def parse_set(self) -> CpSet:
        assert self.get() == "["
        self.skip_ws()
        negate = False
        if self.peek() == "^":
            self.get()
            negate = True
            self.skip_ws()
        parts: list[CpSet] = []
        ops: list[str] = []  # between parts: '' union, '&', '-'
        pending_op = ""
        while True:
            self.skip_ws()
            if self.peek() == "]":
                self.get()
                break
            if self.peek() in "&-":
                pending_op = self.get()
                self.skip_ws()
                continue
            if self.peek() == "[":
                nested = self.parse_set()
                parts.append(nested)
                ops.append(pending_op)
                pending_op = ""
                continue
            if self.peek() == "$":
                name = self.read_ident()
                val = self.vars.get(name)
                if isinstance(val, CpSet):
                    parts.append(val)
                elif isinstance(val, str):
                    parts.append(string_to_set(val))
                else:
                    parts.append(CpSet())
                ops.append(pending_op)
                pending_op = ""
                continue
            if self.peek() == ":" and self.s.startswith("[:", self.i):
                self.i += 2
                end = self.s.find(":]", self.i)
                if end < 0:
                    raise ValueError("unclosed property")
                prop = self.s[self.i:end]
                self.i = end + 2
                parts.append(prop_set(prop))
                ops.append(pending_op)
                pending_op = ""
                continue
            # literal / range
            cp = self.read_cp()
            self.skip_ws()
            if self.peek() == "-":
                # could be range or difference — if next is ] or op, '-' is difference already handled
                # peek next significant
                j = self.i + 1
                while j < len(self.s) and self.s[j] in " \t":
                    j += 1
                if j < len(self.s) and self.s[j] not in "]&-" and not (
                    self.s[j] == ":" or self.s[j] == "[" or self.s[j] == "$"
                ):
                    self.get()  # consume '-'
                    self.skip_ws()
                    cp2 = self.read_cp()
                    parts.append(CpSet([(cp, cp2)]))
                    ops.append(pending_op)
                    pending_op = ""
                    continue
            parts.append(CpSet([(cp, cp)]))
            ops.append(pending_op)
            pending_op = ""
        # fold
        if not parts:
            result = CpSet()
        else:
            result = parts[0]
            for op, p in zip(ops[1:], parts[1:]):
                if op == "&":
                    result = result.intersect(p)
                elif op == "-":
                    result = result.difference(p)
                else:
                    result = result.union(p)
        if negate:
            result = result.complement()
        return result.normalize()

    def read_ident(self) -> str:
        assert self.get() == "$"
        start = self.i
        while self.peek() and (self.peek().isalnum() or self.peek() == "_"):
            self.i += 1
        return self.s[start:self.i]

    def read_cp(self) -> int:
        if self.peek() == "\\":
            self.get()
            if self.peek() == "u":
                self.get()
                if self.peek() == "{":
                    self.get()
                    end = self.s.find("}", self.i)
                    hx = self.s[self.i:end]
                    self.i = end + 1
                    return int(hx, 16)
                hx = self.s[self.i:self.i + 4]
                self.i += 4
                return int(hx, 16)
            if self.peek() == "x":
                self.get()
                if self.peek() == "{":
                    self.get()
                    end = self.s.find("}", self.i)
                    hx = self.s[self.i:end]
                    self.i = end + 1
                    return int(hx, 16)
            # escaped single char
            return ord(self.get())
        if self.peek() == "'":
            # quoted
            self.get()
            chars: list[int] = []
            while self.peek() and self.peek() != "'":
                if self.peek() == "\\":
                    chars.append(self.read_cp())
                else:
                    chars.append(ord(self.get()))
            if self.peek() == "'":
                self.get()
            if not chars:
                return 0
            return chars[0]
        return ord(self.get())


def string_to_set(s: str) -> CpSet:
    cps = [ord(c) for c in s]
    return CpSet([(c, c) for c in cps]).normalize()


def unescape_literal(text: str) -> list[int]:
    """Parse a rule side string into code points (no sets)."""
    out: list[int] = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch in " \t\r\n":
            i += 1
            continue
        if ch == "\\":
            i += 1
            if i >= len(text):
                break
            if text[i] == "u":
                i += 1
                if i < len(text) and text[i] == "{":
                    i += 1
                    j = text.find("}", i)
                    out.append(int(text[i:j], 16))
                    i = j + 1
                else:
                    out.append(int(text[i:i + 4], 16))
                    i += 4
            elif text[i] == "x" and i + 1 < len(text) and text[i + 1] == "{":
                i += 2
                j = text.find("}", i)
                out.append(int(text[i:j], 16))
                i = j + 1
            else:
                out.append(ord(text[i]))
                i += 1
            continue
        if ch == "'":
            # ICU: '' is a literal apostrophe; '...' is a quoted run.
            if i + 1 < len(text) and text[i + 1] == "'":
                out.append(ord("'"))
                i += 2
                continue
            i += 1
            buf: list[int] = []
            while i < len(text):
                if text[i] == "'":
                    if i + 1 < len(text) and text[i + 1] == "'":
                        buf.append(ord("'"))
                        i += 2
                        continue
                    i += 1
                    break
                if text[i] == "\\":
                    sub = unescape_literal(text[i:])
                    if sub:
                        buf.append(sub[0])
                    i += 1
                    if i < len(text) and text[i] == "u":
                        if i + 1 < len(text) and text[i + 1] == "{":
                            j = text.find("}", i + 2)
                            i = (j + 1) if j >= 0 else len(text)
                        else:
                            i += 5
                    else:
                        i += 1
                else:
                    buf.append(ord(text[i]))
                    i += 1
            out.extend(buf)
            continue
        if ch == "$":
            # variable reference — caller should expand; leave as marker by raising
            raise ValueError("bare $ in literal")
        out.append(ord(ch))
        i += 1
    return out


@dataclass
class Rule:
    before: CpSet | None
    key: list[int]
    after: CpSet | None
    repl: list[int]
    order: int


@dataclass
class Stage:
    kind: str  # NFD NFC NFKD NFKC LOWER NULL RULES HANGUL_NFD
    rules: list[Rule] = field(default_factory=list)


@dataclass
class CompiledId:
    id: str
    aliases: list[str]
    direction: str  # forward|reverse
    reversible: int
    source_script: int
    target_script: int
    stages: list[Stage]
    skipped: int = 0


def extract_trule(xml_path: Path) -> tuple[dict, str]:
    tree = ET.parse(xml_path)
    root = tree.getroot()
    # find transform
    tr = None
    for el in root.iter():
        if el.tag.endswith("transform"):
            tr = el
            break
    if tr is None:
        raise ValueError(f"no transform in {xml_path}")
    attrs = {k: v for k, v in tr.attrib.items()}
    body = ""
    for child in tr:
        if child.tag.endswith("tRule"):
            body = (child.text or "") + "".join(ET.tostring(e, encoding="unicode") for e in list(child))
            # CDATA is in .text
            body = child.text or ""
            break
    return attrs, body


def strip_comments(body: str) -> str:
    out: list[str] = []
    i = 0
    while i < len(body):
        if body[i] == "#":
            while i < len(body) and body[i] not in "\r\n":
                i += 1
            continue
        out.append(body[i])
        i += 1
    return "".join(out)


def split_statements(body: str) -> list[str]:
    body = strip_comments(body)
    parts: list[str] = []
    buf: list[str] = []
    i = 0
    in_quote = False
    depth = 0
    while i < len(body):
        ch = body[i]
        # Backslash escapes next character (including ', ;, →).
        if ch == "\\" and i + 1 < len(body):
            buf.append(ch)
            buf.append(body[i + 1])
            i += 2
            continue
        if ch == "[" and not in_quote:
            depth += 1
            buf.append(ch)
            i += 1
            continue
        if ch == "]" and not in_quote:
            depth = max(0, depth - 1)
            buf.append(ch)
            i += 1
            continue
        # Quotes only matter outside UnicodeSets (sets use ' for literals).
        # ICU '' is an escaped apostrophe, not a toggle.
        if ch == "'" and depth == 0:
            if i + 1 < len(body) and body[i + 1] == "'":
                buf.append("'")
                buf.append("'")
                i += 2
                continue
            in_quote = not in_quote
            buf.append(ch)
            i += 1
            continue
        if ch == ";" and depth == 0 and not in_quote:
            stmt = "".join(buf).strip()
            if stmt:
                parts.append(stmt)
            buf = []
            i += 1
            continue
        buf.append(ch)
        i += 1
    stmt = "".join(buf).strip()
    if stmt:
        parts.append(stmt)
    return parts


def find_unescaped_arrow(stmt: str) -> tuple[str | None, int]:
    """Return (arrow, index) for the first unescaped transform arrow."""
    arrows = (ARROW_BOTH, ARROW_FWD, ARROW_REV, "↔", "→", "←")
    i = 0
    while i < len(stmt):
        if stmt[i] == "\\" and i + 1 < len(stmt):
            i += 2
            continue
        for a in arrows:
            if stmt.startswith(a, i):
                return a, i
        i += 1
    return None, -1


def parse_context_side(left: str, vars_map: dict[str, object]) -> tuple[CpSet | None, list[int], CpSet | None]:
    """Parse ICU left-hand side: before{key}after | before}key | key{after | key."""
    left = left.strip()
    before: CpSet | None = None
    after: CpSet | None = None

    def side_to_set(text: str) -> CpSet | None:
        text = text.strip()
        if not text:
            return None
        if text.startswith("$") and text[1:] in vars_map and isinstance(vars_map[text[1:]], CpSet):
            return vars_map[text[1:]]  # type: ignore
        if text.startswith("["):
            return SetParser(text, vars_map).parse()
        try:
            return SetParser(f"[{text}]", vars_map).parse()
        except Exception:
            return string_to_set("".join(chr(c) for c in parse_side_to_cps(text, vars_map)))

    m = re.search(r"\{([^{}]*)\}", left)
    if m:
        key_text = m.group(1)
        before_t = left[: m.start()].strip()
        after_t = left[m.end() :].strip()
        if before_t:
            before = side_to_set(before_t)
        if after_t:
            after = side_to_set(after_t)
        return before, parse_side_to_cps(key_text, vars_map), after

    # before}key  (preceding context)
    if "}" in left and "{" not in left:
        before_t, key_text = left.split("}", 1)
        if before_t.strip():
            before = side_to_set(before_t)
        return before, parse_side_to_cps(key_text, vars_map), None

    # key{after  (following context)
    if "{" in left and "}" not in left:
        key_text, after_t = left.split("{", 1)
        if after_t.strip():
            after = side_to_set(after_t)
        return None, parse_side_to_cps(key_text, vars_map), after

    return None, parse_side_to_cps(left, vars_map), None


def expand_vars_in_text(text: str, vars_map: dict[str, object]) -> str:
    """Replace $var that are strings; leave sets for set parser."""
    def repl(m):
        name = m.group(1)
        val = vars_map.get(name)
        if isinstance(val, str):
            return val
        return m.group(0)
    return re.sub(r"\$([A-Za-z_][A-Za-z0-9_]*)", repl, text)


def parse_side_to_cps(text: str, vars_map: dict[str, object]) -> list[int]:
    text = text.strip()
    if not text:
        return []
    # If entire side is a set, cannot be a key
    text2 = expand_vars_in_text(text, vars_map)
    # Replace remaining $set vars that are CpSet — not valid in key alone
    out: list[int] = []
    i = 0
    while i < len(text2):
        if text2[i] in " \t\r\n":
            i += 1
            continue
        if text2[i] == "$":
            j = i + 1
            while j < len(text2) and (text2[j].isalnum() or text2[j] == "_"):
                j += 1
            name = text2[i + 1:j]
            val = vars_map.get(name)
            if isinstance(val, str):
                out.extend(ord(c) for c in val)
            elif isinstance(val, CpSet):
                # single-cp sets sometimes used as literals
                flat: list[int] = []
                for a, b in val.ranges:
                    for cp in range(a, b + 1):
                        flat.append(cp)
                        if len(flat) > 8:
                            raise ValueError(f"set ${name} too large for literal side")
                out.extend(flat)
            else:
                raise ValueError(f"unknown var ${name}")
            i = j
            continue
        if text2[i] == "[":
            raise ValueError("raw set in key/repl")
        # take one escaped or char via unescape on remaining chunk until space/$/[
        # simpler: use unescape_literal on whole after var expansion for pure literals
        break
    if i == 0:
        return unescape_literal(text2)
    # mixed — parse rest
    out.extend(unescape_literal(text2[i:]))
    return out


def try_parse_rule(stmt: str, vars_map: dict[str, object], order: int) -> Rule | None:
    # Skip unsupported
    if "|" in stmt and not re.search(r"\\\|", stmt):
        # cursor
        return None
    if re.search(r"\$\d", stmt):
        return None
    arrow, aidx = find_unescaped_arrow(stmt)
    if arrow is None:
        return None
    left = stmt[:aidx].strip()
    right = stmt[aidx + len(arrow) :].strip()
    try:
        before, key, after = parse_context_side(left, vars_map)
        repl = parse_side_to_cps(right, vars_map)
    except Exception:
        return None
    if not key and not repl and arrow != ARROW_BOTH:
        if not key:
            return None
    return Rule(before=before, key=key, after=after, repl=repl, order=order)


def rule_side_is_clean(cps: list[int]) -> bool:
    """Reject misparsed sides that leaked syntax into literals."""
    for cp in cps:
        if cp in (0x2190, 0x2192, 0x2194, 0x0A, 0x0D, ord(";"), ord("{"), ord("}")):
            return False
    return True


def compile_body(body: str, direction: str, catalog: dict[str, Path], depth: int = 0) -> tuple[list[Stage], int]:
    if depth > 8:
        raise ValueError("transform chain too deep")
    stmts = split_statements(body)
    vars_map: dict[str, object] = {}
    stages: list[Stage] = []
    cur_rules: list[Rule] = []
    skipped = 0
    order = 0

    def flush_rules():
        nonlocal cur_rules
        if cur_rules:
            # longest key first, then order
            cur_rules.sort(key=lambda r: (-len(r.key), r.order))
            stages.append(Stage("RULES", cur_rules))
            cur_rules = []

    for stmt in stmts:
        stmt = stmt.strip()
        if not stmt:
            continue
        # :: special
        if stmt.startswith("::"):
            flush_rules()
            inner = stmt[2:].strip()
            m = re.match(
                r"(?i)^(NFKD|NFKC|NFD|NFC)\s*(?:\(\s*(NFKD|NFKC|NFD|NFC)?\s*\))?\s*$",
                inner,
            )
            if m:
                fwd, rev = m.group(1).upper(), (m.group(2) or m.group(1)).upper()
                stages.append(Stage(fwd if direction == "forward" else rev))
                continue
            if re.match(r"(?i)^\(?\s*lower\s*\)?\s*$", inner) or re.match(
                r"(?i)^lower\s*\(\s*\)\s*$", inner
            ):
                if direction == "forward":
                    stages.append(Stage("LOWER"))
                continue
            if re.match(r"(?i)^null\s*$", inner):
                stages.append(Stage("NULL"))
                continue
            # Filter UnicodeSet (may be wrapped in parentheses)
            filt = inner.lstrip()
            if filt.startswith("("):
                filt = filt[1:].lstrip()
            if filt.startswith("["):
                continue
            if re.search(r"(?i)fullwidth|halfwidth|name\s*\(|hex\s*\(|remove\s*\(", inner):
                skipped += 1
                continue
            tid = re.sub(r"\s+", "", inner)
            tid = re.sub(r"\(\)\s*$", "", tid)
            # Strip outer parens around bare IDs
            if tid.startswith("(") and tid.endswith(")"):
                tid = tid[1:-1]
            path = resolve_transform_id(tid, catalog)
            if path is None:
                print(f"WARN unresolved ::{tid}", file=sys.stderr)
                skipped += 1
                continue
            sub_dir = direction
            if tid in ("ConjoiningJamo-Latin", "Katakana-Latin"):
                sub_dir = "reverse" if direction == "forward" else "forward"
            _, sub = extract_trule(path)
            st, sk = compile_body(sub, sub_dir, catalog, depth + 1)
            stages.extend(st)
            skipped += sk
            continue

        # variable assignment
        vm = re.match(r"^\$([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$", stmt)
        if vm:
            name, rhs = vm.group(1), vm.group(2).strip()
            if rhs.startswith("["):
                try:
                    vars_map[name] = SetParser(rhs, vars_map).parse()
                except Exception as e:
                    print(f"WARN set var ${name}: {e}", file=sys.stderr)
                    vars_map[name] = CpSet()
            else:
                try:
                    # string var
                    cps = parse_side_to_cps(rhs, vars_map)
                    vars_map[name] = "".join(chr(c) for c in cps)
                except Exception:
                    vars_map[name] = ""
            continue

        # conversion rule
        arrow, _aidx = find_unescaped_arrow(stmt)
        if arrow is None:
            skipped += 1
            continue
        rule = try_parse_rule(stmt, vars_map, order)
        order += 1
        if rule is None:
            skipped += 1
            continue
        # ICU: a→b forward; reverse is b→a. a←b is reverse-only form of b→a.
        # a↔b is both.
        if arrow == ARROW_FWD:
            if direction == "reverse":
                rule = Rule(
                    before=rule.after,
                    key=rule.repl,
                    after=rule.before,
                    repl=rule.key,
                    order=rule.order,
                )
        elif arrow == ARROW_REV:
            if direction == "forward":
                rule = Rule(
                    before=rule.after,
                    key=rule.repl,
                    after=rule.before,
                    repl=rule.key,
                    order=rule.order,
                )
            # reverse direction keeps lhs←rhs as lhs→rhs (parsed key=lhs repl=rhs)
        elif arrow == ARROW_BOTH:
            if direction == "reverse":
                # Jamo/script ↔ Latin rules carry Latin-side contexts that must
                # not become ante-context on the jamo key. ICU Jamo-Latin uses
                # context-free jamo→latin short names for the reverse pass.
                rule = Rule(
                    before=None,
                    key=rule.repl,
                    after=None,
                    repl=rule.key,
                    order=rule.order,
                )
        if not rule.key:
            skipped += 1
            continue
        if not rule_side_is_clean(rule.key) or not rule_side_is_clean(rule.repl):
            skipped += 1
            continue
        cur_rules.append(rule)

    flush_rules()
    return stages, skipped


def resolve_transform_id(tid: str, catalog: dict[str, Path]) -> Path | None:
    tid = tid.strip()
    aliases = {
        "ConjoiningJamo-Latin": "Latin-ConjoiningJamo.xml",  # use reverse
        "Katakana-Latin": "Latin-Katakana.xml",
        "Hiragana-Katakana": "Hiragana-Katakana.xml",
        "Devanagari-InterIndic": "Devanagari-InterIndic.xml",
        "Bengali-InterIndic": "Bengali-InterIndic.xml",
        "InterIndic-Latin": "InterIndic-Latin.xml",
        "Latin-ASCII": "Latin-ASCII.xml",
    }
    if tid in aliases:
        p = SRC_DIR / aliases[tid]
        return p if p.is_file() else None
    # try direct
    for k, p in catalog.items():
        if k.lower() == tid.lower() or tid.lower() in k.lower():
            return p
    p = SRC_DIR / f"{tid}.xml"
    return p if p.is_file() else None


def script_enum(name: str) -> int:
    if not name:
        return 0xFFFF
    key = name.lower().replace("_", "")
    # ISO codes
    iso = {
        "cyrl": "cyrillic",
        "latn": "latin",
        "grek": "greek",
        "arab": "arabic",
        "hebr": "hebrew",
        "deva": "devanagari",
        "beng": "bengali",
        "hang": "hangul",
        "hira": "hiragana",
        "kana": "katakana",
        "jamo": "hangul",
        "ascii": "latin",
        "ru": "cyrillic",
        "uk": "cyrillic",
        "ru_latn": "latin",
        "uk_latn": "latin",
    }
    key = iso.get(key, key)
    return SCRIPT_NAME_TO_ROBO.get(key, SCRIPT_NAME_TO_ROBO.get(name.lower(), 0xFFFF))


def build_catalog() -> dict[str, Path]:
    cat: dict[str, Path] = {}
    for p in SRC_DIR.glob("*.xml"):
        cat[p.stem] = p
    return cat


def compile_all() -> list[CompiledId]:
    catalog = build_catalog()
    out: list[CompiledId] = []
    for spec in PUBLIC_TARGETS:
        path = SRC_DIR / spec["file"]
        if not path.is_file():
            print(f"MISSING {path}", file=sys.stderr)
            continue
        attrs, body = extract_trule(path)
        direction = spec["dir"]
        stages, skipped = compile_body(body, direction, catalog)
        # Hangul: inject algorithmic syllable → jamo before jamo rules
        if spec["id"] == "Hangul-Latin":
            # Prepend Hangul NFD-like decomp stage before first RULES after NFKD
            new_stages: list[Stage] = []
            for st in stages:
                if st.kind == "NFKD":
                    new_stages.append(st)
                    new_stages.append(Stage("HANGUL_DECOMP"))
                else:
                    new_stages.append(st)
            stages = new_stages
        aliases = []
        if "alias" in attrs:
            aliases = attrs["alias"].split()
        src = script_enum(attrs.get("source", ""))
        tgt = script_enum(attrs.get("target", ""))
        if direction == "reverse":
            src, tgt = tgt, src
        reversible = 1 if attrs.get("direction", "").lower() == "both" else 0
        out.append(
            CompiledId(
                id=spec["id"],
                aliases=aliases,
                direction=direction,
                reversible=reversible,
                source_script=src if src != 0xFFFF else 0,
                target_script=tgt if tgt != 0xFFFF else 0,
                stages=stages,
                skipped=skipped,
            )
        )
        print(f"compiled {spec['id']}: stages={len(stages)} skipped_rules={skipped}")
    return out


def emit(ids: list[CompiledId]) -> None:
    # Deduplicate sets and cp pools
    set_list: list[list[tuple[int, int]]] = [[]]  # index 0 = empty/none unused; 0 means no set
    set_index: dict[tuple[tuple[int, int], ...], int] = {(): 0}
    cp_pool: list[int] = []

    def intern_set(s: CpSet | None) -> int:
        if s is None or not s.ranges:
            return 0
        key = tuple(s.ranges)
        if key in set_index:
            return set_index[key]
        idx = len(set_list)
        set_list.append(list(s.ranges))
        set_index[key] = idx
        return idx

    def intern_cps(cps: list[int]) -> tuple[int, int]:
        if not cps:
            return 0, 0
        off = len(cp_pool)
        cp_pool.extend(cps)
        return off, len(cps)

    # Flatten rules globally
    all_rules: list[tuple[int, int, int, int, int, int, int]] = []  # before, after, key_off, key_len, repl_off, repl_len, pad
    all_stages: list[tuple[int, int, int]] = []  # kind, rule_begin, rule_count
    id_rows: list[dict] = []

    KIND = {"NFD": 1, "NFC": 2, "NFKD": 3, "NFKC": 4, "LOWER": 5, "NULL": 6, "RULES": 7, "HANGUL_DECOMP": 8}

    for cid in ids:
        st_begin = len(all_stages)
        for st in cid.stages:
            if st.kind == "RULES":
                rb = len(all_rules)
                for r in st.rules:
                    b = intern_set(r.before)
                    a = intern_set(r.after)
                    ko, kl = intern_cps(r.key)
                    ro, rl = intern_cps(r.repl)
                    if kl > 255 or rl > 255:
                        continue
                    all_rules.append((b, a, ko, kl, ro, rl, 0))
                all_stages.append((KIND["RULES"], rb, len(all_rules) - rb))
            else:
                all_stages.append((KIND.get(st.kind, 0), 0, 0))
        # alias string
        alias_str = "\0".join(cid.aliases)
        id_rows.append(
            {
                "id": cid.id,
                "aliases": alias_str,
                "direction": 1 if cid.direction == "forward" else 2,
                "reversible": cid.reversible,
                "src": cid.source_script,
                "tgt": cid.target_script,
                "st_begin": st_begin,
                "st_count": len(cid.stages),
            }
        )

    # String pool for ids + aliases.
    # Aliases are packed as NUL-separated strings ending with an extra NUL
    # (double-NUL terminator) so runtime walks cannot spill into later IDs.
    str_pool = bytearray()
    str_pool.append(0)

    def add_str(s: str) -> int:
        off = len(str_pool)
        str_pool.extend(s.encode("utf-8"))
        str_pool.append(0)
        return off

    def add_alias_blob(alias_str: str) -> int:
        """Pack aliases with a double-NUL terminator."""
        off = len(str_pool)
        if alias_str:
            str_pool.extend(alias_str.encode("utf-8"))
            str_pool.append(0)
        str_pool.append(0)  # end-of-list
        return off

    for row in id_rows:
        row["id_off"] = add_str(row["id"])
        row["alias_off"] = add_alias_blob(row["aliases"]) if row["aliases"] else 0

    # set ranges flat
    range_pool: list[tuple[int, int]] = []
    set_meta: list[tuple[int, int]] = [(0, 0)]  # begin, count
    for i, ranges in enumerate(set_list):
        if i == 0:
            continue
        b = len(range_pool)
        range_pool.extend(ranges)
        set_meta.append((b, len(ranges)))
    while len(set_meta) < len(set_list):
        set_meta.append((0, 0))

    h = f"""/* Auto-generated by generate_translit.py — DO NOT EDIT.
 * CLDR {CLDR_VERSION} transforms. Generator {GENERATOR_VERSION}.
 */
#ifndef ROBO_TRANSLIT_DATA_H
#define ROBO_TRANSLIT_DATA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define ROBO_TRANSLIT_DATA_CLDR_VERSION "{CLDR_VERSION}"
#define ROBO_TRANSLIT_DATA_GENERATOR_VERSION "{GENERATOR_VERSION}"

typedef enum robo_translit_stage_kind {{
    ROBO_TRANSLIT_STAGE_NONE = 0,
    ROBO_TRANSLIT_STAGE_NFD = 1,
    ROBO_TRANSLIT_STAGE_NFC = 2,
    ROBO_TRANSLIT_STAGE_NFKD = 3,
    ROBO_TRANSLIT_STAGE_NFKC = 4,
    ROBO_TRANSLIT_STAGE_LOWER = 5,
    ROBO_TRANSLIT_STAGE_NULL = 6,
    ROBO_TRANSLIT_STAGE_RULES = 7,
    ROBO_TRANSLIT_STAGE_HANGUL_DECOMP = 8
}} robo_translit_stage_kind_t;

typedef struct robo_translit_range {{
    uint32_t start;
    uint32_t end;
}} robo_translit_range_t;

typedef struct robo_translit_set {{
    uint32_t range_begin;
    uint16_t range_count;
}} robo_translit_set_t;

typedef struct robo_translit_rule {{
    uint32_t key_off;
    uint32_t repl_off;
    uint16_t before_set; /* 0 = none */
    uint16_t after_set;
    uint16_t key_len;
    uint16_t repl_len;
}} robo_translit_rule_t;

typedef struct robo_translit_stage {{
    uint8_t kind;
    uint32_t rule_begin;
    uint32_t rule_count;
}} robo_translit_stage_t;

typedef struct robo_translit_id {{
    uint32_t id_off;
    uint32_t alias_off; /* NUL-separated aliases; double-NUL terminated; 0 = none */
    uint8_t direction;  /* 1 forward, 2 reverse */
    uint8_t reversible;
    uint16_t source_script; /* robo_unicode_script_t */
    uint16_t target_script;
    uint32_t stage_begin;
    uint16_t stage_count;
}} robo_translit_id_t;

extern const char robo_translit_strpool[];
extern const uint32_t robo_translit_cps[];
extern const robo_translit_range_t robo_translit_ranges[];
extern const robo_translit_set_t robo_translit_sets[];
extern const robo_translit_rule_t robo_translit_rules[];
extern const robo_translit_stage_t robo_translit_stages[];
extern const robo_translit_id_t robo_translit_ids[];
extern const uint32_t robo_translit_id_count;
extern const uint32_t robo_translit_rule_count;
extern const uint32_t robo_translit_stage_count;
extern const uint32_t robo_translit_set_count;
extern const uint32_t robo_translit_cp_count;

#ifdef __cplusplus
}}
#endif

#endif /* ROBO_TRANSLIT_DATA_H */
"""

    def c_strpool(b: bytearray) -> str:
        # emit as string with escapes
        parts = []
        line = []
        for by in b:
            if by == 0:
                line.append("\\0")
            elif 32 <= by < 127 and by not in (ord("\\"), ord('"')):
                line.append(chr(by))
            else:
                line.append(f"\\x{by:02x}")
            if len("".join(line)) > 80:
                parts.append('"' + "".join(line) + '"')
                line = []
        if line:
            parts.append('"' + "".join(line) + '"')
        return "\n".join(parts) if parts else '""'

    cps_body = ",\n".join(
        ", ".join(f"0x{cp:04X}u" for cp in cp_pool[i : i + 8]) for i in range(0, len(cp_pool), 8)
    ) or "0"
    ranges_body = ",\n".join(f"    {{{a}u, {b}u}}" for a, b in range_pool) or "    {0u, 0u}"
    sets_body = ",\n".join(f"    {{{b}u, {c}u}}" for b, c in set_meta) or "    {0u, 0u}"
    rules_body = ",\n".join(
        f"    {{{ko}u, {ro}u, {b}, {a}, {kl}u, {rl}u}}" for b, a, ko, kl, ro, rl, _ in all_rules
    ) or "    {0,0,0,0,0,0}"
    stages_body = ",\n".join(f"    {{{k}u, {rb}u, {rc}u}}" for k, rb, rc in all_stages) or "    {0,0,0}"
    ids_body = ",\n".join(
        f"    {{{r['id_off']}u, {r['alias_off']}u, {r['direction']}u, {r['reversible']}u, "
        f"{r['src']}u, {r['tgt']}u, {r['st_begin']}u, {r['st_count']}u}}"
        for r in id_rows
    )

    c = f"""/* Auto-generated by generate_translit.py — DO NOT EDIT.
 * CLDR {CLDR_VERSION} transforms. Generator {GENERATOR_VERSION}.
 */
#include "RoboI18N/RoboTranslitData.h"

const char robo_translit_strpool[] =
{c_strpool(str_pool)};

const uint32_t robo_translit_cps[{max(1, len(cp_pool))}] = {{
{cps_body}
}};
const uint32_t robo_translit_cp_count = {len(cp_pool)}u;

const robo_translit_range_t robo_translit_ranges[{max(1, len(range_pool))}] = {{
{ranges_body}
}};

const robo_translit_set_t robo_translit_sets[{max(1, len(set_meta))}] = {{
{sets_body}
}};
const uint32_t robo_translit_set_count = {len(set_meta)}u;

const robo_translit_rule_t robo_translit_rules[{max(1, len(all_rules))}] = {{
{rules_body}
}};
const uint32_t robo_translit_rule_count = {len(all_rules)}u;

const robo_translit_stage_t robo_translit_stages[{max(1, len(all_stages))}] = {{
{stages_body}
}};
const uint32_t robo_translit_stage_count = {len(all_stages)}u;

const robo_translit_id_t robo_translit_ids[{len(id_rows)}] = {{
{ids_body}
}};
const uint32_t robo_translit_id_count = {len(id_rows)}u;
"""

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(h, encoding="utf-8", newline="\n")
    OUT_C.write_text(c, encoding="utf-8", newline="\n")

    digest = hashlib.sha256(c.encode("utf-8")).hexdigest()
    GOLDEN.write_text(
        '{\n  "robo_translit_data.c": "%s",\n  "id_count": %d,\n  "rule_count": %d\n}\n'
        % (digest, len(id_rows), len(all_rules)),
        encoding="utf-8",
        newline="\n",
    )
    print(f"wrote {OUT_C} ids={len(id_rows)} rules={len(all_rules)} sets={len(set_meta)} cps={len(cp_pool)}")
    print(f"sha256 {digest}")


def main() -> int:
    if not SRC_DIR.is_dir():
        print(f"missing {SRC_DIR}; run Fetch-Transforms.ps1", file=sys.stderr)
        return 1
    # Warm category caches used often (once)
    print("warming Unicode property caches (once)...")
    _build_gc_caches()
    for p in ("script=latin", "script=greek", "script=cyrillic", "script=arabic"):
        prop_set(p)
    ids = compile_all()
    if not ids:
        print("no ids compiled", file=sys.stderr)
        return 1
    emit(ids)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
Compile CLDR plural-rule expressions to compact bytecode (Goal 4.4).

Rule grammar (CLDR / UTS #35 subset used by CLDR 46):
  condition  := and_expr ('or' and_expr)*
  and_expr   := relation ('and' relation)*
  relation   := operand ('%' number)? ('='|'!=') range_list
  range_list := range (',' range)*
  range      := number | number '..' number
  operand    := 'n' | 'i' | 'v' | 'w' | 'f' | 't' | 'c' | 'e'

Samples after '@' are stripped before compilation and harvested for tests.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

CATEGORIES = ("zero", "one", "two", "few", "many", "other")
CAT_TO_ID = {c: i for i, c in enumerate(CATEGORIES)}

OP_N, OP_I, OP_V, OP_W, OP_F, OP_T, OP_E = range(7)
OP_NAME = {"n": OP_N, "i": OP_I, "v": OP_V, "w": OP_W, "f": OP_F, "t": OP_T, "c": OP_E, "e": OP_E}

# Bytecode
PL_END = 0
PL_TRUE = 1
PL_AND = 2
PL_OR = 3
PL_TEST = 4  # operand, flags, mod:u32, n_ranges:u8, (lo:u32, hi:u32)*n


@dataclass
class Range:
    lo: int
    hi: int


@dataclass
class Relation:
    operand: int
    mod: int | None
    negated: bool
    ranges: list[Range]


@dataclass
class CompiledLocale:
    locale: str
    # list of (category_id, bytecode)
    rules: list[tuple[int, bytes]] = field(default_factory=list)
    # (input_str, category_name) from @ samples
    samples: list[tuple[str, str]] = field(default_factory=list)


def strip_samples(rule: str) -> tuple[str, str]:
    """Return (condition, samples_tail)."""
    if "@" in rule:
        cond, _, tail = rule.partition("@")
        return cond.strip(), "@" + tail
    return rule.strip(), ""


def parse_number(tok: str) -> int:
    if not re.fullmatch(r"-?\d+", tok):
        raise ValueError(f"bad number {tok!r}")
    return int(tok)


def parse_range_list(text: str) -> list[Range]:
    ranges: list[Range] = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if ".." in part:
            a, b = part.split("..", 1)
            lo, hi = parse_number(a.strip()), parse_number(b.strip())
            if hi < lo:
                raise ValueError(f"inverted range {part!r}")
            ranges.append(Range(lo, hi))
        else:
            v = parse_number(part)
            ranges.append(Range(v, v))
    if not ranges:
        raise ValueError("empty range list")
    return ranges


def parse_relation(text: str) -> Relation:
    text = text.strip()
    m = re.fullmatch(
        r"([nivwftce])\s*(?:%\s*(\d+))?\s*(=|!=)\s*(.+)",
        text,
    )
    if not m:
        raise ValueError(f"bad relation {text!r}")
    op = OP_NAME[m.group(1)]
    mod = int(m.group(2)) if m.group(2) else None
    if mod is not None and mod <= 0:
        raise ValueError(f"invalid mod {mod}")
    negated = m.group(3) == "!="
    ranges = parse_range_list(m.group(4))
    return Relation(op, mod, negated, ranges)


def parse_condition(text: str) -> list[Relation | str]:
    """Return postfix tokens: Relation objects and 'and'/'or'."""
    text = text.strip()
    if not text:
        return []

    # Split on ' or ' / ' and ' while respecting they are keywords.
    # First split by or (lowest precedence), then and.
    def split_keyword(s: str, kw: str) -> list[str]:
        parts: list[str] = []
        buf = s
        pattern = re.compile(rf"\s+{kw}\s+")
        idx = 0
        for m in pattern.finditer(s):
            parts.append(s[idx : m.start()])
            idx = m.end()
        parts.append(s[idx:])
        return parts

    postfix: list[Relation | str] = []
    or_parts = split_keyword(text, "or")
    for oi, or_part in enumerate(or_parts):
        and_parts = split_keyword(or_part.strip(), "and")
        for ai, and_part in enumerate(and_parts):
            postfix.append(parse_relation(and_part.strip()))
            if ai > 0:
                postfix.append("and")
        if oi > 0:
            postfix.append("or")
    return postfix


def emit_u32(n: int) -> bytes:
    if n < 0 or n > 0xFFFFFFFF:
        raise ValueError(f"u32 out of range {n}")
    return bytes([(n) & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF, (n >> 24) & 0xFF])


def compile_condition(text: str) -> bytes:
    text = text.strip()
    if not text:
        return bytes([PL_TRUE, PL_END])
    postfix = parse_condition(text)
    out = bytearray()
    for tok in postfix:
        if tok == "and":
            out.append(PL_AND)
        elif tok == "or":
            out.append(PL_OR)
        else:
            assert isinstance(tok, Relation)
            flags = 0
            if tok.negated:
                flags |= 1
            if tok.mod is not None:
                flags |= 2
            out.append(PL_TEST)
            out.append(tok.operand & 0xFF)
            out.append(flags & 0xFF)
            out.extend(emit_u32(tok.mod if tok.mod is not None else 0))
            if len(tok.ranges) > 255:
                raise ValueError("too many ranges")
            out.append(len(tok.ranges) & 0xFF)
            for r in tok.ranges:
                out.extend(emit_u32(r.lo))
                out.extend(emit_u32(r.hi))
    out.append(PL_END)
    return bytes(out)


_SAMPLE_INT = re.compile(
    r"@integer\s+([^@]+?)(?=@decimal|$)",
    re.DOTALL,
)
_SAMPLE_DEC = re.compile(
    r"@decimal\s+([^@]+?)(?=@integer|$)",
    re.DOTALL,
)


def _expand_token(tok: str, is_decimal: bool) -> list[str]:
    tok = tok.strip().rstrip(".").strip()
    if not tok or tok in ("…", "...", "…"):
        return []
    # compact decimal samples like 1c6 — skip for e=0 suite (documented limitation)
    if re.search(r"\dc\d", tok, re.I) or "c" in tok.lower() and re.search(r"\d", tok):
        if re.fullmatch(r"\d+[cC]\d+", tok) or re.fullmatch(r"\d+\.\d+[cC]\d+", tok):
            return []
    if "~" in tok:
        a, b = tok.split("~", 1)
        a, b = a.strip(), b.strip()
        if is_decimal:
            try:
                fa, fb = float(a), float(b)
            except ValueError:
                return []
            if fb < fa:
                return []
            # Expand with step 0.1 for short spans; else endpoints only
            out: list[str] = []
            if fb - fa <= 2.0 + 1e-9:
                x = fa
                while x <= fb + 1e-9:
                    # Format preserving one decimal if needed
                    if abs(x - round(x)) < 1e-9:
                        out.append(f"{int(round(x))}.0")
                    else:
                        out.append(f"{x:.1f}")
                    x += 0.1
            else:
                out.append(a)
                out.append(b)
            return out
        try:
            ia, ib = int(a), int(b)
        except ValueError:
            return []
        if ib < ia or ib - ia > 200:
            return [str(ia), str(ib)] if ia != ib else [str(ia)]
        return [str(i) for i in range(ia, ib + 1)]
    # plain value
    if is_decimal:
        if re.fullmatch(r"\d+", tok):
            return [tok + ".0"]
        if re.fullmatch(r"\d+\.\d+", tok):
            return [tok]
        return []
    if re.fullmatch(r"\d+", tok):
        return [tok]
    return []


def harvest_samples(samples_tail: str, category: str) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for m in _SAMPLE_INT.finditer(samples_tail):
        blob = m.group(1)
        for piece in re.split(r"[,\s]+", blob):
            for v in _expand_token(piece, False):
                out.append((v, category))
    for m in _SAMPLE_DEC.finditer(samples_tail):
        blob = m.group(1)
        for piece in re.split(r"[,\s]+", blob):
            for v in _expand_token(piece, True):
                out.append((v, category))
    return out


def compile_locale_rules(locale: str, cat_map: dict[str, str], kind: str) -> CompiledLocale:
    """cat_map: pluralRule-count-X -> rule string."""
    compiled = CompiledLocale(locale=locale)
    # CLDR order: zero, one, two, few, many, then other
    ordered = []
    for cat in CATEGORIES:
        key = f"pluralRule-count-{cat}"
        if key in cat_map:
            ordered.append((cat, cat_map[key]))
    if not ordered:
        raise ValueError(f"{kind} {locale}: no rules")
    if ordered[-1][0] != "other":
        # other may be only rule (Japanese)
        if not any(c == "other" for c, _ in ordered):
            raise ValueError(f"{kind} {locale}: missing other")

    for cat, raw in ordered:
        cond, samples = strip_samples(raw)
        if cat != "other" and not cond:
            raise ValueError(f"{kind} {locale}/{cat}: empty condition")
        try:
            bc = compile_condition(cond)
        except ValueError as exc:
            raise ValueError(f"{kind} {locale}/{cat}: {exc}") from exc
        compiled.rules.append((CAT_TO_ID[cat], bc))
        compiled.samples.extend(harvest_samples(samples, cat))
    return compiled


def compile_all(cardinal: dict, ordinal: dict) -> tuple[list[CompiledLocale], list[CompiledLocale]]:
    cards = []
    for loc in sorted(cardinal.keys()):
        cards.append(compile_locale_rules(loc, cardinal[loc], "cardinal"))
    ords = []
    for loc in sorted(ordinal.keys()):
        ords.append(compile_locale_rules(loc, ordinal[loc], "ordinal"))
    return cards, ords

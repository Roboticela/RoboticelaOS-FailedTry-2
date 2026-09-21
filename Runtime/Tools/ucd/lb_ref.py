#!/usr/bin/env python3
"""
Unicode 16.0.0 UAX #14 Line Breaking — reference implementation.

Loads UCD property files from Tools/ucd/source/ and implements LB1–LB31
exactly as specified for Unicode 16.0 (tr14-53 / LineBreakTest-16.0.0).

Public API:
  line_break_before(cps, index) -> bool
  line_break_kind(cps, index) -> "prohibited" | "allowed" | "mandatory"
"""

from __future__ import annotations

from pathlib import Path

SRC = Path(__file__).resolve().parent / "source"
MAX_CP = 0x110000
HYPHEN_CP = 0x2010
DOTTED_CIRCLE = 0x25CC

# ---------------------------------------------------------------------------
# Property tables (filled by load_data)
# ---------------------------------------------------------------------------

_lb_raw: list[str] = []
_gc: list[str] = []
_eaw: list[str] = []
_ext_pict: list[bool] = []
_loaded = False

NO_BREAK_CM_BASE = frozenset({"BK", "CR", "LF", "NL", "SP", "ZW"})
HARD_BREAK_AFTER = frozenset({"BK", "CR", "LF", "NL"})


def _parse_cp(tok: str) -> int:
    return int(tok.strip(), 16)


def _parse_range(field: str) -> tuple[int, int]:
    field = field.strip()
    if ".." in field:
        a, b = field.split("..")
        return _parse_cp(a), _parse_cp(b)
    cp = _parse_cp(field)
    return cp, cp


def _load_linebreak(path: Path) -> list[str]:
    arr = ["XX"] * MAX_CP
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line or line.startswith("@"):
                continue
            rng, cls = line.split(";")
            a, b = _parse_range(rng)
            cls = cls.strip()
            for cp in range(a, b + 1):
                arr[cp] = cls
    return arr


def _load_gc(path: Path) -> list[str]:
    """General_Category from UnicodeData.txt; unlisted => Cn."""
    arr = ["Cn"] * MAX_CP
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(";")
            cp = int(parts[0], 16)
            name = parts[1]
            gc = parts[2]
            if name.endswith(", First>"):
                # range start; next line is Last
                first = cp
                continue
            if name.endswith(", Last>"):
                for c in range(first, cp + 1):
                    arr[c] = gc
                continue
            arr[cp] = gc
    return arr


def _load_eaw(path: Path) -> list[str]:
    """East_Asian_Width with UAX #11 defaults for unassigned CJK / planes 2–3."""
    arr = ["N"] * MAX_CP
    # Default Wide ranges for unassigned (overridden by explicit entries)
    for a, b in (
        (0x3400, 0x4DBF),
        (0x4E00, 0x9FFF),
        (0xF900, 0xFAFF),
        (0x20000, 0x2FFFD),
        (0x30000, 0x3FFFD),
    ):
        for cp in range(a, b + 1):
            arr[cp] = "W"
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line or line.startswith("@"):
                continue
            rng, val = line.split(";")
            a, b = _parse_range(rng)
            val = val.strip()
            for cp in range(a, b + 1):
                arr[cp] = val
    return arr


def _load_ext_pict(path: Path) -> list[bool]:
    arr = [False] * MAX_CP
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            raw = line.split("#", 1)[0].strip()
            if not raw or ";" not in raw:
                continue
            rng, prop = raw.split(";")
            if prop.strip() != "Extended_Pictographic":
                continue
            a, b = _parse_range(rng)
            for cp in range(a, b + 1):
                arr[cp] = True
    return arr


def load_data(src: Path | None = None) -> None:
    global _lb_raw, _gc, _eaw, _ext_pict, _loaded
    src = src or SRC
    _lb_raw = _load_linebreak(src / "LineBreak.txt")
    _gc = _load_gc(src / "UnicodeData.txt")
    _eaw = _load_eaw(src / "EastAsianWidth.txt")
    _ext_pict = _load_ext_pict(src / "emoji-data.txt")
    _loaded = True


def _ensure() -> None:
    if not _loaded:
        load_data()


# ---------------------------------------------------------------------------
# LB1 resolution and property helpers
# ---------------------------------------------------------------------------

def lb1(cp: int) -> str:
    """Resolve Line_Break class per LB1 (default mapping). CB is left alone."""
    _ensure()
    raw = _lb_raw[cp]
    if raw in ("AI", "SG", "XX"):
        return "AL"
    if raw == "SA":
        return "CM" if _gc[cp] in ("Mn", "Mc") else "AL"
    if raw == "CJ":
        return "NS"
    return raw


def is_east_asian(cp: int) -> bool:
    _ensure()
    return _eaw[cp] in ("F", "W", "H")


def is_qu_pi(cp: int) -> bool:
    _ensure()
    return lb1(cp) == "QU" and _gc[cp] == "Pi"


def is_qu_pf(cp: int) -> bool:
    _ensure()
    return lb1(cp) == "QU" and _gc[cp] == "Pf"


def is_ext_pict_cn(cp: int) -> bool:
    _ensure()
    return _ext_pict[cp] and _gc[cp] == "Cn"


def _is_cm(c: str) -> bool:
    return c in ("CM", "ZWJ")


# ---------------------------------------------------------------------------
# Sequence helpers (after LB1)
# ---------------------------------------------------------------------------

def _classes(cps: list[int]) -> list[str]:
    return [lb1(cp) for cp in cps]


def _skip_cm_back(cls: list[str], j: int) -> int:
    """From index j, skip trailing CM/ZWJ; return base index or -1."""
    while j >= 0 and _is_cm(cls[j]):
        j -= 1
    return j


def _left_base(cls: list[str], i: int) -> tuple[str | None, int]:
    """
    Effective class immediately before break position i (LB9/LB10).
    Returns (class, index_of_base_or_char). sot => (None, -1).
    """
    if i <= 0:
        return None, -1
    j = i - 1
    if _is_cm(cls[j]):
        k = _skip_cm_back(cls, j)
        if k >= 0 and cls[k] not in NO_BREAK_CM_BASE:
            return cls[k], k
        return "AL", j  # LB10 orphan
    return cls[j], j


def _right_eff(cls: list[str], i: int) -> str | None:
    """Effective class at i for rules after LB9 (orphan CM/ZWJ => AL)."""
    if i >= len(cls):
        return None
    c = cls[i]
    if _is_cm(c):
        return "AL"  # LB10 (LB9 already handled attaching CM)
    return c


def _match_zw_sp_star_div(cls: list[str], i: int) -> bool:
    """LB8: ZW SP* ÷  — break before i if chars before i are ZW followed by spaces."""
    j = i - 1
    while j >= 0 and cls[j] == "SP":
        j -= 1
    return j >= 0 and cls[j] == "ZW"


def _op_sp_star(cls: list[str], i: int) -> bool:
    """True if OP SP* immediately precedes position i (CM after OP ignored via base)."""
    j = i - 1
    while j >= 0 and cls[j] == "SP":
        j -= 1
    if j < 0:
        return False
    c, _ = _left_base(cls, j + 1)
    return c == "OP"


def _cl_cp_sp_star(cls: list[str], i: int) -> bool:
    j = i - 1
    while j >= 0 and cls[j] == "SP":
        j -= 1
    if j < 0:
        return False
    c, _ = _left_base(cls, j + 1)
    return c in ("CL", "CP")


def _b2_sp_star(cls: list[str], i: int) -> bool:
    j = i - 1
    while j >= 0 and cls[j] == "SP":
        j -= 1
    if j < 0:
        return False
    c, _ = _left_base(cls, j + 1)
    return c == "B2"


def _lb15a(cls: list[str], cps: list[int], i: int) -> bool:
    """
    LB15a: (sot|BK|CR|LF|NL|OP|QU|GL|SP|ZW) [Pi∩QU] SP* ×
    Break position i is after the SP*; right side is whatever follows.
    """
    # Walk back over spaces
    j = i - 1
    while j >= 0 and cls[j] == "SP":
        j -= 1
    if j < 0:
        return False
    # Must be QU∩Pi (possibly with CM treated as base)
    if _is_cm(cls[j]):
        k = _skip_cm_back(cls, j)
        if k < 0 or cls[k] in NO_BREAK_CM_BASE:
            # orphan — not QU
            return False
        qu_idx = k
    else:
        qu_idx = j
    if not is_qu_pi(cps[qu_idx]):
        return False
    # Character before the QU∩Pi
    before_c, _ = _left_base(cls, qu_idx)
    if before_c is None:
        return True  # sot
    return before_c in ("BK", "CR", "LF", "NL", "OP", "QU", "GL", "SP", "ZW")


def _lb15b(cls: list[str], cps: list[int], i: int) -> bool:
    """
    LB15b: × [Pf∩QU] ( SP | GL | WJ | CL | QU | CP | EX | IS | SY
                       | BK | CR | LF | NL | ZW | eot )
    """
    if i >= len(cps):
        return False
    if not is_qu_pf(cps[i]):
        # right might be CM attached — only care about char at i being Pf∩QU
        return False
    # What follows the QU∩Pf (skip its CM)
    k = i + 1
    while k < len(cls) and _is_cm(cls[k]):
        k += 1
    if k >= len(cls):
        return True  # eot
    fol = cls[k]
    return fol in (
        "SP", "GL", "WJ", "CL", "QU", "CP", "EX", "IS", "SY",
        "BK", "CR", "LF", "NL", "ZW",
    )


def _ri_odd_pair(cls: list[str], i: int) -> bool:
    """
    LB30a: do not break between RI RI when odd number of RIs before the break
    counting from sot or last non-RI (after CM collapsing).
    Break before i where both sides are RI => × if odd count of RI before i.
    """
    # Count RI to the left of i, skipping CM attached to them.
    # Walk collapsed sequence.
    count = 0
    j = i - 1
    while j >= 0:
        if _is_cm(cls[j]):
            k = _skip_cm_back(cls, j)
            if k < 0 or cls[k] in NO_BREAK_CM_BASE:
                break  # orphan AL — not RI
            if cls[k] != "RI":
                break
            count += 1
            j = k - 1
            continue
        if cls[j] != "RI":
            break
        count += 1
        j -= 1
    # × when odd number of RIs before the break (i.e. count is odd)
    return (count % 2) == 1


def _nu_sy_is_star_before(cls: list[str], i: int) -> bool:
    """
    True if immediately before i we match: NU (SY|IS)*
    (with CM collapsing: NU CM* (SY|IS CM*)*).
    Used for LB25.
    """
    j = i - 1
    if j < 0:
        return False
    # Skip trailing SY/IS (with their CM)
    while j >= 0:
        if _is_cm(cls[j]):
            k = _skip_cm_back(cls, j)
            if k < 0 or cls[k] in NO_BREAK_CM_BASE:
                return False
            if cls[k] in ("SY", "IS"):
                j = k - 1
                continue
            if cls[k] == "NU":
                return True
            return False
        if cls[j] in ("SY", "IS"):
            j -= 1
            continue
        if cls[j] == "NU":
            return True
        return False
    return False


def _nu_sy_is_star_cl_cp_before(cls: list[str], i: int, which: str) -> bool:
    """NU (SY|IS)* (CL|CP) before i."""
    j = i - 1
    if j < 0:
        return False
    # Resolve left base
    if _is_cm(cls[j]):
        k = _skip_cm_back(cls, j)
        if k < 0 or cls[k] in NO_BREAK_CM_BASE:
            return False
        if cls[k] != which:
            return False
        return _nu_sy_is_star_before(cls, k)
    if cls[j] != which:
        return False
    return _nu_sy_is_star_before(cls, j)


def _lb20a(cls: list[str], cps: list[int], i: int) -> bool:
    """
    LB20a: (sot|BK|CR|LF|NL|SP|ZW|CB|GL) (HY | U+2010) × AL
    """
    right = _right_eff(cls, i)
    if right != "AL":
        return False
    # left should be HY or U+2010 (BA)
    if i - 1 < 0:
        return False
    left_c, left_i = _left_base(cls, i)
    if left_c is None:
        return False
    if left_c == "HY":
        pass
    elif left_c == "BA" and cps[left_i] == HYPHEN_CP:
        pass
    else:
        return False
    # before the hyphen
    before_c, _ = _left_base(cls, left_i)
    if before_c is None:
        return True
    return before_c in ("BK", "CR", "LF", "NL", "SP", "ZW", "CB", "GL")


def _is_ak_as_or_dotted_circle(cls: list[str], cps: list[int], idx: int, c: str | None) -> bool:
    """LB28a set: AK | AS | U+25CC (dotted circle; lb often AL)."""
    if c in ("AK", "AS"):
        return True
    return 0 <= idx < len(cps) and not _is_cm(cls[idx]) and cps[idx] == DOTTED_CIRCLE


def _lb28a(cls: list[str], cps: list[int], i: int) -> bool:
    """Brahmic orthographic syllables — LB28a (U+25CC as [◌])."""
    n = len(cls)
    left, li = _left_base(cls, i)
    right = _right_eff(cls, i)
    if left is None or right is None:
        return False

    # AP × (AK | ◌ | AS)
    if left == "AP" and _is_ak_as_or_dotted_circle(cls, cps, i, right):
        return True

    # (AK | ◌ | AS) × (VF | VI)
    if _is_ak_as_or_dotted_circle(cls, cps, li, left) and right in ("VF", "VI"):
        return True

    # (AK | ◌ | AS) VI × (AK | ◌)
    if left == "VI":
        before, bi = _left_base(cls, li)
        if _is_ak_as_or_dotted_circle(cls, cps, bi, before):
            if right == "AK" or (i < n and not _is_cm(cls[i]) and cps[i] == DOTTED_CIRCLE):
                return True

    # (AK | ◌ | AS) × (AK | ◌ | AS) VF
    if _is_ak_as_or_dotted_circle(cls, cps, li, left) and _is_ak_as_or_dotted_circle(
        cls, cps, i, right
    ):
        k = i + 1
        while k < n and _is_cm(cls[k]):
            k += 1
        if k < n and cls[k] == "VF":
            return True

    return False


# ---------------------------------------------------------------------------
# Core algorithm
# ---------------------------------------------------------------------------

def line_break_kind(cps: list[int], index: int) -> str:
    """
    Return break kind before `index` in `cps`.
    index==0 => sot; index==len(cps) => eot.
    mandatory only for LB3/LB4/LB5 hard breaks / eot.
    """
    _ensure()
    n = len(cps)
    if index < 0 or index > n:
        raise IndexError("index out of range")
    if index == 0:
        return "prohibited"  # LB2
    if index == n:
        return "mandatory"  # LB3

    cls = _classes(cps)
    left_raw = cls[index - 1]
    right_raw = cls[index]

    # LB4
    if left_raw == "BK":
        return "mandatory"
    # LB5
    if left_raw == "CR" and right_raw == "LF":
        return "prohibited"
    if left_raw in ("CR", "LF", "NL"):
        return "mandatory"
    # LB6
    if right_raw in HARD_BREAK_AFTER:
        return "prohibited"
    # LB7
    if right_raw in ("SP", "ZW"):
        return "prohibited"
    # LB8
    if _match_zw_sp_star_div(cls, index):
        return "allowed"
    # LB8a
    if left_raw == "ZWJ":
        return "prohibited"
    # LB9: × CM|ZWJ when base allows
    if _is_cm(right_raw) and left_raw not in NO_BREAK_CM_BASE:
        return "prohibited"

    # Effective classes for subsequent rules
    left, left_i = _left_base(cls, index)
    right = _right_eff(cls, index)
    assert left is not None and right is not None

    # LB11
    if right == "WJ" or left == "WJ":
        return "prohibited"
    # LB12
    if left == "GL":
        return "prohibited"
    # LB12a: [^SP BA HY] × GL
    if right == "GL" and left not in ("SP", "BA", "HY"):
        return "prohibited"
    # LB13
    if right in ("CL", "CP", "EX", "SY"):
        return "prohibited"
    # LB14
    if _op_sp_star(cls, index):
        return "prohibited"
    # LB15a
    if _lb15a(cls, cps, index):
        return "prohibited"
    # LB15b
    if _lb15b(cls, cps, index):
        return "prohibited"
    # LB15c: SP ÷ IS NU
    if left_raw == "SP" and right == "IS":
        # look ahead for NU after IS (+CM)
        k = index + 1
        while k < n and _is_cm(cls[k]):
            k += 1
        if k < n and cls[k] == "NU":
            return "allowed"
    # LB15d
    if right == "IS":
        return "prohibited"
    # LB16
    if right == "NS" and _cl_cp_sp_star(cls, index):
        return "prohibited"
    # LB17
    if right == "B2" and _b2_sp_star(cls, index):
        return "prohibited"
    # LB18
    if left_raw == "SP":
        return "allowed"

    # LB19
    # × [QU - Pi]
    if right == "QU" and not is_qu_pi(cps[index]):
        return "prohibited"
    # [QU - Pf] ×
    if left == "QU" and left_i >= 0 and not is_qu_pf(cps[left_i]):
        return "prohibited"

    # LB19a
    # [^EastAsian] × QU
    if right == "QU" and left_i >= 0 and not is_east_asian(cps[left_i]):
        return "prohibited"
    # × QU ([^EastAsian] | eot)
    if right == "QU":
        k = index + 1
        while k < n and _is_cm(cls[k]):
            k += 1
        if k >= n or not is_east_asian(cps[k]):
            return "prohibited"
    # QU × [^EastAsian]
    if left == "QU":
        if _is_cm(cls[index]) or not is_east_asian(cps[index]):
            # orphan CM ⇒ LB10 AL with EAW=Na (not EastAsian)
            return "prohibited"
    # (sot | [^EastAsian]) QU ×
    if left == "QU":
        before, bi = _left_base(cls, left_i)
        if before is None or (bi >= 0 and not is_east_asian(cps[bi])):
            return "prohibited"

    # LB20
    if right == "CB" or left == "CB":
        return "allowed"

    # LB20a
    if _lb20a(cls, cps, index):
        return "prohibited"

    # LB21
    if right in ("BA", "HY", "NS"):
        return "prohibited"
    if left == "BB":
        return "prohibited"

    # LB21a: HL (HY | [BA - EastAsian]) × [^HL]
    if left in ("HY", "BA") and right != "HL":
        if left == "BA" and left_i >= 0 and is_east_asian(cps[left_i]):
            pass  # not NonEastAsian BA
        else:
            before, _ = _left_base(cls, left_i)
            if before == "HL" and (left == "HY" or left == "BA"):
                return "prohibited"

    # LB21b
    if left == "SY" and right == "HL":
        return "prohibited"

    # LB22
    if right == "IN":
        return "prohibited"

    # LB23
    if left in ("AL", "HL") and right == "NU":
        return "prohibited"
    if left == "NU" and right in ("AL", "HL"):
        return "prohibited"

    # LB23a
    if left == "PR" and right in ("ID", "EB", "EM"):
        return "prohibited"
    if left in ("ID", "EB", "EM") and right == "PO":
        return "prohibited"

    # LB24
    if left in ("PR", "PO") and right in ("AL", "HL"):
        return "prohibited"
    if left in ("AL", "HL") and right in ("PR", "PO"):
        return "prohibited"

    # LB25
    if _lb25(cls, index, left, right):
        return "prohibited"

    # LB26
    if left == "JL" and right in ("JL", "JV", "H2", "H3"):
        return "prohibited"
    if left in ("JV", "H2") and right in ("JV", "JT"):
        return "prohibited"
    if left in ("JT", "H3") and right == "JT":
        return "prohibited"

    # LB27
    if left in ("JL", "JV", "JT", "H2", "H3") and right == "PO":
        return "prohibited"
    if left == "PR" and right in ("JL", "JV", "JT", "H2", "H3"):
        return "prohibited"

    # LB28
    if left in ("AL", "HL") and right in ("AL", "HL"):
        return "prohibited"

    # LB28a
    if _lb28a(cls, cps, index):
        return "prohibited"

    # LB29
    if left == "IS" and right in ("AL", "HL"):
        return "prohibited"

    # LB30
    if left in ("AL", "HL", "NU") and right == "OP" and not is_east_asian(cps[index]):
        return "prohibited"
    if left == "CP" and left_i >= 0 and not is_east_asian(cps[left_i]) and right in ("AL", "HL", "NU"):
        return "prohibited"

    # LB30a
    if left == "RI" and right == "RI" and _ri_odd_pair(cls, index):
        return "prohibited"

    # LB30b
    if left == "EB" and right == "EM":
        return "prohibited"
    if right == "EM" and left_i >= 0 and is_ext_pict_cn(cps[left_i]):
        return "prohibited"

    # LB31
    return "allowed"


def _lb25(cls: list[str], i: int, left: str, right: str) -> bool:
    """LB25 number patterns; True => prohibited break."""
    n = len(cls)

    # NU (SY|IS)* CL × PO/PR
    # NU (SY|IS)* CP × PO/PR
    # NU (SY|IS)* × PO/PR
    # NU (SY|IS)* × NU
    if right in ("PO", "PR", "NU"):
        if _nu_sy_is_star_cl_cp_before(cls, i, "CL") and right in ("PO", "PR"):
            return True
        if _nu_sy_is_star_cl_cp_before(cls, i, "CP") and right in ("PO", "PR"):
            return True
        if _nu_sy_is_star_before(cls, i) and right in ("PO", "PR", "NU"):
            return True

    # PO × OP NU
    # PO × OP IS NU
    # PO × NU
    if left == "PO":
        if right == "NU":
            return True
        if right == "OP":
            k = i + 1
            while k < n and _is_cm(cls[k]):
                k += 1
            if k < n and cls[k] == "NU":
                return True
            if k < n and cls[k] == "IS":
                k2 = k + 1
                while k2 < n and _is_cm(cls[k2]):
                    k2 += 1
                if k2 < n and cls[k2] == "NU":
                    return True

    # PR × OP NU
    # PR × OP IS NU
    # PR × NU
    if left == "PR":
        if right == "NU":
            return True
        if right == "OP":
            k = i + 1
            while k < n and _is_cm(cls[k]):
                k += 1
            if k < n and cls[k] == "NU":
                return True
            if k < n and cls[k] == "IS":
                k2 = k + 1
                while k2 < n and _is_cm(cls[k2]):
                    k2 += 1
                if k2 < n and cls[k2] == "NU":
                    return True

    # HY × NU
    if left == "HY" and right == "NU":
        return True
    # IS × NU
    if left == "IS" and right == "NU":
        return True

    return False


def line_break_before(cps: list[int], index: int) -> bool:
    """True if a line break is allowed (or mandatory) before `index`."""
    return line_break_kind(cps, index) != "prohibited"


# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------

def _parse_test_line(line: str) -> tuple[list[int], list[bool]] | None:
    """
    Parse a LineBreakTest line into (codepoints, breaks_before).
    breaks_before[i] is True if ÷ before codepoint i; breaks_before[n] for eot.
    Leading ×/÷ before first CP is position 0; trailing is eot.
    """
    s = line.split("#", 1)[0].strip()
    if not s:
        return None
    tokens = s.split()
    cps: list[int] = []
    # breaks[k] corresponds to break before the k-th code point; final for eot
    breaks: list[bool] = []
    pending_break: bool | None = None
    for tok in tokens:
        if tok in ("×", "÷"):
            pending_break = tok == "÷"
        else:
            if pending_break is None:
                # malformed
                return None
            breaks.append(pending_break)
            cps.append(int(tok, 16))
            pending_break = None
    if pending_break is None:
        return None
    breaks.append(pending_break)  # eot
    return cps, breaks


def run_tests(test_path: Path | None = None) -> int:
    _ensure()
    test_path = test_path or (SRC / "LineBreakTest.txt")
    passed = 0
    failed = 0
    failures: list[str] = []
    case_idx = 0
    with test_path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parsed = _parse_test_line(line)
            if parsed is None:
                continue
            cps, expected = parsed
            case_idx += 1
            ok = True
            for pos in range(len(expected)):
                got = line_break_before(cps, pos)
                if got != expected[pos]:
                    ok = False
                    if len(failures) < 20:
                        exp = "÷" if expected[pos] else "×"
                        act = "÷" if got else "×"
                        failures.append(
                            f"case {case_idx} pos {pos}: expected {exp} got {act} "
                            f"cps={[f'{c:04X}' for c in cps]}"
                        )
            if ok:
                passed += 1
            else:
                failed += 1

    print(f"LineBreakTest: {passed} passed, {failed} failed (of {passed + failed})")
    for msg in failures:
        print("  FAIL:", msg)
    return failed


if __name__ == "__main__":
    raise SystemExit(run_tests())

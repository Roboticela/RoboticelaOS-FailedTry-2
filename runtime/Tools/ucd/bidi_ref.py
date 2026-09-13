#!/usr/bin/env python3
"""
Unicode 16.0.0 UAX #9 Bidirectional Algorithm — reference implementation.

Loads UCD property files from Tools/ucd/source/ and implements the full UBA
through rule L2 (P1–P3, X1–X10, W1–W7, N0–N2, I1–I2, L1–L2) for conformance
with BidiCharacterTest.txt / BidiTest.txt (Unicode 16.0.0).

Public API:
  bidi_class(cp) -> str
  analyze(codepoints, paragraph_level=None) -> BidiResult
  run_character_tests() / run_abstract_tests()  (also via __main__)
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

SRC = Path(__file__).resolve().parent / "source"
MAX_CP = 0x110000
MAX_DEPTH = 125
MAX_BRACKET_STACK = 63

# ---------------------------------------------------------------------------
# Property tables
# ---------------------------------------------------------------------------

_bidi: list[str] = []
_bracket_pair: dict[int, int] = {}  # cp -> paired bracket cp
_bracket_type: dict[int, str] = {}  # cp -> 'o' | 'c'
_loaded = False

REMOVE_TYPES = frozenset({"RLE", "LRE", "RLO", "LRO", "PDF", "BN"})
EXPLICIT_EMBED = frozenset({"RLE", "LRE", "RLO", "LRO"})
ISOLATE_INIT = frozenset({"LRI", "RLI", "FSI"})
NI_TYPES = frozenset({"B", "S", "WS", "ON", "FSI", "LRI", "RLI", "PDI"})
STRONG_P2 = frozenset({"L", "R", "AL"})

LONG_TO_SHORT = {
    "Left_To_Right": "L",
    "Right_To_Left": "R",
    "Arabic_Letter": "AL",
    "European_Number": "EN",
    "European_Separator": "ES",
    "European_Terminator": "ET",
    "Arabic_Number": "AN",
    "Common_Separator": "CS",
    "Nonspacing_Mark": "NSM",
    "Boundary_Neutral": "BN",
    "Paragraph_Separator": "B",
    "Segment_Separator": "S",
    "White_Space": "WS",
    "Whitespace": "WS",
    "Other_Neutral": "ON",
    "Left_To_Right_Embedding": "LRE",
    "Left_To_Right_Override": "LRO",
    "Right_To_Left_Embedding": "RLE",
    "Right_To_Left_Override": "RLO",
    "Pop_Directional_Format": "PDF",
    "Left_To_Right_Isolate": "LRI",
    "Right_To_Left_Isolate": "RLI",
    "First_Strong_Isolate": "FSI",
    "Pop_Directional_Isolate": "PDI",
}

# U+232A RIGHT-POINTING ANGLE BRACKET <-> U+3009 RIGHT ANGLE BRACKET
_BRACKET_EQUIV = {0x232A: 0x3009, 0x3009: 0x232A}


def _parse_cp(tok: str) -> int:
    return int(tok.strip(), 16)


def _parse_range(field: str) -> tuple[int, int]:
    field = field.strip()
    if ".." in field:
        a, b = field.split("..")
        return _parse_cp(a), _parse_cp(b)
    cp = _parse_cp(field)
    return cp, cp


def _normalize_bc(name: str) -> str:
    name = name.strip()
    if name in LONG_TO_SHORT:
        return LONG_TO_SHORT[name]
    return name


def load_data() -> None:
    global _bidi, _bracket_pair, _bracket_type, _loaded
    if _loaded:
        return

    # Default L; apply @missing; then explicit DerivedBidiClass entries.
    _bidi = ["L"] * MAX_CP
    path = SRC / "DerivedBidiClass.txt"
    with path.open("r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("# @missing:"):
                body = line[len("# @missing:") :].strip()
                rng, cls = body.split(";")
                a, b = _parse_range(rng)
                cls = _normalize_bc(cls)
                for cp in range(a, b + 1):
                    _bidi[cp] = cls
                continue
            if line.startswith("#"):
                continue
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            rng, cls = line.split(";")
            a, b = _parse_range(rng)
            cls = _normalize_bc(cls)
            for cp in range(a, b + 1):
                _bidi[cp] = cls

    _bracket_pair = {}
    _bracket_type = {}
    bpath = SRC / "BidiBrackets.txt"
    with bpath.open("r", encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = [p.strip() for p in line.split(";")]
            if len(parts) < 3:
                continue
            cp = _parse_cp(parts[0])
            paired = _parse_cp(parts[1])
            bpt = parts[2]
            _bracket_pair[cp] = paired
            _bracket_type[cp] = bpt

    _loaded = True


def bidi_class(cp: int) -> str:
    """Return Bidi_Class for a code point (Unicode 16.0 DerivedBidiClass)."""
    load_data()
    if cp < 0 or cp >= MAX_CP:
        return "L"
    return _bidi[cp]


# ---------------------------------------------------------------------------
# Result
# ---------------------------------------------------------------------------


@dataclass
class BidiResult:
    paragraph_level: int
    levels: list  # int or None for X9-removed (tests use 'x')
    types: list[str]  # final resolved types (after W/N; informative)
    visual_to_logical: list[int]  # indices of non-removed chars in visual order
    logical_to_visual: list[Optional[int]]  # per original index; None if removed


# ---------------------------------------------------------------------------
# Paragraph level (P2–P3) and matching PDI (BD9)
# ---------------------------------------------------------------------------


def _matching_pdis(types: list[str]) -> list[int]:
    """For each index, matching PDI index or -1 (BD9)."""
    n = len(types)
    match = [-1] * n
    for i, t in enumerate(types):
        if t not in ISOLATE_INIT:
            continue
        depth = 1
        for j in range(i + 1, n):
            tj = types[j]
            if tj in ISOLATE_INIT:
                depth += 1
            elif tj == "PDI":
                depth -= 1
                if depth == 0:
                    match[i] = j
                    break
    return match


def _matching_initiators(matching_pdi: list[int]) -> list[int]:
    """Inverse map: PDI index -> isolate initiator index, else -1."""
    inv = [-1] * len(matching_pdi)
    for i, p in enumerate(matching_pdi):
        if p >= 0:
            inv[p] = i
    return inv


def determine_paragraph_level(types: list[str], start: int = 0, end: Optional[int] = None) -> int:
    """P2–P3 over types[start:end]."""
    if end is None:
        end = len(types)
    i = start
    while i < end:
        t = types[i]
        if t in ISOLATE_INIT:
            # skip to matching PDI or end
            depth = 1
            i += 1
            while i < end:
                if types[i] in ISOLATE_INIT:
                    depth += 1
                elif types[i] == "PDI":
                    depth -= 1
                    if depth == 0:
                        i += 1
                        break
                i += 1
            continue
        if t in STRONG_P2:
            return 1 if t in ("R", "AL") else 0
        i += 1
    return 0


def _least_odd_gt(level: int) -> int:
    return level + 1 if (level % 2) == 0 else level + 2


def _least_even_gt(level: int) -> int:
    return level + 1 if (level % 2) == 1 else level + 2


# ---------------------------------------------------------------------------
# Explicit levels X1–X8
# ---------------------------------------------------------------------------


@dataclass
class _StackEntry:
    level: int
    override: str  # 'N', 'L', or 'R'
    isolate: bool


def resolve_explicit(
    types: list[str],
    paragraph_level: int,
    matching_pdi: list[int],
) -> tuple[list[int], list[str]]:
    """
    Apply X1–X8. Returns (levels, types).
    Levels for X9-removed characters are left as -1 (unused).
    """
    n = len(types)
    levels = [-1] * n
    types = list(types)

    stack: list[_StackEntry] = [
        _StackEntry(paragraph_level, "N", False),
    ]
    overflow_isolate = 0
    overflow_embedding = 0
    valid_isolate = 0

    def top() -> _StackEntry:
        return stack[-1]

    i = 0
    while i < n:
        t = types[i]

        if t == "RLE":
            new_level = _least_odd_gt(top().level)
            if (
                new_level <= MAX_DEPTH
                and overflow_isolate == 0
                and overflow_embedding == 0
            ):
                stack.append(_StackEntry(new_level, "N", False))
            elif overflow_isolate == 0:
                overflow_embedding += 1
            i += 1
            continue

        if t == "LRE":
            new_level = _least_even_gt(top().level)
            if (
                new_level <= MAX_DEPTH
                and overflow_isolate == 0
                and overflow_embedding == 0
            ):
                stack.append(_StackEntry(new_level, "N", False))
            elif overflow_isolate == 0:
                overflow_embedding += 1
            i += 1
            continue

        if t == "RLO":
            new_level = _least_odd_gt(top().level)
            if (
                new_level <= MAX_DEPTH
                and overflow_isolate == 0
                and overflow_embedding == 0
            ):
                stack.append(_StackEntry(new_level, "R", False))
            elif overflow_isolate == 0:
                overflow_embedding += 1
            i += 1
            continue

        if t == "LRO":
            new_level = _least_even_gt(top().level)
            if (
                new_level <= MAX_DEPTH
                and overflow_isolate == 0
                and overflow_embedding == 0
            ):
                stack.append(_StackEntry(new_level, "L", False))
            elif overflow_isolate == 0:
                overflow_embedding += 1
            i += 1
            continue

        if t in ISOLATE_INIT:
            # X5c: FSI → RLI or LRI based on P2/P3 of contents
            effective = t
            if t == "FSI":
                pdi = matching_pdi[i]
                end = pdi if pdi >= 0 else n
                pl = determine_paragraph_level(types, i + 1, end)
                effective = "RLI" if pl == 1 else "LRI"

            levels[i] = top().level
            if top().override != "N":
                types[i] = top().override

            if effective == "RLI":
                new_level = _least_odd_gt(top().level)
            else:
                new_level = _least_even_gt(top().level)

            if (
                new_level <= MAX_DEPTH
                and overflow_isolate == 0
                and overflow_embedding == 0
            ):
                valid_isolate += 1
                stack.append(_StackEntry(new_level, "N", True))
            else:
                overflow_isolate += 1
            i += 1
            continue

        if t == "PDI":
            if overflow_isolate > 0:
                overflow_isolate -= 1
            elif valid_isolate == 0:
                pass
            else:
                overflow_embedding = 0
                while stack and not top().isolate:
                    stack.pop()
                if stack and top().isolate:
                    stack.pop()
                    valid_isolate -= 1
            levels[i] = top().level
            if top().override != "N":
                types[i] = top().override
            i += 1
            continue

        if t == "PDF":
            if overflow_isolate > 0:
                pass
            elif overflow_embedding > 0:
                overflow_embedding -= 1
            elif len(stack) >= 2 and not top().isolate:
                stack.pop()
            i += 1
            continue

        if t == "B":
            # X8: paragraph separator gets paragraph level; embeddings end
            levels[i] = paragraph_level
            stack = [_StackEntry(paragraph_level, "N", False)]
            overflow_isolate = 0
            overflow_embedding = 0
            valid_isolate = 0
            i += 1
            continue

        if t == "BN":
            # No level assigned; removed by X9
            i += 1
            continue

        # X6: everything else
        levels[i] = top().level
        if top().override != "N":
            types[i] = top().override
        i += 1

    return levels, types


# ---------------------------------------------------------------------------
# Isolating run sequences (BD13 / X10)
# ---------------------------------------------------------------------------


def _level_runs(indices: list[int], levels: list[int]) -> list[list[int]]:
    if not indices:
        return []
    runs: list[list[int]] = []
    cur = [indices[0]]
    for idx in indices[1:]:
        if levels[idx] == levels[cur[0]]:
            cur.append(idx)
        else:
            runs.append(cur)
            cur = [idx]
    runs.append(cur)
    return runs


def _isolating_run_sequences(
    levels: list[int],
    original_types: list[str],
    matching_pdi: list[int],
    matching_init: list[int],
) -> list[dict]:
    """
    Build isolating run sequences after X9 conceptual removal.
    Each sequence: {
      'indices': [char indices in logical order within the IRS],
      'sos': 'L'|'R',
      'eos': 'L'|'R',
      'level': embedding level,
    }
    Isolate initiator identity for BD13 uses *original* types + matching_pdi.
    """
    n = len(levels)
    kept = [i for i in range(n) if original_types[i] not in REMOVE_TYPES]
    runs = _level_runs(kept, levels)
    if not runs:
        return []

    # Map char index -> run index
    run_of = [-1] * n
    for ri, run in enumerate(runs):
        for idx in run:
            run_of[idx] = ri

    # BD13 algorithm
    assigned = [False] * len(runs)
    sequences_runs: list[list[int]] = []

    for ri, run in enumerate(runs):
        if assigned[ri]:
            continue
        first = run[0]
        # Skip runs that start with a PDI matching some isolate (they are
        # picked up when continuing from the initiator's run).
        if original_types[first] == "PDI" and matching_init[first] >= 0:
            # Only start here if the matching initiator's run was not a
            # valid predecessor — BD13 says start when first char is not a
            # matching PDI.
            continue

        seq = [ri]
        assigned[ri] = True
        while True:
            last_char = runs[seq[-1]][-1]
            if (
                original_types[last_char] in ISOLATE_INIT
                and matching_pdi[last_char] >= 0
            ):
                pdi = matching_pdi[last_char]
                rj = run_of[pdi]
                if rj < 0 or assigned[rj]:
                    break
                # matching PDI must be first character of its level run
                if runs[rj][0] != pdi:
                    break
                seq.append(rj)
                assigned[rj] = True
            else:
                break
        sequences_runs.append(seq)

    # Any unassigned runs (should not happen if BD13 is complete) — assign alone
    for ri in range(len(runs)):
        if not assigned[ri]:
            sequences_runs.append([ri])
            assigned[ri] = True

    paragraph_level_for_sos = None  # filled by caller via levels of neighbors

    result = []
    for seq in sequences_runs:
        indices: list[int] = []
        for ri in seq:
            indices.extend(runs[ri])
        level = levels[indices[0]]
        result.append(
            {
                "indices": indices,
                "level": level,
                "sos": None,  # filled below
                "eos": None,
            }
        )

    # sos / eos (X10)
    # Build ordered list of kept positions for neighbor lookup
    for seq in result:
        indices = seq["indices"]
        first, last = indices[0], indices[-1]
        # preceding kept character
        prev_level = None
        for j in range(first - 1, -1, -1):
            if original_types[j] not in REMOVE_TYPES:
                prev_level = levels[j]
                break
        # following kept character — unless last is isolate initiator lacking matching PDI
        next_level = None
        use_para_for_eos = False
        if original_types[last] in ISOLATE_INIT and matching_pdi[last] < 0:
            use_para_for_eos = True
        else:
            for j in range(last + 1, n):
                if original_types[j] not in REMOVE_TYPES:
                    next_level = levels[j]
                    break
            if next_level is None:
                use_para_for_eos = True

        # paragraph embedding level is the level of the "virtual" ends;
        # caller must pass it — we store placeholder and fix in analyze().
        seq["_prev_level"] = prev_level
        seq["_next_level"] = next_level
        seq["_use_para_eos"] = use_para_for_eos

    return result


def _sos_eos_type(level_a: int, level_b: int) -> str:
    return "R" if (max(level_a, level_b) % 2) == 1 else "L"


# ---------------------------------------------------------------------------
# Weak types W1–W7
# ---------------------------------------------------------------------------


def _apply_W(types: list[str], indices: list[int], sos: str) -> None:
    """Resolve weak types in-place on types[] for one IRS."""
    if not indices:
        return
    m = len(indices)

    def T(k: int) -> str:
        """Type at IRS-local position k; k==-1 → sos."""
        if k < 0:
            return sos
        return types[indices[k]]

    def setT(k: int, v: str) -> None:
        types[indices[k]] = v

    # W1: NSM
    for k in range(m):
        if T(k) != "NSM":
            continue
        if k == 0:
            setT(k, sos)
        else:
            prev = T(k - 1)
            if prev in ISOLATE_INIT or prev == "PDI":
                setT(k, "ON")
            else:
                setT(k, prev)

    # W2: EN → AN after AL
    for k in range(m):
        if T(k) != "EN":
            continue
        j = k - 1
        while j >= 0 and T(j) not in ("R", "L", "AL"):
            j -= 1
        strong = sos if j < 0 else T(j)
        if strong == "AL":
            setT(k, "AN")

    # W3: AL → R
    for k in range(m):
        if T(k) == "AL":
            setT(k, "R")

    # W4: ES/CS between numbers
    for k in range(1, m - 1):
        t = T(k)
        if t == "ES":
            if T(k - 1) == "EN" and T(k + 1) == "EN":
                setT(k, "EN")
        elif t == "CS":
            left, right = T(k - 1), T(k + 1)
            if left == "EN" and right == "EN":
                setT(k, "EN")
            elif left == "AN" and right == "AN":
                setT(k, "AN")

    # W5: ET adjacent to EN → EN
    # First mark ETs that should become EN
    change = [False] * m
    for k in range(m):
        if T(k) != "ET":
            continue
        # adjacent to EN on left or right (through ET sequence)
        # Check if this ET is in a run of ETs that touches an EN
        # Left side
        j = k - 1
        while j >= 0 and T(j) == "ET":
            j -= 1
        if j >= 0 and T(j) == "EN":
            change[k] = True
            continue
        # Right side
        j = k + 1
        while j < m and T(j) == "ET":
            j += 1
        if j < m and T(j) == "EN":
            change[k] = True
    for k in range(m):
        if change[k]:
            setT(k, "EN")

    # W6: remaining ES, CS, ET → ON
    for k in range(m):
        if T(k) in ("ES", "CS", "ET"):
            setT(k, "ON")

    # W7: EN → L after L
    for k in range(m):
        if T(k) != "EN":
            continue
        j = k - 1
        while j >= 0 and T(j) not in ("R", "L"):
            j -= 1
        strong = sos if j < 0 else T(j)
        if strong == "L":
            setT(k, "L")


# ---------------------------------------------------------------------------
# Neutrals N0–N2
# ---------------------------------------------------------------------------


def _brackets_equal(a: int, b: int) -> bool:
    if a == b:
        return True
    return _BRACKET_EQUIV.get(a) == b or _BRACKET_EQUIV.get(b) == a


def _identify_bracket_pairs(
    codepoints: list[int],
    types: list[str],
    indices: list[int],
) -> list[tuple[int, int]]:
    """BD16: return list of (open_index, close_index) in IRS local... actually
    returns pairs of *paragraph* indices, sorted by opening position."""
    stack: list[tuple[int, int]] = []  # (expected_closer_cp, open_pos)
    pairs: list[tuple[int, int]] = []

    for pos in indices:
        cp = codepoints[pos]
        if types[pos] != "ON":
            continue
        bpt = _bracket_type.get(cp)
        if bpt == "o":
            if len(stack) >= MAX_BRACKET_STACK:
                return []  # BD16: abort, empty list
            stack.append((_bracket_pair[cp], pos))
        elif bpt == "c":
            if not stack:
                continue
            # search stack from top for match
            found = -1
            for si in range(len(stack) - 1, -1, -1):
                expected, open_pos = stack[si]
                if _brackets_equal(cp, expected):
                    found = si
                    pairs.append((open_pos, pos))
                    del stack[si:]  # pop through current inclusively
                    break
            # if no match, leave stack unchanged (step 5)

    pairs.sort(key=lambda p: p[0])
    return pairs


def _n0_strong(t: str) -> Optional[str]:
    """Strong type for N0; EN/AN treated as R. Returns 'L', 'R', or None."""
    if t == "L":
        return "L"
    if t in ("R", "EN", "AN"):
        return "R"
    return None


def _apply_N(
    codepoints: list[int],
    types: list[str],
    indices: list[int],
    sos: str,
    eos: str,
    embedding_level: int,
    was_nsm: list[bool],
) -> None:
    if not indices:
        return
    m = len(indices)
    e = "R" if (embedding_level % 2) == 1 else "L"

    # N0
    pairs = _identify_bracket_pairs(codepoints, types, indices)
    idx_set = set(indices)

    for open_pos, close_pos in pairs:
        # Inspect content strictly between brackets, only within IRS
        strong_content: Optional[str] = None
        has_opposite = False
        # Walk IRS indices between open and close
        inside = False
        for pos in indices:
            if pos == open_pos:
                inside = True
                continue
            if pos == close_pos:
                break
            if not inside:
                continue
            s = _n0_strong(types[pos])
            if s is None:
                continue
            if s == e:
                strong_content = e
                break
            has_opposite = True

        if strong_content == e:
            new_t = e
        elif has_opposite:
            # N0c: look backward before opening for established context
            preceding = None
            for pos in reversed(indices):
                if pos >= open_pos:
                    continue
                s = _n0_strong(types[pos])
                if s is not None:
                    preceding = s
                    break
            if preceding is None:
                preceding = sos
            o = "L" if e == "R" else "R"
            if preceding == o:
                new_t = o
            else:
                new_t = e
        else:
            # N0d: no strong types — leave brackets as-is
            continue

        types[open_pos] = new_t
        types[close_pos] = new_t
        # Following original-NSM characters after each bracket
        for bracket_pos in (open_pos, close_pos):
            # next characters in paragraph that were NSM before W1
            j = bracket_pos + 1
            while j < len(types) and was_nsm[j]:
                # only if still in this IRS and adjacent
                if j not in idx_set:
                    break
                types[j] = new_t
                j += 1

    # Helper: type at local k with sos/eos
    def strong_for_N1(t: str) -> Optional[str]:
        if t == "L":
            return "L"
        if t in ("R", "EN", "AN"):
            return "R"
        return None

    def type_at(k: int) -> str:
        if k < 0:
            return sos
        if k >= m:
            return eos
        return types[indices[k]]

    # N1 / N2
    k = 0
    while k < m:
        if types[indices[k]] not in NI_TYPES:
            k += 1
            continue
        # find NI sequence [k, end)
        end = k
        while end < m and types[indices[end]] in NI_TYPES:
            end += 1
        left = strong_for_N1(type_at(k - 1))
        right = strong_for_N1(type_at(end))
        if left is not None and left == right:
            new_t = left
        else:
            new_t = e  # N2
        for j in range(k, end):
            types[indices[j]] = new_t
        k = end


# ---------------------------------------------------------------------------
# Implicit levels I1–I2
# ---------------------------------------------------------------------------


def _apply_I(types: list[str], levels: list[int], indices: list[int]) -> None:
    for pos in indices:
        t = types[pos]
        lev = levels[pos]
        if (lev % 2) == 0:
            # I1
            if t == "R":
                levels[pos] = lev + 1
            elif t in ("AN", "EN"):
                levels[pos] = lev + 2
        else:
            # I2
            if t in ("L", "EN", "AN"):
                levels[pos] = lev + 1


# ---------------------------------------------------------------------------
# L1 / L2
# ---------------------------------------------------------------------------


def _apply_L1(
    levels: list[int],
    original_types: list[str],
    paragraph_level: int,
) -> None:
    """Reset levels for S, B, and trailing WS/isolates (original types)."""
    n = len(levels)
    # Segment / paragraph separators
    for i, t in enumerate(original_types):
        if t in ("S", "B"):
            levels[i] = paragraph_level

    def is_ws_or_isolate(t: str) -> bool:
        return t == "WS" or t in ISOLATE_INIT or t == "PDI"

    # Sequences of WS/isolates preceding S or B
    for i, t in enumerate(original_types):
        if t not in ("S", "B"):
            continue
        j = i - 1
        while j >= 0 and is_ws_or_isolate(original_types[j]):
            levels[j] = paragraph_level
            j -= 1

    # Trailing WS/isolates at end of line (entire paragraph = one line in tests)
    j = n - 1
    while j >= 0 and is_ws_or_isolate(original_types[j]):
        levels[j] = paragraph_level
        j -= 1


def _reorder_L2(levels: list[int], removed: list[bool]) -> list[int]:
    """
    Return visual_to_logical: list of original indices in visual order,
    excluding X9-removed characters.
    """
    order = [i for i in range(len(levels)) if not removed[i]]
    if not order:
        return []
    max_level = max(levels[i] for i in order)
    # lowest odd level
    lowest_odd = max_level if (max_level % 2) == 1 else max_level - 1
    if lowest_odd < 1:
        # still need to process from max down through 1 if max>=1
        pass

    for level in range(max_level, 0, -1):
        if level < 1:
            break
        # reverse contiguous sequences at this level or higher
        i = 0
        while i < len(order):
            if levels[order[i]] >= level:
                j = i
                while j < len(order) and levels[order[j]] >= level:
                    j += 1
                order[i:j] = reversed(order[i:j])
                i = j
            else:
                i += 1
    return order


# ---------------------------------------------------------------------------
# Public analyze()
# ---------------------------------------------------------------------------


def analyze(
    codepoints: Iterable[int],
    paragraph_level: Optional[int] = None,
) -> BidiResult:
    """
    Run the UBA on a single paragraph (no B characters expected for tests).

    paragraph_level:
      0 = LTR, 1 = RTL, None = auto (P2/P3).
    """
    load_data()
    cps = list(codepoints)
    n = len(cps)
    original_types = [bidi_class(cp) for cp in cps]
    was_nsm = [t == "NSM" for t in original_types]
    types = list(original_types)

    matching_pdi = _matching_pdis(original_types)
    matching_init = _matching_initiators(matching_pdi)

    if paragraph_level is None:
        paragraph_level = determine_paragraph_level(types)
    else:
        paragraph_level = int(paragraph_level)

    levels, types = resolve_explicit(types, paragraph_level, matching_pdi)

    removed = [original_types[i] in REMOVE_TYPES for i in range(n)]

    # Assign placeholder levels for removed chars (won't appear in output)
    for i in range(n):
        if removed[i] and levels[i] < 0:
            levels[i] = 0

    sequences = _isolating_run_sequences(
        levels, original_types, matching_pdi, matching_init
    )

    # Fill sos/eos
    for seq in sequences:
        first = seq["indices"][0]
        last = seq["indices"][-1]
        prev = seq["_prev_level"]
        if prev is None:
            prev = paragraph_level
        seq["sos"] = _sos_eos_type(levels[first], prev)

        if seq["_use_para_eos"]:
            nxt = paragraph_level
        else:
            nxt = seq["_next_level"]
            if nxt is None:
                nxt = paragraph_level
        seq["eos"] = _sos_eos_type(levels[last], nxt)

    for seq in sequences:
        idxs = seq["indices"]
        sos, eos = seq["sos"], seq["eos"]
        lev = seq["level"]
        _apply_W(types, idxs, sos)
        _apply_N(cps, types, idxs, sos, eos, lev, was_nsm)
        _apply_I(types, levels, idxs)

    _apply_L1(levels, original_types, paragraph_level)

    visual = _reorder_L2(levels, removed)
    logical_to_visual: list[Optional[int]] = [None] * n
    for vi, li in enumerate(visual):
        logical_to_visual[li] = vi

    # Levels for output: None means 'x' (removed)
    out_levels: list = [None if removed[i] else levels[i] for i in range(n)]

    return BidiResult(
        paragraph_level=paragraph_level,
        levels=out_levels,
        types=types,
        visual_to_logical=visual,
        logical_to_visual=logical_to_visual,
    )


# ---------------------------------------------------------------------------
# Conformance tests
# ---------------------------------------------------------------------------


def _parse_levels_field(s: str) -> list:
    out = []
    for tok in s.split():
        if tok == "x":
            out.append(None)
        else:
            out.append(int(tok))
    return out


def run_character_tests(
    path: Optional[Path] = None,
    max_failures_print: int = 30,
) -> tuple[int, int, list[str]]:
    """
    Run BidiCharacterTest.txt.
    Returns (passed, failed, failure_messages).
    """
    load_data()
    path = path or (SRC / "BidiCharacterTest.txt")
    passed = 0
    failed = 0
    failures: list[str] = []
    line_no = 0

    with path.open("r", encoding="utf-8") as f:
        for raw in f:
            line_no += 1
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(";")
            if len(parts) < 5:
                continue
            cps = [int(x, 16) for x in parts[0].split()]
            para_dir = int(parts[1].strip())
            exp_para = int(parts[2].strip())
            exp_levels = _parse_levels_field(parts[3].strip())
            exp_order = [int(x) for x in parts[4].split()]

            if para_dir == 2:
                pl = None
            else:
                pl = para_dir

            try:
                result = analyze(cps, paragraph_level=pl)
            except Exception as ex:
                failed += 1
                if len(failures) < max_failures_print:
                    failures.append(
                        f"line {line_no}: EXCEPTION {ex}\n  input: {parts[0][:80]}"
                    )
                continue

            ok = True
            reasons = []
            if result.paragraph_level != exp_para:
                ok = False
                reasons.append(
                    f"para_level got {result.paragraph_level} want {exp_para}"
                )
            if result.levels != exp_levels:
                ok = False
                reasons.append(
                    f"levels got {result.levels} want {exp_levels}"
                )
            if result.visual_to_logical != exp_order:
                ok = False
                reasons.append(
                    f"order got {result.visual_to_logical} want {exp_order}"
                )

            if ok:
                passed += 1
            else:
                failed += 1
                if len(failures) < max_failures_print:
                    failures.append(
                        f"line {line_no}: {'; '.join(reasons)}\n"
                        f"  cps={parts[0][:100]} dir={para_dir}"
                    )

    return passed, failed, failures


def run_abstract_tests(
    path: Optional[Path] = None,
    max_failures_print: int = 30,
) -> tuple[int, int, list[str]]:
    """
    Run BidiTest.txt (abstract class sequences).
    Format per UCD:
      # Field 0: bidi class sequence
      # Field 1: bitset of paragraph directions to test (1=LTR, 2=RTL, 4=Auto)
      # Levels: line starting with @Levels:
      # Reorder: line starting with @Reorder:
    """
    load_data()
    path = path or (SRC / "BidiTest.txt")
    if not path.exists():
        return 0, 0, [f"missing {path}"]

    # Map single-token class names used in BidiTest
    # Already short forms.

    passed = 0
    failed = 0
    failures: list[str] = []

    levels: Optional[list] = None
    reorder: Optional[list[int]] = None
    line_no = 0

    with path.open("r", encoding="utf-8") as f:
        for raw in f:
            line_no += 1
            line = raw.strip()
            if not line:
                continue
            if line.startswith("@Levels:"):
                levels = _parse_levels_field(line[len("@Levels:") :].strip())
                continue
            if line.startswith("@Reorder:"):
                rest = line[len("@Reorder:") :].strip()
                reorder = [int(x) for x in rest.split()] if rest else []
                continue
            if line.startswith("#") or line.startswith("@"):
                continue

            parts = line.split(";")
            if len(parts) < 2 or levels is None or reorder is None:
                continue
            classes = parts[0].split()
            bitset = int(parts[1].strip())

            # Analyze using abstract types by inventing code points per class
            # We temporarily override by feeding fake cps and monkeypatching —
            # cleaner: run internal path with explicit types.
            for bit, pl in ((1, 0), (2, 1), (4, None)):
                if (bitset & bit) == 0:
                    continue
                result = _analyze_abstract(classes, paragraph_level=pl)
                ok = True
                reasons = []
                # Auto bit: paragraph level compared? BidiTest levels field
                # already accounts for the paragraph embedding used.
                if result.levels != levels:
                    ok = False
                    reasons.append(
                        f"levels got {result.levels} want {levels}"
                    )
                if result.visual_to_logical != reorder:
                    ok = False
                    reasons.append(
                        f"order got {result.visual_to_logical} want {reorder}"
                    )
                if ok:
                    passed += 1
                else:
                    failed += 1
                    if len(failures) < max_failures_print:
                        dir_name = {1: "LTR", 2: "RTL", 4: "Auto"}[bit]
                        failures.append(
                            f"line {line_no} [{dir_name}]: {'; '.join(reasons)}\n"
                            f"  classes={' '.join(classes)}"
                        )

    return passed, failed, failures


# Representative code points for each Bidi_Class (for BidiTest.txt)
_ABSTRACT_CP = {
    "L": 0x0041,  # A
    "R": 0x05D0,  # Hebrew Alef
    "AL": 0x0627,  # Arabic Alef
    "EN": 0x0030,  # 0
    "ES": 0x002B,  # +
    "ET": 0x0024,  # $
    "AN": 0x0660,  # Arabic-Indic 0
    "CS": 0x002C,  # ,
    "NSM": 0x0300,  # combining grave
    "BN": 0x00AD,  # soft hyphen (BN)
    "B": 0x000A,  # LF as B — tests usually avoid
    "S": 0x0009,  # TAB
    "WS": 0x0020,  # space
    "ON": 0x0021,  # !
    "LRE": 0x202A,
    "LRO": 0x202D,
    "RLE": 0x202B,
    "RLO": 0x202E,
    "PDF": 0x202C,
    "LRI": 0x2066,
    "RLI": 0x2067,
    "FSI": 0x2068,
    "PDI": 0x2069,
}


def _analyze_abstract(
    classes: list[str],
    paragraph_level: Optional[int],
) -> BidiResult:
    """
    Run UBA on an abstract class sequence (BidiTest.txt).

    Uses representative code points, but forces original types to the
    declared classes so property quirks of the representatives do not matter.
    Bracket pairing for ON still uses the representative's bracket property;
    BidiTest ON tokens are non-brackets (!), which is correct.
    """
    load_data()
    cps = [_ABSTRACT_CP[c] for c in classes]
    # Inject custom original types via a thin wrapper path
    return _analyze_with_types(cps, classes, paragraph_level)


def _analyze_with_types(
    cps: list[int],
    original_types: list[str],
    paragraph_level: Optional[int],
) -> BidiResult:
    """Same as analyze() but with caller-supplied original Bidi_Class list."""
    load_data()
    n = len(cps)
    was_nsm = [t == "NSM" for t in original_types]
    types = list(original_types)

    matching_pdi = _matching_pdis(original_types)
    matching_init = _matching_initiators(matching_pdi)

    if paragraph_level is None:
        paragraph_level = determine_paragraph_level(types)
    else:
        paragraph_level = int(paragraph_level)

    levels, types = resolve_explicit(types, paragraph_level, matching_pdi)
    removed = [original_types[i] in REMOVE_TYPES for i in range(n)]
    for i in range(n):
        if removed[i] and levels[i] < 0:
            levels[i] = 0

    sequences = _isolating_run_sequences(
        levels, original_types, matching_pdi, matching_init
    )
    for seq in sequences:
        first = seq["indices"][0]
        last = seq["indices"][-1]
        prev = seq["_prev_level"]
        if prev is None:
            prev = paragraph_level
        seq["sos"] = _sos_eos_type(levels[first], prev)
        if seq["_use_para_eos"]:
            nxt = paragraph_level
        else:
            nxt = seq["_next_level"]
            if nxt is None:
                nxt = paragraph_level
        seq["eos"] = _sos_eos_type(levels[last], nxt)

    for seq in sequences:
        idxs = seq["indices"]
        _apply_W(types, idxs, seq["sos"])
        _apply_N(cps, types, idxs, seq["sos"], seq["eos"], seq["level"], was_nsm)
        _apply_I(types, levels, idxs)

    _apply_L1(levels, original_types, paragraph_level)
    visual = _reorder_L2(levels, removed)
    logical_to_visual: list[Optional[int]] = [None] * n
    for vi, li in enumerate(visual):
        logical_to_visual[li] = vi
    out_levels: list = [None if removed[i] else levels[i] for i in range(n)]
    return BidiResult(
        paragraph_level=paragraph_level,
        levels=out_levels,
        types=types,
        visual_to_logical=visual,
        logical_to_visual=logical_to_visual,
    )


# ---------------------------------------------------------------------------
# __main__
# ---------------------------------------------------------------------------


def main() -> None:
    print("Unicode 16.0.0 UAX #9 reference — BidiCharacterTest.txt")
    p, f, fails = run_character_tests()
    print(f"BidiCharacterTest: {p} passed, {f} failed, {p + f} total")
    for msg in fails:
        print(msg)
        print()

    bidi_test = SRC / "BidiTest.txt"
    if bidi_test.exists() and bidi_test.stat().st_size > 0:
        print("Unicode 16.0.0 UAX #9 reference — BidiTest.txt")
        try:
            p2, f2, fails2 = run_abstract_tests()
            print(f"BidiTest: {p2} passed, {f2} failed, {p2 + f2} total")
            for msg in fails2:
                print(msg)
                print()
        except Exception as ex:
            print(f"BidiTest: skipped/error: {ex}")
    else:
        print("BidiTest.txt not present; skipped.")


if __name__ == "__main__":
    main()


# =============================================================================
# Algorithm helpers (documentation)
# =============================================================================
#
# Data
#   DerivedBidiClass.txt  → bidi_class(cp)  (incl. @missing defaults)
#   BidiBrackets.txt      → paired brackets for N0 / BD14–BD16
#
# Pipeline (single paragraph / single line for conformance tests)
#   P2–P3  paragraph embedding level (or HL1 override via paragraph_level=)
#   BD9    matching PDI for each isolate initiator (original types)
#   X1–X8  explicit embeddings / overrides / isolates → levels + type overrides
#   X9     remove RLE/LRE/RLO/LRO/PDF/BN from subsequent processing (levels='x')
#   X10    isolating run sequences (BD13); sos/eos from neighboring levels
#   W1–W7  weak types per isolating run sequence
#   N0     bracket pairs (BD16) within IRS; EN/AN count as R
#   N1–N2  remaining neutrals / isolate formatting (NI)
#   I1–I2  implicit level adjustments
#   L1     reset S/B and trailing WS+isolates to paragraph level (original types)
#   L2     reverse runs from highest level down to 1
#
# BidiCharacterTest fields
#   codepoints; para_dir(0/1/2); resolved_para_level; levels (x=removed); visual_order
#
# Notes
#   Isolate initiator identity for BD13 uses original Bidi_Class (overrides may
#   change types of LRI/RLI/FSI/PDI to L/R under X5a/X5b/X6a).
#   max_depth = 125. Bracket stack limit = 63 (BD16).
# =============================================================================

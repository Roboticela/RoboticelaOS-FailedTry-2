#!/usr/bin/env python3
"""
Generate RoboUnicode emoji sequence tables (UTS #51 / Emoji 16.0).

Inputs (under Tools/ucd/source/):
  emoji-sequences.txt
  emoji-zwj-sequences.txt
  emoji-test.txt          (optional conformance .bin)

Outputs:
  Include/RoboUnicode/RoboEmojiData.h
  Source/RoboUnicode/data/robo_emoji_data.c
  Tests/Data/robo_emoji_conformance.bin + .h

Representation: compact code-point trie for longest-match RGI sequences.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

from robo_unicode_version import EMOJI_VERSION, UCD_VERSION

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(__file__).resolve().parent / "source"
OUT_H = ROOT / "Include" / "RoboUnicode" / "RoboEmojiData.h"
OUT_C = ROOT / "Source" / "RoboUnicode" / "data" / "robo_emoji_data.c"
OUT_BIN = ROOT / "Tests" / "Data" / "robo_emoji_conformance.bin"
OUT_META = ROOT / "Tests" / "Data" / "robo_emoji_conformance.h"


KIND_ORDER = [
    "NONE",
    "BASIC",
    "KEYCAP",
    "FLAG",
    "TAG",
    "MODIFIER",
    "ZWJ",
]

TYPE_TO_KIND = {
    "Basic_Emoji": 1,
    "Emoji_Keycap_Sequence": 2,
    "RGI_Emoji_Flag_Sequence": 3,
    "RGI_Emoji_Tag_Sequence": 4,
    "RGI_Emoji_Modifier_Sequence": 5,
    "RGI_Emoji_ZWJ_Sequence": 6,
}


def parse_cp_field(field: str) -> list[list[int]]:
    """Expand 'AAAA..BBBB' or 'AAAA BBBB' into list of sequences."""
    field = field.strip()
    if ".." in field and " " not in field:
        a_s, b_s = field.split("..")
        a, b = int(a_s, 16), int(b_s, 16)
        return [[cp] for cp in range(a, b + 1)]
    cps = [int(tok, 16) for tok in field.split()]
    return [cps]


def load_sequences(path: Path) -> list[tuple[tuple[int, ...], int]]:
    out: list[tuple[tuple[int, ...], int]] = []
    with path.open("r", encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = [p.strip() for p in line.split(";")]
            if len(parts) < 2:
                continue
            typ = parts[1].split()[0]
            kind = TYPE_TO_KIND.get(typ)
            if kind is None:
                continue
            for seq in parse_cp_field(parts[0]):
                out.append((tuple(seq), kind))
    return out


class TrieNode:
    __slots__ = ("children", "kind")

    def __init__(self) -> None:
        self.children: dict[int, TrieNode] = {}
        self.kind: int = 0  # 0 = non-accepting; else KIND


def build_trie(seqs: list[tuple[tuple[int, ...], int]]) -> TrieNode:
    root = TrieNode()
    for seq, kind in seqs:
        node = root
        for cp in seq:
            if cp not in node.children:
                node.children[cp] = TrieNode()
            node = node.children[cp]
        # Prefer first-defined kind if duplicate (should be unique)
        if node.kind == 0:
            node.kind = kind
    return root


def flatten_trie(root: TrieNode) -> list[tuple[int, int, int, int]]:
    """
    Emit nodes as (cp, child_begin, child_count, kind).
    Root is index 0 with cp=0 (unused). Children are contiguous sorted by cp.
    """
    nodes: list[TrieNode] = [root]
    # BFS assign indices
    i = 0
    while i < len(nodes):
        n = nodes[i]
        for cp in sorted(n.children.keys()):
            nodes.append(n.children[cp])
        i += 1

    index_of = {id(n): i for i, n in enumerate(nodes)}
    flat: list[tuple[int, int, int, int]] = []
    for n in nodes:
        kids = sorted(n.children.items())
        if kids:
            begin = index_of[id(kids[0][1])]
            # Verify contiguous
            for j, (_cp, child) in enumerate(kids):
                assert index_of[id(child)] == begin + j
            count = len(kids)
            # Represent this node's own edge label via parent; for root cp=0.
            # Child nodes store the code point that leads TO them.
        else:
            begin = 0
            count = 0
        flat.append((0, begin, count, n.kind))  # cp filled below

    # Fill cps: root=0; for each parent, set child.cp
    flat[0] = (0, flat[0][1], flat[0][2], flat[0][3])
    for n in nodes:
        for cp, child in n.children.items():
            ci = index_of[id(child)]
            _old_cp, b, c, k = flat[ci]
            flat[ci] = (cp, b, c, k)
    return flat


def write_conformance(seqs: list[tuple[tuple[int, ...], int]]) -> None:
    """
    Binary (LE):
      magic 'EMOJ'
      maj,min,patch emoji_maj,emoji_min as u16 (6 x u16) — use UCD maj/min/patch + emoji maj/min + 0
      count u32
      for each: len u16, kind u8, pad u8, cps[len] u32
    Uses fully-qualified from emoji-test when available; else all sequences.
    """
    test_path = SRC / "emoji-test.txt"
    cases: list[tuple[tuple[int, ...], int]] = []
    if test_path.exists():
        # Map sequence -> kind from our catalog
        kind_map = {s: k for s, k in seqs}
        with test_path.open("r", encoding="utf-8") as f:
            for raw in f:
                if "; fully-qualified" not in raw or raw.startswith("#"):
                    continue
                left = raw.split(";", 1)[0].strip()
                cps = tuple(int(t, 16) for t in left.split())
                if cps not in kind_map:
                    continue
                cases.append((cps, kind_map[cps]))
    else:
        cases = seqs

    maj, minor, patch = (int(x) for x in UCD_VERSION.split("."))
    emaj, emin = (int(x) for x in EMOJI_VERSION.split("."))
    buf = bytearray()
    buf += b"EMOJ"
    buf += struct.pack("<HHHHHH", maj, minor, patch, emaj, emin, 0)
    buf += struct.pack("<I", len(cases))
    for cps, kind in cases:
        buf += struct.pack("<HBB", len(cps), kind, 0)
        for cp in cps:
            buf += struct.pack("<I", cp)

    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    OUT_BIN.write_bytes(bytes(buf))
    OUT_META.write_text(
        "\n".join(
            [
                "/* Auto-generated by Tools/ucd/generate_emoji.py — DO NOT EDIT. */",
                "#ifndef ROBO_EMOJI_CONFORMANCE_H",
                "#define ROBO_EMOJI_CONFORMANCE_H",
                f"#define ROBO_EMOJI_CONF_CASE_COUNT {len(cases)}u",
                f'#define ROBO_EMOJI_CONF_MAGIC "EMOJ"',
                f'#define ROBO_EMOJI_VERSION_STRING "{EMOJI_VERSION}"',
                "#endif",
                "",
            ]
        ),
        encoding="utf-8",
    )
    print(f"Wrote {OUT_BIN} ({len(cases)} cases, {len(buf)} bytes)")


def main() -> int:
    seq_path = SRC / "emoji-sequences.txt"
    zwj_path = SRC / "emoji-zwj-sequences.txt"
    if not seq_path.exists() or not zwj_path.exists():
        print("Missing emoji-sequences.txt or emoji-zwj-sequences.txt", file=sys.stderr)
        return 1

    seqs = load_sequences(seq_path) + load_sequences(zwj_path)
    # Deduplicate preferring existing
    seen: dict[tuple[int, ...], int] = {}
    for s, k in seqs:
        if s not in seen:
            seen[s] = k
    uniq = [(s, seen[s]) for s in sorted(seen.keys())]

    root = build_trie(uniq)
    flat = flatten_trie(root)

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)

    h = [
        "/* Auto-generated by Tools/ucd/generate_emoji.py — DO NOT EDIT. */",
        f"/* Unicode {UCD_VERSION} / Emoji {EMOJI_VERSION} — UTS #51 RGI sequences */",
        "#ifndef ROBO_EMOJI_DATA_H",
        "#define ROBO_EMOJI_DATA_H",
        "",
        '#include "RoboUnicode/RoboUnicode.h"',
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        f'#define ROBO_EMOJI_UCD_VERSION "{UCD_VERSION}"',
        f'#define ROBO_EMOJI_VERSION "{EMOJI_VERSION}"',
        "",
        "typedef enum robo_unicode_emoji_seq_kind {",
    ]
    for i, name in enumerate(KIND_ORDER):
        h.append(f"    ROBO_EMOJI_SEQ_{name} = {i},")
    h += [
        "    ROBO_EMOJI_SEQ_KIND_COUNT",
        "} robo_unicode_emoji_seq_kind_t;",
        "",
        "typedef struct robo_emoji_trie_node {",
        "    uint32_t cp;",
        "    uint16_t child_begin;",
        "    uint16_t child_count;",
        "    uint8_t kind; /* 0 = non-accepting */",
        "    uint8_t _pad;",
        "} robo_emoji_trie_node_t;",
        "",
        "extern const robo_emoji_trie_node_t robo_emoji_trie[];",
        "extern const size_t robo_emoji_trie_count;",
        "",
        "const char *robo_emoji_ucd_version_string(void);",
        "const char *robo_emoji_version_string(void);",
        "const char *robo_emoji_seq_kind_name(robo_unicode_emoji_seq_kind_t k);",
        "",
        "/** Longest RGI match on code-point array starting at index 0. */",
        "size_t robo_emoji_trie_match(const robo_uchar32_t *cps, size_t len,",
        "                             robo_unicode_emoji_seq_kind_t *kind_out);",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif /* ROBO_EMOJI_DATA_H */",
        "",
    ]
    OUT_H.write_text("\n".join(h), encoding="utf-8")

    c = [
        "/* Auto-generated by Tools/ucd/generate_emoji.py — DO NOT EDIT. */",
        f"/* Unicode {UCD_VERSION} / Emoji {EMOJI_VERSION} */",
        '#include "RoboUnicode/RoboEmojiData.h"',
        "",
        "const robo_emoji_trie_node_t robo_emoji_trie[] = {",
    ]
    for cp, begin, count, kind in flat:
        c.append(
            f"    {{ 0x{cp:04X}u, {begin}u, {count}u, {kind}u, 0u }},"
        )
    c += [
        "};",
        f"const size_t robo_emoji_trie_count = {len(flat)};",
        "",
        "static const char *const robo_emoji_kind_names[] = {",
    ]
    for name in KIND_ORDER:
        c.append(f'    "{name}",')
    c.append("};")
    c.append(
        r"""
const char *robo_emoji_ucd_version_string(void)
{
    return ROBO_EMOJI_UCD_VERSION;
}

const char *robo_emoji_version_string(void)
{
    return ROBO_EMOJI_VERSION;
}

const char *robo_emoji_seq_kind_name(robo_unicode_emoji_seq_kind_t k)
{
    if ((unsigned)k >= (unsigned)ROBO_EMOJI_SEQ_KIND_COUNT) {
        return "?";
    }
    return robo_emoji_kind_names[k];
}

static int robo_emoji_find_child(uint16_t parent, uint32_t cp)
{
    const robo_emoji_trie_node_t *p = &robo_emoji_trie[parent];
    size_t lo = p->child_begin;
    size_t hi = (size_t)p->child_begin + (size_t)p->child_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        uint32_t mcp = robo_emoji_trie[mid].cp;
        if (cp < mcp) {
            hi = mid;
        } else if (cp > mcp) {
            lo = mid + 1u;
        } else {
            return (int)mid;
        }
    }
    return -1;
}

size_t robo_emoji_trie_match(const robo_uchar32_t *cps, size_t len,
                             robo_unicode_emoji_seq_kind_t *kind_out)
{
    size_t i;
    int node = 0;
    size_t matched = 0;
    robo_unicode_emoji_seq_kind_t kind = ROBO_EMOJI_SEQ_NONE;

    if (!cps || len == 0u) {
        if (kind_out) {
            *kind_out = ROBO_EMOJI_SEQ_NONE;
        }
        return 0u;
    }

    for (i = 0; i < len; i++) {
        int child = robo_emoji_find_child((uint16_t)node, (uint32_t)cps[i]);
        if (child < 0) {
            break;
        }
        node = child;
        if (robo_emoji_trie[node].kind != 0u) {
            matched = i + 1u;
            kind = (robo_unicode_emoji_seq_kind_t)robo_emoji_trie[node].kind;
        }
    }

    if (kind_out) {
        *kind_out = kind;
    }
    return matched;
}
"""
    )
    OUT_C.write_text("\n".join(c), encoding="utf-8")
    print(f"Wrote {OUT_H}")
    print(f"Wrote {OUT_C} (sequences={len(uniq)}, trie_nodes={len(flat)})")
    write_conformance(uniq)
    return 0


if __name__ == "__main__":
    sys.exit(main())

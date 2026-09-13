# CFF / CFF2 (Goal 5.5)

Parses and **validates** Compact Font Format tables inside SFNT/OTTO fonts.
Does **not** produce the final RoboFont glyph-outline API (Goal **5.8**),
evaluate variable-font instances (Goal **5.18**), or map Unicode (Goal **5.6**).

```text
SFNT / OpenType
    ├── TrueType outlines   (glyf + loca)     → Goal 5.3
    ├── CFF outlines        ('CFF ' table)    → this goal
    └── CFF2 outlines       ('CFF2' table)    → this goal
```

Outline technology is detected by `robo_font_ot_detect_outline` (Goal 5.4).
CFF/CFF2 **bodies** are parsed by `robo_font_cff_parse`.

## Architecture

```text
RoboFontSource → RoboSfntParser → table span
                                      ↓
                         RoboFontCff (CFF1 or CFF2)
                                      ↓
              INDEX · DICT · Charset · Encoding · CharStrings
              Private · FDArray · FDSelect · VStore (CFF2)
```

Shared low-level pieces: `RoboFontBinary` reader, checked offsets, RoboError,
RoboMem. Metrics remain in shared SFNT tables (`head` / `hhea` / `hmtx` / …).

## CFF1

| Structure | Role |
|---|---|
| Header | major/minor, hdrSize, offSize |
| Name INDEX | PostScript font names |
| Top DICT INDEX | CharStrings, charset, Encoding, Private, ROS/CID |
| String INDEX | custom SIDs |
| Global Subr INDEX | shared Type 2 subroutines |
| CharStrings INDEX | per-glyph Type 2 programs |
| Charset / Encoding | GID↔SID / code maps (not Unicode cmap) |
| Private DICT | local Subrs, widths, blues |

## CFF2

Differs from CFF1: Card32 INDEX counts, Top DICT length in header (no Name/String
INDEX), required FDArray, optional Item Variation Store. Operators 15/16 in the
Top DICT are **vsindex/blend**, not charset/encoding.

## INDEX

Reusable `robo_font_cff_index_t` with `count` / `get` / `get_offset` / `get_length`.
Supports offSize 1–4. Offsets are 1-based relative to the INDEX data region.

## DICT

Operand decoding: integers, reals (nibble encoding), one- and two-byte operators.
Structured Top / Private / Font DICT summaries for later consumers.

## Type 2 CharStrings

`robo_font_cff_charstring_parse` validates numbers, path/hint operators, widths,
hintmasks, and local/global subroutines (bias + depth/call caps).

Output is an **intermediate op list** for Goal 5.8 outline construction — not a
final path API and not rasterization.

## CID-keyed fonts

ROS + FDArray + FDSelect (formats 0 and 3). Glyph → Font DICT selection is
validated; advanced CID rendering is deferred.

## CFF2 Variation Store

Structural parse of region lists, axis coordinates, and item variation data
spans. Instance evaluation belongs to Goal **5.18**.

## Security

Resource limits in `RoboFontCff.h` bound INDEX counts, object sizes, DICT ops,
CharString length, operand stack, subroutine depth/calls, FDArray size, and
variation regions. Malformed input returns RoboError (`ROBO_PARSE_ERROR`, …).
Fuzz entry: `robo_font_cff_fuzz_parse`.

## Relationship to later goals

| Goal | Consumes |
|---|---|
| 5.6 cmap | Unicode → GID (independent of CFF Encoding) |
| 5.8 outlines | CharString IR → path geometry |
| 5.18 variable fonts | VStore evaluation |
| Phase 6 shaping | layout tables from 5.4 + glyphs |

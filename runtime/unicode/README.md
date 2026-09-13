# RoboUnicode

Unicode foundation for Roboticela Runtime (**Goals 3.3–3.14**).

**Pinned Unicode Standard / UCD version:** **16.0.0**  
**Pinned Emoji / UTS #51 data:** **Emoji 16.0** (same train as Unicode 16.0.0)  
**Authoritative pins:** `Tools/ucd/robo_unicode_version.py` → `Include/RoboUnicode/RoboUnicodeVersion.h`

| Layer | Headers / sources |
|---|---|
| Encoding | `Include/RoboUnicode/RoboUnicode.h`, `Source/RoboUnicode/RoboUnicode.c` |
| Version pin | `RoboUnicodeVersion.h` |
| Character Database | `RoboUnicodeProps.h`, `RoboUcdData.h`, `data/robo_ucd_data.c` |
| Normalization | `RoboUnicodeNorm.h`, `RoboUnicodeNorm.c`, `RoboNormData.h`, `data/robo_norm_data.c` |
| Grapheme clusters | `RoboUnicodeGrapheme.h`, `RoboUnicodeGrapheme.c`, `RoboGraphemeData.h`, `data/robo_grapheme_data.c` |
| Case mapping | `RoboUnicodeCase.h`, `RoboUnicodeCase.c`, `RoboCaseData.h`, `data/robo_case_data.c` |
| Word segmentation | `RoboUnicodeWord.h`, `RoboUnicodeWord.c`, `RoboWordData.h`, `data/robo_word_data.c` |
| Sentence segmentation | `RoboUnicodeSentence.h`, `RoboUnicodeSentence.c`, `RoboSentenceData.h`, `data/robo_sentence_data.c` |
| Line breaking | `RoboUnicodeLine.h`, `RoboUnicodeLine.c`, `RoboLineData.h`, `data/robo_line_data.c` |
| Bidirectional Algorithm | `RoboUnicodeBidi.h`, `RoboUnicodeBidi.c`, `RoboBidiData.h`, `data/robo_bidi_data.c` |
| Script / Script_Extensions | `RoboUnicodeProps.h` (+ UCD), `RoboUcdData.h`, `data/robo_ucd_data.c` |
| Emoji properties / RGI sequences | `RoboUnicodeEmoji.h`, `RoboEmojiData.h`, `data/robo_emoji_data.c` |
| Data pipeline | `Tools/ucd/` — see `AUDIT.md`, `UPGRADE.md` |

Docs: [`AUDIT.md`](AUDIT.md) · [`UPGRADE.md`](UPGRADE.md) · [`Docs/UnicodeDataAttribution.md`](../Docs/UnicodeDataAttribution.md)


## Terminology (critical)

| Term | Meaning |
|---|---|
| **Byte** | 8-bit unit in a UTF-8 byte sequence |
| **Code unit** | UTF-16 `uint16` or UTF-32 `uint32` storage unit |
| **Code point** | Integer in `0 .. 0x10FFFF` (may be a surrogate) |
| **Scalar value** | Code point **excluding** surrogates `U+D800..U+DFFF` |
| **Emoji property** | UCD boolean on a **single** code point (`Emoji`, `Emoji_Presentation`, …) |
| **Emoji sequence** | Official multi-code-point RGI sequence (UTS #51 catalog) |
| **Grapheme cluster** | User-perceived character (UAX #29 Extended Grapheme Cluster) |
| **Word boundary** | UAX #29 Word Boundary — **not** a linguistic dictionary word |
| **Sentence boundary** | UAX #29 Sentence Boundary — **not** an NLP sentence parser |
| **Line-break opportunity** | UAX #14 — legal wrap points; **not** visual layout |
| **Bidi level / visual order** | UAX #9 — embedding levels + reorder; **not** shaping |
| **Glyph** | Font shaping/rendering unit — **not yet** |
| **Color emoji appearance** | Font / text-render decision — **not** this subsystem |

**Never assume** 1 byte = 1 character, or 1 code point = 1 displayed character,
or 1 grapheme = 1 word, or 1 word = 1 sentence, or 1 word = 1 line-break unit,
or 1 bidi level = 1 glyph, or **1 emoji sequence = 1 grapheme cluster**,
or **1 emoji property = color rendering**.

`RoboString::size` is **bytes**. Code-point count uses encoding APIs.
Grapheme count uses `robo_unicode_grapheme_*` or `robo_string_grapheme_count()`.
Word segments use `robo_unicode_word_*` (UTF-8 byte ranges).
Sentence segments use `robo_unicode_sentence_*` (UTF-8 byte ranges).
Line-break opportunities use `robo_unicode_line_*` (UTF-8 byte ranges + kind).
Bidi analysis uses `robo_unicode_bidi_*` (per code point levels + visual map).
Emoji sequences use `robo_unicode_emoji_sequence_*` (UTF-8 byte ranges + kind).
Property lookups use `robo_unicode_category` / `script` / `is_emoji` / … (UCD).

### Bytes → code points → graphemes → words → sentences → line breaks → bidi

```text
bytes
  ↓
code points / scalars
  ↓
grapheme clusters   (UAX #29 Grapheme_Cluster_Break)
  ↓
word boundaries     (UAX #29 Word_Break)
  ↓
sentence boundaries (UAX #29 Sentence_Break)
  ↓
line-break opportunities (UAX #14 Line_Break)
  ↓
bidi levels + visual order (UAX #9)   ← independent of line breaks
```

Emoji **properties** attach to scalars. Emoji **sequences** are a parallel
catalog (UTS #51 RGI) that may span one or more scalars. Higher layers may
combine grapheme results with sequence results; they are not interchangeable.

Line breaking and bidi are **separate**. Future layout combines them.

| Measure | API | Notes |
|---|---|---|
| **Byte offset** | `robo_string_size`, iterator `byte_offset` | UTF-8 storage index 0..len |
| **Code-point index** | `robo_string_codepoint_count`, decode loops | Scalar count; ≠ graphemes |
| **Grapheme index** | `robo_unicode_grapheme_iter_t.cluster_index` | Extended grapheme clusters only |
| **Cluster byte range** | `cluster_start`, `cluster_end` | Half-open `[start, end)` in UTF-8 bytes |
| **Word byte range** | `word_start`, `word_end` | Half-open `[start, end)` in UTF-8 bytes |
| **Sentence byte range** | `sentence_start`, `sentence_end` | Half-open `[start, end)` in UTF-8 bytes |
| **Line segment** | `line_start`, `line_end` + `op` | Half-open bytes; `op` is mandatory/allowed |
| **Bidi level** | `levels[i]` per code point | Embedding level; `0xFF` = X9-removed |
| **Visual index** | `visual_to_logical[v]` | L2 reorder; excludes X9-removed |
| **UTF-8 end map** | `byte_ends[i]` (analyze_utf8) | Exclusive UTF-8 end offset of code point `i` |
| **Emoji sequence range** | `out_start` / `out_end` + kind | Half-open UTF-8 bytes; RGI catalog match |

## Architecture

```text
RoboUnicode
├── Encoding              ← Goal 3.3
├── Character Database    ← Goal 3.4
├── Normalization         ← Goal 3.5
├── Grapheme clusters     ← Goal 3.6
├── Case mapping / folding ← Goal 3.7
├── Word segmentation     ← Goal 3.8
├── Sentence segmentation ← Goal 3.9
├── Line breaking         ← Goal 3.10
├── Bidirectional Algorithm ← Goal 3.11
├── Script / Script_Extensions ← Goal 3.12
├── Emoji properties / RGI sequences ← Goal 3.13
├── Data generation / versioning / conformance ← Goal 3.14
└── Unicode Algorithms    ← later
```

Later text stack: `RoboI18N` → `RoboFont` → `RoboText` → `RoboTextShape` →
`RoboTextLayout` → `RoboTextRender` (RTL / complex scripts required).

### Text segmentation roadmap

```text
Grapheme clusters     ← Goal 3.6
  ↓
Case mapping/folding  ← Goal 3.7
  ↓
Word segmentation     ← Goal 3.8
  ↓
Sentence segmentation ← Goal 3.9
  ↓
Line breaking         ← Goal 3.10
  ↓
Bidi (UAX #9)         ← Goal 3.11
  ↓
Script / SCX (UAX #24) ← Goal 3.12
  ↓
Emoji props / RGI (UTS #51) ← Goal 3.13
  ↓
Data pipeline / conformance infra ← Goal 3.14 (this release)
  ↓
Text shaping          ← RoboTextShape
  ↓
Font selection        ← RoboFont
  ↓
Text rendering        ← RoboTextRender
```

## Goal 3.14 — Unicode Data Generation, Versioning, Validation, Conformance

Centralizes the Unicode data pipeline so upgrades are reproducible and safe.

```text
Official Unicode Data (manifest + SHA-256)
        ↓
Tools/ucd generators (build-time Python)
        ↓
Validated compact runtime tables (committed)
        ↓
RoboUnicode public APIs
        ↓
Conformance fixtures + unit suites
```

### Single version pin

| Source of truth | Location |
|---|---|
| Generators | `Tools/ucd/robo_unicode_version.py` |
| C runtime | `Include/RoboUnicode/RoboUnicodeVersion.h` (synced) |
| Manifest | `Tools/ucd/manifest.json` |

All of UCD / norm / grapheme / case / word / sentence / line / bidi / script /
emoji must report the same Unicode **16.0.0** (emoji data **16.0**).

### Offline builds

`Build.ps1` compiles **already-generated** tables. It does **not** download
Unicode data and does **not** run Python generators.

Explicit regeneration: see [`UPGRADE.md`](UPGRADE.md).

### Pipeline commands

```powershell
.\runtime\Tools\ucd\Fetch-Ucd.ps1
python .\runtime\Tools\ucd\generate_all.py
python .\runtime\Tools\ucd\generate_all.py --repro
python .\runtime\Tools\ucd\generate_all.py --update-golden
python .\runtime\Tools\ucd\verify_pipeline.py --sizes
```

Golden hashes: `Tools/ucd/golden_hashes.json` (detect accidental generator drift).

### Public API stability

Generated arrays stay internal (`*Data.h` / `data/*.c`). Callers use
`robo_unicode_*` behavior APIs — not raw table layouts.

### Conformance

Official Unicode tests are imported as fixtures (`.inc` / `.bin`), not thousands
of handwritten cases. Suites report structured diagnostics on failure
(category, version, input, expected, actual). Host suite
`SuiteRoboUnicodeInfra.c` checks cross-module version coherence and broad
script samples.

Attribution: [`Docs/UnicodeDataAttribution.md`](../Docs/UnicodeDataAttribution.md).

## Goal 3.13 — Unicode Emoji Properties and Extended Emoji Handling (UTS #51)


Provides **code-point emoji properties** (shared UCD) and **RGI emoji sequence**
recognition for Unicode / Emoji **16.0**. This layer identifies Unicode facts
only — not fonts, color glyphs, shaping, or layout.

### Boundaries (must not be confused)

| Concept | Owner | Notes |
|---|---|---|
| **Code-point properties** | UCD / `RoboUnicodeProps` | `Emoji`, `Emoji_Presentation`, `Emoji_Modifier`, `Emoji_Modifier_Base`, `Emoji_Component`, `Extended_Pictographic`, `Regional_Indicator`, `Variation_Selector` — independent bits |
| **Emoji sequences** | UTS #51 catalog / `RoboUnicodeEmoji` | Official RGI entries from `emoji-sequences.txt` + `emoji-zwj-sequences.txt` |
| **Extended grapheme clusters** | UAX #29 / `RoboUnicodeGrapheme` | Unchanged; emoji ZWJ / RI / ExtPict already participate via GB rules |
| **Glyphs** | Future `RoboFont` / shaping | Not created here |
| **Rendered appearance** | Future text render | Text vs emoji presentation is a *selection hint*, not a paint decision |

An RGI sequence may equal one grapheme cluster (e.g. many flags) or relate
differently for other text; callers must not assume identity.

### Public APIs

**Properties** (`RoboUnicodeProps.h`):

```c
robo_unicode_is_emoji(cp);
robo_unicode_is_emoji_presentation(cp);
robo_unicode_is_emoji_modifier(cp);
robo_unicode_is_emoji_modifier_base(cp);
robo_unicode_is_emoji_component(cp);
robo_unicode_is_extended_pictographic(cp);
robo_unicode_is_regional_indicator(cp);
robo_unicode_is_variation_selector(cp);
```

**Sequences** (`RoboUnicodeEmoji.h`):

```c
robo_unicode_emoji_sequence_classify(cps, len);   /* exact full match → kind */
robo_unicode_emoji_sequence_match(utf8, len, off, mode, &end, &kind);
robo_unicode_emoji_sequence_next(utf8, len, off, mode, &start, &end, &kind);
robo_unicode_emoji_ucd_version_string();          /* "16.0.0" */
robo_unicode_emoji_version_string();              /* "16.0" */
robo_unicode_emoji_seq_kind_name(kind);
```

Kinds: `NONE`, `BASIC`, `KEYCAP`, `FLAG`, `TAG`, `MODIFIER`, `ZWJ`.

- UTF-8 APIs take explicit lengths (no null-termination requirement).
- Longest RGI match from `offset`; incomplete sequences → `NONE` (not an error).
- Invalid UTF-8 under `STRICT` → `ROBO_PARSE_ERROR` (same policy as RoboUnicode).
- No heap allocation for ordinary lookups.

### Sequence storage

Compact **code-point trie** generated from the official RGI lists (longest match).
Chosen for memory efficiency + streaming-friendly O(length) match without building
a temporary string-wide code-point array for the whole buffer.

### Data sources / generation

| File | Role |
|---|---|
| `ucd/emoji/emoji-data.txt` | Code-point emoji boolean properties (via `generate_ucd.py`) |
| `PropList.txt` | `Regional_Indicator`, `Variation_Selector` |
| `emoji-sequences.txt` | Basic / keycap / flag / tag / modifier RGI |
| `emoji-zwj-sequences.txt` | ZWJ RGI (family, profession, gender-style, …) |
| `emoji-test.txt` | Fully-qualified conformance cases → `.bin` fixture |

URLs (Emoji 16.0): `https://www.unicode.org/Public/emoji/16.0/…`  
UCD emoji-data: `https://www.unicode.org/Public/16.0.0/ucd/emoji/emoji-data.txt`

```powershell
.\runtime\Tools\ucd\Fetch-Ucd.ps1
python .\runtime\Tools\ucd\generate_ucd.py
python .\runtime\Tools\ucd\generate_emoji.py
```

Outputs: `RoboEmojiData.h`, `data/robo_emoji_data.c`,
`Tests/Data/robo_emoji_conformance.bin` (+ `.h`).

License: Unicode Data Files — see `Tools/ucd/UNICODE_DATA_LICENSE.txt`.

### Grapheme relationship

- UAX #29 remains authoritative for grapheme segmentation.
- Extended_Pictographic / ZWJ / RI / modifiers continue to drive GB rules via
  existing grapheme tables.
- Sequence APIs answer “is this an official RGI emoji sequence?” — a different question.

### Deliberately not in this goal

- Font discovery, color emoji glyphs, COLR/CBDT, SVG-in-OT
- Shaping / HarfBuzz-style processing
- Layout, CSS `emoji` presentation, DOM
- Invented / hand-maintained “famous emoji” lists

### Conformance / tests

- Fully-qualified `emoji-test.txt` cases ∩ catalog: **3,781 / 3,781**
- Unit: properties, keycap/flag/modifier/ZWJ/tag/VS, UTF-8 match/next,
  invalid UTF-8, mixed Arabic/Urdu/Persian/Hebrew/Indic/Thai/CJK + emoji

## Goal 3.12 — Script and Script_Extensions (UAX #24)


Provides official Unicode **Script** and **Script_Extensions** for every
scalar value in Unicode 16.0.0. Required later for script-run segmentation,
font fallback, and shaping — but this goal is property lookup only.

### Script vs Script_Extensions

| Property | Meaning |
|---|---|
| **Script** | Primary script assignment (`robo_unicode_script`) |
| **Script_Extensions** | Set of scripts with which the character is commonly used |

`Script_Extensions` is **not** always `{Script}`. Many Common/Inherited
characters (punctuation, combining marks) list multiple concrete scripts in
SCX while Script remains Common or Inherited.

### Public API (`RoboUnicodeProps.h`)

```c
robo_unicode_script(cp);
robo_unicode_script_extensions(cp, out, capacity);
robo_unicode_has_script(cp, script);
robo_unicode_script_name(script);        /* "Arabic" */
robo_unicode_script_short_name(script);  /* "Arab" / Zyyy / Zinh / Zzzz */
robo_unicode_script_from_name("Arab");   /* also Qaai → Inherited */
robo_unicode_script_runs(cps, n, out, cap, &count); /* heuristic only */
```

Script identifiers: dense `robo_unicode_script_t` enum covering **all**
Unicode 16.0.0 scripts (Common, Inherited, Unknown, …), generated from
`Scripts.txt` — not a hand-picked subset.

### Script-run helper (heuristic)

`robo_unicode_script_runs` is a **shaping-prep heuristic**, not UCD truth:

- Concrete scripts form/continue runs of that script.
- Common/Inherited continue the previous concrete script when present;
  otherwise behave as Common.
- Unknown starts its own run.

Raw property APIs never invent a neighbor’s script for combining marks.

### Relationship to bidi

Independent of Goal 3.11. `Script=Arabic` does not imply bidi class `AL`.

### Data sources / generation

| File | Role |
|---|---|
| `Scripts.txt` | Script property |
| `ScriptExtensions.txt` | Script_Extensions |
| `PropertyValueAliases.txt` | ISO 15924 short names + Qaai |

```powershell
python .\runtime\Tools\ucd\generate_ucd.py
python .\runtime\Tools\ucd\validate_script.py
```

SCX storage: deduplicated sorted `uint16` pools + range-indexed set ids
(near-O(1) binary search). No per-query allocation.

Conformance fixture: `Tests/Data/robo_script_conformance.bin`
(204 SCX ranges + 2,253 Script samples).

### Known limitations

- No font fallback, language detection, or OpenType shaping.
- Script-run helper is minimal; full HarfBuzz-style itemization is later.

## Goal 3.11 — Unicode Bidirectional Algorithm (UAX #9)

Implements the **Unicode Bidirectional Algorithm** (Unicode 16.0.0): paragraph
direction, explicit embeddings/overrides/isolates, weak and neutral resolution
(including paired brackets N0), implicit levels, and L2 visual reordering.

**Bidi ordering is not text shaping.**

Arabic / Persian / Urdu / Hebrew **glyph joining and OpenType shaping** are
later (`RoboTextShape` / fonts). This goal only produces levels and visual
order over Unicode scalar values.

### Public API (`RoboUnicodeBidi.h`)

```c
robo_unicode_bidi_class(cp);                    /* raw Bidi_Class */
robo_unicode_bidi_paragraph_direction(cps, n);  /* P2/P3 → 0 or 1 */
robo_unicode_bidi_analyze(cps, n, para_dir, &out);
robo_unicode_bidi_analyze_utf8(src, len, mode, para_dir, &out);
robo_unicode_bidi_resolve_levels(...);
robo_unicode_bidi_reorder(levels, n, visual_to_logical, &vis);
robo_unicode_bidi_result_destroy(&out);
robo_unicode_bidi_ucd_version_string();         /* "16.0.0" */
```

Paragraph direction request:

| Enum | Behavior |
|---|---|
| `ROBO_BIDI_LTR` | Force paragraph level 0 |
| `ROBO_BIDI_RTL` | Force paragraph level 1 |
| `ROBO_BIDI_AUTO` | P2/P3 first-strong (default 0 if none) |

Result fields:

- `paragraph_level` — resolved base level (0 or 1)
- `levels[i]` — resolved embedding level, or `ROBO_BIDI_LEVEL_REMOVED` (`0xFF`) for
  characters removed by X9 (`RLE`/`LRE`/`RLO`/`LRO`/`PDF`/`BN`)
- `visual_to_logical[]` — L2 visual order over **non-removed** positions
- `byte_ends[]` — optional exclusive UTF-8 ends per code point (`analyze_utf8`)

Directional formatting controls remain addressable in the logical index space;
X9-removed ones keep index slots with `LEVEL_REMOVED` and are omitted from
`visual_to_logical`. Isolates (`LRI`/`RLI`/`FSI`/`PDI`) are **not** X9-removed.

### Rules implemented

| Phase | Rules |
|---|---|
| Paragraph | P2, P3 |
| Explicit | X1–X8 (embeddings, overrides, isolates, FSI, PDF, PDI, B) |
| Remove | X9 |
| Isolating runs | X10 / BD13 |
| Weak | W1–W7 |
| Neutrals | N0 (brackets), N1, N2 |
| Implicit | I1, I2 |
| Line / reorder | L1, L2 |

Max embedding depth 125; bracket stack 63 (UAX #9). Bracket equivalence
`U+232A` ↔ `U+3009` for N0 pairing.

### Unicode data sources

| File | Used for |
|---|---|
| `extracted/DerivedBidiClass.txt` | `Bidi_Class` |
| `BidiBrackets.txt` | Paired brackets (N0) |
| `BidiMirroring.txt` | Fetched for attribution (mirroring glyphs = later) |
| `BidiCharacterTest.txt` | Official character-level conformance |
| `BidiTest.txt` | Official abstract-class conformance (Python oracle) |

### Generation

```powershell
python .\runtime\Tools\ucd\generate_bidi.py
```

Produces `RoboBidiData.h`, `data/robo_bidi_data.c`, and
`Tests/Data/robo_bidi_character_conformance.bin` (**91,707** cases) plus
metadata header. Reference oracle: `Tools/ucd/bidi_ref.py`
(CharacterTest **91,707/91,707**, abstract BidiTest **770,241/770,241**).

### Source-index mapping

```text
UTF-8 bytes  --decode-->  code points (logical i)
                              │
                              ├─ levels[i]
                              ├─ byte_ends[i]   (exclusive UTF-8 end)
                              └─ visual_to_logical[v] → logical i
```

Grapheme clusters remain a **separate** layer (Goal 3.6). Future layout maps
bidi levels back through code-point ranges into grapheme/byte ranges; this
API does not collapse graphemes into single bidi units.

### Invalid UTF-8

Same STRICT → `ROBO_PARSE_ERROR` / REPLACE → U+FFFD policy as other RoboUnicode
UTF-8 entry points. Replacement characters participate in bidi like any other
scalar.

### Performance

- One decode + one property pass per analysis; tables are immutable.
- Per-call heap for levels/visual/IRS (RoboMem); no mutable globals.
- Prefer `resolve_levels` when visual order is not needed yet.

### Known limitations

- No Arabic/Hebrew shaping, mirroring glyph selection, fonts, or CSS
  `direction` / `unicode-bidi`.
- No multi-paragraph streaming API (callers split on `B` / newlines).
- Abstract `BidiTest.txt` is validated by the Python oracle; the C suite runs
  `BidiCharacterTest` (sufficient for production character paths).

## Goal 3.10 — Unicode Line Breaking (UAX #14)

Implements **default Unicode Line Breaking** (Unicode 16.0.0): legal wrap
opportunities only. Does **not** measure glyphs, choose fonts, apply CSS
`white-space` / `word-break` / `line-break`, or produce visual lines.

### Break kinds

| Kind | Enum | Meaning |
|---|---|---|
| Prohibited | `ROBO_LINE_BREAK_PROHIBITED` | × — must not break |
| Allowed | `ROBO_LINE_BREAK_ALLOWED` | ÷ — soft opportunity |
| Mandatory | `ROBO_LINE_BREAK_MANDATORY` | ! — hard break (BK/CR/LF/NL/eot) |

`LineBreakTest.txt` marks both soft and hard breaks as `÷`; use
`robo_unicode_line_break_opportunity` when the distinction matters.

### Public API (`RoboUnicodeLine.h`)

```c
robo_unicode_line_break_class_raw / _class   /* LB1-resolved class */
robo_unicode_line_break_opportunity(cps, len, index);
robo_unicode_line_break(cps, len, index);    /* non-prohibited? */
robo_unicode_line_iter_init / _iter_next
robo_unicode_line_break_next / _prev
robo_unicode_line_break_count_utf8
robo_unicode_line_ucd_version_string();      /* "16.0.0" */
```

### Rules implemented

LB1 (AI/SG/XX→AL, SA→CM|AL, CJ→NS), LB2–LB31 including LB8a, LB9/LB10 CM/ZWJ
treat-as, LB15a–d, LB19/LB19a (QU∩Pi/Pf + East_Asian_Width), LB20a, LB25
numbers, LB28a Brahmic, LB30/LB30a/LB30b.

Uses existing Gc, East_Asian_Width, and Extended_Pictographic tables.

### Unicode data sources

| File | Used for |
|---|---|
| `LineBreak.txt` | `Line_Break` property |
| `UnicodeData.txt` (via RoboUCD) | Gc for LB1 SA, QU∩Pi/Pf, ExtPict∩Cn |
| `EastAsianWidth.txt` (via RoboUCD) | LB19a / LB21a / LB30 |
| `emoji-data.txt` (via RoboUCD) | Extended_Pictographic (LB30b) |
| `auxiliary/LineBreakTest.txt` | Official conformance |

### Generation

```powershell
python .\runtime\Tools\ucd\generate_linebreak.py
```

Produces `RoboLineData.h`, `data/robo_line_data.c`, and
`Tests/Data/robo_line_conformance.inc` (**16,672** official vectors).

Reference oracle (dev only): `Tools/ucd/lb_ref.py`.

### Invalid UTF-8

Same STRICT / REPLACE policy as other RoboUnicode iterators.

### Performance

- Opportunity on a code-point array builds resolved classes once per call.
- UTF-8 iterator walks forward with look-ahead for multi-char rules (LB25, etc.).
- `line_break_prev` rescans from the start.

### Known limitations

- No visual layout, hyphenation, dictionary Thai/CJK wrapping, or CSS tailorings.
- Contingent breaks (CB) use default LB20 (break before and after).
- Not O(1) backward iteration.

## Goal 3.9 — Unicode Sentence Segmentation (UAX #29)

Implements **default Unicode Sentence Boundaries** (Unicode 16.0.0), not
NLP / linguistic sentence detection or CLDR locale tailorings.

### UAX #29 vs NLP “sentences”

| | Unicode sentence segmentation | NLP / linguistic parsing |
|---|---|---|
| **Source** | UAX #29 `Sentence_Break` + rules SB1–SB11, SB998 | ML models, abbreviation lists, CLDR |
| **Abbreviations** | Limited (e.g. `U.S.` via SB7); not `Mr. Jones` | Dictionary / locale data |
| **Use** | Cursor movement, selection, rough splits | Translation, summarization |

RoboUnicode provides the **Unicode default** only.

### Public API (`RoboUnicodeSentence.h`)

```c
robo_unicode_sentence_break(cps, len, index);  /* 1 = break before index */
robo_unicode_sentence_iter_init / _iter_next
robo_unicode_sentence_next / _prev
robo_unicode_sentence_count_utf8
robo_unicode_sentence_ucd_version_string();  /* "16.0.0" */
```

Each `iter_next` returns `[sentence_start, sentence_end)` **byte offsets**.

Does **not** implement line breaking, bidi, shaping, or fonts.

### Boundary rules implemented

SB1–SB2 (sot/eot), SB3 (CR×LF), SB4 (after ParaSep), SB5 (ignore Extend|Format
except after sot/ParaSep), SB6–SB8a (ATerm/SATerm contexts), SB9–SB11 (Close/Sp/
ParaSep after terminators), SB998 (otherwise no break).

### Unicode data sources

| File | Used for |
|---|---|
| `auxiliary/SentenceBreakProperty.txt` | `Sentence_Break` property classes |
| `auxiliary/SentenceBreakTest.txt` | Official conformance fixtures |

### Generation

```powershell
python .\runtime\Tools\ucd\generate_sentence.py
```

Produces `RoboSentenceData.h`, `data/robo_sentence_data.c`, and
`Tests/Data/robo_sentence_conformance.inc` (**512** official vectors).

### Invalid UTF-8

Same policy as encoding / word / grapheme:

| Mode | Behavior |
|---|---|
| **STRICT** | `ROBO_PARSE_ERROR`; iterator marked invalid |
| **REPLACE** | `U+FFFD` replacements, then segment |

### Performance

- Forward iterator walks UTF-8 once with look-ahead for SB8 (Lower after junk).
- Property lookup is binary search over compressed ranges.
- `sentence_prev` rescans from the start (same pattern as word/grapheme prev).

### Known limitations

- Not NLP / locale sentence detection (CLDR suppressions, abbreviation lists).
- Backward iteration is not O(1) per step yet.
- UTF-16 adapter not wired (code-point engine is ready).
- Does not implement line breaking.

## Goal 3.8 — Unicode Word Segmentation (UAX #29)

Implements **default Unicode Word Boundaries** (Unicode 16.0.0), not
dictionary-based or locale-specific linguistic tokenization.

### UAX #29 vs linguistic “words”

| | Unicode word segmentation | Dictionary / NLP segmentation |
|---|---|---|
| **Source** | UAX #29 `Word_Break` + rules WB1–WB16, WB999 | Language models, dictionaries, CLDR tailorings |
| **CJK** | Often breaks between ideographs | Finds multi-character lexical words |
| **Thai** | Property-based (no spaces required) | Often needs dictionary |
| **Use** | Cursor movement, selection, search hints | Spellcheck, TTS, search indexing |

RoboUnicode provides the **Unicode default** only. Locale-aware or dictionary
segmentation belongs to later `RoboI18N` / text goals — not Goal 3.8.

### Public API (`RoboUnicodeWord.h`)

**Code-point engine:**

```c
robo_unicode_word_break(cps, len, index);  /* 1 = break before index */
```

**UTF-8 streaming iterator** (preferred):

```c
robo_unicode_word_iter_init / _iter_next
robo_unicode_word_next / _prev
robo_unicode_word_count_utf8
robo_unicode_word_ucd_version_string();  /* "16.0.0" */
```

Each `iter_next` returns `[word_start, word_end)` **byte offsets**. A word may
span multiple grapheme clusters (e.g. combining marks attached via WB4).

Does **not** implement sentence/line breaking, bidi, shaping, or fonts.

### Boundary rules implemented

WB1–WB2 (sot/eot), WB3–WB3d (CR/LF/Newline/ZWJ+ExtPict/WSegSpace), WB4 (ignore
Extend|Format|ZWJ), WB5–WB7c (letters, MidLetter, Hebrew quotes), WB8–WB12
(numbers), WB13–WB13b (Katakana, ExtendNumLet), WB15–WB16 (Regional_Indicator
pairs), WB999 (otherwise break).

Look-ahead for WB6/WB7/WB7b/WB11/WB12 peeks following scalars when iterating
UTF-8 so `"don't"` and `"3.14"` stay one word.

### Unicode data sources

| File | Used for |
|---|---|
| `auxiliary/WordBreakProperty.txt` | `Word_Break` property classes |
| `emoji/emoji-data.txt` (via UCD ExtPict) | WB3c `ZWJ × Extended_Pictographic` |
| `auxiliary/WordBreakTest.txt` | Official conformance fixtures |

### Generation

```powershell
python .\runtime\Tools\ucd\generate_word.py
```

Produces `RoboWordData.h`, `data/robo_word_data.c`, and
`Tests/Data/robo_word_conformance.inc` (**1,826** official vectors).

### Invalid UTF-8

Same policy as encoding / graphemes:

| Mode | Behavior |
|---|---|
| **STRICT** | `ROBO_PARSE_ERROR`; iterator marked invalid |
| **REPLACE** | `U+FFFD` replacements, then segment |

### Performance

- Forward iterator walks UTF-8 once with a small look-ahead window for mid-letter /
  mid-number rules (no full-string re-decode per boundary).
- Property lookup is binary search over compressed ranges.
- `word_prev` rescans from the start of the string (same pattern as grapheme_prev).

### Known limitations

- Not dictionary / locale word breaking (CJK lexical words, Thai dictionary, etc.).
- Backward iteration is not O(1) per step yet.
- UTF-16 adapter not wired (code-point engine is ready).
- Does not implement sentence or line breaking.

## Goal 3.7 — Case mapping and case folding

Locale-independent Unicode case operations (Default Case Algorithms, Unicode 16.0.0).

### Distinctions (do not conflate)

| Operation | Source | Expansion | Notes |
|---|---|---|---|
| **Simple** upper/lower/title | `UnicodeData.txt` fields 12–14 | Always 1:1 | Identity if unlisted |
| **Full** upper/lower/title | Unconditional `SpecialCasing.txt` + simple | May expand (ß→SS, ﬁ→FI) | Context-free except Final_Sigma on string lower |
| **Full case fold** | `CaseFolding.txt` status **C+F** | May expand (ß→ss) | For comparisons |
| **Simple case fold** | `CaseFolding.txt` status **C+S** | Always 1:1 | Length-preserving |
| **Normalization** | Goal 3.5 | Separate | Case folding does **not** preserve NFC/NFD |

`robo_unicode_casefold_compare_utf8` is **binary equality after full case folding** — not normalization-aware, not locale-sensitive.

### Public API (`RoboUnicodeCase.h`)

```c
robo_unicode_simple_toupper / _tolower / _totitle / _casefold   /* 1:1 */
robo_unicode_to_upper / _to_lower / _to_title / _casefold       /* full, out[] */
robo_unicode_case_map_cps / _case_map_utf8
robo_unicode_to_upper_utf8 / _to_lower_utf8 / _to_title_utf8 / _casefold_utf8
robo_unicode_casefold_compare_utf8
```

UTF-8 outputs are RoboMem-allocated NUL-terminated strings; caller `robo_free()`.

**Titlecase (UTF-8):** applies each scalar’s **titlecase mapping** (character-level).
Word-boundary-aware titlecasing can use Goal 3.8 iterators; sentence titlecasing is later.

**Final_Σ:** full lowercase of strings applies Final_Sigma for U+03A3 (context-sensitive,
locale-independent). Single-code-point `robo_unicode_to_lower(0x03A3)` stays σ (no context).

### Locale limitations

Not applied (documented, not silent):

- Turkish/Azeri `SpecialCasing` / CaseFolding status **T**
- Lithuanian Soft_Dotted conditions
- CLDR tailored casing

Unconditional SpecialCasing for İ (U+0130 → i + combining dot) **is** applied (locale-independent entry).

### Data generation

```powershell
python .\runtime\Tools\ucd\generate_case.py
```

| Input | Role |
|---|---|
| `UnicodeData.txt` | Simple mappings |
| `SpecialCasing.txt` | Full unconditional + Final_Sigma map |
| `CaseFolding.txt` | C/F/S folding (T skipped) |
| `DerivedCoreProperties.txt` | `Cased`, `Case_Ignorable` for Final_Sigma |

### Invalid UTF-8

STRICT → `ROBO_PARSE_ERROR`; REPLACE → U+FFFD then map (same as encoding).

### Performance

- Binary-search tables; ASCII fast path for upper/lower/fold UTF-8
- Mapping length capped at `ROBO_CASE_MAX_MAP` (4)
- Compare allocates two temporary folded buffers

### Known limitations

- No locale-sensitive Turkish/Lithuanian casing
- Titlecase is not word-boundary aware
- Case fold compare is not NFKC/NFC aware (callers may normalize first)

## Goal 3.6 — Extended Grapheme Clusters (UAX #29)

Implements **Extended Grapheme Clusters** (Unicode 16.0.0 default), not legacy
grapheme clusters.

### Public API (`RoboUnicodeGrapheme.h`)

**Code-point engine** (UTF-16 can reuse this):

```c
robo_unicode_grapheme_break(cps, len, index);  /* 1 = break before index */
```

**UTF-8 iterator** (preferred for strings):

```c
robo_unicode_grapheme_iter_init / _iter_next
robo_unicode_grapheme_next / _prev
robo_unicode_grapheme_count_utf8
```

Each `iter_next` returns `[cluster_start, cluster_end)` **byte offsets** into the
source buffer. `cluster_index` advances per cluster.

**RoboString** (optional thin wrapper — representation unchanged):

```c
robo_string_grapheme_count(str, mode, &count);
```

### Supported boundary rules

GB1–GB8, GB9–GB9c (extended), GB11–GB13, GB999 from UAX #29, including:

- CR × LF (GB3); control breaks (GB4–GB5)
- Hangul L/V/T/LV/LVT conjoining (GB6–GB8)
- Extend, ZWJ, SpacingMark, Prepend (GB9–GB9b)
- Indic conjunct linker sequences via `Indic_Conjunct_Break` (GB9c)
- Emoji ZWJ sequences via `Extended_Pictographic` (GB11)
- Regional-indicator flag pairs (GB12–GB13)

Does **not** implement word/sentence/line breaking, bidi, shaping, or fonts.

### Unicode data sources

| File | Used for |
|---|---|
| `auxiliary/GraphemeBreakProperty.txt` | `Grapheme_Cluster_Break` property |
| `DerivedCoreProperties.txt` | `Indic_Conjunct_Break` (GB9c) |
| `emoji/emoji-data.txt` (via UCD) | `Extended_Pictographic` (GB11) |
| `auxiliary/GraphemeBreakTest.txt` | Official conformance fixtures |

### Generation

```powershell
python .\runtime\Tools\ucd\generate_grapheme.py
python .\runtime\Tools\ucd\generate_case.py
```

Produces `RoboGraphemeData.h`, `data/robo_grapheme_data.c`, and
`Tests/Data/robo_grapheme_conformance.inc` (1,093 official vectors).

For case tables see Goal 3.7 / `generate_case.py`.

### Invalid UTF-8

Same policy as encoding:

| Mode | Behavior |
|---|---|
| **STRICT** | `ROBO_PARSE_ERROR`; iterator marked invalid |
| **REPLACE** | `U+FFFD` replacements, then segment |

### Performance

- Streaming **forward iterator** — O(clusters) with small stack buffer (48 code
  points); heap only for unusually long clusters (emoji ZWJ chains, many combining marks).
- `grapheme_break` on code-point arrays is O(1) per boundary (GB9c/GB11 scan backward
  within current cluster only).
- Backward iteration (`grapheme_prev`) rescans from start — acceptable for cursor
  movement; can be optimized later with cached offsets.

### Known limitations

- Backward iteration is not O(1) per step yet.
- UTF-16 adapter not wired (code-point engine is ready).
- Legacy grapheme cluster variant (without GB9a/b/c) is not exposed.

## Goal 3.5 — Normalization (UAX #15)

### Supported forms

| Form | Description |
|---|---|
| **NFC** | Canonical Decomposition, then Canonical Composition |
| **NFD** | Canonical Decomposition |
| **NFKC** | Compatibility Decomposition, then Canonical Composition |
| **NFKD** | Compatibility Decomposition |

Normalization is **independent** of case folding, grapheme segmentation, bidi,
shaping, font selection, and rendering.

### Public API (`RoboUnicodeNorm.h`)

**Code-point pipeline** (internal engine; future UTF-16 can reuse this):

```c
robo_unicode_normalize(src, src_len, form, &out, &out_len);
robo_unicode_normalize_nfc / _nfd / _nfkc / _nfkd
```

**UTF-8 I/O** (primary string API):

```c
robo_unicode_normalize_utf8(src, src_len, form, mode, &out, &out_len);
robo_unicode_normalize_nfc_utf8 / _nfd_utf8 / _nfkc_utf8 / _nfkd_utf8
```

- Input/output: UTF-8 bytes (NUL-terminated on output).
- Internal processing: `robo_uchar32_t` scalar arrays.
- `robo_unicode_norm_ucd_version_string()` → `"16.0.0"`.

### Memory ownership

| API | Output | Caller frees |
|---|---|---|
| Code-point | `robo_uchar32_t **` | `robo_free(*out)` when non-NULL |
| UTF-8 | `char **` (NUL-terminated) | `robo_free(*out)` |

Empty code-point input → `*out == NULL`, `*out_len == 0`.
Empty UTF-8 input → allocated `""` with `*out_len == 0`.

### Invalid UTF-8 policy

Same as encoding (`RoboUnicode.h`):

| Mode | Behavior |
|---|---|
| **STRICT** | `ROBO_PARSE_ERROR`; no output allocated |
| **REPLACE** | `U+FFFD` per ill-formed sequence, then normalize |

### Algorithm

Standard Unicode Normalization (Ch. 3 / UAX #15):

1. **Decompose** each scalar recursively (canonical or compatibility mappings).
2. **Hangul** uses the standard algorithmic L/V/T decomposition (not table data).
3. **Canonical ordering** — stable reorder of combining marks by CCC (UCD).
4. **Compose** (NFC/NFKC only) — canonical composition with blocking rules;
   Hangul L+V→LV and LV+T→LVT are algorithmic.

### Unicode data sources

Official UCD 16.0.0 (Unicode License — `Tools/ucd/UNICODE_DATA_LICENSE.txt`):

| File | Used for |
|---|---|
| `UnicodeData.txt` | Canonical + compatibility one-step decompositions |
| `CompositionExclusions.txt` | Composition exclusions |
| `DerivedNormalizationProps.txt` | Reference (Hangul / Quick_Check — not parsed at runtime) |
| `NormalizationTest.txt` | Official conformance fixtures (generated into tests) |

CCC for ordering comes from existing UCD tables (`robo_unicode_combining_class`).

### Generation

```powershell
.\runtime\Tools\ucd\Fetch-Ucd.ps1
python .\runtime\Tools\ucd\generate_ucd.py
python .\runtime\Tools\ucd\generate_norm.py
python .\runtime\Tools\ucd\generate_grapheme.py
```

Produces:

| Output | Role |
|---|---|
| `Include/RoboUnicode/RoboNormData.h` | Table types + lookup decls |
| `Source/RoboUnicode/data/robo_norm_data.c` | Canon/compat decomp + composition pairs |
| `Tests/Data/robo_norm_conformance.inc` | 19,965 official test vectors |

Runtime never parses UCD text files.

### Performance

- **ASCII fast path**: pure ASCII UTF-8 is copied without decomposition (all four forms).
- **Already-normalized** non-ASCII still runs the full pipeline (no Quick_Check table yet).
- Buffers grow geometrically; composition uses in-place sequential algorithm.

### Known limitations

- No `Quick_Check` property optimization yet.
- Code-point API accepts any `uint32`; callers should pass scalars.
- Surrogates in code-point input are not rejected (treated as ordinary values).
- Normalization does not validate UTF-8 beyond the encoding layer.

## Goal 3.4 — Character Database

### Public API (`RoboUnicodeProps.h`)

- `robo_unicode_category` / `robo_unicode_category_name`
- `robo_unicode_script` / `robo_unicode_script_name`
- `robo_unicode_combining_class`
- `robo_unicode_east_asian_width`
- `robo_unicode_is_whitespace` / `alphabetic` / `uppercase` / `lowercase`
- `robo_unicode_is_default_ignorable`
- `robo_unicode_is_emoji` / `emoji_presentation` / `emoji_modifier` /
  `emoji_modifier_base` / `emoji_component` / `extended_pictographic` /
  `regional_indicator` / `variation_selector`
- `robo_unicode_ucd_version_string`

See `Tools/ucd/README.md` for UCD fetch/generate details.

## Encoding (Goal 3.3)

UTF-8/16/32 encode/decode/validate/convert; STRICT vs REPLACE — see `RoboUnicode.h`.

## Invalid UTF policy (encoding)

| Mode | Behavior |
|---|---|
| **STRICT** | Error on ill-formed / truncated / overlong / bad surrogates |
| **REPLACE** | `U+FFFD` with defined advance |

## RoboString

Still stores UTF-8 bytes. `robo_string_size()` remains **byte length**.
Use `robo_string_grapheme_count()` for grapheme clusters — never infer from bytes or
code points alone.

## Thread-safety

Property, normalization, and grapheme lookups use immutable static tables — reentrant /
thread-safe after static init.

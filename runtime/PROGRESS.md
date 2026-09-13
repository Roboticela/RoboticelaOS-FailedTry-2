# Roboticela Runtime Progress

## Goals 0.1–1.5 — RoboBase foundation

**Status:** complete

## Phase 2 — RoboMem (Goals 2.1–2.5)

**Status:** complete

## Goal 3.1 — RoboArray

**Status:** complete

## Goal 3.2 — RoboString

**Status:** complete

## Goal 3.3 — RoboUnicode encoding

**Status:** complete

## Goal 3.4 — Unicode Character Database properties

**Status:** complete

## Goal 3.5 — Unicode Normalization

**Status:** complete

## Goal 3.6 — Extended Grapheme Cluster segmentation

**Status:** complete

## Goal 3.7 — Unicode case mapping and case folding

**Status:** complete

**Done:**

- Official Unicode **16.0.0** case data pipeline (`Tools/ucd/generate_case.py`)
- Simple 1:1 mappings from `UnicodeData.txt`; full mappings from unconditional `SpecialCasing.txt`
- Full case fold (C+F) and simple fold (C+S) from `CaseFolding.txt` (status T omitted)
- Final_Sigma context for Greek Σ in string-level full lowercase
- Public API `RoboUnicodeCase.h` — simple/full upper/lower/title/fold, UTF-8 ops, casefold compare
- Multi-code-point expansions (ß→SS, ﬁ→FI, İ→i+̇)
- Conformance fixtures from CaseFolding + SpecialCasing (1,918 cases)
- Samples: Latin, Greek, Cyrillic, Armenian, Georgian, Turkish İ, sharp S, ligatures, combining, Deseret
- Debug / Release / ASan+UBSan: **116/116** tests pass (pre–Goal 3.8 baseline)

**Definition of done:**

- [x] Simple vs full case mapping distinguished
- [x] Full case folding for comparisons (binary equality after fold)
- [x] Multi-code-point mappings supported
- [x] UCD-backed tables (not ASCII heuristics as the primary path)
- [x] Locale limitations documented (no Turkish T / Lithuanian Soft_Dotted)
- [x] Normalization kept separate
- [x] Existing suites still pass

## Goal 3.8 — Unicode Word Segmentation (UAX #29)

**Status:** complete

**Done:**

- Official Unicode **16.0.0** Word_Break tables (`Tools/ucd/generate_word.py`)
- Rules WB1–WB16 + WB999 with WB4 ignore (Extend|Format|ZWJ); ExtPict for WB3c
- Public API `RoboUnicodeWord.h` — `word_break`, streaming UTF-8 iterator, next/prev/count
- Word segments as half-open UTF-8 byte ranges `[word_start, word_end)`
- Official `WordBreakTest.txt` conformance: **1,826 / 1,826**
- Unit coverage: Latin, Greek, Cyrillic, Arabic/Persian/Urdu, Hebrew, Devanagari,
  Bengali, Thai, CJK, Korean, emoji/flags, numbers, punctuation, invalid UTF-8
- Documented vs dictionary/linguistic segmentation; grapheme API unchanged
- Debug / Release / ASan+UBSan: **127/127** tests pass

**Definition of done:**

- [x] UAX #29 word boundaries (not space-split heuristics)
- [x] Official WordBreakTest conformance
- [x] Streaming UTF-8 iterator with byte-range segments
- [x] Bytes / code points / graphemes / words distinguished in API + docs
- [x] Invalid UTF-8 policy matches RoboUnicode
- [x] No sentence/line/bidi/shaping/dictionary work
- [x] Existing suites still pass

## Goal 3.9 — Unicode Sentence Segmentation (UAX #29)

**Status:** complete

**Done:**

- Official Unicode **16.0.0** Sentence_Break tables (`Tools/ucd/generate_sentence.py`)
- Rules SB1–SB11 + SB998; Extend|Format ignored except after sot/ParaSep
- Public API `RoboUnicodeSentence.h` — `sentence_break`, streaming UTF-8 iterator, next/prev/count
- Sentence segments as half-open UTF-8 byte ranges `[sentence_start, sentence_end)`
- Official `SentenceBreakTest.txt` conformance: **512 / 512**
- Unit coverage: Latin/European, Arabic/Persian/Urdu, Hebrew, Indic, Thai, CJK, Korean,
  emoji, quotes/parens, numbers/abbreviations, paragraph separators, invalid UTF-8
- Documented vs NLP sentence detection; word/grapheme APIs unchanged
- Test registry capacity raised to 256
- Debug / Release / ASan+UBSan: **138/138** tests pass

**Definition of done:**

- [x] UAX #29 sentence boundaries (not `.`/`!`/`?` heuristics)
- [x] Official SentenceBreakTest conformance
- [x] Streaming UTF-8 iterator with byte-range segments
- [x] Bytes / code points / graphemes / words / sentences distinguished
- [x] Invalid UTF-8 policy matches RoboUnicode
- [x] No line/bidi/shaping/NLP/dictionary work
- [x] Existing suites still pass

## Goal 3.10 — Unicode Line Breaking (UAX #14)

**Status:** complete

**Done:**

- Official Unicode **16.0.0** Line_Break tables (`Tools/ucd/generate_linebreak.py`)
- Rules LB1–LB31 (CM/ZWJ treat-as, numbers, quotes, RI, emoji, Brahmic, Hangul, CJK)
- Public API `RoboUnicodeLine.h` — class lookup, opportunity kinds
  (`PROHIBITED` / `ALLOWED` / `MANDATORY`), streaming UTF-8 iterator
- Line segments as half-open UTF-8 byte ranges with break kind
- Official `LineBreakTest.txt` conformance: **16,672 / 16,672**
- Unit coverage: spaces/CR/LF/CRLF/separators, numbers, CJK, Hangul, scripts,
  emoji/ZWJ/RI, combining, quotes, invalid UTF-8
- Reference oracle `Tools/ucd/lb_ref.py`; independent of word/sentence layers
- Debug / Release / ASan+UBSan: **151/151** tests pass

**Definition of done:**

- [x] UAX #14 opportunities (not ASCII whitespace wrapping)
- [x] Mandatory vs allowed vs prohibited distinguished
- [x] Official LineBreakTest conformance
- [x] Streaming UTF-8 iterator with byte-range segments
- [x] Independent of grapheme/word/sentence semantics
- [x] Invalid UTF-8 policy matches RoboUnicode
- [x] No layout/CSS/hyphenation/bidi/shaping
- [x] Existing suites still pass

## Goal 3.11 — Unicode Bidirectional Algorithm (UAX #9)

**Status:** complete

**Done:**

- Official Unicode **16.0.0** Bidi_Class + bracket tables (`Tools/ucd/generate_bidi.py`)
- Full UBA: P2–P3, X1–X10, W1–W7, N0–N2 (brackets), I1–I2, L1–L2
- Public API `RoboUnicodeBidi.h` — class lookup, paragraph direction (LTR/RTL/AUTO),
  analyze / analyze_utf8, resolve_levels, reorder, result destroy
- Levels + visual order; X9-removed → `ROBO_BIDI_LEVEL_REMOVED`; UTF-8 `byte_ends`
- Official `BidiCharacterTest.txt` conformance: **91,707 / 91,707**
- Python oracle also green on abstract `BidiTest.txt`: **770,241 / 770,241**
- Unit coverage: ASCII, Arabic, Persian, Urdu, Hebrew, mixed, EN/AN digits,
  punctuation/brackets, NSM, embeddings/overrides, isolates/FSI, paragraphs,
  empty/long/invalid UTF-8
- Documented: bidi ≠ shaping; separate from line breaking; source-index mapping
- Debug / Release / ASan+UBSan: **166/166** tests pass

**Definition of done:**

- [x] Full UAX #9 (not Arabic=RTL heuristics)
- [x] Official BidiCharacterTest conformance
- [x] Embeddings, overrides, isolates, weak/neutral/implicit, L2 reorder
- [x] Explicit vs AUTO paragraph direction
- [x] Bytes / code points / levels / visual order distinguished
- [x] Invalid UTF-8 policy matches RoboUnicode
- [x] No shaping/fonts/layout/CSS bidi
- [x] Existing suites still pass

## Goal 3.12 — Script and Script_Extensions (UAX #24)

**Status:** complete

**Done:**

- Extended UCD pipeline for **Script_Extensions** + ISO 15924 short names
  (`Scripts.txt`, `ScriptExtensions.txt`, `PropertyValueAliases.txt`)
- Full `robo_unicode_script_t` enum (all Unicode 16.0.0 scripts)
- API: `script`, `script_extensions`, `has_script`, long/short names, `from_name`
- Minimal `script_runs` heuristic (documented; Common/Inherited continuation)
- Compact SCX: 110 unique sets, 174 ranges, sorted uint16 pool
- Conformance: 204 SCX ranges + 2,253 Script samples; `validate_script.py` green
- Coverage: Latin…Hangul catalogue, Persian/Urdu Arabic letters, historical
  scripts, Common/Inherited/SCX, independent of bidi
- Debug / Release / ASan+UBSan: **172/172** tests pass

**Definition of done:**

- [x] Official Script + Script_Extensions (not hand tables)
- [x] Script ≠ Script_Extensions documented and tested
- [x] All Unicode 16 scripts representable
- [x] Short/long names + Qaai
- [x] Independent of bidi / normalization / segmentation
- [x] No font/shaping/RoboText work
- [x] Existing suites still pass

## Goal 3.13 — Unicode Emoji Properties and Extended Emoji Handling (UTS #51)

**Status:** complete

**Done:**

- Extended shared UCD bool properties: Emoji_Modifier, Emoji_Modifier_Base,
  Emoji_Component, Regional_Indicator, Variation_Selector (+ existing Emoji /
  Emoji_Presentation / Extended_Pictographic)
- Official **Emoji 16.0** RGI sequence trie (`generate_emoji.py`) from
  `emoji-sequences.txt` + `emoji-zwj-sequences.txt`
- Public API `RoboUnicodeEmoji.h` — classify / UTF-8 match / next + kinds
- Property wrappers remain on `RoboUnicodeProps.h` (no duplicate tables)
- Boundaries documented: properties ≠ sequences ≠ graphemes ≠ glyphs ≠ paint
- UAX #29 grapheme algorithm unchanged; sequence layer is parallel catalog
- Conformance: fully-qualified `emoji-test.txt` ∩ catalog **3,781 / 3,781**
- Coverage: keycap/flag/modifier/ZWJ/tag/VS, invalid UTF-8, intl mixes
- Debug / Release / ASan+UBSan: **182/182** tests pass

**Definition of done:**

- [x] Official UTS #51 / emoji-data (not hand-coded famous emoji)
- [x] Independent emoji property bits preserved
- [x] RGI sequence categories (basic/keycap/flag/tag/modifier/ZWJ)
- [x] UTF-8 byte ranges; STRICT invalid UTF-8 = same RoboUnicode policy
- [x] Grapheme relationship documented; no unofficial emoji-grapheme algo
- [x] No fonts / color glyphs / shaping / layout / graphics
- [x] Existing suites still pass
- [x] Chromium remains reference-only

## Goal 3.14 — Unicode Data Generation, Versioning, Validation, Conformance

**Status:** complete

**Done:**

- Audit: `unicode/AUDIT.md`; upgrade workflow: `unicode/UPGRADE.md`
- Single pin: `Tools/ucd/robo_unicode_version.py` → `RoboUnicodeVersion.h`
- Machine-readable `manifest.json` (official URLs, SHA-256, roles)
- Orchestrator: `generate_all.py` (validate → generate → golden / repro)
- `verify_pipeline.py`, `golden_hashes.json`, `measure_tables.py`
- `Fetch-Ucd.ps1` reads manifest + verifies checksums
- Attribution: `Docs/UnicodeDataAttribution.md`
- Infra suite: cross-module version coherence, broad scripts, microbench
- Offline policy: normal `Build.ps1` never downloads or regenerates
- Deterministic generation: `--repro` green (31 artifacts)
- Debug / Release / ASan+UBSan: **185/185** tests pass

**Definition of done:**

- [x] One coherent Unicode / Emoji version across all modules
- [x] Manifest + checksum validation
- [x] Reproducible generation without wall-clock metadata
- [x] Golden hashes detect generator drift
- [x] Conformance fixtures remain official Unicode data
- [x] No Chromium dependency; no fonts/shaping/layout
- [x] Goals 3.3–3.13 still pass

## Goal 4.1 — RoboI18N Foundation (Locale / BCP 47)

**Status:** complete

**Done:**

- Module: `Include/RoboI18N/`, `Source/RoboI18N/`, guide `i18n/README.md`
- BCP 47 parse/format for language / script / region / variants / extensions / private-use
- Value-type `robo_i18n_locale_t` (no heap for ordinary parse)
- IANA Preferred-Value + grandfathered/redundant tables (`generate_iana.py`, File-Date 2026-05-05)
- Equality, range matching, truncation `parent` fallback
- Default locale abstraction (`und`, not hard-coded `en-US`)
- Locale preference lists with optional `q=` weights
- CLDR architecture/manifest only (`version: none`) — no invented locale data
- Suite: basic/script/region/complex/invalid/canon/match/fallback/list/fuzz/microbench
- Debug / Release / ASan+UBSan: **197/197** tests pass

**Definition of done:**

- [x] BCP 47 foundation (not stringly `"en"` only)
- [x] Canonicalization from IANA (not invented maps)
- [x] Fallback documented and tested
- [x] Unicode Script ≠ locale documented
- [x] No formatting / plural / collation / messages / detection
- [x] No Chromium dependency
- [x] Existing Unicode tests still pass

## Next

- Goal 4.2+ — locale display / numbering systems / formatting (do not start until requested)

# Roboticela Runtime Progress

## Note on commands in this log

Entries below were written as the work landed, and some of them name test and
run scripts that have since been consolidated. History is left as written; the
current commands are:

```
scripts\robo check                 environment validation
scripts\robo build-all             the whole system
scripts\robo test-all              every test surface
scripts\robo test-<component>      base, memory, containers, unicode, i18n,
                                   font, script, graphics, window, display,
                                   html, demo, infra
scripts\robo test-qemu-<stage>     boot, kernel, display
scripts\robo validate-deployment   structural validation of the artifact
scripts\robo run-qemu              full build, then boot the OS
```

See `docs/Build.md` at the repository root for the authoritative guide.

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
- Debug / Release / ASan+UBSan: **197/197** tests pass (at Goal 4.1 completion)

**Definition of done:**

- [x] BCP 47 foundation (not stringly `"en"` only)
- [x] Canonicalization from IANA (not invented maps)
- [x] Fallback documented and tested
- [x] Unicode Script ≠ locale documented
- [x] No formatting / plural / collation / messages / detection
- [x] No Chromium dependency
- [x] Existing Unicode tests still pass

## Goal 4.2 — RoboI18N Locale Data and CLDR Integration

**Status:** complete

**Done:**

- CLDR **46** (JSON 46.0.0) pin; Unicode association **16.0.0** from CLDR metadata
- Generator `Tools/i18n/generate_cldr.py` + `Fetch-Cldr.ps1` + manifest checksums
- Compact tables: string pool, likely subtags, numbering systems, calendar prefs,
  measurement, parentLocales, per-locale numbering defaults
- APIs: maximize/minimize, data parent/root inheritance, numbering/calendar/measurement
- Suites: `SuiteRoboI18NCldr.c` (intl locales, digits, inheritance, microbench)
- Deterministic generation (`--repro` + `cldr_golden_hashes.json`)
- Docs: `i18n/README.md`, `CLDR_ATTRIBUTION.md`, architecture/version pins
- No number/date formatting, plurals, collation, or messages
- Debug / Release / ASan+UBSan: **209/209** tests pass
- Generated `robo_cldr_data.c` ≈ **511 KiB** source (string pool + tables)

**Definition of done:**

- [x] Versioned CLDR subset, offline builds
- [x] Likely subtags maximize/minimize from CLDR data
- [x] Numbering-system metadata (incl. non-Latin digits)
- [x] Calendar preference metadata (ids only)
- [x] Measurement preferences
- [x] Locale data inheritance including root
- [x] International coverage tests (ur/ar/fa/he/hi/bn/th/zh/ja/ko/…)
- [x] No Chromium dependency; no premature formatting subsystems

## Goal 4.3 — Numbering Systems and Number Formatting Foundation

**Status:** complete

**Done:**

- Extended CLDR generator: symbols, decimal patterns, grouping sizes,
  `minimumGroupingDigits` (511 locale×nsys rows); `robo_cldr_data.c` ≈ **556 KiB**
- Public API `RoboI18NNumber.h`: reusable stack formatter, options, rounding modes
- Exact `int64`/`uint64`/`robo_i18n_decimal_t`; documented IEEE-754 `double` path
- Locale symbols (en/de/fr), Indian grouping (hi/bn), localized digits (arab/arabext/deva/beng/thai)
- Buffer size query + `RoboString` append; no visual/rendering code
- Suite `SuiteRoboI18NNumber.c`; Debug / Release / ASan+UBSan: **221/221**
- Docs: `i18n/README.md`, architecture, version **0.3.0**

**Definition of done:**

- [x] CLDR-driven decimal/group/sign/NaN/∞ symbols
- [x] Grouping primary/secondary + min grouping threshold
- [x] Numbering-system digit substitution
- [x] Integer exactness (incl. UINT64_MAX)
- [x] Documented rounding (default half-even)
- [x] International locale coverage tests
- [x] No currency/date/message/collation/fonts
- [x] No Chromium dependency

## Goal 4.4 — Plural Rules and Locale-Aware Plural Categories

**Status:** complete

**Done:**

- CLDR 46 `plurals.json` / `ordinals.json` → build-time bytecode (`plural_compile.py`)
- Public API `RoboI18NPlural.h`: cardinal/ordinal select, operands, category names
- Exact decimal operands (`n,i,v,w,f,t`; `e/c=0` for ordinary inputs); negative → abs
- Locale resolve: language-region → language-script → language → data parents (`pt-PT` vs `pt`)
- Generated samples from CLDR `@integer`/`@decimal` → `Tests/Data/robo_plural_samples.txt`
- Suite `SuiteRoboI18NPlural.c` (ar/ru/pl/fr/en/ja/… + official samples)
- Debug / Release / ASan+UBSan: **241/241**; CLDR samples **14335/14335**
- Version **0.4.0** / generator **1.3.0**; no message formatting
- Perf (Release): ~60k selects in ~14 ms

**Definition of done:**

- [x] Data-driven cardinal rules (not English binary plural)
- [x] Cardinal + ordinal APIs; ordinal rules from CLDR
- [x] Operands preserve trailing zeros (`1` vs `1.0` vs `1.00`)
- [x] Locale fallback without wrong-language selection
- [x] Official CLDR sample coverage + Arabic/Slavic/decimal/negative/large tests
- [x] No Chromium dependency; no message engine

## Goal 4.5 — Date/Time and Calendar Foundation

**Status:** complete

**Done:**

- Date/time model: instant (`unix_ms`), Gregorian date, wall time (ns), datetime, duration
- Calendar abstraction: gregorian/iso8601/buddhist/japanese conversions; islamic/hebrew/persian/chinese metadata-only
- CLDR calendar generator: weekData, era starts, names/patterns → `RoboCalData.*`
- Time-zone abstraction: UTC + fixed offsets; IANA deferred to Goal 4.14 (`i18n/TZDB.md`)
- Locale week/weekend + calendar display metadata APIs
- Suite `SuiteRoboI18NDateTime.c`; version was **0.5.0** / generator **1.4.0** at completion
- Debug / Release / ASan+UBSan: **252/252** at Goal 4.5 completion
- Localized date/time formatting deferred to Goal 4.6

**Definition of done:**

- [x] Documented model separating instant / local / calendar / zone / locale
- [x] Gregorian + ISO week primitives; leap-year rules
- [x] Instant UTC round-trips; fixed-offset zones
- [x] CLDR calendar metadata generated/validated
- [x] tzdb architecture documented (data not embedded)
- [x] No Chromium dependency; no full formatting engine

## Goal 4.6 — Localized Date/Time Formatting

**Status:** complete

**Done:**

- `RoboI18NDateTimeFormat` — style / pattern / skeleton formatters on top of Goal 4.5
- CLDR short/medium/long/full date & time; datetime combination via `{0}`/`{1}` patterns
- Compiled pattern tokens (no re-parse per format call)
- Skeleton normalize + exact/`availableFormats` match + documented best-match scoring
- Month/weekday/era/day-period names (wide/abbr/narrow where generated)
- Numbering-system digits via `robo_i18n_number_formatter`
- Zone display: GMT/ISO offsets + IANA id; **no invented localized zone names**
- Calendars with conversion only (gregorian/buddhist/japanese); islamic/hebrew/persian/chinese → `ROBO_NOT_SUPPORTED`
- Suite `SuiteRoboI18NDateTimeFormat.c`; API was **0.6.0** / generator **1.5.0** at completion
- `availableFormats` tables in `RoboCalData.*`
- Test harness limit raised to 384 cases
- Debug / Release / ASan+UBSan: **263/263** at Goal 4.6 completion

**Definition of done:**

- [x] Localized date / time / datetime formatting
- [x] CLDR styles + patterns + skeleton foundation
- [x] Localized field names + numbering systems
- [x] Calendar selection where conversion exists
- [x] Time-zone fields integrated with existing abstraction
- [x] RTL / non-Latin semantic string tests
- [x] Deterministic CLDR generation; no Chromium

## Goal 4.7 — Date/Time Interval and Relative Time Foundation

**Status:** complete

**Done:**

- Interval model (`robo_i18n_datetime_interval_t` / instant interval) with explicit reverse handling
- CLDR `intervalFormats` + fallbacks in `RoboCalData.*` (2274+ patterns)
- Greatest-difference field selection + ICU-style repeated-letter pattern split
- Relative-time model + `dateFields` tables (`RoboRelData.*`) for 18 locales
- Auto keywords (yesterday/today/…) vs always-numeric modes
- Plural categories via existing `robo_i18n_plural_*` + number formatting for `{0}`
- Suites `SuiteRoboI18NInterval.c`, `SuiteRoboI18NRelative.c`
- API **0.7.0** / generator **1.6.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **276/276**

**Definition of done:**

- [x] Interval model + CLDR interval formatting with field collapse
- [x] Relative-time formatting with plural integration
- [x] Numeric and natural modes where CLDR provides them
- [x] Non-Latin / RTL / Slavic plural tests
- [x] Calendar / timezone interaction where supported
- [x] Deterministic generation; no Chromium

## Goal 4.8 — Calendar Arithmetic and Date/Time Utilities

**Status:** complete

**Done:**

- `RoboI18NDateTimeArith` — period vs duration, date/instant arithmetic, comparisons
- End-of-month policy: **CLAMP** to last valid day (e.g. Jan 31 + 1 month → Feb 28/29)
- ISO week start/end / weeks-in-year; locale week boundaries (CLDR first day) distinct from ISO
- Civil boundaries (day/month/quarter/year); zone-aware start-of-day and instant+period (FIXED/UTC)
- Calendar-aware `add_period` via Gregorian bridge for gregorian/iso8601/buddhist/japanese; islamic/persian/hebrew → `ROBO_NOT_SUPPORTED`
- Business-day abstraction (CLDR weekend + optional holiday hook; no holiday DB)
- Checked integer overflow (`ROBO_ERROR` / existing `ROBO_NOT_SUPPORTED` on instant overflow)
- Suite `SuiteRoboI18NDateTimeArith.c` (invariants, clamp, calendars, business days, fuzz)
- API **0.8.0** / generator **1.7.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **290/290**

**Definition of done:**

- [x] Date / duration / period arithmetic with documented EOM policy
- [x] Instant arithmetic with overflow checks
- [x] ISO week + boundary utilities
- [x] Time-zone-aware period semantics (FIXED/UTC; IANA → unsupported until tzdb)
- [x] Business-day architecture without hard-coded holidays
- [x] Multi-calendar support / explicit unsupported errors
- [x] Property/fuzz invariants; Debug / Release / Sanitize green
- [x] No Chromium dependency

## Goal 4.9 — Locale-Aware Collation Foundation

**Status:** complete

**Done:**

- `RoboI18NCollation` / `robo_i18n_collator_t` — locale-bound collator (no hidden global locale)
- UCA / DUCET **16.0.0** tables (`RoboUcaData`, `generate_uca.py`, `Fetch-Uca.ps1`)
- Strengths: primary…identical; alternate non-ignorable/shifted; numeric digit-run option
- Pipeline: STRICT UTF-8 → NFD → CE formation (contractions/expansions/implicit) → level compare / sort keys
- Binary / code-point helpers kept separate from linguistic compare
- CLDR **46** collation metadata + compiled remaps for selected tailorings (sv/es/de-phonebook/tr/…)
- Large CJK/Arabic rule CDATA: architecture present (type metadata); runtime uses root UCA until full LDML builder
- Conformance sample from Unicode `CollationTest_NON_IGNORABLE_SHORT.txt`
- Suite `SuiteRoboI18NCollation.c`
- API **0.9.0** / generator **1.8.0** / UCA **16.0.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **300/300**

**Definition of done:**

- [x] RoboCollator with compare / equal / sort keys
- [x] UCA foundation + authoritative allkeys data (deterministic)
- [x] Locale tailoring architecture + representative remaps
- [x] Strength / alternate / numeric / case-first options
- [x] Invalid UTF-8 → `ROBO_PARSE_ERROR`
- [x] International locale smoke + fuzz + UCA sample tests
- [x] Debug / Release / Sanitize green; no Chromium

## Goal 4.10 — Locale Display Names and Localization Data Foundation

**Status:** complete

**Done:**

- `RoboI18NDisplayNames` — language / script / region / currency / calendar /
  numbering-system / time-zone / composed locale display names
- Identifier vs display locale always distinct; output is logical UTF-8 (no bidi reorder)
- Compact CLDR **46** tables (`RoboDispData`, `generate_display.py`) from
  localenames, currencies, timeZoneNames, metaZones
- Fallback: display locale → CLDR parent / truncation → **en** tables →
  identifier (`FALLBACK_CODE`, default) or `ROBO_NOT_FOUND`
- Locale composition via CLDR `localePattern` / `localeSeparator` (not naive join)
- Currency name / symbol / narrow symbol; no conversion rates
- TZ: exemplar city, metazone long names, GMT format for FIXED/UTC; IANA GMT
  without tzdb → `ROBO_NOT_SUPPORTED` where offset is required
- Context: standard names only; `dialect` option reserved
- Suite `SuiteRoboI18NDisplayNames.c` (RTL/non-Latin smoke + fuzz)
- API **0.10.0** / generator **1.9.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **310/310**

**Definition of done:**

- [x] All display-name categories listed in Goal 4.10
- [x] Deterministic generated data + version metadata
- [x] Explicit missing-data / fallback policy
- [x] International locale coverage + fuzz; no invented translations
- [x] Debug / Release / Sanitize green; no Chromium

## Goal 4.11 — Message Localization and Resource Catalog Foundation

**Status:** complete

**Done:**

- RoboMsg source format (`.robomsg.json`) + `generate_messages.py` validator/compiler
- Generated `RoboMsgData` tables + `RMSG` binary blob (`robo_msg_demo_blob`)
- `RoboI18NMessage` — catalog open/load/close, exists/get/format/format_id
- Stable message IDs; optional translator `context`; named `{placeholder}` + `{{`/`}}` escapes
- Plural map architecture selecting via existing `robo_i18n_plural_*`
- Locale fallback: exact → truncation parents → **configured** fallback (not assumed English)
- Missing-message: `NOT_FOUND` or `RETURN_ID` (never empty success)
- Demo catalogs for 15 locales (RTL / non-Latin / emoji / combining)
- Suite `SuiteRoboI18NMessage.c` (fallback, blob load, fuzz)
- Docs: `i18n/RoboMsg.md`
- API **0.11.0** / generator **1.10.0**
- Debug / Release / ASan+UBSan: **319/319**

**Definition of done:**

- [x] Catalog format + compiler + runtime generated catalogs
- [x] Deterministic fallback / missing / placeholder / plural architecture
- [x] International coverage + malformed rejection + fuzz
- [x] Debug / Release / Sanitize green; no Chromium

## Goal 4.12 — RoboI18N Message Formatting Engine

**Status:** complete

**Done:**

- RoboMsgFmt parser + AST + formatter (`RoboI18NMessageFormat`)
- Literals, named args, `number` / `date` / `time`, `select`, `plural`, `selectordinal`
- Exact plural selectors (`=N`), `#` → existing number formatter
- Nested select/plural; depth / size / output limits
- Explicit value model (string / i64 / u64 / decimal / double / bool / date/time/instant)
- Catalog integration: `robo_i18n_message_format_mf`
- Ordinal architecture via existing `robo_i18n_plural_ordinal`
- Suite `SuiteRoboI18NMessageFormat.c` (intl + malformed + fuzz)
- Docs: `i18n/RoboMsgFormat.md`
- API **0.12.0** / generator **1.11.0** (bumped by later goals)
- Debug / Release / ASan+UBSan: **334/334**

**Definition of done:**

- [x] Parser + reusable message representation
- [x] select / cardinal plural / exact / `#` / nesting
- [x] Number + date/time delegated to RoboI18N
- [x] Ordinal API boundary integrated
- [x] Catalog integration; malformed + Unicode + intl tests
- [x] Debug / Release / Sanitize green; no Chromium

## Goal 4.13 — Message Formatting Validation and Compatibility Layer

**Status:** complete

**Done:**

- RoboMsgFmt syntax version **1** + optional `#!robo-mf/1` prologue
- Structured diagnostics (`robo_i18n_mf_diagnostic_t`)
- `message_validate` / `mf_validate_ex` / `mf_parse_ex` with limits
- Static argument analysis + contract compatibility
- Duplicate-branch rejection; type-conflict detection
- Catalog entry enumeration + catalog-wide / fallback validation
- Golden + negative + fuzz suite (`SuiteRoboI18NMessageValidate.c`)
- Docs: `i18n/RoboMsgFormat.md` validation section
- API **0.13.0** / generator **1.12.0** (bumped by later goals)
- Debug / Release / ASan+UBSan: **350/350**

**Definition of done:**

- [x] Versioning + validation + diagnostics + analysis
- [x] Catalog / fallback compatibility
- [x] Limits, golden, negative, fuzz
- [x] Debug / Release / Sanitize green; no Chromium

## Goal 4.14 — Time Zones and Time Zone Database Foundation

**Status:** complete

**Done:**

- Time-zone model: Instant / local / offset / IANA zone / fixed / UTC (not locale)
- IANA tzdb **2026c** pipeline: `Fetch-Tzdb.ps1` + `generate_tzdb.py`
- Compact `RoboTzData` tables (zones, aliases, transitions, string pool)
- TZif parse + POSIX TZ footer expansion through 2100
- Lookup, offset/DST/abbr, instant↔local, next/prev transition
- Ambiguous / nonexistent local times with EARLIER / LATER / REJECT
- Aliases (Link) without duplicating rules; fixed-offset zones ±18h
- Leap-second policy: POSIX 86400-day days (not modeled)
- Suite `SuiteRoboI18NTimeZone.c` (DST, round-trip, intl, fuzz)
- Docs: `i18n/TZDB.md`, `Tools/i18n/TZDB_ATTRIBUTION.md`
- API **0.14.0** / generator **1.13.0** (bumped by later goals)
- Debug / Release / ASan+UBSan: **361/361**

**Definition of done:**

- [x] Model + public API + tzdb integration + reproducible generation
- [x] Instant↔local + ambiguity/gap + transitions + aliases + fixed
- [x] Historical range documented; validation + golden hashes
- [x] Integration with existing datetime APIs; no Chromium
- [x] Debug / Release / Sanitize green

## Goal 4.15 — Time Zone Formatting, Names, and Display Data

**Status:** complete

**Done:**

- Timezone display styles: exemplar, long/short generic/std/dst, specific, GMT, identifier
- `robo_i18n_timezone_display_name_at` (instant-aware metazone + DST)
- CLDR data: short metazone names, zone overrides, historical mz ranges, gmtZeroFormat
- GMT formatting with locale patterns + numbering-system digits
- Exemplar cities (CLDR or structural `_`→` ` fallback)
- Alias → canonical data lookup; requested id for IDENTIFIER
- Date/time formatter `z`/`v`/`V`/`O` wired to display layer
- Suite `SuiteRoboI18NTimeZoneDisplay.c`; docs `i18n/TZDisplay.md`
- API **0.15.0** / generator **1.14.0**
- Debug / Release / ASan+UBSan: **369/369**

**Definition of done:**

- [x] Localized names + GMT + exemplar + metazone windows
- [x] Formatter integration + fallbacks + intl tests
- [x] Generated/validated CLDR display data; no Chromium
- [x] Debug / Release / Sanitize green

## Goal 4.16 — Locale Data Completeness, Fallback, and Runtime Locale Service

**Status:** complete

**Done:**

- `RoboI18NLocaleService` — Unicode keywords (`ca`/`nu`/`hc`/`co`/`tz`), resolve settings, fallback chain, negotiate
- Application vs default vs effective locale abstraction (no OS coupling)
- Centralized numbering / calendar / hour-cycle / collation / timezone selection with documented precedence
- CLDR `timeData` → `robo_hc_data` (`generate_hour_cycle.py`)
- Number, DateTimeFormat, Collation, Message wired to the service / data parents
- Capability queries + `robo_i18n_data_versions`
- Consistency validator `validate_locale_consistency.py`
- Suite `SuiteRoboI18NLocaleService.c`; docs `i18n/LocaleService.md`
- API **0.16.0** / generator **1.15.0**
- Debug / Release / ASan+UBSan: **381/381**

**Definition of done:**

- [x] RoboLocale runtime context + centralized resolve/fallback/matching
- [x] Cross-subsystem consistency + intl/robustness tests
- [x] Documentation + version metadata
- [x] Debug / Release / Sanitize green

## Goal 4.17 — Locale Negotiation, Accept-Language, and Language Matching

**Status:** complete

**Done:**

- `RoboI18NLocaleNegotiate` — language ranges, q millipercent, Accept-Language parse/serialize
- Script-sensitive matching via CLDR likely subtags; region ranking; `q=0` exclusions; `*`
- Deterministic `negotiate_ex` + match metadata; preference normalize (highest-q / first-wins)
- Goal 4.16 `locale_negotiate` delegates to the new engine
- Suite `SuiteRoboI18NLocaleNegotiate.c`; docs `i18n/LocaleNegotiate.md`
- API **0.17.0** (generator **1.15.0**, no new CLDR tables)
- Debug / Release / ASan+UBSan: **395/395**

**Definition of done:**

- [x] Ranges, weights, matching, negotiation, Accept-Language, limits, fuzz
- [x] Intl + cross-subsystem tests; Debug / Release / Sanitize green

## Goal 4.18 — Ordinal Plural Rules

**Status:** complete

**Done:**

- Confirmed CLDR 46 ordinal bytecode path (`ordinals.json` → `robo_plural_ordinal_index`)
- API: `plural_ordinal` / `ordinal_i64`, `has_rules`, `category_from_name`; categories always include `other`
- Cardinal vs ordinal remain distinct; locale fallback shared with Goal 4.4
- RoboMsgFmt `selectordinal` + exact-selector precedence verified
- Suite `SuiteRoboI18NOrdinal.c`; docs `i18n/OrdinalPlurals.md`
- API **0.18.0** (generator **1.15.0**; existing plural tables)
- Debug / Release / ASan+UBSan: **408/408** (CLDR plural samples **14335/14335**)

**Definition of done:**

- [x] Ordinal categories + CLDR data + operands + locale integration
- [x] Message-format selectordinal + exact selectors
- [x] Intl / conformance / fuzz coverage
- [x] Documentation; Debug / Release / Sanitize green

## Goal 4.19 — Relative Time and Unit Formatting

**Status:** complete

**Done:**

- CLDR units tables (`generate_units.py` → `RoboUnitData.*`): 60 curated units × 3 widths × 19 locales
- `RoboI18NUnit` — long/short/narrow formatting, plural patterns, compound `per`, conversion foundation
- Extended Goal 4.7 relative AUTO for CLDR `relative-type--2` / `+2` (e.g. anteayer / vorgestern)
- Suites `SuiteRoboI18NUnit.c` (+ relative ±2); docs `i18n/Units.md`
- API **0.19.0** / generator **1.16.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **417/417**
- No unit parsing (deferred); no Chromium dependency

**Definition of done:**

- [x] Locale-aware unit formatting (styles, plurals, compounds)
- [x] Conversion separated from localization (exact vs approximate documented)
- [x] Relative-time production layer on 4.7 foundation (special forms ±2)
- [x] Intl / fuzz tests; Debug / Release / Sanitize green
- [x] Documentation; PROGRESS updated

## Goal 4.20 — Measurement Systems and Unit Preferences

**Status:** complete

**Done:**

- CLDR `unitPreferenceData` + temperature measurement systems → `RoboMeasurePrefData.*`
- `RoboI18NMeasure` preference context (system, category overrides, `ms` keyword)
- Preferred-unit resolution with documented precedence; integrate convert→format
- Suite `SuiteRoboI18NMeasure.c` (region matrix, lang≠region, conversions, fuzz)
- Docs `i18n/MeasurePrefs.md`
- API **0.20.0** / generator **1.17.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **425/425**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Measurement systems + locale-derived preferred units
- [x] User/app overrides without global mutable state
- [x] Integration with Goal 4.19 convert/format
- [x] Intl / conversion / fuzz tests; Debug / Release / Sanitize green
- [x] Documentation; PROGRESS updated

## Goal 4.21 — Currency Data and Currency Formatting

**Status:** complete

**Done:**

- CLDR `currencyData` + `codeMappings` + `currencyFormats` → `RoboCurrencyData.*`
- `RoboI18NCurrency` — lookup, region/locale defaults, symbol/code/name/accounting
- Standard vs cash precision; `format_minor` for exact minor units; no FX conversion
- Suite `SuiteRoboI18NCurrency.c` (intl matrix, accounting, cash, fuzz)
- Docs `i18n/Currency.md`
- API **0.21.0** / generator **1.18.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **436/436**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Currency metadata + locale-aware formatting on Goal 4.3 numbers
- [x] Fraction digits / cash rounding separated; accounting from CLDR patterns
- [x] Intl / precision / robustness tests; Debug / Release / Sanitize green
- [x] Documentation; PROGRESS updated

## Goal 4.22 — Currency/Number Parsing

**Status:** complete

**Done:**

- `RoboI18NParse` — strict/permissive number + currency parsers
- Localized digits, grouping, decimal/sign symbols; currency symbol/code/name
- Ambiguity reporting; `require_currency` / accepted sets; round-trip tests
- Suite `SuiteRoboI18NParse.c`; docs `i18n/Parse.md`
- API **0.22.0** / generator **1.19.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **448/448**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Number + currency parsing with structured diagnostics
- [x] Strict vs permissive; ambiguity never silent in strict
- [x] Intl / round-trip / digit / fuzz tests; Debug / Release / Sanitize green
- [x] Documentation; PROGRESS updated

## Goal 4.23 — Date/Time Parsing

**Status:** complete

**Done:**

- `RoboI18NDateTimeParse` — strict/permissive date/time/zoned parsers
- CLDR styles/patterns/skeletons; localized names & digits; calendar fields
- Timezone offset/IANA parsing; DST ambiguity policies; no invented UTC
- Suite `SuiteRoboI18NDateTimeParse.c`; docs `i18n/DateTimeParse.md`
- API **0.23.0** / generator **1.20.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **461/461**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Date/time parsing with structured diagnostics
- [x] Strict vs permissive; missing zone never invents UTC
- [x] Intl / round-trip / calendar / DST / digit / fuzz tests green
- [x] Documentation; PROGRESS updated

## Goal 4.24 — Locale-Aware Search & String Matching

**Status:** complete

**Done:**

- `RoboI18NSearch` — explicit modes (exact / codepoint / canonical / casefold /
  canonical+casefold / collation); diacritic-insensitive option; grapheme-safe ranges
- Reusable searcher with owned collator; UTF-8 byte hit ranges; find / iter / find_all
- Builds on RoboUnicode norm/casefold/grapheme + Goal 4.9 collation (no duplicated UCA)
- No transliteration; logical (not visual) order; input / find_all limits
- Suite `SuiteRoboI18NSearch.c`; docs `i18n/Search.md`
- API **0.24.0** / generator **1.21.0** / CLDR **46** / Unicode **16.0.0**
- Debug / Release / ASan+UBSan: **472/472**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Explicit matching modes (not one fuzzy compare)
- [x] Canonical / casefold / diacritic / collation search with source byte ranges
- [x] Grapheme-safe defaults; intl + RTL + fuzz tests; Debug / Release / Sanitize green
- [x] Documentation; PROGRESS updated

## Goal 4.25 — Transliteration Foundation

**Status:** complete

**Done:**

- `RoboI18NTranslit` — CLDR transform pipeline (curated IDs), offline
  `generate_translit.py` + `Fetch-Transforms.ps1`
- Compact rule tables (2835 rules / 16 IDs); forward/reverse; streaming hold
- Distinct from search/collation/translation; search does not auto-transliterate
- Suite `SuiteRoboI18NTranslit.c`; docs `i18n/Transliteration.md`
- API **0.25.0** / generator **1.23.0** / CLDR **46**
- Debug / Release / ASan+UBSan: **484/484**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Data-driven transliteration engine + CLDR transform compilation
- [x] Explicit direction / normalization stages / security limits
- [x] Multilingual matrix + fuzz tests; docs; PROGRESS updated

## Goal 4.26 — Locale Data Completeness and Conformance Suite

**Status:** complete

**Done:**

- Authoritative data/version manifest (`Tools/i18n/manifest.json`,
  `i18n/DataManifest.md`) covering Unicode/Emoji/UCA/CLDR/tzdb/IANA/ISO4217-via-CLDR
- `verify_i18n_pipeline.py` — pin coherence, `i18n_golden_hashes.json`, locale
  consistency, Chromium include/link audit
- Expanded `validate_locale_consistency.py` + `robo_i18n_data_versions` (UCA + ISO4217)
- Conformance suite `SuiteRoboI18NConformance.c` — 18-locale corpus, numbering,
  plural kinds, search≠translit, RTL/fuzz/perf smoke
- Completeness report `i18n/CompletenessReport.md` + `Completeness.md`
- API **0.26.0** / generator **1.24.0** / CLDR **46** / Unicode **16.0.0** / tzdb **2026c**
- Debug / Release / ASan+UBSan: **495/495**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Version / generator / golden / locale audits
- [x] Conformance + regression corpus; Chromium independence check
- [x] Documentation; PROGRESS updated

## Goal 4.27 — Final Integration, API Freeze, and Phase 4 Validation

**Status:** complete — **Phase 4 frozen**

**Done:**

- Public API freeze (`ROBO_I18N_API_FROZEN`, API **0.27.0** / generator **1.25.0**)
- Architecture / UTF-8 / performance / license / Phase4 docs
- Final suite `SuiteRoboI18NPhase4.c` — 20-locale regional matrix, format/parse
  round trips, message plural≠ordinal, Unicode-heavy, search byte ranges,
  concurrent formatters, fuzz/perf
- Dependency + Chromium independence re-audit; completeness limitation review
- Debug / Release / ASan+UBSan + `verify_i18n_pipeline.py` green
- Host suite: **507/507**

**Definition of done:**

- [x] Integration audit + dependency direction documented
- [x] Public API frozen; UTF-8 / locale / version contracts documented
- [x] Final regression / sanitizer / Chromium validation
- [x] PROGRESS marks Phase 4 complete

## Goal 8.6 — HTML attribute model

The temporary attribute table is gone. There is now exactly one attribute
type — `robo_html_attribute_t` — which the tokenizer stages, the tree builder
stores, the element owns, the readers return borrowed, and the mutation API
rewrites. Only the lifetime differs from stage to stage, so nothing has to be
translated between the parser and the tree.

**Shipped:**

- `RoboHTMLElement.h/.c` — the model's surface and its write verbs:
  - `robo_html_attribute_t` (borrowed name/value plus byte lengths and a
    namespace field that stays `NONE` until foreign content lands) and
    `robo_html_attribute_init`;
  - read side — `attribute_count`, `attribute_at`, `find_attribute`,
    `has_attribute`, and `attribute_value`, which returns `NULL` for absent
    and a non-`NULL` empty string for present-and-empty;
  - write side — `set_attribute` (collapses duplicates at the first
    match's position, appends when new), `add_attribute` (appends
    unconditionally; the parser's verb, and the only way a duplicate can
    enter the model), `remove_attribute` (drops every match; absent is a
    successful no-op). Names are validated and lowercased on the way in; the
    parser path still tolerates malformed names, because a stray byte must
    not abort a parse.
- `RoboHTMLPayload.c` — the encoder is the model's stored form: explicit
  lengths are the authority, `0` means "measure with `strlen`", and
  `robo_html_payload_mutate_attributes` rewrites an element payload by
  measuring what survives, staging the replacement body in zeroed scratch
  memory, and appending it. Node identity, children and tree position are
  untouched, so attribute mutation survives finalization — 8.4 closed
  *tree construction*, not the document.
- `RoboHTMLInternal.h` — one shared ASCII-case-insensitive name comparison
  (`robo_html_name_equal` / `_span`), used by element-name and attribute-name
  lookups alike, so the two cannot disagree.
- `RoboHTMLTreeBuilder.c` — `stage_attrs` produces `robo_html_attribute_t`
  instead of a second attribute record, preserving source order and
  duplicates; the tree dump reads attributes through the model.
- `RoboDemoHtml.c` — the walk honours the real `hidden` attribute: the
  element is recognised and counted but paints nothing and its subtree is not
  entered. `id`/`class` on the demo document cannot affect the picture, and a
  rule with visible consequences is the only way to show the seam works.
- `RoboHTMLDom.c` — the 8.1 capability flip: `attributes = 1` now that
  attribute storage is real (the field means the attribute model, not a
  `NamedNodeMap` surface, and its comment says so).
- `html/tests/SuiteRoboHTMLAttribute.c` — 21 `html.attr.*` cases;
  `demo/tests/SuiteRoboDemoHtml.c` — `demo.tree.attribute_hidden`.
- Docs: `html/docs/AttributeModel.md`, `html/README.md`,
  `html/docs/Architecture.md` §14, `html/docs/ElementModel.md`.
- RoboHTML **0.6.0** (`ROBO_HTML_API_FROZEN=0`), runtime **0.11.0** (superseded
  by 8.7's 0.7.0 / 0.12.0).

**Defect fixed (found by the new suite):** the mutation scratch buffer was
**never zeroed**, while every string in the payload layout ends at a
terminator the writer does not copy — so the buffer's contents are part of
the encoding. Fresh allocations happened to be zero-filled, which hid the bug
until enough rewrites reused heap: `mutation_many` then read an element name
whose length drifted (3 → 5 → 11 → 85 bytes), the attribute walk lost its
place, and each rewrite silently dropped the table back to one entry.
`robo_html_payload_mutate_attributes` now zeroes the scratch block, exactly
as the payload writer does. Regression tests: `html.attr.mutation_many`,
`html.attr.teardown_with_many_mutations`.

Also fixed while wiring the verbs: a refused name reported
`ROBO_INVALID_ARGUMENT` for every reason when the caller passed no `diag`,
because the strict normalizer reports through the diagnostics record. The
mutation path now supplies one, so a bad character (`PARSE_ERROR`) and a
too-long name (`NOT_SUPPORTED`) stay distinguishable.

**Definition of done:**

- [x] Real `robo_html_attribute_t` attribute set, owned by the element
- [x] Name and value storage with byte lengths; presence distinguishable from
      an empty value (`<input disabled>` vs `<input disabled="">` vs absent)
- [x] Lookup (case-insensitive, first match), insertion, update, removal,
      iteration, deterministic order, defined duplicate handling
- [x] The parser's duplicates and source order reach storage unchanged; the
      tree builder no longer keeps a parallel attribute record
- [x] Minimal namespace foundation on the attribute type
- [x] Bounded table: over the limit the write is refused and the element is
      left exactly as it was
- [x] Unicode values (Arabic/Persian/Urdu/CJK/emoji) and non-ASCII names
      round-trip byte-exactly; malformed input never desynchronises the walk
- [x] Ownership/lifetime safe: attributes cannot outlive their document, and
      nothing dangles across a rewrite
- [x] Debug dump shows attributes, read through the model
- [x] Visual vertical slice unchanged on the host and in QEMU, with `hidden`
      now driven by the real attribute set
- [x] Full suite green (1159/1159, Debug and Release) plus the
      panic/assert/mem-debug child probes
- [x] No Chromium dependency; all independence scans green
- [x] Stop after 8.6

## Goal 8.7 — Text nodes

**Status:** complete

Character data was already a node kind before this phase (`ROBO_HTML_NODE_KIND_TEXT`,
created by the 8.3 builder, already painted by the demo renderer). 8.7 adds the
**model over that kind**, the way 8.5 added one over elements and 8.6 over
attributes. The organising decision is the same: **a text node is a node**, so
it inherits identity, ownership, staleness detection, sibling links and
teardown rather than reimplementing them.

**What changed**

- `Include/RoboHTML/RoboHTMLText.h`, `Source/RoboHTML/RoboHTMLText.c` — the
  text model: `robo_html_text_create` (the construction seam, text twin of
  `robo_html_element_create`), a view (handle / parent / content / length /
  state), the read side (`is_text`, `content`, `length`, `is_empty`,
  `parent`), and the mutation verbs (`set`, `append`). Sibling navigation is
  deliberately left to `RoboHTMLNode.h` rather than mirrored, so there is no
  second traversal surface to keep in step.
- `RoboHTMLTreeBuilder.c` — text creation goes through the seam (the builder
  fills a descriptor instead of registering, encoding and linking by hand),
  and the run merge goes through `robo_html_text_append`. `create_leaf`
  becomes `create_comment_leaf`; comments are not text nodes.
- `RoboHTMLTreeBuilder.c` (dump) — a text line is labelled `Text: "..."` and
  read through `robo_html_text_content`, so the dump cannot drift from the
  accessor and a whitespace-only run is legible.
- `RoboHTMLTypes.h/.c` — `ROBO_HTML_DOMAIN_TEXT` for text-domain rejections.
- `RoboHTMLDom.h/.c` — capability flip: `text_nodes = 1`, with its comment
  making clear it is the text model and not a `CharacterData` surface.
- `html/tests/SuiteRoboHTMLText.c` — 23 `html.text.*` cases.
- Docs: `html/docs/TextModel.md`, `html/README.md`,
  `html/docs/Architecture.md` §15.
- RoboHTML **0.7.0** (`ROBO_HTML_API_FROZEN=0`), runtime **0.12.0**.

**The three contracts worth stating once**

- **`NULL` is not `""`.** `robo_html_text_content` returns `NULL` for "not a
  text node" and a non-NULL empty string for "a text node whose run is
  empty", the same shape `robo_html_element_attribute_value` uses. `is_empty`
  is therefore a different question from `is_text`.
- **A stale handle is a miss, not misuse.** A well-formed handle naming
  nothing live resolves as `ROBO_NOT_FOUND`; only `NULL` arguments are
  argument errors — so every accessor lets a caller test before trusting.
- **Bytes are preserved exactly, and a run coalesces per run.** No
  normalisation, trimming, collapsing, resampling or re-encoding; and the
  tokenizer's segmentation is not observable in the tree, so
  `<p>Hello <b>world</b>!</p>` is three children rather than one node per
  token.

**Consistency defect found by the new suite:** the first draft of `set` /
`append` rejected an ill-formed handle with `ROBO_INVALID_ARGUMENT` while
`view_get` / `parent` / `content` resolved the same handle as `ROBO_NOT_FOUND`.
The suite pinned the inconsistent pair, and the model now answers one way: a
cleared or stale handle is a lookup that missed. No defects were found in the
payload layer this phase — the 8.6 scratch-zeroing fix already covers the
`set`/`append` path, which the 32-iteration and long-run cases exercise.

**Definition of done:**

- [x] A real text node type, integrated with Node, Element and Document
- [x] `robo_html_text_create` as the single construction seam; the tree
      builder goes through it
- [x] Content / length / parent / state access, with `NULL`-vs-`""`
      distinguishing "not text" from "present and empty"
- [x] `set` and `append`, including the append slow path when a payload sits
      behind the run; identity, parent, siblings and order untouched, so
      mutation survives finalization
- [x] Parser integration: real text nodes, runs that coalesce per run,
      elements/comments/mode changes closing a run
- [x] Whitespace (spaces, tabs, newlines, leading/trailing) and Unicode
      (Arabic/Persian/Urdu/Hindi/Bengali/CJK/emoji) preserved byte-exactly
- [x] Character references arrive already decoded and join their run
- [x] Empty-text policy stated and tested: the model permits an empty node,
      the parser never produces one (`<p></p>` has no children)
- [x] Traversal over a mixed child list; text is a leaf with no children
- [x] Ownership/lifetime safe: no dangling content, stale handles harmless,
      teardown releases registry and arena together
- [x] Malformed input never crashes and always leaves a validate_tree-clean
      tree
- [x] Debug dump shows text nodes with escaped whitespace
- [x] Visual vertical slice unchanged on the host and in QEMU
- [x] Full suite green (1182/1182, Debug and Release)
- [x] No Chromium dependency; all independence scans green
- [x] Stop after 8.7

## Goal 8.8 — DOM traversal

**Status:** complete

The tree was already navigable before this phase (`child_at`, `next_sibling`,
`find_element`, `subtree_text`). What 8.8 adds is the **authoritative
traversal surface over every node kind**: one header that answers what is next,
what is above, what is inside, in what order, and where the first match is —
without any caller knowing how children are stored. Three things made it worth
a phase rather than leaving the accessors alone: the same pre-order successor
existed in three places (element search, subtree text, and the debug dump —
recursively), child iteration by index was O(n²), and the recursive dump was a
stack-overflow vector on a tree the node ceiling permits.

**What changed**

- `Include/RoboHTML/RoboHTMLTraversal.h`, `Source/RoboHTML/RoboHTMLTraversal.c`
  — the traversal surface: relationship accessors (parent / first / last /
  next / prev), `depth` and `root`, the child cursor, the all/elements/text
  filters, the pre-order and descendant walkers, and `find_first` /
  `find_descendant`.
- `RoboHTMLNode.c` — `find_element` and `subtree_text` lose their private
  copies of the pre-order successor and go through the walker (element search
  through a predicate, subtree text through a text-filtered descendant walk),
  so the order, the filter, the cycle bound and the no-recursion rule live in
  one place.
- `RoboHTMLTreeBuilder.c` (dump) — the dump is a visitor on the walker instead
  of a recursion; depth arrives from the walk, and the depth cut is now a dump
  policy rather than a traversal limit.
- `RoboHTMLTypes.h/.c` — `ROBO_HTML_DOMAIN_TRAVERSAL` for traversal-domain
  rejections.
- `RoboHTMLDom.h/.c` — capability flip: `traversal = 1`, with `mutation` and
  `events` explicitly still `0`.
- `html/tests/SuiteRoboHTMLTraversal.c` — 20 `html.traverse.*` cases.
- Docs: `html/docs/Traversal.md`, `html/README.md`,
  `html/docs/Architecture.md` §16.
- RoboHTML **0.8.0** (`ROBO_HTML_API_FROZEN=0`), runtime **0.13.0**.

**The contracts worth stating once**

- **Handles, not pointers.** Every accessor takes and returns a node handle, so
  no caller can see the registry, the sibling links, the payload arena or any
  container layout — which is what lets that storage change later without
  touching a caller. Nothing borrowed can outlive its call: a visitor gets a
  view for that call, and the child cursor holds two integers.
- **"Nothing there" is an answer, not an error.** A missing link is
  `ROBO_NOT_FOUND` with a cleared output, mirroring `child_at`'s out-of-range
  contract, so a zeroed handle is never returned as success. `NULL` arguments
  are argument errors; a handle that does not resolve is a lookup that missed.
  Link targets are looked up rather than trusted, so a corrupted or retired
  link reports a miss instead of aliasing a new node.
- **A filter selects what is visited, never what is stepped through.** A walk
  for elements still descends past text nodes, so every filtered walk is a
  subsequence of the same single document order.
- **Iterative, bounded, allocation-free, deterministic.** No recursion, no
  allocation, O(n), and a step bound of the document's live node count so a
  cycle is reported as `ROBO_ERROR` rather than silently truncating the walk.
  Depth is absolute (document = 0) and free to the visitor.
- **The visitor's code is the walk's result**, so early exit needs no cancel
  channel; `find_first` is built on exactly that. A visitor may walk
  reentrantly; destroying the document mid-walk is detected, not dereferenced.

**Contract defect found while refactoring the consumers:** the walker checked
whether the document still existed only at the top of each step, so a visitor
that destroyed the document could still run a final `advance` and report
`ROBO_OK` — a walk reported as finished over a tree that no longer existed.
The check now also runs immediately after the visitor and at the "finished"
exit, making `ROBO_NOT_FOUND` unconditional. The suite separately caught four
wrong expectations of its own (relative-vs-absolute depth, `find_descendant`
expected to match its start, a cursor copy expected to share state, and dump
indentation off by one level) — each a body-of-the-test error, now pinned by
the literal value the model produces.

**Definition of done:**

- [x] Authoritative traversal API over Document, Element and Text
- [x] Relationships in both directions, with the miss contract
- [x] Absolute depth from the document node; rootmost-ancestor traversal
- [x] Child iteration that is O(n) (the cursor) and safe on empty parents
- [x] Deterministic document-order / pre-order enumeration, allocation-free
      and non-recursive, with a cycle bound that reports rather than truncates
- [x] Element-only and text-only traversals that still step through every node
- [x] Bounded search: `find_first` (at or below) vs `find_descendant` (inside)
- [x] `find_element` and `subtree_text` refactored onto the walker; the dump
      refactored onto the walker; the demo renderer's child iteration moved off
      `child_at` onto the cursor (O(n) per child list instead of O(n^2))
- [x] Attributes and text stay reachable from any traversed handle
- [x] Parser integration, Unicode, a 1000-level tree, empty documents and
      defensive input covered
- [x] Ownership/lifetime safe: stale handles and abandoned cursors are inert
- [x] Read-only by construction; capability reports `traversal = 1`
- [x] Visual vertical slice unchanged on the host and in QEMU
- [x] Full suite green (1202/1202, Debug and Release)
- [x] No Chromium dependency; all independence scans green
- [x] Stop after 8.8

## Goal 8.9 — DOM mutation

The tree was a read-only structure with an owner and a good navigation
surface. Now it is editable, and the whole phase is organised around the fact
that there is still exactly **one place a child link is written**:
`robo_html_node_link_child` (the parser's append, construction-gated) is now a
wrapper over `robo_html_node_insert_relative`, which is where the kind check,
the detached-child rule, the reference-child rule and the sibling rewiring
live. A parse and a DOM edit cannot disagree about what a tree is.

**Five operations, one invariant set.** `append_child`, `insert_before`,
`remove_child`, `replace_child` and `remove_children`, all on a finalized
document — which is the state a parsed page is in, and the reason the phase is
useful at all. A node has at most one parent (inserting an attached node is
`ROBO_ALREADY_EXISTS`, never a silent steal), the result is always acyclic (a
node beneath its own descendant is refused), the document node can never
become a child, and only a document/element/fragment can be a parent.

**Detachment is not destruction.** A removed node keeps its subtree,
attributes and text, stays owned by its document, and can be re-inserted; in
the meantime it is a tree in its own right — `subtree_text`, `contains`,
`depth` (restarting at 0) and walks all work on it. There is no
node-destroy API because there is no state that needs one.

**A refusal is provably inert.** Every rejection is decided before the first
write, so a failed mutation leaves the tree byte-for-byte as it was — including
the document's tree version, which makes "this call changed nothing" something
a test asserts rather than assumes.

**Structural change is observable, so traversal can be honest about it.**
Every insert, detach, replace and clear bumps a monotonic per-document tree
version (0 stays reserved for "no document"). The 8.8 cursor and walkers now
snapshot it and stop with a MUTATION-domain `ROBO_ERROR` the moment it moves,
because the node they were about to hand out may have been detached — and a
detached node is not a child. Mutation of a *different* document cannot affect
an iteration, a refused mutation does not invalidate one, and a
mid-visitor destroy stays the distinct `ROBO_NOT_FOUND` it was.

**The whole-tree audit learned the difference** between a legally detached
subtree (counted once at its root as `detached`, its own links audited, its
descendants marked so they are not miscounted) and an orphan — a node that
claims a parent while nothing reachable lists it, which is exactly the shape a
buggy mutation leaves behind. Without the split, every document holding a
detached node would report an unclean audit, which is how an alarm becomes
noise.

The mutation hook from 8.1 finally has something to report:
`ROBO_HTML_MUTATION_TREE_MUTATED` fires once per structural change, which is
the seam a future style/layout invalidation listens to. It is not a
MutationObserver — no records, no targets, no subtree options, no microtask.

**Two defects the new suite found, both in the tests' own bookkeeping:**
cycle cases were being refused by the one-parent rule before the cycle rule
could decide, so the fixtures now detach the ancestor first and the refusal is
the rule under test rather than an accident (a wrong expectation that would
have passed for the wrong reason); and a fixture created its siblings under the
wrong parent, which the exact positional assertions caught immediately.

**Definition of done:**

- [x] `appendChild` / `insertBefore` / `removeChild` / `replaceChild` +
      bulk child removal, on a finalized document
- [x] One linking primitive shared with the parser; every structural rule in
      one place
- [x] One-parent invariant; cycle prevention; root immutability; parent-kind rule
- [x] First / middle / last / only child positional correctness, each verified
      by a specific assertion *and* a whole-tree audit
- [x] Detached nodes legal, traversable, re-insertable, still document-owned
- [x] Cross-document mutation refused (`ROBO_NOT_SUPPORTED`), stale handles miss
- [x] A refused mutation changes nothing, version included
- [x] Traversal invalidation reported with a MUTATION-domain diagnostic
- [x] Audit separates `detached` from `orphans`
- [x] `TREE_MUTATED` hook event; capability reports `mutation = 1`
- [x] Attributes and Unicode text preserved across moves
- [x] `html.mutate.*` (34 cases) + `demo.mutation.changes_picture`
- [x] Visual vertical slice renders before and after a mutation, unchanged
      when nothing is mutated
- [x] Full suite green (1237/1237, Debug and Release)
- [x] No Chromium dependency
- [x] Stop after 8.9

## Goal 8.10 — document lifecycle

The document had a lifecycle in name since 8.1, but one of its states was doing
two jobs: `READY` meant both "parsing finished" and "there is nothing left to
load". 8.10 splits that into the two facts it always was, and names the point a
parse finishes after the web platform's own vocabulary.

**INTERACTIVE is about the tree, COMPLETE is about the input.** Parsing being
done and input being done are different questions. `LOADING → INTERACTIVE` says
parsing finished and the DOM is live; `INTERACTIVE → COMPLETE` says no further
input can arrive. Keeping them apart is what makes the rest of the phase
possible — a rule about *input* can be wrong in ways a rule about the tree
cannot.

**A document cannot be finished loading before it has finished parsing.**
`CREATED → COMPLETE` and `LOADING → COMPLETE` are refused, not merely unused,
and `COMPLETE → INTERACTIVE` does not exist: the intermediate state is the only
way through, and completion is not a state you step back from. That is the one
edge difference that gives the new state its meaning, so it is tested as a
rule rather than left as an implementation detail.

**Completion describes input, not permission.** Reading, traversal, mutation, a
replacement source and reset all keep working after COMPLETE. A document that
stopped being editable at COMPLETE would be a worse state machine, not a
stricter one — and it would have broken every 8.9 guarantee on the most common
document in the runtime, the one that has finished loading.

**Which entry point settles where now follows from that one idea.** A one-shot
load consumes a complete source, so it settles at COMPLETE with no caller help.
A build **truncated at the node ceiling** settles at INTERACTIVE instead,
because the source was abandoned rather than consumed: claiming completion
would be saying "no further input can arrive" about input that was never read.
Detaching abandons input and settles at INTERACTIVE for the same reason. An
empty source has nothing to parse and settles at COMPLETE — through
INTERACTIVE, never by jumping.

**A document with no source stays CREATED.** A tree built through the element
API has no load to settle, and CREATED already permits content, mutation and
traversal; there is deliberately no `CREATED → INTERACTIVE` edge, because every
state above CREATED is a statement about a load. `hand_built_document_stays_created`
states that as a rule with a test rather than leaving it to be discovered as a
surprise.

**A transition is not a rebuild.** After every transition the suite runs the
whole-tree audit and asserts the root is the same node — a state machine that
quietly retired nodes while reporting a new state would pass every state
assertion and still be broken. And because not one node moves, a lifecycle
change **does not move the tree version**, so the 8.8 cursor and walkers keep
working through one instead of reporting an invalidation for a tree that did not
change. A *reload* is a structural change, and 8.9's rules apply to it
unchanged.

**Observability is id-based and closed by default.** `is_loading`,
`is_interactive`, `is_complete`, `is_settled` are each true for exactly their
state and false for an unknown or destroyed id, so a retired id answers "no"
rather than reporting a state it no longer has. Distinguishing "not that state"
from "not a document" is `get_lifecycle`, which is `ROBO_NOT_FOUND` — retired
ids are not probeable, so the query cannot become an oracle for recycled ids.
That is why there is no `is_destroyed(id)`: the honest version of that question
is asked of a lifecycle *value* you already hold (`is_destroyed(life)`), such as
the one a client lifecycle callback receives.

**One real regression, caught by an existing test.** `html.docmodel.metadata_built`
asserted `previous_lifecycle == LOADING` after a build. A one-shot load now
takes two steps, so `previous_lifecycle` is INTERACTIVE — and that is the better
assertion: the recorded previous state is now evidence that both steps
happened rather than one. Nothing was weakened, and the dump test that pinned
`Document\n` was updated to pin `Document lifecycle=complete\n`, which checks
more than it used to.

**Definition of done:**

- [x] Explicit lifecycle: `created`, `loading`, `interactive`, `complete`,
      `failed`, `destroying`, `destroyed`
- [x] Transition graph with no shortcut into completion (`LOADING → COMPLETE`
      and `CREATED → COMPLETE` refused, `COMPLETE → INTERACTIVE` absent)
- [x] Which entry point settles where: one-shot load, truncated build, detach,
      parser detach, empty source, reset, explicit marks
- [x] Completion never closes the DOM; content and source attach accept
      INTERACTIVE and COMPLETE
- [x] Id-based predicates (each exactly one state, closed by default) and
      value-based classifiers
- [x] A transition never changes the tree: audit clean, same root, same version
- [x] Destroy retires the id; stale handles report `ROBO_NOT_FOUND`; repeated
      destroy is `ROBO_NOT_FOUND`, never a double free
- [x] Reset settles an abandoning document at COMPLETE, a fresh one at CREATED
- [x] Tree dump reports the real state on the root line
- [x] `html.lifecycle.*` (27 cases) + `demo.lifecycle.advance_keeps_picture`
      (a transition renders an identical picture; a reload does not)
- [x] Unicode, attributes and the visual vertical slice preserved
- [x] Full suite green (1264 tests, Debug); the only failures are the two
      documented foreground-desktop `window.host.*` cases
- [x] No Chromium dependency
- [x] Stop after 8.10

## Goal 8.11 — basic HTML elements

An element has had a *name* since 8.5. A name is bytes and a type is a symbol,
and the difference showed in how many places answered "what is this element?".
The element model kept a 102-row table mapping a name to a coarse role. The
tree builder kept its own 18-name list of void elements. A consumer wanting
anything finer compared a string literal of its own. Three answers to one
question, and they drift. 8.11 replaces all of them with one table.

**A row is the whole answer: name, role, flags.** `RoboHTMLTag.h` names every
element this runtime recognises — the 32 basic elements this goal introduces,
plus the entire vocabulary the role table already knew — and gives each its
canonical spelling, its role and its intrinsic flags (void, root, document
structure, metadata, sectioning, region, heading, list, list item,
text-level). The role table *moved* into those rows rather than sitting beside
them, so `robo_html_element_classify` is now a delegation and a role and a type
cannot disagree about what an element is.

**Identity is derived, never stored.** `robo_html_element_tag` is a pure
function of `(namespace, name)`, exactly as the role already was: no payload
field, no arena growth, no cached tag to invalidate when a tree is re-parsed or
mutated, and a name that arrived before this phase resolves exactly as one that
arrives after it. The price is one bounded case-insensitive scan per
inspection — the same shape and the same cost the role lookup already had, and
it runs when an element is inspected rather than once per painted pixel.

**The table is index-addressed, and that is load-bearing.** The enumerators are
dense from 1 and the rows are declared in enum order, so `name`/`class`/`flags`
are `k_tags[tag - 1]` and `is_known` is a range test. `html.tag.table_is_dense`
asserts row order, ids, lowercase spellings, name uniqueness, a defined role
per row and the name round-trip — because if the table and the enum ever drift,
that test is the one that says so, and every other assertion in the phase is
then meaningless rather than misleading.

**The parser now reads two element facts from here, and only two.** Void-ness
and heading-ness are properties of the element *type*: `<br>` has no children
because `br` is void and the parser merely obeys, and "which elements are
headings" has one answer regardless of who asks. Eighteen lines of duplicated
truth were deleted rather than kept in step. Three parser tables deliberately
stayed where they were — the `<p>`-closing start-tag list, the head-content
list and the text-family table — because those are parsing *policy* with
contextual entries (`noscript` is head content in one mode and paragraph
content in another), and moving policy into an identity table would confuse
exactly the two things this phase keeps apart.

**Unknown is a state, not an error.** A custom element, a future element or
any non-HTML-namespace name is `UNKNOWN`, and it is a *fully working* element:
name, attributes, children, text, traversal and mutation all behave, asserted
rather than assumed. Three edges are pinned because each is easy to get wrong:
`is_tag(element, UNKNOWN)` is answerable and is not a shared identity; a
non-element handle (text, document, NULL, stale) satisfies no tag at all,
including UNKNOWN, because "no recognised type" is a statement about an
element; and an undefined enumerator is refused rather than made equivalent to
UNKNOWN, so a mistyped tag cannot silently match every custom element in a
document.

**The element-side question is asked with a symbol.** `is_tag(element,
ROBO_HTML_TAG_DIV)` replaces `is_named(element, "div")` where the question is
about *type*: the caller names a symbol, the runtime owns the spelling, and a
misspelling is a compile error instead of a test that quietly returns false.
`is_named` stays and still compares names, because that is a different question
— it is true of a custom element — and silently routing it through the tag
table would make it answer `false` for every name the vocabulary does not know.

**Two design limits, stated rather than discovered.** Element *creation* stays
gated on a finalized document (8.9's boundary), so the element suite exercises
creation on an unfinalized document and mutation by rearranging parsed nodes —
which is also what a consumer does to a loaded page. And the debug dump was not
reformatted: it already names every element by its tag, which *is* its type, so
the phase adds coverage that every supported type appears instead of a new line
format that would invalidate six suites to say the same thing.

The renderer is the one consumer touched. The demo adapter now asks for
`ROBO_HTML_TAG_BUTTON` and `ROBO_HTML_TAG_TITLE` by symbol rather than by
string, and a `<div>` that holds text and nothing else paints as a text block —
without that row the goal's own checkpoint document would silently drop two of
its four body elements. The condition is not a heuristic: a block's text in
the adapter is its whole subtree, so painting a div that *contains* blocks
would draw their text a second time; a div holding elements is therefore
transparent and the walk descends into it.

**Definition of done:**

- [x] Tag identity for every recognised element, read through a handle
- [x] One vocabulary table: canonical name, role and flags per row
- [x] Role table deleted, classify delegated — one definition of an element's role
- [x] Void and heading knowledge moved to the table; parser tables that are
      policy left alone
- [x] 10 predicates (void, root, document structure, metadata, sectioning,
      region, heading, list, list item, text-level), each pinned to its flag
      for every tag
- [x] Identity derived from (namespace, name); nothing added to a payload
- [x] Unknown/custom elements are supported, fully functional, never coerced
- [x] Element-side typed accessors (`element_tag`, `element_is_tag`), with the
      name-vs-type distinction documented and tested
- [x] `robo_html_dom_capabilities_t.element_tags = 1`, reported by the existing
      query and asserted, so a consumer discovers the model the way it
      discovers every other one
- [x] All 32 basic element types round-trip through the parser
- [x] Every type creatable through the element seam (unfinalized document)
- [x] Attributes, text nodes, whitespace (`<pre>`), and Unicode (Arabic,
      Persian, Urdu, Hindi, Bengali, CJK, emoji) preserved on the new elements
- [x] Traversal order, mutation and lifecycle integration on typed elements
- [x] 23 malformed inputs parsed without a crash and with a sound tree
- [x] Debug dump shows the real types, and reflects mutation
- [x] Visual checkpoint renders a typed multi-element document
      (`demo.paint.element_kinds`), pixel-probed and deterministic
- [x] `html.tag.*` (18) + `html.elements.*` (22) + `demo.paint.element_kinds`
- [x] Full suite green (1305 tests, Debug)
- [x] No Chromium dependency
- [x] Stop after 8.11

## Goal 8.12 — images

8.11 gave `img` identity and void behaviour, so the parser was already building
the element. What was missing is everything after that, because an `<img>` is
only interesting on account of the resource its `src` names. 8.12 adds that
resource — and the decoder underneath it, which the runtime did not have at
all.

**The decoder went into RoboGraphics, not RoboHTML.** A document may own an
`<img>`, but what an `<img>` holds is a `robo_gfx_image_t`; PNG is a container
format, not a document concept, and the same decoder has to serve a screenshot
path, a style sheet, a favicon and a test fixture. Putting it under RoboHTML
would make every future caller depend on the HTML subsystem to read a picture.
It is additive, so the v1 freeze holds: the module minor moves to 0.16.0 and
`ROBO_GRAPHICS_API_FROZEN` stays 1.

**It implements what real files actually use.** PNG at bit depth 8 across
colour types 0/2/3/4/6, DEFLATE in full — stored, fixed-Huffman *and*
dynamic-Huffman blocks — and all five row filters. The 6.13 encoder only ever
writes stored blocks, so a decoder that handled only those would round-trip our
own output perfectly and fail on every PNG from every real encoder. That is a
test that cannot fail for the wrong reason, which is the worst kind, so the
fixtures are generated by `python3 zlib` and hand-built blocks both. Adam7
interlacing and sub-byte bit depths are refused as `ROBO_NOT_SUPPORTED` rather
than approximated, because "this cannot be decoded here" is a better answer
than a silently wrong picture.

**Hostile metadata is treated as the normal case.** A file's dimensions are
checked against caller policy *and* against integer overflow before a single
byte is allocated, the expected raw scanline size is computed with explicit
overflow checks, and the DEFLATE stream must produce exactly that many bytes —
short or long are both `ROBO_ERROR`. A 16-byte file claiming 100000×100000
therefore fails in the header parse at zero allocation, which is the property
the security tests pin.

**Bytes arrive through a seam, never through RoboHTML.** 8.1 drew that line
and this phase keeps it: a caller installs a resolver, RoboHTML hands it the
reference and receives bytes *borrowed for the duration of the call*. That one
sentence is why the future network loader is an *implementation of a resolver*
rather than a change to this model — resource ownership never crosses the
boundary, so a resolver may serve bytes from static storage, a stack buffer, an
arena or an mmap with no lifetime contract. The one resolver this phase ships
is an in-memory asset table: it cannot touch the filesystem, so there is no
path to traverse and nothing to sandbox.

**`UNSUPPORTED` and `FAILED` are different answers.** A reference naming a
scheme (`http:`, `file:`, `data:`, `//host`) is refused as UNSUPPORTED — "this
runtime will not fetch that", which is true whether or not the resource exists.
A reference the resolver does not know is FAILED — "the bytes you asked for are
not here". Collapsing the two would make the network phase's work look like a
bug fix.

**Failure is state, not an exception.** A missing asset, undecodable bytes and
a refused scheme all leave the DOM exactly as it was and move the image's
*state*; `robo_html_image_create_decoded` returns `ROBO_OK` with a FAILED state
precisely so the object exists to be asked why. The states describe the
resource, not the element — `<img>` with no `src` is a resource in state NONE,
which is valid and not an error, and `robo_html_element_image_state` answers it
without needing a resolver, which is what makes it usable from a renderer
handed a document it did not load.

**Declared dimensions are parsed, not trusted.** The HTML attribute rule minus
percentages: optional ASCII whitespace, then digits, nothing else — no `px`, no
signs, no exponent, no overflow. `set` and `valid` are separate flags so
`<img width>`, `<img width="abc">` and `<img>` are three distinguishable
situations, and an over-limit value is reported rather than clamped so a typo
cannot silently resize anything.

**Attachments follow their own refcount, which is why they are not in the
payload arena.** The per-document table is freed as a unit on both `clear_tree`
and `destroy`, but it holds one reference: a caller that retained a resource
keeps a valid image past teardown. The payload arena is freed and re-bumped as
a unit, so a resource does not belong in it.

**The parser needed no change, and the consequence that is easiest to get wrong
is pinned anyway.** `img` is void, so the phase added nothing to the tokenizer
or the tree builder — but the suite still asserts that an `<img>` does not
consume what follows it: in `<div><img …><span>After image</span></div>` the
`span` is a *sibling*, not a child, and document order is
`div, img, span, Text("After image")`.

**Definition of done:**

- [x] PNG decoder: probe / decode_info / decode; colour types 0/2/3/4/6 at bit
      depth 8; DEFLATE stored + fixed + dynamic; all five row filters; IDAT
      across one or many chunks; CRC-32 verified
- [x] Safe refusals: interlaced and sub-byte depth → `ROBO_NOT_SUPPORTED`;
      JPEG/WebP/AVIF/GIF → `CONTAINER_UNKNOWN`; unknown critical chunk →
      `NOT_SUPPORTED`; malformed/truncated → `ROBO_ERROR`, never a partial image
- [x] Allocation bounded by declared limits (default 4096 px / 64 MiB) *and* by
      explicit overflow checks, before any pixel buffer exists
- [x] `RoboGraphicsDecode` added to the authoritative build; freeze pin updated
      to state that 0.16.0 is additive and the v1 surface is unchanged
- [x] `RoboHTMLImage.h`: attribute view with `set`/`valid` split, dimension
      parsing, the five resource states, `describe`
- [x] Fetch seam as a borrowed-bytes resolver, plus the in-memory asset table
      and the `UNSUPPORTED` vs `FAILED` scheme rule
- [x] Per-element attachment: resolve / get / detach / state, replacement on
      re-resolve, and document-level release on `clear_tree` and `destroy`
- [x] `<img>` through the parser: identity, four attributes, void behaviour,
      sibling not child after it
- [x] Unicode `alt` (Arabic, Persian, Urdu, Hindi, Bengali, CJK, emoji)
      preserved byte-for-byte
- [x] Traversal and mutation integration; append/insert/remove/replace an `img`
- [x] `robo_html_dom_capabilities_t` reports the feature, so a consumer
      discovers it the way it discovers every other one
- [x] Debug output (`robo_html_image_describe`) reflects real state
- [x] Renderer: `ROBO_DEMO_BLOCK_IMAGE` paints the real decoded bitmap at one
      image pixel per surface pixel; no fake rectangle
- [x] `graphics.decode.*` (13, so `test-graphics` runs them) + `html.image.*`
      (15) + `demo.image.reaches_framebuffer`
- [x] Full suite green (1334 tests, Debug; 2 documented foreground-desktop
      `window.host.*` cases excluded)
- [x] No Chromium dependency
- [x] Stop after 8.12

## Goal 8.13 — links

8.11 gave `a` identity, so the parser was already building anchors. What was
missing is everything after the element, because an `<a>` is only interesting
on account of what its `href` names. 8.13 adds the name, the resolution and the
activation — and, more importantly, the vocabulary that makes "nothing
happened" impossible to spell "success".

**There is no browser here, so there is no browser security model.**
RoboticelaOS is an application runtime: it has applications and windows, not
origins, cookie jars, tabs or a same-origin policy, and nothing in this model
would use any of them. A document belongs to an application, and navigation is a
request the application's own router takes or does not; what a browser expresses
through cookies and sessions, RoboticelaOS expresses through application
identity and the capability model it already has. So this phase invents no
security model of its own. What it adds is refusal, with a reason.

**The central statement is that a refused scheme never reaches a handler.**
`javascript:`, `data:` and `file:` are refused by policy *before* the seam is
consulted — so an attribute cannot be used to reach a subsystem the document was
never granted, and there is no privileged bypass to forget to check. Two
questions are deliberately separate because they answer different things:
`..._scheme_is_known` says whether the runtime understands a scheme at all,
`..._scheme_is_permitted` says whether it may be handed to a handler. `file` is
known and refused ("we understand what you asked for and we refuse it"); an
unknown scheme is neither.

**And permitted is not reachable.** With no handler installed, a route or an
`https` URI is `UNSUPPORTED`, which is the truth: the stack that would serve it
is a later phase. That is why the outcome vocabulary has **no `NAVIGATED` kind**
and will not have one until something can actually navigate. Every activation
passes through the same order — read and resolve, decide without consulting
anything (no usable `href` → `NO_TARGET`, unparseable → `REJECTED`, a `download`
intent → `DOWNLOAD_PENDING`), resolve a fragment here, refuse an unpermitted
scheme, and only then ask the handler — and `ROBO_OK` accompanies every outcome,
including the refusals, because they are answers rather than errors.

**A fragment resolves locally, because it needs no I/O.** `#Overview` names an
element in *this* document and finding one is a tree walk, so it is answered
here as `FRAGMENT` or `FRAGMENT_MISSING` rather than delegated: the one
navigation this phase can complete, and it completes it honestly. The lookup is
named for its caller and is explicitly **not** `getElementById` — that API
belongs to the later DOM-query phase, with its own caching question and its own
error surface, and shipping a stub of it here would have to be replaced.

**The outcome owns the target it reports.** That is the only shape that works:
activation resolves the reference itself, so a borrowed target would die at the
end of the call and every reported string would dangle. Instead the target is
handed to the outcome, the convenience pointers borrow from it, and one clear
rule — `robo_html_navigation_outcome_clear` — releases it. An outcome that was
never filled is safe to clear, so "clear what you were given" is valid even on a
failure path.

**Resolution is small, total, and the only one.** One function turns a
reference into a target, against the document's own reference: route (root
relative, relative, `./`, `../` with `..` clamped at the root like a URL),
fragment, external URI, invalid, or none. Failure is a *kind*, not an error,
which is also why resolution is a pure function of its arguments and works with
no document at all — and why activation is the only part that has to care about
document lifetime. There is no second URL parser: the `href` bytes are kept
verbatim beside the resolution, so a caller can always see what the document
actually wrote. No percent-encoding, no path case folding, no IDNA — those
belong to a URL library, and half-implementing them would be a behavioural claim
this phase cannot back.

**`target`, `rel` and `download` are preserved and classified, not
implemented.** `_blank` is a distinct context and does not create anything —
RoboticelaOS has applications and windows, not a tab strip, and collapsing the
request to `CURRENT` would throw away what the document asked for. `rel` is
preserved and answerable as a whole token, never interpreted: `noopener`,
`nofollow` and `external` all change what a browser's network layer does and
mean nothing to a runtime with no network, so inventing consequences would be
inventing behaviour. `download` is an intent, and an activation that would need
to save something says `DOWNLOAD_PENDING` instead of pretending.

**The checkpoint shows a link that completes and a link that refuses.** The
built-in demo document gained two anchors and the `id` one of them targets: the
route link reports `unsupported` with its resolution, and the fragment link
reports `fragment` with the element it found — because a checkpoint that only
showed link-coloured text would prove nothing about navigation. The adapter
paints a link in the brand colour with a hairline rule and hit-tests it, which
is temporary adapter behaviour rather than a style engine: styling a link is
CSS's job in a later phase, and no other part of the runtime may infer a visual
contract from it. Two limits are asserted rather than left to be discovered —
a link nested in a paragraph is painted as part of that run, because the adapter
has no inline flow; and a page built from markup owns the document it built,
because the HTML it was handed is gone and activation needs a tree.

**Definition of done:**

- [x] `RoboHTMLAnchor.h` / `.c`: attribute view (present vs usable), navigation
      target, resolution, scheme policy, fragment lookup, target context, `rel`
      tokens, request / handler / outcome, activation, debug describe
- [x] `a` through the parser: identity, attributes, nested content, sibling not
      child after it, malformed markup
- [x] Resolution matrix: root-relative, relative, `./`, `../` (clamped),
      fragment, route + fragment, absolute URIs, overflow, with and without a
      base reference
- [x] Scheme known vs permitted; `javascript:`, `data:` and `file:` never reach
      a handler; unknown schemes refused
- [x] Fragment activation really finds the `id` it names (`FRAGMENT`), and a
      missing one reports `FRAGMENT_MISSING` rather than nothing
- [x] Outcome vocabulary with no "navigated" kind; `NO_TARGET`, `REJECTED`,
      `UNSUPPORTED` and `DOWNLOAD_PENDING` all reachable and all tested
- [x] `robo_html_dom_capabilities_t` reports the feature
- [x] Mutation: append/insert/remove/replace an anchor, `href` set/removed/
      replaced, `target`/`rel`/`download` mutation, and the picture following it
- [x] Lifecycle: activation while created/loading/interactive/complete, and a
      destroyed document answering `NOT_FOUND` with no invented outcome
- [x] Unicode link text (Arabic, Persian, Urdu, emoji) preserved
- [x] Checkpoint: `ROBO_DEMO_BLOCK_LINK` paints the link distinguishably and
      hit-tests it; the host drives pointer → hit test → activation through the
      real event queue and offscreen, and asserts one fragment and one refusal
- [x] QEMU: the guest hit-tests and activates the same two links and reports
      `ROBO_QEMU_HTML_LINKS_HIT=2`, `_LINK_FRAGMENT=1`, `_LINK_UNSUPPORTED=1`,
      so the model is validated on the real display path and not only on the
      host — `test-qemu-display` passes
- [x] `html.anchor.*` (27) + `demo.link.*` (4); full suite green (1365 tests,
      Debug, no documented exclusions)
- [x] No Chromium dependency (verified by scan and by the build's own
      independence assertion)
- [x] Stop after 8.13

## Goal 8.14 — forms

8.11 gave `form`, `label`, `input`, `button`, `textarea`, `select` and `option`
tag identity, so the parser was already building them before this phase began —
`input` was already void and `<textarea>` was already an RCDATA text element.
What was missing is everything after the element, because a control is only
interesting on account of the *state* its attributes declare. 8.14 adds that
state: the attribute views, the type vocabularies, the relationships, and the
declared/current split that a form reset exists to express.

**A control does not have one value; it has two, and the split is the point.**
`<input value="a">` then `.value = "b"` leaves the attribute saying `a` and the
control saying `b`, because the first is the *default* value the author wrote and
the second is the *current* value the control is holding. A model with a single
field cannot express that, so this phase keeps the override in a per-document
table beside the element that owns it and always reports *which* of the two
answered: `robo_html_element_control_value` returns the bytes plus
`out_is_declared`, `set_control_value` pins a current value, and
`clear_control_value` drops the override so the value tracks the attribute again.
The consequence is worth stating rather than tidying away — for a control that
has not been overridden, rewriting the `value` attribute *does* change the
current value, and for one that has, it does not. That is not an inconsistency;
it is the entire content of the dirty-value flag, and the suites pin both halves.
The same shape applies to checkedness, and the two overrides are independent
because in HTML they are separate dirty flags. Checkedness is modelled for
`<input>` only and only when the effective type is checkable, so
`<input type="text" checked>` honestly has an attribute that is preserved and no
checkedness to report, rather than one misleading fact.

**Nothing is cached, so nothing can go stale.** 8.14.15 asks that a mutation
never leave a stale association, and the answer is that there is nothing to keep
in step: a form owner, a label's target, an option's position and a select's
selected option are all derived on demand from the live tree. Move a control
between forms and its owner changes; aim a `form` attribute at a missing form
and the answer comes from ancestry; retarget a label's `for` and the next call
describes the tree as it is. There is no mutation hook because there is no cache,
and the only stored state is the two explicit overrides, which belong to one node
by identity rather than to a relationship — which is exactly why they come along
when the node moves and die with the tree when it is reset or destroyed.

**`present` is not `usable`, and `NONE` is not `UNKNOWN`.** `<form action>` is
present and unusable, `<form action="">` is present and unusable, and `<form>`
is neither, so `action_usable` is reported rather than inferred from a length. A
missing `method` is `NONE` and a `method="weird"` is `UNKNOWN`, because both
would submit as GET in a browser and that defaulting is a *submission* rule this
phase deliberately does not apply. Reading the wrong element is a refusal rather
than an empty view — asking for a `<div>`'s form attributes is
`ROBO_NOT_SUPPORTED`, not "a form with nothing to do".

**An unmodelled vocabulary degrades without destroying anything.** An `<input
 type>` this phase does not model classifies as `UNKNOWN` and behaves as `TEXT`;
a `<button type>` does the same towards `SUBMIT`. The element keeps its
attributes, its name and its place in the tree, and the view reports declared,
effective and *recognised* separately, so a caller can tell "the author said
`text`" from "the author said `date` and we treat it as text". `TEXT` and
`SUBMIT` are the zero values of their enums on purpose, so the zero value and the
HTML default agree instead of needing a special case.

**Numbers are parsed, not compared.** `min`/`max`/`step` carry the same
present/valid split the image module's dimensions do, which is what lets a caller
tell `<input min>` from `<input min="abc">` from `<input>`; an invalid number is
*reported* rather than dropped, because a document that says `min="ten"` has said
something and hiding it would hide a real authoring error from the phase that
finally implements validation. Nothing is validated here: `<input min="10"
max="1">` is reported faithfully rather than flagged. `rows`/`cols` are
non-negative integers bounded by a stated maximum, invalid rather than clamped, so
a typo cannot silently resize a control. Boolean attributes are presence tests,
so `<input checked>`, `checked="checked"` and `checked=""` all report 1.

**Textarea, select and option follow the rules that make them mean something.** A
textarea has no `value` attribute — its declared value is its child text — so the
attributes view has no `value` field rather than inventing one. Options are
counted and ordered by a document-order walk of the subtree, so an option nested
in an `<optgroup>` counts and an index always agrees with the count. An option's
value falls back to its own text when there is no `value` attribute, and the
answer says which of the two it gave. The selected option is determined rather
than guessed: the first with `selected`, else the first that is not `disabled`,
else none — a select with no `selected` shows its first selectable option, and
picking a disabled one would be a state a reader could never reach.

**A label and a form owner resolve by a stated rule, not a maintained index.**
A label resolves through a usable `for` (when the element it names is labelable)
and then through the first labelable descendant, reporting which step answered;
a `for` that names something unlabelable is *not* retried as a descendant search,
because the author named something and the honest answer is that it does not
resolve. A control resolves to the form its `form` attribute names, else to the
nearest ancestor form, else to no owner at all — which is a valid state, not an
error. The source is reported because the two rules differ for a later phase: an
owner found by attribute is a deliberate author reference that survives a move,
and one found by ancestry is a positional fact a move changes.

**The checkpoint shows state, which is the one thing a picture cannot fake.** The
demo adapter gained `ROBO_DEMO_BLOCK_FIELD`: `label`, `input`, `textarea` and
`select` share one style because what a caller needs to tell apart is the state,
not the element. Every reported fact is read through the model at build time —
never re-derived from the markup — so the picture is a report of the DOM rather
than a second opinion about it. A caption draws no box while a field draws a
white bordered one; a control whose text is its placeholder, or which is
disabled, is drawn muted while a value is drawn in the text colour; a checked
box draws the brand mark; a select draws a chevron a text field does not. The
document carries a declared value (`value="8"`), two placeholders, a checked
checkbox, a checked radio, a password field with nothing to show and a select on
its second option, and the field heights, box size and row gap are chosen so the
whole checkpoint fits an 800-pixel display — `demo.layout.flow` pins the result,
because a demonstration that ran off the bottom of a real framebuffer would be a
checkpoint in name only.

**Definition of done:**

- [x] `RoboHTMLForm.h` / `.c`: the `<form>`, `<label>`, `<input>`, `<button>`,
      `<textarea>`, `<select>` and `<option>` attribute views; the input and
      button type vocabularies with declared/effective/recognised and their
      degradation; control state (current value and checkedness, read, pinned
      and cleared, with the declared/current split reported); option count,
      index, value fallback and the deterministic selected option; label
      target; form owner and owner source
- [x] The seven elements through the parser: identity, attributes, voidness,
      RCDATA textarea content, nesting, siblings, malformed markup
- [x] `time`, `number`, `checkbox`, `radio`, `password`, `hidden` and the rest of
      the modelled input types; `range`/`date`/`color`/`file` and unknown
      spellings degrade to `text` while keeping identity and attributes
- [x] Declared vs current: the attribute keeps what the author wrote, the
      override is returned as the current value, `out_is_declared` says which
      answered, a textarea's children are its declared value, and setting one
      override never disturbs the other
- [x] Checkedness only where it exists — checkbox and radio — and refused
      everywhere else
- [x] `min`/`max`/`step` and `rows`/`cols` parsed with the present/valid split,
      invalid values reported rather than clamped or dropped
- [x] `present` vs `usable` and `NONE` vs `UNKNOWN` for `action`, `method` and
      `enctype`; a non-form element refused rather than described as empty
- [x] Select/option ordering by document-order walk (including under
      `<optgroup>`), the option value fallback with `out_is_explicit`, and the
      `selected` → first-enabled → none rule
- [x] Label resolution: `for` by id when labelable, else the first labelable
      descendant, else nothing; a non-labelable target reported rather than
      retried; `out_from_attribute` says which step answered
- [x] Form ownership: attribute by id, else nearest ancestor, else none;
      attribute source vs ancestor source reported; non-form-associated elements
      refused
- [x] Mutation: no stale association under any of append / remove / move between
      forms / removed form / changed `for` / restructured options, with the state
      following the node by identity
- [x] Lifecycle: state readable and writable on a parsed (settled) document, no
      override surviving a reset or a destroy, and a destroyed document
      answering without inventing state
- [x] `robo_html_dom_capabilities_t` reports the forms model
- [x] Unicode values, option text and control text preserved verbatim (Arabic,
      Persian, Urdu, CJK, emoji)
- [x] Checkpoint: `ROBO_DEMO_BLOCK_FIELD` reports and paints the state; `html.form.*`
      (25) and `demo.field.*` (4) cover the model, the pixels, the mutation rule
      and the unknown-type degradation
- [x] QEMU: the guest reports the checkpoint's form state
      (`ROBO_QEMU_HTML_FIELDS`, `_FIELDS_STATE`, `_FIELDS_CHECKED`,
      `_FIELDS_DECLARED`, `_FIELDS_PLACEHOLDER`, `_FIELD_OPTIONS`) and
      `test-qemu-display` asserts the values, so the model is validated on the
      real display path and not only on the host
- [x] Full suite green (1394 tests, Debug, no documented exclusions)
- [x] No Chromium dependency (verified by scan and by the build's own
      independence assertion)
- [x] Stop after 8.14

## Goal 8.15 — DOM tests

This phase adds no product behaviour. Every surface it exercises was built by
8.6 through 8.14, and the running system is unchanged by it. What it adds is the
test layer those phases could not write, because a suite written *by* a phase is
positioned to see that phase's surface and not the seams between surfaces.

**Fourteen suites can each be green while the model as a system is wrong.**
`html.attr.*` can prove an attribute changes and `html.image.*` can prove an
image's state follows its `src`, and both can pass while the *first* one stops
moving the second. `html.anchor.*` can prove a fragment resolves and
`html.form.*` can prove a label resolves its `for`, and both can pass while the
two disagree about which element an id names. `html.tree.*` can prove the builder
never puts a child under `<input>` and `html.mutate.*` can prove the mutation API
inserts children into a node of any kind, and both are correct — the invariant
that is *actually* true is that void is a parser rule and not a DOM rule, and no
single-phase suite has a reason to state it. So this phase adds one more suite,
`SuiteRoboHTMLDom.c` (`html.dom.*`, 13 tests), and it is the only one allowed to
be cross-cutting. It is not a second framework: it uses the same
`robo_test_register` and the same `ROBO_CHECK*` vocabulary, and its shared
support header is `static inline` helpers rather than a runner, a reporter or a
new assertion language.

**The subject is invariants, not APIs.** Three tests carry most of that weight.
`document_isolation` parses the same bytes into two documents and then checks
every cross-document operation is refused *and* that the document nobody
addressed is byte-identical afterwards — which is what the ownership model
exists for, since identical markup means identical names and identical ids, and
anything resolved by name or id-string across documents would look right on one
and corrupt the other. `id_resolution_agrees` asserts the three consumers of the
single id rule agree, including the case where they agree on *nothing*: a `for`
naming a non-labelable element answers nothing rather than falling back to
ancestry, while a `form` attribute that does not resolve deliberately does fall
back — two different choices, both deliberate, and the suite pins the difference
rather than smoothing it away. `attribute_mutation_sequences` walks set → read →
replace → remove with the dependent state checked at every step, through the
anchor view, the image state and the declared/current control value, including
the case that justifies the declared/current split existing at all: an explicit
control value surviving the removal of the attribute it defaulted from.

The rest of the suite is the matrix around those: a malformed corpus that must
still audit clean and rebuild byte-identically, 200 levels and 300 siblings,
thirteen script families through both the parser and the construction seam and
through all three id consumers (with a one-byte near miss), 24 document
lifetimes with the debug allocator's live count returning exactly to baseline,
and one document that uses every subsystem at once, is mutated, and is torn
down. The capability flags are asserted as they are: `dom_nodes` is checked to
be **0**, because it names a *complete* node DOM surface and this build has one
model per aspect rather than the whole, and `events` is 0 because its phase has
not run. A pass does not get to round either of those up.

**The fixtures parse real HTML, with one exception that is the model's.**
`dom_document_of()` runs the real tokenizer and tree builder, and the lookup and
audit helpers use the runtime's own descendant walk and its own
`robo_html_document_validate_tree`, so a test cannot pass on hand-built nodes
while the parser integration it claims to test is broken. Construction is the
one case that cannot go through the parser — the construction seam is gated on a
document that has not been finalized, and a parsed document is finalized by
definition — so those tests build a document by hand and are still checked by
the same traversal and the same audit.

**The sanitizer run was the point of the exercise, and it found two real bugs
that predate this phase.** `-Sanitize` already existed in the host build and was
used rather than duplicated. UBSan reported a misaligned payload header on
*every element write*: the payload arena is a bump allocator over a byte cursor,
so a reservation following an odd-length name began at an odd offset, and a
header is four `uint32_t`s — undefined behaviour on x86-64, a fault on stricter
targets. Fixed by aligning the cursor to the header's own required alignment
before each reservation, with the padding unreachable through any offset/length
pair and the reservation still ending exactly at the new `payload_used` so the
in-place tail-growth path is unchanged. UBSan then found `href + begin` on a NULL
`href` — `NULL + 0` is UB even at a zero offset — fixed by keeping an absent
reference NULL. Neither was introduced here; both had been latent since the arena
and the anchor model were written and both are invisible without UBSan.

- [x] One cross-cutting suite (`SuiteRoboHTMLDom.c`, `html.dom.*`, 13 tests)
      reusing the existing framework — no second runner, reporter or assertion
      vocabulary
- [x] Shared fixtures (`RoboHTMLDomTestSupport.h`): parsed-document builders, the
      three lookups over the runtime's own descendant walk, the whole-tree audit,
      truncation-fatal text/dump readers, and one Unicode sample table shared by
      both Unicode tests
- [x] Document isolation: identical bytes in two documents, every cross-document
      operation refused, the unaddressed document verified unchanged
- [x] Elements, attributes, text nodes, traversal and mutation covered at the
      system level through parsed trees, with the whole-tree audit run after
      every mutating case
- [x] The three id consumers asserted to agree, to move together under an `id`
      rename, and to diverge where the model deliberately diverges — with no
      parallel id index introduced for the tests
- [x] Void asserted as a parser rule *and* as a node-kind rule, stated as two
      different facts rather than one conflated one
- [x] Parser: nested, empty, void, comment, malformed, incomplete and Unicode
      markup, and a recovery corpus that must audit clean and rebuild
      deterministically
- [x] Links, images and forms regression-tested through the shared model,
      including anchor view, image state, label→control, control→form and the
      declared/current value and checkedness sequences
- [x] Lifecycle churn: 24 lifetimes balanced against the debug allocator's live
      count, stale handles after destroy, and a second destroy as `NOT_FOUND`
- [x] Unicode: English, French, German, Arabic, Persian, Urdu, Hebrew,
      Devanagari, Bengali, Chinese, Japanese, Korean and emoji, preserved
      verbatim through parse, attributes, text nodes, mutation and all three id
      consumers
- [x] Attribute mutation sequences, deep (200) and wide (300) structure, and one
      document integrating every subsystem
- [x] `dom_nodes` and `events` asserted 0 — the flags report what this build
      does, not what the phase list intends
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer enabled, full
      suite green, **zero findings after the two fixes**
- [x] Two real latent defects fixed rather than suppressed: misaligned payload
      headers in the arena, and `NULL + 0` in anchor reference trimming
- [x] `html/docs/Tests.md` records the suite map, the cross-cutting tests, the
      fixture rule, the focused and full run commands, and the sanitizer result
- [x] `html/README.md` documents the `html.dom.*` filter and the docs list
- [x] Full suite green (1407 tests, Debug, no documented exclusions) — up 13
      from 8.14's 1394, with no existing test weakened or removed
- [x] No Chromium dependency (verified by scan and by the build's own
      independence assertion)
- [x] Stop after 8.15

## Goal 9.1 — CSS architecture

This phase builds the *model* the rest of Phase 9 will be written against, and
deliberately nothing that interprets it. No tokenizer, no parser, no selector
matching, no cascade, no computed style, no layout, no painting — those are
9.2 onward, and the whole point of doing 9.1 first is that each of them can then
land independently, behind an interface that already exists.

**The interesting decisions are the seams, not the structs.** RoboCSS is a
**peer of RoboHTML**, not a client: per `DEPENDENCIES.md` it works on any DOM
tree, so it must not include RoboHTML, and it must not know that layout,
painting, rendering, windowing, graphics or display exist. That is enforced, not
merely stated — `-Verify` now runs `Assert-RoboCSSChromiumIndependence` over
`Include/RoboCSS`, `Source/RoboCSS` and `css/`, rejecting Chromium/Skia/V8 *and*
the higher runtime layers, the same way the RoboHTML scan does. The DOM stays
authoritative for elements, attributes, tree relationships and text; RoboCSS
owner nothing from the tree and stores no second copy of it.

**Syntax is modelled as by-value records, and that has a real cost.** A token, a
value, a declaration, a selector and a rule are inert description with no
external identity, so they are plain records rather than opaque pointers. The
price is one precondition, stated once in `RoboCSSTypes.h` and depended on
everywhere: a record must be **cleared** before any call that may release what
it holds, because `make`/`init`/`append`/`dispose` release *first* and then set,
so reconfiguring never leaks — and therefore an *uninitialized* record is not
safe, since the release step reads fields still holding stack garbage. That is
not theoretical: a `robo_css_value_t` local that reached
`robo_css_value_make_list` uninitialized walked a `count` of stack garbage and
freed a pointer that was never an allocation. The fix was to name the contract
(`ROBO_CSS_CLEARED`, whose all-zero byte pattern is the cleared state *by
design* — every field's zero is its empty value) and then to *test* it rather
than comment it: `css.records.cleared_state` asserts the equivalence field by
field for eleven record types, then asserts each cleared record is a usable
starting point rather than an inert one.

**Two real defects were found and fixed rather than worked around.**
`robo_css_source_set_string` delegated to `set_memory` and so never produced
`KIND_STRING`, leaving a documented source kind unreachable — the one place a
caller asks for owned bytes it controls the lifetime of. And
`robo_css_stylesheet_parse` dereferenced its `out_error` unconditionally while
reporting the not-yet-implemented parse, even though `robo_css_error_set`
tolerates a NULL destination and the header promises the record is optional;
AddressSanitizer caught the access at `0x0`. Both were in code written this
phase, and both were fixed at the source, not at the test.

**The parser boundary refuses honestly.** `robo_css_stylesheet_parse` answers
`ROBO_NOT_SUPPORTED`, names Goal 9.3 in the message, leaves the rule list and the
stylesheet state untouched, and *records the reason on the stylesheet* so a
caller that did not ask for the error still sees it. There is no stub that
pretends to tokenize or parse. *(Goal 9.3 replaced this refusal with the real
parse; see the Goal 9.3 record below. This paragraph describes 9.1 as it
shipped.)* `robo_css_get_capabilities` reports which
subsystems exist, so a caller never has to infer capability from a version
number — and the suites assert the flags as they are rather than as the phase
list intends them.

**The plan's ten-directory layout is deliberately not reproduced.**
`source/ tokenizer/ parser/ syntax/ selector/ declaration/ stylesheet/ cascade/
computed/` would be ten directories holding one file each, against the
repository's own convention of one flat module directory with a `Robo*`-prefixed
translation unit per concern and an explicit, readable source list in the build.
The separation of concerns the plan asks for lives in sixteen public headers —
one per model, plus the umbrella `RoboCSS.h` — which is where a reader actually
looks for it.

- [x] Module boundaries for every later stage: source, token, value, property,
      declaration, selector, rule, stylesheet, parser cursor, computed style
- [x] Source model: borrowed string, borrowed memory and owned copy, byte
      offsets, 1-based line/column tracking, half-open ranges that slice the
      source, UTF-8 preservation, and malformed input representable safely
- [x] Full CSS Syntax token vocabulary as an enum, with classification, bracket
      pairing, hash types and range binding — but no tokenizer
- [x] Component-value model (`token`/`function`/`block`/`list`) with move-in
      children and recursive ownership — but no value parser
- [x] Declaration model: name, value, `!important`, source ranges, and a
      `NONE`/`PARSED`/`INVALID` state — but no property semantics
- [x] Style rules, at-rules and nested-rule storage; a stylesheet with origin,
      state, attached source, rule storage, rule traversal and module slots
- [x] Selector architecture: simple (type/class/id/attribute/pseudo/universal),
      compound, complex with combinators, and lists — but no matching and no
      specificity
- [x] Property boundary: a property table mapping name → stable per-table
      identity, plus id → parser/computer hook registration, so later phases
      register rather than hard-code a property database
- [x] Error model distinct from `robo_result_t`, with `recoverable` vs `fatal`
      and a non-fatal default for malformed source — nothing crashes on bad CSS
- [x] DOM integration boundary (an element view and a style context) and the
      inline-style boundary — the DOM keeps the `style` attribute, RoboCSS is
      handed its text — with no resolution implemented and no second attribute
      store
- [x] Default/user-agent stylesheet boundary and a stylesheet-origin model
      capable of distinguishing application, inline and runtime sources, with no
      built-in stylesheet and no cascade priority assigned
- [x] Parser pipeline interface defined end to end (source → tokenizer → parser →
      stylesheet) with a real, deterministic, compilable skeleton that refuses
      rather than fakes
- [x] Deterministic lifetimes throughout: static bounded module slots, monotonic
      never-reused stylesheet ids, borrowed slices never freed, and sanitizer
      runs where supported
- [x] Nine suites, 88 tests, reusing the existing framework — no second runner,
      reporter or assertion vocabulary
- [x] Tests: source (kinds, ownership, reconfiguration, argument rejection,
      labels, LF/CRLF/lone-CR positions, ranges, Unicode preservation), token,
      value, declaration, selector, rule, stylesheet, style context, robustness
- [x] Fixtures for later phases as source data only, plus one shared Unicode
      sample table so two tests cannot disagree about what "Arabic" means
- [x] Unicode source preserved verbatim through every model: Arabic, Persian,
      Urdu, Hebrew, Devanagari, Bengali, Chinese, Japanese, Korean and emoji
- [x] Robustness: empty, whitespace-only, large, incomplete, malformed and
      Unicode source, repeated creation/destruction, deep rule nesting, wide
      structures and deep value trees — memory-safe in every case
- [x] Build integration: production sources, test sources, module registration
      and a `css` component in the pipeline's runtime component table, so
      `scripts/robo test-css` is a real focused command — no competing build
- [x] Chromium-independence asserted by the build itself, extended to reject any
      higher runtime layer as well
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer enabled, full
      suite green, **zero findings after the two fixes**
- [x] Phases 6–8 regression-tested: full suite green (1495 tests, Debug, no
      documented exclusions) — up 88 from 8.15's 1407, with no existing test
      weakened or removed and no existing HTML/DOM behaviour changed
- [x] The visual vertical slice still works end to end on the host and in QEMU,
      and CSS is isolated from rendering — nothing in the rendering path
      consults it yet
- [x] `css/README.md`, `css/docs/Architecture.md` and `css/docs/Tests.md`
      document the module boundaries, ownership and lifetime rules, every model,
      the DOM/inline/default-stylesheet boundaries, the fixture rule and how to
      run the focused and full suites
- [x] No Chromium dependency introduced
- [x] Stop after 9.1

## Goal 9.2 — CSS tokenizer

9.1 shipped the record; 9.2 ships the machine that fills it. This is the CSS
Syntax state machine — whitespace, comments, CDO/CDC, identifiers, functions,
at-keywords, hashes, strings, urls, numbers, percentages, dimensions,
punctuation and EOF — written independently, with no Chromium code or tables.
Nothing downstream of it exists: this reads bytes, it does not interpret them.

**The tokenizer is a cursor the caller owns, and it allocates nothing.**
RoboHTML's tokenizer is created and destroyed because it retains a bounded
diagnostic list; RoboCSS's is a by-value record with `init`/`dispose`/`reset`,
exactly like `robo_css_parser_t` — the thing it will be embedded in. So `init`
cannot fail for an allocation reason, nothing leaks, and a tokenizer is as cheap
to put on the stack as to keep in a parser. That is not a micro-optimisation: it
is what makes "does this style sheet tokenize" answerable without a lifetime
ceremony, and it is why the whole subsystem's memory-safety surface is two
`memcmp`s and a bound check.

**The caller's bytes are never rewritten, and that is the decision that
distinguishes the rest.** The specification's input preprocessing maps CR, CRLF
and FF to LF and NUL to U+FFFD; this build does not, because a token's range is a
byte range into the caller's own buffer and rewriting would make that range a
lie. The same effect is reached by *classification* — CR/LF/FF are read as one
newline, a hex escape's trailing whitespace swallows a whole CRLF pair, and a
newline inside a string makes it a `BAD_STRING` rather than being replaced.
Exactly one case classification cannot substitute: NUL, which preprocessing
would have made an identifier character. Reporting the delimiter that is
actually there beats claiming a value the bytes do not contain, and the suite
pins that choice rather than leaving it to drift. Ill-formed UTF-8 is likewise
preserved and reported as an `INVALID` token one byte at a time — the same
"one ill-formed position" rule the source model uses — never repaired and never
silently folded into a neighbouring name.

**A token is raw; its value is computed.** The tokenizer fills every field of
the 9.1 record — numeric value, representation, unit, hash classification — but
leaves `text` as the raw bytes, so `text` and `range` can never disagree. The
resolved value (escapes applied, delimiters stripped) is reached on demand
through the `robo_css_token_value_*` accessors, which work from `text` alone. The
payoff is worth stating plainly: no token points into scratch storage, none is
invalidated by asking for the next one, and a token can be copied, stored and
compared for as long as its source lives.

**Four real defects were found and fixed at the source, not at the test.** The
largest was a rule split across two layers: the scanner swallowed the whitespace
that terminates a hexadecimal escape (`\41 b` is the ident `Ab`), but the *value*
accessor did not re-apply it, so the accessor returned `A b` for a token whose
representation said `Ab`. The same class of drift hit line continuations in
strings, where the accessor wrote both the backslash and the newline into the
resolved text. Both are now tested against each other so the two layers cannot
diverge again. Two more were a wrong comparison in a value predicate and a
framework ceiling — the expectation log is bounded, so a several-thousand-token
audit failed by overflowing the reporter instead of by reporting; that suite now
streams. And one finding was a *contract violation in the new suites
themselves*: records reaching `dispose`/`copy` without being `ROBO_CSS_CLEARED`
first, which surfaced as an invalid-free rather than a failed assertion — the
exact failure mode 9.1's cleared-record contract exists to prevent, and the
reason it gained a regression test in the tokenizer suite.

**Malformed CSS is a value, not a failure.** A malformed style sheet is a token
plus a recorded error, never a failed call and never a terminated caller. The
first problem wins and later ones do not overwrite it, matching the parser
cursor's rule. EOF is a real token with an empty range at the end offset;
asking again returns that same token without advancing or touching the source, so
a loop that terminates only on EOF terminates on a value.

- [x] `robo_css_tokenizer_t` as a real state machine over `robo_css_source_t`,
      emitting the 9.1 token record — no second token model
- [x] Cursor API: `init`/`dispose`/`reset`, `next`/`peek`/`skip`/`skip_trivia`,
      `at_end`/`offset`/`remaining`/`has_peek`/`tokens_produced`/`position`
- [x] One-token lookahead shared by `peek` and `skip_trivia`, so peeking twice
      reads the same token rather than tokenizing it twice — and trivia skipping
      is opt-out, because trivia is preserved by default
- [x] Whitespace runs, comments (empty, multiline, Unicode, unterminated), and
      CDO/CDC as their own tokens
- [x] Identifiers (hyphenated, underscore, `--custom-property`, non-ASCII,
      escaped), functions, at-keywords, hashes (`HASH_ID` vs
      `HASH_UNRESTRICTED`)
- [x] Strings (both quotes, escapes, hex escapes, line continuations, Unicode,
      empty, malformed) and urls (bare, quoted, escaped, malformed) as `URL` /
      `BAD_URL` with `url(` and `)` excluded from the value
- [x] Numbers, percentages and dimensions preserving both components (the
      numeric value and the unit identifier), with no unit semantics
- [x] Delimiters, punctuation and brackets, with matching-open/close pairing
- [x] CSS escapes: hex escapes with their terminator whitespace, escaped
      punctuation, escaped code points, and safe handling of invalid escapes
- [x] Accurate source ranges on every token — including that a whitespace run is
      one token, a comment's range includes its delimiters, a function's includes
      the `(`, and a url's includes `url(` and `)`
- [x] Unicode preserved verbatim through tokenization: Arabic, Persian, Urdu,
      Hebrew, Devanagari, Bengali, Chinese, Japanese, Korean and emoji
- [x] LF/CR/CRLF/form-feed newline handling with deterministic positions
- [x] Deterministic, non-fatal malformed-input behaviour with no crash, no
      out-of-bounds read and no source truncation
- [x] Five new suites, 63 tests: stream/lifetime, core classes plus the two ends
      of the byte spectrum, identifiers/functions/at-keywords/hashes/urls,
      strings/numbers/percentages/dimensions, and whole-snippet sequences
- [x] Tests assert token types, values and ranges — not merely that a function
      returned without crashing
- [x] Fixtures and helpers reused from the 9.1 support header; no second runner,
      reporter or assertion vocabulary
- [x] Build integration through the existing component table and source lists
- [x] Capability flag `tokenizer` flipped to 1 because the thing it names now
      works end to end; every other behaviour flag still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer enabled over the
      whole suite, **zero findings** after the fixes
- [x] Phases 6–9.1 regression-tested: full suite green (1558 tests, Debug, no
      documented exclusions) — up 63 from 9.1's 1495, with no existing test
      weakened or removed and no HTML/DOM behaviour changed
- [x] The visual vertical slice still works end to end on the host and in QEMU;
      tokenization is not consulted by the rendering path
- [x] `css/docs/Tokenizer.md` added, and `css/README.md`, `css/docs/Tests.md` and
      `css/docs/Architecture.md` updated for the tokenizer
- [x] No Chromium dependency introduced
- [x] Stop after 9.2

## Goal 9.3 — CSS parser

9.1 built the record, 9.2 built the machine that fills it, 9.3 builds the grammar
that drives it. This is the CSS Syntax *structure* rules — a stylesheet, a rule
list, an at-rule, a qualified rule, a declaration block, a declaration and
component values — written independently, with no Chromium code or tables.
`robo_css_stylesheet_parse` stopped answering `ROBO_NOT_SUPPORTED` and became a
real parse.

**The grammar lexes nothing.** Every byte it sees arrives as a 9.2 token, and
every token it keeps is that same record — same type, same range, same hash
classification, same dimension unit, same numeric payload. There is no path from
the parser to the source bytes except through a token. That is asserted rather
than asserted-to-be-true: `css.parse.integration.values_are_the_tokenizers_tokens`
re-lexes the same source independently, walks the parse to its leaves, and
requires every leaf to match a token at the same range with the same
classification — and separately checks that `10px`'s number came across as 10 and
its unit as `px`. A parser that scanned bytes itself, or trimmed a token's range,
fails there and nowhere else.

**Two doors, one grammar.** `robo_css_stylesheet_parse` (a sheet's source → its
rules) and `robo_css_declaration_list_parse_text` (a caller's bytes → a
declaration list) both call `grammar_consume_declaration_block`, so a `style`
attribute and the inside of a `{ … }` cannot drift apart — the 9.1 inline-style
boundary was filled without a second implementation.

**The call's outcome and the CSS's problems are different things.** The return
value is `ROBO_OK` whenever a model was built, *including* one built by
recovering, because CSS error recovery is the normal case and the rules that
survived are real rules. `out_error` is always filled with the *first* problem,
or a record of kind `NONE` when the input was clean; the same record is kept on
the sheet so a call site that did not want it still sees it. State is `PARSED`
for a recovered parse and `FAILED` only when no usable stylesheet could be
produced at all. And the parse is **transactional**: it builds into its own rule
list and only a successful parse replaces the sheet's rules, so parsing twice is
idempotent rather than additive.

**Nesting is bounded, and past the bound a construct is flattened rather than
abandoned.** `ROBO_CSS_MAX_NESTING` caps both recursions. The over-deep
construct's tokens are still consumed so the parse resumes in the right place,
the rule or at-rule is still *produced*, and only its contents are dropped. A
hostile sheet therefore costs bounded stack and — the property the test actually
pins — cannot cost the rules that follow it. `css.parse.recover.rule_nesting_bound`
builds a chain past the bound and asserts both that the chain is still there and
that the ordinary rule after it survived.

**One place the grammar speculates, and it says so.** A block's contents are
rules for some at-rules and declarations for others, and an at-rule this build has
never heard of could be either. So the block is read as a rule list first — a rule
carries more information — and when that yields nothing the same bytes are read
again as declarations, on a **tokenizer of its own over the same source**, so no
range has to be adjusted afterwards. The guess can be wrong in the direction of
complaining about legal input (`@font-face { font-family: X }`), so the grammar
saves its error state around the guess and restores it when the second reading
succeeds: the first reading's complaint described this build's guess rather than
the stylesheet.

- [x] `robo_css_grammar_parse_stylesheet` and `robo_css_grammar_parse_declarations`
      behind the two public entry points; one grammar, two doors
- [x] Rule lists, style rules, at-rules (statement and block), declaration blocks,
      declarations and component values — with no interpretation anywhere
- [x] A rule's prelude is component values, so `selectors` stays empty for 9.4;
      `p, .a > b { … }` is nine component values and no combinator
- [x] `prelude_values` and `block_closed` added to the 9.1 rule model, so a prelude
      has structure and an unclosed block is a fact rather than a missing `}`
- [x] `!important` recognised, recorded, and *removed from the value* — finding it
      is what removes it, so no value carries a stray flag
- [x] Last-declaration-without-`;`, repeated semicolons, whitespace around `:`,
      `;` and `{`, and interior whitespace preserved as component values
- [x] Custom properties keep their name and their raw value, including `{ a: b }`
      and a bare `,` — values that are not values anywhere else
- [x] Malformed declarations are **kept and marked** (`state = INVALID`,
      `is_usable() == 0`) rather than dropped, so the count reflects what the
      author wrote; a malformed rule is dropped with the reason recorded
- [x] Two recovery boundaries, both the specification's: skip to the `;`, the
      balanced `{ … }`, or the enclosing `}` — which is deliberately *not*
      consumed, because eating it would end the enclosing block one declaration
      early
- [x] Nested rule and nested at-rule inside a style block are recognised as CSS
      Nesting, consumed whole and skipped, so a nested `color: blue` never becomes
      its parent's declaration
- [x] Accurate byte ranges for rules, at-rules, preludes, blocks, declarations,
      names, values and component values — all inside the source, asserted for a
      fixture containing a custom property, a `calc()` and an attribute selector
- [x] Unicode preserved verbatim through the parse: preludes, string values,
      property names, custom properties, comments and at-rule preludes, over the
      thirteen-script corpus, with byte-offset arithmetic asserted separately
      from the bytes so a character-counting bug cannot hide behind correct text
- [x] Five new suites, 57 tests (`css.parse.*` is 59 with 9.1's two cursor cases)
- [x] Existing 9.1/9.2 suites updated only where they asserted the *old* refusal:
      the parse-boundary and declaration-boundary tests now assert the real parse,
      and the sheet/robust tests were corrected rather than weakened
- [x] Build integration through the existing source and component tables
- [x] Capability flags `parser` and `inline_styles` flipped to 1 because the things
      they name now work end to end; every other behaviour flag still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; `--filter=css` is 208/208 under the sanitized
      binary
- [x] Phases 6–9.2 regression-tested: full suite green (1615 tests, Debug, plain
      build 3 runs in a row) — up 57 from 9.2's 1558, with no existing test
      weakened or removed and no HTML/DOM behaviour changed
- [x] The visual vertical slice still works end to end on the host and in QEMU;
      the parser is not consulted by the rendering path
- [x] `css/docs/Parser.md` added, and `css/README.md`, `css/docs/Tests.md` and
      `css/docs/Architecture.md` updated for the parser
- [x] No Chromium dependency introduced
- [x] Stop after 9.3

### What 9.3 found

No product defect, and that is worth recording precisely. Three of the new
suites' expectations were wrong and the implementation was right each time: `var(--gutter, 20px)` has four children and not three (the comma and the space are
component values until something reads them as separators); `a[1]` is contiguous,
so `x: a[1] (2) [3` is six children with the blocks at 1, 3 and 5; and a block's
range begins at the `{`, one byte after the space that separates it from the
prelude. A fourth was a property of the corpus: four of the thirteen samples are
two words, and a property name is one identifier, so the name test uses each
sample's first word.

The one crash was a **build hazard, not a source defect**: `RoboCSSRule.h` had
grown `prelude_values` and `block_closed`, and the build reused objects compiled
against the old layout. A clean rebuild fixed it, and the 9.1 tests that had
called `strstr` on a now-`NULL` message were then updated to assert the real
parse instead of a refusal that no longer exists.

## Goal 9.4 — CSS selectors

9.1 built the selector *records*, 9.3 deliberately left every rule's `selectors`
empty and kept the prelude as component values so that "has this prelude been
understood yet" would be answerable by asking. 9.4 is the thing that answers it:
a selector grammar over those records, and a matcher that says whether one element
satisfies one selector. Written independently, with no Chromium code or tables.

**The grammar lexes nothing.** Every byte arrives as a 9.2 token and every name it
records is that token's **value** — the tokenizer's own escape rule, already
applied — interned once into the list's name arena. So `.foo\:bar` carries the six
bytes `foo:bar` and `#item\31 23` carries `item23`, and a matcher never has to know
that escapes exist, because both sides compare resolved bytes. There is no second
escape implementation, which is the point: 9.2 has one.

**All-or-nothing, as CSS says.** One bad selector in a list makes the whole list
invalid — the rule that makes `:is()` worth having later. `out` is initialised
first (the 9.3 grammar's rule) and `out_error` is written on success too, so a
caller that never checks a result code still cannot be handed a record describing
a previous call. Nothing fails the process, allocates unboundedly or recurses
unboundedly: `:not()` nesting is capped by `ROBO_CSS_MAX_SELECTOR_NESTING` (8) and
past the cap the selector is **rejected** rather than parsed more deeply.

**The matcher holds no DOM, no document, no cache.** It asks a *host* — the four
callbacks in `RoboCSSStyle.h` — about one element at a time, which is what keeps
RoboCSS a peer of RoboHTML rather than a client: nothing in `Source/RoboCSS/`
includes a RoboHTML header, and the DOM integration suite is what makes the real
tree visible one element at a time.

**Three outcomes, not two.** `matched` and `unsupported` are independent and both
can be set. A host that supplies only a view can answer type, universal, class, id
and attribute selectors perfectly well and cannot answer `>` or `+` at all — so
`h1 + p` against such a host is *undecided*, not false. The distinction is
load-bearing in both directions: an absent callback and an answer of `NONE` both
yield no element, and reading the first as the second would silently turn every
`+` and `>` selector into a non-match, while `:first-child` *is* "no previous
sibling". Every caller that would read the answer as a *fact* goes through
`match_relation_answer`, which refuses the answer when the relation was
unavailable.

**Right-most compound first, with backtracking.** A compound is evaluated against
one view; a complex selector starts at its right-most compound — the one the
candidate must be — and walks leftwards through the combinators. Backtracking is
not an optimisation: `a b c` must be able to reject an ancestor `b` and try a
higher one, and `.x .y .z` can match through either of two `.y`s. The recursion is
bounded by the number of **compounds in the selector**, never by the tree, so the
stack cost is a property of the stylesheet rather than of the document; and a host
whose parent links form a cycle stops after `MATCH_WALK_LIMIT` steps with
`unsupported = 1` rather than answering wrongly or hanging.

**Supported exactly, and no more.** Type, universal, class, id, the six attribute
comparators plus existence (with the `i`/`s` case flags), the four combinators, and
`:root`, `:first-child`, `:last-child`, `:only-child`, `:nth-child()`,
`:nth-last-child()`, `:not()`. Sibling counting is **element** counting, so a text
node or a comment between two elements neither hides a sibling nor counts as one.
Pseudo-elements are parsed and distinguished from pseudo-classes, including the
four legacy one-colon spellings. Everything outside that set is *reported* rather
than guessed at, and two boundaries are deliberate rather than gaps: `:hover` and
its state-dependent relatives parse and are `unsupported`, while a pseudo-element
matches no element **decidedly**, because an element is not its own `::before`.

### Definition of done

- [x] Selector grammar over the 9.2 tokenizer: type, universal, id, class,
      attribute (existence plus all six comparators, quoted and identifier values,
      `i`/`s` flags), all four combinators, compounds, complex selectors, comma
      lists, escapes resolved through the tokenizer (`\:`, hex escapes, leading
      digits), pseudo-classes and pseudo-elements with the legacy one-colon
      spellings
- [x] The seven structural pseudo-classes parse, and `:not()` takes a real
      selector list rather than a re-parsed string, with an explicit nesting bound
- [x] An+B parsed once (`odd`, `even`, integers, `n`, `-n`, `+n`, signed offsets,
      whitespace only where the microsyntax allows it) and matched against a
      1-based element index
- [x] `robo_css_style_rule_parse_selectors` closes 9.3's open question and is the
      **only** way a rule's `selectors` is filled — replaces on success, so calling
      it twice is idempotent, and leaves an empty list on failure
- [x] The element host seam (`robo_css_element_host_t`): one userdata, four
      callbacks, `view` the only required one
- [x] Four matcher entry points, one per structural level, each independently
      testable and each exercised directly
- [x] A relation the host cannot answer is *reported*, never guessed
- [x] `matched` / `unsupported` / `condition` / `selector_index` / `range`, with
      `condition` documented as borrowing from the list's arena
- [x] Malformed selectors fail safely with real diagnostics and real positions,
      and a selector list is all-or-nothing
- [x] Source ranges preserved on every selector record
- [x] Six new suites, 111 tests: `css.select.parse` 51, `css.select.match` 34
      (simple 13, combinators 14, structural 7), `css.select.levels` 14, and
      `css.dom` 12 against the real RoboHTML tree
- [x] DOM integration against the **real** DOM: the visual checkpoint's own
      document, text and comment nodes not counting as siblings, deep trees,
      rebuilding after mutation, determinism, and matching not mutating the DOM
- [x] The visual checkpoint is untouched and still renders: running the selectors
      over its document leaves the picture identical
- [x] Build integration through the existing source and component tables
- [x] Capability flag `selector_matching` flipped to 1 because the thing it names
      now works end to end; `cascade`, `inheritance` and `computed_values` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; the plain build is 1726/1726
- [x] Phases 6–9.3 regression-tested: full suite green (1726 tests) — up 111 from
      9.3's 1615, with no existing test weakened or removed and no HTML/DOM
      behaviour changed
- [x] `css/docs/Selectors.md` added, and `css/README.md`, `css/docs/Tests.md` and
      `css/docs/Architecture.md` updated for the selector milestone
- [x] No Chromium dependency introduced
- [x] No cascade, no specificity, no inheritance, no computed values, no layout,
      no painting, no CSSOM, no animations
- [x] Stop after 9.4

### What 9.4 found

**One product defect, caught by UBSan.** A long enough run of digits in
`:nth-child(...)` overflowed the An+B accumulator — `long` is 32-bit on Windows —
reported as `signed integer overflow`. The accumulation is now bounded at
`ANB_VALUE_MAX` inside `anb_digits` and the oversize form is *rejected* rather
than wrapped into a plausible wrong sequence, with the bound test written as a
division so the check itself cannot overflow.

**One gap in the matcher's public surface**, found by asking which functions
anything actually calls rather than by any failing test:
`robo_css_simple_selector_matches`, `robo_css_compound_selector_matches` and
`robo_css_complex_selector_matches` are declared as independently testable and
were reached by *nothing* — every existing matcher test went through the list
door. All three could have been broken, or could have disagreed with the list, and
the suite would have stayed green. `SuiteRoboCssMatchLevels.c` now drives each
door directly and then requires the levels to agree across a table of 24 selectors
× 7 elements, which is what would catch a change to the shared walk that moved two
doors in different directions.

**Six test expectations were wrong and the implementation was right.** Four were
wrong about the fixture: two hosts asserted `h1 + p` and `:nth-child(2)` over
elements added without a parent, where the host double derives siblings from the
parent link and two parentless elements are not siblings (the `css_host_seal`
rule, which exists so two roots are not treated as siblings); one passed `NULL`
for the matcher's required output; and one asserted that a cyclic host leaves a
`>` selector undecided, when a `>` chain recurses once per compound and so
terminates on its own — the *descendant* walk is the one that needs the step
limit, and both behaviors are now pinned side by side. Two were wrong about CSS
itself: `[data-x=1]` is not a valid attribute selector (an unquoted value must be
an identifier, and a bare number is not one), and `div + , span` cannot be parsed
as "a list with one broken member", because a selector list is all-or-nothing —
so the list door's skip-an-incoherent-member guard only ever sees hand-built
input, and the suite builds it that way while asserting in the same test that the
parser refuses the text that would have produced it.

## Goal 9.5 — CSS cascade

9.4 answered yes or no about one element and one selector. 9.5 is the first thing
in the subsystem that *applies* anything: it collects everything that matches an
element, in cascade order, and answers which declaration wins for a property.
Written independently, with no Chromium code or tables.

**The cascade has three keys and this phase owns two.** Origin and importance
(the *level*), and the document order of the declarations, are complete here.
Specificity is 9.6, and it is not approximated, not defaulted and not given a
placeholder field. So resolution has **three** outcomes rather than two *(9.6 later supplied specificity and **deleted the third verdict** — see the Goal 9.6 record; the text below describes 9.5 as it shipped)*:
`DECIDED`, `NONE`, and `UNDECIDED_SPECIFICITY` for a contest between candidates of
one level from two different selectors. Settling *that* by document order would be
right only by luck, and wrong silently — the same reason 9.4 reports a relation its
host cannot answer instead of reading it as a negative.

**The level table is written out rather than derived**, because the derivation
that reads as obviously right is not. Two rows are decisions: inline sits above
the origin it belongs to (a `style` attribute is author-level with a specificity
no selector reaches — modelled as a level, which is what makes inline work without
9.6), and the important half is *not* a plain reversal — it reverses the origins
while inline stays directly above author, so an inline `!important` beats an
author `!important` and still loses to a user one.

**Collection reports everything it could not use.** An undecided selector, an
at-rule block nothing evaluates, a rule whose prelude was never parsed as
selectors, a sheet that was destroyed while referenced: each is counted on the
result and each sets `unsupported`, because a cascade that dropped any of them
quietly would apply the wrong rule and look like it had answered. `unsupported == 0`
means the answer is complete — **not** that nothing matched.

**One forward pass is enough, and the verdict can go back.** The array is in
cascade order (ascending level, then document order), so a candidate is at the
current level or above it. A strictly stronger level wins and *re-enables* an
answer that an earlier same-level pair had made undecided — the case a resolver
that remembered its first verdict would get wrong — while same-level rivals from
different selectors make it undecided and keep it there however many same-selector
declarations follow. Two declarations of one selector are comparable because a
specificity is a property of the selector and there is only one of it.

### Definition of done

- [x] The cascade model (`RoboCSSCascade.h`): the eight levels, the candidate with
      its provenance and document position, the collected result with its
      counters, and the three verdicts
- [x] `robo_css_cascade_level_of` / `_holds` / `_compare`, the level names, and
      `ROBO_CSS_CASCADE_LEVEL_NONE` for `ORIGIN_NONE` and unknown origins
- [x] `robo_css_style_context_match_declarations` is real: every stylesheet origin
      in the context's order, every matched rule's usable declarations, and the
      element's own `style` attribute, all in cascade order — it was the 9.1
      boundary whose own header said 9.4 and 9.5 would arrive here
- [x] Cascade order maintained by counting sort over the eight levels, stable,
      because two declarations in one rule can sit at different levels and
      document order is the tie-break inside a level
- [x] The level table verified twice: every pair reaching exactly one level and
      every level being reachable, and the eight rows asserted by name so a
      "correct by construction" mistake cannot hide behind a loop
- [x] `robo_css_cascade_resolve_name` and `_resolve_property`, with the winner
      written out **only** for `DECIDED`, and `NONE` refused as a property id
      rather than matched
- [x] `robo_css_cascade_candidate_same_provenance` public, so a caller reading an
      undecided verdict can see which pairs were comparable
- [x] The inline boundary exercised from the host side: no attribute vs `style=""`,
      malformed inline CSS recovered and reported, a host that cannot answer marked
      as an incomplete answer rather than as "no inline declarations", and the
      declarations parsed into a list the cascade owns
- [x] Every "could not use this" path counted and tested: undecided selectors, no
      host at all, unparsed rules, at-rules with blocks vs without, destroyed
      sheets, origin mismatches, unusable declarations
- [x] `robo_css_slice_equals_ci`, the slice-to-slice ASCII fold both the attribute
      view and the cascade need — and it replaced a private duplicate in
      `RoboCSSStyle.c` rather than being added beside one
- [x] `robo_css_style_context_inline_userdata`, so a caller (the cascade) can
      actually call the callback the getter hands out
- [x] Four new suites, 32 tests: `css.cascade.levels` 5, `css.cascade.collect` 15,
      `css.cascade.resolve` 12, plus `css.style.match_declarations_empty_context`
      rewritten from the 9.1 refusal to the 9.5 answer
- [x] `css.dom.cascade` 4 against the **real** RoboHTML tree: the document's own
      `style` attribute read through the seam, a collection that needs real parent
      and sibling links to pick the right rules, importance outranking inline, and
      a byte-for-byte proof that collecting a cascade for all 29 checkpoint
      elements leaves the painted frame identical
- [x] Build integration through the existing source and component tables, and the
      `css/tests` include path the 9.4 DOM suite needed (see below)
- [x] Capability flag `cascade` flipped to 1 for exactly what works end to end;
      `inheritance`, `computed_values`, `default_styles` and `stylesheet_loading`
      still 0. Version 0.5.0, `ROBO_CSS_API_FROZEN` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; the plain build is 1762/1762
- [x] Phases 6–9.4 regression-tested: full suite green (1762 tests) — up 36 from
      9.4's 1726, with no existing test weakened or removed and no HTML/DOM
      behaviour changed
- [x] `css/docs/Cascade.md` added, and `css/README.md`, `css/docs/Tests.md`,
      `css/docs/Architecture.md`, `css/docs/Selectors.md` and `ARCHITECTURE.md`
      updated for the cascade milestone
- [x] No Chromium dependency introduced
- [x] No specificity, no inheritance, no computed values, no colours, no units, no
      box model, no at-rule evaluation, no shorthand expansion, no layout, no
      painting, no CSSOM, no animations
- [x] Stop after 9.5

### What 9.5 found

**One build-integration defect that a green 9.4 claim was hiding.**
`SuiteRoboCssDomSelect.c` lives in `Tests/Suites/` — it is the one suite that must
see RoboCSS and RoboHTML together, and the two are peers whose guards forbid each
other's headers, so it belongs in neither directory — but it includes
`css/tests`' support header and the host compile line never had that directory on
its include path. A suite inside its own test directory resolves the header
relatively; a suite outside one needs `-I`. The 9.4 record reported the whole
suite green, so the claim and the build disagreed; the fix is one line in
`Build.ps1`, and the lesson is that a suite which has never been *compiled* from
the documented command cannot be reported as passing.

**One expectation was wrong about CSS, and the table was right.**
`css.cascade.levels.inline_is_author_level` first asserted that inline loses to a
user rule. It does not: author normal beats user normal, and an inline `style`
attribute is an author declaration with a specificity no selector reaches. The
spelled-out assertions beside the loop exist for exactly this — a table that
returned *a* level for every input would have passed the wrong test.

**Two fixtures disagreed with their own rules.** Three resolution tests asserted
that `div` and `.app` cannot be separated without specificity while the host was
built with the `app` *id*, so `.app` matched nothing, one candidate arrived and the
cascade answered `DECIDED` where the test expected `UNDECIDED_SPECIFICITY`. The
implementation was right both times; the fixtures were not. This is the same
lesson 9.4 learned about its host double, and it is why every cascade test now
adds its elements before the sheet that selects them.

**One thing worth writing down about the sort.** `cascade_order_by_level` counts
and then places over the *same* bucket function, deliberately: a candidate counted
into one bucket and placed from another is the one failure a counting sort has,
and it is invisible — the array would simply be shorter than the collection's own
count, with no error anywhere. The bucket helper is the contract between the two
passes, not an implementation detail.

## Goal 9.6 — CSS specificity

9.5 built an ordering that could place two declarations from one selector but had
to *report* a contest between two declarations from different selectors. 9.6
supplies the missing key, so the cascade is now total: every question it can see,
it answers. Written independently, with no Chromium code or tables.

**Specificity is not a fourth record beside the selector model.** It is three
counts *about* a selector, and the one thing it must never be is a second
description of one — so it is computed by walking the same simple/compound/complex
records the matcher walks, and there is no field on a selector record that could
disagree with a calculation. Nothing is stored, so nothing can go stale and no
cache can be wrong.

**The representation is a tuple, not a packed integer.** `(A, B, C)` — ids, then
classes/attributes/pseudo-classes, then types/pseudo-elements — compared
lexicographically by one exported function. A packed integer has the same
comparison outcome only until a component overflows into its neighbour, at which
point a more specific selector compares as less specific; that ambiguity is not
representable in a tuple. Each count clamps at `ROBO_CSS_SPECIFICITY_COMPONENT_MAX`, so
saturation is a stated behaviour rather than a wrap.

**Three rules that are easy to get subtly wrong, and each has a test that would
catch the wrong reading.** The universal selector is zero, so `*.card` is `(0,1,0)`
and not `(0,1,1)`. `:not()` is worth **its argument's** specificity, not one class
component — because `:not()` takes the *most specific* selector in its argument
list, `:not(.a, #x)` is `(1,0,0)`. And a pseudo-element counts toward the type
component while a pseudo-class counts toward the class component, which is exactly
the distinction the matcher already makes to decide whether an element *is* the
pseudo-element or merely satisfies the pseudo-class.

**A rule's selector list is worth the specificity of the member that matched**,
not its first member and not its maximum. `div, .card, #main` is `(0,0,1)` to a
bare `div` and `(1,0,0)` to the element the `#main` member matched; a cascade that
took the list's maximum would let an unrelated member promote a rule over a more
specific one. The specificity is carried on every *candidate*, so it is the
specificity of the declaration's actual reason for being here.

**9.5's third verdict was deleted rather than kept.** `UNDECIDED_SPECIFICITY`
described a question specificity answers, and keeping it would leave a documented
outcome that can never occur — the same failure mode the capability flags exist to
prevent. `ROBO_CSS_CASCADE_VERDICT_COUNT` is 2 and the enumerator's name string is
gone with it. This is the one change in the phase that could break a caller, which
is why the API is not frozen.

### Definition of done

- [x] The specificity model (`RoboCSSSpecificity.h`): the three-count record, the
      component constants, `robo_css_specificity_compare` / `_equal` / `_is_zero`,
      the clamping add, the four computation doors, and
      `robo_css_specificity_of_list_item` — the per-member door the cascade uses
      with the index the matcher reported
- [x] `RoboCSSSpecificity.c` computes the counts by walking the 9.4 records at all
      four levels (simple, compound, complex, list) — one implementation, and the
      `levels_agree` test requires the levels to reach the same answer for a wide
      selector table so they cannot drift
- [x] `:not()` worth its argument: the argument's *most specific* selector,
      recursed for nested `:not()`, with `not_chain_depth` pinning the recursion to
      the parser's own nesting bound rather than a second one invented here
- [x] Saturation at `ROBO_CSS_SPECIFICITY_COMPONENT_MAX` with a stress selector built from a
      generated `.a.a.a…` chain and a many-id chain, so a wrap could not pass
- [x] Malformed input never reaches a calculation: the parser refuses the text, the
      suite asserts the refusal, and a hand-built incoherent record is refused by
      the calculation door rather than read
- [x] Cascade integration: `RoboCSSCascade.h` carries a `specificity` on every
      candidate, the resolver consults `robo_css_specificity_compare` and holds no
      arithmetic of its own, and `ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY`
      is removed
- [x] Ordering unchanged where it should be: level before specificity, specificity
      before document order, `!important` at its own stage, inline still above
      author — each re-asserted with the new key in place
- [x] Two new suites, 30 tests: `css.specificity.*` 20 (the matrix, the simple
      kinds, the attribute operators, pseudo-classes, pseudo-elements, `:not()`,
      compounds and complexes, combinators-are-zero, lists, escaping, saturation,
      stress, level agreement, malformed, the API contract) and
      `css.specificity.cascade.*` 10 (decides, order-independence, source order
      after specificity, level outranks specificity, importance, the
      matching-member rule, `:not()` deciding, a pseudo-element winning nothing,
      inline, determinism)
- [x] `css.dom.specificity.picks_the_winner` against the **real** RoboHTML tree: an
      `id`/`class`/`type` contest over the visual checkpoint's own document
- [x] The 9.5 tests that asserted the placeholder verdict rewritten to assert the
      *decision* and, in the same test, that the decision is about specificity
      rather than order — the rival is written later in both, so a resolver that
      had fallen back to document order would pick the same winner by luck
- [x] Capability flag `specificity` flipped to 1, and `cascade` re-documented as
      complete now that its third key exists; `inheritance`, `computed_values`,
      `default_styles` and `stylesheet_loading` still 0. Version 0.6.0,
      `ROBO_CSS_API_FROZEN` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; the plain build is 1793/1793 and `css.` is 386/386
- [x] Phases 6–9.5 regression-tested: full suite green (1793 tests) — up 31 from
      9.5's 1762, with no existing test weakened or removed and no HTML/DOM or
      selector behaviour changed
- [x] `css/docs/Specificity.md` added, and `css/README.md`, `css/docs/Tests.md`,
      `css/docs/Cascade.md`, `css/docs/Architecture.md`, `css/docs/Selectors.md`,
      `css/docs/Parser.md` and `ARCHITECTURE.md` updated for the milestone
- [x] No Chromium dependency introduced
- [x] No inheritance, no computed values, no colours, no units, no box model, no
      at-rule evaluation, no shorthand expansion, no layout, no painting, no
      CSSOM, no animations
- [x] Stop after 9.6

### What 9.6 found

**No product defect and no sanitizer finding.** What the phase found was one
public-API change that had to be made rather than tested, and one expectation
written before the implementation that CSS disagrees with.

**The third verdict had to be deleted, and that is the riskiest thing here.** 9.5
shipped `ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY` and its name, and the 9.5
tests asserted both. 9.6 makes the cascade total, so the verdict describes nothing
and is gone: `ROBO_CSS_CASCADE_VERDICT_COUNT` is 2, a value that used to name the
third verdict is no longer known, and its name string is gone with it. The
alternative — keep the dead verdict so nothing has to change — would leave a
documented outcome that can never occur.

**`:not(:not(.a)):not(#x)` is `(1,1,0)`, not `(1,0,0)`.** The matrix was written
before the code, and this row was copied from the simpler `:not(:not(#x))` =
`(1,0,0)` without thinking the two `:not()`s through separately: the outer one is
worth *its argument's whole specificity*, so `:not(:not(.a))` is `(0,1,0)` and the
`:not(#x)` beside it adds the `(1,0,0)`. The implementation was right and the
expectation was wrong. The neighbouring rows (`:not(#x)` and `:not(:not(#x))`, both
`(1,0,0)`) exist so that nesting is pinned as "the argument's specificity, however
deep" rather than by the one case that would pass under either reading.

**Two tests had to say something different, not something weaker.**
`css.cascade.resolve.same_level_rivals_are_*` and
`css.cascade.collect.two_sheets_in_one_origin` both asserted the 9.5 undecided
answer. They now assert the *decision* and, in the same test, that the decision is
about specificity rather than order — the rival selector is written *later* in both
cases, so a resolver that had simply fallen back to document order would have
picked the same winner and the test would have proved nothing. The collect case
goes further and asserts `robo_css_specificity_equal` on the two candidates before
asserting the winner, so "this was decided by order" is a fact the test establishes
rather than an assumption it leans on.

## Goal 9.7 — CSS inheritance

**Status: complete.** The property registry, the resolved-style model, the
resolution, the CSS-wide keywords and the ancestor walk are implemented, tested
against the real RoboHTML DOM, and verified under sanitizers.

### Delivered

- [x] `Include/RoboCSS/RoboCSSPropertyRegistry.h` + `Source/RoboCSS/RoboCSSPropertyRegistry.c`
      — the authoritative metadata: does a property inherit by default, and what is
      its initial value. Initial values are stored as text **and** as the parsed
      component-value tree, built once at registration through the same grammar a
      declaration goes through; a text that does not parse is refused at
      registration. The standard set is the sixteen inherited properties these
      phases know (colour, the font and text properties) and the common
      non-inherited ones. Deliberately *not* a second property table: no ids, no
      per-caller state.
- [x] `robo_css_property_registry_apply_to_table` — the one supported way to build
      a table, setting `ROBO_CSS_PROPERTY_FLAG_INHERITED` from the classification,
      so the flag and the classification cannot drift
- [x] `Include/RoboCSS/RoboCSSInherit.h` + `Source/RoboCSS/RoboCSSInherit.c` — the
      resolved style: one owned entry per property the element has an answer for,
      each carrying its provenance (`DECLARED`/`PARENT`/`DEFAULT`/`DEFERRED`) and
      the CSS-wide keyword that produced it, plus the counters that stop the result
      being read as more complete than it is
- [x] `robo_css_inherit_resolve` — the primitive, and the only door that does any
      work. It takes the parent as an **argument** and walks three sets in order:
      the element's own cascade winners (through the cascade's own resolver), the
      inherited-by-default properties the parent carries, then the registry's
      initial values for the rest. A resolution is therefore a pure function of its
      inputs, with nothing cached and nothing global to invalidate
- [x] The CSS-wide keywords obeyed on the parsed value and never on a string:
      `inherit`, `initial`, `unset`; `revert` recognised, carried as a *deferred*
      entry and **reported** rather than half-obeyed, because obeying it means
      re-running a cascade with one origin removed
- [x] Custom properties: inherit always, compared byte-exact, and carrying no
      initial value (an unset one is invalid, not `initial`)
- [x] `robo_css_style_context_resolve_inherited_style` — the chain door: walk up
      through the host's `parent`, resolve **top down** so each level's parent is
      the *immediate resolved* parent, bounded at `ROBO_CSS_INHERIT_MAX_DEPTH` and
      reported when the bound is hit (a cycle is caught by the same bound)
- [x] `robo_css_style_context_set_property_registry` / `_property_registry` and the
      registry-aware classification path, so the table and the registry are both
      borrowed from the caller
- [x] `robo_css_value_copy` — a deep, transactional copy, the primitive inheritance
      needed and no part of the subsystem had
- [x] Three new suites, 37 tests: `css.registry.*` 11 (lifecycle, the standard set,
      classification, initial values, custom-property naming, lookup, registration,
      definition stability, apply-to-table, the name tables, and a leak check),
      `css.inherit.*` 6 (the keyword table and its negatives, the name tables, the
      resolved style's lifecycle, the deep value copy and its balance) and
      `css.inherit.resolve.*` + `css.inherit.chain.*` 20 (declared, inherited,
      non-inherited, child and intermediate overrides, root behaviour, `inherit`,
      `initial`, `unset`, `revert`, custom properties, an unknown property, inline,
      an incomplete cascade, counters and determinism, a leak check, and the walk)
- [x] Capability flag `inheritance` flipped to 1; `computed_values`,
      `default_styles` and `stylesheet_loading` still 0. Version 0.7.0,
      `ROBO_CSS_API_FROZEN` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; the plain build is 1832/1832 and `css.` is 423/423
- [x] QEMU display validation passes; `scripts/robo check` passes with the runtime
      sources, headers and build wiring **CLEAN** for Chromium independence
- [x] Phases 6–9.6 regression-tested: full suite green — up 37 CSS tests from 9.6,
      with no existing test weakened or removed and no HTML/DOM, selector, cascade
      or specificity behaviour changed
- [x] `css/docs/Inheritance.md` added, and `css/README.md`, `css/docs/Tests.md`,
      `css/docs/Architecture.md` and `ARCHITECTURE.md` updated for the milestone
- [x] No Chromium dependency introduced
- [x] No computed values, no colours, no units, no `calc()`, no `var()
      substitution, no box model, no layout, no painting, no CSSOM, no animations
- [x] Stop after 9.7

### What 9.7 found

**One real product defect, caught by the sanitizer run and not by the plain one.**
`robo_css_property_registry_create_standard` wrote `*out_registry` without checking
it was non-NULL, so `create_standard(NULL, …)` — the argument contract every other
door honours — crashed instead of returning `ROBO_INVALID_ARGUMENT`. The plain
build's combined run died with a segfault whose position could not be located,
because the harness does not flush stdout on exit; the sanitized build pinned it to
the line and the call.

**The fixed slot table is why a filtered run and a combined run disagree.**
`ROBO_CSS_MAX_STYLESHEETS` is 64, and the suites created a sheet per test and never
released it, so a combined `css.` run exhausted the table partway through and every
later sheet creation failed — making unrelated tests fail for a reason with nothing
to do with them. The fix is in the fixture, not the constant: the cascade fixture
now owns the sheets it makes and releases them in its `end`, so the bound stays a
real product bound.

**Three count expectations were off by one against the contract the header
states.** `unset` on an inherited property lands inside the inherited family and so
adds no entry (it is `inherit` on `width`, a property outside the family, that adds
one); a declared inherited property leaves one fewer default; and a child that
declares one family property still inherits the other fifteen. The implementation
matched the header in all three; the expectations did not.

**Two smaller fixture mistakes.** The value-copy tests built tokens with a NULL
source, which the constructor refuses (a token with no source is not a token). And
the without-a-host case cleared the whole element host — taking matching away too —
then asserted the sheet still matched; it now installs a host with the view but no
`parent` callback, which is the case the header describes.

## Goal 9.8 — CSS computed values

**Status: complete.** The typed value model, the property registry's
`computation` column, the normaliser, the computed-style container and the chain
door are implemented, tested against the real RoboHTML DOM, and verified under
sanitizers.

### Delivered

- [x] `Include/RoboCSS/RoboCSSComputedValue.h` + `Source/RoboCSS/RoboCSSComputedValue.c`
      — the typed value, and it is a **closed set of shapes** rather than "a
      string, but typed": `KEYWORD`, `NUMBER`, `PERCENTAGE`, `LENGTH`, `COLOR`,
      `FAMILY_LIST`, `PRESERVED`, `INVALID`. Three of those carry decisions:
      `PRESERVED` is a real computed value (`calc(100% - 20px)` *is* the
      expression, and reducing it would be a lie because `%` needs a containing
      block), `INVALID` is an **answer** the call reports successfully with a
      reason rather than an error return that forces a caller to drop the
      property, and `unresolved` is a flag no kind can express (`var()`, `calc()`
      and `currentColor` are references, not values)
- [x] The unit model: the seven units with a fixed factor (`px cm mm q in pt pc`)
      convert **exactly** (so `1in` and `96px` compare equal), the eight relative
      units are kept as written (converting `2em` here would produce a number that
      looks resolved and is wrong wherever the font size differs), and a bare `0`
      is a length because CSS's one exception says so
- [x] The colour forms this build can already express: the **standard named set**
      in full, `transparent`, all four hex lengths (nibble doubling included;
      every other length refused rather than truncated), and `rgb()`/`rgba()` in
      **both** syntaxes with numeric or percentage channels and alpha. Channels
      clamp rather than wrap; the two separator styles do not mix
- [x] `robo_css_computed_value_compute` — the normaliser, and a function of **the
      value's shape and the value**, never of a property name. That is what keeps
      it free of context (no containing block, no font metrics, no parent) and
      free of the caches a context-dependent stage would need
- [x] `computation`, a new column on the property registry's definition:
      `PRESERVE`, `COLOR`, `LENGTH`, `KEYWORD`, `FONT_FAMILY`, `FONT_WEIGHT`,
      `LINE_HEIGHT`. Seven shapes serve every property; a length-shaped property
      accepts a closed keyword set (the union of the lists of the properties that
      share the shape), so `width: red` is INVALID rather than a keyword no later
      phase can use
- [x] The computed style — the 9.1 container **made richer rather than changed**:
      each entry still holds the specified value a 9.1 caller put there, and now
      also its name, its typed value and a provenance bitset (`DECLARED`,
      `INHERITED`, `DEFAULT`, `DEFERRED`, `CUSTOM`, `UNRESOLVED`, `INVALID`),
      plus `_incomplete_count` and `_is_truncated` so a style cannot be read as
      more settled than it is
- [x] `robo_css_style_context_compute_style` — the chain door: resolve **and
      compute** each level top-down, so a child inherits its parent's *computed*
      value rather than a recomputation of what the parent specified. The
      intermediate styles are the walk's own and are disposed as it descends
- [x] CSS-wide keywords are consumed by 9.7 on the parsed value, so computation
      never sees one; `revert` stays deferred and reported. Custom properties
      remain opaque token sequences, never eagerly interpreted as colours
- [x] Numeric safety: NaN and the infinities are refused rather than clamped or
      dropped (there is no CSS spelling for either), channels clamp, and a
      malformed colour cannot crash the runtime — the robust suite drives a table
      of hostile strings through the normaliser to prove it
- [x] Two new suites, 27 tests: `css.computedvalue.*` 14 (the unit table, the hex
      forms against every wrong length, the whole named set, `rgb()` in both
      syntaxes and its range handling, the value's lifecycle and copy/move
      contract, the lifetime balance, the seven `computation` shapes, and hostile
      input) and `css.computed.*` 13 (the pipeline: declared,
      inherited-as-computed, default-then-replaced, non-inherited, the CSS-wide
      keywords, `var()`/`calc()`/`currentColor` carried and counted, an invalid
      value, custom properties, inline, a three-level chain, a cyclic chain
      reported as truncated, determinism, the argument contract, and no
      stylesheets at all)
- [x] `css.dom.computed.*` 2 — the whole pipeline, all seven stages, over the
      **visual checkpoint's own document**: specificity picking between two
      colours, a typed colour's channels, `font-size: inherit` landing on the
      length the body computed, `width: inherit` pulling a value for a property
      that does not inherit by default, a custom property travelling the path
      untouched, `color: nonsense` reported as invalid without corrupting the
      rest of the style, and a byte-for-byte proof that computing every
      element's style twice changes nothing the demo paints
- [x] Capability flag `computed_values` flipped to 1; `default_styles` and
      `stylesheet_loading` still 0. Version 0.8.0, `ROBO_CSS_API_FROZEN` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer over the whole
      suite, **zero findings**; the plain build is 1859/1859 and `css.` is 452/452
- [x] `scripts/robo check`, `build-runtime`, `test-css`, `test-qemu-display` all
      pass; `test-runtime` reports only the documented environmental
      `window.host.keyboard` failure, which no RoboCSS code touches
- [x] Phases 6–9.7 regression-tested: full suite green — up 29 CSS tests from 9.7,
      with no existing test weakened or removed
- [x] `css/docs/ComputedValues.md` added, and `css/README.md`, `css/docs/Tests.md`,
      `css/docs/Architecture.md`, `css/docs/Inheritance.md`, `css/docs/Cascade.md`,
      `css/docs/Parser.md`, `css/docs/Selectors.md` and `ARCHITECTURE.md` updated
- [x] No Chromium dependency introduced
- [x] No used values, no colours beyond the forms the parser already expressed
      (9.9), no units beyond the length forms a computed value carries (9.10), no
      box model, no layout, no painting, no CSSOM, no animations
- [x] Stop after 9.8

### What 9.8 found

**Three real product defects.** Two of them had survived a green `--filter=css.`
build because the new suite files were compiled but **not registered** — the build
script's source list and `Tests/Main.c` are two places, and a suite has to be in
both. Until it was, 1846 lines of assertions were files on disk and nothing more.

* `robo_css_computed_value_copy` set the text pointer but not its length, so a
  copied value read as *empty*. Inheritance copies the parent's computed value, so
  this is how a keyword would reach a child as no keyword at all.
* `robo_css_unit_is_absolute` was written as `unit_to_px(1.0, unit) != 0.0`, and
  the converter returns the value it was given for a unit with no fixed factor — so
  `em`, `vh` and the empty unit all reported themselves absolute, and `em` and `vh`
  would have been silently converted to pixels in the computed value.
* `rgb()` could not parse the space syntax at all: the splitter skipped whitespace
  and enforced separators with a "last component was an argument" flag, so two
  space-separated numbers looked adjacent. The fix records which of the two
  separator styles a call started with and refuses a second one, which is also
  what makes `rgb(255, 0 0)` malformed and `rgb( 1 , 2 , 3 )` legal.

**Five expectations were wrong rather than the implementation**: `#ff00` is a
valid `#rgba` colour (the malformed case is `#ff`); a length-shaped property must
reject a non-length keyword; `not an identifier` is the right message for `k: 10`
(`10` *is* one component); `css_allocator_ensure_enabled` reports whether *this
call* enabled the tracker; and three argument-contract cases passed a computed
value where a specified value belongs.

## Goal 9.9 — CSS colours

**Status: complete.** The colour module, the value layer's delegation to it, the
contextual `currentColor` resolution in the computed style, the registry's second
colour property, and 19 new tests are implemented and verified.

### Delivered

- [x] `Include/RoboCSS/RoboCSSColor.h` + `Source/RoboCSS/RoboCSSColor.c` — the
      **eighth module**, and the first that is a *representation* rather than a
      stage. A colour is the one CSS value whose meaning is arithmetic, and 9.8's
      colour code lived inside the computed-value layer because that was its only
      reader; 9.9 gives it a header, an implementation and every spelling
- [x] `robo_css_color_t`: **four kinds, not one.** `NONE` (nothing, and what a
      failed parse leaves), `RGBA` (four 8-bit channels in sRGB — the canonical
      resolved colour), `CURRENT` (`currentColor`, contextual *by construction*)
      and `INVALID` (a colour was meant and there is not one). The kind is what
      makes `currentColor` expressible at all, and it is why equality is
      kind-aware: a resolved red is never equal to `currentColor`, because they
      are different answers
- [x] **8 bits per channel, and it is a decision.** CSS's ordinary syntax cannot
      express more, the framebuffer is 8-bit, and a float triple would invite
      equality comparisons that fail on values which are the same colour. `hsl()`
      converts once in doubles with a single rounding at the end, so
      `hsl(120 100% 25%)` is *exactly* `#008000` — a property a test can hold
- [x] The **148-name standard set** (the 16 CSS2.1 keywords, the 130 SVG/X11 names
      with their duplicates, and `rebeccapurple`), ASCII case-insensitive,
      reachable by name **and by index** so a suite walks the whole table rather
      than the handful of names someone thought to type. `transparent` is
      deliberately **not** a table entry: it is a colour but a *keyword*, handled
      where the identifier is. An unknown identifier is never silently a colour
- [x] All four hex lengths with nibble **expansion** (not zero-padding — the
      classic three-digit mistake), every other length and every non-hex digit
      refused rather than truncated, case-insensitive, and an opaque alpha nibble
      normalised away (`#f00f` *is* `#ff0000`)
- [x] `rgb()`/`rgba()` in **both** syntaxes — legacy comma and modern space, with
      numeric, percentage or *mixed* channels and numeric or percentage alpha —
      with `rgb` and `rgba` deliberately not distinguished, because CSS Color 4
      made each accept both shapes. Channels **clamp rather than wrap**; the two
      separator styles do not mix, which is what makes `rgb(255, 0 0)` malformed,
      `rgb( 1 , 2 , 3 )` legal, and `rgb(1 2 3 4)` a call with an alpha separator
      nobody wrote
- [x] `hsl()`/`hsla()` with hue as an angle in the four units (`deg`, `grad`,
      `rad`, `turn`), hue **normalised rather than clamped** (`-120` and `240` are
      the same hue, `480` is `120`, `720` is `0`), saturation and lightness
      clamped, percentages required in the legacy syntax and bare numbers read as
      percentages in the modern one, and a hue too large to reduce saturating to
      zero rather than producing an artefact
- [x] **Six outcomes**, because "did it work" is not enough of an answer:
      `RESOLVED`, `CURRENT`, `DEFERRED`, `MALFORMED`, `UNKNOWN_NAME`, `INVALID`.
      `DEFERRED` is the one that keeps the build from lying — `hwb`, `lab`, `lch`,
      `oklab`, `oklch`, `color()`, `color-mix()`, `device-cmyk()` are legal CSS
      this build cannot represent, so the declaration is carried and flagged
      unresolved rather than called invalid; a wrong colour is worse than a
      deferred one. `MALFORMED`, `UNKNOWN_NAME` and `INVALID` all end as an
      invalid computed value and stay separate because the *diagnostic* differs
      and the diagnostic is the point
- [x] The value layer now **consumes** the colour module instead of carrying its
      own parser — one authoritative implementation, so two spellings of the same
      colour cannot disagree. A **reference is settled before the colour parser is
      asked**, which is the defect this phase found: `color: var(--brand)` is a
      `var()` reference, not a mistake, and it is carried with its tokens and the
      `UNRESOLVED` flag exactly as every other value shape is
- [x] `currentColor` resolved by the **computed style**, at the only point that
      has both the element and the parent, and by **two rules**: on `color` itself
      it is the parent's computed `color` ("the colour I would have inherited",
      the only reading that terminates), and everywhere else it is the element's
      own ("my text colour"). `color` is therefore resolved first, in a pass of its
      own, so nothing can read a value that has not been settled. With nothing to
      resolve against — a root element — the colour **stays** `CURRENT` and
      **stays** flagged, because inventing black is the mistake the keyword exists
      to prevent. Only the `UNRESOLVED` bit is cleared, never the whole flag set
- [x] **`background-color` joined the property registry.** The reason is not
      backgrounds: it is the only *second* colour-shaped property, and without one
      the rule that resolves `currentColor` against the element's own `color`
      would be unreachable and therefore unprovable. A property fact is what that
      table holds — no background is painted, no shorthand is expanded, and
      `background` keeps its own `PRESERVE` classification. The registry suite's
      count assertion (24 → 25 rows, 8 → 9 non-inherited) is updated with it, which
      is exactly what that deliberate count exists for
- [x] Serialization: one stable spelling per kind (`none`, `invalid`,
      `currentColor`, lower-case hex with alpha only when it is not opaque). Named
      colours are deliberately **not** written back as names, because two equal
      colours must print the same and hex always can — which is what makes a
      written colour usable as an assertion. A short buffer is refused with the
      length it needed rather than truncated
- [x] Numeric safety: channels clamp, a non-finite literal is clamped into range,
      an underflow is a channel like any other, and a hue with no fractional part
      left saturates. 37 malformed declarations are driven through the parser with
      two invariants asserted on every one — the outcome is a defined enumerator,
      and a colour that was not produced is `NONE` or `INVALID`, never a
      plausible-looking channel set nobody wrote
- [x] New suite `css/tests/SuiteRoboCssColor.c`, **19 tests**: `css.color.*` 12
      (the record and its kind tables, the whole 148-entry table walked by index,
      all four hex lengths against every wrong length and non-hex digit, both
      argument styles of both functions with clamping and one-rounding, hue
      normalisation, `currentColor` typed and the substitution's four cases, the
      deferred spaces, the not-a-colour line, 37 malformed inputs, serialization)
      and `css.color.computed.*` 7 (twelve spellings of four syntaxes converging on
      one computed colour each, inheritance through two levels with an intermediate
      override, `background-color` not inheriting, the CSS-wide keywords on both
      kinds, the two `currentColor` rules separated by a parent whose colour
      differs from its child's, a root whose `currentColor` stays contextual, the
      `var()` boundary, the whole selector pipeline over a three-level tree, and
      determinism)
- [x] Capability flag `colors` flipped to 1; `default_styles` and
      `stylesheet_loading` still 0. Version 0.9.0, `ROBO_CSS_API_FROZEN` still 0
- [x] Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer, **zero findings**;
      `css.color` is 19/19 under the sanitized binary, the CSS surface is 471/471
      and the whole suite is 1878/1878 run in the foreground
- [x] `scripts/robo check` **PASS** with Chromium independence **CLEAN** (sources
      + headers, build wiring); `build-runtime` **PASS**; `test-css` **PASS** (2/2);
      `test-qemu-display` **PASS** (2/2, the booted QEMU display); the host visual
      checkpoint **PASS** offscreen at **15/15 probes**; `validate-deployment`
      **PASS**. `test-runtime` reports only the documented environmental
      `window.host.keyboard` failure, which no RoboCSS code touches
- [x] Phases 6–9.8 regression-tested: full suite green — up 19 tests from 9.8,
      with no existing test weakened or removed
- [x] `css/docs/Colors.md` added, and `css/README.md`, `css/docs/Tests.md`,
      `css/docs/Architecture.md`, `css/docs/ComputedValues.md` and `ARCHITECTURE.md`
      updated
- [x] No Chromium dependency introduced
- [x] No colour interpolation, no blending, no second space, no unit system beyond
      the length forms a computed value carries (9.10), no box model, no layout, no
      painting, no CSSOM, no animations
- [x] Stop after 9.9

### What 9.9 found

**One real product defect, and it is the kind that hides in plain sight.**

* A `var()` in a colour position was reported as a **mistake**. `color:
  var(--brand)` reached the colour parser, which called it non-colour syntax and
  returned `INVALID`, so a legal reference to a colour produced the same computed
  value as `color: nonsense` and the diagnostic said `not a colour`. The parser
  was right about the syntax and wrong about the author. `computed_compute_color`
  now settles the reference first — a single unresolved function (`var`, `env`,
  `calc`, `min`, `max`, `clamp`) is carried with its tokens and the `UNRESOLVED`
  flag — which is what every other value shape already did.

**Three expectations were wrong rather than the implementation**, and each was a
real piece of CSS arithmetic rather than a typo:

* `#f00f` is `#ff0000` — an opaque alpha nibble normalises away, which is the
  representation agreeing with the `#rrggbb` spelling of itself;
* `rgb(51%, 40%, 60%)` is `#826699`, not `#336699` — the percentage case had the
  numeric channels' expected value copied into it, and the corrected pair now
  proves 20% → 51 exactly and 51% → 130.05 rounded to 130;
* `hsl(210, 100%, 20%)` is `#003366`, not `rgb(0, 68, 102)` — the green channel is
  the sector's *intermediate* (51), not its endpoint, while the blue is lightness
  plus half the chroma (102).

One helper was broken in a way that made a **failure message lie**:
`css_color_same` passed `NULL` for the serialization length, which the writer
refuses, so the two buffers it printed as "the colours that differ" were
uninitialised stack. It now checks both writes, because a diagnostic that names
stack noise as a colour is worse than no diagnostic.

## Goal 9.10 — CSS units

**Status: complete.** The CSS numeric-and-unit subsystem: a closed unit
vocabulary, a resolution context, a typed numeric value, and the computed-value
integration that consumes them.

### Delivered

| Item | Where |
|---|---|
| The unit vocabulary | `Include/RoboCSS/RoboCSSUnit.h`, `Source/RoboCSS/RoboCSSUnit.c` — 31 units plus `NONE`, four families, one table row each: spelling, family, fixed factor, viewport family and dimension |
| The resolution context | `robo_css_unit_context_t` and six `_set_*` setters — a `has_` flag per fact, so "the font size is 16" and "I do not know the font size" stay different answers. An argument, never a global |
| Resolution | `robo_css_unit_resolve_px` — four outcomes (`RESOLVED` / `NEEDS_CONTEXT` / `NOT_A_LENGTH` / `INVALID`), with `out_px` written only on success |
| Absolute conversion | The reference-pixel relationships (`1in` = 96px, `1cm` = 96/2.54px, `1pt` = 4/3px, `1Q` = 1/40cm), exact and context-free — no DPI anywhere |
| The typed value | `robo_css_numeric_t` — `NUMBER` / `INTEGER` / `PERCENTAGE` / `LENGTH`, a tag plus a double plus an enumerator by value, owning nothing |
| The parse | Five outcomes: `RESOLVED`, `DEFERRED` (`calc`/`min`/`max`/`clamp`/`var`/`env`), `UNKNOWN_UNIT`, `NON_FINITE`, `INVALID` |
| Serialization | `robo_css_numeric_write` — one canonical spelling, a short buffer refused rather than truncated |
| Computed-value integration | `RoboCSSComputedValue.c` now *consumes* the module: absolute units convert to `px`, contextual ones carry their unit and number, percentages stay percentages |
| Version | `RoboCSSVersion.h` 0.10.0; capability `units` = 1 |
| Documentation | `css/docs/Units.md` (new, the design record), plus `README.md`, `docs/Architecture.md`, `docs/Tests.md`, `docs/ComputedValues.md` and `docs/Colors.md` |

### Definition of done

- [x] A typed numeric/unit subsystem exists as a module of its own, and the
      computed-value layer holds no second unit table.
- [x] Tokens the tokenizer produces read as one of four kinds, and nothing reads
      a number's text.
- [x] Absolute CSS lengths convert deterministically and exactly, and nothing
      consults a display.
- [x] Relative and viewport lengths have a contextual representation and a
      resolver that can answer `NEEDS_CONTEXT` rather than inventing a font size
      or a viewport.
- [x] Percentages are a kind, not a unit, and stay distinguishable from lengths.
- [x] Unknown units fail safely and are reported apart from a value that is not
      numeric at all.
- [x] Numeric overflow, underflow and non-finite input are refused, and the
      integer conversion checks its range *before* casting.
- [x] `calc()` and the other reference functions are carried, not claimed.
- [x] The computed-value integration keeps 9.8's rule: absolute converts, contextual
      is carried, percentage is kept.
- [x] Cascade, inheritance, colour and selector behaviour are unchanged, and the
      visual vertical slice is verified rather than assumed.
- [x] Tests are comprehensive: 18 in `SuiteRoboCssUnit.c` and one real-DOM pipeline
      test, covering valid, invalid, boundary, conversion, precision, safety and
      regression cases.
- [x] Host and QEMU runtimes still work; no Chromium dependency was introduced.

### What 9.10 found

**One defect the delegation introduced, and one the serializer had all along.**

* **A length-shaped property rejected every keyword.** Moving the keyword handling
  inside the branch guarded by "there is no single token" meant `auto` never
  reached it: an IDENT *is* one token, so the identifier fell through to the
  numeric parse — which is right to call an identifier not a number — and the
  developer was told `not a length` when the property's grammar simply did not
  accept the keyword. Seven 9.8 tests failed on it, all the same defect wearing a
  different property's clothes (`l: auto`, `h: auto`, `font-size: medium`,
  `width: auto`). The fix asks the property's keyword list before the numeric
  vocabulary, and the ordering is now commented as load-bearing.
* **A whole `-0` printed as `-0`.** The negative-zero rule lived in the
  *fractional* branch, and `-0.0` has no fraction to strip — `%.0f` renders it
  whole — so the sign survived on a value that is zero, against the header's own
  documented rule. It now runs after both branches, which is where a rule about the
  rendered digits belongs.

Four expectations were wrong rather than the implementation: `40Q` is `37.795px`
and not `9.6px` (a factor of four applied twice — and the suite now asserts the
relationship in both directions, `40Q` = `1cm` *and* `4Q` = `1mm`); `1e300in` does
*not* overflow, so the overflow case proves nothing until it uses `1e308` (and now
asserts that `1e300` resolves, pinning the boundary from both sides); there are 18
viewport units and not 16 (six in the default family, four in each overridable
one); and the malformed-input sweep used the runtime's own finiteness predicate,
which would have agreed with the code under test by construction.

## Next

- Goal 9.11 — do not start until requested

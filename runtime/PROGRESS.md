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

## Next

- ~~Goal 5.1 — RoboFont foundation~~
- ~~Goal 5.2 — SFNT container parser~~
- ~~Goal 5.3 — TrueType table parsing~~
- ~~Goal 5.4 — OpenType table parsing~~
- ~~Goal 5.5 — CFF/CFF2 support~~ (complete below)

## Goal 5.1 — RoboFont Architecture and Font Model Foundation

**Status:** complete

**Done:**

- Public API under `Include/RoboFont/` — types, style, metrics, source, face, instance
- Memory-backed font sources (copy / borrow) with retain/release ownership
- Face vs instance separation; format magic sniff (SFNT/WOFF/WOFF2) without parsing
- Metrics value model + font-unit ↔ CSS-px helpers; glyph ID type
- Docs: `runtime/font/README.md`, `Architecture.md`, `Api.md`
- Suite `SuiteRoboFont.c` (9 tests); Build.ps1 integration
- Test registry capacity raised (`ROBO_TEST_MAX` 512→640) for Phase 5 growth
- Independent of Chromium / Skia / OS font managers
- Debug / Release (`-Filter font`): **9/9**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Architecture + fundamental types + source/face/instance foundation
- [x] Unit tests; documentation; Chromium-independence (build VERIFY)
- [x] Stop after 5.1 (no SFNT parsing / Goal 5.2)

## Goal 5.2 — SFNT Container Parser

**Status:** complete

**Done:**

- Big-endian bounds-checked reader (`RoboFontBinary.h` / `.c`)
- SFNT header + table directory parser (`RoboFontSfnt.h` / `.c`)
- Supported signatures: `0x00010000`, `'true'`, `'typ1'`, `'OTTO'`; TTC via `'ttcf'` + face index
- Table lookup / enumeration / bounded byte views; duplicate + overlap rejection
- SFNT table checksum (padded BE uint32 sum); optional verify flag (no `head` adjustment yet)
- Defensive limits (max tables / source size); fuzz entry `robo_font_sfnt_fuzz_parse`
- Docs: `runtime/font/docs/Sfnt.md`; README / Architecture updated; version **0.2.0**
- Tests: `runtime/font/tests/SuiteRoboFontSfnt.c`
- Debug / Release / ASan+UBSan (`-Filter font`): **17/17**; VERIFY: no Chromium paths
- Chromium-independence via Build.ps1 VERIFY

**Definition of done:**

- [x] Safe memory-backed SFNT parse, enumerate, lookup, view, checksum
- [x] Malformed-input tests; no Chromium dependency
- [x] Stop after 5.2 (no TrueType semantic tables / Goal 5.3)

## Goal 5.3 — TrueType Font Table Parsing

**Status:** complete

**Done:**

- Parsers: `head`, `hhea`, `maxp`, `hmtx`, `loca`, `glyf` (simple+composite+flags), `name`, `OS/2`, `post`
- Cross-table validation; composite depth/cycle caps; head checksumAdjustment verify option
- Face integration: `robo_font_face_parse_truetype`, glyph_count from `maxp`, style from OS/2
- Metrics bridge from head/hhea/(OS/2 typo); name UTF-16BE → UTF-8 via RoboUnicode
- Docs: `docs/TrueType.md`; version **0.3.0**
- Tests: `font/tests/SuiteRoboFontTrueType.c` (synthetic fixtures)
- Debug / Release / ASan (`-Filter font.`): **23/23**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Safe TT table parse + glyf structure + naming/style metadata
- [x] Malformed tests; Chromium-independence
- [x] Stop after 5.3 (no OpenType layout / Goal 5.4; no cmap / 5.6)

## Goal 5.4 — OpenType Font Table Parsing

**Status:** complete

**Done:**

- Outline technology detection (TrueType / CFF / CFF2 presence; bodies deferred to 5.5)
- Table family registry (`robo_font_ot_table_family`) over SFNT directory
- Shared structures: Offset helpers, Coverage 1–2, ClassDef 1–2, Device, ScriptList / FeatureList / LookupList
- `cmap` structural parse (formats 0/2/4/6/8/10/12/13/14 metadata; no Goal 5.6 mapping)
- GDEF / GSUB / GPOS structural parsers (no substitution/positioning)
- BASE, JSTF, MATH, kern, COLR, CPAL, STAT structural parsers
- Resource limits + RoboError; fuzz entry `robo_font_ot_fuzz_parse`
- Docs: `docs/OpenType.md`; version **0.4.0**
- Tests: `font/tests/SuiteRoboFontOpenType.c`
- Debug / Release / ASan (`-Filter font.`): **30/30**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Safe OpenType structure parse + layout foundations for Phase 6
- [x] Malformed nested-offset tests; Chromium-independence
- [x] Stop after 5.4 (no CFF / Goal 5.5; no shaping / Phase 6)

## Goal 5.5 — CFF / CFF2 Font Support

**Status:** complete

**Done:**

- CFF1 header, INDEX (offSize 1–4), DICT operands/operators, Top/Private/Font DICT
- String INDEX, charset (0/1/2), Encoding (0/1 + supplement), FDSelect (0/3)
- Type 2 CharString IR + local/global subrs (bias, depth/call caps)
- CID-keyed ROS / FDArray / per-FD Private
- CFF2 header, Card32 INDEX, FDArray/FDSelect, Item Variation Store foundation
- Outline tech integration via existing `robo_font_ot_detect_outline`
- Resource limits + RoboError; fuzz entry `robo_font_cff_fuzz_parse`
- Docs: `docs/Cff.md`; version **0.5.0**
- Tests: `font/tests/SuiteRoboFontCff.c`
- Debug / Release / ASan (`-Filter font.`): **36/36**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Safe CFF/CFF2 structure parse + CharString IR for Goal 5.8
- [x] Malformed tests; Chromium-independence
- [x] Stop after 5.5 (no cmap mapping / Goal 5.6; no final outlines / 5.8)

## Goal 5.6 — Character-to-Glyph Mapping (`cmap`)

**Status:** complete

**Done:**

- Immutable `robo_font_cmap_map_t` from SFNT `cmap` or raw span
- Formats 0, 2, 4, 6, 10, 12, 13 + format 14 UVS foundation
- Deterministic Unicode subtable selection (platform/encoding/format scoring)
- Unicode scalar validation; GID checks vs `maxp.numGlyphs`
- Face API: `parse_cmap` / `lookup_glyph`; fuzz entry `robo_font_cmap_fuzz_parse`
- Docs: `docs/Cmap.md`; version **0.6.0**
- Tests: `font/tests/SuiteRoboFontCmap.c` (synthetic fixtures)
- Debug / Release / ASan+UBSan (`-Filter font.`): **48/48**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Safe Unicode→GID across TT / CFF / CFF2 outline technologies
- [x] Supplementary planes + UVS foundation; malformed/fuzz survivability
- [x] Stop after 5.6 (no metrics depth / Goal 5.7; no shaping / Phase 6)

## Goal 5.7 — Font Metrics

**Status:** complete

**Done:**

- Immutable `robo_font_metrics_info_t` from TT/SFNT (TrueType, CFF, CFF2 via `hmtx`)
- Canonical layout policy (OS/2 USE_TYPO_METRICS vs hhea); distinct win/typo/hhea fields
- Glyph advances/LSB (full + compact `hmtx`); RSB + glyf bbox when available
- Cap/x-height, avg width, underline/strikeout/caret; weight/width classes
- Safe scaling + rounding; vertical `vhea`/`vmtx` foundation
- Face/instance integration; fuzz entry `robo_font_metrics_fuzz_parse`
- Docs: `docs/Metrics.md`; version **0.7.0**
- Tests: `font/tests/SuiteRoboFontMetrics.c`
- Debug / Release / ASan+UBSan (`-Filter font.`): **65/65**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Safe unified metrics API across outline technologies
- [x] Malformed/fuzz survivability; Chromium-independence
- [x] Stop after 5.7 (no final outlines / Goal 5.8; no shaping / Phase 6)

## Goal 5.8 — Glyph Outlines

**Status:** complete

**Done:**

- Unified immutable `robo_font_outline_t` (MOVE/LINE/QUAD/CUBIC/CLOSE)
- TrueType simple glyf → quads with implied on-curve midpoints
- TrueType composite flatten: affine, depth/cycle/component limits
- CFF/CFF2 CharString IR → cubics; hints skipped; subr limits inherited
- Mathematical curve extrema for bounds; empty / `.notdef` support
- Face-local outline cache (FIFO ≤ 512); golden `outline_dump`; fuzz entry
- Docs: `docs/Outlines.md`; version **0.8.0**
- Tests: `font/tests/SuiteRoboFontOutline.c`
- Debug / Release / ASan+UBSan (`-Filter font.`): **65/65**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Safe TT/CFF/CFF2 → one geometric outline representation
- [x] Composite security + resource limits; metrics cross-check
- [x] Stop after 5.8 (no naming/metadata / Goal 5.9; no shaping / Phase 6)

## Goal 5.9 — Font Naming & Metadata

**Status:** complete

**Done:**

- Safe `name` table decode on existing SFNT/TT parse (no second SFNT parser)
- Structured entries: platform/encoding/language/name ID + UTF-8 + locale tag
- Standard name IDs 0–25; unknown IDs retained numerically
- UTF-16BE via RoboUnicode; MacRoman map; unsupported encodings explicit
- Windows/Mac/format-1 → BCP-47 locale association (RoboI18N)
- Deterministic localized lookup (exact → language → platform pref → default)
- `RoboFontMetadata` with PRESENT / FALLBACK / UNAVAILABLE / … avail flags
- Legacy / typographic / WWS family & subfamily kept distinct; preferred helpers
- PostScript structural check (no rewrite); version text + optional parse
- Duplicate policy (lowest index among equal score); resource limits; fuzz entry
- Face: `parse_metadata` / `face_name_info` / `face_metadata`; face-local cache
- Docs: `docs/Naming.md`; version **0.9.0**
- Tests: `font/tests/SuiteRoboFontName.c`
- Debug / Release / ASan+UBSan (`-Filter font.`): **71/71**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Safe decode, locale lookup, normalized metadata, limits, no Chromium deps
- [x] Stop after 5.9 (no style/weight/stretch / Goal 5.10; no shaping / Phase 6)

## Goal 5.10 — Font Style / Weight / Stretch Model

**Status:** complete

**Done:**

- Extended style primitives: weight categories, width classes, UNKNOWN style
- `RoboFontClassification`: raw OS/2/head/post/STAT tags/name layers + normalized
- Deterministic precedence for weight, stretch, style (documented)
- fsSelection bit policy (influence vs preserve-only); WWS flag retained
- Bold/italic bits never silently override stronger numeric OS/2 fields
- STAT axis tags retained for variable prep; no fvar/avar/gvar (Goal 5.18)
- Face `parse_classification`; `tt_style_desc` uses classification
- Fuzz entry; docs `docs/Classification.md`; version **0.10.0**
- Tests: `font/tests/SuiteRoboFontClassification.c`
- Debug / Release / ASan+UBSan (`-Filter font.`): **80/80**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Style/weight/stretch normalized without losing raw metadata
- [x] Stop after 5.10 (no selection / Goal 5.11; no variable fonts / 5.18)

## Goal 5.11 — Font Selection

**Status:** complete

**Done:**

- `RoboFontSelection`: request, match score, selection result
- Family gate (typo / WWS / legacy / optional full & PostScript) via casefold
- Stretch / style / weight distance keys (CSS Fonts–inspired)
- Combined order: family rank → stretch → style → weight → exactness → WWS → face_id
- Deterministic tie-break independent of candidate array order
- Selection result cookie so `robo_font_select` never frees an uninitialized `face`
- No discovery/fallback; malformed candidates skipped; fuzz entry
- Docs: `docs/Selection.md`; version **0.11.0**
- Tests: `font/tests/SuiteRoboFontSelection.c`
- Debug / Release / ASan+UBSan (`-Filter font.`): **88/88**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Caller-supplied faces matched deterministically with inspectable scores
- [x] Stop after 5.11 (no discovery / Goal 5.12; no fallback / 5.15)

## Goal 5.12 — Font Discovery

**Status:** complete

**Done:**

- Explicit discovery sources: memory, file, directory (optional recursive)
- Injectable filesystem IO + stub + virtual in-memory tree (no OS-wide scan)
- Format filter: `.ttf` / `.otf` / `.ttc` / `.otc` + SFNT signatures
- TTC/OTC multi-face registration with shared source bytes
- `RoboFontRegistry` / `RoboFontRegistration` with stable IDs + origin key
- Duplicate policy: `(source_id, origin, face_index)` + PostScript/style heuristic
- Registry queries, collect faces → Goal 5.11 selection
- Refresh/rescan; security limits; fail-soft scan; fuzz entry
- Lazy: metadata + classification at register; cmap/outlines on demand
- Docs: `docs/Discovery.md`; version **0.12.0**
- Tests: `font/tests/SuiteRoboFontDiscovery.c`
- Debug / Release / ASan+UBSan (`-Filter font.`): **95/95**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Explicit file/dir/memory discovery registers distinct faces
- [x] Collections and duplicates handled deterministically
- [x] Selector consumes discovered candidates
- [x] Stop after 5.12 (no loading/caching / Goal 5.13; no fallback / 5.15)

## Goal 5.13 — Font Loading & Caching

**Status:** complete

**Done:**

- `RoboFontLoader` / `RoboFontCache` bound to registry with invalidate hook
- Load request/result: policies for load / cache / validate
- Lazy STANDARD load (cmap+metrics); outlines remain on demand
- Bounded LRU cache (max faces / approx bytes); pin + bypass policies
- Eviction preserves consumer-retained faces
- In-flight `LOADING` + `ROBO_BUSY` (no partial publish)
- Permanent vs retryable failure handling
- TTC multi-face load; virtual file source; refresh invalidation
- Docs: `docs/Loading.md`; version **0.13.0**
- Tests: `font/tests/SuiteRoboFontLoader.c` (+ repeated-hit baseline)
- Debug / Release / ASan+UBSan (`-Filter font.`): **100/100**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Registered fonts load on demand and cache safely
- [x] Collection faces independent; malformed inputs safe
- [x] Stop after 5.13 (no coverage analysis / Goal 5.14; no fallback / 5.15)

## Goal 5.14 — Font Coverage Analysis

**Status:** complete

**Done:**

- `RoboFontCoverage` immutable layer on Goal 5.6 cmap (no outline preload)
- Query: unsupported / `.notdef` / usable / invalid scalar / no cmap
- Compact usable ranges + binary-search membership; glyph lookup API
- Script + Script_Extensions analysis (script ≠ language); emoji scalar stats
- Format-14 UVS coverage wrapper; face lazy attach + loader approx bytes
- Concurrent build → `ROBO_BUSY`; fuzz entry; lookup bench baseline
- Docs: `docs/Coverage.md`; version **0.14.0**
- Tests: `font/tests/SuiteRoboFontCoverage.c`
- Debug / Release / ASan+UBSan (`-Filter font.`): **108/108**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Exact Unicode coverage + safe glyph IDs
- [x] Ranges, scripts/SCX, UVS, `.notdef`/empty-outline policy documented
- [x] Cache-integrated immutable coverage; malformed input safe
- [x] Stop after 5.14 (no fallback / Goal 5.15)

## Goal 5.15 — Font Fallback

**Status:** complete

**Done:**

- `RoboFontFallbackResolver` — coverage-first alternate-face resolution
- Request/result/policy/score model (not CSS); sequence foundation without shaping
- Primary fast path via Goal 5.14 coverage; candidates only from Goal 5.12 registry
- Stages: PRIMARY → PREFERRED → SCRIPT → GENERAL → LAST_RESORT → NONE
- Deterministic scoring (coverage hard gate; selection distances for style)
- Script relevance + language preference hooks (no hard-coded language DB)
- Bounded decision cache keyed by registry generation; explicit no-fallback
- Docs: `docs/Fallback.md`; version **0.15.0**
- Tests: `font/tests/SuiteRoboFontFallback.c` (primary, script, preferred, cache,
  sequence/language/fuzz, primary-hit bench)
- Debug / Release / ASan+UBSan (`-Filter font.`): **113/113**; VERIFY: no Chromium paths

**Definition of done:**

- [x] Missing characters trigger fallback; primary coverage avoids unnecessary scan
- [x] Coverage-valid candidates; style/weight/stretch preserved when available
- [x] Script/language hints influence ranking without hard-coded language lists
- [x] Configurable chains; registry changes invalidate stale decisions
- [x] Sequence API foundation without shaping; malformed input safe
- [x] Stop after 5.15 (no generic families / Goal 5.16)

## Goal 5.16 — Generic Font Families

**Status:** complete

**Done:**

- Extended `robo_font_generic_family_t` (serif…ui-rounded + none)
- `RoboFontGenericConfig` — explicit ordered mappings + language overlays
- Layered resolver: application → runtime → platform → optional related
- Empty portable platform defaults; no hard-coded font lists
- `resolve` / `resolve_select` / `resolve_fallback` integrate 5.11 + 5.15
- Bounded cache keyed by config/registry/resolver generation
- Docs: `docs/GenericFamilies.md`; version **0.16.0**
- Tests: `font/tests/SuiteRoboFontGenericFamily.c`
- Debug / Release / ASan+UBSan (`-Filter font.`): **118/118**; VERIFY: no Chromium

**Definition of done:**

- [x] All required generic categories; explicit configurable mappings
- [x] Platform defaults without polluting portable core; app overrides
- [x] Deterministic priority; selection + fallback integration
- [x] Config changes invalidate; missing mapping → explicit unresolved
- [x] system-ui / emoji / math remain explicit; no Chromium dependency
- [x] Stop after 5.16 (no emoji font policy / Goal 5.17)

## Goal 5.17 — Emoji Fonts

**Status:** complete

**Done:**

- `RoboFontEmoji` — kind/capabilities, color vs mono, bitmap/vector flags
- Coverage + UVS (FE0E/FE0F) probes; sequence request foundation (no shaping)
- Selection + fallback glue via generic `emoji` and Goal 5.15
- Face-lazy immutable cache; fuzz entry; docs `docs/Emoji.md`
- Version **0.17.0**
- Tests: `font/tests/SuiteRoboFontEmoji.c`

**Definition of done:**

- [x] Emoji-capable fonts identified without name-only authority
- [x] Color/mono/bitmap/vector capabilities represented where supported
- [x] Generic `emoji` + fallback integration; conservative sequence support
- [x] No Chromium dependency; stop after 5.17 (no variable fonts / 5.18)

## Goal 5.18 — Variable Fonts

**Status:** complete

**Done:**

- `RoboFontVariation` — fvar/avar parse, axes, named instances, normalize
- User → normalized → avar-adjusted coordinates; STAT tags informational
- Instance sharing with variation coords; metrics state PENDING/STATIC
- Selection prefers variable axes for weight/stretch/style when covered
- Optical-size / named-instance request foundation; outline_at accepts coords
- Face-lazy immutable model; fuzz entry; docs `docs/Variation.md`
- Version **0.18.0**
- Tests: `font/tests/SuiteRoboFontVariation.c`

**Definition of done:**

- [x] Variable fonts parsed safely with resource limits
- [x] Axes, named instances, avar, STAT integration represented
- [x] Selection + instance model use variation without duplicate loads
- [x] No gvar/HVAR/CFF2 blend execution; no Chromium dependency
- [x] Stop after 5.18 (no color fonts / Goal 5.19)

## Goal 5.19 — Color Fonts

**Status:** complete

**Done:**

- `RoboFontColor` — unified COLR/CPAL, CBDT/CBLC, sbix, SVG representation
- COLR v0 layers + COLR v1 paint DAG (bounded, tagged, no rasterization)
- CPAL palettes, selection/override foundation
- Face-lazy immutable color cache; variation coords attachable
- Emoji selection can query color via face color capabilities
- Fuzz entry; docs `docs/Color.md`
- Version **0.19.0**
- Tests: `font/tests/SuiteRoboFontColor.c`

**Definition of done:**

- [x] Color capabilities detected safely across major OpenType mechanisms
- [x] COLR/CPAL/paint/bitmap/sbix/SVG represented without execution/rendering
- [x] Palettes queryable; glyph IDs integrate with face/instance model
- [x] Malformed color data cannot crash the runtime; Chromium-independent
- [x] Stop after 5.19 (no Goal 5.20)

## Goal 5.20 — Font Security & Validation

**Status:** complete

**Done:**

- `RoboFontValidate` — staged validation, policy levels, centralized limits
- SFNT/collection/checksum diagnostics; glyph/color/variation/name probes
- Loader `validate_for_load` gate; `PARANOID` loader policy
- Structured diagnostics; fuzz entry; regression corpus in suite
- Docs `docs/Validate.md`; sanitizer-compatible host tests
- Version **0.20.0**
- Tests: `font/tests/SuiteRoboFontValidate.c`

**Definition of done:**

- [x] All font input treated as untrusted; parsers remain defensive
- [x] Centralized limits + staged validation integrated with loading
- [x] Malformed/pathological fonts fail safely with diagnostics
- [x] Fuzz + sanitizer coverage; no Chromium dependency
- [x] Stop after 5.20 (no Goal 5.21)

## Goal 5.21 — Font Conformance Tests

**Status:** complete

**Done:**

- `RoboFontConformance` — categories, goldens, diagnostics, fuzz smoke
- `SuiteRoboFontConformance.c` — cross-cutting coverage for Goals 5.1–5.20
- Corpus under `font/tests/fonts/` with provenance READMEs
- Docs `docs/Conformance.md`, `tests/README.md`
- Version **0.21.0**

**Definition of done:**

- [x] Conformance coverage across implemented RoboFont subsystems
- [x] Valid + malformed/security + variation/color/emoji cases
- [x] Deterministic goldens; fuzz + sanitizer compatible
- [x] No Chromium dependency
- [x] Stop after 5.21 (no Goal 5.22)

## Goal 5.22 — RoboFont Final Integration / API Freeze

**Status:** complete

**Done:**

- Architecture / ownership / threading / security audits documented (`ApiFreeze.md`, `ApiInventory.md`)
- Version **0.22.0** + `ROBO_FONT_API_FROZEN=1` (**RoboFont API v1**)
- Phase 5 E2E acceptance suite `font/tests/SuiteRoboFontPhase5.c` (`font.p5.*`)
- Performance baselines in `docs/Performance.md`; Chromium-independence Verify scan
- Docs: `Phase5.md`, `Completeness.md`, `docs/Integration.md`, `docs/Security.md`, `docs/Testing.md`
- Future consumer contracts documented; Phase 6 not started
- Debug / Release / Sanitize `font.*`: **183/183**

**Definition of done:**

- [x] One coherent RoboFont architecture; public API frozen
- [x] Ownership, threading, memory, error, security documented and tested
- [x] End-to-end acceptance through frozen APIs
- [x] Conformance + sanitizer + Chromium-independence gates
- [x] Stop after 5.22 (do not begin Phase 6 / Goal 6.1)

## Goal 6.1 — RoboGraphics Architecture

**Status:** complete

**Done:**

- Public headers under `Include/RoboGraphics/` (geometry, color, pixel format, surface, image, frame, backend, capabilities)
- CPU-owned / borrowed surfaces via RoboMem; image + frame metadata
- Backend / presentation boundary stubs; capability query
- Docs: `graphics/README.md`, `docs/architecture|ownership|threading|coordinates|backend.md`
- Suite: `graphics/tests/SuiteRoboGraphics.c` (`graphics.*`)
- Version **0.1.0**; Chromium-independence Verify scan for RoboGraphics
- Designed for short path to pixels → framebuffer → RoboWindow / QEMU
- Debug / Release / Sanitize `graphics.*`: **11/11**

**Definition of done:**

- [x] Coherent independent graphics architecture
- [x] Core types compile and test headlessly
- [x] Ownership / threading / coordinates / backends documented
- [x] No Chromium / Skia / GPU API dependency
- [x] Ready for Goal 6.2; stop after 6.1

## Goal 6.2 — Pixel Formats

**Status:** complete

**Done:**

- Implemented formats: RGBA8, BGRA8, RGB8, GRAY8, ALPHA8 (explicit byte layouts)
- Metadata: channels, bpp, alpha, grayscale, normalized-int, byte-addressable, implemented flag
- Safe row/buffer/offset helpers with overflow detection
- Pack / unpack / convert via straight `robo_gfx_color_rgba8_t`
- Documented conversion + luminance rules (`docs/PixelFormats.md`)
- Suite: `graphics/tests/SuiteRoboGraphicsPixelFormat.c` (`graphics.pf.*`)
- Version **0.2.0**
- Debug / Release / Sanitize `graphics.*`: **20/20**

**Definition of done:**

- [x] Clean pixel-format abstraction with explicit channel order
- [x] Bytes-per-pixel, stride, offsets, overflow handling
- [x] Deterministic conversions; RoboError integration
- [x] Tests + docs; no Chromium dependency
- [x] Stop after 6.2 (do not begin 6.3)

## Goal 6.3 — Framebuffer Abstraction

**Status:** complete

**Done:**

- `robo_gfx_framebuffer_t`: owned (RoboMem, zero-filled) + borrowed external storage
- Explicit stride; default = Goal 6.2 minimum row bytes
- Overflow-safe `stride × height` sizing via pixel-format helpers
- Bounds-checked `row` / `pixel` / `pixel_offset` addressing
- Ownership retain/release; no double-free; OOM cleans up partial create
- Docs: `graphics/docs/Framebuffer.md` (+ README / architecture / ownership / threading)
- Suite: `graphics/tests/SuiteRoboGraphicsFramebuffer.c` (`graphics.fb.*`)
- Version **0.3.0**
- Debug / Release / Sanitize `graphics.*`: **31/31**

**Definition of done:**

- [x] Working framebuffer abstraction with owned CPU storage
- [x] Width/height validation; pixel-format integration; explicit stride
- [x] Overflow-safe size; row/pixel addressing; bounds checks; zero init
- [x] RoboMem / RoboError / lifetime correct; thread-safety documented
- [x] Tests + docs; no Chromium dependency
- [x] Ready for Goal 6.4; stop after 6.3

## Goal 6.4 — Surface/Buffer Abstraction

**Status:** complete

**Done:**

- Surface wraps Goal 6.3 framebuffer as CPU backing (owned / borrowed)
- Explicit access modes + `map` / `unmap` with `robo_gfx_surface_map_t`
- Row/pixel/offset APIs delegate to framebuffer (no duplicated arithmetic)
- Clear via Goal 6.2 `pixel_pack`; no separate public Buffer type
- Docs: `graphics/docs/Surface.md` (+ README / architecture / ownership / threading)
- Suite: `graphics/tests/SuiteRoboGraphicsSurface.c` (`graphics.surf.*`)
- Version **0.4.0**
- Debug / Release / Sanitize `graphics.*`: **40/40**

**Definition of done:**

- [x] Clear Surface abstraction on 6.3 framebuffer foundation
- [x] CPU-owned surfaces; dimensions/format/stride/overflow-safe
- [x] Mapping, access modes, ownership, lifetime, addressing
- [x] RoboMem / RoboError / thread-safety docs; invalid states safe
- [x] Tests + docs; no Chromium dependency
- [x] Ready for Goal 6.5; stop after 6.4

## Goal 6.5 — Basic Bitmap Operations

**Status:** complete

**Done:**

- Surface-based bitmap ops (no duplicate Bitmap type): get/set pixel, clear, fill_rect, copy, copy_rect
- Logical RGBA8 via Goal 6.2 pack/unpack/convert; replace writes (no blending)
- Contained-rect bounds contract; empty rect no-op; overlap-safe same-surface blit
- Docs: `graphics/docs/Bitmap.md`
- Suite: `graphics/tests/SuiteRoboGraphicsBitmap.c` (`graphics.bmp.*`)
- Version **0.5.0**
- Debug / Release / Sanitize `graphics.*`: **46/46**

**Definition of done:**

- [x] Pixel read/write, clear, fill_rect, copy with correct formats
- [x] Bounds + overflow + overlap safety; RoboError / RoboMem
- [x] Tests + docs; no Chromium dependency
- [x] Ready for Goal 6.6; stop after 6.5

## Goal 6.6 — Pixels and Rectangles

**Status:** complete

**Done:**

- Canonical top-left / +X-right / +Y-down pixel coordinates (`pixel_x` / `pixel_y`)
- Point/size/rect ops: equality, add/sub/translate, area, LTRB construction
- Half-open rects; containment; intersect; bounding; translate; clip; inset/outset
- Overflow-safe arithmetic (`_checked` + int64 intermediates; no signed wrap)
- Negative logical geometry supported; storage indices validated separately
- `pixel_bounds`, point get/set; bitmap containment via `rect_encloses`
- Docs: `graphics/docs/Geometry.md` (+ coordinates / Bitmap / README)
- Suite: `graphics/tests/SuiteRoboGraphicsGeometry.c` (`graphics.geom.*`)
- Version **0.6.0**
- Debug / Release / Sanitize `graphics.*`: **55/55**

**Definition of done:**

- [x] Point / size / rect complete for graphics use
- [x] Half-open semantics, empty, negative coords, overflow safety documented
- [x] Intersection, bounding, translate, clip; bitmap integration
- [x] Tests + docs; no Chromium dependency
- [x] Ready for Goal 6.7; stop after 6.6

## Goal 6.7 — Lines

**Status:** complete

**Done:**

- `robo_gfx_line_t` + `draw_line` / `draw_line_seg` on CPU surfaces
- Inclusive one-pixel Bresenham (horizontal / vertical / diagonal / slopes)
- Cohen–Sutherland clip to surface pixel bounds; fully outside → OK no-op
- Color via Goal 6.2 pack (direct replace; no AA / blend / width)
- Docs: `graphics/docs/Lines.md`
- Suite: `graphics/tests/SuiteRoboGraphicsLine.c` (`graphics.line.*`)
- Version **0.7.0**
- Debug / Release / Sanitize `graphics.*`: **64/64**

**Definition of done:**

- [x] Deterministic one-pixel lines; inclusive endpoints; single-point
- [x] Clip / outside / formats / overflow-safe arithmetic
- [x] Exact pixel tests; no Chromium dependency
- [x] Ready for Goal 6.8; stop after 6.7

## Goal 6.8 — Paths foundation

**Status:** complete

**Done:**

- Owned `robo_gfx_path_t` with MoveTo / LineTo / Close command verbs
- Extensible verb enum (Quad/Cubic reserved); subpath + Close semantics
- Deterministic iteration (`command_at` / iter); exact AA bounds from points
- Docs: `graphics/docs/Paths.md`
- Suite: `graphics/tests/SuiteRoboGraphicsPath.c` (`graphics.path.*`)
- Version **0.8.0**
- Debug / Release / Sanitize `graphics.*`: **75/75**

**Definition of done:**

- [x] Canonical path model; ownership via RoboMem; no rasterization
- [x] Invalid LineTo rejected; Close no-op when no open contour
- [x] Tests for construction, subpaths, iteration, bounds, lifecycle
- [x] No Chromium dependency
- [x] Ready for Goal 6.9; stop after 6.8

## Goal 6.9 — Transforms

**Status:** complete

**Done:**

- Canonical `robo_gfx_affine_t` 2D affine API: rotate / shear / compose / invert
- Documented composition: `A*B` applies B then A; PreConcat / PostConcat
- Map point (float + integer round), enclosing map_rect, path transform
- Singular-matrix detection; exact 90° rotation matrices
- Docs: `graphics/docs/Transforms.md`
- Suite: `graphics/tests/SuiteRoboGraphicsTransform.c` (`graphics.xform.*`)
- Version **0.9.0**
- Debug / Release / Sanitize `graphics.*`: **86/86**
- RoboTest registry capacity raised to 1536 (suite exceeded 768)

**Definition of done:**

- [x] Identity / translate / scale / rotate / shear / compose / inverse
- [x] Rect AABB + non-mutating path transform; no Chromium dependency
- [x] Ready for Goal 6.10; stop after 6.9

## Goal 6.10 — Alpha compositing

**Status:** complete

**Done:**

- Straight-alpha source-over with premul blend intermediate
- Global opacity; `mul_div255` rounding; premul/unpremul helpers
- `composite_pixel` / `composite_rect` / `composite` with bounds clipping
- Same-storage snapshot for safe self-compositing
- Docs: `graphics/docs/Compositing.md`
- Suite: `graphics/tests/SuiteRoboGraphicsComposite.c` (`graphics.comp.*`)
- Version **0.10.0**
- Debug / Release / Sanitize `graphics.*`: **98/98** each (includes 12 `graphics.comp.*`)

**Definition of done:**

- [x] Deterministic src-over; format integration; clip safety
- [x] Self-composite safe; no Chromium dependency
- [x] Ready for Goal 6.11; stop after 6.10

## Goal 6.11 — CPU raster backend

**Status:** complete

**Done:**

- `robo_gfx_raster_t` immediate draw context (surface target, clip, opacity)
- Pixel / fill_rect / clear / line via existing bitmap + Bresenham + src-over
- `draw_line_clipped` extension for clip ∩ surface + optional blend
- Path fill/stroke API boundary → `ROBO_NOT_SUPPORTED`
- Docs: `graphics/docs/Raster.md`
- Suite: `graphics/tests/SuiteRoboGraphicsRaster.c` (`graphics.raster.*`)
- Version **0.11.0**
- Debug / Release / Sanitize `graphics.*`: **110/110** each (includes 12 `graphics.raster.*`)

**Definition of done:**

- [x] Deterministic CPU raster; clip safety; compositing integration
- [x] Line algorithm preserved; no Chromium dependency
- [x] Ready for Goal 6.12; stop after 6.11

## Goal 6.12 — Text rendering bridge

**Status:** complete

**Done:**

- Glyph-centric bridge: face + glyph id + size + baseline origin
- Font↔device coordinate conversion (Y flip); scaled metrics from RoboFont
- Outline → RoboGraphics path (MOVE/LINE/CLOSE; QUAD/CUBIC flattened)
- `raster_draw_glyph` outline stroke (clip/opacity); MASK/ALPHA/COLOR reserved
- Docs: `graphics/docs/TextBridge.md`
- Suite: `graphics/tests/SuiteRoboGraphicsText.c` (`graphics.text.*`)
- Version **0.12.0**
- Debug / Release / Sanitize `graphics.*`: **115/115** each (includes 5 `graphics.text.*`)
- Font regression (`font.`): **183/183** (Debug)

**Definition of done:**

- [x] No shaping/bidi/fallback in graphics; RoboFont remains authoritative
- [x] No Chromium dependency; RoboFont does not depend on RoboGraphics
- [x] Ready for Goal 6.13; stop after 6.12

## Goal 6.13 — Image output

**Status:** complete

**Done:**

- Encode pipeline: surface → format convert → PNG/BMP → memory / sink
- RGBA8 / RGB8 layouts; opaque RGB requires explicit background when source stores alpha
- Self-contained deterministic PNG (filter 0 + zlib store); BMP24 / BMP32 BI_BITFIELDS
- Full surface + region encode; image wrapper; sink boundary (no OS file API)
- Docs: `graphics/docs/ImageOutput.md`
- Suite: `graphics/tests/SuiteRoboGraphicsEncode.c` (`graphics.encode.*`)
- Version **0.13.0**
- Debug / Release / Sanitize `graphics.*`: **124/124** each (includes 9 `graphics.encode.*`)
- Chromium-independence scan: OK

**Definition of done:**

- [x] No decoding / JPEG / scaling / Chromium dependency
- [x] Ready for Goal 6.14; stop after 6.13

## Goal 6.14 — Screenshot / framebuffer export

**Status:** complete

**Done:**

- Capture pipeline: surface|framebuffer → region → owned snapshot → 6.13 encode
- Region policies: full extent, STRICT enclose, CLIP intersect
- FRONT/BACK capture sources reserved (`ROBO_NOT_SUPPORTED` until present/swap)
- Immutable snapshot; image wrapper; one-shot export helpers
- Docs: `graphics/docs/Screenshot.md`
- Suite: `graphics/tests/SuiteRoboGraphicsScreenshot.c` (`graphics.shot.*`)
- Version **0.14.0**
- Debug / Release / Sanitize `graphics.*`: **132/132** each (includes 8 `graphics.shot.*`)
- Chromium-independence scan: OK

**Definition of done:**

- [x] No window/QEMU/OS capture; no duplicate encoders; no Chromium dependency
- [x] Ready for Goal 6.15; stop after 6.14

## Goal 6.15 — RoboGraphics tests / Phase 6 freeze

**Status:** complete

**Done:**

- Test hierarchy documented (`graphics/docs/TestStrategy.md`, `tests/README.md`)
- Phase 6 integration suite: `SuiteRoboGraphicsPhase6.c` (`graphics.p6.*`)
  - freeze pin, visual fixture, raster→shot→encode, path/transform,
    font→text bridge, alpha overlap, determinism, safety, perf smoke,
    Chromium-independence smoke
- Docs: `Phase6.md`, `ApiFreeze.md`; README/architecture updated
- **RoboGraphics API v1** frozen at **0.15.0** (`ROBO_GRAPHICS_API_FROZEN=1`)
- Chromium comment cleanup in bitmap overlap path
- Validation: `graphics.*` **143/143** Debug + Release + ASan/UBSan;
  `font.*` **183/183** regression; Chromium-independence Verify OK;
  no Chromium linkage

**Definition of done:**

- [x] Phase 6 independently testable; no Phase 7 features
- [x] Chromium independence verified via Build.ps1 scan
- [x] Ready for Phase 7; stop after 6.15

- Phase 7 — RoboWindow & QEMU Display (do not start until requested)

## Goal 7.1 — RoboWindow architecture

**Status:** complete

**Done:**

- Module: `Include/RoboWindow/*`, `Source/RoboWindow/*`, `window/docs`, `window/tests`
- Identity, lifecycle, config/state, display placeholders, events, opaque native handle
- Backend vtable + null architecture backend; mock backend in tests
- Present contract (borrow RoboGraphics surface; present → `ROBO_NOT_SUPPORTED`)
- Suite: `SuiteRoboWindow.c` (`window.*`); version **0.1.0**; **12/12** Debug + Release
- Docs: architecture, lifecycle, backend, threading, graphics, test strategy
- Build.ps1 Chromium/OS-toolkit independence scan for RoboWindow
- Validation: Verify OK (Font/Graphics/Window Chromium scans); `graphics.*` **143/143**;
  no toolkit linkage

**Definition of done:**

- [x] Platform-independent architecture only; no OS/QEMU windows
- [x] No Chromium / toolkit linkage
- [x] Ready for Goal 7.2; stop after 7.1

## Next

- Goal 7.2 — Native window abstraction (do not start until requested)

## Goal 7.2 — RoboWindow native window abstraction

**Status:** complete

**Done:**

- Opaque native handle: token + generation, validity/equal/assign/invalidate
- Backend capabilities query; required caps on vtable registration
- Backend ops: set_title (UTF-8), focus; size/position with min/max + state sync
- Owner-thread bind/enter/require (`ROBO_PERMISSION_DENIED` off-thread)
- Present boundary still returns `ROBO_NOT_SUPPORTED` without present cap
- Suite: `SuiteRoboWindowNative.c` (`window.native.*`); version **0.2.0**
- Docs: Native, Capabilities, Backend, Threading updates
- Validation: `window.*` **22/22** Debug + Release; `graphics.*` **143/143**;
  Verify Chromium/toolkit OK

**Definition of done:**

- [x] No real OS/QEMU windows; no Chromium/toolkit deps
- [x] Phase 7.1 + 7.2 window tests; graphics regression
- [x] Ready for Goal 7.3; stop after 7.2

## Next

- Goal 7.3 — Display abstraction (do not start until requested)

## Goal 7.3 — RoboDisplay abstraction

**Status:** complete

**Done:**

- Dedicated `RoboDisplay` module: identity, info, orientation, connection,
  modes, scale/DPI helpers, refresh validation
- Backend vtable + capabilities; **null** (empty) and **placeholder**
  (1280×800 primary) backends — no host enumeration
- Manager: init/shutdown/refresh/count/at/get/primary/modes
- RoboWindow bridge: placeholder on window init; get/set associated display
- Window version **0.3.0**; display version **0.1.0**
- Suite: `SuiteRoboDisplay.c` (`display.*`) with mock backend
- Docs: `display/README.md`, `display/docs/`
- Validation: `display.*` **12/12**; `window.*` **22/22**; `graphics.*` **143/143**;
  Verify Chromium/toolkit OK

**Definition of done:**

- [x] No real host/QEMU display; no Chromium/toolkit deps in core abstraction
- [x] Phase 7.1–7.3 + graphics regression
- [x] Ready for Goal 7.4; stop after 7.3

## Next

- Goal 7.4 — Framebuffer presentation (do not start until requested)

## Goal 7.4 — Framebuffer presentation

**Status:** complete

**Done:**

- Presentation validation (surface/FB dims, stride, implemented formats,
  backend format mask + max size)
- `robo_window_present` / `robo_window_present_framebuffer` synchronous path
- Caps: `present_format_mask`, `present_max_width/height`
- Mock present backend with pixel-copy integrity checks (test-only)
- Suite: `SuiteRoboWindowPresent.c` (`window.present.*`); version **0.4.0**
- Docs: `Present.md`, Graphics/Capabilities/Native updates
- Validation: `window.present.*` **9/9**; `window.*` **31/31**; `display.*` **12/12**;
  `graphics.*` **143/143**; Verify Chromium/toolkit OK

**Definition of done:**

- [x] No host/QEMU/GPU present; no Chromium/toolkit deps
- [x] Phase 7.1–7.4 + graphics regression
- [x] Ready for Goal 7.5; stop after 7.4

## Goal 7.5 — Native window creation

**Status:** complete

**Done:**

- Host selector API `RoboWindowBackendHost.h` (`use_host` / `host_available` / vtbl)
- Win32 backend under `Source/RoboWindow/platform/win32/` (host-only; no OS headers in public API)
- Real create/destroy/show/hide/title/size/position/query_state/focus; UTF-8→UTF-16 titles
- Opaque native handle lifetime; create-thread affinity; min message pump via `robo_window_process_events`
- Config `x`/`y` with `ROBO_WINDOW_COORD_UNKNOWN`; fullscreen/scale deferred with logs
- Present remains `ROBO_NOT_SUPPORTED` on Win32 (Goal 7.4 mock path unchanged)
- Suite `window.host.*` with headless skip + optional `ROBO_WINDOW_SMOKE=1`
- Docs: `window/docs/Host.md`; version **0.5.0**
- Validation: `window.*` (incl. host/present/native) all pass; `display.*` /
  `graphics.*` regression; Verify Chromium/toolkit OK (win32 under
  `platform/win32/` whitelisted for `windows.h`)

**Definition of done:**

- [x] Real native OS window on Windows host (not mock-only production path)
- [x] Mock/null backends remain for tests
- [x] No Chromium / SDL / GLFW / Qt
- [x] Guest ELF builds without Win32
- [x] Ready for Goal 7.6; stop after 7.5

## Goal 7.6 — RoboWindow sizing

**Status:** complete

**Done:**

- Size helpers (`RoboWindowSize.h`): validate size + constraints; uses `robo_gfx_size_t`
- APIs: `set_size` / `get_size` / `get_requested_size` / get|set_size_constraints / `observe_size`
- Rejection semantics (no silent clamp on resize); corrective clamp only when applying new constraints
- State tracks min/max; sync preserves constraints across `query_state`
- Backend `set_size_constraints` + caps `size_constraints`; Win32 + null + mocks
- Failed resize restores previous requested size
- Suite `window.size.*`; host `window.host.sizing` + smoke sizing steps
- Docs: `window/docs/Size.md`; version **0.6.0**

**Definition of done:**

- [x] Initial + runtime sizing with min/max
- [x] Requested vs actual tracked
- [x] No Chromium / toolkit deps
- [x] Ready for Goal 7.7; stop after 7.6

## Goal 7.7 — RoboWindow close & lifecycle

**Status:** complete

**Done:**

- Lifecycle: UNINITIALIZED → CREATED → CLOSING → CLOSED (visible/hidden orthogonal)
- `request_close` vs `destroy` separated; close irreversible (no cancel)
- `observe_close_request` for future native close (WM_CLOSE → observe)
- Mutations blocked while CLOSING (`ROBO_INVALID_ARGUMENT`); destroy twice → `NOT_FOUND`
- Double close → `ROBO_ALREADY_EXISTS`; native destroy failure leaves lifecycle unchanged
- Shutdown destroys live windows and invalidates handles
- Suite `window.life.*`; host `window.host.close_lifecycle` + smoke close path
- Docs: `window/docs/Lifecycle.md`; version **0.7.0**

**Definition of done:**

- [x] Close request distinct from destruction
- [x] Deterministic transitions + idempotency
- [x] Native close integration boundary (no full event loop)
- [x] No Chromium / toolkit deps
- [x] Ready for Goal 7.8; stop after 7.7

## Goal 7.8 — DPI / scale foundation

**Status:** complete

**Done:**

- `RoboDisplayScale.h`: scale/DPI validation, reference DPI (96), conversions
- Rounding: nearest, ties away from zero; overflow → `ROBO_INVALID_ARGUMENT`
- Size/point/rect logical↔physical helpers; multi-display different scales
- Display APIs: `get_scale` / `get_dpi` / `info_effective_scale` / physical bounds
- Window APIs: `get_scale` / `get_dpi` / `get_size_physical`; preference lock
- Win32: optional `GetDpiForWindow` → state.scale (no fabrication)
- Suite `display.scale.*`; host `window.host.scale_dpi`
- Docs: `display/docs/Scale.md`; RoboDisplay **0.2.0**, RoboWindow **0.8.0**

**Definition of done:**

- [x] Logical vs physical + fractional scales
- [x] Unavailable DPI distinct from invalid
- [x] No hidden RoboGraphics scaling
- [x] No Chromium / toolkit deps
- [x] Ready for Goal 7.9; stop after 7.8

## Goal 7.9 — Native event loop

**Status:** complete

**Done:**

- `RoboWindowEventLoop`: start / stop / poll / run_once / run / wakeup
- Backend optional `wait_events` / `wakeup`; caps flags
- Queue: peek / is_empty; FIFO ordering preserved
- `observe_position` / `observe_visibility` / `observe_focus` (+ existing size/close)
- Geometry-apply suppress to avoid resize/move loops
- Win32: WM_SIZE / WM_MOVE / WM_SHOWWINDOW / focus + MsgWait / PostThreadMessage
- Mock injection suite `window.loop.*`; host `window.host.event_loop`
- Docs: `window/docs/EventLoop.md`; RoboWindow **0.9.0**

**Definition of done:**

- [x] One authoritative native event path
- [x] Close request remains distinct from destroy
- [x] Reentrancy / thread affinity / destroyed-target safety
- [x] No keyboard/mouse/pointer; no Chromium deps
- [x] Ready for Goal 7.10; stop after 7.9

## Goal 7.10 — Keyboard foundation

**Status:** complete

**Done:**

- `RoboWindowKey.h`: physical / logical key enums + modifiers + actions
- `RoboWindowKeyboard.h`: observe_key, state queries, reset
- Event types `KEY_DOWN` / `KEY_UP` with `robo_keyboard_event_t` payload
- Win32 VK/scan translation + modifier query + repeat bit
- Focus targeting via focused window / HWND slot; stale IDs rejected
- Stuck-key reset on focus loss, destroy, shutdown
- Suite `window.key.*`; host `window.host.keyboard`
- Docs: `window/docs/Keyboard.md`; RoboWindow **0.10.0**

**Definition of done:**

- [x] Physical vs logical keys; no Unicode/IME
- [x] Press/release/repeat + modifiers
- [x] Focused-window targeting + stuck-key reset
- [x] Mock inject + real native smoke path
- [x] No mouse/pointer; no Chromium deps
- [x] Ready for Goal 7.11; stop after 7.10

## Goal 7.11 — Mouse foundation

**Status:** complete

**Done:**

- `RoboWindowMouse.h`: buttons, event model, observe_mouse, state, capture foundation
- Event types `MOUSE_MOVE` / `BUTTON_DOWN` / `BUTTON_UP` / `WHEEL` with `robo_mouse_event_t`
- Reuses Goal 7.10 `robo_key_modifier_flags_t`
- Client/screen coords + delta; wheel LINE units (|120| = notch)
- Soft capture + optional Win32 `SetCapture` via backend caps/vtable
- Stuck-button reset on focus loss, destroy, shutdown
- Suite `window.mouse.*`; host `window.host.mouse`
- Docs: `window/docs/Mouse.md`; RoboWindow **0.11.0**

**Definition of done:**

- [x] Distinct mouse events (no touch/pointer/IME)
- [x] Move / buttons / wheel / modifiers / targeting
- [x] Click-count foundation + stuck-button reset
- [x] Mock inject + real native smoke path
- [x] No Chromium deps; stop after 7.11

## Goal 7.12 — QEMU display backend

**Status:** complete

**Done:**

- `RoboQemuFramebuffer` validate/present/get_pixel (BGRX/RGBX ↔ RoboGraphics)
- RoboDisplay QEMU backend + host `robo_qemu_sim_*`
- RoboWindow `platform/qemu`: guest `SYS_DISPLAY` claim/info/present/input;
  host sim blit; HID → `observe_key` / `observe_mouse`
- Guest `user/robo-runtime` deterministic pattern + serial markers
- Suite `window.qemu.*`; `scripts/test-qemu-display.ps1`
- Docs: `window/docs/Qemu.md`; RoboWindow **0.12.0**, RoboDisplay **0.3.0**

**Definition of done:**

- [x] Real path uses repo QEMU `-vga std` / GOP / `SYS_DISPLAY`
- [x] Presentation through RoboWindow/RoboDisplay (no second stack)
- [x] Keyboard/mouse through existing 7.10/7.11 APIs
- [x] Host builds + sim tests; no Chromium deps
- [x] Stop after 7.12

## Goal 7.13 — Host/native display backend

**Status:** complete

**Done:**

- Win32 present: RoboGraphics surface → staging `CreateDIBSection` (BGRX) →
  `InvalidateRect` / `WM_PAINT` `BitBlt`/`StretchBlt` into real HWND
- Host RoboDisplay backend: `EnumDisplayMonitors`, work area, DPI/scale
  (`GetDpiForMonitor` / `LOGPIXELS`); refresh Hz left unknown (0)
- `robo_window_backend_use_host` switches display to host monitors when live
- Suites: `window.host.present_*`, `display.host.*`; smoke
  `scripts/test-host-display.ps1`
- Docs: `window/docs/Host.md`; RoboWindow **0.13.0**, RoboDisplay **0.4.0**
- QEMU backend (7.12) unchanged in contract; guest stub host backend

**Definition of done:**

- [x] Real host OS window presents RoboGraphics
- [x] Display discovery + DPI/scale through RoboDisplay
- [x] Resize / close / keyboard / mouse via existing paths
- [x] Host smoke + unit tests; no Chromium deps
- [x] Stop after 7.13

## Goal 7.14 — Window + RoboGraphics integration

**Status:** complete

**Done:**

- Owned render surface (physical size × format) with retain/release lifecycle
- Frame API: `begin_frame` / `end_frame` / `present_frame` / invalidate / dirty
- Resize/scale/format → stale → recreate before next draw
- `present(NULL)` falls back to owned surface; backends unchanged
- Suites `window.graphics.*`; host smoke + QEMU guest use frame path
- Docs: `window/docs/Graphics.md`; RoboWindow **0.14.0** (bumped to 0.15.0 in 7.15)

**Definition of done:**

- [x] Clean window ↔ graphics surface ownership
- [x] Frame lifecycle + dirty/present
- [x] Host + QEMU present integrated surface
- [x] Keyboard/mouse unchanged; no Chromium deps
- [x] Stop after 7.14

## Goal 7.15 — Window / display tests

**Status:** complete

**Done:**

- Acceptance suite `window.p7.*` (`SuiteRoboWindowPhase7.c`)
- Lifecycle / graphics / scale / resize / multi-window / input / failures / shutdown
- Stale-event purge on destroy (`robo_window_event_purge_window`)
- Pixel fixture + screenshot checks; qemu_sim + host acceptance
- Docs: `window/docs/TestStrategy.md`; gate script `scripts/test-phase7.ps1`
- RoboWindow **0.15.0**

**Definition of done:**

- [x] Comprehensive Phase 7 regression / acceptance suite
- [x] Host + QEMU smoke paths documented and gated
- [x] Stale-window / failure / shutdown coverage
- [x] No Chromium deps; Phase 8 not started
- [x] Stop after 7.15

## Goal 8.1 — RoboHTML architecture

**Status:** complete

**Done:**

- RoboHTML module boundary: `Include/RoboHTML/` (14 public headers) + `Source/RoboHTML/` (13 translation units); independently buildable, listed in `Build.ps1` with its own Chromium/higher-layer independence scan
- Document architecture: create / empty state / attach-detach / mark-ready / mark-failed / destroy; fixed slot table (`ROBO_HTML_MAX_DOCUMENTS`); monotonic ids, never reused
- Document lifecycle state machine: uninitialized → created → loading → ready → failed → destroying → destroyed; deterministic invalid-transition errors
- Node identity & lifetime: generation-guarded handles `(document, node, generation)`; document-owned registry; stale-handle detection; root document node registered at creation
- Parser boundary: exclusive document lease, COMPLETE/INCREMENTAL input modes, idle/feeding/complete/failed/detached states; no tokenizer logic
- Source/input abstraction: empty / memory / string / stream kinds, borrowing-only view, declared-encoding + name metadata
- Encoding boundary: label table (UTF-8, UTF-16LE/BE, UTF-16, windows-1252, Shift_JIS), declared/actual/status state; no decoding yet
- Resource/loading boundary: delegated acquisition slot (`begin`/`end`/`cancel`); zero network code in RoboHTML
- Client/render integration hooks (lifecycle + invalidation) and mutation hook foundation; DOM capability stubs (all off in 8.1)
- Diagnostics: `robo_html_diag_t` with 10 HTML diagnostic domains on top of shared `robo_result_t`
- Suites `html.*` + acceptance `html.p8.*` (25 tests); docs `html/README.md`, `html/docs/Architecture.md`
- RoboHTML **0.1.0** (`ROBO_HTML_API_FROZEN=0`)

**Definition of done:**

- [x] Public/private boundary established; module builds and tests independently
- [x] A future tokenizer can feed a future tree builder through the defined parser boundary
- [x] A future tree builder can construct the tree without redesigning document ownership
- [x] Future DOM has a clear home (`RoboHTMLDom.h` boundary; DOM phase module)
- [x] Future CSS/layout/paint integration has a clean one-way boundary (`RoboHTMLClient.h`)
- [x] Future RoboNetwork integration needs no RoboHTML↔network coupling (`RoboHTMLResource.h`)
- [x] Lifecycle and ownership explicit and testable
- [x] No Chromium dependency; full regression suite green
- [x] Stop after 8.1

## Goal 8.2 — HTML tokenizer

**Status:** complete

**Done:**

- Public API: `RoboHTMLToken.h` (six token kinds, borrowed-view contract,
  positions), `RoboHTMLTokenizer.h` (create / next / reset / at_eof /
  get_state / diagnostics / switch_state / destroy),
  `RoboHTMLTokenizerStates.h` (74-state enumerator, names, raw-content
  classification, switch-entry validation)
- Independent implementation: no Chromium code, data structures or identifiers;
  behavior follows the public WHATWG tokenizer model
- Token kinds: character / start tag / end tag / comment / doctype / EOF, with
  byte offset + 1-based line/column (code-point columns) taken at the token's
  opening byte
- Tags: lowercased ASCII names, attributes (double/single/unquoted, empty,
  boolean, whitespace-tolerant, duplicates preserved in order), self-closing,
  malformed recovery; attribute names lowercased, values reference-decoded
- Comments: normal / empty / abrupt (`<!-->`) / EOF-in-comment recovery /
  bogus comments (`<?`, `<![CDATA[` in HTML content)
- DOCTYPE: case-insensitive keyword, name, public + system identifiers
  (double/single quoted), force-quirks flag, bogus-doctype recovery
- Character references: named (curated catalogue, longest-match by name
  length — order-independent), decimal, hex, legacy semicolonless rules for
  text and attributes, C1 remap, C0 control parse errors that keep the code
  point, out-of-range/surrogate → U+FFFD
- Raw content: `RAWTEXT` / `RCDATA` / script data / `PLAINTEXT` reachable only
  through the controlled switch (tree-builder entry); end-tag name seeded from
  the last start tag or a leading start tag; RCDATA decodes references, only
  the matching end tag closes the region
- Error tolerance: malformed input recovers and continues; bounded
  diagnostics (`ROBO_HTML_TOKENIZER_MAX_DIAGNOSTICS`); unterminated markup at
  EOF emits the EOF token with a diagnostic
- Ownership: tokens borrow tokenizer storage until the next next()/reset()/
  destroy; `robo_html_token_copy_tag_name` / `robo_html_token_copy_attr`
  return caller-owned copies; corrupt/stale usage returns INVALID_ARGUMENT
- Suites: `html.token.*` (22), `html.charref.*` (9), `html.tokstate.*` (9);
  `html.*` total **65** — including prefix sweeps, all-256-byte and
  pseudo-random malformed corpora
- Docs: `html/README.md`, `html/docs/Architecture.md` §9–§10
- RoboHTML **0.2.0** (`ROBO_HTML_API_FROZEN=0`)

**Definition of done:**

- [x] HTML source → deterministic token stream; all six kinds represented
- [x] Attributes, comments, doctypes, character references, EOF and state
      transitions covered by targeted tests
- [x] Malformed HTML never crashes or aborts tokenization
- [x] Token/source ownership explicit, documented and tested
- [x] Tokenizer independent of the tree builder; 8.3 can consume it as-is
- [x] No tree-building behavior; no CSS/layout/paint/JS/network code
- [x] No Chromium dependency; full regression suite green
- [x] Stop after 8.2

## Goal 8.3 — HTML tree builder

**Status:** complete

**Done:**

- `RoboHTMLTreeBuilder.h` — builder create / begin / next / consume / finish /
  build, one-shot `robo_html_build_tree`, introspection (state, insertion
  mode, text mode, current node, open-element stack, result, diagnostics),
  `set_text_mode` hook, and the tree dump (`robo_html_tree_dump` +
  `_to_buffer`)
- Real nodes from the 8.2 stream: `Document`, `DocumentType`, `Element`,
  `Text`, `Comment` — created only through document-scoped registration, so
  there is still exactly one ownership model
- Node payload arena (`RoboHTMLPayload.c`): one definition of the byte layout
  for element name + attribute pairs (u32 lengths, NUL-terminated strings,
  duplicates preserved in source order), text/comment bytes, and doctype
  identifiers; adjacent character tokens merge in place when the previous
  text payload still ends the arena
- Open-element stack (heap, geometric growth) with `push` / `pop` / `current` /
  lookup / scope-based closing; tree links stored as node ids, never registry
  pointers
- Insertion modes: `initial`, `before-html`, `before-head`, `in-head`,
  `after-head`, `in-body`, `after-body`, `after-after-body`, plus `text` for
  RCDATA / RAWTEXT / script data / plaintext; implied `html`/`head`/`body`;
  guarded re-dispatch so no token can loop
- In-body rules: headings close headings and open `p`; block start tags close
  open `p`; nested `<button>` closes the outer one; `pre`/`listing`/`textarea`
  swallow one leading LF; `</br>` re-runs as `<br>`; end tags pop to their
  matching element; void elements never swallow following content
- Recovery: everything malformed is a bounded diagnostic, never fatal; the
  only hard stop is the node ceiling, which truncates, still reports
  `ROBO_OK`, and reports `result.truncated`
- Identity safety: re-building a document clears the tree but keeps node ids
  and generations moving forward, so stale handles can never alias new nodes
- Tree dump: indented, quoted/escaped, attribute-aware, elides deep subtrees
  and truncates long text; bounded iteration so a corrupted link terminates
  instead of hanging
- Phase 8.3 capability flip: `robo_html_dom_query_capabilities().tree_building = 1`
- Suites: `html.tree.*` (34) — structure, implied elements, nesting, siblings,
  text merging, comments, doctype, attributes, void/self-closing, tag
  matching, unexpected end tags, unclosed-at-EOF, duplicate structural tags,
  RCDATA, div/span/button, a/img/form/input, open-element stack, insertion
  modes, ownership/stale handles, teardown, malformed recovery, 200-deep tree,
  node-ceiling truncation, Unicode, dump, tokenizer integration, diagnostics,
  demo document, whitespace preservation
- Demo rewired: `RoboDemo` now walks the real document tree (no token
  flattening); the visual result is unchanged in the host window and in QEMU
  (`ROBO_QEMU_HTML_OK`, `PRESENT_OK`, `INPUT_OK`, `SHUTDOWN_OK`, blocks=4,
  supported=8, ignored=0)
- Docs: `html/docs/TreeBuilder.md`, `html/README.md`,
  `html/docs/Architecture.md` §11, `demo/README.md`,
  `demo/docs/VisualCheckpoint.md`
- RoboHTML **0.3.0** (`ROBO_HTML_API_FROZEN=0`)

**Definition of done:**

- [x] Tokenizer produces tokens; the tree builder consumes them
- [x] A real document tree is constructed with correct parent/child links
- [x] Open-element state is maintained safely and unwinds at EOF
- [x] Basic malformed HTML recovers safely and is reported
- [x] Node ownership, teardown and stale-identity behaviour are tested
- [x] The visual demo traverses the real tree and still renders on the host
      and in QEMU
- [x] Existing Phase 6/7/8.1/8.2 tests still pass (1094/1094, Debug+Release)
- [x] No Chromium dependency; independence scans green
- [x] Stop after 8.3

## Next

- Goal 8.4 — HTML document/DOM boundary work (do not start until requested)

